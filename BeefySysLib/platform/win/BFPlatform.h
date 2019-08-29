#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if (defined __MINGW32__) && (!defined BF_MINGW)
#define BF_MINGW
#endif

#define WIN32_LEAN_AND_MEAN
#define BF_PLATFORM_WINDOWS
#define BF_PLATFORM_NAME "BF_PLATFORM_WINDOWS"

#ifdef BF_MINGW
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunknown-attributes"
#pragma clang diagnostic ignored "-Wunused-member-function"
#pragma clang diagnostic ignored "-Wunused-conversion-function"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#else
#define BF_VC
#endif

#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <assert.h>
#include <wtypes.h>

#define DIR_SEP_CHAR '\\'
#define DIR_SEP_CHAR_ALT '/'

#ifndef BF_NO_FFI
extern "C"
{
	#define FFI_BUILDING
	#include "libffi/i686-pc-cygwin/include/ffi.h"
}
#endif

//#define BF_FORCE_SDL

#ifdef SDL_FORCE_OPENGL_ES2
#define BF_PLATFORM_OPENGL_ES2
#endif

#if (_MSC_VER == 1800) && (!defined BFSYSLIB_STATIC)
#define BF_WWISE_ENABLED
#endif

#define CPP11

#ifdef _DEBUG
#define BF_DEBUG
//#define DEBUG
#endif

#ifdef BF_MINGW
#define NOP asm volatile("nop");
extern "C" _CRTIMP int __cdecl __MINGW_NOTHROW	_stricmp (const char*, const char*);
#elif defined _WIN64
#define BF64
#define NOP GetTickCount()
#else
#define BF32
#define NOP __asm nop;
#endif

#define BF_HAS_TLS_DECLSPEC
#ifdef BF_MINGW
#define BF_TLS_DECLSPEC __thread
#else
#define BF_TLS_DECLSPEC __declspec(thread)
#endif

#if defined _DEBUG || defined BF_DEBUG_ASSERTS
#define BF_ASSERT(_Expression) (void)( (!!(_Expression)) || (Beefy::BFFatalError(#_Expression, __FILE__, __LINE__), 0) )
#else
#define BF_ASSERT(_Expression) (void)(0)
#endif

#define BF_ASSERT_REL(_Expression) (void)( (!!(_Expression)) || (Beefy::BFFatalError(#_Expression, __FILE__, __LINE__), 0) )
#define BF_FATAL(msg) (void) ((Beefy::BFFatalError(msg, __FILE__, __LINE__), 0) )

#if defined _DEBUG || defined BF_DEBUG_ASSERTS
#define BF_DBG_FATAL(msg) (void) ((Beefy::BFFatalError(msg, __FILE__, __LINE__), 0) )
#else
#define BF_DBG_FATAL(msg) 
#endif

#define BFSTDCALL __stdcall 

#define BF_IMPORT extern "C" __declspec(dllimport)

#ifdef BFSYSLIB_DYNAMIC

#define BF_EXPORT extern "C" __declspec(dllexport)
#define BF_CALLTYPE __stdcall
#ifdef BFP_NOEXPORT
#define BFP_EXPORT extern "C"
#define BFP_CALLTYPE __stdcall
#endif

#else
#define BF_EXPORT extern "C"
#define BF_CALLTYPE __stdcall
#endif

#define BF_NOINLINE __declspec(noinline)
#define BF_NAKED __declspec(naked)

#define BF_PACKED(x) __pragma(pack(push, 1)) x __pragma(pack(pop))
#define BF_ALIGNED(x) __declspec(align(x))

#ifndef BF_MINGW
#define strtoull _strtoui64
#endif
//#define snprintf _snprintf
#define __func__ __FUNCTION__

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef unsigned int uint;

typedef intptr_t intptr;
typedef uintptr_t uintptr;

typedef DWORD BF_THREADID;
typedef HANDLE BF_THREADHANDLE;

#ifdef BF_MINGW
#define BF_COMPILER_FENCE() __asm__ __volatile__("mfence" : : : "memory");
#define BF_FULL_MEMORY_FENCE() __asm__ __volatile__("mfence" : : : "memory");
#define BF_SPINWAIT_NOP() __asm__ __volatile__( "pause;" )
#define BF_DEBUG_BREAK() __asm__ __volatile__( "int $0x03;" )
#else
#define BF_COMPILER_FENCE() _ReadWriteBarrier()
//#define BF_FULL_MEMORY_FENCE() __asm { lock add dword ptr [esp],0 }

#ifdef BF32
#define BF_FULL_MEMORY_FENCE() ::MemoryBarrier()
#else
#define BF_FULL_MEMORY_FENCE() _mm_mfence()
#endif

//#define BF_SPINWAIT_NOP() _asm { pause }
#define BF_SPINWAIT_NOP() _mm_pause()
#define BF_DEBUG_BREAK() DebugBreak()
#endif

typedef DWORD BfTLSKey;
#define BfTLSGetValue ::TlsGetValue
#define BfTLSSetValue ::TlsSetValue
#define BfTLSAlloc ::TlsAlloc
#define BfTLSFree ::TlsFree

#define BF_THREAD_YIELD() ::SwitchToThread()

#define BF_UNUSED
#define BF_EXPLICIT explicit

#define BF_ENDIAN_LITTLE

#define WaitForSingleObject_Thread WaitForSingleObject
#define CloseHandle_File CloseHandle
#define CloseHandle_Event CloseHandle
#define CloseHandle_Thread CloseHandle
#define CloseHandle_Process CloseHandle

#ifdef BF32
#define BF_REGISTER_COUNT 7
#else
#define BF_REGISTER_COUNT 15
#endif

#ifndef BF_MINGW
#define __thread __declspec(thread)
#endif

#ifdef BF_MINGW
//
#else
#define strcasecmp stricmp
#endif

struct BfpEvent
{
	CRITICAL_SECTION mCritSect;
	CONDITION_VARIABLE mCondVariable;
	bool mSet;
	bool mManualReset;
};

#include "../PlatformInterface.h"