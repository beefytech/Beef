#pragma once

#include "../../BeefySysLib/Common.h"

NS_BF_BEGIN

typedef void(*CrashInfoFunc)();

class CrashCatcher
{
public:
	CrashCatcher();

	void Init();
	void AddCrashInfoFunc(CrashInfoFunc crashInfoFunc);
	void AddInfo(const StringImpl& str);

	void Test();
	void Crash(const StringImpl& str);	
	void SetCrashReportKind(BfpCrashReportKind crashReportKind);
};

NS_BF_END
