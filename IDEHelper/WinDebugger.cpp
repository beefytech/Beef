#pragma warning(disable:4996)
// TODO: Remove for 64-bit
#pragma warning(disable:4244)
#pragma warning(disable:4267)

#define NTDDI_VERSION 0x06020000

#include "WinDebugger.h"
#include "LLDBDebugger.h"
#include "CPU.h"
#include "DbgModule.h"
#include "DebugVisualizers.h"
#include "MiniDumpDebugger.h"
#include "X86.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/UTF8.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/FileHandleStream.h"
#include "BeefySysLib/util/FileEnumerator.h"
#include <inttypes.h>
#include <windows.h>
#include "DbgExprEvaluator.h"
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"
#include "Compiler/BfDemangler.h"
#include "Compiler/BfPrinter.h"
#include <Shlobj.h>
#include "NetManager.h"
#include "DebugManager.h"
#include "X86Target.h"
#include "HotHeap.h"
#include "HotScanner.h"
#include "Profiler.h"
#include <float.h>

#include <psapi.h>

#if !defined BF32 || !defined BF_DBG_64

#define STATUS_WX86_CONTINUE             0x4000001DL
#define STATUS_WX86_SINGLE_STEP          0x4000001EL
#define STATUS_WX86_BREAKPOINT           0x4000001FL
#define STATUS_WX86_EXCEPTION_CONTINUE   0x40000020L

#pragma pack(push, 1)
struct HotJumpOp
{
	uint8 mOpCode;
	int32 mRelTarget;
};
#pragma pack(pop)

#include "BeefySysLib/util/AllocDebug.h"

#include <limits>

USING_NS_BF_DBG;

static void FilterThreadName(String& name)
{
	for (int i = 0; i < (int)name.length(); i++)
	{
		uint8 c = name[i];
		if (c == 0)
		{
			name.RemoveToEnd(i);
			return;
		}

		if (c < 32)
		{
			name.Remove(i);
			i--;
			continue;
		}
	}
}

static bool IsHandleValid(HANDLE handle)
{
	return (handle != NULL) && (handle != INVALID_HANDLE_VALUE);
}

//////////////////////////////////////////////////////////////////////////

WdBreakpointCondition::~WdBreakpointCondition()
{
	delete mDbgEvaluationContext;
}

//////////////////////////////////////////////////////////////////////////

typedef HRESULT(WINAPI* SetThreadDescription_t)(HANDLE hThread, PCWSTR lpThreadDescription);
typedef HRESULT(WINAPI* GetThreadDescription_t)(HANDLE hThread, PWSTR* lpThreadDescription);
static SetThreadDescription_t gSetThreadDescription = NULL;
static GetThreadDescription_t gGetThreadDescription = NULL;
static HMODULE gKernelDll = NULL;

static void ImportKernel()
{
	if (gKernelDll != NULL)
		return;

	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, MAX_PATH);
	wcscat(path, L"\\kernel32.dll");
	gKernelDll = GetModuleHandle(path);
	if (gKernelDll == NULL)
	{
		return;
	}

	gSetThreadDescription = (SetThreadDescription_t)GetProcAddress(gKernelDll, "SetThreadDescription");
	gGetThreadDescription = (GetThreadDescription_t)GetProcAddress(gKernelDll, "GetThreadDescription");
}

void WinDebugger::TryGetThreadName(WdThreadInfo* threadInfo)
{
	if (threadInfo->mHThread == NULL)
		return;

	ImportKernel();

	PWSTR wStr = NULL;
	if (gGetThreadDescription != NULL)
	{
		gGetThreadDescription(threadInfo->mHThread, &wStr);
		if (wStr == NULL)
			return;

		threadInfo->mName = UTF8Encode(wStr);
		FilterThreadName(threadInfo->mName);
		LocalFree(wStr);
	}
}

//////////////////////////////////////////////////////////////////////////

WinDebugger::WinDebugger(DebugManager* debugManager) : mDbgSymSrv(this)
{
	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));

	mActiveHotIdx = -1;
	mGotStartupEvent = false;
	mIsContinuingFromException = false;
	mDestroying = false;
	mDebugManager = debugManager;
	mNeedsRehupBreakpoints = false;
	mStepInAssembly = false;
	mStepSP = 0;
	mStepIsRecursing = false;
	mStepStopOnNextInstruction = false;
	mDebugTarget = NULL;
	mShuttingDown = false;
	mBfSystem = new BfSystem();
	mAtBreakThread = NULL;
	mActiveThread = NULL;
	mActiveBreakpoint = NULL;
	mSteppingThread = NULL;
	mExplicitStopThread = NULL;
	mStepSwitchedThreads = false;
	mIsDebuggerWaiting = false;
	mWantsDebugContinue = false;
	mContinueFromBreakpointFailed = false;
	mIsStepIntoSpecific = false;
	mDbgBreak = false;
	mDebuggerWaitingThread = NULL;
	mStepType = StepType_None;
	mOrigStepType = StepType_None;
	mLastValidStepIntoPC = 0;
	mActiveSymSrvRequest = NULL;
	mStdInputPipe = INVALID_HANDLE_VALUE;
	mStdOutputPipe = INVALID_HANDLE_VALUE;
	mStdErrorPipe = INVALID_HANDLE_VALUE;

	mStoredReturnValueAddr = 0;
#ifdef BF_DBG_32
	mCPU = gX86Target->mX86CPU;
#else
	mCPU = gX86Target->mX64CPU;
#endif
	mRunState = RunState_NotStarted;
	mIsRunning = false;
	mSavedAtBreakpointAddress = 0;
	mSavedBreakpointAddressContinuing = 0;
	mRequestedStackFrameIdx = 0;
	mShowPCOverride = 0;
	mCurNoInfoStepTries = 0;
	mDbgAttachFlags = BfDbgAttachFlag_None;
	mDbgProcessHandle = 0;
	mDbgThreadHandle = 0;
	mDbgProcessId = 0;
	mDbgHeapData = NULL;
	mIsPartialCallStack = true;
	mHotSwapEnabled = false;
	mOpenFileFlags = DbgOpenFileFlag_None;

	for (int i = 0; i < 4; i++)
	{
		mFreeMemoryBreakIndices.push_back(i);
	}
	mMemoryBreakpointVersion = 0;

	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	mPageSize = systemInfo.dwPageSize;

	mEmptyDebugTarget = new DebugTarget(this);
	mEmptyDebugTarget->CreateEmptyTarget();
	mEmptyDebugTarget->mIsEmpty = true;
	mDebugTarget = mEmptyDebugTarget;
	mDebugPendingExpr = NULL;
	mDebugEvalThreadInfo = WdThreadInfo();

	mMemCacheAddr = 0;
	mDebuggerThreadId = 0;
}

WinDebugger::~WinDebugger()
{
	mDestroying = true;

	delete gDbgPerfManager;
	gDbgPerfManager = NULL;

	if ((mDebugTarget != NULL) && (mDebugTarget != mEmptyDebugTarget))
		Detach();

	for (auto breakpoint : mBreakpoints)
	{
		auto checkBreakpoint = breakpoint->mLinkedSibling;
		while (checkBreakpoint != NULL)
		{
			auto nextBreakpoint = checkBreakpoint->mLinkedSibling;
			delete checkBreakpoint;
			checkBreakpoint = nextBreakpoint;
		}

		delete breakpoint;
	}

	delete mEmptyDebugTarget;

	delete mBfSystem;
	for (auto kv : mPendingProfilerMap)
		delete kv.mValue;
	for (auto profiler : mNewProfilerList)
		delete profiler;
	delete mDebugPendingExpr;
}

void WinDebugger::Fail(const StringImpl& error)
{
	if (mIsRunning)
		mDebugManager->mOutMessages.push_back(StrFormat("error %s", error.c_str()));
}

// Leave active thread unpaused
void WinDebugger::ThreadRestorePause(WdThreadInfo* onlyPauseThread, WdThreadInfo* dontPauseThread)
{
	BfLogDbg("ThreadRestorePause %d %d\n", (onlyPauseThread != NULL) ? onlyPauseThread->mThreadId : 0, (dontPauseThread != NULL) ? dontPauseThread->mThreadId : 0);
	for (auto threadInfo : mThreadList)
	{
		if (((threadInfo != dontPauseThread) && (!threadInfo->mIsBreakRestorePaused)) &&
			((onlyPauseThread == NULL) || (threadInfo == onlyPauseThread)))
		{
			BF_ASSERT(!threadInfo->mIsBreakRestorePaused);

			DWORD prevSuspendCount = ::SuspendThread(threadInfo->mHThread);
			BfLogDbg("SuspendThread %d PrevCount:%d\n", threadInfo->mThreadId, (int)prevSuspendCount);

			threadInfo->mIsBreakRestorePaused = true;
		}
	}
}

void WinDebugger::ThreadRestoreUnpause()
{
	BfLogDbg("ThreadRestoreUnpause\n");
	for (auto threadInfo : mThreadList)
	{
		if (threadInfo->mIsBreakRestorePaused)
		{
			DWORD prevSuspendCount = ::ResumeThread(threadInfo->mHThread);
			BfLogDbg("ResumeThread %d PrevCount:%d\n", threadInfo->mThreadId, (int)prevSuspendCount);
			threadInfo->mIsBreakRestorePaused = false;
		}
	}
}

void WinDebugger::UpdateThreadDebugRegisters(WdThreadInfo* threadInfo)
{
	if (threadInfo->mMemoryBreakpointVersion == mMemoryBreakpointVersion)
		return;

	auto threadId = threadInfo->mHThread;

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_DEBUG_REGISTERS;
	BF_GetThreadContext(threadId, &lcContext);
	for (int memoryBreakIdx = 0; memoryBreakIdx < 4; memoryBreakIdx++)
	{
		WdMemoryBreakpointBind memoryBreakpointBind = mMemoryBreakpoints[memoryBreakIdx];

		WdBreakpoint* wdBreakpoint = memoryBreakpointBind.mBreakpoint;
		if (wdBreakpoint == NULL)
		{
			*(&lcContext.Dr0 + memoryBreakIdx) = 0;
			lcContext.Dr7 &= ~((1 << (memoryBreakIdx * 2)) | (1 << (16 + memoryBreakIdx * 4)) | (3 << (18 + memoryBreakIdx * 4)));
		}
		else
		{
			int sizeCode = 0;
			if (memoryBreakpointBind.mByteCount == 2)
				sizeCode = 1;
			else if (memoryBreakpointBind.mByteCount == 4)
				sizeCode = 3;
			else if (memoryBreakpointBind.mByteCount == 8)
				sizeCode = 2;

			addr_target calcAddr = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress + memoryBreakpointBind.mOfs;
			BF_ASSERT(calcAddr == memoryBreakpointBind.mAddress);

			*(&lcContext.Dr0 + memoryBreakIdx) = calcAddr;
			lcContext.Dr7 |= (1 << (memoryBreakIdx * 2)) | (1 << (16 + memoryBreakIdx * 4)) | (sizeCode << (18 + memoryBreakIdx * 4));
		}
	}
	bool worked = BF_SetThreadContext(threadId, &lcContext) != 0;
	BF_ASSERT(worked || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated));
	threadInfo->mMemoryBreakpointVersion = mMemoryBreakpointVersion;
}

void WinDebugger::UpdateThreadDebugRegisters()
{
	for (auto threadInfo : mThreadList)
	{
		::SuspendThread(threadInfo->mHThread);
		UpdateThreadDebugRegisters(threadInfo);
		::ResumeThread(threadInfo->mHThread);
	}
}

void WinDebugger::PhysSetBreakpoint(addr_target address)
{
	BfLogDbg("PhysSetBreakpoint %p\n", address);

	uint8 newData = 0xCC;

	// This ensure that we have the orig image data cached
	DbgMemoryFlags flags = mDebugTarget->ReadOrigImageData(address, NULL, 1);
	if ((flags & DbgMemoryFlags_Execute) == 0)
	{
		BfLogDbg("Breakpoint ignored - execute flag NOT set in breakpoint address\n", address);

		BfLogDbg("Memory Flags = %d\n", gDebugger->GetMemoryFlags(address));

		return;
	}

	// Replace it with Breakpoint
	SIZE_T dwReadBytes;
	BOOL worked = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, &newData, 1, &dwReadBytes);
	if (!worked)
	{
		int err = GetLastError();
		BfLogDbg("SetBreakpoint FAILED %p\n", address);
	}
	FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, 1);

	{
		uint8 mem = ReadMemory<uint8>(address);
		BfLogDbg("Breakpoint byte %X\n", mem);
	}
}

void WinDebugger::SetBreakpoint(addr_target address, bool fromRehup)
{
	int* countPtr = NULL;
	if (mPhysBreakpointAddrMap.TryAdd(address, NULL, &countPtr))
	{
		BfLogDbg("SetBreakpoint %p\n", address);
		*countPtr = 1;
	}
	else
	{
		if (fromRehup)
		{
			BfLogDbg("SetBreakpoint %p Count: %d. Rehup (ignored).\n", address, *countPtr);
			return;
		}

		(*countPtr)++;
		BfLogDbg("SetBreakpoint %p Count: %d\n", address, *countPtr);
		return;
	}

	PhysSetBreakpoint(address);
}

void WinDebugger::SetTempBreakpoint(addr_target address)
{
	BfLogDbg("SetTempBreakpoint %p\n", address);
	mTempBreakpoint.push_back(address);
	SetBreakpoint(address);
}

void WinDebugger::PhysRemoveBreakpoint(addr_target address)
{
	BfLogDbg("PhysRemoveBreakpoint %p\n", address);

	uint8 origData;
	DbgMemoryFlags flags = mDebugTarget->ReadOrigImageData(address, &origData, 1);
	if ((flags & DbgMemoryFlags_Execute) == 0)
	{
		//BF_ASSERT("Failed" == 0);
		return;
	}

	SIZE_T dwReadBytes;
	if (!WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, &origData, 1, &dwReadBytes))
	{
		int err = GetLastError();
		BfLogDbg("RemoveBreakpoint FAILED %p\n", address);
	}
	FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, 1);
}

void WinDebugger::RemoveBreakpoint(addr_target address)
{
	int* countPtr = NULL;
	mPhysBreakpointAddrMap.TryGetValue(address, &countPtr);

	// This can happen when we shutdown and we're continuing from a breakpoint
	//BF_ASSERT(*countPtr != NULL);
	if (countPtr == NULL)
	{
		BfLogDbg("RemoveBreakpoint %p FAILED\n", address);
		return;
	}

	BfLogDbg("RemoveBreakpoint %p count: %d\n", address, *countPtr);
	if (*countPtr > 1)
	{
		(*countPtr)--;
		return;
	}
	mPhysBreakpointAddrMap.Remove(address);
	PhysRemoveBreakpoint(address);
}

void WinDebugger::SingleStepX86()
{
	// In what cases did this catch bugs?
	// This caused other failures (caught in tests)
// 	if (mActiveThread->mIsAtBreakpointAddress != 0)
// 	{
// 		ContinueFromBreakpoint();
// 		return;
// 	}

	BfLogDbg("Setup SingleStepX86 ActiveThread: %d\n", (mActiveThread != NULL) ? mActiveThread->mThreadId : -1);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
	lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);
}

bool WinDebugger::ContinueFromBreakpoint()
{
	if (mDebuggerWaitingThread->mFrozen)
	{
		BfLogDbg("ContinueFromBreakpoint bailout on frozen thread\n");
		mDebuggerWaitingThread->mStoppedAtAddress = 0;
		mDebuggerWaitingThread->mIsAtBreakpointAddress = 0;
		return true;
	}

	mActiveThread = mDebuggerWaitingThread;
	mActiveBreakpoint = NULL;

	BfLogDbg("ContinueFromBreakpoint. ActiveThread: %d\n", (mActiveThread != NULL) ? mActiveThread->mThreadId : -1);

	BfLogDbg("ResumeThread %d\n", mActiveThread->mThreadId);
	BOOL success = ::ResumeThread(mActiveThread->mHThread);
	if (success)
	{
		// It's possible the active thread is suspended - possibly by the GC, so we would deadlock if we
		//  attempted to pause the other threads
		BfLogDbg("SuspendThread %d\n", mActiveThread->mThreadId);
		BfLogDbg("Thread already paused!\n");
		::SuspendThread(mActiveThread->mHThread);
		return false;
	}

	ThreadRestorePause(NULL, mActiveThread);

	PhysRemoveBreakpoint(mActiveThread->mIsAtBreakpointAddress);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
	lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

	mActiveThread->mStoppedAtAddress = 0;
	mActiveThread->mBreakpointAddressContinuing = mActiveThread->mIsAtBreakpointAddress;
	mActiveThread->mIsAtBreakpointAddress = 0;
	BfLogDbg("ContinueFromBreakpoint set mIsAtBreakpointAddress = 0\n");
	return true;
}

void WinDebugger::ValidateBreakpoints()
{
	HashSet<addr_target> usedBreakpoints;

	std::function<void(WdBreakpoint*)> _AddBreakpoint = [&](WdBreakpoint* breakpoint)
	{
		if (breakpoint->mAddr != 0)
		{
			usedBreakpoints.Add(breakpoint->mAddr);

			WdBreakpoint* foundBreakpoint = NULL;
			auto itr = mBreakpointAddrMap.Find(breakpoint->mAddr);
			bool found = false;
			while (itr != mBreakpointAddrMap.end())
			{
				WdBreakpoint* foundBreakpoint = itr->mValue;
				found |= foundBreakpoint == breakpoint;
				itr.NextWithSameKey(breakpoint->mAddr);
			}

			BF_ASSERT(found);
		}

		auto checkSibling = (WdBreakpoint*)breakpoint->mLinkedSibling;
		while (checkSibling != NULL)
		{
			_AddBreakpoint(checkSibling);
			checkSibling = (WdBreakpoint*)checkSibling->mLinkedSibling;
		}
	};

	for (auto breakpoint : mBreakpoints)
		_AddBreakpoint(breakpoint);

	for (auto& entry : mBreakpointAddrMap)
	{
		BF_ASSERT(usedBreakpoints.Contains(entry.mKey));
	}
}

Breakpoint* WinDebugger::FindBreakpointAt(intptr address)
{
#ifdef _DEBUG
	//ValidateBreakpoints();
#endif

	WdBreakpoint* breakpoint = NULL;
	mBreakpointAddrMap.TryGetValue(address, &breakpoint);
	return breakpoint;
}

Breakpoint* WinDebugger::GetActiveBreakpoint()
{
	if ((mActiveBreakpoint != NULL) && (mActiveBreakpoint->mHead != NULL))
		return mActiveBreakpoint->mHead;
	return mActiveBreakpoint;
}

void WinDebugger::DebugThreadProc()
{
	BpSetThreadName("DebugThread");
	BfpThread_SetName(NULL, "DebugThread", NULL);

	mDebuggerThreadId = GetCurrentThreadId();

	if (!IsMiniDumpDebugger())
	{
		if (!DoOpenFile(mLaunchPath, mArgs, mWorkingDir, mEnvBlock, mOpenFileFlags))
		{
			if (mDbgProcessId != 0)
				OutputRawMessage("error Unable to attach to process");
			else
				OutputRawMessage(StrFormat("error Failed to launch: %s", mLaunchPath.c_str()));
			mShuttingDown = true;
			mRunState = RunState_Terminated;
		}
	}

	while (!mShuttingDown)
	{
		DoUpdate();
	}

	mIsRunning = false;

	for (int i = 0; i < (int) mBreakpoints.size(); i++)
	{
		WdBreakpoint* wdBreakpoint = mBreakpoints[i];

		if (wdBreakpoint->mAddr != 0)
			mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);

		wdBreakpoint->mAddr = 0;
		wdBreakpoint->mLineData = DbgLineDataEx();
		wdBreakpoint->mSrcFile = NULL;
		if (wdBreakpoint->mLinkedSibling != NULL)
		{
			DeleteBreakpoint(wdBreakpoint->mLinkedSibling);
			wdBreakpoint->mLinkedSibling = NULL;
		}
	}

	if (!IsMiniDumpDebugger())
	{
		while (true)
		{
			if (!mIsDebuggerWaiting)
			{
				if (!WaitForDebugEvent(&mDebugEvent, 0))
					break;
			}
			if (mDebuggerWaitingThread != NULL)
			{
				BF_ASSERT_REL((mDebuggerWaitingThread->mIsAtBreakpointAddress == 0) || (mShuttingDown));
				::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_CONTINUE);
				BfLogDbg("::ContinueDebugEvent startup ThreadId:%d\n", mDebuggerWaitingThread->mThreadId);
			}
			mIsDebuggerWaiting = false;
			mDebuggerWaitingThread = NULL;
		}
	}

	mDebuggerThreadId = 0;
}

static void DebugThreadProcThunk(void* winDebugger)
{
	((WinDebugger*) winDebugger)->DebugThreadProc();
}

int WinDebugger::GetAddrSize()
{
	return sizeof(addr_target);
}

bool WinDebugger::CanOpen(const StringImpl& fileName, DebuggerResult* outResult)
{
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (fp == NULL)
	{
		*outResult = DebuggerResult_CannotOpen;
		return false;
	}

	FileStream fs;
	fs.mFP = fp;

	*outResult = DebuggerResult_Ok;
	bool canRead = DbgModule::CanRead(&fs, outResult);
	fclose(fp);
	return canRead;
}

void WinDebugger::OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled, DbgOpenFileFlags openFileFlags)
{
	BF_ASSERT(!mIsRunning);
	mLaunchPath = launchPath;
	mTargetPath = targetPath;
	mArgs = args;
	mWorkingDir = workingDir;
	mEnvBlock = envBlock;
	mHotSwapEnabled = hotSwapEnabled;
	mOpenFileFlags = openFileFlags;
	mDebugTarget = new DebugTarget(this);
}

bool WinDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	BF_ASSERT(!mIsRunning);

	mDbgAttachFlags = attachFlags;
	mDbgProcessHandle = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)processId);
	if (mDbgProcessHandle == 0)
		return false;
	BOOL is32Bit = false;
	if (!IsWow64Process(mDbgProcessHandle, &is32Bit))
	{
		mDbgProcessHandle = 0;
		::CloseHandle(mDbgProcessHandle);
		return false;
	}

	bool want32Bit = sizeof(intptr_target) == 4;
	if (want32Bit != (is32Bit != 0))
	{
		mDbgProcessHandle = 0;
		::CloseHandle(mDbgProcessHandle);
		return false;
	}

	HMODULE mainModule = 0;
	DWORD memNeeded = 0;
	::EnumProcessModules(mDbgProcessHandle, &mainModule, sizeof(HMODULE), &memNeeded);

	WCHAR fileName[MAX_PATH] = {0};
	GetModuleFileNameExW(mDbgProcessHandle, mainModule, fileName, MAX_PATH);
	mLaunchPath = UTF8Encode(fileName);
	mTargetPath = mLaunchPath;

	mDbgProcessId = processId;
	mDbgProcessHandle = 0;
	::CloseHandle(mDbgProcessHandle);

	mDebugTarget = new DebugTarget(this);

	return true;
}

void WinDebugger::GetStdHandles(BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((outStdIn != NULL) && (IsHandleValid(mStdInputPipe)))
	{
		*outStdIn = BfpFile_GetFromHandle((intptr)mStdInputPipe, NULL);
		mStdInputPipe = 0;
	}

	if ((outStdOut != NULL) && (IsHandleValid(mStdOutputPipe)))
	{
		*outStdOut = BfpFile_GetFromHandle((intptr)mStdOutputPipe, NULL);
		mStdOutputPipe = 0;
	}

	if ((outStdErr != NULL) && (IsHandleValid(mStdErrorPipe)))
	{
		*outStdErr = BfpFile_GetFromHandle((intptr)mStdErrorPipe, NULL);
		mStdErrorPipe = 0;
	}
}

void WinDebugger::Run()
{
	mIsRunning = true;
	DWORD localThreadId;
	HANDLE hThread = ::CreateThread(NULL, 64 * 1024, (LPTHREAD_START_ROUTINE) &DebugThreadProcThunk, (void*)this, 0, &localThreadId);
	CloseHandle(hThread);
}

bool WinDebugger::HasLoadedTargetBinary()
{
	if (mDebugTarget == NULL)
		return false;
	return mDebugTarget->mTargetBinary != NULL;
}

void WinDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mDebugTarget->mTargetBinary == NULL)
	{
		Fail("Hot swapping failed because the hot target binary has not yet been loaded.");
		return;
	}

	if (mDebugTarget->mHotHeap == NULL)
	{
		Fail("There is no hot heap space available for hot swapping.");
		return;
	}

	BfLogDbg("WinDebugger::HotLoad Start %d\n", hotIdx);

	SetAndRestoreValue<int> prevHotIdx(mActiveHotIdx, hotIdx);

	BF_ASSERT(mHotThreadStates.empty());
	mHotThreadStates.Resize(mThreadList.size());
	for (int threadIdx = 0; threadIdx < (int)mThreadList.size(); threadIdx++)
	{
		WdThreadInfo* threadInfo = mThreadList[threadIdx];
		SetAndRestoreValue<WdThreadInfo*> prevActiveThread(mActiveThread, threadInfo);
		DWORD prevSuspendCount = ::SuspendThread(threadInfo->mHThread);
		BfLogDbg("SuspendThread %d PrevCount:%d\n", threadInfo->mThreadId, (int)prevSuspendCount);
		if ((prevSuspendCount != 0) || (threadInfo->mIsBreakRestorePaused))
			BfLogDbg("WinDebugger::HotLoad thread %d was already suspended! PrevCount:%d BreakRestorePaused:%d\n",
				threadInfo->mThreadId, (int)prevSuspendCount, threadInfo->mIsBreakRestorePaused ? 1 : 0);
		mHotThreadStates[threadIdx].mThreadId = threadInfo->mThreadId;
		PopulateRegisters(&mHotThreadStates[threadIdx].mRegisters);
	}

	for (auto address : mTempBreakpoint)
		RemoveBreakpoint(address);
	mTempBreakpoint.Clear();
	mStepBreakpointAddrs.Clear();
	for (auto breakpoint : mBreakpoints)
	{
		DetachBreakpoint(breakpoint);
	}

	int startingModuleIdx = (int)mDebugTarget->mDbgModules.size();

	bool hasHotVData = false;

	bool failed = false;
	for (auto fileName : objectFiles)
	{
		if ((fileName.IndexOf("/vdata.") != -1) || (fileName.IndexOf("\\vdata.") != -1))
			hasHotVData = true;

		BfLogDbg("WinDebugger::HotLoad: %s\n", fileName.c_str());
		DbgModule* newBinary = mDebugTarget->HotLoad(fileName, hotIdx);
		if ((newBinary != NULL) && (newBinary->mFailed))
			failed = true;
	}

	for (int moduleIdx = startingModuleIdx; moduleIdx < (int)mDebugTarget->mDbgModules.size(); moduleIdx++)
	{
		auto dbgModule = mDebugTarget->mDbgModules[moduleIdx];
		BF_ASSERT(dbgModule->IsObjectFile());
		BF_ASSERT(dbgModule->mHotIdx == hotIdx);
		dbgModule->FinishHotSwap();
	}

	for (auto dwarf : mDebugTarget->mDbgModules)
		dwarf->RevertWritingEnable();

	int blockAllocSinceClean = mDebugTarget->mHotHeap->mBlockAllocIdx - mDebugTarget->mLastHotHeapCleanIdx;
	// Clean up the hot heap every 64MB
	int blocksBetweenCleans = (64 * 1024 * 1024) / HotHeap::BLOCK_SIZE;

#ifdef _DEBUG
	//TODO: This is just for testing
	blocksBetweenCleans = 1;
#endif

	//TODO: Put this back after we fix the cleanup
	if (blockAllocSinceClean >= blocksBetweenCleans)
		CleanupHotHeap();

	mDebugTarget->RehupSrcFiles();

	if (hasHotVData)
		mDebugTarget->mVDataHotIdx = hotIdx;

	for (int breakIdx = 0; breakIdx < (int)mBreakpoints.size(); breakIdx++)
	{
		auto breakpoint = mBreakpoints[breakIdx];
		CheckBreakpoint(breakpoint);
	}

	for (int hotThreadIdx = 0; hotThreadIdx < (int)mHotThreadStates.size(); hotThreadIdx++)
	{
		auto& hotThreadState = mHotThreadStates[hotThreadIdx];
		WdThreadInfo* threadInfo = NULL;
		if (!mThreadMap.TryGetValue((uint32)hotThreadState.mThreadId, &threadInfo))
			continue;

		DWORD prevSuspendCount = ::ResumeThread(threadInfo->mHThread);
		BfLogDbg("ResumeThread %d PrevCount:%d\n", threadInfo->mThreadId, (int)prevSuspendCount);
	}

	mHotThreadStates.Clear();

	if (IsPaused())
	{
		ClearCallStack();
		UpdateCallStack();
	}
}

void WinDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	delete mHotResolveData;
	mHotResolveData = NULL;

	mHotResolveData = new DbgHotResolveData();
	DbgHotScanner* hotScanner = new DbgHotScanner(this);
	hotScanner->Scan(flags);
	delete hotScanner;
}

intptr WinDebugger::GetDbgAllocHeapSize()
{
	if (mDbgHeapData == NULL)
	{
		Beefy::String memName = StrFormat("BFGC_stats_%d", mProcessInfo.dwProcessId);

		mDbgHeapData = new WinDbgHeapData();
		mDbgHeapData->mFileMapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, memName.c_str());
		if (mDbgHeapData->mFileMapping == 0)
		{
			delete mDbgHeapData;
			mDbgHeapData = NULL;
			return 0;
		}

		mDbgHeapData->mStats = (WinDbgHeapData::Stats*)MapViewOfFile(mDbgHeapData->mFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WinDbgHeapData::Stats));
	}

	if (mDbgHeapData->mStats == NULL)
		return 0;

	return mDbgHeapData->mStats->mHeapSize;
}

String WinDebugger::GetDbgAllocInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto threadInfo : mThreadList)
		::SuspendThread(threadInfo->mHThread);

	delete mHotResolveData;
	mHotResolveData = NULL;

	mHotResolveData = new DbgHotResolveData();
	DbgHotScanner* hotScanner = new DbgHotScanner(this);
	hotScanner->Scan((DbgHotResolveFlags)(DbgHotResolveFlag_Allocations | DbgHotResolveFlag_KeepThreadState));
	delete hotScanner;

	String result;

	if (mHotResolveData != NULL)
	{
		DbgExprEvaluator exprEvaluator(this, NULL, NULL, -1, -1);
		exprEvaluator.mDebugTarget = mDebugTarget;

		String typeName;

		result += ":types\n";

		for (int typeId = 0; typeId < mHotResolveData->mTypeData.size(); typeId++)
		{
			auto& typeData = mHotResolveData->mTypeData[typeId];
			if (typeData.mCount > 0)
			{
				auto type = exprEvaluator.GetBeefTypeById(typeId);
				typeName.Clear();
				exprEvaluator.BeefTypeToString(type, typeName);
				if (typeName.IsEmpty())
					typeName = StrFormat("Type #%d", typeId);
				result += StrFormat("type\t%d\t%s\t%lld\t%lld\n", typeId, typeName.c_str(), typeData.mCount, typeData.mSize);
			}
		}
	}

	for (auto threadInfo : mThreadList)
		::ResumeThread(threadInfo->mHThread);

	return result;
}

static bool CreatePipeWithSecurityAttributes(HANDLE& hReadPipe, HANDLE& hWritePipe, SECURITY_ATTRIBUTES* lpPipeAttributes, int32 nSize)
{
	hReadPipe = 0;
	hWritePipe = 0;
	bool ret = ::CreatePipe(&hReadPipe, &hWritePipe, lpPipeAttributes, nSize);
	if (!ret || (hReadPipe == INVALID_HANDLE_VALUE) || (hWritePipe == INVALID_HANDLE_VALUE))
		return false;
	return true;
}

static bool CreatePipe(HANDLE& parentHandle, HANDLE& childHandle, bool parentInputs)
{
	SECURITY_ATTRIBUTES securityAttributesParent = { 0 };
	securityAttributesParent.bInheritHandle = 1;

	HANDLE hTmp = INVALID_HANDLE_VALUE;
	if (parentInputs)
		CreatePipeWithSecurityAttributes(childHandle, hTmp, &securityAttributesParent, 0);
	else
		CreatePipeWithSecurityAttributes(hTmp, childHandle, &securityAttributesParent, 0);

	HANDLE dupHandle = 0;

	// Duplicate the parent handle to be non-inheritable so that the child process
	// doesn't have access. This is done for correctness sake, exact reason is unclear.
	// One potential theory is that child process can do something brain dead like
	// closing the parent end of the pipe and there by getting into a blocking situation
	// as parent will not be draining the pipe at the other end anymore.
	if (!::DuplicateHandle(GetCurrentProcess(), hTmp,
		GetCurrentProcess(), &dupHandle,
		0, false, DUPLICATE_SAME_ACCESS))
	{
		return false;
	}

	parentHandle = dupHandle;

	if (hTmp != INVALID_HANDLE_VALUE)
		::CloseHandle(hTmp);

	return true;
}

bool WinDebugger::DoOpenFile(const StringImpl& fileName, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, DbgOpenFileFlags openFileFlags)
{
	BP_ZONE("WinDebugger::DoOpenFile");

	AutoCrit autoCrit(mDebugManager->mCritSect);
	//gDbgPerfManager->StartRecording();

	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));

	DWORD flags = DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_DEFAULT_ERROR_MODE;
	BOOL inheritHandles = false;

	// set up the streams
	if ((openFileFlags & (DbgOpenFileFlag_RedirectStdInput | DbgOpenFileFlag_RedirectStdOutput | DbgOpenFileFlag_RedirectStdError)) != 0)
	{
		if ((openFileFlags & DbgOpenFileFlag_RedirectStdInput) != 0)
			CreatePipe(mStdInputPipe, si.hStdInput, true);
		else if (::GetConsoleWindow() != NULL)
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		else
			si.hStdInput = INVALID_HANDLE_VALUE;

		if ((openFileFlags & DbgOpenFileFlag_RedirectStdOutput) != 0)
			CreatePipe(mStdOutputPipe, si.hStdOutput, false);
		else
			si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

		if ((openFileFlags & DbgOpenFileFlag_RedirectStdError) != 0)
			CreatePipe(mStdErrorPipe, si.hStdError, false);
		else
			si.hStdError = GetStdHandle(STD_ERROR_HANDLE);		
		flags |= CREATE_NO_WINDOW;
		si.dwFlags = STARTF_USESTDHANDLES;
		inheritHandles = true;
	}

	if ((openFileFlags & DbgOpenFileFlag_Minimized) != 0)
	{
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWMINNOACTIVE;
	}

	if (mDbgProcessId != 0)
	{
		BOOL success = ::DebugActiveProcess(mDbgProcessId);
		if (!success)
			return false;

		mProcessInfo.dwProcessId = mDbgProcessId;
	}
	else
	{
		BP_ZONE("DoOpenFile_CreateProcessW");

		UTF16String envW;
		
		void* envPtr = NULL;
		if (!envBlock.IsEmpty())
		{
			//UTF16?
			if (envBlock[1] == 0)
			{
				envPtr = (void*)&envBlock[0];
				flags |= CREATE_UNICODE_ENVIRONMENT;
			}
			else
			{
				String str8((char*)&envBlock[0], (int)envBlock.size());
				envW = UTF8Decode(str8);

				envPtr = (void*)envW.c_str();
				flags |= CREATE_UNICODE_ENVIRONMENT;
			}
		}

		String cmdLine = "\"";
		cmdLine += fileName;
		cmdLine += "\"";

		if (!args.IsEmpty())
		{
			cmdLine += " ";
			cmdLine += args;
		}

		BOOL worked = CreateProcessW(NULL, (WCHAR*)UTF8Decode(cmdLine).c_str(), NULL, NULL, inheritHandles,
			flags, envPtr, (WCHAR*)UTF8Decode(workingDir).c_str(), &si, &mProcessInfo);

		if ((openFileFlags & DbgOpenFileFlag_RedirectStdInput) != 0)
			::CloseHandle(si.hStdInput);
		if ((openFileFlags & DbgOpenFileFlag_RedirectStdOutput) != 0)
			::CloseHandle(si.hStdOutput);
		if ((openFileFlags & DbgOpenFileFlag_RedirectStdError) != 0)
			::CloseHandle(si.hStdError);

		if (!worked)
		{
			if (IsHandleValid(mStdInputPipe))
			{
				::CloseHandle(mStdInputPipe);
				mStdInputPipe = 0;
			}
			if (IsHandleValid(mStdOutputPipe))
			{
				::CloseHandle(mStdOutputPipe);
				mStdOutputPipe = 0;
			}
			if (IsHandleValid(mStdErrorPipe))
			{
				::CloseHandle(mStdErrorPipe);
				mStdErrorPipe = 0;
			}

			auto lastError = ::GetLastError();
			if (lastError == ERROR_DIRECTORY)
			{
				mDebugManager->mOutMessages.push_back(StrFormat("error Unable to locate specified working directory '%s'", SlashString(workingDir, false, false).c_str()));
			}

			return false;
		}

		WdThreadInfo* threadInfo = new WdThreadInfo();
		threadInfo->mProcessId = mProcessInfo.dwProcessId;
		threadInfo->mThreadId = mProcessInfo.dwThreadId;
		threadInfo->mHThread = mProcessInfo.hThread;
		threadInfo->mThreadLocalBase = NULL;
		threadInfo->mStartAddress = NULL;
		mThreadMap[mProcessInfo.dwThreadId] = threadInfo;
		mThreadList.push_back(threadInfo);
	}

	mRunState = RunState_Running;

	while (true)
	{
		BP_ZONE("DoOpenFile_WaitForImageBase");
		autoCrit.mCritSect->Unlock();
		DoUpdate();
		autoCrit.mCritSect->Lock();

		ContinueDebugEvent();

		if ((mDebugTarget->mLaunchBinary != NULL) && (mDebugTarget->mLaunchBinary->mOrigImageData != NULL))
			break;
	}

	RehupBreakpoints(true);

	//gDbgPerfManager->StopRecording();
	//gDbgPerfManager->DbgPrint();

	return true;
}

void WinDebugger::StopDebugging()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	BfLogDbg("WinDebugger::Terminate\n");
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminated) || (mRunState == RunState_Terminating))
		return;

	if ((mDbgProcessId != 0) && ((mDbgAttachFlags & BfDbgAttachFlag_ShutdownOnExit) == 0))
	{
		for (auto address : mTempBreakpoint)
			RemoveBreakpoint(address);
		for (auto breakpoint : mBreakpoints)
			DetachBreakpoint(breakpoint);

		BfLogDbg("StopDebugging\n");
		::DebugActiveProcessStop(mDbgProcessId);
		mRunState = RunState_Terminated;
		BfLogDbg("mRunState = RunState_Terminated\n");
	}
	else
	{
		TerminateProcess(mProcessInfo.hProcess, 0);
		mRunState = RunState_Terminating;
		BfLogDbg("mRunState = RunState_Terminating\n");
	}
}

void WinDebugger::Terminate()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	BfLogDbg("WinDebugger::Terminate\n");
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminated) || (mRunState == RunState_Terminating))
		return;
	TerminateProcess(mProcessInfo.hProcess, 0);
	mRunState = RunState_Terminating;
	BfLogDbg("mRunState = RunState_Terminating\n");
}

static int gDebugUpdateCnt = 0;

void WinDebugger::Detach()
{
	BfLogDbg("Debugger Detach\n");

	mDebugManager->mNetManager->CancelAll();
	while ((mIsRunning) || (mDebuggerThreadId != 0))
	{
		mShuttingDown = true;
		Sleep(1);
	}

	for (auto profiler : mProfilerSet)
		profiler->Stop();

	BfLogDbg("Debugger Detach - thread finished\n");

	mPendingProfilerMap.Clear();
	for (auto profiler : mNewProfilerList)
		delete profiler;
	mNewProfilerList.Clear();

	mPendingImageLoad.Clear();
	mPendingDebugInfoLoad.Clear();

	RemoveTempBreakpoints();
	mContinueEvent.Reset();
	if (mDebugTarget != mEmptyDebugTarget)
		delete mDebugTarget;
	mDebugTarget = mEmptyDebugTarget;

	mShuttingDown = false;
	mStepSP = 0;
	ClearCallStack();
	mRunState = RunState_NotStarted;
	mStepType = StepType_None;
	mHadImageFindError = false;
	mIsPartialCallStack = true;

	delete mDebugPendingExpr;
	mDebugPendingExpr = NULL;

	for (auto threadPair : mThreadMap)
	{
		auto threadInfo = threadPair.mValue;
		delete threadInfo;
	}
	mThreadMap.Clear();
	mThreadList.Clear();
	mHotTargetMemory.Clear();

	// We don't need to close the hThread when we have attached to a process
	if (mDbgProcessId == 0)
	{
		CloseHandle(mProcessInfo.hThread);
		CloseHandle(mProcessInfo.hProcess);
	}

	for (auto breakpoint : mBreakpoints)
	{
		if (!mDestroying)
		{
			BF_FATAL("Breakpoints should be deleted already");
		}

		if (breakpoint->mMemoryBreakpointInfo != NULL)
		{
			DetachBreakpoint(breakpoint);
		}
	}

	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));
	mStepBreakpointAddrs.Clear();
	mIsRunning = false;

	mDbgAttachFlags = BfDbgAttachFlag_None;
	mDbgProcessId = 0;
	delete mDbgHeapData;
	mDbgHeapData = NULL;
	mDbgProcessHandle = 0;
	ClearCallStack();
	mWantsDebugContinue = false;
	mAtBreakThread = NULL;
	mActiveThread = NULL;
	mActiveBreakpoint = NULL;
	mSteppingThread = NULL;
	mExplicitStopThread = NULL;
	mIsContinuingFromException = false;
	mGotStartupEvent = false;

	mIsDebuggerWaiting = false;
	mPhysBreakpointAddrMap.Clear();
	mBreakpointAddrMap.Clear();

	gDebugUpdateCnt = 0;

	if (IsHandleValid(mStdInputPipe))
		::CloseHandle(mStdInputPipe);
	mStdInputPipe = INVALID_HANDLE_VALUE;

	if (IsHandleValid(mStdOutputPipe))
		::CloseHandle(mStdOutputPipe);
	mStdOutputPipe = INVALID_HANDLE_VALUE;
	
	if (IsHandleValid(mStdErrorPipe))
		::CloseHandle(mStdErrorPipe);
	mStdErrorPipe = INVALID_HANDLE_VALUE;
}

Profiler* WinDebugger::StartProfiling()
{
	return new DbgProfiler(this);
}

Profiler* WinDebugger::PopProfiler()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	if (mNewProfilerList.IsEmpty())
		return NULL;
	auto profiler = (DbgProfiler*)mNewProfilerList[0];
	mNewProfilerList.erase(mNewProfilerList.begin());
	return profiler;
}

void WinDebugger::AddProfiler(DbgProfiler * profiler)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mProfilerSet.Add(profiler);
}

void WinDebugger::RemoveProfiler(DbgProfiler * profiler)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mProfilerSet.Remove(profiler);
}

void WinDebugger::ReportMemory(MemReporter* memReporter)
{
	mEmptyDebugTarget->ReportMemory(memReporter);
	if (mDebugTarget != mEmptyDebugTarget)
		mDebugTarget->ReportMemory(memReporter);
}

bool WinDebugger::GetEmitSource(const StringImpl& filePath, String& outText)
{
	if (!filePath.StartsWith("$Emit"))
		return false;

	int dollarPos = filePath.IndexOf('$', 1);
	String numStr = filePath.Substring(5, dollarPos - 5);
	int id = atoi(numStr.c_str());

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (dbgModule->mId == id)
			return dbgModule->GetEmitSource(filePath, outText);
	}

	return false;
}

void WinDebugger::ModuleChanged(DbgModule* dbgModule)
{
	mDebugManager->mOutMessages.push_back(String("dbgInfoLoaded ") + dbgModule->mFilePath);
}

bool WinDebugger::DoUpdate()
{
	if ((mDbgProcessId != 0) && ((mDbgAttachFlags & BfDbgAttachFlag_ShutdownOnExit) == 0))
		::DebugSetProcessKillOnExit(FALSE);
	else
		::DebugSetProcessKillOnExit(TRUE);

	//
	{
		AutoCrit autoCrit(mDebugManager->mCritSect);
		auto _ModuleChanged = [&](DbgModule* dbgModule)
		{
			ModuleChanged(dbgModule);
			ClearCallStack(); // We may have actual dbgSubprograms and stuff now...
		};

		for (auto dbgModule : mPendingImageLoad)
		{
			dbgModule->PreCacheImage();
		}

		for (auto kv : mPendingDebugInfoLoad)
		{
			kv.mKey->PreCacheDebugInfo();
		}

		while (!mPendingImageLoad.IsEmpty())
		{
			auto dbgModule = mPendingImageLoad.back();
			mPendingImageLoad.pop_back();
			dbgModule->RequestImage();
			_ModuleChanged(dbgModule);
		}

		if (!mPendingDebugInfoLoad.IsEmpty())
		{
			Array<DbgPendingDebugInfoLoad> pendingList;
			for (auto kv : mPendingDebugInfoLoad)
				pendingList.Add(kv.mValue);
			mPendingDebugInfoLoad.Clear();

			for (auto& entry : pendingList)
			{
				auto dbgModule = entry.mModule;
				entry.mModule->RequestDebugInfo(entry.mAllowRemote);
				// We do a "_ModuleChanged" even if the load failed, so we rehup the callstack and stop
				//  saying "<Loading...>"
				_ModuleChanged(entry.mModule);
			}
		}
	}

	if (IsMiniDumpDebugger())
	{
		//
		{
			AutoCrit autoCrit(mDebugManager->mCritSect);
			if (mRunState == RunState_Terminating)
			{
				mRunState = RunState_Terminated;
				return false;
			}
		}

		Sleep(20);
		return false;
	}

	if (mIsDebuggerWaiting)
	{
		if ((IsInRunState()) || (mRunState == RunState_Terminating) || (mRunState == RunState_DebugEval))
			ContinueDebugEvent();
		if (mContinueEvent.WaitFor(8))
		{
			BF_ASSERT(!mWantsDebugContinue); // mWantsDebugContinue should already been reset
			BfLogDbg("::ContinueDebugEvent 1 ThreadId:%d\n", mDebuggerWaitingThread->mThreadId);
			BF_ASSERT_REL(mDebuggerWaitingThread->mIsAtBreakpointAddress == 0);
			::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, mIsContinuingFromException ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE);
			mIsContinuingFromException = false;
			mIsDebuggerWaiting = false;
			mDebuggerWaitingThread = NULL;
		}
		else
			return false;
	}

	if (!WaitForDebugEvent(&mDebugEvent, 8))
		return false;

	gDebugUpdateCnt++;

	static const char* eventNames[] = { "DBG_EVENT ?",
		"EXCEPTION_DEBUG_EVENT",
		"CREATE_THREAD_DEBUG_EVENT",
		"CREATE_PROCESS_DEBUG_EVENT",
		"EXIT_THREAD_DEBUG_EVENT",
		"EXIT_PROCESS_DEBUG_EVENT",
		"LOAD_DLL_DEBUG_EVENT",
		"UNLOAD_DLL_DEBUG_EVENT",
		"OUTPUT_DEBUG_STRING_EVENT",
		"RIP_EVENT"};

	BfLogDbg("WaitForDebugEvent %s ThreadId:%d\n", eventNames[mDebugEvent.dwDebugEventCode], mDebugEvent.dwThreadId);

	BP_ZONE(eventNames[mDebugEvent.dwDebugEventCode]);

	AutoCrit autoCrit(mDebugManager->mCritSect);

	mActiveBreakpoint = NULL;
	mIsDebuggerWaiting = true;
	mWantsDebugContinue = true;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mShowPCOverride = 0;

	WdThreadInfo* threadInfo = NULL;

	mThreadMap.TryGetValue(mDebugEvent.dwThreadId, &threadInfo);

	mDebuggerWaitingThread = threadInfo;
	mExplicitStopThread = mDebuggerWaitingThread;

	switch (mDebugEvent.dwDebugEventCode)
	{
	case CREATE_PROCESS_DEBUG_EVENT:
		{
			if (threadInfo == NULL)
			{
				BF_ASSERT(mThreadMap.size() == 0);

				WdThreadInfo* newThreadInfo = new WdThreadInfo();
				newThreadInfo->mProcessId = mDebugEvent.dwProcessId;
				newThreadInfo->mThreadId = mDebugEvent.dwThreadId;
				newThreadInfo->mHThread = mDebugEvent.u.CreateProcessInfo.hThread;
				newThreadInfo->mThreadLocalBase = mDebugEvent.u.CreateProcessInfo.lpThreadLocalBase;
				newThreadInfo->mStartAddress = (void*)mDebugEvent.u.CreateProcessInfo.lpStartAddress;

				BF_CONTEXT lcContext;
				lcContext.ContextFlags = BF_CONTEXT_CONTROL;
				BF_GetThreadContext(newThreadInfo->mHThread, &lcContext);
				newThreadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

				mThreadMap[mDebugEvent.dwThreadId] = newThreadInfo;
				mDebuggerWaitingThread = newThreadInfo;
				mThreadList.push_back(mDebuggerWaitingThread);
				UpdateThreadDebugRegisters();

				if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ThreadCreateMessages))
					OutputMessage(StrFormat("Creating thread from CREATE_PROCESS_DEBUG_EVENT %d\n", mDebugEvent.dwThreadId));

				threadInfo = mDebuggerWaitingThread;
				mProcessInfo.dwThreadId = threadInfo->mThreadId;
				mProcessInfo.hThread = threadInfo->mHThread;
				mProcessInfo.hProcess = mDebugEvent.u.CreateProcessInfo.hProcess;
			}
			else
			{
				threadInfo->mThreadLocalBase = mDebugEvent.u.CreateProcessInfo.lpThreadLocalBase;
				threadInfo->mStartAddress = (void*)mDebugEvent.u.CreateProcessInfo.lpStartAddress;
			}

			BF_CONTEXT lcContext;
			lcContext.ContextFlags = BF_CONTEXT_CONTROL;
			BF_GetThreadContext(threadInfo->mHThread, &lcContext);
			threadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

			DbgModule* launchBinary = mDebugTarget->Init(mLaunchPath, mTargetPath, (addr_target)(intptr)mDebugEvent.u.CreateProcessInfo.lpBaseOfImage);
			addr_target gotImageBase = (addr_target)(intptr)mDebugEvent.u.CreateProcessInfo.lpBaseOfImage;
			if (launchBinary->mImageBase != gotImageBase)
			{
				BF_FATAL("Image base didn't match");
			}

			launchBinary->mImageBase = gotImageBase;
			launchBinary->mImageSize = (int)launchBinary->GetImageSize();
			launchBinary->mOrigImageData = new DbgModuleMemoryCache(launchBinary->mImageBase, launchBinary->mImageSize);

			if (launchBinary == mDebugTarget->mTargetBinary)
				mDebugTarget->SetupTargetBinary();

			if (mDebugEvent.u.CreateProcessInfo.hFile != NULL)
				CloseHandle(mDebugEvent.u.CreateProcessInfo.hFile);

			mDbgProcessHandle = mDebugEvent.u.CreateProcessInfo.hProcess;
			mDbgThreadHandle = mDebugEvent.u.CreateProcessInfo.hThread;

			mGotStartupEvent = true;
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case EXIT_PROCESS_DEBUG_EVENT:
		{
			BfLogDbg("EXIT_PROCESS_DEBUG_EVENT\n");

			DWORD exitCode = mDebugEvent.u.ExitProcess.dwExitCode;

			String exitMessage;
			switch (exitCode)
			{
			case STATUS_DLL_NOT_FOUND:
				exitMessage = "STATUS_DLL_NOT_FOUND";
				break;
			case STATUS_DLL_INIT_FAILED:
				exitMessage = "STATUS_DLL_INIT_FAILED";
				break;
			case STATUS_ENTRYPOINT_NOT_FOUND:
				exitMessage = "STATUS_ENTRYPOINT_NOT_FOUND";
				break;
			}

			String exitCodeStr;
			if ((exitCode >= 0x10000000) && (exitCode <= 0xF7000000))
				exitCodeStr = StrFormat("0x%X", exitCode);
			else
				exitCodeStr = StrFormat("%d", exitCode);

			if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ProcessExitMessages))
			{
				if (!exitMessage.IsEmpty())
					OutputMessage(StrFormat("Process terminated. ExitCode: %s (%s).\n", exitCodeStr.c_str(), exitMessage.c_str()));
				else
					OutputMessage(StrFormat("Process terminated. ExitCode: %s.\n", exitCodeStr.c_str()));
			}

			mRunState = RunState_Terminated;
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case LOAD_DLL_DEBUG_EVENT:
		{
			WCHAR moduleNameStr[MAX_PATH] = { 0 };
			GetFinalPathNameByHandleW(mDebugEvent.u.LoadDll.hFile, moduleNameStr, MAX_PATH, FILE_NAME_NORMALIZED);

			std::wstring wow64Dir;
			std::wstring systemDir;

			PWSTR wow64DirPtr = NULL;
			SHGetKnownFolderPath(FOLDERID_SystemX86, KF_FLAG_NO_ALIAS, NULL, &wow64DirPtr);
			if (wow64DirPtr != NULL)
			{
				wow64Dir = wow64DirPtr;
				CoTaskMemFree(wow64DirPtr);
			}

			PWSTR systemDirPtr = NULL;
			SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_NO_ALIAS, NULL, &systemDirPtr);
			if (systemDirPtr != NULL)
			{
				systemDir = systemDirPtr;
				CoTaskMemFree(systemDirPtr);
			}

			if ((mDebugEvent.u.LoadDll.lpImageName != 0) && (mDebugEvent.u.LoadDll.fUnicode))
			{
				addr_target strAddr = ReadMemory<addr_target>((addr_target)(intptr)mDebugEvent.u.LoadDll.lpImageName);

				for (int i = 0; i < MAX_PATH - 1; i++)
				{
					WCHAR c = ReadMemory<WCHAR>(strAddr + i*2);
					moduleNameStr[i] = (WCHAR)c;
					if (c == 0)
						break;
				}
			}

			String origModuleName = UTF8Encode(moduleNameStr);
			String moduleName = origModuleName;

			String loadMsg;
			HANDLE altFileHandle = INVALID_HANDLE_VALUE;
			if (moduleName != origModuleName)
			{
				loadMsg = StrFormat("Loading DLL: %s(%s) @ %s", origModuleName.c_str(), moduleName.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.LoadDll.lpBaseOfDll, true).c_str());
				altFileHandle = ::CreateFileW(UTF8Decode(moduleName).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			}
			else
			{
				loadMsg = StrFormat("Loading DLL: %s @ %s", moduleName.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.LoadDll.lpBaseOfDll, true).c_str());
			}

			BfLogDbg("LOAD_DLL_DEBUG_EVENT %s\n", moduleName.c_str());

			bool skipLoad = false;
#ifdef BF_DBG_32
			if (((uintptr)mDebugEvent.u.LoadDll.lpBaseOfDll & 0xFFFFFFFF00000000LL) != 0)
			{
				skipLoad = true;
				loadMsg += " - Skipped";
			}
#endif

			if (!skipLoad)
			{
				FileHandleStream stream;
				stream.mFileHandle = mDebugEvent.u.LoadDll.hFile;
				if (altFileHandle != INVALID_HANDLE_VALUE)
					stream.mFileHandle = altFileHandle;
				if (mDebugTarget->SetupDyn(moduleName, &stream, (intptr)mDebugEvent.u.LoadDll.lpBaseOfDll) == NULL)
					loadMsg += " - Failed to load";
				stream.mFileHandle = 0;
			}

			if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ModuleLoadMessages))
				OutputMessage(loadMsg + "\n");

			if (altFileHandle != INVALID_HANDLE_VALUE)
				::CloseHandle(altFileHandle);
			::CloseHandle(mDebugEvent.u.LoadDll.hFile);

			// Try to bind any breakpoints tied to this DLL
			RehupBreakpoints(true);
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case UNLOAD_DLL_DEBUG_EVENT:
		{
			bool needsBreakpointRehup = false;

			String name = "???";
			DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress((addr_target)(intptr)mDebugEvent.u.UnloadDll.lpBaseOfDll);
			if (dbgModule != NULL)
			{
				name = dbgModule->mFilePath;

				for (int i = 0; i < (int)mBreakpoints.size(); i++)
				{
					auto breakpoint = mBreakpoints[i];
					auto checkBreakpoint = breakpoint;
					bool hasAddr = false;
					while (checkBreakpoint != NULL)
					{
						if ((checkBreakpoint->mAddr >= dbgModule->mImageBase) && (checkBreakpoint->mAddr < dbgModule->mImageBase + dbgModule->mImageSize))
							hasAddr = true;
						checkBreakpoint = (WdBreakpoint*)checkBreakpoint->mLinkedSibling;
					}

					if (hasAddr)
					{
						DetachBreakpoint(breakpoint);
						needsBreakpointRehup = true;
					}
				}

				bool hadTarget = mDebugTarget->mTargetBinary != NULL;
				mDebugTarget->UnloadDyn(dbgModule->mImageBase);
				if (needsBreakpointRehup)
					RehupBreakpoints(true);

				if ((mDebugTarget->mTargetBinary == NULL) && (hadTarget))
				{
					mRunState = RunState_TargetUnloaded;
				}

				mPendingDebugInfoLoad.Remove(dbgModule);
				mPendingDebugInfoRequests.Remove(dbgModule);
				mDebugManager->mOutMessages.push_back("modulesChanged");
			}

			if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ModuleUnloadMessages) && !name.empty())
				OutputMessage(StrFormat("Unloading DLL: %s @ %0s\n", name.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.UnloadDll.lpBaseOfDll, true).c_str()));

			BfLogDbg("UNLOAD_DLL_DEBUG_EVENT %s\n", name.c_str());
		}
		break;
	case OUTPUT_DEBUG_STRING_EVENT:
		{
			const int maxChars = 1024 * 1024;
			int len = BF_MIN(maxChars, (int)mDebugEvent.u.DebugString.nDebugStringLength); // 1MB max
			char* message = new char[len + 1];

			message[0] = 0;
			message[len] = 0;
			ReadMemory((addr_target)(intptr)mDebugEvent.u.DebugString.lpDebugStringData, len, message);

			if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId == mDebugEvalThreadInfo.mThreadId))
				mDebugManager->mOutMessages.push_back(String("dbgEvalMsg ") + message);
			else
				mDebugManager->mOutMessages.push_back(String("msg ") + message);

			BfLogDbg("OUTPUT_DEBUG_STRING_EVENT (BreakAddr:%@): %s\n", threadInfo->mIsAtBreakpointAddress, message);

			BF_ASSERT_REL(threadInfo->mIsAtBreakpointAddress == 0);

			delete [] message;
		}
		break;
	case CREATE_THREAD_DEBUG_EVENT:
		{
			WdThreadInfo* threadInfo = new WdThreadInfo();
			threadInfo->mProcessId = mDebugEvent.dwProcessId;
			threadInfo->mThreadId = mDebugEvent.dwThreadId;
			threadInfo->mHThread = mDebugEvent.u.CreateThread.hThread;
			threadInfo->mThreadLocalBase = mDebugEvent.u.CreateThread.lpThreadLocalBase;
			threadInfo->mStartAddress = (void*)mDebugEvent.u.CreateThread.lpStartAddress;

			BF_CONTEXT lcContext;
			lcContext.ContextFlags = BF_CONTEXT_CONTROL;
			BF_GetThreadContext(threadInfo->mHThread, &lcContext);
			threadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

			mThreadMap[mDebugEvent.dwThreadId] = threadInfo;
			mDebuggerWaitingThread = threadInfo;
			mThreadList.push_back(mDebuggerWaitingThread);
			UpdateThreadDebugRegisters();
			if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ThreadCreateMessages))
				OutputMessage(StrFormat("Creating thread %d\n", mDebugEvent.dwThreadId));
		}
		break;
	case EXIT_THREAD_DEBUG_EVENT:
		{
			if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_ThreadExitMessages))
				OutputMessage(StrFormat("Exiting thread %d\n", mDebugEvent.dwThreadId));

			if (mSteppingThread == threadInfo)
			{
				// We were attempting stepping on this thread, but not anymore!
				ClearStep();
			}
			::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_CONTINUE);
			mIsDebuggerWaiting = false;
			mWantsDebugContinue = false;

			if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId == mDebugEvalThreadInfo.mThreadId))
			{
				// Thread terminated while evaluating! Is there a more graceful way of handling this?
				CleanupDebugEval(false);
				mRunState = RunState_Running;
			}

			mThreadList.Remove(mDebuggerWaitingThread);

			delete mDebuggerWaitingThread;
			mDebuggerWaitingThread = NULL;
			mThreadMap.Remove(mDebugEvent.dwThreadId);
			return true;
		}
		break;
	case RIP_EVENT:
		OutputMessage("RIP Event\n");
		break;
	case EXCEPTION_DEBUG_EVENT:
		{
			auto exceptionRecord = &mDebugEvent.u.Exception.ExceptionRecord;

			switch (exceptionRecord->ExceptionCode)
			{
			case STATUS_WX86_BREAKPOINT:
			case EXCEPTION_BREAKPOINT:
				{
					if (mRunState == RunState_Terminating)
					{
						BfLogDbg("Ignoring event because of RunState_Terminating\n");
						break;
					}

					mAtBreakThread = threadInfo;
					mActiveThread = mAtBreakThread;

					bool isHighAddr = false;
#ifdef BF_DBG_32
					if (((uintptr)exceptionRecord->ExceptionAddress & 0xFFFFFFFF00000000) != 0)
					{
						if (mActiveThread == mThreadList.front())
						{
							// Skip the initial Wow64 ntdll.dll!LdrpDoDebuggerBreak
							mRunState = RunState_Running;
							break;
						}
						isHighAddr = true;
					}
#endif

					addr_target pcAddress = (addr_target)(intptr)exceptionRecord->ExceptionAddress;
					if (isHighAddr)
						pcAddress = (addr_target)-1;
					//mStoppedAtAddress = pcAddress;

					bool isStepOut = false;

					if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto))
					{
						isStepOut = mStepBreakpointAddrs.Contains(pcAddress);
					}

					BF_CONTEXT lcContext;
					lcContext.ContextFlags = BF_CONTEXT_ALL;
					BF_GetThreadContext(threadInfo->mHThread, &lcContext);

					BfLogDbg("EXCEPTION_BREAKPOINT Thread:%d %p SP:%p\n", mActiveThread->mThreadId, pcAddress, BF_CONTEXT_SP(lcContext));

					uint8 origImageData = 0xCC;
					mDebugTarget->ReadOrigImageData(pcAddress, &origImageData, 1);
					bool wasDebugBreakpoint = origImageData != 0xCC;

					DbgSubprogram* dwSubprogram = NULL;
					DbgLineData* dwLineData = NULL;
					if (!isStepOut)
					{
						dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
						if (dwSubprogram == NULL)
							dwSubprogram = mDebugTarget->FindSubProgram(pcAddress, DbgOnDemandKind_LocalOnly);
					}

					bool isLineStart = (dwLineData != NULL) && (dwSubprogram->GetLineAddr(*dwLineData) == pcAddress);
					bool isNonDebuggerBreak = false;

					if (wasDebugBreakpoint)
					{
						// Go ahead and set EIP back one instruction
						BF_CONTEXT_IP(lcContext)--;
						BF_SetThreadContext(threadInfo->mHThread, &lcContext);

						if ((dwSubprogram != NULL) && (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid) &&
							(pcAddress == dwSubprogram->mBlock.mLowPC))
						{
							BfLogDbg("Hit HotReplaceKind_Invalid breakpoint\n");
							mRunState = RunState_Paused;
							mDebugManager->mOutMessages.push_back("error This lambda was replaced by a new version that has incompatible captures. A program restart is required.");
							PhysRemoveBreakpoint(pcAddress);
							break;
						}
					}
					else
					{
						// This was an actual "break" instruction
						BfLogDbg("Non-debugger break\n");
						isNonDebuggerBreak = true;

						auto prevState = mRunState;

						// Make it an "auto" stop, so for example when we have an assert/retry we won't stop inside assembly
						mRequestedStackFrameIdx = -2;

						mRunState = RunState_Paused;
						CheckNonDebuggerBreak();
						if (IsInRunState())
						{
							BF_ASSERT((prevState == RunState_Running) || (prevState == RunState_DebugEval));
							mRunState = prevState;
							break; // Continue as if nothing happened
						}

						if (prevState == RunState_DebugEval)
							mRequestedStackFrameIdx = -1; // Don't show a rolled back stack idx if a debug eval fails

						ClearStep();
					}

					if (threadInfo->mIsBreakRestorePaused)
					{
						// The thread is supposed to be paused, but the IP has been reset
						//  so just break here so we'll hit that breakpoint again once we're
						//  actually unpaused properly
						BfLogDbg("Ignoring EXCEPTION_BREAKPOINT\n", threadInfo->mThreadId);
						break;
					}

					if ((mRunState == RunState_DebugEval) || (mRunState == RunState_HotStep))
					{
						// If we hit a breakpoint while doing a debug eval, we just remove the breakpoint
						//  and expect to reinstate it during a rehup after the evaluation has completed
						WdBreakpoint* breakpoint = (WdBreakpoint*)FindBreakpointAt((uintptr_t) exceptionRecord->ExceptionAddress);
						if (breakpoint != NULL)
						{
							mNeedsRehupBreakpoints = true;
							RemoveBreakpoint(breakpoint->mLineData.GetAddress());
						}
						break;
					}

					bool isDeeper = false;

					int stepBreakAddrIdx = (int)mStepBreakpointAddrs.IndexOf(pcAddress);

					WdBreakpoint* breakpoint = NULL;
					bool ignoreBreakpoint = false;

					if ((mStepType != StepType_None) && (mSteppingThread == mAtBreakThread))
					{
						if (mStepType == StepType_ToTempBreakpoint)
						{
							RemoveTempBreakpoints();
							mRunState = RunState_Paused;
							break;
						}

						if (mContinueFromBreakpointFailed)
						{
							BfLogDbg("Continuing from ContinueFromBreakpointFailed\n");
							SetupStep(mStepType);
							mRunState = RunState_Running;
							break;
						}

						if (!isStepOut)
							breakpoint = (WdBreakpoint*)FindBreakpointAt(pcAddress);

						// Ignore breakpoint if it's on the line we're stepping off of
						if ((breakpoint != NULL) && (breakpoint->mAddr == mStepPC) &&
							(mStepSP == BF_CONTEXT_SP(lcContext)))
						{
							ignoreBreakpoint = true;
						}
						else if ((breakpoint != NULL) && (stepBreakAddrIdx == -1) && (!CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress)))
						{
							ignoreBreakpoint = true;
						}

						if ((stepBreakAddrIdx == -1) && (breakpoint == NULL) && (!isNonDebuggerBreak))
						{
							// If a breakpoint is removed in a prior thread
							BfLogDbg("Ignoring step break (old breakpoint)\n");

							if ((mSteppingThread == mAtBreakThread) && (mStepSwitchedThreads))
							{
								SetupStep(mStepType);
							}
							break;
						}

						if ((stepBreakAddrIdx != -1) && (breakpoint == NULL) && (mSteppingThread != mActiveThread))
						{
							BfLogDbg("Ignoring break (wrong thread)\n");
							ThreadRestorePause(mSteppingThread, mActiveThread);
							threadInfo->mIsAtBreakpointAddress = pcAddress;
							break;
						}

						isDeeper = mStepSP > BF_CONTEXT_SP(lcContext);
						if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto))
						{
							isDeeper = mStepSP >= BF_CONTEXT_SP(lcContext);
							BfLogDbg("StepOut Iteration SP:%p StartSP:%p IsDeeper:%d\n", BF_CONTEXT_SP(lcContext), mStepSP, isDeeper);
						}

						if (((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto)) && (breakpoint == NULL) && (isDeeper))
						{
							// We're encountered recursion

							// Make sure we don't already have one of these stored
							BF_ASSERT(mStoredReturnValueAddr == 0);
							threadInfo->mIsAtBreakpointAddress = pcAddress;
							break; // Don't fall through, we don't want to set mIsAtBreakpointAddress
						}

						if (isStepOut)
						{
							threadInfo->mIsAtBreakpointAddress = pcAddress;

							if (mStepType == StepType_StepOut_ThenInto)
							{
								dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
								if ((dwLineData != NULL) && (pcAddress == dwSubprogram->GetLineAddr(*dwLineData)))
								{
									// Our step out from a filtered function put us at the start of a new line. Stop here
									// <do nothing>
								}
								else
								{
									// .. otherwise keep going until we get to the start of a new line
									SetupStep(StepType_StepInto);
									mRunState = RunState_Running;
									break;
								}
							}

							if (!mStepInAssembly)
							{
								// Keep stepping out until we find a frame that we have source for
								DbgSubprogram* dwSubprogram = NULL;
								DbgLineData* dwLineData = FindLineDataAtAddress(BF_CONTEXT_IP(lcContext), &dwSubprogram);
								if (dwLineData == NULL)
								{
									SetupStep(StepType_StepOut);
									break;
								}

								if ((dwLineData->mColumn == -1) && (!dwSubprogram->HasValidLines()))
								{
									// This is a method we don't actually want to be in, it has no valid lines!
									SetupStep(StepType_StepOut);
									break;
								}

								if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
								{
									// We've stepped out, but right into the start of an inlined method, so step out of this inlined method now...
									SetupStep(StepType_StepOut);
									break;
								}
							}

							ClearStep();
							mRunState = RunState_Paused;
							threadInfo->mStoppedAtAddress = pcAddress;

							break;
						}

						mRunState = RunState_Paused;
						if (breakpoint != NULL)
						{
							// While stepping we hit a legit breakpoint
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;

							// Ignore breakpoint on return statement if we're return-stepping
							mRunState = RunState_Breakpoint;
						}

						if ((mStepType == StepType_StepInto) && (dwSubprogram != NULL))
						{
							// Don't filter out the current subprogram (would break cases where we explicitly stepped into or hit breakpoint in a filtered subprogram)
							bool isInStartSubprogram = (mStepStartPC >= dwSubprogram->mBlock.mLowPC) && (mStepStartPC < dwSubprogram->mBlock.mHighPC);
							if ((!isInStartSubprogram) && (IsStepFiltered(dwSubprogram, dwLineData)))
							{
								BfLogDbg("Hit step filter\n");
								mRunState = RunState_Running;
								SetupStep(StepType_StepOut_ThenInto);
								break;
							}
						}

						if ((mStepType == StepType_StepOver) && (stepBreakAddrIdx == 0) && (mStepBreakpointAddrs[0] != 0) && (mStepBreakpointAddrs.size() > 1))
						{
							// Break was on the 'call' instruction, not the instruction after it -- means recursion
							BfLogDbg("StepOver detected recursing\n");
							mStepIsRecursing = true;
							if (mTempBreakpoint.Remove(mStepBreakpointAddrs[0]))
							{
								RemoveBreakpoint(mStepBreakpointAddrs[0]);
							}
							mStepBreakpointAddrs[0] = 0;
							mRunState = RunState_Running;
							break;
						}

						if ((mStepType == StepType_StepOver) && (stepBreakAddrIdx > 0) && (mStepBreakpointAddrs[0] != 0) && (isDeeper))
						{
							// This is the first time we've hit the target breakpoint.
							if (HasSteppedIntoCall())
							{
								mStepIsRecursing = true;
								RemoveBreakpoint(mStepBreakpointAddrs[0]);
								mStepBreakpointAddrs[0] = 0;
								//mStepBreakpointAddrs.erase(mStepBreakpointAddrs.begin());
							}
						}

						if ((mStepType == StepType_StepOver) && (mStepIsRecursing) && (stepBreakAddrIdx != -1) && (isDeeper))
						{
							// Decrement so the equality test on "step out" marks us as not being deeper when we
							//  hit the expected SP
							BfLogDbg("Converting StepOver to StepOut\n");
							mStepSP--;
							mStepType = StepType_StepOut_ThenInto;
							//SetupStep(StepType_StepOut);
							mRunState = RunState_Running;
							break;
						}

						if ((mStepType == StepType_StepOver) && (!ignoreBreakpoint) && (breakpoint == NULL) && (!mStepInAssembly))
						{
							// Test for stepping over inline method
							DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);

							// mTempBreakpoints will have 2 entries if we are on a 'call' line.  If we have an inlined call immediately following a call, then we
							//  assume we're hitting a return break
							/*if ((dwSubprogram != NULL) && (dwSubprogram->mInlineParent != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC) && (mTempBreakpoint.size() < 2))
							{
								BfLogDbg("Attempting StepOver of inlined method\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							} */

							//TODO: The previous logic with the "(mTempBreakpoint.size() < 2)" was causing Try!(Method()); stepovers to enter into Try!.  What did we mean by
							//  "assume we're hitting a return break"?
							if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
							{
								RemoveTempBreakpoints();
								BfLogDbg("Attempting StepOver of inlined method\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							}
						}

						if (mStepType == StepType_StepOut_Inline)
						{
							if (mOrigStepType == StepType_StepOver)
							{
								// For the step over, if we are still inside the source line after an inline then step over again...
								DbgSubprogram* origSubprogram = NULL;
								auto origLineData = FindLineDataAtAddress(mStepStartPC, &origSubprogram);
								DbgSubprogram* curSubprogram = NULL;
								auto curLineData = FindLineDataAtAddress(pcAddress, &curSubprogram);
								if ((origLineData != NULL) &&
									((origLineData == curLineData) ||
									 ((origSubprogram == curSubprogram) && (origLineData->mLine == curLineData->mLine))))
								{
									mRunState = RunState_Running;
									SetupStep(StepType_StepOver);
									break;
								}
							}

							ClearStep();
							break;
						}

						if ((mStepType != StepType_None) && (ignoreBreakpoint) && (!mStepInAssembly) && (stepBreakAddrIdx == -1))
						{
							// Ignore breakpoint by just continuing...
							mRunState = RunState_Running;
							break;
						}

						RemoveTempBreakpoints();

						if ((mStepType != StepType_None) && (!mStepInAssembly)  && (!isLineStart) && (stepBreakAddrIdx != -1))
						{
							SetupStep(mStepType);
							mRunState = RunState_Running;
						}
						else
						{
							//if (mStepType != StepType_Return)
							if (stepBreakAddrIdx != -1)
							{
								// Even if we've detected we're at a breakpoint, we mark ourselves as just stepping if we also
								//  have a step breakpoint here
								StepLineTryPause(pcAddress, true);
							}

							if (mRunState == RunState_Paused)
								ClearStep();
						}

						if (ignoreBreakpoint)
						{
							SetupStep(mStepType);
							mRunState = RunState_Running;
						}

						if ((mRunState == RunState_Paused) && (breakpoint != NULL))
						{
							// Just do the 'check' here so we can do the logging/condition stuff
							CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress);
						}
					}
					else
					{
						breakpoint = (WdBreakpoint*)FindBreakpointAt((uintptr_t)exceptionRecord->ExceptionAddress);
						if ((breakpoint != NULL) && (!CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress)))
						{
							ClearCallStack();
							BfLogDbg("Skipping conditional breakpoint. Setting mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;
							mRunState = RunState_Running;
							break;
						}
						if (breakpoint != NULL)
						{
							BfLogDbg("Breakpoint hit. mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;
							mRunState = RunState_Breakpoint;
						}
						else if ((stepBreakAddrIdx != -1) || (isNonDebuggerBreak))
						{
							if (mRunState != RunState_DebugEval)
							{
								// Was in mStepBreakpointAddrs list
								if ((isNonDebuggerBreak) || (mStepType == StepType_None) || (mSteppingThread == mAtBreakThread))
								{
									BfLogDbg("Hit mStepBreakpointAddrs breakpoint\n");
									mRunState = RunState_Paused;
								}
								else
								{
									BfLogDbg("Ignored mStepBreakpointAddrs breakpoint (wrong thread)\n");
									mRunState = RunState_Running;
								}
							}
						}
						else
						{
							BfLogDbg("Ignoring break (old or ignored breakpoint)\n");
							mRunState = RunState_Running;
						}
					}

					if ((breakpoint != NULL) && (!ignoreBreakpoint))
					{
						mActiveBreakpoint = breakpoint;
						mBreakStackFrameIdx = -1;
					}

					if ((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint))
						threadInfo->mStoppedAtAddress = pcAddress;
				}
				break;
			case STATUS_WX86_SINGLE_STEP:
			case EXCEPTION_SINGLE_STEP:
				{
					if (mRunState == RunState_Terminating)
					{
						BfLogDbg("Ignoring event because of RunState_Terminating\n");
						break;
					}

					if ((mStepSwitchedThreads) && (mActiveThread == mSteppingThread) && (mActiveThread->mIsAtBreakpointAddress != NULL))
					{
						ContinueFromBreakpoint();
						break;
					}

					if (mRunState == RunState_HotStep)
					{
						BF_ASSERT(mActiveThread == mDebuggerWaitingThread);
						mRunState = RunState_Paused;
						break;
					}

					mActiveThread = mDebuggerWaitingThread;

					BF_CONTEXT lcContext;
					lcContext.ContextFlags = BF_CONTEXT_ALL;
					BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
					addr_target pcAddress = BF_CONTEXT_IP(lcContext);

					bool wasUnfilteredStep = mStepType == StepType_StepInto_Unfiltered;
					if (mStepType == StepType_StepInto_UnfilteredSingle)
					{
						wasUnfilteredStep = true;
						mStepType = StepType_StepInto;
						mStepStartPC = pcAddress;
					}

					BfLogDbg("EXCEPTION_SINGLE_STEP Thread:%d PC:%p\n", mActiveThread->mThreadId, exceptionRecord->ExceptionAddress);

					if (lcContext.Dr6 & 0x0F) // Memory breakpoint hit
					{
						WdBreakpoint* foundBreakpoint = NULL;
						for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
						{
							if ((lcContext.Dr6 & ((intptr_target)1 << memoryWatchSlot)) != 0)
							{
								foundBreakpoint = mMemoryBreakpoints[memoryWatchSlot].mBreakpoint;
								break;
							}
						}

						if (foundBreakpoint == NULL)
						{
							BfLogDbg("Unknown memory breakpoint hit %p\n", pcAddress);
							mDebugManager->mOutMessages.push_back(StrFormat("memoryBreak %s", EncodeDataPtr(pcAddress, false).c_str()));
							mRunState = RunState_Paused;							
							break;
						}
						else
						{
							DbgSubprogram* subprogram = mDebugTarget->FindSubProgram(pcAddress);
							if (CheckConditionalBreakpoint(foundBreakpoint, subprogram, pcAddress))
							{
								if (foundBreakpoint != NULL)
								{
									mDebugManager->mOutMessages.push_back(StrFormat("memoryBreak %s", EncodeDataPtr(foundBreakpoint->mMemoryBreakpointInfo->mMemoryAddress, false).c_str()));
									mRunState = RunState_Paused;
								}

								mActiveBreakpoint = foundBreakpoint;
								mBreakStackFrameIdx = -1;
								RemoveTempBreakpoints();
								BfLogDbg("Memory breakpoint hit: %p\n", foundBreakpoint);
							}
							else
								ClearCallStack();
							break;
						}
					}

					if ((mRunState == RunState_DebugEval) && (mDebugEvalThreadInfo.mThreadId == mDebuggerWaitingThread->mThreadId))
					{
						if ((addr_target)(intptr)exceptionRecord->ExceptionAddress == mDebugEvalSetRegisters.GetPC())
						{
							// This indicates we are returning from kernel mode and our registers are clobbered
							SetRegisters(&mDebugEvalSetRegisters);
						}
						break;
					}

					bool hadBreakpointContinue = true;
					if (threadInfo->mBreakpointAddressContinuing != 0)
					{
						bool wantsBreakpoint = WantsBreakpointAt(threadInfo->mBreakpointAddressContinuing);
						BfLogDbg("Continuing breakpoint at %p WantsReset:%d\n", threadInfo->mBreakpointAddressContinuing, wantsBreakpoint);

						if (wantsBreakpoint)
						{
							PhysSetBreakpoint(threadInfo->mBreakpointAddressContinuing);
						}
						threadInfo->mBreakpointAddressContinuing = NULL;
						hadBreakpointContinue = true;

						ThreadRestoreUnpause();
					}

					if ((mSteppingThread != NULL) && (mSteppingThread != mActiveThread))
					{
						// This SINGLE_STEP happened in the wrong thread - we need the stepping thread to do the stepping!
						//  Try again.
						mActiveThread = mSteppingThread;
						SingleStepX86();
						break;
					}

					bool isDeeper = mStepSP > BF_CONTEXT_SP(lcContext);
					if ((mStepSwitchedThreads) && (mStepType == StepType_StepOver) && (isDeeper))
					{
						if (HasSteppedIntoCall())
						{
							// Since we switched threads, we needed to do a hardware step which has placed us inside a
							//  call, so we need to step out of that now...
							SetupStep(StepType_StepOut_NoFrame);
							break;
						}
					}

					// If we don't have a mStepBreakpointAddrs set, that means we're stepping through individual instructions --
					//  so process the new location here
					if (((mStepType == StepType_StepInto) || (mStepType == StepType_StepInto_Unfiltered) || (mStepType == StepType_StepOver)) && (mStepBreakpointAddrs.size() == 0))
					{
						DbgSubprogram* dwSubprogram = NULL;
						DbgLineData* dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);

						if ((dwSubprogram != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC) && (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced))
						{
							BfLogDbg("Stepping through hot thunk\n");
							mRunState = RunState_Running;
							SingleStepX86();
							break;
						}

						if ((mStepType == StepType_StepOver) && (!mStepInAssembly))
						{
							if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
							{
								BfLogDbg("Attempting StepOver of inlined method - SingleStep\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							}
						}

						// Column of -1 means "Illegal", keep stepping!
						if ((mStepInAssembly) ||
							((dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dwLineData->mColumn >= 0) &&
								((dwSubprogram->GetLineAddr(*dwLineData) == pcAddress) || (mStepStopOnNextInstruction))))
						{
							// Hit a line while stepping, we're done!
							mRunState = RunState_Paused;
							StepLineTryPause(pcAddress, false);
							if (mRunState == RunState_Paused)
							{
								if ((mStepType == StepType_StepInto) && (!wasUnfilteredStep) && (!mStepInAssembly) && (dwSubprogram != NULL))
								{
									// Don't filter out the current subprogram (would break cases where we explicitly stepped into or hit breakpoint in a filtered subprogram)
									bool isInStartSubprogram = (mStepStartPC >= dwSubprogram->mBlock.mLowPC) && (mStepStartPC < dwSubprogram->mBlock.mHighPC);
									if ((!isInStartSubprogram) && (IsStepFiltered(dwSubprogram, dwLineData)))
									{
										BfLogDbg("Hit step filter (2)\n");
										mRunState = RunState_Running;
										SetupStep(StepType_StepOut_ThenInto);
										break;
									}
								}

								ClearStep();
								mCurNoInfoStepTries = 0; // Reset
							}
							else
								SetupStep(mStepType);
						}
						else if (dwSubprogram != NULL)
						{
							if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && ((mStepType == StepType_StepInto) || (mStepType == StepType_StepInto_Unfiltered)))
							{
								SingleStepX86();
							}
							else
							{
								// Inside a line's instruction, keep going
								SetupStep(mStepType);
								mCurNoInfoStepTries = 0; // Reset
							}
						}
						else if (mStepType == StepType_StepInto_Unfiltered)
						{
							CPUInst inst;
							if (mDebugTarget->DecodeInstruction(pcAddress, &inst))
							{
								if (inst.IsBranch())
								{
									auto target = inst.GetTarget();
									if (target != 0)
									{
										DbgSubprogram* destSubprogram = mDebugTarget->FindSubProgram(target);
										if ((destSubprogram != NULL) && (target == destSubprogram->mBlock.mLowPC))
										{
											// We're jumping to an actual subprogram, so continue stepping here
											mStepType = StepType_StepInto_UnfilteredSingle;
											SingleStepX86();
											break;
										}
									}
								}
							}

							// We requested to step into this method so stop here even if we don't have source
							mRunState = RunState_Paused;
						}
						else
						{
							// No debug info!
							bool doStepOut = false;
							if (mCurNoInfoStepTries < 16)
							{
								mCurNoInfoStepTries++;
								BfLogDbg("NoInfoStepTries: %d\n", mCurNoInfoStepTries);
								if (!SetupStep(mStepType))
									doStepOut = true;
							}
							else
								doStepOut = true;
							if (doStepOut)
							{
								// Step out of current call.
								mStepSP = 0;
								SetupStep(StepType_StepOut_NoFrame);
								// Aggressive stepout - don't monitor BP
								mStepSP = 0;
							}
						}
					}
					else if (!hadBreakpointContinue)
					{
						BF_DBG_FATAL("EXCEPTION_SINGLE_STEP bad debugger state");
					}

					if (mRunState == RunState_Paused)
						threadInfo->mStoppedAtAddress = pcAddress;
				}
				break;
			default:
				{
					bool isSystemException =
						(exceptionRecord->ExceptionCode >= STATUS_ACCESS_VIOLATION) &&
						(exceptionRecord->ExceptionCode <= STATUS_ASSERTION_FAILURE);

					bool isFirstChance = mDebugEvent.u.Exception.dwFirstChance != 0;
					bool handled = false;

					//TODO: Use a user-defined filter here to determine whether to stop or continue
					if (isFirstChance)
					{
						if (exceptionRecord->ExceptionCode == 0x406D1388) // Visual C
						{
							if ((int32)exceptionRecord->ExceptionInformation[0] == 0x1000)
							{
								struct THREADNAME_INFO
								{
									DWORD dwType; // Must be 0x1000.
									LPCSTR szName; // Pointer to name (in user addr space).
									DWORD dwThreadID; // Thread ID (-1=caller thread).
									DWORD dwFlags; // Reserved for future use, must be zero.
								};

								THREADNAME_INFO* threadNameInfo = (THREADNAME_INFO*)exceptionRecord->ExceptionInformation;

								DwFormatInfo formatInfo;
								formatInfo.mRawString = true;
								String nameStr = ReadString(DbgType_SChar, (intptr)threadNameInfo->szName, false, 1024, formatInfo, false);

								WdThreadInfo* namingThreadInfo = threadInfo;
								if (threadNameInfo->dwThreadID != (DWORD)-1)
								{
									namingThreadInfo = NULL;
									mThreadMap.TryGetValue(threadNameInfo->dwThreadID, &namingThreadInfo);
								}

								if (namingThreadInfo != NULL)
								{
									namingThreadInfo->mName = nameStr;
									FilterThreadName(namingThreadInfo->mName);
								}
							}
							else if (((int32)exceptionRecord->ExceptionInformation[0] == 0x1001) && ((int32)exceptionRecord->ExceptionInformation[1] == 0x1002))
							{
								struct FailMessage
								{
									addr_target mPtr0; // Unknown
									addr_target mPtr1; // 0
									addr_target mPtr2; // 0
									addr_target mPtr3; // Unknown
									addr_target mErrorStr;
								};

								FailMessage failMessage = ReadMemory<FailMessage>(exceptionRecord->ExceptionInformation[2]);
								DwFormatInfo formatInfo;
								String failStr = ReadString(DbgType_SChar16, failMessage.mErrorStr, false, 8192, formatInfo, false);

								mDebugManager->mOutMessages.push_back(StrFormat("error Run-Time Check Failure %d - %s", exceptionRecord->ExceptionInformation[6], failStr.c_str()));
								mRunState = RunState_Paused;
								mRequestedStackFrameIdx = -2; // -2 = "auto"
								handled = true;
							}
						}

						if (!handled)
						{
							if (mRunState != RunState_DebugEval)
							{
								if (!(gDebugManager->GetOutputFilterFlags() & BfOutputFilterFlags_FirstChanceExceptions))
									OutputMessage(StrFormat("Skipping first chance exception %08X at address %@ in thread %d\n", exceptionRecord->ExceptionCode, exceptionRecord->ExceptionAddress, threadInfo->mThreadId));
							}
							::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_EXCEPTION_NOT_HANDLED);
							mIsDebuggerWaiting = false;
						}
					}
					else
					{
						BfLogDbg("EXCEPTION in thread %d at %p\n", threadInfo->mThreadId, exceptionRecord->ExceptionAddress);
						OutputDebugStrF("EXCEPTION\n");
						mActiveThread = threadInfo;
						memcpy(&mCurException, exceptionRecord, sizeof(EXCEPTION_RECORD));

						if (mRunState == RunState_DebugEval)
						{
							if ((intptr)mCurException.ExceptionAddress == 42)
							{
								BfLogDbg("RunState_DebugEval_Done\n");
								OutputDebugStrF(" RunState_DebugEval_Done\n");
							}
							else
							{
								BfLogDbg("Exception at 0x%@ in thread %d, exception code 0x%08X",
									mCurException.ExceptionAddress, mActiveThread->mThreadId, mCurException.ExceptionCode);
								mDebugPendingExpr->mException = StrFormat("Exception at 0x%@ in thread %d, exception code 0x%08X",
									mCurException.ExceptionAddress, mActiveThread->mThreadId, mCurException.ExceptionCode);
							}
							mRunState = RunState_DebugEval_Done;

							mExplicitStopThread = mActiveThread;
							mRequestedStackFrameIdx = mDebugPendingExpr->mCallStackIdx;
						}
						else
						{
							mRunState = RunState_Exception;
						}
					}
				}
				break;
			}
		}
		break;
	}

	if ((mDebugEvalThreadInfo.mThreadId != 0) && (mRunState != RunState_DebugEval) && (mRunState != RunState_DebugEval_Done))
	{
		CleanupDebugEval();
	}

	// Stepping done?
	if (mStepType == StepType_None)
	{
		mLastValidStepIntoPC = 0;
	}

	BF_ASSERT(mDebuggerWaitingThread != NULL);
	return true;
}

void WinDebugger::Update()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

// 	if (mRunState == RunState_DebugEval)
// 		ContinueDebugEvent();
	if (mRunState == RunState_DebugEval_Done)
	{
		if (mDebugPendingExpr != NULL)
		{
			mDebugPendingExpr->mIdleTicks++;
			if (mDebugPendingExpr->mIdleTicks >= 2)
			{
				BfLogDbg("Finishing pending expr in thread %d\n", mDebugEvalThreadInfo.mThreadId);

				mRunState = RunState_Paused;
				CleanupDebugEval();
			}
		}
	}
	else if (mDebugPendingExpr != NULL)
	{
		mDebugPendingExpr->mIdleTicks = 0;
	}
}

void WinDebugger::ContinueDebugEvent()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(mRunState != RunState_DebugEval_Done);

	if (!mWantsDebugContinue)
		return;

	if (!TryRunContinue())
		return;

// 	if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId != mDebugEvalThreadInfo.mThreadId))
// 	{
// 		// Don't process the 'mIsAtBreakpointAddress' stuff
// 		mWantsDebugContinue = false;
// 		mContinueEvent.Set();
// 		return;
// 	}

	if ((mDebuggerWaitingThread->mIsAtBreakpointAddress == 0) && (mDebuggerWaitingThread->mStoppedAtAddress != 0))
	{
		auto breakpoint = FindBreakpointAt(mDebuggerWaitingThread->mStoppedAtAddress);
		if (breakpoint != NULL)
		{
			mDebuggerWaitingThread->mIsAtBreakpointAddress = mDebuggerWaitingThread->mStoppedAtAddress;
		}
	}

	if (mDebuggerWaitingThread->mIsAtBreakpointAddress != 0)
	{
		if (!ContinueFromBreakpoint())
		{
			BfLogDbg("ContinueFromBreakpoint failed\n");

			ClearCallStack();
			mDebuggerWaitingThread->mStoppedAtAddress = 0;
			mDebuggerWaitingThread->mIsAtBreakpointAddress = 0;
			mWantsDebugContinue = false;
			mContinueFromBreakpointFailed = true;
			mContinueEvent.Set();
			return;
		}
	}

	if ((mRunState == RunState_Breakpoint) || (mRunState == RunState_Paused) || (mRunState == RunState_TargetUnloaded))
	{
		ClearCallStack();
		mRunState = RunState_Running;
	}

	mDebuggerWaitingThread->mStoppedAtAddress = 0;
	mWantsDebugContinue = false;
	BF_ASSERT_REL(mDebuggerWaitingThread->mIsAtBreakpointAddress == 0);
	mContinueEvent.Set();
}

static BOOL CALLBACK WdEnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	int wantProcessId = lParam;

	HWND owner = GetWindow(hwnd, GW_OWNER);
	if (!IsWindowVisible(hwnd))
		return TRUE;

	DWORD processId = 0;
	DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);

	if (processId != wantProcessId)
		return TRUE;
	
	while (true)
	{
		HWND parentHWnd = GetParent(hwnd);
		if (parentHWnd != NULL)
		{
			hwnd = parentHWnd;
			continue;
		}
		// A minimized window stays minimized, so this would only take the keyboard away from the IDE
		if (!IsIconic(hwnd))
			SetForegroundWindow(hwnd);
		break;
	}

	return TRUE;
}

void WinDebugger::ForegroundTarget(int altProcessId)
{
	int wantProcessId = altProcessId;
	if (wantProcessId == 0)
		wantProcessId = ((WinDebugger*)gDebugger)->mProcessInfo.dwProcessId;

	HWND hwnd = ::GetForegroundWindow();
	if (hwnd != INVALID_HANDLE_VALUE)
	{
		DWORD processId = 0;
		GetWindowThreadProcessId(hwnd, &processId);
		if (processId == ((WinDebugger*)gDebugger)->mProcessInfo.dwProcessId)
			return; // Already good
	}
	
	EnumWindows(WdEnumWindowsProc, wantProcessId);
}

bool WinDebugger::IsStepFiltered(DbgSubprogram* dbgSubprogram, DbgLineData* dbgLineData)
{
	if (mIsStepIntoSpecific)
		return false;

	if (dbgSubprogram->mStepFilterVersion != mDebugManager->mStepFilterVersion)
	{
		String filterName;
		CreateFilterName(filterName, dbgSubprogram);
		dbgSubprogram->PopulateSubprogram();

		bool doDefault = false;

		StepFilter* stepFilterPtr;
		if (mDebugManager->mStepFilters.TryGetValue(filterName, &stepFilterPtr))
		{
			switch (stepFilterPtr->mFilterKind)
			{
			case BfStepFilterKind_Default:
				doDefault = true;
				break;
			case BfStepFilterKind_Filtered:
				dbgSubprogram->mIsStepFiltered = true;
				break;
			case BfStepFilterKind_NotFiltered:
				dbgSubprogram->mIsStepFiltered = false;
				break;
			}
		}
		else
		{
			doDefault = true;
		}

		if (doDefault)
		{
			dbgSubprogram->mIsStepFiltered = dbgSubprogram->mIsStepFilteredDefault;
		}

		dbgSubprogram->mStepFilterVersion = mDebugManager->mStepFilterVersion;
	}

	if (!dbgSubprogram->mIsStepFiltered)
	{
		if (dbgLineData != NULL)
		{
			auto dbgSrcFile = dbgSubprogram->GetLineSrcFile(*dbgLineData);
			if (dbgSrcFile->mStepFilterVersion != mDebugManager->mStepFilterVersion)
			{
				dbgSrcFile->mFileExistKind = dbgSubprogram->mCompileUnit->mDbgModule->CheckSourceFileExist(dbgSrcFile->GetLocalPath());
				dbgSrcFile->mStepFilterVersion = mDebugManager->mStepFilterVersion;
			}

			switch (dbgSrcFile->mFileExistKind)
			{
			case DbgFileExistKind_NotFound:
				return true;
			case DbgFileExistKind_HasOldSourceCommand:
				if (mDebugManager->mStepOverExternalFiles)
					return true;
			}
		}
	}

	return dbgSubprogram->mIsStepFiltered;
}

void WinDebugger::RemoveTempBreakpoints()
{
	BfLogDbg("RemoveTempBreakpoints\n");

	for (auto address : mTempBreakpoint)
	{
		RemoveBreakpoint(address);
// 		if (FindBreakpointAt(address) == NULL)
// 		{
// 			RemoveBreakpoint(address);
// 		}
// 		else
// 		{
// 			BfLogDbg("Ignoring remove on temp breakpoint %p\n", address);
// 		}
	}
	mTempBreakpoint.Clear();
	mStepBreakpointAddrs.Clear();
}

void WinDebugger::RehupBreakpoints(bool doFlush)
{
	BfLogDbg("RehupBreakpoints\n");

	// First pass- detach breakpoints that need to be rebound
	for (int i = 0; i < (int)mBreakpoints.size(); i++)
	{
		auto breakpoint = mBreakpoints[i];
		while (breakpoint != NULL)
		{
			if (((breakpoint->mSrcFile != NULL) && (breakpoint->mSrcFile->mDeferredRefs.size() > 0)) ||
				(!breakpoint->mSymbolName.IsEmpty()))
			{
				// This breakpoint was already bound, but we loaded a debug module that also had this file so rebind it
				DetachBreakpoint(breakpoint);
			}

			breakpoint = (WdBreakpoint*)breakpoint->mLinkedSibling;
		}
	}

	// Second pass- actually set breakpoints
	for (int i = 0; i < (int)mBreakpoints.size(); i++)
	{
		auto breakpoint = mBreakpoints[i];
		while (breakpoint != NULL)
		{
			CheckBreakpoint(breakpoint);
 			if (breakpoint->mAddr != 0)
 				SetBreakpoint(breakpoint->mAddr, true);
			breakpoint = (WdBreakpoint*)breakpoint->mLinkedSibling;
		}
	}

	mNeedsRehupBreakpoints = false;
}

bool WinDebugger::WantsBreakpointAt(addr_target address)
{
	if (mTempBreakpoint.Contains(address))
		return true;
	for (auto breakpoint : mBreakpoints)
	{
		WdBreakpoint* checkBreakpoint = breakpoint;
		while (checkBreakpoint != NULL)
		{
			if (address == checkBreakpoint->mAddr)
				return true;
			checkBreakpoint = (WdBreakpoint*)checkBreakpoint->mLinkedSibling;
		}
	}
	return false;
}

void WinDebugger::CheckBreakpoint(WdBreakpoint* wdBreakpoint, DbgSrcFile* srcFile, int lineNum, int hotIdx)
{
	BP_ZONE("WinDebugger::CheckBreakpoint:atLoc");

	if (hotIdx == -1)
	{
		BF_ASSERT(wdBreakpoint->mPendingHotBindIdx == -1);
	}
	WdBreakpoint* headBreakpoint = wdBreakpoint;
	headBreakpoint->mPendingHotBindIdx = -1;

	bool foundInSequence = false;
	DbgSubprogram* lastFoundSubprogram = NULL;

	int highestHotIdx = -1;
	bool foundLine = false;

	int bestLineNum = -1;
	int bestLineOffset = 0x7FFFFFFF;

	auto _CheckLineInfo = [&](DbgSubprogram* dbgSubprogram, DbgLineInfo* dbgLineInfo)
	{
		// Scan first so we can determine if we want to do fix up line data or not.
		bool hasNear = false;
		int maxLineDist = 6;

		for (int lineIdx = 0; lineIdx < dbgLineInfo->mLines.mSize; lineIdx++)
		{
			auto lineData = &dbgLineInfo->mLines[lineIdx];
			auto& ctx = dbgLineInfo->mContexts[lineData->mCtxIdx];
			if (ctx.mSrcFile != srcFile)
				continue;

			int lineOffset = lineData->mLine - lineNum;
			if ((lineOffset >= 0) && (lineOffset <= maxLineDist))
				hasNear = true;
		}

		if (!hasNear)
			return;

		DbgModule::FixupLineDataForSubprogram(dbgSubprogram);

		for (int lineIdx = 0; lineIdx < dbgLineInfo->mLines.mSize; lineIdx++)
		{
			//TODO: Do fixup lineData... ?
			auto lineData = &dbgLineInfo->mLines[lineIdx];
			auto& ctx = dbgLineInfo->mContexts[lineData->mCtxIdx];
			if (ctx.mSrcFile != srcFile)
				continue;

// 			if (ctx.mInlinee != NULL)
// 			{
// 				if (lineIdx + 1 < dbgLineInfo->mLines.mSize)
// 				{
// 					auto nextLineData = &dbgLineInfo->mLines[lineIdx + 1];
// 					if (nextLineData->mRelAddress == lineData->mRelAddress)
// 					{
// 						// Use the later entry (same logic from DisassembleAt)
// 						continue;
// 					}
// 				}
// 			}

			if ((lineData->mColumn == -1) && (wdBreakpoint->mInstrOffset == -1))
				continue;

			int lineOffset = lineData->mLine - lineNum;

			if (lineOffset == 0)
			{
				foundLine = true;

				auto address = dbgSubprogram->GetLineAddr(*lineData);
				auto subProgram = mDebugTarget->FindSubProgram(address);
				if (subProgram->mNeedLineDataFixup)
					DbgModule::FixupLineDataForSubprogram(subProgram);

				if (subProgram != NULL)
					highestHotIdx = BF_MAX(highestHotIdx, subProgram->mCompileUnit->mDbgModule->mHotIdx);

				if ((foundInSequence) && (subProgram != lastFoundSubprogram))
					foundInSequence = false;

				if ((subProgram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && (address < subProgram->mBlock.mLowPC + sizeof(HotJumpOp)))
				{
					// If this breakpoint ends up on the hot jmp instruction
					continue;
				}

				if (!foundInSequence)
				{
					lastFoundSubprogram = subProgram;

					if ((subProgram != NULL) && (subProgram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && (address == subProgram->mBlock.mLowPC))
					{
						// This instruction is actually the hot jump, we don't need a breakpoint here
						foundInSequence = true;
						continue;
					}

					if (wdBreakpoint->mSrcFile != NULL)
					{
						wdBreakpoint = new WdBreakpoint();
						// Insert at head
						wdBreakpoint->mLinkedSibling = headBreakpoint->mLinkedSibling;
						headBreakpoint->mLinkedSibling = wdBreakpoint;
						wdBreakpoint->mRequestedLineNum = headBreakpoint->mRequestedLineNum;
						wdBreakpoint->mLineNum = headBreakpoint->mLineNum;
						wdBreakpoint->mColumn = headBreakpoint->mColumn;
						wdBreakpoint->mInstrOffset = headBreakpoint->mInstrOffset;
						wdBreakpoint->mIsLinkedSibling = true;
						wdBreakpoint->mHead = headBreakpoint;
					}

					if (wdBreakpoint->mInstrOffset > 0)
					{
						for (int instIdx = 0; instIdx < wdBreakpoint->mInstrOffset; instIdx++)
						{
							CPUInst inst;
							if (!mDebugTarget->DecodeInstruction(address, &inst))
								break;
							address += inst.mSize;
						}
					}

					wdBreakpoint->mSrcFile = ctx.mSrcFile;
					wdBreakpoint->mLineData = DbgLineDataEx(lineData, subProgram);
					wdBreakpoint->mBreakpointType = BreakpointType_User;
					wdBreakpoint->mAddr = address;

					if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mStoppedAtAddress == address))
					{
						BfLogDbg("CheckBreakpoint setting mIsAtBreakpointAddress = %p\n", address);
						mDebuggerWaitingThread->mIsAtBreakpointAddress = address;
					}

					BfLogDbg("Breakpoint %p found at %s in %s\n", wdBreakpoint, subProgram->mName, GetFileName(subProgram->mCompileUnit->mDbgModule->mFilePath).c_str());

					mBreakpointAddrMap.ForceAdd(address, wdBreakpoint);
					SetBreakpoint(address);

					foundInSequence = true;
				}
			}
			else
			{
				//TODO: We didn't have this here, but if we don't have this then there are some cases where the method-closing brace generates code in
				// multiple places so we need to ensure this will break on them all
				foundInSequence = false;
			}

			if ((lineOffset >= 0) && (lineOffset <= maxLineDist) && (lineOffset <= bestLineOffset))
			{
				if (lineOffset < bestLineOffset)
				{
					bestLineNum = lineData->mLine;
					bestLineOffset = lineOffset;
				}
			}
		}
	};

	for (int pass = 0; pass < 2; pass++)
	{
		if (lineNum == -1)
			break;

		bestLineNum = -1;
		bestLineOffset = 0x7FFFFFFF;

		if (hotIdx >= 0)
		{
			if (hotIdx >= srcFile->mHotReplacedDbgLineInfo.size())
				return;

			auto hotReplacedLineInfo = srcFile->mHotReplacedDbgLineInfo[hotIdx];
			for (auto& hotReplacedEntry : hotReplacedLineInfo->mEntries)
			{
				_CheckLineInfo(hotReplacedEntry.mSubprogram, hotReplacedEntry.mLineInfo);
			}
		}
		else
		{
			for (auto subprogram : srcFile->mLineDataRefs)
				_CheckLineInfo(subprogram, subprogram->mLineInfo);
		}

		if (foundLine)
			break;

		// Don't allow the breakpoint to be inexactly bound -- only match on pass 0
		if (hotIdx != -1)
			break;

		if (bestLineNum == -1)
			break;

		lineNum = bestLineNum;
		wdBreakpoint->mLineNum = bestLineNum;
	}

	int highestCheckHotIdx = highestHotIdx - 1;
	if (hotIdx != -1)
		highestCheckHotIdx = hotIdx - 1;

	for (int hotFileIdx = highestCheckHotIdx; hotFileIdx >= 0; hotFileIdx--)
	{
		auto& hotReplacedDbgLineData = wdBreakpoint->mSrcFile->mHotReplacedDbgLineInfo;
		// Only try to bind to an old hot version if we haven't unloaded the hot module
		if ((hotFileIdx < (int)hotReplacedDbgLineData.size()) && (hotReplacedDbgLineData[hotFileIdx]->mEntries.size() > 0))
		{
			headBreakpoint->mPendingHotBindIdx = hotFileIdx;
			break;
		}
	}
}

void WinDebugger::HotBindBreakpoint(Breakpoint* breakpoint, int lineNum, int hotIdx)
{
	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	CheckBreakpoint(wdBreakpoint, wdBreakpoint->mSrcFile, lineNum, hotIdx);
}

void WinDebugger::CheckBreakpoint(WdBreakpoint* wdBreakpoint)
{
	if (!mGotStartupEvent)
		return;

	if (wdBreakpoint->mThreadId == 0) // Not bound to threadId yet...
	{
		return;
	}

	if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
	{
		if (wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap != 0)
			return;

		if (mFreeMemoryBreakIndices.size() == 0)
			return;

		if ((IsInRunState()) || (mActiveThread == NULL))
			return;

		int wantBytes[4];
		int wantBindCount = 0;
		int bytesLeft = wdBreakpoint->mMemoryBreakpointInfo->mByteCount;
		addr_target curAddr = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress;
		while (bytesLeft > 0)
		{
			if (wantBindCount >= mFreeMemoryBreakIndices.size())
				return;

			int curByteCount = 1;
#ifdef BF_DBG_64
			if ((bytesLeft >= 8) && ((curAddr & 7) == 0))
				curByteCount = 8;
			else
#endif
			if ((bytesLeft >= 4) && ((curAddr & 3) == 0))
				curByteCount = 4;
			else if ((bytesLeft >= 2) && ((curAddr & 1) == 0))
				curByteCount = 2;

			wantBytes[wantBindCount++] = curByteCount;
			bytesLeft -= curByteCount;
			curAddr += curByteCount;
		}

		addr_target curOfs = 0;
		for (int i = 0; i < wantBindCount; i++)
		{
			int memoryBreakIdx = mFreeMemoryBreakIndices.back();
			mFreeMemoryBreakIndices.pop_back();

			mMemoryBreakpoints[memoryBreakIdx].mBreakpoint = wdBreakpoint;
			mMemoryBreakpoints[memoryBreakIdx].mAddress = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress + curOfs;
			mMemoryBreakpoints[memoryBreakIdx].mByteCount = wantBytes[i];
			mMemoryBreakpoints[memoryBreakIdx].mOfs = curOfs;
			curOfs += wantBytes[i];
			mMemoryBreakpointVersion++;

			wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap |= 1<<memoryBreakIdx;
		}

		UpdateThreadDebugRegisters();
	}

	if (wdBreakpoint->mAddr != 0)
		return;

	if (!wdBreakpoint->mSymbolName.IsEmpty())
	{
		auto headBreakpoint = wdBreakpoint->GetHeadBreakpoint();

		String symbolName = wdBreakpoint->mSymbolName;
		bool onlyBindFirst = false;
		if (symbolName.StartsWith("-"))
		{
			symbolName.Remove(0);
			onlyBindFirst = true;
		}

		for (auto dbgModule : mDebugTarget->mDbgModules)
		{
			dbgModule->ParseSymbolData();

			addr_target targetAddr = -1;
			auto entry = dbgModule->mSymbolNameMap.Find(symbolName.c_str());
			if (entry != NULL)
			{
				DbgSymbol* dwSymbol = entry->mValue;
				targetAddr = dwSymbol->mAddress;
			}

			if (targetAddr == -1)
			{
				if (symbolName == ".")
				{
					targetAddr = mDebugTarget->mLaunchBinary->mImageBase + mDebugTarget->mLaunchBinary->mEntryPoint;
					onlyBindFirst = true;
				}
			}

			if (targetAddr != -1)
			{
				if (wdBreakpoint->mAddr == 0)
				{
					wdBreakpoint->mAddr = targetAddr;
					wdBreakpoint->mBreakpointType = BreakpointType_User;
					mBreakpointAddrMap.ForceAdd(wdBreakpoint->mAddr, wdBreakpoint);
					SetBreakpoint(wdBreakpoint->mAddr);
				}
				else
				{
					wdBreakpoint = new WdBreakpoint();
					// Insert at head
					wdBreakpoint->mLinkedSibling = headBreakpoint->mLinkedSibling;
					headBreakpoint->mLinkedSibling = wdBreakpoint;
					wdBreakpoint->mSymbolName = headBreakpoint->mSymbolName;
					wdBreakpoint->mIsLinkedSibling = true;
					wdBreakpoint->mHead = headBreakpoint;
				}

				if (onlyBindFirst)
					break;
			}
		}
		return;
	}

	BP_ZONE("WinDebugger::CheckBreakpoint");

	// Rehup if we load a DLL that also uses this file we bound to (thus the mDeferredRefs check)
	if (wdBreakpoint->mSrcFile == NULL)
	{
		DbgSrcFile* srcFile = mDebugTarget->GetSrcFile(wdBreakpoint->mFilePath);
		if (srcFile == NULL)
			return;

		for (auto& deferredSrcFileRef : srcFile->mDeferredRefs)
		{
			deferredSrcFileRef.mDbgModule->ParseCompileUnit(deferredSrcFileRef.mCompileUnitId);
		}
		srcFile->mDeferredRefs.Clear();

		CheckBreakpoint(wdBreakpoint, srcFile, wdBreakpoint->mRequestedLineNum, -1);
	}
}

bool WinDebugger::IsMemoryBreakpointSizeValid(addr_target addr, int size)
{
	int wantBindCount = 0;
	int bytesLeft = size;
	addr_target curAddr = addr;

	for (int i = 0; i < 4; i++)
	{
		int curByteCount = 1;
#ifdef BF_DBG_64
		if ((bytesLeft >= 8) && ((curAddr & 7) == 0))
			curByteCount = 8;
		else
#endif
		if ((bytesLeft >= 4) && ((curAddr & 3) == 0))
			curByteCount = 4;
		else if ((bytesLeft >= 2) && ((curAddr & 1) == 0))
			curByteCount = 2;
		bytesLeft -= curByteCount;
		curAddr += curByteCount;

		if (bytesLeft == 0)
			return true;
	}

	return false;
}

bool WinDebugger::HasMemoryBreakpoint(addr_target addr, int size)
{
	for (int i = 0; i < 4; i++)
	{
		if ((mMemoryBreakpoints[i].mAddress == addr) &&
			(mMemoryBreakpoints[i].mOfs == 0) &&
			(mMemoryBreakpoints[i].mBreakpoint->mMemoryBreakpointInfo->mByteCount == size))
			return true;
	}

	return false;
}

Breakpoint* WinDebugger::CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateBreakpoint %s %d %d\n", fileName.c_str(), lineNum, wantColumn);

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mFilePath = FixPathAndCase(fileName);
	wdBreakpoint->mRequestedLineNum = lineNum;
	wdBreakpoint->mLineNum = lineNum;
	wdBreakpoint->mColumn = wantColumn;
	wdBreakpoint->mInstrOffset = instrOffset;
	mBreakpoints.push_back(wdBreakpoint);

	BfLogDbg("CreateBreakpoint Created %p\n", wdBreakpoint);

	return wdBreakpoint;
}

void WinDebugger::CheckBreakpoint(Breakpoint* checkBreakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	CheckBreakpoint((WdBreakpoint*)checkBreakpoint);
}

Breakpoint* WinDebugger::CreateMemoryBreakpoint(intptr addr, int byteCount)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateMemoryBreakpoint %p %d\n", addr, byteCount);

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();

	WdMemoryBreakpointInfo* memoryBreakInfo = new WdMemoryBreakpointInfo();
	memoryBreakInfo->mMemoryAddress = addr;
	memoryBreakInfo->mByteCount = byteCount;
	wdBreakpoint->mMemoryBreakpointInfo = memoryBreakInfo;
	mBreakpoints.push_back(wdBreakpoint);
	CheckBreakpoint(wdBreakpoint);
	return wdBreakpoint;
}

Breakpoint* WinDebugger::CreateSymbolBreakpoint(const StringImpl& symbolName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateSymbolBreakpoint %s\n", symbolName.c_str());

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mSymbolName = symbolName;
	mBreakpoints.push_back(wdBreakpoint);
	CheckBreakpoint(wdBreakpoint);
	return wdBreakpoint;
}

Breakpoint* WinDebugger::CreateAddressBreakpoint(intptr inAddress)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateAddressBreakpoint %p\n", inAddress);

	addr_target address = (addr_target)inAddress;
	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mAddr = address;
	mBreakpointAddrMap.ForceAdd(wdBreakpoint->mAddr, wdBreakpoint);
	SetBreakpoint(address);

	if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mStoppedAtAddress == address))
	{
		BfLogDbg("CreateAddressBreakpoint setting mIsAtBreakpointAddress = %p\n", address);
		mDebuggerWaitingThread->mIsAtBreakpointAddress = address;
	}

	mBreakpoints.push_back(wdBreakpoint);
	return wdBreakpoint;
}

void WinDebugger::DeleteBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	while (wdBreakpoint != NULL)
	{
		BfLogDbg("WinDebugger::DeleteBreakpoint %p Count:%d\n", wdBreakpoint, mBreakpoints.size());

		if (wdBreakpoint == mActiveBreakpoint)
			mActiveBreakpoint = NULL;

		if (wdBreakpoint->mCondition != NULL)
		{
			if (!wdBreakpoint->mIsLinkedSibling)
				delete wdBreakpoint->mCondition;
		}

		if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
		{
			for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
			{
				if (mMemoryBreakpoints[memoryWatchSlot].mBreakpoint == wdBreakpoint)
				{
					mFreeMemoryBreakIndices.push_back(memoryWatchSlot);
					mMemoryBreakpoints[memoryWatchSlot] = WdMemoryBreakpointBind();
					mMemoryBreakpointVersion++;
					UpdateThreadDebugRegisters();
				}
			}

			wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap = 0;
		}

		if (wdBreakpoint->mAddr != 0)
		{
			mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);
			RemoveBreakpoint(wdBreakpoint->mAddr);

			for (auto thread : mThreadList)
			{
				if (thread->mIsAtBreakpointAddress == wdBreakpoint->mAddr)
					thread->mIsAtBreakpointAddress = NULL;
				if (thread->mBreakpointAddressContinuing == wdBreakpoint->mAddr)
					thread->mBreakpointAddressContinuing = NULL;
			}
		}

		if (!wdBreakpoint->mIsLinkedSibling)
		{
			mBreakpoints.Remove(wdBreakpoint);
		}

		auto nextBreakpoint = (WdBreakpoint*)wdBreakpoint->mLinkedSibling;
		delete wdBreakpoint;

		wdBreakpoint = nextBreakpoint;
	}
}

void WinDebugger::DetachBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("WinDebugger::DetachBreakpoint %p\n", breakpoint);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	if (wdBreakpoint->mAddr != 0)
	{
		mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);
		RemoveBreakpoint(wdBreakpoint->mAddr);
		if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mIsAtBreakpointAddress == wdBreakpoint->mAddr))
			mDebuggerWaitingThread->mIsAtBreakpointAddress = NULL;
		if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mBreakpointAddressContinuing == wdBreakpoint->mAddr))
			mDebuggerWaitingThread->mBreakpointAddressContinuing = NULL;
		wdBreakpoint->mLineData = DbgLineDataEx();
		wdBreakpoint->mAddr = 0;
	}

	if (wdBreakpoint->mCondition != NULL)
	{
		delete wdBreakpoint->mCondition->mDbgEvaluationContext;
		wdBreakpoint->mCondition->mDbgEvaluationContext = NULL;
	}

	if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
	{
		for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
		{
			if (mMemoryBreakpoints[memoryWatchSlot].mBreakpoint == wdBreakpoint)
			{
				mFreeMemoryBreakIndices.push_back(memoryWatchSlot);
				mMemoryBreakpoints[memoryWatchSlot] = WdMemoryBreakpointBind();
				mMemoryBreakpointVersion++;
				UpdateThreadDebugRegisters();
			}
		}

		wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap = 0;
	}

	if (wdBreakpoint->mLinkedSibling != NULL)
	{
		DeleteBreakpoint(wdBreakpoint->mLinkedSibling);
		wdBreakpoint->mLinkedSibling = NULL;
	}

	wdBreakpoint->mSrcFile = NULL;
	wdBreakpoint->mPendingHotBindIdx = -1;
}

void WinDebugger::MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	AutoCrit autoCrit(mDebugManager->mCritSect);

	DetachBreakpoint(wdBreakpoint);

	//TODO: This doesn't actually rebind correctly while the app is running
	if ((lineNum != -1) && (wantColumn != -1))
	{
		wdBreakpoint->mRequestedLineNum = lineNum;
		wdBreakpoint->mLineNum = lineNum;
		wdBreakpoint->mColumn = wantColumn;
	}
	if (rebindNow)
		CheckBreakpoint(wdBreakpoint);
}

void WinDebugger::MoveMemoryBreakpoint(Breakpoint* breakpoint, intptr addr, int byteCount)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	DetachBreakpoint(wdBreakpoint);
	wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress = addr;
	wdBreakpoint->mMemoryBreakpointInfo->mByteCount = byteCount;
	CheckBreakpoint(wdBreakpoint);
}

void WinDebugger::DisableBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	DetachBreakpoint(wdBreakpoint);

	delete wdBreakpoint->mMemoryBreakpointInfo;
	wdBreakpoint->mMemoryBreakpointInfo = NULL;
}

void WinDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& conditionExpr)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);

	if (conditionExpr.empty())
	{
		delete wdBreakpoint->mCondition;
		WdBreakpoint* curBreakpoint = wdBreakpoint;
		wdBreakpoint->mCondition = NULL;
	}
	else
	{
		delete wdBreakpoint->mCondition;
		auto condition = new WdBreakpointCondition();
		condition->mExpr = conditionExpr;
		wdBreakpoint->mCondition = condition;
	}
}

void WinDebugger::SetBreakpointLogging(Breakpoint* breakpoint, const StringImpl& logging, bool breakAfterLogging)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);
	wdBreakpoint->mLogging = logging;
	wdBreakpoint->mBreakAfterLogging = breakAfterLogging;
}

bool WinDebugger::CheckConditionalBreakpoint(WdBreakpoint* breakpoint, DbgSubprogram* dbgSubprogram, addr_target pcAddress)
{
	// What was this assertion for?
	//BF_ASSERT(mCallStack.size() == 0);

	auto headBreakpoint = breakpoint->GetHeadBreakpoint();

	if (headBreakpoint->mThreadId != -1)
	{
		if ((mActiveThread != NULL) && (mActiveThread->mThreadId != headBreakpoint->mThreadId))
			return false;
	}

	auto _SplitExpr = [&](const StringImpl& expr, StringImpl& outExpr, StringImpl& outSubject)
	{
		int crPos = expr.IndexOf('\n');
		if (crPos != -1)
		{
			outExpr += expr.Substring(0, crPos);
			outSubject += expr.Substring(crPos + 1);
		}
		else
		{
			outExpr += expr;
		}
	};

	if (headBreakpoint->mCondition != NULL)
	{
		ClearCallStack();

		auto conditional = headBreakpoint->mCondition;
		if (conditional->mDbgEvaluationContext == NULL)
		{
			CPURegisters registers;
			PopulateRegisters(&registers);
			auto pcAddress = registers.GetPC();
			DbgSubprogram* subprogram = mDebugTarget->FindSubProgram(pcAddress);
			if (subprogram == NULL)
			{
				return false;
			}

			StringT<256> expr;
			StringT<256> subjectExpr;
			if (breakpoint->mMemoryBreakpointInfo != NULL)
			{
				subjectExpr += "*";
			}

			_SplitExpr(conditional->mExpr, expr, subjectExpr);
			DbgLanguage language = DbgLanguage_Unknown;
			if (expr.StartsWith("@Beef:"))
			{
				expr.Remove(0, 6);
				language = DbgLanguage_Beef;
			}
			else if (expr.StartsWith("@C:"))
			{
				expr.Remove(0, 3);
				language = DbgLanguage_C;
			}

			conditional->mDbgEvaluationContext = new DbgEvaluationContext(this, subprogram->mCompileUnit->mDbgModule, expr);
			if (language != DbgLanguage_Unknown)
				conditional->mDbgEvaluationContext->mDbgExprEvaluator->mLanguage = language;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mSubjectExpr = subjectExpr;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mDbgCompileUnit = subprogram->mCompileUnit;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mCallStackIdx = 0;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mExpressionFlags = (DwEvalExpressionFlags)(DwEvalExpressionFlag_AllowSideEffects);
		}

		WdStackFrame* wdStackFrame = new WdStackFrame();
		PopulateRegisters(&wdStackFrame->mRegisters);
		mCallStack.Add(wdStackFrame);
		DbgTypedValue result = conditional->mDbgEvaluationContext->EvaluateInContext(DbgTypedValue());
		ClearCallStack();

		if ((result.mType != NULL) && (result.mType->mTypeCode == DbgType_Bitfield))
			result.mType = result.mType->mTypeParam;

		if (conditional->mDbgEvaluationContext->mPassInstance->HasFailed())
		{
			String errorStr = "FAILED";
			for (auto error : conditional->mDbgEvaluationContext->mPassInstance->mErrors)
			{
				if (!error->mIsWarning)
					errorStr = error->mError;
			}
			String condError = StrFormat("error Conditional breakpoint expression '%s' failed: %s", conditional->mExpr.c_str(), errorStr.c_str());
			mDebugManager->mOutMessages.push_back(condError);
			return true;
		}
		else if (conditional->mDbgEvaluationContext->mDbgExprEvaluator->mBlockedSideEffects)
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' contained function calls, which is not allowed", conditional->mExpr.c_str()));
			return true;
		}
		else if ((!result) || (!result.mType->IsBoolean()))
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' must result in a boolean value", conditional->mExpr.c_str()));
			return true;
		}
		else if (!result.mBool)
			return false;
	}

	headBreakpoint->mHitCount++;
	switch (headBreakpoint->mHitCountBreakKind)
	{
	case DbgHitCountBreakKind_Equals:
		if (headBreakpoint->mHitCount != headBreakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_GreaterEquals:
		if (headBreakpoint->mHitCount < headBreakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_Multiple:
		if ((headBreakpoint->mHitCount % headBreakpoint->mTargetHitCount) != 0)
			return false;
		break;
	}

	mActiveBreakpoint = breakpoint;
	mBreakStackFrameIdx = -1;
	if (!headBreakpoint->mLogging.IsEmpty())
	{
		ClearCallStack();

		DwFormatInfo formatInfo;
		formatInfo.mCallStackIdx = 0;

		DbgCompileUnit* dbgCompileUnit = NULL;
		if (dbgSubprogram == NULL)
			dbgSubprogram = mDebugTarget->FindSubProgram(pcAddress);
		if (dbgSubprogram != NULL)
		{
			dbgCompileUnit = dbgSubprogram->mCompileUnit;
			formatInfo.mLanguage = dbgSubprogram->GetLanguage();
		}

		auto prevRunState = mRunState;
		mRunState = RunState_Paused; // We need to be paused to avoid certain errors in the eval
		String displayString;

		String expr;
		_SplitExpr(headBreakpoint->mLogging, expr, formatInfo.mSubjectExpr);

		if (expr.StartsWith("@Script:"))
		{
			displayString = "script ";
			displayString += expr.Substring(8);
		}
		else
		{
			if (expr.StartsWith("@Beef:"))
			{
				expr.Remove(0, 6);
				formatInfo.mLanguage = DbgLanguage_Beef;
			}
			else if (expr.StartsWith("@C:"))
			{
				expr.Remove(0, 3);
				formatInfo.mLanguage = DbgLanguage_C;
			}

			ProcessEvalString(dbgCompileUnit, DbgTypedValue(), expr, displayString, formatInfo, NULL, false);
			mRunState = prevRunState;

			displayString.Insert(0, "log ");
			displayString.Append("\n");
		}

		mDebugManager->mOutMessages.push_back(displayString);

		if (!headBreakpoint->mBreakAfterLogging)
			return false;
	}

	return true;
}

void WinDebugger::CleanupDebugEval(bool restoreRegisters)
{
	BfLogDbg("CleanupDebugEval ThreadId=%d\n", mDebugEvalThreadInfo.mThreadId);

	WdThreadInfo* evalThreadInfo = NULL;
	if (mThreadMap.TryGetValue(mDebugEvalThreadInfo.mThreadId, &evalThreadInfo))
	{
		if ((restoreRegisters) && (!mDbgBreak))
		{
			SetAndRestoreValue<WdThreadInfo*> activeThread(mActiveThread, evalThreadInfo);
			RestoreAllRegisters();
// 			if (mRunState == RunState_Running_ToTempBreakpoint)
// 				mRunState = RunState_Paused;
		}

		evalThreadInfo->mStartSP = mDebugEvalThreadInfo.mStartSP;
		evalThreadInfo->mStoppedAtAddress = mDebugEvalThreadInfo.mStoppedAtAddress;
		evalThreadInfo->mIsAtBreakpointAddress = mDebugEvalThreadInfo.mIsAtBreakpointAddress;
		evalThreadInfo->mBreakpointAddressContinuing = mDebugEvalThreadInfo.mBreakpointAddressContinuing;
	}

	delete mDebugPendingExpr;
	mDebugPendingExpr = NULL;
	mDebugEvalThreadInfo = WdThreadInfo();

	OutputRawMessage("rehupLoc");
}

int WinDebugger::GetEvalThreadId()
{
	return (mActiveThread != NULL) ? mActiveThread->mThreadId : 0;
}

void WinDebugger::OnPendingEvalStarted()
{
	mDebugEvalThreadInfo = *mActiveThread;
	mActiveThread->mIsAtBreakpointAddress = 0;
	mActiveThread->mStoppedAtAddress = 0;
	mActiveThread->mBreakpointAddressContinuing = 0;
}

void WinDebugger::FinishPendingEval()
{
	BfLogDbg("EvaluateContinue finishing pending expr in thread %d\n", mDebugEvalThreadInfo.mThreadId);
	CleanupDebugEval();
}

bool WinDebugger::HasLineInfoAt(addr_target address)
{
	BP_ZONE("WinDebugger::HasLineInfoAt");

	DbgSubprogram* dbgSubprogram = NULL;
	auto dwLineData = FindLineDataAtAddress(address, &dbgSubprogram);
	return (dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dbgSubprogram->GetLineAddr(*dwLineData) == address);
}

void WinDebugger::StepLineTryPause(addr_target address, bool requireExactMatch)
{
	if (mStepInAssembly)
		return;

	if (mStepLineData.mLineData != NULL)
	{
		DbgSubprogram* dbgSubprogram = NULL;
		DbgSrcFile* dbgSrcFile = NULL;
		auto dwLineData = FindLineDataAtAddress(address, &dbgSubprogram, &dbgSrcFile);
		if ((dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && ((!requireExactMatch) || (dbgSubprogram->GetLineAddr(*dwLineData) == address)))
		{
			// "Invalid" line
			if (dwLineData->mColumn == -1)
			{
				SetupStep(mStepType);
				mRunState = RunState_Running;
				return;
			}

			// If we're on the same line but a different column or a <= address then keep it keep looking
			if ((dbgSrcFile == mStepLineData.GetSrcFile()) &&
				((!requireExactMatch) || (dwLineData != mStepLineData.mLineData) || (address <= mStepStartPC)) &&
				(dwLineData->mLine == mStepLineData.mLineData->mLine))
			{
				SetupStep(mStepType);
				mRunState = RunState_Running;
				return;
			}
		}
	}

	mRunState = RunState_Paused;
}

void WinDebugger::BreakAll()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	::DebugBreakProcess(mProcessInfo.hProcess);
}

void WinDebugger::StepInto(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!TryRunContinue())
		return;

	BfLogDbg("StepInto\n");

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepInto);
	ContinueDebugEvent();
}

void WinDebugger::StepIntoSpecific(intptr inAddr)
{
	addr_target addr = (addr_target)inAddr;

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!TryRunContinue())
		return;

	BfLogDbg("StepIntoSpecific %p\n", addr);

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = false;

	SetupStep(StepType_StepInto);
	mIsStepIntoSpecific = true;
	mStepType = StepType_StepInto_Unfiltered;
	if (mStepStartPC != addr)
	{
		RemoveTempBreakpoints();
		SetTempBreakpoint(addr);
		mStepBreakpointAddrs.push_back(addr);
	}

	ContinueDebugEvent();
}

void WinDebugger::PushValue(CPURegisters* registers, int64 val)
{
	addr_target* regSP = registers->GetSPRegisterRef();
	*regSP -= sizeof(addr_target);
	WriteMemory<addr_target>(*regSP, (addr_target)val);
}

void WinDebugger::PushValue(CPURegisters* registers, const DbgTypedValue& typedValue)
{
	addr_target* regSP = registers->GetSPRegisterRef();

	int byteCount = typedValue.mType->GetByteCount();
	if ((byteCount == 8) || (sizeof(addr_target) == 8))
	{
		*regSP -= sizeof(int64);

		addr_target val = typedValue.mInt64;
		if (typedValue.mType->IsCompositeType())
			val = typedValue.mSrcAddress;
		WriteMemory<int64>(*regSP, val);
	}
	else
	{
		*regSP -= sizeof(int32);
		addr_target val = typedValue.mInt32;
		if (typedValue.mType->IsCompositeType())
			val = typedValue.mSrcAddress;
		WriteMemory<int32>(*regSP, val);
	}
}

void WinDebugger::SetThisRegister(CPURegisters* registers, addr_target val)
{
#if BF_DBG_32
	registers->mIntRegs.ecx = val;
#else
	registers->mIntRegs.rcx = val;
#endif
}

void WinDebugger::AddParamValue(int paramIdx, bool hadThis, CPURegisters* registers, const DbgTypedValue& typedValue)
{
#if BF_DBG_32
	PushValue(registers, typedValue);
#else
	int regIdx = paramIdx + (hadThis ? 1 : 0);
	if (typedValue.mType->IsFloat())
	{
		PushValue(registers, typedValue);
		if (regIdx < 4)
		{
			if (typedValue.mType->mTypeCode == DbgType_Single)
			{
				registers->mXmmRegsArray[regIdx].f[0] = typedValue.mSingle;
			}
			else
			{
				registers->mXmmDRegsArray[regIdx].d[0] = typedValue.mDouble;
			}
		}
	}
	else
	{
		PushValue(registers, typedValue);
		if (regIdx < 4)
		{
			int64 val;
			if (typedValue.mType->IsCompositeType())
            	val = typedValue.mSrcAddress;
			else
				val = typedValue.mPtr;
			if (regIdx == 0)
				registers->mIntRegs.rcx = val;
			else if (regIdx == 1)
				registers->mIntRegs.rdx = val;
			else if (regIdx == 2)
				registers->mIntRegs.r8 = val;
			else if (regIdx == 3)
				registers->mIntRegs.r9 = val;
		}
	}
#endif
}

bool WinDebugger::CheckNeedsSRetArgument(DbgType* retType)
{
	if (!retType->IsCompositeType())
		return false;

	if (retType->GetByteCount() == 0)
		return false;

	//TODO: Change when we change the calling convention
	if (retType->GetLanguage() == DbgLanguage_Beef)
		return true;

	int retSize = retType->GetByteCount();
	//TODO: Check for 'POD' type?
	if ((retSize == 1) || (retSize == 2) || (retSize == 4) || (retSize == sizeof(addr_target)))
		return false;
	return true;
}

DbgTypedValue WinDebugger::ReadReturnValue(CPURegisters* registers, DbgType* type)
{
	DbgTypedValue retValue;

	if (type->IsFloat())
	{
		retValue.mType = type;
#if BF_DBG_32
		retValue.mDouble = ConvertFloat80ToDouble(registers->mFpMmRegsArray[0].fp.fp80);
		if (type->mSize == 4)
			retValue.mSingle = (float)retValue.mDouble;
#else
		if (retValue.mType->mTypeCode == DbgType_Single)
			retValue.mSingle = registers->mXmmRegsArray[0].f[0];
		else
			retValue.mDouble = registers->mXmmDRegsArray[0].d[0];
#endif
	}
	else if (type->IsCompositeType())
	{
		retValue.mType = type;
		if (CheckNeedsSRetArgument(type))
		{
#ifdef BF_DBG_32
			retValue.mSrcAddress = mSavedContext.Esp - BF_ALIGN(type->GetByteCount(), 16);
#else
			retValue.mSrcAddress = mSavedContext.Rsp - BF_ALIGN(type->GetByteCount(), 16);
#endif
		}
		else
		{
#ifdef BF_DBG_32
			retValue.mInt32 = mSavedContext.Eax;
#else
			retValue.mInt64 = mSavedContext.Rax;
#endif
		}
	}
	else
	{
#ifdef BF_DBG_32
		retValue.mType = type;
		retValue.mInt32 = registers->mIntRegs.eax;
		if (type->mSize == 8)
			(&retValue.mInt32)[1] = registers->mIntRegs.edx;
#else
		retValue.mType = type;
		retValue.mInt64 = registers->mIntRegs.rax;
#endif
		return retValue;
	}

	return retValue;
}

bool WinDebugger::SetRegisters(CPURegisters* registers)
{
	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_CONTROL | BF_CONTEXT_INTEGER | BF_CONTEXT_FLOATING_POINT | BF_CONTEXT_EXTENDED_REGISTERS | BF_CONTEXT_SEGMENTS;
	lcContext.ContextFlags |= BF_CONTEXT_EXCEPTION_REQUEST;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

#ifdef BF_DBG_32
	lcContext.Eax = registers->mIntRegs.eax;
	lcContext.Ecx = registers->mIntRegs.ecx;
	lcContext.Edx = registers->mIntRegs.edx;
	lcContext.Ebx = registers->mIntRegs.ebx;
	lcContext.Esp = registers->mIntRegs.esp;
	lcContext.Ebp = registers->mIntRegs.ebp;
	lcContext.Esi = registers->mIntRegs.esi;
	lcContext.Edi = registers->mIntRegs.edi;
	lcContext.Eip = registers->mIntRegs.eip;
	lcContext.EFlags = registers->mIntRegs.efl;

	BF_ASSERT(sizeof(lcContext.FloatSave.RegisterArea) == sizeof(registers->mFpMmRegsArray));
	memcpy(lcContext.FloatSave.RegisterArea, registers->mFpMmRegsArray, sizeof(lcContext.FloatSave.RegisterArea));

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 32*sizeof(float));
	memcpy(&lcContext.ExtendedRegisters[160], registers->mXmmRegsArray, sizeof(registers->mXmmRegsArray));
#else
	lcContext.Rax = registers->mIntRegs.rax;
	lcContext.Rcx = registers->mIntRegs.rcx;
	lcContext.Rdx = registers->mIntRegs.rdx;
	lcContext.Rbx = registers->mIntRegs.rbx;
	lcContext.Rsp = registers->mIntRegs.rsp;
	lcContext.Rbp = registers->mIntRegs.rbp;
	lcContext.Rsi = registers->mIntRegs.rsi;
	lcContext.Rdi = registers->mIntRegs.rdi;
	lcContext.Rip = registers->mIntRegs.rip;
	lcContext.EFlags = (DWORD)registers->mIntRegs.efl;

	lcContext.R8 = registers->mIntRegs.r8;
	lcContext.R9 = registers->mIntRegs.r9;
	lcContext.R10 = registers->mIntRegs.r10;
	lcContext.R11 = registers->mIntRegs.r11;
	lcContext.R12 = registers->mIntRegs.r12;
	lcContext.R13 = registers->mIntRegs.r13;
	lcContext.R14 = registers->mIntRegs.r14;
	lcContext.R15 = registers->mIntRegs.r15;

	for (int i = 0; i < 8; i++)
	{
		memcpy(&lcContext.FltSave.FloatRegisters[i], &registers->mFpMmRegsArray[i], 10);
	}

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 64 * sizeof(float));
	memcpy(BF_CONTEXT_XMMDATA(lcContext), registers->mXmmRegsArray, sizeof(registers->mXmmRegsArray));
#endif

	//lcContext.ContextFlags |= BF_CONTEXT_EXCEPTION_REQUEST;

	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

	return (lcContext.ContextFlags & (BF_CONTEXT_EXCEPTION_ACTIVE | BF_CONTEXT_SERVICE_ACTIVE)) == 0;
}

void WinDebugger::SaveAllRegisters()
{
	BfLogDbg("SaveAllRegisters setting mSavedAtBreakpointAddress = %p\n", mActiveThread->mIsAtBreakpointAddress);
	mSavedAtBreakpointAddress = mActiveThread->mIsAtBreakpointAddress;
	mSavedBreakpointAddressContinuing = mActiveThread->mBreakpointAddressContinuing;
	mSavedContext.ContextFlags = BF_CONTEXT_ALL;
	BF_GetThreadContext(mActiveThread->mHThread, &mSavedContext);
}

void WinDebugger::RestoreAllRegisters()
{
	BfLogDbg("RestoreAllRegisters setting mIsAtBreakpointAddress = %p\n", mSavedAtBreakpointAddress);
	mActiveThread->mIsAtBreakpointAddress = mSavedAtBreakpointAddress;
	mActiveThread->mBreakpointAddressContinuing = mSavedBreakpointAddressContinuing;
	BF_SetThreadContext(mActiveThread->mHThread, &mSavedContext);

#ifdef BF_DBG_32
	//TODO: Find the test that this was required for...
// 	if (mActiveThread->mIsAtBreakpointAddress == mSavedContext.Eip)
// 	{
// 		if (mRunState == RunState_Running_ToTempBreakpoint)
// 			mRunState = RunState_Paused;
// 	}
// 	else
// 	{
// 		SetTempBreakpoint(mSavedContext.Eip);
// 		mRunState = RunState_Running_ToTempBreakpoint;
// 		mStepType = StepType_ToTempBreakpoint;
// 		mSteppingThread = mActiveThread;
// 	}
#endif
}

void WinDebugger::OutputMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back("msg " + msg);
}

void WinDebugger::OutputRawMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back(msg);
}

void WinDebugger::SetRunState(RunState runState)
{
	mRunState = runState;
}

bool WinDebugger::TryRunContinue()
{
	if (mRunState == RunState_Exception)
	{
		mIsContinuingFromException = true;
		mRunState = RunState_Paused;
	}

	if (((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint)) && (mNeedsRehupBreakpoints))
		RehupBreakpoints(true);

	return true;
}

void WinDebugger::ClearStep()
{
	BfLogDbg("ClearStep\n");

	RemoveTempBreakpoints();
	mOrigStepType = StepType_None;
	mStepType = StepType_None;
	mStepStartPC = 0;
	mStepSP = 0;
	mStepPC = 0;
	mIsStepIntoSpecific = false;
	mStepIsRecursing = false;
	mStepStopOnNextInstruction = false;
	mStepLineData = DbgLineDataEx();
}

bool WinDebugger::SetupStep(StepType stepType)
{
	BP_ZONE("SetupStep");

	RemoveTempBreakpoints();
	if (mNeedsRehupBreakpoints)
		RehupBreakpoints(true);

	if (mOrigStepType == StepType_None)
		mOrigStepType = stepType;
	mStepType = stepType;
	mSteppingThread = mActiveThread;
	mStepSwitchedThreads = false;
	mContinueFromBreakpointFailed = false;

	CPURegisters registers;
	PopulateRegisters(&registers);
	addr_target pcAddress = registers.GetPC();

	if (mStepLineData.IsNull())
	{
		DbgSubprogram* dbgSubprogram = NULL;
		auto dbgLineData = FindLineDataAtAddress(pcAddress, &dbgSubprogram);
		mStepLineData = DbgLineDataEx(dbgLineData, dbgSubprogram);
		mStepStartPC = registers.GetPC();
	}

	bool isDeeper = mStepSP > registers.GetSP();

	BfLogDbg("SetupStep %d PC:%p SP:%p StepStartSP:%p Thread:%d\n", stepType, (addr_target)registers.GetPC(), (addr_target)registers.GetSP(), (addr_target)mStepSP, mSteppingThread->mThreadId);

	mStepSP = registers.GetSP();
	mStepPC = registers.GetPC();

	if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_NoFrame) || (mStepType == StepType_StepOut_ThenInto))
	{
		if (mStepType != StepType_StepOut_NoFrame)
		{
			// Test for stepping out of an inline method
			DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);
			if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL))
			{
				DbgSubprogram* topSubprogram = dwSubprogram->GetRootInlineParent();

				if ((mOrigStepType == StepType_StepInto) || (mOrigStepType == StepType_StepInto_Unfiltered))
				{
					mStepType = mOrigStepType;
				}
				else
				{
					mStepType = StepType_StepOut_Inline;
					// Set up pcAddress to detect recursion
					//TODO: We can't set a physical breakpoint here because we will immediately hit it when attempting to step over an inlined method.
					//  An inlined method can't recurse anyway, but store the pcAddress in mTempBreakpoints because we still check that for recursion
					// SetTempBreakpoint(pcAddress);
					//mTempBreakpoint.push_back(pcAddress);
					mStepBreakpointAddrs.push_back(pcAddress);
				}

				addr_target endAddress = dwSubprogram->mBlock.mHighPC;
				if (dwSubprogram->mHasLineAddrGaps)
				{
					// Keep bumping out the address as long as we can find lines that contain the nextPC
					addr_target nextAddr = pcAddress;
					for (auto& lineInfo : topSubprogram->mLineInfo->mLines)
					{
						auto lineAddr = topSubprogram->GetLineAddr(lineInfo);
						if ((nextAddr >= lineAddr) && (nextAddr < lineAddr + lineInfo.mContribSize))
						{
							auto ctx = topSubprogram->mLineInfo->mContexts[lineInfo.mCtxIdx];
							if (ctx.mInlinee == dwSubprogram)
							{
								nextAddr = lineAddr + lineInfo.mContribSize;
							}
						}
					}
					if (nextAddr != pcAddress)
						endAddress = nextAddr;
				}

				BfLogDbg("Stepping out of inlined method, end address: %p\n", endAddress);
				SetTempBreakpoint(endAddress);
				mStepBreakpointAddrs.push_back(endAddress);

				addr_target decodeAddress = dwSubprogram->mBlock.mLowPC;
				while (decodeAddress < endAddress)
				{
					CPUInst inst;
					if (!mDebugTarget->DecodeInstruction(decodeAddress, &inst))
						break;

					addr_target targetAddress = inst.GetTarget();
					// We need to find a targetAddress
					if ((targetAddress != 0) &&
						!((targetAddress >= dwSubprogram->mBlock.mLowPC) && (targetAddress < dwSubprogram->mBlock.mHighPC)) &&
						((targetAddress >= topSubprogram->mBlock.mLowPC) && (targetAddress < topSubprogram->mBlock.mHighPC)))
					{
						BfLogDbg("Stepping out of inlined method, branch address: %p\n", targetAddress);
						SetTempBreakpoint(targetAddress);
						mStepBreakpointAddrs.push_back(targetAddress);
					}

					decodeAddress += inst.GetLength();
				}

				return true;
			}
		}

		if ((mStepType != StepType_StepOut_NoFrame) && (RollBackStackFrame(&registers, true)))
		{
			bool isStackAdjust = false;
			DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);
			if (dwSubprogram != NULL)
			{
				if ((strcmp(dwSubprogram->mName, "_chkstk") == 0) ||
					(strcmp(dwSubprogram->mName, "__chkstk") == 0) ||
					(strcmp(dwSubprogram->mName, "_alloca_probe") == 0))
					isStackAdjust = true;
			}

			pcAddress = registers.GetPC();
			if (isStackAdjust)
			{
				// We set it to zero so we never detect an "isDeeper" condition which would skip over the return-location breakpoint
				mStepSP = 0;
			}
			else
			{
				addr_target oldAddress = pcAddress;

				CPUInst inst;
				while (true)
				{
					if (mStepInAssembly)
						break;
					if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
						break;
					if ((inst.IsBranch()) || (inst.IsCall()) || (inst.IsReturn()))
						break;
#ifdef BF_DBG_32
					if (!inst.StackAdjust(mStepSP))
						break;
#endif
					DbgSubprogram* checkSubprogram = NULL;
					auto checkLineData = FindLineDataAtAddress(pcAddress, &checkSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
					if (checkLineData == NULL)
						break;
					if (checkSubprogram->GetLineAddr(*checkLineData) == pcAddress)
						break;
					pcAddress += inst.GetLength();
				}

				if (pcAddress != oldAddress)
				{
					BfLogDbg("Adjusting stepout address from %p to %p\n", oldAddress, pcAddress);
				}
			}

			BfLogDbg("SetupStep Stepout SetTempBreakpoint %p\n", pcAddress);
			SetTempBreakpoint(pcAddress);
			mStepBreakpointAddrs.push_back(pcAddress);
			if (mStepType != StepType_StepOut_ThenInto)
				mStepType = StepType_StepOut;
		}
		else
		{
			// Try to handle the case where we just entered this call so the return address is the first entry on the stack
			addr_target* regSP = registers.GetSPRegisterRef();
			pcAddress = ReadMemory<addr_target>(*regSP);
			*regSP += sizeof(addr_target);
			if (mDebugTarget->FindSubProgram(pcAddress) != NULL)
			{
				BfLogDbg("SetupStep Stepout SetTempBreakpoint (2) %p\n", pcAddress);

				SetTempBreakpoint(pcAddress);
				mStepBreakpointAddrs.push_back(pcAddress);
				if (mOrigStepType == StepType_StepInto)
					mStepType = StepType_StepInto;
				else
					mStepType = StepType_StepOver;
				return true;
			}
			else
			{
				// Just do stepovers until we eventually step out
				//BF_DBG_FATAL("StepOut Failed");
				BfLogDbg("StepOut Failed\n");

				if (mLastValidStepIntoPC != 0)
				{
					BfLogDbg("Using mLastValidStepIntoPC: %p\n", mLastValidStepIntoPC);
					if (mOrigStepType == StepType_StepInto)
						mStepType = StepType_StepInto;
					else
						mStepType = StepType_StepOver;
					SetTempBreakpoint(mLastValidStepIntoPC);
					mStepBreakpointAddrs.push_back(0);
					mStepBreakpointAddrs.push_back(mLastValidStepIntoPC);
					mLastValidStepIntoPC = 0;
					return true;
				}
				else
				{
					BfLogDbg("Stopping");
					mStepType = StepType_None;
					mRunState = RunState_Paused;
					return true;
				}
			}
		}
	}

	if ((mStepType != StepType_StepOut) && (mStepType != StepType_StepOut_ThenInto))
	{
		if (mDebuggerWaitingThread != mSteppingThread)
		{
			// We've switched threads, so there's a possible race condition:
			//  This new thread may already have an EXCEPTION_BREAKPOINT queued up so the PC is actually
			//  located one byte past the BREAK instruction, which is one byte into whatever instruction
			//  was previously there.  We can't insert normal BREAK instructions because we don't know
			//  if the current PC is actually at an instruction start, so we do a single step with a
			//  slower stack call check to see if we need to step out after a "step over"
			BfLogDbg("Step - switched threads mIsAtBreakpointAddress:%p\n", mSteppingThread->mIsAtBreakpointAddress);
			mStepSwitchedThreads = true;
			SingleStepX86();
			return true;
		}

		bool breakOnNext = false;

		int instIdx = 0;
		for (instIdx = 0; true; instIdx++)
		{
			bool isAtLine = false;
			DbgSubprogram* dwSubprogram = NULL;

			auto dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
			isAtLine = (instIdx > 0) && (dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dwSubprogram->GetLineAddr(*dwLineData) == pcAddress);

			// "Never step into" line
			if ((dwLineData != NULL) && (dwLineData->mColumn == -2) && (stepType == StepType_StepInto))
				stepType = StepType_StepOver;

			CPUInst inst;
			if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
			{
				BfLogDbg("Decode failed, set up SingleStepX86 %p\n", pcAddress);
				SingleStepX86();
				mStepStopOnNextInstruction = true;
				return true;
			}

			if (instIdx > 256)
			{
				BfLogDbg("Too many SetupStep iterations");
				breakOnNext = true;
			}

			if ((inst.IsReturn()) && (instIdx == 0) && (!mStepInAssembly))
			{
				// Do actual STEP OUT so we set up proper "stepping over unimportant post-return instructions"
				if (stepType == StepType_StepInto)
					return SetupStep(StepType_StepOut_ThenInto);
				else
					return SetupStep(StepType_StepOut);
			}

			if ((breakOnNext) || (mStepInAssembly) || (isAtLine) || (inst.IsBranch()) || (inst.IsCall()) || (inst.IsReturn()))
			{
				if (((instIdx == 0) || (mStepInAssembly)) && (!breakOnNext))
				{
					if ((stepType == StepType_StepOver) && (inst.IsCall()))
					{
						// Continue - sets a breakpoint on the call line to detect recursion.
						//  The next loop through will set a breakpoint on the line after the return
						BfLogDbg("StepHadCall\n");
						breakOnNext = true;

						BfLogDbg("StepHadCall setting mIsAtBreakpointAddress = %p\n", pcAddress);
						mSteppingThread->mIsAtBreakpointAddress = pcAddress;

						SetTempBreakpoint(pcAddress);
						mStepBreakpointAddrs.push_back(pcAddress);
					}
					else
					{
						if (inst.IsCall())
						{
							if ((mLastValidStepIntoPC == 0) || (dwSubprogram != NULL))
								mLastValidStepIntoPC = pcAddress + inst.mSize;
						}

						if ((dwLineData != NULL) && (inst.IsBranch()))
						{
							addr_target targetAddr = inst.GetTarget();
							if (targetAddr < dwSubprogram->GetLineAddr(*dwLineData))
							{
								// Jumping backwards, stop at next instruction
								mStepStopOnNextInstruction = true;
							}
						}

						bool isPrefixOnly = false;
						if ((mStepInAssembly) && (stepType == StepType_StepOver) && (inst.IsRep(isPrefixOnly)))
						{
							if (isPrefixOnly)
							{
								CPUInst nextInst;
								if (mDebugTarget->DecodeInstruction(pcAddress + inst.GetLength(), &nextInst))
								{
									if (nextInst.IsBranch())
									{
										// repne jmp - this appears in __chkstk (for example)
										// We don't have a good way to "step over" this one, so just do a single step
									}
									else
									{
										// Step over the rep + target instruction
										auto doneAddr = pcAddress + inst.GetLength() + nextInst.GetLength();
										BfLogDbg("SetupStep SetTempBreakpoint %p\n", doneAddr);
										SetTempBreakpoint(doneAddr);
										mStepBreakpointAddrs.push_back(doneAddr);
										break;
									}
								}
							}
							else
							{
								// Step over the instruction
								auto doneAddr = pcAddress + inst.GetLength();
								BfLogDbg("SetupStep SetTempBreakpoint %p\n", doneAddr);
								SetTempBreakpoint(doneAddr);
								mStepBreakpointAddrs.push_back(doneAddr);
								break;
							}
						}

						// Just step a single instruction
						BfLogDbg("SetupStep SingleStepX86 %p\n", pcAddress);
						SingleStepX86();
						if (inst.IsReturn())
							mStepStopOnNextInstruction = true;
						break;
					}
				}
				else
				{
					// Move us to this instruction so we can hardware single-step into it
					BfLogDbg("SetupStep SetTempBreakpoint %p\n", pcAddress);
					SetTempBreakpoint(pcAddress);
					mStepBreakpointAddrs.push_back(pcAddress);
					break;
				}
			}

			// Not an interesting instruction - move to next
			pcAddress += inst.mSize;

			if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress >= dwSubprogram->mBlock.mHighPC))
			{
				auto endAddress = dwSubprogram->mBlock.mHighPC;
				BfLogDbg("Stepping past end of inlined method, end address: %p\n", endAddress);

				mStepType = StepType_StepOut_Inline;
				SetTempBreakpoint(endAddress);
				mStepBreakpointAddrs.push_back(endAddress);
				return true;
			}
		}

		if (instIdx > 1)
			BfLogDbg("SetupStep instIdx: %d\n", instIdx);
	}

	return true;
}

void WinDebugger::CheckNonDebuggerBreak()
{
	enum MessageType
	{
		MessageType_None = 0,
		MessageType_Error = 1,
		MessageType_ProfilerCmd = 2
	};

	CPURegisters registers;
	PopulateRegisters(&registers);
	addr_target pcAddress = registers.GetPC();

	addr_target debugMessageDataAddr = (addr_target)-1;
	if (mDebugTarget->mTargetBinary != NULL)
	{
		mDebugTarget->mTargetBinary->ParseSymbolData();
		debugMessageDataAddr = mDebugTarget->FindSymbolAddr("gBfDebugMessageData");
	}
	if (debugMessageDataAddr != (addr_target)-1)
	{
		struct BfDebugMessageData
		{
			int mMessageType; // 0 = none, 1 = error
			int mStackWindbackCount;
			int mBufParamLen;
			addr_target mBufParam;
			addr_target mPCOverride;
		};
		BfDebugMessageData messageData = ReadMemory<BfDebugMessageData>(debugMessageDataAddr);
		WriteMemory<int>(debugMessageDataAddr, 0); // Zero out type so we won't trigger again
		if (messageData.mMessageType != 0)
		{
			llvm::SmallVector<char, 4096> strBuf;
			int strLen = messageData.mBufParamLen;
			strBuf.resize(strLen + 1);

			char* str = &strBuf[0];
			str[strLen] = 0;
			if (ReadMemory(messageData.mBufParam, strLen, str))
			{
				if (messageData.mMessageType == MessageType_Error)
				{
					mRequestedStackFrameIdx = messageData.mStackWindbackCount;
					if (messageData.mPCOverride != 0)
					{
						mShowPCOverride = messageData.mPCOverride;
						mRequestedStackFrameIdx = -2;
					}
					mDebugManager->mOutMessages.push_back(StrFormat("error %s", str));
				}
				else if (messageData.mMessageType == MessageType_ProfilerCmd)
				{
					// It's important to set this here, because we unlock the critSect during StopSampling and we can't have the
					//  IDE thinking that we're actually paused when it checks the mRunState
					mRunState = RunState_Running;

					char* cmd = strtok(str, "\t");
					if (strcmp(cmd, "StartSampling") == 0)
					{
						char* sessionIdStr = strtok(NULL, "\t");
						char* threadIdStr = strtok(NULL, "\t");
						char* sampleRateStr = strtok(NULL, "\t");
						char* descStr = strtok(NULL, "\t");

						if (threadIdStr != NULL)
						{
							int threadId = atoi(threadIdStr);
							int sampleRate = atoi(sampleRateStr);
							int sessionId = atoi(sessionIdStr);

							Profiler** profilerPtr;
							if (mPendingProfilerMap.TryAdd(sessionId, NULL, &profilerPtr))
							{
								DbgProfiler* profiler = new DbgProfiler(this);
								if (descStr != NULL)
									profiler->mDescription = descStr;
								if (sampleRate > 0)
									profiler->mSamplesPerSecond = sampleRate;
								profiler->Start();
								*profilerPtr = profiler;

								mDebugManager->mOutMessages.push_back("newProfiler");
								mNewProfilerList.push_back(profiler);
							}
						}
					}
					else if (strcmp(cmd, "StopSampling") == 0)
					{
						char* sessionIdStr = strtok(NULL, "\t");
						if (sessionIdStr != NULL)
						{
							int sessionId = atoi(sessionIdStr);

							Profiler* profiler;
							if (mPendingProfilerMap.Remove(sessionId, &profiler))
							{
								if (profiler->IsSampling())
								{
									// Need to unlock so we don't deadlock
									mDebugManager->mCritSect.Unlock();
									profiler->Stop();
									mDebugManager->mCritSect.Lock();
								}
							}
						}
					}
					else if (strcmp(cmd, "ClearSampling") == 0)
					{
						for (auto& kv : mPendingProfilerMap)
						{
							auto profiler = kv.mValue;
							profiler->Clear();
						}
					}
					else if (strcmp(cmd, "ClearOutput") == 0)
					{
						mDebugManager->mOutMessages.push_back("clearOutput");
					}
				}
				return;
			}
		}
	}

	intptr_target objAddr;
	auto dbgBreakKind = mDebugTarget->GetDbgBreakKind(pcAddress, &registers, &objAddr);
	if (dbgBreakKind == DbgBreakKind_ObjectAccess)
	{
		String errorStr = "error Attempted to access deleted object";
		String objectAddr = EncodeDataPtr((addr_target)objAddr, true);
		errorStr += StrFormat("\x1LEAK\t(System.Object)%s\n   (%s)%s\n", objectAddr.c_str(), "System.Object", objectAddr.c_str());
		mDebugManager->mOutMessages.push_back(errorStr);
		return;
	}
	else if (dbgBreakKind == DbgBreakKind_ArithmeticOverflow)
	{
		String errorStr = "error Arithmetic overflow detected";
		mDebugManager->mOutMessages.push_back(errorStr);
		return;
	}

	bool showMainThread = false;

	String symbol;
	addr_target offset;
	DbgModule* dbgModule;
	if (mDebugTarget->FindSymbolAt(pcAddress, &symbol, &offset, &dbgModule))
	{
		if ((symbol == "DbgBreakPoint") || (symbol == "RtlUserThreadStart") || (symbol == "RtlUserThreadStart@8"))
		{
			showMainThread = true;
		}
	}
#ifdef BF_DBG_32
	else if ((dbgModule != NULL) && (dbgModule->mDisplayName.Equals("kernel32.dll", StringImpl::CompareKind_OrdinalIgnoreCase)))
	{
		showMainThread = true;
	}
#endif

	if (showMainThread)
	{
		// This is a manual break, show the main thread
		mActiveThread = mThreadList.front();
		if (mDebugPendingExpr != NULL)
		{
			for (auto thread : mThreadList)
			{
				if (thread->mThreadId == mDebugEvalThreadInfo.mThreadId)
				{
					mActiveThread = thread;
					break;
				}
			}
		}
	}
}

bool WinDebugger::HasSteppedIntoCall()
{
	//  Some calls (like __chkstk) actually push results to the stack, so we need to check
	//  if we're REALLY deeper or not, by rolling back the callstack once

	CPURegisters registers;
	PopulateRegisters(&registers);
	if (RollBackStackFrame(&registers, true))
	{
		// If the previous frames SP is equal or deeper than our step start then we are indeed inside a call!
		if (mStepSP >= registers.GetSP())
			return true;
	}
	return false;
}

void WinDebugger::StepOver(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("StepOver\n");

	if (!TryRunContinue())
		return;

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepOver);
	ContinueDebugEvent();
}

void WinDebugger::StepOut(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("StepOut\n");

	if (!TryRunContinue())
		return;

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepOut);

	ContinueDebugEvent();
}

void WinDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	DbgSubprogram* subProgram = NULL;
	if (!inAssembly)
	{
		if (mCallStack.size() == 0)
			UpdateCallStack();
		if (mCallStack.size() > 0)
		{
			UpdateCallStackMethod(0);
			subProgram = mCallStack[0]->mSubProgram;
		}
		if (subProgram == NULL)
			return;
	}

	DbgSubprogram* rootInlineParent = NULL;
	if (subProgram != NULL)
		rootInlineParent = subProgram->GetRootInlineParent();

	String result;
	if (mDebugTarget == NULL)
		return;

	DbgSrcFile* srcFile = NULL;
	if (!fileName.IsEmpty())
	{
		srcFile = mDebugTarget->GetSrcFile(fileName);
		if (srcFile == NULL)
			return;
	}

	addr_target pcAddress = 0;
	if (inAssembly)
	{
		pcAddress = lineNumOrAsmAddr;
	}
	else
	{
		int lineNum = (int)lineNumOrAsmAddr;

		addr_target bestAddr[2] = { 0, 0 };
		int checkLineNum[2] = { lineNum - 1, lineNum };

		auto _CheckLineInfo = [&](DbgSubprogram* dbgSubprogram, DbgLineInfo* dbgLineInfo)
		{
			for (int iPass = 0; iPass < 2; ++iPass)
			{
				int bestLineOffset = 0x7FFFFFFF;

				for (auto& lineData : dbgLineInfo->mLines)
				{
					auto addr = dbgSubprogram->GetLineAddr(lineData);
					if ((addr < subProgram->mBlock.mLowPC) || (addr >= subProgram->mBlock.mHighPC))
						continue;

					int lineOffset = lineData.mLine - checkLineNum[iPass];

					if ((lineOffset >= 0) && (lineOffset <= 6) && (lineOffset <= bestLineOffset))
					{
						if (lineOffset < bestLineOffset)
						{
							bestLineOffset = lineOffset;
							bestAddr[iPass] = addr;
						}
					}
				}
			}
		};

		for (int checkHotIdx = -1; checkHotIdx < (int)srcFile->mHotReplacedDbgLineInfo.size(); checkHotIdx++)
		{
			if (checkHotIdx >= 0)
			{
				auto hotReplacedLineInfo = srcFile->mHotReplacedDbgLineInfo[checkHotIdx];
				for (auto& hotReplacedEntry : hotReplacedLineInfo->mEntries)
				{
					_CheckLineInfo(hotReplacedEntry.mSubprogram, hotReplacedEntry.mLineInfo);
				}
			}
			else
			{
				for (auto subprogram : srcFile->mLineDataRefs)
					_CheckLineInfo(subprogram, subprogram->mLineInfo);
			}

			if (bestAddr[1] != 0)
				break;
		}

		if (bestAddr[1] != 0)
		{
			const int kMaxAddrDist = 64; // within reasonable range
			if ((bestAddr[0] != 0) && (bestAddr[1] - bestAddr[0] <= kMaxAddrDist))
			{
				addr_target addrStart = bestAddr[0];
				addr_target addrEnd = bestAddr[1];
				addr_target addr = addrStart;

				BF_ASSERT(addrEnd - addr <= kMaxAddrDist);

				addr_target lastOp = 0;
				while (addr < addrEnd)
				{
					CPUInst inst;
					if (!mDebugTarget->DecodeInstruction(addr, &inst))
						break;
					lastOp = addr;
					addr += inst.GetLength();
				}
			}

			pcAddress = (uint64)bestAddr[1];
		}
	}

	if (pcAddress)
	{
		BF_ASSERT(mActiveThread->mBreakpointAddressContinuing == 0);
		mActiveThread->mIsAtBreakpointAddress = 0;
		mActiveThread->mStoppedAtAddress = pcAddress;

		if (mCallStack.size() == 0)
			UpdateCallStack();

		CPURegisters* regs = &mCallStack.front()->mRegisters;
		*regs->GetPCRegisterRef() = pcAddress;

		SetRegisters(regs);

		WdBreakpoint* breakpoint = (WdBreakpoint*)FindBreakpointAt(pcAddress);
		if (breakpoint != NULL)
		{
			BfLogDbg("SetNextStatement setting mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
			mActiveThread->mIsAtBreakpointAddress = breakpoint->mAddr;
		}
	}
}

bool WinDebugger::PopulateRegisters(CPURegisters* registers, BF_CONTEXT& lcContext)
{
#ifdef BF_DBG_32
	registers->mIntRegs.eax = lcContext.Eax;
	registers->mIntRegs.ecx = lcContext.Ecx;
	registers->mIntRegs.edx = lcContext.Edx;
	registers->mIntRegs.ebx = lcContext.Ebx;
	registers->mIntRegs.esp = lcContext.Esp;
	registers->mIntRegs.ebp = lcContext.Ebp;
	registers->mIntRegs.esi = lcContext.Esi;
	registers->mIntRegs.edi = lcContext.Edi;
	registers->mIntRegs.eip = lcContext.Eip;
	registers->mIntRegs.efl = lcContext.EFlags;

	BF_ASSERT(sizeof(lcContext.FloatSave.RegisterArea) == sizeof(registers->mFpMmRegsArray));
	memcpy(registers->mFpMmRegsArray, lcContext.FloatSave.RegisterArea, sizeof(lcContext.FloatSave.RegisterArea));

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 32 * sizeof(float));
	memcpy(registers->mXmmRegsArray, &lcContext.ExtendedRegisters[160], sizeof(registers->mXmmRegsArray));
#else
	registers->mIntRegs.rax = lcContext.Rax;
	registers->mIntRegs.rcx = lcContext.Rcx;
	registers->mIntRegs.rdx = lcContext.Rdx;
	registers->mIntRegs.rbx = lcContext.Rbx;
	registers->mIntRegs.rsp = lcContext.Rsp;
	registers->mIntRegs.rbp = lcContext.Rbp;
	registers->mIntRegs.rsi = lcContext.Rsi;
	registers->mIntRegs.rdi = lcContext.Rdi;
	registers->mIntRegs.rip = lcContext.Rip;
	registers->mIntRegs.efl = lcContext.EFlags;

	registers->mIntRegs.r8 = lcContext.R8;
	registers->mIntRegs.r9 = lcContext.R9;
	registers->mIntRegs.r10 = lcContext.R10;
	registers->mIntRegs.r11 = lcContext.R11;
	registers->mIntRegs.r12 = lcContext.R12;
	registers->mIntRegs.r13 = lcContext.R13;
	registers->mIntRegs.r14 = lcContext.R14;
	registers->mIntRegs.r15 = lcContext.R15;

	registers->mIntRegs.gs = lcContext.SegGs;

	for (int i = 0; i < 8; i++)
	{
		memcpy(&registers->mFpMmRegsArray[i], &lcContext.FltSave.FloatRegisters[i], 10);
	}

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 64 * sizeof(float));
	memcpy(registers->mXmmRegsArray, BF_CONTEXT_XMMDATA(lcContext), sizeof(registers->mXmmRegsArray));
#endif

	return (lcContext.ContextFlags & (BF_CONTEXT_EXCEPTION_ACTIVE | BF_CONTEXT_SERVICE_ACTIVE)) == 0;
}

bool WinDebugger::PopulateRegisters(CPURegisters* registers)
{
	/*static bool sCheckedProcessorFeatures = false;
	static bool sMmxAvailable = false;
	static bool sXmmAvailable = false;
	if (!sCheckedProcessorFeatures)
	{
		//CDH we don't do anything with these yet since we grab BF_CONTEXT_ALL anyway, but could be useful
		sMmxAvailable = ::IsProcessorFeaturePresent(PF_MMX_INSTRUCTIONS_AVAILABLE) != 0;
		sXmmAvailable = ::IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE) != 0;
		sCheckedProcessorFeatures = true;
	}*/

	BF_ASSERT(registers != nullptr);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL | BF_CONTEXT_EXCEPTION_REQUEST;
	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

	return PopulateRegisters(registers, lcContext);
}

bool WinDebugger::RollBackStackFrame(CPURegisters* registers, bool isStackStart)
{
	BF_ASSERT(registers != nullptr);

	return mDebugTarget->RollBackStackFrame(registers, NULL, isStackStart);
}

bool WinDebugger::SetHotJump(DbgSubprogram* oldSubprogram, addr_target newTarget, int newTargetSize)
{
	BfLogDbg("SetHotJump %s %p->%p\n", oldSubprogram->mName, oldSubprogram->mBlock.mLowPC, newTarget);

	//AutoCrit autoCrit(mDebugManager->mCritSect);
	BF_ASSERT(mDebugManager->mCritSect.mLockCount == 1);

	addr_target jmpInstStart = oldSubprogram->mBlock.mLowPC;
	addr_target jmpInstEnd = jmpInstStart + sizeof(HotJumpOp);

	if (jmpInstEnd > oldSubprogram->mBlock.mHighPC)
	{
		if ((oldSubprogram->mBlock.mHighPC - oldSubprogram->mBlock.mLowPC == 1) &&
			(newTargetSize == 1))
			return true; // Special case for just stub 'ret' methods
		String err = StrFormat("Failed to hot replace method, method '%s' too small to insert hot thunk", oldSubprogram->ToString().c_str());
		Fail(err);
		return false;
	}

	if (oldSubprogram->mHotReplaceKind != DbgSubprogram::HotReplaceKind_Replaced)
	{
		for (int hotThreadIdx = 0; hotThreadIdx < (int)mHotThreadStates.size(); hotThreadIdx++)
		{
			auto& hotThreadState = mHotThreadStates[hotThreadIdx];
			WdThreadInfo* threadInfo = NULL;
			if (!mThreadMap.TryGetValue((uint32)hotThreadState.mThreadId, &threadInfo))
				continue;

			int tryStart = GetTickCount();

			while ((hotThreadState.mRegisters.GetPC() >= jmpInstStart) && (hotThreadState.mRegisters.GetPC() < jmpInstEnd))
			{
				if (GetTickCount() - tryStart >= 8000)
				{
					Fail("Failed to hot replace method, can't move past prelude");
					return false;
				}

				BfLogDbg("SetHotJump skipping through %p\n", hotThreadState.mRegisters.GetPC());

				bool removedBreakpoint = false;

				mActiveThread = threadInfo;
				if ((mActiveThread->mStoppedAtAddress >= jmpInstStart) && (mActiveThread->mStoppedAtAddress < jmpInstEnd))
				{
					for (addr_target addr = jmpInstStart; addr < jmpInstEnd; addr++)
					{
						if (mPhysBreakpointAddrMap.ContainsKey(addr))
						{
							removedBreakpoint = true;
							RemoveBreakpoint(addr);
						}
					}
				}

				RunState oldRunState = mRunState;
				mRunState = RunState_HotStep;

				if (mWantsDebugContinue)
				{
					mWantsDebugContinue = false;
					BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
					mContinueEvent.Set();
				}

				BF_CONTEXT lcContext;
				lcContext.ContextFlags = BF_CONTEXT_ALL;
				BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
				lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
				BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

				::ResumeThread(mActiveThread->mHThread);
				BfLogDbg("ResumeThread %d\n", mActiveThread->mThreadId);

				while (mRunState != RunState_Terminated)
				{
					mDebugManager->mCritSect.Unlock();
					Sleep(0);
					mDebugManager->mCritSect.Lock();
					if (IsPaused())
						break;
					if (mWantsDebugContinue)
					{
						BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

						mWantsDebugContinue = false;
						BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
						mContinueEvent.Set();
					}
				}

				BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

				::SuspendThread(mActiveThread->mHThread);
				BfLogDbg("SuspendThread %d\n", mActiveThread->mThreadId);
				mRunState = oldRunState;

				if ((mRunState != RunState_Terminated) && (mRunState != RunState_Terminating))
				{
					if (!IsPaused())
					{
						BF_ASSERT(mWantsDebugContinue);
						mWantsDebugContinue = false;
						BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
						mContinueEvent.Set();
					}
				}

				PopulateRegisters(&hotThreadState.mRegisters);
			}
		}
	}

	HotJumpOp jumpOp;
	jumpOp.mOpCode = 0xE9;
	jumpOp.mRelTarget = newTarget - oldSubprogram->mBlock.mLowPC - sizeof(HotJumpOp);
	WriteMemory(oldSubprogram->mBlock.mLowPC, jumpOp);
	::FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)oldSubprogram->mBlock.mLowPC, sizeof(HotJumpOp));
	return true;
}

DbgSubprogram* WinDebugger::TryFollowHotJump(DbgSubprogram* subprogram, addr_target addr)
{
	if (subprogram->mHotReplaceKind != DbgSubprogram::HotReplaceKind_Replaced)
		return subprogram;

	if (addr != subprogram->mBlock.mLowPC)
		return subprogram;

	auto dbgModule = subprogram->mCompileUnit->mDbgModule;

	HotJumpOp jumpOp = ReadMemory<HotJumpOp>(addr);
	if (jumpOp.mOpCode != 0xE9)
		return subprogram;
	addr_target jumpAddr = addr + jumpOp.mRelTarget + sizeof(HotJumpOp);

	auto jumpSubprogram = mDebugTarget->FindSubProgram(jumpAddr);
	if (jumpSubprogram == NULL)
		return subprogram;

	return jumpSubprogram;
}

String WinDebugger::GetProcessInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread == NULL) && (!mIsRunning))
		return "";

	SYSTEM_INFO sysinfo = { 0 };
	GetSystemInfo(&sysinfo);

	FILETIME creationTime = { 0 };
	FILETIME exitTime = { 0 };
	FILETIME kernelTime = { 0 };
	FILETIME userTime = { 0 };
	::GetProcessTimes(mProcessInfo.hProcess, &creationTime, &exitTime, &kernelTime, &userTime);

	String retStr;
	PROCESS_MEMORY_COUNTERS memInfo = { 0 };
	::GetProcessMemoryInfo(mProcessInfo.hProcess, &memInfo, sizeof(PROCESS_MEMORY_COUNTERS));

	FILETIME currentTime = { 0 };
	::GetSystemTimeAsFileTime(&currentTime);

	retStr += StrFormat("VirtualMemory\t%lld\n", memInfo.PagefileUsage);
	retStr += StrFormat("WorkingMemory\t%lld\n", memInfo.WorkingSetSize);
	retStr += StrFormat("RunningTime\t%lld\n", *(int64*)&currentTime - *(int64*)&creationTime);
	retStr += StrFormat("KernelTime\t%lld\n", *(int64*)&kernelTime / sysinfo.dwNumberOfProcessors);
	retStr += StrFormat("UserTime\t%lld\n", *(int64*)&userTime / sysinfo.dwNumberOfProcessors);

	return retStr;
}

int WinDebugger::GetProcessId()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	if (!mThreadList.IsEmpty())
		return mThreadList[0]->mProcessId;
	return mDbgProcessId;
}

String WinDebugger::GetThreadInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String retStr;
	if ((mActiveThread == NULL) && (!mIsRunning))
	{
		retStr = "";
	}
	else
	{
		if (mActiveThread != NULL)
			retStr = StrFormat("%d", mActiveThread->mThreadId);

		for (auto threadInfo : mThreadList)
		{
			SetAndRestoreValue<WdThreadInfo*> prevThread(mActiveThread, threadInfo);

			retStr += "\n";

			for (int pass = 0; pass < 2; pass++)
			{
				CPURegisters registers;
				PopulateRegisters(&registers);

				String locString = EncodeDataPtr((addr_target)registers.GetPC(), true);

				TryGetThreadName(threadInfo);

				bool hadThreadName = true;
				String threadName = threadInfo->mName;
				if (threadName.IsEmpty())
				{
					hadThreadName = false;
					if (threadInfo->mThreadId == mProcessInfo.dwThreadId)
						threadName = "Main Thread";
					else
						threadName = "Worker Thread";
				}

				bool isInvalid = false;
				addr_target appendAddr = 0;

				for (int stackIdx = 0; true; stackIdx++)
				{
					auto subProgram = mDebugTarget->FindSubProgram(registers.GetPC(), DbgOnDemandKind_LocalOnly);
					if (subProgram != NULL)
					{
						if (subProgram->mLineInfo != NULL)
						{
							DbgModule* module = subProgram->mCompileUnit->mDbgModule;
							DbgModule* linkedModule = module->GetLinkedModule();
							if (linkedModule->mDisplayName.length() > 0)
							{
								locString = linkedModule->mDisplayName + "!" + subProgram->ToString();
								if (!hadThreadName)
									threadName = module->mDisplayName + " thread";
							}
							else
							{
								locString = subProgram->ToString();
							}
							appendAddr = 0;
							break;
						}
					}

					DbgModule* module = mDebugTarget->FindDbgModuleForAddress(registers.GetPC());
					if (module == NULL)
					{
						isInvalid = true;
						break;
					}

					DbgModule* linkedModule = module->GetLinkedModule();
					appendAddr = (addr_target)registers.GetPC();
					locString = linkedModule->mDisplayName + "!" + EncodeDataPtr((addr_target)registers.GetPC(), true);
					if (!hadThreadName)
						threadName = linkedModule->mDisplayName + " thread";

					if ((mActiveThread == mExplicitStopThread) && (mActiveBreakpoint != NULL))
					{
						if ((subProgram == NULL) ||
							(mActiveBreakpoint->mAddr < subProgram->mBlock.mLowPC) ||
							(mActiveBreakpoint->mAddr >= subProgram->mBlock.mHighPC))
							break;
					}

					if (pass == 1) // Just take the first item
						break;

					if (stackIdx == 128)
						break; // Too many!

					addr_target returnAddr;
					if (!mDebugTarget->RollBackStackFrame(&registers, &returnAddr, true))
					{
						isInvalid = true;
						break;
					}
				}

				if ((isInvalid) && (pass == 0))
					continue;

				if (appendAddr != 0)
				{
					String symbolName;
					addr_target offset;
					DbgModule* dwarf;
					if (mDebugTarget->FindSymbolAt(appendAddr, &symbolName, &offset, &dwarf))
					{
						DbgModule* linkedModule = dwarf->GetLinkedModule();
						String demangledName = BfDemangler::Demangle(symbolName, DbgLanguage_Unknown);
						if (!linkedModule->mDisplayName.empty())
						{
							demangledName = linkedModule->mDisplayName + "!" + demangledName;
						}
						locString = demangledName + StrFormat("+0x%X", offset);
					}
				}

				retStr += StrFormat("%d\t%s\t%s", threadInfo->mThreadId, threadName.c_str(), locString.c_str());

				String attrs;
				if (threadInfo->mFrozen)
				{
					attrs += "Fr";
				}
				if (!attrs.IsEmpty())
				{
					retStr += "\t";
					retStr += attrs;
				}

				break;
			}
		}
	}
	return retStr;
}

void WinDebugger::SetActiveThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread != NULL) && (mActiveThread->mThreadId == threadId))
		return;

	auto prevThread = mActiveThread;

	if (mThreadMap.TryGetValue(threadId, &mActiveThread))
	{
		BfLogDbg("SetActiveThread %d\n", threadId);

		if (prevThread != NULL)
		{
			Array<WdStackFrame*>* prevFrameArray = NULL;
			mSavedCallStacks.TryAdd(prevThread, NULL, &prevFrameArray);
			for (auto frameInfo : *prevFrameArray)
				delete frameInfo;
			*prevFrameArray = mCallStack;
			mCallStack.Clear();
		}

		DoClearCallStack(false);

		Array<WdStackFrame*>* newFrameArray = NULL;
		if (mSavedCallStacks.TryGetValue(mActiveThread, &newFrameArray))
		{
			mCallStack = *newFrameArray;
			newFrameArray->Clear();
		}
	}
	else
	{
		BfLogDbg("SetActiveThread %d FAILED\n", threadId);
	}
}

int WinDebugger::GetActiveThread()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mActiveThread == NULL)
		return -1;
	return mActiveThread->mThreadId;
}

void WinDebugger::FreezeThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(!IsInRunState());
	auto thread = mThreadMap[threadId];
	if (!thread->mFrozen)
	{
		thread->mFrozen = true;
		::SuspendThread(thread->mHThread);
		BfLogDbg("SuspendThread %d from FreezeThread\n", thread->mThreadId);
	}
}

void WinDebugger::ThawThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(!IsInRunState());
	auto thread = mThreadMap[threadId];
	if (thread->mFrozen)
	{
		thread->mFrozen = false;
		::ResumeThread(thread->mHThread);
		BfLogDbg("ResumeThread %d from ThawThread\n", thread->mThreadId);
	}
}

bool WinDebugger::IsActiveThreadWaiting()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	return mActiveThread == mDebuggerWaitingThread;
}

void WinDebugger::DoClearCallStack(bool clearSavedStacks)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("ClearCallstack\n");
	BF_ASSERT(mRunState != RunState_DebugEval);

	for (auto wdStackFrame : mCallStack)
		delete wdStackFrame;

	if (clearSavedStacks)
	{
		for (auto& kv : mSavedCallStacks)
		{
			for (auto wdStackFrame : kv.mValue)
				delete wdStackFrame;
		}
		mSavedCallStacks.Clear();
	}

	mCallStack.Clear();
	mIsPartialCallStack = true;
}

void WinDebugger::ClearCallStack()
{
	DoClearCallStack(true);
}

void WinDebugger::UpdateCallStack(bool slowEarlyOut)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mIsPartialCallStack)
		return;

	if (mActiveThread == NULL)
		return;

	if (IsInRunState())
		return;

	uint32 tickStart = BFTickCount();

	CPURegisters registers;

	if (mCallStack.size() > 0)
	{
		WdStackFrame* wdStackFrame = mCallStack.back();
		if (wdStackFrame->mIsEnd)
		{
			return;
		}
		memcpy(&registers, &wdStackFrame->mRegisters, sizeof(registers));
		bool regsRolledBack = RollBackStackFrame(&registers, mCallStack.size() == 1);
		// If we can't roll them back then mIsEnd should have been set for the previous frame
		BF_ASSERT(regsRolledBack);
	}
	else
	{
		BF_ASSERT(mIsPartialCallStack);
		mCallStack.Reserve(1024);
		PopulateRegisters(&registers);
		BfLogDbg("UpdateCallStack starting. Thread=%d PC=0x%p\n", mActiveThread->mThreadId, registers.GetPC());
	}

	bool isPartial = false;

	// Incrementally fill callstack structure to avoid stepping slowdown during deep nesting
	for (int fillIdx = 0; fillIdx < (slowEarlyOut ? 10000 : 100000); fillIdx++)
	{
		WdStackFrame* wdStackFrame = new WdStackFrame();
		memcpy(&wdStackFrame->mRegisters, &registers, sizeof(registers));
		wdStackFrame->mIsStart = mCallStack.size() == 0;
		wdStackFrame->mIsEnd = false;

		bool rollbackSuccess = false;
		for (int tryCount = 0; tryCount < 16; tryCount++)
		{
			if (!RollBackStackFrame(&registers, wdStackFrame->mIsStart))
			{
				break;
			}
			if (registers.GetPC() > 0xFFFF)
			{
				rollbackSuccess = true;
				break;
			}

			if (mCallStack.size() > 0)
				break; // Only retry for the first frame
		}

		if (!rollbackSuccess)
			wdStackFrame->mIsEnd = true;

		if (registers.GetSP() <= wdStackFrame->mRegisters.GetSP())
		{
			// SP went the wrong direction, stop rolling back
			wdStackFrame->mIsEnd = true;
		}

		mCallStack.push_back(wdStackFrame);

		if (IsMiniDumpDebugger())
		{
			// Make sure to queue up any debug stuff we need
			UpdateCallStackMethod((int)mCallStack.size() - 1);
		}

		if (wdStackFrame->mIsEnd)
			break;

		// Time-limit callstack generation.  Most useful for debug mode.
		if ((slowEarlyOut) && ((fillIdx % 100) == 0))
		{
			uint32 tickEnd = BFTickCount();
			if (tickEnd - tickStart >= 10)
			{
				isPartial = true;
				break;
			}
		}
	}

	if (!isPartial)
		mIsPartialCallStack = false;
}

int WinDebugger::GetCallStackCount()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	return (int)mCallStack.size();
}

int WinDebugger::GetRequestedStackFrameIdx()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread == mExplicitStopThread) && (mRequestedStackFrameIdx >= -1))
	{
		if (mActiveBreakpoint != NULL)
			mRequestedStackFrameIdx = GetBreakStackFrameIdx();
		if (mRequestedStackFrameIdx == -1)
			mRequestedStackFrameIdx = 0;
		return mRequestedStackFrameIdx;
	}

	int newCallStackIdx = 0;
	while (true)
	{
		if (newCallStackIdx >= (int)mCallStack.size() - 1)
			UpdateCallStack();
		if (newCallStackIdx >= (int)mCallStack.size() - 1)
			break;

		intptr addr;
		String file;
		int hotIdx;
		int defLineStart;
		int defLineEnd;
		int line;
		int column;
		int language;
		int stackSize;
		int8 flags;
		GetStackFrameInfo(newCallStackIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
		if (!file.empty())
			return newCallStackIdx;
		newCallStackIdx++;
	}

	return 0;
}

int WinDebugger::GetBreakStackFrameIdx()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveBreakpoint == NULL) || (mRunState != RunState_Breakpoint))
		return -1;
	if ((mBreakStackFrameIdx != -1) || (mActiveThread != mExplicitStopThread))
		return mBreakStackFrameIdx;

	mBreakStackFrameIdx = 0;
	BF_ASSERT(mActiveBreakpoint != NULL);
	if (mCallStack.IsEmpty())
		UpdateCallStack();
	if (!mCallStack.IsEmpty())
	{
		UpdateCallStackMethod(0);

		for (int stackIdx = 0; stackIdx < (int)mCallStack.size(); stackIdx++)
		{
			auto callStackEntry = mCallStack[stackIdx];
			if (callStackEntry->mSubProgram == NULL)
				break;
			if ((mActiveBreakpoint->mAddr < callStackEntry->mSubProgram->mBlock.mLowPC) ||
				(mActiveBreakpoint->mAddr >= callStackEntry->mSubProgram->mBlock.mHighPC))
				break;

			DbgSubprogram* specificSubprogram = callStackEntry->mSubProgram;
			auto dwLineData = callStackEntry->mSubProgram->FindClosestLine(mActiveBreakpoint->mAddr, &specificSubprogram);
			if (dwLineData == NULL)
				break;

			if (mActiveBreakpoint->mLineData == dwLineData)
			{
				mBreakStackFrameIdx = stackIdx;
				break;
			}
		}
	}
	return mBreakStackFrameIdx;
}

static const char* SafeString(const char* str)
{
	if (str == NULL)
		return "???";
	return str;
}

void WinDebugger::UpdateRegisterUsage(int stackFrameIdx)
{
	WdStackFrame* wdStackFrame = mCallStack[stackFrameIdx];
	if (wdStackFrame->mRegForms.size() != 0)
		return;

	auto dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram == NULL)
		return;

	addr_target addr = dwSubprogram->mBlock.mLowPC;

	const uint8* baseOp = nullptr;
	while (addr < dwSubprogram->mBlock.mHighPC)
	{
		CPUInst inst;
		if (!mDebugTarget->DecodeInstruction(addr, &inst))
			break;

		bool overrideForm = inst.mAddress <= (addr_target)wdStackFrame->mRegisters.GetPC();
		inst.MarkRegsUsed(wdStackFrame->mRegForms, overrideForm);

		addr += inst.GetLength();
	}
}

// It's safe to pass an invalid idx in here
void WinDebugger::GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	*outThreadId = 0;
	if (outStackIdx != NULL)
		*outStackIdx = -1;

	if (!IsPaused())
		return;

	for (auto thread : mThreadList)
	{
		NT_TIB64 tib = { 0 };
		if (!ReadMemory((intptr)thread->mThreadLocalBase, sizeof(tib), &tib))
			continue;

		MEMORY_BASIC_INFORMATION stackInfo = { 0 };
		if (VirtualQueryEx(mProcessInfo.hProcess, (void*)(tib.StackBase - 1), &stackInfo, sizeof(MEMORY_BASIC_INFORMATION)) == 0)
			continue;

		if ((addr >= (intptr)stackInfo.AllocationBase) && (addr < (intptr)tib.StackBase))
		{
			*outThreadId = thread->mThreadId;

			if (outStackIdx == NULL)
				return;

			if (mActiveThread == thread)
			{
				UpdateCallStack(false);
				for (int callStackIdx = 0; callStackIdx < (int)mCallStack.size(); callStackIdx++)
				{
					UpdateCallStackMethod(callStackIdx);

					auto stackFrame = mCallStack[callStackIdx];
					if (addr >= (intptr)stackFrame->mRegisters.GetSP())
					{
						*outStackIdx = callStackIdx;
					}
				}
			}
			return;
		}
	}
}

String WinDebugger::GetStackFrameId(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	intptr addr = 0;
	if (wdStackFrame->mSubProgram != NULL)
		addr = wdStackFrame->mSubProgram->mBlock.mLowPC;
	else
		addr = wdStackFrame->mRegisters.GetPC();

	String str = StrFormat("Thread:%d SP:%llX Func:%llX", mActiveThread->mThreadId, wdStackFrame->mRegisters.GetSP(), addr);
	return str;
}

String WinDebugger::Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	DbgModule* dbgModule = NULL;
	DbgSrcFile* dbgSrcFile = NULL;

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* nextStackFrame = mCallStack[actualStackFrameIdx - 1];
		auto subProgram = nextStackFrame->mSubProgram;
		dbgModule = subProgram->mCompileUnit->mDbgModule;

		DbgModule::FixupLineDataForSubprogram(subProgram->mInlineeInfo->mRootInliner);
		DbgSubprogram* parentSubprogram = subProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
		auto foundLine = parentSubprogram->FindClosestLine(subProgram->mBlock.mLowPC, &parentSubprogram);
		if (foundLine != NULL)
			dbgSrcFile = parentSubprogram->GetLineSrcFile(*foundLine);

		DbgSubprogram* callingSubProgram = NULL;
		DbgLineData* callingLineData = FindLineDataAtAddress(nextStackFrame->mSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
		if ((callingLineData != NULL) && (callingSubProgram == wdStackFrame->mSubProgram))
			dbgSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
	}
	else
	{
		DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
		if (dwSubprogram != NULL)
		{
			DbgModule::FixupLineDataForSubprogram(dwSubprogram);
			addr_target findAddress = wdStackFrame->GetSourcePC();

			DbgSubprogram* dbgSubprogram = NULL;
			DbgLineData* dwLineData = dwSubprogram->FindClosestLine(findAddress, &dbgSubprogram, &dbgSrcFile);
			dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		}
	}

	if (dbgSrcFile != NULL)
	{
		// Note: we must use mFilePath here, make sure we don't use GetLocalPath()
		return dbgModule->GetOldSourceCommand(dbgSrcFile->mFilePath);
	}
	return "";
}

int WinDebugger::GetJmpState(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return -1;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();

	CPUInst inst;
	if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
		return -1;

	return inst.GetJmpState(wdStackFrame->mRegisters.mIntRegs.efl);
}

intptr WinDebugger::GetStackFrameCalleeAddr(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return -1;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();
	if (stackFrameIdx == -1)
		pcAddress = mShowPCOverride;

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* inlineStackFrame = mCallStack[actualStackFrameIdx - 1];
		return inlineStackFrame->mSubProgram->mBlock.mLowPC - 1;
	}

	return pcAddress - 1;
}

String WinDebugger::GetStackMethodOwner(int stackFrameIdx, int& language)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	if (actualStackFrameIdx >= (int)mCallStack.size())
		actualStackFrameIdx = 0;
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	if (wdStackFrame->mSubProgram == NULL)
		return "";
	auto parentType = wdStackFrame->mSubProgram->GetParent();
	if (parentType == NULL)
		return "";
	parentType = parentType->GetPrimaryType();
	language = (int)parentType->GetLanguage();
	return parentType->ToString();
}

intptr WinDebugger::GetSymbolAddress(const StringImpl& sym)
{
	return mDebugTarget->FindSymbolAddr(sym);
}

String WinDebugger::DisassembleAtRaw(intptr inAddress)
{
	addr_target address = (addr_target)inAddress;

	const int addrBorder = 1024;

	for (int offset = 0; offset < 8; offset++)
	{
		String result;
		bool addOffset = true;
		bool hadAddr = false;

		DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress(address);
		DbgModuleMemoryCache* memCache = NULL;
		defer
		(
			if (dbgModule == NULL)
				delete memCache;
		);

		if ((dbgModule != NULL) && (dbgModule->mOrigImageData == NULL))
			dbgModule = NULL;

		result += "R\n"; // Raw

		addr_target addrStart = address;
		if (dbgModule != NULL)
		{
			dbgModule->ParseSymbolData();
			memCache = dbgModule->mOrigImageData;
			addrStart = BF_MAX((addr_target)dbgModule->mImageBase, address - addrBorder - offset);
		}
		else
		{
			memCache = new DbgModuleMemoryCache(addrStart & (4096 - 1), 4096 * 2);
		}

		if (memCache->mAddr == 0)
			return "";

		//addr_target imageBase = dbgModule->mImageBase;
		//int imageSize = dbgModule->mImageSize;

		addr_target dataAddr = addrStart;
		addr_target addrEnd = addrStart + addrBorder * 2 + 16;

		while (dataAddr < addrEnd)
		{
			if (dataAddr == address)
				hadAddr = true;
			if (dataAddr > address)
			{
				if (!hadAddr)
				{
					if (offset == 7)
					{
						dataAddr = address;
					}
					break;
				}
			}

			String outSymbol;
			addr_target symOffset = 0;
			DbgModule* symDWARF;

			if (mDebugTarget->FindSymbolAt(dataAddr, &outSymbol, &symOffset, &symDWARF))
			{
				if (symOffset == 0)
				{
					outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);
					if ((symDWARF != NULL) && (!symDWARF->mDisplayName.empty()))
						outSymbol = symDWARF->GetLinkedModule()->mDisplayName + "!" + outSymbol;
					result += "T " + outSymbol + ":\n";
				}
			}

			CPUInst inst;
			if (!mCPU->Decode(dataAddr, memCache, &inst))
			{
				if ((offset == 7) && (!hadAddr))
				{
					uint8 instData[1];
					memCache->Read(dataAddr, instData, 1);
					int instLen = 1;

#ifdef BF_DBG_32
					result += StrFormat("D %08X:        ", dataAddr);
#else
					result += StrFormat("D %@:        ", dataAddr);
#endif
					for (int i = 0; i < instLen; i++)
						result += StrFormat("%02X ", instData[i]);
					for (int i = instLen; i < 8; i++)
						result += "   ";
					result += "\n";
					dataAddr++;

					continue;
				}
				break;
			}

			int instLen = inst.GetLength();

#ifdef BF_DBG_32
			result += StrFormat("D %08X:        ", dataAddr);
#else
			result += StrFormat("D %@:        ", dataAddr);
#endif

			uint8 instData[32];
			int showInstLen = BF_MIN(32, instLen);
			memCache->Read(dataAddr, instData, showInstLen);

			for (int i = 0; i < showInstLen; i++)
				result += StrFormat("%02X ", instData[i]);
			for (int i = instLen; i < 8; i++)
				result += "   ";

			result += mCPU->InstructionToString(&inst, dataAddr);

			if ((inst.IsCall()) || (inst.IsBranch()))
			{
				addr_target targetAddr = inst.GetTarget();
				if (targetAddr != 0)
				{
					if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &symOffset))
					{
						if (symOffset < 0x10000)
						{
							outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);

							result += " ; " + outSymbol;
							if (symOffset > 0)
								result += StrFormat("+%x", symOffset);
							//result += ">";
						}
					}
				}
			}

			result += "\n";
			dataAddr += instLen;
		}

		if (!hadAddr)
			continue;

		return result;
	}

	return "";
}

String WinDebugger::DisassembleAt(intptr inAddress)
{
	BP_ZONE("WinDebugger::DisassembleAt");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	addr_target address = (addr_target)inAddress;

	if (mDebugTarget == NULL)
		return "";

	String result;
	auto dwSubProgram = mDebugTarget->FindSubProgram(address);
	if (dwSubProgram == NULL)
		return DisassembleAtRaw(address);
	dwSubProgram = dwSubProgram->GetRootInlineParent();
	DbgModule* dwarf = dwSubProgram->mCompileUnit->mDbgModule;

	int frameBaseRegister = mDebugTarget->GetFrameBaseRegister(dwSubProgram);

	addr_target addrStart = dwSubProgram->mBlock.mLowPC;
	addr_target addrEnd = dwSubProgram->mBlock.mHighPC;

	auto dwCompileUnit = dwSubProgram->mCompileUnit;
	{
		FixupLineData(dwCompileUnit);
	}

	DbgSrcFile* dwSrcFile = NULL;

	DbgModule::FixupLineDataForSubprogram(dwSubProgram);

	DbgLineData* dwLineData = NULL;
	if (dwSubProgram->mLineInfo != NULL)
    	dwLineData = &dwSubProgram->mLineInfo->mLines[0];
	int nextLineDataIdx = 1;

	if (dwSubProgram->mIsOptimized)
		result += "O\n";

	DbgSrcFile* srcFile = NULL;
	int firstLine = 0;
	int curLine = 0;
	if (dwLineData != NULL)
	{
		srcFile = dwSubProgram->GetLineSrcFile(*dwLineData);
		result += "S " + srcFile->GetLocalPath() + "\n";
		if (srcFile->mHashKind != DbgHashKind_None)
		{
			result += "H ";
			srcFile->GetHash(result);
			result += "\n";
		}

		curLine = BF_MAX(0, dwLineData->mLine - 5);
		//for (; curLine <= dwLineData->mLine; curLine++)
		result += StrFormat("L %d %d\n", curLine, dwLineData->mLine - curLine + 1);
		curLine = dwLineData->mLine + 1;
		firstLine = dwLineData->mLine;
	}

	Array<DbgSubprogram*> inlineStack;

	Array<DbgBlock*> blockList;
	blockList.push_back(&dwSubProgram->mBlock);

	addr_target dataAddr = addrStart;
	int decodeFailureCount = 0;

	auto& _PopInlineStack = [&]()
	{
		int depth = inlineStack.size();
		auto curStackEntry = inlineStack.back();
		if (depth > 1)
			result += StrFormat("T <<<%d Inline End ", depth);
		else
			result += "T <<< Inline End ";
		result += curStackEntry->ToString();
		result += "\n";
		inlineStack.pop_back();
	};

	std::function<void(DbgSubprogram* subprogram, int depth)> _UpdateInlineStackHelper = [&](DbgSubprogram* subprogram, int depth)
	{
		int stackIdx = depth - 1;
		if (stackIdx < inlineStack.size())
		{
			auto curStackEntry = inlineStack[stackIdx];
			if (curStackEntry != subprogram)
				_PopInlineStack();
		}

		if (depth > 1)
		{
			_UpdateInlineStackHelper(subprogram->mInlineeInfo->mInlineParent, depth - 1);
		}

		if (stackIdx >= inlineStack.size())
		{
			if (depth > 1)
				result += StrFormat("T >>>%d Inline ", depth);
			else
				result += "T >>> Inline ";
			result += subprogram->ToString();
			result += "\n";
			inlineStack.push_back(subprogram);
		}
	};

	auto _UpdateInlineStack = [&](DbgSubprogram* subprogram)
	{
		if (subprogram == NULL)
		{
			while (!inlineStack.IsEmpty())
				_PopInlineStack();
			return;
		}

		int inlineDepth = subprogram->GetInlineDepth();
		while (inlineDepth < inlineStack.size())
			_PopInlineStack();
		if (inlineDepth > 0)
			_UpdateInlineStackHelper(subprogram, inlineDepth);
	};

	while (dataAddr < addrEnd)
	{
		// Pop off old scopes
		while (blockList.size() > 0)
		{
			auto lastBlock = blockList.back();
			if (dataAddr < lastBlock->mHighPC)
				break;
			blockList.pop_back();
		}

		// Check entry into new child scopes
		auto lastBlock = blockList.back();
		for (auto checkBlock : lastBlock->mSubBlocks)
		{
			if ((dataAddr >= checkBlock->mLowPC) && (dataAddr < checkBlock->mHighPC))
			{
				blockList.push_back(checkBlock);
				break;
			}
		}

		bool allowSourceJump = false;

		if ((dwLineData != NULL) && (dwLineData->mContribSize != 0) && (dataAddr >= dwSubProgram->GetLineAddr(*dwLineData) + dwLineData->mContribSize))
		{
			DbgSubprogram* inlinedSubprogram = NULL;
			auto inlinedLine = dwSubProgram->FindClosestLine(dataAddr, &inlinedSubprogram);

			_UpdateInlineStack(dwSubProgram);
		}

		// Update line data
		while ((dwLineData != NULL) && (dwSubProgram->GetLineAddr(*dwLineData) <= dataAddr))
		{
			_UpdateInlineStack(dwSubProgram->GetLineInlinee(*dwLineData));

			const int lineLimit = 5; // 15

			if (allowSourceJump)
				curLine = dwLineData->mLine;

			auto lineSrcFile = dwSubProgram->GetLineSrcFile(*dwLineData);

			if (lineSrcFile != srcFile)
			{
				srcFile = lineSrcFile;
				result += "S ";
				result += srcFile->GetLocalPath();
				result += "\n";
				// Just show the one line from the new file
				curLine = dwLineData->mLine;
			}

			if (dwLineData->mLine < curLine - 1)
			{
				// Jumping backwards - possibly into inlined method, or possibly in current method.
				//  Show previous 6 lines, for context
				curLine = BF_MAX(0, dwLineData->mLine - lineLimit);
			}

			if ((curLine <= firstLine) && (dwLineData->mLine >= firstLine))
			{
				// Jumping from inlined method (declared above) back into main method
				curLine = dwLineData->mLine;
			}

			if (curLine < dwLineData->mLine - lineLimit)
			{
				// Don't show huge span of source - only show the last 6 lines at maximum
				curLine = dwLineData->mLine - lineLimit;
			}

			//for ( ; curLine <= dwLineData->mLine; curLine++)
			result += StrFormat("L %d %d\n", curLine, dwLineData->mLine - curLine + 1);
			curLine = dwLineData->mLine + 1;

			DbgLineData* nextLineData = NULL;
			while (nextLineDataIdx < dwSubProgram->mLineInfo->mLines.mSize)
			{
				nextLineData = &dwSubProgram->mLineInfo->mLines[nextLineDataIdx];

				//TODO:
				/*{
					result += StrFormat("T LineIdx: %d (%@ to %@)", nextLineDataIdx, dwSubProgram->GetLineAddr(*nextLineData), dwSubProgram->GetLineAddr(*nextLineData) + nextLineData->mContribSize);
					auto inlinee = dwSubProgram->GetLineInlinee(*nextLineData);
					if (inlinee != NULL)
					{
						result += StrFormat(" Inlinee: %s Depth: %d", inlinee->mName, inlinee->GetInlineDepth());
					}
					result += "\n";
				}*/

				auto nextLineAddr = dwSubProgram->GetLineAddr(*nextLineData);
				if (nextLineAddr > dataAddr)
				{
					if (nextLineDataIdx + 1 < dwSubProgram->mLineInfo->mLines.mSize)
					{
						auto peekLineData = &dwSubProgram->mLineInfo->mLines[nextLineDataIdx + 1];
						if (peekLineData->mRelAddress == nextLineData->mRelAddress)
						{
							// Use the later entry
							++nextLineDataIdx;
							continue;
						}
					}

					break;
				}
				// If we go back to an older entry beacuse of a gap then we need to catch back up...
				++nextLineDataIdx;
				nextLineData = NULL; // Keep searching...
			}

			dwLineData = nextLineData;
			nextLineDataIdx++;
		}

		// Have we gone off the end of the inline function?
		//  We may not have an explicit non-inlined line data at the transition point...
		while (!inlineStack.IsEmpty())
		{
			auto subProgram = inlineStack.back();
			if (dataAddr < subProgram->mBlock.mHighPC)
				break;
			_PopInlineStack();
		}

		bool hadDecodeFailure = false;
		CPUInst inst;
		if (!mCPU->Decode(dataAddr, dwarf->mOrigImageData, &inst))
			hadDecodeFailure = true;

		if ((decodeFailureCount == 8) || ((decodeFailureCount > 0) && (!hadDecodeFailure)))
		{
			for (int i = decodeFailureCount; i < 4 + sizeof(addr_target); i++)
				result += "   ";
			result += " ???\n";
			decodeFailureCount = 0;
		}

		if (decodeFailureCount == 0)
		{
#ifdef BF_DBG_32
			result += StrFormat("D %08X:  ", dataAddr);
#else
			result += StrFormat("D %@:  ", dataAddr);
#endif
		}
		if (hadDecodeFailure)
		{
			uint8 byte = 0;
			dwarf->mOrigImageData->Read(dataAddr, &byte, 1);
			result += StrFormat("%02X ", byte);
			dataAddr++;
			decodeFailureCount++;
			continue;
		}

		int instLen = inst.GetLength();
		uint8 instData[32];
		int showInstLen = BF_MIN(32, instLen);
		dwarf->mOrigImageData->Read(dataAddr, instData, showInstLen);

		for (int i = 0; i < showInstLen; i++)
			result += StrFormat("%02X ", instData[i]);

		for (int i = instLen; i < 4 + sizeof(addr_target); i++)
			result += "   ";

		result += " ";

		result += mCPU->InstructionToString(&inst, dataAddr);

		int reg;
		int offset;
		if (inst.GetIndexRegisterAndOffset(&reg, &offset))
		{
			for (int blockIdx = (int)blockList.size() - 1; blockIdx >= 0; blockIdx--)
			{
				auto dwBlock = blockList[blockIdx];
				for (auto variable : dwBlock->mVariables)
				{
					int varRegister;
					int varOffset;
					if (mDebugTarget->GetVariableIndexRegisterAndOffset(variable, &varRegister, &varOffset))
					{
						if (varRegister == -1)
							varRegister = frameBaseRegister;
						if ((reg == varRegister) && (offset == varOffset))
						{
							result += " ; ";
							result += variable->mName;
							break;
						}
					}
				}
			}
		}
		else if ((inst.IsCall()) || (inst.IsBranch()) || (inst.IsLoadAddress()))
		{
			addr_target targetAddr = inst.GetTarget();
			if (targetAddr != 0)
			{
				if ((targetAddr >= addrStart) && (targetAddr < addrEnd))
				{
					result += StrFormat("\nJ %s", EncodeDataPtr(targetAddr, false).c_str());
				}
				else
				{
					String outSymbol;
					addr_target offset = 0;
					if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &offset))
					{
						if (offset < 0x10000)
						{
							outSymbol = BfDemangler::Demangle(outSymbol, dwSubProgram->GetLanguage());

							result += " ; " + outSymbol;
							if (offset > 0)
								result += StrFormat("+%x", offset);
						}
					}
				}
			}
		}

		result += "\n";
		dataAddr += instLen;
	}

	// Why did we want to "show lines at end"??
	// Show lines at end
	/*if (curLine > 0)
	{
		for (int i = 0; i < 6; i++, curLine++)
			result += StrFormat("L %d\n", curLine);
	}*/
	return result;
}

String WinDebugger::GetCurrentException()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String result = StrFormat("%s\n%08X",
		EncodeDataPtr((addr_target)(intptr)mCurException.ExceptionAddress, true).c_str(),
		mCurException.ExceptionCode);

	String exStr;
	switch (mCurException.ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	{
		String accessType;
		if (mCurException.ExceptionInformation[0] == 0)
			accessType = "reading from";
		else if (mCurException.ExceptionInformation[0] == 8)
			accessType = "executing";
		else
			accessType = "writing to";
		exStr = StrFormat("EXCEPTION_ACCESS_VIOLATION %s %s", accessType.c_str(), EncodeDataPtr((addr_target)mCurException.ExceptionInformation[1], true).c_str());
	}
	break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		exStr = "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_SINGLE_STEP:
		exStr = "EXCEPTION_SINGLE_STEP";
		break;
	case EXCEPTION_BREAKPOINT:
		exStr = "EXCEPTION_BREAKPOINT";
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		exStr = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		exStr = "EXCEPTION_FLT_DENORMAL_OPERAND";
		break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		exStr = "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_FLT_INEXACT_RESULT:
		exStr = "EXCEPTION_FLT_INEXACT_RESULT";
		break;
	case EXCEPTION_FLT_INVALID_OPERATION:
		exStr = "EXCEPTION_FLT_INVALID_OPERATIO";
		break;
	case EXCEPTION_FLT_OVERFLOW:
		exStr = "EXCEPTION_FLT_OVERFLOW";
		break;
	case EXCEPTION_FLT_STACK_CHECK:
		exStr = "EXCEPTION_FLT_STACK_CHECK";
		break;
	case EXCEPTION_FLT_UNDERFLOW:
		exStr = "EXCEPTION_FLT_UNDERFLOW";
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		exStr = "EXCEPTION_INT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_INT_OVERFLOW:
		exStr = "EXCEPTION_INT_OVERFLOW";
		break;
	case EXCEPTION_PRIV_INSTRUCTION:
		exStr = "EXCEPTION_PRIV_INSTRUCTION";
		break;
	case EXCEPTION_IN_PAGE_ERROR:
		exStr = "EXCEPTION_IN_PAGE_ERROR";
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		exStr = "EXCEPTION_ILLEGAL_INSTRUCTION";
		break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		exStr = "EXCEPTION_NONCONTINUABLE_EXCEPTION";
		break;
	case EXCEPTION_STACK_OVERFLOW:
		exStr = "EXCEPTION_STACK_OVERFLOW";
		break;
	case EXCEPTION_INVALID_DISPOSITION:
		exStr = "EXCEPTION_INVALID_DISPOSITION";
		break;
	case EXCEPTION_GUARD_PAGE:
		exStr = "EXCEPTION_GUARD_PAGE";
		break;
	case EXCEPTION_INVALID_HANDLE:
		exStr = "EXCEPTION_INVALID_HANDLE";
		break;
	case CONTROL_C_EXIT:
		exStr = "CONTROL_C_EXIT";
		break;
	default:
		exStr += StrFormat("EXCEPTION %08X", mCurException.ExceptionCode);
	}
	if (mActiveThread != NULL)
		exStr += StrFormat(" in thread %d", mActiveThread->mThreadId);

	if (!exStr.empty())
		result += "\n" + exStr;

	// After we retrieve the exception then we can go back to just being normal 'paused'
	//  This allows us to evaluate stuff, Set Next Statement, etc.
	mRunState = RunState_Paused;

	return result.c_str();
}

void WinDebugger::SetAliasPath(const StringImpl& origPath, const StringImpl& localPath)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String fixedOrigPath = FixPathAndCase(origPath);
	String fixedLocalPath = FixPathAndCase(localPath);

	auto origFile = mDebugTarget->AddSrcFile(origPath);
	origFile->mLocalPath = FixPath(localPath);

	mDebugTarget->mLocalToOrigSrcMap[fixedLocalPath] = fixedOrigPath;
	// We invalidate the step filters, because previously-failing 'CheckSourceFileExist' checks may now succeed
	mDebugManager->mStepFilterVersion++;
}

String WinDebugger::GetModulesInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String str;

	for (auto module : mDebugTarget->mDbgModules)
	{
		COFF* coff = (COFF*)module;
		if (module->mHotIdx > 0)
			continue;

		str += module->mDisplayName;
		str += "\t";
		if (module->mLoadState == DbgModuleLoadState_Loaded)
		{
			str += module->mFilePath;
		}
		else if (module->mLoadState == DbgModuleLoadState_NotLoaded)
		{
			str += module->mFilePath;
			str += " (Loading...)";
		}
		else if (module->mLoadState == DbgModuleLoadState_Failed)
		{
			str += "!";
			str += module->mFilePath;
		}

		if (module->mMappedImageFile != NULL)
		{
			str += " (";
			str += module->mMappedImageFile->mFileName;
			str += ")";
		}

		str += "\t";
		str += coff->mPDBPath;
		str += "\t";
		str += module->mVersion;
		str += StrFormat("\t%@-%@\t%dk\t", module->mImageBase, module->mImageBase + module->mImageSize, module->mImageSize / 1024);

		time_t timestamp = coff->mTimeStamp;
		if (timestamp == 0)
			timestamp = GetFileTimeWrite(coff->mFilePath);
		if (timestamp != 0)
		{
			char timeString[256];
			auto time_info = localtime(&timestamp);
			strftime(timeString, sizeof(timeString), "%D %T", time_info);
			str += timeString;
		}
		str += "\n";
	}

	return str;
}

String WinDebugger::GetModuleInfo(const StringImpl& modulePath)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String result;

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (modulePath.Equals(dbgModule->mFilePath, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			dbgModule->ParseGlobalsData();
			dbgModule->PopulateStaticVariableMap();

			auto coff = (COFF*)dbgModule;
			coff->ParseCompileUnits();

			int fileSize = 0;
			//
			{
				FileStream fs;
				fs.Open(coff->mFilePath, "rb");
				fileSize = fs.GetSize();
			}

			result += StrFormat("Path: %s FileSize:%0.2fk MemoryImage:%0.2fk\n", coff->mFilePath.c_str(), fileSize / 1024.0f, coff->mImageSize / 1024.0f);

			result += "Sections:\n";
			for (auto& section : coff->mSections)
			{
				result += StrFormat("\t%s\t%0.2fk\n", section.mName.c_str(), (section.mAddrLength) / 1024.0f);
			}
			result += "\n";

			result += "Compile Units:\n";
			for (auto compileUnit : dbgModule->mCompileUnits)
			{
				coff->MapCompileUnitMethods(compileUnit);
				result += StrFormat("\t%s PCRange:%0.2fk\n", compileUnit->mName.c_str(), (compileUnit->mHighPC - compileUnit->mLowPC) / 1024.0f);
			}
			result += "\n";

			Array<CvModuleInfo*> moduleInfos;
			for (auto moduleInfo : coff->mCvModuleInfo)
			{
				if (moduleInfo->mSectionContrib.mSize > 0)
					moduleInfos.Add(moduleInfo);
			}
			moduleInfos.Sort([](CvModuleInfo* lhs, CvModuleInfo* rhs)
				{
					return lhs->mSectionContrib.mSize > rhs->mSectionContrib.mSize;
				});

			int totalContrib = 0;
			result += "CV Module Info:\n";
			for (auto moduleInfo : moduleInfos)
			{
				auto section = coff->mSections[moduleInfo->mSectionContrib.mSection - 1];

				result += StrFormat("\t%s\t%s\t%0.2fk\t%@-%@\n", moduleInfo->mModuleName, section.mName.c_str(), (moduleInfo->mSectionContrib.mSize) / 1024.0f,
					coff->GetSectionAddr(moduleInfo->mSectionContrib.mSection, moduleInfo->mSectionContrib.mOffset),
					coff->GetSectionAddr(moduleInfo->mSectionContrib.mSection, moduleInfo->mSectionContrib.mOffset + moduleInfo->mSectionContrib.mSize));
				totalContrib += moduleInfo->mSectionContrib.mSize;
			}
			result += StrFormat("\tTOTAL: %0.2fk\n", (totalContrib) / 1024.0f);
			result += "\n";

			addr_target minAddr = 0;
			Array<DbgCompileUnitContrib*> contribs;
			for (auto itr = mDebugTarget->mContribMap.begin(); itr != mDebugTarget->mContribMap.end(); ++itr)
			{
				auto contrib = *itr;
				if (contrib->mDbgModule != coff)
					continue;

				if (contrib->mAddress < minAddr)
					continue;

				minAddr = contrib->mAddress + contrib->mLength;

				auto section = &coff->mSectionHeaders[contrib->mSection - 1];
				if (section->mSizeOfRawData <= 0)
					continue;

				contribs.Add(contrib);
			}
			contribs.Sort([](DbgCompileUnitContrib* lhs, DbgCompileUnitContrib* rhs)
				{
					return lhs->mLength > rhs->mLength;
				});

			totalContrib = 0;
			result += "Contribs:\n";
			for (auto contrib : contribs)
			{
				auto cvModule = coff->mCvModuleInfo[contrib->mCompileUnitId];
				auto section = &coff->mSectionHeaders[contrib->mSection - 1];
				result += StrFormat("\t%s\t%s\t%0.2fk\t%@\n", cvModule->mModuleName, section->mName, (contrib->mLength)/1024.0f, contrib->mAddress);
				totalContrib += contrib->mLength;
			}
			result += StrFormat("\tTOTAL: %0.2fk\n", (totalContrib) / 1024.0f);
			result += "\n";

			struct SymbolEntry
			{
				const char* mName;
				addr_target mAddress;
				int mSize;
			};
			Array<SymbolEntry> symbolEntries;

			for (auto symbol : mDebugTarget->mSymbolMap)
			{
				if (symbol->mDbgModule != coff)
					continue;

				if (!symbolEntries.IsEmpty())
				{
					auto lastSymbol = &symbolEntries.back();
					if (lastSymbol->mSize == 0)
						lastSymbol->mSize = symbol->mAddress - lastSymbol->mAddress;
				}

				SymbolEntry symbolEntry;
				symbolEntry.mName = symbol->mName;
				symbolEntry.mAddress = symbol->mAddress;
				symbolEntry.mSize = 0;
				symbolEntries.Add(symbolEntry);
			}
			if (!symbolEntries.IsEmpty())
			{
				auto lastSymbol = &symbolEntries.back();
				for (auto contrib : contribs)
				{
					if ((lastSymbol->mAddress >= contrib->mAddress) && (lastSymbol->mAddress < contrib->mAddress + contrib->mLength))
					{
						lastSymbol->mSize = (contrib->mAddress + contrib->mLength) - lastSymbol->mAddress;
						break;
					}
				}
			}
			symbolEntries.Sort([](const SymbolEntry& lhs, const SymbolEntry& rhs)
				{
					return lhs.mSize > rhs.mSize;
				});

			totalContrib = 0;
			result += "Symbols:\n";
			for (auto symbolEntry : symbolEntries)
			{
				result += StrFormat("\t%s\t%0.2fk\t%@\n", symbolEntry.mName, (symbolEntry.mSize) / 1024.0f, symbolEntry.mAddress);
				totalContrib += symbolEntry.mSize;
			}
			result += StrFormat("\tTOTAL: %0.2fk\n", (totalContrib) / 1024.0f);
			result += "\n";

			//////////////////////////////////////////////////////////////////////////

			totalContrib = 0;
			result += "Static Variables:\n";
			for (auto& variable : coff->mStaticVariables)
			{
				result += StrFormat("\t%s\t%0.2fk\n", variable->mName, (variable->mType->GetByteCount()) / 1024.0f);
				totalContrib += variable->mType->GetByteCount();
			}
			result += StrFormat("\tTOTAL: %0.2fk\n", (totalContrib) / 1024.0f);
			result += "\n";

			totalContrib = 0;
			result += "Methods:\n";
			Array<DbgSubprogram*> methods;
			for (int typeIdx = 0; typeIdx < coff->mTypes.mSize; typeIdx++)
			{
				auto type = coff->mTypes[typeIdx];
				type->PopulateType();
				for (auto method : type->mMethodList)
					methods.Add(method);
			}
			for (auto compileUnit : dbgModule->mCompileUnits)
			{
				for (auto method : compileUnit->mOrphanMethods)
					methods.Add(method);
			}
			methods.Sort([](DbgSubprogram* lhs, DbgSubprogram* rhs)
				{
					return lhs->GetByteCount() > rhs->GetByteCount();
				});
			for (auto method : methods)
			{
				int methodSize = method->GetByteCount();
				if (methodSize <= 0)
					continue;
				auto name = method->ToString();
				result += StrFormat("\t%s\t%0.2fk\n", name.c_str(), methodSize / 1024.0f);
				totalContrib += methodSize;
			}

			result += StrFormat("\tTOTAL: %0.2fk\n", (totalContrib) / 1024.0f);
			result += "\n";
		}
	}

	return result;
}

void WinDebugger::CancelSymSrv()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
}

bool WinDebugger::HasPendingDebugLoads()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	return (!mPendingImageLoad.IsEmpty()) || (!mPendingDebugInfoLoad.IsEmpty());
}

int WinDebugger::LoadImageForModule(const StringImpl &modulePath, const StringImpl& imagePath)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (modulePath.Equals(dbgModule->mFilePath, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			auto coff = (COFF*)dbgModule;

			if (!coff->LoadModuleImage(imagePath))
			{
				mDebugManager->mOutMessages.push_back("error Failed to load image " + imagePath);
			}
			ModuleChanged(dbgModule);

			return 0;
		}
	}

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(DbgModule* dbgModule)
{
	if (!dbgModule->HasPendingDebugInfo())
		return 0;

	if (dbgModule->RequestDebugInfo())
	{
		ClearCallStack(); // Make this re-resolve with debug info
		return 1;
	}

	DbgPendingDebugInfoLoad* dbgPendingDebugInfoLoad = NULL;
	if (mPendingDebugInfoLoad.TryAdd(dbgModule, NULL, &dbgPendingDebugInfoLoad))
	{
		dbgPendingDebugInfoLoad->mModule = dbgModule;
		dbgPendingDebugInfoLoad->mAllowRemote = true;
		return 2;
	}
	dbgPendingDebugInfoLoad->mAllowRemote = true;

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(const StringImpl& moduleName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		String checkModuleName = GetFileName(dbgModule->mFilePath);
		if (moduleName.Equals(checkModuleName, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			return LoadDebugInfoForModule(dbgModule);
		}
	}

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(const StringImpl& modulePath, const StringImpl& debugFileName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (modulePath.Equals(dbgModule->mFilePath, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			auto coff = (COFF*)dbgModule;

			String err;
			if (!coff->mPDBLoaded)
			{
				dbgModule->mFailMsgPtr = &err;
				if (coff->TryLoadPDB(debugFileName, coff->mWantPDBGuid, coff->mWantAge))
				{
					ModuleChanged(dbgModule);
				}
				dbgModule->mFailMsgPtr = NULL;
			}
			else
			{
				err = StrFormat("Module '%s' already has debug information loaded", GetFileName(modulePath).c_str());
			}

			if (!err.IsEmpty())
			{
				mDebugManager->mOutMessages.push_back("error " + err);
			}

			return 0;
		}
	}

	return 0;
}

void WinDebugger::FixupLineData(DbgCompileUnit* compileUnit)
{
	if (!compileUnit || !compileUnit->mNeedsLineDataFixup)
		return;
	compileUnit->mNeedsLineDataFixup = false;
}

void WinDebugger::ReserveHotTargetMemory(int size)
{
	HotTargetMemory hotTargetMemory;
	hotTargetMemory.mOffset = 0;
	hotTargetMemory.mSize = 0;
	hotTargetMemory.mPtr = NULL;

	if (size > 0)
	{
		// In 64-bit mode we have a reserved region on program load that we commit here because the offsets
		//  must be within 32-bits of the original EXE image, but in 32-bit mode we don't reserve anything
		//  until here
#ifdef BF_DBG_32
	//hotTargetMemory.mSize = std::max(1024 * 1024, size);
		BF_ASSERT((size & (mPageSize - 1)) == 0);
		hotTargetMemory.mSize = size;
		hotTargetMemory.mPtr = (addr_target)(intptr)VirtualAllocEx(mProcessInfo.hProcess, NULL, hotTargetMemory.mSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		mDebugTarget->mHotHeap->AddTrackedRegion(hotTargetMemory.mPtr, hotTargetMemory.mSize);
#else
		hotTargetMemory.mSize = size;
		hotTargetMemory.mPtr = mDebugTarget->mHotHeap->Alloc(size);
		BF_ASSERT(hotTargetMemory.mPtr != 0);
		auto ptr = ::VirtualAllocEx(mProcessInfo.hProcess, (void*)(intptr)hotTargetMemory.mPtr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		BF_ASSERT(ptr == (void*)(intptr)hotTargetMemory.mPtr);
#endif
	}

	BfLogDbg("ReserveHotTargetMemory %p %d\n", hotTargetMemory.mPtr, hotTargetMemory.mSize);
	int err = GetLastError();
	mHotTargetMemory.push_back(hotTargetMemory);
}

addr_target WinDebugger::AllocHotTargetMemory(int size, bool canExecute, bool canWrite, int* outAllocSize)
{
	int prot = PAGE_READWRITE;
	if (canExecute && canWrite)
		prot = PAGE_EXECUTE_READWRITE;
	else if (canExecute)
		prot = PAGE_EXECUTE_READ;

	size = (size + (mPageSize - 1)) & ~(mPageSize - 1);
	*outAllocSize = size;

	HotTargetMemory* hotTargetMemory = NULL;
	bool foundHotTargetMemory = false;
	for (int i = mHotTargetMemory.mSize - 1; i >= BF_MAX(mHotTargetMemory.mSize - 32, 0); i--)
	{
		hotTargetMemory = &mHotTargetMemory[i];
		if (hotTargetMemory->mPtr == 0)
		{
			Fail("Failed to allocate memory for hot loading");
			return 0;
		}

		if (hotTargetMemory->GetSizeLeft() >= size)
		{
			foundHotTargetMemory = true;
			break;
		}
	}

	if (!foundHotTargetMemory)
	{
		ReserveHotTargetMemory(size);
		foundHotTargetMemory = true;
		hotTargetMemory = &mHotTargetMemory.back();
	}

	BF_ASSERT(hotTargetMemory->mOffset + size <= hotTargetMemory->mSize);
	addr_target result = hotTargetMemory->mPtr + hotTargetMemory->mOffset;

	::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)result, size, prot, NULL);

	BfLogDbg("AllocHotTargetMemory: %p %d %d %d\n", result, size, canExecute, canWrite);

	hotTargetMemory->mOffset += size;
	return result;
}

void WinDebugger::ReleaseHotTargetMemory(addr_target addr, int size)
{
#ifdef BF_DBG_32
	::VirtualFreeEx(mProcessInfo.hProcess, (void*)(intptr)addr, 0, MEM_RELEASE);
#else
	mDebugTarget->mHotHeap->Release(addr, size);
	::VirtualFreeEx(mProcessInfo.hProcess, (void*)(intptr)addr, size, MEM_DECOMMIT);
#endif
}

void WinDebugger::CleanupHotHeap()
{
	mDebugTarget->mLastHotHeapCleanIdx = mDebugTarget->mHotHeap->mBlockAllocIdx;

	// Our criteria for determining whether a hot loaded file is still being used:
	//  1) If we are currently executing a method from that object file.
	//  2) If the symbol map has a symbol with that address.
	//  3) If the static variable map contains a reference - including a conservative scan of the data
	//        This handles vdata references
	// This is a conservative check which won't purge hot reloads that contain deleted
	//  methods (for example), but it will purge hot reloads where all the changed
	//  data has been overwritten.
	// For delegate bindings, the original module declaring the bind creates a "preserve"
	//  global such as "bf_hs_preserve@_ZN5TestO4TestEv", whose preserved symbol ensures it
	//  doesn't get unloaded.  The current version of that method resides in "_ZN5TestO4TestEv",
	//  ensuring that the method pointed to by the global variable is valid
	mDebugTarget->mHotHeap->ClearReferencedFlags();

	addr_target lowAddr = mDebugTarget->mHotHeap->mHotAreaStart;
	addr_target highAddr = lowAddr + mDebugTarget->mHotHeap->mHotAreaSize;

	// Do conservative scan through all thread stacks.  Stack traces aren't 100% reliable, so we
	//  need to do a full conservative scan of any addresses stored in the stack
	//  to ensure we don't miss any return addresses
	for (int threadIdx = 0; threadIdx < (int)mThreadList.size(); threadIdx++)
	{
		WdThreadInfo* threadInfo = mThreadList[threadIdx];

		BF_CONTEXT lcContext;
		lcContext.ContextFlags = BF_CONTEXT_CONTROL;
		BF_GetThreadContext(threadInfo->mHThread, &lcContext);

		addr_target checkStackAddr = BF_CONTEXT_SP(lcContext);
		checkStackAddr &= ~(sizeof(addr_target) - 1);

		// Conservative check on registers
		for (int regNum = 0; regNum < sizeof(BF_CONTEXT)/sizeof(addr_target); regNum++)
		{
			addr_target checkAddr = ((addr_target*)&lcContext)[regNum];
			if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
				mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
		}

		// Conservative check on all stack data
		while (checkStackAddr < threadInfo->mStartSP)
		{
			addr_target checkAddrArr[1024];
			int numAddrsChecking = BF_MIN(1024, (int)((threadInfo->mStartSP - checkStackAddr) / sizeof(addr_target)));
			ReadMemory(checkStackAddr, numAddrsChecking * sizeof(addr_target), checkAddrArr);
			checkStackAddr += numAddrsChecking * sizeof(addr_target);

			for (int addrIdx = 0; addrIdx < numAddrsChecking; addrIdx++)
			{
				addr_target checkAddr = checkAddrArr[addrIdx];
				if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
					mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
			}
		}
	}

	auto mainModule = mDebugTarget->mTargetBinary;
	for (auto entry : mainModule->mSymbolNameMap)
	{
		auto dwSymbol = entry->mValue;
		addr_target checkAddr = dwSymbol->mAddress;
		if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
			mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
	}

	mDebugTarget->CleanupHotHeap();

	BfLogDbg("Hot load memory used: %dk\n", (int)mDebugTarget->mHotHeap->GetUsedSize() / 1024);
}

int WinDebugger::EnableWriting(intptr address, int size)
{
	DWORD oldProt;
	bool success = ::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)address, size, PAGE_READWRITE, &oldProt);
	if (!success)
	{
		int err = GetLastError();
	}
	return (int)oldProt;
}

int WinDebugger::SetProtection(intptr address, int size, int prot)
{
	DWORD oldProt;
	::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)address, size, prot, &oldProt);
	return (int)oldProt;
}

void WinDebugger::EnableMemCache()
{
	mMemCacheAddr = 1;
}

void WinDebugger::DisableMemCache()
{
	mMemCacheAddr = 0;
}

bool WinDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	if (local)
	{
		__try
		{
			memcpy(dest, (void*)address, length);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	if (mMemCacheAddr != 0)
	{
		addr_target targetAddr = (addr_target)address;
		if ((targetAddr >= mMemCacheAddr) && (targetAddr + length <= mMemCacheAddr + WD_MEMCACHE_SIZE) && (mMemCacheAddr > 1))
		{
			memcpy(dest, mMemCacheData + (targetAddr - mMemCacheAddr), length);
			return true;
		}

		// We need a new block
		SIZE_T dwReadBytes;
		if (::ReadProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, mMemCacheData, (SIZE_T)WD_MEMCACHE_SIZE, &dwReadBytes) != 0)
		{
			mMemCacheAddr = targetAddr;
			memcpy(dest, mMemCacheData, length);
			return true;
		}

		// Failed, turn off caching
		mMemCacheAddr = 0;
	}

	SIZE_T dwReadBytes;
	if (::ReadProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, dest, (SIZE_T)length, &dwReadBytes) != 0)
		return true;
	int lastErr = ::GetLastError();
	memset(dest, 0, length);
	return false;
}

bool WinDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	SIZE_T dwBytesWritten = 0;
	int result = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, src, (SIZE_T)length, &dwBytesWritten);
	return result != 0;
}

addr_target WinDebugger::GetTLSOffset(int tlsIndex)
{
	typedef LONG NTSTATUS;
	typedef DWORD KPRIORITY;
	typedef WORD UWORD;

	enum THREADINFOCLASS
	{
		ThreadBasicInformation,
	};

	struct CLIENT_ID
	{
		HANDLE UniqueProcess;
		HANDLE UniqueThread;
	};

	struct
	{
		NTSTATUS mExitStatus;
		void* mTebBaseAddress;
		CLIENT_ID mClientId;
		KAFFINITY mAffinityMask;
		KPRIORITY mPriority;
		KPRIORITY mBasePriority;
	} threadInfo = { 0 };

	ULONG len = 0;
	bool loadedManually = false;
	static HMODULE module = NULL;
	static NTSTATUS(__stdcall *NtQueryInformationThread)(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength, PULONG ReturnLength);

	if (module == NULL)
	{
		module = GetModuleHandleA("ntdll.dll");
		NtQueryInformationThread = reinterpret_cast<decltype(NtQueryInformationThread)>(GetProcAddress(module, "NtQueryInformationThread"));
	}

	if (NtQueryInformationThread == NULL)
		return 0;
	NTSTATUS status = NtQueryInformationThread(mActiveThread->mHThread, (THREADINFOCLASS)0, &threadInfo, sizeof(threadInfo), nullptr);
	if (status < 0)
		return 0;

#ifdef BF_DBG_32
	addr_target tibAddr = ReadMemory<addr_target>((intptr)threadInfo.mTebBaseAddress + 0x0);
	addr_target tlsTable = ReadMemory<addr_target>((intptr)tibAddr + 0x2C);
#else
	addr_target tlsTable = ReadMemory<addr_target>((intptr)threadInfo.mTebBaseAddress + 0x58);
#endif
	return ReadMemory<addr_target>(tlsTable + tlsIndex * sizeof(addr_target));
}

int WinDebugger::GetActiveThreadId()
{
	return mActiveThread->mThreadId;
}

String WinDebugger::GetActiveThreadName()
{
	return mActiveThread->mName;
}

void WinDebugger::ClearExplicitStopThread()
{
	mExplicitStopThread = NULL;
}

int WinDebugger::GetActiveBreakpointHitCount()
{
	if (mActiveBreakpoint != NULL)
		return mActiveBreakpoint->GetHeadBreakpoint()->mHitCount;
	return 0;
}

String WinDebugger::GetActiveBreakpointCondition()
{
	if ((mActiveBreakpoint != NULL) && (mActiveBreakpoint->mCondition != NULL))
		return mActiveBreakpoint->GetHeadBreakpoint()->mCondition->mExpr;
	return "";
}

int WinDebugger::GetTargetProcessId()
{
	return mProcessInfo.dwProcessId;
}

bool WinDebugger::WantsHotHeap()
{
	return (mHotSwapEnabled) && (mDbgProcessId == 0);
}

addr_target WinDebugger::ReserveHotHeapMemory(addr_target address, int size)
{
	return (addr_target)(intptr)::VirtualAllocEx(mProcessInfo.hProcess, (void*)(intptr)address, size, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void WinDebugger::ReleaseHotHeapMemory(addr_target address)
{
	::VirtualFreeEx(mProcessInfo.hProcess, (void*)(intptr)address, 0, MEM_RELEASE);
}

addr_target WinDebugger::DecodeThunk(addr_target address, DbgModuleMemoryCache* memoryCache)
{
	return mCPU->DecodeThunk(address, memoryCache);
}

bool WinDebugger::DecodeInstruction(addr_target address, DbgModuleMemoryCache* memoryCache, CPUInst* inst)
{
	return mCPU->Decode(address, memoryCache, inst);
}

bool WinDebugger::DecodeInstInfo(addr_target address, DbgModuleMemoryCache* memoryCache, CPURegisters* registers, DbgInstInfo* outInfo)
{
	CPUInst inst;
	if (!mCPU->Decode(address, memoryCache, &inst))
		return false;
	outInfo->mSize = inst.GetLength();
	outInfo->mIsCall = inst.IsCall();
	outInfo->mIsReturn = inst.IsReturn();
	outInfo->mTarget = 0;
	if (registers != NULL)
	{
		*registers->GetPCRegisterRef() = address;
		if (outInfo->mIsCall)
			outInfo->mTarget = inst.GetTarget(this, registers);
		inst.PartialSimulate(this, registers);
	}
	return true;
}

DbgBreakKind WinDebugger::GetDbgBreakKind(addr_target address, DbgModuleMemoryCache* memoryCache, intptr_target* regs, intptr_target* outObjectPtr)
{
	return mCPU->GetDbgBreakKind(address, memoryCache, regs, outObjectPtr);
}

bool WinDebugger::WriteInstructions(intptr address, void* src, uint64 length)
{
	SIZE_T dwBytesWritten = 0;
	bool result = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, src, (SIZE_T)length, &dwBytesWritten) != 0;
	result |= ::FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, (SIZE_T)length) != 0;
	BF_ASSERT(result);
	BfLogDbg("WriteInstructions: %p %d\n", address, length);
	return result;
}

DbgMemoryFlags WinDebugger::GetMemoryFlags(intptr address)
{
	MEMORY_BASIC_INFORMATION memBasicInfo;
	if (::VirtualQueryEx(mProcessInfo.hProcess, (void*)address, &memBasicInfo, sizeof(MEMORY_BASIC_INFORMATION)) == 0)
	{
		//BfLogDbg("VirtualQueryEx failed with %d\n", GetLastError());
		return DbgMemoryFlags_None;
	}

	DbgMemoryFlags flags = DbgMemoryFlags_None;
	if (memBasicInfo.AllocationProtect & PAGE_READWRITE)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	if (memBasicInfo.AllocationProtect & PAGE_READONLY)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
	if (memBasicInfo.AllocationProtect & PAGE_WRITECOPY)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_READ)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
	}
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_READWRITE)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_WRITECOPY)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	return flags;
}

#ifdef BF_DBG_32
Debugger* Beefy::CreateDebugger32(DebugManager* debugManager, DbgMiniDump* miniDump)
#else
Debugger* Beefy::CreateDebugger64(DebugManager* debugManager, DbgMiniDump* miniDump)
#endif
{
	if (miniDump != NULL)
	{
		auto debugger = new MiniDumpDebugger(debugManager, miniDump);

		return debugger;
	}

	return new WinDebugger(debugManager);
}

#ifdef BF_DBG_32

void WdAllocTest()
{
	Array<BeefyDbg32::WdStackFrame*> stackFrameList;

	for (int i = 0; true; i++)
	{
		WdStackFrame* stackFrame = new WdStackFrame();
		stackFrameList.push_back(stackFrame);
	}
}
#endif

#endif //!defined BF32 || !defined BF_DBG_64