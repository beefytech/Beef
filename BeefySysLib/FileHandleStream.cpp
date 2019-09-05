#include "FileHandleStream.h"

USING_NS_BF;

FileHandleStream::FileHandleStream()
{
	mFileHandle = NULL;
	mCacheBuffer = NULL;
	mCacheReadPos = -0x3FFFFFFF;
	mCacheSize = 0;
	mVFilePos = 0;
}

FileHandleStream::~FileHandleStream()
{
	if (mCacheBuffer != NULL)
		delete mCacheBuffer;
	if (mFileHandle != NULL)	
		::CloseHandle_File(mFileHandle);	
}

void FileHandleStream::SetCacheSize(int size)
{
	size = (size + (4096 - 1)) & ~(4096 - 1);

	mCacheReadPos = -0x3FFFFFFF;
	delete mCacheBuffer;
	if (size > 0)
		mCacheBuffer = new uint8[size];
	else
		mCacheBuffer = NULL;
	mCacheSize = size;
}

void FileHandleStream::Seek(int pos)
{
	if (mCacheBuffer != NULL)
		mVFilePos += pos;
	else
		::SetFilePointer(mFileHandle, pos, 0, FILE_CURRENT);
}

void FileHandleStream::SetPos(int pos)
{
	if (mCacheBuffer != NULL)
		mVFilePos = pos;
	else
		::SetFilePointer(mFileHandle, pos, 0, FILE_BEGIN);
}

bool FileHandleStream::Eof()
{
	if (mCacheBuffer != NULL)
	{
		::SetFilePointer(mFileHandle, 0, 0, FILE_END);
		return mVFilePos >= (int)::SetFilePointer(mFileHandle, 0, 0, FILE_CURRENT);
	}
	return ::SetFilePointer(mFileHandle, 0, 0, FILE_CURRENT) >= ::GetFileSize(mFileHandle, NULL);
}

int FileHandleStream::GetSize()
{
	return ::GetFileSize(mFileHandle, NULL);
}

void FileHandleStream::Read(void* ptr, int size)
{
	if (mCacheBuffer != NULL)
	{
		while (true)
		{
			int buffOffset = mVFilePos - mCacheReadPos;
			if ((buffOffset >= 0) && (buffOffset + size < mCacheSize))
			{
				// If inside
				memcpy(ptr, mCacheBuffer + buffOffset, size);
				mVFilePos += size;
				return;
			}
			else if ((buffOffset >= 0) && (buffOffset < mCacheSize))
			{
				int subSize = mCacheReadPos + mCacheSize - mVFilePos;
				memcpy(ptr, mCacheBuffer + buffOffset, subSize);
				mVFilePos += subSize;

				ptr = (uint8*)ptr + subSize;
				size -= subSize;
			}

			mCacheReadPos = mVFilePos & ~(4096 - 1);
			::SetFilePointer(mFileHandle, mCacheReadPos, 0, FILE_BEGIN);
			int aSize = 0;			
			::ReadFile(mFileHandle, mCacheBuffer, mCacheSize, (DWORD*)&aSize, NULL);
			if (aSize != mCacheSize)
			{
				// Zero out underflow bytes
				memset((uint8*)ptr + aSize, 0, mCacheSize - aSize);
			}
		}
	}
	else
	{		
		int aSize = 0;
		::ReadFile(mFileHandle, ptr, size, (DWORD*)&aSize, NULL);
		if (aSize != size)
		{
			// Zero out underflow bytes
			memset((uint8*)ptr + aSize, 0, size - aSize);
		}
	}
}

void FileHandleStream::Write(void* ptr, int size)
{	
	::WriteFile(mFileHandle, ptr, size, NULL, NULL);
}

int FileHandleStream::GetPos()
{
	if (mCacheBuffer != NULL)
		return mVFilePos;
	return ::SetFilePointer(mFileHandle, 0, 0, FILE_CURRENT);
}

