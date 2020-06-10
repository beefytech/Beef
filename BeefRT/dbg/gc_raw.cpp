#define _WIN32_WINNT _WIN32_WINNT_WIN8

// This spits out interesting stats periodically to the console
#define BF_GC_PRINTSTATS

// Disable efficient new mass-freeing in TCMalloc
//#define BF_GC_USE_OLD_FREE

#ifdef BF_DEBUG
// Useful for tracing down memory corruption -- memory isn't returned to TCMalloc, it's just marked as freed.  You'll run out eventually
//#define BF_NO_FREE_MEMORY
// Old and not too useful
//#define BG_GC_TRACKPTRS
#endif

#if defined BF_OBJECT_TRACK_ALLOCNUM && defined BF_NO_FREE_MEMORY
//#define BF_GC_VERIFY_SWEEP_IDS
#endif

//#define BF_SECTION_NURSERY

#ifdef BF_PLATFORM_WINDOWS
#include <direct.h>
#endif

#include "gc.h"

bf::System::DbgRawAllocData sEmptyAllocData = { 0 };
bf::System::DbgRawAllocData sObjectAllocData = { 0 };


#ifdef BF_GC_SUPPORTED

#include <fstream>
#include "BeefySysLib/Common.h"
#include "BeefySysLib/BFApp.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/Deque.h"
#include <unordered_set>
#include "../rt/BfObjects.h"
#include "../rt/Thread.h"
#include <map>

#define TCMALLOC_NO_MALLOCGUARD
#define TCMALLOC_NAMESPACE tcmalloc_raw
#define TCMALLOC_EXTERN static
#include "gperftools/src/tcmalloc.cc"

using namespace tcmalloc_raw;
using namespace Beefy;

struct DeferredFreeEntry
{
	bf::System::Object* mObject;
	intptr mAllocSize;
};

static Beefy::Deque<DeferredFreeEntry> gDeferredFrees;
static intptr gRawAllocSize = 0;
static intptr gMaxRawAllocSize = 0;
static intptr gDeferredObjectFreeSize = 0;

void BFGC::RawInit()
{
	if (UNLIKELY(Static::pageheap() == NULL)) ThreadCache::InitModule();
	ThreadCache::InitTSD();
	gGCDbgData.mRawRootPtr = Static::pageheap()->pagemap_.root_;
	gGCDbgData.mRawObjectSentinel = &sObjectAllocData;
}

void BFGC::RawMarkSpan(tcmalloc_raw::Span* span, int expectedStartPage)
{
	if ((gBfRtDbgFlags & BfRtFlags_ObjectHasDebugFlags) == 0)
		return;

	if (span->location != tcmalloc_raw::Span::IN_USE)
		return;

	if (span->start != expectedStartPage)
	{
		// This check covers when a new multi-page span is being put into place
		//  and we catch after the first block, and it also catches the case
		//  when the allocator splits a span and the pagemap can hold a reference
		//  to a span that no longer covers that location.  					
		//  For both of these cases we  ignore the span.  Remember, the worst case 
		//  here is that we'll miss a sweep of an object, which would just delay it's 
		//  cleanup until next GC cycle.  Because the GC is the sole freer of spans,
		//  there can never be a case where we find a valid span and then the span
		//  changes sizeclass or location before we can scan the memory it points to.
		//
		// This also covers the case where a page spans over a radix map section and
		//  we catch it on an outer loop again
		return;
	}

	intptr pageSize = (intptr)1 << kPageShift;
	intptr spanSize = pageSize * span->length;
	void* spanStart = (void*)((intptr)span->start << kPageShift);
	void* spanEnd = (void*)((intptr)spanStart + spanSize);
	void* spanPtr = spanStart;

	BF_LOGASSERT((spanStart >= tcmalloc_raw::PageHeap::sAddressStart) && (spanEnd <= tcmalloc_raw::PageHeap::sAddressEnd));

	intptr elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
	if (elementSize == 0)
		elementSize = spanSize;
	BF_LOGASSERT(elementSize >= sizeof(bf::System::Object));

	while (spanPtr <= (uint8*)spanEnd - elementSize)
	{
		bf::System::DbgRawAllocData* rawAllocData = *(bf::System::DbgRawAllocData**)((uint8*)spanPtr + elementSize - sizeof(intptr));
		if (rawAllocData != NULL)
		{	
			if (rawAllocData->mMarkFunc != NULL)
			{	
				intptr extraDataSize = sizeof(intptr);
				if (rawAllocData->mMaxStackTrace == 1)
				{
					extraDataSize += sizeof(intptr);					
				}
				else if (rawAllocData->mMaxStackTrace > 1)
				{
					intptr stackTraceCount = *(intptr*)((uint8*)spanPtr + elementSize - sizeof(intptr) - sizeof(intptr));					
					extraDataSize += (1 + stackTraceCount) * sizeof(intptr);
				}

				struct MarkTarget
				{
				};
				typedef void(MarkTarget::*MarkFunc)();
				MarkFunc markFunc = *(MarkFunc*)&rawAllocData->mMarkFunc;
				 
				// It's possible we can overestimate elemCount, particularly for large allocations. This doesn't cause a problem
				//  because we can safely mark on complete random memory -- pointer values are always validated before being followed
				intptr elemStride = BF_ALIGN(rawAllocData->mType->mSize, rawAllocData->mType->mAlign);
				if (elemStride > 0)
				{
					intptr dataSize = elementSize - extraDataSize;
					intptr elemCount = dataSize / elemStride;
					for (intptr elemIdx = 0; elemIdx < elemCount; elemIdx++)
					{
						(((MarkTarget*)((uint8*)spanPtr + elemIdx * elemStride))->*markFunc)();
					}
				}
			}
		}

		spanPtr = (void*)((intptr)spanPtr + elementSize);
	}
}

void BFGC::RawMarkAll()
{
	//BP_ZONE("Sweep");

	mCurLiveObjectCount = 0;

#ifdef BF_GC_VERIFY_SWEEP_IDS
	maxAllocNum = bf::System::Object::sCurAllocNum;
	allocIdSet.clear();
#endif

	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

	intptr leafCheckCount = 0;

#ifdef BF32
	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[rootIdx];
		if (rootLeaf == NULL)
			continue;

		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{
			leafCheckCount++;

			tcmalloc_raw::Span* span = (tcmalloc_raw::Span*)rootLeaf->values[leafIdx];
			if (span != NULL)
			{
				int expectedStartPage = (rootIdx * PageHeap::PageMap::LEAF_LENGTH) + leafIdx;
				RawMarkSpan(span, expectedStartPage);
				// We may be tempted to advance by span->length here, BUT
				// let us just scan all leafs becuause span data is
				// sometimes invalid and a long invalid span can cause
				// us to skip over an actual valid span
			}
		}
	}
#else
	for (int pageIdx1 = 0; pageIdx1 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx1++)
	{		
		PageHeap::PageMap::Node* node1 = pageHeap->pagemap_.root_->ptrs[pageIdx1];
		if (node1 == NULL)
			continue;
		for (int pageIdx2 = 0; pageIdx2 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx2++)
		{
			PageHeap::PageMap::Node* node2 = node1->ptrs[pageIdx2];
			if (node2 == NULL)
				continue;
			for (int pageIdx3 = 0; pageIdx3 < PageHeap::PageMap::LEAF_LENGTH; pageIdx3++)
			{
				leafCheckCount++;

				tcmalloc_raw::Span* span = (tcmalloc_raw::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
				{
					int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;
					RawMarkSpan(span, expectedStartPage);
					// We may be tempted to advance by span->length here, BUT
					// let us just scan all leafs becuause span data is
					// sometimes invalid and a long invalid span can cause
					// us to skip over an actual valid span
				}
			}
		}
	}
#endif
}

void BFGC::RawReportHandleSpan(tcmalloc_raw::Span* span, int expectedStartPage, int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>* sizeMap)
{
	if (span->location != tcmalloc_raw::Span::IN_USE)
		return;

	if (span->start != expectedStartPage)
	{
		return;
	}

	intptr pageSize = (intptr)1 << kPageShift;
	intptr spanSize = pageSize * span->length;
	void* spanStart = (void*)((intptr)span->start << kPageShift);
	void* spanEnd = (void*)((intptr)spanStart + spanSize);
	void* spanPtr = spanStart;

	BF_LOGASSERT((spanStart >= tcmalloc_raw::PageHeap::sAddressStart) && (spanEnd <= tcmalloc_raw::PageHeap::sAddressEnd));

	intptr elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
	if (elementSize == 0)
		elementSize = spanSize;		

	while (spanPtr <= (uint8*)spanEnd - elementSize)
	{
		bf::System::DbgRawAllocData* rawAllocData = *(bf::System::DbgRawAllocData**)((uint8*)spanPtr + elementSize - sizeof(intptr));
		if (rawAllocData != NULL)
		{
			bf::System::Type* type = rawAllocData->mType;

			AllocInfo* sizePtr = NULL;

			if (sizeMap == NULL)
			{
				intptr extraDataSize = sizeof(intptr);

				RawLeakInfo rawLeakInfo;
				rawLeakInfo.mRawAllocData = rawAllocData;
				rawLeakInfo.mDataPtr = spanPtr;
				
				if (rawAllocData->mMaxStackTrace == 1)
				{
					extraDataSize += sizeof(intptr);
					rawLeakInfo.mStackTraceCount = 1;
					rawLeakInfo.mStackTracePtr = (uint8*)spanPtr + elementSize - sizeof(intptr) - sizeof(intptr);
				}
				else if (rawAllocData->mMaxStackTrace > 1)
				{
					rawLeakInfo.mStackTraceCount = *(intptr*)((uint8*)spanPtr + elementSize - sizeof(intptr) - sizeof(intptr));
					rawLeakInfo.mStackTracePtr = (uint8*)spanPtr + elementSize - sizeof(intptr) - sizeof(intptr) - rawLeakInfo.mStackTraceCount * sizeof(intptr);
					extraDataSize += (1 + rawLeakInfo.mStackTraceCount) * sizeof(intptr);
				}
				else
				{
					rawLeakInfo.mStackTraceCount = 0;
					rawLeakInfo.mStackTracePtr = NULL;					
				}
				
				if (rawAllocData->mType != NULL)
				{
					intptr typeSize;
					if ((gBfRtDbgFlags & BfRtFlags_ObjectHasDebugFlags) != 0)
						typeSize = rawAllocData->mType->mSize;
					else
						typeSize = ((bf::System::Type_NOFLAGS*)rawAllocData->mType)->mSize;
					if (typeSize > 0)
						rawLeakInfo.mDataCount = (elementSize - extraDataSize) / typeSize;
				}
				else
					rawLeakInfo.mDataCount = 1;

				mSweepInfo.mRawLeaks.Add(rawLeakInfo);
				mSweepInfo.mLeakCount++;
			}
			else
			{
				(*sizeMap).TryAdd(type, NULL, &sizePtr);
				sizePtr->mRawSize += elementSize;
				sizePtr->mRawCount++;
			}
			objectCount++;
		}
		else
		{
			freeSize += elementSize;
		}

		// Do other stuff
		spanPtr = (void*)((intptr)spanPtr + elementSize);
	}
}

void BFGC::RawReportScan(int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>* sizeMap)
{
	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

#ifdef BF32
	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[rootIdx];
		if (rootLeaf == NULL)
			continue;

		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{			
			tcmalloc_raw::Span* span = (tcmalloc_raw::Span*)rootLeaf->values[leafIdx];
			if (span != NULL)
			{				
				int expectedStartPage = (rootIdx * PageHeap::PageMap::LEAF_LENGTH) + leafIdx;
				RawReportHandleSpan(span, expectedStartPage, objectCount, freeSize, sizeMap);
				// We may be tempted to advance by span->length here, BUT
				// let us just scan all leafs becuause span data is
				// sometimes invalid and a long invalid span can cause
				// us to skip over an actual valid span
			}
		}
	}
#else	
	for (int pageIdx1 = 0; pageIdx1 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx1++)
	{
		PageHeap::PageMap::Node* node1 = Static::pageheap()->pagemap_.root_->ptrs[pageIdx1];
		if (node1 == NULL)
			continue;
		for (int pageIdx2 = 0; pageIdx2 < PageHeap::PageMap::INTERIOR_LENGTH; pageIdx2++)
		{
			PageHeap::PageMap::Node* node2 = node1->ptrs[pageIdx2];
			if (node2 == NULL)
				continue;
			for (int pageIdx3 = 0; pageIdx3 < PageHeap::PageMap::LEAF_LENGTH; pageIdx3++)
			{				
				tcmalloc_raw::Span* span = (tcmalloc_raw::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
				{										
					int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;
					RawReportHandleSpan(span, expectedStartPage, objectCount, freeSize, sizeMap);
					// We may be tempted to advance by span->length here, BUT
					// let us just scan all leafs because span data is
					// sometimes invalid and a long invalid span can cause
					// us to skip over an actual valid span
				}
			}
		}
	}
#endif
}

void BFGC::RawReport(String& msg, intptr& freeSize, std::multimap<AllocInfo, bf::System::Type*>& orderedSizeMap)
{
	BP_ZONE("RawReport");
	
	int objectCount = 0;

#ifdef BF_GC_VERIFY_SWEEP_IDS
	maxAllocNum = bf::System::Object::sCurAllocNum;
	allocIdSet.clear();
#endif

	intptr leafCheckCount = 0;
	bool overflowed = false;

	Beefy::Dictionary<bf::System::Type*, AllocInfo> sizeMap;

	RawReportScan(objectCount, freeSize, &sizeMap);	
	for (auto& pair : sizeMap)
	{		
		orderedSizeMap.insert(std::make_pair(pair.mValue, pair.mKey));
	}
	
	msg += Beefy::StrFormat("  Live Non-Objects                                               %d\n", objectCount);
}

extern Beefy::StringT<0> gDbgErrorString;

void BFGC::RawShutdown()
{	
	BF_ASSERT(!mRunning);

	while (!gDeferredFrees.IsEmpty())
	{
		DeferredFreeEntry entry = gDeferredFrees.PopBack();
		gDeferredObjectFreeSize -= entry.mAllocSize;		
		tc_free(entry.mObject);
	}
	BF_ASSERT(gDeferredObjectFreeSize == 0);

	int objectCount = 0;
	intptr freeSize = 0;	
	mSweepInfo.mLeakCount = 0;
	RawReportScan(objectCount, freeSize, NULL);	

	if (mSweepInfo.mLeakCount > 0)
	{
		Beefy::String errorStr = StrFormat("%d raw memory leak%s detected, details in Output panel.",
			mSweepInfo.mLeakCount, (mSweepInfo.mLeakCount != 1) ? "s" : "");
		gDbgErrorString = errorStr;
		gDbgErrorString += "\n";

		for (auto& rawLeak : mSweepInfo.mRawLeaks)
		{
			Beefy::String typeName;
			if (rawLeak.mRawAllocData->mType != NULL)
				typeName = rawLeak.mRawAllocData->mType->GetFullName() + "*";
			else if (rawLeak.mRawAllocData == &sObjectAllocData)
				typeName = "System.Object";			
			else
				typeName = "uint8*";
			errorStr += "\x1";
			String leakStr = StrFormat("(%s)0x%@", typeName.c_str(), rawLeak.mDataPtr);
			if (rawLeak.mDataCount > 1)
				leakStr += StrFormat(",%d", rawLeak.mDataCount);

			errorStr += StrFormat("LEAK\t%s\n", leakStr.c_str());
			errorStr += StrFormat("   %s\n", leakStr.c_str());

			if (rawLeak.mStackTraceCount > 0)
			{
				errorStr += "\x1";
				errorStr += StrFormat("LEAK\t(System.CallStackAddr*)0x%@", rawLeak.mStackTracePtr);
				if (rawLeak.mStackTraceCount == 1)
					errorStr += StrFormat(", nm");
				else
					errorStr += StrFormat(", %d, na", rawLeak.mStackTraceCount);
				errorStr += StrFormat("\n    [AllocStackTrace]\n");
			}

			if (gDbgErrorString.length() < 256)
				gDbgErrorString += StrFormat("   %s\n", leakStr.c_str());
		}

		BF_ASSERT(mSweepInfo.mLeakCount > 0);

		gBfRtDbgCallbacks.SetErrorString(gDbgErrorString.c_str());
		gBfRtDbgCallbacks.DebugMessageData_SetupError(errorStr.c_str(), 0);
		BF_DEBUG_BREAK();
	}
	else
	{
		BF_ASSERT(gRawAllocSize == 0);
	}
}

 inline void* BF_do_malloc_pages(ThreadCache* heap, size_t& size)
 {
 	void* result;
 	bool report_large;
  	
 	heap->requested_bytes_ += size;
 	Length num_pages = TCMALLOC_NAMESPACE::pages(size);
 	size = num_pages << kPageShift;
 
 	if ((TCMALLOC_NAMESPACE::FLAGS_tcmalloc_sample_parameter > 0) && heap->SampleAllocation(size)) {
 		result = DoSampledAllocation(size);
 
 		SpinLockHolder h(Static::pageheap_lock());
 		report_large = should_report_large(num_pages);
 	}
 	else {
 		SpinLockHolder h(Static::pageheap_lock());
 		Span* span = Static::pageheap()->New(num_pages);
 		result = (UNLIKELY(span == NULL) ? NULL : SpanToMallocResult(span));
 		report_large = should_report_large(num_pages);
 	}
 
 	if (report_large) {
 		ReportLargeAlloc(num_pages, result);
 	}
  	
 	return result;
 }
 
 inline void* BF_do_malloc_small(ThreadCache* heap, size_t& size)
 {
 	if (size == 0)	
 		size = (int)sizeof(void*);	
 	
 	ASSERT(Static::IsInited());
 	ASSERT(heap != NULL);
 	heap->requested_bytes_ += size;
 	size_t cl = Static::sizemap()->SizeClass(size);
 	size = Static::sizemap()->class_to_size(cl);	
 
 	void* result;
 	if ((TCMALLOC_NAMESPACE::FLAGS_tcmalloc_sample_parameter > 0) && heap->SampleAllocation(size)) {
 		result = DoSampledAllocation(size);
 	}
 	else {
 		// The common case, and also the simplest.  This just pops the
 		// size-appropriate freelist, after replenishing it if it's empty.
 		result = CheckedMallocResult(heap->Allocate(size, cl));
 	}	
 
 	return result;
 }

void* BfRawAllocate(intptr size, bf::System::DbgRawAllocData* rawAllocData, void* stackTraceInfo, int stackTraceCount)
{		
	size_t totalSize = size;	
	totalSize += sizeof(intptr);
	if (rawAllocData->mMaxStackTrace == 1)
		totalSize += sizeof(intptr);
	else if (rawAllocData->mMaxStackTrace > 1)	
		totalSize += (1 + stackTraceCount) * sizeof(intptr);

	void* result;
 	if (ThreadCache::have_tls &&
 		LIKELY(totalSize < ThreadCache::MinSizeForSlowPath()))
 	{
 		result = BF_do_malloc_small(ThreadCache::GetCacheWhichMustBePresent(), totalSize);
 	}
 	else if (totalSize <= kMaxSize)
 	{
 		result = BF_do_malloc_small(ThreadCache::GetCache(), totalSize);
 	}
 	else 
 	{
 		result = BF_do_malloc_pages(ThreadCache::GetCache(), totalSize);
 	}
	
	if (rawAllocData->mMaxStackTrace == 1)
	{
		memcpy((uint8*)result + totalSize - sizeof(intptr) - sizeof(intptr), stackTraceInfo, sizeof(intptr));
	}
	else if (rawAllocData->mMaxStackTrace > 1)
	{
		memcpy((uint8*)result + totalSize - sizeof(intptr) - sizeof(intptr), &stackTraceCount, sizeof(intptr));
		memcpy((uint8*)result + totalSize - sizeof(intptr) - sizeof(intptr) - stackTraceCount*sizeof(intptr), stackTraceInfo, stackTraceCount*sizeof(intptr));
	}

	memcpy((uint8*)result + totalSize - sizeof(intptr), &rawAllocData, sizeof(intptr));
	
	BfpSystem_InterlockedExchangeAdd32((uint32*)&gRawAllocSize, (uint32)totalSize);

	return result;
}

void BfRawFree(void* ptr)
{	
	const PageID p = reinterpret_cast<uintptr_t>(ptr) >> kPageShift;	
	size_t cl = Static::pageheap()->GetSizeClassIfCached(p);
	intptr allocSize = 0;
	if (cl == 0)
	{		
		auto span = Static::pageheap()->GetDescriptor(p);
		if (span != NULL)
		{
			cl = span->sizeclass;
			if (cl == 0)
			{
				allocSize = span->length << kPageShift;
			}
		}
	}
	if (cl != 0)
		allocSize = Static::sizemap()->class_to_size(cl);

	if (allocSize == 0)
	{
		Beefy::String err = Beefy::StrFormat("Memory deallocation requested at invalid address %@", ptr);
		BF_FATAL(err);		
	}

	if (allocSize != 0)
	{	
		BfpSystem_InterlockedExchangeAdd32((uint32*)&gRawAllocSize, (uint32)-allocSize);

		// Clear out the dbg alloc data at the end		
		void** dbgAllocDataAddr = (void**)((uint8*)ptr + allocSize - sizeof(void*));
		if (*dbgAllocDataAddr == 0)
		{
			Beefy::String err = Beefy::StrFormat("Memory deallocation requested at %@ but no allocation is recorded. Double delete?", ptr);
			BF_FATAL(err);
		}
		else if ((*dbgAllocDataAddr == &sObjectAllocData) && ((gBFGC.mMaxRawDeferredObjectFreePercentage != 0) || (!gDeferredFrees.IsEmpty())))
		{
			*dbgAllocDataAddr = NULL;

			AutoCrit autoCrit(gBFGC.mCritSect);
			gMaxRawAllocSize = BF_MAX(gMaxRawAllocSize, gRawAllocSize);
			gDeferredObjectFreeSize += allocSize;
			
			DeferredFreeEntry entry;
			entry.mObject = (bf::System::Object*)ptr;
			entry.mAllocSize = allocSize;
			gDeferredFrees.Add(entry);

			intptr maxDeferredSize = gMaxRawAllocSize * gBFGC.mMaxRawDeferredObjectFreePercentage / 100;
			while (gDeferredObjectFreeSize > maxDeferredSize)
			{
				DeferredFreeEntry entry = gDeferredFrees.PopBack();
				gDeferredObjectFreeSize -= entry.mAllocSize;
				memset(entry.mObject, 0xDD, allocSize - sizeof(void*));
				tc_free(entry.mObject);
			}

			return;
		}

		*dbgAllocDataAddr = NULL;
		BF_FULL_MEMORY_FENCE();
		memset(ptr, 0xDD, allocSize - sizeof(void*));
	}

	tc_free(ptr);
}

#else // BF_GC_SUPPORTED

void* BfRawAllocate(intptr size, bf::System::DbgRawAllocData* rawAllocData, void* stackTraceInfo, int stackTraceCount)
{
	return malloc(size);
}

void BfRawFree(void* ptr)
{
	free(ptr);
}

#endif