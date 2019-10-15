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

	/*for (int i = 0; i < argc; i++)
	{
		if (i != 0)
			std::cout << " ";
		std::cout << argv[i];
	}	
	std::cout << std::endl;*/

	String cmd;

	bool success = true;
	for (int i = 1; i < argc; i++)
	{
		String arg = argv[i];
		if (arg.StartsWith("--"))
			arg.Remove(0, 1);

		if (!cmd.IsEmpty())
		{
			cmd.Append('=');
			arg.Insert(0, cmd);
			cmd.Clear();
		}

		if (arg[0] == '"')
		{
			arg.Remove(0, 1);
			if ((arg.length() > 1) && (arg[arg.length() - 1] == '"'))
				arg.RemoveToEnd(arg.length() - 1);
			success &= gApp->HandleCmdLine(arg, "");
			continue;
		}
		
		int eqPos = (int)arg.IndexOf('=');
		if (eqPos == -1)
		{
			success &= gApp->HandleCmdLine(arg, "");
			continue;
		}

		cmd = arg.Substring(0, eqPos);
		if (eqPos == arg.length() - 1)
			continue;
		String param = arg.Substring(eqPos + 1);
		if ((param.length() > 1) && (param[0] == '"'))
		{
			param.Remove(0, 1);
			if ((param.length() > 1) && (param[param.length() - 1] == '"'))
				param.Remove(param.length() - 1);
		}
		success &= gApp->HandleCmdLine(cmd, param);
		cmd.Clear();
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
