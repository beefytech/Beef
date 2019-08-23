#include "CachedDataStream.h"

USING_NS_BF;

CachedDataStream::CachedDataStream(DataStream* stream)
{
	mStream = stream;
	mDataPtr = mChunk;
	mDataEnd = mChunk + CHUNK_SIZE;
}

CachedDataStream::~CachedDataStream()
{
	Flush();
}

void CachedDataStream::Flush()
{
	int cachedBytes = (int)(mDataPtr - mChunk);
	if (cachedBytes > 0)
	{
		mStream->Write(mChunk, cachedBytes);
		mDataPtr = mChunk;
	}
}

bool CachedDataStream::Eof()
{
	Flush();
	return mStream->Eof();
}

int CachedDataStream::GetSize()
{
	Flush();
	return mStream->GetSize();
}

void CachedDataStream::Read(void* ptr, int size)
{
	Flush();
	mStream->Read(ptr, size);
}

void CachedDataStream::Write(void* ptr, int size)
{
	while (size > 0)
	{
		int cacheLeft = (int)(mDataEnd - mDataPtr);
		if (cacheLeft == 0)
		{
			Flush();
			continue;
		}

		int writeBytes = std::min(cacheLeft, size);
		memcpy(mDataPtr, ptr, writeBytes);
		ptr = (uint8*)ptr + writeBytes;
		size -= writeBytes;
		mDataPtr += writeBytes;
	}
}

int CachedDataStream::GetPos()
{	
	return mStream->GetPos() + (int)(mDataPtr - mChunk);
}

void CachedDataStream::Seek(int size)
{
	Flush();
	mStream->Seek(size);
}

void CachedDataStream::SetPos(int pos)
{
	Flush();
	mStream->SetPos(pos);
}
