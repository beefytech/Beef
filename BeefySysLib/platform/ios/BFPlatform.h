#pragma once

#define BFSTDCALL

#include "../darwin/DarwinCommon.h"
#include "TargetConditionals.h"

#ifndef __IPHONEOS__
#define __IPHONEOS__
#endif

#define BF_PLATFORM_IOS
#define BF_PLATFORM_POSIX
#define BF_PLATFORM_OPENGL_ES2
#define BF_PLATFORM_FULLSCREEN

#if !TARGET_IPHONE_SIMULATOR
#ifdef __LP64__
#ifdef __I386__
#define BF_PLATFORM_X64
#else
#define BF_PLATFORM_ARM64
#endif
#define BF64
#else ////
#ifdef __I386__
#define BF_PLATFORM_I386
#else
#define BF_PLATFORM_ARM32
#endif
#define BF32
#endif

#else

#ifdef __LP64__
#define BF_PLATFORM_X64
#define BF64
#else ////
#define BF_PLATFORM_I386
#define BF32
#endif

#endif

#ifdef BFSYSLIB_DYNAMIC
#define BF_EXPORT extern "C" __declspec(dllexport)
#define BF_CALLTYPE
#else
#define BF_EXPORT extern "C"
#define BF_CALLTYPE
#endif

#ifdef BF_PLATFORM_ARM32
#define BF_REGISTER_COUNT 15
#elif defined BF32
#define BF_REGISTER_COUNT 7
#else
#define BF_REGISTER_COUNT 15
#endif

