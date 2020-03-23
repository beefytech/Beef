#include "Hash.h"

#pragma warning(disable:4804)

/*
xxHash - Fast Hash algorithm
Copyright (C) 2012-2014, Yann Collet.
BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

You can contact the author at :
- xxHash source repository : http://code.google.com/p/xxhash/
- public discussion board : https://groups.google.com/forum/#!forum/lz4c
*/


//**************************************
// Tuning parameters
//**************************************
// Unaligned memory access is automatically enabled for "common" CPU, such as x86.
// For others CPU, the compiler will be more cautious, and insert extra code to ensure aligned access is respected.
// If you know your target CPU supports unaligned memory access, you want to force this option manually to improve performance.
// You can also enable this parameter if you know your input data will always be aligned (boundaries of 4, for U32).
#if defined(__ARM_FEATURE_UNALIGNED) || defined(__i386) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
#  define XXH_USE_UNALIGNED_ACCESS 1
#endif

// XXH_ACCEPT_NULL_INPUT_POINTER :
// If the input pointer is a null pointer, xxHash default behavior is to trigger a memory access error, since it is a bad pointer.
// When this option is enabled, xxHash output for null input pointers will be the same as a null-length input.
// This option has a very small performance cost (only measurable on small inputs).
// By default, this option is disabled. To enable it, uncomment below define :
// #define XXH_ACCEPT_NULL_INPUT_POINTER 1

// XXH_FORCE_NATIVE_FORMAT :
// By default, xxHash library provides endian-independant Hash values, based on little-endian convention.
// Results are therefore identical for little-endian and big-endian CPU.
// This comes at a performance cost for big-endian CPU, since some swapping is required to emulate little-endian format.
// Should endian-independance be of no importance for your application, you may set the #define below to 1.
// It will improve speed for Big-endian CPU.
// This option has no impact on Little_Endian CPU.
#define XXH_FORCE_NATIVE_FORMAT 0

//**************************************
// Compiler Specific Options
//**************************************
// Disable some Visual warning messages
#ifdef _MSC_VER  // Visual Studio
#  pragma warning(disable : 4127)      // disable: C4127: conditional expression is constant
#endif

#ifdef _MSC_VER    // Visual Studio
#  define FORCE_INLINE static __forceinline
#else
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif

//**************************************
// Includes & Memory related functions
//**************************************
typedef struct { long long ll[6]; } XXH32_state_t;
typedef struct { long long ll[11]; } XXH64_state_t;
typedef enum { XXH_OK=0, XXH_ERROR } XXH_errorcode;

// Modify the local functions below should you wish to use some other memory routines
// for malloc(), free()
#include <stdlib.h>
FORCE_INLINE void* XXH_malloc(size_t s) { return malloc(s); }
FORCE_INLINE void  XXH_free  (void* p)  { free(p); }
// for memcpy()
#include <string.h>
FORCE_INLINE void* XXH_memcpy(void* dest, const void* src, size_t size)
{
    return memcpy(dest,src,size);
}


//**************************************
// Basic Types
//**************************************
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   // C99
# include <stdint.h>
typedef uint8_t  BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef  int32_t S32;
typedef uint64_t U64;
#else
typedef unsigned char      BYTE;
typedef unsigned short     U16;
typedef unsigned int       U32;
typedef   signed int       S32;
typedef unsigned long long U64;
#endif

#if defined(__GNUC__)  && !defined(XXH_USE_UNALIGNED_ACCESS)
#  define _PACKED __attribute__ ((packed))
#else
#  define _PACKED
#endif

#if !defined(XXH_USE_UNALIGNED_ACCESS) && !defined(__GNUC__)
#  ifdef __IBMC__
#    pragma pack(1)
#  else
#    pragma pack(push, 1)
#  endif
#endif

typedef struct _U32_S
{
    U32 v;
} _PACKED U32_S;
typedef struct _U64_S
{
    U64 v;
} _PACKED U64_S;

#if !defined(XXH_USE_UNALIGNED_ACCESS) && !defined(__GNUC__)
#  pragma pack(pop)
#endif

#define A32(x) (((U32_S *)(x))->v)
#define A64(x) (((U64_S *)(x))->v)


//***************************************
// Compiler-specific Functions and Macros
//***************************************
#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

// Note : although _rotl exists for minGW (GCC under windows), performance seems poor
#if defined(_MSC_VER)
#  define XXH_rotl32(x,r) _rotl(x,r)
#  define XXH_rotl64(x,r) _rotl64(x,r)
#else
#  define XXH_rotl32(x,r) ((x << r) | (x >> (32 - r)))
#  define XXH_rotl64(x,r) ((x << r) | (x >> (64 - r)))
#endif

#if defined(_MSC_VER)     // Visual Studio
#  define XXH_swap32 _byteswap_ulong
#  define XXH_swap64 _byteswap_uint64
#elif GCC_VERSION >= 403
#  define XXH_swap32 __builtin_bswap32
#  define XXH_swap64 __builtin_bswap64
#else
static inline U32 XXH_swap32 (U32 x)
{
    return  ((x << 24) & 0xff000000 ) |
            ((x <<  8) & 0x00ff0000 ) |
            ((x >>  8) & 0x0000ff00 ) |
            ((x >> 24) & 0x000000ff );
}
static inline U64 XXH_swap64 (U64 x)
{
    return  ((x << 56) & 0xff00000000000000ULL) |
            ((x << 40) & 0x00ff000000000000ULL) |
            ((x << 24) & 0x0000ff0000000000ULL) |
            ((x << 8)  & 0x000000ff00000000ULL) |
            ((x >> 8)  & 0x00000000ff000000ULL) |
            ((x >> 24) & 0x0000000000ff0000ULL) |
            ((x >> 40) & 0x000000000000ff00ULL) |
            ((x >> 56) & 0x00000000000000ffULL);
}
#endif


//**************************************
// Constants
//**************************************
#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

#define PRIME64_1 11400714785074694791ULL
#define PRIME64_2 14029467366897019727ULL
#define PRIME64_3  1609587929392839161ULL
#define PRIME64_4  9650029242287828579ULL
#define PRIME64_5  2870177450012600261ULL

//**************************************
// Architecture Macros
//**************************************
typedef enum { XXH_bigEndian=0, XXH_littleEndian=1 } XXH_endianess;
#ifndef XXH_CPU_LITTLE_ENDIAN   // It is possible to define XXH_CPU_LITTLE_ENDIAN externally, for example using a compiler switch
static const int one = 1;
#   define XXH_CPU_LITTLE_ENDIAN   (*(char*)(&one))
#endif


//**************************************
// Macros
//**************************************
#define XXH_STATIC_ASSERT(c)   { enum { XXH_static_assert = 1/(!!(c)) }; }    // use only *after* variable declarations


//****************************
// Memory reads
//****************************
typedef enum { XXH_aligned, XXH_unaligned } XXH_alignment;

FORCE_INLINE U32 XXH_readLE32_align(const U32* ptr, XXH_endianess endian, XXH_alignment align)
{
    if (align==XXH_unaligned)
        return endian==XXH_littleEndian ? A32(ptr) : XXH_swap32(A32(ptr));
    else
        return endian==XXH_littleEndian ? *ptr : XXH_swap32(*ptr);
}

FORCE_INLINE U32 XXH_readLE32(const U32* ptr, XXH_endianess endian)
{
    return XXH_readLE32_align(ptr, endian, XXH_unaligned);
}

FORCE_INLINE U64 XXH_readLE64_align(const U64* ptr, XXH_endianess endian, XXH_alignment align)
{
    if (align==XXH_unaligned)
        return endian==XXH_littleEndian ? A64(ptr) : XXH_swap64(A64(ptr));
    else
        return endian==XXH_littleEndian ? *ptr : XXH_swap64(*ptr);
}

FORCE_INLINE U64 XXH_readLE64(const U64* ptr, XXH_endianess endian)
{
    return XXH_readLE64_align(ptr, endian, XXH_unaligned);
}


//****************************
// Simple Hash Functions
//****************************
FORCE_INLINE U32 XXH32_endian_align(const void* input, size_t len, U32 seed, XXH_endianess endian, XXH_alignment align)
{
    const BYTE* p = (const BYTE*)input;
    const BYTE* bEnd = p + len;
    U32 h32;
#define XXH_get32bits(p) XXH_readLE32_align((const U32*)p, endian, align)

#ifdef XXH_ACCEPT_NULL_INPUT_POINTER
    if (p==NULL)
    {
        len=0;
        bEnd=p=(const BYTE*)(size_t)16;
    }
#endif

    if (len>=16)
    {
        const BYTE* const limit = bEnd - 16;
        U32 v1 = seed + PRIME32_1 + PRIME32_2;
        U32 v2 = seed + PRIME32_2;
        U32 v3 = seed + 0;
        U32 v4 = seed - PRIME32_1;

        do
        {
            v1 += XXH_get32bits(p) * PRIME32_2;
            v1 = XXH_rotl32(v1, 13);
            v1 *= PRIME32_1;
            p+=4;
            v2 += XXH_get32bits(p) * PRIME32_2;
            v2 = XXH_rotl32(v2, 13);
            v2 *= PRIME32_1;
            p+=4;
            v3 += XXH_get32bits(p) * PRIME32_2;
            v3 = XXH_rotl32(v3, 13);
            v3 *= PRIME32_1;
            p+=4;
            v4 += XXH_get32bits(p) * PRIME32_2;
            v4 = XXH_rotl32(v4, 13);
            v4 *= PRIME32_1;
            p+=4;
        }
        while (p<=limit);

        h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
    }
    else
    {
        h32  = seed + PRIME32_5;
    }

    h32 += (U32) len;

    while (p+4<=bEnd)
    {
        h32 += XXH_get32bits(p) * PRIME32_3;
        h32  = XXH_rotl32(h32, 17) * PRIME32_4 ;
        p+=4;
    }

    while (p<bEnd)
    {
        h32 += (*p) * PRIME32_5;
        h32 = XXH_rotl32(h32, 11) * PRIME32_1 ;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}


unsigned int XXH32 (const void* input, size_t len, unsigned seed)
{
#if 0
    // Simple version, good for code maintenance, but unfortunately slow for small inputs
    XXH32_state_t state;
    XXH32_reset(&state, seed);
    XXH32_update(&state, input, len);
    return XXH32_digest(&state);
#else
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

#  if !defined(XXH_USE_UNALIGNED_ACCESS)
    if ((((size_t)input) & 3) == 0)   // Input is aligned, let's leverage the speed advantage
    {
        if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
            return XXH32_endian_align(input, len, seed, XXH_littleEndian, XXH_aligned);
        else
            return XXH32_endian_align(input, len, seed, XXH_bigEndian, XXH_aligned);
    }
#  endif

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_endian_align(input, len, seed, XXH_littleEndian, XXH_unaligned);
    else
        return XXH32_endian_align(input, len, seed, XXH_bigEndian, XXH_unaligned);
#endif
}

FORCE_INLINE U64 XXH64_endian_align(const void* input, size_t len, U64 seed, XXH_endianess endian, XXH_alignment align)
{
    const BYTE* p = (const BYTE*)input;
    const BYTE* bEnd = p + len;
    U64 h64;
#define XXH_get64bits(p) XXH_readLE64_align((const U64*)p, endian, align)

#ifdef XXH_ACCEPT_NULL_INPUT_POINTER
    if (p==NULL)
    {
        len=0;
        bEnd=p=(const BYTE*)(size_t)32;
    }
#endif

    if (len>=32)
    {
        const BYTE* const limit = bEnd - 32;
        U64 v1 = seed + PRIME64_1 + PRIME64_2;
        U64 v2 = seed + PRIME64_2;
        U64 v3 = seed + 0;
        U64 v4 = seed - PRIME64_1;

        do
        {
            v1 += XXH_get64bits(p) * PRIME64_2;
            p+=8;
            v1 = XXH_rotl64(v1, 31);
            v1 *= PRIME64_1;
            v2 += XXH_get64bits(p) * PRIME64_2;
            p+=8;
            v2 = XXH_rotl64(v2, 31);
            v2 *= PRIME64_1;
            v3 += XXH_get64bits(p) * PRIME64_2;
            p+=8;
            v3 = XXH_rotl64(v3, 31);
            v3 *= PRIME64_1;
            v4 += XXH_get64bits(p) * PRIME64_2;
            p+=8;
            v4 = XXH_rotl64(v4, 31);
            v4 *= PRIME64_1;
        }
        while (p<=limit);

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);

        v1 *= PRIME64_2;
        v1 = XXH_rotl64(v1, 31);
        v1 *= PRIME64_1;
        h64 ^= v1;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v2 *= PRIME64_2;
        v2 = XXH_rotl64(v2, 31);
        v2 *= PRIME64_1;
        h64 ^= v2;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v3 *= PRIME64_2;
        v3 = XXH_rotl64(v3, 31);
        v3 *= PRIME64_1;
        h64 ^= v3;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v4 *= PRIME64_2;
        v4 = XXH_rotl64(v4, 31);
        v4 *= PRIME64_1;
        h64 ^= v4;
        h64 = h64 * PRIME64_1 + PRIME64_4;
    }
    else
    {
        h64  = seed + PRIME64_5;
    }

    h64 += (U64) len;

    while (p+8<=bEnd)
    {
        U64 k1 = XXH_get64bits(p);
        k1 *= PRIME64_2;
        k1 = XXH_rotl64(k1,31);
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = XXH_rotl64(h64,27) * PRIME64_1 + PRIME64_4;
        p+=8;
    }

    if (p+4<=bEnd)
    {
        h64 ^= (U64)(XXH_get32bits(p)) * PRIME64_1;
        h64 = XXH_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p+=4;
    }

    while (p<bEnd)
    {
        h64 ^= (*p) * PRIME64_5;
        h64 = XXH_rotl64(h64, 11) * PRIME64_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}


unsigned long long XXH64 (const void* input, size_t len, unsigned long long seed)
{
#if 0
    // Simple version, good for code maintenance, but unfortunately slow for small inputs
    XXH64_state_t state;
    XXH64_reset(&state, seed);
    XXH64_update(&state, input, len);
    return XXH64_digest(&state);
#else
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

#  if !defined(XXH_USE_UNALIGNED_ACCESS)
    if ((((size_t)input) & 7)==0)   // Input is aligned, let's leverage the speed advantage
    {
        if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
            return XXH64_endian_align(input, len, seed, XXH_littleEndian, XXH_aligned);
        else
            return XXH64_endian_align(input, len, seed, XXH_bigEndian, XXH_aligned);
    }
#  endif

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_endian_align(input, len, seed, XXH_littleEndian, XXH_unaligned);
    else
        return XXH64_endian_align(input, len, seed, XXH_bigEndian, XXH_unaligned);
#endif
}

/****************************************************
 *  Advanced Hash Functions
****************************************************/

/*** Allocation ***/
typedef struct
{
    U64 total_len;
    U32 seed;
    U32 v1;
    U32 v2;
    U32 v3;
    U32 v4;
    U32 memsize;
    char memory[16];
} XXH_istate32_t;

typedef struct
{
    U64 total_len;
    U64 seed;
    U64 v1;
    U64 v2;
    U64 v3;
    U64 v4;
    U32 memsize;
    char memory[32];
} XXH_istate64_t;


XXH32_state_t* XXH32_createState(void)
{
    XXH_STATIC_ASSERT(sizeof(XXH32_state_t) >= sizeof(XXH_istate32_t));   // A compilation error here means XXH32_state_t is not large enough
    return (XXH32_state_t*)XXH_malloc(sizeof(XXH32_state_t));
}
XXH_errorcode XXH32_freeState(XXH32_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
};

XXH64_state_t* XXH64_createState(void)
{
    XXH_STATIC_ASSERT(sizeof(XXH64_state_t) >= sizeof(XXH_istate64_t));   // A compilation error here means XXH64_state_t is not large enough
    return (XXH64_state_t*)XXH_malloc(sizeof(XXH64_state_t));
}
XXH_errorcode XXH64_freeState(XXH64_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
};


/*** Hash feed ***/

XXH_errorcode XXH32_reset(XXH32_state_t* state_in, U32 seed)
{
    XXH_istate32_t* state = (XXH_istate32_t*) state_in;
    state->seed = seed;
    state->v1 = seed + PRIME32_1 + PRIME32_2;
    state->v2 = seed + PRIME32_2;
    state->v3 = seed + 0;
    state->v4 = seed - PRIME32_1;
    state->total_len = 0;
    state->memsize = 0;
    return XXH_OK;
}

XXH_errorcode XXH64_reset(XXH64_state_t* state_in, unsigned long long seed)
{
    XXH_istate64_t* state = (XXH_istate64_t*) state_in;
    state->seed = seed;
    state->v1 = seed + PRIME64_1 + PRIME64_2;
    state->v2 = seed + PRIME64_2;
    state->v3 = seed + 0;
    state->v4 = seed - PRIME64_1;
    state->total_len = 0;
    state->memsize = 0;
    return XXH_OK;
}


FORCE_INLINE XXH_errorcode XXH32_update_endian (XXH32_state_t* state_in, const void* input, size_t len, XXH_endianess endian)
{
    XXH_istate32_t* state = (XXH_istate32_t *) state_in;
    const BYTE* p = (const BYTE*)input;
    const BYTE* const bEnd = p + len;

#ifdef XXH_ACCEPT_NULL_INPUT_POINTER
    if (input==NULL) return XXH_ERROR;
#endif

    state->total_len += len;

    if (state->memsize + len < 16)   // fill in tmp buffer
    {
        XXH_memcpy(state->memory + state->memsize, input, len);
        state->memsize += (U32)len;
        return XXH_OK;
    }

    if (state->memsize)   // some data left from previous update
    {
        XXH_memcpy(state->memory + state->memsize, input, 16-state->memsize);
        {
            const U32* p32 = (const U32*)state->memory;
            state->v1 += XXH_readLE32(p32, endian) * PRIME32_2;
            state->v1 = XXH_rotl32(state->v1, 13);
            state->v1 *= PRIME32_1;
            p32++;
            state->v2 += XXH_readLE32(p32, endian) * PRIME32_2;
            state->v2 = XXH_rotl32(state->v2, 13);
            state->v2 *= PRIME32_1;
            p32++;
            state->v3 += XXH_readLE32(p32, endian) * PRIME32_2;
            state->v3 = XXH_rotl32(state->v3, 13);
            state->v3 *= PRIME32_1;
            p32++;
            state->v4 += XXH_readLE32(p32, endian) * PRIME32_2;
            state->v4 = XXH_rotl32(state->v4, 13);
            state->v4 *= PRIME32_1;
            p32++;
        }
        p += 16-state->memsize;
        state->memsize = 0;
    }

    if (p <= bEnd-16)
    {
        const BYTE* const limit = bEnd - 16;
        U32 v1 = state->v1;
        U32 v2 = state->v2;
        U32 v3 = state->v3;
        U32 v4 = state->v4;

        do
        {
            v1 += XXH_readLE32((const U32*)p, endian) * PRIME32_2;
            v1 = XXH_rotl32(v1, 13);
            v1 *= PRIME32_1;
            p+=4;
            v2 += XXH_readLE32((const U32*)p, endian) * PRIME32_2;
            v2 = XXH_rotl32(v2, 13);
            v2 *= PRIME32_1;
            p+=4;
            v3 += XXH_readLE32((const U32*)p, endian) * PRIME32_2;
            v3 = XXH_rotl32(v3, 13);
            v3 *= PRIME32_1;
            p+=4;
            v4 += XXH_readLE32((const U32*)p, endian) * PRIME32_2;
            v4 = XXH_rotl32(v4, 13);
            v4 *= PRIME32_1;
            p+=4;
        }
        while (p<=limit);

        state->v1 = v1;
        state->v2 = v2;
        state->v3 = v3;
        state->v4 = v4;
    }

    if (p < bEnd)
    {
        XXH_memcpy(state->memory, p, bEnd-p);
        state->memsize = (int)(bEnd-p);
    }

    return XXH_OK;
}

XXH_errorcode XXH32_update (XXH32_state_t* state_in, const void* input, size_t len)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_update_endian(state_in, input, len, XXH_littleEndian);
    else
        return XXH32_update_endian(state_in, input, len, XXH_bigEndian);
}



FORCE_INLINE U32 XXH32_digest_endian (const XXH32_state_t* state_in, XXH_endianess endian)
{
    XXH_istate32_t* state = (XXH_istate32_t*) state_in;
    const BYTE * p = (const BYTE*)state->memory;
    BYTE* bEnd = (BYTE*)state->memory + state->memsize;
    U32 h32;

    if (state->total_len >= 16)
    {
        h32 = XXH_rotl32(state->v1, 1) + XXH_rotl32(state->v2, 7) + XXH_rotl32(state->v3, 12) + XXH_rotl32(state->v4, 18);
    }
    else
    {
        h32  = state->seed + PRIME32_5;
    }

    h32 += (U32) state->total_len;

    while (p+4<=bEnd)
    {
        h32 += XXH_readLE32((const U32*)p, endian) * PRIME32_3;
        h32  = XXH_rotl32(h32, 17) * PRIME32_4;
        p+=4;
    }

    while (p<bEnd)
    {
        h32 += (*p) * PRIME32_5;
        h32 = XXH_rotl32(h32, 11) * PRIME32_1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}


U32 XXH32_digest (const XXH32_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH32_digest_endian(state_in, XXH_bigEndian);
}


FORCE_INLINE XXH_errorcode XXH64_update_endian (XXH64_state_t* state_in, const void* input, size_t len, XXH_endianess endian)
{
    XXH_istate64_t * state = (XXH_istate64_t *) state_in;
    const BYTE* p = (const BYTE*)input;
    const BYTE* const bEnd = p + len;

#ifdef XXH_ACCEPT_NULL_INPUT_POINTER
    if (input==NULL) return XXH_ERROR;
#endif

    state->total_len += len;

    if (state->memsize + len < 32)   // fill in tmp buffer
    {
        XXH_memcpy(state->memory + state->memsize, input, len);
        state->memsize += (U32)len;
        return XXH_OK;
    }

    if (state->memsize)   // some data left from previous update
    {
        XXH_memcpy(state->memory + state->memsize, input, 32-state->memsize);
        {
            const U64* p64 = (const U64*)state->memory;
            state->v1 += XXH_readLE64(p64, endian) * PRIME64_2;
            state->v1 = XXH_rotl64(state->v1, 31);
            state->v1 *= PRIME64_1;
            p64++;
            state->v2 += XXH_readLE64(p64, endian) * PRIME64_2;
            state->v2 = XXH_rotl64(state->v2, 31);
            state->v2 *= PRIME64_1;
            p64++;
            state->v3 += XXH_readLE64(p64, endian) * PRIME64_2;
            state->v3 = XXH_rotl64(state->v3, 31);
            state->v3 *= PRIME64_1;
            p64++;
            state->v4 += XXH_readLE64(p64, endian) * PRIME64_2;
            state->v4 = XXH_rotl64(state->v4, 31);
            state->v4 *= PRIME64_1;
            p64++;
        }
        p += 32-state->memsize;
        state->memsize = 0;
    }

    if (p+32 <= bEnd)
    {
        const BYTE* const limit = bEnd - 32;
        U64 v1 = state->v1;
        U64 v2 = state->v2;
        U64 v3 = state->v3;
        U64 v4 = state->v4;

        do
        {
            v1 += XXH_readLE64((const U64*)p, endian) * PRIME64_2;
            v1 = XXH_rotl64(v1, 31);
            v1 *= PRIME64_1;
            p+=8;
            v2 += XXH_readLE64((const U64*)p, endian) * PRIME64_2;
            v2 = XXH_rotl64(v2, 31);
            v2 *= PRIME64_1;
            p+=8;
            v3 += XXH_readLE64((const U64*)p, endian) * PRIME64_2;
            v3 = XXH_rotl64(v3, 31);
            v3 *= PRIME64_1;
            p+=8;
            v4 += XXH_readLE64((const U64*)p, endian) * PRIME64_2;
            v4 = XXH_rotl64(v4, 31);
            v4 *= PRIME64_1;
            p+=8;
        }
        while (p<=limit);

        state->v1 = v1;
        state->v2 = v2;
        state->v3 = v3;
        state->v4 = v4;
    }

    if (p < bEnd)
    {
        XXH_memcpy(state->memory, p, bEnd-p);
        state->memsize = (int)(bEnd-p);
    }

    return XXH_OK;
}

XXH_errorcode XXH64_update (XXH64_state_t* state_in, const void* input, size_t len)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_update_endian(state_in, input, len, XXH_littleEndian);
    else
        return XXH64_update_endian(state_in, input, len, XXH_bigEndian);
}



FORCE_INLINE U64 XXH64_digest_endian (const XXH64_state_t* state_in, XXH_endianess endian)
{
    XXH_istate64_t * state = (XXH_istate64_t *) state_in;
    const BYTE * p = (const BYTE*)state->memory;
    BYTE* bEnd = (BYTE*)state->memory + state->memsize;
    U64 h64;

    if (state->total_len >= 32)
    {
        U64 v1 = state->v1;
        U64 v2 = state->v2;
        U64 v3 = state->v3;
        U64 v4 = state->v4;

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);

        v1 *= PRIME64_2;
        v1 = XXH_rotl64(v1, 31);
        v1 *= PRIME64_1;
        h64 ^= v1;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v2 *= PRIME64_2;
        v2 = XXH_rotl64(v2, 31);
        v2 *= PRIME64_1;
        h64 ^= v2;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v3 *= PRIME64_2;
        v3 = XXH_rotl64(v3, 31);
        v3 *= PRIME64_1;
        h64 ^= v3;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v4 *= PRIME64_2;
        v4 = XXH_rotl64(v4, 31);
        v4 *= PRIME64_1;
        h64 ^= v4;
        h64 = h64*PRIME64_1 + PRIME64_4;
    }
    else
    {
        h64  = state->seed + PRIME64_5;
    }

    h64 += (U64) state->total_len;

    while (p+8<=bEnd)
    {
        U64 k1 = XXH_readLE64((const U64*)p, endian);
        k1 *= PRIME64_2;
        k1 = XXH_rotl64(k1,31);
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = XXH_rotl64(h64,27) * PRIME64_1 + PRIME64_4;
        p+=8;
    }

    if (p+4<=bEnd)
    {
        h64 ^= (U64)(XXH_readLE32((const U32*)p, endian)) * PRIME64_1;
        h64 = XXH_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p+=4;
    }

    while (p<bEnd)
    {
        h64 ^= (*p) * PRIME64_5;
        h64 = XXH_rotl64(h64, 11) * PRIME64_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}


unsigned long long XXH64_digest (const XXH64_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH64_digest_endian(state_in, XXH_bigEndian);
}

//////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.

// Note - The x86 and x64 versions do _not_ produce the same results, as the
// algorithms are optimized for their respective platforms. You can still
// compile and run any of them on any platform, but your performance with the
// non-native version will be less than optimal.

//-----------------------------------------------------------------------------
// Platform-specific functions and macros

// Microsoft Visual Studio

#if defined(_MSC_VER)

#ifndef FORCE_INLINE
#define FORCE_INLINE	__forceinline
#endif

#include <stdlib.h>

#define ROTL32(x,y)	_rotl(x,y)
#define ROTL64(x,y)	_rotl64(x,y)

#define BIG_CONSTANT(x) (x)

// Other compilers

#else	// defined(_MSC_VER)

//#define	FORCE_INLINE inline __attribute__((always_inline))

inline uint32_t rotl32(uint32_t x, int8_t r)
{
	return (x << r) | (x >> (32 - r));
}

inline uint64_t rotl64(uint64_t x, int8_t r)
{
	return (x << r) | (x >> (64 - r));
}

#define	ROTL32(x,y)	rotl32(x,y)
#define ROTL64(x,y)	rotl64(x,y)

#define BIG_CONSTANT(x) (x##LLU)

#endif // !defined(_MSC_VER)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

FORCE_INLINE uint32_t getblock32(const uint32_t * p, int i)
{
	return p[i];
}

FORCE_INLINE uint64_t getblock64(const uint64_t * p, int i)
{
	return p[i];
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

FORCE_INLINE uint32_t fmix32(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}

//----------

FORCE_INLINE uint64_t fmix64(uint64_t k)
{
	k ^= k >> 33;
	k *= BIG_CONSTANT(0xff51afd7ed558ccd);
	k ^= k >> 33;
	k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
	k ^= k >> 33;

	return k;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x86_32(const void * key, int len,
	uint32_t seed, void * out)
{
	const uint8_t * data = (const uint8_t*) key;
	const int nblocks = len / 4;

	uint32_t h1 = seed;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	//----------
	// body

	const uint32_t * blocks = (const uint32_t *) (data + nblocks * 4);

	for (int i = -nblocks; i; i++)
	{
		uint32_t k1 = getblock32(blocks, i);

		k1 *= c1;
		k1 = ROTL32(k1, 15);
		k1 *= c2;

		h1 ^= k1;
		h1 = ROTL32(h1, 13);
		h1 = h1 * 5 + 0xe6546b64;
	}

	//----------
	// tail

	const uint8_t * tail = (const uint8_t*) (data + nblocks * 4);

	uint32_t k1 = 0;

	switch (len & 3)
	{
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= len;

	h1 = fmix32(h1);

	*(uint32_t*) out = h1;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x86_128(const void * key, const int len,
	uint32_t seed, void * out)
{
	const uint8_t * data = (const uint8_t*) key;
	const int nblocks = len / 16;

	uint32_t h1 = seed;
	uint32_t h2 = seed;
	uint32_t h3 = seed;
	uint32_t h4 = seed;

	const uint32_t c1 = 0x239b961b;
	const uint32_t c2 = 0xab0e9789;
	const uint32_t c3 = 0x38b34ae5;
	const uint32_t c4 = 0xa1e38b93;

	//----------
	// body

	const uint32_t * blocks = (const uint32_t *) (data + nblocks * 16);

	for (int i = -nblocks; i; i++)
	{
		uint32_t k1 = getblock32(blocks, i * 4 + 0);
		uint32_t k2 = getblock32(blocks, i * 4 + 1);
		uint32_t k3 = getblock32(blocks, i * 4 + 2);
		uint32_t k4 = getblock32(blocks, i * 4 + 3);

		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;

		h1 = ROTL32(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1b;

		k2 *= c2; k2 = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;

		h2 = ROTL32(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747;

		k3 *= c3; k3 = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;

		h3 = ROTL32(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35;

		k4 *= c4; k4 = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;

		h4 = ROTL32(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17;
	}

	//----------
	// tail

	const uint8_t * tail = (const uint8_t*) (data + nblocks * 16);

	uint32_t k1 = 0;
	uint32_t k2 = 0;
	uint32_t k3 = 0;
	uint32_t k4 = 0;

	switch (len & 15)
	{
	case 15: k4 ^= tail[14] << 16;
	case 14: k4 ^= tail[13] << 8;
	case 13: k4 ^= tail[12] << 0;
		k4 *= c4; k4 = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;

	case 12: k3 ^= tail[11] << 24;
	case 11: k3 ^= tail[10] << 16;
	case 10: k3 ^= tail[9] << 8;
	case  9: k3 ^= tail[8] << 0;
		k3 *= c3; k3 = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;

	case  8: k2 ^= tail[7] << 24;
	case  7: k2 ^= tail[6] << 16;
	case  6: k2 ^= tail[5] << 8;
	case  5: k2 ^= tail[4] << 0;
		k2 *= c2; k2 = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;

	case  4: k1 ^= tail[3] << 24;
	case  3: k1 ^= tail[2] << 16;
	case  2: k1 ^= tail[1] << 8;
	case  1: k1 ^= tail[0] << 0;
		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	h1 = fmix32(h1);
	h2 = fmix32(h2);
	h3 = fmix32(h3);
	h4 = fmix32(h4);

	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;

	((uint32_t*) out)[0] = h1;
	((uint32_t*) out)[1] = h2;
	((uint32_t*) out)[2] = h3;
	((uint32_t*) out)[3] = h4;
}

//-----------------------------------------------------------------------------

void MurmurHash3_x64_128(const void * key, const int len,
	const uint32_t seed, void * out)
{
	const uint8_t * data = (const uint8_t*) key;
	const int nblocks = len / 16;

	uint64_t h1 = seed;
	uint64_t h2 = seed;

	const uint64_t c1 = BIG_CONSTANT(0x87c37b91114253d5);
	const uint64_t c2 = BIG_CONSTANT(0x4cf5ad432745937f);

	//----------
	// body

	const uint64_t * blocks = (const uint64_t *) (data);

	for (int i = 0; i < nblocks; i++)
	{
		uint64_t k1 = getblock64(blocks, i * 2 + 0);
		uint64_t k2 = getblock64(blocks, i * 2 + 1);

		k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;

		h1 = ROTL64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

		k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;

		h2 = ROTL64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
	}

	//----------
	// tail

	const uint8_t * tail = (const uint8_t*) (data + nblocks * 16);

	uint64_t k1 = 0;
	uint64_t k2 = 0;

	switch (len & 15)
	{
	case 15: k2 ^= ((uint64_t) tail[14]) << 48;
	case 14: k2 ^= ((uint64_t) tail[13]) << 40;
	case 13: k2 ^= ((uint64_t) tail[12]) << 32;
	case 12: k2 ^= ((uint64_t) tail[11]) << 24;
	case 11: k2 ^= ((uint64_t) tail[10]) << 16;
	case 10: k2 ^= ((uint64_t) tail[9]) << 8;
	case  9: k2 ^= ((uint64_t) tail[8]) << 0;
		k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;

	case  8: k1 ^= ((uint64_t) tail[7]) << 56;
	case  7: k1 ^= ((uint64_t) tail[6]) << 48;
	case  6: k1 ^= ((uint64_t) tail[5]) << 40;
	case  5: k1 ^= ((uint64_t) tail[4]) << 32;
	case  4: k1 ^= ((uint64_t) tail[3]) << 24;
	case  3: k1 ^= ((uint64_t) tail[2]) << 16;
	case  2: k1 ^= ((uint64_t) tail[1]) << 8;
	case  1: k1 ^= ((uint64_t) tail[0]) << 0;
		k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= len; h2 ^= len;

	h1 += h2;
	h2 += h1;

	h1 = fmix64(h1);
	h2 = fmix64(h2);

	h1 += h2;
	h2 += h1;

	((uint64_t*) out)[0] = h1;
	((uint64_t*) out)[1] = h2;
}

//////////////////////////////////////////////////////////////////////////

/*
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * (This is a heavily cut-down "BSD license".)
 *
 * This differs from Colin Plumb's older public domain implementation in that
 * no exactly 32-bit integer data type is required (any 32-bit or wider
 * unsigned integer data type will do), there's no compile-time endianness
 * configuration, and the function prototypes match OpenSSL's.  No code from
 * Colin Plumb's implementation has been reused; this comment merely compares
 * the properties of the two independent implementations.
 *
 * The primary goals of this implementation are portability and ease of use.
 * It is meant to be fast, but not as fast as possible.  Some known
 * optimizations are not included to reduce source code size and avoid
 * compile-time configuration.
 */

 /* Any 32-bit or wider unsigned integer data type will do */
typedef unsigned int MD5_u32plus;

typedef struct {
	MD5_u32plus lo, hi;
	MD5_u32plus a, b, c, d;
	unsigned char buffer[64];
	MD5_u32plus block[16];
} MD5_CTX;

 /*
  * The basic MD5 functions.
  *
  * F and G are optimized compared to their RFC 1321 definitions for
  * architectures that lack an AND-NOT instruction, just like in Colin Plumb's
  * implementation.
  */
#define F(x, y, z)			((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z)			((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z)			(((x) ^ (y)) ^ (z))
#define H2(x, y, z)			((x) ^ ((y) ^ (z)))
#define I(x, y, z)			((y) ^ ((x) | ~(z)))

  /*
   * The MD5 transformation for all four rounds.
   */
#define STEP(f, a, b, c, d, x, t, s) \
	(a) += f((b), (c), (d)) + (x) + (t); \
	(a) = (((a) << (s)) | (((a) & 0xffffffff) >> (32 - (s)))); \
	(a) += (b);

   /*
	* SET reads 4 input bytes in little-endian byte order and stores them in a
	* properly aligned word in host byte order.
	*
	* The check for little-endian architectures that tolerate unaligned memory
	* accesses is just an optimization.  Nothing will break if it fails to detect
	* a suitable architecture.
	*
	* Unfortunately, this optimization may be a C strict aliasing rules violation
	* if the caller's data buffer has effective type that cannot be aliased by
	* MD5_u32plus.  In practice, this problem may occur if these MD5 routines are
	* inlined into a calling function, or with future and dangerously advanced
	* link-time optimizations.  For the time being, keeping these MD5 routines in
	* their own translation unit avoids the problem.
	*/
#if defined(__i386__) || defined(__x86_64__) || defined(__vax__)
#define SET(n) \
	(*(MD5_u32plus *)&ptr[(n) * 4])
#define GET(n) \
	SET(n)
#else
#define SET(n) \
	(ctx->block[(n)] = \
	(MD5_u32plus)ptr[(n) * 4] | \
	((MD5_u32plus)ptr[(n) * 4 + 1] << 8) | \
	((MD5_u32plus)ptr[(n) * 4 + 2] << 16) | \
	((MD5_u32plus)ptr[(n) * 4 + 3] << 24))
#define GET(n) \
	(ctx->block[(n)])
#endif

	/*
	 * This processes one or more 64-byte data blocks, but does NOT update the bit
	 * counters.  There are no alignment requirements.
	 */
static const void *body(MD5_CTX *ctx, const void *data, unsigned long size)
{
	const unsigned char *ptr;
	MD5_u32plus a, b, c, d;
	MD5_u32plus saved_a, saved_b, saved_c, saved_d;

	ptr = (const unsigned char *)data;

	a = ctx->a;
	b = ctx->b;
	c = ctx->c;
	d = ctx->d;

	do {
		saved_a = a;
		saved_b = b;
		saved_c = c;
		saved_d = d;

		/* Round 1 */
		STEP(F, a, b, c, d, SET(0), 0xd76aa478, 7)
			STEP(F, d, a, b, c, SET(1), 0xe8c7b756, 12)
			STEP(F, c, d, a, b, SET(2), 0x242070db, 17)
			STEP(F, b, c, d, a, SET(3), 0xc1bdceee, 22)
			STEP(F, a, b, c, d, SET(4), 0xf57c0faf, 7)
			STEP(F, d, a, b, c, SET(5), 0x4787c62a, 12)
			STEP(F, c, d, a, b, SET(6), 0xa8304613, 17)
			STEP(F, b, c, d, a, SET(7), 0xfd469501, 22)
			STEP(F, a, b, c, d, SET(8), 0x698098d8, 7)
			STEP(F, d, a, b, c, SET(9), 0x8b44f7af, 12)
			STEP(F, c, d, a, b, SET(10), 0xffff5bb1, 17)
			STEP(F, b, c, d, a, SET(11), 0x895cd7be, 22)
			STEP(F, a, b, c, d, SET(12), 0x6b901122, 7)
			STEP(F, d, a, b, c, SET(13), 0xfd987193, 12)
			STEP(F, c, d, a, b, SET(14), 0xa679438e, 17)
			STEP(F, b, c, d, a, SET(15), 0x49b40821, 22)

			/* Round 2 */
			STEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)
			STEP(G, d, a, b, c, GET(6), 0xc040b340, 9)
			STEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)
			STEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)
			STEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)
			STEP(G, d, a, b, c, GET(10), 0x02441453, 9)
			STEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)
			STEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)
			STEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)
			STEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)
			STEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)
			STEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)
			STEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)
			STEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)
			STEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)
			STEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)

			/* Round 3 */
			STEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)
			STEP(H2, d, a, b, c, GET(8), 0x8771f681, 11)
			STEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)
			STEP(H2, b, c, d, a, GET(14), 0xfde5380c, 23)
			STEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)
			STEP(H2, d, a, b, c, GET(4), 0x4bdecfa9, 11)
			STEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)
			STEP(H2, b, c, d, a, GET(10), 0xbebfbc70, 23)
			STEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)
			STEP(H2, d, a, b, c, GET(0), 0xeaa127fa, 11)
			STEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)
			STEP(H2, b, c, d, a, GET(6), 0x04881d05, 23)
			STEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)
			STEP(H2, d, a, b, c, GET(12), 0xe6db99e5, 11)
			STEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)
			STEP(H2, b, c, d, a, GET(2), 0xc4ac5665, 23)

			/* Round 4 */
			STEP(I, a, b, c, d, GET(0), 0xf4292244, 6)
			STEP(I, d, a, b, c, GET(7), 0x432aff97, 10)
			STEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)
			STEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)
			STEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)
			STEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)
			STEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)
			STEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)
			STEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)
			STEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)
			STEP(I, c, d, a, b, GET(6), 0xa3014314, 15)
			STEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)
			STEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)
			STEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)
			STEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)
			STEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)

			a += saved_a;
		b += saved_b;
		c += saved_c;
		d += saved_d;

		ptr += 64;
	} while (size -= 64);

	ctx->a = a;
	ctx->b = b;
	ctx->c = c;
	ctx->d = d;

	return ptr;
}

void MD5_Init(MD5_CTX *ctx)
{
	ctx->a = 0x67452301;
	ctx->b = 0xefcdab89;
	ctx->c = 0x98badcfe;
	ctx->d = 0x10325476;

	ctx->lo = 0;
	ctx->hi = 0;
}

void MD5_Update(MD5_CTX *ctx, const void *data, unsigned long size)
{
	MD5_u32plus saved_lo;
	unsigned long used, available;

	saved_lo = ctx->lo;
	if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo)
		ctx->hi++;
	ctx->hi += size >> 29;

	used = saved_lo & 0x3f;

	if (used) {
		available = 64 - used;

		if (size < available) {
			memcpy(&ctx->buffer[used], data, size);
			return;
		}

		memcpy(&ctx->buffer[used], data, available);
		data = (const unsigned char *)data + available;
		size -= available;
		body(ctx, ctx->buffer, 64);
	}

	if (size >= 64) {
		data = body(ctx, data, size & ~(unsigned long)0x3f);
		size &= 0x3f;
	}

	memcpy(ctx->buffer, data, size);
}

#define MD5_OUT(dst, src) \
	(dst)[0] = (unsigned char)(src); \
	(dst)[1] = (unsigned char)((src) >> 8); \
	(dst)[2] = (unsigned char)((src) >> 16); \
	(dst)[3] = (unsigned char)((src) >> 24);

void MD5_Final(unsigned char *result, MD5_CTX *ctx)
{
	unsigned long used, available;

	used = ctx->lo & 0x3f;

	ctx->buffer[used++] = 0x80;

	available = 64 - used;

	if (available < 8) {
		memset(&ctx->buffer[used], 0, available);
		body(ctx, ctx->buffer, 64);
		used = 0;
		available = 64;
	}

	memset(&ctx->buffer[used], 0, available - 8);

	ctx->lo <<= 3;
	MD5_OUT(&ctx->buffer[56], ctx->lo)
	MD5_OUT(&ctx->buffer[60], ctx->hi)

	body(ctx, ctx->buffer, 64);

	MD5_OUT(&result[0], ctx->a)
	MD5_OUT(&result[4], ctx->b)
	MD5_OUT(&result[8], ctx->c)
	MD5_OUT(&result[12], ctx->d)

	memset(ctx, 0, sizeof(*ctx));
}

//////////////////////////////////////////////////////////////////////////

USING_NS_BF;

uint64 Beefy::Hash64(const void* data, int length, uint64 seed)
{
	return XXH64(data, length, seed);
}

uint64 Beefy::Hash64(uint64 hash, uint64 seed)
{
	//return XXH64((const void*)&hash, sizeof(uint64), seed);

	return fmix64(seed) ^ hash;
}

Val128 Beefy::Hash128(const void* data, int length)
{
	Val128 hashVal;
	MurmurHash3_x64_128(data, length, 0, &hashVal);	
	return hashVal;
}

Val128 Beefy::Hash128(const void* data, int length, const Val128& seed)
{
	if (length < 8)
	{
		uint64 iVal = 0;
		memcpy(&iVal, data, length);

		Val128 val;
		val.mLow = fmix64(seed.mLow) ^ iVal;
		val.mHigh = val.mLow ^ seed.mHigh;

		return val;
	}

	Val128 hashVal;
	MurmurHash3_x64_128(data, length, (uint32)seed.mLow, &hashVal);
	hashVal.mHigh ^= fmix64(seed.mHigh);
	hashVal.mLow ^= fmix64(seed.mLow);
	return hashVal;
}

Val128 Beefy::HashMD5(const void* data, int length)
{
	Val128 result;
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx, data, length);
	MD5_Final((unsigned char*)&result, &ctx);
	return result;
}


//////////////////////////////////////////////////////////////////////////

// Only 63 chars - skip zero
static const char cHash64bToChar[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_' };
String Beefy::HashEncode64(uint64 val)
{
	String outStr;
	if ((int64)val < 0)
	{
		uint64 flippedNum = (uint64)-(int64)val;
		// Only flip if the encoded result would actually be shorter
		if (flippedNum <= 0x00FFFFFFFFFFFFFFLL)
		{
			val = flippedNum;
			outStr.Append('_');
		}
	}

	for (int i = 0; i < 10; i++)
	{
		int charIdx = (int)((val >> (i * 6)) & 0x3F) - 1;
		if (charIdx != -1)
			outStr.Append(cHash64bToChar[charIdx]);
	}
	return outStr;
}

StringT<21> Beefy::HashEncode128(Val128 val)
{
	StringT<21> str;
	str += HashEncode64(val.mHigh);
	str += HashEncode64(val.mLow);
	return str;
}

//////////////////////////////////////////////////////////////////////////

static int gDbgVizIdx = 0;

HashContext::~HashContext()
{
#ifdef BF_PLATFORM_WINDOWS
	delete mDbgVizStream;
#endif
}

void HashContext::Reset()
{
	mBufSize = 0;
	mBufOffset = 0;
}

void HashContext::Mixin(const void* data, int size)
{
	if (size >= 1024)
	{
		Val128 hashVal = Hash128(data, size);
		Mixin(&hashVal, 16);
		return;
	}

	while (size > 0)
	{
		int addBytes = std::min(size, 1024 - mBufSize);

#ifdef BF_PLATFORM_WINDOWS
		if (mDbgViz)
		{
			int findIdx = 0x2cc159;
			if ((mBufOffset + mBufSize <= findIdx) && (mBufOffset + mBufSize + addBytes > findIdx))
			{
				NOP;
			}
		}
#endif

		memcpy(&mBuf[mBufSize], data, addBytes);
		mBufSize += addBytes;
		size -= addBytes;
		data = (uint8*)data + addBytes;
		if (mBufSize == 1024)
		{
			Val128 val = Finish128();
			*(Val128*)mBuf = val;
			mBufSize = 16;
		}
	}
}

void HashContext::MixinHashContext(HashContext& ctx)
{
	Mixin(ctx.mBufSize);
	Mixin(ctx.mBuf, ctx.mBufSize);
}

void HashContext::MixinStr(const char* str)
{
	int len = (int)strlen(str);
	Mixin(len);
	Mixin(str, len);
}

void HashContext::MixinStr(const StringImpl& str)
{
	int len = (int)str.length();
	Mixin(len);
	Mixin(str.c_str(), len);
}

Val128 HashContext::Finish128()
{
#ifdef BF_PLATFORM_WINDOWS
	if (mDbgViz)
	{
// 		String dbg = "HashContext Dbg";
// 		for (int i = 0; i < mBufSize; i++)
// 			dbg += StrFormat(" %X:%02X", i, mBuf[i]);
// 		dbg += "\n";
// 		OutputDebugStr(dbg);

		if (mDbgVizStream == NULL)
		{
			String filePath = StrFormat("c:\\temp\\hash%d.bin", gDbgVizIdx++);
			mDbgVizStream = new	FileStream();
			if (mDbgVizStream->Open(filePath, "wb"))
			{
				OutputDebugStrF("Creating dbg hash: %s\n", filePath.c_str());
			}
			else
			{ 
				OutputDebugStrF("FAILED creating dbg hash: %s\n", filePath.c_str());
			}
		}
		if ((mDbgVizStream != NULL) && (mDbgVizStream->IsOpen()))
			mDbgVizStream->Write(mBuf, mBufSize);
	}
#endif
	
	if (mBufSize <= 16)
	{
		// We do this copy because 'result' gets zero-initialized and then we copy in any applicable data
		Val128 result;
		memcpy(&result, mBuf, mBufSize);
		mBufOffset += mBufSize;
		return result;
	}		
	Val128 val = Hash128(mBuf, mBufSize);
	mBufOffset += mBufSize;
	mBufSize = 0;
	return val;
}


uint64 HashContext::Finish64()
{
#ifdef BF_PLATFORM_WINDOWS
	if (mDbgViz)
	{
		// 		String dbg = "HashContext Dbg";
		// 		for (int i = 0; i < mBufSize; i++)
		// 			dbg += StrFormat(" %X:%02X", i, mBuf[i]);
		// 		dbg += "\n";
		// 		OutputDebugStr(dbg);

		if (mDbgVizStream == NULL)
		{
			String filePath = StrFormat("c:\\temp\\hash%d.bin", gDbgVizIdx++);
			mDbgVizStream = new	FileStream();
			mDbgVizStream->Open(filePath, "wb");
			OutputDebugStrF("Creating dbg hash: %s\n", filePath.c_str());
		}
		mDbgVizStream->Write(mBuf, mBufSize);
	}
#endif

	if (mBufSize <= 8)
	{
		// We do this copy because 'result' gets zero-initialized and then we copy in any applicable data
		uint64 result = 0;
		memcpy(&result, mBuf, mBufSize);
		mBufOffset += mBufSize;
		return result;
	}
	uint64 val = Hash64(mBuf, mBufSize);
	mBufOffset += mBufSize;
	mBufSize = 0;
	return val;
}

