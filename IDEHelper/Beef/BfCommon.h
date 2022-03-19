#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/SizedArray.h"
#include "BeefySysLib/util/TLSingleton.h"
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/BumpAllocator.h"

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)
#include "llvm/ADT/SmallVector.h"
#pragma warning(pop)

NS_BF_BEGIN

template <typename T, unsigned int N>
using BfSizedVector = llvm::SmallVector<T, N>;

template <typename T>
using BfSizedVectorRef = llvm::SmallVectorImpl<T>;

template <typename T>
class BfSizedArray
{
public:
	T* mVals;
	int mSize;

public:
	struct Iterator
	{
	public:
		T* mPtr;

	public:
		Iterator(T* ptr)
		{
			mPtr = ptr;
		}

		Iterator& operator++()
		{
			mPtr++;
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return itr.mPtr != mPtr;
		}

		bool operator==(const Iterator& itr) const
		{
			return itr.mPtr == mPtr;
		}

		T& operator*()
		{
			return *mPtr;
		}
	};

	BfSizedArray()
	{		
		mSize = 0;
		mVals = NULL;
	}

	BfSizedArray(const llvm::SmallVectorImpl<T>& refVec)
	{
		mSize = (int)refVec.size();
		if (mSize > 0)
			mVals = (T*)&refVec[0];
		else
			mVals = NULL;
	}

	BfSizedArray(const Array<T>& refVec)
	{
		mSize = (int)refVec.size();
		if (mSize > 0)
			mVals = (T*)&refVec[0];
		else
			mVals = NULL;
	}

	BfSizedArray(const SizedArrayImpl<T>& refVec)
	{
		mSize = (int)refVec.size();
		if (mSize > 0)
			mVals = (T*)&refVec[0];
		else
			mVals = NULL;
	}

	void CopyFrom(T* vals, int size, BumpAllocator& bumpAlloc)
	{
		mVals = (T*)bumpAlloc.AllocBytes(sizeof(T) * size, alignof(T));
		mSize = size;
		memcpy(mVals, vals, sizeof(T) * size);
	}

	T& operator[](int idx) const
	{
		BF_ASSERT((uint)idx < (uint)mSize);
		return mVals[idx];
	}

	void GetSafe(intptr idx, T& val)
	{
		if ((idx < 0) || (idx >= mSize))
			return;
		val = mVals[idx];
	}

	T GetSafe(intptr idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return T();
		return mVals[idx];
	}

	Iterator begin() const
	{
		return mVals;
	}

	Iterator end() const
	{
		return mVals + mSize;
	}

	T back() const
	{
		return mVals[mSize - 1];
	}

	int size() const
	{
		return mSize;
	}

	bool empty() const
	{
		return mSize == 0;
	}

	bool IsEmpty() const
	{
		return mSize == 0;
	}

	T Get(int idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return (T)0;
		return mVals[idx];
	}

	template <typename T2>
	T2 GetAs(int idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return (T2)0;
		return (T2)mVals[idx];
	}

	T GetLast()
	{
		if (mSize == 0)
			return (T)0;
		return mVals[mSize - 1];
	}

	T GetFirst()
	{
		if (mSize == 0)
			return (T)0;
		return mVals[0];
	}

	/*void Init(const llvm::SmallVectorImpl<T>& vec, BfAstAllocator* alloc)
	{
		mSize = (int)vec.size();
		if (mSize > 0)
		{
			mVals = (T*)alloc->AllocBytes(mSize * sizeof(T), sizeof(T));
			memcpy(mVals, &vec[0], mSize * sizeof(T));
		}
	}

	template <typename T2>
	void InitIndirect(const llvm::SmallVectorImpl<T2>& vec, BfAstAllocator* alloc)
	{
		mSize = (int)vec.size();
		if (mSize > 0)
		{
			mVals = (T*)alloc->AllocBytes(mSize * sizeof(T), sizeof(T));
			for (int i = 0; i < mSize; i++)
				mVals[i] = vec[i];
		}
	}*/

	void SetSize(int size)
	{
		BF_ASSERT(size <= mSize);
		mSize = size;
	}
};

class ChunkedDataBuffer;

class BfIRCodeGenBase
{
public:
	ChunkedDataBuffer* mStream;
	int mPtrSize;
	bool mIsOptimized;
	bool mFailed;
	String mErrorMsg;	

public:
	BfIRCodeGenBase()
	{
		mStream = NULL;
		mPtrSize = -1;
		mIsOptimized = false;
		mFailed = false;
	}

	virtual void Fail(const StringImpl& error)
	{
		if (mErrorMsg.IsEmpty())
			mErrorMsg = error;
		mFailed = true;
	}

	virtual ~BfIRCodeGenBase() {}

	virtual void ProcessBfIRData(const BfSizedArray<uint8>& buffer) = 0;
	virtual void HandleNextCmd() = 0;
	virtual void SetConfigConst(int idx, int value) = 0;
};

extern TLSingleton<String> gTLStrReturn;
extern TLSingleton<String> gTLStrReturn2;

template <typename T>
struct OwnedWrapper
{
public:
	T* mValue;

	OwnedWrapper()
	{
		mValue = NULL;
	}

	OwnedWrapper(const T* value)
	{
		mValue = (T*)value;
	}

	OwnedWrapper(const T& value)
	{
		mValue = (T*)&value;
	}

	bool operator==(const OwnedWrapper& rhs)
	{
		return *mValue == *rhs.mValue;
	}
};

NS_BF_END

namespace std
{
	template <typename T>
	struct hash<Beefy::OwnedWrapper<T> >
	{
		size_t operator()(const Beefy::OwnedWrapper<T>& val) const
		{
			return std::hash<T>()(*val.mValue);
		}
	};
}
