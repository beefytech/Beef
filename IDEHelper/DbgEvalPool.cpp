#include "DbgEvalPool.h"

USING_NS_BF_DBG;

DbgEvalPool::Entry* DbgEvalPool::Add(const std::string& expr, int callStackIdx, int cursorPos, bool allowAssignment, bool allowCalls, int expressionFlags)
{
	Entry* entry = new Entry();
	entry->mExpr = expr;
	entry->mCallStackIdx = callStackIdx;
	//entry->mCursor
	entry->mAllowAssignment = allowAssignment;
	entry->mAllowCall = allowCalls;
	entry->mExpressionFlags = expressionFlags;



	return NULL;
}