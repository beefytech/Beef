#include "BfSource.h"
#include "BfSystem.h"
#include "BfParser.h"

USING_NS_BF;

static int gSourceCount = 0;

BfSource::BfSource(BfSystem* bfSystem)
{
	gSourceCount++;

	mProject = NULL;
	mSystem = bfSystem;
	mAstAllocManager = NULL;
	if (bfSystem != NULL)
		mAstAllocManager = &gBfParserCache->mAstAllocManager;
	mSrc = NULL;
	
	mSrcLength = 0;
	mSrcAllocSize = -1;

	mErrorRootNode = NULL;
	mSidechannelRootNode = NULL;
	mRootNode = NULL;

	mNextRevision = NULL;
	mPrevRevision = NULL;
	mSourceData = NULL;

	mAstScratch = NULL;
	mIsClosed = false;
	mRefCount = 0;
	mParsingFailed = false;
}

BfSource::~BfSource()
{
	int sourceCount = gSourceCount--;	

	if (mSourceData != NULL)
		delete mSourceData;

	if (mSrcAllocSize >= 0)
		delete mSrc;	

	for (auto typeDef : mTypeDefs)
	{
		NOP;
	}
}

bool BfSource::WantsStats()
{
	auto parser = ToParser();
	if (parser == NULL)
		return false;
	return ((int)parser->mFileName.IndexOf("main2.cs") != -1);
}

BfErrorNode* BfSource::CreateErrorNode(BfAstNode* astNode)
{
	BfErrorNode* errorNode = BfNodeDynCast<BfErrorNode>(astNode);
	if (errorNode == NULL)
	{
		errorNode = mAlloc->Alloc<BfErrorNode>();
		errorNode->Init(astNode->GetSrcStart(), astNode->GetSrcStart(), astNode->GetSrcEnd());
		errorNode->mRefNode = astNode;
	}
	return errorNode;
}

void BfSource::AddErrorNode(BfAstNode* astNode)
{	
	mPendingErrorNodes.push_back(CreateErrorNode(astNode));
}

int BfSource::AllocChars(int charCount)
{
	if (mSrcLength + charCount > mSrcAllocSize)
	{
		int newAllocSize = std::max(mSrcLength + charCount, mSrcAllocSize * 2);
		char* newSrc = new char[newAllocSize + 1];
		memset(newSrc + mSrcAllocSize, 0, newAllocSize - mSrcAllocSize);
		if (mSrc != NULL)
		{
			memcpy(newSrc, mSrc, mSrcLength);
			delete mSrc;
		}
		mSrc = newSrc;
		mSrcAllocSize = newAllocSize;

		BF_ASSERT(mSourceData->ToParser() != NULL);
		mSourceData->mSrc = mSrc;		
	}

	int retVal = mSrcLength;
	mSrcLength += charCount;
	return retVal;
}

/*void BfSource::AddReplaceNode(BfAstNode* astNode, const StringImpl& replaceStr)
{
	int srcStart = AllocChars((int)replaceStr.length());
	memcpy((char*)mSrc + srcStart, replaceStr.c_str(), (int)replaceStr.length());

	auto replaceNode = mAlloc.Alloc<BfReplaceNode>();
	replaceNode->mSource = this;
	replaceNode->mSrcStart = srcStart;
	replaceNode->mSrcEnd = srcStart + (int)replaceStr.length();

	astNode->Add(replaceNode);
}
*/

int NodeCompare(const void* lhs, const void* rhs)
{
	//BfAstNode* leftNode = *((ASTREF(BfAstNode*)*)lhs);
	//BfAstNode* rightNode = *((ASTREF(BfAstNode*)*)rhs);

	BfAstNode* leftNode = *((ASTREF(BfAstNode*)*)lhs);
	BfAstNode* rightNode = *((ASTREF(BfAstNode*)*)rhs);
	return leftNode->GetSrcStart() - rightNode->GetSrcStart();
}

void BfSource::FinishSideNodes()
{
	if (!mPendingSideNodes.IsEmpty())
	{
		mSidechannelRootNode->Init(mPendingSideNodes, mAlloc);
		qsort(mSidechannelRootNode->mChildArr.mVals, mSidechannelRootNode->mChildArr.mSize, sizeof(ASTREF(BfAstNode*)), NodeCompare);
		mPendingSideNodes.clear();
	}
}

void BfSource::Close()
{	
// 	if (mAlloc->mSource == NULL)
// 	{
// 		BF_ASSERT(mErrorRootNode == NULL);
// 		BF_ASSERT(mPendingErrorNodes.size() == 0);
// 		return;
// 	}
	mAstScratch = mAlloc->AllocBytes(SCRATCH_SIZE, sizeof(void*));

	FinishSideNodes();

	if (!mPendingErrorNodes.IsEmpty())
	{
		mErrorRootNode->Init(mPendingErrorNodes, mAlloc);
		qsort(mErrorRootNode->mChildArr.mVals, mErrorRootNode->mChildArr.mSize, sizeof(ASTREF(BfAstNode*)), NodeCompare);
		mPendingErrorNodes.clear();
	}

	mIsClosed = true;
}
