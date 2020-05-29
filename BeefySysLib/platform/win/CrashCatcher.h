#pragma once

#include "../../BeefySysLib/Common.h"
#include "../util/CritSect.h"

NS_BF_BEGIN

typedef void(*CrashInfoFunc)();

class CrashCatcher
{
public:
	Array<CrashInfoFunc> mCrashInfoFuncs;
	String mCrashInfo;
	bool mCrashed;
	bool mInitialized;
	CritSect mBfpCritSect;	
	EXCEPTION_POINTERS* mExceptionPointers;
	LPTOP_LEVEL_EXCEPTION_FILTER mPreviousFilter;
	bool mDebugError;
	BfpCrashReportKind mCrashReportKind;	

public:
	CrashCatcher();

	virtual void Init();
	virtual void AddCrashInfoFunc(CrashInfoFunc crashInfoFunc);
	virtual void AddInfo(const StringImpl& str);

	virtual void Test();
	virtual void Crash(const StringImpl& str);
	virtual void SetCrashReportKind(BfpCrashReportKind crashReportKind);

	static CrashCatcher* Get();
};

NS_BF_END
