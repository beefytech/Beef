// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Collections
{
	using System;
	using System.Diagnostics;
	using System.Threading;

	/// A simple stack of objects.  Internally it is implemented as an array,
	/// so Push can be O(n).  Pop is O(1).
	public class Stack<T> : IEnumerable<T>//System.Collections.ICollection, IReadOnlyCollection<T>
	{
		const String InvalidOperation_EnumNotStarted = "Enumeration not started";
		const String InvalidOperation_EnumEnded = "Enumeration ended";
		const String InvalidOperation_EnumFailedVersion = "Collection was modified; enumeration operation may not execute.";

#if BF_LARGE_COLLECTIONS
		const int_cosize SizeFlags = 0x7FFFFFFF'FFFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000'00000000;
#else
		const int_cosize SizeFlags = 0x7FFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000;
#endif
		private const int cDefaultCapacity = 4;


		private T* mItems;// Storage for stack elements
		private int_cosize mAllocSizeAndFlags;
		private int_cosize mSize;// Number of items in the stack.
		private int mVersion;// Used to keep enumerator in sync w/ collection.

		private Object mSyncRoot;


		[AllowAppend]
		public this()
			: this(cDefaultCapacity)
		{
		}

		/// Create a stack with a specific initial capacity.  The initial capacity
		/// must be a non-negative number.
		[AllowAppend]
		public this(int capacity)
		{
			Debug.Assert((uint)capacity <= (uint)SizeFlags);

			T* items = append T[capacity]* (?);
			mSize = 0;
			mVersion = 0;

			if (capacity > 0)
			{
				mItems = items;
				mAllocSizeAndFlags = (int_cosize)(capacity & SizeFlags);
			}
		}

		/// Fills a Stack with the contents of a particular collection.  The items are
		/// pushed onto the stack in the same order they are read by the enumerator.
		public this(IEnumerator<T> enumerator)
		{
			for (let value in enumerator)
				Push(value);
		}

		public ~this()
		{
			if (IsDynAlloc)
			{
				var items = mItems;
#if BF_ENABLE_REALTIME_LEAK_CHECK				
				mItems = null;
				Interlocked.Fence();
#endif
				Free(items);
			}
		}

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

		/*bool System.Collections.ICollection.IsSynchronized
		{
			get { return false; }
		}*/

		/*Object System.Collections.ICollection.SyncRoot
		{
			get
			{
				if (_syncRoot == null)
				{
					System.Threading.Interlocked.CompareExchange<Object>(ref _syncRoot, new Object(), null);
				}
				return _syncRoot;
			}
		}*/

		protected virtual T* Alloc(int size)
		{
			return Internal.AllocRawArrayUnmarked<T>(size);
		}

		protected virtual void Free(T* val)
		{
			delete (void*)val;
		}

		/// Removes all Objects from the Stack.
		public void Clear()
		{
			mSize = 0;
			mVersion++;
		}

		public bool Contains(T item)
		{
			int count = mSize;

			while (count-- > 0)
			{
				if (((Object)item) == null)
				{
					if (((Object)mItems[count]) == null)
						return true;
				}
				else if (mItems[count] != null && mItems[count] == item)
				{
					return true;
				}
			}
			return false;
		}

		/// Copies the stack into an array.
		public void CopyTo(T[] array, int arrayIndex)
		{
			Debug.Assert(array != null);
			Debug.Assert(arrayIndex >= 0 && arrayIndex <= array.Count);
			Debug.Assert(array.Count - arrayIndex >= mSize);

			for (let i < mSize)
			    array[i+arrayIndex] = mItems[mSize-i-1];
		}

		/// Returns an IEnumerator for this Stack.
		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public void TrimExcess()
		{
			let threshold = (int32)(((double)AllocSize) * 0.9);
			if (mSize < threshold)
			{
				Realloc(mSize);
				mVersion++;
			}
		}

		/// Returns the top object on the stack without removing it.  If the stack
		/// is empty, Peek throws an InvalidOperationException.
		public T Peek()
		{
			Debug.Assert(mSize != 0);
			return mItems[mSize - 1];
		}

		/// Pops an item from the top of the stack.  If the stack is empty, Pop
		/// throws an InvalidOperationException.
		public T Pop()
		{
			Debug.Assert(mSize != 0);
			mVersion++;
			T item = mItems[--mSize];
			mItems[mSize] = default(T);     // Free memory quicker.
			return item;
		}

		/// Pushes an item to the top of the stack.
		public void Push(T item)
		{
			if (mSize == AllocSize)
			{
				let newCap = (AllocSize == 0) ? cDefaultCapacity : 2 * AllocSize;
				Realloc(newCap);
			}
			mItems[mSize++] = item;
			mVersion++;
		}

		public void Push(Span<T> values)
		{
			for (let value in values)
				Push(value);
		}

		private T* Realloc(int newSize)
		{
			T* oldAlloc = null;
			if (newSize > 0)
			{
				T* newItems = Alloc(newSize);

				if (mSize > 0)
					Internal.MemCpy(newItems, mItems, mSize * strideof(T), alignof(T));
				if (IsDynAlloc)
					oldAlloc = mItems;
				mItems = newItems;
				mAllocSizeAndFlags = (.)(newSize | DynAllocFlag);
			}
			else
			{
				if (IsDynAlloc)
					oldAlloc = mItems;
				mItems = null;
				mAllocSizeAndFlags = 0;
			}

			if (oldAlloc != null)
			{
#if BF_ENABLE_REALTIME_LEAK_CHECK
				// We need to avoid scanning a deleted mItems
				Interlocked.Fence();
#endif	
				Free(oldAlloc);
				return null;
			}

			return oldAlloc;
		}

		protected override void GCMarkMembers()
		{
			if (mItems == null)
				return;
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
			for (let i < mSize)
			{
			    GC.Mark_Unbound(mItems[i]); 
			}
		}

		public struct Enumerator : IRefEnumerator<T*>, IEnumerator<T>
		{
			private Stack<T> mStack;
			private int mIndex;
			private int mVersion;
			private T* mCurrentElement;

			public this(Stack<T> s)
			{
				mStack = s;
				mVersion = s.mVersion;
				mIndex = -2;
				mCurrentElement = null;
			}

			public void Dispose() mut
			{
				mIndex = -1;
			}

			public bool MoveNext() mut
			{
				bool retval;
				if (mVersion != mStack.mVersion) Runtime.FatalError(InvalidOperation_EnumFailedVersion);
				if (mIndex == -2)
				{  // First call to enumerator.
					mIndex = mStack.mSize - 1;
					retval = (mIndex >= 0);
					if (retval)
						mCurrentElement = &mStack.mItems[mIndex];
					return retval;
				}
				if (mIndex == -1)
				{  // End of enumeration.
					return false;
				}

				retval = (--mIndex >= 0);
				if (retval)
					mCurrentElement = &mStack.mItems[mIndex];
				else
					mCurrentElement = null;
				return retval;
			}

			public T Current
			{
				get
				{
					if (mIndex == -2) Runtime.FatalError(InvalidOperation_EnumNotStarted);
					if (mIndex == -1) Runtime.FatalError(InvalidOperation_EnumEnded);
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

			public void Reset()	mut
			{
				if (mVersion != mStack.mVersion) Runtime.FatalError(InvalidOperation_EnumFailedVersion);
				mIndex = -2;
				mCurrentElement = null;
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

	/*class TestingStack
	{
		[Test]
		public static void TestPush()
		{
		    Stack<int> s = scope .();
		    s.Push(1);
		    s.Push(2);
		    s.Push(3);

		    Test.Assert(s.Count == 3);
		}

		[Test]
		public static void TestPushRange()
		{
		    Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3, 4, 5 });
		    
		    Test.Assert(s.Count == 5);
		}


		[Test]
		public static void TestPop()
		{
		    Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3 });
		    
		    while (s.Count > 0)
		        s.Pop();
		    
		    Test.Assert(s.Count == 0);
		}

		[Test]
		public static void TestClear()
		{
		    Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3, 4, 5 });

		    s.Clear();
		    
		    Test.Assert(s.Count == 0);
		}

		[Test]
		public static void TestTrim()
		{
			Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3, 4, 5 });

			Test.Assert(s.AllocSize >= 5);
			Test.Assert(s.Count == 5);
		    
		    while (s.Count > 1)
		        s.Pop();

			Test.Assert(s.AllocSize >= 5);
			Test.Assert(s.Count == 1);

		    s.TrimExcess();
			Test.Assert(s.AllocSize == s.Count);
		}

		[Test]
		public static void TestContains()
		{
		    Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3, 4, 5 });
		    
		    Test.Assert(s.Contains(3) == true);
		    Test.Assert(s.Contains(10) == false);
		}

		[Test]
		public static void TestCopyTo()
		{
		    Stack<int> s = scope .();
		    s.Push(scope int[] { 1, 2, 3, 4, 5 });

			var destArr = scope int[10];
			s.CopyTo(destArr, 0);
		    
		    Test.Assert(destArr[0] == 5);
			Test.Assert(destArr[4] == 1);
		}

		[Test]
		public static void TestIterator()
		{
		    Stack<int> s = scope .();
			s.Push(scope int[] { 1, 2, 3, 4 });

		    int n = 0;
		    for (let value in s)
		        n += value;
		    Test.Assert(n == 10);
		}

		[Test]
		public static void TestIteratorRef()
		{
		    Stack<int> s = scope .();
			s.Push(scope int[] { 1, 2, 3, 4 });

		    int n = 0;
			for (var value in ref s)
			{
				if (value == 1)
					value = 10;
		        n += value;
			}
		    Test.Assert(n == 19);

			n = 0;
			while (s.Count > 0)
				n += s.Pop();

			Test.Assert(n == 19);
		}
	}*/
}
