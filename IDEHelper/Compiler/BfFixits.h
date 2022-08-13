#pragma once

#include "BfSystem.h"
#include "BfAst.h"
#include "BfParser.h"

NS_BF_BEGIN

class BfFixitFinder : public BfElementVisitor
{
public:
	static int FindLineStartAfter(BfSourceData* source, int idx)
	{
		bool hadBr = false;
		while (idx < source->mSrcLength)
		{
			char c = source->mSrc[idx];
			if (c == '\n')
			{
				idx++;
				break;
			}
			idx++;
		}
		return idx;
	}

	static int FindLineStartAfter(BfAstNode* node)
	{
		return FindLineStartAfter(node->GetSourceData(), node->GetSrcEnd());
	}

	static int FindLineStartBefore(BfSourceData* source, int idx)
	{
		bool hadBr = false;
		while (idx >= 0)
		{
			char c = source->mSrc[idx];
			if (c == '\n')
			{
				idx++;
				break;
			}
			idx--;
		}
		return idx;
	}

	static int FindLineStartBefore(BfAstNode* node)
	{
		return FindLineStartBefore(node->GetSourceData(), node->GetSrcStart());
	}
};

class BfUsingFinder : public BfFixitFinder
{
public:
	int mFromIdx;
	int mLastIdx;

public:
	BfUsingFinder()
	{
		mLastIdx = 0;
		mFromIdx = -1;
	}

	virtual void Visit(BfUsingDirective* usingDirective) override
	{
		mLastIdx = FindLineStartAfter(usingDirective->GetSourceData(), usingDirective->GetSrcEnd());
	}

	virtual void Visit(BfNamespaceDeclaration* namespaceDecl) override
	{
		if (mFromIdx != -1)
		{
			if ((mFromIdx < namespaceDecl->mSrcStart) || (mFromIdx >= namespaceDecl->mSrcEnd))
			{
				// Not inside
				return;
			}
		}

		BfFixitFinder::Visit(namespaceDecl);
	}
};

NS_BF_END