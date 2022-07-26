#pragma once

#include "BeefySysLib/Common.h"

NS_BF_BEGIN

template <typename T>
class AutoPopBack
{
public:
	T* mList;

public:
	AutoPopBack(T* list)
	{
		mList = list;
	}

	~AutoPopBack()
	{
		if (mList != NULL)
			mList->pop_back();
	}

	void Pop()
	{
		if (mList != NULL)
			mList->pop_back();
		mList = NULL;
	}
};

template <typename T>
class SetAndRestoreValue
{
public:
	T* mVarPtr;
	T mPrevVal;
	T mNewVal;

public:
	SetAndRestoreValue()
	{
		mVarPtr = NULL;
	}

	SetAndRestoreValue(T& varRef)
	{
		mPrevVal = varRef;
		mVarPtr = &varRef;
		mNewVal = mPrevVal;
	}

	SetAndRestoreValue(T& varRef, T newVal)
	{
		mPrevVal = varRef;
		mVarPtr = &varRef;
		varRef = newVal;
		mNewVal = newVal;
	}

	SetAndRestoreValue(T& varRef, T newVal, bool doSet)
	{
		mPrevVal = varRef;
		mVarPtr = &varRef;
		if (doSet)
			varRef = newVal;
		mNewVal = newVal;
	}

	void Init(T& varRef, T newVal)
	{
		mPrevVal = varRef;
		mVarPtr = &varRef;
		varRef = newVal;
		mNewVal = newVal;
	}

	~SetAndRestoreValue()
	{
		Restore();
	}

	void Restore()
	{
		if (mVarPtr != NULL)
			*mVarPtr = mPrevVal;
	}

	void CancelRestore()
	{
		mVarPtr = NULL;
	}

	void Set()
	{
		*mVarPtr = mNewVal;
	}
};

template <typename T>
class OwnedVector : public Array<T*>
{
public:
	typedef Array<T*> _Base;

	~OwnedVector()
	{
		for (auto item : *this)
			delete item;
	}

	void Clear()
	{
		for (auto item : *this)
			delete item;
		_Base::Clear();
	}

	void ClearWithoutDeleting()
	{
		_Base::Clear();
	}

	T* Alloc()
	{
		T* item = new T();
		_Base::push_back(item);
		return item;
	}

	template <typename T2>
	T2* Alloc()
	{
		T2* item = new T2();
		_Base::push_back(item);
		return item;
	}
};

// Optimized for Get and then immediate GiveBack - no allocation or vector access
template <typename T>
class BfAllocPool
{
public:
	Array<T*> mVals;
	T* mNext;
	bool mOwnsAll;
	bool mZeroAlloc;

public:
	BfAllocPool(bool ownsAll = false, bool zeroAlloc = false)
	{
		mOwnsAll = ownsAll;
		mZeroAlloc = zeroAlloc;
		mNext = NULL;
	}

	~BfAllocPool()
	{
		if ((mNext != NULL) && (!mOwnsAll))
			delete mNext;
		for (auto val : mVals)
		{
			if (mZeroAlloc)
			{
				val->~T();
				free(val);
			}
			else
				delete val;
		}
	}

	T* Get()
	{
		T* val = mNext;
		if (val == NULL)
		{
			if ((mVals.size() > 0) && (!mOwnsAll))
			{
				val = mVals.back();
				mVals.pop_back();
				return val;
			}

			if (mZeroAlloc)
			{
				void* addr = malloc(sizeof(T));
				memset(addr, 0, sizeof(T));
				val = new(addr) T();
			}
			else
				val = new T();
			if (mOwnsAll)
				mVals.push_back(val);
			return val;
		}
		mNext = NULL;
		return val;
	}

	void GiveBack(T* val)
	{
		if (mNext == NULL)
			mNext = val;
		else if (!mOwnsAll)
			mVals.push_back(val);
	}
};

inline uint64_t DecodeULEB128(const uint8*& p)
{
	uint64_t val = 0;
	unsigned shift = 0;
	do
	{
		val += uint64_t(*p & 0x7f) << shift;
		shift += 7;
	} while ((*p++ & 0x80) != 0);
	return val;
}

inline void EncodeSLEB128(uint8*& buf, int value)
{
	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		*(buf++) = curByte;
	}
	while (hasMore);
}

inline void EncodeSLEB128(uint8*& buf, int64_t value)
{
	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		*(buf++) = curByte;
	}
	while (hasMore);
}

#pragma warning(push)
#pragma warning(disable:4146)

/// Utility function to decode a SLEB128 value.
inline int64_t DecodeSLEB128(const uint8*& p)
{
	int value = 0;
	int shift = 0;
	int curByte;
	do
	{
		curByte = (uint8_t)*p++;
		value |= ((curByte & 0x7f) << shift);
		shift += 7;
	} while (curByte >= 128);
	// Sign extend negative numbers.
	if (((curByte & 0x40) != 0) && (shift < 64))
		value |= ~0LL << shift; //-V610
	return value;
}

#pragma warning(pop)

void* DecodeLocalDataPtr(const char*& strRef);
String EncodeDataPtr(void* addr, bool doPrefix);
String EncodeDataPtr(uint32 addr, bool doPrefix);
String EncodeDataPtr(uint64 addr, bool doPrefix);
String EncodeDataPtr(int addr, bool doPrefix);
void* ZeroedAlloc(int size);
String EncodeFileName(const StringImpl& fromStr); // Make short, only legal chars, with a hash at end

/*template <typename T>
T* ZeroedAlloc()
{
	return new (ZeroedAlloc(sizeof(T))) T();
}*/

bool BfCheckWildcard(const StringImpl& wildcard, const StringImpl& checkStr);

template <typename T>
T* PlaceNew(void* addr)
{
	return new(addr) T();
}

template <typename T>
T* ZeroPlaceNew(void* addr)
{
	memset(addr, 0, sizeof(T));
	return new(addr) T();
}

class CaseInsensitiveString
{
public:
	const char* mStr;

	CaseInsensitiveString(const char* str)
	{
		mStr = str;
	}

	bool operator==(const CaseInsensitiveString& strB) const
	{
		return _stricmp(mStr, strB.mStr) == 0;
	}
};

#define TOKENPASTE(X, Y) X ## Y
#define TOKENPASTE2(X, Y) TOKENPASTE(X, Y)

#define STACK_ZERO_INIT(T, NAME) \
	alignas(16) uint8 TOKENPASTE2(_data__, __LINE__)[sizeof(T)]; \
	memset(TOKENPASTE2(_data__, __LINE__), 0, sizeof(T)); \
	T* NAME = PlaceNew<T>(TOKENPASTE2(_data__, __LINE__));

NS_BF_END

uint64 stouln(const char* str, int len);

namespace std
{
	template<>
	struct hash<Beefy::CaseInsensitiveString>
	{
		size_t operator()(const Beefy::CaseInsensitiveString& val) const
		{
			int curHash = 0;
			const char* str = val.mStr;
			while (true)
			{
				char c = *(str++);
				if (c == 0)
					break;
				c = tolower(c);
				curHash = ((curHash ^ c) << 5) - curHash;
			}
			return curHash;
		}
	};
}