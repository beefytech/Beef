#include "MiniDumpDebugger.h"
#include "DebugManager.h"

#pragma warning(disable:4091)
#include <DbgHelp.h>

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;
USING_NS_BF;

MiniDumpDebugger::MiniDumpDebugger(DebugManager* debugManager, DbgMiniDump* miniDump) : WinDebugger(debugManager)
{
	mMiniDump = miniDump;
	mRunState = RunState_Paused;
	mExceptionThread = NULL;
	mExceptionContextRVA = 0;

	mDebugTarget = new DebugTarget(this);

	for (auto section : mMiniDump->mDirectory)
	{
		if (section.mStreamType == ThreadExListStream)
		{
			auto& threadEx = mMiniDump->GetStreamData<MINIDUMP_THREAD_EX_LIST>(section);
			
			/*WdThreadInfo* threadInfo = new WdThreadInfo();
			threadInfo->mThreadId = threadEx.ThreadId;			
			mThreadList.Add(threadInfo);

			if (mActiveThread == NULL)
			{
				mActiveThread = threadInfo;
				mDebuggerWaitingThread = threadInfo;
			}

			mThreadMap[threadInfo->mThreadId] = threadInfo;*/
		}
		else if (section.mStreamType == ModuleListStream)
		{
			auto& moduleList = mMiniDump->GetStreamData<MINIDUMP_MODULE_LIST>(section);
			for (int moduleIdx = 0; moduleIdx < (int)moduleList.NumberOfModules; moduleIdx++)
			{
				auto& module = moduleList.Modules[moduleIdx];
				COFF* dbgModule = new COFF(mDebugTarget);
				if (mDebugTarget->mTargetBinary == NULL)
				{
					mDebugTarget->mLaunchBinary = dbgModule;
					mDebugTarget->mTargetBinary = dbgModule;
				}
				dbgModule->mImageBase = module.BaseOfImage;
				dbgModule->mImageSize = module.SizeOfImage;

				//TODO: 'get' the actual image
				dbgModule->mTimeStamp = module.TimeDateStamp;
				//dbgModule->mExpectedFileSize = module.;
				
				const wchar_t* moduleName = &mMiniDump->GetData<wchar_t>(module.ModuleNameRva + 4);
				dbgModule->mFilePath = UTF8Encode(moduleName);
				dbgModule->mDisplayName = GetFileName(dbgModule->mFilePath);

				struct _CodeViewEntry
				{
				public:
					int32 mSig;
					uint8 mGUID[16];
					int32 mAge;
					const char mPDBPath[1];
				};
				auto& codeViewEntry = mMiniDump->GetData<_CodeViewEntry>(module.CvRecord.Rva);
				if (codeViewEntry.mSig == 'SDSR')
				{
					// Do nothing, let the binay load the PDB itself
					//dbgModule->LoadPDB(codeViewEntry.mPDBPath, codeViewEntry.mGUID, codeViewEntry.mAge);
				}

				auto miscEntry = &mMiniDump->GetData<char>(module.MiscRecord.Rva);				
				
				mDebugTarget->mDbgModules.Add(dbgModule);

				//TESTING
				/*{
					AutoCrit autoCrit(mDebugManager->mCritSect);
					mDebuggerThreadId = GetCurrentThreadId();
					dbgModule->RequestImage();
					dbgModule->RequestDebugInfo();
					mDebuggerThreadId = 0;
				}*/

				mPendingImageLoad.Add(dbgModule);

				// This is optional
				mPendingDebugInfoRequests.Add(dbgModule);
			}
		}
		else if (section.mStreamType == ThreadListStream)
		{
			auto& threadList = mMiniDump->GetStreamData<MINIDUMP_THREAD_LIST>(section);
			for (int threadIdx = 0; threadIdx < (int)threadList.NumberOfThreads; threadIdx++)
			{
				auto& thread = threadList.Threads[threadIdx];

				WdThreadInfo* threadInfo = new WdThreadInfo();
				threadInfo->mThreadId = thread.ThreadId;
				mThreadList.Add(threadInfo);

				if (mActiveThread == NULL)
				{
					mActiveThread = threadInfo;
					mDebuggerWaitingThread = threadInfo;
				}

				if ((thread.Stack.Memory.Rva != 0) && (thread.Stack.Memory.DataSize > 0))
				{
					void* stackMemory = &mMiniDump->GetData<uint8>(thread.Stack.Memory.Rva);
					MapMemory((addr_target)thread.Stack.StartOfMemoryRange, stackMemory, thread.Stack.Memory.DataSize);
				}

				mThreadMap[threadInfo->mThreadId] = threadInfo;
			}
		}
		else if (section.mStreamType == MemoryInfoListStream)
		{
			auto& memoryInfoList = mMiniDump->GetStreamData<MINIDUMP_MEMORY_INFO_LIST>(section);
			for (int memoryInfoIdx = 0; memoryInfoIdx < (int)memoryInfoList.NumberOfEntries; memoryInfoIdx++)
			{
				auto& memoryInfo = mMiniDump->GetData<MINIDUMP_MEMORY_INFO>(section.mDataRVA + memoryInfoList.SizeOfHeader + memoryInfoIdx*memoryInfoList.SizeOfEntry);				
			}
		}
		else if (section.mStreamType == MemoryListStream)
		{
			auto& memoryList = mMiniDump->GetStreamData<MINIDUMP_MEMORY_LIST>(section);
			for (int memoryIdx = 0; memoryIdx < (int)memoryList.NumberOfMemoryRanges; memoryIdx++)
			{
				auto& memory = memoryList.MemoryRanges[memoryIdx];
				if (memory.Memory.Rva != 0)
				{
					void* memoryPtr = &mMiniDump->GetData<uint8>(memory.Memory.Rva);
					MapMemory((addr_target)memory.StartOfMemoryRange, memoryPtr, memory.Memory.DataSize);
				}
			}
		}
		else if (section.mStreamType == ExceptionStream)
		{
			auto& exceptionStream = mMiniDump->GetStreamData<MINIDUMP_EXCEPTION_STREAM>(section);
			
			//mCurException = exceptionStream.ExceptionRecord;			
			mCurException.ExceptionCode = exceptionStream.ExceptionRecord.ExceptionCode;
			mCurException.ExceptionFlags = exceptionStream.ExceptionRecord.ExceptionFlags;
			mCurException.ExceptionAddress = (PVOID)exceptionStream.ExceptionRecord.ExceptionAddress;
			mCurException.NumberParameters = exceptionStream.ExceptionRecord.NumberParameters;
			for (int i = 0; i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
				mCurException.ExceptionInformation[i] = exceptionStream.ExceptionRecord.ExceptionInformation[i];
			
			WdThreadInfo* threadInfo = NULL;
			if (mThreadMap.TryGetValue(exceptionStream.ThreadId, &threadInfo))
			{
				mActiveThread = threadInfo;
				mExplicitStopThread = mActiveThread;
				mRunState = RunState_Exception;

				/*mDebugPendingExpr->mException = StrFormat("Exception at 0x%@ in thread %d, exception code 0x%08X",
					mCurException.ExceptionAddress, mActiveThread->mThreadId, mCurException.ExceptionCode);*/

				mExceptionThread = mActiveThread;
				mExceptionContextRVA = exceptionStream.ThreadContext.Rva;
			}
		}
		else if (section.mStreamType == SystemInfoStream)
		{
			auto& systemInfo = mMiniDump->GetStreamData<MINIDUMP_SYSTEM_INFO>(section);			
		}
		else if (section.mStreamType == MiscInfoStream)
		{
			auto& miscInfo = mMiniDump->GetStreamData<MINIDUMP_MISC_INFO>(section);			
		}
		else if (section.mStreamType == 21/*SystemMemoryInfoStream*/)
		{
			auto data = ((uint8*)mMiniDump->mMF.mData + section.mDataRVA);			
		}
		else if (section.mStreamType == 22/*ProcessVmCountersStream*/)
		{
			auto data = ((uint8*)mMiniDump->mMF.mData + section.mDataRVA);			
		}
		else if (section.mStreamType == 24) // Thread names
		{
			auto data = ((uint8*)mMiniDump->mMF.mData + section.mDataRVA);
			int count = *(int32*)(data);
			for (int threadIdx = 0; threadIdx < count; threadIdx++)
			{
				struct ThreadNameInfo
				{
					int32 mThreadId;
					int32 mNameRVA;
					int32 mFlags; // Always zero
				};

				ThreadNameInfo* threadNameInfo = (ThreadNameInfo*)(data + 4 + threadIdx*12);

				int nameLen = *(int32*)((uint8*)mMiniDump->mMF.mData + threadNameInfo->mNameRVA);
				UTF16String name = UTF16String((wchar_t*)((uint8*)mMiniDump->mMF.mData + threadNameInfo->mNameRVA + 4), nameLen);
				
				WdThreadInfo* threadInfo = NULL;
				if (mThreadMap.TryGetValue(threadNameInfo->mThreadId, &threadInfo))
				{
					threadInfo->mName = UTF8Encode(name);
				}
			}
		}
		else if (section.mStreamType == 0x43500001) // kMinidumpStreamTypeCrashpadInfo
		{
			struct _MiniDumpCrashPadInfo
			{
				uint32 mVersion;
				GUID mReportID;
				GUID mClientID;
			};

			auto& crashPadInfo = mMiniDump->GetStreamData<_MiniDumpCrashPadInfo>(section);			
		}
		else if (section.mStreamType == 0x4b6b0002) // Stability report
		{
			const char* report = &mMiniDump->GetStreamData<char>(section);			
		}
	}

	Run();
}

MiniDumpDebugger::~MiniDumpDebugger()
{
	delete mMiniDump;

	for (auto mappedFile : mMappedFiles)
		delete mappedFile;
}

void MiniDumpDebugger::MapMemory(addr_target addr, void* data, intptr_target size)
{
	addr_target beginAddress = addr;
	addr_target endAddress = addr + size;

	int memSize = (int)(endAddress - beginAddress);
	for (int memOffset = 0; true; memOffset += DBG_MAX_LOOKBACK)
	{
		int curSize = memSize - memOffset;
		if (curSize <= 0)
			break;

		MiniDumpMemoryRegion* memRegion = mAlloc.Alloc<MiniDumpMemoryRegion>();
		memRegion->mAddress = beginAddress + memOffset;		
		memRegion->mAddressLength = curSize;
		memRegion->mData = (uint8*)data + memOffset;
		memRegion->mNext = NULL;
		mMemMap.Insert(memRegion);
	}
}

MappedFile* MiniDumpDebugger::MapModule(COFF* dbgModule, const StringImpl& fileName)
{
	auto mappedFile = new MappedFile();
	if (!mappedFile->Open(fileName))
	{
		delete mappedFile;
		return NULL;	
	}
	mMappedFiles.Add(mappedFile);	
	return mappedFile;
}

bool MiniDumpDebugger::PopulateRegisters(CPURegisters* registers)
{
	if (mActiveThread == mExceptionThread)
	{
		auto& ctx = mMiniDump->GetData<BF_CONTEXT>(mExceptionContextRVA);
		return PopulateRegisters(registers, ctx);
	}

	for (auto section : mMiniDump->mDirectory)
	{
		if (section.mStreamType == ThreadExListStream)
		{
			
		}
		else if (section.mStreamType == ThreadListStream)
		{
			auto& threadList = mMiniDump->GetStreamData<MINIDUMP_THREAD_LIST>(section);
			for (int threadIdx = 0; threadIdx < (int)threadList.NumberOfThreads; threadIdx++)
			{
				auto& thread = threadList.Threads[threadIdx];
				if (thread.ThreadId == mActiveThread->mThreadId)
				{
					auto& ctx = mMiniDump->GetData<BF_CONTEXT>(thread.ThreadContext.Rva);
					return PopulateRegisters(registers, ctx);
				}
			}
		}
	}

	return false;
}

bool MiniDumpDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	if (local)
	{
		__try
		{
			memcpy(dest, (void*)address, length);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	uintptr useAddr = (uintptr)address;

	while (true)
	{
		MiniDumpMemoryRegion* memRegion = mMemMap.Get(useAddr, DBG_MAX_LOOKBACK);
		if (memRegion == NULL)
			return false;

		if ((uintptr)address < (uintptr)memRegion->mAddress)
			return false; // Out of bounds, done

		while (memRegion != NULL)
		{
			if (((uintptr)address >= (uintptr)memRegion->mAddress) && ((uintptr)address < memRegion->mAddress + memRegion->mAddressLength))
			{
				if ((uintptr)address + length <= (uintptr)(memRegion->mAddress + memRegion->mAddressLength))
				{
					// In bounds
					memcpy(dest, (uint8*)memRegion->mData + (address - memRegion->mAddress), length);
				}
				else
				{
					int headBytes = (int)(memRegion->mAddress + memRegion->mAddressLength - address);
					memcpy(dest, (uint8*)memRegion->mData + (address - memRegion->mAddress), headBytes);
					if (!ReadMemory(address + headBytes, length - headBytes, (uint8*)dest + headBytes, local))
						return false;
				}
				return true;
			}			

			useAddr = BF_MIN(useAddr, memRegion->mAddress - 1);
			memRegion = memRegion->mNext;			
		}

		//if (((uintptr)address < (uintptr)memRegion->mAddress) || ((uintptr)(address + length) > uintptr(memRegion->mAddress + memRegion->mAddressLength)))
			//return false; // Out of bounds
	}
	
	return false;
}

bool MiniDumpDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	return false;
}

