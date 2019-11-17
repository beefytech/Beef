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
			if (this->mVals[childIdx] > this->mVals[parentIdx])
			{
				// swap parent and child
				T t = this->mVals[parentIdx];
				this->mVals[parentIdx] = this->mVals[childIdx];
				this->mVals[childIdx] = t;
				HeapifyUp(parentIdx);
			}
		}
	}


	void HeapifyDown(int32 parentIdx)
	{
		int32 leftChildIdx = 2 * parentIdx + 1;
		int32 rightChildIdx = leftChildIdx + 1;
		int32 largestChildIdx = parentIdx;
		if (leftChildIdx < this->mSize && this->mVals[leftChildIdx] > this->Vals[largestChildIdx])
		{
			largestChildIdx = leftChildIdx;
		}
		if (rightChildIdx < this->mSize && this->mVals[rightChildIdx] > this->mVals[largestChildIdx])
		{
			largestChildIdx = rightChildIdx;
		}
		if (largestChildIdx != parentIdx)
		{
			T t = this->mVals[parentIdx];
			this->mVals[parentIdx] = this->mVals[largestChildIdx];
			this->mVals[largestChildIdx] = t;
			HeapifyDown(largestChildIdx);
		}
	}

public:	
	BinaryMaxHeap() : Array<T>()
	{

	}

	/// Add an item to the heap
	void Add(T item)
	{
		Array<T>::Add(item);
		HeapifyUp(this->mSize - 1);		
	}

	/// Get the item of the root
	T Peek()
	{
		return this->mVals[0];
	}

	/// Extract the item of the root
	T Pop()
	{
		T item = this->mVals[0];
		this->mSize--;
		this->mVals[0] = this->mVals[this->mSize];
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
			if (this->mVals[childIdx] < this->mVals[parentIdx])
			{
				// swap parent and child
				T t = this->mVals[parentIdx];
				this->mVals[parentIdx] = this->mVals[childIdx];
				this->mVals[childIdx] = t;
				HeapifyUp(parentIdx);
			}
		}
	}


	void HeapifyDown(int32 parentIdx)
	{
		int32 leftChildIdx = 2 * parentIdx + 1;
		int32 rightChildIdx = leftChildIdx + 1;
		int32 largestChildIdx = parentIdx;
		if (leftChildIdx < this->mSize && this->mVals[leftChildIdx] < this->mVals[largestChildIdx])
		{
			largestChildIdx = leftChildIdx;
		}
		if (rightChildIdx < this->mSize && this->mVals[rightChildIdx] < this->mVals[largestChildIdx])
		{
			largestChildIdx = rightChildIdx;
		}
		if (largestChildIdx != parentIdx)
		{
			T t = this->mVals[parentIdx];
			this->mVals[parentIdx] = this->mVals[largestChildIdx];
			this->mVals[largestChildIdx] = t;
			HeapifyDown(largestChildIdx);
		}
	}

public:
	BinaryMinHeap() : Array<T>()
	{

	}

	/// Add an item to the heap
	void Add(T item)
	{
		Array<T>::Add(item);
		HeapifyUp(this->mSize - 1);
	}

	/// Get the item of the root
	T Peek()
	{
		return this->mVals[0];
	}

	/// Extract the item of the root
	T Pop()
	{
		T item = this->mVals[0];
		this->mSize--;
		this->mVals[0] = this->mVals[this->mSize];
		HeapifyDown(0);
		return item;
	}
};


NS_BF_END