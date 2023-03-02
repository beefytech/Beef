#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"

NS_BF_BEGIN

class BfDeferEvalChecker : public BfStructuralVisitor
{
public:
	BfAstNode* mRootNode;
	bool mNeedsDeferEval;
	bool mDeferDelegateBind;
	bool mDeferLiterals;
	bool mDeferStrings;

public:
	BfDeferEvalChecker();

	void Check(BfAstNode* node);

	virtual void Visit(BfAstNode* node) override;

	virtual void Visit(BfAttributedExpression* attributedExpr) override;
	virtual void Visit(BfInitializerExpression* collectionInitExpr) override;
	virtual void Visit(BfLiteralExpression* literalExpr) override;
	virtual void Visit(BfCastExpression* castExpr) override;
	virtual void Visit(BfParenthesizedExpression* parenExpr) override;
	virtual void Visit(BfTupleExpression* tupleExpr) override;
	virtual void Visit(BfMemberReferenceExpression* memberRefExpr) override;
	virtual void Visit(BfInvocationExpression* invocationExpr) override;
	virtual void Visit(BfLambdaBindExpression* lambdaBindExpr) override;
	virtual void Visit(BfDelegateBindExpression* delegateBindExpr) override;
	virtual void Visit(BfConditionalExpression* condExpr) override;
	virtual void Visit(BfUnaryOperatorExpression* unaryOpExpr) override;
	virtual void Visit(BfObjectCreateExpression* objCreateExpr) override;
	virtual void Visit(BfBinaryOperatorExpression* binOpExpr) override;
	virtual void Visit(BfDefaultExpression* defaultExpr) override;
	virtual void Visit(BfVariableDeclaration* varDecl) override;
};

NS_BF_END