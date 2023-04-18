#include "BfPrinter.h"
#include "BfParser.h"
#include "BfUtil.h"
#include "BeefySysLib/util/UTF8.h"

USING_NS_BF;

// This is a really long line, I'm testing to see what's up. This is a really long line, I'm testing to see what's up. This is a really long line, I'm testing to see what's up.
// This is a really long line, I'm testing to see what's up.
BfPrinter::BfPrinter(BfRootNode *rootNode, BfRootNode *sidechannelRootNode, BfRootNode *errorRootNode)
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
	mCurBlockState = NULL;
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
	mCurCol = 0;
	mMaxCol = 120;
	mTabSize = 4;
	mWantsTabsAsSpaces = false;
	mIndentCaseLabels = false;
	mFormatDisableCount = 0;
}

void BfPrinter::Write(const StringView& str)
{
	if (str == '\n')
		mCurCol = 0;
	else if (mMaxCol > 0)
	{
		int startCol = mCurCol;

		for (int i = 0; i < (int)str.mLength; i++)
		{
			char c = str[i];
			if (c == '\t')
				mCurCol = ((mCurCol / mTabSize) + 1) * mTabSize;
			else
				mCurCol++;
		}
	}

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
	while ((mQueuedSpaceCount >= mTabSize) && (!mWantsTabsAsSpaces))
	{
		Write("\t");
		mQueuedSpaceCount -= mTabSize;
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

	if (mMaxCol > 0)
	{
		int startCol = mCurCol;

		auto parserData = node->GetParserData();

		for (int i = 0; i < len; i++)
		{
			char c = parserData->mSrc[start + i];
			if (c == '\t')
				mCurCol = ((mCurCol / mTabSize) + 1) * mTabSize;
			else
				mCurCol++;
		}
	}

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
			origLineSpacing += mTabSize;
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
	Update(node);

	if (!mReformatting)
	{
		int startIdx = BF_MAX(node->mTriviaStart, mTriviaIdx);
		Write(node, startIdx, node->mSrcEnd - startIdx);
		mTriviaIdx = node->mSrcEnd;
		return;
	}

	bool startsWithSpace = false;

	bool wasExpectingNewLine = mExpectingNewLine;

	mTriviaIdx = std::max(mTriviaIdx, node->GetTriviaStart());
	int endIdx = mTriviaIdx;
	int crCount = 0;
	auto astNodeSrc = node->GetSourceData();
	int srcEnd = node->GetSrcEnd();
	for (int i = mTriviaIdx; i < srcEnd; i++)
	{
		char c = astNodeSrc->mSrc[i];
		if ((i == mTriviaIdx) && (isspace((uint8)c)))
			startsWithSpace = true;

		if ((c == '\n') && (i < node->GetSrcStart()))
			crCount++;
		if (((c != ' ') && (c != '\t')) || (!mReformatting))
			endIdx = i + 1;
	}

	bool wantsPrefixSpace = (!mOutString.IsEmpty()) && (!isspace((uint8)mOutString[mOutString.mLength - 1]));

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
			{
				Write("\n");
				wantsPrefixSpace = false;
			}
			startIdx = node->GetSrcStart();
			// Leave left-aligned preprocessor nodes
			if ((node->GetSourceData()->mSrc[node->GetSrcStart()] != '#') || (origLineSpacing > 0))
				mQueuedSpaceCount = origLineSpacing + mLastSpaceOffset;
		}
	}

	auto commentNode = BfNodeDynCast<BfCommentNode>(node);
	bool isBlockComment = false;
	bool isStarredBlockComment = false;

	if (commentNode != NULL)
	{
		if ((commentNode->mCommentKind == BfCommentKind_Block) ||
			(commentNode->mCommentKind == BfCommentKind_Documentation_Block_Pre) ||
			(commentNode->mCommentKind == BfCommentKind_Documentation_Block_Post))
		{
			isBlockComment = true;

			int lineCount = 0;
			bool onNewLine = false;
			for (int srcIdx = startIdx; srcIdx < endIdx; srcIdx++)
			{
				char checkC = astNodeSrc->mSrc[srcIdx];
				if (checkC == '\n')
				{
					onNewLine = true;
					lineCount++;
					if (lineCount >= 2)
						break;
				}
				else if ((checkC == '*') && (onNewLine))
					isStarredBlockComment = true;
				else if ((checkC != ' ') && (checkC != '\t'))
					onNewLine = false;
			}
		}
	}
	bool doWrap = (commentNode != NULL) && (mMaxCol > 0);
	bool prevHadWrap = false;

	if (commentNode != NULL)
	{
		Visit((BfAstNode*)node);
		startIdx = node->mSrcStart;

		if (doWrap)
		{
			bool wantWrap = false;

			int spacedWordCount = 0;
			bool inQuotes = false;
			auto src = astNodeSrc->mSrc;
			bool isDefinitelyCode = false;
			bool hadNonSlash = false;

			for (int i = node->mSrcStart + 1; i < node->mSrcEnd - 1; i++)
			{
				char c = src[i];
				if (c != '/')
					hadNonSlash = true;
				if (inQuotes)
				{
					if (c == '\\')
					{
						i++;
					}
					else if (c == '\"')
					{
						inQuotes = false;
					}
				}
				else if (c == '"')
				{
					inQuotes = true;
				}
				else if (c == ' ')
				{
					if ((isalpha((uint8)src[i - 1])) && (isalpha((uint8)src[i + 1])))
						spacedWordCount++;
				}
				else if ((c == '/') && (src[i - 1] == '/') && (hadNonSlash))
					isDefinitelyCode = true;
			}

			// If this doesn't look like a sentence then don't try to word wrap
			if ((isDefinitelyCode) || (spacedWordCount < 4))
				doWrap = false;
		}
	}

	int lineEmittedChars = 0;

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

 		if ((wantsPrefixSpace) && (isspace((uint8)c)))
 			wantsPrefixSpace = false;

		if (c == '\n')
		{
			isNewLine = true;

			if (prevHadWrap)
			{
				bool merging = false;

				int blockContentStart = -1;

				bool foundStar = false;
				int startIdx = srcIdx;
				for (int checkIdx = srcIdx + 1; checkIdx < endIdx + 1; checkIdx++)
				{
					char checkC = astNodeSrc->mSrc[checkIdx];

					if (isBlockComment)
					{
						if (checkC == '\n')
							break;

						if ((isStarredBlockComment) && (!foundStar) && (checkC == '*'))
						{
							foundStar = true;
							continue;
						}

						if ((checkC != ' ') && (checkC != '\t'))
						{
							if (blockContentStart == -1)
							{
								blockContentStart = checkIdx;
							}

							// Only do merge if we have content on this line
							if (isalnum(checkC))
							{
								srcIdx = blockContentStart;
								merging = true;
								break;
							}
						}
					}
					else
					{
						if ((checkC == '/') && (astNodeSrc->mSrc[checkIdx + 1] == '/'))
						{
							srcIdx = checkIdx;
							while (srcIdx < endIdx)
							{
								char checkC = astNodeSrc->mSrc[srcIdx];
								if (checkC != '/')
									break;
								srcIdx++;
							}
							merging = true;
							break;
						}

						if ((checkC != ' ') && (checkC != '\t'))
							break;
					}
				}

				if (merging)
				{
					srcIdx--;
					while (srcIdx < endIdx - 1)
					{
						char checkC = astNodeSrc->mSrc[srcIdx + 1];
						if ((checkC != ' ') && (checkC != '\t'))
							break;
						srcIdx++;
					}
					Write(" ");
					continue;
				}
			}

			mCurCol = 0;
			prevHadWrap = false;
		}
		else if (isNewLine)
		{
			if (c == ' ')
			{
				mQueuedSpaceCount++;
				emitChar = false;
			}
			else if (c == '\t')
			{
				mQueuedSpaceCount += mTabSize;
				emitChar = false;
			}
			else
			{
				// Leave left-aligned preprocessor nodes that are commented out
				if ((c != '#') || (mQueuedSpaceCount > 0))
					mQueuedSpaceCount = std::max(0, mQueuedSpaceCount + mLastSpaceOffset);
				else
					mQueuedSpaceCount = mCurIndentLevel * mTabSize; // Do default indent
				isNewLine = false;
			}
		}

		if (emitChar)
		{
			int startIdx = srcIdx;

			if (c != '\n')
			{
				while (srcIdx < endIdx)
				{
					char c = astNodeSrc->mSrc[srcIdx + 1];
					if (isspace((uint8)c))
						break;
					srcIdx++;
				}
			}

			if (doWrap)
			{
				int len = 0;
				for (int idx = startIdx; idx <= srcIdx; idx++)
				{
					char c = astNodeSrc->mSrc[idx];
					if (isutf(c))
						len++;
				}

				if ((mCurCol + len > mMaxCol) && (lineEmittedChars >= 8))
				{
					Write("\n");
					mQueuedSpaceCount = mCurIndentLevel * mTabSize;
					FlushIndent();

					if (isStarredBlockComment)
						Write(" * ");
					else if (!isBlockComment)
						Write("// ");
					prevHadWrap = true;

					while (startIdx < endIdx)
					{
						char c = astNodeSrc->mSrc[startIdx];
						if (!isspace((uint8)c))
							break;
						startIdx++;
					}
					lineEmittedChars = 0;
				}
			}

			if (wantsPrefixSpace)
			{
				mQueuedSpaceCount++;
				wantsPrefixSpace = false;
			}
			FlushIndent();

			for (int idx = startIdx; idx <= BF_MIN(srcIdx, endIdx - 1); idx++)
			{
				char c = astNodeSrc->mSrc[idx];
				mOutString.Append(c);
				if (mCharMapping != NULL)
				{
					if (idx < mParser->mSrcLength)
						mCharMapping->push_back(idx);
					else
						mCharMapping->push_back(-1);
				}

				if (c == '\n')
				{
					mCurCol = 0;
					lineEmittedChars = 0;
				}
				else if (isutf(c))
				{
					mCurCol++;
					lineEmittedChars++;
				}
			}
		}
	}

	FlushIndent();

	mTriviaIdx = endIdx;
	mIsFirstStatementInBlock = false;
	mExpectingNewLine = wasExpectingNewLine;
}

void BfPrinter::CheckRawNode(BfAstNode* node)
{
	if (node == NULL)
		return;
	if ((!BfNodeIsExact<BfTokenNode>(node)) &&
		(!BfNodeIsExact<BfIdentifierNode>(node)) &&
		(!BfNodeIsExact<BfLiteralExpression>(node)))
		return;

	//mForceUseTrivia = true;

	// Usually 'raw' nodes get merged into larger nodes like expressions/statements, but if they don't then this tries to help formatting
	mExpectingNewLine = false;
	mVirtualNewLineIdx = mNextStateModify.mWantNewLineIdx;
	mNextStateModify.mExpectingSpace = false;

	bool inLineStart = false;
	int spaceCount = 0;

	auto parserData = node->GetParserData();
	for (int i = node->mTriviaStart; i < node->mSrcStart; i++)
	{
		char c = parserData->mSrc[i];
		if (c == '\n')
		{
			ExpectNewLine();
			inLineStart = true;
			spaceCount = 0;
		}
		else if (c == '\t')
		{
			ExpectSpace();
			if (inLineStart)
				spaceCount += mTabSize;
		}
		else if (c == ' ')
		{
			ExpectSpace();
			if (inLineStart)
				spaceCount++;
		}
		else
		{
			inLineStart = false;
		}
	}

	if ((spaceCount > 0) && (mCurBlockState != NULL))
	{
		int indentCount = spaceCount / mTabSize;
		mNextStateModify.mWantVirtualIndent = BF_MAX(indentCount, mCurBlockState->mIndentStart + 1);
	}
}

void BfPrinter::Update(BfAstNode* bfAstNode)
{
	// This won't be true if we move nodes around during refactoring
	if (bfAstNode->GetSrcStart() >= mCurSrcIdx)
	{
		mCurSrcIdx = bfAstNode->GetSrcStart();
	}

	if ((!mReformatting) && (mFormatDisableCount == 0) && (mFormatStart != -1) && (mCurSrcIdx >= mFormatStart) && ((mCurSrcIdx < mFormatEnd) || (mFormatEnd == -1)))
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
				prevSpaceCount += mTabSize;
			else if (c == '\n')
			{
				// Found previous line
				mCurIndentLevel = prevSpaceCount / mTabSize;
				break;
			}
			else
			{
				prevSpaceCount = 0;
			}
			backIdx--;
		}
	}

	if (mFormatDisableCount != 0)
		mReformatting = false;

	if ((mCurSrcIdx >= mFormatEnd) && (mFormatEnd != -1))
		mReformatting = false;

	bool expectingNewLine = mNextStateModify.mWantNewLineIdx != mVirtualNewLineIdx;
	if (expectingNewLine)
		mExpectingNewLine = true;
}

void BfPrinter::Visit(BfAstNode* bfAstNode)
{
	SetAndRestoreValue<bool> prevForceTrivia(mForceUseTrivia);

	bool newExpectingNewLine = mNextStateModify.mWantNewLineIdx != mVirtualNewLineIdx;
	if (newExpectingNewLine)
		mExpectingNewLine = true;
	bool expectingNewLine = mExpectingNewLine;

	int indentOffset = 0;
	if (bfAstNode->GetTriviaStart() != -1)
	{
		if (newExpectingNewLine)
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

	Update(bfAstNode);

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

				bool canUseTrivia = true;
				int spaceTriviaStart = -1;
				int triviaEnd = bfAstNode->GetSrcStart();
				for (int i = mTriviaIdx; i < triviaEnd; i++)
				{
					if (mIgnoreTrivia)
						break;

					char c = astNodeSrc->mSrc[i];

					if (c == ' ')
					{
						if (spaceTriviaStart == -1)
							spaceTriviaStart = i;
						spaceCount++;
					}
					else if (c == '\t')
					{
						if (spaceTriviaStart == -1)
							spaceTriviaStart = i;
						spaceCount += mTabSize;
					}

					if (((c == '\n') || (i == bfAstNode->GetSrcStart() - 1)) && (hadPrevLineSpacing) && (prevSpaceCount > 0))
					{
						mQueuedSpaceCount += std::max(0, spaceCount - prevSpaceCount) - std::max(0, indentOffset * mTabSize);

						prevSpaceCount = -1;
						hadPrevLineSpacing = false;
					}

					if (c == '\n')
					{
						canUseTrivia = false;
						hadNewline = true;
						int backIdx = i - 1;
						prevSpaceCount = 0;
						while (backIdx >= 0)
						{
							char c = astNodeSrc->mSrc[backIdx];
							if (c == ' ')
								prevSpaceCount++;
							else if (c == '\t')
								prevSpaceCount += mTabSize;

							if ((c == '\n') || (backIdx == 0))
							{
								// Found previous line
								usedTrivia = true;
								Write("\n");
								mQueuedSpaceCount = mCurIndentLevel * mTabSize;

								// Indents extra if we have a statement split over multiple lines
								if (!expectingNewLine)
									mQueuedSpaceCount += mTabSize;

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
						spaceTriviaStart = -1;
					}
					else if (!isspace((uint8)c))
					{
						spaceCount = 0;
						spaceTriviaStart = -1;
					}
				}

				if ((canUseTrivia) && (spaceCount > 1) && (spaceTriviaStart != -1))
				{
					Write(bfAstNode, spaceTriviaStart, triviaEnd - spaceTriviaStart);
					mNextStateModify.mExpectingSpace = false;
					usedTrivia = true;
				}
			}

			if (usedTrivia)
			{
				// Already did whitespace
				mNextStateModify.mExpectingSpace = false;
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
						mQueuedSpaceCount = mCurIndentLevel * mTabSize;
						if (!mNextStateModify.mDoingBlockClose)
						{
							int origLineSpacing = CalcOrigLineSpacing(bfAstNode, NULL);
							if (origLineSpacing != -1)
								mLastSpaceOffset = mQueuedSpaceCount - origLineSpacing;
						}
						break;
					}
					idx--;
				}
			}
			else if ((mNextStateModify.mExpectingSpace) || (newExpectingNewLine))
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

void BfPrinter::Visit(BfNamedExpression* namedExpr)
{
	Visit(namedExpr->ToBase());

	VisitChild(namedExpr->mNameNode);
	VisitChild(namedExpr->mColonToken);
	ExpectSpace();
	VisitChild(namedExpr->mExpression);
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
				VisitChildNoRef(indexerExpr->mCommas.GetSafe(i - 1));
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
	if (commentNode->mCommentKind == BfCommentKind_Line)
		ExpectNewLine();
}

void BfPrinter::Visit(BfPreprocesorIgnoredSectionNode* preprocesorIgnoredSection)
{
	WriteIgnoredNode(preprocesorIgnoredSection);
	ExpectNewLine();
}

void BfPrinter::Visit(BfPreprocessorNode* preprocessorNode)
{
	WriteIgnoredNode(preprocessorNode);

	if ((preprocessorNode->mCommand->ToStringView() == "pragma") &&
		(preprocessorNode->mArgument != NULL) &&
		(preprocessorNode->mArgument->mChildArr.mSize == 2) &&
		(preprocessorNode->mArgument->mChildArr[0] != NULL) &&
		(preprocessorNode->mArgument->mChildArr[0]->ToStringView() == "format") &&
		(preprocessorNode->mArgument->mChildArr[1] != NULL))
	{
		if (preprocessorNode->mArgument->mChildArr[1]->ToStringView() == "disable")
			mFormatDisableCount++;
		else if (preprocessorNode->mArgument->mChildArr[1]->ToStringView() == "restore")
			mFormatDisableCount = BF_MAX(0, mFormatDisableCount - 1);
	}

	ExpectNewLine();
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
			VisitChildNoRef(attributeDirective->mCommas.GetSafe(i - 1));
			ExpectSpace();
		}
		VisitChild(attributeDirective->mArguments[i]);
	}
	VisitChild(attributeDirective->mCtorCloseParen);
	VisitChild(attributeDirective->mAttrCloseToken);
	if ((attributeDirective->mNextAttribute != NULL) && (attributeDirective->mNextAttribute->mAttrOpenToken != NULL) &&
		(attributeDirective->mNextAttribute->mAttrOpenToken->mToken == BfToken_LBracket))
		ExpectNewLine();
	VisitChild(attributeDirective->mNextAttribute);
}

void BfPrinter::Visit(BfGenericParamsDeclaration* genericParams)
{
	Visit(genericParams->ToBase());

	VisitChild(genericParams->mOpenChevron);
	for (int i = 0; i < (int)genericParams->mGenericParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(genericParams->mCommas.GetSafe(i - 1));
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
					VisitChildNoRef(genericConstraint->mCommas.GetSafe(i - 1));
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
			VisitChildNoRef(genericArgumentsNode->mCommas.GetSafe(i - 1));
			ExpectSpace();
		}
		VisitChild(genericArgumentsNode->mGenericArgs[i]);
	}
	for (int i = (int)genericArgumentsNode->mGenericArgs.size() - 1; i < (int)genericArgumentsNode->mCommas.size(); i++)
		VisitChildNoRef(genericArgumentsNode->mCommas.GetSafe(i));
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

void BfPrinter::Visit(BfTokenPairNode* tokenPairNode)
{
	Visit(tokenPairNode->ToBase());

	VisitChild(tokenPairNode->mLeft);
	if ((tokenPairNode->mRight != NULL) && (tokenPairNode->mRight->mToken != BfToken_Star))
		ExpectSpace();
	VisitChild(tokenPairNode->mRight);
}

void BfPrinter::Visit(BfUsingSpecifierNode* usingSpecifier)
{
	Visit(usingSpecifier->ToBase());

	VisitChild(usingSpecifier->mProtection);
	ExpectSpace();
	VisitChild(usingSpecifier->mUsingToken);
}

void BfPrinter::Visit(BfLiteralExpression* literalExpr)
{
	Visit(literalExpr->ToBase());

	if (literalExpr->mValue.mTypeCode == BfTypeCode_CharPtr)
	{
		bool isMultiLine = false;

		auto sourceData = literalExpr->GetSourceData();
		for (int i = literalExpr->GetSrcStart(); i < (int)literalExpr->GetSrcEnd(); i++)
		{
			char c = sourceData->mSrc[i];
			if (c == '\n')
			{
				isMultiLine = true;
				break;
			}
		}

		if (isMultiLine)
		{
			int srcLineStart = 0;

			int checkIdx = literalExpr->GetSrcStart() - 1;
			while (checkIdx >= 0)
			{
				char c = sourceData->mSrc[checkIdx];
				if (c == '\n')
				{
					srcLineStart = checkIdx + 1;
					break;
				}
				checkIdx--;
			}

			int queuedSpaceCount = mQueuedSpaceCount;
			FlushIndent();

			for (int i = literalExpr->GetSrcStart(); i < (int)literalExpr->GetSrcEnd(); i++)
			{
				char c = sourceData->mSrc[i];
				Write(c);
				if (c == '\n')
				{
					i++;
					int srcIdx = srcLineStart;
					while (true)
					{
						char srcC = sourceData->mSrc[srcIdx++];
						char litC = sourceData->mSrc[i];

						if (srcC != litC)
							break;
						if ((srcC != ' ') && (srcC != '\t'))
							break;
						i++;
					}

					mQueuedSpaceCount = queuedSpaceCount;
					FlushIndent();

					i--;
				}
			}

			return;
		}
	}

	WriteSourceString(literalExpr);
}

void BfPrinter::Visit(BfStringInterpolationExpression* stringInterpolationExpression)
{
	Visit(stringInterpolationExpression->ToBase());
	String str;
	stringInterpolationExpression->ToString(str);
	Write(str);
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

void BfPrinter::Visit(BfInitializerExpression* initExpr)
{
	Visit(initExpr->ToBase());

	VisitChild(initExpr->mTarget);
	BlockState blockState;
	DoBlockOpen(initExpr->mTarget, initExpr->mOpenBrace, initExpr->mCloseBrace, false, blockState);
	for (int i = 0; i < (int)initExpr->mValues.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(initExpr->mCommas.GetSafe(i - 1));
			if (blockState.mDoInlineBlock)
				ExpectSpace();
			else
				ExpectNewLine();
		}

		VisitChild(initExpr->mValues[i]);
	}
	DoBlockClose(initExpr->mTarget, initExpr->mOpenBrace, initExpr->mCloseBrace, false, blockState);

// 	Visit(initExpr->ToBase());
//
// 	VisitChild(initExpr->mTarget);
// 	ExpectSpace();
// 	VisitChild(initExpr->mOpenBrace);
// 	ExpectIndent();
// 	for (int i = 0; i < (int)initExpr->mValues.size(); i++)
// 	{
// 		if (i > 0)
// 		{
// 			VisitChildNoRef(initExpr->mCommas.GetSafe(i - 1));
// 			ExpectSpace();
// 		}
// 		VisitChild(initExpr->mValues[i]);
// 	}
// 	ExpectUnindent();
// 	VisitChild(initExpr->mCloseBrace);
}

void BfPrinter::Visit(BfCollectionInitializerExpression* initExpr)
{
	Visit(initExpr->ToBase());

	VisitChild(initExpr->mOpenBrace);
	for (int i = 0; i < (int)initExpr->mValues.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(initExpr->mCommas.GetSafe(i - 1));
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
				VisitChildNoRef(genericInstTypeRef->mCommas.GetSafe(i - 1));
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
			VisitChildNoRef(typeRef->mCommas.GetSafe(i - 1));
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
	VisitChild(typeRef->mAttributes);
	ExpectSpace();
	VisitChild(typeRef->mReturnType);
	VisitChild(typeRef->mOpenParen);

	for (int i = 0; i < (int)typeRef->mParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(typeRef->mCommas.GetSafe(i - 1));
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

	VisitChild(varDecl->mAttributes);
	VisitChildWithProceedingSpace(varDecl->mModSpecifier);

	if (varDecl->mPrecedingComma != NULL)
	{
		mNextStateModify.mWantNewLineIdx--;
		VisitChild(varDecl->mPrecedingComma);
	}
	else if (varDecl->mTypeRef != NULL)
	{
		if (varDecl->mTypeRef->mSrcStart >= varDecl->mSrcStart)
		{
			VisitChild(varDecl->mTypeRef);
		}
		else
		{
			// May be from a `for (int k = 1, m = 0; k <= 20; k++)`
		}
	}

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

void BfPrinter::Visit(BfOffsetOfExpression* offsetOfExpr)
{
	VisitChild(offsetOfExpr->mToken);
	VisitChild(offsetOfExpr->mOpenParen);
	VisitChild(offsetOfExpr->mTypeRef);
	VisitChild(offsetOfExpr->mCommaToken);
	ExpectSpace();
	VisitChild(offsetOfExpr->mMemberName);
	VisitChild(offsetOfExpr->mCloseParen);
}

void BfPrinter::Visit(BfDefaultExpression* defaultExpr)
{
	Visit(defaultExpr->ToBase());

	VisitChild(defaultExpr->mDefaultToken);
	VisitChild(defaultExpr->mOpenParen);
	VisitChild(defaultExpr->mTypeRef);
	VisitChild(defaultExpr->mCloseParen);
}

void BfPrinter::Visit(BfIsConstExpression* isConstExpr)
{
	Visit(isConstExpr->ToBase());

	VisitChild(isConstExpr->mIsConstToken);
	VisitChild(isConstExpr->mOpenParen);
	VisitChild(isConstExpr->mExpression);
	VisitChild(isConstExpr->mCloseParen);
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

	if (lambdaBindExpr->mNewToken != NULL)
	{
		VisitChild(lambdaBindExpr->mNewToken);
		ExpectSpace();
	}
	VisitChild(lambdaBindExpr->mOpenParen);
	for (int i = 0; i < (int)lambdaBindExpr->mParams.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(lambdaBindExpr->mCommas.GetSafe(i - 1));
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
	VisitChild(lambdaBindExpr->mDtor);
	mNextStateModify.mExpectingSpace = false;
	mVirtualNewLineIdx = mNextStateModify.mWantNewLineIdx;
	mCurIndentLevel = mNextStateModify.mWantVirtualIndent;
	mVirtualIndentLevel = mNextStateModify.mWantVirtualIndent;
}

void BfPrinter::Visit(BfObjectCreateExpression* newExpr)
{
	Visit(newExpr->ToBase());

	VisitChild(newExpr->mNewNode);
	ExpectSpace();

	VisitChild(newExpr->mTypeRef);

	if (newExpr->mStarToken != NULL)
	{
		VisitChild(newExpr->mStarToken);
		ExpectSpace();
	}

	auto _WriteToken = [&](BfAstNode* node, BfToken token)
	{
		if (node == NULL)
			return;
		Visit((BfAstNode*)node);
		Write(node, node->GetSrcStart(), 0);
		Write(BfTokenToString(token));
	};

	_WriteToken(newExpr->mOpenToken, BfToken_LParen);
	for (int i = 0; i < (int)newExpr->mArguments.size(); i++)
	{
		if (i > 0)
		{
			VisitChildNoRef(newExpr->mCommas.GetSafe(i - 1));
			ExpectSpace();
		}
		VisitChild(newExpr->mArguments[i]);
	}
	_WriteToken(newExpr->mCloseToken, BfToken_RParen);
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
	VisitChild(deleteStmt->mTargetTypeToken);
	VisitChild(deleteStmt->mAllocExpr);
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
			VisitChildNoRef(invocationExpr->mCommas.GetSafe(i - 1));
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
	VisitChild(deferStmt->mScopeName);

	if (deferStmt->mBind != NULL)
	{
		auto bind = deferStmt->mBind;

		VisitChild(bind->mOpenBracket);
		for (int i = 0; i < bind->mParams.size(); i++)
		{
			if (i > 0)
			{
				VisitChildNoRef(bind->mCommas.GetSafe(i - 1));
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
	if (mIndentCaseLabels)
		ExpectIndent();

	VisitChild(switchCase->mCaseToken);
	for (int caseIdx = 0; caseIdx < (int) switchCase->mCaseExpressions.size(); caseIdx++)
	{
		if ((caseIdx == 0) || (caseIdx > (int)switchCase->mCaseCommas.size()))
			ExpectSpace();
		else
			VisitChildNoRef(switchCase->mCaseCommas.GetSafe(caseIdx - 1));
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

	if (mIndentCaseLabels)
		ExpectUnindent();
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
			VisitChildNoRef(forStmt->mInitializerCommas.GetSafe(i - 1));
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
			VisitChildNoRef(forStmt->mIteratorCommas.GetSafe(i - 1));
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
		if (i > 0)
		{
			VisitChildNoRef(tupleExpr->mCommas.GetSafe(i - 1));
			ExpectSpace();
		}

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
			VisitChildNoRef(indexerExpr->mCommas.GetSafe(i - 1));
			ExpectSpace();
		}
		VisitChild(indexerExpr->mArguments[i]);
	}

	VisitChild(indexerExpr->mCloseBracket);
}

void BfPrinter::Visit(BfUnaryOperatorExpression* unaryOpExpr)
{
	Visit(unaryOpExpr->ToBase());

	bool postOp = (unaryOpExpr->mOp == BfUnaryOp_PostIncrement) || (unaryOpExpr->mOp == BfUnaryOp_PostDecrement) || (unaryOpExpr->mOp == BfUnaryOp_PartialRangeFrom);
	if (!postOp)
		VisitChild(unaryOpExpr->mOpToken);
	if ((unaryOpExpr->mOp == BfUnaryOp_Ref) || (unaryOpExpr->mOp == BfUnaryOp_Mut) || (unaryOpExpr->mOp == BfUnaryOp_Out) || (unaryOpExpr->mOp == BfUnaryOp_Params) || (unaryOpExpr->mOp == BfUnaryOp_Cascade))
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

	ExpectNewLine();
	if (ctorDeclaration->mAttributes != NULL)
	{
		QueueVisitChild(ctorDeclaration->mAttributes);
		ExpectNewLine();
	}

	QueueVisitChild(ctorDeclaration->mProtectionSpecifier);
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
			QueueVisitChild(ctorDeclaration->mCommas.GetSafe(i - 1));
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

void BfPrinter::Visit(BfAutoConstructorDeclaration* ctorDeclaration)
{
	if (ctorDeclaration->mPrefix != NULL)
	{
		VisitChild(ctorDeclaration->mPrefix);
		ExpectSpace();
	}
	Visit(ctorDeclaration->ToBase());
}

void BfPrinter::Visit(BfDestructorDeclaration* dtorDeclaration)
{
	//Visit((BfAstNode*)dtorDeclaration);

	QueueVisitChild(dtorDeclaration->mAttributes);
	ExpectNewLine();
	ExpectSpace();
	QueueVisitChild(dtorDeclaration->mProtectionSpecifier);
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
			QueueVisitChild(dtorDeclaration->mCommas.GetSafe(i - 1));
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

	if (methodDeclaration->mReadOnlySpecifier != NULL)
	{
		ExpectSpace();
		QueueVisitChild(methodDeclaration->mReadOnlySpecifier);
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
		ExpectSpace();
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
	if (methodDeclaration->mThisToken != NULL)
	{
		QueueVisitChild(methodDeclaration->mThisToken);
		ExpectSpace();
	}
	for (int i = 0; i < (int) methodDeclaration->mParams.size(); i++)
	{
		if (i > 0)
		{
			QueueVisitChild(methodDeclaration->mCommas.GetSafe(i - 1));
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
	QueueVisitChild(propertyMethodDeclaration->mSetRefSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyMethodDeclaration->mMutSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyMethodDeclaration->mFatArrowToken);
	ExpectSpace();
	if (auto block = BfNodeDynCast<BfBlock>(propertyMethodDeclaration->mBody))
		ExpectNewLine();
	QueueVisitChild(propertyMethodDeclaration->mBody);
	QueueVisitChild(propertyMethodDeclaration->mEndSemicolon);
}

void BfPrinter::Visit(BfPropertyBodyExpression* propertyBodyExpression)
{
	VisitChild(propertyBodyExpression->mMutSpecifier);
	ExpectSpace();
	VisitChild(propertyBodyExpression->mFatTokenArrow);
}

void BfPrinter::Visit(BfPropertyDeclaration* propertyDeclaration)
{
	auto indexerDeclaration = BfNodeDynCast<BfIndexerDeclaration>(propertyDeclaration);

	ExpectNewLine();
	QueueVisitChild(propertyDeclaration->mAttributes);
	ExpectNewLine();
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mProtectionSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mConstSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mReadOnlySpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mVolatileSpecifier);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mNewSpecifier);
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
				QueueVisitChild(indexerDeclaration->mCommas.GetSafe(i - 1));
				ExpectSpace();
			}
			QueueVisitChild(indexerDeclaration->mParams[i]);
		}
		QueueVisitChild(indexerDeclaration->mCloseBracket);
		ExpectSpace();
	}

	if (auto block = BfNodeDynCast<BfBlock>(propertyDeclaration->mDefinitionBlock))
	{
		BlockState blockState;
		DoBlockOpen(NULL, block->mOpenBrace, block->mCloseBrace, true, blockState);
		for (auto method : propertyDeclaration->mMethods)
		{
			Visit(method);
		}
		FlushVisitChild();
		DoBlockClose(NULL, block->mOpenBrace, block->mCloseBrace, true, blockState);
	}
	else
	{
		ExpectSpace();
		QueueVisitChild(propertyDeclaration->mDefinitionBlock);
		ExpectSpace();
		for (auto method : propertyDeclaration->mMethods)
		{
			QueueVisitChild(method->mBody);
		}
	}

	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mEqualsNode);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mInitializer);
	ExpectSpace();
	QueueVisitChild(propertyDeclaration->mFieldDtor);
	FlushVisitChild();

	//QueueVisitChild(propertyDeclaration->mTrailingSemicolon);

	// ??? QueueVisitErrorNodes(propertyDeclaration);
	FlushVisitChild();
}

void BfPrinter::Visit(BfIndexerDeclaration* indexerDeclaration)
{
	Visit((BfPropertyDeclaration*)indexerDeclaration);
}

void BfPrinter::Visit(BfFieldDeclaration* fieldDeclaration)
{
	bool isEnumDecl = false;

	if (auto enumEntry = BfNodeDynCast<BfEnumEntryDeclaration>(fieldDeclaration))
	{
		isEnumDecl = true;
	}

	if (fieldDeclaration->mPrecedingComma != NULL)
	{
		mVirtualNewLineIdx = mNextStateModify.mWantNewLineIdx;

		QueueVisitChild(fieldDeclaration->mPrecedingComma);
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mNameNode);
	}
	else
	{
		if (!isEnumDecl)
			ExpectNewLine();
		if (fieldDeclaration->mAttributes != NULL)
		{
			QueueVisitChild(fieldDeclaration->mAttributes);
			ExpectNewLine();
		}
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
		QueueVisitChild(fieldDeclaration->mExternSpecifier);
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mStaticSpecifier);
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mPrecedingComma);
		ExpectSpace();
		if (isEnumDecl)
			mNextStateModify.mExpectingSpace = false;
		QueueVisitChild(fieldDeclaration->mTypeRef);
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mNameNode);
	}

	if (fieldDeclaration->mEqualsNode != NULL)
	{
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mEqualsNode);
	}
	if (fieldDeclaration->mInitializer != NULL)
	{
		ExpectSpace();
		QueueVisitChild(fieldDeclaration->mInitializer);
	}

	mNextStateModify.mExpectingSpace = false;
	FlushVisitChild();
	VisitChild(fieldDeclaration->mFieldDtor);
	mNextStateModify.mExpectingSpace = false;
}

void BfPrinter::Visit(BfEnumCaseDeclaration* enumCaseDeclaration)
{
	ExpectNewLine();

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
			VisitChildNoRef(enumCaseDeclaration->mCommas.GetSafe(i - 1));
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

void BfPrinter::Visit(BfFieldDtorDeclaration* fieldDtorDeclaration)
{
	ExpectSpace();
	if (fieldDtorDeclaration->mBody != NULL)
	{
		if (fieldDtorDeclaration->mBody->IsA<BfBlock>())
		{
			ExpectNewLine();
			ExpectIndent();
			VisitChild(fieldDtorDeclaration->mTildeToken);
			VisitChild(fieldDtorDeclaration->mBody);
			ExpectUnindent();
		}
		else
		{
			VisitChild(fieldDtorDeclaration->mTildeToken);
			ExpectSpace();
			VisitChild(fieldDtorDeclaration->mBody);
		}
	}
	else
		VisitChild(fieldDtorDeclaration->mTildeToken);

	VisitChild(fieldDtorDeclaration->mNextFieldDtor);
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
	QueueVisitChild(typeDeclaration->mAutoCtor);
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
				QueueVisitChild(typeDeclaration->mBaseClassCommas.GetSafe(i - 1));
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
				if (auto fieldDecl = BfNodeDynCast<BfFieldDeclaration>(member))
					ExpectNewLine();
				else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(member))
				{
					mVirtualNewLineIdx = mNextStateModify.mWantNewLineIdx;
					mNextStateModify.mExpectingSpace = false;
				}
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

	ExpectNewLine();
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

void BfPrinter::Visit(BfUsingModDirective* usingDirective)
{
	ExpectNewLine();
	VisitChild(usingDirective->mUsingToken);
	ExpectSpace();
	VisitChild(usingDirective->mModToken);
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
	VisitChild(namespaceDeclaration->mBody);
}

void BfPrinter::DoBlockOpen(BfAstNode* prevNode, BfTokenNode* blockOpen, BfTokenNode* blockClose, bool queue, BlockState& blockState)
{
	blockState.mLastSpaceOffset = mLastSpaceOffset;
	blockState.mIndentStart = mNextStateModify.mWantVirtualIndent;

	bool doInlineBlock = true;
	if (blockClose != NULL)
	{
		auto blockSrc = blockOpen->GetSourceData();
		int srcEnd = blockClose->GetSrcEnd();

		int srcStart = blockOpen->GetSrcStart();
		if (prevNode != NULL)
			srcStart = prevNode->GetSrcEnd();

		for (int i = srcStart; i < srcEnd; i++)
		{
			if (blockSrc->mSrc[i] == '\n')
				doInlineBlock = false;
		}
	}

	if (!doInlineBlock)
	{
		ExpectNewLine();
		mNextStateModify.mDoingBlockOpen = true;
		if (prevNode != NULL)
			ExpectIndent();
	}
	else
		ExpectSpace();
	if (queue)
		QueueVisitChild(blockOpen);
	else
		VisitChild(blockOpen);
	if (!doInlineBlock)
		ExpectIndent();
	else
		ExpectSpace();
	blockState.mDoInlineBlock = doInlineBlock;
}

void BfPrinter::DoBlockClose(BfAstNode* prevNode, BfTokenNode* blockOpen, BfTokenNode* blockClose, bool queue, BlockState& blockState)
{
	if (!blockState.mDoInlineBlock)
	{
		ExpectUnindent();
		mNextStateModify.mDoingBlockClose = true;
	}
	else
		ExpectSpace();
	if (queue)
		QueueVisitChild(blockClose);
	else
		VisitChild(blockClose);

	if (!blockState.mDoInlineBlock)
	{
		mNextStateModify.mWantVirtualIndent = blockState.mIndentStart;
		mLastSpaceOffset = blockState.mLastSpaceOffset;
	}
}

void BfPrinter::Visit(BfBlock* block)
{
	BlockState blockState;
	SetAndRestoreValue<BlockState*> prevBlockState(mCurBlockState, &blockState);

	DoBlockOpen(NULL, block->mOpenBrace, block->mCloseBrace, false, blockState);
	for (auto& childNodeRef : *block)
	{
		BfAstNode* child = childNodeRef;
		SetAndRestoreValue<bool> prevForceTrivia(mForceUseTrivia);
		SetAndRestoreValue<int> prevVirtualIndent(mNextStateModify.mWantVirtualIndent);
		SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, child);
		CheckRawNode(child);
		child->Accept(this);
	}
	DoBlockClose(NULL, block->mOpenBrace, block->mCloseBrace, false, blockState);

	ExpectNewLine();
}

void BfPrinter::Visit(BfRootNode* rootNode)
{
	for (auto child : rootNode->mChildArr)
	{
		SetAndRestoreValue<BfAstNode*> prevBlockMember(mCurBlockMember, child);
		child->Accept(this);
	}

	// Flush whitespace at the end of the document
	BfParserData* bfParser = rootNode->GetSourceData()->ToParserData();
	if (bfParser != NULL)
	{
		BfAstNode* endNode = mSource->mAlloc.Alloc<BfAstNode>();
		endNode->Init(rootNode->GetSrcEnd(), bfParser->mSrcLength, bfParser->mSrcLength);
		Visit(endNode);
	}

	if (mCharMapping != NULL)
	{
		BF_ASSERT(mCharMapping->size() == mOutString.length());
	}
}

void BfPrinter::Visit(BfInlineAsmStatement* asmStmt)
{
}