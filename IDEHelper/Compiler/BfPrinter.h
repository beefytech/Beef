#include "BeefySysLib/Common.h"
//#include "llvm/Support/raw_ostream.h"
#include "BfAst.h"
#include "BfElementVisitor.h"

NS_BF_BEGIN


class BfPrinter : public BfElementVisitor
{
public:
	struct StateModify
	{
	public:
		bool mExpectingSpace;		
		int mWantNewLineIdx;
		bool mDoingBlockOpen;
		bool mDoingBlockClose;		
		int mWantVirtualIndent;
		BfAstNode* mQueuedNode;

	public:
		StateModify()
		{
			Clear();
			mWantVirtualIndent = 0;
			mWantNewLineIdx = 0;
		}

		void Clear()
		{
			mExpectingSpace = false;			
			mDoingBlockOpen = false;
			mDoingBlockClose = false;
			mQueuedNode = NULL;
		}
	};
	
	BfSourceData* mSource;
	BfParserData* mParser;

	BfBlock::Iterator mSidechannelItr;
	BfAstNode* mSidechannelNextNode;	

	BfBlock::Iterator mErrorItr;
	BfAstNode* mErrorNextNode;
	BfAstNode* mCurBlockMember;
	BfTypeDeclaration* mCurTypeDecl;

	int mTriviaIdx;
	int mCurSrcIdx;
	Array<StateModify> mChildNodeQueue;
	int mFormatStart;
	int mFormatEnd;	
	StateModify mNextStateModify;

	String mOutString;
	bool mReformatting;		
	bool mIgnoreTrivia;
	bool mDocPrep;
	int mCurIndentLevel;
	int mQueuedSpaceCount;
	int mLastSpaceOffset; // Indent difference from original to new	
	bool mExpectingNewLine;

	bool mIsFirstStatementInBlock;
	bool mForceUseTrivia;
	bool mInSideChannel;	

	int mStateModifyVirtualIndentLevel;
	int mVirtualIndentLevel;
	int mVirtualNewLineIdx;
			
	Array<int>* mCharMapping;
	int mHighestCharId;

public:	
	BfPrinter(BfRootNode* rootNode, BfRootNode* sidechannelRootNode, BfRootNode* errorRootNode);
		
public:	
	bool CheckReplace(BfAstNode* astNode);
	void FlushIndent();
	void Write(const StringView& str);
	void Write(BfAstNode* node, int start, int len);
	void WriteSourceString(BfAstNode* node);
	void QueueVisitChild(BfAstNode* astNode);
	void QueueVisitErrorNodes(BfRootNode* astNode);	
	void FlushVisitChild();
	void VisitChildWithPrecedingSpace(BfAstNode* bfAstNode);
	void VisitChildWithProceedingSpace(BfAstNode* bfAstNode);
	void ExpectSpace();
	void ExpectNewLine();
	void ExpectIndent();
	void ExpectUnindent();
	void VisitChildNextLine(BfAstNode* node);
	void DoBlockOpen(BfBlock* block, bool queue, bool* outDoInlineBlock);
	void DoBlockClose(BfBlock* block, bool queue, bool doInlineBlock);
	void QueueMethodDeclaration(BfMethodDeclaration* methodDeclaration);	
	int CalcOrigLineSpacing(BfAstNode* bfAstNode, int* lineStartIdx);
	void WriteIgnoredNode(BfAstNode* node);

	virtual void Visit(BfAstNode* bfAstNode) override;
	virtual void Visit(BfErrorNode* bfErrorNode) override;
	virtual void Visit(BfScopeNode * scopeNode) override;
	virtual void Visit(BfNewNode * newNode) override;
	virtual void Visit(BfExpression* expr) override;
	virtual void Visit(BfExpressionStatement* exprStmt) override;
	virtual void Visit(BfAttributedExpression* attribExpr) override;
	virtual void Visit(BfStatement* stmt) override;	
	virtual void Visit(BfLabelableStatement* labelableStmt) override;

	virtual void Visit(BfCommentNode* commentNode) override;
	virtual void Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection) override;
	virtual void Visit(BfPreprocessorNode* preprocessorNode) override;

	virtual void Visit(BfAttributeDirective* attributeDirective) override;	
	virtual void Visit(BfGenericParamsDeclaration* genericParams) override;
	virtual void Visit(BfGenericOperatorConstraint* genericConstraints) override;
	virtual void Visit(BfGenericConstraintsDeclaration* genericConstraints) override;
	virtual void Visit(BfGenericArgumentsNode* genericArgumentsNode) override;

	virtual void Visit(BfEmptyStatement* emptyStmt) override;	
	virtual void Visit(BfTokenNode* tokenNode) override;
	virtual void Visit(BfLiteralExpression* literalExpr) override;
	virtual void Visit(BfIdentifierNode* identifierNode) override;
	virtual void Visit(BfQualifiedNameNode* nameNode) override;
	virtual void Visit(BfThisExpression* thisExpr) override;
	virtual void Visit(BfBaseExpression* baseExpr) override;
	virtual void Visit(BfMixinExpression* mixinExpr) override;
	virtual void Visit(BfSizedArrayCreateExpression* createExpr) override;
	virtual void Visit(BfCollectionInitializerExpression* initExpr) override;	
	virtual void Visit(BfTypeReference* typeRef) override;
	virtual void Visit(BfNamedTypeReference* typeRef) override;
	virtual void Visit(BfQualifiedTypeReference* qualifiedType) override;
	virtual void Visit(BfVarTypeReference* typeRef) override;
	virtual void Visit(BfLetTypeReference * typeRef) override;
	virtual void Visit(BfConstTypeRef* typeRef) override;
	virtual void Visit(BfConstExprTypeRef* typeRef) override;
	virtual void Visit(BfRefTypeRef* typeRef) override;
	virtual void Visit(BfArrayTypeRef* typeRef) override;
	virtual void Visit(BfGenericInstanceTypeRef* typeRef) override;
	virtual void Visit(BfTupleTypeRef * typeRef) override;
	virtual void Visit(BfDelegateTypeRef* typeRef) override;
	virtual void Visit(BfPointerTypeRef* typeRef) override;
	virtual void Visit(BfNullableTypeRef* typeRef) override;
	virtual void Visit(BfVariableDeclaration* varDecl) override;
	virtual void Visit(BfParameterDeclaration* paramDecl) override;
	virtual void Visit(BfParamsExpression* paramsExpr) override;
	virtual void Visit(BfTypeOfExpression* typeOfExpr) override;
	virtual void Visit(BfSizeOfExpression* sizeOfExpr) override;
	virtual void Visit(BfDefaultExpression* defaultExpr) override;
	virtual void Visit(BfCheckTypeExpression* checkTypeExpr) override;
	virtual void Visit(BfDynamicCastExpression* dynCastExpr) override;
	virtual void Visit(BfCastExpression* castExpr) override;
	virtual void Visit(BfDelegateBindExpression* invocationExpr) override;	
	virtual void Visit(BfLambdaBindExpression* lambdaBindExpr) override;
	virtual void Visit(BfObjectCreateExpression* invocationExpr) override;
	virtual void Visit(BfBoxExpression* boxExpr) override;
	virtual void Visit(BfInvocationExpression* invocationExpr) override;	
	virtual void Visit(BfSwitchCase* switchCase) override;
	virtual void Visit(BfWhenExpression* whenExpr) override;
	virtual void Visit(BfSwitchStatement* switchStmt) override;
	virtual void Visit(BfTryStatement* tryStmt) override;
	virtual void Visit(BfCatchStatement* catchStmt) override;
	virtual void Visit(BfFinallyStatement* finallyStmt) override;
	virtual void Visit(BfCheckedStatement* checkedStmt) override;
	virtual void Visit(BfUncheckedStatement* uncheckedStmt) override;
	virtual void Visit(BfIfStatement* ifStmt) override;
	virtual void Visit(BfThrowStatement* throwStmt) override;
	virtual void Visit(BfDeleteStatement* deleteStmt) override;
	virtual void Visit(BfReturnStatement* returnStmt) override;
	virtual void Visit(BfBreakStatement* breakStmt) override;
	virtual void Visit(BfContinueStatement* continueStmt) override;
	virtual void Visit(BfFallthroughStatement* fallthroughStmt) override;
	virtual void Visit(BfUsingStatement* whileStmt) override;
	virtual void Visit(BfDoStatement* whileStmt) override;
	virtual void Visit(BfRepeatStatement* repeatStmt) override;
	virtual void Visit(BfWhileStatement* whileStmt) override;
	virtual void Visit(BfForStatement* forStmt) override;
	virtual void Visit(BfForEachStatement* forEachStmt) override;
	virtual void Visit(BfDeferStatement* deferStmt) override;
	virtual void Visit(BfEnumCaseBindExpression * caseBindExpr) override;
	virtual void Visit(BfCaseExpression * caseExpr) override;
	virtual void Visit(BfConditionalExpression* condExpr) override;
	virtual void Visit(BfAssignmentExpression* assignExpr) override;
	virtual void Visit(BfParenthesizedExpression* parenExpr) override;
	virtual void Visit(BfTupleExpression * tupleExpr) override;
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override;
	virtual void Visit(BfIndexerExpression* indexerExpr) override;
	virtual void Visit(BfUnaryOperatorExpression* binOpExpr) override;
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr) override;
	virtual void Visit(BfConstructorDeclaration* ctorDeclaration) override;
	virtual void Visit(BfDestructorDeclaration* dtorDeclaration) override;
	virtual void Visit(BfMethodDeclaration* methodDeclaration) override;
	virtual void Visit(BfOperatorDeclaration* opreratorDeclaration) override;
	virtual void Visit(BfPropertyMethodDeclaration* propertyDeclaration) override;
	virtual void Visit(BfPropertyDeclaration* propertyDeclaration) override;
	virtual void Visit(BfFieldDeclaration* fieldDeclaration) override;
	virtual void Visit(BfEnumCaseDeclaration* enumCaseDeclaration) override;
	virtual void Visit(BfTypeAliasDeclaration* typeDeclaration) override;
	virtual void Visit(BfTypeDeclaration* typeDeclaration) override;
	virtual void Visit(BfUsingDirective* usingDirective) override;
	virtual void Visit(BfUsingStaticDirective* usingDirective) override;
	virtual void Visit(BfNamespaceDeclaration* namespaceDeclaration) override;
	virtual void Visit(BfBlock* block) override;
	virtual void Visit(BfRootNode* rootNode) override;
	virtual void Visit(BfInlineAsmStatement* asmStmt) override;
};

NS_BF_END