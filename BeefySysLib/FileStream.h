#pragma once

#include "Common.h"
#include "DataStream.h"

NS_BF_BEGIN;

class FileStream : public DataStream
{
	BF_DISALLOW_COPY(FileStream);
public:
	FILE*					mFP;	
	uint8*					mCacheBuffer;
	int						mCacheReadPos;	
	int						mCacheSize;
	int						mVFilePos;
	bool					mReadPastEnd;

public:
	FileStream();
	~FileStream();

	bool					Open(const StringImpl& filePath, const char* fopenOptions);
	bool					IsOpen();
	void					Close();
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

class FileSubStream : public FileStream
{
public:
	int						mOffset;
	int						mSize;

public:
	int						GetSize() override;
	int						GetPos() override;
	void					SetPos(int pos) override;
};

class SysFileStream : public DataStream
{
	BF_DISALLOW_COPY(SysFileStream);
public:
	BfpFile*				mFile;

public:
	SysFileStream();
	~SysFileStream();

	bool					Open(const StringImpl& filePath, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags);
	bool					IsOpen();
	void					Close();	
	void					SetSizeFast(int size); // May create uninitialized data

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
