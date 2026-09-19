#include "LLDBDebugger.h"
#include "DebugManager.h"
#include "Compiler/BfUtil.h"

#ifdef LLDB_ENABLED

#ifdef BF_PLATFORM_WINDOWS
#pragma comment(lib, "liblldb.lib")
#endif

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <elf.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif

USING_NS_BF;

// Hot swap layout
static const uint64 HOT_HEAP_RESERVE_SIZE = 64 * 1024 * 1024;
static const uint64 HOT_HEAP_EXE_GAP = 256 * 1024 * 1024; // Room for brk heap growth past the executable
static const int HOT_JMP_REL32_SIZE = 5;
static const int HOT_JMP_ABS64_SIZE = 14;
static const int HOT_STUB_SIZE = 16;

// Set BEEF_LLDB_LOG in the environment to enable
void LLDBLog(const char* fmt ...)
{
	static int sEnabled = -1;
	if (sEnabled == -1)
		sEnabled = (getenv("BEEF_LLDB_LOG") != NULL) ? 1 : 0;
	if (sEnabled == 0)
		return;

	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

	OutputDebugStr("LLDB: ");
	OutputDebugStr(aResult);
}

// Populate exception address/code/description from a stopped thread.
static void CollectExceptionInfo(lldb::SBThread& thread, lldb::SBProcess& process,
	uint64& outAddress, uint32& outCode, String& outDescription)
{
	lldb::SBFrame frame = thread.GetFrameAtIndex(0);
	outAddress = frame.IsValid() ? (uint64)frame.GetPC() : 0;

	lldb::StopReason reason = thread.GetStopReason();
	if (reason == lldb::eStopReasonSignal)
	{
		outCode = (uint32)thread.GetStopReasonDataAtIndex(0);
		lldb::SBUnixSignals signals = process.GetUnixSignals();
		const char* sigName = signals.GetSignalAsCString((int32_t)outCode);
		if (sigName != NULL)
			outDescription = StrFormat("%s in thread %d", sigName, (int)thread.GetThreadID());
		else
			outDescription = StrFormat("Signal %d in thread %d", (int)outCode, (int)thread.GetThreadID());
	}
	else if (reason == lldb::eStopReasonException)
	{
		outCode = (uint32)thread.GetStopReasonDataAtIndex(0);
		char descBuf[256] = {};
		thread.GetStopDescription(descBuf, sizeof(descBuf));
		if (descBuf[0] != '\0')
			outDescription = descBuf;
		else
			outDescription = StrFormat("Exception in thread %d", (int)thread.GetThreadID());
	}
	else
	{
		outCode = 0;
		char descBuf[256] = {};
		thread.GetStopDescription(descBuf, sizeof(descBuf));
		outDescription = (descBuf[0] != '\0') ? String(descBuf) : String("Crash");
	}
}

// Return true if the signal number corresponds to a fatal/crash signal.
static bool IsCrashSignal(uint32 signo)
{
	// SIGILL=4, SIGABRT=6, SIGBUS=7(Linux)/10(macOS), SIGFPE=8, SIGSEGV=11
	return ((signo == 4) || (signo == 6) || (signo == 7) ||
	        (signo == 8) || (signo == 10) || (signo == 11));
}

// Translate an LLDB function name to IDE display form.
// Beef functions are emitted as "bf::Namespace::Type.Method" — strip the
// leading "bf::" runtime prefix and replace remaining "::" with ".".
static String FixBeefFunctionName(const char* name)
{
	if ((name == NULL) || (name[0] == '\0'))
		return String();

	const char* src = name;

	// Strip "bf::" prefix
	if ((src[0] == 'b') && (src[1] == 'f') && (src[2] == ':') && (src[3] == ':'))
		src += 4;

	// Replace "::" with "."
	String result;
	while (*src != '\0')
	{
		if ((src[0] == ':') && (src[1] == ':'))
		{
			result += '.';
			src += 2;
		}
		else
		{
			result += *src;
			++src;
		}
	}
	return result;
}

//----------------------------------------------------------------------------
// Constructor / destructor
//----------------------------------------------------------------------------

LLDBDebugger::LLDBDebugger(DebugManager* debugManager)
{
	mDebugManager = debugManager;
	mProcessId = 0;
	mActiveBreakpoint = NULL;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mCallStackDirty = false;
	mDidAttach = false;
	mNeedBreakpointRebind = false;
	mAutoStepRemaining = 0;
	mStepKind = StepKind_None;
	mStepOutThenInto = false;
	mStepOutFinishedLine = false;
	mStepContinueCount = 0;
	mStepStartFunctionAddr = 0;
	mExceptionAddress = 0;
	mExceptionCode = 0;
	mHotSwapEnabled = false;
	mOpenFileFlags = DbgOpenFileFlag_None;
	mLaunchMode = LLDBLaunchMode_Local;
	mUseHardwareBreakpoints = false;
	mLaunchThread = NULL;
	mStdOutPipeWrite = -1;
	mStdErrPipeWrite = -1;
	mStdOutPipeRead = NULL;
	mStdErrPipeRead = NULL;
	HotResetState();
}

LLDBDebugger::~LLDBDebugger()
{
	WaitForLaunchThread();
	CloseOutputPipes();
	HotRemoveDebugInfo();
	for (auto bp : mBreakpoints)
		delete bp;
}

void LLDBDebugger::DumpSymbolAddrs(const StringImpl& sym)
{
	lldb::SBSymbolContextList symList = mLLDBTarget.FindSymbols(sym.c_str());
	uint32 numSymbols = symList.GetSize();
	LLDBLog("FindSymbols('%s'): %d result(s)\n", sym.c_str(), (int)numSymbols);
	for (uint32 i = 0; i < numSymbols; i++)
	{
		lldb::SBSymbolContext ctx = symList.GetContextAtIndex(i);
		lldb::SBSymbol symbol = ctx.GetSymbol();
		lldb::SBModule module = ctx.GetModule();

		const char* symNameStr = symbol.IsValid() ? symbol.GetName() : "(invalid)";
		const char* modNameStr = "(none)";
		if (module.IsValid())
		{
			const char* fn = module.GetFileSpec().GetFilename();
			if ((fn != NULL) && (fn[0] != '\0'))
				modNameStr = fn;
		}

		lldb::addr_t loadAddr = LLDB_INVALID_ADDRESS;
		if (symbol.IsValid())
		{
			lldb::SBAddress startAddr = symbol.GetStartAddress();
			if (startAddr.IsValid())
				loadAddr = startAddr.GetLoadAddress(mLLDBTarget);
		}

		if (loadAddr != LLDB_INVALID_ADDRESS)
			LLDBLog("  [%d] module='%s' sym='%s' addr=0x%llX\n", (int)i, modNameStr, symNameStr, (uint64)loadAddr);
		else
			LLDBLog("  [%d] module='%s' sym='%s' addr=(unresolved)\n", (int)i, modNameStr, symNameStr);
	}
}

void LLDBDebugger::DoCreateBreakpointByName(LLDBBreakpoint* bp)
{
	// A leading "-" means the breakpoint should only bind within the main
	// executable module (not any loaded library).  Strip the prefix and
	// restrict via a module list containing just the target executable.
	const char* sym = bp->mSymbolName.c_str();
	bool mainModuleOnly = (sym[0] == '-');
	if (mainModuleOnly)
		sym++;

	DumpSymbolAddrs(sym);

	if (mainModuleOnly)
	{
		lldb::SBFileSpecList moduleList;
		moduleList.Append(mLLDBTarget.GetExecutable());
		if (moduleList.GetSize() > 0)
		{
			lldb::SBFileSpecList compUnitList;  // empty — match all compile units
			bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByName(
				sym, lldb::eFunctionNameTypeAuto, moduleList, compUnitList);
		}
		else
			bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByName(sym);
	}
	else
	{
		bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByName(sym);
	}
}

//----------------------------------------------------------------------------
// Output helpers
//----------------------------------------------------------------------------

void LLDBDebugger::OutputMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back("msg " + msg);
}

void LLDBDebugger::OutputRawMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back(msg);
}

//----------------------------------------------------------------------------
// Identity / capabilities
//----------------------------------------------------------------------------

int LLDBDebugger::GetAddrSize()
{
	return sizeof(addr_target);
}

bool LLDBDebugger::CanOpen(const StringImpl& fileName, DebuggerResult* outResult)
{
	return true;
}

//----------------------------------------------------------------------------
// Launch / attach
//----------------------------------------------------------------------------

void LLDBDebugger::OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled, DbgOpenFileFlags openFileFlags)
{
	LLDBLog("OpenFile\n");

	mLaunchMode = LLDBLaunchMode_Local;
	mRemoteHost = "";
	mUseHardwareBreakpoints = false;

	const char* rawPath = launchPath.c_str();
	const char* atSign = strchr(rawPath, '@');
	if (atSign != NULL)
	{
		const char* location = atSign + 1;

		if (strncmp(location, "lldb_hw:", 8) == 0)
		{
			mRemoteHost = location + 8;
			mLaunchMode = LLDBLaunchMode_Remote;
			mUseHardwareBreakpoints = true;
		}
		else if (strncmp(location, "lldb:", 5) == 0)
		{
			mRemoteHost = location + 5;
			mLaunchMode = LLDBLaunchMode_Remote;
		}

		mLaunchPath = String(rawPath, (int)(atSign - rawPath));
	}
	else
	{
		mLaunchPath = launchPath;
	}
	
	mLaunchArgs = args;
	mWorkingDir = workingDir;
	mEnvBlock = envBlock;
	mHotSwapEnabled = hotSwapEnabled;
	mOpenFileFlags = openFileFlags;
	HotResetState();
	CreateOutputPipes();
}

void LLDBDebugger::DoLaunch()
{
	LLDBLog("DoLaunch\n");

#ifdef __linux__
	// Distribution LLDB packages can fail to find their own lldb-server (Ubuntu's looks for
	// a fully versioned 'lldb-server-22.x.y'), which makes every local launch fail.
	if (getenv("LLDB_DEBUGSERVER_PATH") == NULL)
	{
		const char* serverPaths[] = { "/usr/lib/llvm-22/bin/lldb-server", "/usr/bin/lldb-server-22", "/usr/bin/lldb-server" };
		for (auto serverPath : serverPaths)
		{
			if (access(serverPath, X_OK) == 0)
			{
				setenv("LLDB_DEBUGSERVER_PATH", serverPath, 1);
				break;
			}
		}
	}
#endif

	lldb::SBDebugger::Initialize();

	lldb::SBDebugger debugger = lldb::SBDebugger::Create(/*source_init_files=*/false);
	debugger.SetAsync(true);


	if (mLaunchMode == LLDBLaunchMode_Remote)
	{
		//ELF path is optional
		lldb::SBError targetError;
		lldb::SBTarget target = debugger.CreateTarget(
			mLaunchPath.IsEmpty() ? "" : mLaunchPath.c_str(),
			NULL, NULL, /*add_dependent_modules=*/true, targetError);

		if (mUseHardwareBreakpoints)
		{
			// Force all breakpoints to be set as hardware breakpoints.
			lldb::SBCommandReturnObject res;
			debugger.GetCommandInterpreter().HandleCommand(
				"settings set target.require-hardware-breakpoint true", res);
		}

		// "connect://" with the "gdb-remote" plugin speaks GDB Remote Serial Protocol.
		lldb::SBListener listener = debugger.GetListener();
		String connectUrl = StrFormat("connect://%s", mRemoteHost.c_str());
		lldb::SBError connErr;
		lldb::SBProcess process = target.ConnectRemote(
			listener, connectUrl.c_str(), "gdb-remote", connErr);

		if ((!process.IsValid()) || connErr.Fail())
		{
			String msg = "LLDB: Failed to connect to '";
			msg += mRemoteHost;
			msg += "'";
			if (connErr.IsValid())
			{
				msg += ": ";
				msg += connErr.GetCString();
			}
			msg += "\n";
			OutputMessage(msg);
			lldb::SBDebugger::Destroy(debugger);
			lldb::SBDebugger::Terminate();
			AutoCrit autoCrit(mDebugManager->mCritSect);
			mRunState = RunState_Terminated;
			return;
		}

		AutoCrit autoCrit(mDebugManager->mCritSect);
		mLLDBDebugger = debugger;
		mLLDBTarget = target;
		mLLDBProcess = process;
		mProcessId = (int)process.GetProcessID();
		mNeedBreakpointRebind = true;
		mRunState = RunState_Running;
		return;
	}

	// Create a target from the executable path.
	lldb::SBError targetError;
	lldb::SBTarget target = debugger.CreateTarget(mLaunchPath.c_str(), NULL, NULL,
		/*add_dependent_modules=*/true, targetError);
	if (!target.IsValid())
	{
		String msg = "LLDB: Failed to create target for '";
		msg += mLaunchPath;
		msg += "'";
		if (targetError.IsValid())
		{
			msg += ": ";
			msg += targetError.GetCString();
		}
		msg += "\n";
		OutputMessage(msg);
		lldb::SBDebugger::Destroy(debugger);
		lldb::SBDebugger::Terminate();
		AutoCrit autoCrit(mDebugManager->mCritSect);
		mRunState = RunState_Terminated;
		return;
	}

	// Parse the args string into a vector of strings.
	Array<String> argStrings;
	Array<const char*> argv;
	{
		const char* p = mLaunchArgs.c_str();
		while (*p != '\0')
		{
			while (*p == ' ')
				++p;
			if (*p == '\0')
				break;
			const char* start = p;
			while ((*p != '\0') && (*p != ' '))
				++p;
			argStrings.push_back(String(start, (int)(p - start)));
		}
		for (auto& s : argStrings)
			argv.push_back(s.c_str());
		argv.push_back(NULL);
	}

	// Parse the env block (null-terminated KEY=VALUE strings, double-null terminated).
	Array<String> envStrings;
	Array<const char*> envp;
	{
		if (!mEnvBlock.IsEmpty())
		{
			const uint8* p = &mEnvBlock.front();
			const uint8* end = p + mEnvBlock.size();
			while ((p < end) && (*p != '\0'))
			{
				const uint8* start = p;
				while ((p < end) && (*p != '\0'))
					++p;
				envStrings.push_back(String((const char*)start, (int)(p - start)));
				if (p < end)
					++p;
			}
		}
		for (auto& s : envStrings)
			envp.push_back(s.c_str());
		envp.push_back(NULL);
	}

	// Launch the process stopped at entry so the IDE can set up before running.
	lldb::SBError launchError;
	lldb::SBListener listener = debugger.GetListener();
	lldb::SBProcess process = target.Launch(
		listener,
		argv.size() > 1 ? &argv.front() : NULL,
		envp.size() > 1 ? &envp.front() : NULL,
		NULL, // stdin
		NULL, // stdout
		NULL, // stderr
		mWorkingDir.IsEmpty() ? NULL : mWorkingDir.c_str(),
		0,    // launch flags
		true, // stop at entry
		launchError);

	if ((!process.IsValid()) || launchError.Fail())
	{
		String msg = "LLDB: Failed to launch '";
		msg += mLaunchPath;
		msg += "'";
		if (launchError.IsValid())
		{
			msg += ": ";
			msg += launchError.GetCString();
		}
		msg += "\n";
		OutputMessage(msg);
		lldb::SBDebugger::Destroy(debugger);
		lldb::SBDebugger::Terminate();
		AutoCrit autoCrit(mDebugManager->mCritSect);
		mRunState = RunState_Terminated;
		return;
	}

	// Publish to the shared state under the lock so Update() sees a consistent view.
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mLLDBDebugger = debugger;
	mLLDBTarget = target;
	mLLDBProcess = process;
	mProcessId = (int)process.GetProcessID();
	mNeedBreakpointRebind = true;
	mRunState = RunState_Running;
}

void BFP_CALLTYPE LLDBDebugger::LaunchThreadProc(void* param)
{
	((LLDBDebugger*)param)->DoLaunch();
}

bool LLDBDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	mDidAttach = true;
	return false;
}

void LLDBDebugger::GetStdHandles(BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
	// Ownership of the read ends passes to the caller, so each is handed out only once
	if (outStdIn != NULL)
		*outStdIn = NULL;
	if (outStdOut != NULL)
	{
		*outStdOut = mStdOutPipeRead;
		mStdOutPipeRead = NULL;
	}
	if (outStdErr != NULL)
	{
		*outStdErr = mStdErrPipeRead;
		mStdErrPipeRead = NULL;
	}
}

//----------------------------------------------------------------------------
// Target stdio
//----------------------------------------------------------------------------

void LLDBDebugger::CreateOutputPipes()
{
	CloseOutputPipes();

#ifdef __linux__
	auto _CreatePipe = [&](int& outWriteFd, BfpFile*& outReadFile)
	{
		static int sPipeIdx = 0;
		String path = StrFormat("/tmp/BeefLLDB_%d_%d", (int)getpid(), sPipeIdx++);
		unlink(path.c_str());
		if (mkfifo(path.c_str(), 0600) != 0)
			return;

		// Opening a FIFO read/write doesn't block on Linux, and gives the read end a writer to open against
		int writeFd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
		BfpFile* readFile = NULL;
		if (writeFd != -1)
		{
			BfpFileResult result;
			readFile = BfpFile_Create(path.c_str(), BfpFileCreateKind_OpenExisting, BfpFileCreateFlag_Read, BfpFileAttribute_None, &result);
		}
		unlink(path.c_str());

		if (readFile == NULL)
		{
			if (writeFd != -1)
				close(writeFd);
			return;
		}
		outWriteFd = writeFd;
		outReadFile = readFile;
	};

	if ((mOpenFileFlags & DbgOpenFileFlag_RedirectStdOutput) != 0)
		_CreatePipe(mStdOutPipeWrite, mStdOutPipeRead);
	if ((mOpenFileFlags & DbgOpenFileFlag_RedirectStdError) != 0)
		_CreatePipe(mStdErrPipeWrite, mStdErrPipeRead);
#endif
}

void LLDBDebugger::CloseOutputPipes()
{
#ifdef __linux__
	// Closing the write ends lets the IDE's reader threads see EOF
	if (mStdOutPipeWrite != -1)
	{
		close(mStdOutPipeWrite);
		mStdOutPipeWrite = -1;
	}
	if (mStdErrPipeWrite != -1)
	{
		close(mStdErrPipeWrite);
		mStdErrPipeWrite = -1;
	}
#endif
	if (mStdOutPipeRead != NULL)
	{
		BfpFile_Release(mStdOutPipeRead);
		mStdOutPipeRead = NULL;
	}
	if (mStdErrPipeRead != NULL)
	{
		BfpFile_Release(mStdErrPipeRead);
		mStdErrPipeRead = NULL;
	}
	mStdOutPending.Clear();
	mStdErrPending.Clear();
}

// LLDB launches the target on a pty and buffers what it writes; forward that to
// the IDE's pipes, or to our own console when output isn't being redirected.
void LLDBDebugger::PumpTargetOutput()
{
#ifdef __linux__
	const intptr maxPending = 8 * 1024 * 1024;

	auto _Forward = [&](const char* data, size_t len, int pipeFd, String& pending, int localFd)
	{
		if (pipeFd == -1)
		{
			while (len > 0)
			{
				ssize_t written = write(localFd, data, len);
				if (written <= 0)
					break;
				data += written;
				len -= (size_t)written;
			}
			return;
		}
		// Drop output rather than grow without bound if nothing is reading the pipe
		if (pending.length() + (intptr)len <= maxPending)
			pending.Append(data, (intptr)len);
	};

	auto _Flush = [&](int pipeFd, String& pending)
	{
		if ((pipeFd == -1) || (pending.IsEmpty()))
			return;
		ssize_t written = write(pipeFd, pending.c_str(), (size_t)pending.length());
		if (written > 0)
			pending.Remove(0, (intptr)written);
	};

	if (mLLDBProcess.IsValid())
	{
		char buf[4096];
		size_t len;
		while ((len = mLLDBProcess.GetSTDOUT(buf, sizeof(buf))) > 0)
			_Forward(buf, len, mStdOutPipeWrite, mStdOutPending, STDOUT_FILENO);
		while ((len = mLLDBProcess.GetSTDERR(buf, sizeof(buf))) > 0)
			_Forward(buf, len, mStdErrPipeWrite, mStdErrPending, STDERR_FILENO);
	}

	_Flush(mStdOutPipeWrite, mStdOutPending);
	_Flush(mStdErrPipeWrite, mStdErrPending);
#endif
}

void LLDBDebugger::WaitForLaunchThread()
{
	if (mLaunchThread != NULL)
	{
		BfpThread_WaitFor(mLaunchThread, -1);
		BfpThread_Release(mLaunchThread);
		mLaunchThread = NULL;
	}
}

void LLDBDebugger::Run()
{
	// Kick off the background launch thread if OpenFile has stored params for us.
	if (!mLaunchPath.IsEmpty())
		mLaunchThread = BfpThread_Create(LaunchThreadProc, (void*)this, 128 * 1024, BfpThreadCreateFlag_StackSizeReserve);
}

//----------------------------------------------------------------------------
// Update — poll process state each IDE tick
//----------------------------------------------------------------------------

void LLDBDebugger::HandleProcessEvent(lldb::StateType state)
{
	// Process exited or was detached
	if ((state == lldb::eStateExited) || (state == lldb::eStateDetached))
	{
		if (mRunState != RunState_Terminated)
		{
			ClearCallStack();
			mActiveBreakpoint = NULL;
			mProcessId = 0;
			mRunState = RunState_Terminated;
		}
		return;
	}

	// Process crashed — treat like an exception
	if (state == lldb::eStateCrashed)
	{
		if ((mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint))
		{
			ClearCallStack();
			mRequestedStackFrameIdx = 0;
			mBreakStackFrameIdx = 0;
			mActiveBreakpoint = NULL;

			lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
			if ((!thread.IsValid()) && (mLLDBProcess.GetNumThreads() > 0))
				thread = mLLDBProcess.GetThreadAtIndex(0);
			if (thread.IsValid())
				CollectExceptionInfo(thread, mLLDBProcess, mExceptionAddress, mExceptionCode, mExceptionDescription);
			else
			{
				mExceptionAddress = 0;
				mExceptionCode = 0;
				mExceptionDescription = "Crash";
			}

			mRunState = RunState_Exception;
		}
		return;
	}

	// Process stopped (breakpoint, step complete, user interrupt, etc.)
	if (state == lldb::eStateStopped)
	{
		// Stale event - the target has been resumed since it was queued. Interrupting
		// the target (as HotLoad does) can deliver a duplicate stop event after we continue.
		if (mLLDBProcess.GetState() != lldb::eStateStopped)
			return;

		if ((mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint))
		{
			// On the first stop after launch (stop-at-entry), the process image is
			// fully loaded and symbols are resolved.  Walk every breakpoint and call
			// CheckBreakpoint so their load addresses are populated and they appear
			// as bound in the IDE.
			if (mNeedBreakpointRebind)
			{
				mNeedBreakpointRebind = false;
				for (auto bp : mBreakpoints)
					CheckBreakpoint(bp);
			}

			ClearCallStack();
			mRequestedStackFrameIdx = 0;
			mBreakStackFrameIdx = 0;

			// Thread info is fully populated once we have consumed the stop event
			lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
			if ((!thread.IsValid()) && (mLLDBProcess.GetNumThreads() > 0))
				thread = mLLDBProcess.GetThreadAtIndex(0);

			auto threadStopReason = thread.IsValid() ? thread.GetStopReason() : lldb::eStopReasonNone;

			// The old version of a lambda whose captures changed incompatibly was called
			if ((thread.IsValid()) && (threadStopReason == lldb::eStopReasonBreakpoint) &&
				(mHotInvalidLambdaTrapIds.Contains((int)thread.GetStopReasonDataAtIndex(0))))
			{
				int trapId = (int)thread.GetStopReasonDataAtIndex(0);
				mHotInvalidLambdaTrapIds.Remove(trapId);
				HotClearStepTraps();
				mStepKind = StepKind_None;
				mActiveBreakpoint = NULL;
				mRunState = RunState_Paused;
				mDebugManager->mOutMessages.push_back("error This lambda was replaced by a new version that has incompatible captures. A program restart is required.");
				return;
			}

			// A step-in that reached the new version of a hot-replaced method through a step trap: finish
			// the step there, past its prologue
			if (!mHotStepTrapIds.IsEmpty())
			{
				bool hitTrap = false;
				if ((thread.IsValid()) && (threadStopReason == lldb::eStopReasonBreakpoint))
					hitTrap = mHotStepTrapIds.Contains((int)thread.GetStopReasonDataAtIndex(0));
				HotClearStepTraps();
				if (hitTrap)
				{
					lldb::SBFrame frame = thread.GetFrameAtIndex(0);
					lldb::SBFunction function = frame.GetFunction();
					uint32 prologueSize = function.IsValid() ? function.GetPrologueByteSize() : 0;
					LLDBLog("Hit hot step trap at %llx\n", (unsigned long long)frame.GetPC());
					if (prologueSize > 0)
					{
						lldb::SBError error;
						thread.RunToAddress(frame.GetPC() + prologueSize, error);
						if (error.Success())
						{
							mRunState = RunState_Running;
							return;
						}
					}
					mActiveBreakpoint = NULL;
					mRunState = RunState_Paused;
					return;
				}
			}

			// A step into a hot-replaced method stops on our jump to the new version: at the entry, or at the
			// end of the prologue when the jump was placed there (see HotGetPatchLayout). Take the jump, then
			// run on to the end of the new version's prologue, as a normal step-in would.
			if ((thread.IsValid()) && ((threadStopReason == lldb::eStopReasonPlanComplete) || (threadStopReason == lldb::eStopReasonTrace)))
			{
				lldb::SBFrame frame = thread.GetFrameAtIndex(0);
				uint64 pc = frame.IsValid() ? (uint64)frame.GetPC() : 0;
				uint64 entryAddr = 0;
				HotPatchedEntry patchedEntry;
				if ((pc != 0) && (HotIsInPatchedEntry(pc, &entryAddr, &patchedEntry)) && ((pc == entryAddr) || (pc == patchedEntry.mJmpAddr)))
				{
					LLDBLog("Following hot jump %llx -> %llx\n", (unsigned long long)pc, (unsigned long long)patchedEntry.mNewAddr);
					frame.SetPC(patchedEntry.mNewAddr);
					frame = thread.GetFrameAtIndex(0);
					pc = patchedEntry.mNewAddr;
				}

				// Only a step in lands at a method's start - other steps can report completing there too (e.g.
				// a step out started at a breakpoint on the entry), and must not be moved
				lldb::SBFunction function = frame.GetFunction();
				if ((mStepKind == StepKind_Into) && (function.IsValid()) && (pc == (uint64)function.GetStartAddress().GetLoadAddress(mLLDBTarget)) &&
					(function.GetPrologueByteSize() > 0))
				{
					lldb::SBError error;
					thread.RunToAddress(pc + function.GetPrologueByteSize(), error);
					if (error.Success())
					{
						mRunState = RunState_Running;
						return;
					}
				}
			}

			// Don't stop a step somewhere the user shouldn't be (as WinDebugger does): compiler-generated
			// methods with no statement lines, like delegate Invoke methods, or methods excluded by the IDE's
			// step filters.
			if ((thread.IsValid()) && (threadStopReason == lldb::eStopReasonPlanComplete) && (mAutoStepRemaining == 0) && (ContinueStep(thread)))
			{
				mRunState = RunState_Running;
				return;
			}
			if ((threadStopReason != lldb::eStopReasonPlanComplete) || (mAutoStepRemaining == 0))
				mStepKind = StepKind_None;

			// Execute the next queued auto-step when a planned step has completed.
			// A breakpoint, signal, or any other non-plan-complete stop cancels the
			// sequence so the user sees the real event rather than stepping past it.
			if (mAutoStepRemaining > 0)
			{
				if ((thread.IsValid()) && (threadStopReason == lldb::eStopReasonPlanComplete))
				{
					if (mAutoStepRemaining == 2)
						thread.StepInto();
					else  // mAutoStepRemaining == 1
						thread.StepOver();
					mAutoStepRemaining--;
					mRunState = RunState_Running;
					return;
				}
				mAutoStepRemaining = 0;  // Unexpected stop — cancel the sequence
			}

			if ((thread.IsValid()) && (threadStopReason == lldb::eStopReasonBreakpoint))
			{
				// Identify which of our breakpoints was hit via its LLDB ID
				lldb::break_id_t bpId = (lldb::break_id_t)thread.GetStopReasonDataAtIndex(0);
				LLDBBreakpoint* bp = NULL;
				mBreakpointIdMap.TryGetValue((int)bpId, &bp);
				if (bp != NULL)
				{
					Breakpoint* head = (bp->mHead != NULL) ? bp->mHead : bp;
					head->mHitCount++;
				}
				mActiveBreakpoint = bp;

				// Record the resolved load address from the PC if not yet known
				if ((bp != NULL) && (bp->mResolvedAddr == 0))
				{
					lldb::SBFrame frame = thread.GetFrameAtIndex(0);
					if (frame.IsValid())
					{
						bp->mResolvedAddr = (uintptr)frame.GetPC();
						mBreakpointAddrMap.ForceAdd(bp->mResolvedAddr, bp);
					}
				}

				mRunState = RunState_Breakpoint;
			}
			else if ((thread.IsValid()) &&
			         ((threadStopReason == lldb::eStopReasonSignal) || (threadStopReason == lldb::eStopReasonException)) &&
			         ((threadStopReason == lldb::eStopReasonException) ||
			          IsCrashSignal((uint32)thread.GetStopReasonDataAtIndex(0))))
			{
				// Fatal signal or hardware exception — treat as crash
				CollectExceptionInfo(thread, mLLDBProcess, mExceptionAddress, mExceptionCode, mExceptionDescription);
				mActiveBreakpoint = NULL;
				mRunState = RunState_Exception;
			}
			else
			{
				mActiveBreakpoint = NULL;
				mRunState = RunState_Paused;
			}
		}
	}
}

void LLDBDebugger::Update()
{
	if (!mLLDBProcess.IsValid())
		return;
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated))
		return;

	// Drain all pending process events (non-blocking).  Thread info is only
	// reliably populated once the event has been consumed from the queue.
	lldb::SBListener listener = mLLDBDebugger.GetListener();
	lldb::SBEvent event;
	while (listener.GetNextEvent(event))
	{
		if (!lldb::SBProcess::EventIsProcessEvent(event))
			continue;
		lldb::StateType state = lldb::SBProcess::GetStateFromEvent(event);
		LLDBLog("Update got event state:%d\n", state);
		HandleProcessEvent(state);
	}

	// After handling events, so output written just before the target exits isn't lost
	PumpTargetOutput();
	if (mRunState == RunState_Terminated)
		CloseOutputPipes();
}

//----------------------------------------------------------------------------
// Execution control
//----------------------------------------------------------------------------

void LLDBDebugger::ContinueDebugEvent()
{
	if (!mLLDBProcess.IsValid())
		return;
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) && (mRunState != RunState_Exception))
		return;

	LLDBLog("ContinueDebugEvent\n");

	mAutoStepRemaining = 0;
	mStepKind = StepKind_None;
	ClearCallStack();
	mActiveBreakpoint = NULL;
	mRunState = RunState_Running;
	mLLDBProcess.Continue();
}

bool LLDBDebugger::TryRunContinue()
{
	return ((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint));
}

void LLDBDebugger::BreakAll()
{
	if ((mLLDBProcess.IsValid()) && (mRunState == RunState_Running))
		mLLDBProcess.Stop();
}

void LLDBDebugger::ForegroundTarget(int altProcessId)
{
}

void LLDBDebugger::StepInto(bool inAssembly)
{
	if (!mLLDBProcess.IsValid())
		return;
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if (thread.IsValid())
	{
		// Any explicit user step resets the auto-step sequence.
		mAutoStepRemaining = 0;

		// When stepping into source code while at the stop-at-entry landing pad
		// inside BeefStartProgram, queue two additional automatic steps so the
		// user lands at the first line of their Program.Main rather than deep
		// inside the Beef runtime bootstrap.
		if (!inAssembly && !mCallStack.IsEmpty())
		{
			const char* funcName = mCallStack[0].GetFunctionName();
			if ((funcName != NULL) && (strstr(funcName, "BeefStartProgram") != NULL))
				mAutoStepRemaining = 2;  // on next stop: StepInto, then StepOver
		}

		BeginStep(thread, inAssembly ? StepKind_None : StepKind_Into);
		ClearCallStack();
		mRunState = RunState_Running;
		if (inAssembly)
			thread.StepInstruction(/*step_over=*/false);
		else
		{
			HotSetStepTraps();
			thread.StepInto();
		}
	}
}

void LLDBDebugger::StepIntoSpecific(intptr addr)
{
}

void LLDBDebugger::StepOver(bool inAssembly)
{
	if (!mLLDBProcess.IsValid())
		return;
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if (thread.IsValid())
	{
		mAutoStepRemaining = 0;
		BeginStep(thread, inAssembly ? StepKind_None : StepKind_Over);
		ClearCallStack();
		mRunState = RunState_Running;
		if (inAssembly)
			thread.StepInstruction(/*step_over=*/true);
		else
			thread.StepOver();
	}
}

void LLDBDebugger::StepOut(bool inAssembly)
{
	if (!mLLDBProcess.IsValid())
		return;
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if (thread.IsValid())
	{
		mAutoStepRemaining = 0;
		BeginStep(thread, inAssembly ? StepKind_None : StepKind_Out);
		ClearCallStack();
		mRunState = RunState_Running;
		thread.StepOut();
	}
}

void LLDBDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
}

//----------------------------------------------------------------------------
// Step filtering
//----------------------------------------------------------------------------

void LLDBDebugger::BeginStep(lldb::SBThread& thread, StepKind stepKind)
{
	mStepKind = stepKind;
	mStepOutThenInto = false;
	mStepOutFinishedLine = false;
	mStepContinueCount = 0;
	lldb::SBFunction function = thread.GetFrameAtIndex(0).GetFunction();
	mStepStartFunctionAddr = function.IsValid() ? (uint64)function.GetStartAddress().GetLoadAddress(mLLDBTarget) : 0;
}

// Whether a method has any statement lines. Beef gives compiler-generated code (like delegate Invoke
// methods) only lines without columns; user code's statements have columns.
bool LLDBDebugger::FunctionHasStatementLines(lldb::SBFunction& function)
{
	uint64 start = (uint64)function.GetStartAddress().GetFileAddress();
	uint64 end = (uint64)function.GetEndAddress().GetFileAddress();
	String cacheKey = StrFormat("%s:%llx", function.GetStartAddress().GetModule().GetFileSpec().GetFilename(), (unsigned long long)start);
	bool* cached = NULL;
	if (mHasStatementLinesCache.TryGetValue(cacheKey, &cached))
		return *cached;

	bool hasLines = false;
	lldb::SBCompileUnit compileUnit = function.GetStartAddress().GetCompileUnit();
	for (uint32 lineIdx = 0; lineIdx < compileUnit.GetNumLineEntries(); lineIdx++)
	{
		lldb::SBLineEntry lineEntry = compileUnit.GetLineEntryAtIndex(lineIdx);
		uint64 addr = (uint64)lineEntry.GetStartAddress().GetFileAddress();
		if ((addr >= start) && (addr < end) && (lineEntry.GetLine() > 0) && (lineEntry.GetColumn() > 0))
		{
			hasLines = true;
			break;
		}
	}
	mHasStatementLinesCache[cacheKey] = hasLines;
	return hasLines;
}

// The IDE's step filter name for a method: "Namespace.Type.Method", without params, and with generic
// arguments dropped so all instances share a filter
static String GetStepFilterName(const char* functionName)
{
	String displayName = FixBeefFunctionName(functionName);
	String name;
	int chevronDepth = 0;
	for (char c : displayName)
	{
		if (c == '(')
			break;
		if (c == '>')
		{
			chevronDepth--;
			continue;
		}
		if (c == '<')
			chevronDepth++;
		if (chevronDepth == 0)
			name.Append(c);
	}
	return name;
}

bool LLDBDebugger::IsStepFiltered(lldb::SBFunction& function)
{
	const char* functionName = function.GetName();
	if (functionName == NULL)
		return false;
	String filterName = GetStepFilterName(functionName);

	StepFilter* stepFilter = NULL;
	if (mDebugManager->mStepFilters.TryGetValue(filterName, &stepFilter))
	{
		if (stepFilter->mFilterKind == BfStepFilterKind_Filtered)
			return true;
		if (stepFilter->mFilterKind == BfStepFilterKind_NotFiltered)
			return false;
	}
	// Unqualified names like "__chkstk" are system functions
	return (functionName[0] == '_') && (functionName[1] == '_');
}

// Called when a step completes. Returns true if it kept stepping instead of stopping here.
bool LLDBDebugger::ContinueStep(lldb::SBThread& thread)
{
	if ((mStepKind == StepKind_None) || (mStepContinueCount >= 16))
		return false;

	lldb::SBFrame frame = thread.GetFrameAtIndex(0);
	lldb::SBFunction function = frame.GetFunction();

	// A step out of a filtered method continues into the rest of the line it returned to
	if (mStepOutThenInto)
	{
		mStepOutThenInto = false;
		lldb::SBLineEntry lineEntry = frame.GetLineEntry();
		if ((lineEntry.IsValid()) && ((uint64)lineEntry.GetStartAddress().GetLoadAddress(mLLDBTarget) != (uint64)frame.GetPC()))
		{
			mStepContinueCount++;
			thread.StepInto();
			return true;
		}
		return false;
	}

	if (!function.IsValid())
		return false;
	if ((uint64)function.GetStartAddress().GetLoadAddress(mLLDBTarget) == mStepStartFunctionAddr)
		return false;

	bool filtered = (mStepKind == StepKind_Into) && (IsStepFiltered(function));
	bool hasStatementLines = FunctionHasStatementLines(function);
	if ((!filtered) && (hasStatementLines))
	{
		// A step out returns into the middle of the caller's line. Like WinDebugger, finish that line
		// (e.g. storing the returned value) and stop at the next one.
		if ((mStepKind == StepKind_Out) && (!mStepOutFinishedLine))
		{
			lldb::SBLineEntry lineEntry = frame.GetLineEntry();
			if ((lineEntry.IsValid()) && ((uint64)lineEntry.GetStartAddress().GetLoadAddress(mLLDBTarget) != (uint64)frame.GetPC()))
			{
				mStepOutFinishedLine = true;
				mStepContinueCount++;
				thread.StepOver();
				return true;
			}
		}
		return false;
	}

	LLDBLog("ContinueStep: not stopping in %s (%s)\n", function.GetName(), filtered ? "step filter" : "no statement lines");
	mStepContinueCount++;
	if ((mStepKind == StepKind_Into) && (!filtered))
	{
		// Keep stepping in - e.g. through a delegate's Invoke method to the lambda it calls
		thread.StepInto();
	}
	else
	{
		mStepOutThenInto = (mStepKind == StepKind_Into);
		thread.StepOut();
	}
	return true;
}

//----------------------------------------------------------------------------
// Breakpoints
//----------------------------------------------------------------------------

Breakpoint* LLDBDebugger::CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset)
{
	LLDBBreakpoint* bp = new LLDBBreakpoint();
	bp->mFilePath = fileName;
	bp->mRequestedLineNum = lineNum;
	bp->mLineNum = lineNum;
	bp->mColumn = wantColumn;
	bp->mInstrOffset = instrOffset;
	mBreakpoints.push_back(bp);

	if (mLLDBTarget.IsValid())
	{
		bp->mLLDBBreakpoint = CreateLineBreakpoint(bp, lineNum);
		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}

	return bp;
}

Breakpoint* LLDBDebugger::CreateMemoryBreakpoint(intptr addr, int byteCount)
{
	return NULL;
}

Breakpoint* LLDBDebugger::CreateSymbolBreakpoint(const StringImpl& symbolName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBLog("CreateSymbolBreakpoint '%s'\n", symbolName.c_str());

	LLDBBreakpoint* bp = new LLDBBreakpoint();
	bp->mSymbolName = symbolName;
	mBreakpoints.push_back(bp);

	if (mLLDBTarget.IsValid())
	{
		DoCreateBreakpointByName(bp);

		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}

	return bp;
}

Breakpoint* LLDBDebugger::CreateAddressBreakpoint(intptr address)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = new LLDBBreakpoint();
	bp->mResolvedAddr = (uintptr)address;
	mBreakpoints.push_back(bp);

	if (mLLDBTarget.IsValid())
	{
		bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByAddress((lldb::addr_t)address);
		if (bp->mLLDBBreakpoint.IsValid())
		{
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
			mBreakpointAddrMap.ForceAdd((uintptr)address, bp);
		}
	}

	return bp;
}

void LLDBDebugger::CheckBreakpoint(Breakpoint* checkBreakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)checkBreakpoint;

	// If the LLDB breakpoint hasn't been created yet (e.g., called before OpenFile),
	// try to create it now that we have a target.
	if ((!bp->mLLDBBreakpoint.IsValid()) && mLLDBTarget.IsValid())
	{
		if ((!bp->mFilePath.IsEmpty()) && (bp->mRequestedLineNum >= 0))
		{
			bp->mLLDBBreakpoint = CreateLineBreakpoint(bp, bp->mRequestedLineNum);
		}
		else if (!bp->mSymbolName.IsEmpty())
		{
			DoCreateBreakpointByName(bp);
		}

		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}

	HotFilterBreakpointLocations(bp);

	// Try to resolve the load address so FindBreakpointAt() works.
	if ((bp->mLLDBBreakpoint.IsValid()) && (bp->mResolvedAddr == 0))
	{
		for (uint32 locIdx = 0; locIdx < bp->mLLDBBreakpoint.GetNumLocations(); locIdx++)
		{
			lldb::SBBreakpointLocation loc = bp->mLLDBBreakpoint.GetLocationAtIndex(locIdx);
			if ((!loc.IsValid()) || (!loc.IsEnabled()))
				continue;
			lldb::addr_t loadAddr = loc.GetLoadAddress();
			if (loadAddr != (lldb::addr_t)-1)
			{
				bp->mResolvedAddr = (uintptr)loadAddr;
				mBreakpointAddrMap.ForceAdd(bp->mResolvedAddr, bp);
				break;
			}
		}
	}
}

// Bind a breakpoint in the code of an older compile, 'lineNum' being the line in that compile's version of
// the file (as remapped by the IDE). Frames still running old code stop there too.
void LLDBDebugger::HotBindBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int hotIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)wdBreakpoint;
	bp->mPendingHotBindIdx = -1;
	if ((!mLLDBTarget.IsValid()) || (bp->mFilePath.IsEmpty()))
		return;

	lldb::SBFileSpec fileSpec(bp->mFilePath.c_str(), false);
	if (lineNum >= 0)
	{
		lldb::SBFileSpecList modules;
		HotGetVersionModules(hotIdx, modules);
		if (modules.GetSize() > 0)
		{
			// The line is already in that compile's numbering, so moving to the nearest line with code (as
			// the IDE's breakpoints on comment lines rely on) finds the same statement there
			lldb::SBBreakpoint versionBreakpoint = mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(lineNum + 1), 0, 0, modules);
			if (versionBreakpoint.IsValid())
			{
				bp->mVersionBreakpoints.Add(versionBreakpoint);
				mBreakpointIdMap.ForceAdd((int)versionBreakpoint.GetID(), bp);
			}
		}
	}
	bp->mPendingHotBindIdx = HotFindVersionWithFile(fileSpec, hotIdx);
	LLDBLog("HotBindBreakpoint %s:%d in compile %d (%d locations), next older compile %d\n", GetFileName(bp->mFilePath).c_str(), lineNum + 1,
		hotIdx, bp->mVersionBreakpoints.IsEmpty() ? -1 : (int)bp->mVersionBreakpoints.back().GetNumLocations(), bp->mPendingHotBindIdx);
	HotFilterBreakpointLocations(bp);
}

void LLDBDebugger::DeleteBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;

	if (bp == mActiveBreakpoint)
		mActiveBreakpoint = NULL;

	if ((bp->mLLDBBreakpoint.IsValid()) && mLLDBTarget.IsValid())
	{
		auto idItr = mBreakpointIdMap.Find((int)bp->mLLDBBreakpoint.GetID());
		if (idItr->mValue == bp)
			mBreakpointIdMap.Remove(idItr);

		mLLDBTarget.BreakpointDelete(bp->mLLDBBreakpoint.GetID());
	}
	HotDeleteVersionBreakpoints(bp);

	if (bp->mResolvedAddr != 0)
	{
		auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
		if (addrItr->mValue == bp)
			mBreakpointAddrMap.Remove(addrItr);
	}

	if (!bp->mIsLinkedSibling)
		mBreakpoints.Remove(bp);

	delete bp;
}

void LLDBDebugger::DetachBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;

	// Disable the physical breakpoint but keep the object alive
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetEnabled(false);
	for (auto& versionBreakpoint : bp->mVersionBreakpoints)
		versionBreakpoint.SetEnabled(false);

	if (bp->mResolvedAddr != 0)
	{
		auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
		if (addrItr->mValue == bp)
			mBreakpointAddrMap.Remove(addrItr);
		bp->mResolvedAddr = 0;
	}

	bp->mLineNum = bp->mRequestedLineNum;
}

void LLDBDebugger::MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;

	// Remove the old binding
	if ((bp->mLLDBBreakpoint.IsValid()) && mLLDBTarget.IsValid())
	{
		auto idItr = mBreakpointIdMap.Find((int)bp->mLLDBBreakpoint.GetID());
		if (idItr->mValue == bp)
			mBreakpointIdMap.Remove(idItr);

		if (bp->mResolvedAddr != 0)
		{
			auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
			if (addrItr->mValue == bp)
				mBreakpointAddrMap.Remove(addrItr);
			bp->mResolvedAddr = 0;
		}

		mLLDBTarget.BreakpointDelete(bp->mLLDBBreakpoint.GetID());
		bp->mLLDBBreakpoint = lldb::SBBreakpoint();
	}
	HotDeleteVersionBreakpoints(bp);

	bp->mLineNum = lineNum;
	bp->mRequestedLineNum = lineNum;
	bp->mColumn = wantColumn;

	if ((rebindNow) && (mLLDBTarget.IsValid()) && (!bp->mFilePath.IsEmpty()))
	{
		bp->mLLDBBreakpoint = CreateLineBreakpoint(bp, lineNum);
		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}
}

void LLDBDebugger::MoveMemoryBreakpoint(Breakpoint* wdBreakpoint, intptr addr, int byteCount)
{
}

void LLDBDebugger::DisableBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetEnabled(false);
	for (auto& versionBreakpoint : bp->mVersionBreakpoints)
		versionBreakpoint.SetEnabled(false);
}

void LLDBDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& condition)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetCondition(condition.IsEmpty() ? NULL : condition.c_str());
	for (auto& versionBreakpoint : bp->mVersionBreakpoints)
		versionBreakpoint.SetCondition(condition.IsEmpty() ? NULL : condition.c_str());
}

void LLDBDebugger::SetBreakpointLogging(Breakpoint* wdBreakpoint, const StringImpl& logging, bool breakAfterLogging)
{
}

Breakpoint* LLDBDebugger::FindBreakpointAt(intptr address)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = NULL;
	mBreakpointAddrMap.TryGetValue((uintptr)address, &bp);
	return bp;
}

Breakpoint* LLDBDebugger::GetActiveBreakpoint()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveBreakpoint != NULL) && (mActiveBreakpoint->mHead != NULL))
		return mActiveBreakpoint->mHead;
	return mActiveBreakpoint;
}

//----------------------------------------------------------------------------
// Call stack
//----------------------------------------------------------------------------

void LLDBDebugger::ClearCallStack()
{
	mCallStack.Clear();
	mCallStackDirty = true;
}

void LLDBDebugger::UpdateCallStack(bool slowEarlyOut)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mCallStackDirty)
		return;
	if (!mLLDBProcess.IsValid())
		return;
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) && (mRunState != RunState_Exception))
		return;

	mCallStack.Clear();

	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if (!thread.IsValid())
	{
		mCallStackDirty = false;
		return;
	}

	uint32 numFrames = thread.GetNumFrames();
	for (uint32 i = 0; i < numFrames; i++)
	{
		lldb::SBFrame frame = thread.GetFrameAtIndex(i);
		if (!frame.IsValid())
			break;
		mCallStack.push_back(frame);
	}

	mCallStackDirty = false;
}

int LLDBDebugger::GetCallStackCount()
{
	return (int)mCallStack.size();
}

int LLDBDebugger::GetRequestedStackFrameIdx()
{
	return mRequestedStackFrameIdx;
}

int LLDBDebugger::GetBreakStackFrameIdx()
{
	return mBreakStackFrameIdx;
}

void LLDBDebugger::UpdateCallStackMethod(int stackFrameIdx)
{
}

void LLDBDebugger::UpdateRegisterUsage(int stackFrameIdx)
{
}

//----------------------------------------------------------------------------
// Stack frame info
//----------------------------------------------------------------------------

String LLDBDebugger::GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd, int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	*addr = 0;
	*outFile = "";
	*outHotIdx = 0;
	*outDefLineStart = -1;
	*outDefLineEnd = -1;
	*outLine = -1;
	*outColumn = 0;
	*outLanguage = 0;
	*outStackSize = 0;
	*outFlags = 0;

	if (!mLLDBProcess.IsValid())
		return String();

	if (mCallStack.IsEmpty())
		UpdateCallStack();

	if ((stackFrameIdx < 0) || (stackFrameIdx >= (int)mCallStack.size()))
		return String();

	lldb::SBFrame& frame = mCallStack[stackFrameIdx];
	if (!frame.IsValid())
		return String();

	*addr = (intptr)frame.GetPC();
	*outHotIdx = HotGetModuleVersion(frame.GetModule());

	// Stack frame size = difference in SP between this frame and its caller
	if (stackFrameIdx + 1 < (int)mCallStack.size())
	{
		lldb::SBFrame& callerFrame = mCallStack[stackFrameIdx + 1];
		if (callerFrame.IsValid())
		{
			lldb::addr_t sp = frame.GetSP();
			lldb::addr_t callerSP = callerFrame.GetSP();
			if (callerSP > sp)
				*outStackSize = (int32)(callerSP - sp);
		}
	}

	// Source location
	lldb::SBLineEntry lineEntry = frame.GetLineEntry();
	if (lineEntry.IsValid())
	{
		lldb::SBFileSpec fileSpec = lineEntry.GetFileSpec();
		if (fileSpec.IsValid())
		{
			char pathBuf[4096];
			if (fileSpec.GetPath(pathBuf, sizeof(pathBuf)) > 0)
				*outFile = pathBuf;
		}

		uint32 lineNum = lineEntry.GetLine();
		if (lineNum != 0)
			*outLine = (int32)(lineNum - 1);
		*outColumn = (int32)lineEntry.GetColumn();
	}

	DbgLanguage language = DbgLanguage_Unknown;

	switch (frame.GuessLanguage())
	{
	case lldb::eLanguageTypeC:
	case lldb::eLanguageTypeC89:
	case lldb::eLanguageTypeC99:
	case lldb::eLanguageTypeC_plus_plus:
	case lldb::eLanguageTypeC_plus_plus_03:
	case lldb::eLanguageTypeC_plus_plus_11:
	case lldb::eLanguageTypeC_plus_plus_14:
	case lldb::eLanguageTypeC_plus_plus_17:
		language = DbgLanguage_C;
		break;
	}

	char filePath [MAX_PATH] ;
	filePath[0] = 0;
	lineEntry.GetFileSpec().GetPath(filePath, MAX_PATH);
	if (filePath[0] != 0)
	{
		if (outFile != NULL)
			*outFile = filePath;

		if (StringView(filePath).EndsWith(".bf", StringView::CompareKind_OrdinalIgnoreCase))
			language = DbgLanguage_Beef;
	}

	*outLanguage = language;

	// Function name — prefer the display name which includes inlined context.
	// Normalize "bf::Namespace::Method" → "Namespace.Method" for Beef frames.
	const char* funcName = frame.GetDisplayFunctionName();
	if ((funcName == NULL) || (funcName[0] == '\0'))
		funcName = frame.GetFunctionName();

	if ((funcName != NULL) && (funcName[0] != '\0'))
	{
		String result;

		// Prefix with "module!" when we can determine the module name
		lldb::SBModule module = frame.GetModule();
		if (module.IsValid())
		{
			const char* moduleName = module.GetFileSpec().GetFilename();
			if ((moduleName != NULL) && (moduleName[0] != '\0'))
			{
				result += moduleName;
				result += '!';
			}
		}

		if (language == DbgLanguage_Beef)
			result += FixBeefFunctionName(funcName);
		else
			result += funcName;
		return result;
	}

	return StrFormat("0x%llX", (uint64)*addr);
}

String LLDBDebugger::Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	return String();
}

int LLDBDebugger::GetJmpState(int stackFrameIdx)
{
	return 0;
}

intptr LLDBDebugger::GetStackFrameCalleeAddr(int stackFrameIdx)
{
	return intptr();
}

String LLDBDebugger::GetStackMethodOwner(int stackFrameIdx, int& language)
{
	return String();
}

//----------------------------------------------------------------------------
// Code address queries
//----------------------------------------------------------------------------

void LLDBDebugger::GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
}

void LLDBDebugger::GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
}

String LLDBDebugger::FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve)
{
	lldb::SBLineEntry entry;
	entry.SetLine(line);
	entry.SetColumn(column);
	lldb::SBFileSpec fileSpec;
	int slash = BF_MAX(fileName.LastIndexOf('/'), fileName.LastIndexOf('\\'));
	if (slash < 0)
	{
		fileSpec.SetDirectory(".");
		fileSpec.SetFilename(fileName.c_str());
	}
	else
	{
		String dir(fileName, 0, slash - 1);
		String filename(fileName, slash + 1, fileName.length() - slash - 1);
		fileSpec.SetDirectory(dir.c_str());
		fileSpec.SetFilename(filename.c_str());
	}
	entry.SetFileSpec(fileSpec);
	auto addr = entry.GetStartAddress();
	if (!addr.IsValid())
		return String();
	String result;
	result += EncodeDataPtr(addr.GetLoadAddress(mLLDBTarget), false);
	result += '\t';
	auto func = addr.GetFunction();
	if (func.IsValid())
		result += func.GetName();
	result += '\n';
	return result;
}

String LLDBDebugger::GetAddressSourceLocation(intptr address)
{
	lldb::SBAddress addr;
	addr.SetLoadAddress(address, mLLDBTarget);
	if (!addr.IsValid())
		return "!Invalid address";
	auto entry = addr.GetLineEntry();
	return StrFormat("%s/%s:%u:%u", 
		entry.GetFileSpec().GetDirectory(), entry.GetFileSpec().GetFilename(), 
		entry.GetLine(), entry.GetColumn());
}

String LLDBDebugger::GetAddressSymbolName(intptr address, bool demangle)
{
	lldb::SBAddress addr;
	addr.SetLoadAddress(address, mLLDBTarget);
	if (!addr.IsValid())
		return "!Invalid address";
	auto symbol = addr.GetSymbol();
	if (!symbol.IsValid())
		return "!Invalid symbol";
	if (demangle)
		return symbol.GetDisplayName();
	else
		return symbol.GetMangledName();
}

static void FormatDisassembleSBInstruction(lldb::SBInstruction inst, lldb::SBTarget target, String& outString)
{
	outString += StrFormat("D %@:  %@   %s %s",
		inst.GetAddress().GetFileAddress(), inst.GetAddress().GetLoadAddress(target),
		inst.GetMnemonic(target), inst.GetOperands(target)
	);
	auto comment = inst.GetComment(target);
	if (comment != NULL && comment[0] != '\0')
	{
		outString += " ; ";
		outString += comment;
	}
	outString += '\n';
}

String LLDBDebugger::DisassembleAtRaw(intptr address)
{
	uint8 buffer[2048];
	auto instructions = mLLDBTarget.GetInstructions((lldb::addr_t)address, buffer, sizeof(buffer));
	if (!instructions.IsValid())
		return "!Disassembly failed";
	
	String result = "R\n";
	lldb::SBSymbol prevSymbol;
	for (int i = 0; true; i++)
	{
		auto inst = instructions.GetInstructionAtIndex(i);
		if (!inst.IsValid()) break;
		auto symbol = inst.GetAddress().GetSymbol();
		if (symbol.IsValid() && symbol != prevSymbol)
		{
			result += "T ";
			result += symbol.GetDisplayName();
			result += ":\n";
			prevSymbol = symbol;
		}
		FormatDisassembleSBInstruction(inst, mLLDBTarget, result);
	}

	return result;
}

String LLDBDebugger::DisassembleAt(intptr address)
{
	lldb::SBAddress addr;
	addr.SetLoadAddress(address, mLLDBTarget);
	if (!addr.IsValid())
		return "!Invalid address";
	auto function = addr.GetFunction();
	if (!function.IsValid())
		return DisassembleAtRaw(address);

	String result;
	if (function.GetIsOptimized())
		result += "O\n";
	int prevLine = -1;
	StringView prevDir;
	StringView prevFilename;
	auto _UpdateLineData = [&](lldb::SBAddress addr)
	{
		auto lineEntry = addr.GetLineEntry();
		if (lineEntry.IsValid())
		{
			auto fileSpec = lineEntry.GetFileSpec();
			if (fileSpec.IsValid())
			{
				StringView dir = fileSpec.GetDirectory();
				StringView filename = fileSpec.GetFilename();
				if (dir != prevDir || filename != prevFilename)
				{
					result += "S " + dir + "/" + filename + "\n";
					prevDir = dir;
					prevFilename = filename;
				}
			}
			int line = lineEntry.GetLine();
			if (line != prevLine)
			{
				if (prevLine == -1)
					result += StrFormat("L %d 1\n", line);
				else if (line > prevLine)
					result += StrFormat("L %d %d\n", prevLine + 1, line - prevLine);
				prevLine = line;
			}
		}
	};

	auto instructions = function.GetInstructions(mLLDBTarget);
	if (!instructions.IsValid())
		return "!Disassembly failed";

	for (int i = 0; true; i++)
	{
		auto inst = instructions.GetInstructionAtIndex(i);
		if (!inst.IsValid()) break;
		_UpdateLineData(inst.GetAddress());
		FormatDisassembleSBInstruction(inst, mLLDBTarget, result);
	}
	return result;
}

String LLDBDebugger::FindLineCallAddresses(intptr address)
{
	return String();
}

//----------------------------------------------------------------------------
// Memory access
//----------------------------------------------------------------------------

bool LLDBDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	if (!mLLDBProcess.IsValid())
		return false;
	lldb::SBError error;
	size_t bytesRead = mLLDBProcess.ReadMemory((lldb::addr_t)address, dest, (size_t)length, error);
	return (!error.Fail()) && (bytesRead == (size_t)length);
}

bool LLDBDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	if (!mLLDBProcess.IsValid())
		return false;
	lldb::SBError error;
	size_t bytesWritten = mLLDBProcess.WriteMemory((lldb::addr_t)address, src, (size_t)length, error);
	return (!error.Fail()) && (bytesWritten == (size_t)length);
}

DbgMemoryFlags LLDBDebugger::GetMemoryFlags(intptr address)
{
	return DbgMemoryFlags_None;
}

//----------------------------------------------------------------------------
// Process / thread info
//----------------------------------------------------------------------------

String LLDBDebugger::GetProcessInfo()
{
	if (!mLLDBProcess.IsValid())
		return String();

	String result;

#ifdef __linux__
	// Virtual / working set from /proc/<pid>/status
	{
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/status", mProcessId);
		FILE* f = fopen(path, "r");
		if (f != NULL)
		{
			uint64 vmSize = 0, vmRSS = 0;
			char line[256];
			while (fgets(line, sizeof(line), f) != NULL)
			{
				unsigned long val = 0;
				if (sscanf(line, "VmSize: %lu kB", &val) == 1)
					vmSize = (uint64)val * 1024ULL;
				else if (sscanf(line, "VmRSS: %lu kB", &val) == 1)
					vmRSS = (uint64)val * 1024ULL;
			}
			fclose(f);
			result += StrFormat("VirtualMemory\t%llu\n", vmSize);
			result += StrFormat("WorkingMemory\t%llu\n", vmRSS);
		}
	}
	// CPU times from /proc/<pid>/stat (fields 14=utime, 15=stime, clock ticks)
	{
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/stat", mProcessId);
		FILE* f = fopen(path, "r");
		if (f != NULL)
		{
			int pid;
			char comm[256];
			char state;
			int ppid, pgrp, session, tty, tpgid;
			unsigned long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
			if (fscanf(f, "%d %255s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu",
				&pid, comm, &state, &ppid, &pgrp, &session, &tty, &tpgid,
				&flags, &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime) == 15)
			{
				long clkTck = sysconf(_SC_CLK_TCK);
				if (clkTck <= 0)
					clkTck = 100;
				// Convert clock ticks → 100-nanosecond units (matches WinDebugger)
				uint64 utimeHns = (uint64)utime * 10000000ULL / (uint64)clkTck;
				uint64 stimeHns = (uint64)stime * 10000000ULL / (uint64)clkTck;
				result += StrFormat("UserTime\t%llu\n",   utimeHns);
				result += StrFormat("KernelTime\t%llu\n", stimeHns);
			}
			fclose(f);
		}
	}
#elif defined(__APPLE__)
	// macOS: use task_info for memory and times
	{
		struct mach_task_basic_info info;
		mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
		task_t task;
		if (task_for_pid(mach_task_self(), mProcessId, &task) == KERN_SUCCESS)
		{
			if (task_info(task, MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
			{
				result += StrFormat("VirtualMemory\t%llu\n",  (uint64)info.virtual_size);
				result += StrFormat("WorkingMemory\t%llu\n",  (uint64)info.resident_size);
				// user_time / system_time are in microseconds (struct time_value_t)
				uint64 utimeHns  = (uint64)info.user_time.seconds   * 10000000ULL
				                 + (uint64)info.user_time.microseconds * 10ULL;
				uint64 stimeHns  = (uint64)info.system_time.seconds  * 10000000ULL
				                 + (uint64)info.system_time.microseconds * 10ULL;
				result += StrFormat("UserTime\t%llu\n",   utimeHns);
				result += StrFormat("KernelTime\t%llu\n", stimeHns);
			}
			mach_port_deallocate(mach_task_self(), task);
		}
	}
#endif

	return result;
}

int LLDBDebugger::GetProcessId()
{
	return mProcessId;
}

String LLDBDebugger::GetThreadInfo()
{
	if (!mLLDBProcess.IsValid())
		return String();

	lldb::SBThread activeThread = mLLDBProcess.GetSelectedThread();
	int activeThreadId = activeThread.IsValid() ? (int)activeThread.GetThreadID() : 0;

	// First line: active thread ID
	String result;
	result += StrFormat("%d\n", activeThreadId);

	uint32 numThreads = mLLDBProcess.GetNumThreads();
	for (uint32 i = 0; i < numThreads; i++)
	{
		lldb::SBThread thread = mLLDBProcess.GetThreadAtIndex(i);
		if (!thread.IsValid())
			continue;

		int threadId = (int)thread.GetThreadID();

		// Thread name: use LLDB name, fall back to ordinal labels
		String threadName;
		const char* name = thread.GetName();
		if ((name != NULL) && (name[0] != '\0'))
			threadName = name;
		else if (i == 0)
			threadName = "Main Thread";
		else
			threadName = StrFormat("Worker Thread %d", threadId);

		// Location: module!function from the topmost frame, or raw PC
		String locString;
		lldb::SBFrame frame = thread.GetFrameAtIndex(0);
		if (frame.IsValid())
		{
			lldb::SBModule module = frame.GetModule();
			if (module.IsValid())
			{
				const char* moduleName = module.GetFileSpec().GetFilename();
				if ((moduleName != NULL) && (moduleName[0] != '\0'))
				{
					locString += moduleName;
					locString += '!';
				}
			}

			const char* funcName = frame.GetDisplayFunctionName();
			if ((funcName == NULL) || (funcName[0] == '\0'))
				funcName = frame.GetFunctionName();
			if ((funcName != NULL) && (funcName[0] != '\0'))
				locString += FixBeefFunctionName(funcName);
			else
				locString += StrFormat("0x%llX", (uint64)frame.GetPC());
		}
		else
		{
			locString = StrFormat("0x%llX", (uint64)0);
		}

		result += StrFormat("%d\t", threadId);
		result += threadName;
		result += '\t';
		result += locString;

		// Mark frozen threads with "Fr" attribute (matching WinDebugger format)
		if (thread.IsSuspended())
			result += "\tFr";

		result += '\n';
	}

	return result;
}

void LLDBDebugger::SetActiveThread(int threadId)
{
	if (!mLLDBProcess.IsValid())
		return;

	// Nothing to do if the requested thread is already selected
	lldb::SBThread current = mLLDBProcess.GetSelectedThread();
	if (current.IsValid() && ((int)current.GetThreadID() == threadId))
		return;

	if (mLLDBProcess.SetSelectedThreadByID((lldb::tid_t)threadId))
	{
		// The call stack belongs to a specific thread — discard it so
		// UpdateCallStack() will rebuild it for the newly selected thread.
		ClearCallStack();
		mCallStackDirty = true;
	}
}

int LLDBDebugger::GetActiveThread()
{
	if (!mLLDBProcess.IsValid())
		return 0;
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	return thread.IsValid() ? (int)thread.GetThreadID() : 0;
}

void LLDBDebugger::FreezeThread(int threadId)
{
}

void LLDBDebugger::ThawThread(int threadId)
{
}

bool LLDBDebugger::IsActiveThreadWaiting()
{
	return false;
}

String LLDBDebugger::GetCurrentException()
{
	if (mRunState != RunState_Exception)
		return String();

	// Format: address\nexceptionCode\ndescription
	// Matches what WinDebugger returns (addr / %08X code / description string).
	String result;
	result += StrFormat("0x%llX", mExceptionAddress);
	result += '\n';
	result += StrFormat("%08X", mExceptionCode);
	result += '\n';
	result += mExceptionDescription;

	// Transition to Paused so the IDE can use eval / step from this state,
	// mirroring the WinDebugger behaviour where GetCurrentException clears the
	// "exception pending" flag.
	mRunState = RunState_Paused;

	return result;
}

//----------------------------------------------------------------------------
// Expression evaluation helpers
//----------------------------------------------------------------------------

struct LLDBFormatInfo
{
	DwIntDisplayType mIntDisplayType;
	String mRefId;       // refid=XXX  — reference ID for persistent display formatting
	String mThisExpr;    // this=EXPR  — implicit "this" context for member access
	bool mNoMembers;     // nm         — suppress member expansion
	bool mNoAddress;     // na         — suppress address metadata
	bool mRawString;     // rawStr     — don't escape string content
	int mMaxCount;       // count=N / maxcount=N / arraysize=N

	LLDBFormatInfo()
	{
		mIntDisplayType = DwIntDisplayType_Default;
		mNoMembers      = false;
		mNoAddress      = false;
		mRawString      = false;
		mMaxCount       = -1;
	}
};

// Try to interpret a single specifier token (everything after a comma).
// Returns true and updates fmtInfo if the token is a recognised specifier.
static bool TryParseSpecifier(const char* spec, LLDBFormatInfo& fmtInfo)
{
	if (strcmp(spec, "x") == 0)
	{
		fmtInfo.mIntDisplayType = DwIntDisplayType_HexadecimalLower;
		return true;
	}
	if ((strcmp(spec, "X") == 0) || (strcmp(spec, "Xh") == 0))
	{
		fmtInfo.mIntDisplayType = DwIntDisplayType_HexadecimalUpper;
		return true;
	}
	if (strcmp(spec, "d") == 0)
	{
		fmtInfo.mIntDisplayType = DwIntDisplayType_Decimal;
		return true;
	}
	// String display hints
	if ((strcmp(spec, "s")   == 0) || (strcmp(spec, "s8")  == 0) ||
	    (strcmp(spec, "s16") == 0) || (strcmp(spec, "s32") == 0))
		return true;
	if (strcmp(spec, "rawStr") == 0)
	{
		fmtInfo.mRawString = true;
		return true;
	}
	// Suppression flags
	if (strcmp(spec, "nm") == 0) { fmtInfo.mNoMembers = true; return true; }
	if (strcmp(spec, "na") == 0) { fmtInfo.mNoAddress = true; return true; }
	// Silently consume other two-char "n*" flags (nd, ne, nv) for compatibility
	if ((spec[0] == 'n') && (spec[1] != '\0') && (spec[2] == '\0'))
		return true;

	// Key=value specifiers
	if (strncmp(spec, "refid=", 6) == 0)
	{
		fmtInfo.mRefId = spec + 6;
		return true;
	}
	if (strncmp(spec, "this=", 5) == 0)
	{
		fmtInfo.mThisExpr = spec + 5;
		return true;
	}
	if ((strncmp(spec, "count=",     6) == 0) ||
	    (strncmp(spec, "maxcount=",  9) == 0) ||
	    (strncmp(spec, "arraysize=", 10) == 0))
	{
		fmtInfo.mMaxCount = atoi(strchr(spec, '=') + 1);
		return true;
	}
	// Silently consume specifiers we accept but don't act on yet
	if ((strncmp(spec, "assign=",          7) == 0) ||
	    (strncmp(spec, "action=",          7) == 0) ||
	    (strncmp(spec, "_=",               2) == 0) ||
	    (strncmp(spec, "expectedType=",   13) == 0) ||
	    (strncmp(spec, "namespaceSearch=", 16) == 0))
		return true;

	return false;
}

// Parse an expression string into a bare expression and a populated LLDBFormatInfo.
// Specifiers are comma-separated tokens appended after the expression and are
// consumed right-to-left, so multiple specifiers can be stacked
// (e.g. "expr,x,refid=foo,this=(String*)0x1234").
// Note: commas inside a specifier value (e.g. template args in a this= type)
// are not supported; use a typedef or pointer cast to avoid them.
static void ParseExprAndFormat(const StringImpl& expr, String& outExpr, LLDBFormatInfo& outFmt)
{
	outExpr = expr;
	outFmt  = LLDBFormatInfo();

	// Strip a leading language prefix (@Beef: / @C:)
	{
		const char* src = outExpr.c_str();
		if ((src[0] == '@') && (src[1] != '\0'))
		{
			const char* colon = strchr(src + 1, ':');
			if (colon != NULL)
				outExpr = String(colon + 1);
		}
	}

	// Consume specifiers right-to-left until we hit something unrecognised
	while (true)
	{
		const char* p   = outExpr.c_str();
		int         len = (int)strlen(p);

		int commaPos = -1;
		for (int i = len - 1; i >= 0; --i)
		{
			if (p[i] == ',')
			{
				commaPos = i;
				break;
			}
		}
		if (commaPos < 0)
			break;

		if (!TryParseSpecifier(p + commaPos + 1, outFmt))
			break;

		outExpr = String(p, commaPos);
	}
}

// Parse an LLDB expression error string into the IDE error wire format:
//   "!LINE\tCOL\tmessage"
// LLDB errors look like:
//   warning: ...\n
//   error: <user expression N>:LINE:COL: message\n
//       N | expr\n
//         | ^~~\n
static String FormatLLDBError(const char* errMsg)
{
	if ((errMsg == NULL) || (errMsg[0] == '\0'))
		return "!Unknown error";

	// Scan lines looking for the first "error:" line (skip warnings)
	const char* errorPayload = NULL;
	const char* p = errMsg;
	while (*p != '\0')
	{
		if (strncmp(p, "error: ", 7) == 0)
		{
			errorPayload = p + 7;
			break;
		}
		while ((*p != '\0') && (*p != '\n'))
			++p;
		if (*p == '\n')
			++p;
	}

	if (errorPayload == NULL)
	{
		// No "error:" line — return the raw message, first line only
		String msg = "!";
		const char* q = errMsg;
		while ((*q != '\0') && (*q != '\n'))
			msg += *q++;
		return msg;
	}

	// Skip optional "<user expression N>" source-file token
	const char* src = errorPayload;
	if (*src == '<')
	{
		const char* gt = strchr(src, '>');
		if (gt != NULL)
		{
			src = gt + 1;
			if (*src == ':')
				++src;
		}
	}

	// Parse LINE:COL: message
	char* end;
	long line = strtol(src, &end, 10);
	if ((end != src) && (*end == ':'))
	{
		const char* colStart = end + 1;
		long col = strtol(colStart, &end, 10);
		if ((end != colStart) && (*end == ':'))
		{
			const char* msgStart = end + 1;
			while (*msgStart == ' ')
				++msgStart;

			// Take just the first line of the message
			String message;
			const char* q = msgStart;
			while ((*q != '\0') && (*q != '\n'))
				message += *q++;

			int errLen = 1; // We don't get a length from LLDB
			return StrFormat("!%d\t%d\t", (int)col - 1, errLen) + message;
		}
	}

	// Fallback: return error payload as-is (first line only)
	String msg = "!";
	const char* q = errorPayload;
	while ((*q != '\0') && (*q != '\n'))
		msg += *q++;
	return msg;
}

// Format a single SBValue into the IDE wire format:
//   line 0 : display value
//   line 1 : type name
//   line 2+: ":key[\tval]" metadata lines
static String FormatSBValueToResult(lldb::SBValue value, const LLDBFormatInfo& fmt)
{
	lldb::SBError error = value.GetError();
	if (error.Fail())
		return FormatLLDBError(error.GetCString());
	if (!value.IsValid())
		return FormatLLDBError("error: invalid expression result");

	lldb::SBType valueType = value.GetType();
	String typeName = FixBeefFunctionName(valueType.GetName());

	bool isPointer   = valueType.IsPointerType();
	bool isReference = valueType.IsReferenceType();
	lldb::BasicType basicType = valueType.GetCanonicalType().GetBasicType();
	lldb::TypeClass typeClass = valueType.GetTypeClass();

	DwIntDisplayType intDisplayType = fmt.mIntDisplayType;

	String displayVal;

	// ---- Compute display value ----
	if (isPointer || isReference)
	{
		uint64 addr = value.GetValueAsUnsigned(0);
		if (addr == 0)
		{
			displayVal = "null";
		}
		else
		{
			// For char*, prefer the string summary LLDB already builds
			lldb::BasicType ptBasic = valueType.GetPointeeType().GetBasicType();
			const char* summary = value.GetSummary();
			if ((summary != NULL) &&
				((ptBasic == lldb::eBasicTypeChar) ||
				 (ptBasic == lldb::eBasicTypeSignedChar) ||
				 (ptBasic == lldb::eBasicTypeUnsignedChar)))
				displayVal = summary;
			else
				displayVal = StrFormat("0x%llX", addr);
		}
	}
	else if (intDisplayType != DwIntDisplayType_Default)
	{
		// User-requested numeric override
		bool isSigned = ((basicType == lldb::eBasicTypeShort) ||
		                 (basicType == lldb::eBasicTypeInt) ||
		                 (basicType == lldb::eBasicTypeLong) ||
		                 (basicType == lldb::eBasicTypeLongLong));
		uint64 uval = value.GetValueAsUnsigned(0);
		if (intDisplayType == DwIntDisplayType_HexadecimalLower)
			displayVal = StrFormat("0x%llx", uval);
		else if (intDisplayType == DwIntDisplayType_HexadecimalUpper)
			displayVal = StrFormat("0x%llX", uval);
		else
		{
			if (isSigned)
				displayVal = StrFormat("%lld", (int64)uval);
			else
				displayVal = StrFormat("%llu", uval);
		}
	}
	else
	{
		// Fall back to LLDB's own value string
		const char* valStr = value.GetValue();
		if ((valStr == NULL) || (valStr[0] == '\0'))
		{
			// Composite type — try summary or load address
			const char* summary = value.GetSummary();
			lldb::addr_t loadAddr = value.GetLoadAddress();
			if (summary != NULL)
				displayVal = summary;
			else if (loadAddr != LLDB_INVALID_ADDRESS)
				displayVal = StrFormat("{...} @ 0x%llX", (uint64)loadAddr);
			else
				displayVal = "{...}";
		}
		else
		{
			displayVal = valStr;
		}
	}

	// ---- Build the result string ----
	String result;
	result += displayVal;
	result += '\n';
	result += typeName;

	if (isPointer || isReference)
	{
		result += "\n:type\tpointer";
		uint64 addr = value.GetValueAsUnsigned(0);
		if ((addr != 0) && !fmt.mNoAddress)
		{
			result += StrFormat("\n:pointer\t0x%llX", addr);
			String pointeeName = FixBeefFunctionName(valueType.GetPointeeType().GetName());
			result += StrFormat("\n:pointeeExpr\t(%s)0x%llX", pointeeName.c_str(), addr);
			result += StrFormat("\n:addrValueExpr\t(%s*)0x%llX", typeName.c_str(), addr);
		}
	}
	else
	{
		// Determine type category
		const char* typeCategory = NULL;
		switch (basicType)
		{
		case lldb::eBasicTypeBool:
		case lldb::eBasicTypeChar:
		case lldb::eBasicTypeSignedChar:
		case lldb::eBasicTypeUnsignedChar:
		case lldb::eBasicTypeWChar:
		case lldb::eBasicTypeChar16:
		case lldb::eBasicTypeChar32:
		case lldb::eBasicTypeShort:
		case lldb::eBasicTypeUnsignedShort:
		case lldb::eBasicTypeInt:
		case lldb::eBasicTypeUnsignedInt:
		case lldb::eBasicTypeLong:
		case lldb::eBasicTypeUnsignedLong:
		case lldb::eBasicTypeLongLong:
		case lldb::eBasicTypeUnsignedLongLong:
		case lldb::eBasicTypeInt128:
		case lldb::eBasicTypeUnsignedInt128:
			typeCategory = "int";
			break;
		case lldb::eBasicTypeHalf:
		case lldb::eBasicTypeFloat:
		case lldb::eBasicTypeDouble:
		case lldb::eBasicTypeLongDouble:
			typeCategory = "float";
			break;
		default:
			if (typeClass == lldb::eTypeClassEnumeration)
				typeCategory = "int";
			else if ((typeClass == lldb::eTypeClassStruct) ||
			         (typeClass == lldb::eTypeClassClass) ||
			         (typeClass == lldb::eTypeClassUnion))
				typeCategory = "object";
			else
				typeCategory = "valuetype";
			break;
		}

		if (typeCategory != NULL)
		{
			result += "\n:type\t";
			result += typeCategory;
		}
	}

	lldb::addr_t loadAddr = value.GetLoadAddress();

	if (isPointer) 
		typeClass = valueType.GetPointeeType().GetTypeClass();
	else if (isReference)
		typeClass = valueType.GetDereferencedType().GetTypeClass();
	bool isComposite = ((typeClass == lldb::eTypeClassStruct) ||
		(typeClass == lldb::eTypeClassClass) ||
		(typeClass == lldb::eTypeClassUnion) ||
		(typeClass == lldb::eTypeClassArray));
	if (isComposite)
	{
		// Expose address so the IDE can navigate members, unless suppressed
		if ((loadAddr != LLDB_INVALID_ADDRESS) && !fmt.mNoAddress && !fmt.mNoMembers && !isPointer && !isReference)
		{
			result += StrFormat("\n:pointer\t0x%llX", (uint64)loadAddr);
			result += StrFormat("\n:addrValueExpr\t(%s*)0x%llX", typeName.c_str(), (uint64)loadAddr);
		}

		// Append member list: alternating name/expression-template pairs.
		// The expression template uses "{0}" as a placeholder for the parent's
		// eval string — WatchPanel substitutes it via AppendF when expanding.
		if (!fmt.mNoMembers)
		{
			result += '\n';
			
			const char* fmt = isPointer ? "({0})->%s" : "({0}).%s";
			uint32 numChildren = value.GetNumChildren();
			for (uint32 i = 0; i < numChildren; i++)
			{
				lldb::SBValue child = value.GetChildAtIndex(i);
				if (!child.IsValid())
					continue;
				const char* childName = child.GetName();
				if ((childName == NULL) || (childName[0] == '\0'))
					continue;

				result += '\n';
				result += childName;
				result += '\t';
				if (childName[0] == '[')
					result += StrFormat("({0})%s", childName);
				else
					result += StrFormat(fmt, childName);
			}
		}
	}
	else
	{
		// Primitive/enum — mark editable if it lives in addressable memory
		if (loadAddr != LLDB_INVALID_ADDRESS)
		{
			result += "\n:canEdit";
			result += "\n:editVal\t";
			result += displayVal;
		}
	}

	return result;
}

//----------------------------------------------------------------------------
// Expression evaluation
//----------------------------------------------------------------------------

String LLDBDebugger::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	LLDBLog("Evaluate '%s'\n", expr.c_str());

	if (!mLLDBProcess.IsValid())
		return "!Not running";
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) &&
		(mRunState != RunState_Exception))
		return "!Not paused";

	if (mCallStack.IsEmpty())
		UpdateCallStack();
	if ((callStackIdx < 0) || (callStackIdx >= (int)mCallStack.size()))
		return "!Invalid stack frame";

	lldb::SBFrame frame = mCallStack[callStackIdx];
	if (!frame.IsValid())
		return "!Invalid stack frame";

	// Strip trailing format specifiers and language prefix
	String evalExpr;
	LLDBFormatInfo fmtInfo;
	ParseExprAndFormat(expr, evalExpr, fmtInfo);

	// Configure evaluation options
	lldb::SBExpressionOptions options;
	options.SetUnwindOnError(true);
	options.SetTryAllThreads(false);
	bool allowSideEffects = ((expressionFlags & DwEvalExpressionFlag_AllowSideEffects) != 0) ||
	                        ((expressionFlags & DwEvalExpressionFlag_AllowCalls) != 0);
	options.SetAllowJIT(allowSideEffects);
	if (!allowSideEffects)
		options.SetSuppressPersistentResult(true);

	// Validate-only mode: just check that the expression compiles
	if ((expressionFlags & DwEvalExpressionFlag_ValidateOnly) != 0)
	{
		lldb::SBValue val = frame.EvaluateExpression(evalExpr.c_str(), options);
		lldb::SBError err = val.GetError();
		return err.Fail() ? FormatLLDBError(err.GetCString()) : String();
	}

	// Evaluate the expression directly
	lldb::SBValue value = frame.EvaluateExpression(evalExpr.c_str(), options);

	// "this=" fallback: if direct evaluation failed and a this-context was specified,
	// retry as "(thisExpr)->expr".  This mirrors WinDebugger behaviour where a bare
	// member name resolves against the implicit this when not found as a local.
	if ((value.GetError().Fail() || !value.IsValid()) && !fmtInfo.mThisExpr.IsEmpty())
	{
		String memberExpr = "(";
		memberExpr += fmtInfo.mThisExpr;
		memberExpr += ")->";
		memberExpr += evalExpr;
		lldb::SBValue memberValue = frame.EvaluateExpression(memberExpr.c_str(), options);
		if (!memberValue.GetError().Fail() && memberValue.IsValid())
			value = memberValue;
	}

	String result = FormatSBValueToResult(value, fmtInfo);

	// Append reference ID if one was specified — the IDE uses this to associate
	// a persistent display format with this particular watch expression
	if (!fmtInfo.mRefId.IsEmpty())
	{
		result += "\n:referenceId\t";
		result += fmtInfo.mRefId;
	}

	//LLDBLog(" Result: %s\n", result.c_str());
	return result;
}

String LLDBDebugger::EvaluateContinue()
{
	return String();
}

void LLDBDebugger::EvaluateContinueKeep()
{
}

String LLDBDebugger::EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos)
{
	/*String evalExpr;
	LLDBFormatInfo fmtInfo;
	ParseExprAndFormat(expr, evalExpr, fmtInfo);
	auto value = mCallStack[callStackIdx].EvaluateExpression(evalExpr.c_str());
	return FormatSBValueToResult(value, fmtInfo);*/
	return Evaluate(expr, callStackIdx, cursorPos, /*unused*/ -1, (DwEvalExpressionFlags)0);
}

String LLDBDebugger::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
{
	return Evaluate(expr, mCallStack.Count() - 1, cursorPos, /*unused*/ -1, (DwEvalExpressionFlags)0);
}

String LLDBDebugger::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	return String();
}

String LLDBDebugger::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	auto locals = mCallStack[callStackIdx].GetVariables(true, true, false, false);

	String result;
	for (int i = 0; i < locals.GetSize(); i++)
	{
		auto local = locals.GetValueAtIndex(i);
		result += StrFormat("&%s\t%llu\t%llu\n", local.GetName(), local.GetLoadAddress(), (uint64)local.GetByteSize());
	}

	return result;
}

String LLDBDebugger::GetAutoLocals(int callStackIdx, bool showRegs)
{
	auto locals = mCallStack[callStackIdx].GetVariables(true, true, false, false);

	String result;
	for (int i = 0; i < locals.GetSize(); i++)
	{
		result += locals.GetValueAtIndex(i).GetName();
		result += '\n';
	}

	if (showRegs)
	{
		auto regs = mCallStack[callStackIdx].GetRegisters();
		for (int i = 0; i < regs.GetSize(); i++)
		{
			result += regs.GetValueAtIndex(i).GetName();
			result += '\n';
		}
	}

	return result;
}

String LLDBDebugger::CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx)
{
	return String();
}

//----------------------------------------------------------------------------
// Module / debug info loading (stubs)
//----------------------------------------------------------------------------

String LLDBDebugger::GetModulesInfo()
{
	return String();
}

void LLDBDebugger::SetAliasPath(const StringImpl& origPath, const StringImpl& localPath)
{
}

void LLDBDebugger::CancelSymSrv()
{
}

bool LLDBDebugger::HasPendingDebugLoads()
{
	return false;
}

int LLDBDebugger::LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

int LLDBDebugger::LoadDebugInfoForModule(const StringImpl& moduleName)
{
	return 0;
}

int LLDBDebugger::LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

//----------------------------------------------------------------------------
// Hot swap
//
// A hot compile hands us the rebuilt ELF relocatable objects. We map a
// region into the target near the executable (so rel32 and abs32
// relocations still reach the original image), load each object's
// allocatable sections into it, resolve and apply its relocations, and then
// redirect every replaced function by writing a 'jmp rel32' over the entry of
// its canonical definition.
//
// Global symbols bind to their canonical definition: the executable's copy,
// or the first hot-loaded copy for symbols introduced by a hot compile. Code
// references therefore always enter through the canonical entry (which jumps
// to the newest version), and data references keep using the original
// storage, so static state survives the swap.
//----------------------------------------------------------------------------


static bool HotFitsInt32(int64 val)
{
	return (val >= INT32_MIN) && (val <= INT32_MAX);
}

static uint64 HotAlignUp(uint64 val, uint64 align)
{
	return (val + align - 1) & ~(align - 1);
}

// jmp [rip+0]; .quad target
static void HotWriteAbsJump(uint8* dest, uint64 target)
{
	dest[0] = 0xFF;
	dest[1] = 0x25;
	memset(dest + 2, 0, 4);
	memcpy(dest + 6, &target, 8);
}

void LLDBDebugger::HotResetState()
{
	mHotHeapStart = 0;
	mHotHeapSize = 0;
	mHotHeapUsed = 0;
	mHotHeapNextHint = 0;
	mHotSymbols.Clear();
	mHotPendingSymbols.Clear();
	mHotExternalAddrs.Clear();
	mHotPatchedEntries.Clear();
	mHotStepTrapIds.Clear();
	mHotInvalidLambdaTrapIds.Clear();
	HotRemoveDebugInfo();
}

// Whether addr is within the jump(s) we wrote at the start of a hot-replaced method
bool LLDBDebugger::HotIsInPatchedEntry(uint64 addr, uint64* outEntryAddr, HotPatchedEntry* outEntry)
{
	// The jump can start up to 127 bytes in (after a 'jmp rel8'), but only does so for prologues shorter than it
	for (int ofs = 0; ofs < HOT_JMP_ABS64_SIZE * 2; ofs++)
	{
		HotPatchedEntry* entry = NULL;
		if ((mHotPatchedEntries.TryGetValue(addr - ofs, &entry)) && (addr < entry->mEndAddr))
		{
			if (outEntryAddr != NULL)
				*outEntryAddr = addr - ofs;
			if (outEntry != NULL)
				*outEntry = *entry;
			return true;
		}
	}
	return false;
}

// A lambda's captures are passed through its '__closure' parameter. Returns false if it has none.
static bool HotGetLambdaClosureType(lldb::SBTarget& target, uint64 addr, String& outTypeName, lldb::SBType& outType)
{
	lldb::SBFunction function = target.ResolveLoadAddress(addr).GetFunction();
	if ((!function.IsValid()) || ((uint64)function.GetStartAddress().GetLoadAddress(target) != addr))
		return false;
	lldb::SBValueList params = function.GetBlock().GetVariables(target, true, false, false);
	for (uint32 paramIdx = 0; paramIdx < params.GetSize(); paramIdx++)
	{
		lldb::SBValue param = params.GetValueAtIndex(paramIdx);
		const char* paramName = param.GetName();
		if ((paramName == NULL) || (strcmp(paramName, "__closure") != 0))
			continue;
		lldb::SBType closureType = param.GetType().GetPointeeType();
		const char* typeName = closureType.GetName();
		outTypeName = (typeName != NULL) ? typeName : "";
		// The closure's definition may be in another module than the lambda
		lldb::SBType completeType = target.FindFirstType(outTypeName.c_str());
		outType = completeType.IsValid() ? completeType : closureType;
		return true;
	}
	return false;
}

// A hot compile can change what a lambda captures, but existing delegates keep the captures they were
// created with. The new code can only run on them if its captures are the old ones, or a prefix of them
// (captures removed from the end) - otherwise the old version is kept and calling it is an error. Closure
// types are named by a hash of their layout, so equal names mean identical captures.
void LLDBDebugger::HotCheckLambdaCaptures(Array<HotPatch>& patches)
{
	for (auto& patch : patches)
	{
		if (!patch.mName.Contains('$'))
			continue;

		String oldTypeName;
		String newTypeName;
		lldb::SBType oldType;
		lldb::SBType newType;
		bool oldHasClosure = HotGetLambdaClosureType(mLLDBTarget, patch.mOldAddr, oldTypeName, oldType);
		bool newHasClosure = HotGetLambdaClosureType(mLLDBTarget, patch.mNewAddr, newTypeName, newType);
		if ((!newHasClosure) || ((oldHasClosure) && (oldTypeName == newTypeName)))
			continue;

		bool compatible = oldHasClosure;
		if (compatible)
		{
			for (uint32 fieldIdx = 0; fieldIdx < newType.GetNumberOfFields(); fieldIdx++)
			{
				if (fieldIdx >= oldType.GetNumberOfFields())
				{
					compatible = false;
					break;
				}
				lldb::SBTypeMember oldField = oldType.GetFieldAtIndex(fieldIdx);
				lldb::SBTypeMember newField = newType.GetFieldAtIndex(fieldIdx);
				const char* oldFieldName = oldField.GetName();
				const char* newFieldName = newField.GetName();
				const char* oldFieldType = oldField.GetType().GetName();
				const char* newFieldType = newField.GetType().GetName();
				if ((oldFieldName == NULL) || (newFieldName == NULL) || (strcmp(oldFieldName, newFieldName) != 0) ||
					(oldFieldType == NULL) || (newFieldType == NULL) || (strcmp(oldFieldType, newFieldType) != 0))
				{
					compatible = false;
					break;
				}
			}
		}

		if (!compatible)
		{
			LLDBLog("HotCheckLambdaCaptures: '%s' captures changed (%s -> %s)\n", patch.mName.c_str(), oldTypeName.c_str(), newTypeName.c_str());
			patch.mIncompatibleLambda = true;
		}
	}
}

// Stepping into a hot-replaced method whose jump couldn't be placed at the end of its old prologue
// (it's too small) would run away: LLDB's step-in runs to a breakpoint there that is never reached.
// While a step-in is in progress, trap the new versions of those methods instead.
void LLDBDebugger::HotSetStepTraps()
{
	HotClearStepTraps();
	for (auto& kv : mHotPatchedEntries)
	{
		if (!kv.mValue.mNeedsStepTrap)
			continue;
		lldb::SBBreakpoint trap = mLLDBTarget.BreakpointCreateByAddress(kv.mValue.mNewAddr);
		if (trap.IsValid())
			mHotStepTrapIds.Add((int)trap.GetID());
	}
}

void LLDBDebugger::HotClearStepTraps()
{
	if (mLLDBTarget.IsValid())
	{
		for (int trapId : mHotStepTrapIds)
			mLLDBTarget.BreakpointDelete((lldb::break_id_t)trapId);
	}
	mHotStepTrapIds.Clear();
}

// A breakpoint on a line at the very start of a hot-replaced method also resolves to the old copy's
// entry, which now holds our jump - it would trap every call on its way to the new code. Frames still
// running the old code use its other locations, so only those on the jump are disabled.
void LLDBDebugger::HotFilterBreakpointLocations(LLDBBreakpoint* bp)
{
	if (mHotPatchedEntries.IsEmpty())
		return;
	auto _Filter = [&](lldb::SBBreakpoint& lldbBreakpoint)
	{
		if (!lldbBreakpoint.IsValid())
			return;
		for (uint32 locIdx = 0; locIdx < lldbBreakpoint.GetNumLocations(); locIdx++)
		{
			lldb::SBBreakpointLocation loc = lldbBreakpoint.GetLocationAtIndex(locIdx);
			if ((loc.IsValid()) && (loc.IsEnabled()) && (HotIsInPatchedEntry((uint64)loc.GetLoadAddress(), NULL, NULL)))
				loc.SetEnabled(false);
		}
	};
	_Filter(bp->mLLDBBreakpoint);
	for (auto& versionBreakpoint : bp->mVersionBreakpoints)
		_Filter(versionBreakpoint);
}

// A file:line breakpoint is bound in the newest compile that has code from the file, since the IDE's
// line numbers are for that version of the file. Older compiles number the file differently; the IDE
// remaps the line for each one (see mPendingHotBindIdx and HotBindBreakpoint).
lldb::SBBreakpoint LLDBDebugger::CreateLineBreakpoint(LLDBBreakpoint* bp, int lineNum)
{
	lldb::SBFileSpec fileSpec(bp->mFilePath.c_str(), false);
	bp->mPendingHotBindIdx = -1;
	HotDeleteVersionBreakpoints(bp);

	int curVersion = HotFindVersionWithFile(fileSpec, INT_MAX);
	if (curVersion <= 0)
		return mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(lineNum + 1));

	lldb::SBFileSpecList modules;
	HotGetVersionModules(curVersion, modules);
	lldb::SBBreakpoint lldbBreakpoint = mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(lineNum + 1), 0, 0, modules);
	bp->mPendingHotBindIdx = HotFindVersionWithFile(fileSpec, curVersion);
	LLDBLog("CreateLineBreakpoint %s:%d in compile %d (%d locations), next older compile %d\n", GetFileName(bp->mFilePath).c_str(), lineNum + 1,
		curVersion, (int)lldbBreakpoint.GetNumLocations(), bp->mPendingHotBindIdx);
	return lldbBreakpoint;
}

void LLDBDebugger::HotDeleteVersionBreakpoints(LLDBBreakpoint* bp)
{
	for (auto& versionBreakpoint : bp->mVersionBreakpoints)
	{
		if (!versionBreakpoint.IsValid())
			continue;
		auto idItr = mBreakpointIdMap.Find((int)versionBreakpoint.GetID());
		if ((idItr != mBreakpointIdMap.end()) && (idItr->mValue == bp))
			mBreakpointIdMap.Remove(idItr);
		if (mLLDBTarget.IsValid())
			mLLDBTarget.BreakpointDelete(versionBreakpoint.GetID());
	}
	bp->mVersionBreakpoints.Clear();
}

// The modules holding code from compile 'hotIdx' - the executable for 0, or that hot load's objects
void LLDBDebugger::HotGetVersionModules(int hotIdx, lldb::SBFileSpecList& outModules)
{
	if (hotIdx == 0)
	{
		outModules.Append(mLLDBTarget.GetExecutable());
		return;
	}
	for (auto& version : mHotVersions)
	{
		if (version.mHotIdx != hotIdx)
			continue;
		for (auto& module : version.mModules)
			outModules.Append(module.GetFileSpec());
	}
}

static bool HotModuleHasFile(lldb::SBModule& module, const lldb::SBFileSpec& fileSpec)
{
	for (uint32 cuIdx = 0; cuIdx < module.GetNumCompileUnits(); cuIdx++)
	{
		if (module.GetCompileUnitAtIndex(cuIdx).FindSupportFileIndex(0, fileSpec, true) != UINT32_MAX)
			return true;
	}
	return false;
}

// The newest compile, older than 'belowHotIdx', that has code from the file (0 is the executable), or -1
int LLDBDebugger::HotFindVersionWithFile(const lldb::SBFileSpec& fileSpec, int belowHotIdx)
{
	for (intptr versionIdx = mHotVersions.size() - 1; versionIdx >= 0; versionIdx--)
	{
		auto& version = mHotVersions[versionIdx];
		if (version.mHotIdx >= belowHotIdx)
			continue;
		for (auto& module : version.mModules)
		{
			if (HotModuleHasFile(module, fileSpec))
				return version.mHotIdx;
		}
	}
	if (belowHotIdx > 0)
	{
		lldb::SBModule exeModule = mLLDBTarget.FindModule(mLLDBTarget.GetExecutable());
		if ((exeModule.IsValid()) && (HotModuleHasFile(exeModule, fileSpec)))
			return 0;
	}
	return -1;
}

// Which compile a module's code is from: 0 for the executable, or the hot load that added it
int LLDBDebugger::HotGetModuleVersion(lldb::SBModule module)
{
	for (auto& version : mHotVersions)
	{
		for (auto& checkModule : version.mModules)
		{
			if (checkModule == module)
				return version.mHotIdx;
		}
	}
	return 0;
}

// Consume process state events until the target reports a (non-restarted) stop.
// Used when we stop or step the target ourselves, so the IDE never sees these stops.
bool LLDBDebugger::HotWaitForStop(String& outError)
{
	lldb::SBListener listener = mLLDBDebugger.GetListener();
	lldb::SBBroadcaster broadcaster = mLLDBProcess.GetBroadcaster();
	for (int tryIdx = 0; tryIdx < 32; tryIdx++)
	{
		lldb::SBEvent event;
		if (!listener.WaitForEventForBroadcasterWithType(10, broadcaster, lldb::SBProcess::eBroadcastBitStateChanged, event))
		{
			outError = "timed out waiting for the target to stop";
			return false;
		}

		lldb::StateType state = lldb::SBProcess::GetStateFromEvent(event);
		if (state == lldb::eStateStopped)
		{
			if (lldb::SBProcess::GetRestartedFromEvent(event))
				continue;
			return true;
		}
		if ((state == lldb::eStateExited) || (state == lldb::eStateDetached) || (state == lldb::eStateCrashed))
		{
			HandleProcessEvent(state);
			outError = "the target stopped running";
			return false;
		}
	}
	outError = "the target did not stop";
	return false;
}

bool LLDBDebugger::HotEvaluate(const StringImpl& expr, uint64& outValue, String& outError)
{
	lldb::SBExpressionOptions options;
	options.SetLanguage(lldb::eLanguageTypeC_plus_plus);
	options.SetUnwindOnError(true);
	options.SetIgnoreBreakpoints(true);
	options.SetTryAllThreads(true);
	options.SetTimeoutInMicroSeconds(5 * 1000 * 1000);

	lldb::SBValue value = mLLDBTarget.EvaluateExpression(expr.c_str(), options);
	lldb::SBError error = value.GetError();
	if ((!value.IsValid()) || (error.Fail()))
	{
		const char* errorStr = error.GetCString();
		outError = StrFormat("expression '%s' failed: %s", expr.c_str(), (errorStr != NULL) ? errorStr : "unknown error");
		return false;
	}
	outValue = value.GetValueAsUnsigned();
	return true;
}

// Map a new RWX region in the target, as close past the executable as we can get it.
bool LLDBDebugger::HotReserveHeap(uint64 minSize, String& outError)
{
	const uint64 mb = 1024 * 1024;
	uint64 reserveSize = BF_MAX(HOT_HEAP_RESERVE_SIZE, HotAlignUp(minSize, mb));

	if (mHotHeapNextHint == 0)
	{
		lldb::SBModule exeModule = mLLDBTarget.FindModule(mLLDBTarget.GetExecutable());
		uint64 imageEnd = 0;
		for (uint32 sectionIdx = 0; sectionIdx < exeModule.GetNumSections(); sectionIdx++)
		{
			lldb::SBSection section = exeModule.GetSectionAtIndex(sectionIdx);
			lldb::addr_t loadAddr = section.GetLoadAddress(mLLDBTarget);
			if (loadAddr != LLDB_INVALID_ADDRESS)
				imageEnd = BF_MAX(imageEnd, (uint64)loadAddr + section.GetByteSize());
		}
		if (imageEnd == 0)
		{
			outError = "unable to determine where the executable is loaded";
			return false;
		}
		mHotHeapNextHint = HotAlignUp(imageEnd + HOT_HEAP_EXE_GAP, mb);
	}

	for (int tryIdx = 0; tryIdx < 24; tryIdx++)
	{
		uint64 hint = mHotHeapNextHint;
		mHotHeapNextHint += reserveSize;

		// PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE
		String expr = StrFormat("(unsigned long)((void*(*)(void*, unsigned long, int, int, int, long))mmap)((void*)0x%llx, 0x%llx, 7, 0x100022, -1, 0)",
			(unsigned long long)hint, (unsigned long long)reserveSize);
		uint64 result = 0;
		if (!HotEvaluate(expr, result, outError))
			return false;

		if (result == hint)
		{
			mHotHeapStart = hint;
			mHotHeapSize = reserveSize;
			mHotHeapUsed = 0;
			return true;
		}

		// Kernels without MAP_FIXED_NOREPLACE treat the address as a hint and may place the mapping elsewhere
		if ((result != 0) && (result != (uint64)-1))
		{
			uint64 unmapResult = 0;
			String unmapExpr = StrFormat("(int)((int(*)(void*, unsigned long))munmap)((void*)0x%llx, 0x%llx)",
				(unsigned long long)result, (unsigned long long)reserveSize);
			HotEvaluate(unmapExpr, unmapResult, outError);
		}
	}

	outError = "unable to reserve memory near the executable";
	return false;
}

uint64 LLDBDebugger::HotAlloc(uint64 size, uint64 align, String& outError)
{
	uint64 addr = HotAlignUp(mHotHeapStart + mHotHeapUsed, align);
	if ((mHotHeapStart == 0) || (addr + size > mHotHeapStart + mHotHeapSize))
	{
		if (!HotReserveHeap(size + align, outError))
			return 0;
		addr = HotAlignUp(mHotHeapStart, align);
	}
	mHotHeapUsed = addr + size - mHotHeapStart;
	return addr;
}

// Find an external code or data symbol defined by the executable itself.
bool LLDBDebugger::HotFindExeSymbol(const StringImpl& name, HotSymbol& outSymbol)
{
	lldb::SBModule exeModule = mLLDBTarget.FindModule(mLLDBTarget.GetExecutable());
	if (!exeModule.IsValid())
		return false;

	lldb::SBSymbolContextList contexts = exeModule.FindSymbols(name.c_str());
	for (uint32 contextIdx = 0; contextIdx < contexts.GetSize(); contextIdx++)
	{
		lldb::SBSymbol symbol = contexts.GetContextAtIndex(contextIdx).GetSymbol();
		if ((!symbol.IsValid()) || (!symbol.IsExternal()))
			continue;
		lldb::SymbolType symbolType = symbol.GetType();
		if ((symbolType != lldb::eSymbolTypeCode) && (symbolType != lldb::eSymbolTypeData))
			continue;
		lldb::addr_t addr = symbol.GetStartAddress().GetLoadAddress(mLLDBTarget);
		if (addr == LLDB_INVALID_ADDRESS)
			continue;

		outSymbol.mAddr = (uint64)addr;
		outSymbol.mSize = symbol.GetSize();
		outSymbol.mIsCode = symbolType == lldb::eSymbolTypeCode;
		return true;
	}
	return false;
}

bool LLDBDebugger::HotFindCanonicalSymbol(const StringImpl& name, HotSymbol& outSymbol)
{
	HotSymbol* hotSymbol = NULL;
	if ((mHotPendingSymbols.TryGetValue(name, &hotSymbol)) || (mHotSymbols.TryGetValue(name, &hotSymbol)))
	{
		outSymbol = *hotSymbol;
		return true;
	}
	return HotFindExeSymbol(name, outSymbol);
}

// Resolve a symbol the object file doesn't define. Symbols outside the executable
// (libc etc.) are looked up with dlsym in the target, which gives us the same
// answer the dynamic linker would - including IFUNC-selected implementations.
// Returns false with an empty outError if the symbol simply doesn't exist.
bool LLDBDebugger::HotResolveExternal(const StringImpl& name, uint64& outAddr, String& outError)
{
	HotSymbol symbol;
	if (HotFindCanonicalSymbol(name, symbol))
	{
		outAddr = symbol.mAddr;
		return true;
	}

	uint64* cachedAddr = NULL;
	if (mHotExternalAddrs.TryGetValue(name, &cachedAddr))
	{
		outAddr = *cachedAddr;
		return true;
	}

	for (char c : name)
	{
		if ((c == '"') || (c == '\\'))
		{
			outError = StrFormat("unable to look up symbol '%s'", name.c_str());
			return false;
		}
	}

	// RTLD_DEFAULT is 0
	String expr = StrFormat("(unsigned long)((void*(*)(void*, const char*))dlsym)((void*)0, \"%s\")", name.c_str());
	uint64 addr = 0;
	if (!HotEvaluate(expr, addr, outError))
		return false;
	if (addr == 0)
		return false;

	mHotExternalAddrs[name] = addr;
	outAddr = addr;
	return true;
}

#ifdef __linux__
// A relocatable object being hot loaded. Objects in a batch are prepared (laid out, with their
// definitions registered) before any are linked, since they can reference each other - vdata,
// for instance, references methods that the batch's module objects define.
NS_BF_BEGIN

struct LLDBHotObject
{
	String mFileName;
	Array<uint8> mFileData;
	Elf64_Shdr* mShdrs;
	int mNumSections;
	Elf64_Sym* mSyms;
	int mNumSyms;
	const char* mStrTab;
	uint64 mStrTabSize;
	Array<int64> mSectionOffsets;   // -1 for sections we don't load
	uint64 mImageAddr;
	uint64 mGotOffset;
	uint64 mStubOffset;
	Dictionary<int, int> mGotSlots;
	Dictionary<int, int> mStubSlots;
	Array<uint8> mImage;
	Array<uint64> mSymAddrs;
	Array<uint8> mSymResolved;

	LLDBHotObject()
	{
		mShdrs = NULL;
		mNumSections = 0;
		mSyms = NULL;
		mNumSyms = 0;
		mStrTab = NULL;
		mStrTabSize = 0;
		mImageAddr = 0;
		mGotOffset = 0;
		mStubOffset = 0;
	}

	const char* GetSymName(int symIdx)
	{
		if (mSyms[symIdx].st_name >= mStrTabSize)
			return "";
		return mStrTab + mSyms[symIdx].st_name;
	}

	bool IsLoadedSection(int sectionIdx)
	{
		return (sectionIdx > 0) && (sectionIdx < mNumSections) && (mSectionOffsets[sectionIdx] != -1);
	}

	bool Fail(const StringImpl& error, String& outError)
	{
		outError = StrFormat("%s: %s", GetFileName(mFileName).c_str(), error.c_str());
		return false;
	}
};

NS_BF_END
#endif

bool LLDBDebugger::HotResolveObjectSymbol(LLDBHotObject* obj, int symIdx, Array<HotPatch>& patches, uint64& outAddr, String& outError)
{
#ifdef __linux__
	if (obj->mSymResolved[symIdx])
	{
		outAddr = obj->mSymAddrs[symIdx];
		return true;
	}

	Elf64_Sym& sym = obj->mSyms[symIdx];
	String name = obj->GetSymName(symIdx);
	int bind = ELF64_ST_BIND(sym.st_info);
	int symType = ELF64_ST_TYPE(sym.st_info);
	uint64 addr = 0;

	if (sym.st_shndx == SHN_UNDEF)
	{
		// 'bf_hs_prev@X' refers to the definition of X from before this hot compile
		String lookupName = name;
		if (lookupName.StartsWith("bf_hs_prev@"))
			lookupName.Remove(0, 11);

		String error;
		if (!HotResolveExternal(lookupName, addr, error))
		{
			if (!error.IsEmpty())
				return obj->Fail(error, outError);
			if (bind != STB_WEAK)
				return obj->Fail(StrFormat("unresolved symbol '%s'", name.c_str()), outError);
			addr = 0;
		}
	}
	else if (sym.st_shndx == SHN_ABS)
	{
		addr = sym.st_value;
	}
	else if (!obj->IsLoadedSection(sym.st_shndx))
	{
		return obj->Fail(StrFormat("symbol '%s' is in a section that can't be hot loaded (such as thread-local data)", name.c_str()), outError);
	}
	else
	{
		addr = obj->mImageAddr + obj->mSectionOffsets[sym.st_shndx] + sym.st_value;
		if ((bind != STB_LOCAL) && (!name.IsEmpty()))
		{
			bool isCode = (symType == STT_FUNC) || ((obj->mShdrs[sym.st_shndx].sh_flags & SHF_EXECINSTR) != 0);
			HotSymbol newSymbol;
			newSymbol.mAddr = addr;
			newSymbol.mSize = sym.st_size;
			newSymbol.mIsCode = isCode;

			// The compiler names data it wants replaced on every hot compile 'bf_hs_replace_*'
			// (vtable extension tables and the like)
			bool isReplace = name.Contains("bf_hs_replace_");
			bool isPendingDuplicate = mHotPendingSymbols.ContainsKey(name);

			HotSymbol canonical;
			if ((isReplace) && (!isPendingDuplicate))
			{
				mHotPendingSymbols[name] = newSymbol;
			}
			else if (HotFindCanonicalSymbol(name, canonical))
			{
				// Replacing a definition from the executable or an earlier hot load. A definition
				// pending from this same batch is a duplicate (e.g. a generic specialization emitted
				// in more than one object) and simply binds to the first copy.
				if (isPendingDuplicate)
				{
					addr = canonical.mAddr;
				}
				else if (isCode)
				{
					if (canonical.mAddr != addr)
					{
						HotPatch patch;
						patch.mName = name;
						patch.mOldAddr = canonical.mAddr;
						patch.mOldSize = canonical.mSize;
						patch.mNewAddr = addr;
						patch.mIncompatibleLambda = false;
						patches.Add(patch);
					}
					addr = canonical.mAddr;
				}
				else
				{
					// Data keeps its original storage, so existing objects and static state carry
					// over. Runtime type tables are updated in place afterwards (HotApplyDataFixups),
					// which is how new virtual method overrides and reflection data take effect.
					HotDataFixupKind fixupKind = HotDataFixupKind_None;
					if (name.Contains("sBfClassVData"))
						fixupKind = name.Contains(".vext") ? HotDataFixupKind_MergeVExt : HotDataFixupKind_MergeVData;
					else if (name.Contains("sBfTypeData"))
						fixupKind = HotDataFixupKind_CopyTypeData;
					else if (name.Contains("sStringLiterals"))
						fixupKind = HotDataFixupKind_LinkStringLiterals;

					bool sizeChanged = (canonical.mSize != 0) && (sym.st_size != 0) && (canonical.mSize != sym.st_size);
					if ((fixupKind == HotDataFixupKind_None) && (sizeChanged))
					{
						// The variable's type changed, so its old storage doesn't fit - use the new (zeroed) copy
						mHotPendingSymbols[name] = newSymbol;
					}
					else
					{
						if (fixupKind != HotDataFixupKind_None)
						{
							HotDataFixup fixup;
							fixup.mKind = fixupKind;
							fixup.mName = name;
							fixup.mOldAddr = canonical.mAddr;
							fixup.mOldSize = canonical.mSize;
							fixup.mNewAddr = addr;
							fixup.mNewSize = sym.st_size;
							mHotPendingDataFixups.Add(fixup);
						}

						if (fixupKind == HotDataFixupKind_MergeVExt)
							mHotPendingSymbols[name] = newSymbol;
						else
							addr = canonical.mAddr;
					}
				}
			}
			else
			{
				mHotPendingSymbols[name] = newSymbol;
			}
		}
	}

	obj->mSymAddrs[symIdx] = addr;
	obj->mSymResolved[symIdx] = 1;
	outAddr = addr;
	return true;
#else
	return false;
#endif
}

// Parse the object, lay out and allocate its image, and register its global definitions.
bool LLDBDebugger::HotPrepareObject(LLDBHotObject* obj, Array<HotPatch>& patches, String& outError)
{
#ifdef __linux__
	{
		int fileSize = 0;
		uint8* rawData = LoadBinaryData(obj->mFileName, &fileSize);
		if ((rawData == NULL) || (fileSize <= 0))
		{
			delete[] rawData;
			outError = StrFormat("unable to read '%s'", obj->mFileName.c_str());
			return false;
		}
		obj->mFileData.Resize(fileSize);
		memcpy(obj->mFileData.mVals, rawData, fileSize);
		delete[] rawData;
	}

	uint64 fileSize = (uint64)obj->mFileData.size();
	uint8* data = obj->mFileData.mVals;
	if (fileSize < sizeof(Elf64_Ehdr))
		return obj->Fail("not an ELF object file", outError);
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)data;
	if ((memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) || (ehdr->e_ident[EI_CLASS] != ELFCLASS64) ||
		(ehdr->e_type != ET_REL) || (ehdr->e_machine != EM_X86_64))
		return obj->Fail("not an x86-64 ELF relocatable object", outError);
	if ((ehdr->e_shoff == 0) || (ehdr->e_shentsize != sizeof(Elf64_Shdr)) || (ehdr->e_shnum == 0) ||
		(ehdr->e_shoff + (uint64)ehdr->e_shnum * sizeof(Elf64_Shdr) > fileSize))
		return obj->Fail("invalid section header table", outError);

	obj->mShdrs = (Elf64_Shdr*)(data + ehdr->e_shoff);
	obj->mNumSections = ehdr->e_shnum;
	int numSections = obj->mNumSections;
	Elf64_Shdr* shdrs = obj->mShdrs;
	for (int sectionIdx = 0; sectionIdx < numSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = shdrs[sectionIdx];
		if ((shdr.sh_type != SHT_NOBITS) && (shdr.sh_offset + shdr.sh_size > fileSize))
			return obj->Fail("section extends past the end of the file", outError);
	}

	// Symbol table
	for (int sectionIdx = 0; sectionIdx < numSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = shdrs[sectionIdx];
		if (shdr.sh_type != SHT_SYMTAB)
			continue;
		if (shdr.sh_link >= (uint32)numSections)
			return obj->Fail("invalid symbol string table", outError);
		obj->mSyms = (Elf64_Sym*)(data + shdr.sh_offset);
		obj->mNumSyms = (int)(shdr.sh_size / sizeof(Elf64_Sym));
		obj->mStrTab = (const char*)(data + shdrs[shdr.sh_link].sh_offset);
		obj->mStrTabSize = shdrs[shdr.sh_link].sh_size;
		break;
	}
	if (obj->mSyms == NULL)
		return obj->Fail("no symbol table", outError);
	int numSyms = obj->mNumSyms;

	// Lay out the sections we load. Thread-local templates can't be hot loaded, and
	// we don't register unwind info for hot code, so both are left out.
	obj->mSectionOffsets.Resize(numSections);
	uint64 imageSize = 0;
	uint64 imageAlign = 16;
	for (int sectionIdx = 0; sectionIdx < numSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = shdrs[sectionIdx];
		obj->mSectionOffsets[sectionIdx] = -1;
		if (((shdr.sh_flags & SHF_ALLOC) == 0) || ((shdr.sh_flags & SHF_TLS) != 0) || (shdr.sh_type == SHT_X86_64_UNWIND))
			continue;
		uint64 align = BF_MAX((uint64)shdr.sh_addralign, (uint64)1);
		if ((align & (align - 1)) != 0)
			return obj->Fail("invalid section alignment", outError);
		imageAlign = BF_MAX(imageAlign, align);
		imageSize = HotAlignUp(imageSize, align);
		obj->mSectionOffsets[sectionIdx] = (int64)imageSize;
		imageSize += shdr.sh_size;
	}

	// Reserve GOT slots for GOT-relative relocations, and call stubs for calls to
	// undefined symbols in case they resolve beyond rel32 range (e.g. into libc).
	for (int sectionIdx = 0; sectionIdx < numSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = shdrs[sectionIdx];
		if ((shdr.sh_type == SHT_REL) && (obj->IsLoadedSection((int)shdr.sh_info)))
			return obj->Fail("SHT_REL relocations are not supported", outError);
		if ((shdr.sh_type != SHT_RELA) || (!obj->IsLoadedSection((int)shdr.sh_info)))
			continue;

		Elf64_Rela* relas = (Elf64_Rela*)(data + shdr.sh_offset);
		int numRelas = (int)(shdr.sh_size / sizeof(Elf64_Rela));
		for (int relaIdx = 0; relaIdx < numRelas; relaIdx++)
		{
			uint32 relocType = ELF64_R_TYPE(relas[relaIdx].r_info);
			int symIdx = (int)ELF64_R_SYM(relas[relaIdx].r_info);
			if ((symIdx < 0) || (symIdx >= numSyms))
				return obj->Fail("relocation references an invalid symbol", outError);
			if ((relocType == R_X86_64_GOTPCREL) || (relocType == R_X86_64_GOTPCRELX) || (relocType == R_X86_64_REX_GOTPCRELX))
				obj->mGotSlots.TryAdd(symIdx, (int)obj->mGotSlots.GetCount());
			else if ((relocType == R_X86_64_PLT32) && (obj->mSyms[symIdx].st_shndx == SHN_UNDEF))
				obj->mStubSlots.TryAdd(symIdx, (int)obj->mStubSlots.GetCount());
		}
	}

	obj->mGotOffset = HotAlignUp(imageSize, 8);
	imageSize = obj->mGotOffset + obj->mGotSlots.GetCount() * 8;
	obj->mStubOffset = HotAlignUp(imageSize, HOT_STUB_SIZE);
	imageSize = obj->mStubOffset + obj->mStubSlots.GetCount() * HOT_STUB_SIZE;

	obj->mImageAddr = HotAlloc(BF_MAX(imageSize, (uint64)1), imageAlign, outError);
	if (obj->mImageAddr == 0)
		return false;

	obj->mImage.Resize((intptr)imageSize);
	if (imageSize > 0)
		memset(obj->mImage.mVals, 0, (size_t)imageSize);
	for (int sectionIdx = 0; sectionIdx < numSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = shdrs[sectionIdx];
		if ((obj->IsLoadedSection(sectionIdx)) && (shdr.sh_type != SHT_NOBITS) && (shdr.sh_size > 0))
			memcpy(obj->mImage.mVals + obj->mSectionOffsets[sectionIdx], data + shdr.sh_offset, (size_t)shdr.sh_size);
	}

	obj->mSymAddrs.Resize(numSyms);
	obj->mSymResolved.Resize(numSyms);
	if (numSyms > 0)
		memset(obj->mSymResolved.mVals, 0, numSyms);

	// Register every global definition, so other objects in the batch can resolve against
	// them and replaced functions get patched even when nothing in this object references them.
	for (int symIdx = 1; symIdx < numSyms; symIdx++)
	{
		Elf64_Sym& sym = obj->mSyms[symIdx];
		if ((ELF64_ST_BIND(sym.st_info) == STB_LOCAL) || (!obj->IsLoadedSection(sym.st_shndx)))
			continue;
		uint64 addr;
		if (!HotResolveObjectSymbol(obj, symIdx, patches, addr, outError))
			return false;
	}
	return true;
#else
	outError = "hot swapping is only supported on Linux";
	return false;
#endif
}

// Apply the object's relocations and write its image into the target.
bool LLDBDebugger::HotLinkObject(LLDBHotObject* obj, Array<HotPatch>& patches, String& outError)
{
#ifdef __linux__
	uint8* data = obj->mFileData.mVals;
	for (int relaSectionIdx = 0; relaSectionIdx < obj->mNumSections; relaSectionIdx++)
	{
		Elf64_Shdr& relaShdr = obj->mShdrs[relaSectionIdx];
		int targetIdx = (int)relaShdr.sh_info;
		if ((relaShdr.sh_type != SHT_RELA) || (!obj->IsLoadedSection(targetIdx)))
			continue;
		Elf64_Shdr& targetShdr = obj->mShdrs[targetIdx];

		Elf64_Rela* relas = (Elf64_Rela*)(data + relaShdr.sh_offset);
		int numRelas = (int)(relaShdr.sh_size / sizeof(Elf64_Rela));
		for (int relaIdx = 0; relaIdx < numRelas; relaIdx++)
		{
			Elf64_Rela& rela = relas[relaIdx];
			uint32 relocType = ELF64_R_TYPE(rela.r_info);
			int symIdx = (int)ELF64_R_SYM(rela.r_info);
			if (relocType == R_X86_64_NONE)
				continue;

			int relocSize = ((relocType == R_X86_64_64) || (relocType == R_X86_64_PC64)) ? 8 : 4;
			if ((targetShdr.sh_type == SHT_NOBITS) || (rela.r_offset + relocSize > targetShdr.sh_size))
				return obj->Fail("relocation is outside of its section", outError);

			uint64 symAddr;
			if (!HotResolveObjectSymbol(obj, symIdx, patches, symAddr, outError))
				return false;

			uint8* loc = obj->mImage.mVals + obj->mSectionOffsets[targetIdx] + rela.r_offset;
			uint64 P = obj->mImageAddr + obj->mSectionOffsets[targetIdx] + rela.r_offset;
			uint64 S = symAddr;
			int64 A = rela.r_addend;
			const char* symName = obj->GetSymName(symIdx);

			switch (relocType)
			{
			case R_X86_64_64:
				{
					uint64 val = S + A;
					memcpy(loc, &val, 8);
				}
				break;
			case R_X86_64_PC64:
				{
					uint64 val = S + A - P;
					memcpy(loc, &val, 8);
				}
				break;
			case R_X86_64_32:
				{
					uint64 val = S + A;
					if (val > 0xFFFFFFFFULL)
						return obj->Fail(StrFormat("R_X86_64_32 relocation against '%s' is out of range", symName), outError);
					uint32 val32 = (uint32)val;
					memcpy(loc, &val32, 4);
				}
				break;
			case R_X86_64_32S:
				{
					int64 val = (int64)(S + A);
					if (!HotFitsInt32(val))
						return obj->Fail(StrFormat("R_X86_64_32S relocation against '%s' is out of range", symName), outError);
					int32 val32 = (int32)val;
					memcpy(loc, &val32, 4);
				}
				break;
			case R_X86_64_PC32:
			case R_X86_64_PLT32:
				{
					int64 val = (int64)(S + A - P);
					int* stubSlot = NULL;
					if ((!HotFitsInt32(val)) && (relocType == R_X86_64_PLT32) && (obj->mStubSlots.TryGetValue(symIdx, &stubSlot)))
					{
						uint64 stubImageOffset = obj->mStubOffset + (uint64)*stubSlot * HOT_STUB_SIZE;
						HotWriteAbsJump(obj->mImage.mVals + stubImageOffset, S);
						val = (int64)(obj->mImageAddr + stubImageOffset + A - P);
					}
					if (!HotFitsInt32(val))
						return obj->Fail(StrFormat("PC-relative relocation against '%s' is out of range", symName), outError);
					int32 val32 = (int32)val;
					memcpy(loc, &val32, 4);
				}
				break;
			case R_X86_64_GOTPCREL:
			case R_X86_64_GOTPCRELX:
			case R_X86_64_REX_GOTPCRELX:
				{
					uint64 slotImageOffset = obj->mGotOffset + (uint64)obj->mGotSlots[symIdx] * 8;
					memcpy(obj->mImage.mVals + slotImageOffset, &S, 8);
					int64 val = (int64)(obj->mImageAddr + slotImageOffset + A - P);
					if (!HotFitsInt32(val))
						return obj->Fail(StrFormat("GOT relocation against '%s' is out of range", symName), outError);
					int32 val32 = (int32)val;
					memcpy(loc, &val32, 4);
				}
				break;
			case R_X86_64_TLSGD:
			case R_X86_64_TLSLD:
			case R_X86_64_DTPOFF32:
			case R_X86_64_DTPOFF64:
			case R_X86_64_GOTTPOFF:
			case R_X86_64_TPOFF32:
			case R_X86_64_TPOFF64:
				return obj->Fail(StrFormat("thread-local variable '%s' can't be hot loaded yet", symName), outError);
			default:
				return obj->Fail(StrFormat("unsupported relocation type %d against '%s'", relocType, symName), outError);
			}
		}
	}

	uint64 imageSize = (uint64)obj->mImage.size();
	if ((imageSize > 0) && (!WriteMemory((intptr)obj->mImageAddr, obj->mImage.mVals, imageSize)))
		return obj->Fail(StrFormat("failed writing %lld bytes to 0x%llx", (long long)imageSize, (unsigned long long)obj->mImageAddr), outError);
	return true;
#else
	outError = "hot swapping is only supported on Linux";
	return false;
#endif
}

// Let LLDB see hot-loaded code: add the object as a module and load its executable sections where
// we put them, so breakpoints, stepping, call stacks and locals work in it. LLDB lays out a relocatable
// object's sections itself (ignoring sh_addr) and applies its debug info relocations. Data sections are
// left unloaded, since the object's data binds to the original storage.
void LLDBDebugger::HotRegisterDebugInfo(LLDBHotObject* obj, int hotIdx)
{
#ifdef __linux__
	// The IDE overwrites the object on the next hot compile, so LLDB gets its own copy
	String path = StrFormat("/tmp/BeefHot_%d_%d_%s", (int)getpid(), hotIdx, GetFileName(obj->mFileName).c_str());
	FILE* fp = fopen(path.c_str(), "wb");
	if (fp == NULL)
		return;
	bool written = fwrite(obj->mFileData.mVals, 1, (size_t)obj->mFileData.size(), fp) == (size_t)obj->mFileData.size();
	fclose(fp);
	mHotModulePaths.Add(path);
	if (!written)
		return;

	// LLDB would otherwise index the new module's symbols on background threads, which races with
	// evaluations the IDE makes right after the hot load and can crash liblldb
	lldb::SBCommandReturnObject commandResult;
	mLLDBDebugger.GetCommandInterpreter().HandleCommand("settings set target.preload-symbols false", commandResult);
	lldb::SBModule module = mLLDBTarget.AddModule(path.c_str(), NULL, NULL);
	mLLDBDebugger.GetCommandInterpreter().HandleCommand("settings set target.preload-symbols true", commandResult);
	if (!module.IsValid())
	{
		LLDBLog("HotRegisterDebugInfo: LLDB rejected '%s'\n", path.c_str());
		return;
	}

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)obj->mFileData.mVals;
	if (ehdr->e_shstrndx >= obj->mNumSections)
		return;
	Elf64_Shdr& shStrTabShdr = obj->mShdrs[ehdr->e_shstrndx];
	const char* shStrTab = (const char*)obj->mFileData.mVals + shStrTabShdr.sh_offset;

	// LLDB lists the sections in ELF order, without the null section
	int numLoaded = 0;
	for (int sectionIdx = 1; sectionIdx < obj->mNumSections; sectionIdx++)
	{
		Elf64_Shdr& shdr = obj->mShdrs[sectionIdx];
		if ((!obj->IsLoadedSection(sectionIdx)) || ((shdr.sh_flags & SHF_EXECINSTR) == 0) || (shdr.sh_name >= shStrTabShdr.sh_size))
			continue;
		lldb::SBSection section = module.GetSectionAtIndex(sectionIdx - 1);
		const char* sectionName = section.GetName();
		if ((sectionName == NULL) || (strcmp(sectionName, shStrTab + shdr.sh_name) != 0))
		{
			LLDBLog("HotRegisterDebugInfo: section %d of '%s' doesn't match\n", sectionIdx, path.c_str());
			continue;
		}
		if (mLLDBTarget.SetSectionLoadAddress(section, obj->mImageAddr + obj->mSectionOffsets[sectionIdx]).Success())
			numLoaded++;
	}
	LLDBLog("HotRegisterDebugInfo: %s, %d code sections\n", path.c_str(), numLoaded);

	if ((mHotVersions.IsEmpty()) || (mHotVersions.back().mHotIdx != hotIdx))
	{
		HotVersion version;
		version.mHotIdx = hotIdx;
		mHotVersions.Add(version);
	}
	mHotVersions.back().mModules.Add(module);
#endif
}

void LLDBDebugger::HotRemoveDebugInfo()
{
#ifdef __linux__
	for (auto& path : mHotModulePaths)
		unlink(path.c_str());
#endif
	mHotModulePaths.Clear();
	mHotVersions.Clear();
}

// Update the runtime's type tables in place, as WinDebugger does (DbgModule::ProcessHotSwapVariables).
bool LLDBDebugger::HotApplyDataFixups(String& outError)
{
	for (auto& fixup : mHotPendingDataFixups)
	{
		uint64 oldSize = (fixup.mOldSize != 0) ? fixup.mOldSize : fixup.mNewSize;
		switch (fixup.mKind)
		{
		case HotDataFixupKind_MergeVData:
		case HotDataFixupKind_MergeVExt:
			{
				// The table can't grow in place (new virtuals go through extension tables), so merge
				// what fits. Removed virtual methods leave 0s in the new table - keep the old entries there.
				uint64 size = BF_MIN(oldSize, fixup.mNewSize) & ~(uint64)7;
				Array<uint64> oldData;
				Array<uint64> newData;
				oldData.Resize((intptr)(size / 8));
				newData.Resize((intptr)(size / 8));
				if ((size > 0) && ((!ReadMemory((intptr)fixup.mOldAddr, size, oldData.mVals)) || (!ReadMemory((intptr)fixup.mNewAddr, size, newData.mVals))))
				{
					outError = StrFormat("failed reading vtable '%s'", fixup.mName.c_str());
					return false;
				}
				for (intptr wordIdx = 0; wordIdx < oldData.size(); wordIdx++)
				{
					if (newData[wordIdx] != 0)
						oldData[wordIdx] = newData[wordIdx];
				}
				if ((size > 0) && (!WriteMemory((intptr)fixup.mOldAddr, oldData.mVals, size)))
				{
					outError = StrFormat("failed updating vtable '%s'", fixup.mName.c_str());
					return false;
				}
				// Extension tables are used at their new address from now on, so they get the merged data too
				if ((fixup.mKind == HotDataFixupKind_MergeVExt) && (size > 0))
					WriteMemory((intptr)fixup.mNewAddr, oldData.mVals, size);
			}
			break;
		case HotDataFixupKind_CopyTypeData:
			{
				if (fixup.mNewSize != oldSize)
				{
					LLDBLog("HotApplyDataFixups: size of '%s' changed (%lld -> %lld), not updated\n", fixup.mName.c_str(), (long long)oldSize, (long long)fixup.mNewSize);
					break;
				}
				Array<uint8> data;
				data.Resize((intptr)fixup.mNewSize);
				if ((fixup.mNewSize > 0) &&
					((!ReadMemory((intptr)fixup.mNewAddr, fixup.mNewSize, data.mVals)) || (!WriteMemory((intptr)fixup.mOldAddr, data.mVals, fixup.mNewSize))))
				{
					outError = StrFormat("failed updating type data '%s'", fixup.mName.c_str());
					return false;
				}
			}
			break;
		case HotDataFixupKind_LinkStringLiterals:
			{
				// The first word of each string literal table links to the next (newer) table
				uint64 prevLink = 0;
				if ((!ReadMemory((intptr)fixup.mOldAddr, 8, &prevLink)) ||
					(!WriteMemory((intptr)fixup.mNewAddr, &prevLink, 8)) ||
					(!WriteMemory((intptr)fixup.mOldAddr, &fixup.mNewAddr, 8)))
				{
					outError = StrFormat("failed linking string literal table '%s'", fixup.mName.c_str());
					return false;
				}
			}
			break;
		default:
			break;
		}
	}
	return true;
}

// Where HotApplyPatches puts the jump to the new version of a method, and its size ('jmp rel32' when the
// new code is in range, else an absolute jump). Returns false if the old method is too small to patch.
//
// LLDB's step-in runs to the end of the prologue of the method it steps into, using a breakpoint.
// If that address fell inside our jump, the breakpoint would corrupt it. So when the prologue is shorter
// than the jump, the jump goes at the end of the prologue (where LLDB's breakpoint then sits on its
// first byte, which LLDB handles) and the entry gets a short jump to it.
bool LLDBDebugger::HotGetPatchLayout(const HotPatch& patch, uint64& outJmpAddr, int& outJmpSize, uint64* outPrologueSize)
{
	uint64 prologueSize = 0;
	lldb::SBFunction function = mLLDBTarget.ResolveLoadAddress(patch.mOldAddr).GetFunction();
	if ((function.IsValid()) && ((uint64)function.GetStartAddress().GetLoadAddress(mLLDBTarget) == patch.mOldAddr))
		prologueSize = function.GetPrologueByteSize();

	outJmpAddr = patch.mOldAddr;
	for (int pass = 0; pass < 2; pass++)
	{
		int64 rel = (int64)(patch.mNewAddr - (outJmpAddr + HOT_JMP_REL32_SIZE));
		outJmpSize = HotFitsInt32(rel) ? HOT_JMP_REL32_SIZE : HOT_JMP_ABS64_SIZE;
		// 'jmp rel8' needs 2 bytes, and its target must stay in range
		if ((pass == 0) && (prologueSize >= 2) && (prologueSize < (uint64)outJmpSize) && (prologueSize <= 127))
			outJmpAddr = patch.mOldAddr + prologueSize;
		else
			break;
	}
	if (outPrologueSize != NULL)
		*outPrologueSize = prologueSize;
	return outJmpAddr + outJmpSize <= patch.mOldAddr + patch.mOldSize;
}

// A thread stopped part-way through the bytes we're about to overwrite would resume
// into a torn instruction, so single-step any such thread until it's clear.
// (A thread exactly at a function's entry is fine - it will execute the new jump.)
bool LLDBDebugger::HotStepThreadsPastPatches(const Array<HotPatch>& patches, String& outError)
{
	for (uint32 threadIdx = 0; threadIdx < mLLDBProcess.GetNumThreads(); threadIdx++)
	{
		lldb::SBThread thread = mLLDBProcess.GetThreadAtIndex(threadIdx);
		for (int stepIdx = 0; true; stepIdx++)
		{
			uint64 pc = (uint64)thread.GetFrameAtIndex(0).GetPC();
			bool inPatch = false;
			for (auto& patch : patches)
			{
				uint64 jmpAddr;
				int jmpSize;
				if ((!patch.mIncompatibleLambda) && (HotGetPatchLayout(patch, jmpAddr, jmpSize, NULL)) && (pc > patch.mOldAddr) && (pc < jmpAddr + jmpSize))
					inPatch = true;
			}
			if (!inPatch)
				break;

			if (stepIdx >= 16)
			{
				outError = StrFormat("unable to move thread %d past the start of a replaced method", (int)thread.GetThreadID());
				return false;
			}

			lldb::SBError error;
			thread.StepInstruction(false, error);
			if (error.Fail())
			{
				outError = StrFormat("failed to step thread %d: %s", (int)thread.GetThreadID(), error.GetCString());
				return false;
			}
			if (!HotWaitForStop(outError))
				return false;
		}
	}
	return true;
}

bool LLDBDebugger::HotApplyPatches(const Array<HotPatch>& patches, int& outNumPatched, String& outError)
{
	outNumPatched = 0;
	HashSet<uint64> patchedAddrs;
	for (intptr patchIdx = patches.size() - 1; patchIdx >= 0; patchIdx--)
	{
		auto& patch = patches[patchIdx];
		if (!patchedAddrs.Add(patch.mOldAddr))
			continue;

		// Leave the old version in place, but stop with an error if it's ever called (as WinDebugger does)
		if (patch.mIncompatibleLambda)
		{
			// One-shot: LLDB removes it after the hit (deleting it ourselves while the thread sits on it
			// confuses LLDB's stepping off the breakpoint site)
			lldb::SBBreakpoint trap = mLLDBTarget.BreakpointCreateByAddress(patch.mOldAddr);
			if (trap.IsValid())
			{
				trap.SetOneShot(true);
				mHotInvalidLambdaTrapIds.Add((int)trap.GetID());
			}
			continue;
		}

		uint64 jmpAddr;
		int jmpSize;
		uint64 prologueSize = 0;
		if (!HotGetPatchLayout(patch, jmpAddr, jmpSize, &prologueSize))
		{
			// Single-byte 'ret' stubs can't be patched, but there's nothing in them to replace
			if (patch.mOldSize > 1)
				OutputMessage(StrFormat("Hot swap: method '%s' is too small to replace\n", patch.mName.c_str()));
			continue;
		}

		uint8 jmp[HOT_JMP_ABS64_SIZE];
		if (jmpSize == HOT_JMP_REL32_SIZE)
		{
			int32 rel32 = (int32)(int64)(patch.mNewAddr - (jmpAddr + HOT_JMP_REL32_SIZE));
			jmp[0] = 0xE9;
			memcpy(jmp + 1, &rel32, 4);
		}
		else
			HotWriteAbsJump(jmp, patch.mNewAddr);

		bool written = WriteMemory((intptr)jmpAddr, jmp, jmpSize);
		if ((written) && (jmpAddr != patch.mOldAddr))
		{
			uint8 shortJmp[2] = { 0xEB, (uint8)(int8)(jmpAddr - (patch.mOldAddr + 2)) };
			written = WriteMemory((intptr)patch.mOldAddr, shortJmp, 2);
		}
		if (!written)
		{
			outError = StrFormat("failed to patch '%s' at 0x%llx", patch.mName.c_str(), (unsigned long long)patch.mOldAddr);
			return false;
		}
		LLDBLog("Patched %s: %llx -> %llx (jump at %llx, %d bytes)\n", patch.mName.c_str(), (unsigned long long)patch.mOldAddr,
			(unsigned long long)patch.mNewAddr, (unsigned long long)jmpAddr, jmpSize);
		HotPatchedEntry patchedEntry;
		patchedEntry.mNewAddr = patch.mNewAddr;
		patchedEntry.mJmpAddr = jmpAddr;
		patchedEntry.mEndAddr = jmpAddr + jmpSize;
		// LLDB's step-in runs to the end of the old prologue. Unless that's where our jump is, it's never
		// reached, so stepping in needs a trap on the new version (see HotSetStepTraps)
		patchedEntry.mNeedsStepTrap = (prologueSize > 0) && (patch.mOldAddr + prologueSize != jmpAddr);
		mHotPatchedEntries[patch.mOldAddr] = patchedEntry;
		outNumPatched++;
	}
	return true;
}

void LLDBDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mLLDBProcess.IsValid())
		return;

	// Handle any stop that's already pending (a breakpoint hit, say) before deciding
	// whether we need to interrupt the target ourselves.
	Update();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated))
		return;

	String error;
	bool wasRunning = (mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint);
	if (wasRunning)
	{
		mLLDBProcess.Stop();
		if (!HotWaitForStop(error))
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Hot swap failed: %s", error.c_str()));
			return;
		}
	}

	// Load every object before patching anything, so a failure leaves the program untouched
	Array<HotPatch> patches;
	Array<LLDBHotObject*> objects;
	bool success = true;
	mHotPendingSymbols.Clear();
	mHotPendingDataFixups.Clear();
	for (auto& fileName : objectFiles)
	{
		LLDBHotObject* obj = new LLDBHotObject();
		obj->mFileName = fileName;
		objects.Add(obj);
		if (!HotPrepareObject(obj, patches, error))
		{
			success = false;
			break;
		}
	}
	if (success)
	{
		for (auto obj : objects)
		{
			if (!HotLinkObject(obj, patches, error))
			{
				success = false;
				break;
			}
		}
	}
	if (success)
	{
		for (auto obj : objects)
			HotRegisterDebugInfo(obj, hotIdx);
	}
	for (auto obj : objects)
		delete obj;

	if (success)
		success = HotApplyDataFixups(error);
	mHotPendingDataFixups.Clear();

	// Only publish new definitions once the whole batch is in the target
	if (success)
	{
		for (auto& kv : mHotPendingSymbols)
			mHotSymbols[kv.mKey] = kv.mValue;
	}
	mHotPendingSymbols.Clear();

	int numPatched = 0;
	if (success)
		HotCheckLambdaCaptures(patches);
	if (success)
		success = HotStepThreadsPastPatches(patches, error);
	if (success)
		success = HotApplyPatches(patches, numPatched, error);

	if (success)
		OutputMessage(StrFormat("Hot swap: replaced %d method%s\n", numPatched, (numPatched == 1) ? "" : "s"));
	else
		mDebugManager->mOutMessages.push_back(StrFormat("error Hot swap failed: %s", error.c_str()));
	LLDBLog("HotLoad %d: %d objects, %s, %d patched%s%s\n", hotIdx, (int)objectFiles.size(), success ? "succeeded" : "failed",
		numPatched, success ? "" : ": ", success ? "" : error.c_str());

	// The IDE unbinds every breakpoint (RehupBreakpoints) before calling HotLoad and relies on
	// us to rebind them afterwards
	for (auto bp : mBreakpoints)
		CheckBreakpoint(bp);

	if ((wasRunning) && (mLLDBProcess.IsValid()) && (mLLDBProcess.GetState() == lldb::eStateStopped))
		mLLDBProcess.Continue();
	else
		ClearCallStack();
}

// After a hot compile the IDE waits for this data before it calls HotLoad.
void LLDBDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	delete mHotResolveData;
	mHotResolveData = new DbgHotResolveData();

	if (!mLLDBProcess.IsValid())
		return;
	Update();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated))
		return;

	String error;
	bool wasRunning = (mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint);
	if (wasRunning)
	{
		mLLDBProcess.Stop();
		if (!HotWaitForStop(error))
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Hot resolve failed: %s", error.c_str()));
			return;
		}
	}

	// A method's mangled name, with the compile it's from when that's a hot compile (as WinDebugger reports
	// them), so the compiler can tell which version of the method it is
	auto _GetMethodEntry = [&](lldb::SBAddress addr, bool requireStart, String& outEntry)
	{
		lldb::SBSymbol symbol = addr.GetSymbol();
		if (!symbol.IsValid())
			return false;
		if ((requireStart) && (symbol.GetStartAddress().GetLoadAddress(mLLDBTarget) != addr.GetLoadAddress(mLLDBTarget)))
			return false;
		const char* name = symbol.GetMangledName();
		if (name == NULL)
			name = symbol.GetName();
		if ((name == NULL) || (name[0] == '\0'))
			return false;
		outEntry = name;
		int hotIdx = HotGetModuleVersion(addr.GetModule());
		if (hotIdx != 0)
			outEntry += StrFormat("\t%d", hotIdx);
		return true;
	};

	// Methods on any thread's stack
	String entry;
	for (uint32 threadIdx = 0; threadIdx < mLLDBProcess.GetNumThreads(); threadIdx++)
	{
		lldb::SBThread thread = mLLDBProcess.GetThreadAtIndex(threadIdx);
		for (uint32 frameIdx = 0; frameIdx < thread.GetNumFrames(); frameIdx++)
		{
			if (_GetMethodEntry(thread.GetFrameAtIndex(frameIdx).GetPCAddress(), false, entry))
				mHotResolveData->mBeefCallStackEntries.Add(entry);
		}
	}

	// Methods that live delegates point to (gBfLiveDelegates in the runtime), which can still be called
	if ((flags & DbgHotResolveFlag_Allocations) != 0)
	{
		struct LiveDelegateTable
		{
			uint64 mEntries;
			int32 mCapacity;
			int32 mCount;
			int32 mUsed;
		};
		HotSymbol tableSymbol;
		LiveDelegateTable table = {};
		if ((HotFindExeSymbol("gBfLiveDelegates", tableSymbol)) && (ReadMemory((intptr)tableSymbol.mAddr, sizeof(table), &table)) &&
			(table.mEntries != 0) && (table.mCapacity > 0) && (table.mCapacity <= 0x1000000))
		{
			Array<uint64> objects;
			objects.Resize(table.mCapacity);
			if (ReadMemory((intptr)table.mEntries, objects.size() * sizeof(uint64), objects.mVals))
			{
				// A delegate's function pointer follows the object header (vdata and debug info)
				const int objectHeaderSize = sizeof(uint64) * 2;
				for (auto object : objects)
				{
					uint64 funcPtr = 0;
					if ((object <= 1) || (!ReadMemory((intptr)(object + objectHeaderSize), sizeof(funcPtr), &funcPtr)) || (funcPtr == 0))
						continue;
					if (_GetMethodEntry(mLLDBTarget.ResolveLoadAddress(funcPtr), true, entry))
						mHotResolveData->mBeefCallStackEntries.Add("D " + entry);
				}
			}
		}
	}

	// Which types have live heap allocations, so the compiler can tell whether a layout change is safe.
	// On Windows the debugger scans the GC's heap; without one, the runtime keeps a count per type
	// (gBfLiveTypeCounts). If that's unavailable, report every type as allocated, so the compiler refuses
	// layout changes rather than applying them to live objects with the old layout.
	if ((flags & DbgHotResolveFlag_Allocations) != 0)
	{
		bool haveCounts = false;
		HotSymbol countsSymbol;
		HotSymbol overflowSymbol;
		if ((HotFindExeSymbol("gBfLiveTypeCounts", countsSymbol)) && (HotFindExeSymbol("gBfLiveTypeCountOverflow", overflowSymbol)) &&
			(countsSymbol.mSize >= sizeof(int32)))
		{
			int32 overflow = 1;
			Array<int32> counts;
			counts.Resize((intptr)(countsSymbol.mSize / sizeof(int32)));
			if ((ReadMemory((intptr)overflowSymbol.mAddr, sizeof(overflow), &overflow)) && (overflow == 0) &&
				(ReadMemory((intptr)countsSymbol.mAddr, counts.size() * sizeof(int32), counts.mVals)))
			{
				haveCounts = true;
				for (intptr typeId = 0; typeId < counts.size(); typeId++)
				{
					if (counts[typeId] <= 0)
						continue;
					while (mHotResolveData->mTypeData.size() <= typeId)
						mHotResolveData->mTypeData.Add(DbgHotResolveData::TypeData());
					mHotResolveData->mTypeData[typeId].mCount = counts[typeId];
				}
			}
		}

		if (!haveCounts)
		{
			HotSymbol typeCountSymbol;
			int32 typeCount = 0;
			if (HotFindExeSymbol("_ZN2bf6System4Type10sTypeCountE", typeCountSymbol))
				ReadMemory((intptr)typeCountSymbol.mAddr, sizeof(typeCount), &typeCount);
			for (int typeId = 0; typeId < typeCount; typeId++)
			{
				DbgHotResolveData::TypeData typeData;
				typeData.mCount = 1;
				mHotResolveData->mTypeData.Add(typeData);
			}
		}
		LLDBLog("InitiateHotResolve: %s\n", haveCounts ? "using the runtime's live type counts" : "no live type counts, reporting all types in use");
	}

	LLDBLog("InitiateHotResolve flags:%d: %d active methods, %d types reported\n", (int)flags,
		(int)mHotResolveData->mBeefCallStackEntries.size(), (int)mHotResolveData->mTypeData.size());

	if ((wasRunning) && (mLLDBProcess.IsValid()) && (mLLDBProcess.GetState() == lldb::eStateStopped))
		mLLDBProcess.Continue();
	else
		ClearCallStack();
}

intptr LLDBDebugger::GetDbgAllocHeapSize()
{
	return intptr();
}

String LLDBDebugger::GetDbgAllocInfo()
{
	return String();
}

//----------------------------------------------------------------------------
// Shutdown
//----------------------------------------------------------------------------

void LLDBDebugger::StopDebugging()
{
	WaitForLaunchThread();

	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mLLDBProcess.IsValid())
	{
		mLLDBProcess.Destroy();
		mLLDBProcess = lldb::SBProcess();
	}

	if (mLLDBTarget.IsValid())
	{
		mLLDBDebugger.DeleteTarget(mLLDBTarget);
		mLLDBTarget = lldb::SBTarget();
	}

	if (mLLDBDebugger.IsValid())
	{
		mLLDBDebugger.Clear();
		lldb::SBDebugger::Destroy(mLLDBDebugger);
		mLLDBDebugger = lldb::SBDebugger();
	}

	mProcessId = 0;
	mRunState = RunState_Terminated;
	CloseOutputPipes();
	HotRemoveDebugInfo();
}

void LLDBDebugger::Terminate()
{
	LLDBLog("Terminate\n");
	WaitForLaunchThread();
	mRunState = RunState_Terminating;

	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mLLDBProcess.IsValid())
	{
		mLLDBProcess.Destroy();
		mLLDBProcess = lldb::SBProcess();
	}

	if (mLLDBDebugger.IsValid() && mLLDBTarget.IsValid())
	{
		mLLDBDebugger.DeleteTarget(mLLDBTarget);
		mLLDBTarget = lldb::SBTarget();
	}

	if (mLLDBDebugger.IsValid())
	{
		lldb::SBDebugger::Destroy(mLLDBDebugger);
		mLLDBDebugger = lldb::SBDebugger();
	}

	mProcessId = 0;
	mRunState = RunState_Terminated;
	CloseOutputPipes();
	HotRemoveDebugInfo();
}

void LLDBDebugger::Detach()
{
	LLDBLog("Detach\n");

	WaitForLaunchThread();

	// Release SBFrame refs before destroying so LLDB can fully drop module handles.
	mCallStack.Clear();
	mCallStackDirty = false;

	if (mLLDBProcess.IsValid())
	{
		if (mDidAttach)
			mLLDBProcess.Detach();
		else
			mLLDBProcess.Kill();

		mLLDBProcess.Destroy(); // Ensure it is fully destroyed
		mLLDBProcess = lldb::SBProcess();
	}

	if (mLLDBDebugger.IsValid() && mLLDBTarget.IsValid())
	{
		mLLDBDebugger.DeleteTarget(mLLDBTarget);
		mLLDBTarget = lldb::SBTarget();
	}

	if (mLLDBDebugger.IsValid())
	{
		lldb::SBDebugger::Destroy(mLLDBDebugger);
		mLLDBDebugger = lldb::SBDebugger();
	}

	// Invalidate the LLDB-side handles stored in each breakpoint object, but do
	// NOT delete the breakpoints themselves — the IDE layer owns them and will
	// re-bind them when a new session starts.  Reset the resolved addresses so
	// that CheckBreakpoint() will re-create them against the next target.
	for (auto bp : mBreakpoints)
	{
		bp->mLLDBBreakpoint = lldb::SBBreakpoint();
		bp->mResolvedAddr = 0;
	}
	mBreakpointIdMap.Clear();
	mBreakpointAddrMap.Clear();

	// Reset all per-session state to initial values, mirroring WinDebugger::Detach.
	mActiveBreakpoint = NULL;
	mProcessId = 0;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mHadImageFindError = false;

	// Clear stored launch params so a subsequent OpenFile starts fresh.
	mLaunchPath.Clear();
	mLaunchArgs.Clear();
	mWorkingDir.Clear();
	mEnvBlock.Clear();

	// Leave mRunState as NotStarted so the debugger can be reused for a new session.
	mRunState = RunState_NotStarted;
	mDidAttach = false;
	mNeedBreakpointRebind = false;
	mAutoStepRemaining = 0;
}

//----------------------------------------------------------------------------
// Misc
//----------------------------------------------------------------------------

Profiler* LLDBDebugger::StartProfiling()
{
	return NULL;
}

Profiler* LLDBDebugger::PopProfiler()
{
	return NULL;
}

void LLDBDebugger::ReportMemory(MemReporter* memReporter)
{
}

bool LLDBDebugger::IsOnDemandDebugger()
{
	return false;
}

bool LLDBDebugger::GetEmitSource(const StringImpl& filePath, String& outText)
{
	return false;
}

#else

USING_NS_BF;

#endif

Debugger* Beefy::CreateDebuggerLLDB(DebugManager* debugManager)
{
#ifdef LLDB_ENABLED
	return new LLDBDebugger(debugManager);
#else
	return NULL;
#endif
}
