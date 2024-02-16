#include "X86Target.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

BF_EXPORT void BF_CALLTYPE Targets_Create()
{
#ifndef __linux__
	gX86Target = new X86Target();
#endif
}

BF_EXPORT void BF_CALLTYPE Targets_Delete()
{
#ifndef __linux__
	delete gX86Target;
	gX86Target = NULL;
#endif
}