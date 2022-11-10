#pragma once

#define BFSTDCALL

#include "LinuxCommon.h"

#define BF_PLATFORM_LINUX
#define BF_PLATFORM_POSIX
#define BF_PLATFORM_NAME "BF_PLATFORM_LINUX"

#define BF_IMPORT extern "C"

#if (defined(__arm__) || defined(__aarch64__))
#define BF_PLATFORM_OPENGL_ES2
#define BF_PLATFORM_ARM
#endif

#ifdef BFSYSLIB_DYNAMIC
#define BF_EXPORT extern "C"
#define BF_CALLTYPE
#else
#define BF_EXPORT extern "C"
#define BF_CALLTYPE
#define BF_RESOURCES_REL_DIR "../Resources"
#endif

#define BF_DEBUG_BREAK()

#include "../PlatformInterface.h"
