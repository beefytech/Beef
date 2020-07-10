#include "FileStream.h"

#pragma warning(disable:4996)

USING_NS_BF;

FileStream::FileStream()
{
	mFP = NULL;	
	mCacheBuffer = NULL;
	mCacheReadPos = -0x3FFFFFFF;
	mCacheSize = 0;
	mVFilePos = 0;
	mReadPastEnd = false;
}

FileStream::~FileStream()
{
	delete mCacheBuffer;
	if (mFP != NULL)
		fclose(mFP);
}

bool FileStream::Open(const StringImpl& filePath, const char* fopenOptions)
{
#ifdef BF_PLATFORM_WINDOWS
	mFP = _wfopen(UTF8Decode(filePath).c_str(), UTF8Decode(fopenOptions).c_str());
#else
	mFP = fopen(filePath.c_str(), fopenOptions);
#endif
	mReadPastEnd = false;
	return mFP != NULL;
}

bool FileStream::IsOpen()
{
	return mFP != NULL;
}

void FileStream::Close()
{
	if (mFP != NULL)
	{
		fclose(mFP);
		mFP = NULL;
	}
}

void FileStream::SetCacheSize(int size)
{
	size = (size + (4096-1)) & ~(4096-1);
	
	mCacheReadPos = -0x3FFFFFFF;
	delete mCacheBuffer;
	if (size > 0)
		mCacheBuffer = new uint8[size];
	else
		mCacheBuffer = NULL;
	mCacheSize = size;
}

void FileStream::Seek(int size)
{
	if (mCacheBuffer != NULL)
		mVFilePos += size;
	else
		fseek(mFP, size, SEEK_CUR);
}

void FileStream::SetPos(int pos)
{
	if (mCacheBuffer != NULL)
		mVFilePos = pos;
	else
		fseek(mFP, pos, SEEK_SET);
}

bool FileStream::Eof()
{
	if (mCacheBuffer != NULL)
	{
		fseek(mFP, 0, SEEK_END);
		return mVFilePos >= ftell(mFP);
	}
	
	int aPos = (int)ftell(mFP);
	fseek(mFP, 0, SEEK_END);
	int aSize = (int)ftell(mFP);
	fseek(mFP, aPos, SEEK_SET);
	return aPos == aSize;
}

int FileStream::GetSize()
{
	int aPos = (int)ftell(mFP);
	fseek(mFP, 0, SEEK_END);
	int aSize = (int)ftell(mFP);
	fseek(mFP, aPos, SEEK_SET);
	return aSize;
}

void FileStream::Read(void* ptr, int size)
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

				ptr = (uint8*) ptr + subSize;
				size -= subSize;
			}
			
			mCacheReadPos = mVFilePos & ~(4096-1);
			fseek(mFP, mCacheReadPos, SEEK_SET);
			int aSize = (int)fread(mCacheBuffer, 1, mCacheSize, mFP);
			if (aSize != mCacheSize)
			{
				// Zero out underflow bytes
				memset((uint8*) ptr + aSize, 0, mCacheSize - aSize);
			}
		}
	}
	else
	{
		int aSize = (int)fread(ptr, 1, size, mFP);
		if (aSize != size)
		{
			// Zero out underflow bytes
			memset((uint8*) ptr + aSize, 0, size - aSize);
			mReadPastEnd = true;
		}
	}
}

void FileStream::Write(void* ptr, int size)
{
	fwrite(ptr, 1, size, mFP);
}

int FileStream::GetPos()
{
	if (mCacheBuffer != NULL)
		return mVFilePos;
	return (int)ftell(mFP);
}

//////////////////////////////////////////////////////////////////////////

int	FileSubStream::GetSize()
{
	return mSize;
}

int	FileSubStream::GetPos()
{
	return FileStream::GetPos() - mOffset;
}

void FileSubStream::SetPos(int pos)
{
	FileStream::SetPos(pos + mOffset);
}

//////////////////////////////////////////////////////////////////////////


SysFileStream::SysFileStream()
{
	mFile = NULL;
}

SysFileStream::~SysFileStream()
{
	if (mFile != NULL)
		BfpFile_Release(mFile);		
}

bool SysFileStream::Open(const StringImpl& filePath, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags)
{
	mFile = BfpFile_Create(filePath.c_str(), createKind, createFlags, BfpFileAttribute_Normal, NULL);
	return mFile != NULL;

	//mHandle = ::CreateFileW(UTF8Decode(filePath).c_str(), access, 0, NULL, CREATE_ALWAYS, 0, 0);
	//return mHandle != INVALID_HANDLE_VALUE;
}

bool SysFileStream::IsOpen()
{
	return mFile != NULL;
}

void SysFileStream::Close()
{
	if (mFile != NULL)
	{
		BfpFile_Release(mFile);		
		mFile = NULL;
	}
}

void SysFileStream::SetSizeFast(int size)
{
	int curSize = GetSize();
	if (size == curSize)
		return;
	
	int curPos = GetPos();
	SetPos(size);
	BfpFile_Truncate(mFile);	
	SetPos(curPos);
	return;	
}

void SysFileStream::Seek(int pos)
{
	BfpFile_Seek(mFile, pos, BfpFileSeekKind_Relative);
}

void SysFileStream::SetPos(int pos)
{	
	BfpFile_Seek(mFile, pos, BfpFileSeekKind_Absolute);	
}

bool SysFileStream::Eof()
{	
	char c;
	int readSize = (int)BfpFile_Read(mFile, &c, 1, -1, NULL);
	if (readSize == 0)
		return true;
	BfpFile_Seek(mFile, -1, BfpFileSeekKind_Relative);
	return false;
}

int SysFileStream::GetSize()
{
	return (int)BfpFile_GetFileSize(mFile);	
}

void SysFileStream::Read(void* ptr, int size)
{	
	int readSize = (int)BfpFile_Read(mFile, ptr, size, -1, NULL);
	if (readSize != size)
	{
		// Zero out underflow bytes
		memset((uint8*)ptr + readSize, 0, size - readSize);
	}
}

void SysFileStream::Write(void* ptr, int size)
{
	BfpFile_Write(mFile, ptr, size, -1, NULL);
}

int SysFileStream::GetPos()
{
	return (int)BfpFile_Seek(mFile, 0, BfpFileSeekKind_Relative);	
}

