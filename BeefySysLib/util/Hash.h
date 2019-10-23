#pragma once

#include "../Common.h"
#include "../FileStream.h"

NS_BF_BEGIN

class Val128
{
public:
	struct Hash
	{
		size_t operator()(const Val128& entry) const
		{
			return (size_t)(entry.mLow ^ entry.mHigh);
		}
	};

	struct Equals
	{
		bool operator()(const Val128& lhs, const Val128& rhs) const
		{
			return (lhs.mLow == rhs.mLow) && (lhs.mHigh == rhs.mHigh);
		}
	};

public:
	uint64 mLow;
	uint64 mHigh;

public:
	Val128()
	{
		mLow = 0;
		mHigh = 0;
	}

	Val128(int val)
	{
		mLow = (uint64)val;
		mHigh = 0;
	}

	Val128& operator=(int val)
	{
		mLow = (uint64)val;
		mHigh = 0;		
		return *this;
	}	

	bool IsZero()
	{
		return (mLow == 0) && (mHigh == 0);
	}	

	explicit operator int()
	{
		return (int)mLow;
	}

	String ToString()
	{
		return StrFormat("%lX%lX", mHigh, mLow);
	}
};

static bool operator!=(const Val128& l, int rLow)
{
	return (l.mHigh != 0) || (l.mLow != rLow);
}

static bool operator==(const Val128& l, const Val128& r)
{
	return (l.mLow == r.mLow) && (l.mHigh == r.mHigh);
}

static bool operator!=(const Val128& l, const Val128& r)
{
	return (l.mLow != r.mLow) || (l.mHigh != r.mHigh);
}

static bool operator<(const Val128& l, const Val128& r)
{
	int* lPtr = (int*)&l.mLow;
	int* rPtr = (int*)&r.mLow;
	if (lPtr[3] != rPtr[3]) //-V557
		return lPtr[3] < rPtr[3]; //-V557
	if (lPtr[2] != rPtr[2]) //-V557
		return lPtr[2] < rPtr[2]; //-V557
	if (lPtr[1] != rPtr[1])
		return lPtr[1] < rPtr[1];	
	return lPtr[0] < rPtr[0];
}

uint64 Hash64(uint64 hash, uint64 seed);
uint64 Hash64(const void* data, int length, uint64 seed = 0);
Val128 Hash128(const void* data, int length);
Val128 Hash128(const void* data, int length, const Val128& seed);
String HashEncode64(uint64 val); // Note: this only encodes the low 60 bits.  Returns up to 10 characters.
StringT<21> HashEncode128(Val128 val); // Returns up to 20 characters.

#define HASH128_MIXIN(hashVal, data) hashVal = Hash128(&data, sizeof(data), hashVal)
#define HASH128_MIXIN_PTR(hashVal, data, size) hashVal = Hash128(data, size, hashVal)
#define HASH128_MIXIN_STR(hashVal, str) hashVal = Hash128(str.c_str(), (int)str.length(), hashVal)

class HashContext
{
public:	
	uint8 mBuf[1024];
	int mBufSize;
	int mBufOffset;
#ifdef BF_PLATFORM_WINDOWS
	bool mDbgViz;
	FileStream* mDbgVizStream;
#endif

public:
	HashContext()
	{
		mBufOffset = 0;
		mBufSize = 0;
#ifdef BF_PLATFORM_WINDOWS
		mDbgViz = false;
		mDbgVizStream = NULL;
#endif
	}

	~HashContext();
	
	void Reset();
	void Mixin(const void* data, int size);
	template <typename T>
	void Mixin(const T& val)
	{
		Mixin((void*)&val, (int)sizeof(val));
	}
	void MixinHashContext(HashContext& ctx);
	void MixinStr(const char* str);
	void MixinStr(const StringImpl& str);
	Val128 Finish128();
	uint64 Finish64();
};

NS_BF_END