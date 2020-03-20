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
	using System.Threading;
	
	/// A simple Queue of generic items.  Internally it is implemented as a
	/// circular buffer, so Enqueue can be O(n).  Dequeue is O(1).
	public class Queue<T> : IEnumerable<T> //, System.Collections.ICollection, IReadOnlyCollection<T>
	{
#if BF_LARGE_COLLECTIONS
		const int_cosize SizeFlags = 0x7FFFFFFF'FFFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000'00000000;
#else
		const int_cosize SizeFlags = 0x7FFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000;
#endif

		private T* mItems;
		private int_cosize mAllocSizeAndFlags;
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

		/// Creates a queue with room for capacity objects. The default initial
		/// capacity and grow factor are used.
		public this()
		{
		}
	
		/// Creates a queue with room for capacity objects. The default grow factor
		/// is used.
		[AllowAppend]
		public this(int capacity)
		{
			Debug.Assert((uint)capacity <= (uint)SizeFlags);
			T* items = append T[capacity]* (?);
			mItems = items;
			mHead = 0;
			mTail = 0;
			mSize = 0;
			if (capacity > 0)
			{
				mItems = items;
				mAllocSizeAndFlags = (int_cosize)(capacity & SizeFlags);
			}
		}

		public ~this()
		{
			if (IsDynAlloc)
			{
				var items = mItems;
#if BF_ENABLE_REALTIME_LEAK_CHECK				
				mItems= null;
				Interlocked.Fence();
#endif
				Free(items);
			}
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

		public int AllocSize
		{
			[Inline]
			get
			{
				return mAllocSizeAndFlags & SizeFlags;
			}
		}

		public bool IsDynAlloc
		{
			[Inline]
			get
			{
				return (mAllocSizeAndFlags & DynAllocFlag) != 0;
			}
		}
	
		public int Count
		{
			get { return mSize; }
		}

		protected T* Alloc(int size)
		{
			return Internal.AllocRawArrayUnmarked<T>(size);
		}

		protected void Free(T* val)
		{
			delete (void*)val;
		}

		/// Removes all items from the queue.
		public void Clear()
		{
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

			int firstPart = (AllocSize - mHead < numToCopy) ? AllocSize - mHead : numToCopy;
			//Array.Copy(mArray, mHead, array, arrayIndex, firstPart);
			Internal.MemCpy(&array.[Friend]GetRef(arrayIndex), mItems + mHead, firstPart * strideof(T), alignof(T));

			numToCopy -= firstPart;
			if (numToCopy > 0)
			{
				//Array.Copy(mArray, 0, array, arrayIndex + AllocSize - mHead, numToCopy);
				Internal.MemCpy(&array.[Friend]GetRef(arrayIndex + AllocSize - mHead), mItems, numToCopy * strideof(T), alignof(T));
			}
		}

		
		/// Adds item to the tail of the queue.
		public void Enqueue(T item)
		{
			if (mSize == AllocSize)
			{
				int newcapacity = (int)((int64)AllocSize * (int64)cGrowFactor / 100);
				if (newcapacity < AllocSize + cMinimumGrow)
				{
					newcapacity = AllocSize + cMinimumGrow;
				}
				SetCapacity(newcapacity);
			}

			mItems[mTail] = item;
			mTail = (mTail + 1) % (int_cosize)AllocSize;
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

			T removed = mItems[mHead];
			mHead = (mHead + 1) % (int_cosize)AllocSize;
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
			return mItems[mHead];
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
					if (((Object)mItems[index]) == null)
						return true;
				}
				else if (mItems[index] != null && mItems[index] == item)
				{
					return true;
				}
				index = (index + 1) % AllocSize;
			}
			return false;
		}

		T GetElement(int i)
		{
			return mItems[(mHead + i) % AllocSize];
		}

		ref T GetElementRef(int i)
		{
			return ref mItems[(mHead + i) % AllocSize];
		}

		// PRIVATE Grows or shrinks the buffer to hold capacity objects. Capacity
		// must be >= _size.
		private void SetCapacity(int value)
		{
			/*T* newarray = new T[capacity]*;
			if (mSize > 0)
			{
				if (mHead < mTail)
				{
					Array.Copy(mArray, mHead, newarray, 0, mSize);
				}else
				{
					Array.Copy(mArray, mHead, newarray, 0, AllocSize - mHead);
					Array.Copy(mArray, 0, newarray, AllocSize - mHead, mTail);
				}
			}*/

			if (value > 0)
			{
				T* newItems = Alloc(value);

#if DBG
				int typeId = typeof(T).GetTypeId();
				if (typeId == sDebugTypeId)
				{
					Debug.WriteLine("Alloc {0} {1} {2}", scope Object[] { this, newItems, sDebugIdx } );
					sDebugIdx++;
				}
#endif

				if (mSize > 0)
				{
					if (mHead < mTail)
					{
						Internal.MemCpy(newItems, mItems + mHead, mSize * strideof(T), alignof(T));

					}else
					{
						Internal.MemCpy(newItems, mItems + mHead, (AllocSize - mHead) * strideof(T), alignof(T));
						Internal.MemCpy(newItems + (AllocSize - mHead), mItems, mTail * strideof(T), alignof(T));
					}

				}

				var oldItems = mItems;
				mItems = newItems;
				if (IsDynAlloc)
				{
#if BF_ENABLE_REALTIME_LEAK_CHECK
					// We need to avoid scanning a deleted mItems
					Interlocked.Fence();
#endif						
					Free(oldItems);
				}
				mAllocSizeAndFlags = (.)(value | DynAllocFlag);
			}
			else
			{
				if (IsDynAlloc)
					Free(mItems);
				mItems = null;
				mAllocSizeAndFlags = 0;
			}

			mHead = 0;
			mTail = (mSize == value) ? 0 : mSize;
#if VERSION_QUEUE
			mVersion++;
#endif
		}

		protected override void GCMarkMembers()
		{
			if (mItems == null)
				return;
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
			int allocSize = AllocSize;
		    for (int i < mSize)
		    {
		        GC.Mark_Unbound(mItems[(i + mHead) % allocSize]);
			}
		}

		public void TrimExcess()
		{
			int32 threshold = (int32)(((double)AllocSize) * 0.9);
			if (mSize < threshold)
			{
				SetCapacity(mSize);
			}
		}    
		
		/// Implements an enumerator for a Queue.  The enumerator uses the
		/// internal version number of the list to ensure that no modifications are
		/// made to the list while an enumeration is in progress.
		public struct Enumerator : IRefEnumerator<T>
		{
			private Queue<T> mQueue;
			private int32 mIndex;   // -1 = not started, -2 = ended/disposed
#if VERSION_QUEUE
			private int32 mVersion;
#endif
			private T* mCurrentElement;

			public this(Queue<T> q)
			{
				mQueue = q;
#if VERSION_QUEUE
				mVersion = mQueue.mVersion;
#endif
				mIndex = -1;
				mCurrentElement = null;
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
				mCurrentElement = null;
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
					mCurrentElement = null;
					return false;
				}

				mCurrentElement = &mQueue.GetElementRef(mIndex);
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
					return *mCurrentElement;
				}
			}

			public ref T CurrentRef
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
					return ref *mCurrentElement;
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

			public Result<T*> GetNextRef() mut
			{
				if (!MoveNext())
					return .Err;
				return &CurrentRef;
			}
		}
	}
}
