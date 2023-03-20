//#define BF_GC_DISABLED

//#define BF_GC_LOG_ENABLED

#define _WIN32_WINNT _WIN32_WINNT_WIN8

// This spits out interesting stats periodically to the console
#define BF_GC_PRINTSTATS

//TEMPORARY
//#define BF_GC_DEBUGSWEEP
//#define BF_GC_EMPTYSCAN

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

#ifdef BF_GC_SUPPORTED

#include <fstream>
#include "BeefySysLib/Common.h"
#include "BeefySysLib/BFApp.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/BinaryHeap.h"
#include <unordered_set>
#include "../rt/BfObjects.h"
#include "../rt/Thread.h"

#ifdef BF_PLATFORM_WINDOWS
#include <psapi.h>
#endif

#ifndef BF_TCMALLOC_DISABLED

#define TCMALLOC_NO_MALLOCGUARD
#define TCMALLOC_NAMESPACE tcmalloc_obj
#define TCMALLOC_EXTERN static
#include "gperftools/src/tcmalloc.cc"

#else
#define tc_malloc malloc
#define tc_free free
#endif

USING_NS_BF;

//System::Threading::Thread* gMainThread;

#include "BeefySysLib/util/PerfTimer.h"
/*#include "BeefyRtCPP/MonitorManager.h"
#include "BeefyRtCPP/InternalThread.h"
#include "BeefyRtCPP/ReflectionData.h"
#include "BeefyRtCPP/RtCommon_inl.h"
#include "BeefyRtCPP/BFArray.h"
#include "BeefyRtCPP/Reflection.h"*/

extern "C" GCDbgData gGCDbgData = { 0 };

void BfLog(const char* fmt ...)
{
	static int lineNum = 0;
	lineNum++;

	static FILE* fp = fopen("dbg_bf.txt", "wb");

	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

	aResult = StrFormat("%d ", lineNum) + aResult;

	fwrite(aResult.c_str(), 1, aResult.length(), fp);
	fflush(fp);
}

//////////////////////////////////////////////////////////////////////////

using Beefy::CritSect;

BF_TLS_DECLSPEC BFGC::ThreadInfo* BFGC::ThreadInfo::sCurThreadInfo;

HANDLE gGCHeap = 0;

#ifdef _DEBUG
bool gGCGetAllocStats = true;
#else
bool gGCGetAllocStats = false;
#endif

#ifdef BF_GC_VERIFY_SWEEP_IDS
	static std::set<int> allocIdSet;
	static int maxAllocNum = 0;
#endif

int gGCAllocCount = 0;
int gGCAllocBytes = 0;
std::vector<int> gGCAllocCountMap;
std::vector<int> gGCAllocSizeMap;

struct TCMallocRecord
{
	void* mPtr;
	int mSize;
};

static std::vector<TCMallocRecord> gTCMallocRecords;

void TCMalloc_RecordAlloc(void* ptr, int size)
{
	TCMallocRecord mallocRecord = { ptr, size };
	gTCMallocRecords.push_back(mallocRecord);
}

static void TCMalloc_FreeAllocs()
{
	for (auto& record : gTCMallocRecords)
	{
		::VirtualFree(record.mPtr, 0, MEM_RELEASE);
	}
	gTCMallocRecords.clear();
}

void PatchWindowsFunctions()
{
	// For TCMalloc, don't really patch
}

#ifdef BG_GC_TRACKPTRS
boost::unordered_set<void*> gTrackPtr;
#endif

void BFMarkThreadPoolJobs()
{

}

#ifndef BF_MONOTOUCH
void BFMarkCOMObjects()
{
}
void BFIMarkStackData(BFIThreadData* bfiThreadData)
{

}
#endif

/*void* sDbgPtr0 = NULL;
void* sDbgPtr1 = NULL;
void* sDbgPtr2 = NULL;*/

void BFGC::MarkMembers(bf::System::Object* obj)
{
	//BP_ZONE("MarkMembers");

	/*if ((obj == sDbgPtr0) || (obj == sDbgPtr1) || (obj == sDbgPtr2))
	{
		int a = 0;
	}*/
	if (((obj->mObjectFlags & BF_OBJECTFLAG_DELETED) != 0) && (!mMarkingDeleted))
	{
		mMarkingDeleted = true;
		gBfRtDbgCallbacks.Object_GCMarkMembers(obj);
		mMarkingDeleted = false;
	}
	else
	{
		gBfRtDbgCallbacks.Object_GCMarkMembers(obj);
	}
}

static void MarkObject(bf::System::Object* obj)
{
	if ((obj != NULL) && ((obj->mObjectFlags & (BF_OBJECTFLAG_MARK_ID_MASK)) != BFGC::sCurMarkId))
		gBFGC.MarkFromGCThread(obj);
}

//////////////////////////////////////////////////////////////////////////

//AtomicOps_x86CPUFeatureStruct AtomicOps_Internalx86CPUFeatures;

namespace tcmalloc
{
	extern "C" int RunningOnValgrind(void)
	{
		return 0;
	}
}

////////////////////////////////////////////

BFGC::ThreadInfo::~ThreadInfo()
{
	if (mThreadHandle != NULL)
		BfpThread_Release(mThreadHandle);
	if (mThreadInfo != NULL)
		BfpThreadInfo_Release(mThreadInfo);
}

bool BFGC::ThreadInfo::WantsSuspend()
{
#ifndef BP_DISABLED
	BfpThreadId threadId = BpManager::Get()->mThreadId;
	return threadId != (BfpThreadId)mThreadId;
#else
	return true;
#endif
}

void BFGC::ThreadInfo::CalcStackStart()
{
	intptr stackBase;
	int stackLimit;
	BfpThreadInfo_GetStackInfo(mThreadInfo, &stackBase, &stackLimit, BfpThreadInfoFlags_NoCache, NULL);
	mStackStart = stackBase;
}

//////////////////////////////////////////////////////////////////////////

#ifdef BF_GC_LOG_ENABLED
class GCLog
{
public:
	enum
	{
		EVENT_ALLOC,
		EVENT_GC_START, // cycle#
		EVENT_GC_UNFREEZE, // cycle#
		EVENT_MARK,
		EVENT_WB_MARK,
		EVENT_THREAD_STARTED,
		EVENT_THREAD_DONE,
		EVENT_SCAN_THREAD,
		EVENT_CONSERVATIVE_SCAN,
        EVENT_FOUND_TARGET,
        EVENT_FINALIZE_LIST,
		EVENT_LEAK,
		EVENT_DELETE,
		EVENT_FOUND,
		EVENT_FREE,
		EVENT_WB_MOVE,
        EVENT_WEAK_REF,
        EVENT_STRONG_REF,
        EVENT_WEAKREF_MARKED,
        EVENT_WEAK,
		EVENT_RESURRECT,
        EVENT_WAS_RESURRECT,
		EVENT_GC_DONE
	};

	class Entry
	{
	public:
		int mEvent;
		intptr mParam1;
		intptr mParam2;
		intptr mParam3;
	};

	Beefy::CritSect mCritSect;
	static const int BUFFSIZE = 4*1024*1024;
	static const int BUFFSIZE_MASK = BUFFSIZE-1;
	Entry mBuffer[BUFFSIZE];
	volatile int mHead;
	volatile int mTail;
	volatile bool mWriting;

public:
	GCLog()
	{
		mWriting = false;
	}

	void Log(int event, intptr param1 = 0, intptr param2 = 0, intptr param3 = 0)
	{
		if (mWriting)
		{
			// Not a strict guarantee, but should keep us from only messing up
			//  more than a single element of the log while we're writing it
			Beefy::AutoCrit autoCrit(mCritSect);
			return;
		}

		BF_FULL_MEMORY_FENCE();

		Entry entry = {event, param1, param2, param3};

		int prevHead;
		int nextHead;

		while (true)
		{
			prevHead = mHead;
			nextHead = (mHead + 1) & BUFFSIZE_MASK;

			if (nextHead == mTail)
			{
				int prevTail = mTail;
				int nextTail = (mTail + 1) & BUFFSIZE_MASK;
				if (::InterlockedCompareExchange((uint32*)&mTail, nextTail, prevTail) != prevTail)
					continue; // Try again
			}

			if (::InterlockedCompareExchange((uint32*)&mHead, nextHead, prevHead) == prevHead)
				break;
		}

		mBuffer[prevHead] = entry;
	}

	const char* GetNameStr(bf::System::Type* type)
	{
		static Beefy::String str;

		if (type == NULL)
			return "NULL";

		try
		{
			bf::System::Type* bfTypeRoot = (bf::System::Type*)type;
			str = type->GetFullName();
			return str.c_str();
		}
		catch (...)
		{
			return "<invalid>";
		}
	}

#pragma warning(disable:4477)

	void Write()
	{
		Beefy::AutoCrit autoCrit(mCritSect);

		wchar_t str[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, str);

		FILE* fp = fopen("c:\\temp\\gclog.txt", "w");

		mWriting = true;
		int sampleHead = mHead;
		BF_FULL_MEMORY_FENCE();
		int sampleTail = mTail;
		BF_FULL_MEMORY_FENCE();

		int pos = sampleTail;
		while (pos != sampleHead)
		{
			Entry ent = mBuffer[pos];

			switch (ent.mEvent)
			{
			case EVENT_ALLOC:
				fprintf(fp, "Alloc:%p Type:%s\n", ent.mParam1, GetNameStr((bf::System::Type*)ent.mParam2));
				break;
			case EVENT_GC_START:
				fprintf(fp, "GCStart MarkId:%d Tick:%d\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_GC_UNFREEZE:
				fprintf(fp, "GCUnfreeze MarkId:%d Tick:%d\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_MARK:
				fprintf(fp, "GCMark Obj:%p Flags:0x%X Parent:%p\n", ent.mParam1, ent.mParam2, ent.mParam3);
				break;
			case EVENT_WB_MARK:
				fprintf(fp, "WriteBarrierMark Obj:%p Type:%s Thread:%p\n", ent.mParam1, GetNameStr((bf::System::Type*)ent.mParam2), ent.mParam3);
				break;
			case EVENT_WB_MOVE:
				fprintf(fp, "WriteBarrier Entry Move Obj:%p Type:%s Thread:%p\n", ent.mParam1, GetNameStr((bf::System::Type*) ent.mParam2), ent.mParam3);
				break;
            case EVENT_FOUND_TARGET:
                fprintf(fp, "Heap Scan Found Target:%p Type:%s Flags:%d\n", ent.mParam1, GetNameStr((bf::System::Type*)ent.mParam2), ent.mParam3);
                break;
            case EVENT_FINALIZE_LIST:
				fprintf(fp, "On Finalize List:%p\n", ent.mParam1);
                break;
			case EVENT_LEAK:
				fprintf(fp, "Leak Detected:%p Type:%s\n", ent.mParam1, GetNameStr((bf::System::Type*)ent.mParam2));
				break;
			case EVENT_FOUND:
				fprintf(fp, "Found Obj:%p Flags:0x%X\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_FREE:
				fprintf(fp, "Free Obj:%p\n", ent.mParam1);
				break;
			case EVENT_DELETE:
				fprintf(fp, "Delete Obj:%p\n", ent.mParam1);
				break;
			case EVENT_THREAD_STARTED:
				fprintf(fp, "ThreadStarted:%p TID:%d\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_THREAD_DONE:
				fprintf(fp, "ThreadDone:%p\n", ent.mParam1);
				break;
			case EVENT_SCAN_THREAD:
				fprintf(fp, "ScanThread:%p Id:%d\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_CONSERVATIVE_SCAN:
				fprintf(fp, "ConservativeScan:%p to %p\n", ent.mParam1, ent.mParam2);
				break;
			case EVENT_GC_DONE:
				fprintf(fp, "GCDone MarkCount:%d HadOverflow:%d\n", ent.mParam1, ent.mParam2);
				break;
			default:
                BF_FATAL("Unknown event");
				break;
			}

			pos = (pos + 1) & BUFFSIZE_MASK;
		}

		fclose(fp);
        mWriting = false;
	}
};

GCLog gGCLog;

void BFGCLogWrite()
{
	gGCLog.Write();
}

#define BFLOG(cmd) gGCLog.Log(cmd)
#define BFLOG1(cmd, param1) gGCLog.Log(cmd, param1)
#define BFLOG2(cmd, param1, param2) gGCLog.Log(cmd, param1, param2)
#define BFLOG3(cmd, param1, param2, param3) gGCLog.Log(cmd, param1, param2, param3)
#else
#define BFLOG(cmd)
#define BFLOG1(cmd, param1)
#define BFLOG2(cmd, param1, param2)
#define BFLOG3(cmd, param1, param2, param3)
void BFGCLogWrite() {}
#endif

void BFGCLogAlloc(bf::System::Object* obj, bf::System::Type* objType, int allocNum)
{
	BFLOG3(GCLog::EVENT_ALLOC, (intptr)obj, (intptr)objType, allocNum);
}

BfDbgInternalThread* GetCurrentInternalThread()
{
	return NULL;
}

////////////////////////////////////////////

static void CheckTcIntegrity()
{
	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

#ifdef BF64
	//BP_ZONE("CheckTcIntegrity");

	Beefy::HashSet<tcmalloc_obj::Span*> spanSet;

	int interiorLen = PageHeap::PageMap::INTERIOR_LENGTH;
	int leafLen = PageHeap::PageMap::LEAF_LENGTH;

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
				tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
				{
					int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;

					if (span->start == expectedStartPage)
					{
						auto result = spanSet.Add(span);
						BF_ASSERT(result);
					}
				}
			}
		}
	}

	int spansFound = spanSet.size();

#endif
}

////////////////////////////////////////////

void BFCheckSuspended()
{
    BfInternalThread* internalThread = (BfInternalThread*)GetCurrentInternalThread();
    if ((internalThread != NULL) && (internalThread->mIsSuspended))
    {
        internalThread = (BfInternalThread*)GetCurrentInternalThread();
        printf("IsSuspended\n");
    }
    BF_ASSERT((internalThread == NULL) || (!internalThread->mIsSuspended));
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

void* BfObjectAllocate(intptr size, bf::System::Type* objType)
{
	size_t totalSize = size;
	totalSize += 4; // int16 protectBytes, <unused bytes>, int16 sizeOffset

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

	BF_ASSERT(totalSize - (size + 4) <= kPageSize);
	*(uint16*)((uint8*)result + size) = 0xBFBF;
	*(uint16*)((uint8*)result + totalSize - 2) = totalSize - size;

	bf::System::Object* obj = (bf::System::Object*)result;

	BFLOG2(GCLog::EVENT_ALLOC, (intptr)obj, (intptr)objType);

	//CheckTcIntegrity();

	gBFGC.mBytesRequested += size;
// #ifdef BF_GC_PRINTSTATS
// 	::InterlockedIncrement((volatile uint32*)&gBFGC.mTotalAllocs);
// #endif

#ifdef BG_GC_TRACKPTRS
	{
		Beefy::AutoCrit autoCrit(gBFGC.mCritSect);
		BF_LOGASSERT(gTrackPtr.find(obj) == gTrackPtr.end());
		gTrackPtr.insert(obj);
	}
#endif

#ifdef _DEBUG
	BF_LOGASSERT((obj->mAllocCheckPtr == 0) || (size > kMaxSize));
#endif

    //memset((void*)obj, 0, size);

#ifdef BF_OBJECT_TRACK_ALLOCNUM
	obj->mAllocNum = (int)::InterlockedIncrement((volatile uint32*) &BfObject::sCurAllocNum);
#endif

#ifdef BF_SECTION_NURSERY
    BfInternalThread* internalThread = (BfInternalThread*)bf::System::Threading::Thread::CurrentInternalThread_internal();
    if ((internalThread != NULL) && (internalThread->mSectionDepth == 1))
        internalThread->mSectionNursery.PushUnsafe(obj);
#endif

    gBFGC.mAllocSinceLastGC += size;

	return obj;
}


//////////////////////////////////////////////////////////////////////////
#define BF_GC_MAX_PENDING_OBJECT_COUNT 16*1024

///

///

class BFFinalizeData
{
public:
	int mFinalizeCount;
    bool mInFinalizeList;

public:
	BFFinalizeData()
	{
		mFinalizeCount = 1;
        mInFinalizeList = false;
	}
};

///

BFGC gBFGC;

int volatile BFGC::sCurMarkId = 1;
int volatile BFGC::sAllocFlags = 1;

int gBFCRTAllocSize = 0;

BFGC::BFGC()
{
	mRunning = false;
	mExiting = false;
	mPaused = false;
	mShutdown = false;
	mForceDecommit = false;
	mCollectFailed = false;
	mLastCollectFrame = 0;
	mSkipMark = false;
	mGracelessShutdown = false;
	mMainThreadTLSPtr = NULL;
	mHadPendingGCDataOverflow = false;
	mCurPendingGCSize = 0;
	mMaxPendingGCSize = 0;

	mCollectIdx = 0;
    mStackScanIdx = 0;
	mMarkDepthCount = 0;
	mMarkingDeleted = false;
	mCurMutatorMarkCount = 0;
	mCurGCMarkCount = 0;
	mCurFreedBytes = 0;
	sCurMarkId = 1;
	mQueueMarkObjects = false;
	//mEphemeronTombstone = NULL;
	mWaitingForGC = false;
	mCollectRequested = false;
    mAllocSinceLastGC = 0;
    mFullGCTriggered = false;
	mDebugDumpState = 0;
	mCurScanIdx = 1;
	mTotalAllocs = 0;
	mTotalFrees = 0;
	mLastFreeCount = 0;
	mBytesFreed = 0;
	mBytesRequested = 0;
	mRequestedSizesInvalid = false;
	mDisplayFreedObjects = false;
	mHadRootError = false;
	mCurLiveObjectCount = 0;
	mFreeSinceLastGC = 0;

	// Default to just over two 60Hz frames
	//  This keeps stack scanning from adding excessive costs to a 30Hz app, or
	//  from impacting two successive frames in a triple-buffered 60Hz app where
	//  only one slow frame could be absorbed
	mMultiStackScanWait = 35;

    // By default we collect after freeing 64MB
    mFreeTrigger = 64*1024*1024;

	mMaxPausePercentage = 20;
	mMaxRawDeferredObjectFreePercentage = 30;

	// Zero means to run continuously. -1 means don't trigger on a time base
	// Defaults to collecting every 2 seconds
	mFullGCPeriod = 2000;

	mGCThread = NULL;

	gGCDbgData.mDbgFlags = gBfRtDbgFlags;
	ThreadCache::InitTSD();
	if (UNLIKELY(Static::pageheap() == NULL)) ThreadCache::InitModule();
	gGCDbgData.mObjRootPtr = Static::pageheap()->pagemap_.root_;

	for (int i = 0; i < kNumClasses; i++)
		gGCDbgData.mSizeClasses[i] = Static::sizemap()->ByteSizeForClass(i);

	mStats = NULL;
	Beefy::String memName = StrFormat("BFGC_stats_%d", GetCurrentProcessId());
	auto* fileMapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(Stats), memName.c_str());
	if (fileMapping != NULL)
	{
		mStats = (Stats*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Stats));
		mStats->mHeapSize = 0;
	}

	RawInit();
}

BFGC::~BFGC()
{
	//::HeapDestroy(gGCHeap);
	//gGCHeap = NULL;
	Beefy::AutoCrit autoCrit(mCritSect);
	for (auto thread : mThreadList)
	{
		delete thread;
	}
}

/*inline T BFWB(typename BFToType<T>::type::BFBaseObjectInObject obj)
{
	if ((obj != NULL) && ((obj->mBFMonitorPtrAndObjFlags & (BF_OBJECTFLAG_MARK_ID_MASK)) != BFGC::sCurMarkId))
		gBFGC.MarkFromMutator(obj, 0);
	return (T) obj;
}*/

/*void BFGC::RegisterRoot(bf::System::Object* obj)
{
	Beefy::AutoCrit autoCrit(mCritSect);
	mExplicitRoots.push_back(obj);
    if (mRunning)
        BFWB(obj);
}*/


#ifdef BF32
static tcmalloc_obj::Span* TCGetSpanAt(void* addr)
{
	int checkPageId = (int)((uintptr)addr >> kPageShift);
	int checkRootIdx = checkPageId >> PageHeap::PageMap::LEAF_BITS;
	int checkLeafIdx = checkPageId & (PageHeap::PageMap::LEAF_LENGTH - 1);

	PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[checkRootIdx];
	if (rootLeaf == NULL)
		return NULL;
	auto span = (tcmalloc_obj::Span*)rootLeaf->values[checkLeafIdx];
// 	intptr pageSize = (intptr)1 << kPageShift;
// 	int spanSize = pageSize * span->length;
// 	void* spanStart = (void*)((intptr)span->start << kPageShift);
// 	void* spanEnd = (void*)((intptr)spanStart + spanSize);
// 	if ((addr >= spanStart) && (addr < spanEnd))
// 		return span;
	return span;
}
#else
static tcmalloc_obj::Span* TCGetSpanAt(void* addr)
{
	int64 checkPageId = (int64)((uintptr)addr >> kPageShift);
	int pageIdx1 = (int)(checkPageId >> (PageHeap::PageMap::LEAF_BITS + PageHeap::PageMap::INTERIOR_BITS));
    int pageIdx2 = (int)((checkPageId >> PageHeap::PageMap::LEAF_BITS) & (PageHeap::PageMap::INTERIOR_LENGTH-1));
	int pageIdx3 = (int)(checkPageId & (PageHeap::PageMap::LEAF_LENGTH-1));

	PageHeap::PageMap::Node* node1 = Static::pageheap()->pagemap_.root_->ptrs[pageIdx1];
	if (node1 == NULL)
		return NULL;
	PageHeap::PageMap::Node* node2 = node1->ptrs[pageIdx2];
	if (node2 == NULL)
		return NULL;
	auto span = (tcmalloc_obj::Span*)node2->ptrs[pageIdx3];
	return span;
// 	if (span == NULL)
// 		return NULL;
// 	intptr pageSize = (intptr)1 << kPageShift;
// 	int spanSize = pageSize * span->length;
// 	void* spanStart = (void*)((intptr)span->start << kPageShift);
// 	void* spanEnd = (void*)((intptr)spanStart + spanSize);
// 	if ((addr >= spanStart) && (addr < spanEnd))
// 		return span;
// 	return NULL;
}
#endif

int gTargetFoundCount = 0;
int gMarkTargetCount = 0;
int gMarkTargetMutatorCount = 0;

int BFGetObjectSize(bf::System::Object* obj)
{
// 	auto span = TCGetSpanAt(obj);
// 	int elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
// 	if (elementSize != 0)
// 		return elementSize;
// 	intptr pageSize = (intptr)1 << kPageShift;
// 	int spanSize = pageSize * span->length;
// 	return spanSize;

	const PageID p = reinterpret_cast<uintptr_t>(obj) >> kPageShift;
	size_t cl = Static::pageheap()->GetSizeClassIfCached(p);
	int allocSize = 0;
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
	return allocSize;
}



void BFCheckObjectSize(bf::System::Object* obj, int size)
{
	if (gBFGC.mRunning)
	{
		BF_ASSERT(BFGetObjectSize(obj) == size);
	}
}

void BFGC::ConservativeScan(void* startAddr, int length)
{
	if ((gBfRtDbgFlags & BfRtFlags_ObjectHasDebugFlags) == 0)
		return;

    BFLOG2(GCLog::EVENT_CONSERVATIVE_SCAN, (intptr)startAddr, (intptr)startAddr + length);

	void* ptr = (void*)((intptr)startAddr & ~((sizeof(intptr)-1)));
	void* endAddr = (uint8*)startAddr + length;

	__try
	{
		while (ptr < endAddr)
		{
			void* addr = *(void**)ptr;
			MarkFromGCThread((bf::System::Object*)addr);
			ptr = (uint8*)ptr + sizeof(intptr);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{

	}
}

static tcmalloc_obj::Span* gLastSpan = NULL;

bool BFGC::IsHeapObject(bf::System::Object* obj)
{
	//BP_ZONE("IsHeapObject");

	if ((obj >= tcmalloc_obj::PageHeap::sAddressStart) && (obj < tcmalloc_obj::PageHeap::sAddressEnd))
	{
		tcmalloc_obj::Span* span = TCGetSpanAt(obj);
		gLastSpan = span;
		return span != NULL;
	}
	return false;
}

/*void BFGC::MarkTypeStatics(BFTypeRoot* checkType)
{
	BFTypeRoot* innerCheckType = (BFTypeRoot*)checkType->mFirstNestedType;
	while (innerCheckType != NULL)
	{
		MarkTypeStatics(innerCheckType);
		innerCheckType = (BFTypeRoot*)innerCheckType->mNextSibling;
	}

	if (checkType->mTypeRootData->mTypeFlags & BF_TYPEFLAG_GENERIC_DEF_TYPE)
		return;

	if (checkType->mTypeRootData->mBFMarkStatics != NULL)
		checkType->mTypeRootData->mBFMarkStatics();
}

void BFGC::MarkStatics()
{
	BFMark(bf::System::Object::sTypeLockObject);

	BFAppDomain* bfDomain = BFAppDomain::GetAppDomain();
	for (int assemblyIdx = 0; assemblyIdx < (int) bfDomain->mBFAssemblyVector.size(); assemblyIdx++)
	{
		BFAssembly* assembly = bfDomain->mBFAssemblyVector[assemblyIdx];
		BFTypeRoot* outerCheckType = (BFTypeRoot*)assembly->mFirstType;
		while (outerCheckType != NULL)
		{
			MarkTypeStatics(outerCheckType);
			outerCheckType = (BFTypeRoot*)outerCheckType->mNextSibling;
		}
	}
   	BFType::BFMarkStatics();
}*/

void BFGC::MarkStatics()
{
	bf::System::GC::DoMarkAllStaticMembers();
	//MarkObject(gMainThread);
}

void BFGC::ObjectDeleteRequested(bf::System::Object* obj)
{
	const PageID p = reinterpret_cast<uintptr_t>(obj) >> kPageShift;
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

	int sizeOffset = *(uint16*)((uint8*)obj + allocSize - 2);
	int requestedSize = allocSize - sizeOffset;
	if ((sizeOffset < 4) || (sizeOffset >= allocSize) || (sizeOffset >= kPageSize + 4) ||
		(*(uint16*)((uint8*)obj + requestedSize) != 0xBFBF))
	{
		Beefy::String err = Beefy::StrFormat("Memory deallocation detected write-past-end error in %d-byte object allocation at 0x%@", requestedSize, obj);
		BF_FATAL(err);
		return;
	}

	if (mFreeTrigger >= 0)
	{
		int objSize = BFGetObjectSize(obj);
		if (BfpSystem_InterlockedExchangeAdd32((uint32*)&mFreeSinceLastGC, (uint32)objSize) + objSize >= mFreeTrigger)
		{
			mFreeSinceLastGC = 0;
			Collect(true);
		}

		BFLOG1(GCLog::EVENT_DELETE, (intptr)obj);
	}
}

bool BFGC::HandlePendingGCData()
{
	int count = 0;

	while (true)
	{
		if (mOrderedPendingGCData.IsEmpty())
			break;

		mCurPendingGCSize = 0;

		bf::System::Object* obj = mOrderedPendingGCData.Pop();
		MarkMembers(obj);
		count++;

		if (mCurPendingGCSize > mMaxPendingGCSize)
			mMaxPendingGCSize = mCurPendingGCSize;
	}

	return count > 0;
}

void BFGC::SweepSpan(tcmalloc_obj::Span* span, int expectedStartPage)
{
	if ((gBfRtDbgFlags & BfRtFlags_ObjectHasDebugFlags) == 0)
		return;

	if (span->location != tcmalloc_obj::Span::IN_USE)
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

	intptr pageSize = (intptr)1<<kPageShift;
	intptr spanSize = pageSize * span->length;
	void* spanStart = (void*)((intptr)span->start << kPageShift);
	void* spanEnd = (void*)((intptr)spanStart + spanSize);
	void* spanPtr = spanStart;

    BF_LOGASSERT((spanStart >= tcmalloc_obj::PageHeap::sAddressStart) && (spanEnd <= tcmalloc_obj::PageHeap::sAddressEnd));

	intptr elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
	if (elementSize == 0)
		elementSize = spanSize;
	BF_LOGASSERT(elementSize >= sizeof(bf::System::Object));

	while (spanPtr <= (uint8*)spanEnd - elementSize)
	{
		//objCheckCount++;

		bf::System::Object* obj = (bf::System::Object*)spanPtr;

#ifdef TARGET_TYPE
		if ((obj->mAllocCheckPtr != 0) && (obj->mBFVData->mType == TARGET_TYPE))
		{
			//sweepFoundCount++;
		}
#endif
		// Mark 0 means 'just allocated'. 'deleteMarkId' is the last one. 'invalidMarkId' should be impossible because we'd either be deleted or marked as leaked before

		int deleteMarkId = mCurMarkId - 1;
		if (deleteMarkId == 0)
			deleteMarkId = 3;

		int invalidMarkId = deleteMarkId - 1;
		if (invalidMarkId == 0)
			invalidMarkId = 3;

		bool showAllAsLeaks = !mRunning;

		if (obj->mAllocCheckPtr != 0)
		{
#ifdef BF_GC_VERIFY_SWEEP_IDS
			BF_LOGASSERT(obj->mAllocNum != 0);
			BF_LOGASSERT(allocIdSet.find(obj->mAllocNum) == allocIdSet.end());
			allocIdSet.insert(obj->mAllocNum);
#endif

			mCurSweepFoundCount++;
			int objectFlags = obj->mObjectFlags;
			if (objectFlags == 0)
				mCurSweepFoundPermanentCount++;

			BFLOG2(GCLog::EVENT_FOUND, (intptr)obj, obj->mObjectFlags);

			int markId = objectFlags & BF_OBJECTFLAG_MARK_ID_MASK;
			if ((mCollectFailed) && (markId != mCurMarkId))
			{
				obj->mObjectFlags = (BfObjectFlags)((obj->mObjectFlags & ~BF_OBJECTFLAG_MARK_ID_MASK) | mCurMarkId);
				markId = mCurMarkId;
			}

			BF_ASSERT(markId != invalidMarkId);

			if (markId == 0)
			{
				// Newly-allocated! Set mark flag now...
				//obj->mObjectFlags = (BfObjectFlags)(obj->mObjectFlags  | mCurMarkId);
				// Newly-allocated! Ignore, it will have its mark set soon (rare race condition)
			}
			else if ((markId == deleteMarkId) || (mSweepInfo.mShowAllAsLeaks))
			{
				if ((objectFlags & BF_OBJECTFLAG_DELETED) == 0)
				{
					if (!mSweepInfo.mEmptyScan)
					{
						// We set this to cause an error like:
						//	Deleting an object that was detected as leaked (internal error)
						obj->mObjectFlags = (BfObjectFlags)(objectFlags | BF_OBJECTFLAG_STACK_ALLOC);

						if (!mHadRootError)
						{
							mSweepInfo.mLeakCount++;
							if (mSweepInfo.mLeakCount <= 1024 * 1024) // Have SOME limit
							{
								mSweepInfo.mLeakObjects.push_back(obj);
							}

							BFLOG2(GCLog::EVENT_LEAK, (intptr)obj, (intptr)obj->_GetType());
#ifdef BF_GC_LOG_ENABLED
							gGCLog.Write();
#endif
						}
					}
				}
				else
				{
					if (!mSweepInfo.mEmptyScan)
					{
						BFLOG1(GCLog::EVENT_FINALIZE_LIST, (intptr)obj);
						//obj->mObjectFlags = (BfObjectFlags) (obj->mObjectFlags & ~BF_OBJECTFLAG_FINALIZE_MAP);
						mFinalizeList.push_back(obj);
					}
				}
			}
			else
				mCurLiveObjectCount++;
		}
		spanPtr = (void*)((intptr)spanPtr + elementSize);
	}
}

void BFGC::Sweep()
{
	BP_ZONE("Sweep");

	mCurLiveObjectCount = 0;

	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

#ifdef BF_GC_VERIFY_SWEEP_IDS
	maxAllocNum = bf::System::Object::sCurAllocNum;
	allocIdSet.clear();
#endif

	int leafCheckCount = 0;

	int bits = kAddressBits;
	int leafLen = PageHeap::PageMap::LEAF_LENGTH;

#ifdef BF32
	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[rootIdx];
		if (rootLeaf == NULL)
			continue;

		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{
			leafCheckCount++;

			tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)rootLeaf->values[leafIdx];
			if (span != NULL)
			{
				int expectedStartPage = (rootIdx * PageHeap::PageMap::LEAF_LENGTH) + leafIdx;
				SweepSpan(span, expectedStartPage);
                // We may be tempted to advance by span->length here, BUT
                // let us just scan all leafs becuause span data is
                // sometimes invalid and a long invalid span can cause
                // us to skip over an actual valid span
			}
		}
	}
#else
	int interiorLen = PageHeap::PageMap::INTERIOR_LENGTH;
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
				leafCheckCount++;

				tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
				{
					int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;
					SweepSpan(span, expectedStartPage);
                    // We may be tempted to advance by span->length here, BUT
                    // let us just scan all leafs becuause span data is
                    // sometimes invalid and a long invalid span can cause
                    // us to skip over an actual valid span
				}
			}
		}
	}
#endif

#ifdef BF_GC_VERIFY_SWEEP_IDS
	for (int allocNum = 1; allocNum < maxAllocNum; allocNum++)
	{
		BF_LOGASSERT(allocIdSet.find(allocNum) != allocIdSet.end());
	}
#endif
}

extern Beefy::StringT<0> gDbgErrorString;

void BFGC::ProcessSweepInfo()
{
	if (mSweepInfo.mLeakCount > 0)
	{
		if (mSweepInfo.mShowAllAsLeaks)
		{
			// We aren't certain of the mark flags, so force the issue
			mCurMarkId = 0;
			for (auto obj : mSweepInfo.mLeakObjects)
			{
				obj->mObjectFlags = (BfObjectFlags)((obj->mObjectFlags & ~BF_OBJECTFLAG_MARK_ID_MASK) | mCurMarkId);
			}
			mCurMarkId = 1;
		}

		for (auto obj : mSweepInfo.mLeakObjects)
		{
			MarkMembers(obj);
		}

		//::MessageBoxA(NULL, "Leak", "Leak", MB_OK);

//#if 0
		Beefy::StringT<1024> errorStr = StrFormat("%d object memory leak%s detected.\nMouse over an 'i' icon in the Output panel to view a leaked object and its allocation stack trace.",
			mSweepInfo.mLeakCount, (mSweepInfo.mLeakCount != 1) ? "s" : "");
		gDbgErrorString = errorStr;
		gDbgErrorString += "\n";

#ifdef BF_GC_DEBUGSWEEP
		Sleep(100);
#endif

		for (int pass = 0; pass < 2; pass++)
		{
			int passLeakCount = 0;

			for (auto obj : mSweepInfo.mLeakObjects)
			{
				bool wantsNoRefs = pass == 0;
				bool hasNoRefs = (obj->mObjectFlags & BF_OBJECTFLAG_MARK_ID_MASK) != mCurMarkId;
				if (hasNoRefs != wantsNoRefs)
					continue;

				Beefy::String typeName = obj->GetTypeName();

				if (passLeakCount == 0)
				{
					Beefy::String header = (pass == 0) ? " Unreferenced:\n" : " Referenced by other leaked objects:\n";
					errorStr += "\x1";
					errorStr += "TEXT\t";
					errorStr += header;

					gDbgErrorString += header;
				}

				if (passLeakCount == 20000) // Only display so many...
					break;

				errorStr += "\x1";
				errorStr += StrFormat("LEAK\t(System.Object)0x%@\n", obj);
				errorStr += StrFormat("   (%s)0x%@\n", typeName.c_str(), obj);

				if (gDbgErrorString.length() < 256)
					gDbgErrorString += StrFormat("   (%s)0x%@\n", typeName.c_str(), obj);

				passLeakCount++;
			}
		}

		//TODO: Testing!
		//OutputDebugStrF(gDbgErrorString.c_str());

		gBfRtDbgCallbacks.SetErrorString(gDbgErrorString.c_str());
		gBfRtDbgCallbacks.DebugMessageData_SetupError(errorStr.c_str(), 1);

		mCritSect.Unlock();
		BF_DEBUG_BREAK();
		mCritSect.Lock();

		for (auto obj : mSweepInfo.mLeakObjects)
		{
			// Allow continuing
			obj->mObjectFlags = (BfObjectFlags)((obj->mObjectFlags & ~BF_OBJECTFLAG_MARK_ID_MASK) | mCurMarkId);
		}
	}

	mSweepInfo.Clear();
}

void BFGC::ReleasePendingSpanObjects(Span* span)
{
    if (span->freeingObjects != NULL)
    {
        Static::central_cache()[span->sizeclass].ReleasePendingSpanObjects(span);
        span->freeingObjects = NULL;
        span->freeingObjectsTail = NULL;
    }
}

void BFGC::ReleasePendingObjects()
{
	BP_ZONE("ReleasePendingObjects");

	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

#ifdef BF_GC_VERIFY_SWEEP_IDS
	maxAllocNum = bf::System::Object::sCurAllocNum;
	allocIdSet.clear();
#endif

	int leafCheckCount = 0;

#ifdef BF32
	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[rootIdx];
		if (rootLeaf == NULL)
			continue;

		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{
			tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)rootLeaf->values[leafIdx];
			if (span != NULL)
                ReleasePendingSpanObjects(span);
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
				tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
                    ReleasePendingSpanObjects(span);
			}
		}
	}
#endif

#ifdef BF_GC_VERIFY_SWEEP_IDS
	for (int allocNum = 1; allocNum < maxAllocNum; allocNum++)
	{
		BF_LOGASSERT(allocIdSet.find(allocNum) != allocIdSet.end());
	}
#endif
}

typedef struct _TEB {
	PVOID Reserved1[11];
	void* ThreadLocalStorage;
	void* ProcessEnvironmentBlock;
	PVOID Reserved2[399];
	BYTE  Reserved3[1952];
	PVOID TlsSlots[64];
	BYTE  Reserved4[8];
	PVOID Reserved5[26];
	PVOID ReservedForOle;
	PVOID Reserved6[4];
	PVOID TlsExpansionSlots;
} TEB, *PTEB;

typedef LONG NTSTATUS;
typedef DWORD KPRIORITY;
typedef WORD UWORD;

typedef struct _CLIENT_ID
{
	PVOID UniqueProcess;
	PVOID UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _THREAD_BASIC_INFORMATION
{
	NTSTATUS                ExitStatus;
	PVOID                   TebBaseAddress;
	CLIENT_ID               ClientId;
	KAFFINITY               AffinityMask;
	KPRIORITY               Priority;
	KPRIORITY               BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

enum THREADINFOCLASS
{
	ThreadBasicInformation,
};

static _TEB* GetTEB(HANDLE hThread)
{
	bool loadedManually = false;
	HMODULE module = GetModuleHandleA("ntdll.dll");

	if (!module)
	{
		module = LoadLibraryA("ntdll.dll");
		loadedManually = true;
	}

	NTSTATUS(__stdcall *NtQueryInformationThread)(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength, PULONG ReturnLength);
	NtQueryInformationThread = reinterpret_cast<decltype(NtQueryInformationThread)>(GetProcAddress(module, "NtQueryInformationThread"));

	if (NtQueryInformationThread)
	{
		NT_TIB tib = { 0 };
		THREAD_BASIC_INFORMATION tbi = { 0 };

		NTSTATUS status = NtQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), nullptr);
		if (status >= 0)
		{
			_TEB* teb = (_TEB*)tbi.TebBaseAddress;
			return teb;
		}
	}

	if (loadedManually)
	{
		FreeLibrary(module);
	}

	return NULL;
}

static void** GetThreadLocalAddressMap(HANDLE hThread)
{
	_TEB* teb = GetTEB(hThread);
	if (teb == NULL)
		return NULL;
	return (void**)teb->ThreadLocalStorage;
}

void BFGC::AdjustStackPtr(intptr& addr, int& size)
{
	int pageSize = 4096;

	// There's a race condition where RSP can be adjusted into a guard page region before the
	//  guard page is actually removed. The guard page is a fire-once error so we can't just
	//  ignore it or else we rob that read of it's ability to expand (crash)
	while (size > 0)
	{
		MEMORY_BASIC_INFORMATION memoryInfo;
		int returnSize = ::VirtualQuery((void*)addr, &memoryInfo, sizeof(memoryInfo));
		if ((returnSize > 0) && ((memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0))
			return;

		addr += pageSize;
		size -= pageSize;
	}
}

bool BFGC::ScanThreads()
{
	BP_ZONE("BFGC::ScanThreads");

	mUsingThreadUnlocked = true;

	BF_FULL_MEMORY_FENCE();

	//BP_ZONE("ScanThreads");

	bool didWork = false;

	mStackScanIdx++;
	mDoStackDeepMark = true;

	int threadIdx = 0;

	while (true)
	{
		ThreadInfo* thread = NULL;

		///
		{
			AutoCrit autoCrit(mCritSect);
			for (auto& kv : mPendingThreads)
			{
				MarkFromGCThread(kv.mValue->mThread);
			}
		}

		///
		{
			AutoCrit autoCrit(mCritSect);
			if (threadIdx >= mThreadList.size())
				break;
			thread = mThreadList[threadIdx++];
		}

		if (thread->mExcluded)
			continue;

		if (!thread->mRunning)
		{
			AutoCrit autoCrit(mCritSect);
			BF_ASSERT(mThreadList[threadIdx - 1] == thread);
			delete thread;
			mThreadList.RemoveAt(threadIdx - 1);
			continue;
		}

		BP_ZONE_F("ThreadCollect %d", thread->mThreadId);
		//Beefy::DebugTimeGuard suspendTimeGuard(10, "ThreadSuspend");

		DWORD result = 0;

		//BFMark(thread);
		//MarkObject(thread->mThread);

		// If (thread->mLastGCScanIdx == mCurScanIdx), that means this is the second cycle of running through here,
		//  which could happen if we added another thread while scanning so we need another pass tSuspendThreadhrough to
		//  catch the new one
		if (((mGCThread != NULL) && (thread->mThreadHandle == mGCThread)) ||
			(!thread->mRunning) /*|| (thread->mLastGCScanIdx == mCurScanIdx)*/)
		{
			continue;
		}

#ifdef BF_GC_LOG_ENABLED
		// If we're writing the log from another thread then wait for it -- we'll deadlock if we pause that thread and then try to log
		while (gGCLog.mWriting)
		{
			Sleep(20);
		}
#endif

		//printf("Processing Thread:%p Handle:%p\n", thread, thread->mThreadHandle);
		//suspendTimeGuard.Start();
		//DWORD lastError = GetLastError();
		BF_LOGASSERT(result == 0);

		didWork = true;

		BFLOG2(GCLog::EVENT_SCAN_THREAD, (intptr)thread, (intptr)thread->mThreadId);

		//
		{
			BP_ZONE("StackMarkableObjects");
			for (auto obj : thread->mStackMarkableObjects)
			{
				MarkMembers(obj);
			}
		}

		intptr regVals[128];
		intptr stackPtr = 0;
		BfpThreadResult threadResult;
		int regValCount = 128;
		///
		{
			BP_ZONE("BfpThread_GetIntRegisters");
			BfpThread_GetIntRegisters(thread->mThreadHandle, &stackPtr, regVals, &regValCount, &threadResult);
		}
		if (threadResult != BfpThreadResult_Ok)
		{
			mCollectFailed = true;
			return false;
		}

		BF_ASSERT(threadResult == BfpThreadResult_Ok);

		if (thread->mTEB != NULL)
		{
			void** threadLoadAddressMap = (void**)((_TEB*)thread->mTEB)->ThreadLocalStorage;
			for (auto& tlsMember : mTLSMembers)
			{
				void* threadLoadAddress = threadLoadAddressMap[tlsMember.mTLSIndex];

				typedef void(*MarkFunc)(void*);
				MarkFunc markFunc = *(MarkFunc*)&tlsMember.mMarkFunc;
				markFunc((uint8*)threadLoadAddress + tlsMember.mTLSOffset);
			}
		}

		mQueueMarkObjects = true;
		ConservativeScan(regVals, regValCount * sizeof(intptr));
		intptr prevStackStart = thread->mStackStart;
		thread->CalcStackStart();

		thread->mLastStackPtr = stackPtr;
		int length = thread->mStackStart - stackPtr;

		AdjustStackPtr(stackPtr, length);
		{
			BP_ZONE("ConservativeScan stack");
			ConservativeScan((void*)stackPtr, length);
		}
		mQueueMarkObjects = false;

		if (mDoStackDeepMark)
		{
			HandlePendingGCData();
		}
		//suspendTimeGuard.Stop();
		BF_LOGASSERT(result != -1);

		if ((!mOrderedPendingGCData.IsEmpty()) || (!mOrderedPendingGCData.IsEmpty()))
		{
			BP_ZONE("HandlePendingGCData(Thread)");
			HandlePendingGCData();
		}
	}

	// This can be write barrier objects be from dead threads
	HandlePendingGCData();

	BF_FULL_MEMORY_FENCE();

	mUsingThreadUnlocked = false;

	return didWork;
}

int gAddedTypeCount = 0;
int gGCTypeCounts = 0;
int gGCAssemblyCount = 0;

static void GCObjFree(void* ptr)
{
	Span* span = TCGetSpanAt(ptr);
	if (span == NULL)
	{
		BF_DBG_FATAL("Bad");
		tc_free(ptr);
		return;
	}

	if (span->sizeclass == 0)
	{
		// Clear out all memory
		intptr pageSize = (intptr)1 << kPageShift;
		int spanSize = pageSize * span->length;
		void* spanStart = (void*)((intptr)span->start << kPageShift);
		memset(spanStart, 0, spanSize);
		tc_free(ptr);
		return;
	}

	int size = Static::sizemap()->class_to_size(span->sizeclass);
	int dataOffset = (int)(sizeof(intptr) * 2);
	memset((uint8*)ptr + dataOffset, 0, size - dataOffset);

    if (span->freeingObjectsTail == NULL)
    {
        span->freeingObjectsTail = ptr;
        *((intptr*)span->freeingObjectsTail) = 1;
    }
    else
    {
        // Increment pending size
        (*((intptr*)span->freeingObjectsTail))++;
        *(reinterpret_cast<void**>(ptr)) = span->freeingObjects;
    }

    span->freeingObjects = ptr;
}

void BFGC::DoCollect(bool doingFullGC)
{
	BP_ZONE("Collect");

	mThreadId = BfpThread_GetCurrentId();

	mStage = 0;

	gTargetFoundCount = 0;
	gMarkTargetCount  = 0;
	gMarkTargetMutatorCount = 0;

	mCurGCMarkCount = 0;
	mCurMutatorMarkCount = 0;
	mCurGCObjectQueuedCount = 0;
	mCurMutatorObjectQueuedCount = 0;
	mCurObjectDeleteCount = 0;
	mCurFinalizersCalled = 0;
	mCurSweepFoundCount = 0;
	mCurSweepFoundPermanentCount = 0;
	mCurFreedBytes = 0;

    if (doingFullGC)
    {
        int nextMark = sCurMarkId + 1;
        if (nextMark == 4)
            nextMark = 1;
        mCurMarkId = nextMark;
        sCurMarkId = nextMark;
        sAllocFlags = sCurMarkId;
    }

	mStage = 1;

	BFLOG2(GCLog::EVENT_GC_START, sCurMarkId, BfpSystem_TickCount());

	gGCTypeCounts = 0;
	if (!mSkipMark)
	{
		BP_ZONE("MarkStatics");
		MarkStatics();
		RawMarkAll();
		bool success = bf::System::GC::DoCallRootCallbacks();
		if ((!success) && (!mHadRootError))
		{
			OutputDebugStringA("WARNING: GC leak detection disabled, a GC root callback disabled it\n");
			mHadRootError = true;
		}
	}

	if (!mSkipMark)
	{
		Beefy::AutoCrit autoCrit(mCritSect);
		BP_ZONE("ExplicitRootsMark");
		mQueueMarkObjects = true;
		for (int i = 0; i < (int)mExplicitRoots.size(); i++)
			MarkObject(mExplicitRoots[i]);
		mQueueMarkObjects = false;
	}

    if ((doingFullGC) && (!mSkipMark))
    {
        //MarkObject(mGCThread);
        BFMarkThreadPoolJobs();
        BFMarkCOMObjects();
    }

	int threadIdx = 0;

	// We need to turn roots black, otherwise the mutator could move a member
	//  of a gray root to the stack after the stack scan and we'd miss it
	{
		BP_ZONE("Collect - HandlePendingGCData(Roots)");
		HandlePendingGCData();
	}

	mStage = 2;

	mCurScanIdx++;

	int passes = 0;

    if (doingFullGC)
    {
		if (!mSkipMark)
		{
			ScanThreads();
			HandlePendingGCData();
		}
    }

	BF_ASSERT(mOrderedPendingGCData.IsEmpty());
}

void BFGC::FinishCollect()
{
	//OutputDebugStrF("Collected %d objects\n", mFinalizeList.size());

	if ((gBfRtDbgFlags & BfRtFlags_ObjectHasDebugFlags) == 0)
		return;

	mLastFreeCount = 0;
	mStage = 3;

    typedef std::unordered_set<bf::System::Object*> BfObjectSet;

	{

        // Handle any pending data from strong GCHandle references
        HandlePendingGCData();
	}

    int finalizeDataCountFound = 0;

	if (mDebugDumpState == DEBUGDUMPSTATE_WAITING_FOR_GC)
	{
		WriteDebugDumpState();
		mDebugDumpState = DEBUGDUMPSTATE_NONE;
	}

	{
		BP_ZONE("FreeingObjects");

		void* lastPtr = NULL;
		for (int i = 0; i < mFinalizeList.size(); i++)
		{
			bf::System::Object* obj = mFinalizeList[i];
			if (obj == NULL)
				continue;
			BF_LOGASSERT((obj->mObjectFlags & (/*BF_OBJECTFLAG_FREED |*/ BF_OBJECTFLAG_ALLOCATED)) == BF_OBJECTFLAG_ALLOCATED);

			//BF_LOGASSERT(obj > lastPtr);
			lastPtr = obj;
		}

		{
			BP_ZONE("ReleaseAtLeastNPages");
			SpinLockHolder h(tcmalloc_obj::Static::pageheap_lock());
			Static::pageheap()->ReleaseAtLeastNPages(256);
		}

		{
			BP_ZONE("DecommitFromReleasedList");
			Static::pageheap()->DecommitFromReleasedList(mForceDecommit);
			mForceDecommit = false;
		}

		Dictionary<bf::System::Type*, int> sizeMap;

		int objFreeSize = 0;
		for (int i = 0; i < mFinalizeList.size(); i++)
		{
			bf::System::Object* obj = mFinalizeList[i];
			if (obj == NULL)
				continue;
			// Removed from list already?
			if ((obj->mObjectFlags & BF_OBJECTFLAG_MARK_ID_MASK) == mCurMarkId)
				continue;

#ifdef BF_DEBUG
			if ((obj->mObjectFlags & (/*BF_OBJECTFLAG_FREED |*/ BF_OBJECTFLAG_ALLOCATED)) != BF_OBJECTFLAG_ALLOCATED)
			{
				tcmalloc_obj::Span* span = TCGetSpanAt(obj);
				BF_FATAL("Object corrupted");
			}

#ifdef TARGET_TYPE
			if (obj->mBFVData->mType == TARGET_TYPE)
			{
				printf("Finalizing target %p\n", obj);
			}
#endif
#endif

			BFLOG1(GCLog::EVENT_FREE, (intptr)obj);

			// BYE!
#ifdef BF_NO_FREE_MEMORY
			//obj->mObjectFlags |= BF_OBJECTFLAG_FREED;
#else

#ifdef BG_GC_TRACKPTRS
			{
				Beefy::AutoCrit autoCrit(gBFGC.mCritSect);
				gTrackPtr.erase(gTrackPtr.find(obj));
			}
#endif

			int objSize = BFGetObjectSize(obj);
            objFreeSize += objSize;

			if (mDisplayFreedObjects)
			{
				// Temporarily remove object flags so GetType() won't fail
				obj->mObjectFlags = BfObjectFlag_None;
				bf::System::Type* type = obj->_GetType();
				//auto pairVal = sizeMap.insert(std::make_pair(type, 0));
				//int newSize = pairVal.first->second + objSize;
				int* sizePtr = NULL;
				sizeMap.TryAdd(type, NULL, &sizePtr);
				*sizePtr += objSize;

				//pairVal.first->second = newSize;
			}

			//obj->mObjectFlags = BfObjectFlag_None;
			obj->mAllocCheckPtr = 0;
			mLastFreeCount++;
#ifdef BF_GC_USE_OLD_FREE
			tc_free(obj);
#else
            GCObjFree(obj);
#endif

#ifdef BF_GC_PRINTSTATS
			::InterlockedIncrement((volatile uint32*) &gBFGC.mTotalFrees);
#endif

#endif
			mCurObjectDeleteCount++;
		}

		if (!sizeMap.IsEmpty())
		{
			std::multimap<int, bf::System::Type*> orderedSizeMap;
			int totalSize = 0;
			for (auto& pair : sizeMap)
			{
				totalSize += pair.mValue;
				orderedSizeMap.insert(std::make_pair(-pair.mValue, pair.mKey));
			}

			Beefy::String msg;
			msg += Beefy::StrFormat("GC Live Count                           : %d\n", mCurLiveObjectCount);
			msg += Beefy::StrFormat("GC Freed Count                          : %d\n", mLastFreeCount);
			msg += Beefy::StrFormat("GC Freed Size                           : %dk\n", (int)(objFreeSize / 1024));
			msg += "GC Objects\n";
			for (auto& pair : orderedSizeMap)
			{
				bf::System::Type* typeName = pair.second;
				msg += StrFormat("  %-37s : %dk\n", typeName->GetFullName().c_str(), (-pair.first + 1023) / 1024);
			}

			Beefy::OutputDebugStr(msg.c_str());
		}

        mBytesFreed += objFreeSize;
        mBytesRequested -= objFreeSize;
        mCurFreedBytes += objFreeSize;
	}

#ifdef BF_GC_USE_OLD_FREE
	{
		BP_ZONE("TCScavenge");
		tcmalloc_obj::ThreadCache::GetCacheWhichMustBePresent()->ForceScavenge();
	}
#else
    {
        //Beefy::DebugTimeGuard suspendTimeGuard(10, "BFGC::ReleasePendingObjects");
        ReleasePendingObjects();
    }
#endif

	mFinalizeList.Clear();

	mStage = 4;
}

void BFGC::UpdateStats()
{
	if (mStats == NULL)
		return;
	mStats->mHeapSize = TCMalloc_SystemTaken;
}

void BFGC::Run()
{
	BfpThread_SetName(BfpThread_GetCurrent(), "BFGC", NULL);

	uint32 lastGCTick = BFTickCount();

	while (!mExiting)
	{
		UpdateStats();

		float fullGCPeriod = mFullGCPeriod;
		if ((fullGCPeriod != -1) && (mMaxPausePercentage > 0) && (!mCollectReports.IsEmpty()))
		{
			// When we are debugging, we can have a very long update when stepping through code,
			//  but otherwise try to pick an update period that keeps our pause time down
			float maxExpandPeriod = BF_MAX(mFullGCPeriod, 2000);
			auto& collectReport = mCollectReports.back();
			fullGCPeriod = BF_MAX(fullGCPeriod, collectReport.mPausedMS * 100 / mMaxPausePercentage);
			fullGCPeriod = BF_MIN(fullGCPeriod, maxExpandPeriod);
		}

		int waitPeriod = BF_MIN(fullGCPeriod, 100);
		if (waitPeriod == 0)
			waitPeriod = -1;
		mCollectEvent.WaitFor(waitPeriod);

		uint32 tickNow = BFTickCount();
		if ((fullGCPeriod >= 0) && (tickNow - lastGCTick >= fullGCPeriod))
			mCollectRequested = true;
		if ((mFreeTrigger >= 0) && (mFreeSinceLastGC >= mFreeTrigger))
			mCollectRequested = true;

		if (!mCollectRequested)
			continue;

		lastGCTick = tickNow;
		mCollectRequested = false;
		mPerformingCollection = true;
		BF_FULL_MEMORY_FENCE();
		while (true)
		{
			mHadPendingGCDataOverflow = false;
			mMaxPendingGCSize = 0;
			PerformCollection();
			if (!mHadPendingGCDataOverflow)
				break;
			mOrderedPendingGCData.Reserve(BF_MAX(mOrderedPendingGCData.mAllocSize + mOrderedPendingGCData.mAllocSize / 2, mMaxPendingGCSize + 256));
		}

		BF_FULL_MEMORY_FENCE();
		mPerformingCollection = false;
		BF_FULL_MEMORY_FENCE();
		mCollectDoneEvent.Set(true);
	}

	mRunning = false;
}

void BFGC::RunStub(void* gc)
{
	((BFGC*)gc)->Run();
}

void BFGC::ThreadStarted(BfDbgInternalThread* thread)
{
	Beefy::AutoCrit autoCrit(mCritSect);

	//thread->mTCMallocObjThreadCache = tcmalloc_obj::ThreadCache::GetCache();
	//mThreadList.push_back(thread);

	BFLOG2(GCLog::EVENT_THREAD_STARTED, (intptr)thread, (intptr)BfpThread_GetCurrentId());

	//printf("ThreadStarted: %p intern:%p TID:%d\n", thread->mThread, thread, ::GetCurrentThreadId());
}

void BFGC::ThreadStopped(BfDbgInternalThread* thread)
{
	Beefy::AutoCrit autoCrit(mCritSect);
	// Keep sync to avoid having the thread exit while the GC is trying to pause it,
	//   but do the actual cleanup from the GC's thread

	//thread->mDone = true;

	//printf("ThreadStopped: %p intern:%p TID:%d\n", thread->mThread, thread, ::GetCurrentThreadId());
}

void BFGC::ThreadStarted()
{
	Beefy::AutoCrit autoCrit(mCritSect);

	ThreadInfo* thread = new ThreadInfo();
	thread->mRunning = true;
	thread->mThreadHandle = BfpThread_GetCurrent();
	thread->mThreadId = BfpThread_GetCurrentId();
	thread->mTEB = GetTEB((HANDLE)thread->mThreadHandle);
	thread->mThreadInfo = BfpThreadInfo_Create();

	thread->CalcStackStart();

	mThreadList.Add(thread);
	mPendingThreads.Remove(thread->mThreadId);

	ThreadInfo::sCurThreadInfo = thread;
}

void BFGC::ThreadStopped()
{
	auto thread = ThreadInfo::sCurThreadInfo;
	if (thread != NULL)
	{
		Beefy::AutoCrit autoCrit(mCritSect);

		BF_ASSERT(thread->mStackMarkableObjects.IsEmpty());
		if (!mUsingThreadUnlocked)
		{
			// Just delete it
			mThreadList.Remove(thread);
			delete thread;
		}
		else
		{
			thread->mRunning = false;
		}
	}
}

void BFGC::Init()
{
	//ThreadStarted();
	Start();
}

void BFGC::Start()
{
#ifndef BF_GC_DISABLED
	mRunning = true;

#ifdef BF_DEBUG
	// More stack space is needed in debug version
	mGCThread = BfpThread_Create(RunStub, (void*)this, 256 * 1024, (BfpThreadCreateFlags)(BfpThreadCreateFlag_Suspended | BfpThreadCreateFlag_StackSizeReserve), &mThreadId);
#else
	mGCThread = BfpThread_Create(RunStub, (void*)this, 64 * 1024, (BfpThreadCreateFlags)(BfpThreadCreateFlag_Suspended | BfpThreadCreateFlag_StackSizeReserve), &mThreadId);
#endif

	BfpThread_Resume(mGCThread, NULL);
#endif
}

void BFGC::StopCollecting()
{
	if (!mRunning)
		return;

	mExiting = true;
	while (mRunning)
	{
		if (BfpThread_WaitFor(mGCThread, 0))
		{
			OutputDebugStr("BeefDbgRT not shut down gracefully!\n");
			mGracelessShutdown = true;
			mRunning = false;
			break;
		}

		//BFRtLock bfLock(mEphemeronTombstone);
		mWaitingForGC = true;
		// Wait for current collection to finish
		mCollectEvent.Set();
		//Monitor::Monitor_wait(mEphemeronTombstone, 20);
		mWaitingForGC = false;
	}
}

void BFGC::AddStackMarkableObject(bf::System::Object* obj)
{
	auto threadInfo = ThreadInfo::sCurThreadInfo;
	Beefy::AutoCrit autoCrit(threadInfo->mCritSect);
	threadInfo->mStackMarkableObjects.Add(obj);
}

void BFGC::RemoveStackMarkableObject(bf::System::Object* obj)
{
	auto threadInfo = ThreadInfo::sCurThreadInfo;
	Beefy::AutoCrit autoCrit(threadInfo->mCritSect);

	int stackIdx = threadInfo->mStackMarkableObjects.LastIndexOf(obj);
	BF_ASSERT(stackIdx != -1);
	if (stackIdx != -1)
		threadInfo->mStackMarkableObjects.RemoveAtFast(stackIdx);
}

void BFGC::AddPendingThread(BfInternalThread* internalThread)
{
	if (internalThread->mThread == 0)
		return;
	Beefy::AutoCrit autoCrit(mCritSect);
	mPendingThreads.TryAdd(internalThread->mThreadId, internalThread);
}

void BFGC::Shutdown()
{
	if (mShutdown)
		return;
	mShutdown = true;

	StopCollecting();

	Beefy::AutoCrit autoCrit(mCritSect);

	if (mGracelessShutdown)
		return;

	// Report any objects that aren't deleted
	mSweepInfo.mShowAllAsLeaks = true;
	Sweep();
	ProcessSweepInfo();

	RawShutdown();
	TCMalloc_FreeAllocs();

	mFinalizeList.Dispose();
	mOrderedPendingGCData.Dispose();
	for (auto thread : mThreadList)
		thread->mStackMarkableObjects.Dispose();
}

void BFGC::InitDebugDump()
{
	mDebugDumpState = DEBUGDUMPSTATE_WAITING_FOR_PREV;
	mCollectEvent.Set();
	while (mDebugDumpState < DEBUGDUMPSTATE_WAITING_FOR_MUTATOR)
	{
		//BFRtLock bfLock(mEphemeronTombstone);
		//Monitor::Monitor_wait(mEphemeronTombstone, 20);
	}
}

void BFGC::EndDebugDump()
{
	mDebugDumpState = DEBUGDUMPSTATE_WAITING_FOR_GC;
}

intptr gFindAddrVal = 0;

void BFGC::DebugDumpLeaks()
{
	CheckTcIntegrity();

	BP_ZONE("DebugDump");

	if (mExiting)
		return;

	mSkipMark = true;
	Collect(false);
}

void BFGC::ObjReportHandleSpan(tcmalloc_obj::Span* span, int expectedStartPage, int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>& sizeMap)
{
	if (span->location != tcmalloc_obj::Span::IN_USE)
		return;

	if (span->start != expectedStartPage)
	{
		return;
	}

	intptr pageSize = (intptr)1<<kPageShift;
	intptr spanSize = pageSize * span->length;
	void* spanStart = (void*)((intptr)span->start << kPageShift);
	void* spanEnd = (void*)((intptr)spanStart + spanSize);
	void* spanPtr = spanStart;

	BF_LOGASSERT((spanStart >= tcmalloc_obj::PageHeap::sAddressStart) && (spanEnd <= tcmalloc_obj::PageHeap::sAddressEnd));

	intptr elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
	if (elementSize == 0)
		elementSize = spanSize;
	BF_LOGASSERT(elementSize >= sizeof(bf::System::Object));

	while (spanPtr <= (uint8*)spanEnd - elementSize)
	{
		bf::System::Object* obj = (bf::System::Object*)spanPtr;
		if (obj->mAllocCheckPtr != 0)
		{
			int objectFlags = obj->mObjectFlags;
			if ((objectFlags & BF_OBJECTFLAG_DELETED) == 0)
			{
				bf::System::Type* type = obj->_GetType();
				//auto pairVal = sizeMap.insert(std::make_pair(type, 0));
				//int newSize = pairVal.first->second + elementSize;
				//pairVal.first->second = newSize;
				AllocInfo* sizePtr = NULL;
				sizeMap.TryAdd(type, NULL, &sizePtr);
				sizePtr->mObjCount++;
				sizePtr->mObjSize += elementSize;

				objectCount++;
			}
		}
		else
			freeSize += elementSize;
		spanPtr = (void*)((intptr)spanPtr + elementSize);
	}
}

void BFGC::ObjReportScan(int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>& sizeMap)
{
	auto pageHeap = Static::pageheap();
	if (pageHeap == NULL)
		return;

#ifdef BF32
	int checkPageId = (int)((uintptr)gFindAddrVal >> kPageShift);
	int checkRootIdx = checkPageId >> PageHeap::PageMap::LEAF_BITS;
	int checkLeafIdx = checkPageId & (PageHeap::PageMap::LEAF_LENGTH - 1);

	for (int rootIdx = 0; rootIdx < PageHeap::PageMap::ROOT_LENGTH; rootIdx++)
	{
		PageHeap::PageMap::Leaf* rootLeaf = Static::pageheap()->pagemap_.root_[rootIdx];
		if (rootLeaf == NULL)
			continue;

		for (int leafIdx = 0; leafIdx < PageHeap::PageMap::LEAF_LENGTH; leafIdx++)
		{
			tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)rootLeaf->values[leafIdx];
			if (span != NULL)
			{
				int expectedStartPage = (rootIdx * PageHeap::PageMap::LEAF_LENGTH) + leafIdx;
				ObjReportHandleSpan(span, expectedStartPage, objectCount, freeSize, sizeMap);
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
				tcmalloc_obj::Span* span = (tcmalloc_obj::Span*)node2->ptrs[pageIdx3];
				if (span != NULL)
				{
					int expectedStartPage = ((pageIdx1 * PageHeap::PageMap::INTERIOR_LENGTH) + pageIdx2) * PageHeap::PageMap::LEAF_LENGTH + pageIdx3;
					ObjReportHandleSpan(span, expectedStartPage, objectCount, freeSize, sizeMap);
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

void BFGC::Report()
{
	AutoCrit autoCrit(mCritSect);

	CheckTcIntegrity();

	BP_ZONE("Report");

	Beefy::String msg;

	int objectCount = 0;

#ifdef BF_GC_VERIFY_SWEEP_IDS
	maxAllocNum = bf::System::Object::sCurAllocNum;
	allocIdSet.clear();
#endif

	int leafCheckCount = 0;
	bool overflowed = false;

	Dictionary<bf::System::Type*, AllocInfo> sizeMap;
	intptr objFreeSize = 0;

	ObjReportScan(objectCount, objFreeSize, sizeMap);

	std::multimap<AllocInfo, bf::System::Type*> orderedSizeMap;
	for (auto& pair : sizeMap)
	{
		orderedSizeMap.insert(std::make_pair(pair.mValue, pair.mKey));
	}

	msg += "Overall GC Summary\n";
	//msg += Beefy::StrFormat("  TotalAllocs                                                  %d\n", mTotalAllocs);
	//msg += Beefy::StrFormat("  TotalAllocs - TotalFrees                                     %d\n", mTotalAllocs - mTotalFrees);
	msg += Beefy::StrFormat("  System Memory Taken                                            %dk\n", (int)(TCMalloc_SystemTaken / 1024));
	//msg += Beefy::StrFormat("  BytesRequested                                               %dk\n", (int)(mBytesRequested / 1024));
	msg += Beefy::StrFormat("  Live Objects                                                   %d\n", objectCount);
	msg += Beefy::StrFormat("  Last Object Freed Count                                        %d\n", mLastFreeCount);

	intptr objReportedCount = 0;
	intptr rawReportedCount = 0;
	intptr rawFreeSize = 0;
	intptr objTotalSize = 0;
	intptr rawTotalSize = 0;
	gBFGC.RawReport(msg, rawFreeSize, orderedSizeMap);
	for (auto& pair : orderedSizeMap)
	{
		objTotalSize += pair.first.mObjSize;
		objReportedCount += pair.first.mObjCount;
		rawTotalSize += pair.first.mRawSize;
		rawReportedCount += pair.first.mRawCount;
	}

	msg += Beefy::StrFormat("  Obj Scanned Alloc Count                                        %d\n", (int)(objReportedCount));
	msg += Beefy::StrFormat("  Raw Scanned Alloc Count                                        %d\n", (int)(rawReportedCount));
	msg += Beefy::StrFormat("  Obj Used Memory                                                %dk\n", (int)(objTotalSize / 1024));
	msg += Beefy::StrFormat("  Raw Used Memory                                                %dk\n", (int)(rawTotalSize / 1024));
	msg += Beefy::StrFormat("  Obj Unusued Memory                                             %dk\n", (int)(objFreeSize / 1024));
	msg += Beefy::StrFormat("  Raw Unusued Memory                                             %dk\n", (int)(rawFreeSize / 1024));


	if (!mCollectReports.IsEmpty())
	{
		for (int reportIdx = 0; reportIdx < (int)mCollectReports.size(); reportIdx++)
		{
			auto& report = mCollectReports[reportIdx];

			msg += Beefy::StrFormat("  Collection %d Total: %dms Paused: %dms CollectCount: %d", report.mCollectIdx, report.mTotalMS, report.mPausedMS, report.mCollectCount);

			if (reportIdx > 0)
			{
				msg += Beefy::StrFormat(" SinceLast: %dms", report.mStartTick - mCollectReports[reportIdx - 1].mStartTick);
			}

			msg += "\n";
		}

		msg += Beefy::StrFormat("  Average Time Between Collections                               %dms\n", BFTickCount() / mCollectIdx);
	}

	msg += "Types                                                                  Size   Count\n";
	for (auto& pair : orderedSizeMap)
	{
		bf::System::Type* type = pair.second;
		Beefy::String typeName;
		if (type == NULL)
			typeName = "NULL";
		else
			typeName = type->GetFullName();

		if (pair.first.mObjCount > 0)
			msg += StrFormat("OBJ %-62s %7dk %7d\n", typeName.c_str(), (pair.first.mObjSize + 1023) / 1024, pair.first.mObjCount);
		if (pair.first.mRawCount > 0)
			msg += StrFormat("RAW %-62s %7dk %7d\n", typeName.c_str(), (pair.first.mRawSize + 1023) / 1024, pair.first.mRawCount);
	}

	Beefy::OutputDebugStr(msg.c_str());

	BFGCLogWrite();
}

void BFGC::ReportTLSMember(int tlsIndex, void* ptr, void* markFunc)
{
	if (mMainThreadTLSPtr == NULL)
	{
		_TEB* teb = NtCurrentTeb();
		mMainThreadTLSPtr = teb->ThreadLocalStorage;
	}

	TLSMember tlsMember;
	tlsMember.mTLSOffset = (uint8*)ptr - ((uint8**)mMainThreadTLSPtr)[tlsIndex];
	tlsMember.mMarkFunc = markFunc;
	tlsMember.mTLSIndex = tlsIndex;
	mTLSMembers.Add(tlsMember);
}

void BFGC::SuspendThreads()
{
	BP_ZONE("TriggerCollection - SuspendThreads");
	auto curThreadId = GetCurrentThreadId();
	for (auto thread : mThreadList)
	{
		if ((thread->mThreadId != curThreadId) && (!thread->mExcluded) && (thread->mRunning) && (thread->WantsSuspend()))
		{
			// We must lock this before suspending so we can access mStackMarkableObjects
			//  Otherwise we could deadlock
			thread->mCritSect.Lock();
			thread->mSuspended = true;

			BfpThreadResult result;
			BfpThread_Suspend(thread->mThreadHandle, &result);
			ASSERT(result == BfpThreadResult_Ok);
		}
	}
}

void BFGC::ResumeThreads()
{
	BP_ZONE("TriggerCollection - ResumeThreads");
	auto curThreadId = GetCurrentThreadId();
	for (auto thread : mThreadList)
	{
		if ((thread->mThreadId != curThreadId) && (thread->mSuspended) && (thread->mRunning) && (thread->WantsSuspend()))
		{
			// Previously locked in SuspendThreads
			thread->mCritSect.Unlock();

			thread->mSuspended = false;
			BfpThread_Resume(thread->mThreadHandle, NULL);
		}
	}
}

void BFGC::PerformCollection()
{
	BP_ZONE("TriggerCollection");

	if (mCollectIdx == 0)
	{
		// 'Prime' register capture
		intptr regVals[128];
		intptr stackPtr = 0;
		BfpThreadResult threadResult;
		int regValCount = 128;
		BfpThread_GetIntRegisters(BfpThread_GetCurrent(), &stackPtr, regVals, &regValCount, &threadResult);
	}

	int prevMarkId = mCurMarkId;

	DWORD startTick = BFTickCount();
	CollectReport collectReport;
	collectReport.mCollectIdx = mCollectIdx;
	collectReport.mStartTick = startTick;

#ifndef BF_MINGW
	//_CrtCheckMemory();
#endif

#ifdef BF_GC_INCREMENTAL
	mFullGCTriggered = true;
	mForceDecommit |= forceDecommit;
	gBFGC.mCollectEvent.Set();
#else
	Beefy::AutoCrit autoCrit(mCritSect);
	mAllocSinceLastGC = 0;
	mCollectFailed = false;

	// This was old "emergency" debugging code to make sure we weren't doing a malloc in the GC code,
	//  but it's a multi-threaded race condition
	/*uint8* mallocAddr = (uint8*)&malloc;
	DWORD oldProtect = 0;
	BOOL worked = ::VirtualProtect(mallocAddr, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
	uint8 oldCode = *mallocAddr;
	*mallocAddr = 0xCC;*/

	mOrderedPendingGCData.Reserve(BF_GC_MAX_PENDING_OBJECT_COUNT);

	uint32 suspendStartTick = BFTickCount();
	SuspendThreads();

#ifndef BF_MINGW
	//_CrtCheckMemory();
#endif
	DoCollect(true);
#ifndef BF_MINGW
	//_CrtCheckMemory();
#endif

	//*mallocAddr = oldCode;

#ifdef BF_GC_EMPTYSCAN
	{
		mSweepInfo.mEmptyScan = true;
		Sweep();
		mSweepInfo.mEmptyScan = false;
	}
#endif

	mFreeSinceLastGC = 0;

	BFLOG2(GCLog::EVENT_GC_UNFREEZE, sCurMarkId, BfpSystem_TickCount());

#ifndef BF_GC_DEBUGSWEEP
	ResumeThreads();
#endif

	collectReport.mPausedMS = BFTickCount() - suspendStartTick;

	//BFGCLogWrite();

	mFinalizeList.Clear();
	Sweep();

#ifdef BF_GC_DEBUGSWEEP
	ResumeThreads();
#endif

	collectReport.mCollectCount = (int)mFinalizeList.size();
	FinishCollect();
	ReleasePendingObjects();
	ProcessSweepInfo();

	BFLOG2(GCLog::EVENT_GC_DONE, mCurGCMarkCount, mHadPendingGCDataOverflow ? 1 : 0);

	collectReport.mTotalMS = BFTickCount() - startTick;

// 	while (mCollectReports.size() > 4)
// 		mCollectReports.RemoveAt(0);

	 while (mCollectReports.size() > 10)
 		mCollectReports.RemoveAt(0);

	mCollectReports.Add(collectReport);
	mCollectIdx++;
#endif
}

void BFGC::Collect(bool async)
{
	mCollectRequested = true;
	BF_FULL_MEMORY_FENCE();
	if (async)
	{
		mCollectEvent.Set();
	}
	else
	{
		if (mPerformingCollection)
			mCollectDoneEvent.WaitFor(0); // Wait for previous to finish
		mCollectDoneEvent.Reset();
		mCollectEvent.Set();
		mCollectDoneEvent.WaitFor();
	}
}

void BFGC::WriteDebugDumpState()
{
	struct DebugInfo
	{
		bf::System::Type* mType;
		int mCount;
		int mSize;
		int mAllocSize;

		DebugInfo()
		{
			mType = NULL;
			mCount = 0;
			mSize = 0;
			mAllocSize = 0;
		}
	};

	std::vector<DebugInfo> debugInfoVector;
	for (int i = 0; i < (int)mFinalizeList.size(); i++)
	{
		bf::System::Object* obj = (bf::System::Object*) mFinalizeList[i];

		if ((bf::System::Type*)obj->GetTypeSafe() != NULL)
		{
			if ((uintptr)obj->GetTypeSafe() <= 1024U*1024U)
			{
				while ((int) debugInfoVector.size() <= 0)
					debugInfoVector.push_back(DebugInfo());

				DebugInfo* debugInfo = &debugInfoVector[0];
				debugInfo->mType = NULL;
				debugInfo->mCount++;
				int objSize = BFGetObjectSize(obj);
				debugInfo->mSize += objSize;
				debugInfo->mAllocSize += objSize;
				//debugInfo->mAllocSize += MallocExtension::instance()->GetEstimatedAllocatedSize(objSize);
			}
			else
			{
				//const bf::System::Type* bfTypeRootData = ((bf::System::Type*)obj->GetTypeSafe())->mTypeRootData;
				bf::System::Type* bfType = obj->GetTypeSafe();
				while ((int) debugInfoVector.size() <= bfType->mTypeId)
					debugInfoVector.push_back(DebugInfo());

				DebugInfo* debugInfo = &debugInfoVector[bfType->mTypeId];
				debugInfo->mType = obj->GetTypeSafe();
				debugInfo->mCount++;
				int objSize = BFGetObjectSize(obj);
				debugInfo->mSize += objSize;
				debugInfo->mAllocSize += objSize;
				//debugInfo->mAllocSize += MallocExtension::instance()->GetEstimatedAllocatedSize(objSize);
			}
		}
	}

	typedef std::multimap<int, DebugInfo*> DebugInfoMap;
	DebugInfoMap debugInfoMap;

	for (int i = 0; i < (int) debugInfoVector.size(); i++)
	{
		DebugInfo* debugInfo = &debugInfoVector[i];
		if (debugInfo->mCount > 0)
			debugInfoMap.insert(DebugInfoMap::value_type(-debugInfo->mSize, debugInfo));
	}

	Beefy::String dbgStr = "\n\nBeefyRT GC DebugDump:\n";
	int countTotal = 0;
	int sizeTotal = 0;
	int allocSizeTotal = 0;
	DebugInfoMap::iterator itr = debugInfoMap.begin();
	while (itr != debugInfoMap.end())
	{
		DebugInfo* debugInfo = itr->second;

		Beefy::String lineStr = StrFormat("%8d %8dk %8dk  ", debugInfo->mCount, (debugInfo->mSize + 1023) / 1024, (debugInfo->mAllocSize + 1023) / 1024);
		Beefy::String typeName;
		if (debugInfo->mType != NULL)
			typeName = debugInfo->mType->GetFullName();
		else
			typeName = "???";

		lineStr += typeName;
		dbgStr += lineStr += "\n";
		countTotal += debugInfo->mCount;
		sizeTotal += debugInfo->mSize;
		allocSizeTotal += debugInfo->mAllocSize;
		++itr;
	}
	dbgStr += StrFormat("%8d %8dk %8dk  TOTAL", countTotal, (sizeTotal + 1023)/1024, (allocSizeTotal + 1023)/1024);

	OutputDebugStrF(dbgStr.c_str());
}

#ifdef BF_DEBUG
//#define NO_QUEUE_OBJECTS
#endif

#ifdef BF_GC_LOG_ENABLED
static bf::System::Object* gMarkingObject[8192];
#endif

void BFGC::MarkFromGCThread(bf::System::Object* obj)
{
	if (obj == NULL)
		return;

	//BP_ZONE("MarkFromGCThread");

	void* addr = obj;
	if ((addr < tcmalloc_obj::PageHeap::sAddressStart) || (addr >= tcmalloc_obj::PageHeap::sAddressEnd))
		return;

	tcmalloc_obj::Span* span = TCGetSpanAt(obj);
	if (span == NULL)
		return;
	if (span->location != tcmalloc_obj::Span::IN_USE)
		return;

	intptr pageSize = (intptr) 1 << kPageShift;
	intptr spanSize = pageSize * span->length;
	void* spanStart = (void*)((intptr)span->start << kPageShift);
	void* spanEnd = (void*)((intptr)spanStart + spanSize);

	if ((addr < spanStart) || (addr > (uint8*)spanEnd - sizeof(bf::System::Object)))
		return;

	// Is it already marked? Ignore.
	if ((obj->mObjectFlags & BF_OBJECTFLAG_MARK_ID_MASK) == mCurMarkId)
		return;
	// Don't do any processing of non-allocated objects (like string literals), or append allocs
	if ((obj->mObjectFlags & BF_OBJECTFLAG_ALLOCATED) == 0)
		return;
	if (obj->mAllocCheckPtr == 0) // It IS in the heap but not allocated
		return;

	bool curIsDeleted = false;
	if (mMarkingDeleted)
	{
		if ((obj->mObjectFlags & BF_OBJECTFLAG_DELETED) == 0)
		{
			// Don't allow a deleted object to mark a non-deleted object-
			//  That should be handled as a LEAK if there aren't any non-deleted objects referencing it
			return;
		}
	}

	intptr elementSize = Static::sizemap()->ByteSizeForClass(span->sizeclass);
	// Large alloc
	if (elementSize == 0)
	{
		if (obj != (bf::System::Object*)spanStart)
			return;
	}
	else
	{
		void* maskedAddr = addr;
		maskedAddr = (uint8*)spanStart + (((uint8*)maskedAddr - (uint8*)spanStart) / elementSize * elementSize);
		if (obj != (bf::System::Object*)maskedAddr)
			return;
	}

#ifndef BF_GC_DISABLED
	BF_LOGASSERT(mThreadId == BfpThread_GetCurrentId());
	BF_LOGASSERT(obj->mClassVData != 0);
#ifdef TARGET_TYPE
	if (obj->mBFVData->mType == TARGET_TYPE)
	{
		gMarkTargetCount++;
		printf("Marking target %p\n", obj);
	}
#endif


#ifdef BF_GC_LOG_ENABLED
	bf::System::Object* parentObj = NULL;
	if (mMarkDepthCount > 0)
		parentObj = gMarkingObject[mMarkDepthCount-1];
	BFLOG3(GCLog::EVENT_MARK, (intptr)obj, obj->mObjectFlags, (intptr)parentObj);
#endif

	obj->mObjectFlags = (BfObjectFlags)((obj->mObjectFlags & ~BF_OBJECTFLAG_MARK_ID_MASK) | mCurMarkId);
	mCurGCMarkCount++;

	mCurGCObjectQueuedCount++;
	mCurPendingGCSize++;

	bool allowQueue = true;

	if (mOrderedPendingGCData.GetFreeCount() > 0)
	{
		mOrderedPendingGCData.Add(obj);
	}
	else
	{
		// No more room left -- we can't queue...
		mHadPendingGCDataOverflow = true;
		mCollectFailed = true;
	}
#endif
}

void BFGC::SetAutoCollectPeriod(int periodMS)
{
	mFullGCPeriod = periodMS;
	mCollectEvent.Set();
}

void BFGC::SetCollectFreeThreshold(int freeBytes)
{
	mFreeTrigger = freeBytes;
	mCollectEvent.Set();
}

void BFGC::SetMaxPausePercentage(int maxPausePercentage)
{
	mMaxPausePercentage = maxPausePercentage;
	mCollectEvent.Set();
}

void BFGC::SetMaxRawDeferredObjectFreePercentage(intptr maxPercentage)
{
	mMaxRawDeferredObjectFreePercentage = maxPercentage;
}

void BFGC::ExcludeThreadId(intptr threadId)
{
	Beefy::AutoCrit autoCrit(mCritSect);
	for (auto thread : mThreadList)
	{
		if (thread->mThreadId == threadId)
			thread->mExcluded = true;
	}
}

using namespace bf::System;

void GC::Run()
{
#ifdef BF_GC_INCREMENTAL
	gBFGC.Run();
#endif
}

void GC::Init()
{
	gBFGC.Init();
}

void GC::ReportTLSMember(intptr tlsIndex, void* ptr, void* markFunc)
{
	gBFGC.ReportTLSMember((int)tlsIndex, ptr, markFunc);
}

void GC::StopCollecting()
{
	gBFGC.StopCollecting();
}

void GC::AddStackMarkableObject(Object* obj)
{
	gBFGC.AddStackMarkableObject(obj);
}

void GC::AddPendingThread(void* internalThreadInfo)
{
	gBFGC.AddPendingThread((BfInternalThread*)internalThreadInfo);
}

void GC::RemoveStackMarkableObject(Object* obj)
{
	gBFGC.RemoveStackMarkableObject(obj);
}

void GC::Shutdown()
{
	gBFGC.Shutdown();
}

void GC::Collect(bool async)
{
    gBFGC.Collect(async);
}

void GC::Report()
{
	gBFGC.Report();
}

void GC::Mark(Object* obj)
{
	gBFGC.MarkFromGCThread(obj);
}

void GC::Mark(void* ptr, intptr size)
{
	gBFGC.ConservativeScan(ptr, (int)size);
}

void GC::DebugDumpLeaks()
{
	gBFGC.DebugDumpLeaks();
}

void GC::SetAutoCollectPeriod(intptr periodMS)
{
	gBFGC.SetAutoCollectPeriod((int)periodMS);
}

void GC::SetCollectFreeThreshold(intptr freeBytes)
{
	gBFGC.SetCollectFreeThreshold((int)freeBytes);
}

BFRT_EXPORT void bf::System::GC::SetMaxPausePercentage(intptr maxPausePercentage)
{
	gBFGC.SetMaxPausePercentage(maxPausePercentage);
}

BFRT_EXPORT void bf::System::GC::SetMaxRawDeferredObjectFreePercentage(intptr maxPercentage)
{
	gBFGC.SetMaxRawDeferredObjectFreePercentage(maxPercentage);
}

BFRT_EXPORT void bf::System::GC::ExcludeThreadId(intptr threadId)
{
	gBFGC.ExcludeThreadId(threadId);
}

#else // BF_GC_SUPPORTED

void* BfObjectAllocate(intptr size, bf::System::Type* type)
{
    BF_FATAL("Not supported");
    return NULL;
}

using namespace bf::System;

void GC::Run()
{
#ifdef BF_GC_INCREMENTAL
    gBFGC.Run();
#endif
}

void GC::Init()
{
}

void GC::ReportTLSMember(intptr tlsIndex, void* ptr, void* markFunc)
{
}

void GC::Shutdown()
{
}

void GC::Collect(bool async)
{
}

void GC::Report()
{
}

/*void GC::Mark(Object* obj)
{
}*/

void GC::Mark(void* ptr, intptr size)
{
}

void GC::DebugDumpLeaks()
{
}

void GC::SetAutoCollectPeriod(intptr periodMS)
{
}

void GC::SetCollectFreeThreshold(intptr freeBytes)
{
}

#endif
