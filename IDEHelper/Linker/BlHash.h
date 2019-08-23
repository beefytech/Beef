#pragma once

#include "../Beef/BfCommon.h"

NS_BF_BEGIN

class BlHash
{
public:
	static uint32 HashStr_PdbV1(const char* str, int len = -1);
	static uint32 HashStr_PdbV2(const char* str);
	static uint32 GetSig_Pdb(void* data, int len);
	static uint32 GetTypeHash(uint8*& data);
};

NS_BF_END
