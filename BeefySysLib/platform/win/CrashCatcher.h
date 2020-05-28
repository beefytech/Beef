#pragma once

#include "../../BeefySysLib/Common.h"
#include "../util/CritSect.h"

NS_BF_BEGIN

typedef void(*CrashInfoFunc)();

class CrashCatcher
{
public:
	Array<CrashInfoFunc> mCrashInfoFuncs;
	StringT<0> mCrashInfo;
	bool mCrashed;
	bool mInitialized;
	CritSect mBfpCritSect;	
	EXCEPTION_POINTERS* mExceptionPointers;
	LPTOP_LEVEL_EXCEPTION_FILTER mPreviousFilter;
	bool mDebugError;
	BfpCrashReportKind mCrashReportKind;

public:
	CrashCatcher();

	void Init();
	void AddCrashInfoFunc(CrashInfoFunc crashInfoFunc);
	void AddInfo(const StringImpl& str);

	void Test();
	void Crash(const StringImpl& str);	
	void SetCrashReportKind(BfpCrashReportKind crashReportKind);	

	static CrashCatcher* Get();
};

NS_BF_END
