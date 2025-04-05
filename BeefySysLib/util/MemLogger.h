#pragma once

#include "../Common.h"

NS_BF_BEGIN

class MemLogger
{
public:
	HANDLE mFileMap;
	void* mMemBuffer;
	int mBufferSize;
	int mTotalWriteSize;
	bool mNoOverflow;

public:
	MemLogger();

	~MemLogger();

	bool Create(const StringImpl& memName, int size);
	bool Get(const StringImpl& memName, String& outStr);
	void Log(const char* fmt ...);
	void Write(const void* ptr, int size);
};



NS_BF_END
