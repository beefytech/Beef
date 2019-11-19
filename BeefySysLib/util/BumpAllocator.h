#pragma once

//#define BUMPALLOC_ETW_TRACING

#include "../Common.h"

//#define BUMPALLOC_TRACKALLOCS

#ifdef BUMPALLOC_ETW_TRACING
#include "VSCustomNativeHeapEtwProvider.h"
#endif

#ifdef BUMPALLOC_TRACKALLOCS
#include "Dictionary.h"
#endif

NS_BF_BEGIN

#ifdef BUMPALLOC_TRACKALLOCS
struct BumpAllocTrackedEntry
{
	int mCount;
	int mSize;

	BumpAllocTrackedEntry()
	{
		mCount = 0;
		mSize = 0;
	}
};
#endif

template <int ALLOC_SIZE = 0x2000>
class BumpAllocatorT
{
public:
	Array<uint8*> mPools;
	Array<int> mSizes;
	uint8* mCurAlloc;
	uint8* mCurPtr;	
	int mCurChunkNum;
	int mPrevSizes;	
	bool mBumpAlloc;
	bool mDisableDebugTracing;
#ifdef BUMPALLOC_ETW_TRACING
	static VSHeapTracker::CHeapTracker* mVSHeapTracker;	
#endif
#ifdef BUMPALLOC_TRACKALLOCS
	Dictionary<String, BumpAllocTrackedEntry> mTrackedAllocs;
#endif

public:
	BumpAllocatorT()
	{
#ifdef BUMPALLOC_ETW_TRACING
		if (mVSHeapTracker == NULL)
			mVSHeapTracker = new VSHeapTracker::CHeapTracker("BumpAllocator");
#endif
		mCurAlloc = NULL;
		mCurPtr = (uint8*)(intptr)ALLOC_SIZE;		
		mBumpAlloc = true;
		mDisableDebugTracing = false;
		mCurChunkNum = -1;
		mPrevSizes = 0;		
	}

	~BumpAllocatorT()
	{		
		Clear();		
	}	

	void Clear()
	{
		mCurAlloc = NULL;
		mCurPtr = (uint8*)(intptr)ALLOC_SIZE;
		for (auto ptr : mPools)
			free(ptr);
		mPools.Clear();
		mSizes.Clear();
		mCurChunkNum = -1;
		mPrevSizes = 0;

#ifdef BUMPALLOC_TRACKALLOCS
		mTrackedAllocs.Clear();
#endif
	}

	int GetAllocSize() const
	{
		return (int)(((mPools.size() - 1) * ALLOC_SIZE) + (mCurPtr - mCurAlloc));
	}

	int GetTotalAllocSize() const
	{
		return (int)mPools.size() * ALLOC_SIZE;
	}

	int CalcUsedSize() const
	{
		int totalSize = 0;
		for (auto& size : mSizes)
			totalSize += size;
		totalSize += mCurPtr - mCurAlloc;
		return totalSize;
	}

#ifdef BUMPALLOC_ETW_TRACING
	__declspec(allocator) void* RecordAlloc(void* addr, int size)
	{
		if (!mDisableDebugTracing)
			mVSHeapTracker->AllocateEvent(addr, size);
		return addr;
	}
#endif


	bool ContainsPtr(void* ptr) const
	{
		for (auto poolPtr : mPools)
		{
			if ((ptr >= poolPtr) && (ptr < poolPtr + ALLOC_SIZE))
				return true;
		}
		return false;
	}

	void GrowPool()
	{
		mCurChunkNum = (int)mPools.size();
		int curSize = (int)(mCurPtr - mCurAlloc);
		mPrevSizes += curSize;
		mSizes.push_back(curSize);
		mCurAlloc = (uint8*)malloc(ALLOC_SIZE);			
		memset(mCurAlloc, 0, ALLOC_SIZE);
		mPools.push_back(mCurAlloc);			
		mCurPtr = mCurAlloc;		
	}

	template <typename T>
	T* Alloc(int extraBytes = 0)
	{	
		int alignSize = alignof(T);
		mCurPtr = (uint8*)(((intptr)mCurPtr + alignSize - 1) & ~(alignSize - 1));

		//AutoPerf perf("Alloc");				
		int wantSize = sizeof(T) + extraBytes;

#ifdef BUMPALLOC_TRACKALLOCS
		const char* name = typeid(T).name();
		BumpAllocTrackedEntry* allocSizePtr;
		mTrackedAllocs.TryAdd(name, NULL, &allocSizePtr);
		allocSizePtr->mCount++;
		allocSizePtr->mSize += wantSize;
#endif

		if (mCurPtr + wantSize >= mCurAlloc + ALLOC_SIZE)
			GrowPool();

#ifdef BUMPALLOC_ETW_TRACING
		//mVSHeapTracker->AllocateEvent(retVal, wantSize);
		T* addr = (T*)RecordAlloc(mCurPtr, wantSize);
		T* retVal = new (addr) T();
#else
		T* retVal = new (mCurPtr) T();
#endif
		//mWriteCurPtr = (uint8*)(((intptr)mWriteCurPtr + wantSize + 7) & ~7);
		mCurPtr += wantSize;

		return retVal;
	}

	int GetChunkedId(void* ptr) const
	{
		return (mCurChunkNum * ALLOC_SIZE) + (int)((uint8*)ptr - mCurAlloc);
	}

	uint8* GetChunkedPtr(int id) const
	{
		int chunkNum = id / ALLOC_SIZE;
		int chunkOfs = id % ALLOC_SIZE;
		return mPools[chunkNum] + chunkOfs;
	}

	int GetStreamPos(void* ptr) const
	{
		return mPrevSizes + (int)((uint8*)ptr - mCurAlloc);
	}

	uint8* AllocBytes(intptr wantSize, int alignSize, const char* dbgName = "AllocBytes")
	{
		mCurPtr = (uint8*)(((intptr)mCurPtr + alignSize - 1) & ~(alignSize - 1));

		uint8* retVal = AllocBytes(wantSize, dbgName);
		return retVal;
	}

	uint8* AllocBytes(intptr wantSize, const char* dbgName = "AllocBytes")
	{
#ifdef BUMPALLOC_TRACKALLOCS		
		BumpAllocTrackedEntry* allocSizePtr;
		mTrackedAllocs.TryAdd(dbgName, NULL, &allocSizePtr);
		allocSizePtr->mCount++;
		allocSizePtr->mSize += wantSize;
#endif

		if (wantSize > ALLOC_SIZE / 2)
		{
			uint8* bigData = (uint8*)malloc(wantSize);
			memset(bigData, 0, wantSize);
			mPools.push_back(bigData);
			return bigData;
		}

		//AutoPerf perf("Alloc");
		if (mCurPtr + wantSize >= mCurAlloc + ALLOC_SIZE)
			GrowPool();

#ifdef BUMPALLOC_ETW_TRACING
		uint8* retVal = (uint8*)RecordAlloc(mCurPtr, wantSize);		
#else
		uint8* retVal = mCurPtr;
#endif		
		mCurPtr += wantSize;

		return retVal;
	}
};

class BumpAllocator : public BumpAllocatorT<0x2000>
{

};

template <typename T>
class AllocatorBump
{
public:
	BumpAllocator* mAlloc;

	AllocatorBump()
	{
		mAlloc = NULL;
	}

	T* allocate(intptr count)
	{
		return (T*)mAlloc->AllocBytes((int)(sizeof(T) * count), alignof(T));
	}

	void deallocate(T* ptr)
	{		
	}

	void* rawAllocate(intptr size)
	{
		return mAlloc->AllocBytes(size, 16);
	}

	void rawDeallocate(void* ptr)
	{
		
	}
};


NS_BF_END

