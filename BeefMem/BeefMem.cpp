#include "gperftools/src/tcmalloc.cc"

#include "BeefySysLib/Common.h"

#ifdef BFMEM_DYNAMIC
#define BFMEM_EXPORT __declspec(dllexport)
#else
#define BFMEM_EXPORT
#endif

USING_NS_BF;

void PatchWindowsFunctions()
{
	// For TCMalloc, don't really patch
}

namespace tcmalloc
{
	extern "C" int RunningOnValgrind(void)
	{
		return 0;
	}
}

struct TCMallocRecord
{
	void* mPtr;
	int mSize;
};

static Array<TCMallocRecord> gTCMallocRecords;

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
	gTCMallocRecords.Clear();
}

extern "C" BFMEM_EXPORT void* BfmAlloc(int size, int align)
{
	return tc_malloc(size);
}

extern "C" BFMEM_EXPORT void BfmFree(void* ptr)
{
	return tc_free(ptr);
}
