#include "MemLogger.h"

USING_NS_BF;

struct MemLogger_Header
{
public:
	int mHead;
	int mTail;
	int mSize;
};

MemLogger::MemLogger()
{	
	mFileMap = NULL;
	mMemBuffer = NULL;
	mBufferSize = 0;
	mTotalWriteSize = 0;	
	mNoOverflow = false;
}

MemLogger::~MemLogger()
{
#ifdef BF_PLATFORM_WINDOWS
	if (mMemBuffer != NULL)
		::UnmapViewOfFile(mMemBuffer);
	if (mFileMap != NULL)
		::CloseHandle(mFileMap);
#endif
}

void MemLogger::Write(const void* ptr, int size)
{
	if (mMemBuffer == NULL)
		return;
		
	int dataSize = mBufferSize - sizeof(MemLogger_Header);
	void* dataPtr = (uint8*)mMemBuffer + sizeof(MemLogger_Header);

	if (mNoOverflow)
		size = BF_MIN(size, dataSize - mTotalWriteSize - 1);

	if (size <= 0)
		return;

	MemLogger_Header* header = (MemLogger_Header*)mMemBuffer;

	bool wasWrapped = header->mHead < header->mTail;

	int writeSize = BF_MIN(size, dataSize - header->mHead);
	memcpy((char*)dataPtr + header->mHead, ptr, writeSize);
	size -= writeSize;

	header->mHead += writeSize;
	while (header->mHead >= dataSize)	
		header->mHead -= dataSize;	
	
	if (size > 0)
	{
		int writeSize2 = BF_MIN(size, dataSize - header->mHead);
		memcpy((char*)dataPtr + header->mHead, (char*)ptr + writeSize, writeSize2);
		header->mHead += writeSize2;
		while (header->mHead >= dataSize)
			header->mHead -= dataSize;
	}

	mTotalWriteSize += writeSize;

	if (mTotalWriteSize >= dataSize)
	{
		header->mTail = header->mHead + 1;
		if (header->mTail > dataSize)
			header->mTail -= dataSize;
	}
}

bool Beefy::MemLogger::Create(const StringImpl& memName, int size)
{
#ifdef BF_PLATFORM_WINDOWS
	String sharedName = "MemLogger_" + memName;
	HANDLE hMapFile = CreateFileMappingA(
		INVALID_HANDLE_VALUE,    // use paging file
		NULL,                    // default security
		PAGE_READWRITE,          // read/write access
		0,                       // maximum object size (high-order DWORD)
		size,                // maximum object size (low-order DWORD)
		sharedName.c_str());                 // name of mapping object

	if (hMapFile == NULL)
		return false;
	
	mMemBuffer = MapViewOfFile(hMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		size);

	if (mMemBuffer == NULL)
		return false;

	mBufferSize = size;

	MemLogger_Header* header = (MemLogger_Header*)mMemBuffer;
	header->mHead = 0;
	header->mTail = 0;
	header->mSize = size;
	return true;
#else
	return false;
#endif
}

bool Beefy::MemLogger::Get(const StringImpl& memName, String& outStr)
{
#ifdef BF_PLATFORM_WINDOWS
	String sharedName = "MemLogger_" + memName;
	HANDLE hMapFile = ::OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, sharedName.c_str());
	if (hMapFile == NULL)
		return false;

	void* memPtr = MapViewOfFile(hMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		sizeof(MemLogger_Header));

	MemLogger_Header* header = (MemLogger_Header*)(memPtr);
	int size = header->mSize;
	UnmapViewOfFile(memPtr);

	memPtr = MapViewOfFile(hMapFile,   // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		size);

	if (memPtr == NULL)
		return false;	

	::CloseHandle(hMapFile);

	header = (MemLogger_Header*)(memPtr);
	
	int dataSize = header->mSize - sizeof(MemLogger_Header);
	void* dataPtr = (uint8*)memPtr + sizeof(MemLogger_Header);

	if (header->mHead >= header->mTail)
	{
		// Not wrapped around
		outStr.Insert(outStr.mLength, (char*)dataPtr + header->mTail, header->mHead - header->mTail);
	}
	else
	{
		outStr.Insert(outStr.mLength, (char*)dataPtr + header->mTail, dataSize - header->mTail);
		outStr.Insert(outStr.mLength, (char*)dataPtr, header->mHead);
	}

	return true;
#else
	return false;
#endif
}

void Beefy::MemLogger::Log(const char* fmt ...)
{
	if (mMemBuffer == NULL)
		return;

	StringT<4096> str;
	va_list argList;
	va_start(argList, fmt);
	vformat(str, fmt, argList);
	va_end(argList);

	Write(str.c_str(), str.mLength);
}
