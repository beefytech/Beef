// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_QUEUE
#endif

namespace System.Collections.Generic
{
	using System;
	using System.Diagnostics;
	
	/// A simple Queue of generic items.  Internally it is implemented as a
	/// circular buffer, so Enqueue can be O(n).  Dequeue is O(1).
	public class Queue<T> : IEnumerable<T> //, System.Collections.ICollection, IReadOnlyCollection<T>
	{
		private T[] mArray;
		private int_cosize mHead;       // First valid element in the queue
		private int_cosize mTail;       // Last valid element in the queue
		private int_cosize mSize;       // Number of elements.
#if VERSION_QUEUE
		private int32 mVersion;
		const String cVersionError = "Queue changed during enumeration";
#endif
		private Object mSyncRoot;

		private const int32 cMinimumGrow = 4;
		private const int32 cShrinkThreshold = 32;
		private const int32 cGrowFactor = 200;  // double each time
		private const int32 cDefaultCapacity = 4;
		static T[] sEmptyArray = new T[0] ~ delete _;
		
		/// Creates a queue with room for capacity objects. The default initial
		/// capacity and grow factor are used.
		public this()
		{
			mArray = sEmptyArray;
		}
	
		/// Creates a queue with room for capacity objects. The default grow factor
		/// is used.
		public this(int capacity)
		{
			Debug.Assert(capacity >= 0);
			mArray = new T[capacity];
			mHead = 0;
			mTail = 0;
			mSize = 0;
		}
		
		// Fills a Queue with the elements of an ICollection.  Uses the enumerator
		// to get each of the elements.
		//
		/// <include file='doc\Queue.uex' path='docs/doc[@for="Queue.Queue3"]/*' />
		/*public void Set<T>(T collection) where T : IEnumerable<T>
		{
			if (collection == null)
				//ThrowHelper.ThrowArgumentNullException(ExceptionArgument.collection);
				Runtime.FatalError();

			_array = new T[_DefaultCapacity];
			_size = 0;
			_version = 0;

			using (var en = collection.GetEnumerator())
			{
				while (en.MoveNext())
				{
					Enqueue(en.Current);
				}
			}
		}*/
	
	
		public int Count
		{
			get { return mSize; }
		}
		
		/// Removes all items from the queue.
		public void Clear()
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			if (mHead < mTail)
				Array.Clear(mArray, mHead, mSize);
			else
			{
				Array.Clear(mArray, mHead, mArray.Count - mHead);
				Array.Clear(mArray, 0, mTail);
			}
#endif
			mHead = 0;
			mTail = 0;
			mSize = 0;
#if VERSION_QUEUE
			mVersion++;
#endif
		}
	
		/// CopyTo copies a collection into an Array, starting at a particular
		/// index into the array.
		public void CopyTo(T[] array, int arrayIndex)
		{
			Debug.Assert((uint)arrayIndex <= (uint)array.Count);
			int arrayLen = array.Count;
			Debug.Assert(arrayLen >= mSize);
			
			int numToCopy = (arrayLen - arrayIndex < mSize) ? (arrayLen - arrayIndex) : mSize;
			if (numToCopy == 0) return;

			int firstPart = (mArray.Count - mHead < numToCopy) ? mArray.Count - mHead : numToCopy;
			Array.Copy(mArray, mHead, array, arrayIndex, firstPart);
			numToCopy -= firstPart;
			if (numToCopy > 0)
			{
				Array.Copy(mArray, 0, array, arrayIndex + mArray.Count - mHead, numToCopy);
			}
		}

		
		/// Adds item to the tail of the queue.
		public void Enqueue(T item)
		{
			if (mSize == mArray.Count)
			{
				int newcapacity = (int)((int64)mArray.Count * (int64)cGrowFactor / 100);
				if (newcapacity < mArray.Count + cMinimumGrow)
				{
					newcapacity = mArray.Count + cMinimumGrow;
				}
				SetCapacity(newcapacity);
			}

			mArray[mTail] = item;
			mTail = (mTail + 1) % (int_cosize)mArray.Count;
			mSize++;
#if VERSION_QUEUE
			mVersion++;
#endif
		}
	
		/// GetEnumerator returns an IEnumerator over this Queue.  This
		/// Enumerator will support removing.
		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		/// Removes the object at the head of the queue and returns it. If the queue
		/// is empty, this method simply returns null.
		public T Dequeue()
		{
			if (mSize == 0)
				//ThrowHelper.ThrowInvalidOperationException(ExceptionResource.InvalidOperation_EmptyQueue);
				Runtime.FatalError();

			T removed = mArray[mHead];
			mArray[mHead] = default(T);
			mHead = (mHead + 1) % (int_cosize)mArray.Count;
			mSize--;
#if VERSION_QUEUE
			mVersion++;
#endif
			return removed;
		}
	
		/// Returns the object at the head of the queue. The object remains in the
		/// queue. If the queue is empty, this method throws an 
		/// InvalidOperationException.
		public T Peek()
		{
			Debug.Assert(mSize != 0);
			return mArray[mHead];
		}
	
		/// Returns true if the queue contains at least one object equal to item.
		/// Equality is determined using item.Equals().
		public bool Contains(T item)
		{
			int index = mHead;
			int count = mSize;
			while (count-- > 0)
			{
				if (((Object)item) == null)
				{
					if (((Object)mArray[index]) == null)
						return true;
				}
				else if (mArray[index] != null && mArray[index] == item)
				{
					return true;
				}
				index = (index + 1) % mArray.Count;
			}
			return false;
		}

		internal T GetElement(int i)
		{
			return mArray[(mHead + i) % mArray.Count];
		}
	
		/// Iterates over the objects in the queue, returning an array of the
		/// objects in the Queue, or an empty array if the queue is empty.
		/// The order of elements in the array is first in to last in, the same
		/// order produced by successive calls to Dequeue.
		public T[] ToArray()
		{
			T[] arr = new T[mSize];
			if (mSize == 0)
				return arr;

			if (mHead < mTail)
			{
				Array.Copy(mArray, mHead, arr, 0, mSize);
			}else
			{
				Array.Copy(mArray, mHead, arr, 0, mArray.Count - mHead);
				Array.Copy(mArray, 0, arr, mArray.Count - mHead, mTail);
			}

			return arr;
		}
	
	
		// PRIVATE Grows or shrinks the buffer to hold capacity objects. Capacity
		// must be >= _size.
		private void SetCapacity(int capacity)
		{
			T[] newarray = new T[capacity];
			if (mSize > 0)
			{
				if (mHead < mTail)
				{
					Array.Copy(mArray, mHead, newarray, 0, mSize);
				}else
				{
					Array.Copy(mArray, mHead, newarray, 0, mArray.Count - mHead);
					Array.Copy(mArray, 0, newarray, mArray.Count - mHead, mTail);
				}
			}

			mArray = newarray;
			mHead = 0;
			mTail = (mSize == capacity) ? 0 : mSize;
#if VERSION_QUEUE
			mVersion++;
#endif
		}

		public void TrimExcess()
		{
			int32 threshold = (int32)(((double)mArray.Count) * 0.9);
			if (mSize < threshold)
			{
				SetCapacity(mSize);
			}
		}    
		
		/// Implements an enumerator for a Queue.  The enumerator uses the
		/// internal version number of the list to ensure that no modifications are
		/// made to the list while an enumeration is in progress.
		public struct Enumerator : IEnumerator<T>
		{
			private Queue<T> mQueue;
			private int32 mIndex;   // -1 = not started, -2 = ended/disposed
#if VERSION_QUEUE
			private int32 mVersion;
#endif
			private T mCurrentElement;

			internal this(Queue<T> q)
			{
				mQueue = q;
#if VERSION_QUEUE
				mVersion = mQueue.mVersion;
#endif
				mIndex = -1;
				mCurrentElement = default(T);
			}

#if VERSION_QUEUE
			void CheckVersion()
			{
				if (mVersion != mQueue.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

			public void Dispose() mut
			{
				mIndex = -2;
				mCurrentElement = default(T);
			}

			public bool MoveNext() mut
			{
#if VERSION_QUEUE
				CheckVersion();
#endif
				if (mIndex == -2)
					return false;

				mIndex++;

				if (mIndex == mQueue.mSize)
				{
					mIndex = -2;
					mCurrentElement = default(T);
					return false;
				}

				mCurrentElement = mQueue.GetElement(mIndex);
				return true;
			}
	
			public T Current
			{
				get
				{
					if (mIndex < 0)
					{
						if (mIndex == -1)
							Runtime.FatalError("Enumeration not started");
						else
							Runtime.FatalError("Enumeration ended");
					}
					return mCurrentElement;
				}
			}

			public void Reset() mut
			{
#if VERSION_QUEUE
				CheckVersion();
#endif
				mIndex = -1;
				mCurrentElement = default;
			}

			public Result<T> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
