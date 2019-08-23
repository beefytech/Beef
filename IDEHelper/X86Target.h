#pragma once

#include "DebugCommon.h"
#include "X86.h"
#include "X64.h"

NS_BF_BEGIN

class X86Target
{
public:
	X86CPU* mX86CPU;
	X64CPU* mX64CPU;

public:
	X86Target();
	~X86Target();
};

extern X86Target* gX86Target;

NS_BF_END