#pragma once

#include "../Common.h"

NS_BF_BEGIN;

class DataStream;

class ChunkedDataBuffer
{
public:	
	static const int ALLOC_SIZE = 0x1000;

public:
	Array<uint8*> mPools;	
	uint8* mWriteCurAlloc;
	uint8* mWriteCurPtr;
	uint8* mReadCurAlloc;
	uint8* mReadCurPtr;
	uint8* mReadNextAlloc;
	int mReadPoolIdx;
	int mSize;
	static int sBlocksAllocated;

public:
	ChunkedDataBuffer();
	~ChunkedDataBuffer();
	
	void InitFlatRef(void* ptr, int size);
	void Clear();
	int GetSize();
	void GrowPool();
	void Write(const void* data, int size);
	void Write(uint8 byte);
	void Write_2(uint16 val);
	void Write_3(uint32 val);
	void Write_4(uint32 val);
	int GetReadPos();
	void SetReadPos(int pos);
	void NextReadPool();
	void Read(void* data, int size);
	void* FastRead(void* data, int size); // Can either return pointer into the stream or read data, depending on if the read crosses a chunk
	uint8 Read();

	void Read(DataStream& stream, int size);
};

NS_BF_END;
