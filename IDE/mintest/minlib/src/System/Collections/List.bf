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

	public class List<T> : IEnumerable<T>, IList
	{
		private const int_cosize cDefaultCapacity = 4;

		const int_cosize SizeFlags = 0x7FFFFFFF;
		const int_cosize DynAllocFlag = (int_cosize)0x80000000;

#if BF_ENABLE_REALTIME_LEAK_CHECK
		static DbgRawAllocData sRawAllocData;
		public static this()
		{
			sRawAllocData.mMarkFunc = null;
			sRawAllocData.mMaxStackTrace = 1;
			sRawAllocData.mType = typeof(T);
		}
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
			if (((int)Internal.UnsafeCastToPtr(this) & 0xFFFF) == 0x4BA0)
			{
				NOP!();
			}
		}

		public this(IEnumerator<T> enumerator)
		{
			for (var item in enumerator)
				Add(item);
		}

		[AllowAppend]
		public this(int capacity)
		{
			Debug.Assert((uint)capacity <= (uint)SizeFlags);
			T* items = append T[capacity]*;
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
				Free(mItems);
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
							Internal.MemCpy(newItems, mItems, mSize * strideof(T), alignof(T));
						if (IsDynAlloc)
							Free(mItems);
						mItems = newItems;
						mAllocSizeAndFlags = (.)(value | DynAllocFlag);
					}
					else
					{
						if (IsDynAlloc)
							Free(mItems);
						mItems = null;
						mAllocSizeAndFlags = 0;
					}
				}
			}
		}

		public int Count
		{
			get
			{
				return mSize;
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
				Debug.Assert((uint)index < (uint)mSize);
				return ref mItems[index];
			}

			[Checked]
			set
			{
				Debug.Assert((uint)index < (uint)mSize);
				mItems[index] = value;
#if VERSION_LIST
				mVersion++;
#endif
			}
		}

		public ref T this[int index]
		{
			[Unchecked, Inline]
			get
			{
				return ref mItems[index];
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

		public ref T Back
		{
			get
			{
				Debug.Assert(mSize != 0);
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

		protected T* Alloc(int size)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			// We don't want to use the default mark function because it will mark the entire array,
			//  whereas we have a custom marking routine because we only want to mark up to mSize
			return (T*)Internal.Dbg_RawAlloc(size * strideof(T), &sRawAllocData);
#else
			return new T[size]*(?);
#endif
		}

		protected void Free(T* val)
		{
			delete (void*)val;
		}
		/*protected T[] Alloc(int size)
		{
			return new:this T[size];
		}

		protected void Free(Object obj)
		{
			delete:this obj;
		}

		protected virtual Object AllocObject(TypeInstance type, int size)
		{
			return Internal.ObjectAlloc(type, size);
		}

		protected virtual void FreeObject(Object obj)
		{
			delete obj;
		}*/

		/// Adds an item to the back of the list.
		public void Add(T item)
		{
			if (((int)Internal.UnsafeCastToPtr(this) & 0xFFFF) == 0x4BA0)
			{
				NOP!();
			}

			if (mSize == AllocSize) EnsureCapacity(mSize + 1);
			mItems[mSize++] = item;
#if VERSION_LIST
			mVersion++;
#endif
		}

		/// Returns a pointer to the start of the added uninitialized section
		public T* GrowUnitialized(int addSize)
		{
			if (mSize + addSize > AllocSize) EnsureCapacity(mSize + addSize);
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
			}
#if VERSION_LIST
			mVersion++;
#endif
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

		public void CopyTo(T[] array)
		{
			CopyTo(array, 0);
		}

		public void CopyTo(List<T> destList)
		{
			destList.EnsureCapacity(mSize);
			destList.mSize = mSize;
			if (mSize > 0)
				Internal.MemCpy(destList.mItems, mItems, mSize * strideof(T), alignof(T));
		}

		public void CopyTo(T[] array, int arrayIndex)
		{
			// Delegate rest of error checking to Array.Copy.
			for (int_cosize i = 0; i < mSize; i++)
				array[i + arrayIndex] = mItems[i];
		}

		public void EnsureCapacity(int min)
		{
			int allocSize = AllocSize;
			if (allocSize < min)
			{
				int_cosize newCapacity = (int_cosize)(allocSize == 0 ? cDefaultCapacity : allocSize * 2);
				// Allow the list to grow to maximum possible capacity (~2G elements) before encountering overflow.
				// Note that this check works even when mItems.Length overflowed thanks to the (uint) cast
				//if ((uint)newCapacity > Array.MaxArrayLength) newCapacity = Array.MaxArrayLength;
				if (newCapacity < min) newCapacity = (int_cosize)min;
				Capacity = newCapacity;
			}
		}

		public void Reserve(int size)
		{
			EnsureCapacity(size);
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public int_cosize IndexOf(T item)
		{
			//return Array.IndexOf(mItems, item, 0, mSize);
			for (int i = 0; i < mSize; i++)
				if (mItems[i] == item)
					return (int_cosize)i;
			return -1;
		}

		public int_cosize IndexOf(T item, int index)
		{
			for (int i = index; i < mSize; i++)
				if (mItems[i] == item)
					return (int_cosize)i;
			return -1;
		}

		public int_cosize IndexOf(T item, int index, int count)
		{
			for (int i = index; i < index + count; i++)
				if (mItems[i] == item)
					return (int_cosize)i;
			return -1;
		}

		public void Insert(int index, T item)
		{
			if (mSize == AllocSize) EnsureCapacity(mSize + 1);
			if (index < mSize)
			{
				Internal.MemCpy(mItems + index + 1, mItems + index, (mSize - index) * strideof(T), alignof(T));
			}
			mItems[index] = item;
			mSize++;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void Insert(int index, Span<T> items)
		{
			if (items.Length == 0)
				return;
		}

		public void RemoveAt(int index)
		{
			if (((int)Internal.UnsafeCastToPtr(this) & 0xFFFF) == 0x4BA0)
			{
				NOP!();
			}

			Debug.Assert((uint)index < (uint)mSize);
			if (index < mSize - 1)
			{
				Internal.MemCpy(mItems + index, mItems + index + 1, (mSize - index - 1) * strideof(T), alignof(T));
			}
			mSize--;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void RemoveRange(int index, int count)
		{
			Debug.Assert((uint)index + (uint)count <= (uint)mSize);
			if (index + count < mSize - 1)
			{
				for (int i = index; i < mSize - count; i++)
					mItems[i] = mItems[i + count];
			}
			mSize -= (.)count;
#if VERSION_LIST
			mVersion++;
#endif
		}

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

		public void Sort(Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(mItems, null, mSize, comp);
			sorter.Sort(0, mSize);
		}

		public T PopBack()
		{
			T backVal = mItems[mSize - 1];
			mSize--;
			return backVal;
		}

		public T PopFront()
		{
			T backVal = mItems[0];
			RemoveAt(0);
			return backVal;
		}

		public bool Remove(T item)
		{
			int_cosize index = IndexOf(item);
			if (index >= 0)
			{
				RemoveAt(index);
				return true;
			}

			return false;
		}

		public static operator Span<T>(List<T> list)
		{
			return Span<T>(list.mItems, list.mSize);
		}

		protected override void GCMarkMembers()
		{
		    for (int i < mSize)
		    {
		        GC.Mark_Unbound(mItems[i]); 
			}
		}

		public struct Enumerator : IRefEnumerator<T>
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
}
