#include "HotScanner.h"
#include "WinDebugger.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

DbgHotScanner::DbgHotScanner(WinDebugger* debugger)
{
	mDebugger = debugger;
	mBfTypesInfoAddr = 0;
	mDbgGCData = { 0 };
}

NS_BF_DBG_BEGIN
namespace TCFake
{
	struct Span
	{
		addr_target   start;          // Starting page number
		addr_target   length;         // Number of pages in span
		addr_target   next;           // Used when in link list
		addr_target   prev;           // Used when in link list
		addr_target   objects;        // Linked list of free objects
		addr_target   freeingObjects; // BCF- waiting to be freed
		addr_target   freeingObjectsTail;  // Note: *((intptr_t*)freeingObjectsTail) is freeingCount
		unsigned int  refcount : 16;  // Number of non-free objects
		unsigned int  sizeclass : 8;  // Size-class for small objects (or 0)
		unsigned int  location : 2;   // Is the span on a freelist, and if so, which?
		unsigned int  decommitDelay : 3;
		unsigned int  sample : 1;     // Sampled object?

#undef SPAN_HISTORY
#ifdef SPAN_HISTORY
  // For debugging, we can keep a log events per span
		int nexthistory;
		char history[64];
		int value[64];
#endif

		// What freelist the span is on: IN_USE if on none, or normal or returned
		enum { IN_USE, ON_NORMAL_FREELIST, ON_RETURNED_FREELIST };
	};
}

struct Fake_BfObject_WithFlags
{
	union
	{
		addr_target mClassVData;
		struct
		{
			BfObjectFlags mObjectFlags;
			uint8 mClassVDataBytes[sizeof(addr_target) - 1];
		};
	};

	union
	{
		addr_target mAllocCheckPtr;
		addr_target mDbgAllocInfo; // Meaning depends on object flags- could be PC at allocation
	};
};

struct Fake_Type_Data
{
	int32 mSize;
	int32 mTypeId;
	int32 mBoxedType;
	int32 mTypeFlags;
};

struct Fake_Delegate_Data
{
	addr_target mFuncPtr;
	addr_target Target;
};

struct Fake_DbgRawAllocData
{
	addr_target mType;
	addr_target mMarkFunc;
	int32 mMaxStackTrace; // Only 0, 1, >1 matters
};

#define BF_OBJECTFLAG_DELETED           0x80

NS_BF_END

using namespace TCFake;

void DbgHotScanner::AddSubProgram(DbgSubprogram* subProgram, bool followInlineParent, const Beefy::StringImpl& prefix)
{
	if ((followInlineParent) && (subProgram->mInlineeInfo != NULL) && (subProgram->mInlineeInfo->mInlineParent != NULL))
		AddSubProgram(subProgram->mInlineeInfo->mInlineParent, true, prefix);

	subProgram->mCompileUnit->mDbgModule->ParseSymbolData();

	String methodName;
	addr_target offset;
	if (mDebugger->mDebugTarget->FindSymbolAt(subProgram->mBlock.mLowPC, &methodName, &offset))
	{
		if (offset == 0)
		{
			if (!prefix.IsEmpty())
				methodName.Insert(0, prefix);

			if (subProgram->mCompileUnit->mDbgModule->mHotIdx != 0)
				methodName += StrFormat("\t%d", subProgram->mCompileUnit->mDbgModule->mHotIdx);
			mDebugger->mHotResolveData->mBeefCallStackEntries.Add(methodName);
		}
	}
}

void DbgHotScanner::PopulateHotCallstacks()
{
	auto prevActiveThread = mDebugger->mActiveThread;
	for (auto threadInfo : mDebugger->mThreadList)
	{
		mDebugger->mActiveThread = threadInfo;
		mDebugger->ClearCallStack();
		mDebugger->UpdateCallStack(false);

		for (int stackFrameIdx = 0; stackFrameIdx < (int)mDebugger->mCallStack.size(); stackFrameIdx++)
			mDebugger->UpdateCallStackMethod(stackFrameIdx);

		for (int stackFrameIdx = 0; stackFrameIdx < (int)mDebugger->mCallStack.size(); stackFrameIdx++)
		{
			auto stackFrame = mDebugger->mCallStack[stackFrameIdx];
			if ((stackFrame->mSubProgram != NULL) && (stackFrame->mSubProgram->GetLanguage() == DbgLanguage_Beef))
			{
				auto subProgram = stackFrame->mSubProgram;

				if (subProgram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced)
					subProgram = mDebugger->TryFollowHotJump(subProgram, stackFrame->mRegisters.GetPC());

				AddSubProgram(subProgram, false, "");
			}
		}
	}

	mDebugger->mActiveThread = prevActiveThread;
	mDebugger->ClearCallStack();
}

void DbgHotScanner::ScanSpan(TCFake::Span* span, int expectedStartPage, int memKind)
{
	if (span->location != TCFake::Span::IN_USE)
		return;

	if (span->start != expectedStartPage)
	{
		return;
	}

	intptr pageSize = (intptr)1 << kPageShift;
	int spanSize = pageSize * span->length;
	addr_target spanStart = ((addr_target)span->start << kPageShift);

	if (spanSize > mScanData.size())
	{
		mScanData.Resize(spanSize);
	}
	void* spanPtr = &mScanData[0];
	mDebugger->ReadMemory(spanStart, spanSize, spanPtr);
	void* spanEnd = (void*)((intptr)spanPtr + spanSize);

	//BF_LOGASSERT((spanStart >= TCFake::PageHeap::sAddressStart) && (spanEnd <= TCFake::PageHeap::sAddressEnd));

	int elementSize = mDbgGCData.mSizeClasses[span->sizeclass];
	if (elementSize == 0)
		elementSize = spanSize;
	//BF_LOGASSERT(elementSize >= sizeof(bf::System::Object));

	auto _MarkTypeUsed = [&](int typeId, intptr size)
	{
		if (typeId < 0)
			return;
		while (mDebugger->mHotResolveData->mTypeData.size() <= typeId)
			mDebugger->mHotResolveData->mTypeData.Add(DbgHotResolveData::TypeData());
		auto& typeData = mDebugger->mHotResolveData->mTypeData[typeId];
		typeData.mSize += size;
		typeData.mCount++;
	};

	int objectSize = ((mDbgGCData.mDbgFlags & BfRtFlags_ObjectHasDebugFlags) != 0) ? sizeof(addr_target)*2 : sizeof(addr_target);

	mDebugger->mDebugTarget->GetCompilerSettings();
	if (mDebugger->mDebugTarget->mBfObjectSize != 0)
		objectSize = mDebugger->mDebugTarget->mBfObjectSize;

	while (spanPtr <= (uint8*)spanEnd - elementSize)
	{
		addr_target classVDataAddr = 0;
		if (memKind == 0)
		{
			// Obj
			Fake_BfObject_WithFlags* obj = (Fake_BfObject_WithFlags*)spanPtr;
			if (obj->mAllocCheckPtr != 0)
			{
				int objectFlags = obj->mObjectFlags;
				if ((objectFlags & BF_OBJECTFLAG_DELETED) == 0)
				{
					classVDataAddr = obj->mClassVData & ~0xFF;
				}
			}
		}
		else
		{
			// Raw
			addr_target rawAllocDataAddr = *(addr_target*)((uint8*)spanPtr + elementSize - sizeof(addr_target));
			if (rawAllocDataAddr != 0)
			{
				if (rawAllocDataAddr == mDbgGCData.mRawObjectSentinel)
				{
					classVDataAddr = *((addr_target*)spanPtr);
					if ((mDbgGCData.mDbgFlags & BfRtFlags_ObjectHasDebugFlags) != 0)
						classVDataAddr = classVDataAddr & ~0xFF;
				}
				else
				{
					int* rawTypeIdPtr = NULL;
					if (mFoundRawAllocDataAddrs.TryAdd(rawAllocDataAddr, NULL, &rawTypeIdPtr))
					{
						*rawTypeIdPtr = -1;
						Fake_DbgRawAllocData rawAllocData = mDebugger->ReadMemory<Fake_DbgRawAllocData>(rawAllocDataAddr);
						if (rawAllocData.mType != NULL)
						{
							int* typeAddrIdPtr = NULL;
							if (mFoundTypeAddrs.TryAdd(rawAllocData.mType, NULL, &typeAddrIdPtr))
							{
								*typeAddrIdPtr = -1;
								Fake_Type_Data typeData;
								if (mDebugger->ReadMemory(rawAllocData.mType + objectSize, sizeof(typeData), &typeData))
								{
									*typeAddrIdPtr = typeData.mTypeId;
									*rawTypeIdPtr = typeData.mTypeId;
									_MarkTypeUsed(typeData.mTypeId, elementSize);
								}
							}
							else
							{
								_MarkTypeUsed(*typeAddrIdPtr, elementSize);
							}
						}
					}
					else
					{
						_MarkTypeUsed(*rawTypeIdPtr, elementSize);
					}
				}
			}
		}

		if (classVDataAddr != 0)
		{
			int* typeIdPtr = NULL;
			if (mFoundClassVDataAddrs.TryAdd(classVDataAddr, NULL, &typeIdPtr))
			{
				*typeIdPtr = -1;

				int typeId = mDebugger->ReadMemory<int32>(classVDataAddr);
				if (typeId >= 0)
				{
					Fake_Type_Data typeData;
					bool foundType = false;
					for (auto typesInfoAddr : mTypeInfoAddrs)
					{
						// Hot type tables only contain entries for the types emitted during that hot
						//  compile, and older tables may be too small for newer typeIds, so on a null
						//  or mismatched entry we keep looking in older tables
						addr_target arrayAddr = typesInfoAddr + typeId * sizeof(addr_target);
						addr_target typeAddr = mDebugger->ReadMemory<addr_target>(arrayAddr);
						if (typeAddr == 0)
							continue;
						memset(&typeData, 0, sizeof(typeData));
						if (!mDebugger->ReadMemory(typeAddr + objectSize, sizeof(typeData), &typeData))
							continue;
						if (typeData.mTypeId != typeId)
							continue;
						foundType = true;
						break;
					}

					if (foundType)
					{
						*typeIdPtr = typeData.mTypeId;
						_MarkTypeUsed(typeData.mTypeId, elementSize);
						if ((typeData.mTypeFlags & BfTypeFlags_Delegate) != 0)
						{
							Fake_Delegate_Data* dlg = (Fake_Delegate_Data*)((uint8*)spanPtr + objectSize);
							if (mFoundFuncPtrs.Add(dlg->mFuncPtr))
							{
								auto subProgram = mDebugger->mDebugTarget->FindSubProgram(dlg->mFuncPtr, DbgOnDemandKind_None);
								if ((subProgram != NULL) && (subProgram->GetLanguage() == DbgLanguage_Beef))
									AddSubProgram(subProgram, true, "D ");
							}
						}
					}
				}
			}
			else
			{
				_MarkTypeUsed(*typeIdPtr, elementSize);
			}
		}

		spanPtr = (void*)((intptr)spanPtr + elementSize);
	}
}

void DbgHotScanner::ScanRoot(addr_target rootPtr, int memKind)
{
	mDebugger->ReadMemory(rootPtr, sizeof(mRoot), &mRoot);

#ifdef BF_DBG_32
	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		addr_target nodeAddr = mRoot.ptrs[rootIdx];
		if (nodeAddr == 0)
			continue;
		mDebugger->ReadMemory(nodeAddr, sizeof(mNode), &mNode);
		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{
			addr_target spanAddr = mNode.ptrs[leafIdx];
			if (spanAddr == 0)
				continue;

			int expectedStartPage = (rootIdx * PageHeap::PageMap::LEAF_LENGTH) + leafIdx;
			TCFake::Span span;
			mDebugger->ReadMemory(spanAddr, sizeof(span), &span);
			ScanSpan(&span, expectedStartPage, memKind);
		}
	}
#else
	int bits = PageHeap::PageMap::BITS;
	int interiorLen = PageHeap::PageMap::INTERIOR_LENGTH;
	int leafLen = PageHeap::PageMap::LEAF_LENGTH;

	for (int pageIdx1 = 0; pageIdx1 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx1++)
	{
		addr_target node1Addr = mRoot.ptrs[pageIdx1];
		if (node1Addr == 0)
			continue;
		mDebugger->ReadMemory(node1Addr, sizeof(mNode1), &mNode1);
		for (int pageIdx2 = 0; pageIdx2 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx2++)
		{
			addr_target node2Addr = mNode1.ptrs[pageIdx2];
			if (node2Addr == 0)
				continue;
			mDebugger->ReadMemory(node2Addr, sizeof(mNode2), &mNode2);
			for (int pageIdx3 = 0; pageIdx3 < PageHeap::PageMap::LEAF_LENGTH; pageIdx3++)
			{
				addr_target spanAddr = mNode2.ptrs[pageIdx3];
				if (spanAddr == 0)
					continue;

				int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;
				TCFake::Span span;
				mDebugger->ReadMemory(spanAddr, sizeof(span), &span);
				ScanSpan(&span, expectedStartPage, memKind);
			}
		}
	}
#endif
}

void DbgHotScanner::Scan(DbgHotResolveFlags flags)
{
	auto prevRunState = mDebugger->mRunState;
	if ((mDebugger->mRunState == RunState_Running) && ((flags & DbgHotResolveFlag_KeepThreadState) == 0))
	{
		mDebugger->ThreadRestorePause(NULL, NULL);
		mDebugger->mRunState = RunState_Paused;
	}

	if ((flags & DbgHotResolveFlag_ActiveMethods) != 0)
		PopulateHotCallstacks();

	if ((flags & DbgHotResolveFlag_Allocations) != 0)
	{
		addr_target gcDbgDataAddr = 0;

		for (auto module : mDebugger->mDebugTarget->mDbgModules)
		{
			if ((module->mFilePath.Contains("Beef")) && (module->mFilePath.Contains("Dbg")))
			{
				module->ParseTypeData();
				module->ParseSymbolData();
				auto entry = module->mSymbolNameMap.Find("gGCDbgData");
				if ((entry != NULL) && (entry->mValue != NULL))
					gcDbgDataAddr = entry->mValue->mAddress;
			}
		}

		auto module = mDebugger->mDebugTarget->mTargetBinary;
		if (module->mBfTypesInfoAddr == 0)
		{
			module->mBfTypesInfoAddr = -1;
			auto typeTypeEntry = module->FindType("System.Type", DbgLanguage_Beef);
			if ((typeTypeEntry != NULL) && (typeTypeEntry->mValue != NULL))
			{
				auto typeType = typeTypeEntry->mValue;
				module->mBfTypeType = typeType;
				if (typeType->mNeedsGlobalsPopulated)
					typeType->mCompileUnit->mDbgModule->PopulateTypeGlobals(typeType);

				for (auto member : typeType->mMemberList)
				{
					if ((member->mIsStatic) && (member->mName != NULL) && (strcmp(member->mName, "sTypes") == 0) && (member->mLocationData != NULL))
					{
						DbgAddrType addrType;
						module->mBfTypesInfoAddr = member->mCompileUnit->mDbgModule->EvaluateLocation(NULL, member->mLocationData, member->mLocationLen, NULL, &addrType);
					}
				}

				if (module->mBfTypesInfoAddr <= 0)
				{
					auto entry = module->mSymbolNameMap.Find(
#ifdef BF_DBG_64
						"?sTypes@Type@System@bf@@2PEAPEAV123@A"
#else
						"?sTypes@Type@System@bf@@2PAPAV123@A"
#endif
					);

					if (entry)
						module->mBfTypesInfoAddr = entry->mValue->mAddress;
				}
			}
		}
		mBfTypesInfoAddr = module->mBfTypesInfoAddr;

		// Hot-loaded vdata modules contain new type tables which hold entries for the types emitted
		//  during that hot compile (including types the original binary doesn't know about), so check
		//  those newest-first before falling back to the original binary's table
		mTypeInfoAddrs.Clear();
		for (int moduleIdx = (int)mDebugger->mDebugTarget->mDbgModules.size() - 1; moduleIdx >= 0; moduleIdx--)
		{
			auto dbgModule = mDebugger->mDebugTarget->mDbgModules[moduleIdx];
			if ((dbgModule->IsHotSwapObjectFile()) && (dbgModule->mBfTypesInfoAddr > 0))
				mTypeInfoAddrs.Add((addr_target)dbgModule->mBfTypesInfoAddr);
		}
		if (mBfTypesInfoAddr > 0)
			mTypeInfoAddrs.Add(mBfTypesInfoAddr);

		if (gcDbgDataAddr == 0)
		{
			// We couldn't locate the GC debug data (ie: the PDB for the Beef debug runtime failed to load,
			//  possibly from a mismatched or missing PDB), so no allocation scanning can be performed.
			//  We can't just return here - we must fall through so the paused threads and the run state
			//  get restored below, otherwise every target thread would be left permanently suspended with
			//  the debugger reporting a phantom pause at the active thread's current (kernel) location.
			BfLogDbg("DbgHotScanner::Scan failed to locate gGCDbgData, allocation scan skipped\n");
			mDebugger->OutputMessage("WARNING: The debugger could not locate 'gGCDbgData' in the Beef debug runtime so no scan for live allocations could be performed. "
				"This may indicate a mismatched or missing PDB for the debug runtime DLL.\n");
		}
		else
		{
			bool success = mDebugger->ReadMemory(gcDbgDataAddr, sizeof(DbgGCData), &mDbgGCData);
			if (!success)
			{
				BF_ASSERT("Failed to read DbgGCData");
				success = mDebugger->ReadMemory(gcDbgDataAddr, sizeof(DbgGCData), &mDbgGCData);
			}
			BF_ASSERT(mDbgGCData.mObjRootPtr != NULL);
			BF_ASSERT(mDbgGCData.mRawRootPtr != NULL);
			if (mDbgGCData.mObjRootPtr != NULL)
				ScanRoot(mDbgGCData.mObjRootPtr, 0);
			if (mDbgGCData.mRawRootPtr != NULL)
				ScanRoot(mDbgGCData.mRawRootPtr, 1);
		}
	}

	if ((prevRunState == RunState_Running) && ((flags & DbgHotResolveFlag_KeepThreadState) == 0))
	{
		mDebugger->ThreadRestoreUnpause();
		mDebugger->mRunState = prevRunState;
	}
}