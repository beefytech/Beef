#pragma once

#include "BfSystem.h"

NS_BF_BEGIN

class BfTargetTriple
{
public:
	String mTargetTriple;
	bool mParsed;
	BfMachineType mMachineType;

public:
	void Parse();

public:
	BfTargetTriple();
	BfTargetTriple(const StringImpl& targetTriple);
	void Set(const StringImpl& targetTriple);
	BfMachineType GetMachineType();
};

NS_BF_END