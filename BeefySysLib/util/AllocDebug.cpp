#include "../Common.h"
#include "CritSect.h"
#include "util/Dictionary.h"

#ifdef DEF_BF_ALLOCDEBUG
#define USE_BF_ALLOCDEBUG
#endif

#pragma warning(disable:4996)
#include <cstdio>
#include "AllocDebug.h"

USING_NS_BF;

#ifdef DEF_BF_ALLOCDEBUG

//////////////////////////////////////////////////////////////////////////

#define USE_STOMP

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

	bool Free(void* ptr, bool leaveAllocated = false)
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
		if (leaveAllocated)
		{
			DWORD oldProtect;
			::VirtualProtect(allocHeader, allocHeader->mNumPages * PAGE_SIZE, /*PAGE_NOACCESS*/PAGE_READONLY, &oldProtect);
		}
		else
		{
			for (int pageIdx = pageStart; pageIdx < pageStart + allocHeader->mNumPages; pageIdx++)
			{
				assert(mUsedBits.IsSet(pageIdx));
				mUsedBits.Clear(pageIdx);
			}
			::VirtualFree(allocHeader, allocHeader->mNumPages * PAGE_SIZE, MEM_DECOMMIT);
		}
		return true;
	}	
};

static std::list<SA_AllocRange>* gAllocRanges = NULL;
static CritSect* gSA_CritSect = NULL;
static bool gStompInside = false;

void StompInit()
{
	gStompInside = true;
	gAllocRanges = new std::list<SA_AllocRange>();
	gSA_CritSect = new CritSect();
	gStompInside = false;
}

static void* StompAlloc(int size)
{
	//return malloc(size);

	if (gStompInside)
		return malloc(size);
	if (gSA_CritSect == NULL)
		StompInit();

	AutoCrit autoCrit(*gSA_CritSect);
	if (gStompInside)
		return malloc(size);
	gStompInside = true;

	while (true)
	{
		for (auto itr = gAllocRanges->rbegin(); itr != gAllocRanges->rend(); itr++)
		{
			auto& alloc = *itr;
			void* result = alloc.Alloc(size);
			if (result != NULL)
			{
				gStompInside = false;
				return result;
			}
		}

		gAllocRanges->resize(gAllocRanges->size() + 1);
	}		
	gStompInside = false;
}

static void StompFree(void* addr)
{
	if (gSA_CritSect == NULL)
		StompInit();

	AutoCrit autoCrit(*gSA_CritSect);
	
	bool leaveAllocated = true;
	for (auto& alloc : *gAllocRanges)
	{
		if (alloc.Free(addr, leaveAllocated))
			return;
	}

	free(addr);
	//assert("Invalid address" == 0);
}

//////////////////////////////////////////////////////////////////////////

struct AllocRecord
{
	uint8_t* mPtr;
	int mSize;

	const char* mAllocFileName;
	int mAllocLineNum;
	int mAllocTransactionIdx;

	const char* mFreeFileName;
	int mFreeLineNum;
	int mFreeTransactionIdx;

	int mHashAtFree;
};

int gDbgHeapTransactionIdx = 1;
int64 gDbgHeapAllocBytes = 0;
int64 gDbgHeapFreedBytes = 0;
std::vector<AllocRecord> gDbgHeapRecords;

#define ALLOC_PADDING 8
#define ALLOC_MAGIC 0xBF

static int DbgHeapHashMemory(void* ptr, int size)
{
	int curHash = 0;
	uint8_t* curHashPtr = (uint8_t*) ptr;
	for (int i = 0; i < size; i++)
	{
		curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
		curHashPtr++;
	}
	return curHash;
}

static void* DbgHeapAlloc(std::size_t size, const char* fileName, int lineNum)
{
	gDbgHeapAllocBytes += size;

	if (gDbgHeapRecords.size() == 0)
		gDbgHeapRecords.reserve(8192);

	AllocRecord allocRecord;
	allocRecord.mAllocFileName = fileName;
	allocRecord.mAllocLineNum = lineNum;
	allocRecord.mAllocTransactionIdx = gDbgHeapTransactionIdx++;
	allocRecord.mFreeFileName = NULL;
	allocRecord.mFreeLineNum = 0;
	allocRecord.mHashAtFree = 0;
	allocRecord.mFreeTransactionIdx = 0;
	allocRecord.mSize = (int)size;
	allocRecord.mPtr = new uint8_t[ALLOC_PADDING + size + ALLOC_PADDING];
	memset(allocRecord.mPtr, ALLOC_MAGIC, ALLOC_PADDING + size + ALLOC_PADDING);

	gDbgHeapRecords.push_back(allocRecord);

	return allocRecord.mPtr + ALLOC_PADDING;
}

void DbgHeapFree(const void* ptr, const char* fileName, int lineNum)
{
	if (ptr == NULL)
		return;

	bool found = false;

	for (int i = 0; i < (int) gDbgHeapRecords.size(); i++)
	{
		auto& allocRecord = gDbgHeapRecords[i];
		if (allocRecord.mPtr + ALLOC_PADDING == ptr)
		{
			assert(allocRecord.mFreeTransactionIdx == 0);

			gDbgHeapFreedBytes += allocRecord.mSize;
			allocRecord.mFreeFileName = fileName;
			allocRecord.mFreeLineNum = lineNum;
			allocRecord.mFreeTransactionIdx = gDbgHeapTransactionIdx++;;
			allocRecord.mHashAtFree = DbgHeapHashMemory(allocRecord.mPtr + ALLOC_PADDING, allocRecord.mSize);
			return;
		}
	}

	assert("Not found" == 0);
}

void DbgHeapCheck()
{
	for (int i = 0; i < (int) gDbgHeapRecords.size(); i++)
	{
		auto& allocRecord = gDbgHeapRecords[i];

		for (int i = 0; i < ALLOC_PADDING; i++)
		{
			assert(allocRecord.mPtr[i] == ALLOC_MAGIC);
			assert(allocRecord.mPtr[ALLOC_PADDING + allocRecord.mSize + ALLOC_PADDING - i - 1] == ALLOC_MAGIC);
		}

		if (allocRecord.mFreeTransactionIdx != 0)
		{
			int curHash = DbgHeapHashMemory(allocRecord.mPtr + ALLOC_PADDING, allocRecord.mSize);
			assert(allocRecord.mHashAtFree == curHash);
		}
	}

#ifndef BF_MINGW
	_CrtCheckMemory();
#endif
}

void DbgHeapCheckLeaks()
{
	OutputDebugStringA("DbgHeapCheckLeaks____________________\n");

	for (int i = 0; i < (int) gDbgHeapRecords.size(); i++)
	{
		auto& allocRecord = gDbgHeapRecords[i];
		if (allocRecord.mFreeTransactionIdx == 0)
		{
			char str[1024];
			sprintf(str, "Alloc #%d in %s on line %d\n", allocRecord.mAllocTransactionIdx, allocRecord.mAllocFileName, allocRecord.mAllocLineNum);
			OutputDebugStringA(str);

			sprintf(str, " %08X", allocRecord.mAllocTransactionIdx /*, allocRecord.mAllocFileName, allocRecord.mAllocLineNum*/);

			for (int i = 0; i < std::min(allocRecord.mSize, 16); i++)
				sprintf(str + strlen(str), " %02X", allocRecord.mPtr[i + ALLOC_PADDING]);

			strcat(str, "\n");
			OutputDebugStringA(str);

			delete allocRecord.mPtr;
		}
	}
	gDbgHeapRecords.clear();	

#ifndef BF_MINGW
	_CrtDumpMemoryLeaks();
#endif
}

#pragma push_macro("new")
#undef new

void* __cdecl operator new(std::size_t size)
{
#ifdef USE_STOMP
	return StompAlloc((int)size);
#else
	return DbgHeapAlloc(size, fileName, lineNum);
#endif
}

void* __cdecl operator new(std::size_t size, const char* fileName, int lineNum)
{
#ifdef USE_STOMP
	return StompAlloc((int)size);
#else
	return DbgHeapAlloc(size, fileName, lineNum);
#endif
}

void operator delete(void* ptr, const char* fileName, int lineNum)
{
#ifdef USE_STOMP
	StompFree(ptr);
#else
	DbgHeapFree(ptr, fileName, lineNum);
#endif
}

void operator delete[](void* ptr, const char* fileName, int lineNum)
{
#ifdef USE_STOMP
	StompFree(ptr);
#else
	DbgHeapFree(ptr, fileName, lineNum);
#endif
}

void operator delete(void* ptr)
{
#ifdef USE_STOMP
	StompFree(ptr);
#else
	DbgHeapFree(ptr, fileName, lineNum);
#endif
}

void operator delete[](void* ptr)
{
#ifdef USE_STOMP
	StompFree(ptr);
#else
	DbgHeapFree(ptr, fileName, lineNum);
#endif
}

#pragma pop_macro("new")

#endif //USE_BF_ALLOCDEBUG

static CritSect gCritSect;
static Dictionary<String, int> gAllocDicts;

void BpAllocName(const char* str, int size)
{
	AutoCrit autoCrit(gCritSect);

	int* sizePtr;
	gAllocDicts.TryAdd(String(str), NULL, &sizePtr);	
	*sizePtr += size;
}

void BpDump()
{
	AutoCrit autoCrit(gCritSect);

	int totalSize = 0;

	OutputDebugStr("BeDump:\n");

	String str;
	for (auto& kv : gAllocDicts)
	{
		str.Clear();
		str.Append(' ');
		str.Append(kv.mKey);
		str.Append(' ');
		while (str.mLength < 32)
			str.Append(' ');

		str += StrFormat("%8dk\n", (kv.mValue + 1023) / 1024);
		totalSize += kv.mValue;

		OutputDebugStr(str);
	}

	str.Clear();
	str.Clear();
	str.Append(" TOTAL ");	
	while (str.mLength < 32)
		str.Append(' ');

	str += StrFormat("%8dk\n", (totalSize + 1023) / 1024);
	OutputDebugStr(str);
}