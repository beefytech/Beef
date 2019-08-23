#pragma once

#include "../Common.h"

NS_BF_BEGIN;

class FileEnumeratorEntry
{
public:	
    String mDirPath;
    BfpFindFileData* mFindData;

    String GetFilePath() const;
    String GetFileName() const;
};

class FileEnumerator : public FileEnumeratorEntry
{
public:
	enum Flags
	{
		Flags_Files = 1,
		Flags_Directories = 2,
	};

	class Iterator
	{
	public:
		FileEnumerator* mFileEnumerator;
		int mIdx;

		const Iterator& operator++();
		bool operator==(const Iterator& rhs);
		bool operator!=(const Iterator& rhs);
		const FileEnumeratorEntry& operator*();
	};

public:	    
	Flags mFlags;	
	int mIdx;

public:
    FileEnumerator(const String& fileName, Flags flags = Flags_Files);
	~FileEnumerator();
	
	bool Next();	

	Iterator begin();
	Iterator end();
};

NS_BF_END;
