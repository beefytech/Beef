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
#include <mach/error.h>

//#define errno (*__error())

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

#define BF_PLATFORM_LINUX
#define BF_PLATFORM_NAME "BF_PLATFORM_LINUX"
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

#if 0

#pragma once

#ifdef __LP64__
#define BF64
#else
#define BF32
#endif

#define BOOST_DETAIL_NO_CONTAINER_FWD

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
#include <libkern/OSAtomic.h>
#include <cstdlib>
#include <unistd.h>
#include <wchar.h>
#include <math.h>

extern "C"
{
#define FFI_BUILDING
#include "third_party/libffi/x86_64-apple-darwin12.5.0/include/ffi.h"
}

#define FFI_STDCALL FFI_DEFAULT_ABI
#define FFI_THISCALL FFI_DEFAULT_ABI
#define FFI_FASTCALL FFI_DEFAULT_ABI

typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int64_t int64;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
typedef unsigned int uint;

#define BF_PLATFORM_DARWIN
#define BF_PLATFORM_SDL

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
typedef uint32 DWORD;
typedef int32 LONG;

typedef pthread_key_t BFTlsKey;
typedef pthread_t BF_THREADID;

int64 abs(int64 val) noexcept;

struct IID
{
    unsigned long  Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char  Data4[ 8 ];
};

typedef void* HANDLE;
typedef void* HMODULE;

#include "../notwin/NotWin.h"

#ifdef DEBUG
#define _DEBUG
#endif

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

#define BF_ASSERT assert
#define BF_FATAL(msg) assert(msg == 0)

#define BF_NOINLINE __attribute__ ((noinline))
#define BF_NAKED

#define _alloca alloca

namespace Beefy
{

class CritSect
{
private:
    pthread_mutex_t mCriticalSection;
    
public:
    CritSect(void)
    {
        pthread_mutexattr_t		attributes;
        
        pthread_mutexattr_init(&attributes);
        pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mCriticalSection, &attributes);
        pthread_mutexattr_destroy(&attributes);
    }
    
    ~CritSect(void)
    {
        pthread_mutex_destroy(&mCriticalSection);
    }
    
    bool TryLock()
    {
        return pthread_mutex_trylock( &mCriticalSection ) == 0;
    }
    
    void Lock()
    {
        pthread_mutex_lock( &mCriticalSection );
    }
    
    void Unlock()
    {
        pthread_mutex_unlock(&mCriticalSection);
    }
};

class SyncEvent
{
private:
    pthread_mutex_t mMutex;
    pthread_cond_t mCondition;
    uint32 mSet;
    bool mManualReset;
    
    bool mInitialState;
    int mSetCount;
    int mWaitForCountFail;
    int mWaitForCountSucceed;
    
public:
    SyncEvent(bool manualReset = false, bool initialState = false)
    {
        mManualReset = manualReset;
        mSet = initialState;
        int result = pthread_mutex_init(&mMutex, NULL);
        BF_ASSERT(result == 0);
        result = pthread_cond_init(&mCondition, NULL);
        BF_ASSERT(result == 0);
        
        mInitialState = initialState;
        mSetCount = 0;
        mWaitForCountFail = 0;
        mWaitForCountSucceed = 0;
    }
    
    ~SyncEvent()
    {
        pthread_cond_destroy(&mCondition);
        pthread_mutex_destroy(&mMutex);
    }
    
    void Set()
    {
        pthread_mutex_lock(&mMutex);
        mSet = true;
        pthread_cond_signal(&mCondition);
        pthread_mutex_unlock(&mMutex);
        
        mSetCount++;
    }
    
    void Reset()
    {
        mSet = false;
    }
    
    bool WaitFor(uint32 timeout)
    {
        int result = pthread_mutex_lock(&mMutex);
        BF_ASSERT(result == 0);
        while (!mSet)
        {
            struct timespec ts;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            
            uint64 nsec = (uint64)timeout * 1000000;
            ts.tv_nsec = tv.tv_usec * 1000;
            ts.tv_sec = tv.tv_sec;
            
            ts.tv_nsec += nsec % 1000000000;
            ts.tv_sec += nsec / 1000000000;
            
            ts.tv_sec += (ts.tv_nsec / 1000000000);
            ts.tv_nsec %= 1000000000;
            
            result = pthread_cond_timedwait(&mCondition, &mMutex, &ts);
            
            if (timeout == (uint32)-1)
                BF_ASSERT(result == 0);
            
            if (result != 0)
            {
                // Timeout
                mWaitForCountFail++;
                pthread_mutex_unlock(&mMutex);
                return false;
            }
        }
        if (!mManualReset)
            mSet = false;
        mWaitForCountSucceed++;
        pthread_mutex_unlock(&mMutex);
        return true;
    }
};

}

inline uint32 InterlockedCompareExchange(volatile uint32* dest, uint32 exch, uint32 comp)
{
    while (true)
    {
        if (OSAtomicCompareAndSwap32Barrier((int32)comp, (int32)exch, (volatile int32*)dest))
            return comp;
        // We don't want to return *dest being equal to 'comp' if the CAS result was false
        uint32 oldVal = *dest;
        if (oldVal != comp)
            return oldVal;
    }
}

inline uint64 InterlockedCompareExchange64(volatile int64* dest, int64 exch, int64 comp)
{
    while (true)
    {
        if (OSAtomicCompareAndSwap64Barrier((int64)comp, (int64)exch, (volatile int64*)dest))
            return comp;
        // We don't want to return *dest being equal to 'comp' if the CAS result was false
        uint64 oldVal = *dest;
        if (oldVal != comp)
            return oldVal;
    }
}

inline void* InterlockedCompareExchangePointer(void* volatile* dest, void* exch, void* comp)
{
    while (true)
    {
        if (OSAtomicCompareAndSwapPtrBarrier(comp, exch, dest))
            return comp;
        // We don't want to return *dest being equal to 'comp' if the CAS result was false
        void* oldVal = *dest;
        if (oldVal != comp)
            return oldVal;
    }
}

inline uint32 InterlockedExchange(volatile uint32* dest, uint32 val)
{
    while (true)
    {
        uint32 oldVal = *dest;
        if (OSAtomicCompareAndSwap32Barrier((int32)oldVal, (int32)val, (volatile int32*)dest))
            return oldVal;
    }
}

inline uint64 InterlockedExchange64(volatile int64* dest, int64 val)
{
    while (true)
    {
        uint64 oldVal = *dest;
        if (OSAtomicCompareAndSwap64Barrier((int64)oldVal, (int64)val, (volatile int64*)dest))
            return oldVal;
    }
}

inline uint32 InterlockedExchangeAdd(volatile uint32* dest, uint32 val)
{
    return 0;
}

inline int32 InterlockedIncrement(volatile uint32* val)
{
    return OSAtomicIncrement32Barrier((int32*)val);
}

inline int64 InterlockedIncrement64(volatile int64* val)
{
    return OSAtomicIncrement64Barrier(val);
}

inline int32 InterlockedDecrement(volatile uint32* val)
{
    return OSAtomicDecrement32Barrier((int32*)val);
}

inline int64 InterlockedDecrement64(volatile int64* val)
{
    return OSAtomicDecrement64Barrier(val);
}

#endif