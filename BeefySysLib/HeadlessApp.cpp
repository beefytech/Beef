#include "HeadlessApp.h"
#include "platform/PlatformHelper.h"

USING_NS_BF;

void HeadlessApp::Init()
{
    mRunning = true;

	Beefy::String exePath;
	BfpGetStrHelper(exePath, [](char* outStr, int* inOutStrSize, BfpResult* result)
	{
		BfpSystem_GetExecutablePath(outStr, inOutStrSize, (BfpSystemResult*)result);
	});

	mInstallDir = GetFileDir(exePath) + "/";
}

void HeadlessApp::Run()
{
	while (mRunning)
	{
        BfpThread_Sleep((uint32)(1000 / mRefreshRate));
		Process();
	}
}
