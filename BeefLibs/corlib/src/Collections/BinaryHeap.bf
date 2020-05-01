// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics;

namespace System.Collections
{
	public class BinaryHeap<T>
	{
		protected T[] mData ~ delete _;
		protected int32 mSize = 0;
		protected Comparison<T> mComparison ~ delete _;

		public this(Comparison<T> comparison)
		{
			Constructor(4, comparison);
		}

		public this(int32 capacity, Comparison<T> comparison)
		{
			Constructor(capacity, comparison);
		}

		private void Constructor(int32 capacity, Comparison<T> comparison)
		{
			mData = new T[capacity];
			mComparison = comparison;
		}

		public int32 Size
		{
			get
			{
				return mSize;
			}
		}

		/// Add an item to the heap
		public void Add(T item)
		{
			if (mSize == mData.Count)
				Resize();
			mData[mSize] = item;
			HeapifyUp(mSize);
			mSize++;
		}

		/// Get the item of the root
		public T Peek()
		{
			return mData[0];
		}

		/// Extract the item of the root
		public T Pop()
		{
			T item = mData[0];
			mSize--;
			mData[0] = mData[mSize];
			HeapifyDown(0);
			return item;
		}

		public void Clear()
		{
			mSize = 0;
		}

		private void Resize()
		{
			T[] resizedData = new T[mData.Count * 2];
			Array.Copy(mData, 0, resizedData, 0, mData.Count);
			delete mData;
			mData = resizedData;
		}

		[Optimize]
		private void HeapifyUp(int32 childIdx)
		{
			if (childIdx > 0)
			{
				int32 parentIdx = (childIdx - 1) / 2;
				if (mComparison(mData[childIdx], mData[parentIdx]) > 0)
				{
					// swap parent and child
					T t = mData[parentIdx];
					mData[parentIdx] = mData[childIdx];
					mData[childIdx] = t;
					HeapifyUp(parentIdx);
				}
			}
		}

		[Optimize]
		private void HeapifyDown(int32 parentIdx)
		{
			int32 leftChildIdx = 2 * parentIdx + 1;
			int32 rightChildIdx = leftChildIdx + 1;
			int32 largestChildIdx = parentIdx;
			if (leftChildIdx < mSize && mComparison(mData[leftChildIdx], mData[largestChildIdx]) > 0)
			{
				largestChildIdx = leftChildIdx;
			}
			if (rightChildIdx < mSize && mComparison(mData[rightChildIdx], mData[largestChildIdx]) > 0)
			{
				largestChildIdx = rightChildIdx;
			}
			if (largestChildIdx != parentIdx)
			{
				T t = mData[parentIdx];
				mData[parentIdx] = mData[largestChildIdx];
				mData[largestChildIdx] = t;
				HeapifyDown(largestChildIdx);
			}
		}
	}
}

namespace System.Collections.Generic
{
	[Obsolete("The System.Collections.Generic types have been moved into System.Collections", false)]
	typealias BinaryHeap<T> = System.Collections.BinaryHeap<T>;
}
