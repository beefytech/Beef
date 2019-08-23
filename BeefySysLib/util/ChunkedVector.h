#pragma once

#include "../Common.h"

NS_BF_BEGIN

template <typename T, size_t NumChunkElements = 0x2000 / sizeof(T)>
class ChunkedVector
{
public:
	Array<T*> mChunks;
	T* mCurChunkPtr;
	T* mCurChunkEnd;

public:
	class Iterator
	{
	public:
		ChunkedVector* mVec;
		int mChunkIdx;
		T* mCurChunkPtr;
		T* mCurChunkEnd;

	public:
		Iterator(ChunkedVector& vec) : mVec(&vec)
		{			
		}

		Iterator& operator++()
		{
			mCurChunkPtr++;
			if (mCurChunkPtr == mCurChunkEnd)
			{
				if (mChunkIdx < (int)mVec->mChunks.size() - 1)
				{
					mChunkIdx++;
					mCurChunkPtr = mVec->mChunks[mChunkIdx];
					mCurChunkEnd = mCurChunkPtr + NumChunkElements;
				}
			}

			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return (mCurChunkPtr != itr.mCurChunkPtr);
		}

		T& operator*()
		{
			return *mCurChunkPtr;
		}
	};

public:
	ChunkedVector()
	{
		mCurChunkPtr = NULL;
		mCurChunkEnd = NULL;
	}

	~ChunkedVector()
	{
		for (auto chunk : mChunks)
			delete [] chunk;
	}

	T& operator[](int idx)
	{
		return mChunks[idx / NumChunkElements][idx % NumChunkElements];
	}

	T& AllocBack()
	{
		if (mCurChunkPtr == mCurChunkEnd)
		{
			mCurChunkPtr = new T[NumChunkElements];
			mCurChunkEnd = mCurChunkPtr + NumChunkElements;
			mChunks.push_back(mCurChunkPtr);
		}
		return *(mCurChunkPtr++);
	}

	void push_back(T val)
	{
		AllocBack() = val;		
	}

	size_t size()
	{
		if (mChunks.size() == 0)
			return 0;
		return (mChunks.size() - 1)*NumChunkElements + (NumChunkElements - (mCurChunkEnd - mCurChunkPtr));
	}

	T& front()
	{
		return mChunks[0][0];
	}

	T& back()
	{
		return mCurChunkPtr[-1];
	}

	Iterator begin()
	{
		Iterator itr(*this);
		if (mChunks.size() == 0)
		{
			itr.mChunkIdx = -1;
			itr.mCurChunkPtr = NULL;
			itr.mCurChunkEnd = NULL;
		}
		else
		{
			itr.mChunkIdx = 0;
			itr.mCurChunkPtr = mChunks[0];
			itr.mCurChunkEnd = itr.mCurChunkPtr + NumChunkElements;
		}
		return itr;
	}

	Iterator end()
	{
		Iterator itr(*this);
		itr.mChunkIdx = -1;
		itr.mCurChunkPtr = mCurChunkPtr;
		itr.mCurChunkEnd = NULL;		
		return itr;
	}

	Iterator GetIterator(size_t idx)
	{
		Iterator itr(*this);
		itr.mChunkIdx = (int)(idx / NumChunkElements);
		if (itr.mChunkIdx == mChunks.size())
		{
			BF_ASSERT(idx == size());
			return end();
		}
		itr.mCurChunkPtr = mChunks[itr.mChunkIdx] + (idx % NumChunkElements);
		itr.mCurChunkEnd = mChunks[itr.mChunkIdx] + NumChunkElements;
		return itr;
	}
};

NS_BF_END
