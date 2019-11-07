#pragma once

#include "Array.h"

NS_BF_BEGIN;

template <typename T>
class BinaryMaxHeap : public Array<T>
{	
private:	
	void HeapifyUp(int32 childIdx)
	{
		if (childIdx > 0)
		{
			int32 parentIdx = (childIdx - 1) / 2;
			if (mVals[childIdx] > mVals[parentIdx])
			{
				// swap parent and child
				T t = mVals[parentIdx];
				mVals[parentIdx] = mVals[childIdx];
				mVals[childIdx] = t;
				HeapifyUp(parentIdx);
			}
		}
	}


	void HeapifyDown(int32 parentIdx)
	{
		int32 leftChildIdx = 2 * parentIdx + 1;
		int32 rightChildIdx = leftChildIdx + 1;
		int32 largestChildIdx = parentIdx;
		if (leftChildIdx < mSize && mVals[leftChildIdx] > mVals[largestChildIdx])
		{
			largestChildIdx = leftChildIdx;
		}
		if (rightChildIdx < mSize && mVals[rightChildIdx] > mVals[largestChildIdx])
		{
			largestChildIdx = rightChildIdx;
		}
		if (largestChildIdx != parentIdx)
		{
			T t = mVals[parentIdx];
			mVals[parentIdx] = mVals[largestChildIdx];
			mVals[largestChildIdx] = t;
			HeapifyDown(largestChildIdx);
		}
	}

public:	
	BinaryMaxHeap() : Array()
	{

	}

	/// Add an item to the heap
	void Add(T item)
	{
		Array::Add(item);
		HeapifyUp(mSize - 1);		
	}

	/// Get the item of the root
	T Peek()
	{
		return mVals[0];
	}

	/// Extract the item of the root
	T Pop()
	{
		T item = mVals[0];
		mSize--;
		mVals[0] = mVals[mSize];
		HeapifyDown(0);
		return item;
	}
};

template <typename T>
class BinaryMinHeap : public Array<T>
{
private:
	void HeapifyUp(int32 childIdx)
	{
		if (childIdx > 0)
		{
			int32 parentIdx = (childIdx - 1) / 2;
			if (mVals[childIdx] < mVals[parentIdx])
			{
				// swap parent and child
				T t = mVals[parentIdx];
				mVals[parentIdx] = mVals[childIdx];
				mVals[childIdx] = t;
				HeapifyUp(parentIdx);
			}
		}
	}


	void HeapifyDown(int32 parentIdx)
	{
		int32 leftChildIdx = 2 * parentIdx + 1;
		int32 rightChildIdx = leftChildIdx + 1;
		int32 largestChildIdx = parentIdx;
		if (leftChildIdx < mSize && mVals[leftChildIdx] < mVals[largestChildIdx])
		{
			largestChildIdx = leftChildIdx;
		}
		if (rightChildIdx < mSize && mVals[rightChildIdx] < mVals[largestChildIdx])
		{
			largestChildIdx = rightChildIdx;
		}
		if (largestChildIdx != parentIdx)
		{
			T t = mVals[parentIdx];
			mVals[parentIdx] = mVals[largestChildIdx];
			mVals[largestChildIdx] = t;
			HeapifyDown(largestChildIdx);
		}
	}

public:
	BinaryMinHeap() : Array()
	{

	}

	/// Add an item to the heap
	void Add(T item)
	{
		Array::Add(item);
		HeapifyUp(mSize - 1);
	}

	/// Get the item of the root
	T Peek()
	{
		return mVals[0];
	}

	/// Extract the item of the root
	T Pop()
	{
		T item = mVals[0];
		mSize--;
		mVals[0] = mVals[mSize];
		HeapifyDown(0);
		return item;
	}
};


NS_BF_END