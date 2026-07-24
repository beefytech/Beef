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

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if ((defined(__clang__)) && (__has_builtin(__builtin_debugtrap)))
#define BF_DEBUG_BREAK() __builtin_debugtrap()
#elif (defined(__GNUC__))
#include <signal.h>
#ifdef SIGTRAP
#define BF_DEBUG_BREAK() raise(SIGTRAP)
#endif
#endif

#ifndef BF_DEBUG_BREAK
#define BF_DEBUG_BREAK()
#endif

#include "../PlatformInterface.h"
