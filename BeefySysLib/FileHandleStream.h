#pragma once

#include "Common.h"
#include "DataStream.h"

NS_BF_BEGIN;

class FileHandleStream : public DataStream
{
public:
	HANDLE					mFileHandle;
	uint8*					mCacheBuffer;
	int						mCacheReadPos;
	int						mCacheSize;
	int						mVFilePos;

public:
	FileHandleStream();
	~FileHandleStream();

	void					SetCacheSize(int size);

	bool					Eof() override;
	int						GetSize() override;
	using DataStream::Read;
	void					Read(void* ptr, int size) override;
	using DataStream::Write;
	void					Write(void* ptr, int size) override;

	int						GetPos() override;
	void					Seek(int size) override;
	void					SetPos(int pos) override;
};

NS_BF_END;
