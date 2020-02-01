#include "BfPrinter.h"
#include "BfParser.h"
#include "BfUtil.h"

USING_NS_BF;

BfPrinter::BfPrinter(BfRootNode* rootNode, BfRootNode* sidechannelRootNode, BfRootNode* errorRootNode)
{	
	mSource = rootNode->GetSourceData();
	mParser = mSource->ToParserData();	

	if (sidechannelRootNode != NULL)
		mSidechannelItr = sidechannelRootNode->begin();
	mSidechannelNextNode = mSidechannelItr.Get();
	
	if (errorRootNode != NULL)
		mErrorItr = errorRootNode->begin();	
	mErrorNextNode = mErrorItr.Get();	

	mTriviaIdx = 0;
	mCurSrcIdx = 0;
	mCurIndentLevel = 0;
	mQueuedSpaceCount = 0;
	mLastSpaceOffset = 0;
	mReformatting = false;	
	mDocPrep = false;
	mIgnoreTrivia = false;
	mForceUseTrivia = false;	
	mIsFirstStatementInBlock = false;	
	mFormatStart = -1;
	mFormatEnd = -1;
	mInSideChannel = false;
	mCharMapping = NULL;
	mExpectingNewLine = false;
	mCurBlockMember = NULL;

	mStateModifyVirtualIndentLevel = 0;
	mVirtualIndentLevel = 0;
	mVirtualNewLineIdx = 0;
	mHighestCharId = 0;
	mCurTypeDecl = NULL;
}

void BfPrinter::Write(const StringImpl& str)
{	
	mOutString.Append(str);
	if (mCharMapping != NULL)
	{
		for (int i = 0; i < (int)str.length(); i++)
		{
			int prevSrcIdx = -1;
			if (mCharMapping->size() > 0)			
				prevSrcIdx = mCharMapping->back();			
			if (prevSrcIdx != -1)
			{
				bool matched = false;
				int curSrcIdx = prevSrcIdx + 1;

				// Search for char, skipping over whitespace
				while (curSrcIdx < mParser->mSrcLength)
				{
					char c = mParser->mSrc[curSrcIdx];
					if (c == str[i])
					{
						matched = true;
						break;
					}
					else if ((c == '\t') || (c == ' '))
						curSrcIdx++;
					else
						break;
				}

				if (matched)
				{
					// It matches the source character, extend...					
					mCharMapping->push_back(curSrcIdx);
				}
				else
				{
					// New char
					mCharMapping->push_back(-1);
				}
			}
			else
			{
				mCharMapping->push_back(-1);
			}
		}
	}	
}

void BfPrinter::FlushIndent()
{	
	while (mQueuedSpaceCount >= 4)
	{
		Write("\t");
		mQueuedSpaceCount -= 4;
	}

	while (mQueuedSpaceCount > 0)
	{
		Write(" ");		
		mQueuedSpaceCount--;
	}
}

void BfPrinter::Write(BfAstNode* node, int start, int len)
{
	FlushIndent();	
	mOutString.Append(node->GetSourceData()->mSrc + start, len);
	if (mCharMapping != NULL)
	{
		if (start < mParser->mSrcLength)
		{
			for (int i = 0; i < len; i++)
			{				
				mCharMapping->push_back(start + i);
			}
		}
		else
		{
			for (int i = 0; i < len; i++)
				mCharMapping->push_back(-1);
		}
	}
}

void BfPrinter::WriteSourceString(BfAstNode* node)
{
	Write(node, node->GetSrcStart(), node->GetSrcLength());	
}

void BfPrinter::QueueVisitChild(BfAstNode* astNode)
{	
	if (astNode != NULL)
	{
		mNextStateModify.mQueuedNode = astNode;
		mChildNodeQueue.push_back(mNextStateModify);
		mNextStateModify.Clear();
	}
}

void BfPrinter::QueueVisitErrorNodes(BfRootNode* astNode)
{
	for (auto& childNodeRef : *astNode)
	{
		BfAstNode* childNode = childNodeRef;		
		if (childNode->IsA<BfErrorNode>())
			QueueVisitChild(childNode);
	}
}

static bool CompareNodeStart(const BfPrinter::StateModify& node1, const BfPrinter::StateModify& node2)
{
	return node1.mQueuedNode->GetSrcStart() < node2.mQueuedNode->GetSrcStart();
}

void BfPrinter::FlushVisitChild()
{
	if (mChildNodeQueue.size() == 0)
		return;
	
	auto nodeQueue = mChildNodeQueue;
	mChildNodeQueue.Clear();		
	
	std::stable_sort(nodeQueue.begin(), nodeQueue.end(), CompareNodeStart);	

	for (auto& node : nodeQueue)
	{
		mNextStateModify = node;
		
		VisitChild(node.mQueuedNode);
		if (mVirtualNewLineIdx == mNextStateModify.mWantNewLineIdx)
			mVirtualNewLineIdx = node.mWantNewLineIdx;
	}
}

void BfPrinter::VisitChildWithPrecedingSpace(BfAstNode* bfAstNode)
{
	if (bfAstNode == NULL)
		return;
	ExpectSpace();
	VisitChild(bfAstNode);
}

void BfPrinter::VisitChildWithProceedingSpace(BfAstNode* bfAstNode)
{
	if (bfAstNode == NULL)
		return;	
	VisitChild(bfAstNode);
	ExpectSpace();
}

void BfPrinter::ExpectSpace()
{
	mNextStateModify.mExpectingSpace = true;	
}

void BfPrinter::ExpectNewLine()
{
	mNextStateModify.mWantNewLineIdx++;	
}

void BfPrinter::ExpectIndent()
{		
	mNextStateModify.mWantVirtualIndent++;
	ExpectNewLine();
}

void BfPrinter::ExpectUnindent()
{	
	mNextStateModify.mWantVirtualIndent--;
	ExpectNewLine();
}

void BfPrinter::VisitChildNextLine(BfAstNode* node)
{
	if (node == NULL)
		return;
	
	if (node->IsA<BfBlock>())
	{
		VisitChild(node);
	}
	else
	{		
		ExpectNewLine();
		ExpectIndent();
		VisitChild(node);
		ExpectUnindent();
	}
}

int BfPrinter::CalcOrigLineSpacing(BfAstNode* bfAstNode, int* lineStartIdx)
{
	int origLineSpacing = 0;
	int checkIdx = bfAstNode->GetSrcStart() - 1;
	auto astNodeSrc = bfAstNode->GetSourceData();
	for ( ; checkIdx >= 0; checkIdx--)
	{
		char c = astNodeSrc->mSrc[checkIdx];
		if (c == '\n')
			break;
		if (c == '\t')
			origLineSpacing += 4;
		else if (c == ' ')
			origLineSpacing += 1;
		else
			return -1;						
	}
	if (lineStartIdx != NULL)
		*lineStartIdx = checkIdx + 1;
	return origLineSpacing;
}

void BfPrinter::WriteIgnoredNode(BfAstNode* node)
{
	mTriviaIdx = std::max(mTriviaIdx, node->GetTriviaStart());
	int endIdx = mTriviaIdx;	
	int crCount = 0;
	auto astNodeSrc = node->GetSourceData();
	int srcEnd = node->GetSrcEnd();
	for (int i = mTriviaIdx; i < srcEnd; i++)
	{
		char c = astNodeSrc->mSrc[i];
		if ((c == '\n') && (i < node->GetSrcStart()))
			crCount++;
		if (((c != ' ') && (c != '\t')) || (!mReformatting))
			endIdx = i + 1;
	}

	bool expectingNewLine = mNextStateModify.mWantNewLineIdx != mVirtualNewLineIdx;
	
	int startIdx = mTriviaIdx;
	if ((expectingNewLine) && (mReformatting))
	{
		// Try to adjust the tabbing of this comment by the same amount that the previous line was adjusted
		int lineStart = -1;
		int origLineSpacing = CalcOrigLineSpacing(node, &lineStart);

		if ((origLineSpacing != -1) && (lineStart != -1))
		{
			for (int i = 0; i < crCount; i++)
				Write("\n");
			startIdx = node->GetSrcStart();
			// Leave left-aligned preprocessor nodes
			if ((node->GetSourceData()->mSrc[node->GetSrcStart()] != '#') || (origLineSpacing > 0))
				mQueuedSpaceCount = origLineSpacing + mLastSpaceOffset;			
		}
	}	

	// This handles tab adjustment within multiline comments
	FlushIndent();	
	bool isNewLine = false;	
	for (int srcIdx = startIdx; srcIdx < endIdx; srcIdx++)
	{
#ifdef A
		int zap = 999;
#endif

		bool emitChar = true;
		char c = astNodeSrc->mSrc[srcIdx];
		if (c == '\n')		
			isNewLine = true;		
		else if (isNewLine) 
		{
			if (c == ' ')
			{
				mQueuedSpaceCount++;
				emitChar = false;
			}
			else if (c == '\t')
			{
				mQueuedSpaceCount += 4;
				emitChar = false;
			}
			else
			{					
				// Leave left-aligned preprocessor nodes that are commented out
				if ((c != '#') || (mQueuedSpaceCount > 0))
					mQueuedSpaceCount = std::max(0, mQueuedSpaceCount + mLastSpaceOffset);				
				isNewLine = false;				
			}
		}

		if (emitChar)
		{						
			FlushIndent();
			mOutString.Append(c);
			if (mCharMapping != NULL)
			{
				if (srcIdx < mParser->mSrcLength)				
					mCharMapping->push_back(srcIdx);				
				else
					mCharMapping->push_back(-1);
			}
		}
	}

	FlushIndent();

	mTriviaIdx = endIdx;	
	mIsFirstStatementInBlock = false;
}

void BfPrinter::Visit(BfAstNode* bfAstNode)
{		
	SetAndRestoreValue<bool> prevForceTrivia(mForceUseTrivia);

	bool expectingNewLine = mNextStateModify.mWantNewLineIdx != mVirtualNewLineIdx;
	if (expectingNewLine)
		mExpectingNewLine = true;

	int indentOffset = 0;
	if (bfAstNode->GetTriviaStart() != -1)
	{				
		if (expectingNewLine)
		{
			indentOffset = mNextStateModify.mWantVirtualIndent - mVirtualIndentLevel;
			mVirtualIndentLevel += indentOffset;
			mCurIndentLevel += indentOffset;
		}
	}

	while (true)
	{
		int sidechannelDist = -1;		
		if (mSidechannelNextNode != NULL)
			sidechannelDist = bfAstNode->GetSrcStart() - mSidechannelNextNode->GetSrcStart();

		int errorDist = -1;
		if (mErrorNextNode != NULL)
			errorDist = bfAstNode->GetSrcStart() - mErrorNextNode->GetSrcStart();

		if ((sidechannelDist > 0) && (sidechannelDist >= errorDist))
		{
			BF_ASSERT(!mInSideChannel);
			mInSideChannel = true;
			VisitChild(mSidechannelNextNode);
			mInSideChannel = false;
			++mSidechannelItr;
			mSidechannelNextNode = mSidechannelItr.Get();
		}
		else if (errorDist > 0)
		{
			auto curErrorNode = mErrorNextNode;
			++mErrorItr;
			mErrorNextNode = mErrorItr.Get();			
			mForceUseTrivia = true;
			VisitChild(curErrorNode);			
		}
		else
			break;
	}
	
	// This won't be true if we move nodes around during refactoring
	if (bfAstNode->GetSrcStart() >= mCurSrcIdx)
	{		
		mCurSrcIdx = bfAstNode->GetSrcStart();
	}
	
	if ((!mReformatting) && (mFormatStart != -1) && (mCurSrcIdx >= mFormatStart) && ((mCurSrcIdx < mFormatEnd) || (mFormatEnd == -1)))
	{		
		mReformatting = true;

		// We entered the format region, figure our what our current indent level is
		int backIdx = mCurSrcIdx - 1;
		int prevSpaceCount = 0;
		auto astNodeSrc = bfAstNode->GetSourceData();
		while (backIdx >= 0)
		{
			char c = astNodeSrc->mSrc[backIdx];
			if (c == ' ')
				prevSpaceCount++;
			else if (c == '\t')
				prevSpaceCount += 4;
			else if (c == '\n')
			{
				// Found previous line
				mCurIndentLevel = prevSpaceCount / 4;
				break;
			}
			else
			{				
				prevSpaceCount = 0;
			}
			backIdx--;
		}		
	}

	if ((mCurSrcIdx >= mFormatEnd) && (mFormatEnd != -1))
		mReformatting = false;
	
	// When triviaStart == -1, that indicates it's a combined node where the text we want to process is on the inside
	if (bfAstNode->GetTriviaStart() != -1) 
	{		
		mTriviaIdx = std::max(mTriviaIdx, bfAstNode->GetTriviaStart());
		bool usedTrivia = false;
						
		if ((!mIgnoreTrivia) && (mTriviaIdx < bfAstNode->GetSrcStart()))
		{
			if ((!mReformatting) || (mForceUseTrivia))
			{
				Write(bfAstNode, mTriviaIdx, bfAstNode->GetSrcStart() - mTriviaIdx);
				usedTrivia = true;
			}			
		}		

		if ((mReformatting) && (!mInSideChannel))
		{
			// Check to see if our trivia contained a newline and duplicate whatever the
			//  indent increase was from the previous line
			if ((!usedTrivia) && (mTriviaIdx < bfAstNode->GetSrcStart()) && (!mIsFirstStatementInBlock) && (!mNextStateModify.mDoingBlockOpen) && (!mNextStateModify.mDoingBlockClose))
			{
				bool hadNewline = true;
				bool hadPrevLineSpacing = false;
				int prevSpaceCount = -1;
				int spaceCount = 0;
				auto astNodeSrc = bfAstNode->GetSourceData();
				for (int i = mTriviaIdx; i < bfAstNode->GetSrcStart(); i++)
				{
					if (mIgnoreTrivia)
						break;					

					char c = astNodeSrc->mSrc[i];
					
					if (c == ' ')
						spaceCount++;
					else if (c == '\t')
						spaceCount += 4;

					if (((c == '\n') || (i == bfAstNode->GetSrcStart() - 1)) && (hadPrevLineSpacing) && (prevSpaceCount > 0))
					{							
						mQueuedSpaceCount += std::max(0, spaceCount - prevSpaceCount) - std::max(0, indentOffset * 4);
						
						prevSpaceCount = -1;
						hadPrevLineSpacing = false;
					}

					if (c == '\n')
					{
						hadNewline = true;
						int backIdx = i - 1;
						prevSpaceCount = 0;						
						while (backIdx >= 0)
						{
							char c = astNodeSrc->mSrc[backIdx];
							if (c == ' ')
								prevSpaceCount++;
							else if (c == '\t')
								prevSpaceCount += 4;
							
							if ((c == '\n') || (backIdx == 0))
							{
								// Found previous line
								usedTrivia = true;
								Write("\n");
								mQueuedSpaceCount = mCurIndentLevel * 4;																

								// Indents extra if we have a statement split over multiple lines
								if (!mExpectingNewLine)
									mQueuedSpaceCount += 4;

								break;
							}
							else
							{
								hadPrevLineSpacing = true;
								prevSpaceCount = 0;
							}
							backIdx--;
						}
						spaceCount = 0;
					}
					else if (!isspace((uint8)c))
						spaceCount = 0;					
				}				
			}

			if (usedTrivia)
			{
				// Already did whitespace
			}
			else if ((mNextStateModify.mDoingBlockOpen) || (mNextStateModify.mDoingBlockClose) || (mIsFirstStatementInBlock))
			{
				int wasOnNewLine = true;
				int idx = (int)mOutString.length() - 1;
				while (idx >= 0)
				{
					char c = mOutString[idx];
					if (c == '\n')
						break;
					if (!isspace((uint8)c))
					{
						Write("\n");
						mQueuedSpaceCount = mCurIndentLevel * 4;					
						int origLineSpacing = CalcOrigLineSpacing(bfAstNode, NULL);
						if (origLineSpacing != -1)
							mLastSpaceOffset = mQueuedSpaceCount - origLineSpacing;
						break;
					}
					idx--;
				}
			}
			else if ((mNextStateModify.mExpectingSpace) || (expectingNewLine))
			{				
				FlushIndent();
				char startChar = bfAstNode->GetSourceData()->mSrc[bfAstNode->GetSrcStart()];
				bool isEnding = (startChar == ';') || (startChar == ')');
				if ((!isEnding) && (mOutString.length() > 0) && (!isspace((uint8)mOutString[mOutString.length() - 1])))
					Write(" ");
			}			
		}				

		if (!mInSideChannel)
		{
			if (mNextStateModify.mDoingBlockOpen)
				mIsFirstStatementInBlock = true;
			else
				mIsFirstStatementInBlock = false;
			mNextStateModify.mExpectingSpace = false;
			mNextStateModify.mDoingBlockOpen = false;
			mNextStateModify.mDoingBlockClose = false;
			mNextStateModify.Clear();
			mVirtualNewLineIdx = mNextStateModify.mWantNewLineIdx;
		}

		mForceUseTrivia = false;		
		if (bfAstNode->GetSrcStart() >= mTriviaIdx)
			mTriviaIdx = bfAstNode->GetSrcStart();

		// We either used this mExpectingNewLine to do an extra indent for a statement split over multiple lines or we didn't
		mExpectingNewLine = false;
	}
}

bool BfPrinter::CheckReplace(BfAstNode* astNode)
{	
	return false;
}

void BfPrinter::Visit(BfErrorNode* errorNode)
{
	SetAndRestoreValue<bool> prevForceUseTrivia(mForceUseTrivia, true);
	SetAndRestoreValue<bool> prevFormatting(mReformatting, false);
	SetAndRestoreValue<int> prevFormatEndIdx(mFormatEnd, 0);
	VisitChild(errorNode->mRefNode);
}

void BfPrinter::Visit(BfScopeNode* scopeNode)
{
	Visit(scopeNode->ToBase());

	VisitChild(scopeNode->mScopeToken);
	VisitChild(scopeNode->mColonToken);
	VisitChild(scopeNode->mTargetNode);
	if (scopeNode->mAttributes != NULL)
	{
		ExpectSpace();
		VisitChild(scopeNode->mAttributes);
	}
}

void BfPrinter::Visit(BfNewNode* newNode)
{
	Visit(newNode->ToBase());

	VisitChild(newNode->mNewToken);
	VisitChild(newNode->mColonToken);
	VisitChild(newNode->mAllocNode);
	if (newNode->mAttributes != NULL)
	{
		ExpectSpace();
		VisitChild(newNode->mAttributes);
	}
}


void BfPrinter::Visit(BfExpression* expr)
{
	Visit(expr->ToBase());	
}

void BfPrinter::Visit(BfExpressionStatement* exprStmt)
{
	//Visit((BfAstNode*)exprStmt);
	Visit(exprStmt->ToBase());

	VisitChild(exprStmt->mExpression);
	VisitChild(exprStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfAttributedExpression* attribExpr)
{	
	Visit(attribExpr->ToBase());

	if (auto indexerExpr = BfNodeDynCast<BfIndexerExpression>(attribExpr->mExpression))
	{
		VisitChild(indexerExpr->mTarget);
		VisitChild(indexerExpr->mOpenBracket);
		VisitChild(attribExpr->mAttributes);
		for (int i = 0; i < (int)indexerExpr->mArguments.size(); i++)
		{
			if (i > 0)
			{
				VisitChild(indexerExpr->mCommas[i - 1]);
				ExpectSpace();
			}
			VisitChild(indexerExpr->mArguments[i]);
		}

		VisitChild(indexerExpr->mCloseBracket);				
	}
	else
	{
		VisitChild(attribExpr->mAttributes);
		VisitChild(attribExpr->mExpression);
	}		
}

void BfPrinter::Visit(BfStatement* stmt)
{	
	// Trailing semicolon must come after, so every derived type is responsible for printing it

#ifdef BF_AST_HAS_PARENT_MEMBER	
// 	if (stmt->mParent != NULL)
// 	{
// 		if (auto block = BfNodeDynCast<BfBlock>(stmt->mParent))
// 		{
// 			if (block->mOpenBrace != NULL)
// 			{
// 				if (mReformatting)
// 					BF_ASSERT(stmt == mCurBlockMember);
// 			}
// 		}
// 	}
#endif
		
	if (stmt == mCurBlockMember)
		ExpectNewLine();

	//Visit(stmt->ToBase());
}

void BfPrinter::Visit(BfLabelableStatement* labelableStmt)
{
	Visit(labelableStmt->ToBase());

	if (labelableStmt->mLabelNode != NULL)
	{
		ExpectNewLine();
		VisitChild(labelableStmt->mLabelNode->mLabel);
		VisitChild(labelableStmt->mLabelNode->mColonToken);
	}
}

void BfPrinter::Visit(BfCommentNode* commentNode)
{	
	WriteIgnoredNode(commentNode);
}

void BfPrinter::Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection)
{
	WriteIgnoredNode(preprocesorIgnoredSection);
}

void BfPrinter::Visit(BfPreprocessorNode* preprocessorNode)
{
	WriteIgnoredNode(preprocessorNode);
}

void BfPrinter::Visit(BfAttributeDirective* attributeDirective)
{
	Visit(attributeDirective->ToBase());
	
	if (attributeDirective->mAttrOpenToken != NULL)
	{
		if (attributeDirective->mAttrOpenToken->mToken == BfToken_Comma)
		{
			VisitChild(attributeDirective->mAttrOpenToken);
			ExpectSpace();
		}
		else
		{
			SetAndRestoreValue<bool> prevExpectNewLine(mExpectingNewLine, true);
			VisitChild(attributeDirective->mAttrOpenToken);
		}
	}
		
	if (attributeDirective->mAttributeTargetSpecifier != NULL)
	{
		if (auto attributeTargetSpecifier = BfNodeDynCast<BfAttributeTargetSpecifier>(attributeDirective->mAttributeTargetSpecifier))
		{
			VisitChild(attributeTargetSpecifier->mTargetToken);
			VisitChild(attributeTargetSpecifier->mColonToken);
			ExpectSpace();
		}
		else
		{
			VisitChild(attributeDirective->mAttributeTargetSpecifier);
		}
	}
	
	VisitChild(attributeDirective->mAttributeTypeRef);
	VisitChild(attributeDirective->mCtorOpenParen);
	for (int i = 0; i < (int)attributeDirective->mArguments.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(attributeDirective->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(attributeDirective->mArguments[i]);		
	}
	VisitChild(attributeDirective->mCtorCloseParen);
	VisitChild(attributeDirective->mAttrCloseToken);
	
	VisitChild(attributeDirective->mNextAttribute);
}

void BfPrinter::Visit(BfGenericParamsDeclaration* genericParams)
{
	Visit(genericParams->ToBase());

	VisitChild(genericParams->mOpenChevron);
	for (int i = 0; i < (int) genericParams->mGenericParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(genericParams->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(genericParams->mGenericParams[i]);		
	}
	VisitChild(genericParams->mCloseChevron);
}

void BfPrinter::Visit(BfGenericOperatorConstraint* genericConstraints)
{
	Visit(genericConstraints->ToBase());

	VisitChild(genericConstraints->mOperatorToken);
	ExpectSpace();
	VisitChild(genericConstraints->mLeftType);
	ExpectSpace();
	VisitChild(genericConstraints->mOpToken);
	ExpectSpace();
	VisitChild(genericConstraints->mRightType);
}

void BfPrinter::Visit(BfGenericConstraintsDeclaration* genericConstraints)
{
	Visit(genericConstraints->ToBase());

	for (auto genericConstraintNode : genericConstraints->mGenericConstraints)
	{
		auto genericConstraint = BfNodeDynCast<BfGenericConstraint>(genericConstraintNode);

		ExpectSpace();
		VisitChild(genericConstraint->mWhereToken);
		ExpectSpace();
		VisitChild(genericConstraint->mTypeRef);
		ExpectSpace();
		VisitChild(genericConstraint->mColonToken);
		ExpectSpace();
		for (int i = 0; i < (int)genericConstraint->mConstraintTypes.size(); i++)
		{
			if (i > 0)
			{
				if (!genericConstraint->mCommas.IsEmpty())
					VisitChild(genericConstraint->mCommas[i - 1]);
				ExpectSpace();
			}
			VisitChild(genericConstraint->mConstraintTypes[i]);			
		}
	}
}

void BfPrinter::Visit(BfGenericArgumentsNode* genericArgumentsNode)
{
	Visit(genericArgumentsNode->ToBase());

	VisitChild(genericArgumentsNode->mOpenChevron);
	for (int i = 0; i < (int)genericArgumentsNode->mGenericArgs.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(genericArgumentsNode->mCommas[i]);
			ExpectSpace();
		}
		VisitChild(genericArgumentsNode->mGenericArgs[i]);		
	}
	VisitChild(genericArgumentsNode->mCloseChevron);
}

void BfPrinter::Visit(BfEmptyStatement* emptyStmt)
{
	Visit(emptyStmt->ToBase());

	VisitChild(emptyStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfTokenNode* tokenNode)
{
	Visit(tokenNode->ToBase());

	if (mDocPrep)
	{
		if (tokenNode->mToken == BfToken_Mut)
			return;
	}

	if (tokenNode->GetSrcEnd() == 0)
	{		
		String tokenStr = BfTokenToString(tokenNode->GetToken());
		Write(tokenStr);
	}
	else
	{
		BF_ASSERT(tokenNode->GetSrcEnd() > tokenNode->GetSrcStart());
		WriteSourceString(tokenNode);
	}
}

void BfPrinter::Visit(BfLiteralExpression* literalExpr)
{
	Visit(literalExpr->ToBase());

	WriteSourceString(literalExpr);	
}

void BfPrinter::Visit(BfIdentifierNode* identifierNode)
{
	Visit(identifierNode->ToBase());	

	if (!CheckReplace(identifierNode))
		WriteSourceString(identifierNode);
}

void BfPrinter::Visit(BfQualifiedNameNode* nameNode)
{
	Visit((BfAstNode*)nameNode);

	VisitChild(nameNode->mLeft);
	VisitChild(nameNode->mDot);
	VisitChild(nameNode->mRight);
}

void BfPrinter::Visit(BfThisExpression* thisExpr)
{
	Visit((BfAstNode*)thisExpr);

	WriteSourceString(thisExpr);
}

void BfPrinter::Visit(BfBaseExpression* baseExpr)
{
	Visit((BfAstNode*)baseExpr);

	WriteSourceString(baseExpr);
}

void BfPrinter::Visit(BfMixinExpression* mixinExpr)
{
	Visit((BfAstNode*)mixinExpr);

	WriteSourceString(mixinExpr);
}

void BfPrinter::Visit(BfSizedArrayCreateExpression* createExpr)
{
	Visit(createExpr->ToBase());

	VisitChild(createExpr->mTypeRef);
	VisitChildWithPrecedingSpace(createExpr->mInitializer);
}

void BfPrinter::Visit(BfCollectionInitializerExpression* initExpr)
{
	Visit(initExpr->ToBase());

	VisitChild(initExpr->mOpenBrace);
	for (int i = 0; i < (int) initExpr->mValues.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(initExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(initExpr->mValues[i]);
	}
	VisitChild(initExpr->mCloseBrace);
}

void BfPrinter::Visit(BfTypeReference* typeRef)
{
	Visit(typeRef->ToBase());
}

void BfPrinter::Visit(BfNamedTypeReference* typeRef)
{
	Visit((BfAstNode*) typeRef);

	VisitChild(typeRef->mNameNode);
}

void BfPrinter::Visit(BfQualifiedTypeReference* qualifiedTypeRef)
{
	Visit(qualifiedTypeRef->ToBase());

	VisitChild(qualifiedTypeRef->mLeft);
	VisitChild(qualifiedTypeRef->mDot);
	VisitChild(qualifiedTypeRef->mRight);
}

void BfPrinter::Visit(BfVarTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mVarToken);
}

void BfPrinter::Visit(BfLetTypeReference* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mLetToken);
}

void BfPrinter::Visit(BfConstTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mConstToken);
	ExpectSpace();
	VisitChild(typeRef->mElementType);
}

void BfPrinter::Visit(BfConstExprTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	if (typeRef->mConstToken != NULL)
	{
		VisitChild(typeRef->mConstToken);
		ExpectSpace();
	}
	VisitChild(typeRef->mConstExpr);
}

void BfPrinter::Visit(BfRefTypeRef* typeRef)
{
	Visit(typeRef->ToBase());

	VisitChild(typeRef->mRefToken);
	ExpectSpace();
	VisitChild(typeRef->mElementType);
}

void BfPrinter::Visit(BfArrayTypeRef* arrayTypeRef)
{
	Visit((BfAstNode*)arrayTypeRef);
	
 	std::function<void(BfTypeReference*)> _VisitElements = [&](BfTypeReference* elementRef)
 	{
 		auto arrType = BfNodeDynCast<BfArrayTypeRef>(elementRef);
 		
 		if (arrType != NULL)
 		{
			_VisitElements(arrType->mElementType);
 		}
 		else
 		{
 			VisitChild(elementRef);
 		}
 	};

	std::function<void(BfArrayTypeRef*)> _VisitBrackets = [&](BfArrayTypeRef* arrayTypeRef)
	{
		VisitChild(arrayTypeRef->mOpenBracket);
		for (int paramIdx = 0; paramIdx < (int)arrayTypeRef->mParams.size(); paramIdx++)
		{
			auto param = arrayTypeRef->mParams[paramIdx];
			if (paramIdx > 0)
			{
				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(param))
				{

				}
				else
				{
					ExpectSpace();
				}
			}

			VisitChild(param);
		}
		VisitChild(arrayTypeRef->mCloseBracket);

		if (auto innerArrayType = BfNodeDynCast<BfArrayTypeRef>(arrayTypeRef->mElementType))
			_VisitBrackets(innerArrayType);
	};

	_VisitElements(arrayTypeRef);
	_VisitBrackets(arrayTypeRef);
  	
}

void BfPrinter::Visit(BfGenericInstanceTypeRef* genericInstTypeRef)
{
	Visit((BfAstNode*) genericInstTypeRef);

	VisitChild(genericInstTypeRef->mElementType);
	VisitChild(genericInstTypeRef->mOpenChevron);
	if (genericInstTypeRef->mGenericArguments.size() > 0)
	{
		for (int i = 0; i < (int)genericInstTypeRef->mGenericArguments.size(); i++)
		{
			if (i > 0)
			{
				VisitChild(genericInstTypeRef->mCommas[i - 1]);
				ExpectSpace();
			}
			VisitChild(genericInstTypeRef->mGenericArguments[i]);
		}
	}
	else
	{
		for (auto comma : genericInstTypeRef->mCommas)
			VisitChild(comma);
	}
	VisitChild(genericInstTypeRef->mCloseChevron);
}

void BfPrinter::Visit(BfTupleTypeRef* typeRef)
{
	Visit((BfAstNode*) typeRef);

	VisitChild(typeRef->mOpenParen);	
	for (int i = 0; i < (int)typeRef->mFieldTypes.size(); i++)
	{		
		if (i > 0)
		{
			VisitChild(typeRef->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(typeRef->mFieldTypes[i]);

		if (i < (int)typeRef->mFieldNames.size())
		{
			ExpectSpace();
			auto fieldNameNode = typeRef->mFieldNames[i];
			if (fieldNameNode != NULL)			
				VisitChild(fieldNameNode);
		}
	}
	VisitChild(typeRef->mCloseParen);
}

void BfPrinter::Visit(BfDelegateTypeRef* typeRef)
{
	Visit((BfAstNode*)typeRef);

	VisitChild(typeRef->mTypeToken);
	ExpectSpace();
	VisitChild(typeRef->mReturnType);
	VisitChild(typeRef->mAttributes);
	VisitChild(typeRef->mOpenParen);

	for (int i = 0; i < (int)typeRef->mParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(typeRef->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(typeRef->mParams[i]);	
	}	
	VisitChild(typeRef->mCloseParen);
}

void BfPrinter::Visit(BfPointerTypeRef* ptrType)
{
	Visit((BfAstNode*)ptrType);

	VisitChild(ptrType->mElementType);
	VisitChild(ptrType->mStarNode);
}

void BfPrinter::Visit(BfNullableTypeRef* ptrType)
{
	Visit((BfAstNode*) ptrType);

	VisitChild(ptrType->mElementType);
	VisitChild(ptrType->mQuestionToken);
}

void BfPrinter::Visit(BfVariableDeclaration* varDecl)
{
	//Visit(varDecl->ToBase());

	VisitChildWithProceedingSpace(varDecl->mModSpecifier);

	if (varDecl->mPrecedingComma != NULL)
	{
		mNextStateModify.mWantNewLineIdx--;
		VisitChild(varDecl->mPrecedingComma);
	}
	else
		VisitChild(varDecl->mTypeRef);
			
	VisitChildWithPrecedingSpace(varDecl->mNameNode);
	VisitChildWithPrecedingSpace(varDecl->mEqualsNode);
	
	if (varDecl->mInitializer != NULL)
	{
		if (varDecl->mNameNode != NULL)
			ExpectSpace();
		VisitChild(varDecl->mInitializer);
	}	
}

void BfPrinter::Visit(BfParameterDeclaration* paramDecl)
{
	if (paramDecl->mModToken != NULL)
	{
		VisitChild(paramDecl->mModToken);
		ExpectSpace();
	}

	Visit(paramDecl->ToBase());
}

void BfPrinter::Visit(BfParamsExpression* paramsExpr)
{
	Visit(paramsExpr->ToBase());

	VisitChild(paramsExpr->mParamsToken);
}

void BfPrinter::Visit(BfTypeOfExpression* typeOfExpr)
{
	Visit(typeOfExpr->ToBase());

	VisitChild(typeOfExpr->mToken);
	VisitChild(typeOfExpr->mOpenParen);
	VisitChild(typeOfExpr->mTypeRef);
	VisitChild(typeOfExpr->mCloseParen);	
}

void BfPrinter::Visit(BfSizeOfExpression* sizeOfExpr)
{
	Visit(sizeOfExpr->ToBase());

	VisitChild(sizeOfExpr->mToken);
	VisitChild(sizeOfExpr->mOpenParen);
	VisitChild(sizeOfExpr->mTypeRef);
	VisitChild(sizeOfExpr->mCloseParen);
}

void BfPrinter::Visit(BfDefaultExpression* defaultExpr)
{
	Visit(defaultExpr->ToBase());

	VisitChild(defaultExpr->mDefaultToken);
	VisitChild(defaultExpr->mOpenParen);
	VisitChild(defaultExpr->mTypeRef);
	VisitChild(defaultExpr->mCloseParen);
}

void BfPrinter::Visit(BfCheckTypeExpression* checkTypeExpr)
{
	Visit(checkTypeExpr->ToBase());

	VisitChild(checkTypeExpr->mTarget);
	ExpectSpace();
	VisitChild(checkTypeExpr->mIsToken);
	ExpectSpace();
	VisitChild(checkTypeExpr->mTypeRef);
}

void BfPrinter::Visit(BfDynamicCastExpression* dynCastExpr)
{
	Visit(dynCastExpr->ToBase());

	VisitChild(dynCastExpr->mTarget);
	ExpectSpace();
	VisitChild(dynCastExpr->mAsToken);
	ExpectSpace();
	VisitChild(dynCastExpr->mTypeRef);
}

void BfPrinter::Visit(BfCastExpression* castExpr)
{	
	Visit((BfAstNode*)castExpr);

	VisitChild(castExpr->mOpenParen);
	VisitChild(castExpr->mTypeRef);
	VisitChild(castExpr->mCloseParen);

	bool surroundWithParen = false;
	
	if (surroundWithParen)
		Write("(");
	VisitChild(castExpr->mExpression);
	if (surroundWithParen)
		Write(")");
}

void BfPrinter::Visit(BfDelegateBindExpression* bindExpr)
{
	Visit(bindExpr->ToBase());

	VisitChild(bindExpr->mNewToken);
	ExpectSpace();
	VisitChild(bindExpr->mFatArrowToken);
	ExpectSpace();
	VisitChild(bindExpr->mTarget);
	VisitChild(bindExpr->mGenericArgs);
}

void BfPrinter::Visit(BfLambdaBindExpression* lambdaBindExpr)
{
	Visit(lambdaBindExpr->ToBase());

	VisitChild(lambdaBindExpr->mNewToken);
	ExpectSpace();	
	VisitChild(lambdaBindExpr->mOpenParen);
	for (int i = 0; i < (int)lambdaBindExpr->mParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(lambdaBindExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(lambdaBindExpr->mParams[i]);		
	}
	VisitChild(lambdaBindExpr->mCloseParen);
	ExpectSpace();
	VisitChild(lambdaBindExpr->mFatArrowToken);	
	if (lambdaBindExpr->mBody != NULL)
	{
		if (lambdaBindExpr->mBody->IsA<BfBlock>())
		{
			ExpectNewLine();
			ExpectIndent();
			VisitChild(lambdaBindExpr->mBody);			
			ExpectUnindent();						
		}
		else
		{
			ExpectSpace();
			VisitChild(lambdaBindExpr->mBody);		
		}
	}	
}

void BfPrinter::Visit(BfObjectCreateExpression* newExpr)
{
	Visit(newExpr->ToBase());

	VisitChild(newExpr->mNewNode);
	ExpectSpace();
	
	VisitChild(newExpr->mTypeRef);	

	VisitChild(newExpr->mOpenToken);
	for (int i = 0; i < (int)newExpr->mArguments.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(newExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(newExpr->mArguments[i]);		
	}
	VisitChild(newExpr->mCloseToken);		

	if (newExpr->mStarToken != NULL)
	{
		VisitChild(newExpr->mStarToken);
		ExpectSpace();
	}
}

void BfPrinter::Visit(BfBoxExpression* boxExpr)
{
	Visit(boxExpr->ToBase());

	VisitChild(boxExpr->mAllocNode);
	ExpectSpace();
	VisitChild(boxExpr->mBoxToken);
	ExpectSpace();
	VisitChild(boxExpr->mExpression);	
}

void BfPrinter::Visit(BfThrowStatement* throwStmt)
{
	Visit(throwStmt->ToBase());

	VisitChild(throwStmt->mThrowToken);
	ExpectSpace();
	VisitChild(throwStmt->mExpression);

	VisitChild(throwStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfDeleteStatement* deleteStmt)
{
	Visit(deleteStmt->ToBase());

	VisitChild(deleteStmt->mDeleteToken);
	ExpectSpace();
	VisitChild(deleteStmt->mExpression);

	VisitChild(deleteStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfInvocationExpression* invocationExpr)
{
	Visit(invocationExpr->ToBase());

	VisitChild(invocationExpr->mTarget);
	VisitChild(invocationExpr->mGenericArgs);
	VisitChild(invocationExpr->mOpenParen);
	for (int i = 0; i < (int) invocationExpr->mArguments.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(invocationExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(invocationExpr->mArguments[i]);
	}
	VisitChild(invocationExpr->mCloseParen);
}

void BfPrinter::Visit(BfDeferStatement* deferStmt)
{
	Visit(deferStmt->ToBase());

	VisitChild(deferStmt->mDeferToken);
	VisitChild(deferStmt->mColonToken);
	VisitChild(deferStmt->mScopeToken);

	if (deferStmt->mBind != NULL)
	{
		auto bind = deferStmt->mBind;
		
		VisitChild(bind->mOpenBracket);		
		for (int i = 0; i < bind->mParams.size(); i++)
		{
			if (i > 0)
			{
				VisitChild(bind->mCommas[i - 1]);
				ExpectSpace();
			}
			VisitChild(bind->mParams[i]);
		}
		VisitChild(bind->mCloseBracket);
	}

	VisitChild(deferStmt->mOpenParen);
	VisitChild(deferStmt->mScopeToken);
	VisitChild(deferStmt->mCloseParen);		
	ExpectSpace();
	VisitChild(deferStmt->mTargetNode);
	
	VisitChild(deferStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfEnumCaseBindExpression* caseBindExpr)
{
	Visit(caseBindExpr->ToBase());

	VisitChild(caseBindExpr->mBindToken);
	VisitChild(caseBindExpr->mEnumMemberExpr);
	VisitChild(caseBindExpr->mBindNames);
}

void BfPrinter::Visit(BfCaseExpression* caseExpr)
{
	Visit(caseExpr->ToBase());

	if ((caseExpr->mValueExpression == NULL) || (caseExpr->mCaseToken->GetSrcStart() < caseExpr->mValueExpression->GetSrcStart()))
	{
		// Old version
		VisitChild(caseExpr->mCaseToken);
		ExpectSpace();
		VisitChild(caseExpr->mCaseExpression);
		ExpectSpace();
		VisitChild(caseExpr->mEqualsNode);
		ExpectSpace();
		VisitChild(caseExpr->mValueExpression);
	}
	else
	{
		VisitChild(caseExpr->mValueExpression); 
		ExpectSpace();
		VisitChild(caseExpr->mCaseToken);						
		BF_ASSERT(caseExpr->mEqualsNode == NULL);
		ExpectSpace();
		VisitChild(caseExpr->mCaseExpression);
	}	
}

void BfPrinter::Visit(BfSwitchCase* switchCase)
{
	VisitChild(switchCase->mCaseToken);	
	for (int caseIdx = 0; caseIdx < (int) switchCase->mCaseExpressions.size(); caseIdx++)
	{
		if ((caseIdx == 0) || (caseIdx > (int)switchCase->mCaseCommas.size()))
			ExpectSpace();
		else
			VisitChild(switchCase->mCaseCommas[caseIdx - 1]);
		VisitChild(switchCase->mCaseExpressions[caseIdx]);
	}
	VisitChild(switchCase->mColonToken);
	ExpectNewLine();
	ExpectIndent();
	VisitChild(switchCase->mCodeBlock);
	ExpectUnindent();
	if (switchCase->mEndingToken != NULL)
	{
		ExpectNewLine();
		VisitChild(switchCase->mEndingToken);
		VisitChild(switchCase->mEndingSemicolonToken);
		ExpectNewLine();
	}
}

void BfPrinter::Visit(BfWhenExpression* whenExpr)
{
	VisitChild(whenExpr->mWhenToken);
	ExpectSpace();
	VisitChild(whenExpr->mExpression);
}

void BfPrinter::Visit(BfSwitchStatement* switchStmt)
{
	Visit(switchStmt->ToBase());

	VisitChild(switchStmt->mSwitchToken);
	ExpectSpace();
	VisitChild(switchStmt->mOpenParen);
	VisitChild(switchStmt->mSwitchValue);
	VisitChild(switchStmt->mCloseParen);
	ExpectNewLine();

	VisitChild(switchStmt->mOpenBrace);
	ExpectNewLine();	
	
	for (auto switchCase : switchStmt->mSwitchCases)
		Visit(switchCase);
	if (switchStmt->mDefaultCase != NULL)
		Visit(switchStmt->mDefaultCase);
	
	ExpectNewLine();
	VisitChild(switchStmt->mCloseBrace);

	VisitChild(switchStmt->mTrailingSemicolon);
}


void BfPrinter::Visit(BfTryStatement* tryStmt)
{
	Visit(tryStmt->ToBase());
	
	VisitChild(tryStmt->mTryToken);
	VisitChild(tryStmt->mStatement);
}

void BfPrinter::Visit(BfCatchStatement* catchStmt)
{	
	Visit(catchStmt->ToBase());

	WriteSourceString(catchStmt);

	VisitChild(catchStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfFinallyStatement* finallyStmt)
{
	Visit(finallyStmt->ToBase());

	VisitChild(finallyStmt->mFinallyToken);
	VisitChild(finallyStmt->mStatement);
}

void BfPrinter::Visit(BfCheckedStatement* checkedStmt)
{
	Visit(checkedStmt->ToBase());

	VisitChild(checkedStmt->mCheckedToken);
	VisitChild(checkedStmt->mStatement);
}

void BfPrinter::Visit(BfUncheckedStatement* uncheckedStmt)
{
	Visit(uncheckedStmt->ToBase());

	VisitChild(uncheckedStmt->mUncheckedToken);
	VisitChild(uncheckedStmt->mStatement);
}

void BfPrinter::Visit(BfIfStatement* ifStmt)
{
	Visit(ifStmt->ToBase());

	VisitChild(ifStmt->mIfToken);
	ExpectSpace();
	VisitChild(ifStmt->mOpenParen);	
	VisitChild(ifStmt->mCondition);
	VisitChild(ifStmt->mCloseParen);
	ExpectSpace();	
	VisitChildNextLine(ifStmt->mTrueStatement);		
	VisitChild(ifStmt->mElseToken);	
	if (ifStmt->mFalseStatement != NULL)
	{
		ExpectSpace();
		if (ifStmt->mFalseStatement->IsA<BfIfStatement>())
			VisitChild(ifStmt->mFalseStatement);
		else
			VisitChildNextLine(ifStmt->mFalseStatement);
	}

	VisitChild(ifStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfReturnStatement* returnStmt)
{	
	Visit(returnStmt->ToBase());
	
	VisitChild(returnStmt->mReturnToken);
	if (returnStmt->mExpression != NULL)
	{
		ExpectSpace();
		VisitChild(returnStmt->mExpression);
	}
	VisitChild(returnStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfUsingStatement* doStmt)
{	
	Visit(doStmt->ToBase());

	VisitChild(doStmt->mUsingToken);
	ExpectSpace();
	VisitChild(doStmt->mOpenParen);
	VisitChild(doStmt->mVariableDeclaration);
	VisitChild(doStmt->mCloseParen);
	VisitChildNextLine(doStmt->mEmbeddedStatement);

	VisitChild(doStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfDoStatement* doStmt)
{
	Visit(doStmt->ToBase());

	VisitChild(doStmt->mDoToken);
	VisitChildNextLine(doStmt->mEmbeddedStatement);	
}

void BfPrinter::Visit(BfRepeatStatement* repeatStmt)
{
	Visit(repeatStmt->ToBase());

	VisitChild(repeatStmt->mRepeatToken);	
	VisitChildNextLine(repeatStmt->mEmbeddedStatement);
	VisitChild(repeatStmt->mWhileToken);
	ExpectSpace();
	VisitChild(repeatStmt->mOpenParen);
	VisitChild(repeatStmt->mCondition);
	VisitChild(repeatStmt->mCloseParen);	

	VisitChild(repeatStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfWhileStatement* whileStmt)
{
	Visit(whileStmt->ToBase());

	VisitChild(whileStmt->mWhileToken);	
	ExpectSpace();
	VisitChild(whileStmt->mOpenParen);
	VisitChild(whileStmt->mCondition);
	VisitChild(whileStmt->mCloseParen);
	VisitChildNextLine(whileStmt->mEmbeddedStatement);

	VisitChild(whileStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfBreakStatement* breakStmt)
{
	Visit(breakStmt->ToBase());

	VisitChild(breakStmt->mBreakNode);
	if (breakStmt->mLabel != NULL)
	{
		ExpectSpace();
		VisitChild(breakStmt->mLabel);
	}
	VisitChild(breakStmt->mTrailingSemicolon);	
}

void BfPrinter::Visit(BfContinueStatement* continueStmt)
{
	Visit(continueStmt->ToBase());

	VisitChild(continueStmt->mContinueNode);
	if (continueStmt->mLabel != NULL)
	{
		ExpectSpace();
		VisitChild(continueStmt->mLabel);
	}
	VisitChild(continueStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfFallthroughStatement* fallthroughStmt)
{
	Visit(fallthroughStmt->ToBase());

	VisitChild(fallthroughStmt->mFallthroughToken);

	VisitChild(fallthroughStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfForStatement* forStmt)
{
	Visit(forStmt->ToBase());

	VisitChild(forStmt->mForToken);
	ExpectSpace();
	VisitChild(forStmt->mOpenParen);
	for (int i = 0; i < (int) forStmt->mInitializers.size(); i++)
	{
		if (i > 0)
			VisitChild(forStmt->mInitializerCommas[i - 1]);
		VisitChild(forStmt->mInitializers[i]);
	}
	VisitChild(forStmt->mInitializerSemicolon);
	ExpectSpace();
	VisitChild(forStmt->mCondition);
	VisitChild(forStmt->mConditionSemicolon);
	ExpectSpace();
	for (int i = 0; i < (int) forStmt->mIterators.size(); i++)
	{
		if (i > 0)
			VisitChild(forStmt->mIteratorCommas[i - 1]);
		VisitChild(forStmt->mIterators[i]);
	}
	VisitChild(forStmt->mCloseParen);
	VisitChildNextLine(forStmt->mEmbeddedStatement);
}

void BfPrinter::Visit(BfForEachStatement* forEachStmt)
{
	Visit(forEachStmt->ToBase());	

	VisitChild(forEachStmt->mForToken);
	ExpectSpace();
	VisitChild(forEachStmt->mOpenParen);	
	if (forEachStmt->mReadOnlyToken != NULL)
	{
		VisitChild(forEachStmt->mReadOnlyToken);
		ExpectSpace();
	}
	VisitChild(forEachStmt->mVariableTypeRef);
	ExpectSpace();
	VisitChild(forEachStmt->mVariableName);
	ExpectSpace();
	VisitChild(forEachStmt->mInToken);
	ExpectSpace();
	VisitChild(forEachStmt->mCollectionExpression);	
	VisitChild(forEachStmt->mCloseParen);
	ExpectNewLine();
	VisitChildNextLine(forEachStmt->mEmbeddedStatement);

	VisitChild(forEachStmt->mTrailingSemicolon);
}

void BfPrinter::Visit(BfConditionalExpression* condExpr)
{
	Visit(condExpr->ToBase());

	VisitChild(condExpr->mConditionExpression);
	ExpectSpace();
	VisitChild(condExpr->mQuestionToken);
	ExpectSpace();
	VisitChild(condExpr->mTrueExpression);
	ExpectSpace();
	VisitChild(condExpr->mColonToken);
	ExpectSpace();
	VisitChild(condExpr->mFalseExpression);
}

void BfPrinter::Visit(BfAssignmentExpression* assignExpr)
{
	Visit(assignExpr->ToBase());

	VisitChild(assignExpr->mLeft);
	ExpectSpace();
	VisitChild(assignExpr->mOpToken);
	ExpectSpace();
	VisitChild(assignExpr->mRight);
}

void BfPrinter::Visit(BfParenthesizedExpression* parenExpr)
{
	Visit(parenExpr->ToBase());

	VisitChild(parenExpr->mOpenParen);	
	VisitChild(parenExpr->mExpression);	
	VisitChild(parenExpr->mCloseParen);
}

void BfPrinter::Visit(BfTupleExpression* tupleExpr)
{
	Visit(tupleExpr->ToBase());

	VisitChild(tupleExpr->mOpenParen);
	for (int i = 0; i < (int)tupleExpr->mValues.size(); i++)
	{
		if (i < (int)tupleExpr->mNames.size())
		{
			auto nameNode = tupleExpr->mNames[i];
			if (nameNode != NULL)
			{
				VisitChild(nameNode->mNameNode);
				VisitChild(nameNode->mColonToken);
				ExpectSpace();
			}
		}

		if (i > 0)
		{
			VisitChild(tupleExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(tupleExpr->mValues[i]);
	}

	VisitChild(tupleExpr->mCloseParen);
}

void BfPrinter::Visit(BfMemberReferenceExpression* memberRefExpr)
{
	Visit(memberRefExpr->ToBase());

	VisitChild(memberRefExpr->mTarget);
	VisitChild(memberRefExpr->mDotToken);
	VisitChild(memberRefExpr->mMemberName);
}

void BfPrinter::Visit(BfIndexerExpression* indexerExpr)
{
	Visit(indexerExpr->ToBase());

	VisitChild(indexerExpr->mTarget);
	VisitChild(indexerExpr->mOpenBracket);
	for (int i = 0; i < (int)indexerExpr->mArguments.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(indexerExpr->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(indexerExpr->mArguments[i]);		
	}

	VisitChild(indexerExpr->mCloseBracket);
}

void BfPrinter::Visit(BfUnaryOperatorExpression* unaryOpExpr)
{
	Visit(unaryOpExpr->ToBase());

	bool postOp = (unaryOpExpr->mOp == BfUnaryOp_PostIncrement) || (unaryOpExpr->mOp == BfUnaryOp_PostDecrement);
	if (!postOp)
		VisitChild(unaryOpExpr->mOpToken);
	if ((unaryOpExpr->mOp == BfUnaryOp_Ref) || (unaryOpExpr->mOp == BfUnaryOp_Mut) || (unaryOpExpr->mOp == BfUnaryOp_Out) || (unaryOpExpr->mOp == BfUnaryOp_Params))
		ExpectSpace();
	VisitChild(unaryOpExpr->mExpression);
	if (postOp)
		VisitChild(unaryOpExpr->mOpToken);
}

void BfPrinter::Visit(BfBinaryOperatorExpression* binOpExpr)
{
	//Visit(binOpExpr->ToBase());

	VisitChild(binOpExpr->mLeft);
	ExpectSpace();
	VisitChild(binOpExpr->mOpToken);
	ExpectSpace();
	VisitChild(binOpExpr->mRight);
}

void BfPrinter::Visit(BfConstructorDeclaration* ctorDeclaration)
{
	//Visit((BfAstNode*)ctorDeclaration);

	QueueVisitChild(ctorDeclaration->mAttributes);
	ExpectNewLine();
	QueueVisitChild(ctorDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(ctorDeclaration->mInternalSpecifier);
	ExpectSpace();
	QueueVisitChild(ctorDeclaration->mNewSpecifier);
	ExpectSpace();
	QueueVisitChild(ctorDeclaration->mStaticSpecifier);	
	ExpectSpace();

	if (mDocPrep)
	{
		FlushVisitChild();
		Write(" ");
		VisitChild(mCurTypeDecl->mNameNode);
	}
	else
	{
		QueueVisitChild(ctorDeclaration->mThisToken);
	}	

	QueueVisitChild(ctorDeclaration->mOpenParen);
	for (int i = 0; i < (int) ctorDeclaration->mParams.size(); i++)
	{
		if (i > 0)
		{
			QueueVisitChild(ctorDeclaration->mCommas[i - 1]);
			ExpectSpace();
		}
		QueueVisitChild(ctorDeclaration->mParams[i]);
	}
	QueueVisitChild(ctorDeclaration->mCloseParen);
	ExpectSpace();
	QueueVisitChild(ctorDeclaration->mInitializerColonToken);
	ExpectSpace();
	QueueVisitChild(ctorDeclaration->mInitializer);

	QueueVisitChild(ctorDeclaration->mFatArrowToken);
	QueueVisitChild(ctorDeclaration->mBody);
	
	FlushVisitChild();
}

void BfPrinter::Visit(BfDestructorDeclaration* dtorDeclaration)
{
	Visit((BfAstNode*)dtorDeclaration);

	QueueVisitChild(dtorDeclaration->mAttributes);
	ExpectNewLine();
	QueueVisitChild(dtorDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(dtorDeclaration->mInternalSpecifier);
	ExpectSpace();
	QueueVisitChild(dtorDeclaration->mNewSpecifier);
	ExpectSpace();
	QueueVisitChild(dtorDeclaration->mStaticSpecifier);
	ExpectSpace();
	QueueVisitChild(dtorDeclaration->mTildeToken);

	if (mDocPrep)
	{
		FlushVisitChild();		
		VisitChild(mCurTypeDecl->mNameNode);
	}
	else
	{
		QueueVisitChild(dtorDeclaration->mThisToken);
	}
	
	QueueVisitChild(dtorDeclaration->mOpenParen);
	for (int i = 0; i < (int) dtorDeclaration->mParams.size(); i++)
	{
		if (i > 0)
		{
			QueueVisitChild(dtorDeclaration->mCommas[i - 1]);
			ExpectSpace();
		}
		QueueVisitChild(dtorDeclaration->mParams[i]);
	}
	QueueVisitChild(dtorDeclaration->mCloseParen);	

	QueueVisitChild(dtorDeclaration->mFatArrowToken);
	QueueVisitChild(dtorDeclaration->mBody);

	FlushVisitChild();
}


void BfPrinter::QueueMethodDeclaration(BfMethodDeclaration* methodDeclaration)
{	
	if (methodDeclaration->mAttributes != NULL)
	{
		QueueVisitChild(methodDeclaration->mAttributes);
		ExpectNewLine();
	}
	
	if (methodDeclaration->mExternSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mExternSpecifier);		
	}

	if (methodDeclaration->mProtectionSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mProtectionSpecifier);		
	}

	if (methodDeclaration->mInternalSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mInternalSpecifier);		
	}

	if (methodDeclaration->mNewSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mNewSpecifier);		
	}
	
	if (methodDeclaration->mVirtualSpecifier != NULL)
	{	
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mVirtualSpecifier);		
	}
	
	if (methodDeclaration->mStaticSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mStaticSpecifier);		
	}

	if (methodDeclaration->mMixinSpecifier != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mMixinSpecifier);		
	}

	if (methodDeclaration->mPartialSpecifier != NULL)
	{			
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mPartialSpecifier);		
	}
	
	if ((methodDeclaration->mNameNode != NULL) || (methodDeclaration->mExplicitInterface != NULL))
		ExpectSpace();
	QueueVisitChild(methodDeclaration->mExplicitInterface);	
	QueueVisitChild(methodDeclaration->mExplicitInterfaceDotToken);
	QueueVisitChild(methodDeclaration->mNameNode);
	
	if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
	{
		if ((operatorDecl->mOpTypeToken != NULL) && (operatorDecl->mOpTypeToken->mToken == BfToken_LChevron))
			ExpectSpace();
	}
	QueueVisitChild(methodDeclaration->mGenericParams);

	if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(methodDeclaration))
	{	
		ExpectSpace();
		QueueVisitChild(operatorDecl->mExplicitToken);
		ExpectSpace();
		QueueVisitChild(operatorDecl->mOperatorToken);		
		QueueVisitChild(operatorDecl->mOpTypeToken);
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mReturnType);		
	}
	else if (methodDeclaration->mReturnType != NULL)
	{		
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mReturnType);		
	}

	QueueVisitChild(methodDeclaration->mOpenParen);
	for (int i = 0; i < (int) methodDeclaration->mParams.size(); i++)
	{
		if (i > 0)
		{
			QueueVisitChild(methodDeclaration->mCommas[i - 1]);
			ExpectSpace();
		}		
		QueueVisitChild(methodDeclaration->mParams[i]);
	}
	QueueVisitChild(methodDeclaration->mCloseParen);
	ExpectSpace();
	QueueVisitChild(methodDeclaration->mMutSpecifier);
	ExpectSpace();
	QueueVisitChild(methodDeclaration->mGenericConstraintsDeclaration);
	if (methodDeclaration->mFatArrowToken != NULL)
	{
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mFatArrowToken);
		ExpectSpace();
	}	
	QueueVisitChild(methodDeclaration->mBody);
	QueueVisitChild(methodDeclaration->mEndSemicolon);	
}

void BfPrinter::Visit(BfMethodDeclaration* methodDeclaration)
{
	//Visit(methodDeclaration->ToBase());
	
	ExpectNewLine();
	QueueMethodDeclaration(methodDeclaration);
	//?? QueueVisitErrorNodes(methodDeclaration);

	FlushVisitChild();
	ExpectNewLine();
}

void BfPrinter::Visit(BfOperatorDeclaration* opreratorDeclaration)
{
	Visit(opreratorDeclaration->ToBase());
}

void BfPrinter::Visit(BfPropertyMethodDeclaration* propertyMethodDeclaration)
{	
	ExpectNewLine();
	QueueVisitChild(propertyMethodDeclaration->mAttributes);
	ExpectNewLine();
	QueueVisitChild(propertyMethodDeclaration->mProtectionSpecifier);
	ExpectSpace();		
	QueueVisitChild(propertyMethodDeclaration->mNameNode);
	ExpectSpace();
	QueueVisitChild(propertyMethodDeclaration->mMutSpecifier);
	ExpectSpace();
	if (auto block = BfNodeDynCast<BfBlock>(propertyMethodDeclaration->mBody))	
		ExpectNewLine();	
	QueueVisitChild(propertyMethodDeclaration->mBody);
}

void BfPrinter::Visit(BfPropertyDeclaration* propertyDeclaration)
{	
	auto indexerDeclaration = BfNodeDynCast<BfIndexerDeclaration>(propertyDeclaration);

	ExpectNewLine();
	QueueVisitChild(propertyDeclaration->mAttributes);
	ExpectNewLine();
	QueueVisitChild(propertyDeclaration->mInternalSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mConstSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mVolatileSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mVirtualSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mExternSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mStaticSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mTypeRef);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mExplicitInterface);
	QueueVisitChild(propertyDeclaration->mExplicitInterfaceDotToken);
	QueueVisitChild(propertyDeclaration->mNameNode);
	ExpectSpace();
	
	if (indexerDeclaration != NULL)
	{
		QueueVisitChild(indexerDeclaration->mThisToken);		
		QueueVisitChild(indexerDeclaration->mOpenBracket);		
		for (int i = 0; i < (int)indexerDeclaration->mParams.size(); i++)
		{
			if (i > 0)
			{
				QueueVisitChild(indexerDeclaration->mCommas[i - 1]);
				ExpectSpace();
			}
			QueueVisitChild(indexerDeclaration->mParams[i]);
		}
		QueueVisitChild(indexerDeclaration->mCloseBracket);
		ExpectSpace();
	}

	QueueVisitChild(propertyDeclaration->mEqualsNode);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mInitializer);

	if (auto block = BfNodeDynCast<BfBlock>(propertyDeclaration->mDefinitionBlock))
	{
		bool doInlineBlock = false;		
		DoBlockOpen(block, true, &doInlineBlock);
		for (auto method : propertyDeclaration->mMethods)
		{
			Visit(method);
		}		
		DoBlockClose(block, true, doInlineBlock);
	}
	else
	{
		QueueVisitChild(propertyDeclaration->mDefinitionBlock);
		ExpectSpace();
		for (auto method : propertyDeclaration->mMethods)
		{
			QueueVisitChild(method->mBody);
		}
	}

	//QueueVisitChild(propertyDeclaration->mTrailingSemicolon);

	// ??? QueueVisitErrorNodes(propertyDeclaration);
	FlushVisitChild();
}

void BfPrinter::Visit(BfFieldDeclaration* fieldDeclaration)
{	
	if (fieldDeclaration->mPrecedingComma != NULL)
	{
		VisitChild(fieldDeclaration->mPrecedingComma);
		ExpectSpace();
		VisitChild(fieldDeclaration->mNameNode);
		return;
	}

	ExpectNewLine();
	if (fieldDeclaration->mAttributes != NULL)
	{
		QueueVisitChild(fieldDeclaration->mAttributes);
		ExpectNewLine();
	}
	QueueVisitChild(fieldDeclaration->mInternalSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mConstSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mReadOnlySpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mVolatileSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mNewSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mStaticSpecifier);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mPrecedingComma);
	ExpectSpace();	
	QueueVisitChild(fieldDeclaration->mTypeRef);
	ExpectSpace();	
	QueueVisitChild(fieldDeclaration->mNameNode);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mEqualsNode);
	ExpectSpace();
	QueueVisitChild(fieldDeclaration->mInitializer);

	auto fieldDtor = fieldDeclaration->mFieldDtor;
	while (fieldDtor != NULL)
	{
		ExpectSpace();
		QueueVisitChild(fieldDtor->mTildeToken);
		ExpectSpace();
		QueueVisitChild(fieldDtor->mBody);
		fieldDtor = fieldDtor->mNextFieldDtor;
	}
	
	mNextStateModify.mExpectingSpace = false;
	FlushVisitChild();
}

void BfPrinter::Visit(BfEnumCaseDeclaration* enumCaseDeclaration)
{
	Visit(enumCaseDeclaration->ToBase());

	if (mDocPrep)
	{
		for (int i = 0; i < (int)enumCaseDeclaration->mEntries.size(); i++)
		{
			auto fieldDecl = enumCaseDeclaration->mEntries[i];
			Visit((BfAstNode*)fieldDecl);
			Write("public static ");
			Visit(mCurTypeDecl->mNameNode);
			Write(" ");
			VisitChild(fieldDecl);
			Write(";");
		}
		return;
	}

	VisitChild(enumCaseDeclaration->mCaseToken);
	ExpectSpace();	

	for (int i = 0; i < (int)enumCaseDeclaration->mEntries.size(); i++)
	{
		if (i > 0)
		{
			VisitChild(enumCaseDeclaration->mCommas[i - 1]);
			ExpectSpace();
		}
		VisitChild(enumCaseDeclaration->mEntries[i]);
	}
}

void BfPrinter::Visit(BfTypeAliasDeclaration* typeDeclaration)
{
	Visit(typeDeclaration->ToBase());

	ExpectSpace();
	VisitChild(typeDeclaration->mEqualsToken);
	ExpectSpace();
	VisitChild(typeDeclaration->mAliasToType);
	VisitChild(typeDeclaration->mEndSemicolon);
}

void BfPrinter::Visit(BfTypeDeclaration* typeDeclaration)
{
	SetAndRestoreValue<BfTypeDeclaration*> prevTypeDecl(mCurTypeDecl, typeDeclaration);

	//Visit(typeDeclaration->ToBase());

	bool isOneLine = true;
	const char* src = typeDeclaration->GetSourceData()->mSrc;
	for (int i = typeDeclaration->GetSrcStart(); i < typeDeclaration->GetSrcEnd(); i++)
	{
		if (src[i] == '\n')
		{
			isOneLine = false;
			break;
		}
	}
	
	ExpectNewLine();	
	QueueVisitChild(typeDeclaration->mAttributes);
	if (!isOneLine)
		ExpectNewLine();	
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mAbstractSpecifier);
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mSealedSpecifier);
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mInternalSpecifier);
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mStaticSpecifier);
	ExpectSpace();
	QueueVisitChild(typeDeclaration->mPartialSpecifier);
	ExpectSpace();

	bool isEnumDoc = false;
	if ((mDocPrep) && (typeDeclaration->mTypeNode != NULL) && (typeDeclaration->mTypeNode->mToken == BfToken_Enum))
	{
		if (auto defineBlock = BfNodeDynCast<BfBlock>(typeDeclaration->mDefineNode))
		{			
			if (auto enumEntryDecl = BfNodeDynCast<BfEnumEntryDeclaration>(defineBlock->GetFirst()))
			{
				
			}
			else
			{
				isEnumDoc = true;
			}
		}		
	}

	if (isEnumDoc)
	{
		FlushVisitChild();
		Write(" struct");
	}
	else
		QueueVisitChild(typeDeclaration->mTypeNode);

	bool queueChildren = (typeDeclaration->mTypeNode != NULL) &&
		((typeDeclaration->mTypeNode->mToken == BfToken_Delegate) || (typeDeclaration->mTypeNode->mToken == BfToken_Function));

	ExpectSpace();
	QueueVisitChild(typeDeclaration->mNameNode);
	QueueVisitChild(typeDeclaration->mGenericParams);
	if (typeDeclaration->mColonToken != NULL)
	{
		ExpectSpace();
		QueueVisitChild(typeDeclaration->mColonToken);
		for (int i = 0; i < (int)typeDeclaration->mBaseClasses.size(); i++)
		{
			ExpectSpace();
			QueueVisitChild(typeDeclaration->mBaseClasses[i]);
			if (i > 0)
			{
				QueueVisitChild(typeDeclaration->mBaseClassCommas[i - 1]);
			}
		}
		ExpectSpace();		
	}
	QueueVisitChild(typeDeclaration->mGenericConstraintsDeclaration);

	if (queueChildren)
	{
		if (auto defineBlock = BfNodeDynCast<BfBlock>(typeDeclaration->mDefineNode))
		{						
			for (auto member : defineBlock->mChildArr)
			{
				SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, member);
				if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(member))
				{
					QueueMethodDeclaration(methodDecl);
				}
				else
					BF_FATAL("Error");
			}
			FlushVisitChild();
		}
		else
		{
			FlushVisitChild();
			VisitChild(typeDeclaration->mDefineNode);
		}
	}
	else
	{		
		FlushVisitChild();
		if (auto defineBlock = BfNodeDynCast<BfBlock>(typeDeclaration->mDefineNode))
		{
			if (!isOneLine)
			{
				ExpectNewLine();
				mNextStateModify.mDoingBlockOpen = true;
			}
			else
				ExpectSpace();
			VisitChild(defineBlock->mOpenBrace);
			ExpectIndent();
			for (auto member : defineBlock->mChildArr)
			{
				SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, member);
				VisitChild(member);
			}
			ExpectUnindent();
			VisitChild(defineBlock->mCloseBrace);			
		}		
		else
		{
			FlushVisitChild();
			VisitChild(typeDeclaration->mDefineNode);
		}
	}		
}

void BfPrinter::Visit(BfUsingDirective* usingDirective)
{
	//Visit(usingDirective->ToBase());
	
	ExpectNewLine();
	VisitChild(usingDirective->mUsingToken);	
	ExpectSpace();
	VisitChild(usingDirective->mNamespace);	

	VisitChild(usingDirective->mTrailingSemicolon);	

	ExpectNewLine();
}

void BfPrinter::Visit(BfUsingStaticDirective * usingDirective)
{
	ExpectNewLine();
	VisitChild(usingDirective->mUsingToken);
	ExpectSpace();
	VisitChild(usingDirective->mStaticToken);
	ExpectSpace();
	VisitChild(usingDirective->mTypeRef);

	VisitChild(usingDirective->mTrailingSemicolon);

	ExpectNewLine();
}

void BfPrinter::Visit(BfNamespaceDeclaration* namespaceDeclaration)
{
	//Visit(namespaceDeclaration->ToBase());

	ExpectNewLine();
	VisitChild(namespaceDeclaration->mNamespaceNode);
	ExpectSpace();
	VisitChild(namespaceDeclaration->mNameNode);	
	VisitChild(namespaceDeclaration->mBlock);	
}

void BfPrinter::DoBlockOpen(BfBlock* block, bool queue, bool* outDoInlineBlock)
{
	bool doInlineBlock = true;
	if (block->mCloseBrace != NULL)
	{
		auto blockSrc = block->GetSourceData();
		int srcEnd = block->mCloseBrace->GetSrcEnd();
		for (int i = block->mOpenBrace->GetSrcStart(); i < srcEnd; i++)
		{
			if (blockSrc->mSrc[i] == '\n')
				doInlineBlock = false;
		}
	}

	if (!doInlineBlock)
	{
		ExpectNewLine();
		mNextStateModify.mDoingBlockOpen = true;
	}
	else
		ExpectSpace();
	if (queue)
		QueueVisitChild(block->mOpenBrace);
	else
		VisitChild(block->mOpenBrace);
	if (!doInlineBlock)
		ExpectIndent();
	else
		ExpectSpace();
	*outDoInlineBlock = doInlineBlock;
}

void BfPrinter::DoBlockClose(BfBlock* block, bool queue, bool doInlineBlock)
{
	if (!doInlineBlock)
	{
		ExpectUnindent();
		mNextStateModify.mDoingBlockClose = true;
	}
	else
		ExpectSpace();
	if (queue)
		QueueVisitChild(block->mCloseBrace);
	else
		VisitChild(block->mCloseBrace);
}

void BfPrinter::Visit(BfBlock* block)
{	
	bool doInlineBlock;
	DoBlockOpen(block, false, &doInlineBlock);
	for (auto& childNodeRef : *block)
	{
		BfAstNode* child = childNodeRef;
		SetAndRestoreValue<bool> prevForceTrivia(mForceUseTrivia);
		bool isSolitary = child->IsA<BfIdentifierNode>();
		if (isSolitary)
			mForceUseTrivia = true;

		SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, child);
		child->Accept(this);
	}
	DoBlockClose(block, false, doInlineBlock);

	ExpectNewLine();
}

void BfPrinter::Visit(BfRootNode* rootNode)
{			
	for (auto child : rootNode->mChildArr)
	{
		SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, child);
		child->Accept(this);
	}

	BfParserData* bfParser = rootNode->GetSourceData()->ToParserData();
	if (bfParser != NULL)
		Write(rootNode, rootNode->GetSrcEnd(), bfParser->mSrcLength - rootNode->GetSrcEnd());

	if (mCharMapping != NULL)
	{
		BF_ASSERT(mCharMapping->size() == mOutString.length());
	}
}

void BfPrinter::Visit(BfInlineAsmStatement* asmStmt)
{
	
}
