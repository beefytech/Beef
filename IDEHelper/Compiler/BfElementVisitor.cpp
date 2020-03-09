#include "BfElementVisitor.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

BfElementVisitor::BfElementVisitor()
{
	
}

void BfElementVisitor::Visit(BfTypedValueExpression* typedValueExpr)
{
	Visit(typedValueExpr->ToBase());
}

void BfElementVisitor::Visit(BfCommentNode* commentNode)
{
	Visit(commentNode->ToBase());
}

void BfElementVisitor::Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection)
{
	Visit(preprocesorIgnoredSection->ToBase());
}

void BfElementVisitor::Visit(BfPreprocessorNode* preprocessorNode)
{
	Visit(preprocessorNode->ToBase());

	VisitChild(preprocessorNode->mCommand);
	VisitChild(preprocessorNode->mArgument);
}

void BfElementVisitor::Visit(BfPreprocessorDefinedExpression* definedExpr)
{
	Visit(definedExpr->ToBase());

	VisitChild(definedExpr->mIdentifier);	
}

void BfElementVisitor::Visit(BfAttributeDirective* attributeDirective)
{
	Visit(attributeDirective->ToBase());

	VisitChild(attributeDirective->mAttrOpenToken); // Either [ or ,
	VisitChild(attributeDirective->mAttrCloseToken);
	VisitChild(attributeDirective->mAttributeTargetSpecifier);

	VisitChild(attributeDirective->mAttributeTypeRef);
	VisitChild(attributeDirective->mCtorOpenParen);
	VisitChild(attributeDirective->mCtorCloseParen);
	for (auto& val : attributeDirective->mArguments)
		VisitChild(val);
	for (auto& val : attributeDirective->mCommas)
		VisitChild(val);
	VisitChild(attributeDirective->mNextAttribute);
}

void BfElementVisitor::Visit(BfGenericParamsDeclaration* genericParams)
{
	Visit(genericParams->ToBase());

	VisitChild(genericParams->mOpenChevron);
	for (auto& val : genericParams->mGenericParams)
		VisitChild(val);
	for (auto& val : genericParams->mCommas)
		VisitChild(val);	
	VisitChild(genericParams->mCloseChevron);
}

void BfElementVisitor::Visit(BfGenericOperatorConstraint* genericConstraints)
{
	Visit(genericConstraints->ToBase());

	VisitChild(genericConstraints->mOperatorToken);
	VisitChild(genericConstraints->mLeftType);
	VisitChild(genericConstraints->mOpToken);
	VisitChild(genericConstraints->mRightType);
}

void BfElementVisitor::Visit(BfGenericConstraintsDeclaration* genericConstraints)
{
	Visit(genericConstraints->ToBase());

	for (auto genericConstraint : genericConstraints->mGenericConstraints)
	{		
		VisitChild(genericConstraint->mWhereToken);
		VisitChild(genericConstraint->mTypeRef);
		VisitChild(genericConstraint->mColonToken);
		for (auto val : genericConstraint->mConstraintTypes)
			VisitChild(val);
		for (auto val : genericConstraint->mCommas)
			VisitChild(val);		
	}		
}

void BfElementVisitor::Visit(BfGenericArgumentsNode* genericArgumentsNode)
{
	Visit(genericArgumentsNode->ToBase());

	VisitChild(genericArgumentsNode->mOpenChevron);
	for (auto& val : genericArgumentsNode->mGenericArgs)
		VisitChild(val);
	for (auto& val : genericArgumentsNode->mCommas)
		VisitChild(val);	
	VisitChild(genericArgumentsNode->mCloseChevron);
}

void BfElementVisitor::Visit(BfStatement* stmt)
{
	Visit(stmt->ToBase());

	VisitChild(stmt->mTrailingSemicolon);
}

void BfElementVisitor::Visit(BfLabelableStatement* labelableStmt)
{
	Visit(labelableStmt->ToBase());

	if (labelableStmt->mLabelNode != NULL)
	{
		VisitChild(labelableStmt->mLabelNode->mLabel);
		VisitChild(labelableStmt->mLabelNode->mColonToken);
	}
}

void BfElementVisitor::Visit(BfScopeNode* scopeNode)
{
	Visit(scopeNode->ToBase());

	VisitChild(scopeNode->mScopeToken);
	VisitChild(scopeNode->mColonToken);
	VisitChild(scopeNode->mTargetNode);
	VisitChild(scopeNode->mAttributes);
}

void BfElementVisitor::Visit(BfNewNode* newNode)
{
	Visit(newNode->ToBase());

	VisitChild(newNode->mNewToken);
	VisitChild(newNode->mColonToken);
	VisitChild(newNode->mAllocNode);
	VisitChild(newNode->mAttributes);
}

void BfElementVisitor::Visit(BfLabeledBlock* labeledBlock)
{
	Visit(labeledBlock->ToBase());
	
	VisitChild(labeledBlock->mBlock);
}

void BfElementVisitor::Visit(BfExpression* expr)
{
	Visit(expr->ToBase());
}

void BfElementVisitor::Visit(BfExpressionStatement* exprStmt)
{
	Visit(exprStmt->ToBase());

	VisitChild(exprStmt->mExpression);
}

void BfElementVisitor::Visit(BfAttributedExpression* attribExpr)
{
	Visit(attribExpr->ToBase());

	VisitChild(attribExpr->mAttributes);
	VisitChild(attribExpr->mExpression);
}

void BfElementVisitor::Visit(BfEmptyStatement* emptyStmt)
{
	Visit(emptyStmt->ToBase());
}

void BfElementVisitor::Visit(BfTokenNode* tokenNode)
{
	Visit(tokenNode->ToBase());
}

void BfElementVisitor::Visit(BfTokenPairNode* tokenPairNode)
{
	Visit(tokenPairNode->ToBase());

	VisitChild(tokenPairNode->mLeft);
	VisitChild(tokenPairNode->mRight);
}

void BfElementVisitor::Visit(BfLiteralExpression* literalExpr)
{
	Visit(literalExpr->ToBase());
}

void BfElementVisitor::Visit(BfIdentifierNode* identifierNode)
{
	Visit(identifierNode->ToBase());
}

void BfElementVisitor::Visit(BfAttributedIdentifierNode * attrIdentifierNode)
{
	Visit(attrIdentifierNode->ToBase());
	VisitChild(attrIdentifierNode->mAttributes);
	VisitChild(attrIdentifierNode->mIdentifier);
}

void BfElementVisitor::Visit(BfQualifiedNameNode* nameNode)
{
	Visit(nameNode->ToBase());

	VisitChild(nameNode->mLeft);
	VisitChild(nameNode->mDot);
	VisitChild(nameNode->mRight);
}

void BfElementVisitor::Visit(BfThisExpression* thisExpr)
{
	Visit(thisExpr->ToBase());	
}

void BfElementVisitor::Visit(BfBaseExpression* baseExpr)
{
	Visit(baseExpr->ToBase());
}

void BfElementVisitor::Visit(BfMixinExpression* mixinExpr)
{
	Visit(mixinExpr->ToBase());
}

void BfElementVisitor::Visit(BfSizedArrayCreateExpression* createExpr)
{
	Visit(createExpr->ToBase());

	VisitChild(createExpr->mTypeRef);
	VisitChild(createExpr->mInitializer);
}

void BfElementVisitor::Visit(BfCollectionInitializerExpression* collectionInitExpr)
{
	Visit(collectionInitExpr->ToBase());

 	VisitChild(collectionInitExpr->mOpenBrace);
	for (auto& val : collectionInitExpr->mValues)
		VisitChild(val);	
	for (auto& val : collectionInitExpr->mCommas)
		VisitChild(val);	
	VisitChild(collectionInitExpr->mCloseBrace);
}

void BfElementVisitor::Visit(BfTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfElementVisitor::Visit(BfNamedTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mNameNode);
}

void BfElementVisitor::Visit(BfQualifiedTypeReference* qualifiedTypeRef)
{
	Visit(qualifiedTypeRef->ToBase());
	
	VisitChild(qualifiedTypeRef->mLeft);
	VisitChild(qualifiedTypeRef->mDot);
	VisitChild(qualifiedTypeRef->mRight);
}

void BfElementVisitor::Visit(BfDotTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mDotToken);
}

void BfElementVisitor::Visit(BfVarTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mVarToken);
}

void BfElementVisitor::Visit(BfVarRefTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mVarToken);
	VisitChild(typeRef->mRefToken);
}

void BfElementVisitor::Visit(BfLetTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mLetToken);
}

void BfElementVisitor::Visit(BfConstTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot

	VisitChild(typeRef->mConstToken);
	VisitChild(typeRef->mElementType);
}

void BfElementVisitor::Visit(BfConstExprTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot

	VisitChild(typeRef->mConstToken);
	VisitChild(typeRef->mConstExpr);
}

void BfElementVisitor::Visit(BfRefTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot
	
	VisitChild(typeRef->mRefToken);
	VisitChild(typeRef->mElementType);
}

void BfElementVisitor::Visit(BfRetTypeTypeRef * typeRef)
{	
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot

	VisitChild(typeRef->mRetTypeToken);
	VisitChild(typeRef->mOpenParen);	
	VisitChild(typeRef->mElementType);
	VisitChild(typeRef->mCloseParen);
}

void BfElementVisitor::Visit(BfArrayTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot
	
	VisitChild(typeRef->mElementType);
	VisitChild(typeRef->mOpenBracket);
	for (auto& val : typeRef->mParams)
		VisitChild(val);	
	VisitChild(typeRef->mCloseBracket);
}

void BfElementVisitor::Visit(BfGenericInstanceTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mOpenChevron);
	for (auto& val : typeRef->mGenericArguments)
		VisitChild(val);	
	if (typeRef->mCommas.mVals != NULL)
	{
		for (auto& val : typeRef->mCommas)
			VisitChild(val);
	}
	VisitChild(typeRef->mCloseChevron);
}

void BfElementVisitor::Visit(BfTupleTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mOpenParen);
	for (auto& val : typeRef->mFieldNames)
		VisitChild(val);	
	for (auto& val : typeRef->mFieldTypes)
		VisitChild(val);
	if (typeRef->mCommas.mVals != NULL)
	{
		for (auto& val : typeRef->mCommas)
			VisitChild(val);
	}
	VisitChild(typeRef->mCloseParen);
}

void BfElementVisitor::Visit(BfDeclTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mToken);
	VisitChild(typeRef->mOpenParen);
	VisitChild(typeRef->mTarget);
	VisitChild(typeRef->mCloseParen);
}

void BfElementVisitor::Visit(BfDelegateTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef);

	VisitChild(typeRef->mTypeToken);
	VisitChild(typeRef->mReturnType);
	VisitChild(typeRef->mAttributes);
	VisitChild(typeRef->mOpenParen);
	for (auto& param : typeRef->mParams)
		VisitChild(param);
	for (auto& comma : typeRef->mCommas)
		VisitChild(comma);
	VisitChild(typeRef->mCloseParen);
}

void BfElementVisitor::Visit(BfPointerTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot

	VisitChild(typeRef->mElementType);
	VisitChild(typeRef->mStarNode);
}

void BfElementVisitor::Visit(BfNullableTypeRef* typeRef)
{
	Visit((BfTypeReference*)typeRef); // Skip the Elemented part so we can put the element in the right spot

	VisitChild(typeRef->mElementType);
	VisitChild(typeRef->mQuestionToken);
}

void BfElementVisitor::Visit(BfVariableDeclaration* varDecl)
{
	Visit(varDecl->ToBase());

	VisitChild(varDecl->mAttributes);
	VisitChild(varDecl->mModSpecifier);
	VisitChild(varDecl->mTypeRef);
	VisitChild(varDecl->mPrecedingComma);
	VisitChild(varDecl->mNameNode); // Either BfIdentifierNode or BfTupleExpression
	VisitChild(varDecl->mEqualsNode);
	VisitChild(varDecl->mInitializer);
}

void BfElementVisitor::Visit(BfLocalMethodDeclaration* methodDecl)
{
	Visit(methodDecl->ToBase());

	VisitChild(methodDecl->mMethodDeclaration);
}

void BfElementVisitor::Visit(BfParameterDeclaration* paramDecl)
{
	Visit(paramDecl->ToBase());

	VisitChild(paramDecl->mModToken); // 'Params'
}

void BfElementVisitor::Visit(BfParamsExpression* paramsExpr)
{
	Visit(paramsExpr->ToBase());

	VisitChild(paramsExpr->mParamsToken);
}

void BfElementVisitor::Visit(BfTypeAttrExpression* typeAttrExpr)
{
	Visit(typeAttrExpr->ToBase());

	VisitChild(typeAttrExpr->mToken);
	VisitChild(typeAttrExpr->mOpenParen);
	VisitChild(typeAttrExpr->mTypeRef);
	VisitChild(typeAttrExpr->mCloseParen);
}

void BfElementVisitor::Visit(BfDefaultExpression* defaultExpr)
{
	Visit(defaultExpr->ToBase());

	VisitChild(defaultExpr->mDefaultToken);
	VisitChild(defaultExpr->mOpenParen);
	VisitChild(defaultExpr->mTypeRef);
	VisitChild(defaultExpr->mCloseParen);
}

void BfElementVisitor::Visit(BfUninitializedExpression* uninitializedExpr)
{
	Visit(uninitializedExpr->ToBase());

	VisitChild(uninitializedExpr->mQuestionToken);
}

void BfElementVisitor::Visit(BfCheckTypeExpression* checkTypeExpr)
{
	Visit(checkTypeExpr->ToBase());

	VisitChild(checkTypeExpr->mTarget);
	VisitChild(checkTypeExpr->mIsToken);
	VisitChild(checkTypeExpr->mTypeRef);
}

void BfElementVisitor::Visit(BfDynamicCastExpression* dynCastExpr)
{
	Visit(dynCastExpr->ToBase());

	VisitChild(dynCastExpr->mTarget);
	VisitChild(dynCastExpr->mAsToken);
	VisitChild(dynCastExpr->mTypeRef);
}

void BfElementVisitor::Visit(BfCastExpression* castExpr)
{
	Visit(castExpr->ToBase());

	VisitChild(castExpr->mOpenParen);
	VisitChild(castExpr->mTypeRef);
	VisitChild(castExpr->mCloseParen);
}

void BfElementVisitor::Visit(BfDelegateBindExpression* bindExpr)
{
	Visit(bindExpr->ToBase());

	VisitChild(bindExpr->mNewToken);
	VisitChild(bindExpr->mFatArrowToken);
	VisitChild(bindExpr->mTarget);
	VisitChild(bindExpr->mGenericArgs);
}

void BfElementVisitor::Visit(BfLambdaBindExpression* lambdaBindExpr)
{
	Visit(lambdaBindExpr->ToBase());

	VisitChild(lambdaBindExpr->mNewToken);
	
	VisitChild(lambdaBindExpr->mOpenParen);
	VisitChild(lambdaBindExpr->mCloseParen);
	for (auto& val : lambdaBindExpr->mParams)
		VisitChild(val);
	for (auto& val : lambdaBindExpr->mCommas)
		VisitChild(val);	
	VisitChild(lambdaBindExpr->mFatArrowToken);
	VisitChild(lambdaBindExpr->mBody); // Either expression or block
		
	VisitChild(lambdaBindExpr->mDtor);
}

void BfElementVisitor::Visit(BfObjectCreateExpression* newExpr)
{
	Visit(newExpr->ToBase());

	VisitChild(newExpr->mNewNode);
	VisitChild(newExpr->mStarToken);
	VisitChild(newExpr->mTypeRef);	
	VisitChild(newExpr->mOpenToken);
	VisitChild(newExpr->mCloseToken);	
	for (auto& val : newExpr->mArguments)
		VisitChild(val);
	for (auto& val : newExpr->mCommas)
		VisitChild(val);	
}

void BfElementVisitor::Visit(BfBoxExpression* boxExpr)
{
	Visit(boxExpr->ToBase());

	VisitChild(boxExpr->mAllocNode);
	VisitChild(boxExpr->mBoxToken);
	VisitChild(boxExpr->mExpression);
}

void BfElementVisitor::Visit(BfScopedInvocationTarget* scopedTarget)
{
	Visit(scopedTarget->ToBase());

	VisitChild(scopedTarget->mTarget);
	VisitChild(scopedTarget->mColonToken);
	VisitChild(scopedTarget->mScopeName);
}

void BfElementVisitor::Visit(BfThrowStatement* throwStmt)
{
	Visit(throwStmt->ToBase());

	VisitChild(throwStmt->mThrowToken);
	VisitChild(throwStmt->mExpression);
}

void BfElementVisitor::Visit(BfDeleteStatement* deleteStmt)
{
	Visit(deleteStmt->ToBase());
	
	VisitChild(deleteStmt->mDeleteToken);
	VisitChild(deleteStmt->mTargetTypeToken);
	VisitChild(deleteStmt->mAllocExpr);
	VisitChild(deleteStmt->mExpression);
}

void BfElementVisitor::Visit(BfInvocationExpression* invocationExpr)
{
	Visit(invocationExpr->ToBase());

	VisitChild(invocationExpr->mTarget);
	VisitChild(invocationExpr->mOpenParen);
	VisitChild(invocationExpr->mCloseParen);
	VisitChild(invocationExpr->mGenericArgs);
	for (auto& val : invocationExpr->mArguments)
		VisitChild(val);
	for (auto& val : invocationExpr->mCommas)
		VisitChild(val);	
}

void BfElementVisitor::Visit(BfEnumCaseBindExpression* caseBindExpr)
{
	Visit(caseBindExpr->ToBase());

	VisitChild(caseBindExpr->mBindToken);
	VisitChild(caseBindExpr->mEnumMemberExpr);
	VisitChild(caseBindExpr->mBindNames);
}

void BfElementVisitor::Visit(BfCaseExpression* caseExpr)
{
	Visit(caseExpr->ToBase());

	VisitChild(caseExpr->mCaseToken);
	VisitChild(caseExpr->mCaseExpression);
	VisitChild(caseExpr->mEqualsNode);
	VisitChild(caseExpr->mValueExpression);
}

void BfElementVisitor::Visit(BfSwitchCase* switchCase)
{
	Visit(switchCase->ToBase());

	VisitChild(switchCase->mCaseToken);
	for (auto& val : switchCase->mCaseExpressions)
		VisitChild(val);
	for (auto& val : switchCase->mCaseCommas)
		VisitChild(val);	
	VisitChild(switchCase->mColonToken);
	VisitChild(switchCase->mCodeBlock);
	VisitChild(switchCase->mEndingToken);
	VisitChild(switchCase->mEndingSemicolonToken);
}

void BfElementVisitor::Visit(BfWhenExpression* whenExpr)
{
	Visit(whenExpr->ToBase());

	VisitChild(whenExpr->mWhenToken);
	VisitChild(whenExpr->mExpression);
}

void BfElementVisitor::Visit(BfSwitchStatement* switchStmt)
{
	Visit(switchStmt->ToBase());

	VisitChild(switchStmt->mSwitchToken);
	VisitChild(switchStmt->mOpenParen);
	VisitChild(switchStmt->mSwitchValue);
	VisitChild(switchStmt->mCloseParen);

	VisitChild(switchStmt->mOpenBrace);
	for (auto& val : switchStmt->mSwitchCases)
		VisitChild(val);
	VisitChild(switchStmt->mDefaultCase);
	VisitChild(switchStmt->mCloseBrace);
}

void BfElementVisitor::Visit(BfTryStatement* tryStmt)
{
	Visit(tryStmt->ToBase());

	VisitChild(tryStmt->mTryToken);
	VisitChild(tryStmt->mStatement);
}

void BfElementVisitor::Visit(BfCatchStatement* catchStmt)
{
	Visit(catchStmt->ToBase());

	VisitChild(catchStmt->mCatchToken);
	VisitChild(catchStmt->mStatement);
}

void BfElementVisitor::Visit(BfFinallyStatement* finallyStmt)
{
	Visit(finallyStmt->ToBase());

	VisitChild(finallyStmt->mFinallyToken);
	VisitChild(finallyStmt->mStatement);
}

void BfElementVisitor::Visit(BfCheckedStatement* checkedStmt)
{
	Visit(checkedStmt->ToBase());

	VisitChild(checkedStmt->mCheckedToken);
	VisitChild(checkedStmt->mStatement);
}

void BfElementVisitor::Visit(BfUncheckedStatement* uncheckedStmt)
{
	Visit(uncheckedStmt->ToBase());

	VisitChild(uncheckedStmt->mUncheckedToken);
	VisitChild(uncheckedStmt->mStatement);
}

void BfElementVisitor::Visit(BfIfStatement* ifStmt)
{
	Visit(ifStmt->ToBase());

	VisitChild(ifStmt->mIfToken);
	VisitChild(ifStmt->mOpenParen);
	VisitChild(ifStmt->mCondition);
	VisitChild(ifStmt->mCloseParen);
	VisitChild(ifStmt->mTrueStatement);
	VisitChild(ifStmt->mElseToken);
	VisitChild(ifStmt->mFalseStatement);
}

void BfElementVisitor::Visit(BfDeferStatement* deferStmt)
{
	Visit(deferStmt->ToBase());

	VisitChild(deferStmt->mDeferToken);
	VisitChild(deferStmt->mColonToken);
	VisitChild(deferStmt->mScopeName);

	if (deferStmt->mBind != NULL)
	{
		auto bind = deferStmt->mBind;
		
		VisitChild(bind->mOpenBracket);		
		VisitChild(bind->mCloseBracket);
		for (auto& val : bind->mParams)
			VisitChild(val);
		for (auto& val : bind->mCommas)
			VisitChild(val);				
	}

	VisitChild(deferStmt->mOpenParen);
	VisitChild(deferStmt->mScopeToken);
	VisitChild(deferStmt->mCloseParen);

	VisitChild(deferStmt->mTargetNode);
}

void BfElementVisitor::Visit(BfReturnStatement* returnStmt)
{
 	Visit(returnStmt->ToBase());

	VisitChild(returnStmt->mReturnToken);
	VisitChild(returnStmt->mExpression);
}

void BfElementVisitor::Visit(BfYieldStatement* yieldStmt)
{
	Visit(yieldStmt->ToBase());

	VisitChild(yieldStmt->mReturnOrBreakToken);
	VisitChild(yieldStmt->mExpression);
}

void BfElementVisitor::Visit(BfUsingStatement* usingStmt)
{
	Visit(usingStmt->ToBase());

	VisitChild(usingStmt->mUsingToken);
	VisitChild(usingStmt->mOpenParen);
	VisitChild(usingStmt->mVariableDeclaration);
	VisitChild(usingStmt->mCloseParen);
	VisitChild(usingStmt->mEmbeddedStatement);
	
}

void BfElementVisitor::Visit(BfDoStatement* doStmt)
{
	Visit(doStmt->ToBase());

	VisitChild(doStmt->mDoToken);
	VisitChild(doStmt->mEmbeddedStatement);	
}

void BfElementVisitor::Visit(BfRepeatStatement* repeatStmt)
{
	Visit(repeatStmt->ToBase());

	VisitChild(repeatStmt->mRepeatToken);
	VisitChild(repeatStmt->mEmbeddedStatement);
	VisitChild(repeatStmt->mWhileToken);
	VisitChild(repeatStmt->mOpenParen);
	VisitChild(repeatStmt->mCondition);
	VisitChild(repeatStmt->mCloseParen);
}

void BfElementVisitor::Visit(BfWhileStatement* whileStmt)
{
	Visit(whileStmt->ToBase());

	VisitChild(whileStmt->mWhileToken);
	VisitChild(whileStmt->mOpenParen);
	VisitChild(whileStmt->mCondition);
	VisitChild(whileStmt->mCloseParen);
	VisitChild(whileStmt->mEmbeddedStatement);
}

void BfElementVisitor::Visit(BfBreakStatement* breakStmt)
{
	Visit(breakStmt->ToBase());

	VisitChild(breakStmt->mBreakNode);
	VisitChild(breakStmt->mLabel);
}

void BfElementVisitor::Visit(BfContinueStatement* continueStmt)
{
	Visit(continueStmt->ToBase());

	VisitChild(continueStmt->mContinueNode);
	VisitChild(continueStmt->mLabel);
}

void BfElementVisitor::Visit(BfFallthroughStatement* fallthroughStmt)
{
	Visit(fallthroughStmt->ToBase());

	VisitChild(fallthroughStmt->mFallthroughToken);
}

void BfElementVisitor::Visit(BfForStatement* forStmt)
{
	Visit(forStmt->ToBase());

	VisitChild(forStmt->mForToken);
	VisitChild(forStmt->mOpenParen);
	for (auto& val : forStmt->mInitializers)
		VisitChild(val);
	for (auto& val : forStmt->mInitializerCommas)
		VisitChild(val);	
	VisitChild(forStmt->mInitializerSemicolon);
	VisitChild(forStmt->mCondition);
	VisitChild(forStmt->mConditionSemicolon);
	for (auto& val : forStmt->mIterators)
		VisitChild(val);
	for (auto& val : forStmt->mIteratorCommas)
		VisitChild(val);	
	VisitChild(forStmt->mCloseParen);
	VisitChild(forStmt->mEmbeddedStatement);
}

void BfElementVisitor::Visit(BfForEachStatement* forEachStmt)
{
	Visit(forEachStmt->ToBase());

	VisitChild(forEachStmt->mForToken);
	VisitChild(forEachStmt->mOpenParen);
	VisitChild(forEachStmt->mReadOnlyToken);
	VisitChild(forEachStmt->mVariableTypeRef);
	VisitChild(forEachStmt->mVariableName);
	VisitChild(forEachStmt->mInToken);
	VisitChild(forEachStmt->mCollectionExpression);
	VisitChild(forEachStmt->mCloseParen);
	VisitChild(forEachStmt->mEmbeddedStatement);
}

void BfElementVisitor::Visit(BfConditionalExpression* condExpr)
{
	Visit(condExpr->ToBase());

	VisitChild(condExpr->mConditionExpression);
	VisitChild(condExpr->mQuestionToken);
	VisitChild(condExpr->mTrueExpression);
	VisitChild(condExpr->mColonToken);
	VisitChild(condExpr->mFalseExpression);
}

void BfElementVisitor::Visit(BfAssignmentExpression* assignExpr)
{
	Visit(assignExpr->ToBase());

	VisitChild(assignExpr->mOpToken);
	VisitChild(assignExpr->mLeft);
	VisitChild(assignExpr->mRight);
}

void BfElementVisitor::Visit(BfParenthesizedExpression* parenExpr)
{
	Visit(parenExpr->ToBase());

	VisitChild(parenExpr->mOpenParen);
	VisitChild(parenExpr->mExpression);
	VisitChild(parenExpr->mCloseParen);
}

void BfElementVisitor::Visit(BfTupleExpression* tupleExpr)
{
	Visit(tupleExpr->ToBase());

	VisitChild(tupleExpr->mOpenParen);
	for (auto& val : tupleExpr->mNames)
	{
		if (val != NULL)
		{
			VisitChild(val->mNameNode);
			VisitChild(val->mColonToken);
		}		
	}
	for (auto& val : tupleExpr->mValues)
		VisitChild(val);
	for (auto& val : tupleExpr->mCommas)
		VisitChild(val);	
	VisitChild(tupleExpr->mCloseParen);
}

void BfElementVisitor::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	Visit(memberRefExpr->ToBase());

	VisitChild(memberRefExpr->mDotToken);
	VisitChild(memberRefExpr->mTarget);
	VisitChild(memberRefExpr->mMemberName);
}

void BfElementVisitor::Visit(BfIndexerExpression* indexerExpr)
{
	Visit(indexerExpr->ToBase());

	VisitChild(indexerExpr->mTarget);
	VisitChild(indexerExpr->mOpenBracket);
	VisitChild(indexerExpr->mCloseBracket);	
	for (auto& arg : indexerExpr->mArguments)
		VisitChild(arg);
	for (auto& comma : indexerExpr->mCommas)
		VisitChild(comma);	
}

void BfElementVisitor::Visit(BfUnaryOperatorExpression* binOpExpr)
{
	Visit(binOpExpr->ToBase());

	VisitChild(binOpExpr->mOpToken);
	VisitChild(binOpExpr->mExpression);
}

void BfElementVisitor::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	Visit(binOpExpr->ToBase());

	VisitChild(binOpExpr->mOpToken);
	VisitChild(binOpExpr->mLeft);
	VisitChild(binOpExpr->mRight);
}

void BfElementVisitor::Visit(BfConstructorDeclaration* ctorDeclaration)
{
	Visit(ctorDeclaration->ToBase());

	VisitChild(ctorDeclaration->mThisToken);

	VisitChild(ctorDeclaration->mInitializerColonToken);
	VisitChild(ctorDeclaration->mInitializer);
}

void BfElementVisitor::Visit(BfDestructorDeclaration* dtorDeclaration)
{
	Visit(dtorDeclaration->ToBase());

	VisitChild(dtorDeclaration->mTildeToken);
	VisitChild(dtorDeclaration->mThisToken);
}

void BfElementVisitor::Visit(BfMethodDeclaration* methodDeclaration)
{
	Visit(methodDeclaration->ToBase());

	VisitChild(methodDeclaration->mAttributes);	
	VisitChild(methodDeclaration->mProtectionSpecifier);
	VisitChild(methodDeclaration->mReadOnlySpecifier);
	VisitChild(methodDeclaration->mStaticSpecifier);

	VisitChild(methodDeclaration->mReturnAttributes);
	VisitChild(methodDeclaration->mExternSpecifier);
	VisitChild(methodDeclaration->mVirtualSpecifier); // either 'virtual', 'override', or 'abstract'
	VisitChild(methodDeclaration->mNewSpecifier);
	VisitChild(methodDeclaration->mMixinSpecifier);
	VisitChild(methodDeclaration->mPartialSpecifier);
	VisitChild(methodDeclaration->mMutSpecifier);
	VisitChild(methodDeclaration->mReturnType);
	VisitChild(methodDeclaration->mExplicitInterface);
	VisitChild(methodDeclaration->mExplicitInterfaceDotToken);
	VisitChild(methodDeclaration->mNameNode);
	VisitChild(methodDeclaration->mOpenParen);
	for (auto& param : methodDeclaration->mParams)
		VisitChild(param);
	for (auto& comma : methodDeclaration->mCommas)
		VisitChild(comma);	
	VisitChild(methodDeclaration->mCloseParen);
	VisitChild(methodDeclaration->mGenericParams);
	VisitChild(methodDeclaration->mGenericConstraintsDeclaration);
	VisitChild(methodDeclaration->mEndSemicolon);
	VisitChild(methodDeclaration->mFatArrowToken);
	VisitChild(methodDeclaration->mBody);
}

void BfElementVisitor::Visit(BfOperatorDeclaration* operatorDeclaration)
{
	Visit(operatorDeclaration->ToBase());

	VisitChild(operatorDeclaration->mExplicitToken);
	VisitChild(operatorDeclaration->mOperatorToken);
	VisitChild(operatorDeclaration->mOpTypeToken);
}

void BfElementVisitor::Visit(BfPropertyMethodDeclaration* propertyDeclaration)
{
	Visit(propertyDeclaration->ToBase());

	VisitChild(propertyDeclaration->mAttributes);
	VisitChild(propertyDeclaration->mProtectionSpecifier);
	VisitChild(propertyDeclaration->mNameNode);
	VisitChild(propertyDeclaration->mMutSpecifier);
	VisitChild(propertyDeclaration->mBody);
}

void BfElementVisitor::Visit(BfPropertyBodyExpression* propertyBodyExpression)
{
	Visit(propertyBodyExpression->ToBase());

	VisitChild(propertyBodyExpression->mFatTokenArrow);	
}

void BfElementVisitor::Visit(BfPropertyDeclaration* propertyDeclaration)
{
	Visit(propertyDeclaration->ToBase());

	VisitChild(propertyDeclaration->mAttributes);	
	VisitChild(propertyDeclaration->mProtectionSpecifier);
	VisitChild(propertyDeclaration->mStaticSpecifier);

	VisitChild(propertyDeclaration->mVirtualSpecifier); // either 'virtual', 'override', or 'abstract'
	VisitChild(propertyDeclaration->mExplicitInterface);
	VisitChild(propertyDeclaration->mExplicitInterfaceDotToken);
	
	if (auto block = BfNodeDynCast<BfBlock>(propertyDeclaration->mDefinitionBlock))
	{
		VisitChild(block->mOpenBrace);
		for (auto& method : propertyDeclaration->mMethods)
			VisitChild(method);		
		VisitChild(block->mCloseBrace);
	}
	else
	{
		VisitChild(propertyDeclaration->mDefinitionBlock);
		for (auto& method : propertyDeclaration->mMethods)
			VisitChild(method);
	}
}

void BfElementVisitor::Visit(BfIndexerDeclaration* indexerDeclaration)
{
	Visit(indexerDeclaration->ToBase());

	VisitChild(indexerDeclaration->mThisToken);
	VisitChild(indexerDeclaration->mOpenBracket);
	for (auto& param : indexerDeclaration->mParams)
		VisitChild(param);
	for (auto& comma : indexerDeclaration->mCommas)
		VisitChild(comma);
	VisitChild(indexerDeclaration->mCloseBracket);
}

void BfElementVisitor::Visit(BfFieldDeclaration* fieldDeclaration)
{
	Visit(fieldDeclaration->ToBase());

	VisitChild(fieldDeclaration->mAttributes);	
	VisitChild(fieldDeclaration->mProtectionSpecifier);
	VisitChild(fieldDeclaration->mStaticSpecifier);

	VisitChild(fieldDeclaration->mPrecedingComma);
	VisitChild(fieldDeclaration->mConstSpecifier);
	VisitChild(fieldDeclaration->mReadOnlySpecifier);
	VisitChild(fieldDeclaration->mVolatileSpecifier);
	VisitChild(fieldDeclaration->mNewSpecifier);
	VisitChild(fieldDeclaration->mExternSpecifier);
	VisitChild(fieldDeclaration->mTypeRef);
	VisitChild(fieldDeclaration->mNameNode);
	VisitChild(fieldDeclaration->mEqualsNode);
	VisitChild(fieldDeclaration->mInitializer);
	VisitChild(fieldDeclaration->mFieldDtor);
}

void BfElementVisitor::Visit(BfEnumCaseDeclaration* enumCaseDeclaration)
{
	Visit(enumCaseDeclaration->ToBase());

	VisitChild(enumCaseDeclaration->mCaseToken);
	for (auto& entry : enumCaseDeclaration->mEntries)
		VisitChild(entry);
	for (auto& comma : enumCaseDeclaration->mCommas)
		VisitChild(comma);	
}

void BfElementVisitor::Visit(BfFieldDtorDeclaration* fieldDtorDeclaration)
{
	Visit(fieldDtorDeclaration->ToBase());

	VisitChild(fieldDtorDeclaration->mTildeToken);
	VisitChild(fieldDtorDeclaration->mBody);
	VisitChild(fieldDtorDeclaration->mNextFieldDtor);
}

void BfElementVisitor::Visit(BfTypeDeclaration* typeDeclaration)
{
	Visit(typeDeclaration->ToBase());

	VisitChild(typeDeclaration->mAttributes);
	VisitChild(typeDeclaration->mAbstractSpecifier);
	VisitChild(typeDeclaration->mSealedSpecifier);	
	VisitChild(typeDeclaration->mProtectionSpecifier);
	VisitChild(typeDeclaration->mStaticSpecifier);
	VisitChild(typeDeclaration->mPartialSpecifier);
	VisitChild(typeDeclaration->mTypeNode);
	VisitChild(typeDeclaration->mNameNode);
	VisitChild(typeDeclaration->mColonToken);
	for (auto& baseClass : typeDeclaration->mBaseClasses)
		VisitChild(baseClass);
	for (auto& comma : typeDeclaration->mBaseClassCommas)
		VisitChild(comma);	

	VisitChild(typeDeclaration->mGenericParams);
	VisitChild(typeDeclaration->mGenericConstraintsDeclaration);

	VisitChild(typeDeclaration->mDefineNode);
	/*if (typeDeclaration->mDefineBlock != NULL)
	{
		for (auto& member : *typeDeclaration->mDefineBlock)
			VisitChild(member);
	}*/
	
}

void BfElementVisitor::Visit(BfTypeAliasDeclaration* typeDeclaration)
{
	Visit(typeDeclaration->ToBase());

	VisitChild(typeDeclaration->mEqualsToken);
	VisitChild(typeDeclaration->mAliasToType);
	VisitChild(typeDeclaration->mEndSemicolon);
}

void BfElementVisitor::Visit(BfUsingDirective* usingDirective)
{
	Visit(usingDirective->ToBase());

	VisitChild(usingDirective->mUsingToken);	
	VisitChild(usingDirective->mNamespace);
}

void BfElementVisitor::Visit(BfUsingStaticDirective * usingDirective)
{
	Visit(usingDirective->ToBase());

	VisitChild(usingDirective->mUsingToken);
	VisitChild(usingDirective->mStaticToken);
	VisitChild(usingDirective->mTypeRef);
}

void BfElementVisitor::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	Visit(namespaceDeclaration->ToBase());

	VisitChild(namespaceDeclaration->mNamespaceNode);
	VisitChild(namespaceDeclaration->mNameNode);
	VisitChild(namespaceDeclaration->mBlock);
}

void BfElementVisitor::Visit(BfBlock* block)
{
	Visit(block->ToBase());

	VisitChild(block->mOpenBrace);
	VisitMembers(block);
	VisitChild(block->mCloseBrace);
}

void BfElementVisitor::Visit(BfRootNode* rootNode)
{
	Visit(rootNode->ToBase());

	VisitMembers(rootNode);
}

void BfElementVisitor::Visit(BfInlineAsmStatement* asmStmt)
{
	Visit(asmStmt->ToBase());
}

void BfElementVisitor::Visit(BfInlineAsmInstruction* asmInst)
{
	Visit(asmInst->ToBase());
}