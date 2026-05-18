#include "LLDBDebugger.h"
#include "DebugManager.h"

#ifdef LLDB_ENABLED

USING_NS_BF;

void LLDBLog(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

	OutputDebugStr("LLDB: ");
	OutputDebugStr(aResult);
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
}

LLDBDebugger::~LLDBDebugger()
{
	for (auto bp : mBreakpoints)
		delete bp;
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
	return 8;
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

	// Initialize LLDB the first time we launch.
	lldb::SBDebugger::Initialize();

	mLLDBDebugger = lldb::SBDebugger::Create(/*source_init_files=*/false);
	mLLDBDebugger.SetAsync(true);

	// Create a target from the executable path.
	lldb::SBError targetError;
	mLLDBTarget = mLLDBDebugger.CreateTarget(launchPath.c_str(), NULL, NULL,
		/*add_dependent_modules=*/true, targetError);
	if (!mLLDBTarget.IsValid())
	{
		String msg = "LLDB: Failed to create target for '";
		msg += launchPath;
		msg += "'";
		if (targetError.IsValid())
		{
			msg += ": ";
			msg += targetError.GetCString();
		}
		msg += "\n";
		OutputMessage(msg);
		return;
	}

	// Parse the args string into a vector of strings, then into a const char** array.
	// Args are space-separated; quoted strings are not currently handled.
	Array<String> argStrings;
	Array<const char*> argv;
	{
		const char* p = args.c_str();
		while (*p != '\0')
		{
			while (*p == ' ')
				++p;
			if (*p == '\0')
				break;
			const char* start = p;
			while (*p != '\0' && *p != ' ')
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
		const uint8* p = &envBlock.front();
		const uint8* end = p + envBlock.size();
		while ((p < end) && (*p != '\0'))
		{
			const uint8* start = p;
			while ((p < end) && (*p != '\0'))
				++p;
			envStrings.push_back(String((const char*)start, (int)(p - start)));
			if (p < end)
				++p; // skip null terminator
		}
		for (auto& s : envStrings)
			envp.push_back(s.c_str());
		envp.push_back(NULL);
	}

	// Launch the process.
	lldb::SBError launchError;
	mLLDBProcess = mLLDBTarget.Launch(
		mLLDBDebugger.GetListener(),
		argv.size() > 1 ? &argv.front() : NULL,
		envp.size() > 1 ? &envp.front() : NULL,
		NULL, // stdin
		NULL, // stdout
		NULL, // stderr
		workingDir.IsEmpty() ? NULL : workingDir.c_str(),
		0,    // launch flags
		true, // stop at entry
		launchError);

	if ((!mLLDBProcess.IsValid()) || launchError.Fail())
	{
		String msg = "LLDB: Failed to launch '";
		msg += launchPath;
		msg += "'";
		if (launchError.IsValid())
		{
			msg += ": ";
			msg += launchError.GetCString();
		}
		msg += "\n";
		OutputMessage(msg);
		lldb::SBDebugger::Destroy(mLLDBDebugger);
		return;
	}

	mProcessId = (int)mLLDBProcess.GetProcessID();
	mRunState = RunState_Running;
}

bool LLDBDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	mDidAttach = true;
	return false;
}

void LLDBDebugger::GetStdHandles(BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
}

void LLDBDebugger::Run()
{
	// Resume a process that was stopped at entry or after a breakpoint.
	if (mLLDBProcess.IsValid())
		mLLDBProcess.Continue();
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
			mRunState = RunState_Exception;
		}
		return;
	}

	// Process stopped (breakpoint, step complete, user interrupt, etc.)
	if (state == lldb::eStateStopped)
	{
		if ((mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint))
		{
			ClearCallStack();
			mRequestedStackFrameIdx = 0;
			mBreakStackFrameIdx = 0;

			// Thread info is fully populated once we have consumed the stop event
			lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
			if ((!thread.IsValid()) && (mLLDBProcess.GetNumThreads() > 0))
				thread = mLLDBProcess.GetThreadAtIndex(0);

			auto threadStopReason = thread.IsValid() ? thread.GetStopReason() : lldb::eStopReasonNone;

			LLDBLog("HandleProcessEvent Stopped. ThreadIsValid:%d StopReason:%d\n", thread.IsValid(), threadStopReason);

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
	LLDBBreakpoint* bp = new LLDBBreakpoint();
	bp->mSymbolName = symbolName;
	mBreakpoints.push_back(bp);

	if (mLLDBTarget.IsValid())
	{
		bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByName(symbolName.c_str());
		if (bp->mLLDBBreakpoint.IsValid())
			mBreakpointIdMap.ForceAdd((int)bp->mLLDBBreakpoint.GetID(), bp);
	}

	return bp;
}

Breakpoint* LLDBDebugger::CreateAddressBreakpoint(intptr address)
{
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
			bp->mLLDBBreakpoint = mLLDBTarget.BreakpointCreateByName(bp->mSymbolName.c_str());
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
	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetEnabled(false);
}

void LLDBDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& condition)
{
	LLDBBreakpoint* bp = (LLDBBreakpoint*)breakpoint;
	if (bp->mLLDBBreakpoint.IsValid())
		bp->mLLDBBreakpoint.SetCondition(condition.IsEmpty() ? NULL : condition.c_str());
}

void LLDBDebugger::SetBreakpointLogging(Breakpoint* wdBreakpoint, const StringImpl& logging, bool breakAfterLogging)
{
}

Breakpoint* LLDBDebugger::FindBreakpointAt(intptr address)
{
	LLDBBreakpoint* bp = NULL;
	mBreakpointAddrMap.TryGetValue((uintptr)address, &bp);
	return bp;
}

Breakpoint* LLDBDebugger::GetActiveBreakpoint()
{
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
	return String();
}

int LLDBDebugger::GetProcessId()
{
	return mProcessId;
}

String LLDBDebugger::GetThreadInfo()
{
	return String();
}

void LLDBDebugger::SetActiveThread(int threadId)
{
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
	return String();
}

//----------------------------------------------------------------------------
// Expression evaluation (stubs)
//----------------------------------------------------------------------------

String LLDBDebugger::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	return String();
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

	// Leave mRunState as NotStarted so the debugger can be reused for a new session.
	mRunState = RunState_NotStarted;
	mDidAttach = false;	

	//lldb::SBDebugger::Terminate();
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
