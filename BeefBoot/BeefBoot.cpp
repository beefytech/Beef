//#define USE_OLD_BEEFBUILD

#pragma warning(disable:4996)

#include <iostream>
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/SizedArray.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/CabUtil.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/Deque.h"
#include "BeefySysLib/util/HashSet.h"
#include "BeefySysLib/util/MultiHashSet.h"

//#define TEST_CRASH
#ifdef TEST_CRASH
#include "CrashCatcher.h"
#endif

//#include <mmsystem.h>
//#include <shellapi.h>
//#include <Objbase.h>

#define BF_DBG_64
#include "IDEHelper/StrHashMap.h"

using namespace Beefy;

#include "BootApp.h"

BF_IMPORT void BF_CALLTYPE Debugger_ProgramDone();

int main(int argc, char* argv[])
{	
#ifdef TEST_CRASH
	CrashCatcher catcher;
	catcher.SetCrashReportKind(BfpCrashReportKind_GUI);
	catcher.Test();
#endif

	BfpSystem_SetCommandLine(argc, argv);

	BfpThread_SetName(NULL, "MainThread", NULL);

    BfpSystem_Init(BFP_VERSION, BfpSystemInitFlag_InstallCrashCatcher);
	
	gApp = new BootApp();

	bool success = true;
	for (int i = 1; i < argc; i++)
	{			
		std::string arg = argv[i];
				
		if (arg[0] == '"')
		{
			arg.erase(0, 1);
			if ((arg.length() > 1) && (arg[arg.length() - 1] == '"'))
				arg.erase(arg.length() - 1);
			success &= gApp->HandleCmdLine(arg, "");
			continue;
		}
		
		int eqPos = (int)arg.find('=');
		if (eqPos == -1)
		{
			success &= gApp->HandleCmdLine(arg, "");
			continue;
		}

		std::string cmd = arg.substr(0, eqPos);
		std::string param = arg.substr(eqPos + 1);
		if ((param.length() > 1) && (param[0] == '"'))
		{
			param.erase(0, 1);
			if ((param.length() > 1) && (param[param.length() - 1] == '"'))
				param.erase(param.length() - 1);
		}
		success &= gApp->HandleCmdLine(cmd, param);		
	}
	
	if (!gApp->mShowedHelp)
	{
		if (success)
			success = gApp->Init();
		if (success)
			success = gApp->Compile();

		if (success)
			gApp->OutputLine("SUCCESS", OutputPri_Critical);
		else
			gApp->OutputLine("FAILED", OutputPri_Critical);
	}

	delete gApp;		

	Debugger_ProgramDone();

    BfpSystem_Shutdown();

	BP_SHUTDOWN();

	return success ? 0 : 1;
}
