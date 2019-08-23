#include "Debugger.h"
#include "DebugManager.h"
#include "Compiler/BfSystem.h"

USING_NS_BF;

DbgModuleMemoryCache::DbgModuleMemoryCache(uintptr addr, int size)
{
	mAddr = addr;
	mSize = size;

	mBlockSize = 4096;
	mBlockCount = size / mBlockSize;
	BF_ASSERT((size & (mBlockSize - 1)) == 0);
	//mBlocks = (uint8**)dwarf->mAlloc.AllocBytes(dwarf->mOrigImageBlockCount * sizeof(uint8*), alignof(uint8*));
	mBlocks = new uint8*[mBlockCount];
	memset(mBlocks, 0, mBlockCount * sizeof(uint8*));
	mFlags = new DbgMemoryFlags[mBlockCount];
	memset(mBlocks, 0, mBlockCount * sizeof(DbgMemoryFlags));
	mOwns = true;
}

DbgModuleMemoryCache::DbgModuleMemoryCache(uintptr addr, uint8* data, int size, bool makeCopy)
{
	mAddr = addr;
	mBlockSize = size;
	mBlocks = new uint8*[1];
	mFlags = new DbgMemoryFlags[1];
	mSize = size;

	if (makeCopy)
	{
		uint8* dataCopy = new uint8[size];
		if (data != NULL)
			memcpy(dataCopy, data, size);
		mBlocks[0] = dataCopy;
	}
	else
	{
		mBlocks[0] = data;
	}	

	mOwns = makeCopy;
	mBlockCount = 1;
}

DbgModuleMemoryCache::~DbgModuleMemoryCache()
{
	if (mOwns)
	{
		for (int i = 0; i < mBlockCount; i++)
			delete mBlocks[i];
	}
	delete [] mBlocks;
	delete [] mFlags;
}

DbgMemoryFlags DbgModuleMemoryCache::Read(uintptr addr, uint8* data, int size)
{
	int sizeLeft = size;
	DbgMemoryFlags flags = DbgMemoryFlags_None;

	if ((addr < mAddr) || (addr > mAddr + mSize))
	{
		gDebugger->ReadMemory(addr, size, data);
		return gDebugger->GetMemoryFlags(addr);
	}

	int relAddr = (int)(addr - mAddr);
	int blockIdx = relAddr / mBlockSize;
	int curOffset = relAddr % mBlockSize;
	
	while (sizeLeft > 0)
	{
		uint8* block = mBlocks[blockIdx];
		if (block == NULL)
		{
			block = new uint8[mBlockSize];
			gDebugger->ReadMemory(mAddr + blockIdx * mBlockSize, mBlockSize, block);
			mBlocks[blockIdx] = block;

			mFlags[blockIdx] = gDebugger->GetMemoryFlags(mAddr + blockIdx * mBlockSize);
			//BfLogDbg("Memory flags for %p = %d\n", mAddr + blockIdx * mBlockSize, mFlags[blockIdx]);
		}

		flags = mFlags[blockIdx];
		int readSize = BF_MIN(sizeLeft, mBlockSize - curOffset);
		if (data != NULL)
		{
			memcpy(data, block + curOffset, readSize);
			data += readSize;
		}
		sizeLeft -= readSize;
		curOffset = 0;
		blockIdx++;		
	}

	return flags;
}

void DbgModuleMemoryCache::ReportMemory(MemReporter * memReporter)
{
	int totalMemory = 0;
	if (mOwns)
	{
		for (int i = 0; i < mBlockCount; i++)
			if (mBlocks[i] != NULL)
				totalMemory += mBlockSize;
	}
	memReporter->Add(totalMemory);
}
