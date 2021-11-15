#pragma once

#include "../Common.h"

NS_BF_BEGIN

template <typename T>
class Span
{
public:
	T* mVals;
	intptr mSize;

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

	Span()
	{

	}

	Span(T* mPtr, intptr size)
	{
		mVals = mPtr;
		mSize = size;		
	}
	
	T& operator[](intptr idx) const
	{
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

	intptr size() const
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

	T Get(intptr idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return (T)0;
		return mVals[idx];
	}

	template <typename T2>
	T2 GetAs(intptr idx)
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

	void SetSize(intptr size)
	{
		BF_ASSERT(size <= mSize);
		mSize = size;
	}
};

NS_BF_END
