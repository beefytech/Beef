#include "X86Target.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

BF_EXPORT void BF_CALLTYPE Targets_Create()
{
	gX86Target = new X86Target();
}

BF_EXPORT void BF_CALLTYPE Targets_Delete()
{
	delete gX86Target;
	gX86Target = NULL;
}