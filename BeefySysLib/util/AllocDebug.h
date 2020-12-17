#pragma once

//#define BP_ALLOC_TRACK

#include <cstdint>

#ifdef BF_PLATFORM_WINDOWS

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

//#define USE_BF_ALLOCDEBUG

#ifdef USE_BF_ALLOCDEBUG

#define DBG_NEW new ( __FILE__ , __LINE__ )
#define new DBG_NEW
#undef delete

#pragma push_macro("new")
#undef new
#undef delete
void* operator new(std::size_t size);
void* operator new(std::size_t size, const char* fileName, int lineNum);
void operator delete(void* ptr, const char* fileName, int lineNum);
void operator delete[](void* ptr, const char* fileName, int lineNum);
void operator delete(void* ptr);
void operator delete[](void* ptr);

#pragma pop_macro("new")

/*#undef delete
#define delete DbgHeapDeleter(__FILE__, __LINE__) << 

void DbgHeapFree(const void* ptr, const char* fileName, int lineNum);

struct DbgHeapDeleter
{
	const char* mFileName;
	int mLineNum;

	DbgHeapDeleter(const char* fileName, int lineNum)
	{
		mFileName = fileName;
		mLineNum = lineNum;
	}

	void operator<<(const void* ptr)
	{
		DbgHeapFree(ptr, mFileName, mLineNum);
	}
};*/

extern int gDbgHeapTransactionIdx;
void DbgHeapCheck();
void DbgHeapCheckLeaks();

#elif (defined _WIN32) && (!defined BF_MINGW)//USE_BF_ALLOCDEBUG

#ifndef DBG_NEW
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif

#define DbgHeapCheck _CrtCheckMemory
#define DbgHeapCheckLeaks _CrtDumpMemoryLeaks

#endif // USE_BF_ALLOCDEBUG

#endif // BF_PLATFORM_WINDOWS

void BpAllocName(const char* str, int size);
void BpDump();

#ifdef BP_ALLOC_TRACK

#define BP_ALLOC(str, size) BpAllocName(str, size);
#define BP_ALLOC_RAW_T(T) BpAllocName(#T, sizeof(T))
//#define BP_ALLOC_T(T) BpAllocName(#T, sizeof(T))
#define BP_ALLOC_T(T)

#else

#define BP_ALLOC(str, size)
#define BP_ALLOC_T(T)
#define BP_ALLOC_RAW_T(T)

#endif

void* StompAlloc(int size);
void StompFree(void* addr);

template <typename T>
class AllocatorStomp
{
public:
	T* allocate(intptr_t count)
	{
		return (T*)StompAlloc((int)(sizeof(T) * count));
	}

	void deallocate(T* ptr)
	{
		StompFree(ptr);
	}

	void* rawAllocate(intptr_t size)
	{
		return StompAlloc((int)size);
	}

	void rawDeallocate(void* ptr)
	{
		StompFree(ptr);
	}
};
