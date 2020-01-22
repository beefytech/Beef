#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/MemStream.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/FileStream.h"
#include <unordered_map>

NS_BF_BEGIN

struct BeLibMemberHeader
{
	char mName[16];
	char mDate[12];
	char mUserId[6];
	char mGroupId[6];
	char mMode[8];
	char mSize[10];
	char mEnd[2];

	void Init()
	{
		const char* spaces = "                ";
		memcpy(mName, spaces, 16);
		memcpy(mDate, spaces, 12);
		memcpy(mUserId, spaces, 6);
		memcpy(mGroupId, spaces, 6);
		memcpy(mMode, spaces, 8);
		memcpy(mSize, spaces, 10);
		mEnd[0] = '`';
		mEnd[1] = '\n';
	}

	void Init(const char* name, const char* mode, int size)
	{
		Init();
		memcpy(mName, name, strlen(name));
		memcpy(mDate, "0", 1);
		memcpy(mMode, mode, strlen(mode));

		char sizeStr[32];		
		sprintf(sizeStr, "%d", size);
		memcpy(mSize, sizeStr, strlen(sizeStr));
	}
};

class BeLibFile;

class BeLibEntry
{
public:
	BeLibFile* mLibFile;
	String mName;
	bool mReferenced;
	int mOldDataPos;
	int mNewDataPos;
	int mLength;
	Array<String> mSymbols;
	Array<uint8> mData;
	BeLibEntry* mNextWithSameName;

public:	
	BeLibEntry()
	{
		mReferenced = false;
		mOldDataPos = -1;
		mNewDataPos = -1;
		mLength = 0;
		mNextWithSameName = NULL;
	}

	~BeLibEntry()
	{
		delete mNextWithSameName;
	}

	void AddSymbol(const StringImpl& sym);
};

class BeLibFile
{
public:
	String mFilePath;
	String mOldFilePath;
	FileStream mOldFileStream;
	FileStream mFileStream;	
	Dictionary<String, BeLibEntry*> mOldEntries;
	Dictionary<String, BeLibEntry*> mEntries;
	bool mFailed;

public:
	bool ReadLib();

public:
	BeLibFile();
	~BeLibFile();

	bool Init(const StringImpl& fileName, bool moveFile);
	bool Finish();
};

class BeLibManager
{
public:
	CritSect mCritSect;
	Dictionary<String, BeLibFile*> mLibFiles;
	Array<String> mErrors;

public:	
	BeLibManager();
	~BeLibManager();

	void Clear();

	BeLibEntry* AddFile(const StringImpl& fileName, void* data, int size);
	bool AddUsedFileName(const StringImpl& fileName); // Returns true if have old data for this file
	void Finish();

	static String GetLibFilePath(const StringImpl& objFilePath);
	static BeLibManager* Get();		
};


NS_BF_END
