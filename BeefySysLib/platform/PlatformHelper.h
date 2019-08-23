#pragma once

#include "../Common.h"

#define OUTRESULT(res) do { if (outResult != NULL) *outResult = (res); } while (0)

static bool TryStringOut(const Beefy::String& str, char* outStr, int* inOutSize)
{
	if ((outStr == NULL) || (*inOutSize < str.length() + 1))
	{
		if ((outStr != NULL) && (*inOutSize != 0))
			outStr[0] = 0; // Return empty string
		*inOutSize = (int)str.length() + 1;
		return false;
	}

	*inOutSize = (int)str.length() + 1;
	memcpy(outStr, str.c_str(), (int)str.length() + 1);
	return true;
}

static bool TryStringOut(const Beefy::String& str, char* outStr, int* inOutSize, BfpResult* outResult)
{
	if (TryStringOut(str, outStr, inOutSize))
	{
		OUTRESULT(BfpResult_Ok);
		return true;
	}
	else
	{
		OUTRESULT(BfpResult_InsufficientBuffer);
		return false;
	}
}

static BfpResult BfpGetStrHelper(Beefy::StringImpl& outStr, std::function<void(char* outStr, int* inOutStrSize, BfpResult* result)> func)
{
	const int initSize = 4096;
	char localBuf[initSize];

	int strSize = initSize;
	BfpResult result = BfpResult_Ok;
	func(localBuf, &strSize, &result);

	if (result == BfpResult_Ok)
	{
		outStr.Append(localBuf, strSize - 1);
		return BfpResult_Ok;
	}
	else if (result == BfpResult_InsufficientBuffer)
	{
		while (true)
		{
			char* localBufPtr = (char*)malloc(strSize);
			func(localBufPtr, &strSize, &result);
			if (result == BfpResult_InsufficientBuffer)
			{
				free(localBufPtr);
				continue;
			}
			outStr.Append(localBuf, strSize - 1);
			free(localBufPtr);
			return BfpResult_Ok;
		}
	}

	return result;
}

#define BFP_GETSTR_HELPER(STRNAME, __RESULT, CMD) \
	{ \
		int strLen = 0; \
		int* __STRLEN = &strLen; \
		char* __STR = NULL; \
		CMD; \
		if ((BfpResult)__RESULT == BfpResult_InsufficientBuffer) \
		{ \
			STRNAME.Reserve(strLen); \
			__STR = STRNAME.GetMutablePtr(); \
			CMD; \
			STRNAME.mLength = strLen - 1; \
		}\
	}
