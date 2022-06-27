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

#define BF_DBG_64
#include "IDEHelper/StrHashMap.h"

using namespace Beefy;

#include "FuzzApp.h"

BF_IMPORT void BF_CALLTYPE IDEHelper_ProgramStart();
BF_IMPORT void BF_CALLTYPE IDEHelper_ProgramDone();

static void FuzzInit()
{
	BfpSystem_SetCommandLine(0, NULL);

	BfpThread_SetName(NULL, "MainThread", NULL);

	BfpSystem_Init(BFP_VERSION, BfpSystemInitFlag_None);

	IDEHelper_ProgramStart();

	gApp = new FuzzApp();
	gApp->SetTargetPath("fuzz_testd");
	gApp->AddDefine("CLI");
	gApp->AddDefine("DEBUG");
	gApp->SetStartupObject("fuzz_test.Program");
	gApp->SetLinkParams("./libBeefRT_d.a ./libBeefySysLib_d.so  -ldl -lpthread -Wl,-rpath -Wl,$ORIGIN");
	
	BF_ASSERT(gApp->Init());
}

void trimwhitespace(const uint8_t*& str, size_t& len)
{
	while (len > 0 && isspace(*str))
	{
		str++;
		len--;
	}

	const uint8_t* end = str + len - 1;
	while (len > 0 && isspace(*end))
	{
		end--;
		len--;
	}
}

static bool init = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
	if (!init)
	{
		init = true;
		FuzzInit();
	}

	trimwhitespace(Data, Size);
	if (Size == 0)
		return 0;

	gApp->PrepareCompiler();

	bool ready = gApp->QueueFile((char*)Data, Size);

	//if (ready)
	//	ready = gApp->QueuePath("./corlib/src");

	//if (ready)
	//	gApp->Compile();

	gApp->ReleaseCompiler();
	
	return 0;
}