#include "BeefySysLib/Common.h"
#include "BfAst.h"
#include "BfSystem.h"
#include "BfCompiler.h"
#include "BfExprEvaluator.h"

NS_BF_BEGIN

class BfModule;
class BfMethodMatcher;

enum BfConstResolveFlags
{
	BfConstResolveFlag_None = 0,
	BfConstResolveFlag_ExplicitCast = 1,
	BfConstResolveFlag_NoCast = 2,
	BfConstResolveFlag_AllowSoftFail = 4,
	BfConstResolveFlag_RemapFromStringId = 8
};

class BfConstResolver : public BfExprEvaluator
{
public:
	bool mIsInvalidConstExpr;
	bool mAllowGenericConstValue;

public:
	virtual bool CheckAllowValue(const BfTypedValue& typedValue, BfAstNode* refNode) override;

public:
	BfConstResolver(BfModule* bfModule);		

	BfTypedValue Resolve(BfExpression* expr, BfType* wantType = NULL, BfConstResolveFlags flags = BfConstResolveFlag_None);
	bool PrepareMethodArguments(BfAstNode* targetSrc, BfMethodMatcher* methodMatcher, Array<BfIRValue>& llvmArgs);		
};

NS_BF_END