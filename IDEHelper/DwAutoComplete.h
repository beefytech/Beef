#pragma once

#include "DebugCommon.h"
#include "Compiler/BfAutoComplete.h"
#include "BeefySysLib/util/Array.h"

NS_BF_DBG_BEGIN

class DbgSubprogram;
class DbgType;
typedef Beefy::Array<DbgType*> DwTypeVector;

class DwAutoComplete : public Beefy::AutoCompleteBase
{
public:
	class MethodMatchEntry
	{
	public:
		DwTypeVector mDwGenericArguments;
		DbgSubprogram* mDwSubprogram;
	};

	class MethodMatchInfo
	{
	public:
		Beefy::Array<MethodMatchEntry> mInstanceList;
		int mBestIdx;
		int mPrevBestIdx;
		bool mHadExactMatch;
		int mMostParamsMatched;
		Beefy::Array<int> mSrcPositions; // start, commas, end

	public:
		MethodMatchInfo()
		{
			mBestIdx = 0;
			mPrevBestIdx = -1;
			mHadExactMatch = false;
			mMostParamsMatched = 0;
		}
	};

	MethodMatchInfo* mMethodMatchInfo;
	bool mIsCapturingMethodMatchInfo;

public:
	DwAutoComplete()
	{
		mMethodMatchInfo = NULL;
		mIsCapturingMethodMatchInfo = false;
	}

	~DwAutoComplete()
	{
		delete mMethodMatchInfo;
	}
};

NS_BF_DBG_END