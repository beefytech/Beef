#include "BfDeferEvalChecker.h"
#include "BfUtil.h"

USING_NS_BF;

BfDeferEvalChecker::BfDeferEvalChecker()
{
	mRootNode = NULL;
	mNeedsDeferEval = false;
	mDeferLiterals = true;
	mDeferStrings = false;
	mDeferDelegateBind = true;
}

void BfDeferEvalChecker::Check(BfAstNode* node)
{
	if (auto namedNode = BfNodeDynCastExact<BfNamedExpression>(node))
		node = namedNode->mExpression;

	if (node == NULL)
		return;

	SetAndRestoreValue<BfAstNode*> rootNode(mRootNode, node);
	node->Accept(this);
}

void BfDeferEvalChecker::Visit(BfAstNode* attribExpr)
{
	mNeedsDeferEval = false;
}

void BfDeferEvalChecker::Visit(BfAttributedExpression* attributedExpr)
{
	VisitChild(attributedExpr->mExpression);
}

void BfDeferEvalChecker::Visit(BfInitializerExpression* collectionInitExpr)
{
	VisitChild(collectionInitExpr->mTarget);
}

void BfDeferEvalChecker::Visit(BfLiteralExpression* literalExpr)
{
	switch (literalExpr->mValue.mTypeCode)
	{
	case BfTypeCode_NullPtr:
	case BfTypeCode_Boolean:
	case BfTypeCode_Char8:
	case BfTypeCode_Int8:
	case BfTypeCode_UInt8:
	case BfTypeCode_Int16:
	case BfTypeCode_UInt16:
	case BfTypeCode_Int32:
	case BfTypeCode_UInt32:
	case BfTypeCode_Int64:
	case BfTypeCode_UInt64:
	case BfTypeCode_IntPtr:
	case BfTypeCode_UIntPtr:
	case BfTypeCode_IntUnknown:
	case BfTypeCode_UIntUnknown:
		if (mDeferLiterals)
			mNeedsDeferEval = true;
		break;
	case BfTypeCode_CharPtr:
		if (mDeferStrings)
			mNeedsDeferEval = true;
		break;
	default:
		mNeedsDeferEval = false;
	}
}

void BfDeferEvalChecker::Visit(BfCastExpression* castExpr)
{
	if (auto namedTypeRef = BfNodeDynCastExact<BfNamedTypeReference>(castExpr->mTypeRef))
	{
		if (namedTypeRef->ToString() == "ExpectedType")
		{
			mNeedsDeferEval = true;
		}
	}
	else if (auto dotTypeRef = BfNodeDynCastExact<BfDotTypeReference>(castExpr->mTypeRef))
	{
		mNeedsDeferEval = true;
	}
}

void BfDeferEvalChecker::Visit(BfParenthesizedExpression* parenExpr)
{
	VisitChild(parenExpr->mExpression);
}

void BfDeferEvalChecker::Visit(BfTupleExpression* tupleExpr)
{
	bool needDeferEval = false;
	for (auto element : tupleExpr->mValues)
	{
		VisitChild(element);
		needDeferEval |= mNeedsDeferEval;
	}

	mNeedsDeferEval = needDeferEval;
}

void BfDeferEvalChecker::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	// Dotted name?
	mNeedsDeferEval = (memberRefExpr->mTarget == NULL);
}

void BfDeferEvalChecker::Visit(BfInvocationExpression* invocationExpr)
{
	VisitChild(invocationExpr->mTarget);
}

void BfDeferEvalChecker::Visit(BfLambdaBindExpression* lambdaBindExpr)
{
	if (mDeferDelegateBind)
		mNeedsDeferEval = true;
}

void BfDeferEvalChecker::Visit(BfDelegateBindExpression* delegateBindExpr)
{
	if (mDeferDelegateBind)
		mNeedsDeferEval = true;
}

void BfDeferEvalChecker::Visit(BfConditionalExpression* condExpr)
{
	VisitChild(condExpr->mConditionExpression);
	bool prev = mNeedsDeferEval;
	VisitChild(condExpr->mTrueExpression);
	prev |= mNeedsDeferEval;
	VisitChild(condExpr->mFalseExpression);
	mNeedsDeferEval |= prev;
}

void BfDeferEvalChecker::Visit(BfUnaryOperatorExpression* unaryOpExpr)
{
	switch (unaryOpExpr->mOp)
	{
	case BfUnaryOp_Negate:
	case BfUnaryOp_Positive:
	case BfUnaryOp_InvertBits:
		VisitChild(unaryOpExpr->mExpression);
		break;
// 	case BfUnaryOp_Params:
// 		mNeedsDeferEval = true;
// 		break;
	default:
		mNeedsDeferEval = false;
	}
}

void BfDeferEvalChecker::Visit(BfObjectCreateExpression * objCreateExpr)
{
	if (objCreateExpr->mTypeRef != NULL)
	{
		if (objCreateExpr->mTypeRef->IsExact<BfDotTypeReference>())
			mNeedsDeferEval = true;
	}
}

void BfDeferEvalChecker::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	switch (binOpExpr->mOp)
	{
	case BfBinaryOp_Add:
	case BfBinaryOp_Subtract:
	case BfBinaryOp_Multiply:
	case BfBinaryOp_Divide:
	case BfBinaryOp_Modulus:
	case BfBinaryOp_BitwiseAnd:
	case BfBinaryOp_BitwiseOr:
	case BfBinaryOp_ExclusiveOr:
	case BfBinaryOp_LeftShift:
	case BfBinaryOp_RightShift:
	case BfBinaryOp_GreaterThan:
	case BfBinaryOp_LessThan:
	case BfBinaryOp_GreaterThanOrEqual:
	case BfBinaryOp_LessThanOrEqual:
		{
			VisitChild(binOpExpr->mLeft);
			bool prev = mNeedsDeferEval;
			VisitChild(binOpExpr->mRight);
			mNeedsDeferEval |= prev;
		}
		break;
	default:
		mNeedsDeferEval = false;
	}
}

void BfDeferEvalChecker::Visit(BfDefaultExpression* defaultExpr)
{
	if (defaultExpr->mTypeRef == NULL)
		mNeedsDeferEval = true;
}

void BfDeferEvalChecker::Visit(BfVariableDeclaration* varDecl)
{
	if (varDecl != mRootNode)
		mNeedsDeferEval = true;
}