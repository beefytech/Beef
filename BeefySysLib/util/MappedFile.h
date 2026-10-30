#pragma once

#include "../Common.h"

NS_BF_BEGIN

#ifdef BF_PLATFORM_WINDOWS

class MappedFile
{
	BF_DISALLOW_COPY(MappedFile);
public:
	String mFileName;
	HANDLE mMappedFile;
	void* mData;
	HANDLE mMappedFileMapping;
	int mFileSize;

public:
	bool Open(const StringImpl& fileName);

	MappedFile();
	~MappedFile();
};

#else

class MappedFile
{
	BF_DISALLOW_COPY(MappedFile);
public:
	String mFileName;
	void* mData;
	int mFileSize;

public:
	bool Open(const StringImpl& fileName);

	MappedFile();
	~MappedFile();
};

#endif

NS_BF_END