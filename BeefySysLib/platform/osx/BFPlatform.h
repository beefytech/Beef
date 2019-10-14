#pragma once

#define BFSTDCALL

#include "../darwin/DarwinCommon.h"
#include <string>

#define BF_PLATFORM_OSX

#define BF_IMPORT extern "C"

#ifdef BFSYSLIB_DYNAMIC
#define BF_EXPORT extern "C"
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

#define BF_DEBUG_BREAK()

#include "../PlatformInterface.h"