#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BfSystem.h"
#include "BfElementVisitor.h"

NS_BF_BEGIN

enum BfSourceElementType
{
	BfSourceElementType_Normal,
	BfSourceElementType_Keyword,
	BfSourceElementType_Literal,
	BfSourceElementType_Identifier,	
	BfSourceElementType_Comment,
	BfSourceElementType_Method,
	BfSourceElementType_Type,
	BfSourceElementType_PrimitiveType,
	BfSourceElementType_Struct,
	BfSourceElementType_GenericParam,
	BfSourceElementType_RefType,
	BfSourceElementType_Interface,
	BfSourceElementType_Namespace
};

enum BfSourceElementFlags
{
	BfSourceElementFlag_Error = 1,
	BfSourceElementFlag_Warning = 2,
	BfSourceElementFlag_IsAfter = 4,
	BfSourceElementFlag_Skipped = 8,
	BfSourceElementFlag_CompilerFlags_Mask = 0x0F,
	BfSourceElementFlag_MASK = 0xFF
};

enum BfSourceDisplayId : uint8
{
	BfSourceDisplayId_Cleared,
	BfSourceDisplayId_AutoComplete,
	BfSourceDisplayId_SpellCheck,
	BfSourceDisplayId_FullClassify,
	BfSourceDisplayId_SkipResult
};

class BfSourceClassifier : public BfElementVisitor
{
public:
	struct CharData
	{
		char mChar;				
		uint8 mDisplayPassId;
		uint8 mDisplayTypeId;
		uint8 mDisplayFlags;
#ifdef INCLUDE_CHARDATA_CHARID
		uint32 mCharId; // Unique ID for each character, for tracking moving text
#endif
	};

public:
	BfParser* mParser;	
	CharData* mCharData;	
	bool mEnabled;
	bool mSkipMethodInternals;
	bool mSkipTypeDeclarations;
	bool mSkipAttributes;
	bool mIsSideChannel;
	bool mPreserveFlags;
	uint8 mClassifierPassId;
	BfAstNode* mPrevNode;
	BfAstNode* mCurMember;
	BfLocalMethodDeclaration* mCurLocalMethodDeclaration;
	Array<BfAstNode*> mDeferredNodes;

public:
	void HandleLeafNode(BfAstNode* node);
	void VisitMembers(BfBlock* node);
	void ModifyFlags(BfAstNode* node, uint8 andFlags, uint8 orFlags);
	void ModifyFlags(int startPos, int endPos, uint8 andFlags, uint8 orFlags);
	void SetElementType(BfAstNode* node, BfSourceElementType elementType);
	void SetElementType(BfAstNode* node, BfTypeCode typeCode);
	void SetElementType(int startPos, int endPos, BfSourceElementType elementType);
	void SetHighestElementType(BfAstNode* node, BfSourceElementType elementType);
	void SetHighestElementType(int startPos, int endPos, BfSourceElementType elementType);
	bool IsInterestedInMember(BfAstNode* node, bool forceSkip = false);
	bool WantsSkipParentMethod(BfAstNode* node);
	void Handle(BfTypeDeclaration* typeDeclaration);
	void MarkSkipped(int startPos, int endPos);
	void MarkSkipped(BfAstNode* node);
	void DeferNodes(BfBlock* block);
	void FlushDeferredNodes();

public:
	BfSourceClassifier(BfParser* bfParser, CharData* charData);

	virtual void Visit(BfGenericConstraintsDeclaration* genericConstraints) override;

	virtual void Visit(BfAstNode* node) override;
	virtual void Visit(BfErrorNode* errorNode) override;		
	virtual void Visit(BfFieldDeclaration* fieldDecl) override;
	virtual void Visit(BfFieldDtorDeclaration* fieldDtorDecl) override;
	virtual void Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection) override;
	virtual void Visit(BfPreprocessorNode* preprocessorNode) override;
	virtual void Visit(BfCommentNode* commentNode) override;
	virtual void Visit(BfAttributeDirective* attributeDirective) override;
	virtual void Visit(BfIdentifierNode* identifier) override;
	virtual void Visit(BfQualifiedNameNode* identifier) override;	
	virtual void Visit(BfThisExpression* thisExpr) override;
	virtual void Visit(BfBaseExpression* baseExpr) override;
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override;	
	virtual void Visit(BfQualifiedTypeReference* qualifiedType) override;
	virtual void Visit(BfRefTypeRef* typeRef) override;
	virtual void Visit(BfArrayTypeRef* arrayType) override;
	virtual void Visit(BfPointerTypeRef* pointerType) override;
	virtual void Visit(BfNamedTypeReference* typeRef) override;
	virtual void Visit(BfGenericInstanceTypeRef* typeRef) override;
	virtual void Visit(BfLocalMethodDeclaration * methodDecl) override;
	virtual void Visit(BfLiteralExpression* literalExpr) override;
	virtual void Visit(BfStringInterpolationExpression* stringInterpolationExpression) override;
	virtual void Visit(BfTokenNode* tokenNode) override;
	virtual void Visit(BfInvocationExpression* invocationExpr) override;	
	virtual void Visit(BfIndexerExpression* indexerExpr) override;
	virtual void Visit(BfConstructorDeclaration* ctorDeclaration) override;	
	virtual void Visit(BfDestructorDeclaration* dtorDeclaration) override;	
	virtual void Visit(BfMethodDeclaration* methodDeclaration) override;	
	virtual void Visit(BfPropertyMethodDeclaration* propertyMethodDeclaration) override;
	virtual void Visit(BfPropertyDeclaration* propertyDeclaration) override;
	virtual void Visit(BfTypeDeclaration* typeDeclaration) override;
	virtual void Visit(BfTypeAliasDeclaration* typeDeclaration) override;
	virtual void Visit(BfUsingDirective* usingDirective) override;
	virtual void Visit(BfUsingModDirective* usingDirective) override;
	virtual void Visit(BfNamespaceDeclaration* namespaceDeclaration) override;
	virtual void Visit(BfBlock* block) override;
	virtual void Visit(BfRootNode* rootNode) override;
	virtual void Visit(BfInlineAsmStatement* asmStmt) override;
	virtual void Visit(BfInlineAsmInstruction* asmInst) override;
};

NS_BF_END