// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

#if PARANOID
#define VERSION_LIST
#endif

using System;
using System.Diagnostics;
using System.Diagnostics.Contracts;
using System.Threading;
using System.Reflection;

namespace System.Collections
{
	interface IList
	{
		Variant this[int index]
		{
			get;
			set;
		}
	}

	public class List<T> : IEnumerable<T>, IList, ICollection<T>
	{
		private const int_cosize cDefaultCapacity = 4;

#if BF_LARGE_COLLECTIONS
		const int_cosize SizeFlags = 0x7FFFFFFF'FFFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000'00000000;
#else
		const int_cosize SizeFlags = 0x7FFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000;
#endif

		private T* mItems;
		private int_cosize mSize;
		private int_cosize mAllocSizeAndFlags;
#if VERSION_LIST
		private int32 mVersion;
		const String cVersionError = "List changed during enumeration";
#endif

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

		public this()
		{
		}

		public this(IEnumerator<T> enumerator)
		{
			for (var item in enumerator)
				Add(item);
		}

		[AllowAppend]
		public this(Span<T> span) : this(span.Length)
		{
			AddRange(span);
		}

		[AllowAppend]
		public this(int capacity)
		{
			Debug.Assert((uint)capacity <= (uint)SizeFlags);
			T* items = append T[capacity]* (?);
			if (capacity > 0)
			{
				mItems = items;
				mAllocSizeAndFlags = (int_cosize)(capacity & SizeFlags);
			}
		}
		/*public this(int capacity)
		{
			Debug.Assert((uint)capacity <= (uint)SizeFlags);
			if (capacity > 0)
			{
				mItems = Alloc(capacity);
				mAllocSizeAndFlags = (int_cosize)(capacity | DynAllocFlag);
			}
		}*/

		public ~this()
		{
#if DBG
			int typeId = typeof(T).GetTypeId();
			if (typeId == sDebugTypeId)
			{
				Debug.WriteLine("Dealloc {0} {1}", scope Object[] { this, mItems } );
			}
#endif

			if (IsDynAlloc)
			{
				var items = mItems;
#if BF_ENABLE_REALTIME_LEAK_CHECK
				// To avoid scanning items being deleted
				mItems = null;
				Interlocked.Fence();
#endif
				Free(items);
			}
		}

		public T* Ptr
		{
			get
			{
				return mItems;
			}
		}

#if DBG
		static int_cosize sDebugTypeId = 470;
		static int_cosize sDebugIdx = 0;
#endif

		public int Capacity
		{
			get
			{
				return mAllocSizeAndFlags & SizeFlags;
			}

			set
			{
				Debug.Assert((uint)value <= (uint)SizeFlags);
				if (value != AllocSize)
				{
					T* oldAlloc = Realloc(value, true);
					if (oldAlloc != null)
						Free(oldAlloc);
				}
			}
		}

		public int Count
		{
			[Inline]
			get
			{
				return mSize;
			}

			set
			{
				if (value <= mSize)
				{
					mSize = (.)value;
				}
				else
				{
					int addSize = value - mSize;
					Internal.MemSet(GrowUnitialized(addSize), 0, addSize * strideof(T));
				}
			}
		}

		public bool IsEmpty
		{
			get
			{
				return mSize == 0;
			}
		}

		public ref T this[int index]
		{
			[Checked]
			get
			{
				Runtime.Assert((uint)index < (uint)mSize);
				return ref mItems[index];
			}

			[Unchecked, Inline]
			get
			{
				return ref mItems[index];
			}

			[Checked]
			set
			{
				Runtime.Assert((uint)index < (uint)mSize);
				mItems[index] = value;
#if VERSION_LIST
				mVersion++;
#endif
			}

			[Unchecked, Inline]
			set
			{
				mItems[index] = value;
#if VERSION_LIST
				mVersion++;
#endif
			}
		}

		public ref T this[Index index]
		{
			[Checked]
			get
			{
				int idx = index.Get(mSize);
				Runtime.Assert((uint)idx < (uint)mSize);
				return ref mItems[idx];
			}

			[Unchecked, Inline]
			get
			{
				return ref mItems[index.Get(mSize)];
			}

			[Checked]
			set
			{
				int idx = index.Get(mSize);
				Runtime.Assert((uint)idx < (uint)mSize);
				mItems[idx] = value;
#if VERSION_LIST
				mVersion++;
#endif
			}

			[Unchecked, Inline]
			set
			{
				mItems[index.Get(mSize)] = value;
#if VERSION_LIST
				mVersion++;
#endif
			}
		}

		public Span<T> this[IndexRange range]
		{
#if !DEBUG
			[Inline]
#endif
			get
			{
				return Span<T>(mItems, mSize)[range];
			}
		}

		public ref T Front
		{
			[Checked]
			get
			{
				Runtime.Assert(mSize != 0);
				return ref mItems[0];
			}

			[Unchecked, Inline]
			get
			{
				return ref mItems[0];
			}
		}
		
		public ref T Back
		{
			[Checked]
			get
			{
				Runtime.Assert(mSize != 0);
				return ref mItems[mSize - 1];
			}

			[Unchecked, Inline]
			get
			{
				return ref mItems[mSize - 1];
			}
		}

		Variant IList.this[int index]
		{
			get
			{
				return [Unbound]Variant.Create(this[index]);
			}

			set
			{
				ThrowUnimplemented();
			}
		}

		protected virtual T* Alloc(int size)
		{
			return Internal.AllocRawArrayUnmarked<T>(size);
		}

		protected virtual void Free(T* val)
		{
			delete (void*)val;
		}

		T* Realloc(int newSize, bool autoFree)
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

			if ((autoFree) && (oldAlloc != null))
			{
				Free(oldAlloc);
				return null;
			}

			return oldAlloc;
		}

		/// Adds an item to the back of the list.
		public void Add(T item)
		{
			if (mSize == AllocSize)
			{
				// We free after the insert to allow for inserting a ref to another list element
				let oldPtr = EnsureCapacity(mSize + 1, false);
				mItems[mSize++] = item;
				Free(oldPtr);
				return;
			}
			mItems[mSize++] = item;
#if VERSION_LIST
			mVersion++;
#endif
		}

		/// Adds an item to the front of the list.
		public void AddFront(T item)
		{
			Insert(0, item);
		}

		/// Adds an item to the back of the list.
		public void AddRange(Span<T> addSpan)
		{
			if (mSize + addSpan.Length > AllocSize)
			{
				let oldPtr = EnsureCapacity(mSize + addSpan.Length, false);
				for (var val in ref addSpan)
					mItems[mSize++] = val;
				Free(oldPtr);
				return;
			}
			for (var val in ref addSpan)
				mItems[mSize++] = val;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void AddRange(IEnumerator<T> items)
		{
			for (let item in items)
				Add(item);
		}

		public void AddRange<TEnumable>(TEnumable items) where TEnumable : IEnumerable<T>
		{
			for (let item in items.GetEnumerator())
				Add(item);
		}

		public Span<T> GetRange(int offset)
		{
			Debug.Assert((uint)offset <= (uint)mSize);
			return .(mItems + offset, mSize - offset);
		}

		public Span<T> GetRange(int offset, int count)
		{
			Debug.Assert((uint)offset <= (uint)mSize);
			Debug.Assert((uint)offset + (uint)count <= (uint)mSize);
			return .(mItems + offset, count);
		}

		/// Returns a pointer to the start of the added uninitialized section
		public T* GrowUnitialized(int addSize)
		{
			if (mSize + addSize > AllocSize) EnsureCapacity(mSize + addSize, true);
			mSize += (int_cosize)addSize;
#if VERSION_LIST
			mVersion++;
#endif
			if (addSize == 0)
				return null;
			return &mItems[mSize - addSize];
		}

		public void Clear()
		{
			if (mSize > 0)
			{
				mSize = 0;
#if VERSION_LIST
				mVersion++;
#endif
			}
		}

		/*public static void DeleteItemsAndClear<T>(List<T> list) where T : delete
		{
			foreach (var item in list)
				delete item;
			list.Clear();
		}*/

		public bool Contains(T item)
		{
			if (item == null)
			{
			    for (int i = 0; i < mSize; i++)
			        if (mItems[i] == null)
			    		return true;
			    return false;
			}
			else
			{
				for (int i = 0; i < mSize; i++)
					if (mItems[i] == item)
						return true;
			    return false;
			}
		}

		public bool ContainsStrict(T item)
		{
			if (item == null)
			{
			    for (int i = 0; i < mSize; i++)
			        if (mItems[i] === null)
			    		return true;
			    return false;
			}
			else
			{
				for (int i = 0; i < mSize; i++)
					if (mItems[i] === item)
						return true;
			    return false;
			}
		}

		public bool ContainsAlt<TAlt>(TAlt item) where TAlt : IHashable where bool : operator T == TAlt
		{
			return IndexOfAlt(item) != -1;
		}

		public void CopyTo(T[] array)
		{
			CopyTo(array, 0);
		}

		public void CopyTo(List<T> destList)
		{
			destList.EnsureCapacity(mSize, true);
			destList.mSize = mSize;
			if (mSize > 0)
				Internal.MemCpy(destList.mItems, mItems, mSize * strideof(T), alignof(T));
		}

		public void CopyTo(Span<T> span)
		{
			// Delegate rest of error checking to Array.Copy.
			for (int i = 0; i < mSize; i++)
				span[i] = mItems[i];
		}

		public void CopyTo(T[] array, int arrayIndex)
		{
			// Delegate rest of error checking to Array.Copy.
			for (int i = 0; i < mSize; i++)
				array[i + arrayIndex] = mItems[i];
		}

		public void CopyTo(int index, T[] array, int arrayIndex, int count)
		{
			// Delegate rest of error checking to Array.Copy.
			for (int i = 0; i < count; i++)
				array[i + arrayIndex] = mItems[i + index];
		}

		public T* EnsureCapacity(int min, bool autoFree)
		{
			int allocSize = AllocSize;
			if (allocSize >= min)
				return null;
			
			int_cosize newCapacity = (int_cosize)(allocSize == 0 ? cDefaultCapacity : allocSize * 2);
			// Allow the list to grow to maximum possible capacity (~2G elements) before encountering overflow.
			// Note that this check works even when mItems.Length overflowed thanks to the (uint) cast
			//if ((uint)newCapacity > Array.MaxArrayLength) newCapacity = Array.MaxArrayLength;
			if (newCapacity < min) newCapacity = (int_cosize)min;
			return Realloc(newCapacity, autoFree);
		}

		public void Reserve(int size)
		{
			EnsureCapacity(size, true);
		}

		public void Resize(int size)
		{
			EnsureCapacity(size, true);
			int addSize = size - mSize;
			if (addSize > 0)
				Internal.MemSet(Ptr + mSize, 0, addSize * alignof(T));
			mSize = (.)size;
		}

		public void Resize(int newSize, T fillValue)
		{
		    let prevSize = mSize;
		    Count = newSize;
	        for (int i = prevSize; i < newSize; i++)
	            mItems[i] = fillValue;
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public int FindIndex(Predicate<T> match)
		{
			for (int i = 0; i < mSize; i++)
				if (match(mItems[i]))
					return i;
			return -1;
		}

		public int IndexOf(T item)
		{
			for (int i = 0; i < mSize; i++)
				if (mItems[i] == item)
					return i;
			return -1;
		}

		public int IndexOf(T item, int index)
		{
			for (int i = index; i < mSize; i++)
				if (mItems[i] == item)
					return i;
			return -1;
		}

		public int IndexOf(T item, int index, int count)
		{
			for (int i = index; i < index + count; i++)
				if (mItems[i] == item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item)
		{
			for (int i = 0; i < mSize; i++)
				if (mItems[i] === item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item, int index)
		{
			for (int i = index; i < mSize; i++)
				if (mItems[i] === item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item, int index, int count)
		{
			for (int i = index; i < index + count; i++)
				if (mItems[i] === item)
					return i;
			return -1;
		}

		public int IndexOfAlt<TAlt>(TAlt item) where TAlt : IHashable where bool : operator T == TAlt
		{
			for (int i = 0; i < mSize; i++)
				if (mItems[i] == item)
					return i;
			return -1;
		}

		public int LastIndexOf(T item)
		{
			for (int i = mSize - 1; i >= 0; i--)
				if (mItems[i] == item)
					return i;
			return -1;
		}

		public int LastIndexOfStrict(T item)
		{
			for (int i = mSize - 1; i >= 0; i--)
				if (mItems[i] === item)
					return i;
			return -1;
		}

		public void Set(Span<T> span)
		{
			Count = span.Length;
			for (int i < mSize)
				mItems[i] = span[i];
		}

		public void SetAll(T value)
		{
			for (int i < mSize)
				mItems[i] = value;
		}

		public void Insert(int index, T item)
		{
			var item; // This creates a copy - required if item is a ref to an element
			if (mSize == AllocSize) EnsureCapacity(mSize + 1, true);
			if (index < mSize)
			{
				Internal.MemMove(mItems + index + 1, mItems + index, (mSize - index) * strideof(T), alignof(T));
			}
			mItems[index] = item;
			mSize++;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void Insert(int index, Span<T> items)
		{
			//TODO: Handle case where Span is a reference to ourselves

			if (items.Length == 0)
				return;
			int addCount = items.Length;
			if (mSize + addCount > AllocSize) EnsureCapacity(mSize + addCount, true);
			if (index < mSize)
			{
				Internal.MemMove(mItems + index + addCount, mItems + index, (mSize - index) * strideof(T), alignof(T));
			}
			Internal.MemCpy(mItems + index, items.Ptr, addCount * strideof(T));
			mSize += (int_cosize)addCount;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void RemoveAt(int index)
		{
			Debug.Assert((uint)index < (uint)mSize);
			if (index < mSize - 1)
			{
				Internal.MemMove(mItems + index, mItems + index + 1, (mSize - index - 1) * strideof(T), alignof(T));
			}
			mSize--;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void RemoveRange(int index, int count)
		{
			Debug.Assert((uint)index + (uint)count <= (uint)mSize);
			if (index + count <= mSize - 1)
			{
				for (int i = index; i < mSize - count; i++)
					mItems[i] = mItems[i + count];
			}
			mSize -= (.)count;
#if VERSION_LIST
			mVersion++;
#endif
		}

		/// Will change the order of items in the list
		public void RemoveAtFast(int index)
		{
			Debug.Assert((uint32)index < (uint32)mSize);
			mSize--;
			if (mSize > 0)
		        mItems[index] = mItems[mSize];
#if VERSION_LIST
			mVersion++;
#endif
		}

		/// Will change the order of items in the list
		public void RemoveRangeFast(int index, int count)
		{
			Debug.Assert((uint)index + (uint)count <= (uint)mSize);
			if (index + count <= mSize - 1)
			{
				int moveCount = Math.Min(count, mSize - (index + count));
				for (int i < moveCount)
					mItems[index + i] = mItems[mSize - moveCount + i];
			}
			mSize -= (.)count;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void Sort(Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(mItems, null, mSize, comp);
			sorter.[Friend]Sort(0, mSize);
		}

		public void Sort(Comparison<T> comp, int index, int count)
		{
			Debug.Assert((uint)index + (uint)count <= (uint)mSize);
			var sorter = Sorter<T, void>(mItems, null, mSize, comp);
			sorter.[Friend]Sort(index, count);
		}

		public int RemoveAll(Predicate<T> match)
		{
			int_cosize freeIndex = 0;   // the first free slot in items array

			// Find the first item which needs to be removed.
			while (freeIndex < mSize && !match(mItems[freeIndex])) freeIndex++;
			if (freeIndex >= mSize) return 0;

			int_cosize current = freeIndex + 1;
			while (current < mSize)
			{
				// Find the first item which needs to be kept.
				while (current < mSize && match(mItems[current])) current++;

				if (current < mSize)
				{
					// copy item to the free slot.
					mItems[freeIndex++] = mItems[current++];
				}
			}

			int_cosize result = mSize - freeIndex;
			mSize = freeIndex;
#if VERSION_LIST
			mVersion++;
#endif
			return result;
		}

		[Checked]
		public T PopBack()
		{
			Runtime.Assert(mSize != 0);
			return mItems[--mSize];
		}

		[Unchecked]
		public T PopBack()
		{
			return mItems[--mSize];
		}

		[Checked]
		public T PopFront()
		{
			Runtime.Assert(mSize != 0);
			T backVal = mItems[0];
			RemoveAt(0);
			return backVal;
		}

		[Unchecked]
		public T PopFront()
		{
			T backVal = mItems[0];
			RemoveAt(0);
			return backVal;
		}

		public bool Remove(T item)
		{
			int index = IndexOf(item);
			if (index >= 0)
			{
				RemoveAt(index);
				return true;
			}

			return false;
		}

		public bool RemoveStrict(T item)
		{
			int index = IndexOfStrict(item);
			if (index >= 0)
			{
				RemoveAt(index);
				return true;
			}

			return false;
		}

		public bool RemoveAlt<TAlt>(TAlt item) where TAlt : IHashable where bool : operator T == TAlt
		{
			int index = IndexOfAlt(item);
			if (index >= 0)
			{
				RemoveAt(index);
				return true;
			}

			return false;
		}

		public Result<T> GetAndRemove(T item)
		{
			int index = IndexOf(item);
			if (index >= 0)
			{
				T val = mItems[index];
				RemoveAt(index);
				return val;
			}

			return .Err;
		}

		public Result<T> GetAndRemoveStrict(T item)
		{
			int index = IndexOfStrict(item);
			if (index >= 0)
			{
				T val = mItems[index];
				RemoveAt(index);
				return val;
			}

			return .Err;
		}

		public Result<T> GetAndRemoveAlt<TAlt>(TAlt item) where TAlt : IHashable where bool : operator T == TAlt
		{
			int index = IndexOfAlt(item);
			if (index >= 0)
			{
				T val = mItems[index];
				RemoveAt(index);
				return val;
			}

			return .Err;
		}

		/// The method returns the index of the given value in the list. If the
		/// list does not contain the given value, the method returns a negative
		/// integer. The bitwise complement operator (~) can be applied to a
		/// negative result to produce the index of the first element (if any) that
		/// is larger than the given search value. This is also the index at which
		/// the search value should be inserted into the list in order for the list
		/// to remain sorted.
		/// 
		/// The method uses the Array.BinarySearch method to perform the
		/// search.
		///
		/// @brief Searches a section of the list for a given element using a binary search algorithm.
		public int BinarySearch(T item, delegate int(T lhs, T rhs) comparer)
		{
			return Array.BinarySearch(mItems, Count, item, comparer);
		}

		public int BinarySearchAlt<TAlt>(TAlt item, delegate int(T lhs, TAlt rhs) comparer)
		{
			return Array.BinarySearchAlt(mItems, Count, item, comparer);
		}

		public int BinarySearch(int index, int count, T item, delegate int(T lhs, T rhs) comparer)
		{
			Debug.Assert((uint)index <= (uint)mSize);
			Debug.Assert(index + count <= mSize);
			return (int)Array.BinarySearch(mItems + index, count, item, comparer);
		}

		public static operator Span<T>(List<T> list)
		{
			return Span<T>(list.mItems, list.mSize);
		}

		protected override void GCMarkMembers()
		{
			if (mItems == null)
				return;
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
		    for (int i < mSize)
		    {
		        GC.Mark!(mItems[i]); 
			}
		}

		public struct Enumerator : IRefEnumerator<T*>, IEnumerator<T>, IResettable
		{
	        private List<T> mList;
	        private int mIndex;
#if VERSION_LIST
	        private int32 mVersion;
#endif
	        private T* mCurrent;

	        public this(List<T> list)
	        {
	            mList = list;
	            mIndex = 0;
#if VERSION_LIST
	            mVersion = list.mVersion;
#endif
	            mCurrent = null;
	        }

#if VERSION_LIST
			void CheckVersion()
			{
				if (mVersion != mList.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

	        public void Dispose()
	        {
	        }

	        public bool MoveNext() mut
	        {
	            List<T> localList = mList;
	            if ((uint(mIndex) < uint(localList.mSize)))
	            {
	                mCurrent = &localList.mItems[mIndex];
	                mIndex++;
	                return true;
	            }			   
	            return MoveNextRare();
	        }

	        private bool MoveNextRare() mut
	        {
#if VERSION_LIST
				CheckVersion();
#endif
	        	mIndex = mList.mSize + 1;
	            mCurrent = null;
	            return false;
	        }

	        public T Current
	        {
	            get
	            {
	                return *mCurrent;
	            }

				set
				{
					*mCurrent = value;
				}
	        }

			public ref T CurrentRef
			{
			    get
			    {
			        return ref *mCurrent;
			    }
			}

			public int Index
			{
				get
				{
					return mIndex - 1;
				}				
			}

			public int Count
			{
				get
				{
					return mList.Count;
				}				
			}

			public void Remove() mut
			{
				int curIdx = mIndex - 1;
				mList.RemoveAt(curIdx);
#if VERSION_LIST
				mVersion = mList.mVersion;
#endif
				mIndex = curIdx;
			}

			public void RemoveFast() mut
			{
				int curIdx = mIndex - 1;
				int lastIdx = mList.Count - 1;
				if (curIdx < lastIdx)
	                mList[curIdx] = mList[lastIdx];
				mList.RemoveAt(lastIdx);
#if VERSION_LIST
				mVersion = mList.mVersion;
#endif
				mIndex = curIdx;
			}
	        
	        public void Reset() mut
	        {
	            mIndex = 0;
	            mCurrent = null;
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

	extension List<T> where int : operator T <=> T
	{
		public int BinarySearch(T item)
		{
			return (int)Array.BinarySearch(mItems, Count, item);
		}

		public int BinarySearch(int index, int count, T item)
		{
			Debug.Assert((uint)index <= (uint)mSize);
			Debug.Assert(index + count <= mSize);
			return (int)Array.BinarySearch(mItems + index, count, item);
		}

		public void Sort()
		{
			Sort(scope (lhs, rhs) => lhs <=> rhs);
		}
	}

	extension List<T> where T : delete
	{
		public void ClearAndDeleteItems()
		{
			for (var item in this)
				delete item;
			Clear();
		}
	}

	extension List<T> where T : String
	{
		public bool Contains(T item, StringComparison comparison)
		{
			if (item == null)
			{
			    for (int i = 0; i < mSize; i++)
			        if (mItems[i] == null)
			    		return true;
			    return false;
			}
			else
			{
				for (int i = 0; i < mSize; i++)
					if (mItems[i].Equals(item, comparison))
						return true;
			    return false;
			}
		}

		public int IndexOf(T item, StringComparison comparison)
		{
			for (int i = 0; i < mSize; i++)
				if (mItems[i].Equals(item, comparison))
					return i;
			return -1;
		}

		public int IndexOf(T item, int index, StringComparison comparison)
		{
			for (int i = index; i < mSize; i++)
				if (mItems[i].Equals(item, comparison))
					return i;
			return -1;
		}

		public int IndexOf(T item, int index, int count, StringComparison comparison)
		{
			for (int i = index; i < index + count; i++)
				if (mItems[i].Equals(item, comparison))
					return i;
			return -1;
		}

		public int LastIndexOf(T item, StringComparison comparison)
		{
			for (int i = mSize - 1; i >= 0; i--)
				if (mItems[i].Equals(item, comparison))
					return i;
			return -1;
		}
	}

	class ListWithAlloc<T> : List<T>
	{
		IRawAllocator mAlloc;

		public this(IRawAllocator alloc)
		{
			mAlloc = alloc;
		}

		/*protected override void* Alloc(int size, int align)
		{
			 return mAlloc.Alloc(size, align);
		}

		protected override void Free(void* ptr)
		{
			 mAlloc.Free(ptr);
		}*/
	}
}

