#pragma once

#ifdef __LP64__
#define BF64
#else
#define BF32
#endif

#define BOOST_DETAIL_NO_CONTAINER_FWD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
//#include <libkern/OSAtomic.h>
#include <cstdlib>
#include <unistd.h>
#include <wchar.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <wctype.h>
#include <stddef.h>

//#define offsetof(type, member)  __builtin_offsetof (type, member)

extern "C"
{
//#define FFI_BUILDING
//#include "third_party/libffi/x86_64-apple-darwin12.5.0/include/ffi.h"
}

#define BF_ENDIAN_LITTLE

#define _NOEXCEPT noexcept
#define NTAPI

#define FFI_STDCALL FFI_DEFAULT_ABI
#define FFI_THISCALL FFI_DEFAULT_ABI
#define FFI_FASTCALL FFI_DEFAULT_ABI

#define INVALID_SOCKET -1

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef unsigned int uint;

//#define BF_PLATFORM_SDL

#define NOP
//#define BF_NOTHROW throw ()
//#define BF_NOTHROW noexcept
#define BF_NOTHROW

#ifdef BF64
typedef int64 intptr;
typedef uint64 uintptr;
#else
typedef int32 intptr;
typedef uint32 uintptr;
#endif

typedef wchar_t* BSTR;
typedef int HRESULT;
typedef uint8 BYTE;
typedef uint16 WORD;
typedef uint32 DWORD;
typedef int32 LONG;

typedef pthread_key_t BFTlsKey;
typedef pthread_t BF_THREADID;
typedef pthread_t BF_THREADHANDLE;

#define BF_HAS_TLS_DECLSPEC
#define BF_TLS_DECLSPEC thread_local

//:int64 abs(int64 val);

#define _stricmp stricmp
#define strnicmp strncasecmp

struct IID
{
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[ 8 ];
};

typedef void* HANDLE;
typedef void* HMODULE;

// We only need the stdcall attribute for x32?
//#define BFSTDCALL __attribute__((stdcall))

//#include "../notwin/NotWin.h"

#ifdef DEBUG
#define _DEBUG
#endif

#define NOT_IMPL throw "Unimplemented";

//ARM

#if defined(__x86_64__) || defined(__i386__)
#define BF_FULL_MEMORY_FENCE() __asm__ __volatile__("mfence": : :"memory")
#define BF_SPINWAIT_NOP() __asm__ volatile ("pause\n" : : : "memory" );
#else
#define BF_FULL_MEMORY_FENCE() __sync_synchronize()
#define BF_SPINWAIT_NOP() ((void) 0)
#endif

#define BF_COMPILER_FENCE() __asm__ __volatile__("": : :"memory")
#define BF_THREAD_YIELD() sched_yield()

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

#define BF_NOINLINE __attribute__ ((noinline))
#define BF_NAKED

#define stricmp strcasecmp
#define _alloca alloca

#define DIR_SEP_CHAR '/'
#define DIR_SEP_CHAR_ALT '\\'

static char* itoa(int value, char* str, int base)
{
    if (base == 16)
        sprintf(str, "%X", value);
    else
        sprintf(str, "%d", value);
    return str;
}

inline uint32 InterlockedCompareExchange(volatile uint32* dest, uint32 exch, uint32 comp)
{
	return __sync_val_compare_and_swap(dest, comp, exch);
}

inline uint64 InterlockedCompareExchange64(volatile int64* dest, int64 exch, int64 comp)
{
	return __sync_val_compare_and_swap(dest, comp, exch);
}

inline void* InterlockedCompareExchangePointer(void* volatile* dest, void* exch, void* comp)
{
	return __sync_val_compare_and_swap(dest, comp, exch);
}

inline uint32 InterlockedExchange(volatile uint32* dest, uint32 val)
{
	return __sync_lock_test_and_set(dest, val);
}

inline uint64 InterlockedExchange64(volatile int64* dest, int64 val)
{
	return __sync_lock_test_and_set(dest, val);
}

inline uint32 InterlockedExchangeAdd(volatile uint32* dest, uint32 val)
{
	return __sync_add_and_fetch(dest, val);
}

inline int32 InterlockedIncrement(volatile uint32* val)
{
    return __sync_add_and_fetch(val, 1);
}

inline int64 InterlockedIncrement64(volatile int64* val)
{
	return __sync_add_and_fetch(val, 1);
}

inline int32 InterlockedDecrement(volatile uint32* val)
{
	return __sync_add_and_fetch(val, -1);
}

inline int64 InterlockedDecrement64(volatile int64* val)
{
	return __sync_add_and_fetch(val, -1);
}
