#pragma once

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

// #include <string>
// #include <map>
// #include <set>
// #include <vector>
// #include <list>
 #include <algorithm>
// #include <functional>
#include <functional>
#include "BFPlatform.h"

inline size_t HashBytes(const uint8* ptr, size_t count) noexcept
{
#ifdef BF64
	const size_t _FNV_offset_basis = 14695981039346656037ULL;
	const size_t _FNV_prime = 1099511628211ULL;
#else
	const size_t _FNV_offset_basis = 2166136261U;
	const size_t _FNV_prime = 16777619U;
#endif

	size_t val = _FNV_offset_basis;
	for (size_t _Next = 0; _Next < count; ++_Next)
	{
		val ^= (size_t)ptr[_Next];
		val *= _FNV_prime;
	}
	return (val);
}

template <typename T>
struct BeefHash : std::hash<T>
{
};

template<class T>
struct BeefHash<T*>
{
	size_t operator()(T* val) const
	{
		return (size_t)val ^ (size_t)val >> 11;
	}
};

template <>
struct BeefHash<int>
{
	size_t operator()(int val)
	{
		return (size_t)val;
	}
};

template <>
struct BeefHash<int64>
{
	size_t operator()(int64 val)
	{
		return (size_t)val;
	}
};

struct uint128
{
	uint64 mLo;
	uint64 mHigh;
};

struct int128
{
	uint64 mLo;
	uint64 mHigh;
};

#define BF_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BF_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define BF_CLAMP(val, minVal, maxVal) (((val) < (minVal)) ? (minVal) : ((val) > (maxVal)) ? (maxVal) : (val))
#define BF_SWAP(a, b) { auto _a = (a); (a) = (b); (b) = (_a); }

extern int gBFArgC;
extern char** gBFArgV;

#define NS_BF_BEGIN namespace Beefy {
#define NS_BF_END }
#define USING_NS_BF using namespace Beefy
#define BF_ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BF_CLEAR_VALUE(val) memset(&val, 0, sizeof(val))
#define BF_ALIGN(intVal, alignSize) (((intVal) + ((alignSize) - 1)) & ~((alignSize) - 1))
#define BF_DISALLOW_COPY(name) name(const name& from) = delete;

#define BF_ASSERT_CONCAT_(a, b) a##b
#define BF_ASSERT_CONCAT(a, b) BF_ASSERT_CONCAT_(a, b)
#ifdef __COUNTER__
#define BF_STATIC_ASSERT(e) \
    ;enum { BF_ASSERT_CONCAT(static_assert_, __COUNTER__) = 1/(int)(!!(e)) }
#else
#define BF_STATIC_ASSERT(e) \
    ;enum { BF_ASSERT_CONCAT(assert_line_, __LINE__) = 1/(int)(!!(e)) }
#endif

NS_BF_BEGIN;

#ifndef max
template <typename T>
constexpr const T& max(const T& lhs, const T& rhs) noexcept
{
	return (lhs < rhs) ? rhs : lhs;
}

template <typename T>
constexpr const T& min(const T& lhs, const T& rhs) noexcept
{
	return (lhs < rhs) ? lhs : rhs;
}
#endif

class StringImpl;

template <const int TBufSize>
class StringT;
class UTF16String;

typedef StringT<16> String;

#ifdef BF64
#define V_32_64(v32, v64) v64
#else
#define V_32_64(v32, v64) v32
#endif

#define BF_PI 3.14159265359f
#define BF_PI_D 3.14159265359

//typedef std::vector<int> IntVector;

inline float BFRound(float aVal)
{
	if (aVal < 0)
		return (float) (int) (aVal - 0.5f);
	else
		return (float) (int) (aVal + 0.5f);
}

inline float BFClamp(float val, float min, float max)
{
	return (val <= min) ? min : (val >= max) ? max : val;
}

inline int BFClamp(int val, int min, int max)
{
	return (val <= min) ? min : (val >= max) ? max : val;
}

uint32 BFTickCount();
void BFFatalError(const char* message, const char* file, int line);
void BFFatalError(const StringImpl& message, const StringImpl& file, int line);

int64 EndianSwap(int64 val);
int32 EndianSwap(int32 val);
int16 EndianSwap(int16 val);

#ifdef BF_ENDIAN_LITTLE
static inline int64 FromBigEndian(int64 val) { return Beefy::EndianSwap(val); }
static inline int32 FromBigEndian(int32 val) { return Beefy::EndianSwap(val); }
static inline int16 FromBigEndian(int16 val) { return Beefy::EndianSwap(val); }
static inline uint64 FromBigEndian(uint64 val) { return Beefy::EndianSwap(*((int64*)&val)); }
static inline uint32 FromBigEndian(uint32 val) { return Beefy::EndianSwap(*((int32*)&val)); }
static inline uint16 FromBigEndian(uint16 val) { return Beefy::EndianSwap(*((int16*)&val)); }

static inline int64 ToBigEndian(int64 val) { return Beefy::EndianSwap(val); }
static inline int32 ToBigEndian(int32 val) { return Beefy::EndianSwap(val); }
static inline int16 ToBigEndian(int16 val) { return Beefy::EndianSwap(val); }
static inline uint64 ToBigEndian(uint64 val) { return Beefy::EndianSwap(*((int64*)&val)); }
static inline uint32 ToBigEndian(uint32 val) { return Beefy::EndianSwap(*((int32*)&val)); }
static inline uint16 ToBigEndian(uint16 val) { return Beefy::EndianSwap(*((int16*)&val)); }

static inline int64 FromLittleEndian(int64 val) { return val; }
static inline int32 FromLittleEndian(int32 val) { return val; }
static inline int16 FromLittleEndian(int16 val) { return val; }
static inline int64 FromLittleEndian(uint64 val) { return val; }
static inline int32 FromLittleEndian(uint32 val) { return val; }
static inline int16 FromLittleEndian(uint16 val) { return val; }
#endif

uint64 BFGetTickCountMicro();
uint64 BFGetTickCountMicroFast();

extern String vformat(const char* fmt, va_list argPtr);
extern String StrFormat(const char* fmt ...);
void ExactMinimalFloatToStr(float f, char* str);
void ExactMinimalDoubleToStr(double d, char* str);
String IntPtrDynAddrFormat(intptr addr);
void OutputDebugStr(const StringImpl& theString);
void OutputDebugStrF(const char* fmt ...);
UTF16String ToWString(const StringImpl& theString);
String ToString(const UTF16String& theString);
String ToUpper(const StringImpl& theString);
void MakeUpper(StringImpl& theString);
UTF16String ToUpper(const UTF16String& theString);
UTF16String ToLower(const UTF16String& theString);
String ToLower(const StringImpl& theString);
//UTF16String Trim(const UTF16String& theString);
String Trim(const StringImpl& theString);
bool StrReplace(StringImpl& str, const StringImpl& from, const StringImpl& to);
bool StrStartsWith(const StringImpl& str, const StringImpl& subStr);
bool StrEndsWith(const StringImpl& str, const StringImpl& subStr);
String SlashString(const StringImpl& str, bool utf8decode, bool utf8encode, bool beefString = false);
UTF16String UTF8Decode(const StringImpl& theString);
String UTF8Encode(const UTF16String& theString);
String UTF8Encode(const uint16* theString, int length);
UTF16String UTF16Decode(const uint16* theString);
String FileNameToURI(const StringImpl& fileName);
int64 DecodeULEB32(const char*& p);
void EncodeULEB32(uint64 value, StringImpl& buffer);
int32 Rand();
int32 GetHighestBitSet(int32 n);

uint8* LoadBinaryData(const StringImpl& path, int* size);
char* LoadTextData(const StringImpl& path, int* size);
bool LoadTextData(const StringImpl& path, StringImpl& str);
int64 GetFileTimeWrite(const StringImpl& path);
String GetFileDir(const StringImpl& path);
String GetFileName(const StringImpl& path);
String GetFileExtension(const StringImpl& path);
String GetRelativePath(const StringImpl& fullPath, const StringImpl& curDir);
String GetAbsPath(const StringImpl& relPath, const StringImpl& dir);
String FixPath(const StringImpl& path);
String FixPathAndCase(const StringImpl& path);
String EnsureEndsInSlash(const StringImpl& dir);
String RemoveTrailingSlash(const StringImpl& dir);
bool FileNameEquals(const StringImpl& filePathA, const StringImpl& filePathB);
bool FileExists(const StringImpl& path, String* outActualName = NULL);
bool DirectoryExists(const StringImpl& path, String* outActualName = NULL);
bool RecursiveCreateDirectory(const StringImpl& dirName);
bool RecursiveDeleteDirectory(const StringImpl& dirName);

#define CHARTAG(val) FromBIGEndian(val)

int64 EndianSwap(int64 val);
int32 EndianSwap(int32 val);
int16 EndianSwap(int16 val);

template<typename T>
struct RemoveTypePointer
{
};

template<typename T>
struct RemoveTypePointer<T*>
{
	typedef T type;
};

#ifndef BF_SMALL
template <typename F>
struct BF_Defer {
	F f;
	BF_Defer(F f) : f(f) {}
	~BF_Defer() { f(); }
};

template <typename F>
BF_Defer<F> BF_defer_func(F f) {
	return BF_Defer<F>(f);
}

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x)    DEFER_2(x, __COUNTER__)
#define defer(code)   auto DEFER_3(_defer_) = BF_defer_func([&](){code;})
#endif //BF_SMALL

NS_BF_END

#include "util/Array.h"
#include "util/String.h"
