#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Hash.h"

NS_BF_BEGIN;

class BfProject;
class BfParser;
class BfParserData;

class BfSourceData
{
public:
	enum ExternalNodesState : int8
	{
		ExternalNodesState_Unchecked,
		ExternalNodesState_Success,
		ExternalNodesState_Failed
	};

public:	
	const char* mSrc;
	int mSrcLength;
	BfAstAllocManager* mAstAllocManager;
	BfAstAllocator mAlloc;

	BfRootNode* mSidechannelRootNode; // Holds comments and preprocessor nodes
	BfRootNode* mRootNode;
	BfRootNode* mErrorRootNode;
	
	BfSizedArray<BfExteriorNode> mExteriorNodes;
	int mExteriorNodesCheckIdx; // 0 = unchecked, -1 = failed, >0 means success and equals the BfSystem.mTypesIdx

	BfSourceData()
	{
		mSrc = NULL;
		mSrcLength = 0;
		mAstAllocManager = NULL;
		mSidechannelRootNode = NULL;
		mRootNode = NULL;
		mErrorRootNode = NULL;
		mExteriorNodesCheckIdx = 0;
	}

	virtual ~BfSourceData()
	{
		BF_ASSERT(mExteriorNodes.mSize >= 0);
		BF_ASSERT(mExteriorNodes.mSize < 0x00FFFFFF);
		delete mSrc;
	}

	virtual BfParserData* ToParserData()
	{
		return NULL;
	}

	virtual BfParser* ToParser()
	{
		return NULL;
	}
};

class BfSource
{
public:
	static const int SCRATCH_SIZE = sizeof(BfGenericInstanceTypeRef) + sizeof(BfDirectStrTypeReference);

	BfSourceData* mSourceData;

	BfProject* mProject;
	BfSystem* mSystem;
	BfAstAllocManager* mAstAllocManager;
	BfAstAllocator* mAlloc;
	const char* mSrc;
	int mSrcLength;
	int mSrcAllocSize;	
	bool mParsingFailed;
	bool mIsClosed;
	uint8* mAstScratch;
	int mRefCount; // Refs from BfTypeDefs
	Array<BfTypeDef*> mTypeDefs;

	BfRootNode* mSidechannelRootNode; // Holds comments and preprocessor nodes
	BfRootNode* mRootNode;
	BfRootNode* mErrorRootNode;
	
	BfParser* mNextRevision;
	BfParser* mPrevRevision;

	SizedArray<BfAstNode*, 8> mPendingSideNodes;
	SizedArray<BfAstNode*, 8> mPendingErrorNodes;

public:	
	bool WantsStats();

public:
	BfSource(BfSystem* bfSystem);
	virtual ~BfSource();

	virtual BfParser* ToParser() { return NULL; }	
	virtual void HadSrcRealloc() {}
	
	BfErrorNode* CreateErrorNode(BfAstNode* astNode);
	void AddErrorNode(BfAstNode* astNode);	
	int AllocChars(int charCount);	
	void FinishSideNodes();
	virtual void Close(); // Writing done, return unused pages but retain used pages	
};

NS_BF_END;