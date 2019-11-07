#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/Dictionary.h"
#include <unordered_map>
#include <map>
#include "../rt/BfObjects.h"

//#include "boost/lockfree/stack.hpp"

#ifdef BF_PLATFORM_WINDOWS
#define BF_GC_SUPPORTED
#endif

#ifdef BF_GC_SUPPORTED

class GCDbgData
{
public:
	static const int MAX_SIZE_CLASSES = 95;

public:	
	BfRtFlags mDbgFlags;
	void* mObjRootPtr;
	void* mRawRootPtr;
	void* mRawObjectSentinel;
	int mSizeClasses[MAX_SIZE_CLASSES];
};
extern "C" GCDbgData gGCDbgData;

class BfDbgInternalThread;

void* BfRawAllocate(intptr elemCount, bf::System::DbgRawAllocData* rawAllocData, void* stackTraceInfo, int stackTraceCount);
void BfRawFree(void* ptr);

void* BfObjectAllocate(intptr size, bf::System::Type* type);

void BFDumpAllocStats();

class BfInternalThread;

// Incremental/concurrent requires write barriers
//#define BF_GC_INCREMENTAL

//#define BF_GC_LOG_ENABLED
#ifdef BF_DEBUG
//#define BF_GC_LOG_ENABLED
#endif

#ifdef BF_GC_LOG_ENABLED
void BFGCLogWrite();
//void BFGCLogAlloc(bf::System::Object* obj, bf::System::Type* objType, int allocNum);
#ifdef BF_PLATFORM_WINDOWS
#define BF_LOGASSERT(_Expression) (void)( (!!(_Expression)) || (BFGCLogWrite(), 0) || (Beefy::BFFatalError(#_Expression, __FILE__, __LINE__), 0) )
#else
#define BF_LOGASSERT(_Expression) (void)( (!!(_Expression)) || (BFGCLogWrite(), 0) || (BF_ASSERT(_Expression), 0) )
#endif
#else
#define BF_LOGASSERT(_Expression)
#endif

extern HANDLE gGCHeap;

template <class T>
class GCAllocStd
{
public:
	typedef size_t    size_type;
	typedef ptrdiff_t difference_type;
	typedef T*        pointer;
	typedef const T*  const_pointer;
	typedef T&        reference;
	typedef const T&  const_reference;
	typedef T         value_type;

	GCAllocStd() {}
	GCAllocStd(const GCAllocStd&) {}

	pointer   allocate(size_type n, const void * = 0)
	{
		if (gGCHeap == NULL)
			gGCHeap = ::HeapCreate(0, 0, 0);
		return (T*)::HeapAlloc(gGCHeap, 0, n * sizeof(T));
	}

	void      deallocate(void* p, size_type)
	{
		::HeapFree(gGCHeap, 0, p);
	}
	pointer           address(reference x) const { return &x; }
	const_pointer     address(const_reference x) const { return &x; }
	GCAllocStd<T>&  operator=(const GCAllocStd&) { return *this; }
	void              construct(pointer p, const T& val)
	{
		new ((T*)p) T(val);
	}
	void              destroy(pointer p) { p->~T(); }
	size_type         max_size() const { return size_t(-1); }

	template <class U>
	struct rebind { typedef GCAllocStd<U> other; };

	template <class U>
	GCAllocStd(const GCAllocStd<U>&) {}

	template <class U>
	GCAllocStd& operator=(const GCAllocStd<U>&) { return *this; }
};

template <class T>
class GCAlloc
{
public:	
	T* allocate(intptr n)
	{
		if (gGCHeap == NULL)
			gGCHeap = ::HeapCreate(0, 0, 0);
		return (T*)::HeapAlloc(gGCHeap, 0, n * sizeof(T));
	}

	void deallocate(void* p)
	{
		::HeapFree(gGCHeap, 0, p);
	}	
};

class BFIThreadData
{
public:

};

//#define BF_OBJECTFLAG_FREED				0x10
//#define BF_OBJECTFLAG_QUEUE_FULLMARK	0x20

//If we need an extr aflag, we could use two bits to store Allocated/Stack_Alloc/Append_Alloc since those are all
//  mutually exclusive
// If ALLOCINFO is not set mAllocVal is a single stack trace entry
//  When ALLOC_INFO_SHORT is set, mDebugData is: [ObjectSize:*][MetadataSize:8][StackCount:8]
//  if any of those fields is too large then we use ALLOC_INFO, where ObjectSize takes the whole mDebugData, 
///  then at the end of the object we have: [MetadataSize:*][StackCount:16]
#define BF_OBJECTFLAG_MARK_ID_MASK      0x03
#define BF_OBJECTFLAG_ALLOCATED         0x04
#define BF_OBJECTFLAG_STACK_ALLOC       0x08
#define BF_OBJECTFLAG_APPEND_ALLOC		0x10
#define BF_OBJECTFLAG_ALLOCINFO			0x20
#define BF_OBJECTFLAG_ALLOCINFO_SHORT	0x40
#define BF_OBJECTFLAG_DELETED           0x80

namespace tcmalloc_obj
{
	struct Span;
}

namespace tcmalloc_raw
{
	struct Span;
}


class BFGC
{
public:
	struct ThreadInfo
	{
		static BF_TLS_DECLSPEC ThreadInfo* sCurThreadInfo;

		Beefy::CritSect mCritSect;
		BfpThread* mThreadHandle;
		BfpThreadId mThreadId;
		void* mTEB;
		intptr mStackStart;
		bool mRunning;
		Beefy::Array<bf::System::Object*> mStackMarkableObjects;

		ThreadInfo()
		{
			mThreadId = 0;
			mThreadHandle = NULL;
			mTEB = NULL;
			mStackStart = NULL;
			mRunning = true;
		}

		~ThreadInfo();
	};

	struct RawLeakInfo
	{
		bf::System::DbgRawAllocData* mRawAllocData;
		void* mDataPtr;
		void* mStackTracePtr;
		int mStackTraceCount;
		int mDataCount;
	};

	struct CollectReport
	{
		int mCollectIdx;
		uint32 mStartTick;
		int mTotalMS;
		int mPausedMS;
		int mCollectCount;
	};

	struct SweepInfo
	{		
		Beefy::Array<bf::System::Object* /*, GCAlloc<System::Object*> */> mLeakObjects;
		Beefy::Array<RawLeakInfo> mRawLeaks;
		int mLeakCount;
		bool mShowAllAsLeaks;
		bool mEmptyScan;

		SweepInfo()
		{
			mLeakCount = 0;
			mShowAllAsLeaks = false;
			mEmptyScan = false;
		}

		void Clear()
		{
			mLeakCount = 0;
			mShowAllAsLeaks = false;
			mLeakObjects.Clear();
			mRawLeaks.Clear();
		}
	};

	struct TLSMember
	{
		intptr mTLSOffset;
		void* mMarkFunc;
		int mTLSIndex;
	};

	struct AllocInfo
	{
		int mCount;
		int mSize;

		bool operator<(const AllocInfo &rhs) const
		{
			return mSize > rhs.mSize;
		}
	};

	enum
	{
		DEBUGDUMPSTATE_NONE,
		DEBUGDUMPSTATE_WAITING_FOR_PREV,
		DEBUGDUMPSTATE_WAITING_FOR_PREV_2,
		DEBUGDUMPSTATE_WAITING_FOR_MUTATOR,
		DEBUGDUMPSTATE_WAITING_FOR_GC
	};

	Beefy::CritSect mCritSect;	
	Beefy::SyncEvent mCollectEvent;
	Beefy::SyncEvent mCollectDoneEvent;

	BfpThread* mGCThread;
	//BfObject* mEphemeronTombstone;

	BfpThreadId mThreadId;

	volatile bool mExiting;
	volatile bool mRunning;
	bool mGracelessShutdown;
	bool mPaused;
	bool mShutdown;
	bool mWaitingForGC; // GC.Collect sets this		
	int mAllocSinceLastGC; // Added to on alloc and subtracted from on nursery cleanup
	int mFreeSinceLastGC;

	bool mFullGCTriggered;
	bool mForceDecommit;	
	bool mSkipMark;
	volatile bool mCollectRequested;
	volatile bool mPerformingCollection;	
	volatile bool mUsingThreadUnlocked;
	volatile int mDebugDumpState;
	Beefy::Array<bf::System::Object*> mExplicitRoots;
	Beefy::Array<TLSMember> mTLSMembers;
	Beefy::Array<CollectReport> mCollectReports;
	int mCollectIdx;
	void* mMainThreadTLSPtr;

	int mMultiStackScanWait; // To avoid multiple stack scans per frame
	int mFullGCPeriod; // Maximum milliseconds between GC cycles		
	int mFreeTrigger; // Bytes before a full GC is triggered
	int mMaxPausePercentage; // Maximum percentage we're allowed to stop threads
	int mMaxRawDeferredObjectFreePercentage; // Maximum percentage of heap usage to defer raw object

	int mStackScanIdx;
	bool mDoStackDeepMark;
	int mStage;
	int mLastCollectFrame;
	int mCurMarkId;
	static int volatile sCurMarkId; //0-3
	static int volatile sAllocFlags;

	Beefy::Array<bf::System::Object*> mPendingGCData;
	bool mHadPendingGCDataOverflow;
	int mCurPendingGCDepth;
	int mMaxPendingGCDepth;
	Beefy::Array<ThreadInfo*> mThreadList;
	int mCurMutatorMarkCount;
	int mCurGCMarkCount;
	int mCurGCObjectQueuedCount;
	int mCurMutatorObjectQueuedCount;
	int mCurObjectDeleteCount;	
	int mCurFinalizersCalled;	
	int mCurSweepFoundCount;
	int mCurSweepFoundPermanentCount;
	int mCurFreedBytes;
	int mCurLiveObjectCount;

	int mCurScanIdx;
	int mTotalAllocs;
	int mTotalFrees;		
	uint64 mBytesFreed;
	size_t mBytesRequested; // Consistent but race-susceptible. Fixup once per cycle from TLS data	
	bool mRequestedSizesInvalid; // Can occur if reflection data is trimmed -- only matters for debug reporting anyway
	int mMarkDepthCount;
	bool mMarkingDeleted;
	bool mQueueMarkObjects;
	int mLastFreeCount;
	Beefy::Array<bf::System::Object* /*, GCAlloc<System::Object*>*/ > mFinalizeList;
	SweepInfo mSweepInfo;
	bool mDisplayFreedObjects;
	bool mHadRootError;

public:
	void RawInit();
	void RawShutdown();
	void WriteDebugDumpState();
	bool HandlePendingGCData(Beefy::Array<bf::System::Object*>* pendingGCData);
	bool HandlePendingGCData();	

	void MarkMembers(bf::System::Object* obj);
	void AdjustStackPtr(intptr& addr, int& size);
	bool ScanThreads();
	void ReportLeak(bf::System::Object* obj);
	void SweepSpan(tcmalloc_obj::Span* span, int expectedStartPage);	
	void Sweep();
	void RawMarkSpan(tcmalloc_raw::Span* span, int expectedStartPage);
	void RawMarkAll();	
	void ProcessSweepInfo();
	void ReleasePendingSpanObjects(tcmalloc_obj::Span* span);
	void ReleasePendingObjects();
	void ConservativeScan(void* addr, int length);
	bool IsHeapObject(bf::System::Object* obj);
	void MarkStatics();	
	void ObjectDeleteRequested(bf::System::Object* obj);

	void DoCollect(bool doingFullGC);
	void FinishCollect();
	void Run();		

	static void BFP_CALLTYPE RunStub(void* gc);

	void DumpLeaksSpan(tcmalloc_obj::Span* span, int expectedStartPage, Beefy::StringImpl& msg);

public:
	BFGC();
	~BFGC();

	void Init();
	void Start();
	void StopCollecting();
	void AddStackMarkableObject(bf::System::Object* obj);
	void RemoveStackMarkableObject(bf::System::Object* obj);
	void Shutdown();	
	void InitDebugDump();
	void EndDebugDump();	
	void SuspendThreads();
	void ResumeThreads();
	void PerformCollection();
	void Collect(bool async);	
	void DebugDumpLeaks();

	void ObjReportHandleSpan(tcmalloc_obj::Span* span, int expectedStartPage, int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>& sizeMap);
	void ObjReportScan(int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>& sizeMap);
	void RawReportHandleSpan(tcmalloc_raw::Span* span, int expectedStartPage, int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>* sizeMap);
	void RawReportScan(int& objectCount, intptr& freeSize, Beefy::Dictionary<bf::System::Type*, AllocInfo>* sizeMap);
	void Report();
	void RawReport(Beefy::String& msg, intptr& freeSize, std::multimap<AllocInfo, bf::System::Type*>& orderedSizeMap);
	void ReportTLSMember(int tlsIndex, void* ptr, void* markFunc);

	//void RegisterRoot(BfObject* obj);

	void ThreadStarted(BfDbgInternalThread* thread);
	void ThreadStopped(BfDbgInternalThread* thread);

	void ThreadStarted();
	void ThreadStopped();

	void MarkFromGCThread(bf::System::Object* obj); // Can only called from within GC thread	

	void SetAutoCollectPeriod(int periodMS);
	void SetCollectFreeThreshold(int freeBytes);
	void SetMaxPausePercentage(int maxPausePercentage);
	void SetMaxRawDeferredObjectFreePercentage(intptr maxPercentage);
};

extern BFGC gBFGC;

#define BF_OBJALLOC_NO_ALLOCDONE(klass) (bfNewObject = new (BfObjectAllocate(sizeof(klass), klass::sBFTypeID)) klass(), bfNewObject->mBFVData = &klass::sClassVData, bfNewObject)
#define BF_OBJALLOC(klass) (bfNewObject = new (BfObjectAllocate(sizeof(klass), klass::sBFTypeID)) klass(), bfNewObject->mBFVData = &klass::sClassVData, bfNewObject->BFAllocDone(0), bfNewObject)
//#define BF_OBJALLOC(klass) (bfNewObject = new (BfObjectAllocate(sizeof(klass), klass::sBFTypeID)) klass(), bfNewObject->mBFVData = &klass::sClassVData, bfNewObject->BFAllocDone(0), BFCheckObjectSize(bfNewObject, sizeof(klass)), bfNewObject)
#define BF_OBJALLOC_PERMANENT(klass) (bfNewObject = new (BfObjectAllocate(sizeof(klass), klass::sBFTypeID)) klass(), bfNewObject->mBFVData = &klass::sClassVData, bfNewObject->BFAllocDone(BF_OBJECTFLAG_PERMANENT), bfNewObject)

#else //BF_GC_SUPPORTED

class BFGC
{
public:
	static const int sAllocFlags = 0;
};

void* BfObjectAllocate(intptr size, bf::System::Type* type);
void* BfRawAllocate(intptr elemCount, bf::System::DbgRawAllocData* rawAllocData, void* stackTraceInfo, int stackTraceCount);
void BfRawFree(void* ptr);

#endif

namespace bf
{
	namespace System
	{
		class Object;
		namespace Threading
		{
			class Thread;
		}

		class GC : public Object
		{
		private:
			BFRT_EXPORT static void Init();
			BFRT_EXPORT static void Run();						
			BFRT_EXPORT static void ReportTLSMember(intptr tlsIndex, void* ptr, void* markFunc);
			BFRT_EXPORT static void StopCollecting();
			BFRT_EXPORT static void AddStackMarkableObject(Object* obj);
			BFRT_EXPORT static void RemoveStackMarkableObject(Object* obj);
			
		public:
			BFRT_EXPORT static void Shutdown();			
			BFRT_EXPORT static void Collect(bool async);
			BFRT_EXPORT static void Report();
			BFRT_EXPORT static void Mark(Object* obj);
			BFRT_EXPORT static void Mark(void* ptr, intptr size);
			BFRT_EXPORT static void DebugDumpLeaks();
			//static void ToLeakString(Object* obj, String* strBuffer);
			static void DoMarkAllStaticMembers()
			{
				BFRTCALLBACKS.GC_MarkAllStaticMembers();
			}
			static bool DoCallRootCallbacks()
			{
				return BFRTCALLBACKS.GC_CallRootCallbacks();
			}
			BFRT_EXPORT static void SetAutoCollectPeriod(intptr periodMS);
			BFRT_EXPORT static void SetCollectFreeThreshold(intptr freeBytes);
			BFRT_EXPORT static void SetMaxPausePercentage(intptr maxPausePercentage);
			BFRT_EXPORT static void SetMaxRawDeferredObjectFreePercentage(intptr maxPercentage);
		};
	}
}
