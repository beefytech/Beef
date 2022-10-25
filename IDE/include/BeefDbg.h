#pragma once

#ifdef BEEFDBG_IMPLEMENTATION

struct BfDebugMessageData
{
	int mMessageType; // 0 = none, 1 = error
	int mStackWindbackCount;
	int mBufParamLen;
	const char* mBufParam;
	void* mPCOverride;
};

extern "C"
{
	BfDebugMessageData gBfDebugMessageData;
}

extern "C" __declspec(dllimport) void DebugBreak();

void BfProfileStart(int sampleRatea = 1000)
{
	char str[128];
	sprintf(str, "StartSampling\t0\t%d\t0");
	gBfDebugMessageData.mBufParam = str;
	gBfDebugMessageData.mBufParamLen = strlen(gBfDebugMessageData.mBufParam);
	DebugBreak();
}

void BfProfileEnd()
{
	gBfDebugMessageData.mBufParam = "StopSampling\t0";
	gBfDebugMessageData.mBufParamLen = strlen(gBfDebugMessageData.mBufParam);
	DebugBreak();
}

#else

void BfProfileStart(int sampleRatea = 1000);
void BfProfileEnd();

#endif