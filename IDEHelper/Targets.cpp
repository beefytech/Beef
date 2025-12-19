#include "X86Target.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

BF_EXPORT void BF_CALLTYPE Targets_Create()
{
#if !defined(__linux__) && !defined(__APPLE__)
	gX86Target = new X86Target();
#endif
}

BF_EXPORT void BF_CALLTYPE Targets_Delete()
{
#if !defined(__linux__) && !defined(__APPLE__)
	delete gX86Target;
	gX86Target = NULL;
#endif
}