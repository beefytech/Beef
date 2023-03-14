#include "Profiler.h"
#include "DebugManager.h"
#include <mmsystem.h>
#include <ntsecapi.h>   // UNICODE_STRING

#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004
typedef LONG KPRIORITY;

USING_NS_BF_DBG;

enum SYSTEM_INFORMATION_CLASS
{
	SystemProcessInformation = 5
}; // SYSTEM_INFORMATION_CLASS

struct CLIENT_ID
{
	HANDLE UniqueProcess;
	HANDLE UniqueThread;
}; // struct CLIENT_ID

enum THREAD_STATE
{
	StateInitialized,
	StateReady,
	StateRunning,
	StateStandby,
	StateTerminated,
	StateWait,
	StateTransition,
	StateUnknown
};

struct VM_COUNTERS
{
	SIZE_T        PeakVirtualSize;
	SIZE_T        VirtualSize;
	ULONG         PageFaultCount;
	SIZE_T        PeakWorkingSetSize;
	SIZE_T        WorkingSetSize;
	SIZE_T        QuotaPeakPagedPoolUsage;
	SIZE_T        QuotaPagedPoolUsage;
	SIZE_T        QuotaPeakNonPagedPoolUsage;
	SIZE_T        QuotaNonPagedPoolUsage;
	SIZE_T        PagefileUsage;
	SIZE_T        PeakPagefileUsage;
	SIZE_T        PrivatePageCount;
};

struct SYSTEM_THREAD {
	LARGE_INTEGER   KernelTime;
	LARGE_INTEGER   UserTime;
	LARGE_INTEGER   CreateTime;
	ULONG           WaitTime;
	LPVOID          StartAddress;
	CLIENT_ID       ClientId;
	DWORD           Priority;
	LONG            BasePriority;
	ULONG           ContextSwitchCount;
	THREAD_STATE    State;
	ULONG           WaitReason;
};

struct SYSTEM_PROCESS_INFORMATION
{
	ULONG                   NextEntryOffset;
	ULONG                   NumberOfThreads;
	LARGE_INTEGER           Reserved[3];
	LARGE_INTEGER           CreateTime;
	LARGE_INTEGER           UserTime;
	LARGE_INTEGER           KernelTime;
	UNICODE_STRING          ImageName;
	KPRIORITY               BasePriority;
	HANDLE                  ProcessId;
	HANDLE                  InheritedFromProcessId;
	ULONG                   HandleCount;
	ULONG                   Reserved2[2];
	ULONG                   PrivatePageCount;
	VM_COUNTERS             VirtualMemoryCounters;
	IO_COUNTERS             IoCounters;
	SYSTEM_THREAD           Threads[1];
};

typedef NTSTATUS(NTAPI* NtQuerySystemInformation_t)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NtQuerySystemInformation_t NtQuerySystemInformation = NULL;
static HMODULE ntdll = NULL;

DbgProfiler::DbgProfiler(WinDebugger* debugger) : mShutdownEvent(true)
{
	mDebugger = debugger;
	mIsRunning = false;
	mWantsClear = false;

	mSamplesPerSecond = 1000;

	mTotalVirtualSamples = 0;
	mTotalActualSamples = 0;
	mTotalActiveSamplingMS = 0;

	mStartTick = BFTickCount();
	mEndTick = 0;

	mDebugger->AddProfiler(this);

	mIdleSymbolNames.Add("NtUserGetMessage");
	mIdleSymbolNames.Add("NtUserMsgWaitForMultipleObjectsEx");
	mIdleSymbolNames.Add("NtWaitForAlertByThreadId");
	mIdleSymbolNames.Add("NtWaitForMultipleObjects");
	mIdleSymbolNames.Add("NtWaitForSingleObject");
	mIdleSymbolNames.Add("ZwDelayExecution");
	mIdleSymbolNames.Add("ZwRemoveIoCompletion");
	mIdleSymbolNames.Add("ZwWaitForAlertByThreadId");
	mIdleSymbolNames.Add("ZwWaitForMultipleObjects");
	mIdleSymbolNames.Add("ZwWaitForSingleObject");
	mIdleSymbolNames.Add("ZwWaitForWorkViaWorkerFactory");
}

DbgProfiler::~DbgProfiler()
{
	mDebugger->RemoveProfiler(this);

	Stop();

	DoClear();
}

static SYSTEM_PROCESS_INFORMATION* CaptureProcessInfo()
{
	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, MAX_PATH);
	wcscat(path, L"\\ntdll.dll");
	ntdll = GetModuleHandle(path);
	if (ntdll == NULL)
		return NULL;
	NtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(ntdll, "NtQuerySystemInformation");

	uint allocSize = 1024;
	uint8* data = NULL;

	while (true)
	{
		data = new uint8[allocSize];

		ULONG wantSize = 0;
		NTSTATUS status = ::NtQuerySystemInformation(SystemProcessInformation, data, allocSize, &wantSize);

		if (status != STATUS_INFO_LENGTH_MISMATCH)
			return (SYSTEM_PROCESS_INFORMATION*)data;

		allocSize = wantSize + 4096;
		delete data;
	}
}

void DbgProfiler::DoClear()
{
	for (auto& kv : mThreadInfo)
		delete kv.mValue;
	for (auto& val : mUniqueProcSet)
		delete val.mProcId;
}

ProfileProcId* DbgProfiler::Get(const StringImpl& str, bool* outIsNew)
{
	ProfileProdIdEntry checkEntry;
	checkEntry.mProcId = (ProfileProcId*)&str;

	ProfileProdIdEntry* entryPtr;
	if (mUniqueProcSet.TryAdd(checkEntry, &entryPtr))
	{
		auto procId = new ProfileProcId();
		procId->mProcName = str;
		procId->mIsIdle = false;
		entryPtr->mProcId = procId;
		if (outIsNew != NULL)
			*outIsNew = true;
		return procId;
	}
	else
	{
		if (outIsNew != NULL)
			*outIsNew = false;
		return entryPtr->mProcId;
	}
}

void DbgProfiler::ThreadProc()
{
	//TODO: Do timing smarter, handle delays and slow stack traces and stuff

	//timeBeginPeriod(1);

	BF_ASSERT(mTotalVirtualSamples == 0);

	ProfileAddrEntry profileEntry;

	const int maxStackTrace = 1024;
	addr_target stackTrace[maxStackTrace];

	DWORD prevSampleTick = timeGetTime();
	uint32 accumMS = 0;

	int totalWait = 0;
	int totalWait2 = 0;
	int iterations = 0;
	HashSet<int> idleThreadSet;

	while (true)
	{
		if (mWantsClear)
		{
			DoClear();

			mTotalVirtualSamples = 0;
			mTotalActualSamples = 0;
			mTotalActiveSamplingMS = 0;

			mAlloc.Clear();
			mThreadInfo.Clear();
			mThreadIdList.Clear();
			mProfileAddrEntrySet.Clear();
			mProfileAddrEntries.Clear();
			mPendingProfileEntries.Clear();
			mProfileProcEntrySet.Clear();
			mProfileProcEntries.Clear();
			mProfileAddrToProcMap.Clear();
			mProcMap.Clear();
			mUniqueProcSet.Clear();
			mWantsClear = false;

			accumMS = 0;
			prevSampleTick = timeGetTime();
		}

		iterations++;
		DWORD startTick0 = timeGetTime();
		idleThreadSet.Clear();

		SYSTEM_PROCESS_INFORMATION* processData = NULL;
		std::unique_ptr<SYSTEM_PROCESS_INFORMATION> ptrDelete(processData);

		//processData = CaptureProcessInfo();

		auto curProcessData = processData;
		while (true)
		{
			if (processData == NULL)
				break;

			if ((DWORD)(uintptr)curProcessData->ProcessId == mDebugger->mProcessInfo.dwProcessId)
			{
				for (int threadIdx = 0; threadIdx < (int)curProcessData->NumberOfThreads; threadIdx++)
				{
					auto& threadInfo = curProcessData->Threads[threadIdx];
					if ((threadInfo.State == StateWait) || (threadInfo.State == StateTerminated))
						idleThreadSet.Add((int)(intptr)threadInfo.ClientId.UniqueThread);
					break;
				}
			}

			if (curProcessData->NextEntryOffset == 0)
				break;
			curProcessData = (SYSTEM_PROCESS_INFORMATION*)((intptr)curProcessData + curProcessData->NextEntryOffset);
		}

		if (mShutdownEvent.WaitFor(0))
		{
			break;
		}

		DWORD tickNow = timeGetTime();
		accumMS += (int)(tickNow - prevSampleTick);
		prevSampleTick = tickNow;

		int wantVirtualSamples = (int)((int64)accumMS * mSamplesPerSecond / 1000);

		int curSampleCount = wantVirtualSamples - mTotalVirtualSamples;
		//BF_ASSERT(curSampleCount >= 0);
		if (curSampleCount <= 0)
			continue;

		{
			AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

			if ((mDebugger->mRunState == RunState_NotStarted) ||
				(mDebugger->mRunState == RunState_Terminated))
				break;

			if (mDebugger->mRunState != RunState_Running)
			{
				// Reset timer
				accumMS = (int)((int64)mTotalVirtualSamples * 1000 / mSamplesPerSecond);
				continue;
			}
		}

		int maxSamplesPerIteration = mSamplesPerSecond / 10;
		if (curSampleCount > maxSamplesPerIteration)
		{
			curSampleCount = maxSamplesPerIteration;
			wantVirtualSamples = mTotalVirtualSamples + curSampleCount;
			accumMS = (int)((int64)wantVirtualSamples * 1000 / mSamplesPerSecond);
		}

		mTotalVirtualSamples += curSampleCount;
		mTotalActualSamples++;

		int threadIdx = 0;
		while (true)
		{
			int startTick = timeGetTime();

			AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

			if (mDebugger->mRunState != RunState_Running)
				break;

			if (threadIdx >= mDebugger->mThreadList.size())
				break;

			auto thread = mDebugger->mThreadList[threadIdx];

			if ((mTargetThreadId > 0) && (thread->mThreadId != mTargetThreadId))
			{
				threadIdx++;
				continue;
			}

			ProfileThreadInfo* profileThreadInfo;
			ProfileThreadInfo** profileThreadInfoPtr;
			if (mThreadInfo.TryAdd(thread->mThreadId, NULL, &profileThreadInfoPtr))
			{
				mDebugger->TryGetThreadName(thread);
				profileThreadInfo = new	ProfileThreadInfo();
				profileThreadInfo->mName = thread->mName;
				*profileThreadInfoPtr = profileThreadInfo;
				mThreadIdList.Add(thread->mThreadId);
			}
			else
			{
				profileThreadInfo = *profileThreadInfoPtr;
			}

			bool isThreadIdle = idleThreadSet.Contains(thread->mThreadId);

			mDebugger->mActiveThread = thread;

			::SuspendThread(thread->mHThread);

			CPURegisters registers;
			mDebugger->PopulateRegisters(&registers);

			addr_target prevPC = 0;
			bool traceIsValid = true;
			int stackSize = 0;
			for (int stackIdx = 0; stackIdx < maxStackTrace; stackIdx++)
			{
				auto pc = registers.GetPC();
				if (pc == 0)
				{
					bool* valuePtr = NULL;
					if (mStackHeadCheckMap.TryAdd(prevPC, NULL, &valuePtr))
					{
						addr_target symbolOffset = 0;
						String symbolName;
						mDebugger->mDebugTarget->FindSymbolAt(prevPC, &symbolName, &symbolOffset, NULL, false);
						*valuePtr = (symbolName == "RtlUserThreadStart");
					}

					if (!*valuePtr)
						traceIsValid = false;

					// Done - success (?). Check lastDbgModule.
					break;
				}

				prevPC = pc;
				auto lastDbgModule = mDebugger->mDebugTarget->FindDbgModuleForAddress(pc);
				if (lastDbgModule == NULL)
				{
					traceIsValid = false;
					break;
				}

				stackTrace[stackSize++] = pc;
				auto prevSP = registers.GetSP();
				if (!mDebugger->RollBackStackFrame(&registers, stackIdx == 0))
					break;
				if (registers.GetSP() <= prevSP)
				{
					// SP went the wrong direction, stop rolling back
					traceIsValid = false;
					break;
				}
			}

			if (traceIsValid)
			{
				profileThreadInfo->mTotalSamples += curSampleCount;
				if (isThreadIdle)
					profileThreadInfo->mTotalIdleSamples += curSampleCount;

				ProfileAddrEntry* insertedProfileEntry = AddToSet(mProfileAddrEntrySet, stackTrace, stackSize);
				if (insertedProfileEntry->mEntryIdx == -1)
				{
					insertedProfileEntry->mEntryIdx = (int)mProfileAddrEntrySet.size(); // Starts at '1'
					mPendingProfileEntries.Add(*insertedProfileEntry);
				}

				for (int i = 0; i < curSampleCount; i++)
				{
					int entryIdx = insertedProfileEntry->mEntryIdx;
					if (isThreadIdle)
						entryIdx = -entryIdx;
					profileThreadInfo->mProfileAddrEntries.push_back(entryIdx);
				}

				int elapsedTime = timeGetTime() - startTick;
				mTotalActiveSamplingMS += elapsedTime;
			}

			::ResumeThread(thread->mHThread);

			mDebugger->mActiveThread = NULL;

			threadIdx++;

			HandlePendingEntries();
		}
	}

	mIsRunning = false;

	mEndTick = BFTickCount();
}

static void BFP_CALLTYPE ThreadProcThunk(void* profiler)
{
	((DbgProfiler*)profiler)->ThreadProc();
}

void DbgProfiler::Start()
{
	BF_ASSERT(!mIsRunning);

	mNeedsProcessing = true;
	mIsRunning = true;
	auto thread = BfpThread_Create(ThreadProcThunk, (void*)this, 128 * 1024, BfpThreadCreateFlag_StackSizeReserve);
	BfpThread_Release(thread);
}

void DbgProfiler::Stop()
{
	mShutdownEvent.Set();
	while (mIsRunning)
		BfpThread_Yield();

	//Process();
}

void DbgProfiler::Clear()
{
	mWantsClear = true;
}

String DbgProfiler::GetOverview()
{
	String str;

	uint32 curTick = BFTickCount();
	if (mEndTick == 0)
		str += StrFormat("%d", curTick - mStartTick);
	else
		str += StrFormat("%d\t%d", mEndTick - mStartTick, curTick - mEndTick);
	str += "\n";

	str += StrFormat("%d\t%d\t%d\n", mSamplesPerSecond, mTotalVirtualSamples, mTotalActualSamples);

	str += mDescription;
	return str;
}

String DbgProfiler::GetThreadList()
{
	BF_ASSERT(!mIsRunning);

	String result;

	for (auto threadId : mThreadIdList)
	{
		auto threadInfo = mThreadInfo[threadId];
		int cpuUsage = 0;
		if (threadInfo->mTotalSamples > 0)
			cpuUsage = 100 - (threadInfo->mTotalIdleSamples * 100 / threadInfo->mTotalSamples);
		result += StrFormat("%d\t%d\t%s\n", threadId, cpuUsage, threadInfo->mName.c_str());
	}

	return result;
}

void DbgProfiler::AddEntries(String& str, Array<ProfileProcEntry*>& procEntries, int rangeStart, int rangeEnd, int stackIdx, ProfileProcId* findProc)
{
	struct _QueuedEntry
	{
		int mRangeIdx;
		int mRangeEnd;
		int mStackIdx;
	};
	Array<_QueuedEntry> workQueue;
	int skipStackCount = 0;

	auto _AddEntries = [&](int rangeStart, int rangeEnd, int stackIdx, ProfileProcId* findProc)
	{
		int selfSampleCount = 0;
		int childSampleCount = 0;

		// First arrange list so we only contain items that match 'findProc'
		if (stackIdx >= skipStackCount)
		{
			for (int idx = rangeStart; idx < rangeEnd; idx++)
			{
				auto procEntry = procEntries[idx];

				if (procEntry->mUsed)
					continue;

				// The range should only contain candidates
				BF_ASSERT(procEntry->mSize > stackIdx);

				auto curProc = procEntry->mData[procEntry->mSize - 1 - stackIdx];

				bool doRemove = false;

				if (findProc != curProc)
				{
					// Not a match
					doRemove = true;
				}
				else
				{
					if (procEntry->mSize == stackIdx + 1)
					{
						BF_ASSERT(selfSampleCount == 0);
						selfSampleCount = procEntry->mSampleCount;

						// We are a the full actual entry, so we're done here...
						procEntry->mUsed = true;
						doRemove = true;
					}
					else
					{
						childSampleCount += procEntry->mSampleCount;
					}
				}

				if (doRemove)
				{
					// 'fast remove' entry
					BF_SWAP(procEntries[rangeEnd - 1], procEntries[idx]);
					rangeEnd--;
					idx--;
					continue;
				}
			}

			if (findProc == NULL)
				str += "???";
			else
				str += findProc->mProcName;
			char cStr[256];
			sprintf(cStr, "\t%d\t%d\n", selfSampleCount, childSampleCount);
			str += cStr;
		}

		_QueuedEntry entry;
		entry.mRangeIdx = rangeStart;
		entry.mRangeEnd = rangeEnd;
		entry.mStackIdx = stackIdx;
		workQueue.Add(entry);
	};
	_AddEntries(rangeStart, rangeEnd, stackIdx, findProc);

	while (skipStackCount < 6)
	{
		bool isSkipValid = true;

		for (int idx = rangeStart; idx < rangeEnd; idx++)
		{
			auto procEntry = procEntries[idx];
			if (procEntry->mUsed)
				continue;

			int stackIdx = procEntry->mSize - 1 - skipStackCount;
			if (stackIdx < 0)
			{
				isSkipValid = false;
				break;
			}

			auto checkProc = procEntry->mData[procEntry->mSize - 1 - skipStackCount];

			switch (skipStackCount)
			{
			case 0:
				if (checkProc->mProcName != "RtlUserThreadStart")
					isSkipValid = false;
				break;
			case 1:
				if (checkProc->mProcName != "BaseThreadInitThunk")
					isSkipValid = false;
				break;
			case 2:
				if ((checkProc->mProcName != "__scrt_common_main_seh()") && (checkProc->mProcName != "mainCRTStartup()"))
					isSkipValid = false;
				break;
			case 3:
				if ((checkProc->mProcName != "invoke_main()") && (checkProc->mProcName != "main"))
					isSkipValid = false;
				break;
			case 4:
				if (checkProc->mProcName != "main")
					isSkipValid = false;
				break;
			case 5:
				if (checkProc->mProcName != "BeefStartProgram")
					isSkipValid = false;
				break;
			default:
				isSkipValid = false;
			}

			if (!isSkipValid)
				break;
		}

		if (!isSkipValid)
			break;
		skipStackCount++;
	}

	while (!workQueue.IsEmpty())
	{
		auto& entry = workQueue.back();

		bool addedChild = false;
		while (entry.mRangeIdx < entry.mRangeEnd)
		{
			auto procEntry = procEntries[entry.mRangeIdx];
			if (procEntry->mUsed)
			{
				entry.mRangeIdx++;
				continue;
			}

			int nextStackIdx = entry.mStackIdx + 1;
			auto nextFindProc = procEntry->mData[procEntry->mSize - 1 - nextStackIdx];
			_AddEntries(entry.mRangeIdx, entry.mRangeEnd, nextStackIdx, nextFindProc);
			addedChild = true;
			break;
		}

		if (!addedChild)
		{
			if ((entry.mStackIdx != -1) && (entry.mStackIdx >= skipStackCount))
				str += "-\n";
			workQueue.pop_back();
		}
	}
}

void DbgProfiler::HandlePendingEntries()
{
	bool reverse = false;
	const int maxStackTrace = 1024;
	ProfileProcId* procStackTrace[maxStackTrace];

	while (mProfileAddrToProcMap.size() < mProfileAddrEntrySet.size() + 1)
		mProfileAddrToProcMap.push_back(-1);

	for (int addrEntryIdx = 0; addrEntryIdx < (int)mPendingProfileEntries.size(); addrEntryIdx++)
	{
		const ProfileAddrEntry* addrEntry = &mPendingProfileEntries[addrEntryIdx];

		ProfileProcId** procStackTraceHead = reverse ? (procStackTrace + maxStackTrace) : procStackTrace;
		int stackTraceProcIdx = 0;
		for (int addrIdx = 0; addrIdx < addrEntry->mSize; addrIdx++)
		{
			addr_target addr = addrEntry->mData[addrIdx];
			if (addrIdx > 0)
			{
				// To reference from SourcePC (calling address, not return address)
				addr--;
			}
			ProfileProcId* procId = NULL;

			auto subProgram = mDebugger->mDebugTarget->FindSubProgram(addr, DbgOnDemandKind_LocalOnly);
			while (stackTraceProcIdx < maxStackTrace)
			{
				if (subProgram != NULL)
				{
					ProfileProcId** procIdPtr;
					if (mProcMap.TryAdd(subProgram, NULL, &procIdPtr))
					{
						String procName = subProgram->ToString();
						procId = Get(procName);
						*procIdPtr = procId;
					}
					else
						procId = *procIdPtr;
				}
				else
				{
					String symbolName;
					addr_target symbolOffset = 0;
					if ((!mDebugger->mDebugTarget->FindSymbolAt(addr, &symbolName, &symbolOffset, NULL, false)) || (symbolOffset > 64 * 1024))
					{
						auto dbgModule = mDebugger->mDebugTarget->FindDbgModuleForAddress(addr);
						if (dbgModule != NULL)
							symbolName += dbgModule->mDisplayName + "!";
						symbolName += StrFormat("0x%@", addr);
					}

					bool isNew = false;
					procId = Get(symbolName, &isNew);
					if (isNew)
						procId->mIsIdle = mIdleSymbolNames.Contains(symbolName);
				}

				if (reverse)
					*(--procStackTraceHead) = procId;
				else
					procStackTraceHead[stackTraceProcIdx] = procId;
				stackTraceProcIdx++;

				if (subProgram == NULL)
					break;
				if (subProgram->mInlineeInfo == NULL)
					break;
				subProgram = subProgram->mInlineeInfo->mInlineParent;
			}
		}

		auto procEntry = AddToSet(mProfileProcEntrySet, procStackTraceHead, stackTraceProcIdx);
		if (procEntry->mEntryIdx == -1)
		{
			procEntry->mEntryIdx = (int)mProfileProcEntrySet.size() - 1; // Start at '0'
		}
		mProfileAddrToProcMap[addrEntry->mEntryIdx] = procEntry->mEntryIdx;
	}
	mPendingProfileEntries.Clear();
}

void DbgProfiler::Process()
{
	AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

	mNeedsProcessing = false;

	int time = mTotalActiveSamplingMS;

	BF_ASSERT(mProfileAddrEntries.IsEmpty());

	mProfileAddrEntries.Resize(mProfileAddrEntrySet.size() + 1);
	for (auto& val : mProfileAddrEntrySet)
		mProfileAddrEntries[val.mEntryIdx] = &val;

	BF_ASSERT(mProfileProcEntries.IsEmpty());
	mProfileProcEntries.Resize(mProfileProcEntrySet.size());
	for (auto& val : mProfileProcEntrySet)
		mProfileProcEntries[val.mEntryIdx] = &val;

	for (auto threadKV : mThreadInfo)
	{
		auto threadInfo = threadKV.mValue;

		for (auto addrEntryIdx : threadInfo->mProfileAddrEntries)
		{
			if (addrEntryIdx < 0)
			{
				addrEntryIdx = -addrEntryIdx;
			}

			int procEntryIdx = mProfileAddrToProcMap[addrEntryIdx];
			auto procEntry = mProfileProcEntries[procEntryIdx];

			auto curProc = procEntry->mData[0];
			if (curProc->mIsIdle)
				threadInfo->mTotalIdleSamples++;
		}
	}
}

String DbgProfiler::GetCallTree(int threadId, bool reverse)
{
	if (mNeedsProcessing)
		Process();

	AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

	BF_ASSERT(!mIsRunning);

	Array<ProfileProcEntry*> procEntries;

	String str;

	for (auto procEntry : mProfileProcEntries)
	{
		procEntry->mUsed = false;
		procEntry->mSampleCount = 0;
	}

	bool showIdleEntries = false;

	int totalSampleCount = 0;
	int totalActiveSampleCount = 0;
	for (auto& threadPair : mThreadInfo)
	{
		if ((threadId == 0) || (threadId == threadPair.mKey))
		{
			auto threadInfo = threadPair.mValue;
			totalSampleCount += threadInfo->mTotalSamples;
			for (auto addrEntryIdx : threadInfo->mProfileAddrEntries)
			{
				if (addrEntryIdx < 0)
				{
					if (showIdleEntries)
						addrEntryIdx = -addrEntryIdx;
					else
						continue;
				}

				int procEntryIdx = mProfileAddrToProcMap[addrEntryIdx];
				auto procEntry = mProfileProcEntries[procEntryIdx];

				if (procEntry->mSampleCount == 0)
					procEntries.push_back(procEntry);
				procEntry->mSampleCount++;
				totalActiveSampleCount++;
			}
		}
	}

	if (threadId == 0)
		str += "All Threads";
	else
		str += StrFormat("Thread %d", threadId);
	str += StrFormat("\t0\t%d\n", totalSampleCount);
	int idleTicks = totalSampleCount - totalActiveSampleCount;
	if (idleTicks != 0)
		str += StrFormat("<Idle>\t%d\t0\n-\n", idleTicks);

	AddEntries(str, procEntries, 0, (int)procEntries.size(), -1, NULL);

	str += "-\n";

	//BF_ASSERT(childSampleCount == totalSampleCount);

	return str;
}