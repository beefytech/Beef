#include "BfVarDeclChecker.h"

USING_NS_BF;

BfVarDeclChecker::BfVarDeclChecker()
{
	mHasVarDecl = false;
}

void BfVarDeclChecker::Visit(BfVariableDeclaration * binOpExpr)
{
	mHasVarDecl = true;
}
