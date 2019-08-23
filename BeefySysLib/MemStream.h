#pragma once

#include "Common.h"
#include "DataStream.h"

NS_BF_BEGIN;

class MemStream : public DataStream
{
public:
	uint8*					mData;
	int						mSize;
	int						mPos;
	bool					mFreeMemory;

public:
	MemStream(void* data, int size, bool freeMemory);
	~MemStream();

	static MemStream* CreateWithDuplicate(void* data, int size);
	static MemStream* CreateWithOwnershipTaken(void* data, int size);
	static MemStream* CreateWithNoCopy(void* data, int size);

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

class SafeMemStream : public MemStream
{
public:
	bool mFailed;

	SafeMemStream(void* data, int size, bool freeMemory);

	using DataStream::Read;
	void					Read(void* ptr, int size) override;
};

class DynMemStream : public DataStream
{
public:
	Array<uint8>			mData;
	int						mPos;

public:
	DynMemStream();
		
	bool					Eof() override;
	int						GetSize() override;
	using DataStream::Read;
	void					Read(void* ptr, int size) override;
	using DataStream::Write;
	void					Write(void* ptr, int size) override;
	void					Write(uint8 val) override;

	int						GetPos() override;
	void					Seek(int size) override;
	void					SetPos(int pos) override;

	void					Clear();
	void*					GetPtr();
};

NS_BF_END;
