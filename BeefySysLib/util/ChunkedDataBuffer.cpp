#include "ChunkedDataBuffer.h"
#include "DataStream.h"

USING_NS_BF;

int Beefy::ChunkedDataBuffer::sBlocksAllocated = 0;

/*static void Log(const char* fmt ...)
{		
	static FILE* fp = fopen("chunked_dbg.txt", "wb");

	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);
	
	fwrite(aResult.c_str(), 1, aResult.length(), fp);
	fflush(fp);
}*/

ChunkedDataBuffer::ChunkedDataBuffer()
{
	mWriteCurAlloc = NULL;
	mWriteCurPtr = (uint8*)(intptr)ALLOC_SIZE;
	mReadCurAlloc = NULL;
	mReadCurPtr = (uint8*)(intptr)ALLOC_SIZE;
	mReadNextAlloc = mReadCurPtr;
	mReadPoolIdx = -1;
	mSize = 0;
}

ChunkedDataBuffer::~ChunkedDataBuffer()
{		
	Clear();		
}	

void ChunkedDataBuffer::InitFlatRef(void* ptr, int size)
{
	mReadPoolIdx = 0;
	mReadCurPtr = (uint8*)ptr;
	mReadCurAlloc = mReadCurPtr;
	mReadNextAlloc = mReadCurPtr + size;
}

void ChunkedDataBuffer::Clear()
{
	mWriteCurAlloc = NULL;
	mWriteCurPtr = (uint8*)(intptr)ALLOC_SIZE;
	mReadCurAlloc = NULL;
	mReadCurPtr = (uint8*)(intptr)ALLOC_SIZE;
	mReadNextAlloc = mReadCurPtr;
	mReadPoolIdx = -1;
	mSize = 0;
	for (auto ptr : mPools)
	{
		//Log("Free %p\n", ptr);
		free(ptr);
		sBlocksAllocated--;
	}
	mPools.Clear();		
}

int ChunkedDataBuffer::GetSize()
{
	//int calcSize = (int)(((mPools.mSize - 1) * ALLOC_SIZE) + (mWriteCurPtr - mWriteCurAlloc));
	//BF_ASSERT(calcSize == mSize);
	return mSize;
}

void ChunkedDataBuffer::GrowPool()
{
	int curSize = (int)(mWriteCurPtr - mWriteCurAlloc);
	mWriteCurAlloc = (uint8*)malloc(ALLOC_SIZE);
	//Log("Alloc %p\n", mWriteCurAlloc);
	sBlocksAllocated++;
	memset(mWriteCurAlloc, 0, ALLOC_SIZE);
	mPools.push_back(mWriteCurAlloc);			
	mWriteCurPtr = mWriteCurAlloc;		
}

void ChunkedDataBuffer::Write(const void* data, int size)
{		
	while (mWriteCurPtr + size >= mWriteCurAlloc + ALLOC_SIZE)
	{
		int curSize = (int)((mWriteCurAlloc + ALLOC_SIZE) - mWriteCurPtr);
		if (curSize > 0)
			memcpy(mWriteCurPtr, data, curSize);

		GrowPool();
		data = (uint8*)data + curSize;
		size -= curSize;
		mSize += curSize;
		if (size == 0)
			break;
	}

	memcpy(mWriteCurPtr, data, size);
	mWriteCurPtr += size;
	mSize += size;
}

void ChunkedDataBuffer::Write(uint8 byte)
{
	while (mWriteCurPtr == mWriteCurAlloc + ALLOC_SIZE)		
		GrowPool();			
	*(mWriteCurPtr++) = byte;
	mSize++;
}

void ChunkedDataBuffer::Write_2(uint16 val)
{
	while (mWriteCurPtr + 2 > mWriteCurAlloc + ALLOC_SIZE)
	{
		Write((uint8*)&val, 2);
		return;
	}
	*(uint16*)mWriteCurPtr = val;
	mWriteCurPtr += 2;
	mSize += 2;
}

void ChunkedDataBuffer::Write_3(uint32 val)
{	
	while (mWriteCurPtr + 3 > mWriteCurAlloc + ALLOC_SIZE)
	{
		Write((uint8*)&val, 3);
		return;
	}
	*(uint32*)mWriteCurPtr = val;
	mWriteCurPtr += 3;
	mSize += 3;
}

void ChunkedDataBuffer::Write_4(uint32 val)
{	
	while (mWriteCurPtr + 4 > mWriteCurAlloc + ALLOC_SIZE)
	{
		Write((uint8*)&val, 4);
		return;
	}
	*(uint32*)mWriteCurPtr = val;
	mWriteCurPtr += 4;
	mSize += 4;
}

int ChunkedDataBuffer::GetReadPos()
{
	return mReadPoolIdx * ALLOC_SIZE + (int)(mReadCurPtr - mReadCurAlloc);
}

void ChunkedDataBuffer::SetReadPos(int pos)
{
	mReadPoolIdx = pos / ALLOC_SIZE;
	mReadCurAlloc = mPools[mReadPoolIdx];
	mReadCurPtr = mReadCurAlloc + pos % ALLOC_SIZE;
	mReadNextAlloc = mReadCurPtr + ALLOC_SIZE;
}

void ChunkedDataBuffer::NextReadPool()
{		
	mReadCurAlloc = mPools[++mReadPoolIdx];
	mReadCurPtr = mReadCurAlloc;
	mReadNextAlloc = mReadCurPtr + ALLOC_SIZE;		
}

void ChunkedDataBuffer::Read(void* data, int size)
{
	while (mReadCurPtr + size > mReadNextAlloc)
	{
		int curSize = (int)(mReadNextAlloc - mReadCurPtr);
		if (curSize > 0)
			memcpy(data, mReadCurPtr, curSize);

		NextReadPool();
		data = (uint8*)data + curSize;
		size -= curSize;
	}

	memcpy(data, mReadCurPtr, size);
	mReadCurPtr += size;
}

void* ChunkedDataBuffer::FastRead(void * data, int size)
{
	// Fast case- no crossing chunk boundary
	if (mReadCurPtr + size <= mReadNextAlloc)
	{
		void* retVal = mReadCurPtr;
		mReadCurPtr += size;
		return retVal;
	}

	void* dataHead = data;
	while (mReadCurPtr + size > mReadNextAlloc)
	{
		int curSize = (int)(mReadNextAlloc - mReadCurPtr);
		if (curSize > 0)
			memcpy(data, mReadCurPtr, curSize);

		NextReadPool();
		data = (uint8*)data + curSize;
		size -= curSize;
	}	

	memcpy(data, mReadCurPtr, size);
	mReadCurPtr += size;
	return dataHead;
}

uint8 ChunkedDataBuffer::Read()
{
	if (mReadCurPtr == mReadNextAlloc)
		NextReadPool();
	return *(mReadCurPtr++);
}

void ChunkedDataBuffer::Read(DataStream& stream, int size)
{
	while (mReadCurPtr + size > mReadNextAlloc)
	{
		int curSize = (int)(mReadNextAlloc - mReadCurPtr);
		if (curSize > 0)
			stream.Write(mReadCurPtr, curSize);

		NextReadPool();		
		size -= curSize;
	}

	stream.Write(mReadCurPtr, size);
	mReadCurPtr += size;
}
