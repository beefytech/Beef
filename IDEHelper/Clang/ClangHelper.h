#ifdef IDE_C_SUPPORT

#pragma once

#include "BeefySysLib/Common.h"
#include "clang-c/Index.h"
#include "../Compiler/BfSourceClassifier.h"
#include "../Compiler/BfAutoComplete.h"
#include "BeefySysLib/util/PerfTimer.h"

NS_BF_BEGIN

class ClangTranslationUnit
{
public:
	String mTransFileName; // Fake for header files
	String mRequestedFileName;
	CXTranslationUnit mTransUnit;
	char* mChars;
	int mCharLen;
	int mRefCount;

	bool mIsHeader;
	char* mHeaderPrefix;
	int mHeaderPrefixLen;

public:
	ClangTranslationUnit()
	{
		mChars = NULL;
		mCharLen = 0;
		mIsHeader = false;
		mHeaderPrefix = NULL;
		mHeaderPrefixLen = 0;
		mRefCount = 0;
		mTransUnit = NULL;
	}
};

struct StrPathCompare
{
	bool operator() (const StringImpl& lhs, const StringImpl& rhs) const
	{
#ifdef _WIN32
		return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
#else
		return lhs < rhs;
#endif		
	}
};

template <typename T>
typename T::value_type* VecFirstRef(T& vec)
{
	if (vec.size() == 0)
		return 0;
	return &vec[0];
}

typedef std::map<String, ClangTranslationUnit, StrPathCompare> ClangTranslationUnitMap;

// Only one ClangHelper instance (the Resolve one) has write access to this, which means we only
//  need to lock on writing on that one and for reading on anyone else
class ClangUnsavedFiles
{
public:
	CritSect mCritSect;
	std::vector<CXUnsavedFile> mUnsavedFiles;
};

extern ClangUnsavedFiles* gClangUnsavedFiles;

class ClangHelper
{
public:
	CXIndex mIndex;
	bool mIsForResolve;

	String mInvokeString;
	String mReturnString;

	ClangTranslationUnitMap mTranslationUnits;	
	int mCurThreadId;
	int mEntryCount;

public:
	ClangHelper();
	~ClangHelper();
	ClangTranslationUnit* GetTranslationUnit(const StringImpl& fileName, const char* headerPrefix, BfSourceClassifier::CharData* charData = NULL, int charLen = 0, const char** args = NULL, int argCount = 0, bool forceReparse = false, bool noTransUnit = false, bool noReparse = false);

	void AddTranslationUnit(const StringImpl& fileName, const char* headerPrefix, const StringImpl& clangArgs, BfSourceClassifier::CharData* charData, int charLen);
	void RemoveTranslationUnit(const StringImpl& fileName);
	const char* DetermineFilesReferenced(const StringImpl& fileName);
	void CheckErrorRange(const StringImpl& errorString, int startIdx, int endIdx, int errorLookupTextIdx);
	const char* Classify(const StringImpl& fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx, int errorLookupTextIdx, bool ignoreErrors);
	void DecodeCompletionString(CXCompletionString completionString, String& displayText, String& replaceText, String& chunkText);
	const char* Autocomplete(const StringImpl& fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx);

};

NS_BF_END

#endif