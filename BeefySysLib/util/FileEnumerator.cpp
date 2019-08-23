#include "FileEnumerator.h"

USING_NS_BF;

String FileEnumeratorEntry::GetFilePath() const
{	
    char outName[4096];
    int outSize = 4096;
    BfpFindFileData_GetFileName(mFindData, outName, &outSize, NULL);
    return mDirPath + "/" + outName;
}

String FileEnumeratorEntry::GetFileName() const
{
    char outName[4096];
    int outSize = 4096;
    BfpFindFileData_GetFileName(mFindData, outName, &outSize, NULL);
    return outName;
}

const FileEnumerator::Iterator& FileEnumerator::Iterator::operator++()
{
	BF_ASSERT(mIdx == mFileEnumerator->mIdx);
	mFileEnumerator->Next();
	mIdx = mFileEnumerator->mIdx;
	return *this;
}

bool FileEnumerator::Iterator::operator==(const FileEnumerator::Iterator& rhs)
{
	return 
		(rhs.mFileEnumerator == mFileEnumerator) &&
		(rhs.mIdx == mIdx);
}

bool FileEnumerator::Iterator::operator!=(const FileEnumerator::Iterator& rhs)
{
	return
		(rhs.mFileEnumerator != mFileEnumerator) ||
		(rhs.mIdx != mIdx);
}

const FileEnumerator::FileEnumeratorEntry& FileEnumerator::Iterator::operator*()
{
	return *mFileEnumerator;
}

FileEnumerator::FileEnumerator(const String& dirPath, Flags flags)
{
	mDirPath = dirPath;	
    mFindData = BfpFindFileData_FindFirstFile((dirPath + "/*.*").c_str(), (BfpFindFileFlags)flags, NULL);
	mFlags = flags;    
    if (mFindData != NULL)
        mIdx = 0;
    else
        mIdx = -1;
}

FileEnumerator::~FileEnumerator()
{
    if (mFindData != NULL)
        BfpFindFileData_Release(mFindData);
}

bool FileEnumerator::Next()
{
    if (BfpFindFileData_FindNextFile(mFindData))
        mIdx++;
    else
        mIdx = -1;
    return mIdx != -1;
}

FileEnumerator::Iterator FileEnumerator::begin()
{		
	Iterator itr;
	itr.mFileEnumerator = this;
	itr.mIdx = mIdx;
	return itr;
}

FileEnumerator::Iterator FileEnumerator::end()
{
	Iterator itr;
	itr.mFileEnumerator = this;
	itr.mIdx = -1;
	return itr;
}
