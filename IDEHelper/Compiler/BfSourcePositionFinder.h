#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BfSystem.h"
#include "BfElementVisitor.h"

NS_BF_BEGIN

class BfSourcePositionFinder : public BfElementVisitor
{
public:
	BfParser* mParser;
	int mFindPosition;
	BfAstNode* mClosestElement;

public:
	BfSourcePositionFinder(BfParser* bfParser, int findPosition);

	using BfStructuralVisitor::Visit;
	virtual void Visit(BfAstNode* node) override;
};

NS_BF_END