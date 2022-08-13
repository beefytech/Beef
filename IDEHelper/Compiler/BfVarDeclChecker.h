#pragma once

#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BfElementVisitor.h"

NS_BF_BEGIN

class BfVarDeclChecker : public BfElementVisitor
{
public:
	bool mHasVarDecl;

public:
	BfVarDeclChecker();

	virtual void Visit(BfVariableDeclaration* binOpExpr) override;
};

NS_BF_END