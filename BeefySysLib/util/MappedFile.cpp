#include "MappedFile.h"

USING_NS_BF;

#ifdef BF_PLATFORM_WINDOWS

MappedFile::MappedFile()
{
	mMappedFile = INVALID_HANDLE_VALUE;
	mMappedFileMapping = INVALID_HANDLE_VALUE;
	mData = NULL;
	mFileSize = 0;
}

MappedFile::~MappedFile()
{
	if (mData != NULL)
		::UnmapViewOfFile(mData);
	if (mMappedFileMapping != INVALID_HANDLE_VALUE)
		::CloseHandle(mMappedFileMapping);
	if (mMappedFile != INVALID_HANDLE_VALUE)
		::CloseHandle(mMappedFile);
}

bool MappedFile::Open(const StringImpl& fileName)
{
	mFileName = fileName;
	mMappedFile = CreateFileA(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (mMappedFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD highFileSize = 0;
	mFileSize = (int)GetFileSize(mMappedFile, &highFileSize);
	mMappedFileMapping = CreateFileMapping(mMappedFile, NULL, PAGE_READONLY, 0, mFileSize, NULL);
	mData = MapViewOfFile(mMappedFileMapping, FILE_MAP_READ, 0, 0, mFileSize);
	if (mData == NULL)
	{
		return false;
	}

	return true;
}

#endif