#pragma once

#define BFSTDCALL __stdcall

#include "../darwin/DarwinCommon.h"

#define BF_PLATFORM_OSX

#ifdef BFSYSLIB_DYNAMIC
#define BF_EXPORT extern "C" __declspec(dllexport)
#define BF_CALLTYPE
#else
#define BF_EXPORT extern "C"
#define BF_CALLTYPE
#define BF_RESOURCES_REL_DIR "../Resources"
#endif

#ifdef BF32
#define BF_REGISTER_COUNT 7
#else
#define BF_REGISTER_COUNT 15
#endif

