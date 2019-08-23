#include "MemStream.h"

USING_NS_BF;

MemStream* MemStream::CreateWithDuplicate(void* data, int size)
{
	uint8* newData = new uint8[size];
	memcpy(newData, data, size);
	MemStream* memStream = new MemStream(newData, size, true);
	return memStream;
}

MemStream* MemStream::CreateWithOwnershipTaken(void* data, int size)
{
	return new MemStream(data, size, true);
}

MemStream* MemStream::CreateWithNoCopy(void* data, int size)
{
	return new MemStream(data, size, false);
}

MemStream::MemStream(void* data, int size, bool freeMemory)
{
	mData = (uint8*) data;
	mSize = size;
	mPos = 0;
	mFreeMemory = freeMemory;
}

MemStream::~MemStream()
{
	if (mFreeMemory)
		delete mData;
}

bool MemStream::Eof()
{
	return mPos >= mSize;
}

int MemStream::GetSize()
{
	return mSize;
}

void MemStream::Read(void* ptr, int size)
{
	memcpy(ptr, mData + mPos, size);
	mPos += size;
}


void Beefy::MemStream::Write(void* ptr, int size)
{
	memcpy(mData + mPos, ptr, size);
	mPos += size;
}

int MemStream::GetPos() 
{
	return mPos;
}

void MemStream::Seek(int size)
{
	mPos += size;
}

void MemStream::SetPos(int pos)
{
	mPos = pos;
}

//////////////////////////////////////////////////////////////////////////

SafeMemStream::SafeMemStream(void* data, int size, bool freeMemory) : MemStream(data, size, freeMemory)
{
	mFailed = false;
}

void SafeMemStream::Read(void* ptr, int size)
{
	if (mPos + size > mSize)
	{
		mFailed = true;
		memset(ptr, 0, size);
	}
	else
	{
		memcpy(ptr, mData + mPos, size);		
	}
	mPos += size;
}

//////////////////////////////////////////////////////////////////////////

DynMemStream::DynMemStream()
{
	mPos = 0;
}

bool DynMemStream::Eof()
{
	return mPos >= (int)mData.size();
}

int DynMemStream::GetSize()
{
	return (int)mData.size();
}

void DynMemStream::Read(void* ptr, int size)
{
	memcpy(ptr, (uint8*)&mData.front() + mPos, size);
	mPos += size;
}

void DynMemStream::Write(void* ptr, int size)
{	
	mData.Insert(mPos, (uint8*)ptr, size);
	mPos += size;
}

void DynMemStream::Write(uint8 val)
{
	mData.push_back(val);
	mPos++;
}

int DynMemStream::GetPos()
{
	return mPos;
}

void DynMemStream::Seek(int size)
{
	mPos += size;
}

void DynMemStream::SetPos(int pos)
{
	mPos = pos;
}

void DynMemStream::Clear()
{
	mPos = 0;
	mData.Clear();
}

void* DynMemStream::GetPtr()
{
	return &mData[0];
}