#pragma once

#include "../Compiler/BfSystem.h"
#include "../Compiler/BfParser.h"
#include "../Compiler/BfReducer.h"
#include <map>

NS_BF_BEGIN

class CppParser;
class ClangHelper;

class CDepFileData
{
public:
	char* mData;
	int mLength;

public:
	CDepFileData()
	{
		mData = NULL;
		mLength = 0;
	}
};

class CDepFile
{
public:		
	CppParser* mParser;

	CDepFileData mFileData;
	String mFilePath;
	std::vector<CDepFile*> mDepList;
	std::set<String> mFilesReferenced;
	int64 mFileTime;
	bool mProcessing;
	
	// The set of files that tried to include us as we were being processed
	std::set<CDepFile*> mDeferredDepSet;

public:
	~CDepFile();
};

class CppParser;

class CDepChecker;

class CDepEvaluateState
{
public:	
	std::vector<BfAstNode*> mParams;
	CDepEvaluateState* mPrevEvalState;
};

class CppParser : public BfParser
{
public:	
	CDepChecker* mCDepChecker;
	CDepFile* mCDepFile;
	bool mFileAlreadyVisited;
	bool mAborted;
	
public:	
	virtual void HandlePragma(const StringImpl& pragma, BfBlock* block) override;
	virtual void HandleDefine(const StringImpl& name, BfAstNode* paramNode) override;
	virtual void HandleUndefine(const StringImpl& name) override;
	virtual MaybeBool HandleIfDef(const StringImpl& name) override;
	virtual MaybeBool HandleProcessorCondition(BfBlock* paramNode) override;
	virtual void HandleInclude(BfAstNode* paramNode) override;
	virtual void HandleIncludeNext(BfAstNode* paramNode) override;	
	bool IncludeHelper(BfAstNode* paramNode, bool includeNext, bool scanOnly);

	CppParser() : BfParser(NULL, NULL)
	{
		mScanOnly = true;
		mCompatMode = true;
		mCDepChecker = NULL;
		mCDepFile = NULL;
		mFileAlreadyVisited = false;
		mAborted = false;
	}
};

typedef std::map<String, CDepFile*> CDepFileMap;

class CDepChecker
{
public:	
	std::vector<String> mIncludeDirs;
	CDepFileMap mDepFileMap;
	std::vector<CDepFileData> mDepFileData;

	std::vector<CDepFile*> mIncludeStack;
	std::set<String> mFilesVisited;
	bool mAbort;

	std::map<String, bool> mFileExistsCache;
	int mCPPCount;

	String mFindHeaderFileName;
	int mFindHeaderIdx;
	BfAstAllocManager mAstAllocManager;

public:
	CDepChecker();

	CDepFile* LoadFile(const StringImpl& fileName, CDepFile* fileFrom, bool* isFromCache, const char* contentOverride = NULL);
	void ClearCache();
	void SetCArgs(const char* cArgs);

	bool CachedFileExists(const StringImpl& filePath);
};

NS_BF_END
