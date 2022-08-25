#include "BfAst.h"
#include "BfReducer.h"
#include "BfParser.h"
#include "BfSystem.h"
#include "BfUtil.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/StackHelper.h"

#include "BeefySysLib/util/AllocDebug.h"

#include <functional>

USING_NS_BF;

#define MEMBER_SET(dest, member, src) \
	{ dest->member = src; \
	MoveNode(src, dest); }

#define MEMBER_SET_CHECKED(dest, member, src) \
	{ if (src == NULL) return dest; \
	dest->member = src; \
	MoveNode(src, dest); }

#define MEMBER_SET_CHECKED_BOOL(dest, member, src) \
	{ if (src == NULL) return false; \
	dest->member = src; \
	MoveNode(src, dest); }

BfReducer::BfReducer()
{
	mCurTypeDecl = NULL;
	mLastTypeDecl = NULL;
	mCurMethodDecl = NULL;
	mLastBlockNode = NULL;
	mSource = NULL;
	mClassDepth = 0;
	mAlloc = NULL;
	mStmtHasError = false;
	mPrevStmtHadError = false;
	mPassInstance = NULL;
	mCompatMode = false;
	mAllowTypeWildcard = false;
	mIsFieldInitializer = false;
	mInParenExpr = false;
	mSkipCurrentNodeAssert = false;
	mAssertCurrentNodeIdx = 0;
	mSystem = NULL;
	mResolvePassData = NULL;
	mMethodDepth = 0;
	mDocumentCheckIdx = 0;
	mTypeMemberNodeStart = NULL;
}

bool BfReducer::IsSemicolon(BfAstNode* node)
{
	auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
	return (tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Semicolon);
}

bool BfReducer::StringEquals(BfAstNode* node, BfAstNode* node2)
{
	int len = node->GetSrcLength();
	int len2 = node2->GetSrcLength();
	if (len != len2)
		return false;
	int start = node->GetSrcStart();
	int start2 = node2->GetSrcStart();
	const char* srcStr = node->GetSourceData()->mSrc;
	for (int i = 0; i < len; i++)
	{
		if (srcStr[start + i] != srcStr[start2 + i])
			return false;
	}
	return true;
}

int gAssertCurrentNodeIdx = 0;
void BfReducer::AssertCurrentNode(BfAstNode* node)
{
	if (mSkipCurrentNodeAssert)
		return;

	auto currentNode = mVisitorPos.GetCurrent();
	if (currentNode == NULL)
		return;
	if (!node->LocationEndEquals(currentNode))
	{
		const char* lastCPtr = &node->GetSourceData()->mSrc[node->GetSrcEnd() - 1];
		// We have an "exceptional" case where breaking a double chevron will look like a position error
		if ((lastCPtr[0] != '>') || (lastCPtr[1] != '>'))
		{
			BF_FATAL("Internal parsing error");
		}
	}
	gAssertCurrentNodeIdx++;
	mAssertCurrentNodeIdx++;
}

// For autocomplete we only do a reduce on nodes the cursor is in
bool BfReducer::IsNodeRelevant(BfAstNode* astNode)
{
	BfParser* bfParser = astNode->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return true;
	int cursorPos = bfParser->mCursorIdx;
	if ((cursorPos == -1) || (astNode->Contains(cursorPos, 1, 0)))
		return true;
	BF_ASSERT(bfParser->mParserData->mRefCount == -1);
	return false;
}

bool BfReducer::IsNodeRelevant(BfAstNode* startNode, BfAstNode* endNode)
{
	if (startNode == NULL)
		return IsNodeRelevant(endNode);

	BfParser* bfParser = startNode->GetSourceData()->ToParser();
	if (bfParser == NULL)
		return true;
	int cursorPos = bfParser->mCursorIdx;
	int lenAdd = 1;
	if ((cursorPos == -1) ||
		((cursorPos >= startNode->GetSrcStart()) && (cursorPos < endNode->GetSrcEnd() + lenAdd)))
		return true;
	BF_ASSERT(bfParser->mParserData->mRefCount == -1);
	return false;
}

void BfReducer::MoveNode(BfAstNode* srcNode, BfAstNode* newOwner)
{
#ifdef BF_AST_HAS_PARENT_MEMBER
	srcNode->mParent = newOwner;
#endif
	int srcStart = srcNode->GetSrcStart();
	int srcEnd = srcNode->GetSrcEnd();
	if (srcStart < newOwner->GetSrcStart())
		newOwner->SetSrcStart(srcStart);
	if (srcEnd > newOwner->GetSrcEnd())
		newOwner->SetSrcEnd(srcEnd);
}

// Replaces prevNode with new node and adds prevNode to newNode's childrenj
//  It can be considered that newNode encapsulated prevNode.
void BfReducer::ReplaceNode(BfAstNode* prevNode, BfAstNode* newNode)
{
#ifdef BF_AST_HAS_PARENT_MEMBER
	newNode->mParent = prevNode->mParent;
#endif

	if (!newNode->IsInitialized())
	{
#ifdef BF_AST_COMPACT
		if (prevNode->mIsCompact)
		{
			newNode->mIsCompact = prevNode->mIsCompact;
			newNode->mCompact_SrcStart = prevNode->mCompact_SrcStart;
			newNode->mCompact_SrcLen = prevNode->mCompact_SrcLen;
			newNode->mCompact_TriviaLen = prevNode->mCompact_TriviaLen;
		}
		else
		{
			int prevTriviaStart;
			int prevSrcStart;
			int prevSrcEnd;
			prevNode->GetSrcPositions(prevTriviaStart, prevSrcStart, prevSrcEnd);
			newNode->Init(prevTriviaStart, prevSrcStart, prevSrcEnd);
		}
#else
		newNode->mTriviaStart = prevNode->mTriviaStart;
		newNode->mSrcStart = prevNode->mSrcStart;
		newNode->mSrcEnd = prevNode->mSrcEnd;
#endif
	}
	else
	{
		int newTriviaStart;
		int newSrcStart;
		int newSrcEnd;
		newNode->GetSrcPositions(newTriviaStart, newSrcStart, newSrcEnd);

		int prevTriviaStart;
		int prevSrcStart;
		int prevSrcEnd;
		prevNode->GetSrcPositions(prevTriviaStart, prevSrcStart, prevSrcEnd);

		if (prevTriviaStart < newTriviaStart)
			newNode->SetTriviaStart(prevTriviaStart);
		if (prevSrcStart < newSrcStart)
			newNode->SetSrcStart(prevSrcStart);
		if (prevSrcEnd > newSrcEnd)
			newNode->SetSrcEnd(prevSrcEnd);
	}

#ifdef BF_AST_HAS_PARENT_MEMBER
	prevNode->mParent = newNode;
#endif
}

BfAstNode* BfReducer::Fail(const StringImpl& errorMsg, BfAstNode* refNode)
{
	mStmtHasError = true;
	if (mPassInstance->HasLastFailedAt(refNode)) // No duplicate failures
		return NULL;
	auto error = mPassInstance->Fail(errorMsg, refNode);
	if ((error != NULL) && (mSource != NULL))
		error->mProject = mSource->mProject;
	return NULL;
}

BfAstNode* BfReducer::FailAfter(const StringImpl& errorMsg, BfAstNode* prevNode)
{
	mStmtHasError = true;
	auto error = mPassInstance->FailAfter(errorMsg, prevNode);
	if ((error != NULL) && (mSource != NULL))
		error->mProject = mSource->mProject;
	return NULL;
}

void BfReducer::AddErrorNode(BfAstNode* astNode, bool removeNode)
{
	if (mSource != NULL)
		mSource->AddErrorNode(astNode);
	if (removeNode)
		astNode->RemoveSelf();
}

bool BfReducer::IsTypeReference(BfAstNode* checkNode, BfToken successToken, int endNode, int* retryNode, int* outEndNode, bool* couldBeExpr, bool* isGenericType, bool* isTuple)
{
	AssertCurrentNode(checkNode);

	if (couldBeExpr != NULL)
		*couldBeExpr = true;
	if (outEndNode != NULL)
		*outEndNode = -1;

	auto firstNode = checkNode;
	if (checkNode == NULL)
		return false;
	int checkIdx = mVisitorPos.mReadPos;
	if ((!checkNode->IsA<BfIdentifierNode>()) && (!checkNode->IsA<BfMemberReferenceExpression>()))
	{
		if (auto checkTokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
		{
			BfToken checkToken = checkTokenNode->GetToken();
			if ((checkToken == BfToken_Ref) || (checkToken == BfToken_Mut))
			{
				checkIdx++;
				if (mVisitorPos.Get(checkIdx) == NULL)
					return false;
			}
			else if ((checkToken == BfToken_Var) || (checkToken == BfToken_Let))
			{
				checkIdx++;
				checkNode = mVisitorPos.Get(checkIdx);
				checkTokenNode = BfNodeDynCast<BfTokenNode>(checkNode);

				if (outEndNode)
					*outEndNode = checkIdx;

				if (successToken == BfToken_None)
					return true;

				return (checkToken == successToken);
			}
			else if (checkToken == BfToken_Unsigned)
			{
				// Unsigned val start
			}
			else if (checkToken == BfToken_LParen)
			{
				// Tuple start
			}
			else if ((checkToken == BfToken_Comptype) || (checkToken == BfToken_Decltype) || (checkToken == BfToken_AllocType) || (checkToken == BfToken_RetType) || (checkToken == BfToken_Nullable))
			{
				// Decltype start
			}
			else if ((checkToken == BfToken_Delegate) || (checkToken == BfToken_Function))
			{
				int startNode = mVisitorPos.mReadPos;
				mVisitorPos.mReadPos++;

				int endNode = -1;

				bool failed = false;

				// Return type
				auto checkNode = mVisitorPos.GetCurrent();
				if (auto checkToken = BfNodeDynCast<BfTokenNode>(checkNode))
				{
					if (checkToken->mToken == BfToken_LBracket)
					{
						while (true)
						{
							mVisitorPos.mReadPos++;
							checkNode = mVisitorPos.GetCurrent();
							if (checkNode == NULL)
							{
								failed = true;
								break;
							}

							if (BfNodeIsA<BfBlock>(checkNode))
							{
								failed = true;
								break;
							}

							if (checkToken = BfNodeDynCast<BfTokenNode>(checkNode))
							{
								if (checkToken->mToken == BfToken_RBracket)
								{
									mVisitorPos.mReadPos++;
									checkNode = mVisitorPos.GetCurrent();
									break;
								}
								if ((checkToken->mToken != BfToken_Comma) &&
									(checkToken->mToken != BfToken_Dot) &&
									(checkToken->mToken != BfToken_LParen) &&
									(checkToken->mToken != BfToken_RParen))
								{
									failed = true;
									break;
								}
							}
						}
					}
				}

				if ((failed) || (checkNode == NULL) || (!IsTypeReference(checkNode, BfToken_LParen, -1, &endNode, couldBeExpr, isGenericType, isTuple)))
				{
					if (outEndNode != NULL)
						*outEndNode = endNode;
					mVisitorPos.mReadPos = startNode;
					return false;
				}

				// Take in params as a tuple
				mVisitorPos.mReadPos = endNode;
				auto currentNode = mVisitorPos.GetCurrent();
				bool hasParams = false;
				if (currentNode != NULL)
				{
					if (auto openToken = BfNodeDynCast<BfTokenNode>(currentNode))
					{
						if (openToken->GetToken() == BfToken_LParen)
						{
							int parenDepth = 1;

							// Do a smarter check?
							checkIdx = endNode + 1;
							while (true)
							{
								auto checkNode = mVisitorPos.Get(checkIdx);
								if (checkNode == NULL)
									break;

								if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
								{
									bool done = false;

									switch (tokenNode->GetToken())
									{
									case BfToken_LParen:
										parenDepth++;
										break;
									case BfToken_RParen:
										parenDepth--;
										if (parenDepth == 0)
										{
											endNode = checkIdx + 1;
											done = true;
										}
										break;
									case BfToken_Semicolon:
										// Failed
										done = true;
										break;
									default: break;
									}

									if (done)
										break;
								}

								checkIdx++;
							}

							hasParams = parenDepth == 0;
						}
					}
				}

				if (!hasParams)
				{
					if (outEndNode != NULL)
						*outEndNode = endNode;
					mVisitorPos.mReadPos = startNode;
					return false;
				}

				if (outEndNode != NULL)
					*outEndNode = endNode;
				mVisitorPos.mReadPos = startNode;
				return true;
			}
			else
				return false;
		}
		else
			return false;
	}
	int chevronDepth = 0;
	bool identifierExpected = true;
	int endBracket = -1;
	int bracketDepth = 0;
	int parenDepth = 0;
	bool hadTupleComma = false;
	bool hadIdentifier = false;
	bool foundSuccessToken = false;
	bool hadUnexpectedIdentifier = false;
	BfTokenNode* lastToken = NULL;

	SizedArray<BfToken, 8> tokenStack;

	while (true)
	{
		if ((endNode != -1) && (checkIdx >= endNode))
			break;

		auto checkNode = mVisitorPos.Get(checkIdx);
		if (checkNode == NULL)
			break;
		auto checkTokenNode = BfNodeDynCast<BfTokenNode>(checkNode);
		if (checkTokenNode != NULL)
		{
			if (endBracket != -1)
			{
				if (outEndNode)
					*outEndNode = endBracket;
				return false;
			}

			BfToken checkToken = checkTokenNode->GetToken();
			if (bracketDepth > 0)
			{
				if ((checkToken == BfToken_LBracket) || (checkToken == BfToken_QuestionLBracket))
				{
					bracketDepth++;
				}
				else if (checkToken == BfToken_RBracket)
				{
					bracketDepth--;
				}
			}
			else
			{
				bool doEnding = (checkToken == successToken) && (checkTokenNode != firstNode) && (tokenStack.size() <= 1);
				if ((doEnding) && (tokenStack.size() == 1))
					doEnding = checkToken == tokenStack.back();
				if (doEnding)
				{
					bool success = false;

					if ((lastToken != NULL) && ((lastToken->GetToken() == BfToken_RChevron) || (lastToken->GetToken() == BfToken_RDblChevron)))
					{
						if (couldBeExpr != NULL)
							*couldBeExpr = false;
					}

					if (successToken == BfToken_RParen)
					{
						success = (chevronDepth == 0) && (bracketDepth == 0) && (parenDepth == 1);

						if (success)
						{
							// Check identifierExpected - this catches (.) casts
							if ((identifierExpected) && (couldBeExpr != NULL))
								*couldBeExpr = false;
						}
					}
					else if ((successToken == BfToken_Comma) ||
						(successToken == BfToken_LBracket))
					{
						success = (chevronDepth == 0) && (bracketDepth == 0) && (parenDepth == 0);
					}

					if ((success) || (doEnding))
					{
						if ((!hadTupleComma) && (hadUnexpectedIdentifier)) // Looked like a tuple but wasn't
							return false;
					}

					if (success)
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return true;
					}

					if (doEnding)
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return (chevronDepth == 0) && (bracketDepth == 0) && (parenDepth == 0);
					}
				}

				bool isDone = false;

				if (checkToken == BfToken_LParen)
				{
					if (chevronDepth > 0)
					{
						SetAndRestoreValue<int> prevIdx(mVisitorPos.mReadPos, checkIdx);

						int endToken = 0;
						if (!IsTypeReference(checkNode, BfToken_RParen, -1, &endToken, NULL, NULL, NULL))
							return false;
						checkIdx = endToken + 1;
						continue;
					}
					else if ((hadIdentifier) && (chevronDepth == 0) && (bracketDepth == 0) && (parenDepth == 0))
						isDone = true;
					else
					{
						tokenStack.Add(BfToken_RParen);
						parenDepth++;
					}
				}
				else if (checkToken == BfToken_RParen)
				{
					if ((parenDepth == 0) || (tokenStack.back() != BfToken_RParen))
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return false;
					}

					tokenStack.pop_back();
					parenDepth--;
					if (parenDepth > 0)
					{
						// if we are embedded in a multi-tuple like (A, (B, C), D) then we expect a , or ) after
						//  closing an inner tuple.  Otherwise this is an expression like ((Type)a)
						auto nextNode = mVisitorPos.Get(checkIdx + 1);
						bool isOkay = false;
						if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
						{
							isOkay = (nextToken->mToken == BfToken_Comma) || (nextToken->mToken == BfToken_RParen);
							if ((!tokenStack.IsEmpty()) && (nextToken->mToken == tokenStack.back()))
								isOkay = true;
						}
						if (!isOkay)
						{
							if (outEndNode != NULL)
								*outEndNode = checkIdx;
							return false;
						}
					}

					if ((parenDepth < 0) ||
						// Probably a cast
						((successToken == BfToken_None) && (parenDepth == 0) && (!hadTupleComma)))
					{
						if (successToken == BfToken_RParen)
						{
							foundSuccessToken = true;
							break;
						}
						else
						{
							if (outEndNode != NULL)
								*outEndNode = checkIdx;
							return false;
						}
					}
				}
				else if ((checkToken == BfToken_Const) && (chevronDepth > 0))
				{
					if (mCompatMode)
					{
						identifierExpected = true;
					}
					else
					{
						int prevReadPos = mVisitorPos.mReadPos;

						auto nextNode = mVisitorPos.Get(checkIdx + 1);
						if (nextNode != NULL)
						{
							mVisitorPos.mReadPos = checkIdx + 1;
							auto expr = CreateExpression(nextNode, CreateExprFlags_BreakOnRChevron);
							int endExprReadPos = mVisitorPos.mReadPos;
							mVisitorPos.mReadPos = prevReadPos;

							if (expr == NULL)
							{
								if (outEndNode != NULL)
									*outEndNode = checkIdx;
								return false;
							}

							checkIdx = endExprReadPos;
						}
					}
				}
				else if ((checkToken == BfToken_Minus) && (mCompatMode) && (chevronDepth > 0) && (parenDepth == 0) && (bracketDepth == 0))
				{
					// Allow
				}
				else if (checkToken == BfToken_Unsigned)
				{
					identifierExpected = true;
				}
				else if ((checkToken == BfToken_Ref) || (checkToken == BfToken_Mut))
				{
					identifierExpected = true;
				}
				else if (checkToken == BfToken_LChevron)
				{
					identifierExpected = true;
					chevronDepth++;
					tokenStack.Add(BfToken_RChevron);
					*retryNode = checkIdx;
				}
				else if ((checkToken == BfToken_RChevron) || (checkToken == BfToken_RDblChevron))
				{
					*retryNode = -1;

					if (tokenStack.IsEmpty())
						break;

					for (int i = 0; i < ((checkToken == BfToken_RDblChevron) ? 2 : 1); i++)
					{
						if (tokenStack.back() != BfToken_RChevron)
						{
							if (outEndNode != NULL)
								*outEndNode = checkIdx;
							return false;
						}
						tokenStack.pop_back();
						chevronDepth--;
					}

					identifierExpected = false;
					if (chevronDepth < 0)
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return false;
					}

					if (chevronDepth == 0)
					{
						if (isGenericType != NULL)
							*isGenericType = true;
					}
				}
				else if (checkToken == BfToken_RDblChevron)
					chevronDepth -= 2;
				else if (checkToken == BfToken_Comma)
				{
					if ((bracketDepth == 0) && (tokenStack.IsEmpty()))
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return false;
					}

					if ((!tokenStack.IsEmpty()) && (tokenStack.back() == BfToken_RParen))
					{
						hadTupleComma = true;
						if (isTuple != NULL)
						{
							*isTuple = true;
						}
					}

					identifierExpected = true;
				}
				else if (checkToken == BfToken_Dot)
				{
					auto prevNode = mVisitorPos.Get(checkIdx - 1);
					if (auto prevToken = BfNodeDynCast<BfTokenNode>(prevNode))
					{
						if (couldBeExpr != NULL)
						{
							// UH- NO, it could be referencing a static member
							// a ">." can only be a reference to an inner type of a generic type
							//if ((prevToken->GetToken() == BfToken_RChevron) || (prevToken->GetToken() == BfToken_RDblChevron))
							//*couldBeExpr = false;
						}

						// a ".[" can only be a member reference after an indexer expression
						if (prevToken->GetToken() == BfToken_RBracket)
						{
							if (outEndNode != NULL)
								*outEndNode = checkIdx;
							return false;
						}
					}

					identifierExpected = true;
				}
				else if (checkToken == BfToken_RBracket)
				{
					if (bracketDepth == 0)
					{
						// Not even an array
						return false;
					}

					endBracket = checkIdx;
				}
				else if ((checkToken == BfToken_Star) || (checkToken == BfToken_Question))
				{
					bool keepParsing = false;

					if (checkToken == BfToken_Star)
					{
						auto prevNode = mVisitorPos.Get(checkIdx - 1);
						if (auto prevToken = BfNodeDynCast<BfTokenNode>(prevNode))
						{
							switch (prevToken->GetToken())
							{
							case BfToken_RParen:
							case BfToken_RBracket:
							case BfToken_RChevron:
							case BfToken_RDblChevron:
								break;
							default:
								// These are definitely dereferences
								return false;
							}
						}

						while (true)
						{
							auto nextNode = mVisitorPos.Get(checkIdx + 1);
							auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode);
							if ((nextToken != NULL) && (nextToken->GetToken() == BfToken_LBracket))
							{
								keepParsing = true;
								break;
							}

							if ((nextToken == NULL) || (nextToken->GetToken() != BfToken_Star))
								break;
							checkTokenNode = nextToken;
							checkToken = checkTokenNode->GetToken();
							checkIdx++;
						}
					}
					else
					{
						auto prevNode = mVisitorPos.Get(checkIdx - 1);
						if (auto prevToken = BfNodeDynCast<BfTokenNode>(prevNode))
						{
							// If this is just a 'loose' comma then it can't be part of a nullable
							if (((prevToken->GetToken() == BfToken_Comma) && (chevronDepth == 0)) ||
								(prevToken->GetToken() == BfToken_LParen))
							{
								return false;
							}
						}
					}

					// Star or Question normally end a TypeRef
					if (keepParsing)
					{
						// Keep going
					}
					else if ((chevronDepth == 0) && (parenDepth == 0) && (bracketDepth == 0))
					{
						if (hadTupleComma)
							return false;

						if (couldBeExpr != NULL)
							*couldBeExpr = false;
						if (outEndNode != NULL)
							*outEndNode = checkIdx + 1;
						if (successToken == BfToken_None)
							return true;
						auto nextNode = mVisitorPos.Get(checkIdx + 1);
						if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
						{
							if (nextToken->GetToken() == successToken)
							{
								if (outEndNode != NULL)
									*outEndNode = checkIdx + 1;
								return true;
							}

							if (nextToken->GetToken() == BfToken_LBracket)
							{
								// A rare case of something like "char*[...]", let the bracket information through
							}
							else
								break;
						}
						else
							return false;
					}
				}
				else if ((checkToken == BfToken_LBracket) || (checkToken == BfToken_QuestionLBracket))
				{
					auto prevNode = mVisitorPos.Get(checkIdx - 1);
					if (auto prevToken = BfNodeDynCast<BfTokenNode>(prevNode))
					{
						// .[ - that's not a valid type, but could be an attributed member reference
						if ((prevToken->GetToken() == BfToken_Dot) || (prevToken->GetToken() == BfToken_DotDot))
						{
							if (outEndNode != NULL)
								*outEndNode = checkIdx - 1;
							return false;
						}
					}

					bracketDepth++;
				}
				else if (checkToken == BfToken_This)
				{
					if ((parenDepth == 1) && (hadIdentifier))
					{
						// If this looks like it's from a '(<type> this ...)' then it could be part of a function declaration, so allow it
					}
					else
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return false;
					}
				}
				else if ((checkToken == BfToken_Delegate) || (checkToken == BfToken_Function))
				{
					int funcEndNode = -1;
					int prevReadPos = mVisitorPos.mReadPos;
					mVisitorPos.mReadPos = checkIdx;
					bool isTypeRef = IsTypeReference(checkNode, BfToken_None, -1, &funcEndNode);
					mVisitorPos.mReadPos = prevReadPos;

					if (!isTypeRef)
					{
						if (outEndNode != NULL)
							*outEndNode = checkIdx;
						return false;
					}

					checkIdx = funcEndNode;
					continue;
				}
				else if ((checkToken == BfToken_Comptype) || (checkToken == BfToken_Decltype) || (checkToken == BfToken_AllocType) || (checkToken == BfToken_RetType) || (checkToken == BfToken_Nullable))
				{
					int endNodeIdx = checkIdx + 1;

					auto nextNode = mVisitorPos.Get(checkIdx + 1);
					if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
					{
						if (tokenNode->mToken != BfToken_LParen)
						{
							isDone = true;
						}
						else
						{
							int openCount = 1;

							while (true)
							{
								endNodeIdx++;

								auto checkNextNode = mVisitorPos.Get(endNodeIdx);
								if (checkNextNode == NULL)
									break;

								if (auto checkNextToken = BfNodeDynCast<BfTokenNode>(checkNextNode))
								{
									if (checkNextToken->GetToken() == BfToken_LParen)
										openCount++;
									else if (checkNextToken->GetToken() == BfToken_RParen)
									{
										openCount--;
										if (openCount == 0)
											break;
									}
								}
							}
						}
					}

					identifierExpected = false;
					checkIdx = endNodeIdx;

					/*if (outEndNode != NULL)
					*outEndNode = endNodeIdx + 1;

					return true;*/
				}
				else if ((checkToken == BfToken_DotDotDot) && (chevronDepth > 0))
				{
					isDone = true;

					auto nextNode = mVisitorPos.Get(checkIdx + 1);
					if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
					{
						if ((nextToken->mToken == BfToken_RChevron) || (nextToken->mToken == BfToken_RDblChevron))
							isDone = false;
					}
				}
				else if (checkToken != BfToken_LBracket)
					isDone = true;

				if (isDone)
				{
					if (outEndNode != NULL)
						*outEndNode = checkIdx;

					if (isGenericType != NULL)
						*isGenericType = false;

					return false;
				}
			}
		}
		else if (bracketDepth > 0)
		{
			// Ignore
		}
		else if ((checkNode->IsA<BfIdentifierNode>()) || (checkNode->IsA<BfMemberReferenceExpression>()))
		{
			// Identifier is always allowed in tuple (parenDepth == 0), because it's potentially the field name
			//  (successToken == BfToken_RParen) infers we are already checking inside parentheses, such as
			//  when we see a potential cast expression
			if (!identifierExpected)
			{
				if ((parenDepth == 0) && (successToken != BfToken_RParen))
				{
					if (outEndNode != NULL)
						*outEndNode = checkIdx;
					if (successToken == BfToken_None)
						return chevronDepth == 0;
					return false;
				}
				hadUnexpectedIdentifier = true;
			}

			hadIdentifier = true;
			identifierExpected = false;
		}
		else if (checkNode->IsA<BfBlock>())
		{
			if (successToken == BfToken_LBrace)
			{
				foundSuccessToken = true;
			}
			break;
		}
		else if ((mCompatMode) && (checkNode->IsExact<BfLiteralExpression>()) && (chevronDepth > 0) && (identifierExpected))
		{
			// Allow
			identifierExpected = false;
		}
		else
		{
			bool mayBeExprPart = false;
			if (chevronDepth > 0)
			{
				if (checkNode->IsExact<BfLiteralExpression>())
					mayBeExprPart = true;
				else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
				{
					if (tokenNode->mToken == BfToken_Question)
						mayBeExprPart = true;
				}
			}

			if (!mayBeExprPart)
			{
				if (outEndNode != NULL)
					*outEndNode = checkIdx;
				return false;
			}
		}
		lastToken = checkTokenNode;
		checkIdx++;
	}
	if (outEndNode != NULL)
		*outEndNode = checkIdx;
	if ((!hadTupleComma) && (hadUnexpectedIdentifier)) // Looked like a tuple but wasn't
		return false;
	return (hadIdentifier) && (chevronDepth == 0) && (bracketDepth == 0) && (parenDepth == 0) && ((successToken == BfToken_None) || (foundSuccessToken));
}

static int sTRIdx = 0;

bool BfReducer::IsTypeReference(BfAstNode* checkNode, BfToken successToken, int endNode, int* outEndNode, bool* couldBeExpr, bool* isGenericType, bool* isTuple)
{
	int retryNode = -1;
	if (IsTypeReference(checkNode, successToken, endNode, &retryNode, outEndNode, couldBeExpr, isGenericType, isTuple))
		return true;

	if ((retryNode != -1) && (successToken == BfToken_None))
	{
 		int newEndNode = -1;
 		if (IsTypeReference(checkNode, successToken, retryNode, &retryNode, &newEndNode, couldBeExpr, isGenericType, isTuple))
 		{
 			if (outEndNode != NULL)
 				*outEndNode = newEndNode;
 			return true;
 		}
	}
	return false;
}

bool BfReducer::IsLocalMethod(BfAstNode* nameNode)
{
	AssertCurrentNode(nameNode);

	int parenDepth = 0;
	bool hadParens = false;
	int chevronDepth = 0;
	bool hadGenericParams = false;

	int checkIdx = mVisitorPos.mReadPos + 1;
	while (true)
	{
		auto checkNode = mVisitorPos.Get(checkIdx);
		if (checkNode == NULL)
			return false;

		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
		{
			BfToken token = tokenNode->GetToken();
			if (token == BfToken_LParen)
			{
				parenDepth++;
				hadParens = true;
			}
			else if (token == BfToken_RParen)
			{
				parenDepth--;
				if (parenDepth == 0)
					return true;
			}
			else
			{
				switch (token)
				{
				case BfToken_Semicolon: // Never can be a local method
					return false;
				case BfToken_Where: // Always denotes a local method
					return true;
				default: break;
				}
			}
		}
		else if (auto tokenNode = BfNodeDynCast<BfBlock>(checkNode))
		{
			//return (hadParens) && (parenDepth == 0);
			return false;
		}
		else
		{
		}

		checkIdx++;
	}
	return false;
}

int BfReducer::QualifiedBacktrack(BfAstNode* endNode, int checkIdx, bool* outHadChevrons)
{
	auto checkNode = endNode;
	BF_ASSERT(checkNode == mVisitorPos.Get(checkIdx));
	int chevronDepth = 0;
	bool identifierExpected = true;
	bool hadEndBracket = false;
	bool lastWasIdentifier = false;
	while (checkNode != NULL)
	{
		auto checkTokenNode = BfNodeDynCast<BfTokenNode>(checkNode);
		if (checkTokenNode != NULL)
		{
			BfToken checkToken = checkTokenNode->GetToken();
			if ((checkToken == BfToken_Dot) || (checkToken == BfToken_DotDot))
			{
				if (chevronDepth == 0)
					return checkIdx;
			}
			else if (checkToken == BfToken_LChevron)
			{
				if (outHadChevrons != NULL)
					*outHadChevrons = true;
				chevronDepth++;
			}
			else if (checkToken == BfToken_RChevron)
			{
				// Was this a case like "Test<T> MethodName"?  Those are split.
				if (lastWasIdentifier)
					return -1;
				chevronDepth--;
			}
			else if (checkToken == BfToken_RDblChevron)
			{
				if (lastWasIdentifier)
					return -1;
				chevronDepth -= 2;
			}
			else if ((checkToken != BfToken_Comma) &&
				(checkToken != BfToken_Dot) &&
				(checkToken != BfToken_DotDot) &&
				(checkToken != BfToken_RBracket) &&
				(checkToken != BfToken_Star) &&
				(checkToken != BfToken_Question) &&
				(checkToken != BfToken_LBracket))
			{
				return -1;
			}
			if (chevronDepth == 0)
				return -1;
			lastWasIdentifier = false;
		}
		else if (checkNode->IsA<BfIdentifierNode>())
		{
			// Two identifiers in a row denotes a break
			if (lastWasIdentifier)
				return -1;
			lastWasIdentifier = true;
		}
		else
		{
			return -1;
		}
		checkIdx--;
		checkNode = mVisitorPos.Get(checkIdx);
	}
	return -1;
}

BfExpression* BfReducer::ApplyToFirstExpression(BfUnaryOperatorExpression* unaryOp, BfExpression* target)
{
	auto condExpression = BfNodeDynCast<BfConditionalExpression>(target);
	if (condExpression != NULL)
	{
		auto result = ApplyToFirstExpression(unaryOp, condExpression->mConditionExpression);
		if (result == condExpression->mConditionExpression)
		{
			//TODO: Make sure this one works, check children and next's and such
			//ReplaceNode(unaryOp, binOpExpression);
			unaryOp->mExpression = condExpression->mConditionExpression;
			ReplaceNode(unaryOp, condExpression);
			unaryOp->SetSrcEnd(condExpression->mConditionExpression->GetSrcEnd());
			condExpression->mConditionExpression = unaryOp;
		}
		condExpression->SetSrcStart(unaryOp->GetSrcStart());
		return result;
	}

	auto binOpExpression = BfNodeDynCast<BfBinaryOperatorExpression>(target);
	if (binOpExpression != NULL)
	{
		auto result = ApplyToFirstExpression(unaryOp, binOpExpression->mLeft);
		if (result == binOpExpression->mLeft)
		{
			unaryOp->mExpression = binOpExpression->mLeft;
			unaryOp->SetSrcEnd(binOpExpression->mLeft->GetSrcEnd());
			binOpExpression->mLeft = unaryOp;
		}
		binOpExpression->SetSrcStart(unaryOp->GetSrcStart());
		return result;
	}

	return target;
}

static String DbgNodeToString(BfAstNode* astNode)
{
	if (auto binOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>(astNode))
	{
		String str;
		str += "(";
		str += DbgNodeToString(binOpExpr->mLeft);
		str += " ";
		str += DbgNodeToString(binOpExpr->mOpToken);
		str += " ";
		str += DbgNodeToString(binOpExpr->mRight);
		str += ")";
		return str;
	}
	else if (auto condExpr = BfNodeDynCast<BfConditionalExpression>(astNode))
	{
		String str;
		str += "( ";
		str += "(";
		str += DbgNodeToString(condExpr->mConditionExpression);
		str += ") ? (";
		str += DbgNodeToString(condExpr->mTrueExpression);
		str += ") : (";
		str += DbgNodeToString(condExpr->mFalseExpression);
		str += ")";
		str += " )";
		return str;
	}

	return astNode->ToString();
}

BfExpression* BfReducer::CheckBinaryOperatorPrecedence(BfBinaryOperatorExpression* binOpExpression)
{
	BfExpression* resultExpr = binOpExpression;

	bool dbg = false;

#ifdef BF_AST_HAS_PARENT_MEMBER
	BF_ASSERT(BfNodeDynCast<BfBinaryOperatorExpression>(binOpExpression->mParent) == NULL);
#endif

	SizedArray<BfBinaryOperatorExpression*, 8> binOpParents;
	SizedArray<BfBinaryOperatorExpression*, 8> deferredChecks;

	BfBinaryOperatorExpression* checkBinOpExpression = binOpExpression;
	while (true)
	{
		if (checkBinOpExpression == NULL)
		{
			if (deferredChecks.size() == 0)
				break;
			checkBinOpExpression = deferredChecks.back();
			deferredChecks.pop_back();
		}

		if (dbg)
			OutputDebugStrF("Checking: %s\n", DbgNodeToString(checkBinOpExpression).c_str());

#ifdef BF_AST_HAS_PARENT_MEMBER
		BfBinaryOperatorExpression* prevBinOpExpression = BfNodeDynCast<BfBinaryOperatorExpression>(checkBinOpExpression->mParent);
		if (prevBinOpExpression != NULL)
		{
			BF_ASSERT(binOpParents.back() == prevBinOpExpression);
		}
#else
		BfBinaryOperatorExpression* prevBinOpExpression = NULL;
#endif

		if (!binOpParents.IsEmpty())
		{
			prevBinOpExpression = binOpParents.back();
		}

		BfBinaryOperatorExpression* nextBinaryOperatorExpression = NULL;

		bool didCondSwap = false;
		while (auto rightCondExpression = BfNodeDynCast<BfConditionalExpression>(checkBinOpExpression->mRight))
		{
			if (rightCondExpression->mTrueExpression == NULL)
				break;

			// Turn (A || (B ? C : D)) into ((A || B) ? C : D)

			BfExpression* exprA = checkBinOpExpression->mLeft;
			BfExpression* exprB = rightCondExpression->mConditionExpression;
			BfExpression* exprC = rightCondExpression->mTrueExpression;
			checkBinOpExpression->SetSrcEnd(exprB->GetSrcEnd());

			MEMBER_SET(rightCondExpression, mConditionExpression, checkBinOpExpression);
			MEMBER_SET(checkBinOpExpression, mLeft, exprA);
			MEMBER_SET(checkBinOpExpression, mRight, exprB);

			didCondSwap = true;

			if (dbg)
			{
				OutputDebugStrF("NewCond: %s\n", DbgNodeToString(rightCondExpression).c_str());
				OutputDebugStrF("CheckAfterCond: %s\n", DbgNodeToString(checkBinOpExpression).c_str());
			}

			if (prevBinOpExpression != NULL)
			{
				BF_ASSERT(checkBinOpExpression == prevBinOpExpression->mRight);
				MEMBER_SET(prevBinOpExpression, mRight, rightCondExpression);
				nextBinaryOperatorExpression = prevBinOpExpression;
				binOpParents.pop_back();
			}
			else
			{
				BF_ASSERT(resultExpr == checkBinOpExpression);
				resultExpr = rightCondExpression;
			}
		}

		if (nextBinaryOperatorExpression != NULL)
		{
			checkBinOpExpression = nextBinaryOperatorExpression;
			continue;
		}

		/*auto _CheckLeftBinaryOpearator = [&](BfBinaryOperatorExpression* checkBinOpExpression)
		{
			while (auto leftBinOpExpression = BfNodeDynCast<BfBinaryOperatorExpression>(checkBinOpExpression->mLeft))
			{
				if (dbg)
				{
					OutputDebugStrF("CheckCur  : %s\n", DbgNodeToString(checkBinOpExpression).c_str());
					OutputDebugStrF("Left      : %s\n", DbgNodeToString(leftBinOpExpression).c_str());
				}

				if (leftBinOpExpression->mRight == NULL)
				{
					BF_ASSERT(mPassInstance->HasFailed());
					return;
				}

				int leftPrecedence = BfGetBinaryOpPrecendence(leftBinOpExpression->mOp);
				int rightPrecedence = BfGetBinaryOpPrecendence(checkBinOpExpression->mOp);

				// Turn ((A + B) * C) into (A + (B * C))
				if (leftPrecedence >= rightPrecedence)
				{
					break;
				}

				BfTokenNode* tokenNode = checkBinOpExpression->mOpToken;

				BfExpression* exprA = leftBinOpExpression->mLeft;
				BfExpression* exprB = leftBinOpExpression->mRight;
				BfExpression* exprC = checkBinOpExpression->mRight;

				auto rightBinOpExpression = leftBinOpExpression; // We reuse this memory for the right side now

				auto binOp = checkBinOpExpression->mOp;
				checkBinOpExpression->mLeft = exprA;
				checkBinOpExpression->mOp = leftBinOpExpression->mOp;
				checkBinOpExpression->mOpToken = leftBinOpExpression->mOpToken;
				checkBinOpExpression->mRight = rightBinOpExpression;

				rightBinOpExpression->mLeft = exprB;
				rightBinOpExpression->mOp = binOp;
				rightBinOpExpression->mOpToken = tokenNode;
				rightBinOpExpression->mRight = exprC;
				rightBinOpExpression->SetSrcStart(rightBinOpExpression->mLeft->GetSrcStart());
				rightBinOpExpression->SetSrcEnd(rightBinOpExpression->mRight->GetSrcEnd());

				if (dbg)
				{
					OutputDebugStrF("CheckAfter  : %s\n", DbgNodeToString(checkBinOpExpression).c_str());
				}
			}
		};*/

		while (auto rightBinOpExpression = BfNodeDynCast<BfBinaryOperatorExpression>(checkBinOpExpression->mRight))
		{
			if (dbg)
			{
				OutputDebugStrF("CheckCur  : %s\n", DbgNodeToString(checkBinOpExpression).c_str());
				OutputDebugStrF("Right     : %s\n", DbgNodeToString(rightBinOpExpression).c_str());
			}

			if (rightBinOpExpression->mRight == NULL)
			{
				BF_ASSERT(mPassInstance->HasFailed());
				return binOpExpression;
			}

			int leftPrecedence = BfGetBinaryOpPrecendence(checkBinOpExpression->mOp);
			int rightPrecedence = BfGetBinaryOpPrecendence(rightBinOpExpression->mOp);

			// Turn (A * (B + C)) into ((A * B) + C)
			//  Note: this DOES need to be '<' in order to preserve left-to-right evaluation when precedence is equal
			if (leftPrecedence < rightPrecedence)
			{
				binOpParents.Add(checkBinOpExpression);
				nextBinaryOperatorExpression = rightBinOpExpression;
				break;
			}

			BfTokenNode* tokenNode = checkBinOpExpression->mOpToken;

			BfExpression* exprA = checkBinOpExpression->mLeft;
			BfExpression* exprB = rightBinOpExpression->mLeft;
			BfExpression* exprC = rightBinOpExpression->mRight;

			auto leftBinOpExpression = rightBinOpExpression; // We reuse this memory for the left side now

			auto binOp = checkBinOpExpression->mOp;
			checkBinOpExpression->mLeft = leftBinOpExpression;
			checkBinOpExpression->mOp = rightBinOpExpression->mOp;
			checkBinOpExpression->mOpToken = rightBinOpExpression->mOpToken;
			checkBinOpExpression->mRight = exprC;

			leftBinOpExpression->mLeft = exprA;
			leftBinOpExpression->mOp = binOp;
			leftBinOpExpression->mOpToken = tokenNode;
			leftBinOpExpression->mRight = exprB;
			leftBinOpExpression->SetSrcStart(leftBinOpExpression->mLeft->GetSrcStart());
			leftBinOpExpression->SetSrcEnd(leftBinOpExpression->mRight->GetSrcEnd());

			if (dbg)
			{
				OutputDebugStrF("CheckAfter: %s\n", DbgNodeToString(checkBinOpExpression).c_str());
			}

			if ((leftPrecedence > rightPrecedence) && (prevBinOpExpression != NULL))
			{
				// Backtrack
				nextBinaryOperatorExpression = prevBinOpExpression;
				binOpParents.pop_back();
				break;
			}

			if (auto leftBinaryExpr = BfNodeDynCast<BfBinaryOperatorExpression>(checkBinOpExpression->mLeft))
			{
				deferredChecks.push_back(leftBinaryExpr);
			}
		}

		checkBinOpExpression = nextBinaryOperatorExpression;
	}

	if (dbg)
		OutputDebugStrF("NodeOut: %s\n", DbgNodeToString(resultExpr).c_str());

	return resultExpr;
}

BfAstNode* BfReducer::ReplaceTokenStarter(BfAstNode* astNode, int idx, bool allowIn)
{
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(astNode))
	{
		if ((tokenNode->GetToken() == BfToken_As) ||
			((tokenNode->GetToken() == BfToken_In) && (!allowIn)))
		{
			if (idx == -1)
				idx = mVisitorPos.mReadPos;
			BF_ASSERT(mVisitorPos.Get(idx) == astNode);

			auto identifierNode = mAlloc->Alloc<BfIdentifierNode>();
			ReplaceNode(tokenNode, identifierNode);
			mVisitorPos.Set(idx, identifierNode);
			return identifierNode;
		}
	}
	return astNode;
}

BfEnumCaseBindExpression* BfReducer::CreateEnumCaseBindExpression(BfTokenNode* bindToken)
{
	auto bindExpr = mAlloc->Alloc<BfEnumCaseBindExpression>();
	MEMBER_SET(bindExpr, mBindToken, bindToken);
	mVisitorPos.MoveNext();

	auto nextNode = mVisitorPos.GetNext();
	if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (nextToken->GetToken() == BfToken_Dot)
		{
			auto memberReferenceExpr = mAlloc->Alloc<BfMemberReferenceExpression>();
			MEMBER_SET(memberReferenceExpr, mDotToken, nextToken);
			mVisitorPos.MoveNext();
			auto memberName = ExpectIdentifierAfter(memberReferenceExpr);
			if (memberName != NULL)
			{
				MEMBER_SET(memberReferenceExpr, mMemberName, memberName);
			}
			MEMBER_SET(bindExpr, mEnumMemberExpr, memberReferenceExpr);
		}
	}

	if (bindExpr->mEnumMemberExpr == NULL)
	{
		auto typeRef = CreateTypeRefAfter(bindExpr);
		if (typeRef != NULL)
		{
			if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(typeRef))
			{
				MEMBER_SET(bindExpr, mEnumMemberExpr, namedTypeRef->mNameNode);
			}
			else
			{
				auto memberRefExpr = mAlloc->Alloc<BfMemberReferenceExpression>();

				if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
				{
					if (auto namedTypeRef = BfNodeDynCast<BfNamedTypeReference>(qualifiedTypeRef->mRight))
					{
						MEMBER_SET(memberRefExpr, mTarget, qualifiedTypeRef->mLeft);
						MEMBER_SET(memberRefExpr, mDotToken, qualifiedTypeRef->mDot);
						MEMBER_SET(memberRefExpr, mMemberName, namedTypeRef->mNameNode);
					}
				}
				else
				{
					MEMBER_SET(memberRefExpr, mTarget, typeRef);
				}

				MEMBER_SET(bindExpr, mEnumMemberExpr, memberRefExpr);
			}
		}

		if (bindExpr->mEnumMemberExpr == NULL)
		{
			Fail("Expected enum case name", typeRef);
		}
	}

	if (bindExpr->mEnumMemberExpr != NULL)
	{
		auto openToken = ExpectTokenAfter(bindExpr->mEnumMemberExpr, BfToken_LParen);
		if (openToken != NULL)
		{
			auto tupleExpr = CreateTupleExpression(openToken, NULL);
			MEMBER_SET(bindExpr, mBindNames, tupleExpr);
		}
	}

	return bindExpr;
}

BfExpression* BfReducer::CreateExpression(BfAstNode* node, CreateExprFlags createExprFlags)
{
	if (node == NULL)
		return NULL;

	BP_ZONE("CreateExpression");

	//
	{
		BP_ZONE("CreateExpression.CheckStack");

		StackHelper stackHelper;
		if (!stackHelper.CanStackExpand(64 * 1024))
		{
			BfExpression* result = NULL;
			if (!stackHelper.Execute([&]()
			{
				result = CreateExpression(node, createExprFlags);
			}))
			{
				Fail("Expression too complex to parse", node);
			}
			return result;
		}
	}

	AssertCurrentNode(node);

	auto rhsCreateExprFlags = (CreateExprFlags)(createExprFlags & CreateExprFlags_BreakOnRChevron);

	auto exprLeft = BfNodeDynCast<BfExpression>(node);

	AssertCurrentNode(node);

	if (auto interpolateExpr = BfNodeDynCastExact<BfStringInterpolationExpression>(node))
	{
		for (auto block : interpolateExpr->mExpressions)
		{
			HandleBlock(block, true);
		}
		return interpolateExpr;
	}

	if ((createExprFlags & (CreateExprFlags_AllowVariableDecl | CreateExprFlags_PermissiveVariableDecl)) != 0)
	{
		bool isLocalVariable = false;
		auto nextNode = mVisitorPos.GetNext();
		BfVariableDeclaration* continuingVariable = NULL;

		int outEndNode = -1;
		bool couldBeExpr = false;
		bool isTuple = false;
		if (IsTypeReference(node, BfToken_None, -1, &outEndNode, &couldBeExpr, NULL, &isTuple))
		{
			if ((createExprFlags & CreateExprFlags_PermissiveVariableDecl) != 0)
				isLocalVariable = true;
			else if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.Get(outEndNode)))
			{
				if (auto equalsToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(outEndNode + 1)))
				{
					if (equalsToken->GetToken() == BfToken_AssignEquals)
						isLocalVariable = true;
				}

				//if (!couldBeExpr)
				{
					auto endingTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(outEndNode - 1));
					// If the type ends with a * or a ? then it could be an expression
					if (endingTokenNode == NULL)
					{
						isLocalVariable = true;
					}
					else
					{
						BfToken endingToken = endingTokenNode->GetToken();
						if (endingToken == BfToken_RParen)
						{
							if (isTuple)
								isLocalVariable = true;
						}
						else if ((endingToken != BfToken_Star) && (endingToken != BfToken_Question))
							isLocalVariable = true;
					}
				}
			}

			if (auto typeNameToken = BfNodeDynCast<BfTokenNode>(node))
			{
				if ((typeNameToken->GetToken() == BfToken_Var) || (typeNameToken->GetToken() == BfToken_Let))
					isLocalVariable = true;
			}
		}

		if (nextNode == NULL)
		{
			// Treat ending identifier as just an identifier (could be block result)
			isLocalVariable = false;
		}
		if ((isLocalVariable) || (continuingVariable != NULL))
		{
			auto variableDeclaration = mAlloc->Alloc<BfVariableDeclaration>();
			BfTypeReference* typeRef = NULL;
			if (continuingVariable != NULL)
			{
				typeRef = continuingVariable->mTypeRef;
				variableDeclaration->mModSpecifier = continuingVariable->mModSpecifier;
				ReplaceNode(node, variableDeclaration);
				variableDeclaration->mPrecedingComma = (BfTokenNode*)node;
			}
			else
			{
				typeRef = CreateTypeRef(node);
				if (typeRef == NULL)
					return NULL;
				ReplaceNode(typeRef, variableDeclaration);
			}

			variableDeclaration->mTypeRef = typeRef;

			BfAstNode* variableNameNode = NULL;

			nextNode = mVisitorPos.GetNext();
			if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (tokenNode->GetToken() == BfToken_LParen)
				{
					mVisitorPos.mReadPos++;
					variableNameNode = CreateTupleExpression(tokenNode);
				}
			}
			if (variableNameNode == NULL)
				variableNameNode = ExpectIdentifierAfter(variableDeclaration, "variable name");
			if (variableNameNode == NULL)
				return variableDeclaration;

			auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
			variableDeclaration->mNameNode = variableNameNode;
			MoveNode(variableNameNode, variableDeclaration);

			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_AssignEquals))
			{
				MEMBER_SET(variableDeclaration, mEqualsNode, tokenNode);
				mVisitorPos.MoveNext();

				if (variableDeclaration->mInitializer == NULL)
					variableDeclaration->mInitializer = CreateExpressionAfter(variableDeclaration);
				if (variableDeclaration->mInitializer == NULL)
					return variableDeclaration;
				MoveNode(variableDeclaration->mInitializer, variableDeclaration);
			}

			exprLeft = variableDeclaration;
		}
	}

	if (auto block = BfNodeDynCast<BfBlock>(node))
	{
		HandleBlock(block, true);
		exprLeft = block;
	}

	if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(exprLeft))
	{
		identifierNode = CompactQualifiedName(identifierNode);

		exprLeft = identifierNode;
		if ((mCompatMode) && (exprLeft->ToString() == "defined"))
		{
			auto definedNodeExpr = mAlloc->Alloc<BfPreprocessorDefinedExpression>();
			ReplaceNode(identifierNode, definedNodeExpr);
			auto nextNode = mVisitorPos.GetNext();
			bool hadParenToken = false;
			if (auto parenToken = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (parenToken->GetToken() == BfToken_LParen)
				{
					mVisitorPos.MoveNext();
					MoveNode(parenToken, definedNodeExpr);
					hadParenToken = true;
				}
			}

			auto definedIdentifier = ExpectIdentifierAfter(definedNodeExpr);
			MEMBER_SET_CHECKED(definedNodeExpr, mIdentifier, definedIdentifier);

			if (hadParenToken)
			{
				auto parenToken = ExpectTokenAfter(definedNodeExpr, BfToken_RParen);
				if (parenToken != NULL)
					MoveNode(parenToken, definedNodeExpr);
			}

			exprLeft = definedNodeExpr;
		}

		int endNodeIdx = -1;
		if ((IsTypeReference(exprLeft, BfToken_LBracket, -1, &endNodeIdx, NULL)))
		{
			if (IsTypeReference(exprLeft, BfToken_LBrace, -1, NULL, NULL))
			{
				BfSizedArrayCreateExpression* arrayCreateExpr = mAlloc->Alloc<BfSizedArrayCreateExpression>();
				auto typeRef = CreateTypeRef(exprLeft);
				if (typeRef != NULL)
				{
					ReplaceNode(typeRef, arrayCreateExpr);

					auto arrayType = BfNodeDynCast<BfArrayTypeRef>(typeRef);
					if (arrayType != NULL)
					{
						arrayCreateExpr->mTypeRef = arrayType;
						auto nextNode = mVisitorPos.GetNext();
						auto block = BfNodeDynCast<BfBlock>(nextNode);
						if (block != NULL)
						{
							Fail("Brace initialization is not supported. Enclose initializer with parentheses.", block);
							auto initializerExpr = CreateCollectionInitializerExpression(block);
							MEMBER_SET(arrayCreateExpr, mInitializer, initializerExpr);
							mVisitorPos.MoveNext();
						}
						exprLeft = arrayCreateExpr;
					}
					else
					{
						Fail("Sized array type expected", typeRef);
					}
				}
			}
			else if (IsTypeReference(exprLeft, BfToken_LParen, -1, NULL, NULL))
			{
				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(endNodeIdx - 1)))
				{
					if ((tokenNode->mToken == BfToken_Star) || (tokenNode->mToken == BfToken_Question)) // Is it something that can ONLY be a sized type reference?
					{
						BfSizedArrayCreateExpression* arrayCreateExpr = mAlloc->Alloc<BfSizedArrayCreateExpression>();
						auto typeRef = CreateTypeRef(exprLeft);
						if (typeRef != NULL)
						{
							ReplaceNode(typeRef, arrayCreateExpr);

							auto arrayType = BfNodeDynCast<BfArrayTypeRef>(typeRef);
							if (arrayType != NULL)
							{
								arrayCreateExpr->mTypeRef = arrayType;
								auto nextNode = mVisitorPos.GetNext();
								if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
								{
									mVisitorPos.MoveNext();
									auto initializerExpr = CreateCollectionInitializerExpression(nextToken);
									MEMBER_SET(arrayCreateExpr, mInitializer, initializerExpr);
								}
								exprLeft = arrayCreateExpr;
							}
							else
							{
								Fail("Sized array type expected", typeRef);
							}
						}
					}
				}
			}
		}
		else if (endNodeIdx != -1)
		{
			if (auto blockNode = BfNodeDynCast<BfBlock>(mVisitorPos.Get(endNodeIdx)))
			{
				auto typeRef = CreateTypeRef(mVisitorPos.GetCurrent());
				if (typeRef)
				{
					exprLeft = TryCreateInitializerExpression(typeRef);
				}
			}
		}
	}

	if (exprLeft == NULL)
	{
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
		{
			BfToken token = tokenNode->GetToken();

			auto nextNode = mVisitorPos.GetNext();
			if ((nextNode != NULL) &&
				((token == BfToken_Checked) ||
				(token == BfToken_Unchecked)))
			{
				mVisitorPos.MoveNext();
				auto nextExpr = CreateExpression(nextNode);
				if (nextExpr == NULL)
					return NULL;
				return nextExpr;
			}
			else if ((token == BfToken_New) ||
				(token == BfToken_Scope) ||
				(token == BfToken_Stack) ||
				(token == BfToken_Append))
			{
				if (token == BfToken_Stack)
					Fail("'Stack' not supported. Use 'scope::' instead.", tokenNode);

				auto allocNode = CreateAllocNode(tokenNode);

				bool isDelegateBind = false;
				bool isLambdaBind = false;
				bool isBoxing = false;

				auto nextNode = mVisitorPos.GetNext();
				if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
				{
					int nextToken = nextTokenNode->GetToken();
					isBoxing = nextToken == BfToken_Box;
					isDelegateBind = nextToken == BfToken_FatArrow;
					isLambdaBind = (nextToken == BfToken_LBracket);

					// Either this is a lambda bind or a dynamic tuple allocation.
					//  We assume it's a lambda bind unless theres a "()" afterward
					if (nextToken == BfToken_LParen)
					{
						int endNode = -1;
						mVisitorPos.mReadPos++;
						if (!IsTypeReference(nextTokenNode, BfToken_LParen, -1, &endNode))
						{
							isLambdaBind = true;
						}
						mVisitorPos.mReadPos--;
					}
				}

				if (auto interpExpr = BfNodeDynCastExact<BfStringInterpolationExpression>(nextNode))
				{
					mVisitorPos.MoveNext();
					auto nextInterpExpr = CreateExpression(nextNode);
					BF_ASSERT(nextInterpExpr == interpExpr);

					interpExpr->mAllocNode = allocNode;
					interpExpr->mTriviaStart = allocNode->mTriviaStart;
					interpExpr->mSrcStart = allocNode->mSrcStart;

					exprLeft = interpExpr;
				}
				else if (isBoxing)
				{
					auto boxExpr = mAlloc->Alloc<BfBoxExpression>();
					ReplaceNode(allocNode, boxExpr);
					boxExpr->mAllocNode = allocNode;
					tokenNode = ExpectTokenAfter(boxExpr, BfToken_Box);
					MEMBER_SET(boxExpr, mBoxToken, tokenNode);
					auto exprNode = CreateExpressionAfter(boxExpr);
					if (exprNode != NULL)
					{
						MEMBER_SET(boxExpr, mExpression, exprNode);
					}
					return boxExpr;
				}
				else if (isLambdaBind)
					exprLeft = CreateLambdaBindExpression(allocNode);
				else if (isDelegateBind)
					exprLeft = CreateDelegateBindExpression(allocNode);
				else
				{
					exprLeft = CreateObjectCreateExpression(allocNode);
					if (auto initExpr = TryCreateInitializerExpression(exprLeft))
						exprLeft = initExpr;
				}

				if (token == BfToken_Append)
				{
#ifdef BF_AST_HAS_PARENT_MEMBER
					auto ctorDeclP = exprLeft->FindParentOfType<BfConstructorDeclaration>();
					if (ctorDeclP != NULL)
						ctorDeclP->mHasAppend = true;
#endif

#ifdef BF_AST_HAS_PARENT_MEMBER
					BF_ASSERT(ctorDecl == ctorDeclP);
#endif
				}
			}
			else if (token == BfToken_This)
			{
				auto thisExpr = mAlloc->Alloc<BfThisExpression>();
				ReplaceNode(tokenNode, thisExpr);
				thisExpr->SetTriviaStart(tokenNode->GetTriviaStart());
				exprLeft = thisExpr;
			}
			else if (token == BfToken_Base)
			{
				auto baseExpr = mAlloc->Alloc<BfBaseExpression>();
				ReplaceNode(tokenNode, baseExpr);
				baseExpr->SetTriviaStart(tokenNode->GetTriviaStart());
				exprLeft = baseExpr;
			}
			else if (token == BfToken_Null)
			{
				BfVariant nullVariant;
				nullVariant.mTypeCode = BfTypeCode_NullPtr;

				auto bfLiteralExpression = mAlloc->Alloc<BfLiteralExpression>();
				bfLiteralExpression->mValue = nullVariant;
				ReplaceNode(tokenNode, bfLiteralExpression);
				bfLiteralExpression->SetTriviaStart(tokenNode->GetTriviaStart());
				exprLeft = bfLiteralExpression;
			}
			else if ((token == BfToken_TypeOf) || (token == BfToken_SizeOf) ||
				(token == BfToken_AlignOf) || (token == BfToken_StrideOf))
			{
				BfTypeAttrExpression* typeAttrExpr = NULL;
				switch (tokenNode->GetToken())
				{
				case BfToken_TypeOf:
					typeAttrExpr = mAlloc->Alloc<BfTypeOfExpression>();
					break;
				case BfToken_SizeOf:
					typeAttrExpr = mAlloc->Alloc<BfSizeOfExpression>();
					break;
				case BfToken_AlignOf:
					typeAttrExpr = mAlloc->Alloc<BfAlignOfExpression>();
					break;
				case BfToken_StrideOf:
					typeAttrExpr = mAlloc->Alloc<BfStrideOfExpression>();
					break;
				default: break;
				}

				ReplaceNode(tokenNode, typeAttrExpr);
				typeAttrExpr->mToken = tokenNode;
				tokenNode = ExpectTokenAfter(typeAttrExpr, BfToken_LParen);
				MEMBER_SET_CHECKED(typeAttrExpr, mOpenParen, tokenNode);
				auto typeRef = CreateTypeRefAfter(typeAttrExpr);
				MEMBER_SET_CHECKED(typeAttrExpr, mTypeRef, typeRef);
				tokenNode = ExpectTokenAfter(typeAttrExpr, BfToken_RParen);
				MEMBER_SET_CHECKED(typeAttrExpr, mCloseParen, tokenNode);
				exprLeft = typeAttrExpr;
			}
			else if (token == BfToken_OffsetOf)
			{
				BfOffsetOfExpression* typeAttrExpr = mAlloc->Alloc<BfOffsetOfExpression>();
				ReplaceNode(tokenNode, typeAttrExpr);
				typeAttrExpr->mToken = tokenNode;
				tokenNode = ExpectTokenAfter(typeAttrExpr, BfToken_LParen);
				MEMBER_SET_CHECKED(typeAttrExpr, mOpenParen, tokenNode);
				auto typeRef = CreateTypeRefAfter(typeAttrExpr);
				MEMBER_SET_CHECKED(typeAttrExpr, mTypeRef, typeRef);

				tokenNode = ExpectTokenAfter(typeAttrExpr, BfToken_Comma);
				MEMBER_SET_CHECKED(typeAttrExpr, mCommaToken, tokenNode);

				auto nameNode = ExpectIdentifierAfter(typeAttrExpr);
				MEMBER_SET_CHECKED(typeAttrExpr, mMemberName, nameNode);

				tokenNode = ExpectTokenAfter(typeAttrExpr, BfToken_RParen);
				MEMBER_SET_CHECKED(typeAttrExpr, mCloseParen, tokenNode);
				exprLeft = typeAttrExpr;
			}
			else if (token == BfToken_Default)
			{
				auto defaultExpr = mAlloc->Alloc<BfDefaultExpression>();
				ReplaceNode(tokenNode, defaultExpr);
				defaultExpr->mDefaultToken = tokenNode;
				if ((tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext())) && (tokenNode->GetToken() == BfToken_LParen))
				{
					mVisitorPos.MoveNext();
					//tokenNode = ExpectTokenAfter(defaultExpr, BfToken_LParen);
					MEMBER_SET_CHECKED(defaultExpr, mOpenParen, tokenNode);
					auto sizeRef = CreateTypeRefAfter(defaultExpr);
					MEMBER_SET_CHECKED(defaultExpr, mTypeRef, sizeRef);
					tokenNode = ExpectTokenAfter(defaultExpr, BfToken_RParen);
					MEMBER_SET_CHECKED(defaultExpr, mCloseParen, tokenNode);
				}

				exprLeft = defaultExpr;
			}
			else if (token == BfToken_IsConst)
			{
				auto isConstExpr = mAlloc->Alloc<BfIsConstExpression>();
				ReplaceNode(tokenNode, isConstExpr);
				isConstExpr->mIsConstToken = tokenNode;
				tokenNode = ExpectTokenAfter(isConstExpr, BfToken_LParen);
				MEMBER_SET_CHECKED(isConstExpr, mOpenParen, tokenNode);
				auto expr = CreateExpressionAfter(isConstExpr);
				MEMBER_SET_CHECKED(isConstExpr, mExpression, expr);
				tokenNode = ExpectTokenAfter(isConstExpr, BfToken_RParen);
				MEMBER_SET_CHECKED(isConstExpr, mCloseParen, tokenNode);
				exprLeft = isConstExpr;
			}
			else if (token == BfToken_Question)
			{
				auto uninitExpr = mAlloc->Alloc<BfUninitializedExpression>();
				ReplaceNode(tokenNode, uninitExpr);
				uninitExpr->mQuestionToken = tokenNode;
				return uninitExpr;
				//MEMBER_SET(variableDeclaration, mInitializer, uninitExpr);
			}
			else if (token == BfToken_Case)
			{
				auto caseExpr = mAlloc->Alloc<BfCaseExpression>();
				ReplaceNode(tokenNode, caseExpr);
				caseExpr->mCaseToken = tokenNode;

				if (auto bindToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if ((bindToken->GetToken() == BfToken_Var) || (bindToken->GetToken() == BfToken_Let))
					{
						auto expr = CreateEnumCaseBindExpression(bindToken);
						if (expr == NULL)
							return caseExpr;
						MEMBER_SET(caseExpr, mCaseExpression, expr);
					}
				}
				if (caseExpr->mCaseExpression == NULL)
				{
					auto expr = CreateExpressionAfter(caseExpr, (CreateExprFlags)(CreateExprFlags_NoAssignment | CreateExprFlags_PermissiveVariableDecl));
					if (expr == NULL)
						return caseExpr;
					MEMBER_SET(caseExpr, mCaseExpression, expr);
				}
				auto equalsNode = ExpectTokenAfter(caseExpr, BfToken_AssignEquals);
				if (equalsNode == NULL)
					return caseExpr;
				MEMBER_SET(caseExpr, mEqualsNode, equalsNode);
				auto expr = CreateExpressionAfter(caseExpr);
				if (expr == NULL)
					return caseExpr;
				MEMBER_SET(caseExpr, mValueExpression, expr);
				return caseExpr;
			}
			else if (token == BfToken_Dot) // Abbreviated dot syntax ".EnumVal"
			{
				// Initializer ".{ x = 1, y = 2 }"
				if (auto blockNode = BfNodeDynCast<BfBlock>(mVisitorPos.GetNext()))
				{
					auto typeRef = CreateTypeRef(mVisitorPos.GetCurrent());
					if (typeRef)
					{
						exprLeft = TryCreateInitializerExpression(typeRef);
					}
				}
				else
				{
					auto memberReferenceExpr = mAlloc->Alloc<BfMemberReferenceExpression>();
					ReplaceNode(tokenNode, memberReferenceExpr);
					MEMBER_SET(memberReferenceExpr, mDotToken, tokenNode);

					bool handled = false;
					if (auto nextToken = BfNodeDynCastExact<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if (nextToken->GetToken() == BfToken_LParen)
						{
							// It's an unnamed dot ctor
							handled = true;
						}
					}

					if (!handled)
					{
						auto memberName = ExpectIdentifierAfter(memberReferenceExpr);
						if (memberName != NULL)
						{
							MEMBER_SET(memberReferenceExpr, mMemberName, memberName);
						}
					}
					// We don't set exprLeft here because it's illegal to do ".EnumVal.SomethingElse".  That wouldn't make
					//  sense because the abbreviated syntax relies on type inference and the ".SomethingElse" wouldn't be
					//  the right type (whatever it is), AND mostly importantly, it breaks autocomplete when we are typing
					//  "KEnum val = ." above a line that starts with a something like a method call "OtherThing.MethodCall()"
					// The exception is if we're creating an enum val with a payload

					//auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
					//if ((nextToken != NULL) && (nextToken->GetToken() == BfToken_LParen))
					{
						exprLeft = memberReferenceExpr;
					}
					/*else
					{
					return memberReferenceExpr;
					}*/
				}
			}
			else if (token == BfToken_LBracket)
			{
				exprLeft = CreateAttributedExpression(tokenNode, false);
			}
			else if (token == BfToken_FatArrow)
			{
				auto delegateBindExpr = mAlloc->Alloc<BfDelegateBindExpression>();
				ReplaceNode(tokenNode, delegateBindExpr);
				MEMBER_SET_CHECKED(delegateBindExpr, mFatArrowToken, tokenNode);
				auto expr = CreateExpressionAfter(delegateBindExpr);
				MEMBER_SET_CHECKED(delegateBindExpr, mTarget, expr);

				auto nextNode = mVisitorPos.GetNext();
				if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
				{
					if (tokenNode->GetToken() == BfToken_LChevron)
					{
						mVisitorPos.MoveNext();
						auto genericParamsDecl = CreateGenericArguments(tokenNode);
						MEMBER_SET_CHECKED(delegateBindExpr, mGenericArgs, genericParamsDecl);
					}
				}

				return delegateBindExpr;
			}
		}
	}

	if (exprLeft == NULL)
	{
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
		{
			BfUnaryOperatorExpression* unaryOpExpr = NULL;

			auto nextNode = mVisitorPos.GetNext();
			if ((tokenNode->GetToken() == BfToken_LParen) && (nextNode != NULL))
			{
				bool couldBeExpr = true;

				// Peek ahead
				int endNodeIdx = -1;
				BfAstNode* endNode = NULL;
				bool isTuple = false;

				bool outerIsTypeRef = IsTypeReference(tokenNode, BfToken_FatArrow, -1, &endNodeIdx, &couldBeExpr, NULL, &isTuple);
				if (outerIsTypeRef)
				{
					if (endNodeIdx != -1)
					{
						endNode = mVisitorPos.Get(endNodeIdx);
						if (auto endToken = BfNodeDynCast<BfTokenNode>(endNode))
						{
							bool isLambda = false;
							if (endToken->GetToken() == BfToken_FatArrow)
							{
								isLambda = true;
								if (auto innerToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
								{
									if (innerToken->mToken != BfToken_RParen)
									{
										// Specifically we're looking for a (function ...) cast, but any token besides a close here means it's not a lambda
										isLambda = false;
									}
								}
							}
							if (isLambda)
								return CreateLambdaBindExpression(NULL, tokenNode);
						}
					}
				}

				bool isCastExpr = false;
				couldBeExpr = false;
				isTuple = false;
				if ((createExprFlags & CreateExprFlags_NoCast) == 0)
					isCastExpr = IsTypeReference(node, BfToken_RParen, -1, &endNodeIdx, &couldBeExpr, NULL, &isTuple);
				if (endNodeIdx != -1)
					endNode = mVisitorPos.Get(endNodeIdx);
				if (isCastExpr)
				{
					bool isValidTupleCast = false;

					auto tokenNextNode = mVisitorPos.Get(mVisitorPos.mReadPos + 1);
					if (auto startToken = BfNodeDynCast<BfTokenNode>(tokenNextNode))
					{
						if (startToken->GetToken() == BfToken_LParen)
						{
							if (endNode != NULL)
							{
								auto afterEndNode = mVisitorPos.Get(endNodeIdx + 1);
								if (auto afterEndToken = BfNodeDynCast<BfTokenNode>(afterEndNode))
								{
									if (afterEndToken->GetToken() == BfToken_RParen)
									{
										isValidTupleCast = true;
										endNode = afterEndToken; // Check this one now, instead...
									}
								}
							}
							if (!isValidTupleCast)
								isCastExpr = false;
						}
					}

					BfAstNode* beforeEndNode = mVisitorPos.Get(endNodeIdx - 1);
					if (auto prevTokenNode = BfNodeDynCast<BfTokenNode>(beforeEndNode))
					{
						int prevToken = prevTokenNode->GetToken();
						// Ending in a "*" or a ">" means it's definitely a type reference
						if ((prevToken == BfToken_Star) || (prevToken == BfToken_RChevron) || (prevToken == BfToken_RDblChevron))
							couldBeExpr = false;
					}

					// It's not a cast expression if a binary operator (like +) immediately follows
					if (couldBeExpr)
					{
						auto endNextNode = mVisitorPos.Get(endNodeIdx + 1);

						if (endNextNode == NULL)
						{
							isCastExpr = false;
						}
						else if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(endNextNode))
						{
							BfToken nextToken = nextTokenNode->GetToken();
							//TODO: Hm. What other tokens make it into a cast expr?
							auto binaryOp = BfTokenToBinaryOp(nextToken);
							// When we have a binary operator token following, it COULD either be a "(double)-val" or it COULD be a "(val2)-val"
							//  But we can't tell until we determine whether the thing inside the paren is a type name or a value name, so we
							//  have special code in BfExprEvaluator that can fix those cases at evaluation time
							if (((binaryOp != BfBinaryOp_None) /*&& (nextToken != BfToken_Star) && (nextToken != BfToken_Ampersand)*/) || // Star could be dereference, not multiply
								(nextToken == BfToken_RParen) ||
								(nextToken == BfToken_LBracket) ||
								(nextToken == BfToken_Dot) ||
								(nextToken == BfToken_DotDot) ||
								(nextToken == BfToken_Comma) ||
								(nextToken == BfToken_Colon) ||
								(nextToken == BfToken_Question) ||
								(nextToken == BfToken_Semicolon) ||
								(nextToken == BfToken_AssignEquals) ||
								(nextToken == BfToken_Case))
								isCastExpr = false;
						}
					}
				}

				if (isCastExpr)
				{
					BfCastExpression* bfCastExpr = NULL;
					if (isTuple)
					{
						// Create typeRef including the parens (tuple type)
						auto castTypeRef = CreateTypeRef(tokenNode);
						if (castTypeRef != NULL)
						{
							bfCastExpr = mAlloc->Alloc<BfCastExpression>();
							ReplaceNode(castTypeRef, bfCastExpr);
							bfCastExpr->mTypeRef = castTypeRef;
						}
					}
					else
					{
						auto castTypeRef = CreateTypeRefAfter(tokenNode);
						if (castTypeRef != NULL)
						{
							bfCastExpr = mAlloc->Alloc<BfCastExpression>();

							ReplaceNode(tokenNode, bfCastExpr);
							bfCastExpr->mOpenParen = tokenNode;

							MEMBER_SET_CHECKED(bfCastExpr, mTypeRef, castTypeRef);
							tokenNode = ExpectTokenAfter(bfCastExpr, BfToken_RParen);
							MEMBER_SET_CHECKED(bfCastExpr, mCloseParen, tokenNode);
						}
					}

					if (bfCastExpr == NULL)
						isCastExpr = false;
					else
					{
						auto expression = CreateExpressionAfter(bfCastExpr);
						if (expression == NULL)
							return bfCastExpr;
						MEMBER_SET(bfCastExpr, mExpression, expression);
						unaryOpExpr = bfCastExpr;
					}
				}

				if (!isCastExpr)
				{
					if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						// Empty tuple?
						if (nextToken->GetToken() == BfToken_RParen)
						{
							auto tupleExpr = mAlloc->Alloc<BfTupleExpression>();
							ReplaceNode(tokenNode, tupleExpr);
							tupleExpr->mOpenParen = tokenNode;
							MEMBER_SET(tupleExpr, mCloseParen, nextToken);
							mVisitorPos.MoveNext();
							return tupleExpr;
						}
					}

					// 					static int sItrIdx = 0;
					// 					sItrIdx++;
					// 					int itrIdx = sItrIdx;
					// 					if (itrIdx == 197)
					// 					{
					// 						NOP;
					// 					}

										// BfParenthesizedExpression or BfTupleExpression
					SetAndRestoreValue<bool> prevInParenExpr(mInParenExpr, true);
					auto innerExpr = CreateExpressionAfter(tokenNode, CreateExprFlags_AllowVariableDecl);
					if (innerExpr == NULL)
						return NULL;

					BfTokenNode* closeParenToken;
					if (innerExpr->IsA<BfIdentifierNode>())
						closeParenToken = ExpectTokenAfter(innerExpr, BfToken_RParen, BfToken_Comma, BfToken_Colon);
					else
						closeParenToken = ExpectTokenAfter(innerExpr, BfToken_RParen, BfToken_Comma);
					if ((closeParenToken == NULL) || (closeParenToken->GetToken() == BfToken_RParen))
					{
						auto parenExpr = mAlloc->Alloc<BfParenthesizedExpression>();
						parenExpr->mExpression = innerExpr;
						ReplaceNode(node, parenExpr);
						parenExpr->mOpenParen = tokenNode;
						MoveNode(innerExpr, parenExpr);

						if (closeParenToken == NULL)
							return parenExpr;

						MEMBER_SET(parenExpr, mCloseParen, closeParenToken);
						exprLeft = parenExpr;

						if ((createExprFlags & CreateExprFlags_ExitOnParenExpr) != 0)
							return exprLeft;
					}
					else
					{
						mVisitorPos.mReadPos--; // Backtrack to before token
						exprLeft = CreateTupleExpression(tokenNode, innerExpr);
					}
				}
			}
			else if ((tokenNode->GetToken() == BfToken_Comptype) || (tokenNode->GetToken() == BfToken_Decltype))
			{
				auto typeRef = CreateTypeRef(tokenNode, CreateTypeRefFlags_EarlyExit);
				if (typeRef != NULL)
				{
					exprLeft = CreateMemberReferenceExpression(typeRef);
				}
			}
			else if (tokenNode->GetToken() == BfToken_NameOf)
			{
				BfNameOfExpression* nameOfExpr = mAlloc->Alloc<BfNameOfExpression>();
				ReplaceNode(tokenNode, nameOfExpr);
				nameOfExpr->mToken = tokenNode;
				tokenNode = ExpectTokenAfter(nameOfExpr, BfToken_LParen);
				MEMBER_SET_CHECKED(nameOfExpr, mOpenParen, tokenNode);

				mVisitorPos.MoveNext();
				int outEndNode = -1;
				bool isTypeRef = IsTypeReference(mVisitorPos.GetCurrent(), BfToken_RParen, -1, &outEndNode);
				mVisitorPos.mReadPos--;

 				if ((isTypeRef) && (outEndNode > 0))
 				{
					if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(outEndNode - 1)))
					{
						if ((tokenNode->mToken == BfToken_RChevron) || (tokenNode->mToken == BfToken_RDblChevron))
						{
							// Can ONLY be a type reference
							auto typeRef = CreateTypeRefAfter(nameOfExpr);
							MEMBER_SET_CHECKED(nameOfExpr, mTarget, typeRef);
						}
					}
 				}

				if (nameOfExpr->mTarget == NULL)
				{
					auto expr = CreateExpressionAfter(nameOfExpr);
					MEMBER_SET_CHECKED(nameOfExpr, mTarget, expr);
				}
				tokenNode = ExpectTokenAfter(nameOfExpr, BfToken_RParen);
				MEMBER_SET_CHECKED(nameOfExpr, mCloseParen, tokenNode);
				exprLeft = nameOfExpr;
			}

			if (exprLeft == NULL)
			{
				BfUnaryOp unaryOp = BfTokenToUnaryOp(tokenNode->GetToken());

				if (unaryOp != BfUnaryOp_None)
				{
					unaryOpExpr = mAlloc->Alloc<BfUnaryOperatorExpression>();
					unaryOpExpr->mOp = unaryOp;
					unaryOpExpr->mOpToken = tokenNode;
					ReplaceNode(tokenNode, unaryOpExpr);

					CreateExprFlags innerFlags = (CreateExprFlags)(rhsCreateExprFlags | CreateExprFlags_EarlyExit);
					if (unaryOp == BfUnaryOp_Cascade)
						innerFlags = (CreateExprFlags)(innerFlags | (createExprFlags & CreateExprFlags_AllowVariableDecl));

					if (unaryOp == BfUnaryOp_PartialRangeThrough) // This allows for just a naked '...'
						innerFlags = (CreateExprFlags)(innerFlags | CreateExprFlags_AllowEmpty);

					// Don't attempt binary or unary operations- they will always be lower precedence
					unaryOpExpr->mExpression = CreateExpressionAfter(unaryOpExpr, innerFlags);
					if (unaryOpExpr->mExpression == NULL)
						return unaryOpExpr;
					MoveNode(unaryOpExpr->mExpression, unaryOpExpr);
				}

				if (unaryOpExpr != NULL)
				{
					exprLeft = unaryOpExpr;

					if (auto binaryOpExpr = BfNodeDynCast<BfBinaryOperatorExpression>(unaryOpExpr->mExpression))
					{
						exprLeft = binaryOpExpr;
						ApplyToFirstExpression(unaryOpExpr, binaryOpExpr);
					}

					if (auto condExpr = BfNodeDynCast<BfConditionalExpression>(unaryOpExpr->mExpression))
					{
						if (exprLeft == unaryOpExpr)
							exprLeft = condExpr;
						ApplyToFirstExpression(unaryOpExpr, condExpr);
					}

					if (auto assignmentExpr = BfNodeDynCast<BfAssignmentExpression>(unaryOpExpr->mExpression))
					{
						// Apply unary operator (likely a dereference) to LHS
						assignmentExpr->RemoveSelf();
						ReplaceNode(unaryOpExpr, assignmentExpr);
						if (assignmentExpr->mLeft != NULL)
						{
							MEMBER_SET(unaryOpExpr, mExpression, assignmentExpr->mLeft);
							unaryOpExpr->SetSrcEnd(assignmentExpr->mLeft->GetSrcEnd());
							MEMBER_SET(assignmentExpr, mLeft, unaryOpExpr);
							if (exprLeft == unaryOpExpr)
								exprLeft = assignmentExpr;
						}
					}

					if (auto dynCastExpr = BfNodeDynCast<BfDynamicCastExpression>(unaryOpExpr->mExpression))
					{
						// Apply unary operator (likely a dereference) to Expr
						dynCastExpr->RemoveSelf();
						ReplaceNode(unaryOpExpr, dynCastExpr);
						if (dynCastExpr->mTarget != NULL)
						{
							MEMBER_SET(unaryOpExpr, mExpression, dynCastExpr->mTarget);
							unaryOpExpr->SetSrcEnd(dynCastExpr->mTarget->GetSrcEnd());
							MEMBER_SET(dynCastExpr, mTarget, unaryOpExpr);
							if (exprLeft == unaryOpExpr)
								exprLeft = dynCastExpr;
						}
					}

					if (auto caseExpr = BfNodeDynCast<BfCaseExpression>(unaryOpExpr->mExpression))
					{
						// Apply unary operator (likely a dereference) to Expr
						caseExpr->RemoveSelf();
						ReplaceNode(unaryOpExpr, caseExpr);
						if (caseExpr->mValueExpression != NULL)
						{
							MEMBER_SET(unaryOpExpr, mExpression, caseExpr->mValueExpression);
							unaryOpExpr->SetSrcEnd(caseExpr->mValueExpression->GetSrcEnd());
							MEMBER_SET(caseExpr, mValueExpression, unaryOpExpr);
							if (exprLeft == unaryOpExpr)
								exprLeft = caseExpr;
						}
					}
				}
			}
		}
	}

	if (exprLeft == NULL)
	{
		if ((createExprFlags & CreateExprFlags_AllowEmpty) == 0)
			Fail("Expected expression", node);
		return NULL;
	}

	while (true)
	{
		auto nextNode = mVisitorPos.GetNext();
		if (nextNode == NULL)
			break;
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			BfToken token = tokenNode->GetToken();

			if (((createExprFlags & CreateExprFlags_BreakOnRChevron) != 0) &&
				((token == BfToken_RChevron) || (token == BfToken_RDblChevron)))
				return exprLeft;

			BfUnaryOp postUnaryOp = BfUnaryOp_None;
			if (token == BfToken_DblPlus)
				postUnaryOp = BfUnaryOp_PostIncrement;
			if (token == BfToken_DblMinus)
				postUnaryOp = BfUnaryOp_PostDecrement;

			if (token == BfToken_DotDotDot)
			{
				//TODO: Detect if this is a BfUnaryOp_PartialRangeFrom
			}

			if (postUnaryOp != BfUnaryOp_None)
			{
				auto unaryOpExpr = mAlloc->Alloc<BfUnaryOperatorExpression>();
				ReplaceNode(exprLeft, unaryOpExpr);
				unaryOpExpr->mOp = postUnaryOp;
				MEMBER_SET(unaryOpExpr, mOpToken, tokenNode);
				MEMBER_SET(unaryOpExpr, mExpression, exprLeft);
				exprLeft = unaryOpExpr;
				mVisitorPos.MoveNext();
				continue;
			}

			if (token == BfToken_As)
			{
				auto dynCastExpr = mAlloc->Alloc<BfDynamicCastExpression>();
				ReplaceNode(exprLeft, dynCastExpr);
				dynCastExpr->mTarget = exprLeft;
				MEMBER_SET(dynCastExpr, mAsToken, tokenNode);
				mVisitorPos.MoveNext();
				auto typeRef = CreateTypeRefAfter(dynCastExpr);
				if (typeRef == NULL)
					return dynCastExpr;
				MEMBER_SET(dynCastExpr, mTypeRef, typeRef);
				return dynCastExpr;
			}

			if (token == BfToken_Is)
			{
				auto checkTypeExpr = mAlloc->Alloc<BfCheckTypeExpression>();
				ReplaceNode(exprLeft, checkTypeExpr);
				checkTypeExpr->mTarget = exprLeft;
				MEMBER_SET(checkTypeExpr, mIsToken, tokenNode);
				mVisitorPos.MoveNext();
				auto typeRef = CreateTypeRefAfter(checkTypeExpr);
				if (typeRef == NULL)
					return checkTypeExpr;
				MEMBER_SET(checkTypeExpr, mTypeRef, typeRef);
				exprLeft = checkTypeExpr;
				continue;
			}

			if (token == BfToken_Question)
			{
				if ((createExprFlags & CreateExprFlags_EarlyExit) != 0)
					return exprLeft;
				auto conditionExpr = mAlloc->Alloc<BfConditionalExpression>();
				ReplaceNode(exprLeft, conditionExpr);
				conditionExpr->mConditionExpression = exprLeft;
				MEMBER_SET(conditionExpr, mQuestionToken, tokenNode);
				mVisitorPos.MoveNext();
				auto expr = CreateExpressionAfter(conditionExpr);
				if (expr != NULL)
				{
					MEMBER_SET(conditionExpr, mTrueExpression, expr);
					tokenNode = ExpectTokenAfter(conditionExpr, BfToken_Colon);
					if (tokenNode != NULL)
					{
						MEMBER_SET(conditionExpr, mColonToken, tokenNode);
						expr = CreateExpressionAfter(conditionExpr);
						if (expr != NULL)
						{
							MEMBER_SET(conditionExpr, mFalseExpression, expr);
						}
					}
				}
				exprLeft = conditionExpr;
				continue;
			}

			if ((token == BfToken_Case) && ((createExprFlags & CreateStmtFlags_NoCaseExpr) == 0))
			{
				if ((createExprFlags & CreateExprFlags_EarlyExit) != 0)
					return exprLeft;
				// If we have a ".Member case <XXX>" expression, that is an invalid construct.  We bail out here
				//  because it allows the ".Member" to autocomplete because we will treat it as a full expression instead
				//  of making it the target of an illegal expression
				if (auto memberRefLeft = BfNodeDynCast<BfMemberReferenceExpression>(exprLeft))
				{
					if (memberRefLeft->mTarget == NULL)
						return exprLeft;
				}

				auto caseExpr = mAlloc->Alloc<BfCaseExpression>();
				ReplaceNode(exprLeft, caseExpr);
				caseExpr->mValueExpression = exprLeft;
				MEMBER_SET(caseExpr, mCaseToken, tokenNode);
				mVisitorPos.MoveNext();
				exprLeft = caseExpr;

				if (auto bindTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					BfToken bindToken = bindTokenNode->GetToken();
					if ((bindToken == BfToken_Var) || (bindToken == BfToken_Let))
					{
						auto expr = CreateEnumCaseBindExpression(bindTokenNode);
						if (expr == NULL)
							continue;
						MEMBER_SET(caseExpr, mCaseExpression, expr);
					}
				}
				if (caseExpr->mCaseExpression == NULL)
				{
					auto expr = CreateExpressionAfter(caseExpr, (CreateExprFlags)(CreateExprFlags_NoAssignment | CreateExprFlags_PermissiveVariableDecl | CreateExprFlags_EarlyExit));
					if (expr == NULL)
						continue;
					MEMBER_SET(caseExpr, mCaseExpression, expr);
				}
				continue;
			}

			if (token == BfToken_LChevron)
			{
				bool hadEndingToken = false;

				// If this is a complex member reference (IE: GType<int>.sVal) then condense in into a BfMemberReference.
				//  We need to be conservative about typeRef names, so GType<int>.A<T>.B.C.D, we can only assume "GType<int>.A<T>" is a typeref
				//  and the ".B.C.D" part is exposed as a MemberReference that may or may not include inner types
				int outNodeIdx = -1;
				bool isGenericType = false;
				bool isTypeRef = ((IsTypeReference(exprLeft, BfToken_None, -1, &outNodeIdx, NULL, &isGenericType)) &&
					(outNodeIdx != -1));
				BfAstNode* outNode = mVisitorPos.Get(outNodeIdx);
				if ((!isTypeRef) && (outNodeIdx != -1))
				{
					if ((isGenericType) && (outNode == NULL))
					{
						for (int checkOutNodeIdx = outNodeIdx + 1; true; checkOutNodeIdx++)
						{
							BfAstNode* checkNode = mVisitorPos.Get(checkOutNodeIdx);
							if (checkNode == NULL)
								break;
							outNode = checkNode;
						}
						isTypeRef = true;
					}
					else if (auto outTokenNode = BfNodeDynCast<BfTokenNode>(outNode))
					{
						BfToken outToken = outTokenNode->GetToken();
						if ((outToken == BfToken_Semicolon) || (outToken == BfToken_RParen) || (outToken == BfToken_Comma) ||
							(outToken == BfToken_Let) || (outToken == BfToken_Var) || (outToken == BfToken_Const))
						{
							auto prevNode = mVisitorPos.Get(outNodeIdx - 1);
							if (auto prevToken = BfNodeDynCast<BfTokenNode>(prevNode))
							{
								// This handles such as "dlg = stack => obj.method<int>;"
								if ((prevToken->GetToken() == BfToken_RChevron) || (prevToken->GetToken() == BfToken_RDblChevron))
									hadEndingToken = true;
							}
						}

						//TODO: We had BfToken_Semicolon here, but it broke legitimate cases of "a.b.c < d.e.f;" expressions
						//if ((outToken == BfToken_Semicolon) || (isGenericType) || (outToken == BfToken_LParen) || (outToken == BfToken_Var) || (outToken == BfToken_Const))
						{
							// Was just 'true'
							int newOutNodeIdx = -1;
							//bool newIsTypeRef = IsTypeReference(exprLeft, outToken, -1, &newOutNodeIdx, NULL, &isGenericType);
							bool newIsTypeRef = IsTypeReference(exprLeft, BfToken_None, outNodeIdx, &newOutNodeIdx, NULL, &isGenericType);
							BfAstNode* newOutNode = mVisitorPos.Get(newOutNodeIdx);
							if ((newIsTypeRef) && (newOutNode == outNode) && (isGenericType))
								isTypeRef = true;
						}
					}
				}

				if (isTypeRef)
				{
					auto startIdentifier = exprLeft;
					int curNodeEndIdx = outNodeIdx - 1;
					BfAstNode* curNodeEnd = mVisitorPos.Get(curNodeEndIdx);

					bool isDotName = false;
					if (auto qualifiedIdentifierNode = BfNodeDynCast<BfMemberReferenceExpression>(startIdentifier))
					{
						// Don't try to convert dot-name to a qualified type
						isDotName = qualifiedIdentifierNode->mTarget == NULL;
					}

					int chevronDepth = 0;

					bool didSplit = false;
					for (int checkIdx = curNodeEndIdx; checkIdx >= mVisitorPos.mReadPos; checkIdx--)
					{
						if (isDotName)
							break;

						auto checkNode = mVisitorPos.Get(checkIdx);
						if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
						{
							BfToken token = tokenNode->GetToken();
							if (((token == BfToken_RChevron) || (token == BfToken_RDblChevron)) && (chevronDepth == 0))
							{
								auto nextCheckNode = mVisitorPos.Get(checkIdx + 1);
								if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(nextCheckNode))
								{
									if (nextTokenNode->GetToken() == BfToken_Dot)
									{
										TryIdentifierConvert(checkIdx + 2);

										auto nextNextCheckNode = mVisitorPos.Get(checkIdx + 2);

										if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(nextNextCheckNode))
										{
											// Remove dot temporarily so we create a typedef up to that dot
											nextTokenNode->SetToken(BfToken_None);
											auto typeRef = CreateTypeRef(startIdentifier);
											if (typeRef == NULL)
												return NULL;
											nextTokenNode->SetToken(BfToken_Dot);

											auto memberRefExpr = mAlloc->Alloc<BfMemberReferenceExpression>();
											ReplaceNode(identifierNode, memberRefExpr);
											MEMBER_SET(memberRefExpr, mDotToken, nextTokenNode);
											MEMBER_SET(memberRefExpr, mTarget, typeRef);
											MEMBER_SET(memberRefExpr, mMemberName, identifierNode);
											exprLeft = memberRefExpr;
											mVisitorPos.mReadPos = checkIdx + 2;
											didSplit = true;
											break;
										}
										else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNextCheckNode))
										{
											if (tokenNode->GetToken() == BfToken_LBracket)
											{
												// Remove dot temporarily so we create a typedef up to that dot
												nextTokenNode->SetToken(BfToken_None);
												auto typeRef = CreateTypeRef(startIdentifier);
												if (typeRef == NULL)
													return NULL;
												nextTokenNode->SetToken(BfToken_Dot);
												mVisitorPos.mReadPos = checkIdx + 0;
												exprLeft = CreateMemberReferenceExpression(typeRef);
												didSplit = true;
											}
										}
									}
									else if (nextTokenNode->GetToken() == BfToken_LBracket)
									{
										int endNodeIdx = -1;
										if (IsTypeReference(startIdentifier, BfToken_LParen, -1, &endNodeIdx))
										{
											if (endNodeIdx > checkIdx + 1)
											{
												auto typeRef = CreateTypeRef(startIdentifier);
												auto arrayType = BfNodeDynCast<BfArrayTypeRef>(typeRef);
												if (arrayType == NULL)
													return NULL;
												mVisitorPos.mReadPos = checkIdx + 0;

												auto arrayCreateExpr = mAlloc->Alloc<BfSizedArrayCreateExpression>();
												ReplaceNode(typeRef, arrayCreateExpr);
												arrayCreateExpr->mTypeRef = arrayType;

												mVisitorPos.mReadPos = endNodeIdx;
												auto initializerExpr = CreateCollectionInitializerExpression(BfNodeDynCast<BfTokenNode>(mVisitorPos.GetCurrent()));
												MEMBER_SET(arrayCreateExpr, mInitializer, initializerExpr);
												exprLeft = arrayCreateExpr;
												return arrayCreateExpr;
											}
										}
									}
								}
							}

							if (token == BfToken_RChevron)
								chevronDepth++;
							else if (token == BfToken_RDblChevron)
								chevronDepth += 2;
							else if (token == BfToken_LChevron)
								chevronDepth--;
							else if (token == BfToken_LDblChevron)
								chevronDepth -= 2;
						}
					}

					if (didSplit)
						continue;
				}

				// Could be a struct generic initializer, or a generic method invocation
				if (auto outToken = BfNodeDynCast<BfTokenNode>(outNode))
				{
					int endNodeIdx = -1;
					if (((outToken->mToken == BfToken_LParen) && (IsTypeReference(exprLeft, BfToken_LParen, -1, &endNodeIdx))) ||
						(outToken->mToken == BfToken_DotDotDot))
					{
						exprLeft = CreateInvocationExpression(exprLeft);
						if (exprLeft == NULL)
							return NULL;
						continue;
					}
				}

				if (hadEndingToken)
					return exprLeft;
			}

			if (token == BfToken_Star)
			{
				auto nextNode = mVisitorPos.GetNext();
				if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
				{
					if ((nextToken->mToken == BfToken_Star) || (nextToken->mToken == BfToken_LBracket))
					{
						//if (IsTypeReference(tokenNode, BfToken_LBracket))
						{
							/*auto typeRef = CreateTypeRef(tokenNode, true);
							exprLeft = CreateObjectCreateExpression(typeRef);
							return exprLeft;*/
						}
					}
				}
			}

			BfBinaryOp binOp = BfTokenToBinaryOp(tokenNode->GetToken());
			if (binOp != BfBinaryOp_None)
			{
				if ((createExprFlags & CreateExprFlags_EarlyExit) != 0)
					return exprLeft;

				mVisitorPos.MoveNext();

				// We only need to check binary operator precedence at the "top level" binary operator
				rhsCreateExprFlags = (CreateExprFlags)(rhsCreateExprFlags | CreateExprFlags_NoCheckBinOpPrecedence);

				if (tokenNode->mToken == BfToken_DotDotDot)
					rhsCreateExprFlags = (CreateExprFlags)(rhsCreateExprFlags | CreateExprFlags_AllowEmpty);

				BfExpression* exprRight = CreateExpressionAfter(tokenNode, rhsCreateExprFlags);

				if (exprRight == NULL)
				{
					if (tokenNode->mToken == BfToken_DotDotDot)
					{
						auto unaryOpExpression = mAlloc->Alloc<BfUnaryOperatorExpression>();
						ReplaceNode(exprLeft, unaryOpExpression);
						unaryOpExpression->mExpression = exprLeft;
						unaryOpExpression->mOp = BfUnaryOp_PartialRangeFrom;
						MEMBER_SET(unaryOpExpression, mOpToken, tokenNode);
						return unaryOpExpression;
					}
				}

				auto binOpExpression = mAlloc->Alloc<BfBinaryOperatorExpression>();
				ReplaceNode(exprLeft, binOpExpression);
				binOpExpression->mLeft = exprLeft;
				binOpExpression->mOp = binOp;
				MEMBER_SET(binOpExpression, mOpToken, tokenNode);

				if (exprRight == NULL)
					return binOpExpression;
				MEMBER_SET(binOpExpression, mRight, exprRight);

				if ((createExprFlags & CreateExprFlags_NoCheckBinOpPrecedence) != 0)
					return binOpExpression;

				return CheckBinaryOperatorPrecedence(binOpExpression);
			}

			auto assignmentOp = BfTokenToAssignmentOp(tokenNode->GetToken());
			if (assignmentOp != BfAssignmentOp_None)
			{
				if ((createExprFlags & CreateExprFlags_EarlyExit) != 0)
					return exprLeft;
				if ((createExprFlags & CreateExprFlags_NoAssignment) != 0)
					return exprLeft;

				auto assignmentExpression = mAlloc->Alloc<BfAssignmentExpression>();
				ReplaceNode(exprLeft, assignmentExpression);
				mVisitorPos.MoveNext();

				assignmentExpression->mOp = assignmentOp;
				assignmentExpression->mLeft = exprLeft;
				assignmentExpression->mOpToken = tokenNode;
				MoveNode(assignmentExpression->mOpToken, assignmentExpression);

				bool continueCascade = false;
				if (auto memberExpr = BfNodeDynCast<BfMemberReferenceExpression>(exprLeft))
				{
					if ((memberExpr->mDotToken != NULL) && (memberExpr->mDotToken->GetToken() == BfToken_DotDot))
						continueCascade = true;
				}

				CreateExprFlags flags = rhsCreateExprFlags;
				if (continueCascade)
					flags = (CreateExprFlags)(rhsCreateExprFlags | CreateExprFlags_BreakOnCascade);

				auto exprRight = CreateExpressionAfter(assignmentExpression, flags);
				if (exprRight == NULL)
				{
					FailAfter("Invalid expression", assignmentExpression);
					return assignmentExpression;
				}

				assignmentExpression->mRight = exprRight;
				MoveNode(assignmentExpression->mRight, assignmentExpression);

				if (continueCascade)
				{
					if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if (nextToken->GetToken() == BfToken_DotDot)
						{
							exprLeft = assignmentExpression;
							continue;
						}
					}
				}

				return assignmentExpression;
			}

			BF_ASSERT(tokenNode->GetToken() == token);

			// Not a binary op, it's a 'close'
			if (token == BfToken_Bang)
			{
				if ((createExprFlags & CreateExprFlags_ExitOnBang) != 0)
					return exprLeft;
				exprLeft = CreateInvocationExpression(exprLeft);
			}
			else if (token == BfToken_LParen)
			{
				exprLeft = CreateInvocationExpression(exprLeft, (CreateExprFlags)(createExprFlags & ~(CreateExprFlags_NoCast)));
				if (auto initExpr = TryCreateInitializerExpression(exprLeft))
					exprLeft = initExpr;
			}
			else if ((token == BfToken_LBracket) || (token == BfToken_QuestionLBracket))
			{
				exprLeft = CreateIndexerExpression(exprLeft);
			}
			else if ((token == BfToken_Dot) || (token == BfToken_DotDot) || (token == BfToken_QuestionDot) || (token == BfToken_Arrow))
			{
				if ((token == BfToken_DotDot) && ((createExprFlags & CreateExprFlags_BreakOnCascade) != 0))
					return exprLeft;

				if (auto memberExpr = BfNodeDynCastExact<BfMemberReferenceExpression>(exprLeft))
				{
					if (memberExpr->mTarget == NULL)
					{
						// A dot syntax like ".A.B" is never valid - to help with autocomplete we stop the expr parsing here
						Fail("Unexpected '.' token", tokenNode);
						return exprLeft;
					}
				}

				exprLeft = CreateMemberReferenceExpression(exprLeft);
			}
			else
			{
				return exprLeft;
			}

			if (exprLeft == NULL)
				return NULL;
		}
		else
			break;
	}

	return exprLeft;
}

BfExpression* BfReducer::CreateExpressionAfter(BfAstNode* node, CreateExprFlags createExprFlags)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	bool isEmpty = false;
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (tokenNode->GetToken() == BfToken_RParen)
			isEmpty = true;
	}

	if ((nextNode == NULL) || (isEmpty))
	{
		FailAfter("Expression expected", node);
		return NULL;
	}
	int startReadPos = mVisitorPos.mReadPos;
	mVisitorPos.MoveNext();
	auto result = CreateExpression(nextNode, createExprFlags);
	if (result == NULL)
	{
		// Nope, didn't handle it
		mVisitorPos.mReadPos = startReadPos;
	}
	return result;
}

BfForEachStatement* BfReducer::CreateForEachStatement(BfAstNode* node, bool hasTypeDecl)
{
	auto forToken = BfNodeDynCast<BfTokenNode>(node);
	auto parenToken = ExpectTokenAfter(forToken, BfToken_LParen);
	if (parenToken == NULL)
		return NULL;

	auto forEachStatement = mAlloc->Alloc<BfForEachStatement>();
	ReplaceNode(forToken, forEachStatement);
	MEMBER_SET(forEachStatement, mForToken, forToken);
	MEMBER_SET(forEachStatement, mOpenParen, parenToken);

	if (hasTypeDecl)
	{
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
		{
			if (tokenNode->mToken == BfToken_ReadOnly)
			{
				MEMBER_SET_CHECKED(forEachStatement, mReadOnlyToken, tokenNode);
				mVisitorPos.MoveNext();
			}
		}

		auto typeRef = CreateTypeRefAfter(forEachStatement);
		if (typeRef == NULL)
			return forEachStatement;
		MEMBER_SET_CHECKED(forEachStatement, mVariableTypeRef, typeRef);
	}

	if (auto nextNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
	{
		if ((nextNode->mToken == BfToken_LParen) || (nextNode->mToken == BfToken_LessEquals))
		{
			mVisitorPos.MoveNext();
			auto tupleNode = CreateTupleExpression(nextNode);
			MEMBER_SET_CHECKED(forEachStatement, mVariableName, tupleNode);
		}
	}

	if (forEachStatement->mVariableName == NULL)
	{
		auto name = ExpectIdentifierAfter(forEachStatement, "variable name");
		MEMBER_SET_CHECKED(forEachStatement, mVariableName, name);
	}
	auto inToken = ExpectTokenAfter(forEachStatement, BfToken_In, BfToken_LChevron, BfToken_LessEquals);
	MEMBER_SET_CHECKED(forEachStatement, mInToken, inToken);
	auto expr = CreateExpressionAfter(forEachStatement);
	MEMBER_SET_CHECKED(forEachStatement, mCollectionExpression, expr);
	parenToken = ExpectTokenAfter(forEachStatement, BfToken_RParen);
	MEMBER_SET_CHECKED(forEachStatement, mCloseParen, parenToken);

	auto stmt = CreateStatementAfter(forEachStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt == NULL)
		return forEachStatement;
	MEMBER_SET(forEachStatement, mEmbeddedStatement, stmt);

	return forEachStatement;
}

BfStatement* BfReducer::CreateForStatement(BfAstNode* node)
{
	int startReadIdx = mVisitorPos.mReadPos;
	auto forToken = BfNodeDynCast<BfTokenNode>(node);
	auto parenToken = ExpectTokenAfter(forToken, BfToken_LParen);
	if (parenToken == NULL)
		return NULL;

	int outNodeIdx = -1;
	auto nextNode = mVisitorPos.GetNext();
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		// Handle 'for (let (key, value) in dict)'
		if ((tokenNode->mToken == BfToken_Let) || (tokenNode->mToken == BfToken_Var))
		{
			if (auto afterLet = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2)))
			{
				if (afterLet->mToken == BfToken_LParen)
				{
					bool isTupleIn = true;
					int parenDepth = 1;
					for (int readPos = mVisitorPos.mReadPos + 3; true; readPos++)
					{
						auto checkNode = mVisitorPos.Get(readPos);
						if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
						{
							if (tokenNode->mToken == BfToken_RParen)
							{
								if (parenDepth != 1)
								{
									isTupleIn = false;
									break;
								}
								parenDepth--;
							}
							else if (tokenNode->mToken == BfToken_In)
							{
								if (parenDepth != 0)
									isTupleIn = false;
								break;
							}
							else if (tokenNode->mToken == BfToken_Comma)
							{
								//
							}
							else
							{
								isTupleIn = false;
								break;
							}
						}
						else if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(checkNode))
						{
							//
						}
						else
						{
							isTupleIn = false;
							break;
						}
					}

					if (isTupleIn)
					{
						mVisitorPos.mReadPos = startReadIdx;
						return CreateForEachStatement(node, true);
					}
				}
			}
		}
	}

	bool isTypeRef = false;
	if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (nextNode->mToken == BfToken_ReadOnly)
		{
			mVisitorPos.mReadPos += 2;
			isTypeRef = IsTypeReference(mVisitorPos.Get(mVisitorPos.mReadPos), BfToken_None, -1, &outNodeIdx);
			mVisitorPos.mReadPos -= 2;
		}
	}
	if (!isTypeRef)
	{
		mVisitorPos.mReadPos++;
		isTypeRef = IsTypeReference(nextNode, BfToken_None, -1, &outNodeIdx);
		mVisitorPos.mReadPos--;
	}

	BfAstNode* outNode = mVisitorPos.Get(outNodeIdx);
	if (isTypeRef)
	{
		auto nextNode = mVisitorPos.Get(outNodeIdx + 1);
		if ((outNode != NULL) && (nextNode != NULL))
		{
			auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if (tokenNode != NULL)
			{
				int token = tokenNode->GetToken();
				if ((token == BfToken_In) || (token == BfToken_LChevron) || (token == BfToken_LessEquals))
				{
					mVisitorPos.mReadPos = startReadIdx;
					return CreateForEachStatement(node, true);
				}
			}
		}
	}
	else
	{
		int checkNodeIdx = (outNodeIdx != -1) ? outNodeIdx : mVisitorPos.mReadPos + 1;
		auto checkNode = mVisitorPos.Get(checkNodeIdx);
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(checkNode))
		{
			BfToken token = tokenNode->GetToken();
			// Is this an 'anonymous' foreach?
			if ((token != BfToken_Semicolon) && (token != BfToken_LChevron) &&
				(token != BfToken_In) && (token != BfToken_AssignEquals))
			{
				mVisitorPos.mReadPos = startReadIdx;

				auto forToken = BfNodeDynCast<BfTokenNode>(node);
				auto parenToken = ExpectTokenAfter(forToken, BfToken_LParen);
				if (parenToken == NULL)
					return NULL;

				auto forEachStatement = mAlloc->Alloc<BfForEachStatement>();
				ReplaceNode(forToken, forEachStatement);
				MEMBER_SET(forEachStatement, mForToken, forToken);
				MEMBER_SET(forEachStatement, mOpenParen, parenToken);

				auto expr = CreateExpressionAfter(forEachStatement);
				MEMBER_SET_CHECKED(forEachStatement, mCollectionExpression, expr);
				parenToken = ExpectTokenAfter(forEachStatement, BfToken_RParen);
				MEMBER_SET_CHECKED(forEachStatement, mCloseParen, parenToken);

				auto stmt = CreateStatementAfter(forEachStatement, CreateStmtFlags_FindTrailingSemicolon);
				if (stmt == NULL)
					return forEachStatement;
				MEMBER_SET(forEachStatement, mEmbeddedStatement, stmt);

				return forEachStatement;
			}
		}
	}

	auto nextNextNode = mVisitorPos.Get(mVisitorPos.mReadPos + 2);
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNextNode))
	{
		if ((tokenNode != NULL) && ((tokenNode->GetToken() == BfToken_LChevron) || (tokenNode->GetToken() == BfToken_In)))
		{
			Fail("Ranged for statement must declare new value variable, consider adding a type name, 'var', or 'let'", tokenNode);
			mVisitorPos.mReadPos = startReadIdx;
			return CreateForEachStatement(node, false);
		}
	}

	BfAstNode* stmt;
	auto forStatement = mAlloc->Alloc<BfForStatement>();
	BfDeferredAstSizedArray<BfAstNode*> initializers(forStatement->mInitializers, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> initializerCommas(forStatement->mInitializerCommas, mAlloc);
	BfDeferredAstSizedArray<BfAstNode*> iterators(forStatement->mIterators, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> iteratorCommas(forStatement->mIteratorCommas, mAlloc);

	ReplaceNode(forToken, forStatement);
	MEMBER_SET(forStatement, mForToken, forToken);
	MEMBER_SET(forStatement, mOpenParen, parenToken);

	// Initializers
	for (int listIdx = 0; true; listIdx++)
	{
		auto nextNode = mVisitorPos.GetNext();
		if ((listIdx > 0) || (!IsSemicolon(nextNode)))
		{
			auto stmt = CreateStatementAfter(forStatement);
			if (stmt == NULL)
				return forStatement;

			if (!initializers.IsEmpty())
			{
				// Try to convert 'int i = 0, j = 0` into two variable declarations instead of a var decl and an assignment
				if (auto prevExprStmt = BfNodeDynCast<BfExpressionStatement>(initializers.back()))
				{
					if (auto prevVarDecl = BfNodeDynCast<BfVariableDeclaration>(prevExprStmt->mExpression))
					{
						if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(stmt))
						{
							if (auto assignExpr = BfNodeDynCast<BfAssignmentExpression>(exprStmt->mExpression))
							{
								if (assignExpr->mOp != BfAssignmentOp_Assign)
									continue;

								if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(assignExpr->mLeft))
								{
									auto varDecl = mAlloc->Alloc<BfVariableDeclaration>();
									ReplaceNode(assignExpr, varDecl);
									varDecl->mTypeRef = prevVarDecl->mTypeRef;
									varDecl->mNameNode = identifierNode;
									varDecl->mEqualsNode = assignExpr->mOpToken;
									varDecl->mInitializer = assignExpr->mRight;
									varDecl->mModSpecifier = prevVarDecl->mModSpecifier;
									exprStmt->mExpression = varDecl;
								}
							}
						}
					}
				}
			}

			initializers.push_back(stmt);
			MoveNode(stmt, forStatement);
		}

		nextNode = mVisitorPos.GetNext();
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode == NULL)
			return forStatement;
		if (tokenNode->GetToken() == BfToken_Semicolon)
		{
			MEMBER_SET(forStatement, mInitializerSemicolon, tokenNode);
			mVisitorPos.MoveNext();
			break;
		}
		else if (tokenNode->GetToken() == BfToken_Comma)
		{
			MoveNode(tokenNode, forStatement);
			initializerCommas.push_back(tokenNode);
			mVisitorPos.MoveNext();
		}
		else
		{
			Fail("Expected ',' or ';'", tokenNode);
			return forStatement;
		}
	}

	// Condition
	nextNode = mVisitorPos.GetNext();
	if (!IsSemicolon(nextNode))
	{
		bool doExpr = true;
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			if (tokenNode->GetToken() == BfToken_RParen)
			{
				Fail("Expected expression or ';'", tokenNode);
				doExpr = false;
			}
		}
		if (doExpr)
		{
			auto expr = CreateExpressionAfter(forStatement);
			if (expr == NULL)
				return forStatement;
			MEMBER_SET(forStatement, mCondition, expr);
		}
	}
	auto tokenNode = ExpectTokenAfter(forStatement, BfToken_Semicolon);
	if (tokenNode == NULL)
		return forStatement;
	MEMBER_SET(forStatement, mConditionSemicolon, tokenNode);

	// Iterators
	for (int listIdx = 0; true; listIdx++)
	{
		if (listIdx == 0)
		{
			auto nextNode = mVisitorPos.GetNext();
			if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (tokenNode->GetToken() == BfToken_RParen)
				{
					MEMBER_SET(forStatement, mCloseParen, tokenNode);
					mVisitorPos.MoveNext();
					break;
				}
			}
		}

		auto stmt = CreateStatementAfter(forStatement);
		if (stmt == NULL)
			return forStatement;
		iterators.push_back(stmt);
		MoveNode(stmt, forStatement);

		auto nextNode = mVisitorPos.GetNext();
		tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode == NULL)
			return forStatement;
		if (tokenNode->GetToken() == BfToken_RParen)
		{
			MEMBER_SET(forStatement, mCloseParen, tokenNode);
			mVisitorPos.MoveNext();
			break;
		}
		else if (tokenNode->GetToken() == BfToken_Comma)
		{
			MoveNode(tokenNode, forStatement);
			iteratorCommas.push_back(tokenNode);
			mVisitorPos.MoveNext();
		}
		else
		{
			Fail("Expected ',' or ')'", tokenNode);
			return forStatement;
		}
	}

	stmt = CreateStatementAfter(forStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt == NULL)
		return forStatement;
	MEMBER_SET(forStatement, mEmbeddedStatement, stmt);

	return forStatement;
}

BfUsingStatement* BfReducer::CreateUsingStatement(BfAstNode* node)
{
	auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
	auto usingStatement = mAlloc->Alloc<BfUsingStatement>();
	ReplaceNode(tokenNode, usingStatement);
	MEMBER_SET(usingStatement, mUsingToken, tokenNode);
	tokenNode = ExpectTokenAfter(usingStatement, BfToken_LParen);
	if (tokenNode == NULL)
		return NULL;
	MEMBER_SET(usingStatement, mOpenParen, tokenNode);

	auto nextNode = mVisitorPos.GetNext();
	if (nextNode != NULL)
	{
		BfVariableDeclaration* varDecl = mAlloc->Alloc<BfVariableDeclaration>();

		int outNodeIdx = -1;
		auto nextNode = mVisitorPos.GetNext();
		mVisitorPos.mReadPos++;
		bool isTypeReference = IsTypeReference(nextNode, BfToken_None, -1, &outNodeIdx);
		mVisitorPos.mReadPos--;
		if (isTypeReference)
		{
			BfAstNode* outNode = mVisitorPos.Get(outNodeIdx);
			BfAstNode* outNodeNext = mVisitorPos.Get(outNodeIdx + 1);
			BfTokenNode* equalsNode = NULL;
			if ((outNode != NULL) && (BfNodeIsA<BfIdentifierNode>(outNode)) && (BfNodeIsA<BfTokenNode>(outNodeNext)))
			{
				auto typeRef = CreateTypeRefAfter(usingStatement);
				if (typeRef == NULL)
					return usingStatement;
				MEMBER_SET(varDecl, mTypeRef, typeRef);
				usingStatement->SetSrcEnd(typeRef->GetSrcEnd());

				auto nameNode = ExpectIdentifierAfter(usingStatement, "variable name");
				if (nameNode == NULL)
					return usingStatement;

				MEMBER_SET(varDecl, mNameNode, nameNode);
				usingStatement->SetSrcEnd(varDecl->GetSrcEnd());

				auto equalsNode = ExpectTokenAfter(usingStatement, BfToken_AssignEquals);
				if (equalsNode == NULL)
					return usingStatement;
				MEMBER_SET(varDecl, mEqualsNode, equalsNode);
				usingStatement->SetSrcEnd(equalsNode->GetSrcEnd());
			}
		}

		auto expr = CreateExpressionAfter(usingStatement);
		if (expr == NULL)
			return usingStatement;
		MEMBER_SET(varDecl, mInitializer, expr);
		MEMBER_SET(usingStatement, mVariableDeclaration, varDecl);
	}
	tokenNode = ExpectTokenAfter(usingStatement, BfToken_RParen);
	if (tokenNode == NULL)
		return usingStatement;
	MEMBER_SET(usingStatement, mCloseParen, tokenNode);

	auto stmt = CreateStatementAfter(usingStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt == NULL)
		return usingStatement;
	MEMBER_SET(usingStatement, mEmbeddedStatement, stmt);

	return usingStatement;
}

BfWhileStatement* BfReducer::CreateWhileStatement(BfAstNode* node)
{
	auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
	auto whileStatement = mAlloc->Alloc<BfWhileStatement>();
	ReplaceNode(tokenNode, whileStatement);
	MEMBER_SET(whileStatement, mWhileToken, tokenNode);
	tokenNode = ExpectTokenAfter(whileStatement, BfToken_LParen);
	if (tokenNode == NULL)
		return NULL;
	MEMBER_SET(whileStatement, mOpenParen, tokenNode);

	// Condition
	auto nextNode = mVisitorPos.GetNext();
	if (!IsSemicolon(nextNode))
	{
		auto expr = CreateExpressionAfter(whileStatement);
		if (expr == NULL)
			return whileStatement;
		MEMBER_SET(whileStatement, mCondition, expr);
	}
	tokenNode = ExpectTokenAfter(whileStatement, BfToken_RParen);
	if (tokenNode == NULL)
		return whileStatement;
	MEMBER_SET(whileStatement, mCloseParen, tokenNode);

	auto stmt = CreateStatementAfter(whileStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt == NULL)
		return whileStatement;
	MEMBER_SET(whileStatement, mEmbeddedStatement, stmt);

	return whileStatement;
}

BfDoStatement* BfReducer::CreateDoStatement(BfAstNode* node)
{
	auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
	auto doStatement = mAlloc->Alloc<BfDoStatement>();
	ReplaceNode(tokenNode, doStatement);
	MEMBER_SET(doStatement, mDoToken, tokenNode);

	auto stmt = CreateStatementAfter(doStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt != NULL)
	{
		MEMBER_SET(doStatement, mEmbeddedStatement, stmt);
	}

	return doStatement;
}

BfRepeatStatement* BfReducer::CreateRepeatStatement(BfAstNode* node)
{
	auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
	auto repeatStatement = mAlloc->Alloc<BfRepeatStatement>();
	ReplaceNode(tokenNode, repeatStatement);
	MEMBER_SET(repeatStatement, mRepeatToken, tokenNode);

	auto stmt = CreateStatementAfter(repeatStatement, CreateStmtFlags_FindTrailingSemicolon);
	if (stmt != NULL)
	{
		MEMBER_SET(repeatStatement, mEmbeddedStatement, stmt);
	}
	tokenNode = ExpectTokenAfter(repeatStatement, BfToken_While);
	if (tokenNode != NULL)
	{
		MEMBER_SET(repeatStatement, mWhileToken, tokenNode);
	}
	tokenNode = ExpectTokenAfter(repeatStatement, BfToken_LParen);
	if (tokenNode != NULL)
	{
		MEMBER_SET(repeatStatement, mOpenParen, tokenNode);
	}

	// Condition
	auto nextNode = mVisitorPos.GetNext();
	if (!IsSemicolon(nextNode))
	{
		auto expr = CreateExpressionAfter(repeatStatement);
		if (expr == NULL)
			return repeatStatement;
		MEMBER_SET(repeatStatement, mCondition, expr);
	}
	tokenNode = ExpectTokenAfter(repeatStatement, BfToken_RParen);
	if (tokenNode != NULL)
	{
		MEMBER_SET(repeatStatement, mCloseParen, tokenNode);
	}

	return repeatStatement;
}

BfSwitchStatement* BfReducer::CreateSwitchStatement(BfTokenNode* tokenNode)
{
	auto switchStatement = mAlloc->Alloc<BfSwitchStatement>();
	BfDeferredAstSizedArray<BfSwitchCase*> switchCases(switchStatement->mSwitchCases, mAlloc);
	ReplaceNode(tokenNode, switchStatement);
	switchStatement->mSwitchToken = tokenNode;
	tokenNode = ExpectTokenAfter(switchStatement, BfToken_LParen);
	if (tokenNode != NULL)
	{
		MEMBER_SET(switchStatement, mOpenParen, tokenNode);
	}
	auto switchValue = CreateExpressionAfter(switchStatement);
	if (switchValue != NULL)
	{
		MEMBER_SET(switchStatement, mSwitchValue, switchValue);
	}
	tokenNode = ExpectTokenAfter(switchStatement, BfToken_RParen);
	if (tokenNode != NULL)
	{
		MEMBER_SET(switchStatement, mCloseParen, tokenNode);
	}

	auto block = ExpectBlockAfter(switchStatement);
	if (block == NULL)
		return switchStatement;
	MoveNode(block, switchStatement);
	switchStatement->mOpenBrace = block->mOpenBrace;
	switchStatement->mCloseBrace = block->mCloseBrace;

	bool hadEmptyCaseStatement = false;
	BfSwitchCase* switchCase = NULL;

	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));
	bool isDone = !mVisitorPos.MoveNext();
	while (!isDone)
	{
		auto child = mVisitorPos.GetCurrent();
		tokenNode = BfNodeDynCast<BfTokenNode>(child);
		BfToken token = BfToken_None;
		if (tokenNode != NULL)
			token = tokenNode->GetToken();
		if ((tokenNode == NULL) ||
			((token != BfToken_Case) && (token != BfToken_When) && (token != BfToken_Default)))
		{
			Fail("Expected 'case'", child);
			AddErrorNode(child);
			isDone = !mVisitorPos.MoveNext();
			continue;
		}

		//TODO: This error was getting annoying... Put back?
		// This check is here at the top, being processed for the previous switchCase because
		//  we don't throw an error on the last case in the switch
		/*if (hadEmptyCaseStatement)
		FailAfter("Expected case statement, 'fallthrough', or 'break'", switchCase);*/

		bool isDefault = token == BfToken_Default;

		switchCase = mAlloc->Alloc<BfSwitchCase>();
		BfDeferredAstSizedArray<BfExpression*> caseExpressions(switchCase->mCaseExpressions, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> caseCommas(switchCase->mCaseCommas, mAlloc);
		ReplaceNode(tokenNode, switchCase);
		BfTokenNode* whenToken = NULL;
		if (token == BfToken_When)
			whenToken = tokenNode;
		else
			switchCase->mCaseToken = tokenNode;

		for (int caseIdx = 0; true; caseIdx++)
		{
			if (!isDefault)
			{
				BfExpression* expr = NULL;

				bool wasWhenSet = whenToken != NULL;
				if (!wasWhenSet)
				{
					auto nextNode = mVisitorPos.GetNext();
					whenToken = BfNodeDynCast<BfTokenNode>(nextNode);
					if ((whenToken != NULL) && (whenToken->GetToken() == BfToken_When))
					{
						mVisitorPos.MoveNext();
					}
					else
						whenToken = NULL;
				}
				if (whenToken != NULL)
				{
					auto whenExpr = mAlloc->Alloc<BfWhenExpression>();
					whenExpr->mWhenToken = whenToken;
					ReplaceNode(whenToken, whenExpr);
					//mVisitorPos.MoveNext();
					//auto exprAfter = wasWhenSet ? (BfAstNode*)switchCase : (BfAstNode*)whenExpr;
					if (expr == NULL)
					{
						auto innerExpr = CreateExpressionAfter(whenToken);
						if (innerExpr != NULL)
						{
							MEMBER_SET(whenExpr, mExpression, innerExpr);
						}
						expr = whenExpr;
					}
				}

				if (expr == NULL)
				{
					if (auto bindToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if ((bindToken->GetToken() == BfToken_Var) || (bindToken->GetToken() == BfToken_Let))
						{
							expr = CreateEnumCaseBindExpression(bindToken);
						}
					}
				}

				if (expr == NULL)
					expr = CreateExpressionAfter(switchCase);
				if (expr == NULL)
				{
					///
				}
				else
				{
					caseExpressions.push_back(expr);
					MoveNode(expr, switchCase);
				}
			}

			if ((whenToken == NULL) && (!isDefault))
				tokenNode = ExpectTokenAfter(switchCase, BfToken_When, BfToken_Colon, BfToken_Comma);
			else
				tokenNode = ExpectTokenAfter(switchCase, BfToken_Colon);
			if (tokenNode == NULL)
				break;

			if (tokenNode->GetToken() == BfToken_When)
			{
				whenToken = tokenNode;
				continue;
			}

			if (tokenNode->GetToken() == BfToken_Colon)
			{
				MEMBER_SET(switchCase, mColonToken, tokenNode);
				break;
			}
			if (isDefault)
			{
				Fail("Expected ':'", tokenNode);
				break;
			}

			MoveNode(tokenNode, switchCase);
			caseCommas.push_back(tokenNode);

			BF_ASSERT(whenToken == NULL);
		}

		if (isDefault)
		{
			if (switchStatement->mDefaultCase != NULL)
			{
				Fail("Only one default case is allowed", switchCase);
				// Add to normal switch cases just so we process it
				switchCases.push_back(switchCase);
			}
			else
				switchStatement->mDefaultCase = switchCase;
		}
		else
		{
			if (switchStatement->mDefaultCase != NULL)
			{
				Fail("Default case must be last case", switchStatement->mDefaultCase);
			}
			switchCases.push_back(switchCase);
		}

		hadEmptyCaseStatement = true;

		auto codeBlock = mAlloc->Alloc<BfBlock>();
		//codeBlock->mSource = switchCase->mSource;
		MEMBER_SET(switchCase, mCodeBlock, codeBlock);
		//switchCase->Add(codeBlock);

		BfDeferredAstSizedArray<BfAstNode*> codeBlockChildArr(codeBlock->mChildArr, mAlloc);
		SetAndRestoreValue<BfAstNode*> prevLastBlockNode(mLastBlockNode, NULL);

		while (true)
		{
			auto nextNode = mVisitorPos.GetNext();
			auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if (tokenNode != NULL)
			{
				int token = tokenNode->GetToken();
				if ((token == BfToken_Case) || (token == BfToken_Default) || (token == BfToken_When))
					break; // Done! No fallthrough
			}

			nextNode = mVisitorPos.GetNext();
			if (nextNode == NULL)
				break;

			hadEmptyCaseStatement = false;
			mVisitorPos.MoveNext();
			// We need to use CreateStmtFlags_NoCaseExpr because otherwise during typing a new statement at the end of one case, it
			//  could interpret the 'case' token for the next case as being part of a case expression in the first case
			auto stmt = CreateStatement(nextNode, (CreateStmtFlags)(CreateStmtFlags_FindTrailingSemicolon | CreateStmtFlags_NoCaseExpr | CreateStmtFlags_AllowLocalFunction));
			if (stmt == NULL)
			{
				AddErrorNode(nextNode);
			}
			else
			{
				MoveNode(stmt, codeBlock);
				codeBlockChildArr.push_back(stmt);
			}

			mLastBlockNode = stmt;
		}
		MoveNode(switchCase, switchStatement);
		if (!codeBlock->IsInitialized())
		{
			int srcPos = switchCase->GetSrcEnd();
			codeBlock->Init(srcPos, srcPos, srcPos);
		}

		isDone = !mVisitorPos.MoveNext();
	}

	return switchStatement;
}

// Does everything but pull the trailing semicolon
BfAstNode* BfReducer::DoCreateStatement(BfAstNode* node, CreateStmtFlags createStmtFlags)
{
	auto subCreateStmtFlags = (CreateStmtFlags)(createStmtFlags & (CreateStmtFlags_NoCaseExpr | CreateStmtFlags_FindTrailingSemicolon | CreateStmtFlags_AllowUnterminatedExpression));

	if (node->IsA<BfBlock>())
	{
		auto block = (BfBlock*)node;
		HandleBlock(block, (createStmtFlags & CreateStmtFlags_AllowUnterminatedExpression) != 0);
		return block;
	}

	BfVariableDeclaration* continuingVariable = NULL;

	BfTokenNode* refToken = NULL;
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
	{
		int token = tokenNode->GetToken();
		if ((token == BfToken_Ref) || (token == BfToken_Mut))
		{
			refToken = tokenNode;
		}
		else if (token == BfToken_Semicolon)
		{
			Fail("Empty statement not allowed", tokenNode);
			auto emptyStatement = mAlloc->Alloc<BfEmptyStatement>();
			ReplaceNode(tokenNode, emptyStatement);
			emptyStatement->mTrailingSemicolon = tokenNode;
			return emptyStatement;
		}
		else if (token == BfToken_Break)
		{
			auto breakStmt = mAlloc->Alloc<BfBreakStatement>();
			ReplaceNode(tokenNode, breakStmt);
			breakStmt->mBreakNode = tokenNode;
			if (auto label = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
			{
				MEMBER_SET(breakStmt, mLabel, label);
				mVisitorPos.MoveNext();
			}
			else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			{
				if (tokenNode->GetToken() == BfToken_Mixin)
				{
					MEMBER_SET(breakStmt, mLabel, tokenNode);
					mVisitorPos.MoveNext();
				}
			}
			return breakStmt;
		}
		else if (token == BfToken_Continue)
		{
			auto continueStmt = mAlloc->Alloc<BfContinueStatement>();
			ReplaceNode(tokenNode, continueStmt);
			continueStmt->mContinueNode = tokenNode;
			if (auto label = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
			{
				MEMBER_SET(continueStmt, mLabel, label);
				mVisitorPos.MoveNext();
			}
			else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			{
				if (tokenNode->GetToken() == BfToken_Mixin)
				{
					MEMBER_SET(continueStmt, mLabel, tokenNode);
					mVisitorPos.MoveNext();
				}
			}
			return continueStmt;
		}
		else if (token == BfToken_Fallthrough)
		{
			auto fallthroughStmt = mAlloc->Alloc<BfFallthroughStatement>();
			ReplaceNode(tokenNode, fallthroughStmt);
			fallthroughStmt->mFallthroughToken = tokenNode;
			return fallthroughStmt;
		}
		else if (token == BfToken_For)
		{
			return CreateForStatement(node);
		}
		else if (token == BfToken_Using)
		{
			return CreateUsingStatement(node);
		}
		else if (token == BfToken_While)
		{
			return CreateWhileStatement(node);
		}
		else if (token == BfToken_Do)
		{
			auto checkNode = mVisitorPos.Get(mVisitorPos.mReadPos + 2);
			auto checkToken = BfNodeDynCast<BfTokenNode>(checkNode);
			if ((checkToken != NULL) && (checkToken->GetToken() == BfToken_While))
				return CreateRepeatStatement(node);
			else
				return CreateDoStatement(node);
		}
		else if (token == BfToken_Repeat)
		{
			return CreateRepeatStatement(node);
		}
		else if (token == BfToken_Return)
		{
			auto returnStmt = mAlloc->Alloc<BfReturnStatement>();
			ReplaceNode(node, returnStmt);
			MEMBER_SET(returnStmt, mReturnToken, tokenNode);

			auto nextNode = mVisitorPos.GetNext();
			if (!IsSemicolon(nextNode))
			{
				auto expr = CreateExpressionAfter(returnStmt);
				MEMBER_SET_CHECKED(returnStmt, mExpression, expr);
			}
			return returnStmt;
		}
		else if (token == BfToken_Delete)
		{
			auto deleteStmt = mAlloc->Alloc<BfDeleteStatement>();
			ReplaceNode(node, deleteStmt);
			MEMBER_SET(deleteStmt, mDeleteToken, tokenNode);

			auto nextNode = mVisitorPos.GetNext();
			if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
			{
				if (tokenNode->GetToken() == BfToken_Colon)
				{
					MEMBER_SET(deleteStmt, mTargetTypeToken, tokenNode);
					mVisitorPos.MoveNext();

					if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if (tokenNode->mToken == BfToken_Append)
						{
							MEMBER_SET(deleteStmt, mAllocExpr, tokenNode);
							mVisitorPos.MoveNext();
						}
					}

					if (deleteStmt->mAllocExpr == NULL)
					{
						auto allocExpr = CreateExpressionAfter(deleteStmt, (CreateExprFlags)(CreateExprFlags_NoCast | CreateExprFlags_ExitOnParenExpr));
						if (allocExpr != NULL)
						{
							MEMBER_SET(deleteStmt, mAllocExpr, allocExpr);
						}
					}
				}
			}

			nextNode = mVisitorPos.GetNext();
			if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
			{
				if (tokenNode->mToken == BfToken_LBracket)
				{
					mVisitorPos.MoveNext();
					auto attrib = CreateAttributeDirective(tokenNode);
					if (attrib == NULL)
						return deleteStmt;
					MEMBER_SET(deleteStmt, mAttributes, attrib);
				}
			}

			auto expr = CreateExpressionAfter(deleteStmt);
			MEMBER_SET_CHECKED(deleteStmt, mExpression, expr);
			return deleteStmt;
		}
		else if (token == BfToken_Throw)
		{
			auto throwStmt = mAlloc->Alloc<BfThrowStatement>();
			ReplaceNode(node, throwStmt);
			MEMBER_SET(throwStmt, mThrowToken, tokenNode);
			auto expr = CreateExpressionAfter(throwStmt);
			MEMBER_SET_CHECKED(throwStmt, mExpression, expr);
			return throwStmt;
		}
		else if (token == BfToken_If)
		{
			subCreateStmtFlags = (CreateStmtFlags)(createStmtFlags & (CreateStmtFlags_FindTrailingSemicolon));

			auto ifStmt = mAlloc->Alloc<BfIfStatement>();
			ReplaceNode(node, ifStmt);
			MEMBER_SET(ifStmt, mIfToken, tokenNode);

			tokenNode = ExpectTokenAfter(ifStmt, BfToken_LParen);
			MEMBER_SET_CHECKED(ifStmt, mOpenParen, tokenNode);

			auto condExpr = CreateExpressionAfter(ifStmt, CreateExprFlags_AllowVariableDecl);
			MEMBER_SET_CHECKED(ifStmt, mCondition, condExpr);

			tokenNode = ExpectTokenAfter(ifStmt, BfToken_RParen);
			MEMBER_SET_CHECKED(ifStmt, mCloseParen, tokenNode);

			auto trueStmt = CreateStatementAfter(ifStmt, subCreateStmtFlags);
			MEMBER_SET_CHECKED(ifStmt, mTrueStatement, trueStmt);

			auto nextNode = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Else))
			{
				MEMBER_SET(ifStmt, mElseToken, tokenNode);
				mVisitorPos.MoveNext();
				auto falseStmt = CreateStatementAfter(ifStmt, subCreateStmtFlags);
				MEMBER_SET_CHECKED(ifStmt, mFalseStatement, falseStmt);
			}

			return ifStmt;
		}
		else if (token == BfToken_Switch)
		{
			return CreateSwitchStatement(tokenNode);
		}
		else if (token == BfToken_Defer)
		{
			auto deferStmt = mAlloc->Alloc<BfDeferStatement>();
			ReplaceNode(tokenNode, deferStmt);
			deferStmt->mDeferToken = tokenNode;

			auto nextNode = mVisitorPos.GetNext();
			if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (nextTokenNode->GetToken() == BfToken_Colon)
				{
					MEMBER_SET(deferStmt, mColonToken, nextTokenNode);
					mVisitorPos.MoveNext();

					if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if ((nextToken->GetToken() == BfToken_Colon) || (nextToken->GetToken() == BfToken_Mixin))
						{
							MEMBER_SET(deferStmt, mScopeName, nextToken);
							mVisitorPos.MoveNext();
						}
					}
					else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
					{
						MEMBER_SET(deferStmt, mScopeName, identifier);
						mVisitorPos.MoveNext();
					}

					if (deferStmt->mScopeName == NULL)
					{
						FailAfter("Expected scope name", deferStmt);
					}
				}
				else if (nextTokenNode->GetToken() == BfToken_LParen)
				{
					mPassInstance->Warn(0, "Syntax deprecated", nextTokenNode);

					MEMBER_SET(deferStmt, mOpenParen, nextTokenNode);
					mVisitorPos.MoveNext();

					nextTokenNode = ExpectTokenAfter(deferStmt, BfToken_Scope, BfToken_Stack);
					MEMBER_SET_CHECKED(deferStmt, mScopeToken, nextTokenNode);

					nextTokenNode = ExpectTokenAfter(deferStmt, BfToken_RParen);
					MEMBER_SET_CHECKED(deferStmt, mCloseParen, nextTokenNode);
				}
			}

			if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			{
				if (nextTokenNode->GetToken() == BfToken_LBracket)
				{
					auto bindNode = mAlloc->Alloc<BfDeferBindNode>();
					ReplaceNode(nextTokenNode, bindNode);

					MEMBER_SET(bindNode, mOpenBracket, nextTokenNode);
					mVisitorPos.MoveNext();

					BfDeferredAstSizedArray<BfIdentifierNode*> params(bindNode->mParams, mAlloc);
					BfDeferredAstSizedArray<BfTokenNode*> commas(bindNode->mCommas, mAlloc);

					for (int paramIdx = 0; true; paramIdx++)
					{
						bool isRBracket = false;
						auto nextNode = mVisitorPos.GetNext();
						if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
							isRBracket = tokenNode->GetToken() == BfToken_RBracket;
						if (!isRBracket)
						{
							auto nameIdentifier = ExpectIdentifierAfter(bindNode, "parameter name");
							if (nameIdentifier == NULL)
								break;
							MoveNode(nameIdentifier, bindNode);
							params.push_back(nameIdentifier);
						}

						tokenNode = ExpectTokenAfter(bindNode, BfToken_Comma, BfToken_RBracket);
						if (tokenNode == NULL)
							return deferStmt;
						if (tokenNode->GetToken() == BfToken_RBracket)
						{
							MEMBER_SET(bindNode, mCloseBracket, tokenNode);
							break;
						}
						MoveNode(tokenNode, bindNode);
						commas.push_back(tokenNode);
					}

					MEMBER_SET(deferStmt, mBind, bindNode);
				}
			}

			BfAstNode* targetNode = CreateStatementAfter(deferStmt);
			if (targetNode != NULL)
			{
				BfAstNode* innerTarget = targetNode;

				if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(innerTarget))
					innerTarget = exprStmt->mExpression;

				if (deferStmt->mBind != NULL)
				{
					if (!innerTarget->IsA<BfBlock>())
					{
						Fail("Only blocks are allowed when defer binding is used", targetNode);
					}
				}
				else
				{
					if ((!innerTarget->IsA<BfInvocationExpression>()) &&
						(!innerTarget->IsA<BfBlock>()) &&
						(!innerTarget->IsA<BfDeleteStatement>()))
					{
						Fail("Only invocation expressions, statement blocks, or deletes are allowed", targetNode);
					}
				}

				MEMBER_SET(deferStmt, mTargetNode, targetNode);
			}

			return deferStmt;
		}
		else if (token == BfToken_Comma)
		{
			BfAstNode* prevVarDecl = mVisitorPos.Get(mVisitorPos.mWritePos - 1);
			if (mLastBlockNode != NULL)
				prevVarDecl = mLastBlockNode;

			if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(prevVarDecl))
			{
				continuingVariable = BfNodeDynCast<BfVariableDeclaration>(exprStmt->mExpression);
			}
		}
		else if ((token == BfToken_Const) || (token == BfToken_ReadOnly) || (token == BfToken_Static))
		{
			auto flags = (CreateStmtFlags)(CreateStmtFlags_FindTrailingSemicolon | CreateStmtFlags_ForceVariableDecl);
			if (token == BfToken_Static)
				flags = (CreateStmtFlags)(flags | CreateStmtFlags_AllowLocalFunction);
			auto stmt = CreateStatementAfter(tokenNode, flags);
			if (auto exprStmt = BfNodeDynCast<BfExpressionStatement>(stmt))
			{
				if (auto variableDecl = BfNodeDynCast<BfVariableDeclaration>(exprStmt->mExpression))
				{
					if (variableDecl->mModSpecifier != NULL)
					{
						Fail(StrFormat("'%s' already specified", BfTokenToString(variableDecl->mModSpecifier->GetToken())), variableDecl->mModSpecifier);
					}
					MEMBER_SET(variableDecl, mModSpecifier, tokenNode);
					exprStmt->SetSrcStart(tokenNode->mSrcStart);
					return stmt;
				}
			}
			if (auto localMethod = BfNodeDynCast<BfLocalMethodDeclaration>(stmt))
			{
				if (localMethod->mMethodDeclaration->mStaticSpecifier != NULL)
				{
					Fail(StrFormat("'%s' already specified", BfTokenToString(localMethod->mMethodDeclaration->mStaticSpecifier->GetToken())), localMethod->mMethodDeclaration->mStaticSpecifier);
				}
				MEMBER_SET(localMethod->mMethodDeclaration, mStaticSpecifier, tokenNode);
				localMethod->SetSrcStart(tokenNode->mSrcStart);
				return localMethod;
			}

			Fail(StrFormat("Unexpected '%s' specifier", BfTokenToString(tokenNode->GetToken())), tokenNode);
			return stmt;
		}
		else if (token == BfToken_Volatile)
		{
			Fail("Cannot create volatile local variables", tokenNode);
			return NULL;
		}
		else if (token == BfToken_Asm)
		{
			return CreateInlineAsmStatement(node);
		}
		else if (token == BfToken_Mixin)
		{
			auto methodDecl = mAlloc->Alloc<BfMethodDeclaration>();
			BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDecl->mParams, mAlloc);
			BfDeferredAstSizedArray<BfTokenNode*> commas(methodDecl->mCommas, mAlloc);
			ReplaceNode(tokenNode, methodDecl);
			methodDecl->mDocumentation = FindDocumentation(methodDecl);
			methodDecl->mMixinSpecifier = tokenNode;
			auto nameNode = ExpectIdentifierAfter(methodDecl);
			if (nameNode != NULL)
			{
				MEMBER_SET(methodDecl, mNameNode, nameNode);
				ParseMethod(methodDecl, &params, &commas, true);
			}

			auto localMethodDecl = mAlloc->Alloc<BfLocalMethodDeclaration>();
			ReplaceNode(methodDecl, localMethodDecl);
			localMethodDecl->mMethodDeclaration = methodDecl;

			return localMethodDecl;
		}
		else if (token == BfToken_LBracket)
		{
			return CreateAttributedStatement(tokenNode, createStmtFlags);
		}
	}

	if (auto identifier = BfNodeDynCast<BfIdentifierNode>(node))
	{
		node = CompactQualifiedName(identifier);
	}

	bool isLocalVariable = false;
	auto nextNode = mVisitorPos.GetNext();
	if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (nextToken->GetToken() == BfToken_Colon)
		{
			auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(node);
			if (nameIdentifier != NULL)
			{
				BfLabelNode* labelNode = mAlloc->Alloc<BfLabelNode>();
				ReplaceNode(nameIdentifier, labelNode);
				labelNode->mLabel = nameIdentifier;
				mVisitorPos.MoveNext();
				MEMBER_SET(labelNode, mColonToken, nextToken);

				BfAstNode* stmt = NULL;
				auto nextNode = mVisitorPos.GetNext();
				if (nextNode != NULL)
				{
					mVisitorPos.MoveNext();
					stmt = DoCreateStatement(nextNode);
					if (auto labelableStmt = BfNodeDynCast<BfLabelableStatement>(stmt))
					{
						MEMBER_SET(labelableStmt, mLabelNode, labelNode);
						return labelableStmt;
					}
					if (auto block = BfNodeDynCast<BfBlock>(stmt))
					{
						auto labeledBlock = mAlloc->Alloc<BfLabeledBlock>();
						ReplaceNode(block, labeledBlock);
						labeledBlock->mBlock = block;
						MEMBER_SET(labeledBlock, mLabelNode, labelNode);
						return labeledBlock;
					}
				}

				Fail("Label must appear before a labelable statement (if, for, do, while, switch, block)", labelNode);
				AddErrorNode(labelNode);
				return stmt;
			}
		}
	}

	int typeRefEndNode = -1;
	bool isTuple = false;
	if (IsTypeReference(node, BfToken_None, -1, &typeRefEndNode, NULL, NULL, &isTuple))
		isLocalVariable = true;

	if ((isLocalVariable) && (isTuple))
	{
		if (typeRefEndNode != -1)
		{
			// When we're typing something like "(a, )" ... don't make that a type ref yet because it could be "(a, b) = " where
			//  we're typing a tuple value rather than a tuple tpye ref
			auto checkNode = mVisitorPos.Get(typeRefEndNode);
			if (checkNode == NULL)
				isLocalVariable = false;
		}
	}

	if (nextNode == NULL)
	{
		// Treat ending identifier as just an identifier (could be block result)
		isLocalVariable = false;
	}

	if ((isLocalVariable) && (typeRefEndNode != -1) && (continuingVariable == NULL) && ((createStmtFlags & CreateStmtFlags_AllowLocalFunction) != 0))
	{
		BfTokenNode* nextToken = BfNodeDynCast<BfTokenNode>(node);
		if ((nextToken == NULL) ||
			((nextToken->GetToken() != BfToken_Delegate) && (nextToken->GetToken() != BfToken_Function)))
		{
			auto afterTypeRefNode = mVisitorPos.Get(typeRefEndNode);
			if (auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(afterTypeRefNode))
			{
				auto nextNextNode = mVisitorPos.Get(typeRefEndNode + 1);
				if (auto afterNameToken = BfNodeDynCast<BfTokenNode>(nextNextNode))
				{
					bool isLocalMethod = (afterNameToken->GetToken() == BfToken_LParen) || (afterNameToken->GetToken() == BfToken_LChevron);
					if (isLocalMethod)
					{
						int prevReadPos = mVisitorPos.mReadPos;
						mVisitorPos.mReadPos = typeRefEndNode;
						isLocalMethod = IsLocalMethod(nameIdentifier);
						mVisitorPos.mReadPos = prevReadPos;
					}

					if (isLocalMethod)
					{
						auto typeRef = CreateTypeRef(node);

						if (mVisitorPos.GetNext() != nameIdentifier)
							isLocalMethod = false;
						else
						{
							int prevReadPos = mVisitorPos.mReadPos;
							mVisitorPos.MoveNext();
							isLocalMethod = IsLocalMethod(nameIdentifier);
							if (!isLocalMethod)
								mVisitorPos.mReadPos = prevReadPos;
						}

						if (!isLocalMethod)
						{
							// TypeRef didn't match what we expected, just set it up as a variable declaration
							auto variableDeclaration = mAlloc->Alloc<BfVariableDeclaration>();
							ReplaceNode(typeRef, variableDeclaration);
							variableDeclaration->mTypeRef = typeRef;
							return variableDeclaration;
						}

						auto methodDecl = mAlloc->Alloc<BfMethodDeclaration>();
						ReplaceNode(typeRef, methodDecl);
						methodDecl->mDocumentation = FindDocumentation(methodDecl);
						methodDecl->mReturnType = typeRef;

						BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDecl->mParams, mAlloc);
						BfDeferredAstSizedArray<BfTokenNode*> commas(methodDecl->mCommas, mAlloc);
						MEMBER_SET(methodDecl, mNameNode, nameIdentifier);

						if (afterNameToken->GetToken() == BfToken_LChevron)
						{
							auto genericParams = CreateGenericParamsDeclaration(afterNameToken);
							if (genericParams != NULL)
							{
								MEMBER_SET(methodDecl, mGenericParams, genericParams);
							}
						}

						ParseMethod(methodDecl, &params, &commas, true);

						auto localMethodDecl = mAlloc->Alloc<BfLocalMethodDeclaration>();
						ReplaceNode(methodDecl, localMethodDecl);
						localMethodDecl->mMethodDeclaration = methodDecl;

						return localMethodDecl;
					}
				}
			}
			else if (afterTypeRefNode == NULL)
				isLocalVariable = false;
			else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(afterTypeRefNode))
			{
				if (tokenNode->mToken != BfToken_LParen)
					isLocalVariable = false; // May be tuple
			}
		}
	}

	if ((isLocalVariable) || (continuingVariable != NULL))
	{
		auto variableDeclaration = mAlloc->Alloc<BfVariableDeclaration>();
		BfTypeReference* typeRef = NULL;
		if (continuingVariable != NULL)
		{
			typeRef = continuingVariable->mTypeRef;
			variableDeclaration->mModSpecifier = continuingVariable->mModSpecifier;
			ReplaceNode(node, variableDeclaration);
			variableDeclaration->mPrecedingComma = (BfTokenNode*)node;
		}
		else
		{
			typeRef = CreateTypeRef(node);
			if (typeRef == NULL)
				return NULL;
			ReplaceNode(typeRef, variableDeclaration);
		}

		variableDeclaration->mTypeRef = typeRef;

		BfAstNode* variableNameNode = NULL;

		nextNode = mVisitorPos.GetNext();
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			if (tokenNode->GetToken() == BfToken_LParen)
			{
				mVisitorPos.mReadPos++;
				variableNameNode = CreateTupleExpression(tokenNode);
			}
		}
		if (variableNameNode == NULL)
		{
			auto checkToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2));
			if ((checkToken != NULL) && (checkToken->mToken == BfToken_Dot))
			{
				FailAfter("Expected variable name", variableDeclaration);
			}
			else
				variableNameNode = ExpectIdentifierAfter(variableDeclaration, "variable name");
		}
		if (variableNameNode == NULL)
			return variableDeclaration;

		bool isValidFinish = false;

		BfTokenNode* tokenNode;
		if ((tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext())))
		{
			int token = tokenNode->GetToken();
			if ((token == BfToken_AssignEquals) || (token == BfToken_Semicolon) || (token == BfToken_Comma))
				isValidFinish = true;
		}

		/*if (!isValidFinish)
		{
		if (auto nextIdentifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
		{
		if (auto nextNextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2)))
		{
		if ((nextNextToken->GetToken() == BfToken_LParen) || (nextNextToken->GetToken() == BfToken_LChevron))
		{
		// It's less destructive to not consume the identifier we see as the name, because it may not be.  This handles the case
		//  where we're typing some stuff before a local method declaration.  Kindof a corner case...
		Fail("Unexpected type", typeRef);
		mVisitorPos.mReadPos--;
		return variableDeclaration;
		}
		}
		}
		}*/

		// Is a local variable?
		tokenNode = ExpectTokenAfter(variableNameNode, BfToken_AssignEquals, BfToken_Semicolon, BfToken_Comma);

		variableDeclaration->mNameNode = variableNameNode;
		MoveNode(variableNameNode, variableDeclaration);

		if (tokenNode == NULL)
			return variableDeclaration;
		if (tokenNode->GetToken() == BfToken_AssignEquals)
		{
			MEMBER_SET(variableDeclaration, mEqualsNode, tokenNode);

			if (variableDeclaration->mInitializer == NULL)
				variableDeclaration->mInitializer = CreateExpressionAfter(variableDeclaration, (CreateExprFlags)(createStmtFlags & CreateStmtFlags_To_CreateExprFlags_Mask));
			if (variableDeclaration->mInitializer == NULL)
				return variableDeclaration;
			MoveNode(variableDeclaration->mInitializer, variableDeclaration);
		}
		else
			mVisitorPos.mReadPos--; // Backtrack to the semicolon or comma

		return variableDeclaration;
	}

	if ((createStmtFlags & CreateStmtFlags_ForceVariableDecl) != 0)
	{
		Fail("Expected local variable declaration", node);
		return NULL;
	}

	// Must be an expression.  Always set CreateExprFlags_NoCaseExpr, to keep ending statements in a switch case to look like case expressions

	CreateExprFlags exprFlags = (CreateExprFlags)(createStmtFlags & CreateStmtFlags_To_CreateExprFlags_Mask);
	if ((createStmtFlags & CreateStmtFlags_AllowUnterminatedExpression) == 0)
		exprFlags = (CreateExprFlags)(exprFlags | CreateExprFlags_NoCaseExpr);

	auto expr = CreateExpression(node, exprFlags);
	if (expr == NULL)
		return NULL;

	bool isOkUnary = false;
	if (auto unaryOperatorExpr = BfNodeDynCast<BfUnaryOperatorExpression>(expr))
	{
		isOkUnary =
			(unaryOperatorExpr->mOp == BfUnaryOp_Increment) ||
			(unaryOperatorExpr->mOp == BfUnaryOp_PostIncrement) ||
			(unaryOperatorExpr->mOp == BfUnaryOp_Decrement) ||
			(unaryOperatorExpr->mOp == BfUnaryOp_PostDecrement);

		if (unaryOperatorExpr->mOp == BfUnaryOp_Out)
		{
			unaryOperatorExpr->mOp = BfUnaryOp_Ref;
			Fail("Cannot use 'out' in this context", unaryOperatorExpr);
		}
	}

	if ((!mPrevStmtHadError) && (!mStmtHasError))
	{
		nextNode = mVisitorPos.GetNext();
		if (nextNode != NULL)
		{
			if (!isOkUnary)
				expr->VerifyIsStatement(mPassInstance);
		}
	}

	return expr;
}

// This is conservative - it doesn't HAVE to return true, but in some cases may cause a parsing error if the nodes
//  can be consumed as a statement rather than an expression.  We must be more careful about not returning 'true'
//  for something that can only be interpreted as a statement, however.
bool BfReducer::IsTerminatingExpression(BfAstNode* node)
{
	int parenDepth = 0;
	int chevronDepth = 0;
	int readIdx = mVisitorPos.mReadPos;
	bool prevWasValue = false;

	BfTokenNode* prevTokenNode = NULL;
	while (true)
	{
		auto node = mVisitorPos.Get(readIdx);
		if (node == NULL)
			break;

		auto tokenNode = BfNodeDynCast<BfTokenNode>(node);
		if (tokenNode != NULL)
		{
			switch (tokenNode->GetToken())
			{
			case BfToken_AssignEquals:
				if (parenDepth == 0)
					return false;
				break;
			case BfToken_LParen:
				chevronDepth = 0;
				parenDepth++;
				break;
			case BfToken_RParen:
				chevronDepth = 0;
				parenDepth--;
				break;
			case BfToken_LChevron:
				chevronDepth++;
				break;
			case BfToken_RChevron:
				// If we find a < and > that are not separated by parens, that's a generic, which must be a
				//  variable decl if it's not in parens
				if ((parenDepth == 0) && (chevronDepth > 0))
					return false;
				chevronDepth--;
				break;
			case BfToken_RDblChevron:
				chevronDepth--;
				break;

			case BfToken_Comma:
				if (parenDepth == 0)
					return false;
				break;

			case BfToken_As:
			case BfToken_AllocType:
			case BfToken_Append:
			case BfToken_Default:
			case BfToken_Is:
			case BfToken_Stack:
			case BfToken_Scope:
			case BfToken_New:
			case BfToken_RetType:
			case BfToken_Nullable:
			case BfToken_SizeOf:
			case BfToken_This:
			case BfToken_TypeOf:
			case BfToken_LessEquals:
			case BfToken_GreaterEquals:
			case BfToken_LBracket:
			case BfToken_RBracket:
			case BfToken_Colon:
			case BfToken_Dot:
			case BfToken_DotDot:
			case BfToken_QuestionDot:
			case BfToken_QuestionLBracket:
			case BfToken_Plus:
			case BfToken_Minus:
			case BfToken_DblPlus:
			case BfToken_DblMinus:
			case BfToken_Star:
			case BfToken_ForwardSlash:
			case BfToken_Modulus:
			case BfToken_Ampersand:
			case BfToken_At:
			case BfToken_DblAmpersand:
			case BfToken_Bar:
			case BfToken_DblBar:
			case BfToken_Bang:
			case BfToken_Carat:
			case BfToken_Tilde:
			case BfToken_Question:
			case BfToken_DblQuestion:
			case BfToken_Arrow:
			case BfToken_FatArrow:
				// Allow these
				break;

			case BfToken_Semicolon:
				return false;
			default:
				// Disallow everything else
				return false;
			}
		}

		if ((node->IsExact<BfIdentifierNode>()) ||
			(node->IsExact<BfQualifiedNameNode>()) ||
			(node->IsExact<BfMemberReferenceExpression>()) ||
			(node->IsExact<BfLiteralExpression>()))
		{
			if (prevWasValue)
			{
				// Two values in a row cannot be a valid expression
				return false;
			}
			prevWasValue = true;
		}
		else
		{
			prevWasValue = false;
		}

		if (auto block = BfNodeDynCastExact<BfBlock>(node))
		{
			// Local method decl
			if ((parenDepth == 0) && (prevTokenNode != NULL) && (prevTokenNode->GetToken() == BfToken_RParen))
				return false;
		}

		prevTokenNode = tokenNode;
		readIdx++;
	}

	int outEndNode = 0;
	bool couldBeExpr = false;
	if (IsTypeReference(node, BfToken_None, -1, &outEndNode, &couldBeExpr))
	{
		if (outEndNode == mVisitorPos.mTotalSize - 1)
		{
			if (auto name = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.Get(outEndNode)))
			{
				auto beforeNameToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(outEndNode - 1));

				// We treat "a*b" as a multiply rather than a variable declaration
				if ((beforeNameToken == NULL) || (beforeNameToken->GetToken() != BfToken_Star))
				{
					return false;
				}
			}
		}
	}

	return true;
}

BfAstNode* BfReducer::CreateStatement(BfAstNode* node, CreateStmtFlags createStmtFlags)
{
	AssertCurrentNode(node);

	if ((createStmtFlags & CreateStmtFlags_AllowUnterminatedExpression) != 0)
	{
		if (IsTerminatingExpression(node))
		{
			mPrevStmtHadError = false;

			// Must be an expression.  Always set CreateExprFlags_NoCaseExpr, to keep ending statements in a switch case to look like case expressions
			auto expr = CreateExpression(node, (CreateExprFlags)((createStmtFlags & CreateStmtFlags_To_CreateExprFlags_Mask) | CreateExprFlags_NoCaseExpr));
			if (expr != NULL)
			{
				auto nextNode = mVisitorPos.GetNext();
				if (nextNode != NULL)
					FailAfter("Semicolon expected", expr);
			}
			return expr;
		}
	}

	mStmtHasError = false;
	auto stmtNode = DoCreateStatement(node, createStmtFlags);
	mPrevStmtHadError = mStmtHasError;
	if (stmtNode == NULL)
		return NULL;

	auto origStmtNode = stmtNode;
	if (stmtNode->IsA<BfBlock>())
		return stmtNode;

	auto nextNode = mVisitorPos.GetNext();
	if (auto expr = BfNodeDynCast<BfExpression>(stmtNode))
	{
		if (((createStmtFlags & CreateStmtFlags_AllowUnterminatedExpression) != 0) && (nextNode == NULL))
			return expr;

		auto stmt = mAlloc->Alloc<BfExpressionStatement>();
		ReplaceNode(expr, stmt);
		stmt->mExpression = expr;
		stmtNode = stmt;
	}

	if (auto stmt = BfNodeDynCast<BfStatement>(stmtNode))
	{
		if ((stmt->IsMissingSemicolon()) && ((createStmtFlags & CreateStmtFlags_FindTrailingSemicolon) != 0) && (!stmt->IsA<BfEmptyStatement>()))
		{
			if (!IsSemicolon(nextNode))
			{
				bool doWarn = false;

				// Why did we have this BfIdentifierNode check? It failed to throw an error on just things like "{ a }"
				if (origStmtNode->IsA<BfRepeatStatement>())
				{
					// These do require a semicolon
					doWarn = true;
				}
				else if (/*(origStmtNode->IsA<BfIdentifierNode>()) || */(origStmtNode->IsA<BfCompoundStatement>()) || (origStmtNode->IsA<BfBlock>()))
					return stmt;
				if (origStmtNode->IsA<BfVariableDeclaration>())
				{
					// For compound variables
					auto commaToken = BfNodeDynCast<BfTokenNode>(nextNode);
					if ((commaToken != NULL) && (commaToken->GetToken() == BfToken_Comma))
						return stmt;
				}

				if (((createStmtFlags & CreateStmtFlags_AllowUnterminatedExpression) != 0) && (origStmtNode->IsA<BfExpression>()) && (nextNode == NULL))
					return stmt;

				BfError* error;
				if (doWarn)
					error = mPassInstance->WarnAfterAt(0, "Semicolon expected", node->GetSourceData(), stmt->GetSrcEnd() - 1);
				else
					error = mPassInstance->FailAfterAt("Semicolon expected", node->GetSourceData(), stmt->GetSrcEnd() - 1);

				if ((error != NULL) && (mSource != NULL))
					error->mProject = mSource->mProject;
				mPrevStmtHadError = true;
				return stmt;
			}

			if ((!stmt->IsA<BfBlock>()) && (!stmt->IsA<BfIdentifierNode>()))
			{
				mVisitorPos.MoveNext();
				MEMBER_SET(stmt, mTrailingSemicolon, (BfTokenNode*)nextNode);
			}
		}
	}

	return stmtNode;
}

BfAstNode* BfReducer::CreateStatementAfter(BfAstNode* node, CreateStmtFlags createStmtFlags)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	if (nextNode == NULL)
	{
		FailAfter("Expected statement", node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	BfAstNode* stmt = CreateStatement(nextNode, createStmtFlags);
	if (stmt == NULL)
	{
		// Nope, didn't handle it
		mVisitorPos.mReadPos--;
	}
	return stmt;
}

bool BfReducer::IsExtendedTypeName(BfIdentifierNode* identifierNode)
{
	auto nextNode = mVisitorPos.GetNext();
	if (nextNode == NULL)
		return false;
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
	if (tokenNode == NULL)
		return false;
	int token = tokenNode->GetToken();
	return ((token == BfToken_Star) ||
		(token == BfToken_Question) ||
		(token == BfToken_Dot) ||
		(token == BfToken_LChevron) ||
		(token == BfToken_LBracket));
}

static String TypeToString(BfTypeReference* typeRef)
{
	if (typeRef == NULL)
		return "null";
	if (auto qualifiedTypeRef = BfNodeDynCast<BfQualifiedTypeReference>(typeRef))
		return "(" + TypeToString(qualifiedTypeRef->mLeft) + " . " + TypeToString(qualifiedTypeRef->mRight) + ")";
	if (auto genericTypeInstanceRef = BfNodeDynCast<BfGenericInstanceTypeRef>(typeRef))
		return TypeToString(genericTypeInstanceRef->mElementType) + "<...>";
	return typeRef->ToString();
}

BfTypeReference* BfReducer::DoCreateNamedTypeRef(BfIdentifierNode* identifierNode)
{
	if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(identifierNode))
	{
		auto qualifiedTypeRef = mAlloc->Alloc<BfQualifiedTypeReference>();
		ReplaceNode(identifierNode, qualifiedTypeRef);

		MoveNode(qualifiedNameNode->mLeft, qualifiedTypeRef);
		auto leftTypeRef = DoCreateNamedTypeRef(qualifiedNameNode->mLeft);
		MEMBER_SET(qualifiedTypeRef, mLeft, leftTypeRef);
		MEMBER_SET(qualifiedTypeRef, mDot, qualifiedNameNode->mDot);
		MoveNode(qualifiedNameNode->mRight, qualifiedNameNode);
		auto rightTypeRef = DoCreateNamedTypeRef(qualifiedNameNode->mRight);
		MEMBER_SET(qualifiedTypeRef, mRight, rightTypeRef);

		return qualifiedTypeRef;
	}
	else
	{
		auto namedTypeRef = mAlloc->Alloc<BfNamedTypeReference>();
		namedTypeRef->mNameNode = identifierNode;
		ReplaceNode(identifierNode, namedTypeRef);
		namedTypeRef->SetTriviaStart(identifierNode->GetTriviaStart());
		return namedTypeRef;
	}
}

BfTypeReference* BfReducer::DoCreateTypeRef(BfAstNode* firstNode, CreateTypeRefFlags createTypeRefFlags, int endNode)
{
	AssertCurrentNode(firstNode);

	bool parseArrayBracket = (createTypeRefFlags & CreateTypeRefFlags_NoParseArrayBrackets) == 0;

	auto identifierNode = BfNodeDynCast<BfIdentifierNode>(firstNode);
	if (identifierNode == NULL)
	{
		if (auto memberReferenceExpression = BfNodeDynCast<BfMemberReferenceExpression>(firstNode))
		{
			SetAndRestoreValue<bool> prevSkipCurrentNodeAssert(mSkipCurrentNodeAssert, true);

			auto qualifiedTypeRef = mAlloc->Alloc<BfQualifiedTypeReference>();
			ReplaceNode(firstNode, qualifiedTypeRef);
			BF_ASSERT(memberReferenceExpression->mTarget != NULL);
			if (memberReferenceExpression->mTarget != NULL)
			{
				MoveNode(memberReferenceExpression->mTarget, qualifiedTypeRef);
				auto leftTypeRef = DoCreateTypeRef(memberReferenceExpression->mTarget);
				MEMBER_SET(qualifiedTypeRef, mLeft, leftTypeRef);
			}
			MEMBER_SET(qualifiedTypeRef, mDot, memberReferenceExpression->mDotToken);
			if (memberReferenceExpression->mDotToken->mToken == BfToken_DotDot)
				Fail("Invalid use of '..' in type reference", memberReferenceExpression->mDotToken);
			if (memberReferenceExpression->mMemberName != NULL)
			{
				MoveNode(memberReferenceExpression->mMemberName, memberReferenceExpression);
				auto rightTypeRef = DoCreateTypeRef(memberReferenceExpression->mMemberName);
				MEMBER_SET(qualifiedTypeRef, mRight, rightTypeRef);
			}
			firstNode = qualifiedTypeRef;
		}
		else
		{
			bool isHandled = false;
			auto tokenNode = BfNodeDynCast<BfTokenNode>(firstNode);
			if (tokenNode != NULL)
			{
				BfToken token = tokenNode->GetToken();
				if (token == BfToken_Dot)
				{
					auto dotTypeRef = mAlloc->Alloc<BfDotTypeReference>();
					ReplaceNode(firstNode, dotTypeRef);
					dotTypeRef->mDotToken = tokenNode;
					firstNode = dotTypeRef;
					isHandled = true;
				}
				else if (token == BfToken_DotDotDot)
				{
					auto dotTypeRef = mAlloc->Alloc<BfDotTypeReference>();
					ReplaceNode(firstNode, dotTypeRef);
					dotTypeRef->mDotToken = tokenNode;
					firstNode = dotTypeRef;
					isHandled = true;
					return dotTypeRef;
				}
				else if ((token == BfToken_Star) && (mAllowTypeWildcard))
				{
					auto wildcardTypeRef = mAlloc->Alloc<BfWildcardTypeReference>();
					ReplaceNode(firstNode, wildcardTypeRef);
					wildcardTypeRef->mWildcardToken = tokenNode;
					return wildcardTypeRef;
				}
				else if ((token == BfToken_Var) || (token == BfToken_Let))
				{
					if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
					{
						if ((nextToken->GetToken() == BfToken_Ref) || (nextToken->GetToken() == BfToken_Mut))
						{
							auto varTypeRef = mAlloc->Alloc<BfVarRefTypeReference>();
							ReplaceNode(firstNode, varTypeRef);
							varTypeRef->mVarToken = tokenNode;
							MEMBER_SET(varTypeRef, mRefToken, nextToken);
							mVisitorPos.MoveNext();
							return varTypeRef;
						}
					}

					if (token == BfToken_Var)
					{
						auto varTypeRef = mAlloc->Alloc<BfVarTypeReference>();
						ReplaceNode(firstNode, varTypeRef);
						varTypeRef->mVarToken = tokenNode;
						return varTypeRef;
					}
					else
					{
						auto letTypeRef = mAlloc->Alloc<BfLetTypeReference>();
						ReplaceNode(firstNode, letTypeRef);
						letTypeRef->mLetToken = tokenNode;
						return letTypeRef;
					}
				}
				else if ((mCompatMode) && (token == BfToken_Minus))
				{
					auto constExpr = CreateExpression(tokenNode, CreateExprFlags_BreakOnRChevron);
					auto constTypeRef = mAlloc->Alloc<BfConstExprTypeRef>();
					ReplaceNode(firstNode, constTypeRef);
					MEMBER_SET_CHECKED(constTypeRef, mConstExpr, constExpr);
					return constTypeRef;
				}
				else if (token == BfToken_Const)
				{
					if (!mCompatMode)
					{
						//Fail("Invalid use of 'const', only fields and local variables can be declared as const", tokenNode);
						//AddErrorNode(tokenNode);

						auto constExpr = CreateExpressionAfter(tokenNode, CreateExprFlags_BreakOnRChevron);
						auto constTypeRef = mAlloc->Alloc<BfConstExprTypeRef>();
						ReplaceNode(firstNode, constTypeRef);
						MEMBER_SET(constTypeRef, mConstToken, tokenNode);
						MEMBER_SET_CHECKED(constTypeRef, mConstExpr, constExpr);
						return constTypeRef;
					}
					else
					{
						auto elementType = CreateTypeRefAfter(tokenNode, createTypeRefFlags);
						auto constTypeRef = mAlloc->Alloc<BfConstTypeRef>();
						ReplaceNode(firstNode, constTypeRef);
						MEMBER_SET(constTypeRef, mConstToken, tokenNode);
						MEMBER_SET_CHECKED(constTypeRef, mElementType, elementType);
						return constTypeRef;
					}
				}
				else if (token == BfToken_Unsigned)
				{
					BF_ASSERT(mCompatMode);
					auto elementType = CreateTypeRefAfter(tokenNode, createTypeRefFlags);

					BfTypeReference* rootElementParent = NULL;
					auto rootElement = elementType;
					while (auto elementedType = BfNodeDynCast<BfElementedTypeRef>(rootElement))
					{
						rootElementParent = rootElement;
						rootElement = elementedType->mElementType;
					}

					auto unsignedTypeRef = mAlloc->Alloc<BfUnsignedTypeRef>();
					ReplaceNode(firstNode, unsignedTypeRef);
					MEMBER_SET(unsignedTypeRef, mUnsignedToken, tokenNode);
					if (rootElement == elementType)
					{
						MEMBER_SET_CHECKED(unsignedTypeRef, mElementType, elementType);
						return unsignedTypeRef;
					}
					else
					{
#ifdef BF_AST_HAS_PARENT_MEMBER
						BF_ASSERT(rootElementParent == rootElement->mParent);
#endif
						auto elementedType = BfNodeDynCast<BfElementedTypeRef>(rootElementParent);
						MEMBER_SET_CHECKED(unsignedTypeRef, mElementType, rootElement);
						MEMBER_SET_CHECKED(elementedType, mElementType, unsignedTypeRef);
						elementType->SetSrcStart(unsignedTypeRef->GetSrcStart());
						return elementType;
					}
				}
				else if ((token == BfToken_AllocType) || (token == BfToken_Nullable) || (token == BfToken_RetType))
				{
					auto retTypeTypeRef = mAlloc->Alloc<BfModifiedTypeRef>();
					ReplaceNode(firstNode, retTypeTypeRef);
					MEMBER_SET(retTypeTypeRef, mRetTypeToken, tokenNode);

					tokenNode = ExpectTokenAfter(retTypeTypeRef, BfToken_LParen);
					MEMBER_SET_CHECKED(retTypeTypeRef, mOpenParen, tokenNode);

					auto elementType = CreateTypeRefAfter(retTypeTypeRef, createTypeRefFlags);
					MEMBER_SET_CHECKED(retTypeTypeRef, mElementType, elementType);

					tokenNode = ExpectTokenAfter(retTypeTypeRef, BfToken_RParen);
					MEMBER_SET_CHECKED(retTypeTypeRef, mCloseParen, tokenNode);
					return retTypeTypeRef;
				}
				else if ((token == BfToken_Delegate) || (token == BfToken_Function))
				{
					auto delegateTypeRef = mAlloc->Alloc<BfDelegateTypeRef>();
					ReplaceNode(firstNode, delegateTypeRef);
					MEMBER_SET(delegateTypeRef, mTypeToken, tokenNode);

					auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
					if ((nextToken != NULL) && (nextToken->mToken == BfToken_LBracket))
					{
						mVisitorPos.MoveNext();
						auto attribs = CreateAttributeDirective(nextToken);
						MEMBER_SET_CHECKED(delegateTypeRef, mAttributes, attribs);
					}

					auto returnType = CreateTypeRefAfter(delegateTypeRef);
					MEMBER_SET_CHECKED(delegateTypeRef, mReturnType, returnType);

					tokenNode = ExpectTokenAfter(delegateTypeRef, BfToken_LParen);
					MEMBER_SET_CHECKED(delegateTypeRef, mOpenParen, tokenNode);

					BfDeferredAstSizedArray<BfParameterDeclaration*> params(delegateTypeRef->mParams, mAlloc);
					BfDeferredAstSizedArray<BfTokenNode*> commas(delegateTypeRef->mCommas, mAlloc);
					auto closeNode = ParseMethodParams(delegateTypeRef, &params, &commas, BfToken_RParen, false);
					if (closeNode == NULL)
					{
						if (!params.empty())
							delegateTypeRef->AdjustSrcEnd(params.back());
						if (!commas.empty())
							delegateTypeRef->AdjustSrcEnd(commas.back());
					}
					MEMBER_SET_CHECKED(delegateTypeRef, mCloseParen, closeNode);
					mVisitorPos.MoveNext();

					for (auto paramDecl : params)
					{
						if ((paramDecl != NULL) && (paramDecl->mEqualsNode != NULL))
							Fail(StrFormat("Initializers cannot be used in anonymous %s type references. Consider creating a named %s type.", BfTokenToString(token), BfTokenToString(token)), paramDecl->mEqualsNode);
					}

					isHandled = true;
					firstNode = delegateTypeRef;

					if ((createTypeRefFlags & CreateTypeRefFlags_EarlyExit) != 0)
						return delegateTypeRef;
				}
				else if ((token == BfToken_Comptype) || (token == BfToken_Decltype))
				{
					auto declTypeRef = mAlloc->Alloc<BfExprModTypeRef>();
					ReplaceNode(tokenNode, declTypeRef);
					declTypeRef->mToken = tokenNode;
					tokenNode = ExpectTokenAfter(declTypeRef, BfToken_LParen);
					MEMBER_SET_CHECKED(declTypeRef, mOpenParen, tokenNode);
					auto targetExpr = CreateExpressionAfter(declTypeRef);
					MEMBER_SET_CHECKED(declTypeRef, mTarget, targetExpr);
					tokenNode = ExpectTokenAfter(declTypeRef, BfToken_RParen);
					MEMBER_SET_CHECKED(declTypeRef, mCloseParen, tokenNode);

					isHandled = true;
					firstNode = declTypeRef;

					if ((createTypeRefFlags & CreateTypeRefFlags_EarlyExit) != 0)
						return declTypeRef;
				}
				else if (token == BfToken_LParen)
				{
					auto tupleTypeRef = mAlloc->Alloc<BfTupleTypeRef>();
					BfDeferredAstSizedArray<BfTypeReference*> fieldTypes(tupleTypeRef->mFieldTypes, mAlloc);
					BfDeferredAstSizedArray<BfIdentifierNode*> fieldNames(tupleTypeRef->mFieldNames, mAlloc);
					BfDeferredAstSizedArray<BfAstNode*> commas(tupleTypeRef->mCommas, mAlloc);
					ReplaceNode(firstNode, tupleTypeRef);
					tupleTypeRef->mOpenParen = tokenNode;

					while (true)
					{
						auto tupleFieldType = CreateTypeRefAfter(tupleTypeRef);
						if (tupleFieldType == NULL)
							return tupleTypeRef;

						fieldTypes.push_back(tupleFieldType);
						MoveNode(tupleFieldType, tupleTypeRef);

						auto nextNode = mVisitorPos.GetNext();
						if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(nextNode))
						{
							while (fieldNames.size() < fieldTypes.size() - 1)
								fieldNames.push_back(NULL);
							MoveNode(identifierNode, tupleTypeRef);
							fieldNames.push_back(identifierNode);
							mVisitorPos.MoveNext();
						}

						auto tokenNode = ExpectTokenAfter(tupleTypeRef, BfToken_Comma, BfToken_RParen);
						if (tokenNode == NULL)
							return tupleTypeRef;

						if (tokenNode->GetToken() == BfToken_RParen)
						{
							if ((fieldTypes.size() == 1) && ((createTypeRefFlags & CreateTypeRefFlags_AllowSingleMemberTuple) == 0))
							{
								Fail("Tuple types must contain more than one member", tokenNode);
							}

							MEMBER_SET(tupleTypeRef, mCloseParen, tokenNode);
							//return tupleTypeRef;
							firstNode = tupleTypeRef;
							isHandled = true;
							break;
						}

						MoveNode(tokenNode, tupleTypeRef);
						commas.push_back(tokenNode);
					}
				}
			}
			else if (mCompatMode)
			{
				if (auto literalExpr = BfNodeDynCast<BfLiteralExpression>(firstNode))
				{
					auto constExpr = CreateExpression(literalExpr, CreateExprFlags_BreakOnRChevron);
					auto constTypeRef = mAlloc->Alloc<BfConstExprTypeRef>();
					ReplaceNode(firstNode, constTypeRef);
					MEMBER_SET_CHECKED(constTypeRef, mConstExpr, constExpr);
					return constTypeRef;
				}
			}

			if (!isHandled)
			{
				Fail("Expected type", firstNode);
				return NULL;
			}
		}
	}

	BfTypeReference* typeRef = BfNodeDynCast<BfTypeReference>(firstNode);
	if (typeRef == NULL)
	{
		typeRef = DoCreateNamedTypeRef(identifierNode);
	}

	while (true)
	{
		if ((endNode != -1) && (mVisitorPos.mReadPos + 1 >= endNode))
			break;

		auto nextNode = mVisitorPos.GetNext();
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode != NULL)
		{
			BfToken token = tokenNode->GetToken();
			if (token == BfToken_Dot)
			{
				BfQualifiedTypeReference* qualifiedTypeRef = mAlloc->Alloc<BfQualifiedTypeReference>();
				ReplaceNode(typeRef, qualifiedTypeRef);
				qualifiedTypeRef->mLeft = typeRef;
				MEMBER_SET(qualifiedTypeRef, mDot, tokenNode);
				mVisitorPos.MoveNext();

				while (true)
				{
					bool handled = false;

					if (mAllowTypeWildcard)
					{
						auto nextNode = mVisitorPos.GetNext();
						if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
						{
							if (nextTokenNode->mToken == BfToken_Star)
							{
								auto wildcardTypeRef = mAlloc->Alloc<BfWildcardTypeReference>();
								ReplaceNode(nextTokenNode, wildcardTypeRef);
								wildcardTypeRef->mWildcardToken = nextTokenNode;
								typeRef = wildcardTypeRef;
								handled = true;
								mVisitorPos.MoveNext();
							}
						}
					}

					if (!handled)
					{
						auto rightIdentifer = ExpectIdentifierAfter(qualifiedTypeRef);
						if (rightIdentifer == NULL)
							return qualifiedTypeRef;

						auto namedTypeRef = mAlloc->Alloc<BfNamedTypeReference>();
						namedTypeRef->mNameNode = rightIdentifer;
						ReplaceNode(rightIdentifer, namedTypeRef);
						namedTypeRef->SetTriviaStart(rightIdentifer->GetTriviaStart());
						typeRef = namedTypeRef;
					}

					MEMBER_SET(qualifiedTypeRef, mRight, typeRef);

					nextNode = mVisitorPos.GetNext();
					if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
					{
						if (tokenNode->GetToken() == BfToken_Dot)
						{
							BfQualifiedTypeReference* outerQualifiedTypeRef = mAlloc->Alloc<BfQualifiedTypeReference>();
							ReplaceNode(qualifiedTypeRef, outerQualifiedTypeRef);
							outerQualifiedTypeRef->mLeft = qualifiedTypeRef;
							MEMBER_SET(outerQualifiedTypeRef, mDot, tokenNode);
							qualifiedTypeRef = outerQualifiedTypeRef;
							mVisitorPos.MoveNext();
						}
						else
							break;
					}
					else
					{
						break;
					}
				}

				typeRef = qualifiedTypeRef;
			}
			else if (token == BfToken_Star)
			{
				auto ptrType = mAlloc->Alloc<BfPointerTypeRef>();
				ReplaceNode(typeRef, ptrType);
				ptrType->mElementType = typeRef;
				MEMBER_SET(ptrType, mStarNode, tokenNode);
				typeRef = ptrType;
				mVisitorPos.MoveNext();
			}
			else if ((token == BfToken_Question) || (token == BfToken_QuestionLBracket))
			{
				if (token == BfToken_QuestionLBracket)
					tokenNode = BreakQuestionLBracket(tokenNode);
				else
					mVisitorPos.MoveNext();

				auto nullableType = mAlloc->Alloc<BfNullableTypeRef>();
				ReplaceNode(typeRef, nullableType);
				nullableType->mElementType = typeRef;
				MEMBER_SET(nullableType, mQuestionToken, tokenNode);
				typeRef = nullableType;
			}
			else if (token == BfToken_LBracket)
			{
				if (!parseArrayBracket)
					return typeRef;

				auto arrayType = mAlloc->Alloc<BfArrayTypeRef>();
				auto newArrayType = arrayType;
				ReplaceNode(typeRef, arrayType);
				arrayType->mOpenBracket = tokenNode;
				MoveNode(tokenNode, arrayType);
				arrayType->mDimensions = 1;
				arrayType->mElementType = typeRef;
				mVisitorPos.MoveNext();

				while (true)
				{
					auto prevArrayType = BfNodeDynCast<BfArrayTypeRef>(arrayType->mElementType);
					if (prevArrayType == NULL)
						break;

					std::swap(prevArrayType->mOpenBracket, arrayType->mOpenBracket);
					std::swap(prevArrayType->mParams, arrayType->mParams);
					std::swap(prevArrayType->mCloseBracket, arrayType->mCloseBracket);
					std::swap(prevArrayType->mDimensions, arrayType->mDimensions);
					prevArrayType->SetSrcEnd(arrayType->GetSrcEnd());

					arrayType = prevArrayType;
				}

				BF_ASSERT(arrayType->mParams.mVals == NULL);
				arrayType->mParams.mSize = 0;
				BfDeferredAstSizedArray<BfAstNode*> params(arrayType->mParams, mAlloc);

				bool hasFailed = false;

				bool isSized = false;
				while (true)
				{
					nextNode = mVisitorPos.GetNext();
					tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
					if (tokenNode != NULL)
					{
						if (tokenNode->GetToken() == BfToken_Comma)
						{
							MoveNode(tokenNode, arrayType);
							mVisitorPos.MoveNext();
							arrayType->mDimensions++;
							params.push_back(tokenNode);
						}
						else if (tokenNode->GetToken() == BfToken_RBracket)
						{
							MoveNode(tokenNode, arrayType);
							mVisitorPos.MoveNext();
							arrayType->mCloseBracket = tokenNode;
							break;
						}
						else
							tokenNode = NULL;
					}

					if (tokenNode == NULL)
					{
						if ((!params.IsEmpty()) && (!BfNodeIsExact<BfTokenNode>(params.back())))
						{
							FailAfter("Expected ','", params.back());
							hasFailed = true;
							break;
						}

						BfExpression* sizeExpr = CreateExpressionAfter(arrayType);
						if (sizeExpr == NULL)
						{
							hasFailed = true;
							break;
						}

						MoveNode(sizeExpr, arrayType);
						params.push_back(sizeExpr);
					}
				}

				newArrayType->SetSrcEnd(arrayType->GetSrcEnd());

				if (hasFailed)
					return newArrayType;

				typeRef = newArrayType;
			}
			else if (token == BfToken_LChevron)
			{
				if (auto elementGeneric = BfNodeDynCastExact<BfGenericInstanceTypeRef>(typeRef))
				{
					// Already a generic
					return typeRef;
				}

				auto genericInstance = mAlloc->Alloc<BfGenericInstanceTypeRef>();
				BfDeferredSizedArray<BfAstNode*> genericArguments(genericInstance->mGenericArguments, mAlloc);
				BfDeferredAstSizedArray<BfAstNode*> commas(genericInstance->mCommas, mAlloc);
				ReplaceNode(typeRef, genericInstance);
				genericInstance->mOpenChevron = tokenNode;
				MoveNode(tokenNode, genericInstance);
				genericInstance->mElementType = typeRef;
				mVisitorPos.MoveNext();

				bool isBoundName = false;
				bool isUnboundName = false;

				while (true)
				{
					auto nextNode = mVisitorPos.GetNext();
					auto genericIdentifier = BfNodeDynCast<BfIdentifierNode>(nextNode);
					bool doAddType = genericIdentifier != NULL;
					bool addAsExpr = false;

					//if (mCompatMode)
					{
						if (BfNodeDynCast<BfLiteralExpression>(nextNode) != NULL)
						{
							doAddType = true;
							addAsExpr = true;
						}
					}
					if (genericIdentifier == NULL)
					{
						auto nextNode = mVisitorPos.GetNext();
						tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
						BfToken token = BfToken_None;
						if (tokenNode != NULL)
							token = tokenNode->GetToken();
						if ((tokenNode != NULL) &&
							((token == BfToken_Const) ||
							(token == BfToken_Ref) ||
								(token == BfToken_Mut) ||
								(token == BfToken_LParen) ||
								(token == BfToken_Delegate) ||
								(token == BfToken_Function) ||
								(token == BfToken_Comptype) ||
								(token == BfToken_Decltype) ||
								((token == BfToken_Star) && (mAllowTypeWildcard))))
							doAddType = true;
					}

					if ((!doAddType) && (isBoundName))
					{
						FailAfter("Expected type", genericInstance);
					}
					if ((doAddType) && (!isUnboundName))
					{
						BfAstNode* genericArgumentTypeRef = NULL;

						if (addAsExpr)
						{
							genericArgumentTypeRef = CreateExpressionAfter(genericInstance, CreateExprFlags_BreakOnRChevron);
						}
						else
							genericArgumentTypeRef = CreateTypeRefAfter(genericInstance);
						if (genericArgumentTypeRef == NULL)
							return NULL;
						MoveNode(genericArgumentTypeRef, genericInstance);
						genericArguments.push_back(genericArgumentTypeRef);
						isBoundName = true;
					}
					else
						isUnboundName = true;

					nextNode = mVisitorPos.GetNext();
					tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
					if (tokenNode == NULL)
					{
						FailAfter("Expected ',' or '>'", genericInstance);
						return genericInstance;
					}

					token = tokenNode->GetToken();
					if (token == BfToken_RDblChevron)
					{
						tokenNode = BreakDoubleChevron(tokenNode);
						token = tokenNode->GetToken();
					}
					else
					{
						mVisitorPos.MoveNext();
					}

					if (token == BfToken_RChevron)
					{
						MoveNode(tokenNode, genericInstance);
						genericInstance->mCloseChevron = tokenNode;
						break;
					}
					if (token != BfToken_Comma)
					{
						Fail("Either ',' or '>' expected", tokenNode);
						mVisitorPos.mReadPos--;
						//AddErrorNode(tokenNode);
						return genericInstance;
					}
					MoveNode(tokenNode, genericInstance);
					commas.push_back(tokenNode);
				}

				typeRef = genericInstance;
			}
			else
				break;
		}
		else
			break;
	}

	return typeRef;
}

BfTypeReference* BfReducer::CreateTypeRef(BfAstNode* firstNode, CreateTypeRefFlags createTypeRefFlags)
{
	int endNode = -1;
	if ((createTypeRefFlags & CreateTypeRefFlags_SafeGenericParse) != 0)
	{
		createTypeRefFlags = (CreateTypeRefFlags)(createTypeRefFlags & ~CreateTypeRefFlags_SafeGenericParse);
		int outEndNode = -1;
		int retryNode = -1;
		bool isTypeRef = IsTypeReference(firstNode, BfToken_None, -1, &retryNode, &outEndNode, NULL, NULL, NULL);
		if ((!isTypeRef) && (retryNode != -1))
			endNode = retryNode;
	}

	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(firstNode))
	{
		BfToken token = tokenNode->GetToken();
		if ((token == BfToken_Ref) || (token == BfToken_Mut))
		{
			auto nextNode = mVisitorPos.GetNext();
			mVisitorPos.MoveNext();
			auto typeRef = DoCreateTypeRef(nextNode, createTypeRefFlags);
			if (typeRef == NULL)
			{
				mVisitorPos.mReadPos--;
				AddErrorNode(tokenNode);
				return NULL;
			}
			return CreateRefTypeRef(typeRef, tokenNode);
		}
	}
	return DoCreateTypeRef(firstNode, createTypeRefFlags, endNode);
}

BfTypeReference* BfReducer::CreateTypeRefAfter(BfAstNode* astNode, CreateTypeRefFlags createTypeRefFlags)
{
	AssertCurrentNode(astNode);
	auto nextNode = mVisitorPos.GetNext();
	if (nextNode == NULL)
	{
		FailAfter("Expected type", astNode);
		return NULL;
	}

	mVisitorPos.MoveNext();
	int startPos = mVisitorPos.mReadPos;
	BfTypeReference* typeRef = CreateTypeRef(nextNode, createTypeRefFlags);
	if (typeRef == NULL)
	{
		BF_ASSERT(mVisitorPos.mReadPos == startPos);
		mVisitorPos.mReadPos--;
	}
	return typeRef;
}

BfTypeReference* BfReducer::CreateRefTypeRef(BfTypeReference* elementType, BfTokenNode* refTokenNode)
{
	BfToken refToken = refTokenNode->GetToken();
	BF_ASSERT((refToken == BfToken_Ref) || (refToken == BfToken_Mut) || (refToken == BfToken_In) || (refToken == BfToken_Out));

	if (elementType->IsA<BfRefTypeRef>())
	{
		Fail("Multiple ref levels are not allowed", refTokenNode);
		AddErrorNode(refTokenNode);
		return elementType;
	}

	if (elementType->IsA<BfConstTypeRef>())
	{
		Fail("Refs to const values are not allowed", refTokenNode);
		AddErrorNode(refTokenNode);
		return elementType;
	}

	auto refTypeRef = mAlloc->Alloc<BfRefTypeRef>();
	MEMBER_SET(refTypeRef, mRefToken, refTokenNode);
	ReplaceNode(elementType, refTypeRef);
	refTypeRef->mElementType = elementType;
	return refTypeRef;
}

BfIdentifierNode* BfReducer::CompactQualifiedName(BfAstNode* leftNode)
{
	AssertCurrentNode(leftNode);

	if ((leftNode == NULL) || (!leftNode->IsA<BfIdentifierNode>()))
		return NULL;

	auto prevNode = mVisitorPos.Get(mVisitorPos.mWritePos - 1);
	auto leftIdentifier = (BfIdentifierNode*)leftNode;
	while (true)
	{
		auto nextToken = mVisitorPos.Get(mVisitorPos.mReadPos + 1);
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextToken);
		if ((tokenNode == NULL) || ((tokenNode->GetToken() != BfToken_Dot) /*&& (tokenNode->GetToken() != BfToken_QuestionDot)*/))
			return leftIdentifier;

		auto nextNextToken = mVisitorPos.Get(mVisitorPos.mReadPos + 2);
		auto rightIdentifier = BfNodeDynCast<BfIdentifierNode>(nextNextToken);
		if (rightIdentifier == NULL)
		{
			if (auto rightToken = BfNodeDynCast<BfTokenNode>(nextNextToken))
			{
				if (BfTokenIsKeyword(rightToken->mToken))
				{
					rightIdentifier = mAlloc->Alloc<BfIdentifierNode>();
					ReplaceNode(rightToken, rightIdentifier);
				}
			}

			if (rightIdentifier == NULL)
				return leftIdentifier;
		}

		// If the previous dotted span failed (IE: had chevrons) then don't insert qualified names in the middle of it
		auto prevNodeToken = BfNodeDynCast<BfTokenNode>(prevNode);
		if ((prevNodeToken != NULL) &&
			((prevNodeToken->GetToken() == BfToken_Dot) ||
			 (prevNodeToken->GetToken() == BfToken_QuestionDot) ||
			 (prevNodeToken->GetToken() == BfToken_Arrow)))
			return leftIdentifier;

		mVisitorPos.MoveNext(); // past .
		mVisitorPos.MoveNext(); // past right

		auto qualifiedNameNode = mAlloc->Alloc<BfQualifiedNameNode>();
		ReplaceNode(leftIdentifier, qualifiedNameNode);
		qualifiedNameNode->mLeft = leftIdentifier;
		MEMBER_SET(qualifiedNameNode, mDot, tokenNode);
		MEMBER_SET(qualifiedNameNode, mRight, rightIdentifier);

		leftIdentifier = qualifiedNameNode;
		prevNode = NULL;
	}

	return leftIdentifier;
}

void BfReducer::TryIdentifierConvert(int readPos)
{
	auto node = mVisitorPos.Get(readPos);
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
	{
		if (BfTokenIsKeyword(tokenNode->mToken))
		{
			auto identifierNode = mAlloc->Alloc<BfIdentifierNode>();
			ReplaceNode(tokenNode, identifierNode);
			mVisitorPos.Set(readPos, identifierNode);
		}
	}
}

void BfReducer::CreateQualifiedNames(BfAstNode* node)
{
	auto block = BfNodeDynCast<BfBlock>(node);
	if (block == NULL)
		return;

	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));

	bool isDone = !mVisitorPos.MoveNext();
	while (!isDone)
	{
		auto child = mVisitorPos.GetCurrent();
		BfAstNode* newNode = CompactQualifiedName(child);
		if (newNode == NULL)
			newNode = child;
		CreateQualifiedNames(child);

		isDone = !mVisitorPos.MoveNext();
		if (newNode != NULL)
			mVisitorPos.Write(newNode);
	}
	mVisitorPos.Trim();
}

BfAttributeDirective* BfReducer::CreateAttributeDirective(BfTokenNode* startToken)
{
	BfAttributeDirective* attributeDirective = mAlloc->Alloc<BfAttributeDirective>();
	BfDeferredAstSizedArray<BfExpression*> arguments(attributeDirective->mArguments, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(attributeDirective->mCommas, mAlloc);

	ReplaceNode(startToken, attributeDirective);
	attributeDirective->mAttrOpenToken = startToken;

	bool isHandled = false;

	auto nextNode = mVisitorPos.GetNext();
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
	if (tokenNode != NULL)
	{
		if (tokenNode->GetToken() == BfToken_Return)
		{
			auto attributeTargetSpecifier = mAlloc->Alloc<BfAttributeTargetSpecifier>();
			ReplaceNode(tokenNode, attributeTargetSpecifier);
			MEMBER_SET(attributeDirective, mAttributeTargetSpecifier, attributeTargetSpecifier);
			attributeTargetSpecifier->mTargetToken = tokenNode;
			mVisitorPos.MoveNext();
			tokenNode = ExpectTokenAfter(attributeDirective, BfToken_Colon);
			if (tokenNode != NULL)
				MEMBER_SET(attributeTargetSpecifier, mColonToken, tokenNode);
			attributeDirective->SetSrcEnd(attributeDirective->mAttributeTargetSpecifier->GetSrcEnd());
		}
		else if ((tokenNode->mToken == BfToken_Ampersand) || (tokenNode->mToken == BfToken_AssignEquals))
		{
			MEMBER_SET(attributeDirective, mAttributeTargetSpecifier, tokenNode);
			mVisitorPos.MoveNext();
			isHandled = true;
			nextNode = mVisitorPos.GetNext();
			if (auto identiferNode = BfNodeDynCast<BfIdentifierNode>(nextNode))
			{
				attributeDirective->SetSrcEnd(identiferNode->GetSrcEnd());
				arguments.push_back(identiferNode);
				mVisitorPos.MoveNext();
				nextNode = mVisitorPos.GetNext();
			}
		}
	}

	if (!isHandled)
	{
		auto typeRef = CreateTypeRefAfter(attributeDirective);
		if (typeRef == NULL)
		{
			auto nextNode = mVisitorPos.GetNext();
			if (BfTokenNode* endToken = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (endToken->GetToken() == BfToken_RBracket)
				{
					mVisitorPos.MoveNext();
					MEMBER_SET(attributeDirective, mCtorCloseParen, endToken);
					return attributeDirective;
				}
			}

			return attributeDirective;
		}
		MEMBER_SET(attributeDirective, mAttributeTypeRef, typeRef);
	}

	tokenNode = ExpectTokenAfter(attributeDirective, BfToken_LParen, BfToken_RBracket, BfToken_Comma);
	if (tokenNode == NULL)
		return attributeDirective;
	if (tokenNode->GetToken() == BfToken_LParen)
	{
		MEMBER_SET(attributeDirective, mCtorOpenParen, tokenNode);
		tokenNode = ReadArguments(attributeDirective, attributeDirective, &arguments, &commas, BfToken_RParen, false);
		if (tokenNode == NULL)
		{
			auto nextNode = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_RBracket))
			{
				mVisitorPos.MoveNext();
				goto Do_RBracket;
			}
			return attributeDirective;
		}
		MEMBER_SET(attributeDirective, mCtorCloseParen, tokenNode);
		tokenNode = ExpectTokenAfter(attributeDirective, BfToken_RBracket, BfToken_Comma);
		if (tokenNode == NULL)
			return attributeDirective;
	}
Do_RBracket:
	if (tokenNode->GetToken() == BfToken_RBracket)
	{
		MEMBER_SET(attributeDirective, mAttrCloseToken, tokenNode);
		auto nextNode = mVisitorPos.GetNext();
		tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if ((tokenNode == NULL) || (tokenNode->GetToken() != BfToken_LBracket))
			return attributeDirective;
		mVisitorPos.MoveNext();
	}

	// Has another one- chain it
	auto nextAttribute = CreateAttributeDirective(tokenNode);
	if (nextAttribute != NULL)
	{
		MEMBER_SET(attributeDirective, mNextAttribute, nextAttribute);
	}

	return attributeDirective;
}

BfStatement* BfReducer::CreateAttributedStatement(BfTokenNode* tokenNode, CreateStmtFlags createStmtFlags)
{
	auto attrib = CreateAttributeDirective(tokenNode);
	if (attrib == NULL)
		return NULL;

	BfAstNode* stmt = CreateStatementAfter(attrib, createStmtFlags);
	if (stmt != NULL)
	{
		if (auto localMethodDecl = BfNodeDynCastExact<BfLocalMethodDeclaration>(stmt))
		{
			BF_ASSERT(localMethodDecl->mMethodDeclaration->mAttributes == NULL);
			localMethodDecl->mSrcStart = attrib->mSrcStart;
			MEMBER_SET(localMethodDecl->mMethodDeclaration, mAttributes, attrib);
			return localMethodDecl;
		}

		bool isValid = true;

		auto checkNode = stmt;
		if (auto exprStatement = BfNodeDynCast<BfExpressionStatement>(checkNode))
			checkNode = exprStatement->mExpression;

 		if ((checkNode->IsA<BfObjectCreateExpression>()) ||
 			(checkNode->IsA<BfInvocationExpression>()) ||
 			(checkNode->IsA<BfVariableDeclaration>()) ||
			(checkNode->IsA<BfStringInterpolationExpression>()) ||
 			(checkNode->IsA<BfBlock>()))
		{
			BfAttributedStatement* attribStmt = mAlloc->Alloc<BfAttributedStatement>();
			ReplaceNode(attrib, attribStmt);
			attribStmt->mAttributes = attrib;
			MEMBER_SET(attribStmt, mStatement, stmt);
			return attribStmt;
		}
	}

	Fail("Prefixed attributes can only be used on allocations, invocations, blocks, or variable declarations", attrib);

	BfAttributedStatement* attribStmt = mAlloc->Alloc<BfAttributedStatement>();
	ReplaceNode(attrib, attribStmt);
	attribStmt->mAttributes = attrib;
	if (stmt != NULL)
		MEMBER_SET(attribStmt, mStatement, stmt);
	return attribStmt;
}

BfExpression* BfReducer::CreateAttributedExpression(BfTokenNode* tokenNode, bool onlyAllowIdentifier)
{
	auto attrib = CreateAttributeDirective(tokenNode);
	if (attrib == NULL)
		return NULL;

	if (!onlyAllowIdentifier)
	{
		BfExpression* expr = CreateExpressionAfter(attrib, CreateExprFlags_EarlyExit);
		if (expr != NULL)
		{
			if (auto identifier = BfNodeDynCast<BfIdentifierNode>(expr))
			{
				auto attrIdentifier = mAlloc->Alloc<BfAttributedIdentifierNode>();
				ReplaceNode(attrib, attrIdentifier);
				attrIdentifier->mAttributes = attrib;
				MEMBER_SET(attrIdentifier, mIdentifier, identifier);
				return attrIdentifier;
			}

			if ((expr->IsA<BfObjectCreateExpression>()) ||
				(expr->IsA<BfInvocationExpression>()) ||
				(expr->IsA<BfVariableDeclaration>()) ||
				(expr->IsA<BfStringInterpolationExpression>()) ||
				(expr->IsA<BfBlock>()))
			{
				BfAttributedExpression* attribExpr = mAlloc->Alloc<BfAttributedExpression>();
				ReplaceNode(attrib, attribExpr);
				attribExpr->mAttributes = attrib;
				MEMBER_SET(attribExpr, mExpression, expr);
				return attribExpr;
			}
		}

		Fail("Prefixed attributes can only be used on allocations, invocations, blocks, or variable declarations", attrib);

		BfAttributedExpression* attribExpr = mAlloc->Alloc<BfAttributedExpression>();
		ReplaceNode(attrib, attribExpr);
		attribExpr->mAttributes = attrib;
		if (expr != NULL)
			MEMBER_SET(attribExpr, mExpression, expr);
		return attribExpr;
	}

	auto attrIdentifier = mAlloc->Alloc<BfAttributedIdentifierNode>();
	ReplaceNode(attrib, attrIdentifier);
	attrIdentifier->mAttributes = attrib;
	auto identifier = ExpectIdentifierAfter(attrib);
	if (identifier != NULL)
	{
		MEMBER_SET(attrIdentifier, mIdentifier, identifier);
	}
	return attrIdentifier;
}

BfTokenNode* BfReducer::ReadArguments(BfAstNode* parentNode, BfAstNode* afterNode, SizedArrayImpl<BfExpression*>* arguments, SizedArrayImpl<BfTokenNode*>* commas, BfToken endToken, bool allowSkippedArgs, CreateExprFlags createExprFlags)
{
	for (int paramIdx = 0; true; paramIdx++)
	{
		auto nextNode = mVisitorPos.GetNext();
		if ((nextNode == NULL) && (endToken == BfToken_None))
			return NULL;

		BfTokenNode* tokenNode = BfNodeDynCastExact<BfTokenNode>(nextNode);
		if (tokenNode != NULL)
		{
			if (tokenNode->GetToken() == endToken)
			{
				MoveNode(tokenNode, parentNode);
				mVisitorPos.MoveNext();
				return tokenNode;
			}
			if (paramIdx > 0)
			{
				if (tokenNode->GetToken() != BfToken_Comma)
				{
					Fail("Expected comma", tokenNode);
					return NULL;
				}
				commas->push_back(tokenNode);
				MoveNode(tokenNode, parentNode);
				mVisitorPos.MoveNext();
			}
		}
		else
		{
			if (paramIdx > 0)
			{
				FailAfter("Expected comma", afterNode);
				return NULL;
			}
		}

		BfExpression* argumentExpr = NULL;

		if (allowSkippedArgs)
		{
			auto nextNode = mVisitorPos.GetNext();

			if ((nextNode == NULL) && (endToken == BfToken_None))
				return NULL;

			if (auto nextToken = BfNodeDynCastExact<BfTokenNode>(nextNode))
			{
				if (nextToken->GetToken() == endToken)
					continue;

				if (nextToken->GetToken() == BfToken_Comma)
				{
					arguments->push_back(NULL);
					continue;
				}
			}

			if (auto identifierNode = BfNodeDynCastExact<BfIdentifierNode>(nextNode))
			{
				if (auto nextNextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2)))
				{
					if (nextNextToken->mToken == BfToken_Colon)
					{
						auto namedExpr = mAlloc->Alloc<BfNamedExpression>();
						ReplaceNode(identifierNode, namedExpr);
						MEMBER_SET(namedExpr, mNameNode, identifierNode);
						MEMBER_SET(namedExpr, mColonToken, nextNextToken)
						mVisitorPos.MoveNext();
						mVisitorPos.MoveNext();

						auto innerExpr = CreateExpressionAfter(namedExpr, CreateExprFlags_AllowVariableDecl);
						if (innerExpr != NULL)
						{
							MEMBER_SET(namedExpr, mExpression, innerExpr);
						}
						argumentExpr = namedExpr;
					}
				}
			}
		}

		if (argumentExpr == NULL)
			argumentExpr = CreateExpressionAfter(afterNode, CreateExprFlags_AllowVariableDecl);
		if ((argumentExpr != NULL) || (endToken != BfToken_None))
			arguments->push_back(argumentExpr);
		if (argumentExpr == NULL)
		{
			auto nextNode = mVisitorPos.GetNext();
			if (auto tokenNode = BfNodeDynCastExact<BfTokenNode>(nextNode))
			{
				// Try to continue with param list even after an error
				if ((tokenNode->GetToken() == BfToken_Comma) || (tokenNode->GetToken() == endToken))
					continue;
			}

			return NULL;
		}
		MoveNode(argumentExpr, parentNode);
		afterNode = parentNode;
	}
}

BfIdentifierNode* BfReducer::ExtractExplicitInterfaceRef(BfAstNode* memberDeclaration, BfIdentifierNode* nameIdentifier, BfTypeReference** outExplicitInterface, BfTokenNode** outExplicitInterfaceDotToken)
{
	int dotTokenIdx = -1;
	if (auto qualifiedName = BfNodeDynCast<BfQualifiedNameNode>(nameIdentifier))
	{
		MoveNode(qualifiedName, memberDeclaration);
		MoveNode(qualifiedName->mDot, memberDeclaration);
		*outExplicitInterfaceDotToken = qualifiedName->mDot;

		auto explicitInterfaceRef = CreateTypeRef(qualifiedName->mLeft);
		BF_ASSERT(explicitInterfaceRef != NULL);
		MoveNode(explicitInterfaceRef, memberDeclaration);
		*outExplicitInterface = explicitInterfaceRef;

		return qualifiedName->mRight;
	}
	else if (IsTypeReference(nameIdentifier, BfToken_Dot, -1, &dotTokenIdx))
	{
		BfAstNode* dotToken = mVisitorPos.Get(dotTokenIdx);
		MoveNode(dotToken, memberDeclaration);
		*outExplicitInterfaceDotToken = (BfTokenNode*)dotToken;
		auto explicitInterfaceRef = CreateTypeRef(nameIdentifier);
		BF_ASSERT(explicitInterfaceRef != NULL);
		MoveNode(explicitInterfaceRef, memberDeclaration);
		*outExplicitInterface = explicitInterfaceRef;

		return ExpectIdentifierAfter(memberDeclaration);
	}
	return nameIdentifier;
}

BfFieldDtorDeclaration* BfReducer::CreateFieldDtorDeclaration(BfAstNode* srcNode)
{
	BfFieldDtorDeclaration* firstFieldDtor = NULL;

	BfFieldDtorDeclaration* prevFieldDtor = NULL;
	auto nextNode = mVisitorPos.GetNext();
	while (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (nextToken->GetToken() != BfToken_Tilde)
			break;
		auto fieldDtor = mAlloc->Alloc<BfFieldDtorDeclaration>();
		ReplaceNode(nextToken, fieldDtor);
		fieldDtor->mTildeToken = nextToken;

		//int prevReadPos = mVisitorPos.mReadPos;
		mVisitorPos.MoveNext();

		if (prevFieldDtor != NULL)
		{
			MEMBER_SET(prevFieldDtor, mNextFieldDtor, fieldDtor);
		}
		else
		{
			firstFieldDtor = fieldDtor;
			MoveNode(fieldDtor, srcNode);
		}

		auto statement = CreateStatementAfter(srcNode);
		if (statement == NULL)
		{
			//mVisitorPos.mReadPos = prevReadPos;
			break;
		}
		MEMBER_SET(fieldDtor, mBody, statement);
		fieldDtor->SetSrcEnd(statement->GetSrcEnd());
		prevFieldDtor = fieldDtor;

		srcNode->SetSrcEnd(fieldDtor->GetSrcEnd());

		nextNode = mVisitorPos.GetNext();

		// We used to NOT have this break here.  Why were we allowing multiple ~'s when a block statement would have made more sense?
		break; // Wh
	}

	return firstFieldDtor;
}

BfFieldDeclaration* BfReducer::CreateFieldDeclaration(BfTokenNode* tokenNode, BfTypeReference* typeRef, BfIdentifierNode* nameIdentifier, BfFieldDeclaration* prevFieldDeclaration)
{
	auto fieldDeclaration = mAlloc->Alloc<BfFieldDeclaration>();
	if (prevFieldDeclaration != NULL)
	{
		ReplaceNode(tokenNode, fieldDeclaration);
		MEMBER_SET(fieldDeclaration, mPrecedingComma, tokenNode);
		MEMBER_SET(fieldDeclaration, mNameNode, nameIdentifier);
		fieldDeclaration->mDocumentation = prevFieldDeclaration->mDocumentation;
		fieldDeclaration->mAttributes = prevFieldDeclaration->mAttributes;
		fieldDeclaration->mProtectionSpecifier = prevFieldDeclaration->mProtectionSpecifier;
		fieldDeclaration->mStaticSpecifier = prevFieldDeclaration->mStaticSpecifier;
		fieldDeclaration->mTypeRef = prevFieldDeclaration->mTypeRef;
		fieldDeclaration->mConstSpecifier = prevFieldDeclaration->mConstSpecifier;
		fieldDeclaration->mReadOnlySpecifier = prevFieldDeclaration->mReadOnlySpecifier;
		fieldDeclaration->mVolatileSpecifier = prevFieldDeclaration->mVolatileSpecifier;
		fieldDeclaration->mNewSpecifier = prevFieldDeclaration->mNewSpecifier;
		fieldDeclaration->mExternSpecifier = prevFieldDeclaration->mExternSpecifier;
		tokenNode = ExpectTokenAfter(fieldDeclaration, BfToken_Semicolon, BfToken_AssignEquals, BfToken_Comma, BfToken_Tilde);
		if (tokenNode == NULL)
			return fieldDeclaration;
		mVisitorPos.mReadPos--; // Go back to token
	}
	else
	{
		ReplaceNode(typeRef, fieldDeclaration);
		fieldDeclaration->mTypeRef = typeRef;
		fieldDeclaration->mNameNode = nameIdentifier;
		fieldDeclaration->mInitializer = NULL;
		MoveNode(fieldDeclaration->mNameNode, fieldDeclaration);
		//mVisitorPos.MoveNext();
	}
	BfToken token = tokenNode->GetToken();
	if (token == BfToken_AssignEquals)
	{
		MEMBER_SET(fieldDeclaration, mEqualsNode, tokenNode);
		MoveNode(tokenNode, fieldDeclaration);
		mVisitorPos.MoveNext();

		mIsFieldInitializer = true;
		fieldDeclaration->mInitializer = CreateExpressionAfter(fieldDeclaration);
		mIsFieldInitializer = false;

		if (fieldDeclaration->mInitializer != NULL)
		{
			MoveNode(fieldDeclaration->mInitializer, fieldDeclaration);
			auto nextToken = ExpectTokenAfter(fieldDeclaration, BfToken_Semicolon, BfToken_Comma, BfToken_Tilde);
			if (nextToken != NULL)
				mVisitorPos.mReadPos--; // Backtrack, someone else eats these
		}
	}
	else if (token == BfToken_Comma)
	{
		//
	}
	else if (token == BfToken_Tilde)
	{
		//
	}
	else if (token != BfToken_Semicolon)
	{
		//MEMBER_SET(fieldDeclaration, mEqualsNode, tokenNode);
		FailAfter("';', '=', or '~' expected", nameIdentifier);
		return fieldDeclaration;
	}

	bool hasSemicolon = false;

	auto fieldDtor = CreateFieldDtorDeclaration(fieldDeclaration);
	if (fieldDtor != NULL)
	{
		fieldDeclaration->mFieldDtor = fieldDtor;
		if (fieldDtor->mBody != NULL)
			hasSemicolon = !fieldDtor->mBody->IsMissingSemicolon();
	}

	if ((!hasSemicolon) && (ExpectTokenAfter(fieldDeclaration, BfToken_Semicolon, BfToken_Comma) != NULL))
	{
		// This gets taken later
		mVisitorPos.mReadPos--;
	}

	fieldDeclaration->mDocumentation = FindDocumentation(mTypeMemberNodeStart, fieldDeclaration, true);

	return fieldDeclaration;
}

BfAstNode* BfReducer::ReadTypeMember(BfTokenNode* tokenNode, bool declStarted, int depth, BfAstNode* deferredHeadNode)
{
	BfToken token = tokenNode->GetToken();

	if (token == BfToken_Semicolon)
		return tokenNode;

	if (token == BfToken_LBracket)
	{
		auto attributes = CreateAttributeDirective(tokenNode);
		if (attributes == NULL)
			return NULL;

		auto nextNode = mVisitorPos.GetNext();
		if (nextNode == NULL)
		{
			FailAfter("Expected member", attributes);
			return NULL;
		}

		SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, attributes, !declStarted);
		mVisitorPos.MoveNext();
		auto memberNode = ReadTypeMember(nextNode, true, depth, (deferredHeadNode != NULL) ? deferredHeadNode : attributes);
		if (memberNode == NULL)
			return NULL;

		if (auto enumCaseDecl = BfNodeDynCast<BfEnumCaseDeclaration>(memberNode))
		{
			if (!enumCaseDecl->mEntries.IsEmpty())
			{
				enumCaseDecl->mSrcStart = attributes->mSrcStart;
				enumCaseDecl->mEntries[0]->mAttributes = attributes;
				enumCaseDecl->mEntries[0]->mSrcStart = attributes->mSrcStart;
				return enumCaseDecl;
			}
		}

		auto member = BfNodeDynCast<BfMemberDeclaration>(memberNode);
		if (member == NULL)
		{
			if (auto innerType = BfNodeDynCast<BfTypeDeclaration>(memberNode))
			{
				ReplaceNode(attributes, innerType);
				innerType->mAttributes = attributes;
				return innerType;
			}
			Fail("Invalid target for attributes", memberNode);
			return memberNode;
		}

		ReplaceNode(attributes, member);
		member->mAttributes = attributes;
		return member;
	}

	if (token == BfToken_Operator)
	{
		auto operatorDecl = mAlloc->Alloc<BfOperatorDeclaration>();
		BfDeferredAstSizedArray<BfParameterDeclaration*> params(operatorDecl->mParams, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(operatorDecl->mCommas, mAlloc);
		ReplaceNode(tokenNode, operatorDecl);
		operatorDecl->mOperatorToken = tokenNode;

		auto typeRef = CreateTypeRefAfter(operatorDecl);
		if (typeRef == NULL)
			return operatorDecl;
		MEMBER_SET_CHECKED(operatorDecl, mReturnType, typeRef);
		operatorDecl->mIsConvOperator = true;

		ParseMethod(operatorDecl, &params, &commas);

		return operatorDecl;
	}

	if (token == BfToken_This)
	{
		auto ctorDecl = mAlloc->Alloc<BfConstructorDeclaration>();
		BfDeferredAstSizedArray<BfParameterDeclaration*> params(ctorDecl->mParams, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(ctorDecl->mCommas, mAlloc);
		ctorDecl->mReturnType = NULL;
		ReplaceNode(tokenNode, ctorDecl);
		MEMBER_SET(ctorDecl, mThisToken, tokenNode);
		if (auto block = BfNodeDynCast<BfBlock>(mVisitorPos.GetNext()))
		{
			mVisitorPos.MoveNext();
			MEMBER_SET(ctorDecl, mBody, block);

			if (IsNodeRelevant(ctorDecl))
			{
				SetAndRestoreValue<BfMethodDeclaration*> prevMethodDeclaration(mCurMethodDecl, ctorDecl);
				HandleBlock(block);
			}
		}
		else
			ParseMethod(ctorDecl, &params, &commas);
		return ctorDecl;
	}

	if (token == BfToken_Tilde)
	{
		auto thisToken = ExpectTokenAfter(tokenNode, BfToken_This);
		if (thisToken == NULL)
		{
			auto nextNode = mVisitorPos.GetNext();
			auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(nextNode);
			if (nameIdentifier != NULL)
			{
				// Eat DTOR name
				AddErrorNode(nameIdentifier);
				//nameIdentifier->RemoveSelf();
			}
			else
			{
				//AddErrorNode(tokenNode);
				//return NULL;
			}

			AddErrorNode(tokenNode);
			return NULL;
		}

		auto dtorDecl = mAlloc->Alloc<BfDestructorDeclaration>();
		BfDeferredAstSizedArray<BfParameterDeclaration*> params(dtorDecl->mParams, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(dtorDecl->mCommas, mAlloc);
		dtorDecl->mReturnType = NULL;
		ReplaceNode(tokenNode, dtorDecl);
		dtorDecl->mTildeToken = tokenNode;
		if (thisToken != NULL)
		{
			MEMBER_SET(dtorDecl, mThisToken, thisToken);
		}

		ParseMethod(dtorDecl, &params, &commas);
		return dtorDecl;
	}

	if (token == BfToken_Mixin)
	{
		auto methodDecl = mAlloc->Alloc<BfMethodDeclaration>();
		BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDecl->mParams, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(methodDecl->mCommas, mAlloc);
		ReplaceNode(tokenNode, methodDecl);
		methodDecl->mDocumentation = FindDocumentation(methodDecl);
		methodDecl->mMixinSpecifier = tokenNode;
		//mVisitorPos.MoveNext();
		auto nameNode = ExpectIdentifierAfter(methodDecl);
		if (nameNode != NULL)
		{
			MEMBER_SET(methodDecl, mNameNode, nameNode);

			auto nextNode = mVisitorPos.GetNext();
			if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
			{
				if (tokenNode->GetToken() == BfToken_LChevron)
				{
					auto genericParams = CreateGenericParamsDeclaration(tokenNode);
					if (genericParams != NULL)
					{
						MEMBER_SET(methodDecl, mGenericParams, genericParams);
					}
				}
			}

			ParseMethod(methodDecl, &params, &commas);
		}
		return methodDecl;
	}

	if (token == BfToken_Case)
	{
		auto enumCaseDecl = mAlloc->Alloc<BfEnumCaseDeclaration>();
		BfDeferredAstSizedArray<BfFieldDeclaration*> entries(enumCaseDecl->mEntries, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(enumCaseDecl->mCommas, mAlloc);
		ReplaceNode(tokenNode, enumCaseDecl);
		enumCaseDecl->mCaseToken = tokenNode;

		while (true)
		{
			auto caseName = ExpectIdentifierAfter(enumCaseDecl);
			if (caseName == NULL)
				break;

			auto enumEntry = mAlloc->Alloc<BfEnumEntryDeclaration>();
			ReplaceNode(caseName, enumEntry);
			enumEntry->mNameNode = caseName;
			entries.push_back(enumEntry);

			tokenNode = ExpectTokenAfter(enumEntry, BfToken_Comma, BfToken_AssignEquals, BfToken_LParen, BfToken_Semicolon);
			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_LParen))
			{
				auto typeRef = CreateTypeRef(tokenNode, CreateTypeRefFlags_AllowSingleMemberTuple);
				tokenNode = NULL;
				auto tupleType = BfNodeDynCast<BfTupleTypeRef>(typeRef);
				if (tupleType != NULL)
				{
					MEMBER_SET(enumEntry, mTypeRef, tupleType);
					tokenNode = ExpectTokenAfter(enumEntry, BfToken_Comma, BfToken_AssignEquals, BfToken_Semicolon);
				}
			}

			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_AssignEquals))
			{
				MEMBER_SET(enumEntry, mEqualsNode, tokenNode);
				tokenNode = NULL;

				auto initializer = CreateExpressionAfter(enumEntry);
				if (initializer != NULL)
				{
					MEMBER_SET(enumEntry, mInitializer, initializer);
					tokenNode = ExpectTokenAfter(enumEntry, BfToken_Comma, BfToken_Semicolon);
				}
			}

			MoveNode(enumEntry, enumCaseDecl);

			if (tokenNode == NULL)
				return enumCaseDecl;
			if (tokenNode->GetToken() == BfToken_Semicolon)
			{
				mVisitorPos.mReadPos--;
				return enumCaseDecl;
			}

			MoveNode(tokenNode, enumCaseDecl);
			commas.push_back(tokenNode);
		}

		return enumCaseDecl;
	}

	auto nextNode = mVisitorPos.GetNext();
	if (nextNode == NULL)
		FailAfter("Type expected", tokenNode);

	if ((token == BfToken_Struct) || (token == BfToken_Enum) || (token == BfToken_Class) || (token == BfToken_Delegate) ||
		(token == BfToken_Function) || (token == BfToken_Interface) || (token == BfToken_Extension) || (token == BfToken_TypeAlias))
	{
		mVisitorPos.mReadPos -= depth;
		BfAstNode* startNode = mVisitorPos.GetCurrent();
		auto startToken = BfNodeDynCast<BfTokenNode>(startNode);

		auto topLevelObject = CreateTopLevelObject(startToken, NULL, deferredHeadNode);
		auto typeDecl = BfNodeDynCast<BfTypeDeclaration>(topLevelObject);
		if (typeDecl == NULL)
		{
			AddErrorNode(tokenNode);
			return NULL;
		}
		return typeDecl;
	}

	if (token == BfToken_Comma)
	{
		auto prevNode = mVisitorPos.Get(mVisitorPos.mWritePos - 1);
		auto prevFieldDecl = BfNodeDynCast<BfFieldDeclaration>(prevNode);
		if (prevFieldDecl != NULL)
		{
			auto nameIdentifier = ExpectIdentifierAfter(tokenNode);
			if (nameIdentifier == NULL)
			{
				AddErrorNode(tokenNode);
				return NULL;
			}
			return CreateFieldDeclaration(tokenNode, prevFieldDecl->mTypeRef, nameIdentifier, prevFieldDecl);
		}
	}

	switch (token)
	{
	case BfToken_Sealed:
	case BfToken_Static:
	case BfToken_Const:
	case BfToken_Mut:
	case BfToken_Public:
	case BfToken_Protected:
	case BfToken_Private:
	case BfToken_Internal:
	case BfToken_Virtual:
	case BfToken_Override:
	case BfToken_Abstract:
	case BfToken_Concrete:
	case BfToken_Append:
	case BfToken_Extern:
	case BfToken_New:
	case BfToken_Implicit:
	case BfToken_Explicit:
	case BfToken_ReadOnly:
	case BfToken_Inline:
	case BfToken_Using:
	case BfToken_Volatile:
		break;
	default:
		AddErrorNode(tokenNode);
		Fail("Unexpected token", tokenNode);
		return NULL;
		break;
	}

	int startNodeIdx = gAssertCurrentNodeIdx;
	BfAstNode* typeMember = NULL;
	nextNode = mVisitorPos.GetNext();
	if (nextNode != NULL)
	{
		if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			if ((nextTokenNode->mToken == BfToken_LBracket) && (depth > 0))
			{
				// We can't apply this to a custom attribute
				AddErrorNode(tokenNode);
				Fail("Unexpected token", tokenNode);
				return NULL;
			}
		}

		mVisitorPos.MoveNext();
		typeMember = ReadTypeMember(nextNode, true, depth + 1);
	}

	auto memberDecl = BfNodeDynCast<BfMemberDeclaration>(typeMember);
	if (memberDecl == NULL) // May be an embedded type
	{
		if (auto typeDecl = BfNodeDynCast<BfTypeDeclaration>(typeMember))
			return typeMember;
	}

	if (typeMember == NULL)
	{
		auto propertyDeclaration = mAlloc->Alloc<BfPropertyDeclaration>();
		ReplaceNode(tokenNode, propertyDeclaration);
		propertyDeclaration->mDocumentation = FindDocumentation(propertyDeclaration);
		typeMember = memberDecl = propertyDeclaration;
	}

	if (memberDecl == NULL)
		return NULL;

	if (token == BfToken_Static)
	{
		if (memberDecl->mStaticSpecifier != NULL)
		{
			AddErrorNode(memberDecl->mStaticSpecifier);
			Fail("Static already specified", memberDecl->mStaticSpecifier);
		}
		MEMBER_SET(memberDecl, mStaticSpecifier, tokenNode);

		auto fieldDecl = BfNodeDynCast<BfFieldDeclaration>(memberDecl);
		if (fieldDecl != NULL)
		{
			if ((fieldDecl->mStaticSpecifier != NULL) && (fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Const))
				Fail("Cannot use 'static' and 'const' together", fieldDecl);
		}

		return memberDecl;
	}

	if ((token == BfToken_Public) ||
		(token == BfToken_Protected) ||
		(token == BfToken_Private) ||
		(token == BfToken_Internal))
	{
		if (auto fieldDecl = BfNodeDynCast<BfFieldDeclaration>(memberDecl))
		{
			if (auto usingSpecifier = BfNodeDynCastExact<BfUsingSpecifierNode>(fieldDecl->mConstSpecifier))
			{
				SetProtection(memberDecl, usingSpecifier->mProtection, tokenNode);
				usingSpecifier->mTriviaStart = tokenNode->mTriviaStart;
				return memberDecl;
			}
		}

		SetProtection(memberDecl, memberDecl->mProtectionSpecifier, tokenNode);
		return memberDecl;
	}

	if (auto methodDecl = BfNodeDynCast<BfMethodDeclaration>(memberDecl))
	{
		if ((token == BfToken_Virtual) ||
			(token == BfToken_Override) ||
			(token == BfToken_Abstract) ||
			(token == BfToken_Concrete))
		{
			if (methodDecl->mVirtualSpecifier != NULL)
			{
				AddErrorNode(methodDecl->mVirtualSpecifier);
				if (methodDecl->mVirtualSpecifier->GetToken() == tokenNode->GetToken())
					Fail(StrFormat("Already specified '%s'", BfTokenToString(tokenNode->GetToken())), methodDecl->mVirtualSpecifier);
				else
					Fail(StrFormat("Cannot specify both '%s' and '%s'", BfTokenToString(methodDecl->mVirtualSpecifier->GetToken()), BfTokenToString(tokenNode->GetToken())), methodDecl->mVirtualSpecifier);
			}

			MEMBER_SET(methodDecl, mVirtualSpecifier, tokenNode);
			return memberDecl;
		}

		if (token == BfToken_Extern)
		{
			if (methodDecl->mExternSpecifier != NULL)
			{
				AddErrorNode(methodDecl->mExternSpecifier);
				Fail("Extern already specified", methodDecl->mExternSpecifier);
			}
			MEMBER_SET(methodDecl, mExternSpecifier, tokenNode);
			return memberDecl;
		}

		if (token == BfToken_New)
		{
			if (methodDecl->mNewSpecifier != NULL)
			{
				AddErrorNode(methodDecl->mNewSpecifier);
				Fail("New already specified", methodDecl->mNewSpecifier);
			}
			MEMBER_SET(methodDecl, mNewSpecifier, tokenNode);
			return memberDecl;
		}

		if (token == BfToken_Mut)
		{
			if (methodDecl->mMutSpecifier != NULL)
			{
				AddErrorNode(methodDecl->mMutSpecifier);
				Fail("Mut already specified", methodDecl->mMutSpecifier);
			}
			MEMBER_SET(methodDecl, mMutSpecifier, tokenNode);
			return memberDecl;
		}

		if (token == BfToken_ReadOnly)
		{
			if (methodDecl->mReadOnlySpecifier == NULL)
			{
				MEMBER_SET(methodDecl, mReadOnlySpecifier, tokenNode);
			}
			return memberDecl;
		}
	}

	if (auto operatorDecl = BfNodeDynCast<BfOperatorDeclaration>(memberDecl))
	{
		if ((token == BfToken_Implicit) ||
			(token == BfToken_Explicit))
		{
			if (operatorDecl->mExplicitToken != NULL)
			{
				AddErrorNode(operatorDecl->mExplicitToken);
				Fail(StrFormat("'%s' already specified", BfTokenToString(operatorDecl->mExplicitToken->GetToken())), operatorDecl->mExplicitToken);
			}
			MEMBER_SET(operatorDecl, mExplicitToken, tokenNode);
			return memberDecl;
		}
	}

	if (auto propDecl = BfNodeDynCast<BfPropertyDeclaration>(memberDecl))
	{
		if ((token == BfToken_Virtual) ||
			(token == BfToken_Override) ||
			(token == BfToken_Abstract) ||
			(token == BfToken_Concrete))
		{
			if (propDecl->mVirtualSpecifier != NULL)
			{
				AddErrorNode(propDecl->mVirtualSpecifier);
				if (propDecl->mVirtualSpecifier->GetToken() == token)
					Fail(StrFormat("Already specified '%s'", BfTokenToString(token)), propDecl->mVirtualSpecifier);
				else
					Fail(StrFormat("Cannot specify both '%s' and '%s'", BfTokenToString(propDecl->mVirtualSpecifier->GetToken()), BfTokenToString(token)), propDecl->mVirtualSpecifier);
			}
			MEMBER_SET(propDecl, mVirtualSpecifier, tokenNode);
			return memberDecl;
		}
	}

	if (auto fieldDecl = BfNodeDynCast<BfFieldDeclaration>(memberDecl))
	{
		if (token == BfToken_Override)
		{
			// Must be typing an 'override' over a field declaration
			auto propDecl = mAlloc->Alloc<BfPropertyDeclaration>();
			propDecl->mVirtualSpecifier = tokenNode;
			typeMember = memberDecl = propDecl;
		}

		bool handled = false;

		if (token == BfToken_Const)
		{
			if ((fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Using))
			{
				Fail("Const cannot be used with 'using' specified", tokenNode);
			}
			else if (fieldDecl->mConstSpecifier != NULL)
			{
				Fail("Const already specified", tokenNode);
			}
			MEMBER_SET(fieldDecl, mConstSpecifier, tokenNode);
			handled = true;
		}

		if (token == BfToken_Using)
		{
			if ((fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Const))
			{
				Fail("Const cannot be used with 'using' specified", tokenNode);
			}
			else if (fieldDecl->mConstSpecifier != NULL)
			{
				Fail("Using already specified", tokenNode);
			}

			auto usingSpecifier = mAlloc->Alloc<BfUsingSpecifierNode>();
			ReplaceNode(tokenNode, usingSpecifier);
			MEMBER_SET(usingSpecifier, mUsingToken, tokenNode);
			MEMBER_SET(fieldDecl, mConstSpecifier, usingSpecifier);
			handled = true;
		}

		if (token == BfToken_ReadOnly)
		{
			if (fieldDecl->mReadOnlySpecifier == NULL)
			{
				MEMBER_SET(fieldDecl, mReadOnlySpecifier, tokenNode);
			}
			handled = true;
		}

		if (token == BfToken_Inline)
		{
			MEMBER_SET(fieldDecl, mReadOnlySpecifier, tokenNode);
			handled = true;
		}

		if (token == BfToken_Volatile)
		{
			MEMBER_SET(fieldDecl, mVolatileSpecifier, tokenNode);
			handled = true;
		}

		if (token == BfToken_Extern)
		{
			if ((fieldDecl->mExternSpecifier != NULL) && (fieldDecl->mExternSpecifier->mToken == BfToken_Append))
 			{
 				Fail("Extern cannot be used with 'append' specified", tokenNode);
 			}
 			else if (fieldDecl->mExternSpecifier != NULL)
 			{
				Fail("Extern already specified", tokenNode);
 			}

			MEMBER_SET(fieldDecl, mExternSpecifier, tokenNode);
			handled = true;
		}

		if (token == BfToken_Append)
		{
			if ((fieldDecl->mExternSpecifier != NULL) && (fieldDecl->mExternSpecifier->mToken == BfToken_Extern))
			{
				Fail("Append cannot be used with 'extern' specified", tokenNode);
			}
			else if (fieldDecl->mExternSpecifier != NULL)
			{
				Fail("Append already specified", tokenNode);
			}

			MEMBER_SET(fieldDecl, mExternSpecifier, tokenNode);
			handled = true;
		}

		if (token == BfToken_New)
		{
			if (fieldDecl->mNewSpecifier != NULL)
			{
				Fail("New already specified", tokenNode);
			}
			MEMBER_SET(fieldDecl, mNewSpecifier, tokenNode);
			handled = true;
		}

		if ((fieldDecl->mStaticSpecifier != NULL) && (fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Const))
			Fail("Cannot use 'static' and 'const' together", fieldDecl);

		if ((fieldDecl->mReadOnlySpecifier != NULL) && (fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Const))
			Fail("Cannot use 'readonly' and 'const' together", fieldDecl);

		if ((fieldDecl->mVolatileSpecifier != NULL) && (fieldDecl->mConstSpecifier != NULL) && (fieldDecl->mConstSpecifier->mToken == BfToken_Const))
			Fail("Cannot use 'volatile' and 'const' together", fieldDecl);

		if (handled)
			return fieldDecl;
	}

	// Eat node
	if (memberDecl != NULL)
		ReplaceNode(tokenNode, memberDecl);
	Fail("Invalid token", tokenNode);
	return memberDecl;
}

void BfReducer::ReadPropertyBlock(BfPropertyDeclaration* propertyDeclaration, BfBlock* block)
{
	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));
	BfDeferredAstSizedArray<BfPropertyMethodDeclaration*> methods(propertyDeclaration->mMethods, mAlloc);

	bool hadGet = false;
	bool hadSet = false;

	while (true)
	{
		BfAstNode* protectionSpecifier = NULL;
		BfAttributeDirective* attributes = NULL;
		auto child = mVisitorPos.GetNext();
		if (child == NULL)
			break;

		String accessorName;
		BfTokenNode* mutSpecifier = NULL;
		BfTokenNode* refSpecifier = NULL;

		while (true)
		{
			auto tokenNode = BfNodeDynCast<BfTokenNode>(child);
			if (tokenNode == NULL)
				break;
			BfToken token = tokenNode->GetToken();
			if (token == BfToken_LBracket)
			{
				mVisitorPos.MoveNext();
				attributes = CreateAttributeDirective(tokenNode);
				child = mVisitorPos.GetNext();
			}

			tokenNode = BfNodeDynCast<BfTokenNode>(child);
			if (tokenNode == NULL)
				break;
			token = tokenNode->GetToken();
			if ((token == BfToken_Private) ||
				(token == BfToken_Protected) ||
				(token == BfToken_Public) ||
				(token == BfToken_Internal))
			{
				SetProtection(NULL, protectionSpecifier, tokenNode);
				mVisitorPos.MoveNext();
				child = mVisitorPos.GetCurrent();
			}
			else if (token == BfToken_Mut)
			{
				Fail("'mut' must be specified after get/set", tokenNode);
				mutSpecifier = tokenNode;

				mVisitorPos.MoveNext();
			}
			else
				break;
			child = mVisitorPos.GetNext();
		}

		auto identifierNode = child;
		auto accessorIdentifier = BfNodeDynCast<BfIdentifierNode>(child);
		if (accessorIdentifier != NULL)
		{
			accessorName = accessorIdentifier->ToString();
			mVisitorPos.MoveNext();
			child = mVisitorPos.GetNext();
		}
		if (accessorName == "get")
		{
			// 			if (hadGet)
			// 				Fail("Only one 'get' method can be specified", accessorIdentifier);
			hadGet = true;
		}
		else if (accessorName == "set")
		{
			// 			if (hadSet)
			// 				Fail("Only one 'set' method can be specified", accessorIdentifier);
			hadSet = true;
		}
		else
		{
			auto refNode = child;
			if (refNode == NULL)
				refNode = block->mCloseBrace;
			Fail("Expected 'get' or 'set'", refNode);
		}

		BfAstNode* bodyAfterNode = accessorIdentifier;

		BfAstNode* body = NULL;

		auto tokenNode = BfNodeDynCast<BfTokenNode>(child);
		if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Ref) && (accessorName == "set"))
		{
			refSpecifier = tokenNode;
			bodyAfterNode = tokenNode;

			mVisitorPos.MoveNext();
			child = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(child);
		}

		if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Mut))
		{
			if (mutSpecifier != NULL)
			{
				Fail("'mut' already specified", mutSpecifier);
			}
			mutSpecifier = tokenNode;
			bodyAfterNode = tokenNode;

			mVisitorPos.MoveNext();
			child = mVisitorPos.GetNext();
		}

		bool handled = false;
		BfTokenNode* fatArrowToken = NULL;
		BfAstNode* endSemicolon = NULL;
		if ((tokenNode = BfNodeDynCast<BfTokenNode>(child)))
		{
			if ((tokenNode->GetToken() == BfToken_Semicolon))
			{
				handled = true;
				endSemicolon = tokenNode;
				mVisitorPos.MoveNext();
			}
			else if (tokenNode->mToken == BfToken_FatArrow)
			{
				handled = true;
				fatArrowToken = tokenNode;
				mVisitorPos.MoveNext();

				auto expr = CreateExpressionAfter(tokenNode);
				if (expr != NULL)
				{
					body = expr;
					endSemicolon = ExpectTokenAfter(expr, BfToken_Semicolon);
				}
			}
		}

		if (!handled)
		{
			if (bodyAfterNode != NULL)
			{
				body = ExpectBlockAfter(bodyAfterNode);
			}
			else
			{
				if (auto accessorBlock = BfNodeDynCast<BfBlock>(child))
				{
					body = accessorBlock;
					mVisitorPos.MoveNext();
				}
				if (body == NULL)
				{
					if (child != NULL)
					{
						Fail("Block expected", child);
						AddErrorNode(child);
						mVisitorPos.MoveNext();
					}
				}
			}
		}

		if (auto block = BfNodeDynCast<BfBlock>(body))
		{
			if (((attributes == NULL) && (IsNodeRelevant(block))) ||
				((attributes != NULL) && (IsNodeRelevant(attributes, block))))
			{
				HandleBlock(block);
			}
		}

		if ((body == NULL) && (!handled))
		{
			if (protectionSpecifier != NULL)
				AddErrorNode(protectionSpecifier);
			if (accessorIdentifier != NULL)
				AddErrorNode(accessorIdentifier);
			if (mutSpecifier != NULL)
				AddErrorNode(mutSpecifier);
			if (refSpecifier != NULL)
				AddErrorNode(refSpecifier);
			continue;
		}

		auto method = mAlloc->Alloc<BfPropertyMethodDeclaration>();
		method->mPropertyDeclaration = propertyDeclaration;

		if (fatArrowToken != NULL)
			MEMBER_SET(method, mFatArrowToken, fatArrowToken);
		if (endSemicolon != NULL)
			MEMBER_SET(method, mEndSemicolon, endSemicolon);
		if (protectionSpecifier != NULL)
			MEMBER_SET(method, mProtectionSpecifier, protectionSpecifier);
		if (accessorIdentifier != NULL)
			MEMBER_SET(method, mNameNode, accessorIdentifier);
		if (body != NULL)
		{
			MEMBER_SET(method, mBody, body);
		}
		if (refSpecifier != NULL)
			MEMBER_SET(method, mSetRefSpecifier, refSpecifier);
		if (mutSpecifier != NULL)
			MEMBER_SET(method, mMutSpecifier, mutSpecifier);
		// 		if ((accessorBlock != NULL) && (IsNodeRelevant(propertyDeclaration)))
		// 			HandleBlock(accessorBlock);
		if (attributes != NULL)
			MEMBER_SET(method, mAttributes, attributes);

		methods.push_back(method);
	}
}

BfAstNode* BfReducer::ReadTypeMember(BfAstNode* node, bool declStarted, int depth, BfAstNode* deferredHeadNode)
{
// 	SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, node, false);
// 	if (depth == 0)
// 		prevTypeMemberNodeStart.Set();

	if (mCurTypeDecl != NULL)
		AssertCurrentNode(node);

	BfTokenNode* refToken = NULL;

	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
	{
		BfToken token = tokenNode->GetToken();
		bool isTypeRef = false;
		if ((token == BfToken_Delegate) || (token == BfToken_Function))
		{
			// We need to differentiate between a delegate type reference and a delegate type declaration
			int endNodeIdx = -1;
			if (IsTypeReference(node, BfToken_LParen, -1, &endNodeIdx))
			{
				isTypeRef = true;
			}
		}

		if ((token == BfToken_LBracket) && (depth > 0))
		{
			Fail("Unexpected custom attribute", node);
			return NULL;
		}

		if (isTypeRef)
		{
			// Handled below
		}
		else if ((token == BfToken_Ref) || (token == BfToken_Mut))
		{
			refToken = tokenNode;
			mVisitorPos.MoveNext();
			// Read type member
		}
		else if ((token == BfToken_Var) ||
			(token == BfToken_Let) ||
			(token == BfToken_AllocType) ||
			(token == BfToken_RetType) ||
			(token == BfToken_Nullable) ||
			(token == BfToken_Comptype) ||
			(token == BfToken_Decltype) ||
			(token == BfToken_LParen))
		{
			// Read type member
		}
		else
		{
			SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, tokenNode, !declStarted);
			return ReadTypeMember(tokenNode, declStarted, depth, deferredHeadNode);
		}
	}
	else if (auto block = BfNodeDynCast<BfBlock>(node))
	{
		Fail("Expected method declaration", node);

		HandleBlock(block);
		auto methodDecl = mAlloc->Alloc<BfMethodDeclaration>();
		ReplaceNode(block, methodDecl);
		methodDecl->mDocumentation = FindDocumentation(methodDecl);
		methodDecl->mBody = block;

		return methodDecl;
	}

	BfTokenNode* indexerThisToken = NULL;
	bool isIndexProp = false;

	auto origNode = node;

	if (refToken != NULL)
	{
		auto nextNode = mVisitorPos.Get(mVisitorPos.mReadPos);
		if (nextNode != NULL)
			node = nextNode;
	}

	auto typeRef = CreateTypeRef(node);
	if (typeRef == NULL)
	{
		if (refToken != NULL)
		{
			AddErrorNode(refToken);
			if (mVisitorPos.Get(mVisitorPos.mReadPos) == node)
			{
				mVisitorPos.mReadPos--;
				return NULL;
			}
		}
#ifdef BF_AST_HAS_PARENT_MEMBER
		BF_ASSERT(node->mParent != NULL);
#endif
		AddErrorNode(node);
		return NULL;
	}

	if (refToken != NULL)
		typeRef = CreateRefTypeRef(typeRef, refToken);

	node = typeRef;

	auto nextNode = mVisitorPos.GetNext();
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (tokenNode->GetToken() == BfToken_This)
			indexerThisToken = tokenNode;
	}

	BfTokenNode* explicitInterfaceDot = NULL;
	BfTypeReference* explicitInterface = NULL;
	bool forceIsMethod = false;

	nextNode = mVisitorPos.GetNext();
	auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(nextNode);

	if (nameIdentifier != NULL)
	{
		mVisitorPos.MoveNext();

		bool doExplicitInterface = false;
		int endNodeIdx = -1;
		//mVisitorPos.mReadPos++;
		int nameIdentifierIdx = mVisitorPos.mReadPos;
		doExplicitInterface = IsTypeReference(nameIdentifier, BfToken_LParen, -1, &endNodeIdx);
		//mVisitorPos.mReadPos--;
		BfAstNode* endNode = mVisitorPos.Get(endNodeIdx);
		if (!doExplicitInterface)
		{
			if (auto endToken = BfNodeDynCastExact<BfTokenNode>(endNode))
			{
				if (endToken->GetToken() == BfToken_This)
				{
					indexerThisToken = endToken;
					doExplicitInterface = true;
				}
			}
			else if (auto block = BfNodeDynCastExact<BfBlock>(endNode))
			{
				doExplicitInterface = true; // Qualified property
			}
		}

		// Experimental 'more permissive' explicit interface check
		if (endNodeIdx != -1)
			doExplicitInterface = true;

		if (doExplicitInterface)
		{
			auto prevEndNode = mVisitorPos.Get(endNodeIdx - 1);
			int explicitInterfaceDotIdx = QualifiedBacktrack(prevEndNode, endNodeIdx - 1);
			if (explicitInterfaceDotIdx != -1)
			{
				explicitInterfaceDot = (BfTokenNode*)mVisitorPos.Get(explicitInterfaceDotIdx);
				mVisitorPos.mReadPos = nameIdentifierIdx;
				explicitInterfaceDot->SetToken(BfToken_Bar); // Hack to stop TypeRef parsing
				explicitInterface = CreateTypeRef(nameIdentifier);
				explicitInterfaceDot->SetToken(BfToken_Dot);

				if (explicitInterface == NULL)
					return NULL;
				mVisitorPos.mReadPos = explicitInterfaceDotIdx;
				if (indexerThisToken == NULL)
				{
					nameIdentifier = ExpectIdentifierAfter(explicitInterfaceDot);
					if (nameIdentifier == NULL)
					{
						// Looks like a method declaration
						auto methodDeclaration = mAlloc->Alloc<BfMethodDeclaration>();
						BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDeclaration->mParams, mAlloc);
						BfDeferredAstSizedArray<BfTokenNode*> commas(methodDeclaration->mCommas, mAlloc);
						if (typeRef != NULL)
							ReplaceNode(typeRef, methodDeclaration);
						else
							ReplaceNode(nameIdentifier, methodDeclaration);
						methodDeclaration->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
						MEMBER_SET(methodDeclaration, mReturnType, typeRef);
						MEMBER_SET(methodDeclaration, mExplicitInterface, explicitInterface);
						MEMBER_SET(methodDeclaration, mExplicitInterfaceDotToken, explicitInterfaceDot);
						return methodDeclaration;
					}
				}
			}
		}
	}
	else if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
	{
		if (nextToken->GetToken() == BfToken_Operator)
		{
			auto operatorDecl = mAlloc->Alloc<BfOperatorDeclaration>();
			BfDeferredAstSizedArray<BfParameterDeclaration*> params(operatorDecl->mParams, mAlloc);
			BfDeferredAstSizedArray<BfTokenNode*> commas(operatorDecl->mCommas, mAlloc);
			ReplaceNode(typeRef, operatorDecl);
			operatorDecl->mReturnType = typeRef;
			mVisitorPos.MoveNext();

			MEMBER_SET(operatorDecl, mOperatorToken, nextToken);

			bool hadFailedOperator = false;
			auto nextNode = mVisitorPos.GetNext();
			if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if ((nextToken->mToken == BfToken_Implicit) || (nextToken->mToken == BfToken_Explicit))
				{
					operatorDecl->mIsConvOperator = true;
					MEMBER_SET(operatorDecl, mOpTypeToken, nextToken);
					mVisitorPos.MoveNext();
				}
				else
				{
					operatorDecl->mBinOp = BfTokenToBinaryOp(nextToken->GetToken());
					if (operatorDecl->mBinOp != BfBinaryOp_None)
					{
						MEMBER_SET(operatorDecl, mOpTypeToken, nextToken);
						mVisitorPos.MoveNext();
					}
					else
					{
						operatorDecl->mUnaryOp = BfTokenToUnaryOp(nextToken->GetToken());
						if (operatorDecl->mUnaryOp != BfUnaryOp_None)
						{
							MEMBER_SET(operatorDecl, mOpTypeToken, nextToken);
							mVisitorPos.MoveNext();
						}
						else
						{
							operatorDecl->mAssignOp = BfTokenToAssignmentOp(nextToken->GetToken());
							if (operatorDecl->mAssignOp == BfAssignmentOp_Assign)
							{
								Fail("The assignment operator '=' cannot be overridden", nextToken);
							}

							if (operatorDecl->mAssignOp != BfAssignmentOp_None)
							{
								MEMBER_SET(operatorDecl, mOpTypeToken, nextToken);
								mVisitorPos.MoveNext();
							}
							else if (nextToken->GetToken() != BfToken_LParen)
							{
								Fail("Invalid operator type", nextToken);
								MEMBER_SET(operatorDecl, mOpTypeToken, nextToken);
								mVisitorPos.MoveNext();
							}
						}
					}
				}
			}

			if ((operatorDecl->mOpTypeToken == NULL) && (operatorDecl->mReturnType == NULL) && (!hadFailedOperator))
			{
				FailAfter("Expected operator type", operatorDecl);
				return NULL;
			}

			ParseMethod(operatorDecl, &params, &commas);

			if (params.size() == 1)
			{
				if (operatorDecl->mBinOp == BfBinaryOp_Add)
				{
					operatorDecl->mBinOp = BfBinaryOp_None;
					operatorDecl->mUnaryOp = BfUnaryOp_Positive;
				}
				else if (operatorDecl->mBinOp == BfBinaryOp_Subtract)
				{
					operatorDecl->mBinOp = BfBinaryOp_None;
					operatorDecl->mUnaryOp = BfUnaryOp_Negate;
				}
			}

			return operatorDecl;
		}
		else if (nextToken->GetToken() == BfToken_LParen)
		{
			Fail("Method return type expected", node);

			nameIdentifier = BfNodeDynCast<BfIdentifierNode>(origNode);
			if (nameIdentifier != NULL)
			{
				// Remove TypeRef
				ReplaceNode(typeRef, nameIdentifier);
				typeRef = NULL;
			}
		}
	}

	if ((nameIdentifier != NULL) || (forceIsMethod) || (indexerThisToken != NULL))
	{
		//ExtractExplicitInterfaceRef

		int blockAfterIdx = mVisitorPos.mReadPos + 1;
		BfAstNode* blockAfterPos = nameIdentifier;
		BfPropertyDeclaration* propertyDeclaration = NULL;

		if ((indexerThisToken != NULL) && (mVisitorPos.GetNext() != indexerThisToken))
		{
			indexerThisToken = NULL;
		}

		if (indexerThisToken != NULL)
		{
			auto indexerDeclaration = mAlloc->Alloc<BfIndexerDeclaration>();
			BfDeferredAstSizedArray<BfParameterDeclaration*> params(indexerDeclaration->mParams, mAlloc);
			BfDeferredAstSizedArray<BfTokenNode*> commas(indexerDeclaration->mCommas, mAlloc);
			ReplaceNode(typeRef, indexerDeclaration);
			indexerDeclaration->mTypeRef = typeRef;
			MEMBER_SET(indexerDeclaration, mThisToken, indexerThisToken);
			mVisitorPos.MoveNext();

			if (explicitInterface != NULL)
			{
				MEMBER_SET(indexerDeclaration, mExplicitInterface, explicitInterface);
				MEMBER_SET(indexerDeclaration, mExplicitInterfaceDotToken, explicitInterfaceDot);
				//mVisitorPos.mReadPos = endNodeIdx;
			}

			auto openToken = ExpectTokenAfter(indexerDeclaration, BfToken_LBracket);
			if (openToken == NULL)
				return indexerDeclaration;
			MEMBER_SET(indexerDeclaration, mOpenBracket, openToken);
			auto endToken = ParseMethodParams(indexerDeclaration, &params, &commas, BfToken_RBracket, true);
			if (endToken == NULL)
				return indexerDeclaration;
			MEMBER_SET(indexerDeclaration, mCloseBracket, endToken);
			propertyDeclaration = indexerDeclaration;
			blockAfterPos = propertyDeclaration;
			mVisitorPos.MoveNext();

			blockAfterIdx = mVisitorPos.mReadPos + 1;
		}
		else
		{
			blockAfterPos = nameIdentifier;
		}

		nextNode = mVisitorPos.Get(blockAfterIdx);
		auto block = BfNodeDynCast<BfBlock>(nextNode);
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);

		bool isExprBodyProp = (tokenNode != NULL) && ((tokenNode->mToken == BfToken_FatArrow) || (tokenNode->mToken == BfToken_Mut));
		// Property.
		//  If we don't have a token afterwards then still treat it as a property for autocomplete purposes
		if ((typeRef != NULL) &&
			((block != NULL) || (tokenNode == NULL) || (isExprBodyProp)))
		{
			if (propertyDeclaration == NULL)
			{
				if ((block == NULL) && (!isExprBodyProp))
				{
					auto propDecl = mAlloc->Alloc<BfPropertyDeclaration>();
					ReplaceNode(typeRef, propDecl);
					propDecl->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
					propDecl->mTypeRef = typeRef;

					if (explicitInterface != NULL)
					{
						MEMBER_SET(propDecl, mExplicitInterface, explicitInterface);
						MEMBER_SET(propDecl, mExplicitInterfaceDotToken, explicitInterfaceDot);
					}

					// Don't set the name identifier, this could be bogus
					//mVisitorPos.mReadPos--;

					// WHY did we want to not set this?
					// If we don't, then typing a new method name will end up treating the name node as a typeRef
					//  which can autocomplete incorrectly
					MEMBER_SET(propDecl, mNameNode, nameIdentifier);

					return propDecl;
				}

				mVisitorPos.mReadPos = blockAfterIdx;
				propertyDeclaration = mAlloc->Alloc<BfPropertyDeclaration>();
				ReplaceNode(typeRef, propertyDeclaration);
				propertyDeclaration->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
				propertyDeclaration->mTypeRef = typeRef;

				if (explicitInterface != NULL)
				{
					MEMBER_SET(propertyDeclaration, mExplicitInterface, explicitInterface);
					MEMBER_SET(propertyDeclaration, mExplicitInterfaceDotToken, explicitInterfaceDot);
				}

				MEMBER_SET(propertyDeclaration, mNameNode, nameIdentifier);
				//mVisitorPos.MoveNext();
			}
			else
				mVisitorPos.mReadPos = blockAfterIdx;

			if (block != NULL)
			{
				MEMBER_SET(propertyDeclaration, mDefinitionBlock, block);
				ReadPropertyBlock(propertyDeclaration, block);

				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if (tokenNode->mToken == BfToken_AssignEquals)
					{
						MEMBER_SET(propertyDeclaration, mEqualsNode, tokenNode);
						mVisitorPos.MoveNext();
						auto initExpr = CreateExpressionAfter(propertyDeclaration);
						if (initExpr != NULL)
						{
							MEMBER_SET(propertyDeclaration, mInitializer, initExpr);
						}
					}
				}

				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if (tokenNode->mToken == BfToken_Tilde)
					{
						auto fieldDtor = CreateFieldDtorDeclaration(propertyDeclaration);
						MEMBER_SET(propertyDeclaration, mFieldDtor, fieldDtor);
					}
				}
			}
			else if (isExprBodyProp)
			{
				BfDeferredAstSizedArray<BfPropertyMethodDeclaration*> methods(propertyDeclaration->mMethods, mAlloc);

				auto propertyBodyExpr = mAlloc->Alloc<BfPropertyBodyExpression>();
				ReplaceNode(tokenNode, propertyBodyExpr);

				BfTokenNode* mutSpecifier = NULL;

				if (tokenNode->mToken == BfToken_Mut)
				{
					MEMBER_SET(propertyBodyExpr, mMutSpecifier, tokenNode);
					tokenNode = ExpectTokenAfter(tokenNode, BfToken_FatArrow);
				}

				if (tokenNode != NULL)
				{
					MEMBER_SET(propertyBodyExpr, mFatTokenArrow, tokenNode);

					auto method = mAlloc->Alloc<BfPropertyMethodDeclaration>();
					method->mPropertyDeclaration = propertyDeclaration;
					method->mNameNode = propertyDeclaration->mNameNode;

					auto expr = CreateExpressionAfter(tokenNode);
					if (expr != NULL)
					{
						MEMBER_SET(method, mBody, expr);
						propertyDeclaration->SetSrcEnd(expr->GetSrcEnd());
					}

					methods.Add(method);
				}

				MEMBER_SET(propertyDeclaration, mDefinitionBlock, propertyBodyExpr);
			}

			return propertyDeclaration;
		}
		else if (propertyDeclaration != NULL)
		{
			// Failure case
			block = ExpectBlockAfter(blockAfterPos);
			BF_ASSERT(block == NULL);
			return propertyDeclaration;
		}

		//nextNode = mVisitorPos.Get(mVisitorPos.mReadPos + 2);
		/*if (tokenNode == NULL)
		{
		mVisitorPos.MoveNext();
		ExpectTokenAfter(nameIdentifier, BfToken_Semicolon);

		// Just 'eat' the typeRef by stuffing it into a property declaration
		auto fieldDecl = mAlloc->Alloc<BfPropertyDeclaration>();
		ReplaceNode(typeRef, fieldDecl);
		fieldDecl->mTypeRef = typeRef;

		return fieldDecl;
		}*/

		if (tokenNode == NULL)
		{
			auto fieldDecl = mAlloc->Alloc<BfPropertyDeclaration>();
			ReplaceNode(nameIdentifier, fieldDecl);
			fieldDecl->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
			fieldDecl->mNameNode = nameIdentifier;
			return fieldDecl;
		}

		BfToken token = tokenNode->GetToken();
		if ((token == BfToken_LParen) ||
			(token == BfToken_LChevron))
		{
			if (token == BfToken_LChevron)
			{
				bool isTypeRef = IsTypeReference(nameIdentifier, BfToken_LParen);
				if (!isTypeRef)
				{
					bool onNewLine = false;
					auto src = nameIdentifier->GetSourceData();
					int srcStart = nameIdentifier->GetSrcStart();
					for (int srcIdx = typeRef->GetSrcEnd(); srcIdx < srcStart; srcIdx++)
						if (src->mSrc[srcIdx] == '\n')
							onNewLine = false;

					if (onNewLine)
					{
						// Actually it looks like a partially formed type declaration followed by a method
						//  which returns a generic type
						FailAfter("Name expected", typeRef);

						auto fieldDecl = mAlloc->Alloc<BfFieldDeclaration>();
						ReplaceNode(typeRef, fieldDecl);
						fieldDecl->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
						fieldDecl->mTypeRef = typeRef;
						return fieldDecl;
					}
				}
			}

			// Looks like a method declaration
			auto methodDeclaration = mAlloc->Alloc<BfMethodDeclaration>();
			BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDeclaration->mParams, mAlloc);
			BfDeferredAstSizedArray<BfTokenNode*> commas(methodDeclaration->mCommas, mAlloc);
			if (typeRef != NULL)
				ReplaceNode(typeRef, methodDeclaration);
			else
				ReplaceNode(nameIdentifier, methodDeclaration);
			methodDeclaration->mDocumentation = FindDocumentation(mTypeMemberNodeStart);

			if (explicitInterface != NULL)
			{
				MEMBER_SET(methodDeclaration, mExplicitInterface, explicitInterface);
				MEMBER_SET(methodDeclaration, mExplicitInterfaceDotToken, explicitInterfaceDot);
			}

			if (token == BfToken_LChevron)
			{
				auto genericParams = CreateGenericParamsDeclaration(tokenNode);
				if (genericParams != NULL)
				{
					MEMBER_SET(methodDeclaration, mGenericParams, genericParams);
				}
			}

			methodDeclaration->mReturnType = typeRef;
			MEMBER_SET_CHECKED(methodDeclaration, mNameNode, nameIdentifier);
			mCurMethodDecl = methodDeclaration;
			ParseMethod(methodDeclaration, &params, &commas);
			mCurMethodDecl = NULL;
			return methodDeclaration;
		}

		return CreateFieldDeclaration(tokenNode, typeRef, nameIdentifier, NULL);
	}

	FailAfter("Member name expected", node);

	// Same fail case as "expected member declaration"
	auto fieldDecl = mAlloc->Alloc<BfPropertyDeclaration>();
	ReplaceNode(typeRef, fieldDecl);
	fieldDecl->mDocumentation = FindDocumentation(mTypeMemberNodeStart);
	fieldDecl->mTypeRef = typeRef;
	return fieldDecl;
}

BfInvocationExpression* BfReducer::CreateInvocationExpression(BfAstNode* target, CreateExprFlags createExprFlags)
{
	auto tokenNode = ExpectTokenAfter(target, BfToken_LParen, BfToken_LChevron, BfToken_Bang);

	auto invocationExpr = mAlloc->Alloc<BfInvocationExpression>();
	BfDeferredAstSizedArray<BfExpression*> arguments(invocationExpr->mArguments, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(invocationExpr->mCommas, mAlloc);
	invocationExpr->mTarget = target;
	ReplaceNode(target, invocationExpr);

	if (tokenNode == NULL)
		return invocationExpr;

	if (tokenNode->GetToken() == BfToken_Bang)
	{
		MoveNode(tokenNode, invocationExpr);
		if (tokenNode->GetSrcEnd() == invocationExpr->mTarget->GetSrcEnd() + 1)
		{
			BfAstNode* target = invocationExpr->mTarget;

			while (true)
			{
				if (auto qualifiedNameNode = BfNodeDynCast<BfQualifiedNameNode>(target))
					target = qualifiedNameNode->mRight;
				else if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(target))
					target = memberRefExpr->mMemberName;
				else if (auto attribIdentifierNode = BfNodeDynCast<BfAttributedIdentifierNode>(target))
					target = attribIdentifierNode->mIdentifier;
				else
					break;
			}

			if (target != NULL)
			{
				BF_ASSERT(tokenNode->GetSrcEnd() == target->GetSrcEnd() + 1);
				target->SetSrcEnd(tokenNode->GetSrcEnd());
				invocationExpr->mTarget->SetSrcEnd(tokenNode->GetSrcEnd());

				if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if (nextToken->GetToken() == BfToken_Colon)
					{
						auto scopedInvocationTarget = CreateScopedInvocationTarget(invocationExpr->mTarget, nextToken);
						invocationExpr->SetSrcEnd(scopedInvocationTarget->GetSrcEnd());
					}
				}
			}
		}
		else
			Fail("No space allowed before token", tokenNode);
		tokenNode = ExpectTokenAfter(invocationExpr, BfToken_LParen, BfToken_LChevron);
		if (tokenNode == NULL)
			return invocationExpr;
	}

	if (tokenNode->GetToken() == BfToken_LChevron)
	{
		auto genericParamsDecl = CreateGenericArguments(tokenNode, true);
		MEMBER_SET_CHECKED(invocationExpr, mGenericArgs, genericParamsDecl);
		tokenNode = ExpectTokenAfter(invocationExpr, BfToken_LParen);
	}

	MEMBER_SET_CHECKED(invocationExpr, mOpenParen, tokenNode);

	tokenNode = ReadArguments(invocationExpr, invocationExpr, &arguments, &commas, BfToken_RParen, true);
	if (tokenNode != NULL)
	{
		MEMBER_SET(invocationExpr, mCloseParen, tokenNode);
	}

	return invocationExpr;
}

BfInitializerExpression* BfReducer::TryCreateInitializerExpression(BfAstNode* target)
{
	auto block = BfNodeDynCast<BfBlock>(mVisitorPos.GetNext());
	if (block == NULL)
		return NULL;

	mVisitorPos.MoveNext();

	auto initializerExpr = mAlloc->Alloc<BfInitializerExpression>();
	ReplaceNode(target, initializerExpr);
	initializerExpr->mTarget = target;
	MEMBER_SET(initializerExpr, mOpenBrace, block->mOpenBrace);

	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));

	bool isDone = !mVisitorPos.MoveNext();

	BfDeferredAstNodeSizedArray<BfExpression*> values(initializerExpr, initializerExpr->mValues, mAlloc);
	BfDeferredAstNodeSizedArray<BfTokenNode*> commas(initializerExpr, initializerExpr->mCommas, mAlloc);

	BfAstNode* nextNode = NULL;
	while (!isDone)
	{
		BfAstNode* node = mVisitorPos.GetCurrent();
		initializerExpr->mSrcEnd = node->mSrcEnd;

		auto expr = CreateExpression(node);
		isDone = !mVisitorPos.MoveNext();
		if (expr != NULL)
			values.Add(expr);
		else
			AddErrorNode(node);

		if (!isDone)
		{
			bool foundComma = false;

			node = mVisitorPos.GetCurrent();
			if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
			{
				if (tokenNode->mToken == BfToken_Comma)
				{
					foundComma = true;
					commas.Add(tokenNode);
					isDone = !mVisitorPos.MoveNext();
				}
			}
		}
	}

	mVisitorPos.Trim();

	if (block->mCloseBrace != NULL)
		MEMBER_SET(initializerExpr, mCloseBrace, block->mCloseBrace);

	return initializerExpr;
}

BfDelegateBindExpression* BfReducer::CreateDelegateBindExpression(BfAstNode* allocNode)
{
	auto delegateBindExpr = mAlloc->Alloc<BfDelegateBindExpression>();
	ReplaceNode(allocNode, delegateBindExpr);
	MEMBER_SET(delegateBindExpr, mNewToken, allocNode);
	auto tokenNode = ExpectTokenAfter(delegateBindExpr, BfToken_FatArrow);
	MEMBER_SET_CHECKED(delegateBindExpr, mFatArrowToken, tokenNode);
	auto expr = CreateExpressionAfter(delegateBindExpr);
	MEMBER_SET_CHECKED(delegateBindExpr, mTarget, expr);

	auto nextNode = mVisitorPos.GetNext();
	if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
	{
		if (tokenNode->GetToken() == BfToken_LChevron)
		{
			mVisitorPos.MoveNext();
			auto genericParamsDecl = CreateGenericArguments(tokenNode);
			MEMBER_SET_CHECKED(delegateBindExpr, mGenericArgs, genericParamsDecl);
		}
	}

	return delegateBindExpr;
}

BfLambdaBindExpression* BfReducer::CreateLambdaBindExpression(BfAstNode* allocNode, BfTokenNode* parenToken)
{
	auto lambdaBindExpr = mAlloc->Alloc<BfLambdaBindExpression>();
	BfDeferredAstSizedArray<BfIdentifierNode*> params(lambdaBindExpr->mParams, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(lambdaBindExpr->mCommas, mAlloc);
	BfTokenNode* tokenNode;
	if (allocNode != NULL)
	{
		ReplaceNode(allocNode, lambdaBindExpr);
		MEMBER_SET(lambdaBindExpr, mNewToken, allocNode);
		tokenNode = ExpectTokenAfter(lambdaBindExpr, BfToken_LParen, BfToken_LBracket);
	}
	else
	{
		ReplaceNode(parenToken, lambdaBindExpr);
		tokenNode = parenToken;
	}

	if (tokenNode == NULL)
		return lambdaBindExpr;

	MEMBER_SET_CHECKED(lambdaBindExpr, mOpenParen, tokenNode);

	for (int paramIdx = 0; true; paramIdx++)
	{
		bool isRParen = false;
		auto nextNode = mVisitorPos.GetNext();
		if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
			isRParen = tokenNode->GetToken() == BfToken_RParen;
		if (!isRParen)
		{
			auto nameIdentifier = ExpectIdentifierAfter(lambdaBindExpr, "parameter name");
			if (nameIdentifier == NULL)
				return lambdaBindExpr;
			MoveNode(nameIdentifier, lambdaBindExpr);
			params.push_back(nameIdentifier);
		}

		tokenNode = ExpectTokenAfter(lambdaBindExpr, BfToken_Comma, BfToken_RParen);
		if (tokenNode == NULL)
			return lambdaBindExpr;
		if (tokenNode->GetToken() == BfToken_RParen)
		{
			MEMBER_SET(lambdaBindExpr, mCloseParen, tokenNode);
			break;
		}
		MoveNode(tokenNode, lambdaBindExpr);
		commas.push_back(tokenNode);
	}

	tokenNode = ExpectTokenAfter(lambdaBindExpr, BfToken_FatArrow);
	MEMBER_SET_CHECKED(lambdaBindExpr, mFatArrowToken, tokenNode);

	auto nextNode = mVisitorPos.GetNext();
	auto block = BfNodeDynCast<BfBlock>(nextNode);
	if (block != NULL)
	{
		HandleBlock(block, true);
		mVisitorPos.MoveNext();
		MEMBER_SET_CHECKED(lambdaBindExpr, mBody, block);
	}
	else
	{
		auto expr = CreateExpressionAfter(lambdaBindExpr);
		MEMBER_SET_CHECKED(lambdaBindExpr, mBody, expr);
	}

	auto lambdaDtor = CreateFieldDtorDeclaration(lambdaBindExpr);
	if (lambdaDtor != NULL)
	{
		if ((mIsFieldInitializer) && (!mInParenExpr))
		{
			Fail("Ambiguous destructor: could be field destructor or lambda destructor. Disambiguate with parentheses, either '(lambda) ~ fieldDtor' or '(lambda ~ lambdaDtor)'", lambdaBindExpr);
		}

		lambdaBindExpr->mDtor = lambdaDtor;
	}

	return lambdaBindExpr;
}

BfCollectionInitializerExpression* BfReducer::CreateCollectionInitializerExpression(BfBlock* block)
{
	auto arrayInitializerExpression = mAlloc->Alloc<BfCollectionInitializerExpression>();
	BfDeferredAstSizedArray<BfExpression*> values(arrayInitializerExpression->mValues, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(arrayInitializerExpression->mCommas, mAlloc);
	ReplaceNode(block, arrayInitializerExpression);
	MEMBER_SET(arrayInitializerExpression, mOpenBrace, block->mOpenBrace);
	if (block->mCloseBrace != NULL)
		MEMBER_SET(arrayInitializerExpression, mCloseBrace, block->mCloseBrace);
	block->mOpenBrace = NULL;
	block->mCloseBrace = NULL;

	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));
	bool isDone = !mVisitorPos.MoveNext();
	while (true)
	{
		auto head = mVisitorPos.GetCurrent();
		if (head == NULL)
			break;

		BfExpression* expression;
		if (auto innerBlock = BfNodeDynCast<BfBlock>(head))
			expression = CreateCollectionInitializerExpression(innerBlock);
		else
			expression = CreateExpression(head);
		if (expression == NULL)
			break;
		auto nextNode = mVisitorPos.GetNext();
		bool atEnd = nextNode == NULL;
		auto tokenNode = atEnd ? NULL : ExpectTokenAfter(expression, BfToken_Comma, BfToken_RBrace);

		MoveNode(expression, arrayInitializerExpression);
		values.push_back(expression);

		if ((!atEnd) && (tokenNode == NULL))
			break;

		if (atEnd)
			break;
		MoveNode(tokenNode, arrayInitializerExpression);
		commas.push_back(tokenNode);

		isDone = !mVisitorPos.MoveNext();
	}
	return arrayInitializerExpression;
}

BfCollectionInitializerExpression * BfReducer::CreateCollectionInitializerExpression(BfTokenNode* openToken)
{
	auto arrayInitializerExpression = mAlloc->Alloc<BfCollectionInitializerExpression>();
	BfDeferredAstSizedArray<BfExpression*> values(arrayInitializerExpression->mValues, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(arrayInitializerExpression->mCommas, mAlloc);
	ReplaceNode(openToken, arrayInitializerExpression);
	MEMBER_SET(arrayInitializerExpression, mOpenBrace, openToken);

	bool isDone = !mVisitorPos.MoveNext();
	while (true)
	{
		auto head = mVisitorPos.GetCurrent();
		if (head == NULL)
			break;

		BfExpression* expression;
		if (auto innerBlock = BfNodeDynCast<BfBlock>(head))
			expression = CreateCollectionInitializerExpression(innerBlock);
		else
		{
			if (auto tokenNode = BfNodeDynCast<BfTokenNode>(head))
			{
				if (tokenNode->mToken == BfToken_RParen)
				{
					MEMBER_SET(arrayInitializerExpression, mCloseBrace, tokenNode);
					return arrayInitializerExpression;
				}
			}

			expression = CreateExpression(head);
		}
		if (expression == NULL)
			break;
		auto nextNode = mVisitorPos.GetNext();
		bool atEnd = nextNode == NULL;
		auto tokenNode = atEnd ? NULL : ExpectTokenAfter(expression, BfToken_Comma, BfToken_RParen);

		MoveNode(expression, arrayInitializerExpression);
		values.push_back(expression);

		if ((!atEnd) && (tokenNode == NULL))
			break;

		if (atEnd)
			break;
		MoveNode(tokenNode, arrayInitializerExpression);
		if (tokenNode->GetToken() == BfToken_RParen)
		{
			MEMBER_SET(arrayInitializerExpression, mCloseBrace, tokenNode);
			break;
		}

		commas.push_back(tokenNode);

		isDone = !mVisitorPos.MoveNext();
	}
	return arrayInitializerExpression;
}

BfScopedInvocationTarget* BfReducer::CreateScopedInvocationTarget(BfAstNode*& targetRef, BfTokenNode* colonToken)
{
	auto scopedInvocationTarget = mAlloc->Alloc<BfScopedInvocationTarget>();
	ReplaceNode(targetRef, scopedInvocationTarget);
	scopedInvocationTarget->mTarget = targetRef;
	targetRef = scopedInvocationTarget;

	if (colonToken == NULL)
		return scopedInvocationTarget;

	MEMBER_SET(scopedInvocationTarget, mColonToken, colonToken);

	mVisitorPos.MoveNext();
	if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
	{
		if ((nextToken->GetToken() == BfToken_Colon) || (nextToken->GetToken() == BfToken_Mixin))
		{
			MEMBER_SET(scopedInvocationTarget, mScopeName, nextToken);
			mVisitorPos.MoveNext();
		}
	}
	else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
	{
		MEMBER_SET(scopedInvocationTarget, mScopeName, identifier);
		mVisitorPos.MoveNext();
	}

	if (scopedInvocationTarget->mScopeName == NULL)
	{
		FailAfter("Expected scope name", scopedInvocationTarget);
	}

	return scopedInvocationTarget;
}

bool BfReducer::SetProtection(BfAstNode* parentNode, BfAstNode*& protectionNodeRef, BfTokenNode* tokenNode)
{
	bool failed = false;

	if (protectionNodeRef != NULL)
	{
		if (auto prevToken = BfNodeDynCast<BfTokenNode>(protectionNodeRef))
		{
			if (((prevToken->mToken == BfToken_Protected) && (tokenNode->mToken == BfToken_Internal)) ||
				((prevToken->mToken == BfToken_Internal) && (tokenNode->mToken == BfToken_Protected)))
			{
				auto tokenPair = mAlloc->Alloc<BfTokenPairNode>();

				if (prevToken->mSrcStart < tokenNode->mSrcStart)
				{
					ReplaceNode(prevToken, tokenPair);
					MEMBER_SET(tokenPair, mLeft, prevToken);
					MEMBER_SET(tokenPair, mRight, tokenNode);
				}
				else
				{
					ReplaceNode(tokenNode, tokenPair);
					MEMBER_SET(tokenPair, mLeft, tokenNode);
					MEMBER_SET(tokenPair, mRight, prevToken);
				}

				protectionNodeRef = tokenPair;
				if (parentNode != NULL)
					MoveNode(tokenPair, parentNode);
				return true;
			}
		}

		Fail("Protection already specified", protectionNodeRef);
	}
	protectionNodeRef = tokenNode;
	if (parentNode != NULL)
		MoveNode(tokenNode, parentNode);

	return !failed;
}

BfAstNode* BfReducer::CreateAllocNode(BfTokenNode* allocToken)
{
	if (allocToken->GetToken() == BfToken_Scope)
	{
		auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
		if (nextToken == NULL)
			return allocToken;
		if ((nextToken->mToken != BfToken_Colon) && (nextToken->mToken != BfToken_LBracket))
			return allocToken;

		auto scopeNode = mAlloc->Alloc<BfScopeNode>();
		ReplaceNode(allocToken, scopeNode);
		scopeNode->mScopeToken = allocToken;

		if (nextToken->mToken == BfToken_Colon)
		{
			MEMBER_SET(scopeNode, mColonToken, nextToken);
			mVisitorPos.MoveNext();

			if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			{
				if ((nextToken->GetToken() == BfToken_Colon) || (nextToken->GetToken() == BfToken_Mixin))
				{
					MEMBER_SET(scopeNode, mTargetNode, nextToken);
					mVisitorPos.MoveNext();
				}
			}
			else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
			{
				MEMBER_SET(scopeNode, mTargetNode, identifier);
				mVisitorPos.MoveNext();
			}

			if (scopeNode->mTargetNode == NULL)
			{
				FailAfter("Expected scope name", scopeNode);
			}
		}

		nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
		if (nextToken == NULL)
			return scopeNode;
		if (nextToken->mToken != BfToken_LBracket)
			return scopeNode;

		mVisitorPos.MoveNext();
		auto attributeDirective = CreateAttributeDirective(nextToken);
		MEMBER_SET(scopeNode, mAttributes, attributeDirective);

		return scopeNode;
	}

	if (allocToken->GetToken() == BfToken_New)
	{
		auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());

		if (nextToken == NULL)
			return allocToken;
		if ((nextToken->mToken != BfToken_Colon) && (nextToken->mToken != BfToken_LBracket))
			return allocToken;

		auto newNode = mAlloc->Alloc<BfNewNode>();
		ReplaceNode(allocToken, newNode);
		newNode->mNewToken = allocToken;

		if (nextToken->mToken == BfToken_Colon)
		{
			MEMBER_SET(newNode, mColonToken, nextToken);
			mVisitorPos.MoveNext();

			if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			{
				BfToken nextToken = nextTokenNode->GetToken();
				if ((nextToken == BfToken_LParen) || (nextToken == BfToken_This) || (nextToken == BfToken_Null))
				{
					auto allocExpr = CreateExpressionAfter(newNode, (CreateExprFlags)(CreateExprFlags_NoCast | CreateExprFlags_ExitOnParenExpr));
					if (allocExpr != NULL)
					{
						MEMBER_SET(newNode, mAllocNode, allocExpr);
					}
				}
			}
			else
			{
				int endNodeIdx = -1;
				int nodeIdx = mVisitorPos.mReadPos;
				mVisitorPos.MoveNext();
				if (IsTypeReference(mVisitorPos.GetCurrent(), BfToken_Bang, -1, &endNodeIdx))
				{
					if (auto bangToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(endNodeIdx)))
					{
						if (auto prevIdentifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.Get(endNodeIdx - 1)))
						{
							if (bangToken->GetSrcStart() == prevIdentifier->GetSrcEnd())
							{
								if (endNodeIdx == nodeIdx + 2)
								{
									MEMBER_SET(newNode, mAllocNode, prevIdentifier);
									prevIdentifier->SetSrcEnd(bangToken->GetSrcEnd());
								}
								else
								{
									mVisitorPos.mReadPos = nodeIdx + 1;
									BfExpression* expr = CreateExpression(mVisitorPos.Get(nodeIdx + 1), CreateExprFlags_ExitOnBang);
									expr->SetSrcEnd(bangToken->GetSrcEnd());
									MEMBER_SET(newNode, mAllocNode, expr);

									BfAstNode* memberNode = expr;
									if (auto memberRefExpr = BfNodeDynCast<BfMemberReferenceExpression>(expr))
									{
										memberNode = memberRefExpr->mMemberName;
									}
									else if (auto qualifiedIdentifier = BfNodeDynCast<BfQualifiedNameNode>(expr))
									{
										memberNode = qualifiedIdentifier->mRight;
									}
									if (memberNode != NULL)
										memberNode->SetSrcEnd(bangToken->GetSrcEnd());
								}
								newNode->SetSrcEnd(bangToken->GetSrcEnd());
								mVisitorPos.mReadPos = endNodeIdx;

								BfTokenNode* colonToken = NULL;
								if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(endNodeIdx + 1)))
								{
									if (nextToken->GetToken() == BfToken_Colon)
										colonToken = nextToken;
								}

								auto scopedInvocationTarget = CreateScopedInvocationTarget(newNode->mAllocNode, colonToken);
								newNode->SetSrcEnd(scopedInvocationTarget->GetSrcEnd());
							}
						}
					}
				}

				if (newNode->mAllocNode == NULL)
				{
					mVisitorPos.mReadPos = nodeIdx;
					if (auto identifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext()))
					{
						MEMBER_SET(newNode, mAllocNode, identifier);
						mVisitorPos.MoveNext();
					}
				}
			}

			if (newNode->mAllocNode == NULL)
			{
				FailAfter("Expected allocator expression", newNode);
			}
		}

		nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
		if (nextToken == NULL)
			return newNode;
		if (nextToken->mToken != BfToken_LBracket)
			return newNode;

		mVisitorPos.MoveNext();
		auto attributeDirective = CreateAttributeDirective(nextToken);
		MEMBER_SET(newNode, mAttributes, attributeDirective);

		return newNode;
	}

	return allocToken;
}

BfObjectCreateExpression* BfReducer::CreateObjectCreateExpression(BfAstNode* allocNode)
{
	auto objectCreateExpr = mAlloc->Alloc<BfObjectCreateExpression>();
	BfDeferredAstSizedArray<BfExpression*> arguments(objectCreateExpr->mArguments, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(objectCreateExpr->mCommas, mAlloc);

	ReplaceNode(allocNode, objectCreateExpr);
	MEMBER_SET(objectCreateExpr, mNewNode, allocNode);

	auto nextNode = mVisitorPos.GetNext();

	BfTokenNode* tokenNode;

	// Why did we want this syntax?
	// 	if (tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
	// 	{
	// 		if (tokenNode->GetToken() == BfToken_Star)
	// 		{
	// 			MEMBER_SET(objectCreateExpr, mStarToken, tokenNode);
	// 			mVisitorPos.MoveNext();
	// 		}
	// 	}

	auto typeRef = CreateTypeRefAfter(objectCreateExpr);
	if (typeRef == NULL)
		return objectCreateExpr;

	bool isArray = false;

	if (auto ptrType = BfNodeDynCast<BfPointerTypeRef>(typeRef))
	{
		if (auto arrayType = BfNodeDynCast<BfArrayTypeRef>(ptrType->mElementType))
		{
			MEMBER_SET(objectCreateExpr, mStarToken, ptrType->mStarNode);
			typeRef = ptrType->mElementType;
			isArray = true;
		}
	}
	else if (auto arrayType = BfNodeDynCast<BfArrayTypeRef>(typeRef))
	{
		isArray = true;
	}

	MEMBER_SET(objectCreateExpr, mTypeRef, typeRef);

	if (auto block = BfNodeDynCast<BfBlock>(mVisitorPos.GetNext()))
	{
		mPassInstance->Warn(0, "Expected '('", block->mOpenBrace);

		mVisitorPos.MoveNext();
		MEMBER_SET(objectCreateExpr, mOpenToken, block->mOpenBrace);
		//
		{
			SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));
			ReadArguments(objectCreateExpr, objectCreateExpr, &arguments, &commas, BfToken_None, true);

			while (true)
			{
				auto nextNode = mVisitorPos.GetNext();
				if (nextNode == NULL)
					break;
				AddErrorNode(nextNode);
				mVisitorPos.MoveNext();
			}
		}
		if (block->mCloseBrace != NULL)
			MEMBER_SET(objectCreateExpr, mCloseToken, block->mCloseBrace);
		objectCreateExpr->mSrcEnd = block->mSrcEnd;
		return objectCreateExpr;
	}

	if (isArray)
	{
		tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());

		if (tokenNode == NULL)
			return objectCreateExpr;
		if (tokenNode->GetToken() != BfToken_LParen)
			return objectCreateExpr;

		mVisitorPos.MoveNext();
	}
	else
	{
		// Note- if there WERE an LBracket here then we'd have an 'isArray' case. We pass this in here for
		// error display purposes
		tokenNode = ExpectTokenAfter(objectCreateExpr, BfToken_LParen, BfToken_LBracket);
		if (tokenNode == NULL)
			return objectCreateExpr;
	}

	MEMBER_SET(objectCreateExpr, mOpenToken, tokenNode);

	BfToken endToken = (BfToken)(tokenNode->GetToken() + 1);
	tokenNode = ReadArguments(objectCreateExpr, objectCreateExpr, &arguments, &commas, BfToken_RParen, true);
	if (tokenNode == NULL)
		return objectCreateExpr;
	MEMBER_SET(objectCreateExpr, mCloseToken, tokenNode);

	return objectCreateExpr;
}

BfExpression* BfReducer::CreateIndexerExpression(BfExpression* target)
{
	auto tokenNode = ExpectTokenAfter(target, BfToken_LBracket, BfToken_QuestionLBracket);

	auto indexerExpr = mAlloc->Alloc<BfIndexerExpression>();
	BfDeferredAstSizedArray<BfExpression*> arguments(indexerExpr->mArguments, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(indexerExpr->mCommas, mAlloc);
	indexerExpr->mTarget = target;
	ReplaceNode(target, indexerExpr);

	indexerExpr->mOpenBracket = tokenNode;
	MoveNode(indexerExpr->mOpenBracket, indexerExpr);

	BfAstNode* argAfterNode = indexerExpr;
	BfAttributeDirective* attributeDirective = NULL;
	if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
	{
		if (tokenNode->mToken == BfToken_LBracket)
		{
			mVisitorPos.MoveNext();
			attributeDirective = CreateAttributeDirective(tokenNode);
			argAfterNode = attributeDirective;
		}
	}

	indexerExpr->mCloseBracket = ReadArguments(indexerExpr, argAfterNode, &arguments, &commas, BfToken_RBracket, false);

	if (attributeDirective != NULL)
	{
		BfAttributedExpression* attribExpr = mAlloc->Alloc<BfAttributedExpression>();
		ReplaceNode(indexerExpr, attribExpr);
		attribExpr->mExpression = indexerExpr;
		MEMBER_SET(attribExpr, mAttributes, attributeDirective);
		return attribExpr;
	}

	return indexerExpr;
}

BfMemberReferenceExpression* BfReducer::CreateMemberReferenceExpression(BfAstNode* target)
{
	auto tokenNode = ExpectTokenAfter(target, BfToken_Dot, BfToken_DotDot, BfToken_QuestionDot, BfToken_Arrow);

	auto memberReferenceExpr = mAlloc->Alloc<BfMemberReferenceExpression>();
	if (target != NULL)
	{
		memberReferenceExpr->mTarget = target;
		ReplaceNode(target, memberReferenceExpr);
	}

	MEMBER_SET_CHECKED(memberReferenceExpr, mDotToken, tokenNode);

	auto nextNode = mVisitorPos.GetNext();
	if (auto literalExpr = BfNodeDynCast<BfLiteralExpression>(nextNode))
	{
		// Special case for "tuple.0" type references
		if (literalExpr->mValue.mTypeCode == BfTypeCode_IntUnknown)
		{
			MEMBER_SET(memberReferenceExpr, mMemberName, literalExpr);
			mVisitorPos.MoveNext();
			return memberReferenceExpr;
		}
	}

	if ((tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext())))
	{
		if (tokenNode->GetToken() == BfToken_LBracket)
		{
			mVisitorPos.MoveNext();
			auto attrIdentifier = CreateAttributedExpression(tokenNode, true);
			if (attrIdentifier != NULL)
			{
				MEMBER_SET(memberReferenceExpr, mMemberName, attrIdentifier);
			}
		}
	}

	if (memberReferenceExpr->mMemberName == NULL)
	{
		auto memberName = ExpectIdentifierAfter(memberReferenceExpr);
		if (memberName != NULL)
		{
			MEMBER_SET(memberReferenceExpr, mMemberName, memberName);
		}
	}

	return memberReferenceExpr;
}

BfTupleExpression* BfReducer::CreateTupleExpression(BfTokenNode* node, BfExpression* innerExpr)
{
	auto tupleExpr = mAlloc->Alloc<BfTupleExpression>();
	ReplaceNode(node, tupleExpr);
	tupleExpr->mOpenParen = node;

	BfDeferredAstSizedArray<BfTupleNameNode*> names(tupleExpr->mNames, mAlloc);
	BfDeferredAstSizedArray<BfExpression*> values(tupleExpr->mValues, mAlloc);
	BfDeferredAstSizedArray<BfTokenNode*> commas(tupleExpr->mCommas, mAlloc);

	while (true)
	{
		BfTokenNode* closeParenToken;

		if (innerExpr == NULL)
		{
			bool skipExpr = false;
			if (values.size() != 0)
			{
				if (auto nextToken = BfNodeDynCastExact<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if (nextToken->GetToken() == BfToken_RParen)
					{
						// Unterminated - default initialize
						skipExpr = true;
					}
				}
			}

			if (!skipExpr)
				innerExpr = CreateExpressionAfter(tupleExpr, CreateExprFlags_PermissiveVariableDecl);
		}
		if (innerExpr == NULL)
		{
			// Failed, but can we pull in the closing rparen?
			auto nextNode = mVisitorPos.GetNext();
			if ((closeParenToken = BfNodeDynCast<BfTokenNode>(nextNode)))
			{
				if (closeParenToken->GetToken() == BfToken_RParen)
				{
					MEMBER_SET(tupleExpr, mCloseParen, closeParenToken);
					mVisitorPos.MoveNext();
				}
			}

			break;
		}

		if (innerExpr->IsA<BfIdentifierNode>())
			closeParenToken = ExpectTokenAfter(innerExpr, BfToken_RParen, BfToken_Comma, BfToken_Colon);
		else
			closeParenToken = ExpectTokenAfter(innerExpr, BfToken_RParen, BfToken_Comma);

		if (closeParenToken == NULL)
		{
			values.push_back(innerExpr);
			MoveNode(innerExpr, tupleExpr);
			break;
		}

		//TODO: Why did we have this compat mode thing? It kept us from properly creating tuples with names
		if ((closeParenToken->GetToken() == BfToken_Colon) /*&& (!mCompatMode)*/)
		{
			BfTupleNameNode* tupleNameNode = mAlloc->Alloc<BfTupleNameNode>();
			ReplaceNode(innerExpr, tupleNameNode);
			tupleNameNode->mNameNode = (BfIdentifierNode*)innerExpr;
			MEMBER_SET(tupleNameNode, mColonToken, closeParenToken);
			while ((int)values.size() > names.size())
				names.push_back(NULL);
			names.push_back(tupleNameNode);
			MoveNode(tupleNameNode, tupleExpr);

			innerExpr = CreateExpressionAfter(tupleExpr);
			if (innerExpr == NULL)
				break;
			closeParenToken = ExpectTokenAfter(innerExpr, BfToken_RParen, BfToken_Comma);
		}

		values.push_back(innerExpr);
		MoveNode(innerExpr, tupleExpr);
		innerExpr = NULL;

		if (closeParenToken == NULL)
			break;

		if (closeParenToken->GetToken() == BfToken_RParen)
		{
			MEMBER_SET(tupleExpr, mCloseParen, closeParenToken);
			break;
		}

		commas.push_back(closeParenToken);
		MoveNode(closeParenToken, tupleExpr);
	}

	return tupleExpr;
}

BfAstNode* BfReducer::HandleTopLevel(BfBlock* node)
{
	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(node));

	BfAstNode* prevNode = NULL;
	bool hadPrevFail = false;

	bool isDone = !mVisitorPos.MoveNext();

	auto parser = mSource->ToParser();

	if ((parser != NULL) && (parser->mEmbedKind == BfSourceEmbedKind_Type))
	{
		while (!isDone)
		{
			auto node = mVisitorPos.GetCurrent();
			if (node == prevNode)
			{
				// If we're stuck on an error and can't process any more nodes
				break;
			}
			prevNode = node;
			BfAstNode* typeMember = BfNodeDynCast<BfMemberDeclaration>(node);
			if (typeMember == NULL)
			{
				SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, node);
				typeMember = ReadTypeMember(node);
			}

			//methodDeclaration->mDocumentation = FindDocumentation(methodDeclaration);

			isDone = !mVisitorPos.MoveNext();
			if (typeMember != NULL)
			{
				mVisitorPos.Write(typeMember);
			}
		}
	}

	if ((parser != NULL) && (parser->mEmbedKind == BfSourceEmbedKind_Method))
	{
		bool allowEndingExpression = false;
		BfAstNode* nextNode = NULL;
		while (!isDone)
		{
			BfAstNode* node = mVisitorPos.GetCurrent();

			CreateStmtFlags flags = (CreateStmtFlags)(CreateStmtFlags_FindTrailingSemicolon | CreateStmtFlags_AllowLocalFunction);
			if (allowEndingExpression)
				flags = (CreateStmtFlags)(flags | CreateStmtFlags_AllowUnterminatedExpression);

			auto statement = CreateStatement(node, flags);
			if ((statement == NULL) && (mSource != NULL))
				statement = mSource->CreateErrorNode(node);

			isDone = !mVisitorPos.MoveNext();
			if (statement != NULL)
				mVisitorPos.Write(statement);
		}
	}

	while (!isDone)
	{
		auto child = mVisitorPos.GetCurrent();
		if (child == prevNode)
		{
			// If we're stuck on an error and can't process any more nodes
			break;
		}
		prevNode = child;
		auto tokenNode = BfNodeDynCast<BfTokenNode>(child);
		if (tokenNode == NULL)
		{
			if (!hadPrevFail)
				Fail("Namespace or type declaration expected", child);
			hadPrevFail = true;
			isDone = !mVisitorPos.MoveNext();
			mVisitorPos.Write(child); // Just keep it...
			continue;
		}
		SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, tokenNode);
		auto newNode = CreateTopLevelObject(tokenNode, NULL);
		hadPrevFail = newNode == NULL;

		isDone = !mVisitorPos.MoveNext();
		if (newNode != NULL)
			mVisitorPos.Write(newNode);
	}
	mVisitorPos.Trim();
	return node;
}

BfAstNode* BfReducer::CreateTopLevelObject(BfTokenNode* tokenNode, BfAttributeDirective* attributes, BfAstNode* deferredHeadNode)
{
	AssertCurrentNode(tokenNode);

	bool isSimpleEnum = false;
	if (tokenNode->GetToken() == BfToken_Enum)
	{
		int checkReadPos = mVisitorPos.mReadPos + 1;

		// Do we just have a value list with no members?
		auto nextNode = mVisitorPos.Get(checkReadPos);
		auto checkNode = nextNode;
		while (checkNode != NULL)
		{
			if (auto block = BfNodeDynCast<BfBlock>(checkNode))
			{
				SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));
				mVisitorPos.MoveNext();

				bool hadIllegal = false;
				bool inAssignment = false;
				int bracketDepth = 0;
				int parenDepth = 0;

				int checkIdx = 0;
				while (true)
				{
					auto node = block->mChildArr.Get(checkIdx);
					if (node == NULL)
						break;

					if (auto identifierNode = BfNodeDynCast<BfIdentifierNode>(node))
					{
						// Allow
					}
					else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(node))
					{
						if (tokenNode->mToken == BfToken_Comma)
						{
							// Allow
							inAssignment = false;
						}
						else if (tokenNode->mToken == BfToken_AssignEquals)
						{
							inAssignment = true;
						}
						else if (tokenNode->mToken == BfToken_LBracket)
						{
							bracketDepth++;
						}
						else if (tokenNode->mToken == BfToken_RBracket)
						{
							bracketDepth--;
						}
						else if (tokenNode->mToken == BfToken_LParen)
						{
							parenDepth++;
						}
						else if (tokenNode->mToken == BfToken_RParen)
						{
							parenDepth--;
						}
						else if ((bracketDepth > 0) || (parenDepth > 0))
						{
							// Allow
						}
						else if (tokenNode->mToken == BfToken_Semicolon)
						{
							hadIllegal = true;
							break;
						}
						else
						{
							if (!inAssignment)
							{
								hadIllegal = true;
								break;
							}
						}
					}
					else if ((bracketDepth > 0) || (parenDepth > 0))
					{
						// Allow
					}
					else
					{
						if (!inAssignment)
						{
							hadIllegal = true;
							break;
						}
					}

					checkIdx++;
				}

				if (!hadIllegal)
				{
					isSimpleEnum = true;
				}

				break;
			}

			checkReadPos++;
			auto nextCheckNode = mVisitorPos.Get(checkReadPos);
			checkNode = nextCheckNode;
		}
	}

	switch (tokenNode->GetToken())
	{
	case BfToken_Using:
	{
		if (auto nextTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
		{
			if ((nextTokenNode->mToken == BfToken_Static) || (nextTokenNode->mToken == BfToken_Internal))
			{
				auto usingDirective = mAlloc->Alloc<BfUsingModDirective>();
				ReplaceNode(tokenNode, usingDirective);
				usingDirective->mUsingToken = tokenNode;

				MEMBER_SET(usingDirective, mModToken, nextTokenNode);
				mVisitorPos.MoveNext();

				auto typeRef = CreateTypeRefAfter(usingDirective);
				if (typeRef != NULL)
					MEMBER_SET(usingDirective, mTypeRef, typeRef);

				tokenNode = ExpectTokenAfter(usingDirective, BfToken_Semicolon);
				if (tokenNode != NULL)
					MEMBER_SET(usingDirective, mTrailingSemicolon, tokenNode);

				BfExteriorNode exteriorNode;
				exteriorNode.mNode = usingDirective;
				BfSizedArrayInitIndirect(exteriorNode.mNamespaceNodes, mCurNamespaceStack, mAlloc);
				mExteriorNodes.Add(exteriorNode);
				return usingDirective;
			}
		}

		auto usingDirective = mAlloc->Alloc<BfUsingDirective>();
		ReplaceNode(tokenNode, usingDirective);
		usingDirective->mUsingToken = tokenNode;

		auto identifierNode = ExpectIdentifierAfter(usingDirective);
		if (identifierNode != NULL)
		{
			identifierNode = CompactQualifiedName(identifierNode);

			MEMBER_SET(usingDirective, mNamespace, identifierNode);
			tokenNode = ExpectTokenAfter(usingDirective, BfToken_Semicolon);
			if (tokenNode == NULL)
			{
				// Failure, but eat any following dot for autocompletion purposes
				auto nextNode = mVisitorPos.GetNext();
				if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
				{
					if (tokenNode->GetToken() == BfToken_Dot)
					{
						auto qualifiedNameNode = mAlloc->Alloc<BfQualifiedNameNode>();
						ReplaceNode(usingDirective->mNamespace, qualifiedNameNode);
						qualifiedNameNode->mLeft = usingDirective->mNamespace;
						MEMBER_SET(qualifiedNameNode, mDot, tokenNode);
						usingDirective->mNamespace = qualifiedNameNode;
						usingDirective->SetSrcEnd(qualifiedNameNode->GetSrcEnd());
						return usingDirective;
					}
				}
			}
			else if (tokenNode != NULL)
			{
				MEMBER_SET(usingDirective, mTrailingSemicolon, tokenNode);
			}
		}

		BfExteriorNode exteriorNode;
		exteriorNode.mNode = usingDirective;
		mExteriorNodes.Add(exteriorNode);
		return usingDirective;
	}
	break;
	case BfToken_Namespace:
	{
		auto namespaceDeclaration = mAlloc->Alloc<BfNamespaceDeclaration>();
		namespaceDeclaration->mNamespaceNode = tokenNode;

		auto identifierNode = ExpectIdentifierAfter(tokenNode);
		if (identifierNode == NULL)
			return namespaceDeclaration;
		identifierNode = CompactQualifiedName(identifierNode);

		namespaceDeclaration->mNameNode = identifierNode;
		ReplaceNode(tokenNode, namespaceDeclaration);
		MoveNode(identifierNode, namespaceDeclaration);

		BfAstNode* bodyNode = NULL;
		BfBlock* blockNode = NULL;

		if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
			bodyNode = ExpectTokenAfter(namespaceDeclaration, BfToken_Semicolon);
		else
			bodyNode = blockNode = ExpectBlockAfter(namespaceDeclaration);

		if (bodyNode == NULL)
			return namespaceDeclaration;
		MoveNode(bodyNode, namespaceDeclaration);
		namespaceDeclaration->mBody = bodyNode;

		mCurNamespaceStack.Add(namespaceDeclaration);
		if (blockNode != NULL)
			HandleTopLevel(blockNode);
		mCurNamespaceStack.pop_back();
		return namespaceDeclaration;
	}
	break;
	case BfToken_LBracket:
	{
		auto attributes = CreateAttributeDirective(tokenNode);
		if (attributes == NULL)
			return NULL;

		auto nextNode = mVisitorPos.GetNext();
		auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode);
		if (nextToken == NULL)
		{
			FailAfter("Expected type declaration", tokenNode);
			return NULL;
		}

		mVisitorPos.MoveNext();
		auto topLevelObject = CreateTopLevelObject(nextToken, attributes);
		if (topLevelObject == NULL)
			return NULL;

		auto typeDeclaration = BfNodeDynCast<BfTypeDeclaration>(topLevelObject);
		if (typeDeclaration == NULL)
		{
			Fail("Invalid type specifier", tokenNode);
			return NULL;
		}

		typeDeclaration->mAttributes = attributes;
		ReplaceNode(attributes, typeDeclaration);

		return typeDeclaration;
	}
	break;

	case BfToken_Sealed:
	case BfToken_Abstract:
	case BfToken_Public:
	case BfToken_Private:
	case BfToken_Protected:
	case BfToken_Internal:
	case BfToken_Static:
	{
		auto nextNode = mVisitorPos.GetNext();
		if ((tokenNode->GetToken() == BfToken_Static) && BfNodeIsA<BfBlock>(nextNode))
		{
			// It's a global block!
			auto typeDeclaration = mAlloc->Alloc<BfTypeDeclaration>();
			ReplaceNode(tokenNode, typeDeclaration);
			typeDeclaration->mDocumentation = FindDocumentation(typeDeclaration);
			typeDeclaration->mStaticSpecifier = tokenNode;
			auto block = BfNodeDynCast<BfBlock>(nextNode);
			MEMBER_SET(typeDeclaration, mDefineNode, block);
			mVisitorPos.MoveNext();

			HandleTypeDeclaration(typeDeclaration, attributes, deferredHeadNode);

			return typeDeclaration;
		}

		nextNode = mVisitorPos.GetNext();
		auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode);
		if (nextToken == NULL)
		{
			AddErrorNode(tokenNode);
			FailAfter("Expected type declaration", tokenNode);
			return NULL;
		}
		mVisitorPos.MoveNext();

		auto topLevelObject = CreateTopLevelObject(nextToken, attributes);
		if (topLevelObject == NULL)
		{
			AddErrorNode(tokenNode);
			return NULL;
		}

		auto typeDeclaration = BfNodeDynCast<BfTypeDeclaration>(topLevelObject);
		if (typeDeclaration == NULL)
		{
			AddErrorNode(tokenNode);
			Fail("Invalid type specifier", tokenNode);
			return NULL;
		}

		ReplaceNode(tokenNode, typeDeclaration);

		BfToken token = tokenNode->GetToken();
		if ((token == BfToken_Public) ||
			(token == BfToken_Protected) ||
			(token == BfToken_Private) ||
			(token == BfToken_Internal))
		{
			SetProtection(typeDeclaration, typeDeclaration->mProtectionSpecifier, tokenNode);
		}

		if (token == BfToken_Static)
		{
			if (typeDeclaration->mStaticSpecifier != NULL)
			{
				Fail("Static already specified", tokenNode);
			}
			MEMBER_SET(typeDeclaration, mStaticSpecifier, tokenNode);
		}

		if (token == BfToken_Sealed)
		{
			if (typeDeclaration->mSealedSpecifier != NULL)
			{
				Fail("Sealed already specified", tokenNode);
			}
			MEMBER_SET(typeDeclaration, mSealedSpecifier, tokenNode);
		}

		if (token == BfToken_Abstract)
		{
			if (typeDeclaration->mAbstractSpecifier != NULL)
			{
				Fail(StrFormat("'%s' already specified", typeDeclaration->mAbstractSpecifier->ToString().c_str()), tokenNode);
			}
			MEMBER_SET(typeDeclaration, mAbstractSpecifier, tokenNode);
		}

		//TODO: Store type specifiers
		return typeDeclaration;
	}
	break;

	case BfToken_Delegate:
	case BfToken_Function:
	{
		auto typeDeclaration = mAlloc->Alloc<BfTypeDeclaration>();
		SetAndRestoreValue<BfTypeDeclaration*> prevTypeDecl(mCurTypeDecl, typeDeclaration);

		ReplaceNode(tokenNode, typeDeclaration);
		typeDeclaration->mDocumentation = FindDocumentation(typeDeclaration);
		typeDeclaration->mTypeNode = tokenNode;

		auto retType = CreateTypeRefAfter(typeDeclaration);
		if (retType == NULL)
			return typeDeclaration;

		auto methodDecl = mAlloc->Alloc<BfMethodDeclaration>();
		MEMBER_SET(methodDecl, mReturnType, retType);
		BfDeferredAstSizedArray<BfParameterDeclaration*> params(methodDecl->mParams, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(methodDecl->mCommas, mAlloc);
		methodDecl->mDocumentation = FindDocumentation(methodDecl);

		BfBlock* defineBlock = mAlloc->Alloc<BfBlock>();
		MoveNode(defineBlock, typeDeclaration);
		BfDeferredAstSizedArray<BfAstNode*> members(defineBlock->mChildArr, mAlloc);
		members.push_back(methodDecl);
		MoveNode(methodDecl, typeDeclaration);
		typeDeclaration->mDefineNode = defineBlock;

		auto name = ExpectIdentifierAfter(retType);
		if (name == NULL)
		{
			ReplaceNode(retType, methodDecl);
			return typeDeclaration;
		}

		ReplaceNode(name, methodDecl);
		MEMBER_SET_CHECKED(typeDeclaration, mNameNode, name);

		auto nextNode = mVisitorPos.GetNext();
		if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
		{
			if (tokenNode->GetToken() == BfToken_LChevron)
			{
				auto genericParams = CreateGenericParamsDeclaration(tokenNode);
				MEMBER_SET_CHECKED(typeDeclaration, mGenericParams, genericParams);
				methodDecl->SetSrcEnd(genericParams->GetSrcEnd());
			}
		}

		if (!ParseMethod(methodDecl, &params, &commas))
			return typeDeclaration;

		if (methodDecl->mEndSemicolon == NULL)
			FailAfter("Expected ';'", methodDecl->mCloseParen);

		//MEMBER_SET(methodDecl, mReturnType, retType);

		return typeDeclaration;
	}
	break;
	case BfToken_TypeAlias:
	{
		auto identifierNode = ExpectIdentifierAfter(tokenNode);
		if (identifierNode == NULL)
			return NULL;

		auto typeDeclaration = mAlloc->Alloc<BfTypeAliasDeclaration>();
		BfDeferredAstSizedArray<BfTypeReference*> baseClasses(typeDeclaration->mBaseClasses, mAlloc);
		BfDeferredAstSizedArray<BfAstNode*> baseClassCommas(typeDeclaration->mBaseClassCommas, mAlloc);
		mLastTypeDecl = typeDeclaration;
		typeDeclaration->mTypeNode = tokenNode;
		typeDeclaration->mNameNode = identifierNode;
		ReplaceNode(tokenNode, typeDeclaration);
		MoveNode(identifierNode, typeDeclaration);
		typeDeclaration->mDocumentation = FindDocumentation(typeDeclaration);

		auto nextNode = mVisitorPos.GetNext();
		auto chevronToken = BfNodeDynCast<BfTokenNode>(nextNode);
		if ((chevronToken != NULL) && (chevronToken->GetToken() == BfToken_LChevron))
		{
			auto genericMethodParams = CreateGenericParamsDeclaration(chevronToken);
			if (genericMethodParams != NULL)
			{
				MEMBER_SET(typeDeclaration, mGenericParams, genericMethodParams);
			}
		}

		auto tokenNode = ExpectTokenAfter(typeDeclaration, BfToken_AssignEquals);
		if (tokenNode != NULL)
		{
			MEMBER_SET(typeDeclaration, mEqualsToken, tokenNode);

			auto aliasToType = CreateTypeRefAfter(typeDeclaration);
			if (aliasToType != NULL)
			{
				MEMBER_SET(typeDeclaration, mAliasToType, aliasToType);

				auto tokenNode = ExpectTokenAfter(typeDeclaration, BfToken_Semicolon);
				if (tokenNode != NULL)
					MEMBER_SET(typeDeclaration, mEndSemicolon, tokenNode);
			}
		}

		if (!IsNodeRelevant(deferredHeadNode, typeDeclaration))
			typeDeclaration->mIgnoreDeclaration = true;

		return typeDeclaration;
	}
	break;
	case BfToken_Class:
	case BfToken_Struct:
	case BfToken_Interface:
	case BfToken_Enum:
	case BfToken_Extension:
	{
		if ((tokenNode->GetToken() == BfToken_Enum) && (isSimpleEnum))
			break;

		auto identifierNode = ExpectIdentifierAfter(tokenNode);
		if (identifierNode == NULL)
		{
			AddErrorNode(tokenNode);
			return NULL;
		}

		// We put extra effort in here to continue after failure, since 'return NULL' failure
		//  means we don't parse members inside type (messes up colorization and such)

		auto typeDeclaration = mAlloc->Alloc<BfTypeDeclaration>();
		BfDeferredAstSizedArray<BfTypeReference*> baseClasses(typeDeclaration->mBaseClasses, mAlloc);
		BfDeferredAstSizedArray<BfAstNode*> baseClassCommas(typeDeclaration->mBaseClassCommas, mAlloc);
		mLastTypeDecl = typeDeclaration;
		typeDeclaration->mTypeNode = tokenNode;
		typeDeclaration->mNameNode = identifierNode;
		ReplaceNode(tokenNode, typeDeclaration);
		MoveNode(identifierNode, typeDeclaration);
		typeDeclaration->mDocumentation = FindDocumentation(mTypeMemberNodeStart);

		auto nextNode = mVisitorPos.GetNext();
		auto chevronToken = BfNodeDynCast<BfTokenNode>(nextNode);
		if ((chevronToken != NULL) && (chevronToken->GetToken() == BfToken_LChevron))
		{
			auto genericMethodParams = CreateGenericParamsDeclaration(chevronToken);
			if (genericMethodParams != NULL)
			{
				MEMBER_SET(typeDeclaration, mGenericParams, genericMethodParams);
			}
		}

		nextNode = mVisitorPos.GetNext();
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Colon))
		{
			MEMBER_SET(typeDeclaration, mColonToken, tokenNode);
			mVisitorPos.MoveNext();
			tokenNode = NULL;
			for (int baseTypeIdx = 0; true; baseTypeIdx++)
			{
				nextNode = mVisitorPos.GetNext();
				if ((baseTypeIdx > 0) && (BfNodeDynCast<BfBlock>(nextNode)))
					break;

				if (baseTypeIdx > 0)
				{
					if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
					{
						if (tokenNode->mToken == BfToken_Semicolon)
						{
							break;
						}
					}

					BfTokenNode* commaToken = NULL;
					if (typeDeclaration->mGenericParams != NULL)
					{
						commaToken = ExpectTokenAfter(typeDeclaration, BfToken_Comma, BfToken_Where);
						if ((commaToken != NULL) && (commaToken->GetToken() == BfToken_Where))
						{
							mVisitorPos.mReadPos--;
							break;
						}
					}
					else
						commaToken = ExpectTokenAfter(typeDeclaration, BfToken_Comma);
					if (commaToken == NULL)
						break;
					MoveNode(commaToken, typeDeclaration);
					baseClassCommas.push_back(commaToken);
				}

				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
				{
					if (tokenNode->mToken == BfToken_This)
					{
						mVisitorPos.MoveNext();
						auto ctorDecl = mAlloc->Alloc<BfAutoConstructorDeclaration>();
						BfDeferredAstSizedArray<BfParameterDeclaration*> params(ctorDecl->mParams, mAlloc);
						BfDeferredAstSizedArray<BfTokenNode*> commas(ctorDecl->mCommas, mAlloc);
						ctorDecl->mReturnType = NULL;
						ReplaceNode(tokenNode, ctorDecl);
						MEMBER_SET(ctorDecl, mThisToken, tokenNode);
						ParseMethod(ctorDecl, &params, &commas);

						if (!baseClassCommas.IsEmpty())
						{
							ctorDecl->mPrefix = baseClassCommas.back();
							baseClassCommas.pop_back();
							ctorDecl->mSrcStart = ctorDecl->mPrefix->mSrcStart;
						}

						if (typeDeclaration->mAutoCtor == NULL)
						{
							MEMBER_SET(typeDeclaration, mAutoCtor, ctorDecl);
						}
						else
						{
							Fail("Only one auto-constructor is allowed", ctorDecl);
							AddErrorNode(ctorDecl);
						}
						continue;
					}
				}

				auto baseType = CreateTypeRefAfter(typeDeclaration);
				if (baseType == NULL)
					break;
				MoveNode(baseType, typeDeclaration);
				baseClasses.push_back(baseType);
			}
			nextNode = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		}

		if (tokenNode != NULL)
		{
			if (tokenNode->GetToken() == BfToken_Where)
			{
				mVisitorPos.MoveNext();
				auto constraints = CreateGenericConstraintsDeclaration(tokenNode);
				if (constraints != NULL)
				{
					MEMBER_SET(typeDeclaration, mGenericConstraintsDeclaration, constraints);
				}

				nextNode = mVisitorPos.GetNext();
				tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			}
		}

		if (tokenNode != NULL)
		{
			if (tokenNode->GetToken() == BfToken_Semicolon)
			{
				typeDeclaration->mDefineNode = tokenNode;
				MoveNode(tokenNode, typeDeclaration);
				mVisitorPos.MoveNext();
				return typeDeclaration;
			}
		}

		auto blockNode = ExpectBlockAfter(typeDeclaration);
		if (blockNode != NULL)
		{
			typeDeclaration->mDefineNode = blockNode;
			MoveNode(blockNode, typeDeclaration);
			HandleTypeDeclaration(typeDeclaration, attributes, (deferredHeadNode != NULL) ? deferredHeadNode : attributes);
		}

		return typeDeclaration;
	}
	break;
	default: break;
	}

	if (isSimpleEnum)
	{
		auto identifierNode = ExpectIdentifierAfter(tokenNode, "enum name");
		if (identifierNode == NULL)
			return NULL;

		// We put extra effort in here to continue after failure, since 'return NULL' failure
		//  means we don't parse members inside type (messes up colorization and such)

		auto typeDeclaration = mAlloc->Alloc<BfTypeDeclaration>();
		BfDeferredAstSizedArray<BfTypeReference*> baseClasses(typeDeclaration->mBaseClasses, mAlloc);
		auto prevTypeDecl = mCurTypeDecl;
		mCurTypeDecl = typeDeclaration;
		typeDeclaration->mTypeNode = tokenNode;
		typeDeclaration->mNameNode = identifierNode;
		ReplaceNode(tokenNode, typeDeclaration);
		MoveNode(identifierNode, typeDeclaration);
		typeDeclaration->mDocumentation = FindDocumentation(typeDeclaration);

		auto nextNode = mVisitorPos.GetNext();
		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_Colon))
		{
			MEMBER_SET(typeDeclaration, mColonToken, tokenNode);
			mVisitorPos.MoveNext();
			tokenNode = NULL;

			auto baseType = CreateTypeRefAfter(typeDeclaration);
			if (baseType == NULL)
				return NULL;
			MoveNode(baseType, typeDeclaration);
			baseClasses.push_back(baseType);
		}

		auto blockNode = ExpectBlockAfter(typeDeclaration);
		if (blockNode != NULL)
		{
			SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(blockNode));

			mVisitorPos.MoveNext();
			for (int fieldNum = 0; true; fieldNum++)
			{
				BfAstNode* child = mVisitorPos.GetCurrent();
				if (child == NULL)
					break;

				if (fieldNum > 0)
				{
					auto commaToken = BfNodeDynCast<BfTokenNode>(child);
					if ((commaToken == NULL) || (commaToken->GetToken() != BfToken_Comma))
					{
						Fail("Comma expected", child);
						break;
					}

					MoveNode(commaToken, mCurTypeDecl);
					mVisitorPos.MoveNext();
					mVisitorPos.Write(commaToken);
					child = mVisitorPos.GetCurrent();
				}

				if (child == NULL)
					break;
				auto fieldDecl = mAlloc->Alloc<BfEnumEntryDeclaration>();

				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(child))
				{
					if (tokenNode->mToken == BfToken_LBracket)
					{
						BfAttributeDirective* attribute = CreateAttributeDirective(tokenNode);
						if (attribute != NULL)
						{
							mVisitorPos.MoveNext();
							child = mVisitorPos.GetCurrent();
							fieldDecl->mAttributes = attribute;

							if (child == NULL)
								break;
						}
					}
				}

				mVisitorPos.MoveNext();
				mVisitorPos.Write(fieldDecl);
				ReplaceNode(child, fieldDecl);

				if (fieldDecl->mAttributes != NULL)
					fieldDecl->mSrcStart = fieldDecl->mAttributes->mSrcStart;

				auto valueName = BfNodeDynCast<BfIdentifierNode>(child);
				if (valueName == NULL)
				{
					Fail("Enum value name expected", child);
					break;
				}
				MEMBER_SET(fieldDecl, mNameNode, valueName);
				auto nextNode = mVisitorPos.GetCurrent();
				if (auto equalsToken = BfNodeDynCast<BfTokenNode>(nextNode))
				{
					if (equalsToken->GetToken() == BfToken_AssignEquals)
					{
						MEMBER_SET(fieldDecl, mEqualsNode, equalsToken);
						fieldDecl->mInitializer = CreateExpressionAfter(fieldDecl);
						if (fieldDecl->mInitializer != NULL)
						{
							mVisitorPos.MoveNext();
							MoveNode(fieldDecl->mInitializer, fieldDecl);
						}
					}
				}

				fieldDecl->mDocumentation = FindDocumentation(fieldDecl, NULL, true);
				MoveNode(fieldDecl, mCurTypeDecl);
			}

			mVisitorPos.Trim();
		}

		typeDeclaration->mDefineNode = blockNode;
		if (blockNode != NULL)
		{
			MoveNode(blockNode, typeDeclaration);
		}
		mCurTypeDecl = prevTypeDecl;
		return typeDeclaration;
	}

	AddErrorNode(tokenNode, false);
	Fail("Unexpected token", tokenNode);
	return NULL;
}

BfTokenNode* BfReducer::ExpectTokenAfter(BfAstNode* node, BfToken token)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
	if ((tokenNode == NULL) ||
		(tokenNode->GetToken() != token))
	{
		FailAfter(StrFormat("Expected '%s'", BfTokenToString(token)), node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return tokenNode;
}

BfTokenNode* BfReducer::ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
	if ((tokenNode == NULL) ||
		((tokenNode->GetToken() != tokenA) && (tokenNode->GetToken() != tokenB)))
	{
		FailAfter(StrFormat("Expected '%s' or '%s'", BfTokenToString(tokenA), BfTokenToString(tokenB)), node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return tokenNode;
}

BfTokenNode* BfReducer::ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB, BfToken tokenC)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);

	BfToken token = BfToken_Null;
	if (tokenNode != NULL)
		token = tokenNode->GetToken();
	if ((tokenNode == NULL) ||
		((token != tokenA) && (token != tokenB) && (token != tokenC)))
	{
		FailAfter(StrFormat("Expected '%s', '%s', or '%s'", BfTokenToString(tokenA), BfTokenToString(tokenB), BfTokenToString(tokenC)), node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return tokenNode;
}

BfTokenNode* BfReducer::ExpectTokenAfter(BfAstNode* node, BfToken tokenA, BfToken tokenB, BfToken tokenC, BfToken tokenD)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
	BfToken token = BfToken_Null;
	if (tokenNode != NULL)
		token = tokenNode->GetToken();
	if ((tokenNode == NULL) ||
		((token != tokenA) && (token != tokenB) && (token != tokenC) && (token != tokenD)))
	{
		FailAfter(StrFormat("Expected '%s', '%s', '%s', or '%s'", BfTokenToString(tokenA), BfTokenToString(tokenB), BfTokenToString(tokenC), BfTokenToString(tokenD)), node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return tokenNode;
}

BfIdentifierNode* BfReducer::ExpectIdentifierAfter(BfAstNode* node, const char* typeName)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto identifierNode = BfNodeDynCast<BfIdentifierNode>(nextNode);
	if (identifierNode == NULL)
	{
		if (typeName != NULL)
			FailAfter(StrFormat("Expected %s", typeName), node);
		else
			FailAfter("Expected identifier", node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return identifierNode;
}

BfBlock* BfReducer::ExpectBlockAfter(BfAstNode* node)
{
	AssertCurrentNode(node);
	auto nextNode = mVisitorPos.GetNext();
	auto block = BfNodeDynCast<BfBlock>(nextNode);
	if (block == NULL)
	{
		FailAfter("Block expected", node);
		return NULL;
	}
	mVisitorPos.MoveNext();
	return block;
}

BfTokenNode* BfReducer::BreakDoubleChevron(BfTokenNode* tokenNode)
{
	// Break up those chevrons
	auto firstChevron = mAlloc->Alloc<BfTokenNode>();
	firstChevron->SetToken(BfToken_RChevron);

	int triviaStart;
	int srcStart;
	int srcEnd;
	tokenNode->GetSrcPositions(triviaStart, srcStart, srcEnd);
	firstChevron->Init(triviaStart, srcStart, srcEnd - 1);

	tokenNode->SetToken(BfToken_RChevron);
	tokenNode->SetSrcStart(srcStart + 1);
	tokenNode->SetTriviaStart(srcStart);

	return firstChevron;
}

BfTokenNode* BfReducer::BreakQuestionLBracket(BfTokenNode* tokenNode)
{
	// Break up those chevrons
	auto firstToken = mAlloc->Alloc<BfTokenNode>();
	firstToken->SetToken(BfToken_Question);

	int triviaStart;
	int srcStart;
	int srcEnd;
	tokenNode->GetSrcPositions(triviaStart, srcStart, srcEnd);
	firstToken->Init(triviaStart, srcStart, srcEnd - 1);

	tokenNode->SetToken(BfToken_LBracket);
	tokenNode->SetSrcStart(srcStart + 1);
	tokenNode->SetTriviaStart(srcStart);

	return firstToken;
}

BfCommentNode * BfReducer::FindDocumentation(BfAstNode* defNodeHead, BfAstNode* defNodeEnd, bool checkDocAfter)
{
	if (defNodeEnd == NULL)
		defNodeEnd = defNodeHead;
	else if (defNodeHead == NULL)
		defNodeHead = defNodeEnd;

	while (mDocumentCheckIdx < mSource->mSidechannelRootNode->mChildArr.mSize)
	{
		auto checkComment = BfNodeDynCast<BfCommentNode>(mSource->mSidechannelRootNode->mChildArr[mDocumentCheckIdx]);
		if ((checkComment == NULL) || (checkComment->mCommentKind == BfCommentKind_Block) || (checkComment->mCommentKind == BfCommentKind_Line))
		{
			mDocumentCheckIdx++;
			continue;
		}
		if (checkComment->GetSrcEnd() > defNodeEnd->GetSrcStart())
		{
			if ((checkComment->mCommentKind == BfCommentKind_Documentation_Line_Post) ||
				(checkComment->mCommentKind == BfCommentKind_Documentation_Block_Post))
			{
				int defEnd = defNodeEnd->GetSrcEnd();
				if (checkDocAfter)
				{
					int endDiff = defEnd - checkComment->GetSrcStart();
					if (endDiff > 256)
						return NULL;

					for (int idx = defEnd; idx < checkComment->GetSrcStart(); idx++)
					{
						char c = mSource->mSrc[idx];
						if (idx == defEnd)
						{
							if ((c == ';') || (c == ','))
								continue;
						}
						if (c == '\n')
							return NULL; // No newline allowed
						if (!isspace((uint8)c))
							return NULL;
					}

					mDocumentCheckIdx++;
					return checkComment;
				}
			}

			return NULL;
		}

		if ((checkComment->mCommentKind != BfCommentKind_Documentation_Line_Pre) &&
			(checkComment->mCommentKind != BfCommentKind_Documentation_Block_Pre))
		{
			// Skip this, not used
			mDocumentCheckIdx++;
			continue;
		}

		if (mDocumentCheckIdx < mSource->mSidechannelRootNode->mChildArr.mSize - 1)
		{
			auto nextComment = mSource->mSidechannelRootNode->mChildArr[mDocumentCheckIdx + 1];
			if (nextComment->GetSrcEnd() <= defNodeHead->GetSrcStart())
			{
				// This comment is still before the node in question
				mDocumentCheckIdx++;
				continue;
			}
		}

		mDocumentCheckIdx++;
		int defSrcIdx = defNodeHead->GetSrcStart();
		for (int idx = checkComment->GetSrcEnd(); idx < defSrcIdx; idx++)
		{
			char c = mSource->mSrc[idx];
			if (!isspace((uint8)c))
				return NULL;
		}
		return checkComment;
	}
	return NULL;
}

BfTokenNode* BfReducer::ParseMethodParams(BfAstNode* node, SizedArrayImpl<BfParameterDeclaration*>* params, SizedArrayImpl<BfTokenNode*>* commas, BfToken endToken, bool requireNames)
{
	BfAstNode* nameAfterNode = node;

	BfAttributeDirective* attributes = NULL;
	for (int paramIdx = 0; true; paramIdx++)
	{
		auto nextNode = ReplaceTokenStarter(mVisitorPos.GetNext(), mVisitorPos.mReadPos + 1, true);

		auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode != NULL)
		{
			BfToken token = tokenNode->GetToken();

			if ((paramIdx > 0) && (token == BfToken_AssignEquals))
			{
				auto paramDecl = params->back();
				MEMBER_SET(paramDecl, mEqualsNode, tokenNode);
				paramDecl->mEqualsNode = tokenNode;
				mVisitorPos.MoveNext();
				mSkipCurrentNodeAssert = true;
				auto initExpr = CreateExpressionAfter(node);
				mSkipCurrentNodeAssert = false;
				if (initExpr == NULL)
					return NULL;
				MEMBER_SET(paramDecl, mInitializer, initExpr);
				node->mSrcEnd = paramDecl->mSrcEnd;
				auto nextNode = mVisitorPos.GetNext();
				tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
				if (tokenNode == NULL)
					return NULL;
				token = tokenNode->GetToken();
			}

			if (token == endToken)
				return tokenNode;

			if ((paramIdx == 0) && (
				(token == BfToken_In) || (token == BfToken_Out) || (token == BfToken_Ref) || (token == BfToken_Mut) ||
				(token == BfToken_Delegate) || (token == BfToken_Function) ||
				(token == BfToken_Comptype) || (token == BfToken_Decltype) ||
				(token == BfToken_AllocType) || (token == BfToken_RetType) ||
				(token == BfToken_Params) || (token == BfToken_LParen) ||
				(token == BfToken_Var) || (token == BfToken_LBracket) ||
				(token == BfToken_ReadOnly) || (token == BfToken_DotDotDot)))
			{
				// These get picked up below
			}
			else
			{
				if ((paramIdx == 0) || (token != BfToken_Comma))
				{
					Fail("Expected ')' or parameter list", tokenNode);
					return NULL;
				}
				if (paramIdx > 0)
					commas->push_back(tokenNode);
				MoveNode(tokenNode, node);
				mVisitorPos.MoveNext();
			}

			nameAfterNode = tokenNode;
		}
		else
		{
			if (paramIdx > 0)
			{
				FailAfter("Expected ')' or additional parameters", nameAfterNode);
				return NULL;
			}
		}

		bool nextNextIsIdentifier = BfNodeIsA<BfIdentifierNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2));

		attributes = NULL;
		BfTokenNode* modTokenNode = NULL;
		nextNode = ReplaceTokenStarter(mVisitorPos.GetNext(), mVisitorPos.mReadPos + 1, nextNextIsIdentifier);
		tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		BfTypeReference* typeRef = NULL;
		if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_LBracket))
		{
			mVisitorPos.MoveNext();
			attributes = CreateAttributeDirective(tokenNode);
			if (attributes != NULL)
			{
				nameAfterNode = attributes;
				auto nextNode = mVisitorPos.GetNext();
				tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			}
		}

		int paramStartReadPos = mVisitorPos.mReadPos;
		bool isParams = false;
		if (tokenNode != NULL)
		{
			BfToken token = tokenNode->GetToken();
			if ((token == BfToken_Var) || (token == BfToken_LParen) ||
				(token == BfToken_Delegate) || (token == BfToken_Function) ||
				(token == BfToken_Comptype) || (token == BfToken_Decltype) ||
				(token == BfToken_AllocType) || (token == BfToken_RetType) ||
				(token == BfToken_DotDotDot))
			{
				mVisitorPos.MoveNext();
				typeRef = CreateTypeRef(tokenNode);
			}
			else
			{
				if ((token != BfToken_In) && (token != BfToken_Out) && (token != BfToken_Ref) && (token != BfToken_Mut) && (token != BfToken_Params) && (token != BfToken_ReadOnly))
				{
					if (attributes != NULL)
					{
						auto paramDecl = mAlloc->Alloc<BfParameterDeclaration>();
						ReplaceNode(attributes, paramDecl);
						MoveNode(paramDecl, node);
						params->push_back(paramDecl);
						MEMBER_SET(paramDecl, mAttributes, attributes);
						attributes = NULL;
					}

					Fail("Invalid token", tokenNode);
					return NULL;
				}
				if (token == BfToken_Params)
					isParams = true;
				modTokenNode = tokenNode;
				nameAfterNode = modTokenNode;
				mVisitorPos.MoveNext();
			}
		}

		if (typeRef == NULL)
		{
			auto nextNode = mVisitorPos.GetNext();
			if (nextNode == NULL)
			{
				FailAfter("Type expected", nameAfterNode);
				break;
			}

			mVisitorPos.MoveNext();

			typeRef = CreateTypeRef(nextNode);
			if (typeRef == NULL)
			{
				mVisitorPos.mReadPos = paramStartReadPos;
				break;
			}
		}

		BfToken modToken = BfToken_None;
		if (modTokenNode != NULL)
			modToken = modTokenNode->GetToken();

		if ((modTokenNode != NULL) && ((modToken == BfToken_Ref) || (modToken == BfToken_Mut) || (modToken == BfToken_In) || (modToken == BfToken_Out)))
		{
			typeRef = CreateRefTypeRef(typeRef, modTokenNode);
			modTokenNode = NULL;
		}

		auto paramDecl = mAlloc->Alloc<BfParameterDeclaration>();
		ReplaceNode(typeRef, paramDecl);
		MoveNode(paramDecl, node);
		params->push_back(paramDecl);
		MEMBER_SET(paramDecl, mTypeRef, typeRef);
		if (attributes != NULL)
		{
			MEMBER_SET(paramDecl, mAttributes, attributes);
			attributes = NULL;
		}

		if (modTokenNode != NULL)
		{
			MEMBER_SET(paramDecl, mModToken, modTokenNode);
		}

		if ((tokenNode != NULL) && (tokenNode->mToken == BfToken_DotDotDot))
			continue;

		bool allowNameFail = false;
		bool nextIsName = false;
		auto afterNameTokenNode = BfNodeDynCast<BfTokenNode>(mVisitorPos.Get(mVisitorPos.mReadPos + 2));
		if (afterNameTokenNode != NULL)
		{
			BfToken afterNameToken = afterNameTokenNode->GetToken();
			if ((afterNameToken == BfToken_Comma) ||
				(afterNameToken == BfToken_AssignEquals) ||
				(afterNameToken == BfToken_RParen))
			{
				nextIsName = true;
			}
		}

		// We definitely have a failure, but we want to attempt to scan to the actual param name if we can...
		if (!nextIsName)
		{
			int nameIdx = -1;
			int checkIdx = mVisitorPos.mReadPos + 1;
			bool useNameIdx = true;
			int parenDepth = 1;
			int bracketDepth = 0;

			while (true)
			{
				auto checkNode = mVisitorPos.Get(checkIdx);

				auto checkTokenNode = BfNodeDynCast<BfTokenNode>(checkNode);
				if (checkTokenNode != NULL)
				{
					BfToken checkToken = checkTokenNode->GetToken();
					if (nameIdx == -1)
					{
						if ((parenDepth == 1) && (bracketDepth == 0))
						{
							if ((checkToken == BfToken_Comma) ||
								(checkToken == BfToken_AssignEquals) ||
								(checkToken == BfToken_RParen))
							{
								if (auto nameIdentifier = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.Get(checkIdx - 1)))
								{
									nameIdx = checkIdx - 1;
								}
								else
								{
									useNameIdx = false;
									break;
								}
							}
						}
					}

					if (checkToken == BfToken_RParen)
						parenDepth--;
					else if (checkToken == BfToken_LParen)
						parenDepth++;
					else if (checkToken == BfToken_LBracket)
						bracketDepth++;
					else if (checkToken == BfToken_RBracket)
						bracketDepth--;

					if (parenDepth == 0)
					{
						if (bracketDepth != 0)
							useNameIdx = false;
						break;
					}

					if (checkToken == BfToken_Semicolon)
					{
						useNameIdx = false;
						break;
					}
				}
				else if (auto identifier = BfNodeDynCast<BfIdentifierNode>(checkNode))
				{
					// Okay
				}
				else
				{
					// Nothing else is okay
					useNameIdx = false;
					break;
				}

				checkIdx++;
			}

			if ((useNameIdx) && (nameIdx != -1))
			{
				if (nameIdx <= mVisitorPos.mReadPos)
				{
					// We have a valid-enough param list but a missing name, so keep going
					allowNameFail = true;
				}
				else
				{
					for (int errIdx = mVisitorPos.mReadPos + 1; errIdx < nameIdx; errIdx++)
					{
						auto node = mVisitorPos.Get(errIdx);
						if (auto token = BfNodeDynCast<BfTokenNode>(node))
							Fail("Unexpected token", node);
						else
							Fail("Unexpected identifier", node);
						AddErrorNode(node);
					}

					auto nameIdentifierNode = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.Get(nameIdx));
					paramDecl->mNameNode = nameIdentifierNode;
					MoveNode(nameIdentifierNode, paramDecl);
					nameAfterNode = nameIdentifierNode;
					mVisitorPos.mReadPos = nameIdx;
					continue;
					//mVisitorPos.mReadPos = nameIdx - 1;
				}
			}
		}

		if (auto nameToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
		{
			if ((nameToken->GetToken() == BfToken_This))
			{
				bool isDelegate = false;
				bool isFunction = false;
				if (auto delegateTypeRef = BfNodeDynCast<BfDelegateTypeRef>(node))
				{
					if (delegateTypeRef->mTypeToken->GetToken() == BfToken_Function)
						isFunction = true;
					else
						isDelegate = true;
				}
				else if ((mCurTypeDecl->mTypeNode != NULL) && (mCurTypeDecl->mTypeNode->GetToken() == BfToken_Function))
					isFunction = true;
				else if ((mCurTypeDecl->mTypeNode != NULL) && (mCurTypeDecl->mTypeNode->GetToken() == BfToken_Delegate))
					isDelegate = true;

				if (isFunction || isDelegate)
				{
					if (paramIdx != 0)
						Fail("'this' can only be used as the first parameter", nameToken);
					if (!isFunction)
						Fail("'this' can only be specified on function types", nameToken);
					mVisitorPos.MoveNext();
					paramDecl->mNameNode = nameToken;
					MoveNode(nameToken, paramDecl);
					nameAfterNode = nameToken;
				}
			}
		}

		if (paramDecl->mNameNode == NULL)
		{
			BfAstNode* nameIdentifierNode;
			if (requireNames)
			{
				nameIdentifierNode = ExpectIdentifierAfter(node, "parameter name");
				if (nameIdentifierNode == NULL)
				{
					if (!allowNameFail)
						return NULL;
				}
			}
			else
			{
				nameIdentifierNode = BfNodeDynCast<BfIdentifierNode>(mVisitorPos.GetNext());
				if (nameIdentifierNode != NULL)
					mVisitorPos.MoveNext();
			}

			if (nameIdentifierNode != NULL)
			{
				paramDecl->mNameNode = nameIdentifierNode;
				MoveNode(nameIdentifierNode, paramDecl);
				nameAfterNode = nameIdentifierNode;
			}
		}

		node->mSrcEnd = paramDecl->mSrcEnd;
	}

	if (attributes != NULL)
	{
		Fail("Unexpected attributes", attributes);
		AddErrorNode(attributes);
	}

	return NULL;
}

bool BfReducer::ParseMethod(BfMethodDeclaration* methodDeclaration, SizedArrayImpl<BfParameterDeclaration*>* params, SizedArrayImpl<BfTokenNode*>* commas, bool alwaysIncludeBlock)
{
	BfTokenNode* tokenNode;
	auto nextNode = mVisitorPos.GetNext();
	if (methodDeclaration->mGenericParams == NULL)
	{
		if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
		{
			if (tokenNode->GetToken() == BfToken_LChevron)
			{
				auto genericParams = CreateGenericParamsDeclaration(tokenNode);
				if (genericParams != NULL)
				{
					MEMBER_SET(methodDeclaration, mGenericParams, genericParams);
				}
			}
		}
	}

	tokenNode = ExpectTokenAfter(methodDeclaration, BfToken_LParen, BfToken_Bang);
	if (tokenNode == NULL)
		return false;
	if (tokenNode->GetToken() == BfToken_Bang)
	{
		Fail("Cannot include '!' in the method declaration", tokenNode);
		MoveNode(tokenNode, methodDeclaration);
		tokenNode = ExpectTokenAfter(methodDeclaration, BfToken_LParen);
		if (tokenNode == NULL)
			return false;
	}
	methodDeclaration->mOpenParen = tokenNode;
	MoveNode(methodDeclaration->mOpenParen, methodDeclaration);

	bool isFunction = false;
	bool isDelegate = false;
	if ((mCurTypeDecl != NULL) && (mCurTypeDecl->mTypeNode != NULL) && (mCurTypeDecl->mTypeNode->GetToken() == BfToken_Function))
		isFunction = true;
	else if ((mCurTypeDecl != NULL) && (mCurTypeDecl->mTypeNode != NULL) && (mCurTypeDecl->mTypeNode->GetToken() == BfToken_Delegate))
		isDelegate = true;

	if ((!isFunction) && (!isDelegate))
	{
		if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
		{
			if (nextToken->mToken == BfToken_This)
			{
				MEMBER_SET(methodDeclaration, mThisToken, nextToken);
				mVisitorPos.MoveNext();
			}
		}
	}

	methodDeclaration->mCloseParen = ParseMethodParams(methodDeclaration, params, commas, BfToken_RParen, true);

	// RParen
	if (methodDeclaration->mCloseParen == NULL)
	{
		auto nextNode = mVisitorPos.GetNext();
		while (auto checkToken = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			if (checkToken->GetToken() == BfToken_RParen)
			{
				methodDeclaration->mCloseParen = checkToken;
				break;
			}
			else
			{
				// Just eat it - for autocompletion.  This helps make cases nicer where we're typing in the "in" for "int", for example
				MoveNode(checkToken, methodDeclaration);
				AddErrorNode(checkToken);
				mVisitorPos.MoveNext();
				nextNode = mVisitorPos.GetNext();
			}
		}
		if (methodDeclaration->mCloseParen == NULL)
			return false;
	}
	MoveNode(methodDeclaration->mCloseParen, methodDeclaration);
	mVisitorPos.MoveNext();

	auto typeDecl = mCurTypeDecl;
	nextNode = mVisitorPos.GetNext();
	if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
	{
		if (tokenNode->GetToken() == BfToken_Mut)
		{
			if (methodDeclaration->mMutSpecifier != NULL)
			{
				AddErrorNode(methodDeclaration->mMutSpecifier);
				Fail("Mut already specified", methodDeclaration->mMutSpecifier);
			}
			MEMBER_SET(methodDeclaration, mMutSpecifier, tokenNode);
			mVisitorPos.MoveNext();
			nextNode = mVisitorPos.GetNext();
		}
	}

	if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
	{
		if (tokenNode->GetToken() == BfToken_Where)
		{
			mVisitorPos.MoveNext();
			auto genericConstraints = CreateGenericConstraintsDeclaration(tokenNode);
			MEMBER_SET_CHECKED_BOOL(methodDeclaration, mGenericConstraintsDeclaration, genericConstraints);
		}
	}

	auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration);

	nextNode = mVisitorPos.GetNext();
	auto endToken = BfNodeDynCast<BfTokenNode>(nextNode);
	if ((endToken != NULL) && (endToken->GetToken() == BfToken_Colon))
	{
		if (auto ctorDecl = BfNodeDynCast<BfConstructorDeclaration>(methodDeclaration))
		{
			MEMBER_SET(ctorDecl, mInitializerColonToken, endToken);
			mVisitorPos.MoveNext();

			BfAstNode* invokeAfter = ctorDecl;
			auto nextNode = mVisitorPos.GetNext();

			BfAttributeDirective* attributeDirective = NULL;
			if (auto nextToken = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				if (nextToken->mToken == BfToken_LBracket)
				{
					mVisitorPos.MoveNext();
					attributeDirective = CreateAttributeDirective(nextToken);
					nextNode = mVisitorPos.GetNext();
					if (attributeDirective != NULL)
						invokeAfter = attributeDirective;
				}
			}

			endToken = ExpectTokenAfter(invokeAfter, BfToken_This, BfToken_Base);
			if (endToken != NULL)
			{
				auto invocationExpr = CreateInvocationExpression(endToken);
				if (invocationExpr != NULL)
				{
					MEMBER_SET(ctorDecl, mInitializer, invocationExpr);
				}
			}
			else if (auto identifierAfter = BfNodeDynCast<BfIdentifierNode>(nextNode))
			{
				// In process of typing - just eat identifier so we don't error out on whole method
				MoveNode(identifierAfter, ctorDecl);
				mVisitorPos.MoveNext();
				AddErrorNode(identifierAfter);
			}

			if (attributeDirective != NULL)
			{
				BfAttributedExpression* attribExpr = mAlloc->Alloc<BfAttributedExpression>();
				ReplaceNode(attributeDirective, attribExpr);
				attribExpr->mAttributes = attributeDirective;
				if (ctorDecl->mInitializer != NULL)
				{
					MEMBER_SET(attribExpr, mExpression, ctorDecl->mInitializer);
				}
				MEMBER_SET(ctorDecl, mInitializer, attribExpr);
			}
		}

		endToken = NULL;
	}

	if (auto autoCtorDecl = BfNodeDynCast<BfAutoConstructorDeclaration>(ctorDecl))
		return true;

	if ((endToken != NULL) && (endToken->GetToken() == BfToken_Semicolon))
	{
		MEMBER_SET_CHECKED_BOOL(methodDeclaration, mEndSemicolon, endToken);
		mVisitorPos.MoveNext();
	}
	else
	{
		nextNode = mVisitorPos.GetNext();
		auto blockNode = BfNodeDynCast<BfBlock>(nextNode);
		if (blockNode != NULL)
		{
			methodDeclaration->mBody = blockNode;
			MoveNode(blockNode, methodDeclaration);
			mVisitorPos.MoveNext();

			if ((IsNodeRelevant(methodDeclaration)) || (alwaysIncludeBlock))
			{
				SetAndRestoreValue<BfMethodDeclaration*> prevMethodDeclaration(mCurMethodDecl, methodDeclaration);
				HandleBlock(blockNode, methodDeclaration->mMixinSpecifier != NULL);
			}

			return true;
		}
		else
		{
			nextNode = mVisitorPos.GetNext();
			auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if ((tokenNode != NULL) && (tokenNode->GetToken() == BfToken_FatArrow))
			{
				MEMBER_SET(methodDeclaration, mFatArrowToken, tokenNode);
				mVisitorPos.MoveNext();

				auto methodExpression = CreateExpressionAfter(methodDeclaration);
				MEMBER_SET_CHECKED_BOOL(methodDeclaration, mBody, methodExpression);

				auto semicolonToken = ExpectTokenAfter(methodDeclaration, BfToken_Semicolon);
				MEMBER_SET_CHECKED_BOOL(methodDeclaration, mEndSemicolon, semicolonToken);

				return true;
			}
		}

		FailAfter("Expected method body", methodDeclaration);
	}
	return true;
}

BfGenericArgumentsNode* BfReducer::CreateGenericArguments(BfTokenNode* tokenNode, bool allowPartial)
{
	auto genericArgs = mAlloc->Alloc<BfGenericArgumentsNode>();
	BfDeferredAstSizedArray<BfAstNode*> genericArgsArray(genericArgs->mGenericArgs, mAlloc);
	BfDeferredAstSizedArray<BfAstNode*> commas(genericArgs->mCommas, mAlloc);
	ReplaceNode(tokenNode, genericArgs);
	genericArgs->mOpenChevron = tokenNode;

	while (true)
	{
		bool doAsExpr = false;
		auto nextNode = mVisitorPos.GetNext();
		if (BfNodeIsA<BfLiteralExpression>(nextNode))
			doAsExpr = true;
		else if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
		{
			if (tokenNode->mToken == BfToken_Question)
				doAsExpr = true;
		}

		BfAstNode* genericArg = NULL;
		if (doAsExpr)
			genericArg = CreateExpressionAfter(genericArgs, CreateExprFlags_BreakOnRChevron);
		else
			genericArg = CreateTypeRefAfter(genericArgs);
		if (genericArg == NULL)
		{
			genericArgsArray.push_back(NULL); // Leave empty for purposes of generic argument count

			auto nextNode = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			if (tokenNode != NULL)
			{
				// Try to get right chevron.  Reduces error count when we're typing out a generic argument list
				if (tokenNode->GetToken() == BfToken_RDblChevron)
					tokenNode = BreakDoubleChevron(tokenNode);
				if (tokenNode->GetToken() == BfToken_RChevron)
				{
					MoveNode(tokenNode, genericArgs);
					genericArgs->mCloseChevron = tokenNode;
					mVisitorPos.MoveNext();
				}
			}

			return genericArgs;
		}
		MoveNode(genericArg, genericArgs);
		genericArgsArray.push_back(genericArg);

		nextNode = mVisitorPos.GetNext();
		tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode == NULL)
		{
			FailAfter("Expected ',' or '>'", genericArgs);
			return genericArgs;
		}

		BfToken token = tokenNode->GetToken();
		if (token == BfToken_RDblChevron)
			tokenNode = BreakDoubleChevron(tokenNode);

		if ((token == BfToken_DotDotDot) && (allowPartial))
		{
			commas.push_back(tokenNode);
			mVisitorPos.MoveNext();
			nextNode = mVisitorPos.GetNext();
			tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
			token = tokenNode->GetToken();
		}

		if (token == BfToken_RChevron)
		{
			MoveNode(tokenNode, genericArgs);
			genericArgs->mCloseChevron = tokenNode;
			mVisitorPos.MoveNext();
			break;
		}
		if (token != BfToken_Comma)
		{
			Fail("Either , or > expected", tokenNode);
			return genericArgs;
		}
		MoveNode(tokenNode, genericArgs);
		commas.push_back(tokenNode);
		mVisitorPos.MoveNext();
	}

	return genericArgs;
}

BfGenericParamsDeclaration* BfReducer::CreateGenericParamsDeclaration(BfTokenNode* tokenNode)
{
	auto genericParams = mAlloc->Alloc<BfGenericParamsDeclaration>();
	BfDeferredAstSizedArray<BfIdentifierNode*> genericParamsArr(genericParams->mGenericParams, mAlloc);
	BfDeferredAstSizedArray<BfAstNode*> commas(genericParams->mCommas, mAlloc);
	ReplaceNode(tokenNode, genericParams);
	genericParams->mOpenChevron = tokenNode;
	mVisitorPos.MoveNext();

	while (true)
	{
		auto genericIdentifier = ExpectIdentifierAfter(genericParams, "generic parameters");
		if (genericIdentifier == NULL)
			return genericParams;
		MoveNode(genericIdentifier, genericParams);
		genericParamsArr.push_back(genericIdentifier);

		auto nextNode = mVisitorPos.GetNext();
		tokenNode = BfNodeDynCast<BfTokenNode>(nextNode);
		if (tokenNode == NULL)
		{
			FailAfter("Expected ',' or '>'", genericParams);
			return genericParams;
		}

		BfToken token = tokenNode->GetToken();
		if (token == BfToken_RDblChevron)
			tokenNode = BreakDoubleChevron(tokenNode);

		MoveNode(tokenNode, genericParams);
		mVisitorPos.MoveNext();

		if (token == BfToken_RChevron)
		{
			genericParams->mCloseChevron = tokenNode;
			break;
		}
		if (token != BfToken_Comma)
		{
			Fail("Either , or > expected", tokenNode);
			return genericParams;
		}
		commas.push_back(tokenNode);
	}

	return genericParams;
}

BfGenericConstraintsDeclaration* BfReducer::CreateGenericConstraintsDeclaration(BfTokenNode* tokenNode)
{
	auto constraintsDeclaration = mAlloc->Alloc<BfGenericConstraintsDeclaration>();

	BfDeferredAstSizedArray<BfAstNode*> genericConstraintsArr(constraintsDeclaration->mGenericConstraints, mAlloc);

	bool isDone = false;
	for (int constraintIdx = 0; !isDone; constraintIdx++)
	{
// 		if (auto nextToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
// 		{
// 			if (nextToken->mToken == BfToken_LParen)
// 			{
// 				BfGenericConstraintExpression* genericConstraint = mAlloc->Alloc<BfGenericConstraintExpression>();
// 				ReplaceNode(tokenNode, genericConstraint);
// 				genericConstraint->mWhereToken = tokenNode;
// 				constraintsDeclaration->mHasExpressions = true;
//
// 				genericConstraintsArr.push_back(genericConstraint);
//
// 				auto expr = CreateExpressionAfter(genericConstraint, CreateExprFlags_EarlyExit);
// 				if (expr == NULL)
// 					break;
//
// 				MEMBER_SET(genericConstraint, mExpression, expr);
//
// 				BfTokenNode* nextWhereToken = NULL;
// 				if (auto checkToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext()))
// 				{
// 					if (checkToken->mToken != BfToken_Where)
// 						nextWhereToken = checkToken;
// 				}
//
// 				auto nextNode = mVisitorPos.GetNext();
// 				if (BfNodeDynCast<BfBlock>(nextNode))
// 					break;
//
// 				bool handled = false;
// 				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
// 				{
// 					if (tokenNode->mToken == BfToken_FatArrow)
// 						break;
// 				}
//
// 				tokenNode = ExpectTokenAfter(genericConstraint, BfToken_LBrace, BfToken_Where, BfToken_Semicolon);
// 				if (tokenNode == NULL)
// 					break;
//
// 				BfToken token = tokenNode->GetToken();
// 				if (token != BfToken_Where)
// 				{
// 					mVisitorPos.mReadPos--;
// 					break;
// 				}
//
// 				continue;
// 			}
// 		}

		BfGenericConstraint* genericConstraint = mAlloc->Alloc<BfGenericConstraint>();
		BfDeferredAstSizedArray<BfAstNode*> constraintTypes(genericConstraint->mConstraintTypes, mAlloc);
		BfDeferredAstSizedArray<BfTokenNode*> commas(genericConstraint->mCommas, mAlloc);

		ReplaceNode(tokenNode, genericConstraint);
		genericConstraint->mWhereToken = tokenNode;

		genericConstraintsArr.push_back(genericConstraint);

		auto genericParamName = CreateTypeRefAfter(genericConstraint);
		if (genericParamName != NULL)
		{
			MEMBER_SET(genericConstraint, mTypeRef, genericParamName);
			tokenNode = ExpectTokenAfter(genericConstraint, BfToken_Colon);
		}
		else
			isDone = true;

		if (tokenNode != NULL)
		{
			MEMBER_SET(genericConstraint, mColonToken, tokenNode);
		}
		else
			isDone = true;

		for (int typeIdx = 0; !isDone; typeIdx++)
		{
			if (typeIdx > 0)
			{
				auto nextNode = mVisitorPos.GetNext();
				if (BfNodeDynCast<BfBlock>(nextNode))
				{
					isDone = true;
					break;
				}

				bool handled = false;
				if (auto tokenNode = BfNodeDynCast<BfTokenNode>(nextNode))
				{
					if (tokenNode->mToken == BfToken_FatArrow)
					{
						isDone = true;
						break;
					}
				}

				tokenNode = ExpectTokenAfter(genericConstraint, BfToken_Comma, BfToken_LBrace, BfToken_Where, BfToken_Semicolon);
				if (tokenNode == NULL)
				{
					isDone = true;
					break;
				}
				BfToken token = tokenNode->GetToken();
				if (token == BfToken_Where)
					break;
				if ((token == BfToken_LBrace) || (token == BfToken_Semicolon))
				{
					mVisitorPos.mReadPos--;
					isDone = true;
					break;
				}
				MoveNode(tokenNode, genericConstraint);
				commas.push_back(tokenNode);
			}

			auto nextNode = mVisitorPos.GetNext();
			if (auto constraintToken = BfNodeDynCast<BfTokenNode>(nextNode))
			{
				BfAstNode* constraintNode = NULL;

				bool addToConstraint = false;
				switch (constraintToken->GetToken())
				{
				case BfToken_Class:
				case BfToken_Struct:
				case BfToken_Const:
				case BfToken_Concrete:
				case BfToken_Var:
				case BfToken_New:
				case BfToken_Delete:
				case BfToken_Enum:
				case BfToken_Interface:
					addToConstraint = true;
					break;
				case BfToken_Operator:
				{
					BfGenericOperatorConstraint* opConstraint = mAlloc->Alloc<BfGenericOperatorConstraint>();
					constraintNode = opConstraint;

					ReplaceNode(constraintToken, opConstraint);

					MEMBER_SET(opConstraint, mOperatorToken, constraintToken);
					mVisitorPos.MoveNext();

					auto opToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());
					if (opToken == NULL)
					{
						auto typeRef = CreateTypeRefAfter(opConstraint, BfReducer::CreateTypeRefFlags_SafeGenericParse);
						if (typeRef == NULL)
							break;
						MEMBER_SET(opConstraint, mLeftType, typeRef);
						opToken = BfNodeDynCast<BfTokenNode>(mVisitorPos.GetNext());

						if (opToken == NULL)
						{
							if (auto pointerTypeRef = BfNodeDynCast<BfPointerTypeRef>(typeRef))
							{
								MEMBER_SET(opConstraint, mLeftType, pointerTypeRef->mElementType);
								opToken = pointerTypeRef->mStarNode;
								MEMBER_SET(opConstraint, mOpToken, opToken);
							}
						}
					}

					if (opConstraint->mOpToken == NULL)
					{
						if (opToken == NULL)
						{
							Fail("Conversion operators require either 'implicit' or 'explicit' qualifiers", opConstraint->mOperatorToken);
							break;
						}
						MEMBER_SET(opConstraint, mOpToken, opToken);
						mVisitorPos.MoveNext();
					}

					auto typeRef = CreateTypeRefAfter(opConstraint);
					if (typeRef == NULL)
						break;
					MEMBER_SET(opConstraint, mRightType, typeRef);
				}
				break;
				default: break;
				}

				if (addToConstraint)
				{
					constraintNode = constraintToken;
					bool addToConstraint = false;

					mVisitorPos.MoveNext();
					if (constraintToken->GetToken() == BfToken_Struct)
					{
						addToConstraint = true;
						nextNode = mVisitorPos.GetNext();
						if ((tokenNode = BfNodeDynCast<BfTokenNode>(nextNode)))
						{
							if (tokenNode->GetToken() == BfToken_Star)
							{
								auto tokenPair = mAlloc->Alloc<BfTokenPairNode>();
								ReplaceNode(constraintToken, tokenPair);
								MEMBER_SET(tokenPair, mLeft, constraintToken);
								MEMBER_SET(tokenPair, mRight, tokenNode);

								constraintNode = tokenPair;
								MoveNode(constraintToken, genericConstraint);
								genericConstraint->SetSrcEnd(tokenNode->GetSrcEnd());
								mVisitorPos.MoveNext();
							}
						}
					}
					else if (constraintToken->GetToken() == BfToken_Const)
					{
						constraintTypes.push_back(constraintNode);
						genericConstraint->mSrcEnd = constraintNode->mSrcEnd;

						auto typeRef = CreateTypeRefAfter(nextNode);
						if (typeRef == NULL)
						{
							isDone = true;
							break;
						}

						MoveNode(typeRef, genericConstraint);
						constraintTypes.push_back(typeRef);

						continue;
					}
				}

				if (constraintNode != NULL)
				{
					MoveNode(constraintNode, genericConstraint);
					constraintTypes.push_back(constraintNode);
					continue;
				}
			}

			auto typeRef = CreateTypeRefAfter(genericConstraint);
			if (typeRef == NULL)
			{
				isDone = true;
				break;
			}

			MoveNode(typeRef, genericConstraint);
			constraintTypes.push_back(typeRef);
		}

		if (constraintIdx == 0)
			ReplaceNode(genericConstraint, constraintsDeclaration);
		else
			MoveNode(genericConstraint, constraintsDeclaration);
	}

	return constraintsDeclaration;
}

void BfReducer::HandleBlock(BfBlock* block, bool allowEndingExpression)
{
	//for (auto node : block->mChildren)

	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(block));

	bool isDone = !mVisitorPos.MoveNext();

	BfAstNode* nextNode = NULL;
	while (!isDone)
	{
		BfAstNode* node = mVisitorPos.GetCurrent();

		CreateStmtFlags flags = (CreateStmtFlags)(CreateStmtFlags_FindTrailingSemicolon | CreateStmtFlags_AllowLocalFunction);
		if (allowEndingExpression)
			flags = (CreateStmtFlags)(flags | CreateStmtFlags_AllowUnterminatedExpression);

		auto statement = CreateStatement(node, flags);
		if ((statement == NULL) && (mSource != NULL))
			statement = mSource->CreateErrorNode(node);

		isDone = !mVisitorPos.MoveNext();
		if (statement != NULL)
			mVisitorPos.Write(statement);
	}

	mVisitorPos.Trim();
}

void BfReducer::HandleTypeDeclaration(BfTypeDeclaration* typeDecl, BfAttributeDirective* attributes, BfAstNode* deferredHeadNode)
{
	SetAndRestoreValue<BfTypeDeclaration*> prevTypeDecl(mCurTypeDecl, typeDecl);
	SetAndRestoreValue<BfVisitorPos> prevVisitorPos(mVisitorPos, BfVisitorPos(BfNodeDynCast<BfBlock>(typeDecl->mDefineNode)));

	if (attributes != NULL)
	{
		MEMBER_SET(typeDecl, mAttributes, attributes);
	}

	if ((!IsNodeRelevant(deferredHeadNode, typeDecl)) && (!typeDecl->IsTemporary()))
	{
		typeDecl->mIgnoreDeclaration = true;
		return;
	}

	BfAstNode* prevNode = NULL;

	bool isDone = !mVisitorPos.MoveNext();
	while (!isDone)
	{
		auto node = mVisitorPos.GetCurrent();
		if (node == prevNode)
		{
			BF_FATAL("Should have handled node already");
			// If we're stuck on an error and can't process any more nodes
			break;
		}
		prevNode = node;
		BfAstNode* typeMember = BfNodeDynCast<BfMemberDeclaration>(node);
		if (typeMember == NULL)
		{
			SetAndRestoreValue<BfAstNode*> prevTypeMemberNodeStart(mTypeMemberNodeStart, node);
			typeMember = ReadTypeMember(node);
		}

		//methodDeclaration->mDocumentation = FindDocumentation(methodDeclaration);

		isDone = !mVisitorPos.MoveNext();
		if (typeMember != NULL)
		{
			mVisitorPos.Write(typeMember);
		}
	}
	mVisitorPos.Trim();
}

void BfReducer::HandleRoot(BfRootNode* rootNode)
{
	String fileName;
	auto parser = rootNode->GetSourceData()->ToParserData();
	if (parser != NULL)
		fileName = parser->mFileName;
	BP_ZONE_F("BfReducer::HandleRoot %s", fileName.c_str());

	mAlloc = &rootNode->GetSourceData()->mAlloc;
	mSystem = mSource->mSystem;
	HandleTopLevel(rootNode);
	BfSizedArrayInitIndirect(mSource->mSourceData->mExteriorNodes, mExteriorNodes, mAlloc);
	mAlloc = NULL;

	if (mPassInstance->HasFailed())
		mSource->mParsingFailed = true;
}

static String NodeToString(BfAstNode* node)
{
	return String(&node->GetSourceData()->mSrc[node->GetSrcStart()], node->GetSrcLength());
}

BfInlineAsmStatement* BfReducer::CreateInlineAsmStatement(BfAstNode* asmNode)
{
	auto asmToken = BfNodeDynCast<BfTokenNode>(asmNode);
	auto nextNode = mVisitorPos.GetNext();
	auto blockNode = BfNodeDynCast<BfInlineAsmStatement>(nextNode);
	if (blockNode == NULL)
		return (BfInlineAsmStatement*)Fail("Expected inline assembly block", asmNode);

	ReplaceNode(asmToken, blockNode);

	BfInlineAsmStatement* asmStatement = (BfInlineAsmStatement*)blockNode;

	{
		auto processInstrNodes = [&](const Array<BfAstNode*>& nodes) -> BfInlineAsmInstruction*
		{
			int nodeCount = (int)nodes.size();
			int curNodeIdx = 0;

			auto instNode = mAlloc->Alloc<BfInlineAsmInstruction>();
			//instNode->mSource = asmStatement->mSource;
			int srcStart = nodes.front()->GetSrcStart();
			int srcEnd = nodes.back()->GetSrcEnd();
			instNode->Init(srcStart, srcStart, srcEnd);

			auto replaceWithLower = [](String& s)
			{
				std::transform(s.begin(), s.end(), s.begin(), ::tolower);
			};

			auto readIdent = [&](String& outStr, bool forceLowerCase, const StringImpl& errorExpectation, bool peekOnly) -> bool
			{
				if (curNodeIdx >= nodeCount)
				{
					if (!peekOnly)
						Fail(StrFormat("Expected %s", errorExpectation.c_str()), instNode);
					return false;
				}
				BfAstNode* curNode = nodes[curNodeIdx];
				if (!peekOnly)
					++curNodeIdx;
				if (!BfNodeDynCast<BfIdentifierNode>(curNode))
				{
					if (!peekOnly)
						Fail(StrFormat("Found \"%s\", expected %s", NodeToString(curNode).c_str(), errorExpectation.c_str()), instNode);
					return false;
				}

				outStr = NodeToString(curNode);
				if (forceLowerCase)
					replaceWithLower(outStr);
				return true;
			};
			auto readToken = [&](BfToken tokenType, const StringImpl& errorExpectation, bool peekOnly) -> bool
			{
				if (curNodeIdx >= nodeCount)
				{
					if (!peekOnly)
						Fail(StrFormat("Expected %s", errorExpectation.c_str()), instNode);
					return false;
				}
				BfAstNode* curNode = nodes[curNodeIdx];
				if (!peekOnly)
					++curNodeIdx;
				auto tokenNode = BfNodeDynCast<BfTokenNode>(curNode);
				if (!tokenNode || tokenNode->GetToken() != tokenType)
				{
					if (!peekOnly)
						Fail(StrFormat("Found \"%s\", expected %s", NodeToString(curNode).c_str(), errorExpectation.c_str()), instNode);
					return false;
				}

				return true;
			};
			auto readInteger = [&](int& outInt, const StringImpl& errorExpectation, bool peekOnly, int& outAdvanceTokenCount) -> bool
			{
				int origCurNodeIdx = curNodeIdx;
				outAdvanceTokenCount = 0;

				bool negate = false;
				if (readToken(BfToken_Minus, "", true))
				{
					++curNodeIdx;
					++outAdvanceTokenCount;
					negate = true;
				}

				if (curNodeIdx >= nodeCount)
				{
					if (!peekOnly)
						Fail(StrFormat("Expected %s", errorExpectation.c_str()), instNode);
					else
						curNodeIdx = origCurNodeIdx;

					return false;
				}

				BfAstNode* curNode = nodes[curNodeIdx];
				++curNodeIdx;
				++outAdvanceTokenCount;
				auto litNode = BfNodeDynCast<BfLiteralExpression>(curNode);
				if (!litNode || litNode->mValue.mTypeCode != BfTypeCode_Int32)
				{
					if (!peekOnly)
						Fail(StrFormat("Found \"%s\", expected %s", NodeToString(curNode).c_str(), errorExpectation.c_str()), instNode);
					else
						curNodeIdx = origCurNodeIdx;

					return false;
				}

				outInt = litNode->mValue.mInt32;
				if (negate)
					outInt = -outInt;
				if (peekOnly)
					curNodeIdx = origCurNodeIdx;

				return true;
			};

			auto readArgMemPrimaryExpr = [&](BfInlineAsmInstruction::AsmArg& outArg) -> bool
			{
				String primaryIdent;
				int primaryInt;
				int advanceTokenCount = 0;

				if (readIdent(primaryIdent, false, "", true))
				{
					outArg.mMemFlags = BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg;
					outArg.mReg = primaryIdent;
					++curNodeIdx;
					return true;
				}
				else if (readInteger(primaryInt, "", true, advanceTokenCount))
				{
					outArg.mMemFlags = BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp;
					outArg.mInt = primaryInt;
					curNodeIdx += advanceTokenCount;
					return true;
				}
				else
				{
					Fail(StrFormat("Found \"%s\", expected integer or identifier", NodeToString(nodes[curNodeIdx]).c_str()), instNode);
					return false;
				}
			};
			std::function<bool(BfInlineAsmInstruction::AsmArg&)> readArgMemMulExpr = [&](BfInlineAsmInstruction::AsmArg& outArg) -> bool
			{
				BfInlineAsmInstruction::AsmArg exprArgLeft, exprArgRight;

				if (!readArgMemPrimaryExpr(exprArgLeft))
					return false;

				if (!readToken(BfToken_Star, "", true))
				{
					outArg = exprArgLeft;
					return true;
				}

				++curNodeIdx;
				if (!readArgMemMulExpr(exprArgRight))
					return false;

				bool leftIdent = (exprArgLeft.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg) != 0;
				bool rightIdent = (exprArgRight.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg) != 0;
				if (leftIdent && rightIdent)
				{
					Fail(StrFormat("Memory expressions can only scale by an integer", NodeToString(nodes[curNodeIdx]).c_str()), instNode);
					return false;
				}
				else if (leftIdent || rightIdent)
				{
					if (leftIdent)
					{
						outArg = exprArgLeft;
						outArg.mAdjRegScalar = exprArgRight.mInt;
					}
					else
					{
						outArg = exprArgRight;
						outArg.mAdjRegScalar = exprArgLeft.mInt;
					}

					outArg.mMemFlags &= ~BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg;
					outArg.mMemFlags |= BfInlineAsmInstruction::AsmArg::ARGMEMF_AdjReg;
					outArg.mAdjReg = outArg.mReg;
					outArg.mReg.clear();
				}
				else
				{
					outArg = exprArgLeft;
					outArg.mInt = exprArgLeft.mInt * exprArgRight.mInt;
				}

				return true;
			};
			std::function<bool(BfInlineAsmInstruction::AsmArg&, bool)> readArgMemAddExpr = [&](BfInlineAsmInstruction::AsmArg& outArg, bool subtractLeft) -> bool // can't use 'auto' here since it's recursive
			{
				BfInlineAsmInstruction::AsmArg exprArgLeft, exprArgRight;

				if (!readArgMemMulExpr(exprArgLeft))
					return false;

				if (subtractLeft)
				{
					if (exprArgLeft.mMemFlags != BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp)
					{
						Fail("Memory expressions can only subtract by an integer", instNode);
						return false;
					}

					exprArgLeft.mInt = -exprArgLeft.mInt;
				}

				bool subtract = false;
				if (!readToken(BfToken_Plus, "", true))
				{
					if (!readToken(BfToken_Minus, "", true))
					{
						outArg = exprArgLeft;
						return true;
					}
					else
						subtract = true;
				}

				++curNodeIdx;
				if (!readArgMemAddExpr(exprArgRight, subtract))
					return false;

				bool leftScaling = (exprArgLeft.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_AdjReg) != 0;
				bool rightScaling = (exprArgRight.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_AdjReg) != 0;
				if (leftScaling && rightScaling)
				{
					Fail("Memory expressions can only have one scaling register and one non-scaling register", instNode);
					return false;
				}
				BfInlineAsmInstruction::AsmArg* scaledArg = leftScaling ? &exprArgLeft : (rightScaling ? &exprArgRight : nullptr);

				if (scaledArg)
				{
					BfInlineAsmInstruction::AsmArg* otherArg = leftScaling ? &exprArgRight : &exprArgLeft;

					outArg = *scaledArg;
					outArg.mMemFlags |= otherArg->mMemFlags;
					if (otherArg->mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg)
					{
						if (scaledArg->mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg)
						{
							Fail("Memory expressions can involve at most two registers", instNode);
							return false;
						}
						outArg.mReg = otherArg->mReg;
					}
					if (otherArg->mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp)
					{
						outArg.mInt += otherArg->mInt;
					}
				}
				else
				{
					outArg.mInt = 0;
					if (exprArgLeft.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp)
					{
						outArg.mMemFlags |= BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp;
						outArg.mInt += exprArgLeft.mInt;
					}
					if (exprArgRight.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp)
					{
						outArg.mMemFlags |= BfInlineAsmInstruction::AsmArg::ARGMEMF_ImmediateDisp;
						outArg.mInt += exprArgRight.mInt;
					}

					bool leftIdent = (exprArgLeft.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg) != 0;
					bool rightIdent = (exprArgRight.mMemFlags & BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg) != 0;

					if (leftIdent && rightIdent)
					{
						outArg.mMemFlags |= (BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg | BfInlineAsmInstruction::AsmArg::ARGMEMF_AdjReg);
						outArg.mReg = exprArgLeft.mReg;
						outArg.mAdjReg = exprArgRight.mReg;
						outArg.mAdjRegScalar = 1;
					}
					else if (leftIdent)
					{
						outArg.mMemFlags |= BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg;
						outArg.mReg = exprArgLeft.mReg;
					}
					else if (rightIdent)
					{
						outArg.mMemFlags |= BfInlineAsmInstruction::AsmArg::ARGMEMF_BaseReg;
						outArg.mReg = exprArgRight.mReg;
					}
				}

				return true;
			};

			auto parseArg = [&](BfInlineAsmInstruction::AsmArg& outArg) -> bool
			{
				bool keepGoing = true;
				while (keepGoing)
				{
					keepGoing = false;

					int peekInt;
					String peekStr;
					int advanceTokenCount;
					if (readInteger(peekInt, "", true, advanceTokenCount))
					{
						outArg.mType = BfInlineAsmInstruction::AsmArg::ARGTYPE_Immediate;
						outArg.mInt = peekInt;
						curNodeIdx += advanceTokenCount;
					}
					else if (readIdent(peekStr, false, "", true))
					{
						++curNodeIdx;
						String s(peekStr);
						replaceWithLower(s);

						String tempIdent;

						if ((s == "cs" || s == "ds" || s == "es" || s == "fs" || s == "gs" || s == "ss") && readToken(BfToken_Colon, "", true))
						{
							++curNodeIdx;
							outArg.mSegPrefix = s;
							keepGoing = true;
						}
						else if (s == "st" && readToken(BfToken_LParen, "", true))
						{
							++curNodeIdx;
							outArg.mType = BfInlineAsmInstruction::AsmArg::ARGTYPE_FloatReg;
							if (!readInteger(peekInt, "integer floating-point register number", false, advanceTokenCount))
								return false;
							outArg.mInt = peekInt;
							if (!readToken(BfToken_RParen, "')'", false))
								return false;
						}
						else if ((s == "byte" || s == "word" || s == "dword" || s == "qword" || s == "xword" || s == "xmmword" || s == "opaque") && readIdent(tempIdent, true, "", true))
						{
							if (tempIdent != "ptr")
							{
								Fail(StrFormat("Found \"%s\", expected \"ptr\"", NodeToString(nodes[curNodeIdx]).c_str()), instNode);
								return false;
							}
							++curNodeIdx;
							outArg.mSizePrefix = s;
							keepGoing = true;
						}
						else
						{
							outArg.mType = BfInlineAsmInstruction::AsmArg::ARGTYPE_IntReg;
							outArg.mReg = peekStr;
						}
					}
					else if (readToken(BfToken_LBracket, "", true))
					{
						++curNodeIdx;

						BfInlineAsmInstruction::AsmArg exprArgLeft;
						if (!readArgMemAddExpr(exprArgLeft, false))
							return false;
						if (!readToken(BfToken_RBracket, "']'", false))
							return false;
						if (readToken(BfToken_Dot, "", true))
						{
							++curNodeIdx;
							if (!readIdent(outArg.mMemberSuffix, false, "struct member suffix identifier", false))
								return false;
						}

						outArg.mType = BfInlineAsmInstruction::AsmArg::ARGTYPE_Memory;
						outArg.mMemFlags = exprArgLeft.mMemFlags;
						//outArg.mSegPrefix = already_set_leave_me_alone;
						//outArg.mSizePrefix = already_set_leave_me_alone;
						outArg.mInt = exprArgLeft.mInt;
						outArg.mReg = exprArgLeft.mReg;
						outArg.mAdjReg = exprArgLeft.mAdjReg;
						outArg.mAdjRegScalar = exprArgLeft.mAdjRegScalar;
						//outArg.mMemberSuffix = already_set_leave_me_alone;

						return true;
					}
					else
						return false;
				}

				return true;
			};

			BfInlineAsmInstruction::AsmInst& outInst = instNode->mAsmInst;

			// instruction / instruction prefix / label
			String opStr;
			if (!readIdent(opStr, false, "instruction, instruction prefix, or label", false))
				return nullptr;

			if (readToken(BfToken_Colon, "", true))
			{
				++curNodeIdx;
				outInst.mLabel = opStr;
				if (curNodeIdx >= nodeCount)
					return instNode;

				if (!readIdent(opStr, false, "instruction or instruction prefix", false))
					return nullptr;
			}

			replaceWithLower(opStr);

			// check for instruction prefix(s)
			while (opStr == "lock" || opStr == "rep" || opStr == "repe" || opStr == "repne" || opStr == "repz" || opStr == "repnz")
			{
				if (curNodeIdx >= nodeCount) // in case prefix is listed like a separate instruction
					break;
				outInst.mOpPrefixes.push_back(opStr);

				if (!readIdent(opStr, true, "instruction or instruction prefix", false))
					return nullptr;
			}

			outInst.mOpCode = opStr;

			BfInlineAsmInstruction::AsmArg asmArg;
			while (parseArg(asmArg))
			{
				outInst.mArgs.push_back(asmArg);
				asmArg = BfInlineAsmInstruction::AsmArg();

				if (!readToken(BfToken_Comma, "", true))
					break;
				++curNodeIdx;
			}

			if (curNodeIdx < nodeCount)
				return (BfInlineAsmInstruction*)Fail(StrFormat("Found unexpected \"%s\"", NodeToString(nodes[curNodeIdx]).c_str()), instNode);

			//String testStr = outInst.ToString();

			int unusedLineChar = 0;
			auto bfParser = instNode->GetSourceData()->ToParserData();
			if (bfParser != NULL)
				bfParser->GetLineCharAtIdx(instNode->GetSrcStart(), outInst.mDebugLine, unusedLineChar);
			//return (BfInlineAsmInstruction*)Fail(StrFormat("Line %d\n", outInst.mDebugLine), instNode);

			return instNode;
		};

		// split nodes by newlines into individual instructions, skipping empty lines

		Array<BfAstNode*> instrNodes;
		Array<BfInlineAsmInstruction*> dstInstructions;

		//BCF: Add this back
		/*for (auto child = asmStatement->mChildren.mHead; child; child = child->mNext)
		{
		auto childToken = BfNodeDynCast<BfTokenNode>(child);
		if (childToken && childToken->GetToken() == BfToken_AsmNewline)
		{
		if (!instrNodes.empty())
		{
		BfInlineAsmInstruction* instNode = processInstrNodes(instrNodes);
		if (instNode)
		dstInstructions.push_back(instNode);
		instrNodes.clear();
		}
		}
		else if (childToken && childToken->GetToken() == BfToken_Asm)
		{
		// ignore the actual 'asm' keyword as we no longer need it
		continue; //CDH FIXME this is because ReplaceNode adds the original 'asm' keyword as a child, which I don't need; maybe I'm missing how the CST->AST replacment pattern is intended to work?
		}
		else
		{
		instrNodes.push_back(child);
		}
		}*/

		if (!instrNodes.empty())
		{
			BfInlineAsmInstruction* instNode = processInstrNodes(instrNodes);
			if (instNode)
				dstInstructions.push_back(instNode);
			instrNodes.Clear();
		}

		for (auto & instNode : dstInstructions)
			MoveNode(instNode, asmStatement);
		asmStatement->mInstructions = std::move(dstInstructions);
	}

	return asmStatement;
}