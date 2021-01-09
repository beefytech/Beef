#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/HashSet.h"
#include "BfUtil.h"
#include "BfSource.h"
#include "MemReporter.h"
#include <set>

NS_BF_BEGIN

class BfPassInstance;
class BfProject;

enum BfSyntaxToken
{
	BfSyntaxToken_None,
	BfSyntaxToken_Token,
	BfSyntaxToken_Identifier,	
	BfSyntaxToken_CharQuote,
	BfSyntaxToken_StringQuote,	
	BfSyntaxToken_ForwardSlash,
	BfSyntaxToken_Literal,	
	BfSyntaxToken_CommentLine,
	BfSyntaxToken_CommentBlock,
	BfSyntaxToken_GeneratedNode,	
	BfSyntaxToken_FAILED,	
	BfSyntaxToken_HIT_END_IDX,
	BfSyntaxToken_EOF
};

struct BfLineStartEntry
{
	int mCharIdx;
	int mLineNum;
};

#define PARSER_JUMPTABLE_DIVIDE 64

enum BfParserFlag
{
	ParserFlag_None,
	ParserFlag_Autocomplete = 1,
	ParserFlag_GetDefinition = 2,
	ParserFlag_Classifying = 4,
};

struct BfParserWarningEnabledChange
{
	int mWarningNumber;
	bool mEnable;
};

enum MaybeBool
{
	MaybeBool_None = -1,
	MaybeBool_False = 0,
	MaybeBool_True = 1,	
};

class BfParserData : public BfSourceData
{
public:	
	uint64 mHash;
	int mDataId;
	int mRefCount; // -1 = not cached
	BfParser* mUniqueParser; // For non-cached usage (ie: autocomplete)
	String mFileName;
	uint8* mCharIdData;
	Val128 mMD5Hash;

	HashSet<String> mDefines_Def;
	HashSet<String> mDefines_NoDef;

	BfLineStartEntry* mJumpTable;
	int mJumpTableSize;
	OwnedVector<String> mStringLiterals;
	Dictionary<int, BfParserWarningEnabledChange> mWarningEnabledChanges;
	std::set<int> mUnwarns;
	bool mFailed; // Don't cache if there's a warning or an error
	bool mDidReduce;			

public:
	BfParserData();
	~BfParserData();
	void Deref();

	virtual BfParserData* ToParserData() override
	{
		return this;
	}

	virtual BfParser* ToParser() override;
	int GetCharIdAtIndex(int findIndex);	
	void GetLineCharAtIdx(int idx, int& line, int& lineChar);
	bool IsUnwarnedAt(BfAstNode* node);
	bool IsWarningEnabledAtSrcIndex(int warningNumber, int srcIdx);
	void ReportMemory(MemReporter* memReporter);	
};

class BfParserCache
{
public:
	struct LookupEntry
	{
		uint64 mHash;
		String mFileName;
		const char* mSrc;
		int mSrcLength;
		BfProject* mProject;
	};

	struct DataEntry
	{		
		BfParserData* mParserData;

		bool operator==(const LookupEntry& lookup) const;
		bool operator==(const DataEntry& lookup) const
		{
			return lookup.mParserData == mParserData;
		}
	};		

public:
	CritSect mCritSect;
	int mRefCount;
	BfAstAllocManager mAstAllocManager;
	HashSet<DataEntry> mEntries;

public:	
	BfParserCache();
	~BfParserCache();
	void ReportMemory(MemReporter* memReporter);
};

enum BfDefineState
{
	BfDefineState_FromProject,
	BfDefineState_ManualSet,
	BfDefineState_ManualUnset
};

class BfParser : public BfSource
{
public:		
	BfParserData* mParserData;
	bool mUsingCache;

	BfPassInstance* mPassInstance;
	String mFileName;	
	bool mAwaitingDelete;	
	
	bool mCompatMode; // Does C++ compatible parsing
	bool mQuickCompatMode;	
	bool mScanOnly;
	bool mCompleteParse;
	bool mIsEmitted;
	BfLineStartEntry* mJumpTable;
	int mJumpTableSize;
	int mOrigSrcLength;	
	int mDataId;

	int mSrcIdx;
	int mLineStart;
	int mLineNum;	
	bool mInAsmBlock;

	BfParserFlag mParserFlags;
	int mCursorIdx;
	int mCursorCheckIdx;

	BfSyntaxToken mSyntaxToken;
	int mTriviaStart; // mTriviaStart < mTokenStart when there's leading whitespace
	int mTokenStart;
	int mTokenEnd;	
	BfAstNode* mGeneratedNode;
	BfVariant mLiteral;	
	BfToken mToken;
	BfPreprocesorIgnoredSectionNode* mPreprocessorIgnoredSectionNode;
	int mPreprocessorIgnoreDepth;
	Array< std::pair<BfAstNode*, bool> > mPreprocessorNodeStack;

	Dictionary<String, BfDefineState> mPreprocessorDefines;
	
	std::set<int> mPreprocessorIgnoredSectionStarts;			

public:
	virtual void HandleInclude(BfAstNode* paramNode);
	virtual void HandleIncludeNext(BfAstNode* paramNode);
	virtual void HandlePragma(const StringImpl& pragma, BfBlock* block);
	virtual void HandleDefine(const StringImpl& name, BfAstNode* paramNode);
	virtual void HandleUndefine(const StringImpl& name);	
	virtual MaybeBool HandleIfDef(const StringImpl& name);
	virtual MaybeBool HandleProcessorCondition(BfBlock* paramNode);

public:		
	void Init(uint64 cacheHash = 0);
	void NewLine();	
	BfExpression* CreateInlineExpressionFromNode(BfBlock* block);
	bool EvaluatePreprocessor(BfExpression* expr);
	BfBlock* ParseInlineBlock(int spaceIdx, int endIdx);
	bool HandlePreprocessor();	
	bool IsUnwarnedAt(BfAstNode* node);
	bool SrcPtrHasToken(const char* name);
	uint32 GetTokenHash();
	void ParseBlock(BfBlock* astNode, int depth, bool isInterpolate = false);
	double ParseLiteralDouble();	
	void AddErrorNode(int startIdx, int endIdx);
	BfCommentKind GetCommentKind(int startIdx);	

public:	
	BfParser(BfSystem* bfSystem, BfProject* bfProject = NULL);
	~BfParser();

	void SetCursorIdx(int cursorIdx);
	virtual BfParser* ToParser() override { return this; }

	void GetLineCharAtIdx(int idx, int& line, int& lineChar);

	void Fail(const StringImpl& error, int offset = -1);
	void TokenFail(const StringImpl& error, int offset = -1);	
	
	void SetSource(const char* data, int length);	
	void MoveSource(const char* data, int length); // Takes ownership of data ptr
	void RefSource(const char* data, int length);
	void NextToken(int endIdx = -1, bool outerIsInterpolate = false);
	BfAstNode* CreateNode();	
		
	void Parse(BfPassInstance* passInstance);		
	int GetCharIdAtIndex(int findIndex);
	virtual void Close() override;
	virtual void HadSrcRealloc() override;

	void GenerateAutoCompleteFrom(int srcPosition);	
		
	void ReportMemory(MemReporter* memReporter);	
	void GetSrcPosition(int idx, int& lineNum, int& column);
};

extern BfParserCache* gBfParserCache;

NS_BF_END

namespace std
{
	template<>
	struct hash<Beefy::BfParserCache::LookupEntry>
	{
		size_t operator()(const Beefy::BfParserCache::LookupEntry& val) const
		{
			return (size_t)val.mHash;
		}
	};

	template<>
	struct hash<Beefy::BfParserCache::DataEntry>
	{
		size_t operator()(const Beefy::BfParserCache::DataEntry& val) const
		{
			return (size_t)val.mParserData->mHash;
		}
	};
}