#pragma once

#include "Debugger.h"

NS_BF_DBG_BEGIN

class DbgEvalPool
{
public:
	class Entry
	{
	public:
		std::string mExpr;
		int mCallStackIdx;
		//int mCursorPos;
		int mAllowAssignment;
		int mAllowCall;
		int mExpressionFlags;
	};	

public:
	std::vector<Entry*> mEntryList;

public:
	~DbgEvalPool();

	Entry* Add(const std::string& expr, int callStackIdx, int cursorPos, bool allowAssignment, bool allowCalls, int expressionFlags);
};

NS_BF_DBG_END
