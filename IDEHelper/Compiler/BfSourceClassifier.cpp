#include "BfSourceClassifier.h"
#include "BfParser.h"

USING_NS_BF;

BfSourceClassifier::BfSourceClassifier(BfParser* bfParser, CharData* charData)
{
	mParser = bfParser;
	mCharData = charData;
	mSkipMethodInternals = false;	
	mSkipTypeDeclarations = false;
	mSkipAttributes = false;
	mIsSideChannel = false;
	mPreserveFlags = false;
	mClassifierPassId = 0;
	mEnabled = true;
	mPrevNode = NULL;
	mCurMember = NULL;
}

void BfSourceClassifier::ModifyFlags(BfAstNode* node, uint8 andFlags, uint8 orFlags)
{
	if (node != NULL)
	{		
		ModifyFlags(node->GetSrcStart(), node->GetSrcEnd(), andFlags, orFlags);
	}
}

void BfSourceClassifier::ModifyFlags(int startPos, int endPos, uint8 andFlags, uint8 orFlags)
{
	if (!mEnabled)
		return;

	endPos = std::min(endPos, mParser->mOrigSrcLength);
	for (int i = startPos; i < endPos; i++)
	{			
		mCharData[i].mDisplayPassId = mClassifierPassId;		
		mCharData[i].mDisplayFlags = (mCharData[i].mDisplayFlags & andFlags) | orFlags;
	}
}

void BfSourceClassifier::SetElementType(BfAstNode* node, BfSourceElementType elementType)
{
	if (node != NULL)
	{
		SetElementType(node->GetSrcStart(), node->GetSrcEnd(), elementType);
	}
}

void BfSourceClassifier::SetElementType(int startPos, int endPos, BfSourceElementType elementType)
{	
	if (!mEnabled)
		return;

	endPos = BF_MIN(endPos, mParser->mOrigSrcLength);
	for (int i = startPos; i < endPos; i++)
	{		
		mCharData[i].mDisplayPassId = mClassifierPassId;
		mCharData[i].mDisplayTypeId = (uint8)elementType;	
	}
}

void BfSourceClassifier::VisitMembers(BfBlock* node)
{
	mPrevNode = NULL;
	for (auto& childNodeRef : *node)
	{		
		BfAstNode* child = childNodeRef;
		child->Accept(this);
		mPrevNode = child; 
	}
}

bool BfSourceClassifier::IsInterestedInMember(BfAstNode* node, bool forceSkip)
{	
	if ((mSkipMethodInternals || forceSkip) && (mParser->mCursorIdx != -1) &&
		(!node->Contains(mParser->mCursorIdx, 1, 0)))
		return false;
	return true;
}

void BfSourceClassifier::HandleLeafNode(BfAstNode* node)
{
	if (!mEnabled)
		return;

	int nodeStart = node->GetSrcStart();
	int srcStart = nodeStart;	
	int triviaStart = node->GetTriviaStart();
	if (triviaStart != -1)
	{
		srcStart = triviaStart;
		
		if ((mIsSideChannel) && (mPrevNode != NULL))
			srcStart = std::max(mPrevNode->GetSrcEnd(), srcStart);
	}
	
	if (nodeStart != srcStart)
		SetElementType(srcStart, nodeStart, BfSourceElementType_Normal);
	//SetElementType(srcStart, node->GetSrcEnd(), BfSourceElementType_Normal);
	if (!mPreserveFlags)
		ModifyFlags(srcStart, node->GetSrcEnd(), ~BfSourceElementFlag_CompilerFlags_Mask, 0);
}

void BfSourceClassifier::Visit(BfAstNode* node)
{
	
}

void BfSourceClassifier::Visit(BfErrorNode* errorNode)
{
	//Visit(errorNode->ToBase());
	VisitChildNoRef(errorNode->mRefNode);
}

void BfSourceClassifier::Visit(BfFieldDeclaration* fieldDecl)
{
	if (!IsInterestedInMember(fieldDecl))
		return;
	
	BfElementVisitor::Visit(fieldDecl);

	VisitChild(fieldDecl->mConstSpecifier);
	VisitChild(fieldDecl->mReadOnlySpecifier);
	VisitChild(fieldDecl->mTypeRef);
	VisitChild(fieldDecl->mNameNode);
}

void BfSourceClassifier::Visit(BfFieldDtorDeclaration* fieldDtorDecl)
{
	Visit(fieldDtorDecl->ToBase());

	BfElementVisitor::Visit(fieldDtorDecl);

	if (fieldDtorDecl->mTildeToken != NULL)
		SetElementType(fieldDtorDecl->mTildeToken, BfSourceElementType_Method);
}

void BfSourceClassifier::Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection)
{
	HandleLeafNode(preprocesorIgnoredSection);

	Visit(preprocesorIgnoredSection->ToBase());

	SetElementType(preprocesorIgnoredSection, BfSourceElementType_Comment);
}

void BfSourceClassifier::Visit(BfPreprocessorNode* preprocessorNode)
{
	HandleLeafNode(preprocessorNode);

	if (!mPreserveFlags)
		ModifyFlags(preprocessorNode, ~BfSourceElementFlag_CompilerFlags_Mask, 0);
	SetElementType(preprocessorNode, BfSourceElementType_Normal);
	
	Visit(preprocessorNode->ToBase());	
}

void BfSourceClassifier::Visit(BfCommentNode* commentNode)
{	
	HandleLeafNode(commentNode);

	Visit(commentNode->ToBase());

	SetElementType(commentNode, BfSourceElementType_Comment);
}

void BfSourceClassifier::Visit(BfAttributeDirective* attributeDirective)
{
	if (mSkipAttributes)
		return;

	// Skip?
	{
		if (auto typeDeclaration = BfNodeDynCast<BfTypeDeclaration>(mCurMember))
		{
			if (typeDeclaration->mAttributes == attributeDirective)
				return;
		}

		if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(mCurMember))
		{
			if (methodDecl->mAttributes == attributeDirective)
				return;
		}

		if (auto propDecl = BfNodeDynCast<BfPropertyDeclaration>(mCurMember))
		{
			if (propDecl->mAttributes == attributeDirective)
				return;

			for (auto methodDeclaration : propDecl->mMethods)
			{
				if (methodDeclaration->mAttributes == attributeDirective)
					return;
			}
		}
	}

	BfElementVisitor::Visit(attributeDirective);	

	VisitChild(attributeDirective->mAttrCloseToken);

	VisitChild(attributeDirective->mAttrOpenToken); // Either [ or ,
	VisitChild(attributeDirective->mAttrCloseToken);

	if (attributeDirective->mAttributeTargetSpecifier != NULL)
	{
		VisitChild(attributeDirective->mAttributeTargetSpecifier->mTargetToken);
		VisitChild(attributeDirective->mAttributeTargetSpecifier->mColonToken);
	}

	VisitChild(attributeDirective->mAttributeTypeRef);
	VisitChild(attributeDirective->mCtorOpenParen);
	VisitChild(attributeDirective->mCtorCloseParen);
	for (auto& arg : attributeDirective->mArguments)
		VisitChild(arg);
	for (auto& comma : attributeDirective->mCommas)
		VisitChild(comma);	

	VisitChild(attributeDirective->mNextAttribute);
}

void BfSourceClassifier::Visit(BfIdentifierNode* identifier)
{
	HandleLeafNode(identifier);

	Visit(identifier->ToBase());

	SetElementType(identifier, BfSourceElementType_Identifier);
}

void BfSourceClassifier::Visit(BfQualifiedNameNode* qualifiedName)
{	
	Visit((BfAstNode*)qualifiedName);

	VisitChild(qualifiedName->mLeft);
	VisitChild(qualifiedName->mDot);
	VisitChild(qualifiedName->mRight);
}

void BfSourceClassifier::Visit(BfThisExpression* thisExpr)
{
	HandleLeafNode(thisExpr);
	Visit((BfAstNode*)thisExpr);
}

void BfSourceClassifier::Visit(BfBaseExpression* baseExpr)
{
	HandleLeafNode(baseExpr);
	Visit((BfAstNode*)baseExpr);
}

void BfSourceClassifier::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	Visit((BfAstNode*)memberRefExpr);
	VisitChild(memberRefExpr->mTarget);
	VisitChild(memberRefExpr->mDotToken);
	VisitChild(memberRefExpr->mMemberName);
}

void BfSourceClassifier::Visit(BfNamedTypeReference* typeRef)
{
	HandleLeafNode(typeRef);

	Visit(typeRef->ToBase());

	//auto identifier = typeRef->mNameNode;
	if (typeRef != NULL)
	{
		BfIdentifierNode* checkName = typeRef->mNameNode;
		while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkName))
		{
			SetElementType(qualifiedNameNode->mRight, BfSourceElementType_TypeRef);
			checkName = qualifiedNameNode->mLeft;
		}
		if (checkName != NULL)
			SetElementType(checkName, BfSourceElementType_TypeRef);
	}
}

void BfSourceClassifier::Visit(BfQualifiedTypeReference* qualifiedType)
{
	Visit((BfAstNode*)qualifiedType);

	VisitChild(qualifiedType->mLeft);
	VisitChild(qualifiedType->mDot);
	VisitChild(qualifiedType->mRight);
}

void BfSourceClassifier::Visit(BfRefTypeRef* typeRef)
{
	Visit((BfAstNode*)typeRef);

	VisitChild(typeRef->mRefToken);
	SetElementType(typeRef->mRefToken, BfSourceElementType_TypeRef);
	VisitChild(typeRef->mElementType);
}

void BfSourceClassifier::Visit(BfArrayTypeRef* arrayType)
{
	Visit((BfAstNode*)arrayType);

	VisitChild(arrayType->mElementType);
	VisitChild(arrayType->mOpenBracket);
	for (auto& param : arrayType->mParams)
		VisitChild(param);
	VisitChild(arrayType->mCloseBracket);
}

void BfSourceClassifier::Visit(BfPointerTypeRef* pointerType)
{
	Visit((BfAstNode*)pointerType);

	VisitChild(pointerType->mElementType);
	VisitChild(pointerType->mStarNode);
}

void BfSourceClassifier::Visit(BfGenericInstanceTypeRef* genericInstTypeRef)
{
	BfElementVisitor::Visit(genericInstTypeRef);

	VisitChild(genericInstTypeRef->mElementType);
	VisitChild(genericInstTypeRef->mOpenChevron);
	for (int i = 0; i < (int) genericInstTypeRef->mGenericArguments.size(); i++)
	{
		if (genericInstTypeRef->mCommas.mVals != NULL)
		{
			if ((i > 0) && (i - 1 < (int)genericInstTypeRef->mCommas.size()))
				VisitChild(genericInstTypeRef->mCommas[i - 1]);
		}
		VisitChild(genericInstTypeRef->mGenericArguments[i]);
	}
	VisitChild(genericInstTypeRef->mCloseChevron);
}

void BfSourceClassifier::Visit(BfLocalMethodDeclaration* methodDecl)
{
	if (IsInterestedInMember(methodDecl, true))
		BfElementVisitor::Visit(methodDecl);
}

void BfSourceClassifier::Visit(BfLiteralExpression* literalExpr)
{
	HandleLeafNode(literalExpr);

	Visit(literalExpr->ToBase());

	SetElementType(literalExpr, BfSourceElementType_Literal);
}

void BfSourceClassifier::Visit(BfTokenNode* tokenNode)
{
	HandleLeafNode(tokenNode);

	Visit(tokenNode->ToBase());
	
	if (BfTokenIsKeyword(tokenNode->GetToken()))
		SetElementType(tokenNode, BfSourceElementType_Keyword);
	else
		SetElementType(tokenNode, BfSourceElementType_Normal);
}

void BfSourceClassifier::Visit(BfInvocationExpression* invocationExpr)
{
	BfElementVisitor::Visit(invocationExpr);
	
	BfAstNode* target = invocationExpr->mTarget;
	if (target == NULL)
		return;

	if (auto scopedTarget = BfNodeDynCast<BfScopedInvocationTarget>(target))
	{
		target = scopedTarget->mTarget;
		VisitChild(scopedTarget->mScopeName);
	}

	BfAstNode* identifier = NULL;
	if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(target))
	{
		VisitChild(qualifiedName->mLeft);
		VisitChild(qualifiedName->mDot);
		identifier = qualifiedName->mRight;		
	}
	else if ((identifier = BfNodeDynCast<BfIdentifierNode>(target)))
	{
		// Leave as BfAttributedIdentifierNode if that's the case
		identifier = target;
	}
	else if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(target))
	{
		VisitChild(qualifiedName->mLeft);
		VisitChild(qualifiedName->mDot);
		identifier = qualifiedName->mRight;		
	}
	else if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(target))
	{
		VisitChild(memberRefExpr->mTarget);
		VisitChild(memberRefExpr->mDotToken);
		identifier = memberRefExpr->mMemberName;		
	}

	if (identifier != NULL)
	{
		if (auto attrIdentifier = BfNodeDynCast<BfAttributedIdentifierNode>(identifier))
		{
			VisitChild(attrIdentifier->mAttributes);
			identifier = attrIdentifier->mIdentifier;			
		}
		
		if (identifier != NULL)
			SetElementType(identifier, BfSourceElementType_Method);
	}
}

void BfSourceClassifier::Visit(BfIndexerExpression* indexerExpr)
{
	BfElementVisitor::Visit(indexerExpr);

	VisitChild(indexerExpr->mTarget);
	VisitChild(indexerExpr->mOpenBracket);
	for (int i = 0; i < (int) indexerExpr->mArguments.size(); i++)
	{
		if (i > 0)
			VisitChild(indexerExpr->mCommas[i - 1]);
		VisitChild(indexerExpr->mArguments[i]);
	}
	VisitChild(indexerExpr->mCloseBracket);
}

void BfSourceClassifier::Visit(BfConstructorDeclaration* ctorDeclaration)
{
	if (!IsInterestedInMember(ctorDeclaration))
		return;

	BfElementVisitor::Visit(ctorDeclaration);
	VisitChild(ctorDeclaration->mThisToken);

	auto identifier = ctorDeclaration->mThisToken;
	if (identifier == NULL)
		return;
	SetElementType(identifier, BfSourceElementType_Method);
}

void BfSourceClassifier::Visit(BfDestructorDeclaration* dtorDeclaration)
{
	BfElementVisitor::Visit(dtorDeclaration);

	VisitChild(dtorDeclaration->mTildeToken);
	VisitChild(dtorDeclaration->mThisToken);

	auto identifier = dtorDeclaration->mThisToken;
	if (identifier == NULL)
		return;
	SetElementType(identifier, BfSourceElementType_Method);

	identifier = dtorDeclaration->mTildeToken;
	if (identifier == NULL)
		return;
	SetElementType(identifier, BfSourceElementType_Method);
}

void BfSourceClassifier::Visit(BfMethodDeclaration* methodDeclaration)
{	
	if (!IsInterestedInMember(methodDeclaration))
		return;

	SetAndRestoreValue<BfAstNode*> prevMember(mCurMember, methodDeclaration);

	BfElementVisitor::Visit(methodDeclaration);	
	
	SetElementType(methodDeclaration->mNameNode, BfSourceElementType_Method);

	if (methodDeclaration->mGenericParams != NULL)
	{
		for (auto& genericParam : methodDeclaration->mGenericParams->mGenericParams)
		{
			BfIdentifierNode* typeRef = genericParam;
			SetElementType(typeRef, BfSourceElementType_TypeRef);
		}
	}

	if (methodDeclaration->mGenericConstraintsDeclaration != NULL)
	{
		for (auto constraintNode : methodDeclaration->mGenericConstraintsDeclaration->mGenericConstraints)
		{
			if (auto genericConstraint = BfNodeDynCast<BfGenericConstraint>(constraintNode))
			{
				BfTypeReference* typeRef = genericConstraint->mTypeRef;
				if (typeRef != NULL)
					SetElementType(typeRef, BfSourceElementType_TypeRef);
			}
		}
	}
}

void BfSourceClassifier::Visit(BfPropertyMethodDeclaration* propertyMethodDeclaration)
{
	if ((propertyMethodDeclaration->mBody != NULL) && (!IsInterestedInMember(propertyMethodDeclaration->mBody)))
		return;

	BfElementVisitor::Visit(propertyMethodDeclaration);
}

void BfSourceClassifier::Visit(BfPropertyDeclaration* propertyDeclaration)
{
	SetAndRestoreValue<BfAstNode*> prevMember(mCurMember, propertyDeclaration);

	BfElementVisitor::Visit(propertyDeclaration);

	for (auto methodDeclaration : propertyDeclaration->mMethods)
	{
		if ((methodDeclaration != NULL) && (methodDeclaration->mNameNode != NULL))
			SetElementType(methodDeclaration->mNameNode, BfSourceElementType_Method);
	}
}

void BfSourceClassifier::Visit(BfTypeDeclaration* typeDeclaration)
{
	if (typeDeclaration->mIgnoreDeclaration)
		return;

	SetAndRestoreValue<BfAstNode*> prevMember(mCurMember, typeDeclaration);

	if (mSkipTypeDeclarations)
	{
		if (auto defineBlock = BfNodeDynCast<BfBlock>(typeDeclaration->mDefineNode))
		{
			// Clear out any potential "fail after" errors on the closing brace-
			//  Can happen when we don't close out a namespace, for example
			if (defineBlock->mCloseBrace != NULL)
				VisitChild(defineBlock->mCloseBrace);
		}
		return;
	}

	Handle(typeDeclaration);
}

void BfSourceClassifier::Handle(BfTypeDeclaration* typeDeclaration)
{
	if (mParser->mCursorIdx != -1)
	{
		// This is to fix a case where we are typing out a type name, so an "actualTypeDef" will not be found during autocomplete
		//  and therefore we will not process the attributes. Removing this will cause classify flashing while typing
		SetAndRestoreValue<bool> prevSkipAttributes(mSkipAttributes, true);
		BfElementVisitor::Visit(typeDeclaration);
	}
	else
		BfElementVisitor::Visit(typeDeclaration);

	llvm::SmallVector<BfTypeReference*, 2> mBaseClasses;
	llvm::SmallVector<BfAstNode*, 2> mBaseClassCommas;
	
	if (typeDeclaration->mGenericParams != NULL)
	{
		for (auto& genericParam : typeDeclaration->mGenericParams->mGenericParams)
		{
			BfIdentifierNode* typeRef = genericParam;
			SetElementType(typeRef, BfSourceElementType_TypeRef);
		}
	}

	if (typeDeclaration->mGenericConstraintsDeclaration != NULL)
	{
		for (auto constraintNode : typeDeclaration->mGenericConstraintsDeclaration->mGenericConstraints)
		{
			auto genericConstraint = BfNodeDynCast<BfGenericConstraint>(constraintNode);

			BfTypeReference* typeRef = genericConstraint->mTypeRef;
			if (typeRef != NULL)
				SetElementType(typeRef, BfSourceElementType_TypeRef);
		}
	}

	auto typeRef = typeDeclaration->mNameNode;
	if (typeRef != NULL)
		SetElementType(typeRef, BfSourceElementType_TypeRef);
}

void BfSourceClassifier::MarkSkipped(int startPos, int endPos)
{
	for (int i = startPos; i < endPos; i++)
	{
		mCharData[i].mDisplayPassId = BfSourceDisplayId_SkipResult;
	}
}

void BfSourceClassifier::MarkSkipped(BfAstNode* node)
{
	MarkSkipped(node->GetSrcStart(), node->GetSrcEnd());
}

void BfSourceClassifier::Visit(BfTypeAliasDeclaration* typeDeclaration)
{
	if (typeDeclaration->mIgnoreDeclaration)
		return;

	BfElementVisitor::Visit(typeDeclaration);	
}

void BfSourceClassifier::Visit(BfUsingDirective* usingDirective)
{
	BfElementVisitor::Visit(usingDirective);

	auto checkIdentifier = usingDirective->mNamespace;
	if (checkIdentifier != NULL)
	{
		while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkIdentifier))
		{
			SetElementType(qualifiedNameNode->mRight, BfSourceElementType_Namespace);
			checkIdentifier = qualifiedNameNode->mLeft;
		}

		if (checkIdentifier != NULL)
			SetElementType(checkIdentifier, BfSourceElementType_Namespace);
	}
}

void BfSourceClassifier::Visit(BfUsingStaticDirective* usingDirective)
{
	BfElementVisitor::Visit(usingDirective);	
}

void BfSourceClassifier::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	BfElementVisitor::Visit(namespaceDeclaration);

	auto checkIdentifier = namespaceDeclaration->mNameNode;
	if (checkIdentifier != NULL)
	{
		while (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(checkIdentifier))
		{
			SetElementType(qualifiedNameNode->mRight, BfSourceElementType_Namespace);
			checkIdentifier = qualifiedNameNode->mLeft;
		}

		if (checkIdentifier != NULL)
			SetElementType(checkIdentifier, BfSourceElementType_Namespace);
	}
}

bool BfSourceClassifier::WantsSkipParentMethod(BfAstNode* node)
{
	if (!mSkipMethodInternals)
		return false;
	
#ifdef BF_AST_HAS_PARENT_MEMBER
	if (node->mParent->IsA<BfMethodDeclaration>())
	{
		BF_ASSERT(node->mParent == mCurMember);
	}
	if (auto propDecl = BfNodeDynCast<BfPropertyDeclaration>(node->mParent))
	{
		BF_ASSERT(node->mParent == mCurMember);
	}
#endif

	if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(mCurMember))
	{
		if (methodDecl->mBody == node)
			return true;
	}

	if (auto propDecl = BfNodeDynCast<BfPropertyDeclaration>(mCurMember))
	{
		for (auto methodDeclaration : propDecl->mMethods)
		{
			if (node == methodDeclaration->mBody)
			 	return true;
		}
	}

	return false;
}

void BfSourceClassifier::Visit(BfGenericConstraintsDeclaration* genericConstraints)
{
	/*if (WantsSkipParentMethod(genericConstraints))
		return;*/
	BfElementVisitor::Visit(genericConstraints);
}

void BfSourceClassifier::Visit(BfBlock* block)
{		
	if (WantsSkipParentMethod(block))
		return;
	if (block->mOpenBrace != NULL)
		Visit(block->mOpenBrace);
	if (block->mCloseBrace != NULL)
		Visit(block->mCloseBrace);
	VisitMembers(block);
}

void BfSourceClassifier::Visit(BfRootNode* rootNode)
{
	// Clear off the flags at the end
	ModifyFlags(mParser->mRootNode->GetSrcEnd(), mParser->mOrigSrcLength, 0, 0);

	VisitMembers(rootNode);	
}

void BfSourceClassifier::Visit(BfInlineAsmStatement* asmStmt)
{
	if (asmStmt->mOpenBrace != NULL)
		Visit(asmStmt->mOpenBrace);
	if (asmStmt->mCloseBrace != NULL)
		Visit(asmStmt->mCloseBrace);
	
	//VisitMembers(asmStmt);
}

void BfSourceClassifier::Visit(BfInlineAsmInstruction* asmInst)
{
	//VisitMembers(asmInst);
}