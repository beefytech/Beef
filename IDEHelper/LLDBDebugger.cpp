#include "LLDBDebugger.h"
#include "DebugManager.h"

#ifdef LLDB_ENABLED

#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

USING_NS_BF;

void LLDBLog(const char* fmt ...)
{
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
	mExceptionAddress = 0;
	mExceptionCode = 0;
	mHotSwapEnabled = false;
	mOpenFileFlags = DbgOpenFileFlag_None;
	mLaunchThread = NULL;
}

LLDBDebugger::~LLDBDebugger()
{
	WaitForLaunchThread();
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

	// Store all parameters; the actual LLDB initialisation happens on a background
	// thread started by Run() so we don't block the main thread.
	mLaunchPath = launchPath;
	mLaunchArgs = args;
	mWorkingDir = workingDir;
	mEnvBlock = envBlock;
	mHotSwapEnabled = hotSwapEnabled;
	mOpenFileFlags = openFileFlags;
}

void LLDBDebugger::DoLaunch()
{
	LLDBLog("DoLaunch\n");

	lldb::SBDebugger::Initialize();

	lldb::SBDebugger debugger = lldb::SBDebugger::Create(/*source_init_files=*/false);
	debugger.SetAsync(true);

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
	lldb::SBProcess process = target.Launch(
		debugger.GetListener(),
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

			LLDBLog("HandleProcessEvent Stopped. ThreadIsValid:%d StopReason:%d\n", thread.IsValid(), threadStopReason);

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

		ClearCallStack();
		mRunState = RunState_Running;
		if (inAssembly)
			thread.StepInstruction(/*step_over=*/false);
		else
			thread.StepInto();
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
		ClearCallStack();
		mRunState = RunState_Running;
		thread.StepOut();
	}
}

void LLDBDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
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
		lldb::SBFileSpec fileSpec(fileName.c_str(), /*resolve=*/false);
		bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(lineNum + 1));
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
			lldb::SBFileSpec fileSpec(bp->mFilePath.c_str(), false);
			bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(bp->mRequestedLineNum + 1));
		}
		else if (!bp->mSymbolName.IsEmpty())
		{
			DoCreateBreakpointByName(bp);			
		}

		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}

	// Try to resolve the load address so FindBreakpointAt() works.
	if ((bp->mLLDBBreakpoint.IsValid()) && (bp->mResolvedAddr == 0))
	{
		size_t numResolved = bp->mLLDBBreakpoint.GetNumResolvedLocations();
		if (numResolved > 0)
		{
			lldb::SBBreakpointLocation loc = bp->mLLDBBreakpoint.GetLocationAtIndex(0);
			if (loc.IsValid())
			{
				lldb::addr_t loadAddr = loc.GetLoadAddress();
				if (loadAddr != (lldb::addr_t)-1)
				{
					bp->mResolvedAddr = (uintptr)loadAddr;
					mBreakpointAddrMap.ForceAdd(bp->mResolvedAddr, bp);
				}
			}
		}
	}
}

void LLDBDebugger::HotBindBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int hotIdx)
{
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

	bp->mLineNum = lineNum;
	bp->mRequestedLineNum = lineNum;
	bp->mColumn = wantColumn;

	if ((rebindNow) && (mLLDBTarget.IsValid()) && (!bp->mFilePath.IsEmpty()))
	{
		lldb::SBFileSpec fileSpec(bp->mFilePath.c_str(), false);
		bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByLocation(fileSpec, (uint32)(lineNum + 1));
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
}

void LLDBDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& condition)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetCondition(condition.IsEmpty() ? NULL : condition.c_str());
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
	return String();
}

String LLDBDebugger::GetAddressSourceLocation(intptr address)
{
	return String();
}

String LLDBDebugger::GetAddressSymbolName(intptr address, bool demangle)
{
	return String();
}

String LLDBDebugger::DisassembleAtRaw(intptr address)
{
	return String();
}

String LLDBDebugger::DisassembleAt(intptr address)
{
	return String();
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

		lldb::addr_t loadAddr = value.GetLoadAddress();

		bool isComposite = ((typeClass == lldb::eTypeClassStruct) ||
		                    (typeClass == lldb::eTypeClassClass) ||
		                    (typeClass == lldb::eTypeClassUnion));
		if (isComposite)
		{
			// Expose address so the IDE can navigate members, unless suppressed
			if ((loadAddr != LLDB_INVALID_ADDRESS) && !fmt.mNoAddress && !fmt.mNoMembers)
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

				uint32 numChildren = value.GetNumChildren();
				for (uint32 i = 0; i < numChildren; i++)
				{
					lldb::SBValue child = value.GetChildAtIndex(i);
					if (!child.IsValid())
						continue;
					const char* childName = child.GetName();
					if ((childName == NULL) || (childName[0] == '\0'))
						continue;
					// Skip array-index synthetic children (e.g. "[0]", "[1]")
					if (childName[0] == '[')
						continue;

					result += '\n';
					result += childName;
					result += '\t';
					result += StrFormat("({0}).%s", childName);					
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
	return String();
}

String LLDBDebugger::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
{
	return String();
}

String LLDBDebugger::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	return String();
}

String LLDBDebugger::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	return String();
}

String LLDBDebugger::GetAutoLocals(int callStackIdx, bool showRegs)
{
	return String();
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
// Hot-reload stubs
//----------------------------------------------------------------------------

void LLDBDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
}

void LLDBDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
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
	LLDBLog("StopDebugging\n");

	WaitForLaunchThread();

	// Release SBFrame refs before destroying so LLDB can fully drop module handles.
	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mLLDBProcess.IsValid())
		mLLDBProcess.Destroy();
	mLLDBProcess = lldb::SBProcess();
	
	if (mLLDBTarget.IsValid())
		mLLDBDebugger.DeleteTarget(mLLDBTarget);
	mLLDBTarget = lldb::SBTarget();

	if (mLLDBDebugger.IsValid())
	{
		mLLDBDebugger.Clear();
		lldb::SBDebugger::Destroy(mLLDBDebugger);
	}
	mLLDBDebugger = lldb::SBDebugger();
	
	mProcessId = 0;
	mRunState = RunState_Terminated;
}

void LLDBDebugger::Terminate()
{
	LLDBLog("Terminate\n");

	WaitForLaunchThread();

	mRunState = RunState_Terminating;

	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mLLDBProcess.IsValid())
		mLLDBProcess.Destroy();
	mLLDBProcess = lldb::SBProcess();

	mLLDBTarget = lldb::SBTarget();

	if (mLLDBDebugger.IsValid())
		lldb::SBDebugger::Destroy(mLLDBDebugger);
	mLLDBDebugger = lldb::SBDebugger();
	
	mProcessId = 0;
	mRunState = RunState_Terminated;
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
		{
			// Detach from the inferior so it keeps running after we disconnect.
			lldb::StateType state = mLLDBProcess.GetState();
			if ((state == lldb::eStateRunning) || (state == lldb::eStateStopped) ||
				(state == lldb::eStateSuspended))
			{
				mLLDBProcess.Detach();
			}
		}
		else
			mLLDBProcess.Kill();
		mLLDBProcess = lldb::SBProcess();
	}

	mLLDBTarget = lldb::SBTarget();

	// Tear down the LLDB session and release global state.
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

#endif
