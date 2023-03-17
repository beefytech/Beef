#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BfType.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_BEGIN

class BfPassInstance;
class BfResolvePassData;

class BfReducer
{
public:
	enum CreateExprFlags
	{
		CreateExprFlags_None,
		CreateExprFlags_NoCaseExpr = 1,
		CreateExprFlags_NoAssignment = 2,
		CreateExprFlags_AllowVariableDecl = 4,
		CreateExprFlags_PermissiveVariableDecl = 8,
		CreateExprFlags_NoCast = 0x10,
		CreateExprFlags_BreakOnRChevron = 0x20,
		CreateExprFlags_ExitOnBang = 0x40,
		CreateExprFlags_ExitOnParenExpr = 0x80,
		CreateExprFlags_NoCheckBinOpPrecedence = 0x100,
		CreateExprFlags_BreakOnCascade = 0x200,
		CreateExprFlags_EarlyExit = 0x400, // Don't attempt binary or ternary operations
		CreateExprFlags_AllowEmpty = 0x800,
		CreateExprFlags_AllowAnonymousIndexer = 0x1000
	};

	enum CreateStmtFlags
	{
		CreateStmtFlags_None,
		CreateStmtFlags_NoCaseExpr = 1,
		CreateStmtFlags_FindTrailingSemicolon = 2,
		CreateStmtFlags_AllowUnterminatedExpression = 4,
		CreateStmtFlags_AllowLocalFunction = 8,
		CreateStmtFlags_ForceVariableDecl = 0x10,

		CreateStmtFlags_To_CreateExprFlags_Mask = 1
	};

	enum CreateTypeRefFlags
	{
		CreateTypeRefFlags_None,
		CreateTypeRefFlags_NoParseArrayBrackets = 1,
		CreateTypeRefFlags_SafeGenericParse = 2,
		CreateTypeRefFlags_AllowSingleMemberTuple = 4,
		CreateTypeRefFlags_EarlyExit = 8
	};

	struct BfVisitorPos
	{
		BfBlock* mParent;
		int mReadPos;
		int mWritePos;
		int mTotalSize;

		BfVisitorPos(BfBlock* parent = NULL)
		{
			mParent = parent;
			mReadPos = -1;
			mWritePos = 0;
			if (parent != NULL)
				mTotalSize = parent->GetSize();
			else
				mTotalSize = 0;
		}

		bool MoveNext()
		{
			mReadPos++;
			return mReadPos < mTotalSize;
		}

		BfAstNode* GetCurrent()
		{
			if ((uint)mReadPos >= (uint)mTotalSize)
				return NULL;
			return (*mParent)[mReadPos];
		}

		BfAstNode* Get(int idx)
		{
			if (((uint)idx >= (uint)mTotalSize))
				return NULL;
			return (*mParent)[idx];
		}

		void Set(int idx, BfAstNode* node)
		{
			if (((uint)idx >= (uint)mTotalSize))
				return;
			(*mParent)[idx] = node;
		}

		void ReplaceCurrent(BfAstNode* node)
		{
			if ((uint)mReadPos >= (uint)mTotalSize)
				return;
			(*mParent)[mReadPos] = node;
		}

		void Write(BfAstNode* node)
		{
			(*mParent)[mWritePos++] = node;
			BF_ASSERT(mWritePos <= mReadPos);
		}

		BfAstNode* GetNext()
		{
			if ((uint)(mReadPos + 1) >= (uint)(mTotalSize))
				return NULL;
			return (*mParent)[mReadPos + 1];
		}

		int GetReadNodesLeft()
		{
			return std::max(0, mTotalSize - mReadPos);
		}

		void Trim()
		{
			mParent->SetSize(mWritePos);
		}
	};

public:
	BfAstAllocator* mAlloc;
	BfSystem* mSystem;
	BfSource* mSource;
	BfPassInstance* mPassInstance;
	BfResolvePassData* mResolvePassData;
	BfAstNode* mTypeMemberNodeStart;
	int mClassDepth;
	int mMethodDepth;
	BfTypeDeclaration* mCurTypeDecl;
	BfTypeDeclaration* mLastTypeDecl;
	BfMethodDeclaration* mCurMethodDecl;
	BfAstNode* mLastBlockNode;
	bool mStmtHasError;
	bool mPrevStmtHadError;
	bool mCompatMode; // Does C++ compatible parsing
	bool mAllowTypeWildcard;
	bool mIsFieldInitializer;
	bool mInParenExpr;
	bool mSkipCurrentNodeAssert;
	BfVisitorPos mVisitorPos;
	int mDocumentCheckIdx;
	SizedArray<BfNamespaceDeclaration*, 4> mCurNamespaceStack;
	SizedArray<BfExteriorNode, 4> mExteriorNodes;

	int mAssertCurrentNodeIdx;

public:
	BfAstNode* Fail(const StringImpl& errorMsg, BfAstNode* refNode);
	BfAstNode* FailAfter(const StringImpl& errorMsg, BfAstNode* refNode);
	void AddErrorNode(BfAstNode* astNode, bool removeNode = true);

public:
	bool StringEquals(BfAstNode* node, BfAstNode* node2);
	bool IsSemicolon(BfAstNode* node);
	BfTokenNode* ExpectTokenAfter(BfAstNode* node, BfToken token);
	BfTokenNode* ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB);
	BfTokenNode* ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB, BfToken tokenC);
	BfTokenNode* ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB, BfToken tokenC, BfToken tokenD);
	BfIdentifierNode* ExpectIdentifierAfter(BfAstNode* node, const char* typeName = NULL);
	BfBlock* ExpectBlockAfter(BfAstNode* node);
	BfTokenNode* BreakDoubleChevron(BfTokenNode* tokenNode);
	BfTokenNode* BreakQuestionLBracket(BfTokenNode* tokenNode);
	BfCommentNode* FindDocumentation(BfAstNode* defNodeHead, BfAstNode* defNodeEnd = NULL, bool checkDocAfter = false);

	void AssertCurrentNode(BfAstNode* node);
	bool IsNodeRelevant(BfAstNode* astNode);
	bool IsNodeRelevant(BfAstNode* startNode, BfAstNode* endNode);
	void MoveNode(BfAstNode* srcNode, BfAstNode* newOwner);
	void ReplaceNode(BfAstNode* prevNode, BfAstNode* newNode);

	bool SetProtection(BfAstNode* parentNode, BfAstNode*& protectionNodeRef, BfTokenNode* tokenNode);
	BfAstNode* CreateAllocNode(BfTokenNode* newNode);
	BfAstNode* ReplaceTokenStarter(BfAstNode* astNode, int idx = -1, bool allowIn = false);
	BfEnumCaseBindExpression* CreateEnumCaseBindExpression(BfTokenNode* bindToken);
	BfExpression* CheckBinaryOperatorPrecedence(BfBinaryOperatorExpression* binOpExpression);
	BfExpression* ApplyToFirstExpression(BfUnaryOperatorExpression* unaryOp, BfExpression* target);
	BfIdentifierNode* ExtractExplicitInterfaceRef(BfAstNode* memberDeclaration, BfIdentifierNode* nameIdentifier, BfTypeReference** outExplicitInterface, BfTokenNode** outExplicitInterfaceDotToken);
	BfTokenNode* ParseMethodParams(BfAstNode* node, SizedArrayImpl<BfParameterDeclaration*>* params, SizedArrayImpl<BfTokenNode*>* commas, BfToken endToken, bool requireNames);
	BfTokenNode* ReadArguments(BfAstNode* parentNode, BfAstNode* afterNode, SizedArrayImpl<BfExpression*>* arguments, SizedArrayImpl<BfTokenNode*>* commas, BfToken endToken, bool allowSkippedArgs = false, CreateExprFlags createExprFlags = CreateExprFlags_None);
	void ReadPropertyBlock(BfPropertyDeclaration* propertyDeclaration, BfBlock* block);
	BfAstNode* ReadTypeMember(BfTokenNode* node, bool declStarted = false, int depth = 0, BfAstNode* deferredHeadNode = NULL);
	BfAstNode* ReadTypeMember(BfAstNode* node, bool declStarted = false, int depth = 0, BfAstNode* deferredHeadNode = NULL);
	BfIdentifierNode* CompactQualifiedName(BfAstNode* leftNode);
	void TryIdentifierConvert(int readPos);
	void CreateQualifiedNames(BfAstNode* node);
	BfFieldDtorDeclaration* CreateFieldDtorDeclaration(BfAstNode* srcNode);
	BfFieldDeclaration* CreateFieldDeclaration(BfTokenNode* tokenNode, BfTypeReference* typeRef, BfIdentifierNode* nameIdentifier, BfFieldDeclaration* prevFieldDeclaration);
	BfAttributeDirective* CreateAttributeDirective(BfTokenNode* startToken);
	BfStatement* CreateAttributedStatement(BfTokenNode* tokenNode, CreateStmtFlags createStmtFlags = CreateStmtFlags_None);
	BfExpression* CreateAttributedExpression(BfTokenNode* tokenNode, bool onlyAllowIdentifier);
	BfDelegateBindExpression* CreateDelegateBindExpression(BfAstNode* allocNode);
	BfLambdaBindExpression* CreateLambdaBindExpression(BfAstNode* allocNode, BfTokenNode* parenToken = NULL);
	BfCollectionInitializerExpression* CreateCollectionInitializerExpression(BfBlock* block);
	BfCollectionInitializerExpression* CreateCollectionInitializerExpression(BfTokenNode* openToken);
	BfObjectCreateExpression* CreateObjectCreateExpression(BfAstNode* allocNode);
	BfScopedInvocationTarget* CreateScopedInvocationTarget(BfAstNode*& targetRef, BfTokenNode* colonToken);
	BfInvocationExpression* CreateInvocationExpression(BfAstNode* target, CreateExprFlags createExprFlags = CreateExprFlags_None);
	BfInitializerExpression* TryCreateInitializerExpression(BfAstNode* target);
	BfExpression* CreateIndexerExpression(BfExpression* target, BfTokenNode* openBracketNode = NULL);
	BfMemberReferenceExpression* CreateMemberReferenceExpression(BfAstNode* target);
	BfTupleExpression* CreateTupleExpression(BfTokenNode* newNode, BfExpression* innerExpr = NULL);
	BfExpression* CreateExpression(BfAstNode* node, CreateExprFlags createExprFlags = CreateExprFlags_None);
	BfExpression* CreateExpressionAfter(BfAstNode* node, CreateExprFlags createExprFlags = CreateExprFlags_None);
	BfSwitchStatement* CreateSwitchStatement(BfTokenNode* tokenNode);
	BfAstNode* DoCreateStatement(BfAstNode* node, CreateStmtFlags createStmtFlags = CreateStmtFlags_None);
	bool IsTerminatingExpression(BfAstNode * node);
	BfAstNode* CreateStatement(BfAstNode* node, CreateStmtFlags createStmtFlags = CreateStmtFlags_None);
	BfAstNode* CreateStatementAfter(BfAstNode* node, CreateStmtFlags createStmtFlags = CreateStmtFlags_None);
	bool IsExtendedTypeName(BfIdentifierNode* identifierNode);
	bool IsTypeReference(BfAstNode* checkNode, BfToken successToken, int endNode, int* retryNode, int* outEndNode, bool* couldBeExpr, bool* isGenericType, bool* isTuple);
	bool IsTypeReference(BfAstNode* checkNode, BfToken successToken, int endNode = -1, int* outEndNode = NULL, bool* couldBeExpr = NULL, bool* isGenericType = NULL, bool* isTuple = NULL);
	bool IsLocalMethod(BfAstNode* nameNode);
	int QualifiedBacktrack(BfAstNode* endNode, int checkIdx, bool* outHadChevrons = NULL); // Backtracks to dot token
	BfTypeReference* DoCreateNamedTypeRef(BfIdentifierNode* identifierNode);
	BfTypeReference* DoCreateTypeRef(BfAstNode* identifierNode, CreateTypeRefFlags createTypeRefFlags = CreateTypeRefFlags_None, int endNode = -1);
	BfTypeReference* CreateTypeRef(BfAstNode* identifierNode, CreateTypeRefFlags createTypeRefFlags = CreateTypeRefFlags_None);
	BfTypeReference* CreateTypeRefAfter(BfAstNode* astNode, CreateTypeRefFlags createTypeRefFlags = CreateTypeRefFlags_None);
	BfTypeReference* CreateRefTypeRef(BfTypeReference* elementType, BfTokenNode* refToken);
	bool ParseMethod(BfMethodDeclaration* methodDeclaration, SizedArrayImpl<BfParameterDeclaration*>* params, SizedArrayImpl<BfTokenNode*>* commas, bool alwaysIncludeBlock = false);
	BfGenericArgumentsNode* CreateGenericArguments(BfTokenNode* tokenNode, bool allowPartial = false);
	BfGenericParamsDeclaration* CreateGenericParamsDeclaration(BfTokenNode* tokenNode);
	BfGenericConstraintsDeclaration* CreateGenericConstraintsDeclaration(BfTokenNode* tokenNode);
	BfForEachStatement* CreateForEachStatement(BfAstNode* node, bool hasTypeDecl);
	BfStatement* CreateForStatement(BfAstNode* node);
	BfUsingStatement* CreateUsingStatement(BfAstNode* node);
	BfWhileStatement* CreateWhileStatement(BfAstNode* node);
	BfDoStatement* CreateDoStatement(BfAstNode* node);
	BfRepeatStatement* CreateRepeatStatement(BfAstNode* node);
	BfAstNode* CreateTopLevelObject(BfTokenNode* tokenNode, BfAttributeDirective* attributes, BfAstNode* deferredHeadNode = NULL);
	BfAstNode* HandleTopLevel(BfBlock* node);
	BfInlineAsmStatement* CreateInlineAsmStatement(BfAstNode* asmNode);

	void HandleBlock(BfBlock* block, bool allowEndingExpression = false);
	void HandleTypeDeclaration(BfTypeDeclaration* typeDecl, BfAttributeDirective* attributes, BfAstNode* deferredHeadNode = NULL);

public:
	BfReducer();
	void HandleRoot(BfRootNode* rootNode);
};

NS_BF_END
