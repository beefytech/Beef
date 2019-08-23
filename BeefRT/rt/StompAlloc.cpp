#ifdef BF_USE_STOMP_ALLOC

#include "BeefySysLib/Common.h"

#ifdef BF_PLATFORM_WINDOWS

#include "StompAlloc.h"
#include "BeefySysLib/util/CritSect.h"
#include <list>
#include <assert.h>

USING_NS_BF;

#define STOMP_MAGIC 0xBF12BF34

class BfBitSet
{
public:
	uint32* mBits;

public:
	BfBitSet(int numBits);
	~BfBitSet();
	
	bool IsSet(int idx);
	void Set(int idx);
	void Clear(int idx);
};


BfBitSet::BfBitSet(int numBits)
{
	int numInts = (numBits + 31) / 32;
	mBits = new uint32[numInts];
	memset(mBits, 0, numInts * 4);
}

BfBitSet::~BfBitSet()
{
	delete mBits;
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

struct SA_AllocHeader
{	
	int mNumPages;	
	int mMagic;
};

struct SA_AllocRange
{
public:
	static const int PAGE_SIZE = 4096;
	static const int NUM_PAGES = 0x8000;
	uint8* mMemory;
	int mSize;
	int mLastUsedIdx;
	BfBitSet mUsedBits;
	
public:
	SA_AllocRange() : mUsedBits(NUM_PAGES)
	{
		mMemory = (uint8*)::VirtualAlloc(0, PAGE_SIZE * NUM_PAGES, MEM_RESERVE, PAGE_READWRITE);		
		mSize = 0;
		mLastUsedIdx = -1;
	}

	~SA_AllocRange()
	{
		if (mMemory != NULL)
			::VirtualFree(mMemory, 0, MEM_RELEASE);
	}

	int FindFreeRange(int numPages, int from, int to)
	{
		int lastUsedIdx = from - 1;
		for (int pageIdx = from; pageIdx < to; pageIdx++)
		{
			if (mUsedBits.IsSet(pageIdx))
			{
				lastUsedIdx = pageIdx;
			}
			else if (pageIdx - lastUsedIdx >= numPages)
			{
				return lastUsedIdx + 1;
			}
		}
		return -1;
	}

	void* Alloc(int size)
	{
		int numPages = (size + sizeof(SA_AllocHeader) + PAGE_SIZE - 1) / PAGE_SIZE;

		int startIdx = FindFreeRange(numPages, mLastUsedIdx + 1, NUM_PAGES);
		if (startIdx == -1)
			startIdx = FindFreeRange(numPages, 0, mLastUsedIdx);
		if (startIdx == -1)
			return NULL;
		
		mLastUsedIdx = startIdx + numPages - 1;
		for (int markIdx = startIdx; markIdx < startIdx + numPages; markIdx++)
		{
			mUsedBits.Set(markIdx);
		}

		uint8* ptr = mMemory + startIdx*PAGE_SIZE;
		auto allocHeader = (SA_AllocHeader*)ptr;
		::VirtualAlloc(ptr, numPages * PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);								

		allocHeader->mNumPages = numPages;
		allocHeader->mMagic = STOMP_MAGIC;

		int alignedOffset = sizeof(SA_AllocHeader);
		bool alignAtEnd = true;
		if (alignAtEnd)
		{
			alignedOffset = (PAGE_SIZE - (size % PAGE_SIZE));
			if (alignedOffset < sizeof(SA_AllocHeader))
			{
				// For cases where the alloc size (mod PAGE_SIZE) is almost equal to the page size, we need to bump the offset into the next page
				//  so we don't clobber the SA_AllocHeader
				alignedOffset += PAGE_SIZE; 
			}
		}
		return ptr + alignedOffset;		
	}

	bool Free(void* ptr)
	{
		uint8* memPtr = (uint8*)ptr;
		if ((memPtr < mMemory) || (memPtr >= mMemory + PAGE_SIZE * NUM_PAGES))
			return false;

		int pageStart = (int)(memPtr - mMemory) / PAGE_SIZE;
		int pageOfs = (int)(memPtr - mMemory) % PAGE_SIZE;
		if (pageOfs < sizeof(SA_AllocHeader))
			pageStart--; // We actually allocated on the previous page

		SA_AllocHeader* allocHeader = (SA_AllocHeader*)(mMemory + pageStart*PAGE_SIZE);		
		assert(allocHeader->mMagic == STOMP_MAGIC);
		for (int pageIdx = pageStart; pageIdx < pageStart + allocHeader->mNumPages; pageIdx++)
		{
			assert(mUsedBits.IsSet(pageIdx));
			mUsedBits.Clear(pageIdx);		
		}
		::VirtualFree(allocHeader, allocHeader->mNumPages * PAGE_SIZE, MEM_DECOMMIT);
		return true;
	}
};

static std::list<SA_AllocRange> gAllocRanges;
static CritSect gSA_CritSect;

extern "C" void* StompAlloc(intptr size)
{
	AutoCrit autoCrit(gSA_CritSect);

	while (true)
	{
		for (auto itr = gAllocRanges.rbegin(); itr != gAllocRanges.rend(); itr++)
		{
			auto& alloc = *itr;
			void* result = alloc.Alloc((int)size);
			if (result != NULL)
				return result;
		}

		gAllocRanges.resize(gAllocRanges.size() + 1);
	}		
}

extern "C" void StompFree(void* addr)
{	
	AutoCrit autoCrit(gSA_CritSect);

	for (auto& alloc : gAllocRanges)
	{
		if (alloc.Free(addr))
			return;
	}

	assert("Invalid address" == 0);
}

#endif //BF_PLATFORM_WINDOWS

#endif