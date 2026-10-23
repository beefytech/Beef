#include "MappedFile.h"

#ifndef BF_PLATFORM_WINDOWS
#include <climits>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

#else

MappedFile::MappedFile()
{
	mData = NULL;
	mFileSize = 0;
}

MappedFile::~MappedFile()
{
	if (mData != NULL)
		munmap(mData, mFileSize);
}

bool MappedFile::Open(const StringImpl& fileName)
{
	mFileName = fileName;
	int fd = open(fileName.c_str(), O_RDONLY);
	if (fd == -1)
		return false;

	struct stat fileStat;
	if ((fstat(fd, &fileStat) != 0) || (fileStat.st_size <= 0) || (fileStat.st_size > INT_MAX))
	{
		close(fd);
		return false;
	}

	mFileSize = (int)fileStat.st_size;
	void* data = mmap(NULL, mFileSize, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (data == MAP_FAILED)
		return false;
	mData = data;
	return true;
}

#endif