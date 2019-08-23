#include "BfAstAllocator.h"
#include "BfSource.h"
#include "BfSystem.h"

//Craps();

USING_NS_BF;

BfBitSet::BfBitSet()
{
	mBits = NULL;
}

BfBitSet::~BfBitSet()
{
	delete [] mBits;
}

void BfBitSet::Init(int numBits)
{
	BF_ASSERT(mBits == NULL);
	int numInts = (numBits + 31) / 32;
	mBits = new uint32[numInts];
	memset(mBits, 0, numInts * 4);
}


bool BfBitSet::IsSet(int idx)
{
	return (mBits[idx / 32] & (1 << (idx % 32))) != 0;
}

void BfBitSet::Set(int idx)
{	
	mBits[idx / 32] |= (1 << (idx % 32));
}

void BfBitSet::Clear(int idx)
{	
	mBits[idx / 32] &= ~(1 << (idx % 32));
}

//////////////////////////////////////////////////////////////////////////

BfAstAllocator::BfAstAllocator()
{	
	mSourceData = NULL;
	mCurPtr = NULL;	
	mCurPageEnd = mCurPtr;	
	mLargeAllocSizes = 0;
	mNumPagesUsed = 0;	
	mUsedSize = 0;
}

BfAstAllocator::~BfAstAllocator()
{	
	for (auto addr : mLargeAllocs)
		delete [] (uint8*)addr;
	if (mPages.size() != 0)
		mSourceData->mAstAllocManager->FreePages(mPages);
}

void BfAstAllocator::InitChunkHead(int wantSize)
{		
	mCurPtr = mSourceData->mAstAllocManager->AllocPage();
	mPages.push_back(mCurPtr);
	mCurPageEnd = mCurPtr + BfAstAllocManager::PAGE_SIZE;
	mNumPagesUsed++;
#ifdef BF_AST_ALLOCATOR_USE_PAGES	
	BfAstPageHeader* pageHeader = (BfAstPageHeader*)mCurPtr;
	pageHeader->mSourceData = mSourceData;
	BF_ASSERT(sizeof(BfAstPageHeader) <= 16);
	mCurPtr += 16;		
#endif
}

//////////////////////////////////////////////////////////////////////////

BfAstAllocManager::BfAstAllocManager()
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	mFreePageCount = 0;
#endif
}

BfAstAllocManager::~BfAstAllocManager()
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	for (int chunkIdx = (int)mAllocChunks.size() - 1; chunkIdx >= 0; chunkIdx--)
	{
		auto chunk = mAllocChunks[chunkIdx];
		::VirtualFree(chunk, 0, MEM_RELEASE);
		//BfLog("BfAstAllocManager free %p\n", chunk);
	}
#endif
}

//TODO: Remove this 
//static int gAstChunkAllocCount = 0;
uint8* BfAstAllocManager::AllocPage()
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	AutoCrit autoCrit(mCritSect);

	if (mFreePageCount != 0)	
	{
		mFreePageCount--;
		return (uint8*)mFreePages.PopFront();
	}
	
	BF_ASSERT(mFreePages.mHead == NULL);
	
	//auto newChunk = (uint8*)::VirtualAlloc((void*)(0x4200000000 + gAstChunkAllocCount*CHUNK_SIZE), CHUNK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	//gAstChunkAllocCount++;

	auto newChunk = (uint8*)::VirtualAlloc(NULL, CHUNK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	BF_ASSERT(newChunk != NULL);
	BF_ASSERT(((intptr)newChunk & (PAGE_SIZE - 1)) == 0);	
	mAllocChunks.push_back(newChunk);	

	//BfLog("BfAstAllocManager alloc %p\n", newChunk);

	for (uint8* ptr = newChunk; ptr < newChunk + CHUNK_SIZE; ptr += PAGE_SIZE)
	{
		auto freePage = (BfAstFreePage*)ptr;
		mFreePages.PushBack(freePage);
		mFreePageCount++;
	}

	mFreePageCount--;
	return (uint8*)mFreePages.PopFront();	
#else
	return new uint8[PAGE_SIZE];
#endif
}

void BfAstAllocManager::FreePage(uint8* page)
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	AutoCrit autoCrit(mCritSect);
	mFreePageCount++;
	auto pageVal = (BfAstFreePage*)page;
	pageVal->mNext = NULL;
	mFreePages.PushFront(pageVal);
#else
	delete[] page;
#endif
}

void BfAstAllocManager::FreePages(Array<uint8*> pages)
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	AutoCrit autoCrit(mCritSect);
	for (auto page : pages)
	{
		mFreePageCount++;
		auto pageVal = (BfAstFreePage*)page;
		pageVal->mNext = NULL;
		mFreePages.PushFront(pageVal);
	}
#else
	for (auto page : pages)
		delete[] page;
#endif
}

void BfAstAllocManager::GetStats(int& allocPages, int& usedPages)
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	AutoCrit autoCrit(mCritSect);

	//mShowStats = true;

	allocPages = (int)mAllocChunks.size() * CHUNK_SIZE / PAGE_SIZE;
	usedPages = allocPages - mFreePageCount;
#else
	allocPages = 0;
	usedPages = 0;
#endif
}