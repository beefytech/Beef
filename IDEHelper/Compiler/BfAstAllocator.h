#pragma once

//#define BF_USE_NEAR_NODE_REF

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/SLIList.h"
#include "../Beef/BfCommon.h"

#ifdef BF_PLATFORM_WINDOWS
#define BF_AST_ALLOCATOR_USE_PAGES
#endif

/*#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "llvm/ADT/SmallVector.h"

#pragma warning(pop)*/

NS_BF_BEGIN

class BfSource;
class BfSourceData;

class BfBitSet
{
public:
	uint32* mBits;

public:
	BfBitSet();
	~BfBitSet();

	void Init(int numBits);
	bool IsSet(int idx);
	void Set(int idx);
	void Clear(int idx);
};

#ifdef BF_AST_ALLOCATOR_USE_PAGES
class BfAstPageHeader
{
public:
	BfSourceData* mSourceData;
};
#endif

class BfAstAllocChunk;
class BfAstAllocManager;

struct BfAstFreePage
{
	BfAstFreePage* mNext;
};

class BfAstAllocManager
{
public:
	static const int CHUNK_SIZE = 1024*1024;
	static const int PAGE_SIZE = 4096;

#ifdef BF_AST_ALLOCATOR_USE_PAGES
	CritSect mCritSect;
	SLIList<BfAstFreePage*> mFreePages;
	int mFreePageCount;
	Array<uint8*> mAllocChunks;
#endif

public:
	BfAstAllocManager();
	~BfAstAllocManager();

	uint8* AllocPage();
	void FreePage(uint8* page);
	void FreePages(Array<uint8*> pages);
	void GetStats(int& allocPages, int& usedPages);
};

// #ifdef BUMPALLOC_TRACKALLOCS
// struct BumpAllocTrackedEntry
// {
// 	int mCount;
// 	int mSize;
//
// 	BumpAllocTrackedEntry()
// 	{
// 		mCount = 0;
// 		mSize = 0;
// 	}
// };
// #endif

class BfAstAllocator
{
public:
	static const int LARGE_ALLOC_SIZE = 2048;
	BfSourceData* mSourceData;
	uint8* mCurPtr;
	uint8* mCurPageEnd;
	Array<void*> mLargeAllocs;
	Array<uint8*> mPages;
	int mLargeAllocSizes;
	int mNumPagesUsed;
	int mUsedSize;
#ifdef BUMPALLOC_TRACKALLOCS
	Dictionary<String, BumpAllocTrackedEntry> mTrackedAllocs;
#endif

public:
	BfAstAllocator();
	~BfAstAllocator();

	void InitChunkHead(int wantSize);

	int GetAllocSize() const
	{
		return (int)(((mPages.size() - 1) * BfAstAllocManager::PAGE_SIZE) + (BfAstAllocManager::PAGE_SIZE - (mCurPageEnd - mCurPtr)));
	}

	int GetTotalAllocSize() const
	{
		return (int)mPages.size() * BfAstAllocManager::PAGE_SIZE;
	}

	int CalcUsedSize() const
	{
		return GetAllocSize();
	}

	template <typename T>
	T* Alloc(int extraBytes = 0)
	{
		int alignSize = alignof(T);
		mCurPtr = (uint8*)(((intptr)mCurPtr + alignSize - 1) & ~(alignSize - 1));
		int wantSize = sizeof(T) + extraBytes;

#ifdef BUMPALLOC_TRACKALLOCS
		const char* name = typeid(T).name();
		BumpAllocTrackedEntry* allocSizePtr;
		mTrackedAllocs.TryAdd(name, NULL, &allocSizePtr);
		allocSizePtr->mCount++;
		allocSizePtr->mSize += wantSize;
#endif

		if (mCurPtr + wantSize >= mCurPageEnd)
			InitChunkHead(wantSize);
		memset(mCurPtr, 0, wantSize);
		T* retVal = new (mCurPtr) T();
		mCurPtr += wantSize;

#ifndef BF_AST_ALLOCATOR_USE_PAGES
		retVal->mSourceData = this->mSourceData;
#endif

		return retVal;
	}

	uint8* AllocBytes(int wantSize, int alignSize, const char* dbgName = "AllocBytes")
	{
#ifdef BUMPALLOC_TRACKALLOCS
		BumpAllocTrackedEntry* allocSizePtr;
		mTrackedAllocs.TryAdd(dbgName, NULL, &allocSizePtr);
		allocSizePtr->mCount++;
		allocSizePtr->mSize += wantSize;
#endif

#ifndef BF_USE_NEAR_NODE_REF
		if (wantSize >= LARGE_ALLOC_SIZE)
		{
			mLargeAllocSizes += wantSize;
			uint8* addr = new uint8[wantSize];
			mLargeAllocs.push_back(addr);
			return addr;
		}
#endif

		mCurPtr = (uint8*)(((intptr)mCurPtr + alignSize - 1) & ~(alignSize - 1));
		if (mCurPtr + wantSize >= mCurPageEnd)
			InitChunkHead(wantSize);
		memset(mCurPtr, 0, wantSize);
		uint8* retVal = mCurPtr;
		mCurPtr += wantSize;
		return retVal;
	}

	uint8* AllocBytes(int wantSize, const char* dbgName = "AllocBytes")
	{
#ifdef BUMPALLOC_TRACKALLOCS
		BumpAllocTrackedEntry* allocSizePtr;
		mTrackedAllocs.TryAdd(dbgName, NULL, &allocSizePtr);
		allocSizePtr->mCount++;
		allocSizePtr->mSize += wantSize;
#endif

		if (wantSize >= LARGE_ALLOC_SIZE)
		{
			uint8* addr = new uint8[wantSize];
			mLargeAllocs.push_back(addr);
			return addr;
		}

		if (mCurPtr + wantSize >= mCurPageEnd)
			InitChunkHead(wantSize);
		memset(mCurPtr, 0, wantSize);
		uint8* retVal = mCurPtr;
		mCurPtr += wantSize;
		return retVal;
	}
};

NS_BF_END
