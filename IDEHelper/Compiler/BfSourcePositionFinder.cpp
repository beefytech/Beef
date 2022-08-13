#include "BfSourcePositionFinder.h"
#include "BfParser.h"

USING_NS_BF;

BfSourcePositionFinder::BfSourcePositionFinder(BfParser* bfParser, int findPosition)
{
	mParser = bfParser;
	mFindPosition = findPosition;
	mClosestElement = NULL;
}

void BfSourcePositionFinder::Visit(BfAstNode* node)
{
	if ((mFindPosition >= node->GetSrcStart()) && (mFindPosition <= node->GetSrcEnd()))
		mClosestElement = node;
}