#include "BfTargetTriple.h"

USING_NS_BF;

BfTargetTriple::BfTargetTriple()
{
	mParsed = true;
	mMachineType = BfMachineType_Unknown;
}

BfTargetTriple::BfTargetTriple(const StringImpl& targetTriple)
{
	mParsed = false;
	mMachineType = BfMachineType_Unknown;
	mTargetTriple = targetTriple;
}

void BfTargetTriple::Parse()
{
	if (mTargetTriple.StartsWith("x86_64"))
		mMachineType = BfMachineType_x64;
	else if ((mTargetTriple.StartsWith("i686")) || (mTargetTriple.StartsWith("x86")))
		mMachineType = BfMachineType_x64;
	else if ((mTargetTriple.StartsWith("aarch64")) || (mTargetTriple.StartsWith("arm64")))
		mMachineType = BfMachineType_AArch64;
	else
		mMachineType = BfMachineType_Unknown;
	mParsed = true;
}

void BfTargetTriple::Set(const StringImpl& targetTriple)
{
	mTargetTriple = targetTriple;
	mParsed = false;
}

BfMachineType BfTargetTriple::GetMachineType()
{
	if (!mParsed)
		Parse();
	return mMachineType;
}