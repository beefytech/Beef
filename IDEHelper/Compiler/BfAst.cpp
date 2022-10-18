#define BF_AST_DO_IMPL

#include "BfAst.h"
#include "BfParser.h"
#include "BfSystem.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

BfStructuralVisitor::BfStructuralVisitor()
{
	std::hash<std::string>();

	mCapturingChildRef = false;
	mCurChildRef = NULL;
}

void BfStructuralVisitor::VisitMembers(BfBlock* node)
{
	for (auto& child : *node)
	{
		child->Accept(this);
	}
}

void BfStructuralVisitor::VisitChildNoRef(BfAstNode* node)
{
	mCurChildRef = NULL;
	node->Accept(this);
	mCurChildRef = NULL;
}

void BfStructuralVisitor::DoVisitChild(BfAstNode*& node)
{
	if (node == NULL)
		return;
	mCurChildRef = &node;
	node->Accept(this);
	mCurChildRef = NULL;
}

void BfStructuralVisitor::AssertValidChildAddr(BfAstNode** nodeRef)
{
	//auto bfSource = (*nodeRef)->mSource;
	//BF_ASSERT(bfSource->mAlloc.ContainsPtr(nodeRef));
}

void BfStructuralVisitor::Visit(BfLabeledBlock* labeledBlock)
{
	Visit(labeledBlock->ToBase());
}

void BfStructuralVisitor::Visit(BfErrorNode* bfErrorNode)
{
	Visit(bfErrorNode->ToBase());
}

void BfStructuralVisitor::Visit(BfScopeNode* scopeNode)
{
	Visit(scopeNode->ToBase());
}

void BfStructuralVisitor::Visit(BfNewNode* newNode)
{
	Visit(newNode->ToBase());
}

void BfStructuralVisitor::Visit(BfTypedValueExpression* typedValueExpr)
{
	Visit(typedValueExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfCommentNode* commentNode)
{
	Visit(commentNode->ToBase());
}

void BfStructuralVisitor::Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection)
{
	Visit(preprocesorIgnoredSection->ToBase());
}

void BfStructuralVisitor::Visit(BfPreprocessorNode* preprocessorNode)
{
	Visit(preprocessorNode->ToBase());
}

void BfStructuralVisitor::Visit(BfPreprocessorDefinedExpression* definedExpr)
{
	Visit(definedExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfAttributeDirective* attributeDirective)
{
	Visit(attributeDirective->ToBase());
}

void BfStructuralVisitor::Visit(BfGenericParamsDeclaration* genericParams)
{
	Visit(genericParams->ToBase());
}

void BfStructuralVisitor::Visit(BfGenericOperatorConstraint* genericConstraints)
{
	Visit(genericConstraints->ToBase());
}

void BfStructuralVisitor::Visit(BfGenericConstraintsDeclaration* genericConstraints)
{
	Visit(genericConstraints->ToBase());
}

void BfStructuralVisitor::Visit(BfGenericArgumentsNode* genericArgumentsNode)
{
	Visit(genericArgumentsNode->ToBase());
}

void BfStructuralVisitor::Visit(BfStatement* stmt)
{
	Visit(stmt->ToBase());
}

void BfStructuralVisitor::Visit(BfAttributedStatement* attribStmt)
{
	Visit(attribStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfLabelableStatement* labelableStmt)
{
	Visit(labelableStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfExpression* expr)
{
	Visit(expr->ToBase());
}

void BfStructuralVisitor::Visit(BfExpressionStatement* exprStmt)
{
	Visit(exprStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfNamedExpression* namedExpr)
{
	Visit(namedExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfAttributedExpression* attribExpr)
{
	Visit(attribExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfEmptyStatement* emptyStmt)
{
	Visit(emptyStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfTokenNode* tokenNode)
{
	Visit(tokenNode->ToBase());
}

void BfStructuralVisitor::Visit(BfTokenPairNode* tokenPairNode)
{
	Visit(tokenPairNode->ToBase());
}

void BfStructuralVisitor::Visit(BfUsingSpecifierNode* usingSpecifier)
{
	Visit(usingSpecifier->ToBase());
}

void BfStructuralVisitor::Visit(BfLiteralExpression* literalExpr)
{
	Visit(literalExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfStringInterpolationExpression* stringInterpolationExpression)
{
	Visit(stringInterpolationExpression->ToBase());
}

void BfStructuralVisitor::Visit(BfIdentifierNode* identifierNode)
{
	Visit(identifierNode->ToBase());
}

void BfStructuralVisitor::Visit(BfAttributedIdentifierNode* attrIdentifierNode)
{
	Visit(attrIdentifierNode->ToBase());
}

void BfStructuralVisitor::Visit(BfQualifiedNameNode* nameNode)
{
	Visit(nameNode->ToBase());
}

void BfStructuralVisitor::Visit(BfThisExpression* thisExpr)
{
	Visit(thisExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfBaseExpression* baseExpr)
{
	Visit(baseExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfMixinExpression* mixinExpr)
{
	Visit(mixinExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfSizedArrayCreateExpression* createExpr)
{
	Visit(createExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfInitializerExpression* initExpr)
{
	Visit(initExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfCollectionInitializerExpression* collectionInitExpr)
{
	Visit(collectionInitExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfNamedTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfQualifiedTypeReference* qualifiedTypeRef)
{
	Visit(qualifiedTypeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfDotTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfVarTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfVarRefTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfLetTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfConstTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfConstExprTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfRefTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfModifiedTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfArrayTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfGenericInstanceTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfTupleTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfDelegateTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfExprModTypeRef* declTypeRef)
{
	Visit(declTypeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfPointerTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfNullableTypeRef* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfStructuralVisitor::Visit(BfVariableDeclaration* varDecl)
{
	Visit(varDecl->ToBase());
}

void BfStructuralVisitor::Visit(BfLocalMethodDeclaration* methodDecl)
{
	Visit(methodDecl->ToBase());
}

void BfStructuralVisitor::Visit(BfParameterDeclaration* paramDecl)
{
	Visit(paramDecl->ToBase());
}

void BfStructuralVisitor::Visit(BfTypeAttrExpression* typeAttrExpr)
{
	Visit(typeAttrExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfTypeOfExpression* typeOfExpr)
{
	Visit(typeOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfSizeOfExpression* sizeOfExpr)
{
	Visit(sizeOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfAlignOfExpression* alignOfExpr)
{
	Visit(alignOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfStrideOfExpression* strideOfExpr)
{
	Visit(strideOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfOffsetOfExpression* offsetOfExpr)
{
	Visit(offsetOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfNameOfExpression* nameOfExpr)
{
	Visit(nameOfExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfIsConstExpression* isConstExpr)
{
	Visit(isConstExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfDefaultExpression* defaultExpr)
{
	Visit(defaultExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfUninitializedExpression* uninitializedExpr)
{
	Visit(uninitializedExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfCheckTypeExpression* checkTypeExpr)
{
	Visit(checkTypeExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfDynamicCastExpression* dynCastExpr)
{
	Visit(dynCastExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfCastExpression* castExpr)
{
	Visit(castExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfDelegateBindExpression* bindExpr)
{
	Visit(bindExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfLambdaBindExpression* lambdaBindExpr)
{
	Visit(lambdaBindExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfObjectCreateExpression* newExpr)
{
	Visit(newExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfBoxExpression* boxExpr)
{
	Visit(boxExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfThrowStatement* throwStmt)
{
	Visit(throwStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfDeleteStatement* deleteStmt)
{
	Visit(deleteStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfScopedInvocationTarget* scopedTarget)
{
	Visit(scopedTarget->ToBase());
}

void BfStructuralVisitor::Visit(BfInvocationExpression* invocationExpr)
{
	Visit(invocationExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfEnumCaseBindExpression* caseBindExpr)
{
	Visit(caseBindExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfCaseExpression* caseExpr)
{
	Visit(caseExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfSwitchCase* switchCase)
{
	Visit(switchCase->ToBase());
}

void BfStructuralVisitor::Visit(BfWhenExpression* whenExpr)
{
	Visit(whenExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfSwitchStatement* switchStmt)
{
	Visit(switchStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfTryStatement* tryStmt)
{
	Visit(tryStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfCatchStatement* catchStmt)
{
	Visit(catchStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfFinallyStatement* finallyStmt)
{
	Visit(finallyStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfCheckedStatement* checkedStmt)
{
	Visit(checkedStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfUncheckedStatement* uncheckedStmt)
{
	Visit(uncheckedStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfIfStatement* ifStmt)
{
	Visit(ifStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfDeferStatement* deferStmt)
{
	Visit(deferStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfReturnStatement* returnStmt)
{
	Visit(returnStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfYieldStatement* yieldStmt)
{
	Visit(yieldStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfUsingStatement* whileStmt)
{
	Visit(whileStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfDoStatement* doStmt)
{
	Visit(doStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfRepeatStatement* repeatStmt)
{
	Visit(repeatStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfWhileStatement* whileStmt)
{
	Visit(whileStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfBreakStatement* breakStmt)
{
	Visit(breakStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfContinueStatement* continueStmt)
{
	Visit(continueStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfFallthroughStatement* fallthroughStmt)
{
	Visit(fallthroughStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfForStatement* forStmt)
{
	Visit(forStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfForEachStatement* forEachStmt)
{
	Visit(forEachStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfConditionalExpression* condExpr)
{
	Visit(condExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfAssignmentExpression* assignExpr)
{
	Visit(assignExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfParenthesizedExpression* parenExpr)
{
	Visit(parenExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfTupleExpression* tupleExpr)
{
	Visit(tupleExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	Visit(memberRefExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfIndexerExpression* indexerExpr)
{
	Visit(indexerExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfUnaryOperatorExpression* binOpExpr)
{
	Visit(binOpExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	Visit(binOpExpr->ToBase());
}

void BfStructuralVisitor::Visit(BfConstructorDeclaration* ctorDeclaration)
{
	Visit(ctorDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfAutoConstructorDeclaration* ctorDeclaration)
{
	Visit(ctorDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfDestructorDeclaration* dtorDeclaration)
{
	Visit(dtorDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfMethodDeclaration* methodDeclaration)
{
	Visit(methodDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfOperatorDeclaration* operatorDeclaration)
{
	Visit(operatorDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfPropertyMethodDeclaration* propertyMethodDeclaration)
{
	Visit(propertyMethodDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfPropertyBodyExpression* propertyBodyExpression)
{
	Visit(propertyBodyExpression->ToBase());
}

void BfStructuralVisitor::Visit(BfPropertyDeclaration* propertyDeclaration)
{
	Visit(propertyDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfIndexerDeclaration* indexerDeclaration)
{
	Visit(indexerDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfFieldDeclaration* fieldDeclaration)
{
	Visit(fieldDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfEnumCaseDeclaration* enumCaseDeclaration)
{
	Visit(enumCaseDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfFieldDtorDeclaration* fieldDtorDeclaration)
{
	Visit(fieldDtorDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfTypeDeclaration* typeDeclaration)
{
	Visit(typeDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfTypeAliasDeclaration* typeDeclaration)
{
	Visit(typeDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfUsingDirective* usingDirective)
{
	Visit(usingDirective->ToBase());
}

void BfStructuralVisitor::Visit(BfUsingModDirective * usingDirective)
{
	Visit(usingDirective->ToBase());
}

void BfStructuralVisitor::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	Visit(namespaceDeclaration->ToBase());
}

void BfStructuralVisitor::Visit(BfBlock* block)
{
	Visit(block->ToBase());
}

void BfStructuralVisitor::Visit(BfUnscopedBlock* block)
{
	Visit(block->ToBase());
}

void BfStructuralVisitor::Visit(BfBlockExtension* block)
{
	BF_ASSERT("Shouldn't see this block, BfBlock::Iterator not being used?");
}

void BfStructuralVisitor::Visit(BfRootNode* rootNode)
{
	Visit(rootNode->ToBase());
}

void BfStructuralVisitor::Visit(BfInlineAsmStatement* asmStmt)
{
	Visit(asmStmt->ToBase());
}

void BfStructuralVisitor::Visit(BfInlineAsmInstruction* asmInst)
{
	Visit(asmInst->ToBase());
}

//////////////////////////////////////////////////////////////////////////

static Array<BfAstTypeInfo*> gTypes;
static Array<BfAstAcceptFunc> gAcceptFuncs;
static int sTypeCount;

BfAstTypeInfo::BfAstTypeInfo(const char* name, BfAstTypeInfo* baseType, BfAstAcceptFunc acceptFunc)
{
	mName = name;
	mBaseType = baseType;
	mAcceptFunc = acceptFunc;
	if (mBaseType != NULL)
	{
		mBaseType->mDerivedTypes.Add(this);
	}
	sTypeCount++;

#ifdef _DEBUG
	auto checkBase = mBaseType;
	while (checkBase != NULL)
	{
		if (checkBase == &BfAstNode::sTypeInfo)
			break;
		checkBase = checkBase->mBaseType;
	}
#endif
}

static void AddTypeInfo(BfAstTypeInfo* typeInfo)
{
	BF_ASSERT(typeInfo->mTypeId == 0);

	typeInfo->mTypeId = (uint8)gTypes.size();
	gTypes.Add(typeInfo);
	gAcceptFuncs.Add(typeInfo->mAcceptFunc);
	for (auto derivedType : typeInfo->mDerivedTypes)
		AddTypeInfo(derivedType);
	typeInfo->mFullDerivedCount = (uint8)(gTypes.size() - typeInfo->mTypeId - 1);
}

void BfAstTypeInfo::Init()
{
	if (!gTypes.IsEmpty())
		return;

	gTypes.Add(NULL);
	gAcceptFuncs.Add(NULL);
	AddTypeInfo(&BfAstNode::sTypeInfo);
}

BfIdentifierNode* Beefy::BfIdentifierCast(BfAstNode* node)
{
	if (node == NULL)
		return NULL;
	if (node->GetTypeId() == BfAttributedIdentifierNode::sTypeInfo.mTypeId)
		return ((BfAttributedIdentifierNode*)node)->mIdentifier;
	bool canCast = (uint)node->GetTypeId() - (uint)BfIdentifierNode::sTypeInfo.mTypeId <= (uint)BfIdentifierNode::sTypeInfo.mFullDerivedCount;
	return canCast ? (BfIdentifierNode*)node : NULL;
}

BfAstNode* Beefy::BfNodeToNonTemporary(BfAstNode* node)
{
	if (node == NULL)
		return NULL;
	if (node->GetTypeId() == BfNamedTypeReference::sTypeInfo.mTypeId)
	{
		auto namedTypeRef = (BfNamedTypeReference*)node;
		if (namedTypeRef->IsTemporary())
			return BfNodeToNonTemporary(namedTypeRef->mNameNode);
		return namedTypeRef;
	}
	if (!node->IsTemporary())
		return node;
	return NULL;
}

//////////////////////////////////////////////////////////////////////////

bool BfAstNode::IsMissingSemicolon()
{
	if (auto deferStmt = BfNodeDynCast<BfDeferStatement>(this))
	{
		if (BfNodeIsExact<BfBlock>(deferStmt->mTargetNode))
			return false;
	}
	if (auto stmt = BfNodeDynCast<BfCompoundStatement>(this))
	{
		if (auto repeatStmt = BfNodeDynCast<BfRepeatStatement>(this))
		{
			if (repeatStmt->mWhileToken == NULL)
				return false;
		}
		else
			return false;
	}
	if (auto attribExpr = BfNodeDynCastExact<BfAttributedStatement>(this))
		return (attribExpr->mStatement == NULL) || (attribExpr->mStatement->IsMissingSemicolon());

	if (auto stmt = BfNodeDynCast<BfStatement>(this))
		return stmt->mTrailingSemicolon == NULL;

	return false;
}

bool BfAstNode::IsExpression()
{
	if (auto deferStmt = BfNodeDynCast<BfDeferStatement>(this))
	{
		if (BfNodeIsExact<BfBlock>(deferStmt->mTargetNode))
			return false;
	}
	if (auto block = BfNodeDynCast<BfBlock>(this))
	{
		if (block->mChildArr.mSize == 0)
			return false;
		return block->mChildArr.GetLast()->IsExpression();
	}

	return IsA<BfExpression>();
}

bool BfAstNode::WantsWarning(int warningNumber)
{
	auto parserData = GetParserData();
	if (parserData == NULL)
		return true;
	int srcStart = GetSrcStart();
	return (!parserData->IsUnwarnedAt(this)) && (parserData->IsWarningEnabledAtSrcIndex(warningNumber, GetSrcStart()));
}

bool BfAstNode::LocationEquals(BfAstNode* otherNode)
{
	return (GetSourceData() == otherNode->GetSourceData()) &&
		(GetSrcStart() == otherNode->GetSrcStart()) &&
		(GetSrcEnd() == otherNode->GetSrcEnd());
}

bool BfAstNode::LocationEndEquals(BfAstNode* otherNode)
{
	return (GetSourceData() == otherNode->GetSourceData()) &&
		(GetSrcEnd() == otherNode->GetSrcEnd());
}

String BfAstNode::LocationToString()
{
	auto parserData = GetParserData();
	if (parserData == NULL)
		return String();

	String loc;

	int line = -1;
	int lineChar = -1;
	parserData->GetLineCharAtIdx(mSrcStart, line, lineChar);
	if (line != -1)
		loc += StrFormat("at line %d:%d", line + 1, lineChar + 1);
	loc += " in ";
	loc += parserData->mFileName;

	return loc;
}

void BfAstNode::Add(BfAstNode* bfAstNode)
{
#ifdef BF_AST_HAS_PARENT_MEMBER
	BF_ASSERT(bfAstNode->mParent == NULL);
	bfAstNode->mParent = this;
#endif

	if (!IsInitialized())
	{
		int childTriviaStart;
		int childSrcStart;
		int childSrcEnd;
		bfAstNode->GetSrcPositions(childTriviaStart, childSrcStart, childSrcEnd);
		Init(childTriviaStart, childSrcStart, childSrcEnd);
		return;
	}

#ifdef BF_AST_COMPACT
	int childTriviaStart;
	int childSrcStart;
	int childSrcEnd;
	bfAstNode->GetSrcPositions(childTriviaStart, childSrcStart, childSrcEnd);

	int prevTriviaStart;
	int prevSrcStart;
	int prevSrcEnd;
	GetSrcPositions(prevTriviaStart, prevSrcStart, prevSrcEnd);

	if (childTriviaStart < prevTriviaStart)
		SetTriviaStart(childTriviaStart);
	if (childSrcStart < prevSrcStart)
		SetSrcStart(childSrcStart);
	if (childSrcEnd > prevSrcEnd)
		SetSrcEnd(childSrcEnd);
#else
	BF_ASSERT(mSrcStart >= 0);
	BF_ASSERT(bfAstNode->mSrcStart >= 0);

	mSrcStart = BF_MIN(mSrcStart, bfAstNode->mSrcStart);
	mSrcEnd = BF_MAX(mSrcEnd, bfAstNode->mSrcEnd);
#endif
}

#ifdef BF_AST_COMPACT
BfAstInfo* BfAstNode::AllocAstInfo()
{
	return GetSource()->mAlloc.Alloc<BfAstInfo>();
}
#endif

void BfAstNode::RemoveSelf()
{
	//mParent->mChildren.Remove(this);
#ifdef BF_AST_HAS_PARENT_MEMBER
	mParent = NULL;
#endif
}

void BfAstNode::DeleteSelf()
{
	//if (mParent != NULL)
		//mParent->mChildren.Remove(this);
	delete this;
}

void BfAstNode::RemoveNextSibling()
{
	//mNext->RemoveSelf();
}

void BfAstNode::DeleteNextSibling()
{
	//mNext->DeleteSelf();
}

void BfAstNode::Init(BfParser* bfParser)
{
	BF_ASSERT(GetSourceData() == bfParser->mSourceData);
	Init(bfParser->mTriviaStart, bfParser->mTokenStart, bfParser->mTokenEnd);
}

void BfAstNode::Accept(BfStructuralVisitor* bfVisitor)
{
	(*gAcceptFuncs[GetTypeId()])(this, bfVisitor);
}

bool BfAstNode::IsTemporary()
{
#ifdef BF_AST_COMPACT
	return (mIsCompact) && (mCompact_SrcStart == 0) && (mCompact_SrcLen == 0);
#else
	return (mSrcEnd == 0);
#endif
}

int BfAstNode::GetStartCharId()
{
	if (!IsTemporary())
	{
		auto bfParser = GetSourceData()->ToParserData();
		if (bfParser != NULL)
			return bfParser->GetCharIdAtIndex(GetSrcStart());
	}
	return GetSrcStart();
}

BfSourceData* BfAstNode::GetSourceData()
{
#ifdef BF_AST_ALLOCATOR_USE_PAGES
	//BF_ASSERT((intptr)this > 0x4200000000);
	BfAstPageHeader* pageHeader = (BfAstPageHeader*)((intptr)this & ~(BfAstAllocManager::PAGE_SIZE - 1));
	return pageHeader->mSourceData;
#else
	return mSourceData;
#endif
}

BfParserData* BfAstNode::GetParserData()
{
	BfSourceData* sourceData = GetSourceData();
	if (sourceData == NULL)
		return NULL;
	return sourceData->ToParserData();
}

BfParser* BfAstNode::GetParser()
{
	BfSourceData* sourceData = GetSourceData();
	if (sourceData == NULL)
		return NULL;
	return sourceData->ToParser();
}

bool BfAstNode::IsEmitted()
{
	auto parser = GetParser();
	if (parser == NULL)
		return false;
	return parser->mIsEmitted;
}

bool BfAstNode::IsFromParser(BfParser* parser)
{
	if (parser == NULL)
		return false;
	if (IsTemporary())
		return false;
	BfSourceData* sourceData = GetSourceData();
	if (sourceData == NULL)
		return false;
	return parser == sourceData->ToParser();
}

String BfAstNode::ToString()
{
	int srcLen = GetSrcLength();
	if (srcLen <= 0)
	{
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(this))
			return namedTypeRef->mNameNode->ToString();

		return "";
	}

	auto source = GetSourceData();
	String str(source->mSrc + GetSrcStart(), srcLen);
	return str;
}

StringView BfAstNode::ToStringView()
{
	int srcLen = GetSrcLength();
	if (srcLen <= 0)
	{
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(this))
			return namedTypeRef->mNameNode->ToStringView();
		return StringView();
	}

	auto source = GetSourceData();
	return StringView(source->mSrc + GetSrcStart(), srcLen);
}

void BfAstNode::ToString(StringImpl& str)
{
	int srcLen = GetSrcLength();
	if (srcLen <= 0)
	{
		if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(this))
			namedTypeRef->mNameNode->ToString(str);
		return;
	}

	auto source = GetSourceData();
	str.Append(source->mSrc + GetSrcStart(), srcLen);
}

bool BfAstNode::Equals(const StringImpl& str)
{
	int len = mSrcEnd - mSrcStart;
	if (len != str.mLength)
		return false;
	auto source = GetSourceData();
	return strncmp(str.GetPtr(), source->mSrc + mSrcStart, len) == 0;
}

bool BfAstNode::Equals(const StringView& str)
{
	int len = mSrcEnd - mSrcStart;
	if (len != str.mLength)
		return false;
	auto source = GetSourceData();
	return strncmp(str.mPtr, source->mSrc + mSrcStart, len) == 0;
}

bool BfAstNode::Equals(const char* str)
{
	auto source = GetSourceData();
	const char* ptrLhs = source->mSrc + mSrcStart;
	const char* ptrLhsEnd = source->mSrc + mSrcEnd;
	const char* ptrRhs = str;

	while (true)
	{
		char cRhs = *(ptrRhs++);
		if (cRhs == 0)
			return ptrLhs == ptrLhsEnd;
		if (ptrLhs == ptrLhsEnd)
			return false;
		char cLhs = *(ptrLhs++);
		if (cLhs != cRhs)
			return false;
	}
}

//////////////////////////////////////////////////////////////////////////

void BfBlock::Init(const SizedArrayImpl<BfAstNode*>& vec, BfAstAllocator* alloc)
{
#ifdef BF_USE_NEAR_NODE_REF
	int curIdx = 0;
	int elemsLeft = (int)vec.size();
	BfBlockExtension* curExt = NULL;

	while (elemsLeft > 0)
	{
		int bytesLeft = alloc->GetCurPageBytesLeft();
		int useElems = std::min(bytesLeft / (int)sizeof(ASTREF(BfAstNode*)), elemsLeft);
		BfBlockExtension* nextExt = NULL;
		BfSizedArray<ASTREF(BfAstNode*)>& childArrRef = (curExt != NULL) ? curExt->mChildArr : mChildArr;
		childArrRef.mVals = (ASTREF(BfAstNode*)*)alloc->AllocBytes(useElems * sizeof(ASTREF(BfAstNode*)), sizeof(ASTREF(BfAstNode*)));
		childArrRef.mSize = useElems;
		if (useElems < elemsLeft)
		{
			nextExt = alloc->Alloc<BfBlockExtension>();
			useElems--;
		}

		for (int i = 0; i < useElems; i++)
			childArrRef[i] = vec[curIdx++];
		if (nextExt != NULL)
		{
			childArrRef[useElems] = nextExt;
			curExt = nextExt;
		}
		elemsLeft -= useElems;
	}
#else
	BfSizedArrayInitIndirect(mChildArr, vec, alloc);
#endif
}

BfAstNode* BfBlock::GetFirst()
{
	if (mChildArr.mSize == 0)
		return NULL;
	return mChildArr.mVals[0];
}

BfAstNode* BfBlock::GetLast()
{
	 if (mChildArr.mSize == 0)
		 return NULL;
	 auto backNode = mChildArr.mVals[mChildArr.mSize - 1];
	 while (auto blockExt = BfNodeDynCastExact<BfBlockExtension>(backNode))
		 backNode = blockExt->mChildArr.GetLast();
	 return backNode;
}

int BfBlock::GetSize()
{
	int size = mChildArr.mSize;
	if (mChildArr.mSize == 0)
		return size;
	BfAstNode* backNode = mChildArr.mVals[mChildArr.mSize - 1];
	while (true)
	{
		if (auto blockExt = BfNodeDynCastExact<BfBlockExtension>(backNode))
		{
			size--;
			size += blockExt->mChildArr.mSize;
			backNode = blockExt->mChildArr.mVals[blockExt->mChildArr.mSize - 1];
		}
		else
		{
			break;
		}
	}

	return size;
}

void BfBlock::SetSize(int wantSize)
{
	int size = mChildArr.mSize;
	if (wantSize == size)
		return;

	if (wantSize < size)
	{
		mChildArr.mSize = wantSize;
	}
	else
	{
		BfAstNode* backNode = mChildArr.mVals[mChildArr.mSize - 1];
		while (true)
		{
			if (auto blockExt = BfNodeDynCastExact<BfBlockExtension>(backNode))
			{
				size--;
				size += blockExt->mChildArr.mSize;

				if (wantSize < size)
				{
					blockExt->mChildArr.mSize -= (size - wantSize);
					break;
				}

				backNode = blockExt->mChildArr.mVals[blockExt->mChildArr.mSize - 1];
			}
			else
			{
				break;
			}
		}
	}

	BF_ASSERT(wantSize == GetSize());
}

//////////////////////////////////////////////////////////////////////////

bool BfTypeReference::IsNamedTypeReference()
{
	return IsA<BfNamedTypeReference>() || IsA<BfDirectStrTypeReference>();
}

bool BfTypeReference::IsTypeDefTypeReference()
{
	return IsA<BfNamedTypeReference>() || IsA<BfDirectStrTypeReference>() || IsA<BfDirectTypeDefReference>();
}

String BfTypeReference::ToCleanAttributeString()
{
	// ToString might return something like "System.InlineAttribute", which we want to clean before we test for "Inline"
	auto typeRefName = ToString();
	if (typeRefName.EndsWith("Attribute"))
	{
		int attribNameStart = (int)typeRefName.LastIndexOf('.');
		if (attribNameStart != -1)
			typeRefName.Remove(0, attribNameStart + 1);

		if (typeRefName.EndsWith("Attribute"))
			typeRefName.RemoveFromEnd(9);
	}

	return typeRefName;
}

//////////////////////////////////////////////////////////////////////////

BfPropertyMethodDeclaration* BfPropertyDeclaration::GetMethod(const StringImpl& findName)
{
	String methodName;

	for (auto& methodDeclaration : mMethods)
	{
		if (methodDeclaration->mNameNode != NULL)
		{
			methodName.Clear();
			methodDeclaration->mNameNode->ToString(methodName);

			if (methodName == findName)
				return methodDeclaration;
		}
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////

bool BfExpression::VerifyIsStatement(BfPassInstance* passInstance, bool ignoreError)
{
	if (auto attribExpr = BfNodeDynCast<BfAttributedExpression>(this))
	{
		return attribExpr->mExpression->VerifyIsStatement(passInstance, ignoreError);
	}

	if ((!BfNodeIsExact<BfAssignmentExpression>(this)) &&
		(!BfNodeIsExact<BfInvocationExpression>(this)) &&
		(!BfNodeIsExact<BfObjectCreateExpression>(this)))
	{
		if (auto castExpr = BfNodeDynCast<BfCastExpression>(this))
		{
			if ((castExpr->mTypeRef != NULL) && (castExpr->mTypeRef->ToString() == "void"))
			{
				// Is "(void)variable;" expression, used to remove warning about unused local variables
				return false;
			}
		}

		if (!ignoreError)
			passInstance->Fail("This expression cannot be used as a statement", this);
			//passInstance->Fail("Only assignment, call, increment, decrement, and allocation expressions can be used as a statement", this);
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////

bool BfAttributeDirective::Contains(const StringImpl& findName)
{
	StringT<128> name;

	auto attrDirective = this;
	while (attrDirective != NULL)
	{
		name.clear();
		if (attrDirective->mAttributeTypeRef != NULL)
			attrDirective->mAttributeTypeRef->ToString(name);

		if (findName == name)
			return true;
		if (name.EndsWith("Attribute"))
		{
			int attribNameStart = (int)name.LastIndexOf('.');
			if (attribNameStart != -1)
				name.Remove(0, attribNameStart + 1);

			name.RemoveToEnd(name.length() - 9);
			if (findName == name)
				return true;
		}

		attrDirective = attrDirective->mNextAttribute;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

const char* Beefy::BfTokenToString(BfToken token)
{
	switch (token)
	{
	case BfToken_Abstract:
		return "abstract";
	case BfToken_AlignOf:
		return "alignof";
	case BfToken_AllocType:
		return "alloctype";
	case BfToken_Append:
		return "append";
	case BfToken_As:
		return "as";
	case BfToken_Asm:
		return "asm";
	case BfToken_AsmNewline:
		return "\n";
	case BfToken_Base:
		return "base";
	case BfToken_Box:
		return "box";
	case BfToken_Break:
		return "break";
	case BfToken_Case:
		return "case";
	case BfToken_Catch:
		return "catch";
	case BfToken_Checked:
		return "checked";
	case BfToken_Class:
		return "class";
	case BfToken_Comptype:
		return "comptype";
	case BfToken_Concrete:
		return "concrete";
	case BfToken_Const:
		return "const";
	case BfToken_Continue:
		return "continue";
	case BfToken_Decltype:
		return "decltype";
	case BfToken_Default:
		return "default";
	case BfToken_Defer:
		return "defer";
	case BfToken_Delegate:
		return "delegate";
	case BfToken_Delete:
		return "delete";
	case BfToken_Do:
		return "do";
	case BfToken_Else:
		return "else";
	case BfToken_Enum:
		return "enum";
	case BfToken_Explicit:
		return "explicit";
	case BfToken_Extern:
		return "extern";
	case BfToken_Extension:
		return "extension";
	case BfToken_Fallthrough:
		return "fallthrough";
	case BfToken_Finally:
		return "finally";
	case BfToken_Fixed:
		return "fixed";
	case BfToken_For:
		return "for";
	case BfToken_Function:
		return "function";
	case BfToken_Goto:
		return "goto";
	case BfToken_If:
		return "if";
	case BfToken_Implicit:
		return "implicit";
	case BfToken_In:
		return "in";
	case BfToken_Interface:
		return "interface";
	case BfToken_Internal:
		return "internal";
	case BfToken_Is:
		return "is";
	case BfToken_IsConst:
		return "isconst";
	case BfToken_Let:
		return "let";
	case BfToken_Mixin:
		return "mixin";
	case BfToken_Mut:
		return "mut";
	case BfToken_NameOf:
		return "nameof";
	case BfToken_Namespace:
		return "namespace";
	case BfToken_New:
		return "new";
	case BfToken_Null:
		return "null";
	case BfToken_Nullable:
		return "nullable";
	case BfToken_OffsetOf:
		return "offsetof";
	case BfToken_Operator:
		return "operator";
	case BfToken_Out:
		return "out";
	case BfToken_Override:
		return "override";
	case BfToken_Params:
		return "params";
	case BfToken_Private:
		return "private";
	case BfToken_Protected:
		return "protected";
	case BfToken_Public:
		return "public";
	case BfToken_ReadOnly:
		return "readonly";
	case BfToken_Repeat:
		return "repeat";
	case BfToken_Ref:
		return "ref";
	case BfToken_RetType:
		return "rettype";
	case BfToken_Return:
		return "return";
	case BfToken_Scope:
		return "scope";
	case BfToken_Sealed:
		return "sealed";
	case BfToken_SizeOf:
		return "sizeof";
	case BfToken_Stack:
		return "stack";
	case BfToken_Static:
		return "static";
	case BfToken_StrideOf:
		return "strideof";
	case BfToken_Struct:
		return "struct";
	case BfToken_Switch:
		return "switch";
	case BfToken_This:
		return "this";
	case BfToken_Throw:
		return "throw";
	case BfToken_Try:
		return "try";
	case BfToken_TypeAlias:
		return "typealias";
	case BfToken_TypeOf:
		return "typeof";
	case BfToken_Unchecked:
		return "unchecked";
	case BfToken_Unsigned:
		return "unsigned";
	case BfToken_Using:
		return "using";
	case BfToken_Var:
		return "var";
	case BfToken_Virtual:
		return "virtual";
	case BfToken_Volatile:
		return "volatile";
	case BfToken_When:
		return "when";
	case BfToken_Where:
		return "where";
	case BfToken_While:
		return "while";
	case BfToken_Yield:
		return "yield";
	case BfToken_AssignEquals:
		return "=";
	case BfToken_CompareEquals:
		return "==";
	case BfToken_CompareStrictEquals:
		return "===";
	case BfToken_CompareNotEquals:
		return "!=";
	case BfToken_CompareStrictNotEquals:
		return "!==";
	case BfToken_LessEquals:
		return "<=";
	case BfToken_GreaterEquals:
		return ">=";
	case BfToken_Spaceship:
		return "<=>";
	case BfToken_PlusEquals:
		return "+=";
	case BfToken_MinusEquals:
		return "-=";
	case BfToken_MultiplyEquals:
		return "*=";
	case BfToken_DivideEquals:
		return "/=";
	case BfToken_ModulusEquals:
		return "%=";
	case BfToken_ShiftLeftEquals:
		return "<<=";
	case BfToken_ShiftRightEquals:
		return ">>=";
	case BfToken_AndEquals:
		return "&=";
	case BfToken_AndMinus:
		return "&-";
	case BfToken_AndPlus:
		return "&+";
	case BfToken_AndStar:
		return "&*";
	case BfToken_AndMinusEquals:
		return "&-=";
	case BfToken_AndPlusEquals:
		return "&+=";
	case BfToken_AndStarEquals:
		return "&*=";
	case BfToken_OrEquals:
		return "|=";
	case BfToken_XorEquals:
		return "^=";
	case BfToken_NullCoalsceEquals:
		return "\?\?=";
	case BfToken_LBrace:
		return "{";
	case BfToken_RBrace:
		return "}";
	case BfToken_LParen:
		return "(";
	case BfToken_RParen:
		return ")";
	case BfToken_LBracket:
		return "[";
	case BfToken_RBracket:
		return "]";
	case BfToken_LChevron:
		return "<";
	case BfToken_RChevron:
		return ">";
	case BfToken_LDblChevron:
		return "<<";
	case BfToken_RDblChevron:
		return ">>";
	case BfToken_Semicolon:
		return ";";
	case BfToken_Colon:
		return ":";
	case BfToken_Comma:
		return ",";
	case BfToken_Dot:
	case BfToken_AutocompleteDot:
		return ".";
	case BfToken_DotDot:
		return "..";
	case BfToken_DotDotDot:
		return "...";
	case BfToken_DotDotLess:
		return "..<";
	case BfToken_QuestionDot:
		return "?.";
	case BfToken_QuestionLBracket:
		return "?[";
	case BfToken_Plus:
		return "+";
	case BfToken_Minus:
		return "-";
	case BfToken_DblPlus:
		return "++";
	case BfToken_DblMinus:
		return "--";
	case BfToken_Star:
		return "*";
	case BfToken_ForwardSlash:
		return "/";
	case BfToken_Modulus:
		return "%";
	case BfToken_Ampersand:
		return "&";
	case BfToken_At:
		return "@";
	case BfToken_DblAmpersand:
		return "&&";
	case BfToken_Bar:
		return "|";
	case BfToken_DblBar:
		return "||";
	case BfToken_Bang:
		return "!";
	case BfToken_Carat:
		return "^";
	case BfToken_Tilde:
		return "~";
	case BfToken_Question:
		return "?";
	case BfToken_DblQuestion:
		return "??";
	case BfToken_Arrow:
		return "->";
	case BfToken_FatArrow:
		return "=>";
	default:
		break;
	}
	BF_FATAL("Unknown token");
	return NULL;
}

bool Beefy::BfTokenIsKeyword(BfToken token)
{
	return (token >= BfToken_Abstract) && (token <= BfToken_Yield);
}

BfBinaryOp Beefy::BfAssignOpToBinaryOp(BfAssignmentOp assignmentOp)
{
	switch (assignmentOp)
	{
	case BfAssignmentOp_Add:
		return BfBinaryOp_Add;
	case BfAssignmentOp_Subtract:
		return BfBinaryOp_Subtract;
	case BfAssignmentOp_Multiply:
		return BfBinaryOp_Multiply;
	case BfAssignmentOp_Divide:
		return BfBinaryOp_Divide;
	case BfAssignmentOp_OverflowAdd:
		return BfBinaryOp_OverflowAdd;
	case BfAssignmentOp_OverflowSubtract:
		return BfBinaryOp_OverflowSubtract;
	case BfAssignmentOp_OverflowMultiply:
		return BfBinaryOp_OverflowMultiply;
	case BfAssignmentOp_Modulus:
		return BfBinaryOp_Modulus;
	case BfAssignmentOp_ShiftLeft:
		return BfBinaryOp_LeftShift;
	case BfAssignmentOp_ShiftRight:
		return BfBinaryOp_RightShift;
	case BfAssignmentOp_BitwiseAnd:
		return BfBinaryOp_BitwiseAnd;
	case BfAssignmentOp_BitwiseOr:
		return BfBinaryOp_BitwiseOr;
	case BfAssignmentOp_ExclusiveOr:
		return BfBinaryOp_ExclusiveOr;
	case BfAssignmentOp_NullCoalesce:
		return BfBinaryOp_NullCoalesce;
	default:
		break;
	}
	return BfBinaryOp_None;
}

int Beefy::BfGetBinaryOpPrecendence(BfBinaryOp binOp)
{
	switch (binOp)
	{
	case BfBinaryOp_Multiply:
	case BfBinaryOp_OverflowMultiply:
	case BfBinaryOp_Divide:
	case BfBinaryOp_Modulus:
		return 14;
	case BfBinaryOp_Add:
	case BfBinaryOp_Subtract:
	case BfBinaryOp_OverflowAdd:
	case BfBinaryOp_OverflowSubtract:
		return 13;
	case BfBinaryOp_LeftShift:
	case BfBinaryOp_RightShift:
		return 12;
	case BfBinaryOp_BitwiseAnd:
		return 11;
	case BfBinaryOp_ExclusiveOr:
		return 10;
	case BfBinaryOp_BitwiseOr:
		return 9;
	case BfBinaryOp_Range:
	case BfBinaryOp_ClosedRange:
		return 8;
	case BfBinaryOp_Is:
	case BfBinaryOp_As:
		return 7;
	case BfBinaryOp_Compare:
		return 6;
	case BfBinaryOp_GreaterThan:
	case BfBinaryOp_LessThan:
	case BfBinaryOp_GreaterThanOrEqual:
	case BfBinaryOp_LessThanOrEqual:
		return 5;
	case BfBinaryOp_Equality:
	case BfBinaryOp_StrictEquality:
	case BfBinaryOp_InEquality:
	case BfBinaryOp_StrictInEquality:
		return 4;
	case BfBinaryOp_ConditionalAnd:
		return 3;
	case BfBinaryOp_ConditionalOr:
		return 2;
	case BfBinaryOp_NullCoalesce:
		return 1;
	default:
		break;
	}

	return 0;
}

const char* Beefy::BfGetOpName(BfBinaryOp binOp)
{
	switch (binOp)
	{
	case BfBinaryOp_None: return "";
	case BfBinaryOp_Add: return "+";
	case BfBinaryOp_Subtract: return "-";
	case BfBinaryOp_Multiply: return "*";
	case BfBinaryOp_OverflowAdd: return "&+";
	case BfBinaryOp_OverflowSubtract: return "&-";
	case BfBinaryOp_OverflowMultiply: return "&*";
	case BfBinaryOp_Divide: return "/";
	case BfBinaryOp_Modulus: return "%";
	case BfBinaryOp_BitwiseAnd: return "&";
	case BfBinaryOp_BitwiseOr: return "|";
	case BfBinaryOp_ExclusiveOr: return "^";
	case BfBinaryOp_LeftShift: return "<<";
	case BfBinaryOp_RightShift: return ">>";
	case BfBinaryOp_Equality: return "==";
	case BfBinaryOp_StrictEquality: return "===";
	case BfBinaryOp_InEquality: return "!=";
	case BfBinaryOp_StrictInEquality: return "!==";
	case BfBinaryOp_GreaterThan: return ">";
	case BfBinaryOp_LessThan: return "<";
	case BfBinaryOp_GreaterThanOrEqual: return ">=";
	case BfBinaryOp_LessThanOrEqual: return "<=";
	case BfBinaryOp_Compare: return "<=>";
	case BfBinaryOp_ConditionalAnd: return "&&";
	case BfBinaryOp_ConditionalOr: return "||";
	case BfBinaryOp_NullCoalesce: return "??";
	case BfBinaryOp_Is: return "is";
	case BfBinaryOp_As: return "as";
	case BfBinaryOp_Range: return "..<";
	case BfBinaryOp_ClosedRange: return "...";
	default: return "???";
	}
}

const char* Beefy::BfGetOpName(BfUnaryOp unaryOp)
{
	switch (unaryOp)
	{
	case BfUnaryOp_None: return "";
	case BfUnaryOp_AddressOf: return "&";
	case BfUnaryOp_Arrow: return "->";
	case BfUnaryOp_Dereference: return "*";
	case BfUnaryOp_Negate: return "-";
	case BfUnaryOp_Not: return "!";
	case BfUnaryOp_Positive: return "+";
	case BfUnaryOp_InvertBits: return "~";
	case BfUnaryOp_Increment: return "++";
	case BfUnaryOp_Decrement: return "--";
	case BfUnaryOp_PostIncrement: return "++";
	case BfUnaryOp_PostDecrement: return "--";
	case BfUnaryOp_NullConditional: return "?";
	case BfUnaryOp_Ref: return "ref";
	case BfUnaryOp_Out: return "out";
	case BfUnaryOp_Mut: return "mut";
	case BfUnaryOp_Params: return "params";
	case BfUnaryOp_Cascade: return "..";
	case BfUnaryOp_FromEnd: return "^";
	case BfUnaryOp_PartialRangeUpTo: return "..<";
	case BfUnaryOp_PartialRangeThrough: return "...";
	case BfUnaryOp_PartialRangeFrom: return "...";
	default: return "???";
	}
}

BfBinaryOp Beefy::BfTokenToBinaryOp(BfToken token)
{
	switch (token)
	{
	case BfToken_Plus:
		return BfBinaryOp_Add;
	case BfToken_Minus:
		return BfBinaryOp_Subtract;
	case BfToken_Star:
		return BfBinaryOp_Multiply;
	case BfToken_AndPlus:
		return BfBinaryOp_OverflowAdd;
	case BfToken_AndMinus:
		return BfBinaryOp_OverflowSubtract;
	case BfToken_AndStar:
		return BfBinaryOp_OverflowMultiply;
	case BfToken_ForwardSlash:
		return BfBinaryOp_Divide;
	case BfToken_Modulus:
		return BfBinaryOp_Modulus;
	case BfToken_Ampersand:
		return BfBinaryOp_BitwiseAnd;
	case BfToken_Bar:
		return BfBinaryOp_BitwiseOr;
	case BfToken_Carat:
		return BfBinaryOp_ExclusiveOr;
	case BfToken_LDblChevron:
		return BfBinaryOp_LeftShift;
	case BfToken_RDblChevron:
		return BfBinaryOp_RightShift;
	case BfToken_CompareEquals:
		return BfBinaryOp_Equality;
	case BfToken_CompareStrictEquals:
		return BfBinaryOp_StrictEquality;
	case BfToken_CompareNotEquals:
		return BfBinaryOp_InEquality;
	case BfToken_CompareStrictNotEquals:
		return BfBinaryOp_StrictInEquality;
	case BfToken_RChevron:
		return BfBinaryOp_GreaterThan;
	case BfToken_LChevron:
		return BfBinaryOp_LessThan;
	case BfToken_GreaterEquals:
		return BfBinaryOp_GreaterThanOrEqual;
	case BfToken_LessEquals:
		return BfBinaryOp_LessThanOrEqual;
	case BfToken_Spaceship:
		return BfBinaryOp_Compare;
	case BfToken_DblAmpersand:
		return BfBinaryOp_ConditionalAnd;
	case BfToken_DblBar:
		return BfBinaryOp_ConditionalOr;
	case BfToken_DblQuestion:
		return BfBinaryOp_NullCoalesce;
	case BfToken_DotDotLess:
		return BfBinaryOp_Range;
	case BfToken_DotDotDot:
		return BfBinaryOp_ClosedRange;
	default:
		return BfBinaryOp_None;
	}
}

BfUnaryOp Beefy::BfTokenToUnaryOp(BfToken token)
{
	switch (token)
	{
	case BfToken_Star:
		return BfUnaryOp_Dereference;
	case BfToken_Ampersand:
		return BfUnaryOp_AddressOf;
	case BfToken_Arrow:
		return BfUnaryOp_Arrow;
	case BfToken_Minus:
		return BfUnaryOp_Negate;
	case BfToken_Bang:
		return BfUnaryOp_Not;
	case BfToken_Plus:
		return BfUnaryOp_Positive;
	case BfToken_Tilde:
		return BfUnaryOp_InvertBits;
	case BfToken_DblPlus:
		return BfUnaryOp_Increment;
	case BfToken_DblMinus:
		return BfUnaryOp_Decrement;
	case BfToken_Question:
		return BfUnaryOp_NullConditional;
	case BfToken_Ref:
		return BfUnaryOp_Ref;
	case BfToken_Mut:
		return BfUnaryOp_Mut;
	case BfToken_Out:
		return BfUnaryOp_Out;
	case BfToken_Params:
		return BfUnaryOp_Params;
	case BfToken_DotDot:
		return BfUnaryOp_Cascade;
	case BfToken_Carat:
		return BfUnaryOp_FromEnd;
	case BfToken_DotDotDot:
		return BfUnaryOp_PartialRangeThrough;
	case BfToken_DotDotLess:
		return BfUnaryOp_PartialRangeUpTo;
	default:
		return BfUnaryOp_None;
	}
}

bool Beefy::BfCanOverloadOperator(BfUnaryOp unaryOp)
{
	switch (unaryOp)
	{
	case BfUnaryOp_Negate:
	case BfUnaryOp_Not:
	case BfUnaryOp_Positive:
	case BfUnaryOp_InvertBits:
	case BfUnaryOp_Increment:
	case BfUnaryOp_Decrement:
	case BfUnaryOp_PostIncrement:
	case BfUnaryOp_PostDecrement:
	case BfUnaryOp_NullConditional:
		return true;
	default:
		return false;
	}
}

BfAssignmentOp Beefy::BfTokenToAssignmentOp(BfToken token)
{
	switch (token)
	{
	case BfToken_AssignEquals:
		return BfAssignmentOp_Assign;
	case BfToken_PlusEquals:
		return BfAssignmentOp_Add;
	case BfToken_MinusEquals:
		return BfAssignmentOp_Subtract;
	case BfToken_MultiplyEquals:
		return BfAssignmentOp_Multiply;
	case BfToken_AndPlusEquals:
		return BfAssignmentOp_OverflowAdd;
	case BfToken_AndMinusEquals:
		return BfAssignmentOp_OverflowSubtract;
	case BfToken_AndStarEquals:
		return BfAssignmentOp_OverflowMultiply;
	case BfToken_DivideEquals:
		return BfAssignmentOp_Divide;
	case BfToken_ModulusEquals:
		return BfAssignmentOp_Modulus;
	case BfToken_ShiftLeftEquals:
		return BfAssignmentOp_ShiftLeft;
	case BfToken_ShiftRightEquals:
		return BfAssignmentOp_ShiftRight;
	case BfToken_AndEquals:
		return BfAssignmentOp_BitwiseAnd;
	case BfToken_OrEquals:
		return BfAssignmentOp_BitwiseOr;
	case BfToken_XorEquals:
		return BfAssignmentOp_ExclusiveOr;
	case BfToken_NullCoalsceEquals:
		return BfAssignmentOp_NullCoalesce;
	default:
		return BfAssignmentOp_None;
	}
}

BfBinaryOp Beefy::BfGetOppositeBinaryOp(BfBinaryOp origOp)
{
	switch (origOp)
	{
	case BfBinaryOp_Equality:
		return BfBinaryOp_InEquality;
	case BfBinaryOp_StrictEquality:
		return BfBinaryOp_StrictInEquality;
	case BfBinaryOp_InEquality:
		return BfBinaryOp_Equality;
	case BfBinaryOp_StrictInEquality:
		return BfBinaryOp_StrictEquality;
	case BfBinaryOp_LessThan:
		return BfBinaryOp_GreaterThanOrEqual;
	case BfBinaryOp_LessThanOrEqual:
		return BfBinaryOp_GreaterThan;
	case BfBinaryOp_GreaterThan:
		return BfBinaryOp_LessThanOrEqual;
	case BfBinaryOp_GreaterThanOrEqual:
		return BfBinaryOp_LessThan;
	default: break;
	}

	return BfBinaryOp_None;
}

BfBinaryOp Beefy::BfGetFlippedBinaryOp(BfBinaryOp origOp)
{
	switch (origOp)
	{
	case BfBinaryOp_Equality:
		return BfBinaryOp_Equality;
	case BfBinaryOp_InEquality:
		return BfBinaryOp_InEquality;
	case BfBinaryOp_LessThan:
		return BfBinaryOp_GreaterThan;
	case BfBinaryOp_LessThanOrEqual:
		return BfBinaryOp_GreaterThanOrEqual;
	case BfBinaryOp_GreaterThan:
		return BfBinaryOp_LessThan;
	case BfBinaryOp_GreaterThanOrEqual:
		return BfBinaryOp_LessThanOrEqual;
	default: break;
	}

	return BfBinaryOp_None;
}

bool Beefy::BfBinOpEqualityCheck(BfBinaryOp binOp)
{
	return (binOp >= BfBinaryOp_Equality) && (binOp <= BfBinaryOp_StrictInEquality);
}

bool Beefy::BfIsCommentBlock(BfCommentKind commentKind)
{
	return
		(commentKind == BfCommentKind_Block) ||
		(commentKind == BfCommentKind_Documentation_Block_Pre) ||
		(commentKind == BfCommentKind_Documentation_Block_Post);
}

BfInlineAsmInstruction::AsmArg::AsmArg()
	: mType(ARGTYPE_Immediate)
	, mMemFlags(0)
	, mInt(0)
	, mAdjRegScalar(1)
{}

#pragma warning(disable:4996)
String BfInlineAsmInstruction::AsmArg::ToString()
{
	String s;

	if (!mSizePrefix.empty())
	{
		s += mSizePrefix;
		s += " ptr ";
	}
	if (!mSegPrefix.empty())
	{
		s += mSegPrefix;
		s += ":";
	}
	switch(mType)
	{
	case ARGTYPE_Immediate:
		{
			char buf[64];
			sprintf(buf, "%d", mInt);
			s += buf;
		}
		break;
	case ARGTYPE_FloatReg:
		{
			char buf[64];
			sprintf(buf, "st(%d)", mInt);
			s += buf;
		}
		break;
	case ARGTYPE_IntReg:
		{
			s += mReg;
		}
		break;
	case ARGTYPE_Memory:
		{
			s += "[";
			if (mMemFlags & ARGMEMF_BaseReg)
			{
				s += mReg;
				if (mMemFlags != ARGMEMF_BaseReg)
					s += " + ";
			}
			if (mMemFlags & ARGMEMF_AdjReg)
			{
				s += mAdjReg;
				if (mAdjRegScalar != 1)
				{
					char buf[64];
					sprintf(buf, "%d", mAdjRegScalar);
					s += "*";
					s += buf;
				}
				if (mMemFlags & ARGMEMF_ImmediateDisp)
					s += " + ";
			}
			if (mMemFlags & ARGMEMF_ImmediateDisp)
			{
				char buf[64];
				sprintf(buf, "%d", mInt);
				s += buf;
			}
			s += "]";
			if (!mMemberSuffix.empty())
			{
				s += ".";
				s += mMemberSuffix;
			}
		}
		break;
	}

	return s;
}

BfInlineAsmInstruction::AsmInst::AsmInst()
	: mDebugLine(0)
{}

String BfInlineAsmInstruction::AsmInst::ToString()
{
	String s;

	if (!mLabel.empty())
	{
		s += mLabel;
		s += ": ";
	}

	for (auto const& p : mOpPrefixes)
	{
		s += p;
		s += " ";
	}
	s += mOpCode;
	s += " ";
	for (int i=0; i<(int)mArgs.size(); ++i)
	{
		if (i > 0)
			s += ", ";
		s += mArgs[i].ToString();
	}

	return s;
}