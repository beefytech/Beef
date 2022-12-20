#pragma warning(disable:4996)

#include "BfParser.h"
#include "BfReducer.h"
#include "BfPrinter.h"
#include "BfDefBuilder.h"
#include "BfCompiler.h"
#include "BfSourceClassifier.h"
#include "BfSourcePositionFinder.h"
#include <sstream>
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/UTF8.h"
#include "BfAutoComplete.h"
#include "BfResolvePass.h"
#include "BfElementVisitor.h"
#include "BeefySysLib/util/UTF8.h"

extern "C"
{
#include "BeefySysLib/third_party/utf8proc/utf8proc.h"
}

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

static bool IsWhitespace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r');
}

static bool IsWhitespaceOrPunctuation(char c)
{
	switch (c)
	{
	case ',':
	case ';':
	case ':':
	case '(':
	case ')':
	case '[':
	case ']':
	case '{':
	case '}':
	case '<':
	case '>':
	case '/':
	case '-':
	case '=':
	case '+':
	case '!':
	case '%':
	case '&':
	case '|':
	case '#':
	case '@':
	case '`':
	case '^':
	case '~':
	case '*':
	case '?':
	case '\n':
	case ' ':
	case '\t':
	case '\v':
	case '\f':
	case '\r':
		return true;
	default:
		return false;
	}
}

BfParser* BfParserData::ToParser()
{
	if (mUniqueParser != NULL)
	{
		BF_ASSERT(mUniqueParser->mOrigSrcLength >= 0);
		BF_ASSERT((mUniqueParser->mCursorIdx >= -1) || (mUniqueParser->mCursorIdx <= mUniqueParser->mOrigSrcLength));
	}
	return mUniqueParser;
}

//////////////////////////////////////////////////////////////////////////

/// raw_null_ostream - A raw_ostream that discards all output.
/*class debug_ostream : public llvm::raw_ostream
{
/// write_impl - See raw_ostream::write_impl.
void write_impl(const char *Ptr, size_t size) override
{
char str[256] = {0};
memcpy(str, Ptr, std::min((int)size, 255));
OutputDebugStr(str);
}

/// current_pos - Return the current position within the stream, not
/// counting the bytes currently in the buffer.
uint64_t current_pos() const override
{
return 0;
}
};*/

//////////////////////////////////////////////////////////////////////////

BfParserCache* Beefy::gBfParserCache = NULL;

bool BfParserCache::DataEntry::operator==(const LookupEntry& lookup) const
{
	if ((mParserData->mFileName == lookup.mFileName) &&
		(mParserData->mSrcLength == lookup.mSrcLength) &&
		(memcmp(mParserData->mSrc, lookup.mSrc, lookup.mSrcLength) == 0))
	{
		for (auto& setDefine : mParserData->mDefines_Def)
			if (!lookup.mProject->mPreprocessorMacros.Contains(setDefine))
				return false;
		for (auto& setDefine : mParserData->mDefines_NoDef)
			if (lookup.mProject->mPreprocessorMacros.Contains(setDefine))
				return false;

		return true;
	}
	return false;
}

BfParserCache::BfParserCache()
{
	mRefCount = 0;
}

BfParserCache::~BfParserCache()
{
	for (auto& entry : mEntries)
	{
		BF_ASSERT(entry.mParserData->mRefCount == 0);
		delete entry.mParserData;
	}
}

void BfParserCache::ReportMemory(MemReporter* memReporter)
{
	int srcLen = 0;
	int allocBytesUsed = 0;
	int largeAllocs = 0;

	for (auto& entry : mEntries)
	{
		auto parserData = entry.mParserData;
		parserData->ReportMemory(memReporter);
		srcLen += parserData->mSrcLength;
		allocBytesUsed += (int)(parserData->mAlloc.mPages.size() * BfAstAllocManager::PAGE_SIZE);
		largeAllocs += parserData->mAlloc.mLargeAllocSizes;
	}

	int allocPages = 0;
	int usedPages = 0;
	mAstAllocManager.GetStats(allocPages, usedPages);

	OutputDebugStrF("Parsers: %d  Chars: %d  UsedAlloc: %dk  BytesPerChar: %d  SysAllocPages: %d  SysUsedPages: %d (%dk)  LargeAllocs: %dk\n", (int)mEntries.size(), srcLen, allocBytesUsed / 1024,
		allocBytesUsed / BF_MAX(1, srcLen), allocPages, usedPages, (usedPages * BfAstAllocManager::PAGE_SIZE) / 1024, largeAllocs / 1024);

	//memReporter->AddBumpAlloc("BumpAlloc", mAstAllocManager);
}

void BfParserData::ReportMemory(MemReporter* memReporter)
{
	memReporter->Add("JumpTable", mJumpTableSize * sizeof(BfLineStartEntry));
	memReporter->Add("Source", mSrcLength);
	memReporter->AddBumpAlloc("AstAlloc", mAlloc);
}

static int DecodeInt(uint8* buf, int& idx)
{
	int value = 0;
	int shift = 0;
	int curByte;
	do
	{
		curByte = buf[idx++];
		value |= ((curByte & 0x7f) << shift);
		shift += 7;
	} while (curByte >= 128);
	// Sign extend negative numbers.
	if (((curByte & 0x40) != 0) && (shift < 64))
		value |= ~0LL << shift;
	return value;
}

static int gCurDataId = 0;
BfParserData::BfParserData()
{
	mDataId = (int)BfpSystem_InterlockedExchangeAdd32((uint32*)&gCurDataId, 1) + 1;

	mHash = 0;
	mRefCount = -1;
	mJumpTable = NULL;
	mJumpTableSize = 0;
	mFailed = false;
	mCharIdData = NULL;
	mUniqueParser = NULL;
	mDidReduce = false;
}

BfParserData::~BfParserData()
{
	delete[] mJumpTable;
	delete[] mCharIdData;
}

int BfParserData::GetCharIdAtIndex(int findIndex)
{
	if (mCharIdData == NULL)
		return findIndex;

	int encodeIdx = 0;
	int charId = 1;
	int charIdx = 0;
	while (true)
	{
		int cmd = DecodeInt(mCharIdData, encodeIdx);
		if (cmd > 0)
			charId = cmd;
		else
		{
			int spanSize = -cmd;
			if ((findIndex >= charIdx) && (findIndex < charIdx + spanSize))
				return charId + (findIndex - charIdx);
			charId += spanSize;
			charIdx += spanSize;

			if (cmd == 0)
				return -1;
		}
	}
}

void BfParserData::GetLineCharAtIdx(int idx, int& line, int& lineChar)
{
	if (mJumpTableSize <= 0)
	{
		line = 0;
		lineChar = 0;
		return;
	}

	if (idx >= mSrcLength)
		idx = mSrcLength - 1;
	auto* jumpEntry = mJumpTable + (idx / PARSER_JUMPTABLE_DIVIDE);
	if (jumpEntry->mCharIdx > idx)
		jumpEntry--;

	line = jumpEntry->mLineNum;
	lineChar = 0;
	int curSrcPos = jumpEntry->mCharIdx;

	while (curSrcPos < idx)
	{
		if (mSrc[curSrcPos] == '\n')
		{
			line++;
			lineChar = 0;
		}
		else
		{
			lineChar++;
		}

		curSrcPos++;
	}
}

bool BfParserData::IsUnwarnedAt(BfAstNode* node)
{
	if (mUnwarns.empty())
		return false;
	auto unwarnItr = mUnwarns.upper_bound(node->GetSrcStart());
	if (unwarnItr == mUnwarns.begin())
		return false;
	unwarnItr--;

	int unwarnIdx = *unwarnItr;

	int checkIdx = node->GetSrcStart();
	int lineCount = 0;
	while (checkIdx > 0)
	{
		checkIdx--;
		if (checkIdx < unwarnIdx)
			return true;
		if (mSrc[checkIdx] == '\n')
		{
			lineCount++;
			// #unwarn must be immediately preceding the start of the statement containing this node
			if (lineCount == 2)
				return false;
		}
	}

	return true;
}

bool BfParserData::IsWarningEnabledAtSrcIndex(int warningNumber, int srcIdx)
{
	int enabled = 1; //CDH TODO if/when we add warning level support, this default will change based on the warning number and the general project warning level setting
	int lastUnwarnPos = 0;

	for (const auto& it : mWarningEnabledChanges)
	{
		if (it.mKey > srcIdx)
			break;
		if (it.mValue.mWarningNumber == warningNumber)
		{
			if (it.mValue.mEnable)
				enabled++;
			else
				enabled--;
		}
		if (it.mValue.mWarningNumber == -1)
			lastUnwarnPos = -1;
	}
	return enabled > 0;
}

void BfParserData::Deref()
{
	mRefCount--;
	BF_ASSERT(mRefCount >= 0);
	if (mRefCount == 0)
	{
		AutoCrit autoCrit(gBfParserCache->mCritSect);
		BfParserCache::DataEntry dataEntry;
		dataEntry.mParserData = this;
		bool didRemove = gBfParserCache->mEntries.Remove(dataEntry);
		BF_ASSERT(didRemove);
		delete this;
	}
}

//////////////////////////////////////////////////////////////////////////

static int gParserCount = 0;

BfParser::BfParser(BfSystem* bfSystem, BfProject* bfProject) : BfSource(bfSystem)
{
	BfLogSys(bfSystem, "BfParser::BfParser %08X\n", this);

	gParserCount++;

	mTextVersion = -1;
	mEmbedKind = BfSourceEmbedKind_None;
	mUsingCache = false;
	mParserData = NULL;
	mAwaitingDelete = false;
	mScanOnly = false;
	mCompleteParse = false;
	mIsEmitted = false;
	mJumpTable = NULL;
	mProject = bfProject;
	mPassInstance = NULL;
	mSourceClassifier = NULL;
	mPrevRevision = NULL;
	mNextRevision = NULL;
	mOrigSrcLength = 0;
	mSrcAllocSize = -1;
	mSrcLength = 0;
	mSrcIdx = 0;
	mParserFlags = ParserFlag_None;
	mCursorIdx = -1;
	mCursorCheckIdx = -1;
	mLineStart = 0;
	//mCurToken = (BfSyntaxToken)0;
	mToken = BfToken_None;
	mSyntaxToken = BfSyntaxToken_None;

	mTokenStart = 0;
	mTokenEnd = 0;
	mLineNum = 0;
	mCompatMode = false;
	mQuickCompatMode = false;
	mLiteral.mWarnType = 0;
	mDataId = -1;

	mTriviaStart = 0;
	mParsingFailed = false;
	mInAsmBlock = false;
	mPreprocessorIgnoredSectionNode = NULL;
	mPreprocessorIgnoreDepth = 0;

	if (bfProject != NULL)
	{
		for (auto macro : bfProject->mPreprocessorMacros)
			mPreprocessorDefines[macro] = BfDefineState_FromProject;
	}
}

//static std::set<BfAstNode*> gFoundNodes;

BfParser::~BfParser()
{
	int parserCount = gParserCount--;

	if (mParserData == NULL)
	{
	}
	else if (mParserData->mRefCount == -1)
	{
		// Owned data, never intended for cache
		mParserData->mSrc = NULL; // Count on BfSource dtor to release strc
		delete mParserData;
	}
	else if (mParserData->mRefCount == 0)
	{
		// Just never got added to the cache
		delete mParserData;
	}
	else
	{
		mParserData->Deref();
	}

	mSourceData = NULL;

	BfLogSys(mSystem, "BfParser::~BfParser %p\n", this);
}

void BfParser::SetCursorIdx(int cursorIdx)
{
	mCursorIdx = cursorIdx;
	mCursorCheckIdx = cursorIdx;

	int checkIdx = cursorIdx;
	while (checkIdx > 0)
	{
		char c = mSrc[checkIdx - 1];
		if (!IsWhitespace(c))
		{
			if (c == '.')
				mCursorCheckIdx = checkIdx;
			break;
		}
		checkIdx--;
	}
}

//static int gDeleteCount = 0;
//static int gIndentCount = 0;

void BfParser::GetLineCharAtIdx(int idx, int& line, int& lineChar)
{
	mParserData->GetLineCharAtIdx(idx, line, lineChar);
}

int BfParser::GetIndexAtLine(int line)
{
	if (line == 0)
		return 0;

	int curLine = 0;
	for (int i = 0; i < mSrcLength; i++)
	{
		char c = mSrc[i];
		if (c == '\n')
		{
			curLine++;
			if (line == curLine)
				return i + 1;
		}
	}

	return -1;
}

void BfParser::Fail(const StringImpl& error, int offset)
{
	mPassInstance->FailAt(error, mSourceData, mSrcIdx + offset);
}

void BfParser::TokenFail(const StringImpl& error, int offset)
{
	if (mPreprocessorIgnoredSectionNode == NULL)
		Fail(error, offset);
}

void BfParser::Init(uint64 cacheHash)
{
	BF_ASSERT(mParserData == NULL);

	mParserData = new BfParserData();
	mSourceData = mParserData;
	mParserData->mFileName = mFileName;
	if (mDataId != -1)
		mParserData->mDataId = mDataId;
	else
		mDataId = mParserData->mDataId;
	mParserData->mAstAllocManager = &gBfParserCache->mAstAllocManager;
	mParserData->mSrc = mSrc;
	mParserData->mSrcLength = mSrcLength;

	if (cacheHash != 0)
	{
		BfLogSysM("Creating cached parserData %p for %p %s\n", mParserData, this, mFileName.c_str());

		mParserData->mHash = cacheHash;
		mParserData->mRefCount = 0; // 0 means we want to EVENTUALLY write it to the cache
		mSrcAllocSize = -1;
	}
	else
	{
		BfLogSysM("Creating unique parserData %p for %p %s\n", mParserData, this, mFileName.c_str());

		mParserData->mUniqueParser = this;
	}

	mJumpTableSize = ((mSrcLength + 1) + PARSER_JUMPTABLE_DIVIDE - 1) / PARSER_JUMPTABLE_DIVIDE;
	mJumpTable = new BfLineStartEntry[mJumpTableSize];
	memset(mJumpTable, 0, mJumpTableSize * sizeof(BfLineStartEntry));

	mParserData->mJumpTable = mJumpTable;
	mParserData->mJumpTableSize = mJumpTableSize;

	mAlloc = &mParserData->mAlloc;
	mAlloc->mSourceData = mSourceData;
}

void BfParser::NewLine()
{
	mLineStart = mSrcIdx;
	mLineNum++;

	if (mJumpTable == NULL)
		return;
	int idx = (mSrcIdx / PARSER_JUMPTABLE_DIVIDE);
	if (idx == 0)
		return;
	BF_ASSERT(idx < mJumpTableSize);
	BfLineStartEntry* jumpTableEntry = mJumpTable + idx;
	jumpTableEntry->mCharIdx = mSrcIdx;
	jumpTableEntry->mLineNum = mLineNum;
}

void BfParser::SetSource(const char* data, int length)
{
	const int EXTRA_BUFFER_SIZE = 80; // Extra chars for a bit of AllocChars room

	//TODO: Check cache
	// Don't cache if we have a Cursorid set,
	//  if mDataId != -1
	//  if BfParerFlag != 0

	bool canCache = true;
	if (mDataId != -1)
		canCache = false;
	if (mParserFlags != 0)
		canCache = false;
	if (mCursorIdx != -1)
		canCache = false;
	if (mCompatMode)
		canCache = false;
	if (mQuickCompatMode)
		canCache = false;
	if (mFileName.IsEmpty())
		canCache = false;
	if (mProject == NULL)
		canCache = false;
	if (mIsEmitted)
		canCache = false;

	uint64 cacheHash = 0;
	if (canCache)
	{
		AutoCrit autoCrit(gBfParserCache->mCritSect);

		HashContext hashCtx;
		hashCtx.MixinStr(mFileName);
		hashCtx.Mixin(data, length);
		cacheHash = hashCtx.Finish64();

		BfParserCache::LookupEntry lookupEntry;
		lookupEntry.mFileName = mFileName;
		lookupEntry.mSrc = data;
		lookupEntry.mSrcLength = length;
		lookupEntry.mHash = cacheHash;
		lookupEntry.mProject = mProject;

		BfParserCache::DataEntry* dataEntryP;
		if (gBfParserCache->mEntries.TryGetWith(lookupEntry, &dataEntryP))
		{
			mUsingCache = true;

			mParserData = dataEntryP->mParserData;
			BF_ASSERT(mParserData->mDidReduce);

			BfLogSysM("Using cached parserData %p for %p %s\n", mParserData, this, mFileName.c_str());

			mParserData->mRefCount++;
			mSourceData = mParserData;

			mSrc = mParserData->mSrc;
			mSrcLength = mParserData->mSrcLength;
			mOrigSrcLength = mParserData->mSrcLength;
			mSrcAllocSize = -1;
			mSrcIdx = 0;
			mJumpTable = mParserData->mJumpTable;
			mJumpTableSize = mParserData->mJumpTableSize;
			mAlloc = &mParserData->mAlloc;
			return;
		}
	}

	mSrcLength = length;
	mOrigSrcLength = length;
	mSrcAllocSize = mSrcLength /*+ EXTRA_BUFFER_SIZE*/;
	char* ownedSrc = new char[mSrcAllocSize + 1];
	if (data != NULL)
		memcpy(ownedSrc, data, length);
	ownedSrc[length] = 0;
	mSrc = ownedSrc;
	mSrcIdx = 0;

	Init(cacheHash);
}

void BfParser::MoveSource(const char* data, int length) // Takes ownership of data ptr
{
	mSrcLength = length;
	mOrigSrcLength = length;
	mSrcAllocSize = mSrcLength;
	mSrc = data;
	mSrcIdx = 0;
	Init();
}

void BfParser::RefSource(const char* data, int length)
{
	mSrcLength = length;
	mOrigSrcLength = length;
	mSrcAllocSize = -1;
	mSrc = data;
	mSrcIdx = 0;
	Init();
}

bool BfParser::SrcPtrHasToken(const char* name)
{
	const char* namePtr = name;
	int checkIdx = mSrcIdx - 1;
	while (*namePtr)
	{
		if (*(namePtr++) != mSrc[checkIdx])
			return false;
		checkIdx++;
	}
	if (!IsWhitespaceOrPunctuation(mSrc[checkIdx]))
		return false;
	mSrcIdx = checkIdx;
	mTokenEnd = checkIdx;
	return true;
}

void BfParser::AddErrorNode(int startIdx, int endIdx)
{
	auto identifierNode = mAlloc->Alloc<BfIdentifierNode>();
	identifierNode->Init(mTriviaStart, startIdx, endIdx);
	//identifierNode->mSource = this;
	BfSource::AddErrorNode(identifierNode);
}

BfCommentKind BfParser::GetCommentKind(int startIdx)
{
	if ((mSrc[startIdx] == '/') && (mSrc[startIdx + 1] == '*') && (mSrc[startIdx + 2] == '*') && (mSrc[startIdx + 3] == '<'))
		return BfCommentKind_Documentation_Block_Post;
	if ((mSrc[startIdx] == '/') && (mSrc[startIdx + 1] == '/') && (mSrc[startIdx + 2] == '/') && (mSrc[startIdx + 3] == '<'))
		return BfCommentKind_Documentation_Line_Post;
	if ((mSrc[startIdx] == '/') && (mSrc[startIdx + 1] == '*') && (mSrc[startIdx + 2] == '*') && (mSrc[startIdx + 3] != '/'))
		return BfCommentKind_Documentation_Block_Pre;
	if ((mSrc[startIdx] == '/') && (mSrc[startIdx + 1] == '/') && (mSrc[startIdx + 2] == '/') && (mSrc[startIdx + 3] != '/'))
		return BfCommentKind_Documentation_Line_Pre;
	if ((mSrc[startIdx] == '/') && (mSrc[startIdx + 1] == '*'))
		return BfCommentKind_Block;
	return BfCommentKind_Line;
}

bool BfParser::EvaluatePreprocessor(BfExpression* expr)
{
	bool isInvalid = false;

	if (expr == NULL)
		return false;

	if (auto binaryOp = BfNodeDynCast<BfBinaryOperatorExpression>(expr))
	{
		switch (binaryOp->mOp)
		{
		case BfBinaryOp_ConditionalOr:
			return EvaluatePreprocessor(binaryOp->mLeft) || EvaluatePreprocessor(binaryOp->mRight);
		case BfBinaryOp_ConditionalAnd:
			return EvaluatePreprocessor(binaryOp->mLeft) && EvaluatePreprocessor(binaryOp->mRight);
		default: break;
		}
	}

	if (auto unaryOp = BfNodeDynCast<BfUnaryOperatorExpression>(expr))
	{
		switch (unaryOp->mOp)
		{
		case BfUnaryOp_Not:
			return !EvaluatePreprocessor(unaryOp->mExpression);
		default: break;
		}
	}

	if (auto identifier = BfNodeDynCast<BfIdentifierNode>(expr))
	{
		return HandleIfDef(identifier->ToString()) == MaybeBool_True;
	}

	if (auto parenExpr = BfNodeDynCast<BfParenthesizedExpression>(expr))
	{
		return EvaluatePreprocessor(parenExpr->mExpression);
	}

	if (auto literalExpr = BfNodeDynCast<BfLiteralExpression>(expr))
	{
		if (literalExpr->mValue.mTypeCode == BfTypeCode_Boolean)
		{
			return literalExpr->mValue.mBool;
		}
	}

	mPassInstance->Fail("Invalid preprocessor expression", expr);

	return false;
}

BfBlock* BfParser::ParseInlineBlock(int spaceIdx, int endIdx)
{
	BfBlock* block = NULL;
	SizedArray<BfAstNode*, 8> childArr;

	mSrcIdx = spaceIdx;
	BfAstNode* startNode = NULL;
	int usedEndIdx = spaceIdx;
	int usedLineNum = mLineNum;
	int usedLineStart = mLineStart;

	while (true)
	{
		NextToken(endIdx + 1, false, true);
		if (mSyntaxToken == BfSyntaxToken_HIT_END_IDX)
		{
			mSrcIdx = usedEndIdx;
			mLineNum = usedLineNum;
			mLineStart = usedLineStart;

			auto lastNode = mSidechannelRootNode->GetLast();
			if (lastNode != NULL)
				mSrcIdx = std::max(mSrcIdx, lastNode->GetSrcEnd());
			break;
		}

		usedEndIdx = mSrcIdx;
		usedLineStart = mLineStart;
		usedLineNum = mLineNum;

		auto childNode = CreateNode();
		if (childNode == NULL)
			break;
		if ((childNode->IsA<BfCommentNode>()))
		{
			mSidechannelRootNode->Add(childNode);
			mPendingSideNodes.push_back(childNode);
			continue;
		}
		if (startNode == NULL)
			startNode = childNode;
		if (block == NULL)
			block = mAlloc->Alloc<BfBlock>();
		block->Add(childNode);
		childArr.push_back(childNode);
		//block->mChildArr.Add(childNode, &mAlloc);
	}

	if (block != NULL)
		block->Init(childArr, mAlloc);

	return block;
}

BfExpression* BfParser::CreateInlineExpressionFromNode(BfBlock* block)
{
	BfReducer reducer;
	reducer.mPassInstance = mPassInstance;
	reducer.mAlloc = mAlloc;
	reducer.mCompatMode = mCompatMode;
	reducer.mVisitorPos = BfReducer::BfVisitorPos(block);
	reducer.mVisitorPos.MoveNext();
	auto startNode = reducer.mVisitorPos.GetCurrent();
	if (startNode == NULL)
		return NULL;

	auto paramExpression = reducer.CreateExpression(startNode);
	if ((paramExpression != NULL) && (reducer.mVisitorPos.GetNext() != NULL))
		mPassInstance->Fail("Expression parsing error", reducer.mVisitorPos.GetNext());
	return paramExpression;
}

void BfParser::HandlePragma(const StringImpl& pragma, BfBlock* block)
{
	auto itr = block->begin();
	auto paramNode = *itr;

	if (paramNode->ToStringView() == "warning")
	{
		++itr;
		//auto iterNode = paramNode->mNext;
		//BfAstNode* iterNode = parentNode->mChildArr.GetAs<BfAstNode*>(++curIdx);

		BfAstNode* iterNode = itr.Get();
		if (iterNode)
		{
			++itr;

			bool enable;
			if (iterNode->ToStringView() == "disable")
			{
				enable = false;
			}
			else if (iterNode->ToStringView() == "restore")
			{
				enable = true;
			}
			else
			{
				enable = true;
				mPassInstance->FailAt("Expected \"disable\" or \"restore\" after \"warning\"", mSourceData, iterNode->GetSrcStart(), iterNode->GetSrcLength());
			}

			//iterNode = parentNode->mChildArr.GetAs<BfAstNode*>(++curIdx);
			iterNode = itr.Get();
			while (iterNode)
			{
				++itr;
				auto tokenStr = iterNode->ToString();
				if (tokenStr != ",") // commas allowed between warning numbers but not required; we just ignore them
				{
					bool isNum = true;
					for (const auto it : tokenStr)
					{
						char c = it;
						if (c < '0' || c > '9')
						{
							isNum = false;
							break;
						}
					}
					if (isNum)
					{
						BfParserWarningEnabledChange wec;
						wec.mEnable = enable;
						wec.mWarningNumber = atoi(tokenStr.c_str());
						mParserData->mWarningEnabledChanges[iterNode->GetSrcStart()] = wec;
					}
					else
					{
						mPassInstance->FailAt("Expected decimal warning number", mSourceData, iterNode->GetSrcStart(), iterNode->GetSrcLength());
					}
				}

				//iterNode = parentNode->mChildArr.Get(++curIdx);
				iterNode = itr.Get();
			}
		}
		else
		{
			mPassInstance->FailAfterAt("Expected \"disable\" or \"restore\" after \"warning\"", mSourceData, paramNode->GetSrcEnd() - 1);
		}
	}
	else if (paramNode->ToStringView() == "format")
	{
		++itr;
		BfAstNode* iterNode = itr.Get();
		if (iterNode)
		{
			if ((iterNode->ToStringView() != "disable") &&
				(iterNode->ToStringView() != "restore"))
			{
				mPassInstance->FailAfterAt("Expected \"disable\" or \"restore\" after \"format\"", mSourceData, paramNode->GetSrcEnd() - 1);
			}
		}
	}
	else
	{
		mPassInstance->FailAt("Unknown #pragma directive", mSourceData, paramNode->GetSrcStart(), paramNode->GetSrcLength());
	}
}

void BfParser::HandleDefine(const StringImpl& name, BfAstNode* paramNode)
{
	mPreprocessorDefines[name] = BfDefineState_ManualSet;
}

void BfParser::HandleUndefine(const StringImpl& name)
{
	mPreprocessorDefines[name] = BfDefineState_ManualUnset;
}

MaybeBool BfParser::HandleIfDef(const StringImpl& name)
{
	BfDefineState defineState;
	if (mPreprocessorDefines.TryGetValue(name, &defineState))
	{
		if (defineState == BfDefineState_FromProject)
		{
			mParserData->mDefines_Def.Add(name);
		}

		return (defineState != BfDefineState_ManualUnset) ? MaybeBool_True : MaybeBool_False;
	}
	else
	{
		mParserData->mDefines_NoDef.Add(name);
		return MaybeBool_False;
	}
}

MaybeBool BfParser::HandleProcessorCondition(BfBlock* paramNode)
{
	if (paramNode == NULL)
		return MaybeBool_False;

	bool found = false;
	auto paramExpression = CreateInlineExpressionFromNode(paramNode);
	if (paramExpression != NULL)
	{
		return EvaluatePreprocessor(paramExpression) ? MaybeBool_True : MaybeBool_False;
	}
	return MaybeBool_False;
}

void BfParser::HandleInclude(BfAstNode* paramNode)
{
}

void BfParser::HandleIncludeNext(BfAstNode* paramNode)
{
}

bool BfParser::HandlePreprocessor()
{
	int triviaStart = mTriviaStart;

	int checkIdx = 0;
	for (int checkIdx = mLineStart; checkIdx < mSrcIdx - 1; checkIdx++)
	{
		if (!isspace((uint8)mSrc[checkIdx]))
		{
			if (mPreprocessorIgnoreDepth == 0)
			{
				if (mSrc[mSrcIdx - 1] != '#')
					return false;
				mPassInstance->FailAt("Preprocessor directives must appear as the first non-whitespace character on a line", mSourceData, checkIdx);
				break;
			}
			else
				continue; // Keep searching for #endif
		}
	}

	String pragma;
	String pragmaParam;

	switch (mSrc[mSrcIdx - 1])
	{
	case '<':
		if (mPreprocessorIgnoreDepth > 0)
			return false;
		pragma = "<<<";
		break;
	case '=':
		if (mPreprocessorIgnoreDepth > 0)
			return false;
		pragma = "===";
		break;
	case '>':
		if (mPreprocessorIgnoreDepth > 1)
			return false;
		pragma = ">>>";
		break;
	}

	bool atEnd = false;

	int startIdx = mSrcIdx - 1;
	int spaceIdx = -1;
	int charIdx = -1;
	while (true)
	{
		char c = mSrc[mSrcIdx++];

		if (c == '\n')
		{
			int checkIdx = mSrcIdx - 2;
			bool hadSlash = false;
			while (checkIdx >= startIdx)
			{
				char checkC = mSrc[checkIdx];
				if (checkC == '\\')
				{
					hadSlash = true;
					break;
				}
				if ((checkC != ' ') && (checkC != '\t'))
					break;
				checkIdx--;
			}

			if (!hadSlash)
				break;
		}

		if (c == '\0')
		{
			mSrcIdx--;
			break;
		}

		if (charIdx == -1)
		{
			if (!pragma.IsEmpty())
			{
				if (!IsWhitespace(c))
					charIdx = mSrcIdx - 1;
			}
			else
			{
				if (!IsWhitespaceOrPunctuation(c))
					charIdx = mSrcIdx - 1;
			}
		}
		else if ((IsWhitespaceOrPunctuation(c)) && (spaceIdx == -1))
			spaceIdx = mSrcIdx - 1;
	}

	if (charIdx == -1)
	{
		mPassInstance->FailAt("Preprocessor directive expected", mSourceData, startIdx);
		return true;
	}

	int endIdx = mSrcIdx - 1;
	while (endIdx >= startIdx)
	{
		if (!IsWhitespace(mSrc[endIdx]))
			break;
		endIdx--;
	}

	BfBlock* paramNode = NULL;
	if (pragma.IsEmpty())
	{
		if (spaceIdx != -1)
		{
			pragma = String(mSrc + charIdx, mSrc + spaceIdx);
			int breakIdx = spaceIdx;
			while (spaceIdx <= endIdx)
			{
				if (!isspace((uint8)mSrc[spaceIdx]))
					break;
				spaceIdx++;
			}
			if (spaceIdx <= endIdx)
				pragmaParam = String(mSrc + spaceIdx, mSrc + endIdx + 1);

			paramNode = ParseInlineBlock(breakIdx, endIdx);
		}
		else
		{
			pragma = String(mSrc + charIdx, mSrc + endIdx + 1);
			mSrcIdx = endIdx + 1;
		}
	}
	else
	{
		mSrcIdx--;
	}

	bool wantsSingleParam = true;
	bool addToPreprocessorAccept = false;
	bool addToPreprocessorAcceptResolved = true;
	bool wantedParam = false;
	if (mPreprocessorIgnoreDepth > 0)
	{
		BF_ASSERT(!mPreprocessorNodeStack.empty());

		int ignoreEnd = std::max(mPreprocessorIgnoredSectionNode->GetSrcStart(), mLineStart - 1);
		if ((pragma == "endif") || (pragma == ">>>"))
		{
			mPreprocessorIgnoreDepth--;
			if (mPreprocessorIgnoreDepth > 0)
				return true;
			mPreprocessorNodeStack.pop_back();
			mPreprocessorIgnoredSectionNode->SetSrcEnd(ignoreEnd);
			mPreprocessorIgnoredSectionNode = NULL;
			triviaStart = ignoreEnd;
		}
		else if ((pragma == "if") ||
			((mCompatMode) && (pragma == "ifdef")) ||
			((mCompatMode) && (pragma == "ifndef")))
		{
			wantsSingleParam = false;
			wantedParam = true;
			mPreprocessorIgnoreDepth++;
		}
		else if (pragma == "else")
		{
			if (mCompatMode)
			{
				if (paramNode != NULL)
				{
					if (paramNode->ToString() == "if")
					{
						bool found = HandleProcessorCondition(paramNode) != MaybeBool_False;
						if (found)
						{
							addToPreprocessorAccept = true;
							mPreprocessorNodeStack.pop_back();
							mPreprocessorIgnoreDepth = 0;
							mPreprocessorIgnoredSectionNode->SetSrcEnd(ignoreEnd);
							mPreprocessorIgnoredSectionNode = NULL;
							triviaStart = ignoreEnd;
						}
						else
						{
							mPreprocessorIgnoredSectionStarts.insert(mSrcIdx);
						}
						return true;
					}
				}
			}

			if ((mPreprocessorIgnoreDepth == 1) && !mPreprocessorNodeStack.back().second)
			{
				addToPreprocessorAccept = true;
				mPreprocessorNodeStack.pop_back();
				mPreprocessorIgnoreDepth = 0;
				mPreprocessorIgnoredSectionNode->SetSrcEnd(ignoreEnd);
				mPreprocessorIgnoredSectionNode = NULL;
				triviaStart = ignoreEnd;
			}
		}
		else if (pragma == "elif")
		{
			wantsSingleParam = false;
			if ((mPreprocessorIgnoreDepth == 1) && !mPreprocessorNodeStack.back().second)
			{
				wantedParam = true;
				bool found = HandleProcessorCondition(paramNode) != MaybeBool_False;

				if (found)
				{
					addToPreprocessorAccept = true;
					mPreprocessorNodeStack.pop_back();
					mPreprocessorIgnoreDepth = 0;
					mPreprocessorIgnoredSectionNode->SetSrcEnd(ignoreEnd);
					mPreprocessorIgnoredSectionNode = NULL;
					triviaStart = ignoreEnd;
				}
				else
				{
					mPreprocessorIgnoredSectionStarts.insert(mSrcIdx);
				}
			}
		}

		if (mPreprocessorIgnoreDepth > 0)
			return true;
	}
	else
	{
		if ((pragma == "if") || (pragma == "<<<") ||
			((mCompatMode) && (pragma == "ifdef")) ||
			((mCompatMode) && (pragma == "ifndef")))
		{
			wantsSingleParam = false;
			wantedParam = true;
			bool found = false;
			if (pragma == "<<<")
			{
				mPassInstance->FailAt("Conflict marker found", mSourceData, startIdx, endIdx - startIdx + 1);
				wantedParam = false;
				found = true;
			}
			else if (!mQuickCompatMode)
			{
				if (pragma == "if")
					found = HandleProcessorCondition(paramNode) != MaybeBool_False;
				else if (pragma == "ifdef")
					found = HandleIfDef(pragmaParam) != MaybeBool_False;
				else if (pragma == "ifndef")
					found = HandleIfDef(pragmaParam) != MaybeBool_True;
			}

			if (!found)
				mPreprocessorIgnoredSectionStarts.insert(mSrcIdx);

			addToPreprocessorAccept = true;
			if ((!found) && (!mQuickCompatMode) && (!mCompleteParse))
			{
				addToPreprocessorAcceptResolved = false;
				mPreprocessorIgnoreDepth = 1;
			}
		}
		else if ((pragma == "else") || (pragma == "==="))
		{
			if (!mQuickCompatMode && !mCompleteParse)
			{
				if (mPreprocessorNodeStack.empty())
					mPassInstance->FailAt("Unexpected #else", mSourceData, startIdx, mSrcIdx - startIdx);
				else
				{
					BF_ASSERT(mPreprocessorNodeStack.back().second);
					mPreprocessorIgnoreDepth = 1;
				}
			}
		}
		else if (pragma == "elif")
		{
			wantsSingleParam = false;
			if (!mQuickCompatMode && !mCompleteParse)
			{
				if (mPreprocessorNodeStack.empty())
					mPassInstance->FailAt("Unexpected #elif", mSourceData, startIdx, mSrcIdx - startIdx);
				else
				{
					BF_ASSERT(mPreprocessorNodeStack.back().second);
					mPreprocessorIgnoreDepth = 1;
				}
			}
			wantedParam = true;
		}
		else if (pragma == "endif")
		{
			if (mPreprocessorNodeStack.empty())
				mPassInstance->FailAt("Unexpected #endif", mSourceData, startIdx, mSrcIdx - startIdx);
			else
				mPreprocessorNodeStack.pop_back();
		}
		else if (pragma == "define")
		{
			if ((paramNode != NULL) && (!paramNode->mChildArr.IsEmpty()))
				HandleDefine(paramNode->mChildArr[0]->ToString(), paramNode);
			wantedParam = true;
		}
		else if (pragma == "undef")
		{
			if ((paramNode != NULL) && (!paramNode->mChildArr.IsEmpty()))
				HandleUndefine(paramNode->mChildArr[0]->ToString());
			wantedParam = true;
		}
		else if (pragma == "error")
		{
			wantsSingleParam = false;
			mPassInstance->FailAt(pragmaParam, mSourceData, startIdx, mSrcIdx - startIdx);
			wantedParam = true;
		}
		else if (pragma == "warning")
		{
			wantsSingleParam = false;
			mPassInstance->WarnAt(BfWarning_CS1030_PragmaWarning, pragmaParam, mSourceData, startIdx, mSrcIdx - startIdx);
			wantedParam = true;
		}
		else if (pragma == "region")
		{
			wantsSingleParam = false;
			wantedParam = true;
		}
		else if (pragma == "endregion")
		{
			wantsSingleParam = false;
			if (!pragmaParam.empty())
				wantedParam = true;
		}
		else if (pragma == "pragma")
		{
			wantsSingleParam = false;
			wantedParam = true;
			if (paramNode != NULL)
				HandlePragma(pragmaParam, paramNode);
		}
		else if (pragma == "unwarn")
		{
			mParserData->mUnwarns.insert(mSrcIdx);
		}
		else if ((mCompatMode) && (pragma == "include"))
		{
			HandleInclude(paramNode);
			wantedParam = true;
		}
		else if ((mCompatMode) && (pragma == "include_next"))
		{
			HandleIncludeNext(paramNode);
			wantedParam = true;
		}
		else
		{
			mPassInstance->FailAt("Unknown preprocessor directive", mSourceData, startIdx, mSrcIdx - startIdx);
		}
	}

	if ((wantsSingleParam) && (paramNode != NULL) && (paramNode->mChildArr.size() > 1))
	{
		mPassInstance->FailAt("Only one parameter expected", mSourceData, paramNode->GetSrcStart(), paramNode->GetSrcLength());
	}

	if ((wantedParam) && (paramNode == NULL))
	{
		mPassInstance->FailAt("Expected parameter", mSourceData, startIdx, mSrcIdx - startIdx);
	}
	else if ((!wantedParam) && (paramNode != NULL))
	{
		mPassInstance->FailAt("Parameter not expected", mSourceData, startIdx, mSrcIdx - startIdx);
	}

	mTokenStart = charIdx;
	mTokenEnd = charIdx + (int)pragma.length();
	mTriviaStart = -1;
	auto bfPreprocessorCmdNode = mAlloc->Alloc<BfIdentifierNode>();
	bfPreprocessorCmdNode->Init(this);

	mTriviaStart = triviaStart;
	auto bfPreprocessorNode = mAlloc->Alloc<BfPreprocessorNode>();
	mTokenStart = startIdx;
	mTokenEnd = mSrcIdx;
	bfPreprocessorNode->Init(this);
	bfPreprocessorNode->Add(bfPreprocessorCmdNode);
	bfPreprocessorNode->mCommand = bfPreprocessorCmdNode;

	if (paramNode != NULL)
	{
		int curIdx = 0;
		bfPreprocessorNode->mArgument = paramNode;
	}
	mPendingSideNodes.push_back(bfPreprocessorNode);
	mTokenStart = mSrcIdx;
	mTriviaStart = mSrcIdx;
	triviaStart = mSrcIdx;

	if (addToPreprocessorAccept)
		mPreprocessorNodeStack.push_back(std::pair<BfAstNode*, bool>(bfPreprocessorNode, addToPreprocessorAcceptResolved));

	if (mPreprocessorIgnoreDepth > 0)
	{
		mPreprocessorIgnoredSectionNode = mAlloc->Alloc<BfPreprocesorIgnoredSectionNode>();
		mPreprocessorIgnoredSectionNode->Init(this);
		mSidechannelRootNode->Add(mPreprocessorIgnoredSectionNode);
		mPendingSideNodes.push_back(mPreprocessorIgnoredSectionNode);
	}

	return true;
}

static int ValSign(int64 val)
{
	if (val < 0)
		return -1;
	if (val > 0)
		return 1;
	return 0;
}

template <int Len>
struct StrHashT
{
	const static int HASH = 0;
};

template <>
struct StrHashT<4>
{
	template <const char* Str>
	struct DoHash
	{
		const static int HASH = (StrHashT<3>::HASH) ^ Str[4];
	};
};

// This is little endian only
#define TOKEN_HASH(a, b, c, d) ((int)a << 0) | ((int)b << 8) | ((int)c << 16) | ((int)d << 24)

const int text_const = (1 << 2);
const int gClassConst = 0;

uint32 BfParser::GetTokenHash()
{
	char hashChars[4] = { 0 };
	int idx = 0;
	uint32 tokenHash = 0;
	int checkIdx = mSrcIdx - 1;
	while ((!IsWhitespaceOrPunctuation(mSrc[checkIdx])) && (idx < 4))
	{
		hashChars[idx++] = mSrc[checkIdx];
		checkIdx++;
	}
	return *((uint32*)hashChars);
}

double BfParser::ParseLiteralDouble()
{
	char buf[256];
	int len = std::min(mTokenEnd - mTokenStart, 255);

	int outLen = 0;
	for (int i = 0; i < len; i++)
	{
		char c = mSrc[mTokenStart + i];
		if (c != '\'')
			buf[outLen++] = c;
	}

	char c = buf[outLen - 1];
	if ((c == 'd') || (c == 'D') || (c == 'f') || (c == 'F'))
		buf[outLen - 1] = '\0';
	else
		buf[outLen] = '\0';

	return strtod(buf, NULL);
}

void BfParser::NextToken(int endIdx, bool outerIsInterpolate, bool disablePreprocessor)
{
	auto prevToken = mToken;

	mToken = BfToken_None;
	if (mSyntaxToken == BfSyntaxToken_EOF)
		Fail("Unexpected end of file");

	mTriviaStart = mSrcIdx;

	bool isLineStart = true;
	bool isVerbatim = false;
	int interpolateSetting = 0;
	int stringStart = -1;

	while (true)
	{
		bool setVerbatim = false;
		bool setInterpolate = false;
		uint32 checkTokenHash = 0;

		if ((endIdx != -1) && (mSrcIdx >= endIdx))
		{
			mSyntaxToken = BfSyntaxToken_HIT_END_IDX;
			return;
		}

		mTokenStart = mSrcIdx;
		mTokenEnd = mSrcIdx + 1;
		char c = mSrc[mSrcIdx++];

		if (outerIsInterpolate)
		{
			if (c == '"')
			{
				mSyntaxToken = BfSyntaxToken_StringQuote;
				return;
			}
		}

		if ((mPreprocessorIgnoreDepth > 0) && (endIdx == -1))
		{
			if (c == 0)
			{
				mSyntaxToken = BfSyntaxToken_EOF;
				mSrcIdx--;
				break;
			}

			if ((c == '>') && (mSrc[mSrcIdx] == '>') && (mSrc[mSrcIdx + 1] == '>'))
			{
				// Allow through
			}
			else if ((c != '#') || (!isLineStart))
			{
				if (c == '\n')
				{
					NewLine();
					isLineStart = true;
					continue;
				}

				if (IsWhitespace(c))
					continue;

				isLineStart = false;
				continue;
			}
		}

		switch (c)
		{
		case '!':
			if (mSrc[mSrcIdx] == '=')
			{
				if (mSrc[mSrcIdx + 1] == '=')
				{
					mToken = BfToken_CompareStrictNotEquals;
					++mSrcIdx;
					mTokenEnd = ++mSrcIdx;
				}
				else
				{
					mToken = BfToken_CompareNotEquals;
					mTokenEnd = ++mSrcIdx;
				}
			}
			else
				mToken = BfToken_Bang;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '=':
			if (mSrc[mSrcIdx] == '=')
			{
				if (mSrc[mSrcIdx + 1] == '=')
				{
					if (mSrc[mSrcIdx + 2] == '=')
					{
						if (HandlePreprocessor())
						{
							// Conflict split
							break;
						}
						else
						{
							mToken = BfToken_CompareStrictEquals;
							++mSrcIdx;
							mTokenEnd = ++mSrcIdx;
						}
					}
					else
					{
						mToken = BfToken_CompareStrictEquals;
						++mSrcIdx;
						mTokenEnd = ++mSrcIdx;
					}
				}
				else
				{
					mToken = BfToken_CompareEquals;
					mTokenEnd = ++mSrcIdx;
				}
			}
			else if (mSrc[mSrcIdx] == '>')
			{
				mToken = BfToken_FatArrow;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_AssignEquals;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '+':
			if (mSrc[mSrcIdx] == '+')
			{
				mToken = BfToken_DblPlus;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_PlusEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Plus;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '^':
			if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_XorEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Carat;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '~':
			mToken = BfToken_Tilde;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '%':
			if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_ModulusEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Modulus;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '&':
			if (mSrc[mSrcIdx] == '&')
			{
				mToken = BfToken_DblAmpersand;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_AndEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '+')
			{
				if (mSrc[mSrcIdx + 1] == '=')
				{
					mToken = BfToken_AndPlusEquals;
					++mSrcIdx;
				}
				else
					mToken = BfToken_AndPlus;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '-')
			{
				if (mSrc[mSrcIdx + 1] == '=')
				{
					mToken = BfToken_AndMinusEquals;
					++mSrcIdx;
				}
				else
					mToken = BfToken_AndMinus;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '*')
			{
				if (mSrc[mSrcIdx + 1] == '=')
				{
					mToken = BfToken_AndStarEquals;
					++mSrcIdx;
				}
				else
					mToken = BfToken_AndStar;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Ampersand;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '|':
			if (mSrc[mSrcIdx] == '|')
			{
				mToken = BfToken_DblBar;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_OrEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Bar;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '*':
			if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_MultiplyEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Star;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '?':
			if (mSrc[mSrcIdx] == '?')
			{
				mTokenEnd = ++mSrcIdx;
				if (mSrc[mSrcIdx] == '=')
				{
					mToken = BfToken_NullCoalsceEquals;
					mTokenEnd = ++mSrcIdx;
				}
				else
					mToken = BfToken_DblQuestion;
			}
			else if (mSrc[mSrcIdx] == '.')
			{
				mToken = BfToken_QuestionDot;
				mTokenEnd = ++mSrcIdx;
			}
			else if (mSrc[mSrcIdx] == '[')
			{
				mToken = BfToken_QuestionLBracket;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_Question;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '<':
			if (mSrc[mSrcIdx] == '<')
			{
				mTokenEnd = ++mSrcIdx;
				if (mSrc[mSrcIdx] == '=')
				{
					mToken = BfToken_ShiftLeftEquals;
					mTokenEnd = ++mSrcIdx;
				}
				else if (mSrc[mSrcIdx] == '<')
				{
					mSrcIdx--;
					if (HandlePreprocessor())
					{
						// Conflict end
						break;
					}
					else
					{
						mSrcIdx++;
						mToken = BfToken_LDblChevron;
					}
				}
				else
					mToken = BfToken_LDblChevron;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				if (mSrc[mSrcIdx + 1] == '>')
				{
					mToken = BfToken_Spaceship;
					mSrcIdx += 2;
					mTokenEnd = mSrcIdx;
				}
				else
				{
					mToken = BfToken_LessEquals;
					mTokenEnd = ++mSrcIdx;
				}
			}
			else
				mToken = BfToken_LChevron;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '>':
			if (mSrc[mSrcIdx] == '>')
			{
				mTokenEnd = ++mSrcIdx;
				if (mSrc[mSrcIdx] == '=')
				{
					mToken = BfToken_ShiftRightEquals;
					mTokenEnd = ++mSrcIdx;
				}
				else if (mSrc[mSrcIdx] == '>')
				{
					mSrcIdx--;
					if (HandlePreprocessor())
					{
						// Conflict start
						break;
					}
					else
					{
						mSrcIdx++;
						mToken = BfToken_RDblChevron;
					}
				}
				else
					mToken = BfToken_RDblChevron;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_GreaterEquals;
				mTokenEnd = ++mSrcIdx;
			}
			else
				mToken = BfToken_RChevron;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '@':
			setVerbatim = true;
			c = mSrc[mSrcIdx];
			if ((c == '\"') || (c == '$'))
			{
				setVerbatim = true;
			}
			else if (((c >= 'A') && (c <= 'a')) || ((c >= 'a') && (c <= 'z')) || (c == '_') || (c == '@'))
			{
				setVerbatim = true;
			}
			else
			{
				mSyntaxToken = BfSyntaxToken_Identifier;
				return;
			}
			break;
		case '$':

			c = mSrc[mSrcIdx];
			if ((c == '\"') || (c == '@') || (c == '$'))
			{
				setInterpolate = true;
			}
			else if (!mCompatMode)
				Fail("Expected to precede string");
			break;
		case '"':
		case '\'':
		{
			SizedArray<BfUnscopedBlock*, 4> interpolateExpressions;

			String lineHeader;
			String strLiteral;
			char startChar = c;
			bool isMultiline = false;
			int triviaStart = mTriviaStart;

			if ((mSrc[mSrcIdx] == '"') && (mSrc[mSrcIdx + 1] == '"'))
			{
				isMultiline = true;
				mSrcIdx += 2;
			}

			int contentErrorStart = -1;
			int lineIdx = 0;
			int lineIndentIdx = -1;
			if (isMultiline)
			{
				int checkIdx = mSrcIdx;
				int lineStartIdx = checkIdx;

				while (true)
				{
					char c = mSrc[checkIdx++];

					if ((c == '"') && (mSrc[checkIdx] == '"') && (mSrc[checkIdx + 1] == '"'))
					{
						lineIndentIdx = lineStartIdx;
						for (int i = lineStartIdx; i < checkIdx - 1; i++)
						{
							char c = mSrc[i];
							if ((c != '\t') && (c != ' '))
							{
								mPassInstance->FailAt("Multi-line string literal closing delimiter must begin on a new line", mSourceData, i, checkIdx - i + 3);
								break;
							}

							lineHeader.Append(c);
						}
						break;
					}
					else if (c == '\n')
					{
						if (contentErrorStart != -1)
						{
							mPassInstance->FailAt("Multi-line string literal content must begin on a new line", mSourceData, contentErrorStart, checkIdx - contentErrorStart - 1);
							contentErrorStart = -1;
						}

						lineStartIdx = checkIdx;
						lineIdx++;
					}
					else if (c == '\0')
						break; // Will throw an error in next pass
					else if ((c == ' ') || (c == '\t') || (c == '\r'))
					{
						// Allow
					}
					else if (lineIdx == 0)
					{
						if (contentErrorStart == -1)
							contentErrorStart = checkIdx - 1;
					}
				}
			}

			int lineCount = lineIdx + 1;
			lineIdx = 0;
			int lineStart = mSrcIdx;

			while (true)
			{
				char c = mSrc[mSrcIdx++];
				if (c == '\0')
				{
					// Invalid file end
					mPassInstance->FailAt("String not terminated", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
					mSrcIdx--;
					break;
				}
				else if (c == '\n')
				{
					if (isMultiline)
					{
						lineIdx++;

						if ((lineIdx > 1) && (lineIdx < lineCount - 1))
						{
							strLiteral += "\n";
						}

						lineStart = mSrcIdx;
						for (int i = 0; i < lineHeader.GetLength(); i++)
						{
							char wantC = lineHeader[i];
							char c = mSrc[mSrcIdx];
							if (c == '\r')
								continue;
							if (c == '\n')
								break;
							if (wantC == c)
							{
								mSrcIdx++;
							}
							else
							{
								BfError* error = NULL;
								if (c == ' ')
								{
									error = mPassInstance->FailAt("Unexpected space in indentation of line in multi-line string literal", mSourceData, mSrcIdx, 1, BfFailFlag_ShowSpaceChars);
								}
								else if (c == '\t')
								{
									error = mPassInstance->FailAt("Unexpected tab in indentation of line in multi-line string literal", mSourceData, mSrcIdx, 1, BfFailFlag_ShowSpaceChars);
								}
								else
								{
									error = mPassInstance->FailAt("Insufficient indentation of line in multi-line string literal", mSourceData, lineStart, mSrcIdx - lineStart + 1, BfFailFlag_ShowSpaceChars);
								}
								if (error != NULL)
								{
									mPassInstance->MoreInfoAt("Change indentation of this line to match closing delimiter", mSourceData, lineIndentIdx, lineHeader.GetLength(), BfFailFlag_ShowSpaceChars);
								}
								break;
							}
						}

						NewLine();
					}
					else
					{
						mSrcIdx--;
						int errorIdx = mSrcIdx - 1;
						while ((errorIdx > 0) && (IsWhitespace(mSrc[errorIdx])))
							errorIdx--;
						mPassInstance->FailAfterAt("Newline not allowed in string", mSourceData, errorIdx);
						break;
					}
				}
				else if ((c == '"') && (c == startChar))
				{
					if (isMultiline)
					{
						if ((mSrc[mSrcIdx] == '"') && (mSrc[mSrcIdx + 1] == '"')) // Triple quote
						{
							// Done
							mSrcIdx += 2;
							break;
						}

						strLiteral += '"';
					}
					else
					{
						if (mSrc[mSrcIdx] == '"') // Double quote
						{
							strLiteral += '"';
							mSrcIdx++;
						}
						else
							break;
					}
				}
				else if ((c == '\'') && (c == startChar))
				{
					break;
				}
				else if ((c == '\\') && (!isVerbatim))
				{
					char c = mSrc[mSrcIdx++];
					switch (c)
					{
					case '0':
						strLiteral += '\0';
						break;
					case 'a':
						strLiteral += '\a';
						break;
					case 'b':
						strLiteral += '\b';
						break;
					case 'f':
						strLiteral += '\f';
						break;
					case 'n':
						strLiteral += '\n';
						break;
					case 'r':
						strLiteral += '\r';
						break;
					case 't':
						strLiteral += '\t';
						break;
					case 'v':
						strLiteral += '\v';
						break;
					case '\\':
					case '"':
					case '\'':
						strLiteral += c;
						break;
					case '{':
					case '}':
						strLiteral += c;
						if (interpolateSetting > 0)
							strLiteral += c;
						else
							Fail("Invalid escape sequence");
						break;
					case 'x':
					{
						int wantHexChars = 2;

						int hexVal = 0;
						int numHexChars = 0;
						while (true)
						{
							char c = mSrc[mSrcIdx];
							int hexChar = 0;

							if ((c >= '0') && (c <= '9'))
								hexChar = c - '0';
							else if ((c >= 'a') && (c <= 'f'))
								hexChar = c - 'a' + 0xa;
							else if ((c >= 'A') && (c <= 'F'))
								hexChar = c - 'A' + 0xA;
							else
							{
								Fail("Expected two hex characters");
								break;
							}
							mSrcIdx++;
							numHexChars++;
							hexVal = (hexVal * 0x10) + hexChar;

							if (numHexChars == wantHexChars)
								break;
						}

						strLiteral += (char)hexVal;
					}
					break;
					case 'u':
					{
						if (mSrc[mSrcIdx] != '{')
						{
							Fail("Expected hexadecimal code in braces after unicode escape");
							break;
						}
						mSrcIdx++;

						int hexStart = mSrcIdx;

						int hexVal = 0;
						int numHexChars = 0;
						while (true)
						{
							char c = mSrc[mSrcIdx];
							int hexChar = 0;

							if (c == '}')
							{
								if (numHexChars == 0)
									Fail("Unicode escape sequence expects hex digits");

								mSrcIdx++;
								break;
							}

							if ((c >= '0') && (c <= '9'))
								hexChar = c - '0';
							else if ((c >= 'a') && (c <= 'f'))
								hexChar = c - 'a' + 0xa;
							else if ((c >= 'A') && (c <= 'F'))
								hexChar = c - 'A' + 0xA;
							else
							{
								Fail("Hex encoding error");
								break;
							}
							mSrcIdx++;
							numHexChars++;

							if (numHexChars > 8)
							{
								Fail("Too many hex digits for an unicode scalar");
							}

							hexVal = (hexVal * 0x10) + hexChar;
						}

						char outStrUTF8[8];
						int size = u8_toutf8(outStrUTF8, 8, (uint32)hexVal);
						if (size == 0)
						{
							mPassInstance->FailAt("Invalid unicode scalar", mSourceData, hexStart, mSrcIdx - hexStart - 1);
						}
						strLiteral += outStrUTF8;
					}
					break;
					default:
						Fail("Unrecognized escape sequence");
						strLiteral += c;
					}
				}
				else
				{
					strLiteral += c;

					if (interpolateSetting > 0)
					{
						if (c == '{')
						{
							int braceCount = 1;
							while (mSrc[mSrcIdx] == '{')
							{
								braceCount++;
								mSrcIdx++;
							}

							int literalBraces = braceCount;
							if (((interpolateSetting == 1) && (braceCount % 2 == 1)) ||
								((interpolateSetting > 1) && (braceCount >= interpolateSetting)))
							{
								BfUnscopedBlock* newBlock = mAlloc->Alloc<BfUnscopedBlock>();
								mTokenStart = mSrcIdx - interpolateSetting;
								mTriviaStart = mTokenStart;
								mTokenEnd = mTokenStart + 1;
								mToken = BfToken_LBrace;
								newBlock->mOpenBrace = (BfTokenNode*)CreateNode();
								newBlock->Init(this);
								ParseBlock(newBlock, 1, true);
								if (mToken == BfToken_RBrace)
								{
									newBlock->mCloseBrace = (BfTokenNode*)CreateNode();
									newBlock->SetSrcEnd(mSrcIdx);
									mSrcIdx--;
								}
								else if (mSyntaxToken == BfSyntaxToken_StringQuote)
								{
									mSrcIdx--;
									mPassInstance->FailAfterAt("Expected '}'", mSourceData, newBlock->GetSrcEnd() - 1);
								}
								mInAsmBlock = false;
								interpolateExpressions.Add(newBlock);
							}

							if (interpolateSetting == 1)
							{
								for (int i = 0; i < braceCount - 1; i++)
									strLiteral += '{';
							}
							else
							{
								if (braceCount >= interpolateSetting)
								{
									for (int i = 0; i < (braceCount - interpolateSetting) * 2; i++)
										strLiteral += '{';
								}
								else
								{
									for (int i = 0; i < braceCount * 2 - 1; i++)
										strLiteral += '{';
								}
							}
						}
						else if (c == '}')
						{
							int braceCount = 1;
							while (mSrc[mSrcIdx] == '}')
							{
								braceCount++;
								mSrcIdx++;
							}

							bool isClosingBrace = false;

							if (!interpolateExpressions.IsEmpty())
							{
								auto block = interpolateExpressions.back();
								if (block->mCloseBrace == NULL)
								{
									mTokenStart = mSrcIdx - 1;
									mTriviaStart = mTokenStart;
									mTokenEnd = mTokenStart + 1;
									mToken = BfToken_RBrace;
									block->mCloseBrace = (BfTokenNode*)CreateNode();
									block->SetSrcEnd(mSrcIdx);
									isClosingBrace = true;
								}
								else if (block->mCloseBrace->mSrcStart == mSrcIdx - braceCount)
								{
									block->mCloseBrace->mSrcEnd = mSrcIdx - braceCount + interpolateSetting;
									isClosingBrace = true;
								}
							}

							if (interpolateSetting == 1)
							{
								for (int i = 0; i < braceCount - 1; i++)
									strLiteral += '}';
							}
							else
							{
								if (isClosingBrace)
								{
									if (braceCount < interpolateSetting)
										Fail("Mismatched closing brace set");

									for (int i = 0; i < (braceCount - interpolateSetting) * 2; i++)
										strLiteral += '}';
								}
								else
								{
									for (int i = 0; i < braceCount * 2 - 1; i++)
										strLiteral += '}';
								}
							}
						}
					}
				}
			}

			if (stringStart != -1)
			{
				mTokenStart = stringStart;
				stringStart = -1;
			}

			mTriviaStart = triviaStart;
			mTokenEnd = mSrcIdx;
			mSyntaxToken = BfSyntaxToken_Literal;
			if (startChar == '\'')
			{
				mLiteral.mTypeCode = BfTypeCode_Char8;
				if (strLiteral.length() == 0)
				{
					if (mPreprocessorIgnoredSectionNode == NULL)
						mPassInstance->FailAt("Empty char literal", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
				}
				else if (strLiteral.length() > 1)
				{
					int utf8Len = u8_seqlen((char*)strLiteral.c_str());
					if (utf8Len == (int)strLiteral.length())
					{
						mLiteral.mUInt64 = u8_toucs((char*)strLiteral.c_str(), (int)strLiteral.length());
					}
					else if (mPreprocessorIgnoredSectionNode == NULL)
					{
						bool isGraphemeCluster = false;

						// There's no explicit unicode limit to how many diacriticals a grapheme cluster can contain,
						// but we apply a limit for sanity for the purpose of this error
						if (strLiteral.length() < 64)
						{
							int numCodePoints;
							int numCombiningMarks;
							UTF8Categorize(strLiteral.c_str(), (int)strLiteral.length(), numCodePoints, numCombiningMarks);
							isGraphemeCluster = numCodePoints - numCombiningMarks <= 1;
						}

						if (isGraphemeCluster)
							mPassInstance->FailAt("Grapheme clusters cannot be used as character literals", mSourceData, mTokenStart + 1, mSrcIdx - mTokenStart - 2);
						else
							mPassInstance->FailAt("Too many characters in character literal", mSourceData, mTokenStart + 1, mSrcIdx - mTokenStart - 2);
					}
				}
				else
				{
					mLiteral.mInt64 = (uint8)strLiteral[0];
				}

				if (mLiteral.mInt64 >= 0x8000) // Use 0x8000 to remain UTF16-compatible
					mLiteral.mTypeCode = BfTypeCode_Char32;
				else if (mLiteral.mInt64 >= 0x80) // Use 0x80 to remain UTF8-compatible
					mLiteral.mTypeCode = BfTypeCode_Char16;
			}
			else
			{
				auto* strLiteralPtr = new String(std::move(strLiteral));
				mParserData->mStringLiterals.push_back(strLiteralPtr);
				mLiteral.mTypeCode = BfTypeCode_CharPtr;
				mLiteral.mString = strLiteralPtr;
			}

			if (interpolateSetting > 0)
			{
				if (mLiteral.mTypeCode == BfTypeCode_CharPtr)
				{
					auto interpolateExpr = mAlloc->Alloc<BfStringInterpolationExpression>();
					interpolateExpr->mString = mLiteral.mString;
					interpolateExpr->mTriviaStart = mTriviaStart;
					interpolateExpr->mSrcStart = mTokenStart;
					interpolateExpr->mSrcEnd = mSrcIdx;
					BfSizedArrayInitIndirect(interpolateExpr->mExpressions, interpolateExpressions, mAlloc);
					mGeneratedNode = interpolateExpr;
					mSyntaxToken = BfSyntaxToken_GeneratedNode;
					mToken = BfToken_None;
				}
			}

			return;
		}
		break;
		case '/':
			if (mSrc[mSrcIdx] == '/')
			{
				// Comment line
				while (true)
				{
					char c = mSrc[mSrcIdx++];
					if ((c == '\n') || (c == '\0'))
					{
						mSrcIdx--;
						break;
					}
				}
				mTokenEnd = mSrcIdx;

				if (mPreprocessorIgnoredSectionNode == NULL)
				{
					auto commentKind = GetCommentKind(mTokenStart);
					bool handled = false;
					if (!mPendingSideNodes.IsEmpty())
					{
						if (auto prevComment = BfNodeDynCast<BfCommentNode>(mPendingSideNodes.back()))
						{
							// This is required for folding '///' style multi-line documentation into a single node
							if (prevComment->GetTriviaStart() == mTriviaStart)
							{
								auto prevCommentKind = GetCommentKind(prevComment->mSrcStart);

								if ((!BfIsCommentBlock(commentKind)) && (commentKind == prevCommentKind))
								{
									prevComment->SetSrcEnd(mSrcIdx);
									handled = true;
								}
							}
						}
					}

					if ((!handled) && (!disablePreprocessor))
					{
						auto bfCommentNode = mAlloc->Alloc<BfCommentNode>();
						bfCommentNode->Init(this);
						bfCommentNode->mCommentKind = commentKind;
						mSidechannelRootNode->Add(bfCommentNode);
						mPendingSideNodes.push_back(bfCommentNode);
					}
				}
				break;
			}
			else if (mSrc[mSrcIdx] == '=')
			{
				mToken = BfToken_DivideEquals;
				mTokenEnd = ++mSrcIdx;
				mSyntaxToken = BfSyntaxToken_Token;
				return;
			}
			else if (mSrc[mSrcIdx] == '*')
			{
				// Comment block
				int nestCount = 1;
				mSrcIdx++;
				while (true)
				{
					char c = mSrc[mSrcIdx++];
					if (c == '\n')
					{
						NewLine();
					}
					else if ((c == '\0') || ((c == '*') && (mSrc[mSrcIdx] == '/')))
					{
						// Block ends
						if (c == '\0')
						{
							nestCount = 0;
							mSrcIdx--;
						}
						else
						{
							c = 0;
							nestCount--;
							mSrcIdx++;
						}

						if (nestCount == 0)
						{
							mTokenEnd = mSrcIdx;
							if (mPreprocessorIgnoredSectionNode == NULL)
							{
								bool handled = false;
								if (!mPendingSideNodes.IsEmpty())
								{
									if (auto prevComment = BfNodeDynCast<BfCommentNode>(mPendingSideNodes.back()))
									{
										// This is required for folding documentation into a single node
										if (prevComment->GetTriviaStart() == mTriviaStart)
										{
											//TODO: Why did we allow merging BLOCKS of comments together? This messes up BfPrinter word wrapping on comments
// 											if (GetCommentKind(prevComment->mSrcStart) == GetCommentKind(mTokenStart))
// 											{
// 												prevComment->SetSrcEnd(mSrcIdx);
// 												handled = true;
// 											}
										}
									}
								}

								if (!handled)
								{
									auto bfCommentNode = mAlloc->Alloc<BfCommentNode>();
									bfCommentNode->Init(this);
									bfCommentNode->mCommentKind = GetCommentKind(mTokenStart);
									mSidechannelRootNode->Add(bfCommentNode);
									mPendingSideNodes.push_back(bfCommentNode);
								}
							}
							break;
						}
					}
					else if ((!mCompatMode) && ((c == '/') && (mSrc[mSrcIdx] == '*') && (mSrc[mSrcIdx - 2] != '/')))
					{
						nestCount++;
						mSrcIdx++;
					}
				}
			}
			else
			{
				mToken = BfToken_ForwardSlash;
				mSyntaxToken = BfSyntaxToken_Token;
				return;
			}
			break;
		case '#':
			if (disablePreprocessor)
			{
				TokenFail("Unexpected character");
				continue;
			}
			else
				HandlePreprocessor();
			if (mSyntaxToken == BfSyntaxToken_EOF)
				return;
			break;
		case '.':
			if (mSrc[mSrcIdx] == '.')
			{
				if (mSrc[mSrcIdx + 1] == '.')
				{
					mSrcIdx += 2;
					mTokenEnd = mSrcIdx;
					mToken = BfToken_DotDotDot;
					mSyntaxToken = BfSyntaxToken_Token;
				}
				else if (mSrc[mSrcIdx + 1] == '<')
				{
					mSrcIdx += 2;
					mTokenEnd = mSrcIdx;
					mToken = BfToken_DotDotLess;
					mSyntaxToken = BfSyntaxToken_Token;
				}
				else
				{
					mSrcIdx++;
					mTokenEnd = mSrcIdx;
					mToken = BfToken_DotDot;
					mSyntaxToken = BfSyntaxToken_Token;
				}
			}
			else
			{
				mToken = BfToken_Dot;
				mSyntaxToken = BfSyntaxToken_Token;
			}
			return;
		case ',':
			mToken = BfToken_Comma;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case ';':
			if (mInAsmBlock)
			{
				mToken = BfToken_AsmNewline;
				mSyntaxToken = BfSyntaxToken_Token;
			}
			else
			{
				mToken = BfToken_Semicolon;
				mSyntaxToken = BfSyntaxToken_Token;
			}
			return;
		case ':':
		{
			if ((mCompatMode) && (mSrc[mSrcIdx] == ':'))
			{
				mSrcIdx++;
				mTokenEnd = mSrcIdx;
				mToken = BfToken_Dot;
				mSyntaxToken = BfSyntaxToken_Token;
			}
			else
			{
				mToken = BfToken_Colon;
				mSyntaxToken = BfSyntaxToken_Token;
			}
		}
		return;
		case '(':
			mToken = BfToken_LParen;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case ')':
			mToken = BfToken_RParen;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '{':
			mToken = BfToken_LBrace;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '}':
			mToken = BfToken_RBrace;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '[':
			mToken = BfToken_LBracket;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case ']':
			mToken = BfToken_RBracket;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case '\n':
			NewLine();
			if (!mInAsmBlock)
				continue;
			mToken = BfToken_AsmNewline;
			mSyntaxToken = BfSyntaxToken_Token;
			return;
		case ' ':
		case '\t':
		case '\v':
		case '\f':
		case '\r':
			continue; // Whitespace
		case '\0':
			mSrcIdx--; // Stay on EOF marker
			mSyntaxToken = BfSyntaxToken_EOF;
			return;
		default:
			if (((c >= '0') && (c <= '9')) || (c == '-'))
			{
				bool prevIsDot = prevToken == BfToken_Dot;

				if (c == '-')
				{
					// Not a number!
					if (mSrc[mSrcIdx] == '-')
					{
						mToken = BfToken_DblMinus;
						mSrcIdx++;
					}
					else if (mSrc[mSrcIdx] == '=')
					{
						mToken = BfToken_MinusEquals;
						mSrcIdx++;
					}
					else if (mSrc[mSrcIdx] == '>')
					{
						if (mCompatMode)
							mToken = BfToken_Dot;
						else
							mToken = BfToken_Arrow;
						mSrcIdx++;
					}
					else
						mToken = BfToken_Minus;
					mSyntaxToken = BfSyntaxToken_Token;
					mTokenEnd = mSrcIdx;
					return;
				}

				bool hadOverflow = false;
				uint64 val = 0;
				int numberBase = 10;
				int expVal = 0;
				int expSign = 0;
				bool hasExp = false;
				bool hadSeps = false;
				bool hadLeadingHexSep = false;
				int hexDigits = 0;
				if (c == '-')
				{
					BF_FATAL("Parsing error");
				}

				val = c - '0';

				if (c == '0')
				{
					switch (mSrc[mSrcIdx])
					{
					case 'b':
					case 'B':
						numberBase = 2;
						mSrcIdx++;
						break;
					case 'o':
					case 'O':
						numberBase = 8;
						mSrcIdx++;
						break;
					case 'x':
					case 'X':
						numberBase = 16;
						mSrcIdx++;
						break;
					}
				}

				while (true)
				{
					char c = mSrc[mSrcIdx++];

					if (c == '\'')
					{
						hadSeps = true;
						if ((numberBase == 0x10) && (hexDigits == 0))
							hadLeadingHexSep = true;
						continue;
					}

					if ((numberBase == 10) && ((c == 'e') || (c == 'E')))
					{
						// Specifying exponent

						while (true)
						{
							c = mSrc[mSrcIdx++];
							if (c == '+')
							{
								if (expSign != 0)
									TokenFail("Format error");
								expSign = 1;
							}
							else if (c == '-')
							{
								if (expSign != 0)
									TokenFail("Format error");
								expSign = -1;
							}
							else if ((c >= '0') && (c <= '9'))
							{
								hasExp = true;
								expVal *= 10;
								expVal += c - '0';
							}
							else
							{
								if (expSign == -1)
									expVal = -expVal;
								mSrcIdx--;
								break;
							}
						}
						if (!hasExp)
						{
							TokenFail("Expected an exponent");
						}
					}

					bool endNumber = false;

					bool hasDot = c == '.';
					if ((hasDot) && (mSrc[mSrcIdx] == '.'))
					{
						// Skip float parsing if we have a double-dot `1..` case
						hasDot = false;
					}

					// The 'prevIsDot' helps tuple lookups like "tuple.0.0", interpreting those as two integers rather than a float
					if (((hasDot) && (!prevIsDot)) || (hasExp))
					{
						// Switch to floating point mode
						//double dVal = val;
						//double dValScale = 0.1;
						//if (hasExp)
						//dVal *= pow(10, expVal);
						while (true)
						{
							char c = mSrc[mSrcIdx++];

							if (IsWhitespaceOrPunctuation(c))
							{
								mTokenEnd = mSrcIdx - 1;
								mSrcIdx--;
								mLiteral.mTypeCode = BfTypeCode_Double;
								mLiteral.mDouble = ParseLiteralDouble();//dVal;
								mSyntaxToken = BfSyntaxToken_Literal;
								return;
							}

							if ((c == 'e') || (c == 'E'))
							{
								// Specifying exponent

								if (hasExp)
									TokenFail("Format error");

								while (true)
								{
									c = mSrc[mSrcIdx++];
									if (c == '+')
									{
										if (expSign != 0)
											TokenFail("Format error");
										expSign = 1;
									}
									else if (c == '-')
									{
										if (expSign != 0)
											TokenFail("Format error");
										expSign = -1;
									}
									else if ((c >= '0') && (c <= '9'))
									{
										hasExp = true;
										expVal *= 10;
										expVal += c - '0';
									}
									else
									{
										if (expSign == -1)
											expVal = -expVal;
										mSrcIdx--;
										//dVal *= pow(10, expVal);
										break;
									}
								}
								if (!hasExp)
								{
									TokenFail("Expected an exponent");
								}
								continue;
							}

							if ((c == 'f') || (c == 'F'))
							{
								mTokenEnd = mSrcIdx;
								mLiteral.mTypeCode = BfTypeCode_Float;
								mLiteral.mSingle = (float)ParseLiteralDouble();//(float)dVal;
								mSyntaxToken = BfSyntaxToken_Literal;
								return;
							}
							else if ((c == 'd') || (c == 'D'))
							{
								mTokenEnd = mSrcIdx;
								mLiteral.mTypeCode = BfTypeCode_Double;
								mLiteral.mDouble = ParseLiteralDouble();//(double)dVal;
								mSyntaxToken = BfSyntaxToken_Literal;
								return;
							}
							else if ((c >= '0') && (c <= '9'))
							{
								//dVal += (c - '0') * dValScale;
								//dValScale *= 0.1;
							}
							else if ((((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))) &&
								(mSrc[mSrcIdx - 2] == '.'))
							{
								// This is actually a integer followed by an Int32 call (like 123.ToString)
								mSrcIdx -= 2;
								mTokenEnd = mSrcIdx;
								mLiteral.mUInt64 = val;
								mLiteral.mTypeCode = BfTypeCode_IntUnknown;
								mSyntaxToken = BfSyntaxToken_Literal;
								return;
							}
							else
							{
								mSrcIdx--;
								mTokenEnd = mSrcIdx;
								mLiteral.mTypeCode = BfTypeCode_Double;
								mLiteral.mDouble = ParseLiteralDouble();//(double)dVal;
								mSyntaxToken = BfSyntaxToken_Literal;
								TokenFail("Unexpected character while parsing number", 0);
								return;
							}
						}
						return;
					}
					else if (c == '.')
						endNumber = true;

					if (IsWhitespaceOrPunctuation(c))
						endNumber = true;

					if (endNumber)
					{
						mTokenEnd = mSrcIdx - 1;
						mSrcIdx--;

						if ((numberBase == 0x10) &&
							((hexDigits >= 16) || ((hadSeps) && (hexDigits > 8)) || ((hadLeadingHexSep) && (hexDigits == 8))))
						{
							if (hexDigits > 16)
								mPassInstance->FailAt("Too many hex digits for int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
							mLiteral.mUInt64 = val;
							if (val >= 0x8000000000000000)
								mLiteral.mTypeCode = BfTypeCode_UInt64;
							else
								mLiteral.mTypeCode = BfTypeCode_Int64;
						}
						else
						{
							mLiteral.mUInt64 = val;
							mLiteral.mTypeCode = BfTypeCode_IntUnknown;

							if ((numberBase == 0x10) && (hexDigits == 7))
								mLiteral.mWarnType = BfWarning_BF4201_Only7Hex;
							if ((numberBase == 0x10) && (hexDigits == 9))
								mLiteral.mWarnType = BfWarning_BF4202_TooManyHexForInt;

							if (hadOverflow)
							{
								mPassInstance->FailAt("Value doesn't fit into int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
								mLiteral.mTypeCode = BfTypeCode_Int64;
							}
							//else if ((val < -0x80000000LL) || (val > 0xFFFFFFFFLL))
							else if (val >= 0x8000000000000000)
							{
								mLiteral.mTypeCode = BfTypeCode_UInt64;
							}
							else if (val > 0xFFFFFFFFLL)
							{
								mLiteral.mTypeCode = BfTypeCode_Int64;
							}
						}

						mSyntaxToken = BfSyntaxToken_Literal;
						return;
					}

					uint64 prevVal = val;
					if ((c >= '0') && (c <= '9') && (c < '0' + numberBase))
					{
						if (numberBase == 0x10)
							hexDigits++;
						val *= numberBase;
						val += c - '0';
					}
					else if ((numberBase == 0x10) && (c >= 'A') && (c <= 'F'))
					{
						hexDigits++;
						val *= numberBase;
						val += c - 'A' + 0xA;
					}
					else if ((numberBase == 0x10) && (c >= 'a') && (c <= 'f'))
					{
						hexDigits++;
						val *= numberBase;
						val += c - 'a' + 0xa;
					}

					else if ((c == 'u') || (c == 'U'))
					{
						if ((mSrc[mSrcIdx] == 'l') || (mSrc[mSrcIdx] == 'L'))
						{
							if (mSrc[mSrcIdx] == 'l')
								TokenFail("Uppercase 'L' required for int64");
							mSrcIdx++;
							mTokenEnd = mSrcIdx;
							mLiteral.mTypeCode = BfTypeCode_UInt64;
							mLiteral.mUInt64 = (uint64)val;
							if (hexDigits > 16)
								mPassInstance->FailAt("Too many hex digits for int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
							else if (hadOverflow)
								mPassInstance->FailAt("Value doesn't fit into uint64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
							mSyntaxToken = BfSyntaxToken_Literal;
							return;
						}
						mTokenEnd = mSrcIdx;
						mLiteral.mTypeCode = BfTypeCode_UIntPtr;
						mLiteral.mUInt32 = (uint32)val;
						if ((hadOverflow) || ((uint64)val != (uint64)mLiteral.mUInt32))
							mPassInstance->FailAt("Value doesn't fit into uint32", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
						mSyntaxToken = BfSyntaxToken_Literal;
						return;
					}
					else if ((c == 'l') || (c == 'L'))
					{
						if (c == 'l')
							TokenFail("Uppercase 'L' required for int64");
						if ((mSrc[mSrcIdx] == 'u') || (mSrc[mSrcIdx] == 'U'))
						{
							mSrcIdx++;
							mTokenEnd = mSrcIdx;
							mLiteral.mTypeCode = BfTypeCode_UInt64;
							mLiteral.mUInt64 = (uint64)val;
							if (hexDigits > 16)
								mPassInstance->FailAt("Too many hex digits for int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
							else if (hadOverflow)
								mPassInstance->FailAt("Value doesn't fit into uint64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
							mSyntaxToken = BfSyntaxToken_Literal;
							return;
						}
						mTokenEnd = mSrcIdx;
						mLiteral.mTypeCode = BfTypeCode_Int64;
						mLiteral.mInt64 = (int64)val;
						if (val == 0x8000000000000000)
							mLiteral.mTypeCode = BfTypeCode_UInt64;
						else if (val >= 0x8000000000000000)
							hadOverflow = true;
						if (numberBase == 0x10)
						{
							if (hexDigits > 16)
								mPassInstance->FailAt("Too many hex digits for int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
						}
						else if (hadOverflow)
							mPassInstance->FailAt("Value doesn't fit into int64", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
						mSyntaxToken = BfSyntaxToken_Literal;
						return;
					}
					else if ((c == 'f') || (c == 'F'))
					{
						mTokenEnd = mSrcIdx;
						mLiteral.mTypeCode = BfTypeCode_Float;
						mLiteral.mSingle = (float)ParseLiteralDouble();//(float)val;
						mSyntaxToken = BfSyntaxToken_Literal;
						return;
					}
					else if ((c == 'd') || (c == 'D'))
					{
						mTokenEnd = mSrcIdx;
						mLiteral.mTypeCode = BfTypeCode_Double;
						mLiteral.mDouble = ParseLiteralDouble();//(double)val;
						mSyntaxToken = BfSyntaxToken_Literal;
						return;
					}
					else
					{
						mTokenEnd = mSrcIdx - 1;
						mSrcIdx--;
						mLiteral.mUInt64 = val;
						mLiteral.mTypeCode = BfTypeCode_IntUnknown;
						mSyntaxToken = BfSyntaxToken_Literal;
						TokenFail("Unexpected character while parsing number", 0);
						return;
					}

					if ((uint64)prevVal > (uint64)val)
						hadOverflow = true;
				}
			}
			else
			{
				if ((mCompatMode) && (c == '\\'))
				{
					int checkIdx = mSrcIdx;
					bool isAtLineEnd = true;
					while (true)
					{
						char checkC = mSrc[checkIdx];
						if ((checkC == '\r') || (checkC == '\n'))
							break;
						if ((checkC != ' ') && (checkC != '\t'))
						{
							isAtLineEnd = false;
							break;
						}
						checkIdx++;
					}

					if (isAtLineEnd)
						continue;
				}

				if (!isVerbatim)
				{
					switch (GetTokenHash())
					{
					case TOKEN_HASH('t', 'r', 'u', 'e'):
						if (SrcPtrHasToken("true"))
						{
							mLiteral.mTypeCode = BfTypeCode_Boolean;
							mLiteral.mInt64 = 1;
							mSyntaxToken = BfSyntaxToken_Literal;
							return;
						}
						break;
					case TOKEN_HASH('f', 'a', 'l', 's'):
						if (SrcPtrHasToken("false"))
						{
							mLiteral.mTypeCode = BfTypeCode_Boolean;
							mLiteral.mInt64 = 0;
							mSyntaxToken = BfSyntaxToken_Literal;
							return;
						}
						break;

					case TOKEN_HASH('a', 'b', 's', 't'):
						if ((!mCompatMode) && (SrcPtrHasToken("abstract")))
							mToken = BfToken_Abstract;
						break;
					case TOKEN_HASH('a', 'l', 'l', 'o'):
						if (SrcPtrHasToken("alloctype"))
							mToken = BfToken_AllocType;
						break;
					case TOKEN_HASH('a', 'l', 'i', 'g'):
						if (SrcPtrHasToken("alignof"))
							mToken = BfToken_AlignOf;
						break;
					case TOKEN_HASH('a', 'p', 'p', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("append")))
							mToken = BfToken_Append;
						break;
					case TOKEN_HASH('a', 's', 0, 0):
						if ((!mCompatMode) && (SrcPtrHasToken("as")))
							mToken = BfToken_As;
						break;
					case TOKEN_HASH('a', 's', 'm', 0):
						if (SrcPtrHasToken("asm"))
							mToken = BfToken_Asm;
						break;
					case TOKEN_HASH('b', 'a', 's', 'e'):
						if (SrcPtrHasToken("base"))
							mToken = BfToken_Base;
						break;
					case TOKEN_HASH('b', 'o', 'x', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("box")))
							mToken = BfToken_Box;
						break;
					case TOKEN_HASH('b', 'r', 'e', 'a'):
						if (SrcPtrHasToken("break"))
							mToken = BfToken_Break;
						break;
					case TOKEN_HASH('c', 'a', 's', 'e'):
						if (SrcPtrHasToken("case"))
							mToken = BfToken_Case;
						break;
					case TOKEN_HASH('c', 'a', 't', 'c'):
						if (SrcPtrHasToken("catch"))
							mToken = BfToken_Catch;
						break;
					case TOKEN_HASH('c', 'h', 'e', 'c'):
						if ((!mCompatMode) && (SrcPtrHasToken("checked")))
							mToken = BfToken_Checked;
						break;
					case TOKEN_HASH('c', 'l', 'a', 's'):
						if (SrcPtrHasToken("class"))
							mToken = BfToken_Class;
						break;
					case TOKEN_HASH('c', 'o', 'm', 'p'):
						if ((!mCompatMode) && (SrcPtrHasToken("comptype")))
							mToken = BfToken_Comptype;
						break;
					case TOKEN_HASH('c', 'o', 'n', 'c'):
						if ((!mCompatMode) && (SrcPtrHasToken("concrete")))
							mToken = BfToken_Concrete;
						break;
					case TOKEN_HASH('c', 'o', 'n', 's'):
						if (SrcPtrHasToken("const"))
							mToken = BfToken_Const;
						break;
					case TOKEN_HASH('c', 'o', 'n', 't'):
						if (SrcPtrHasToken("continue"))
							mToken = BfToken_Continue;
						break;
					case TOKEN_HASH('d', 'e', 'c', 'l'):
						if (SrcPtrHasToken("decltype"))
							mToken = BfToken_Decltype;
						break;
					case TOKEN_HASH('d', 'e', 'f', 'a'):
						if (SrcPtrHasToken("default"))
							mToken = BfToken_Default;
						break;
					case TOKEN_HASH('d', 'e', 'f', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("defer")))
							mToken = BfToken_Defer;
						break;
					case TOKEN_HASH('d', 'e', 'l', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("delegate")))
							mToken = BfToken_Delegate;
						else if (SrcPtrHasToken("delete"))
							mToken = BfToken_Delete;
						break;
					case TOKEN_HASH('d', 'o', 0, 0):
						if (SrcPtrHasToken("do"))
							mToken = BfToken_Do;
						break;
						break;
					case TOKEN_HASH('e', 'l', 's', 'e'):
						if (SrcPtrHasToken("else"))
							mToken = BfToken_Else;
						break;
					case TOKEN_HASH('e', 'n', 'u', 'm'):
						if (SrcPtrHasToken("enum"))
							mToken = BfToken_Enum;
						break;
					case TOKEN_HASH('e', 'x', 'p', 'l'):
						if ((!mCompatMode) && (SrcPtrHasToken("explicit")))
							mToken = BfToken_Explicit;
						break;
					case TOKEN_HASH('e', 'x', 't', 'e'):
						if (SrcPtrHasToken("extern"))
							mToken = BfToken_Extern;
						else if ((!mCompatMode) && (SrcPtrHasToken("extension")))
							mToken = BfToken_Extension;
						break;
					case TOKEN_HASH('f', 'a', 'l', 'l'):
						if ((!mCompatMode) && (SrcPtrHasToken("fallthrough")))
							mToken = BfToken_Fallthrough;
						break;
					case TOKEN_HASH('f', 'i', 'n', 'a'):
						if (SrcPtrHasToken("finally"))
							mToken = BfToken_Finally;
						break;
					case TOKEN_HASH('f', 'i', 'x', 'e'):
						if (SrcPtrHasToken("fixed"))
							mToken = BfToken_Fixed;
						break;
					case TOKEN_HASH('f', 'o', 'r', 0):
						if (SrcPtrHasToken("for"))
							mToken = BfToken_For;
						break;
					case TOKEN_HASH('f', 'o', 'r', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("foreach")))
						{
							mToken = BfToken_For;
							mPassInstance->WarnAt(0, "'foreach' should be renamed to 'for'", mSourceData, mTokenStart, mSrcIdx - mTokenStart);
						}
						break;
					case TOKEN_HASH('f', 'u', 'n', 'c'):
						if ((!mCompatMode) && (SrcPtrHasToken("function")))
							mToken = BfToken_Function;
						break;
					case TOKEN_HASH('g', 'o', 't', 'o'):
						if (SrcPtrHasToken("goto"))
							mToken = BfToken_Goto;
						break;
					case TOKEN_HASH('i', 'f', 0, 0):
						if (SrcPtrHasToken("if"))
							mToken = BfToken_If;
						break;
					case TOKEN_HASH('i', 'm', 'p', 'l'):
						if ((!mCompatMode) && (SrcPtrHasToken("implicit")))
							mToken = BfToken_Implicit;
						break;
					case TOKEN_HASH('i', 'n', 0, 0):
						if ((!mCompatMode) && (SrcPtrHasToken("in")))
							mToken = BfToken_In;
						break;
					case TOKEN_HASH('i', 'n', 'l', 'i'):
						if ((!mCompatMode) && (SrcPtrHasToken("inline")))
							mToken = BfToken_Inline;
						break;
					case TOKEN_HASH('i', 'n', 't', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("interface")))
							mToken = BfToken_Interface;
						else if ((!mCompatMode) && (SrcPtrHasToken("internal")))
							mToken = BfToken_Internal;
						break;
					case TOKEN_HASH('i', 's', 0, 0):
						if ((!mCompatMode) && (SrcPtrHasToken("is")))
							mToken = BfToken_Is;
						break;
					case TOKEN_HASH('i', 's', 'c', 'o'):
						if ((!mCompatMode) && (SrcPtrHasToken("isconst")))
							mToken = BfToken_IsConst;
						break;
					case TOKEN_HASH('l', 'e', 't', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("let")))
							mToken = BfToken_Let;
						break;
					case TOKEN_HASH('m', 'i', 'x', 'i'):
						if ((!mCompatMode) && (SrcPtrHasToken("mixin")))
							mToken = BfToken_Mixin;
						break;
					case TOKEN_HASH('m', 'u', 't', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("mut")))
							mToken = BfToken_Mut;
						break;
					case TOKEN_HASH('n', 'a', 'm', 'e'):
						if (SrcPtrHasToken("namespace"))
							mToken = BfToken_Namespace;
						else if (SrcPtrHasToken("nameof"))
							mToken = BfToken_NameOf;
						break;
					case TOKEN_HASH('n', 'e', 'w', 0):
						if (SrcPtrHasToken("new"))
							mToken = BfToken_New;
						break;
					case TOKEN_HASH('n', 'u', 'l', 'l'):
						if (SrcPtrHasToken("null"))
							mToken = BfToken_Null;
						else if (SrcPtrHasToken("nullable"))
							mToken = BfToken_Nullable;
						break;
					case TOKEN_HASH('o', 'f', 'f', 's'):
						if (SrcPtrHasToken("offsetof"))
							mToken = BfToken_OffsetOf;
						break;
					case TOKEN_HASH('o', 'p', 'e', 'r'):
						if (SrcPtrHasToken("operator"))
							mToken = BfToken_Operator;
						break;
					case TOKEN_HASH('o', 'u', 't', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("out")))
							mToken = BfToken_Out;
						break;
					case TOKEN_HASH('o', 'v', 'e', 'r'):
						if (SrcPtrHasToken("override"))
							mToken = BfToken_Override;
						break;
					case TOKEN_HASH('p', 'a', 'r', 'a'):
						if ((!mCompatMode) && (SrcPtrHasToken("params")))
							mToken = BfToken_Params;
						break;
					case TOKEN_HASH('p', 'r', 'i', 'v'):
						if (SrcPtrHasToken("private"))
							mToken = BfToken_Private;
						break;
					case TOKEN_HASH('p', 'r', 'o', 't'):
						if (SrcPtrHasToken("protected"))
							mToken = BfToken_Protected;
						break;
					case TOKEN_HASH('p', 'u', 'b', 'l'):
						if (SrcPtrHasToken("public"))
							mToken = BfToken_Public;
						break;
					case TOKEN_HASH('r', 'e', 'a', 'd'):
						if ((!mCompatMode) && (SrcPtrHasToken("readonly")))
							mToken = BfToken_ReadOnly;
						break;
					case TOKEN_HASH('r', 'e', 'f', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("ref")))
							mToken = BfToken_Ref;
						break;
					case TOKEN_HASH('r', 'e', 'p', 'e'):
						if ((!mCompatMode) && (SrcPtrHasToken("repeat")))
							mToken = BfToken_Repeat;
						break;
					case TOKEN_HASH('r', 'e', 't', 't'):
						if ((!mCompatMode) && (SrcPtrHasToken("rettype")))
							mToken = BfToken_RetType;
						break;
					case TOKEN_HASH('r', 'e', 't', 'u'):
						if (SrcPtrHasToken("return"))
							mToken = BfToken_Return;
						break;
					case TOKEN_HASH('s', 'c', 'o', 'p'):
						if ((!mCompatMode) && (SrcPtrHasToken("scope")))
							mToken = BfToken_Scope;
						break;
					case TOKEN_HASH('s', 'e', 'a', 'l'):
						if ((!mCompatMode) && (SrcPtrHasToken("sealed")))
							mToken = BfToken_Sealed;
						break;
					case TOKEN_HASH('s', 'i', 'z', 'e'):
						if (SrcPtrHasToken("sizeof"))
							mToken = BfToken_SizeOf;
						break;
					case TOKEN_HASH('s', 't', 'a', 'c'):
						if ((!mCompatMode) && (SrcPtrHasToken("stack")))
							mToken = BfToken_Stack;
						break;
					case TOKEN_HASH('s', 't', 'a', 't'):
						if (SrcPtrHasToken("static"))
							mToken = BfToken_Static;
						break;
					case TOKEN_HASH('s', 't', 'r', 'i'):
						if (SrcPtrHasToken("strideof"))
							mToken = BfToken_StrideOf;
						break;
					case TOKEN_HASH('s', 't', 'r', 'u'):
						if (SrcPtrHasToken("struct"))
							mToken = BfToken_Struct;
						break;
					case TOKEN_HASH('s', 'w', 'i', 't'):
						if (SrcPtrHasToken("switch"))
							mToken = BfToken_Switch;
						break;
					case TOKEN_HASH('t', 'h', 'i', 's'):
						if (SrcPtrHasToken("this"))
							mToken = BfToken_This;
						break;
					case TOKEN_HASH('t', 'h', 'r', 'o'):
						if (SrcPtrHasToken("throw"))
							mToken = BfToken_Throw;
						break;
					case TOKEN_HASH('t', 'r', 'y', 0):
						if (SrcPtrHasToken("try"))
							mToken = BfToken_Try;
						break;
					case TOKEN_HASH('t', 'y', 'p', 'e'):
						if (SrcPtrHasToken("typeof"))
							mToken = BfToken_TypeOf;
						else if (SrcPtrHasToken("typealias"))
							mToken = BfToken_TypeAlias;
						break;
					case TOKEN_HASH('u', 'n', 'c', 'h'):
						if (SrcPtrHasToken("unchecked"))
							mToken = BfToken_Unchecked;
						break;
					case TOKEN_HASH('u', 'n', 's', 'i'):
						if (mCompatMode)
						{
							if (SrcPtrHasToken("unsigned"))
								mToken = BfToken_Unsigned;
						}
						break;
					case TOKEN_HASH('u', 's', 'i', 'n'):
						if (SrcPtrHasToken("using"))
							mToken = BfToken_Using;
						break;
					case TOKEN_HASH('v', 'a', 'r', 0):
						if ((!mCompatMode) && (SrcPtrHasToken("var")))
							mToken = BfToken_Var;
						break;
					case TOKEN_HASH('v', 'i', 'r', 't'):
						if (SrcPtrHasToken("virtual"))
							mToken = BfToken_Virtual;
						break;
					case TOKEN_HASH('v', 'o', 'l', 'a'):
						if (SrcPtrHasToken("volatile"))
							mToken = BfToken_Volatile;
						break;
					case TOKEN_HASH('w', 'h', 'e', 'n'):
						if (SrcPtrHasToken("when"))
							mToken = BfToken_When;
						break;
					case TOKEN_HASH('w', 'h', 'e', 'r'):
						if (SrcPtrHasToken("where"))
							mToken = BfToken_Where;
						break;
					case TOKEN_HASH('w', 'h', 'i', 'l'):
						if (SrcPtrHasToken("while"))
							mToken = BfToken_While;
						break;
					case TOKEN_HASH('y', 'i', 'e', 'l'):
						if ((!mCompatMode) && (SrcPtrHasToken("yield")))
							mToken = BfToken_Yield;
						break;
					}
				}
				if (mToken != BfToken_None)
				{
					mSyntaxToken = BfSyntaxToken_Token;
					return;
				}

				bool allowChar = false;
				if (mCompatMode)
					allowChar = (c == '$') || (c == '`');

				if ((uint8)c >= 0xC0)
				{
					int cLen = 0;
					mSrcIdx--;
					uint32 c32 = u8_toucs(mSrc + mSrcIdx, mOrigSrcLength - mSrcIdx, &cLen);
					mSrcIdx += cLen;

					utf8proc_category_t cat = utf8proc_category(c32);
					switch (cat)
					{
					case UTF8PROC_CATEGORY_LU:
					case UTF8PROC_CATEGORY_LL:
					case UTF8PROC_CATEGORY_LT:
					case UTF8PROC_CATEGORY_LM:
					case UTF8PROC_CATEGORY_LO:
					case UTF8PROC_CATEGORY_NL:
					case UTF8PROC_CATEGORY_SM:
					case UTF8PROC_CATEGORY_SC:
					case UTF8PROC_CATEGORY_SK:
					case UTF8PROC_CATEGORY_SO:

						allowChar = true;
					default: break;
					}
				}

				if ((allowChar) ||
					((c >= 'A') && (c <= 'Z')) ||
					((c >= 'a') && (c <= 'z')) ||
					(c == '_'))
				{
					if (stringStart != -1)
					{
						mTokenStart = stringStart;
						stringStart = -1;
					}

					while (true)
					{
						int curSrcIdx = mSrcIdx;
						char c = mSrc[mSrcIdx++];
						bool isValidChar =
							(((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) || (c == '_') || ((c >= '0') && (c <= '9')));
						if (mCompatMode)
							isValidChar |= (c == '$') || (c == '`') || (c == '\'');

						if ((uint8)c >= 0xC0)
						{
							int cLen = 0;
							mSrcIdx--;
							uint32 c32 = u8_toucs(mSrc + mSrcIdx, mOrigSrcLength - mSrcIdx, &cLen);
							mSrcIdx += cLen;

							utf8proc_category_t cat = utf8proc_category(c32);
							switch (cat)
							{
							case UTF8PROC_CATEGORY_LU:
							case UTF8PROC_CATEGORY_LL:
							case UTF8PROC_CATEGORY_LT:
							case UTF8PROC_CATEGORY_LM:
							case UTF8PROC_CATEGORY_LO:
							case UTF8PROC_CATEGORY_NL:
							case UTF8PROC_CATEGORY_SM:
							case UTF8PROC_CATEGORY_SC:
							case UTF8PROC_CATEGORY_SK:
							case UTF8PROC_CATEGORY_SO:

							case UTF8PROC_CATEGORY_MN:
							case UTF8PROC_CATEGORY_MC:
							case UTF8PROC_CATEGORY_ND:
							case UTF8PROC_CATEGORY_PC:
							case UTF8PROC_CATEGORY_CF:
								isValidChar = true;
							default: break;
							}
						}

						if (!isValidChar)
						{
							mTokenEnd = curSrcIdx;
							mSrcIdx = curSrcIdx;
							mSyntaxToken = BfSyntaxToken_Identifier;
							return;
						}
					}

					mSyntaxToken = BfSyntaxToken_Identifier;
					return;
				}
				else
				{
					AddErrorNode(mTokenStart, mSrcIdx);
					mTriviaStart = mSrcIdx;
					TokenFail("Unexpected character");
					continue;
				}
			}
			return;
		}

		if ((setVerbatim) && (!isVerbatim))
		{
			isVerbatim = true;
			stringStart = mTokenStart;
		}
		if (setInterpolate)
		{
			if (interpolateSetting == 0)
				stringStart = mTokenStart;
			interpolateSetting++;
		}
	}
}

static int gParseBlockIdx = 0;
static int gParseMemberIdx = 0;

void BfParser::ParseBlock(BfBlock* astNode, int depth, bool isInterpolate)
{
	gParseBlockIdx++;
	int startParseBlockIdx = gParseBlockIdx;

	bool isAsmBlock = false;
	bool isTernary = false;

	SizedArray<BfAstNode*, 32> childArr;

	int parenDepth = 0;

	while (true)
	{
		if ((mSyntaxToken == BfSyntaxToken_Token) && (mToken == BfToken_Asm))
		{
			if (isAsmBlock || mInAsmBlock)
				mPassInstance->Fail("Already inside an 'asm' block", astNode);
			else
				isAsmBlock = true;
		}

		NextToken(-1, isInterpolate && (parenDepth == 0));

		if (mPreprocessorIgnoredSectionNode != NULL)
		{
			if (mSyntaxToken != BfSyntaxToken_EOF)
				continue;
			mPreprocessorIgnoredSectionNode->SetSrcEnd(mSrcIdx);
		}

		if (mScanOnly)
		{
			if (mSyntaxToken == BfSyntaxToken_EOF)
				break;
			continue;
		}

		gParseMemberIdx++;
		int memberIdx = gParseMemberIdx;

		auto childNode = CreateNode();
		if (childNode == NULL)
			break;
		if (mSyntaxToken == BfSyntaxToken_EOF)
		{
			if (astNode != 0)
				Fail("Unexpected end of file");
			break;
		}

		if (mToken == BfToken_LBrace)
		{
			BfBlock* newBlock;
			BfInlineAsmStatement* asmBlock = nullptr;
			BfBlock* genBlock = nullptr;
			/*if (isAsmBlock)
			{
			asmBlock = mAlloc->Alloc<BfInlineAsmStatement>();
			asmBlock->mOpenBrace = (BfTokenNode*)CreateNode();
			newBlock = asmBlock;
			mInAsmBlock = true;
			isAsmBlock = false;
			}
			else*/
			{
				genBlock = mAlloc->Alloc<BfBlock>();
				genBlock->mOpenBrace = (BfTokenNode*)CreateNode();
				newBlock = genBlock;
			}
			newBlock->Init(this);
			ParseBlock(newBlock, depth + 1);
			if (mToken == BfToken_RBrace)
			{
				if (genBlock)
					genBlock->mCloseBrace = (BfTokenNode*)CreateNode();
				else if (asmBlock)
					asmBlock->mCloseBrace = (BfTokenNode*)CreateNode();

				newBlock->SetSrcEnd(mSrcIdx);
			}
			else
			{
				if (mSyntaxToken == BfSyntaxToken_EOF)
					mPassInstance->FailAfterAt("Expected '}'", mSourceData, newBlock->GetSrcEnd() - 1);
			}
			mInAsmBlock = false;
			astNode->Add(newBlock);
			childArr.push_back(newBlock);
		}
		else if (mToken == BfToken_RBrace)
		{
			if (depth == 0)
				Fail("Unexpected ending brace");
			break;
		}
		else
		{
			if (mToken == BfToken_LParen)
				parenDepth++;
			else if (mToken == BfToken_RParen)
				parenDepth--;

			if ((isInterpolate) && (parenDepth == 0))
			{
				if (mToken == BfToken_Question)
					isTernary = true;

				bool endNow = false;
				if (mToken == BfToken_Colon)
				{
					endNow = true;
					if (!childArr.IsEmpty())
					{
						if (auto prevToken = BfNodeDynCast<BfTokenNode>(childArr.back()))
						{
							if ((prevToken->mToken == BfToken_Scope) || (prevToken->mToken == BfToken_New) || (prevToken->mToken == BfToken_Bang) ||
								(prevToken->mToken == BfToken_Colon))
								endNow = false;
						}
					}

					if ((endNow) && (isTernary))
					{
						isTernary = false;
						endNow = false;
					}
				}

				if (mToken == BfToken_Comma)
					endNow = true;

				if (endNow)
				{
					mSrcIdx = mTokenStart;
					break;
				}
			}

			astNode->Add(childNode);
			childArr.Add(childNode);

			if ((mSyntaxToken == BfSyntaxToken_Token) && (mToken == BfToken_RBrace))
				break;
		}
	}

	astNode->Init(childArr, mAlloc);
}

const char* BfNodeToString(BfAstNode* node)
{
	static char str[256] = { 0 };
	strncpy(str, node->GetSourceData()->mSrc + node->GetSrcStart(), node->GetSrcLength());
	return str;
}

BfAstNode* BfParser::CreateNode()
{
	switch (mSyntaxToken)
	{
		case BfSyntaxToken_Token:
		{
			auto bfTokenNode = mAlloc->Alloc<BfTokenNode>();
			bfTokenNode->Init(this);
			bfTokenNode->SetToken(mToken);
			return bfTokenNode;
		}
	case BfSyntaxToken_Identifier:
		{
			//auto bfIdentifierNode = new BfIdentifierNode();
			auto bfIdentifierNode = mAlloc->Alloc<BfIdentifierNode>();
			bfIdentifierNode->Init(this);
			return bfIdentifierNode;
		}
	case BfSyntaxToken_Literal:
		{
			auto bfLiteralExpression = mAlloc->Alloc<BfLiteralExpression>();
			bfLiteralExpression->Init(this);
			bfLiteralExpression->mValue = mLiteral;
			mLiteral.mTypeCode = BfTypeCode_None;
			mLiteral.mWarnType = 0;
			return bfLiteralExpression;
		}
	case BfSyntaxToken_GeneratedNode:
		return mGeneratedNode;
	default: break;
	}

	return NULL;
}

void BfParser::Parse(BfPassInstance* passInstance)
{
	BP_ZONE_F("BfParser::Parse %s", mFileName.c_str());

	mSyntaxToken = BfSyntaxToken_None;
	mPassInstance = passInstance;

	int startIdx = mSrcIdx;

	if (mUsingCache)
	{
		mRootNode = mParserData->mRootNode;
		mSidechannelRootNode = mParserData->mSidechannelRootNode;
		mErrorRootNode = mParserData->mErrorRootNode;
		return;
	}

	mRootNode = mAlloc->Alloc<BfRootNode>();
	mRootNode->Init(this);
	mParserData->mRootNode = mRootNode;

	mSidechannelRootNode = mAlloc->Alloc<BfRootNode>();
	mSidechannelRootNode->Init(this);
	mParserData->mSidechannelRootNode = mSidechannelRootNode;

	mErrorRootNode = mAlloc->Alloc<BfRootNode>();
	mErrorRootNode->Init(this);
	mParserData->mErrorRootNode = mErrorRootNode;

	ParseBlock(mRootNode, 0);

	if (mPreprocessorNodeStack.size() > 0)
	{
		mPassInstance->Warn(0, "No matching #endif found", mPreprocessorNodeStack.back().first);
	}

	if (mJumpTable != NULL)
	{
		for (int i = (startIdx / PARSER_JUMPTABLE_DIVIDE) + 1; i < mJumpTableSize; i++)
			if (mJumpTable[i].mCharIdx == 0)
				mJumpTable[i] = mJumpTable[i - 1];
	}

	if (mPassInstance->HasFailed())
		mParsingFailed = true;

	if ((mPassInstance->HasMessages()) || (mParsingFailed))
	{
		mParserData->mFailed = true; // Don't reuse cache if there were errors or warnings
	}

	mPassInstance = NULL;
}

int BfParser::GetCharIdAtIndex(int findIndex)
{
	return mParserData->GetCharIdAtIndex(findIndex);
}

void BfParser::Close()
{
	BfSource::Close();

	BfLogSys(mSystem, "Parser %p closing. RefCount:%d Failed:%d\n", this, mParserData->mRefCount, mParserData->mFailed);

	if ((mParserData->mRefCount == 0) && (!mParserData->mFailed))
	{
		BF_ASSERT(mParserData->mDidReduce);

		AutoCrit autoCrit(gBfParserCache->mCritSect);
		BfParserCache::DataEntry dataEntry;
		dataEntry.mParserData = mParserData;
		if (gBfParserCache->mEntries.Add(dataEntry))
		{
			BfLogSys(mSystem, "Parser %p added to cache\n", this);
			mParserData->mRefCount++;
		}
		else
		{
			// It's possible two of the same entries were being parsed at the same time on different threads.
			//  Just let the loser be deleted in the dtor
			BfLogSys(mSystem, "Duplicate parser %p not added to cache\n", this);
		}
	}
}

void BfParser::HadSrcRealloc()
{
	int jumpTableSize = ((mSrcAllocSize + 1) + PARSER_JUMPTABLE_DIVIDE - 1) / PARSER_JUMPTABLE_DIVIDE;
	if (jumpTableSize > mJumpTableSize)
	{
		auto jumpTable = new BfLineStartEntry[jumpTableSize];
		memset(jumpTable, 0, jumpTableSize * sizeof(BfLineStartEntry));
		memcpy(jumpTable, mJumpTable, mJumpTableSize * sizeof(BfLineStartEntry));

		delete [] mJumpTable;

		mJumpTable = jumpTable;
		mJumpTableSize = jumpTableSize;

		mParserData->mJumpTable = mJumpTable;
		mParserData->mJumpTableSize = mJumpTableSize;
	}
}

void BfParser::GenerateAutoCompleteFrom(int srcPosition)
{
	BfSourcePositionFinder posFinder(this, srcPosition);
	posFinder.Visit(mRootNode);
	if (posFinder.mClosestElement != NULL)
	{
	}
}

void BfParser::ReportMemory(MemReporter* memReporter)
{
	//memReporter->Add("SmallAstAlloc", (int)mAlloc->mPages.size() * BfAstAllocManager::PAGE_SIZE);
	//memReporter->Add("LargeAstAlloc", mAlloc->mLargeAllocSizes);

// 	if (!mUsingCache)
// 		memReporter->AddBumpAlloc("AstAlloc", *mAlloc);
//
// 	memReporter->Add("JumpTable", mJumpTableSize * sizeof(BfLineStartEntry));

	memReporter->Add(sizeof(BfParser));
	if (mParserData->mRefCount <= 0)
		mParserData->ReportMemory(memReporter);

// 	if (mSrcAllocSize > 0)
// 		memReporter->Add("Source", mSrcAllocSize);
}

class BfInnermostFinder : public BfElementVisitor
{
public:
	int mCursorIdx;
	BfAstNode* mFoundNode;

	BfInnermostFinder(int cursorIdx)
	{
		mFoundNode = NULL;
		mCursorIdx = cursorIdx;
	}

	virtual void Visit(BfAstNode* node) override
	{
		if ((node->Contains(mCursorIdx)) && (!node->IsA<BfBlock>()))
		{
			if ((mFoundNode == NULL) || ((node->GetSrcLength()) <= (mFoundNode->GetSrcLength())))
				mFoundNode = node;
		}
	}

	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override
	{
		BfElementVisitor::Visit(memberRefExpr);
		if (mFoundNode == memberRefExpr->mMemberName)
		{
			mFoundNode = memberRefExpr;
		}
	}

	virtual void Visit(BfAttributedIdentifierNode* identifierNode) override
	{
		BfElementVisitor::Visit(identifierNode);
		if (mFoundNode == identifierNode->mIdentifier)
		{
			mFoundNode = identifierNode;
		}
	}

	virtual void Visit(BfQualifiedNameNode* qualifiedNameNode) override
	{
		BfElementVisitor::Visit(qualifiedNameNode);
		if (mFoundNode == qualifiedNameNode->mRight)
		{
			mFoundNode = qualifiedNameNode;
		}
	}

	virtual void Visit(BfBinaryOperatorExpression* binaryOpExpr) override
	{
		BfElementVisitor::Visit(binaryOpExpr);
		if (mFoundNode == binaryOpExpr->mOpToken)
		{
			mFoundNode = binaryOpExpr;
		}
	}

	virtual void Visit(BfPreprocessorNode* preprocNode) override
	{
		if (preprocNode->mArgument != NULL)
		{
			for (auto arg : preprocNode->mArgument->mChildArr)
			{
				Visit((BfAstNode*)arg);
				if (mFoundNode == arg)
				{
					mFoundNode = preprocNode;
				}
			}
		}
	}
};

static BfAstNode* FindDebugExpressionNode(BfAstNode* checkNode, int cursorIdx)
{
	BfInnermostFinder innermostFinder(cursorIdx);
	innermostFinder.VisitChild(checkNode);
	BfAstNode* exprNode = innermostFinder.mFoundNode;
	return exprNode;
}

//////////////////////////////////////////////////////////////////////////

BF_EXPORT void BF_CALLTYPE BfParser_SetSource(BfParser* bfParser, const char* data, int length, const char* fileName, int textVersion)
{
	bfParser->mFileName = fileName;
	bfParser->mTextVersion = textVersion;
	bfParser->SetSource(data, length);
}

BF_EXPORT void BF_CALLTYPE BfParser_SetCharIdData(BfParser* bfParser, uint8* data, int length)
{
	delete bfParser->mParserData->mCharIdData;
	bfParser->mParserData->mCharIdData = new uint8[length];
	memcpy(bfParser->mParserData->mCharIdData, data, length);
}

BF_EXPORT void BF_CALLTYPE BfParser_SetHashMD5(BfParser* bfParser, Val128* md5Hash)
{
	if (md5Hash != NULL)
		bfParser->mParserData->mMD5Hash = *md5Hash;
}

BF_EXPORT void BF_CALLTYPE BfParser_Delete(BfParser* bfParser)
{
	if (bfParser->mNextRevision != NULL)
		bfParser->mNextRevision->mPrevRevision = NULL;
	auto itr = std::find(bfParser->mSystem->mParsers.begin(), bfParser->mSystem->mParsers.end(), bfParser);
	bfParser->mSystem->mParsers.erase(itr);
	delete bfParser;
}

BF_EXPORT void BF_CALLTYPE BfParser_SetNextRevision(BfParser* bfParser, BfParser* nextRevision)
{
	BF_ASSERT(bfParser->mNextRevision == NULL);
	BF_ASSERT(nextRevision->mPrevRevision == NULL);
	bfParser->mNextRevision = nextRevision;
	nextRevision->mPrevRevision = bfParser;

	nextRevision->mDataId = bfParser->mDataId;
}

BF_EXPORT void BF_CALLTYPE BfParser_SetCursorIdx(BfParser* bfParser, int cursorIdx)
{
	bfParser->SetCursorIdx(cursorIdx);
}

BF_EXPORT void BF_CALLTYPE BfParser_SetIsClassifying(BfParser* bfParser)
{
	bfParser->mParserFlags = (BfParserFlag)(bfParser->mParserFlags | ParserFlag_Classifying);
}

BF_EXPORT void BF_CALLTYPE BfParser_SetEmbedKind(BfParser* bfParser, BfSourceEmbedKind embedKind)
{
	bfParser->mEmbedKind = embedKind;
}

BF_EXPORT void BF_CALLTYPE BfParser_SetAutocomplete(BfParser* bfParser, int cursorIdx)
{
	BF_ASSERT(bfParser->mParserData->mRefCount == -1);
	bfParser->SetCursorIdx(cursorIdx);
	bfParser->mParserFlags = (BfParserFlag)(bfParser->mParserFlags | ParserFlag_Autocomplete | ParserFlag_Classifying);
}

PerfManager* BfGetPerfManager(BfParser* bfParser)
{
	if (bfParser == NULL)
		return NULL;
	if (bfParser->mCursorIdx != -1)
		return gPerfManager;
	return NULL;
}

BF_EXPORT bool BF_CALLTYPE BfParser_Parse(BfParser* bfParser, BfPassInstance* bfPassInstance, bool compatMode)
{
	BP_ZONE("BfParser_Parse");
	int startFailIdx = bfPassInstance->mFailedIdx;
	bfParser->mCompatMode = compatMode;
	bfParser->mQuickCompatMode = compatMode;
	bfParser->Parse(bfPassInstance);
	return startFailIdx == bfPassInstance->mFailedIdx;
}

BF_EXPORT bool BF_CALLTYPE BfParser_Reduce(BfParser* bfParser, BfPassInstance* bfPassInstance)
{
	BP_ZONE("BfParser_Reduce");
	if (bfParser->mUsingCache)
		return true; // Already reduced

	bfParser->FinishSideNodes();
	int startFailIdx = bfPassInstance->mFailedIdx;
	int startWarningCount = bfPassInstance->mWarningCount;
	BfReducer bfReducer;
	bfReducer.mSource = bfParser;
	bfReducer.mCompatMode = bfParser->mCompatMode;
	bfReducer.mPassInstance = bfPassInstance;
	bfReducer.HandleRoot(bfParser->mRootNode);
	if ((startFailIdx != bfPassInstance->mFailedIdx) ||
		(startWarningCount != bfPassInstance->mWarningCount))
		bfParser->mParserData->mFailed = true;
	bfParser->mParserData->mDidReduce = true;
	bfParser->Close();

	return startFailIdx == bfPassInstance->mFailedIdx;
}

static Array<int> gCharMapping;
BF_EXPORT const char* BF_CALLTYPE BfParser_Format(BfParser* bfParser, int formatStart, int formatEnd, int** outCharMapping, int maxCol, int tabSize, bool wantsTabsAsSpaces,
	bool indentCaseLabels)
{
	BP_ZONE("BfParser_Reduce");
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	gCharMapping.Clear();
	BfPrinter bfPrinter(bfParser->mRootNode, bfParser->mSidechannelRootNode, bfParser->mErrorRootNode);
	bfPrinter.mMaxCol = maxCol;
	bfPrinter.mTabSize = tabSize;
	bfPrinter.mWantsTabsAsSpaces = wantsTabsAsSpaces;
	bfPrinter.mIndentCaseLabels = indentCaseLabels;
	bfPrinter.mFormatStart = formatStart;
	bfPrinter.mFormatEnd = formatEnd;
	bfPrinter.mCharMapping = &gCharMapping;
	bfPrinter.Visit(bfParser->mRootNode);
	outString = bfPrinter.mOutString;
	*outCharMapping = &gCharMapping[0];
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfParser_DocPrep(BfParser* bfParser)
{
	BP_ZONE("BfParser_Reduce");
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	gCharMapping.Clear();
	BfPrinter bfPrinter(bfParser->mRootNode, bfParser->mSidechannelRootNode, bfParser->mErrorRootNode);
	bfPrinter.mFormatStart = -1;
	bfPrinter.mFormatEnd = -1;
	bfPrinter.mCharMapping = &gCharMapping;
	bfPrinter.mDocPrep = true;
	bfPrinter.Visit(bfParser->mRootNode);
	outString = bfPrinter.mOutString;
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BfParser_GetDebugExpressionAt(BfParser* bfParser, int cursorIdx)
{
	BP_ZONE("BfParser_Reduce");
	String& outString = *gTLStrReturn.Get();
	outString.clear();

	BfAstNode* exprNode = FindDebugExpressionNode(bfParser->mRootNode, cursorIdx);
	if (exprNode == NULL)
		exprNode = FindDebugExpressionNode(bfParser->mSidechannelRootNode, cursorIdx);
	if (exprNode == NULL)
		return NULL;

	if ((exprNode->IsA<BfMethodDeclaration>()) ||
		(exprNode->IsA<BfBlock>()) ||
		(exprNode->IsA<BfStatement>()) ||
		(exprNode->IsA<BfTokenNode>())
		)
	{
		return NULL;
	}

	BfPrinter bfPrinter(bfParser->mRootNode, NULL, NULL);
	bfPrinter.mReformatting = true;
	bfPrinter.mIgnoreTrivia = true;
	bfPrinter.VisitChild(exprNode);
	outString = bfPrinter.mOutString;

	if (auto preprocessorNode = BfNodeDynCast<BfPreprocessorNode>(exprNode))
	{
		auto firstStr = preprocessorNode->mArgument->mChildArr[0]->ToString();

		if (firstStr == "warning")
		{
			String warningNumStr = preprocessorNode->mArgument->mChildArr.back()->ToString();
			int warningNum = atoi(warningNumStr.c_str());
			String warningStr;
			switch (warningNum)
			{
			case BfWarning_CS0108_MemberHidesInherited:
				warningStr = "CS0108: Derived member hides inherited member";
				break;
			case BfWarning_CS0114_MethodHidesInherited:
				warningStr = "CS0114: Derived method hides inherited member";
				break;
			case BfWarning_CS0162_UnreachableCode:
				warningStr = "CS0162: Unreachable code";
				break;
			case BfWarning_CS0168_VariableDeclaredButNeverUsed:
				warningStr = "CS0168: Variable declared but never used";
				break;
			case BfWarning_CS0472_ValueTypeNullCompare:
				warningStr = "CS0472: ValueType compared to null";
				break;
			case BfWarning_CS1030_PragmaWarning:
				warningStr = "CS1030: Pragma warning";
				break;
			}
			if (!warningStr.empty())
				outString = "`" + warningStr;
		}
	}

	return outString.c_str();
}

BF_EXPORT BfResolvePassData* BF_CALLTYPE BfParser_CreateResolvePassData(BfParser* bfParser, BfResolveType resolveType, bool doFuzzyAutoComplete)
{
	auto bfResolvePassData = new BfResolvePassData();
	bfResolvePassData->mResolveType = resolveType;
	if (bfParser != NULL)
		bfResolvePassData->mParsers.Add(bfParser);
	if ((bfParser != NULL) && ((bfParser->mParserFlags & ParserFlag_Autocomplete) != 0))
		bfResolvePassData->mAutoComplete = new BfAutoComplete(resolveType, doFuzzyAutoComplete);
	return bfResolvePassData;
}

BF_EXPORT bool BF_CALLTYPE BfParser_BuildDefs(BfParser* bfParser, BfPassInstance* bfPassInstance, BfResolvePassData* resolvePassData, bool fullRefresh)
{
	if (bfParser->mCursorIdx != -1)
		resolvePassData->mHasCursorIdx = true;

	BP_ZONE("BfParser_BuildDefs");
	int startFailIdx = bfPassInstance->mFailedIdx;
	BfDefBuilder defBuilder(bfParser->mSystem);
	defBuilder.mResolvePassData = resolvePassData;
	defBuilder.Process(bfPassInstance, bfParser, fullRefresh);
	return startFailIdx == bfPassInstance->mFailedIdx;;
}

BF_EXPORT void BF_CALLTYPE BfParser_RemoveDefs(BfParser* bfParser)
{
}

BF_EXPORT void BF_CALLTYPE BfParser_ClassifySource(BfParser* bfParser, BfSourceClassifier::CharData* charData, bool preserveFlags)
{
	if (!bfParser->mIsClosed)
		bfParser->Close();

	BfSourceClassifier bfSourceClassifier(bfParser, charData);
	bfSourceClassifier.mPreserveFlags = preserveFlags;
	bfSourceClassifier.Visit(bfParser->mRootNode);
	bfSourceClassifier.mIsSideChannel = false; //? false or true?
	bfSourceClassifier.Visit(bfParser->mErrorRootNode);
	bfSourceClassifier.mIsSideChannel = true;
	bfSourceClassifier.Visit(bfParser->mSidechannelRootNode);
}

BF_EXPORT void BF_CALLTYPE BfParser_CreateClassifier(BfParser* bfParser, BfPassInstance* bfPassInstance, BfResolvePassData* resolvePassData, BfSourceClassifier::CharData* charData)
{
	resolvePassData->mIsClassifying = true;
	bfParser->mSourceClassifier = new BfSourceClassifier(bfParser, charData);
	bfParser->mSourceClassifier->mClassifierPassId = bfPassInstance->mClassifierPassId;

	if ((resolvePassData->mParsers.IsEmpty()) || (bfParser != resolvePassData->mParsers[0]))
		resolvePassData->mParsers.Add(bfParser);

	bool doClassifyPass = (charData != NULL) && (resolvePassData->mResolveType <= BfResolveType_Autocomplete_HighPri);
	bfParser->mSourceClassifier->mEnabled = doClassifyPass;

	bfParser->mSourceClassifier->mSkipMethodInternals = true;
	bfParser->mSourceClassifier->mSkipTypeDeclarations = true;
	if (charData != NULL)
	{
		if ((doClassifyPass) && (bfParser->mRootNode != NULL))
			bfParser->mSourceClassifier->Visit(bfParser->mRootNode);
	}
	bfParser->mSourceClassifier->mSkipTypeDeclarations = false;
	bfParser->mSourceClassifier->mSkipMethodInternals = false;
}

BF_EXPORT void BF_CALLTYPE BfParser_FinishClassifier(BfParser* bfParser, BfResolvePassData* resolvePassData)
{
	if (bfParser->mSourceClassifier == NULL)
		return;

	bool doClassifyPass = (bfParser->mSourceClassifier->mCharData != NULL) && (resolvePassData->mResolveType <= BfResolveType_Autocomplete_HighPri);

	if (doClassifyPass)
	{
		bfParser->mSourceClassifier->mIsSideChannel = false;
		if (bfParser->mErrorRootNode != NULL)
			bfParser->mSourceClassifier->Visit(bfParser->mErrorRootNode);

		bfParser->mSourceClassifier->mIsSideChannel = true;
		if (bfParser->mSidechannelRootNode != NULL)
			bfParser->mSourceClassifier->Visit(bfParser->mSidechannelRootNode);
	}

	delete bfParser->mSourceClassifier;
	bfParser->mSourceClassifier = NULL;
}

BF_EXPORT void BF_CALLTYPE BfParser_GenerateAutoCompletionFrom(BfParser* bfParser, int srcPosition)
{
	BP_ZONE("BfParser_GenerateAutoCompletionFrom");
	bfParser->GenerateAutoCompleteFrom(srcPosition);
}

BF_EXPORT void BF_CALLTYPE BfParser_SetCompleteParse(BfParser* bfParser)
{
	bfParser->mCompleteParse = true;
}

BF_EXPORT void BF_CALLTYPE BfParser_GetLineCharAtIdx(BfParser* bfParser, int idx, int* line, int* lineChar)
{
	int _line, _lineChar;

	bfParser->GetLineCharAtIdx(idx, _line, _lineChar);

	*line = _line;
	*lineChar = _lineChar;
}

BF_EXPORT int BF_CALLTYPE BfParser_GetIndexAtLine(BfParser* bfParser, int line)
{
	return bfParser->GetIndexAtLine(line);
}
