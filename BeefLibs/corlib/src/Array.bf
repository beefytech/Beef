// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections;
using System.Diagnostics;

namespace System
{
#if BF_LARGE_COLLECTIONS
	typealias int_arsize = int64;
#else
	typealias int_arsize = int32;
#endif
	
	class Array
	{
		protected int_arsize mLength;

		public int Count
		{
			[Inline]
			set
			{
				// We only allow reducing the length - consider using System.Collections.List<T> when dynamic sizing is required
				Runtime.Assert(value <= mLength);
				mLength = (int_arsize)value;
			}

			[Inline]
			get
			{
				return mLength;
			}
		}

		public bool IsEmpty
		{
			[Inline]
			get
			{
				return mLength == 0;
			}
		}

		public static int BinarySearch<T>(T* arr, int length, T value) where int : operator T <=> T
		{
			int lo = 0;
            int hi = length - 1;
			
			while (lo <= hi)
            {
                int i = (lo + hi) / 2;
				T midVal = arr[i];
				int c = midVal <=> value;
                if (c == 0) return i;
                if (c < 0)
                    lo = i + 1;
                else
                    hi = i - 1;
			}
			return ~lo;
		}

		public static int BinarySearch<T>(T[] arr, T value) where int : operator T <=> T
		{
			return BinarySearch(&arr.[Friend]GetRef(0), arr.mLength, value);
		}

		public static int BinarySearch<T>(T[] arr, int idx, int length, T value) where int : operator T <=> T
		{
			Debug.Assert((uint)idx <= (uint)arr.mLength);
			Debug.Assert(idx + length <= arr.mLength);
			return BinarySearch(&arr.[Friend]GetRef(idx), length, value);
		}

		public static int BinarySearch<T>(T* arr, int length, T value, delegate int(T lhs, T rhs) comp)
		{
			int lo = 0;
            int hi = length - 1;
			
			while (lo <= hi)
            {
                int i = (lo + hi) / 2;
				T midVal = arr[i];
				int c = comp(midVal, value);
                if (c == 0) return i;
                if (c < 0)
                    lo = i + 1;                    
                else
                    hi = i - 1;
			}
			return ~lo;
		}

		public static int BinarySearchAlt<T, TAlt>(T* arr, int length, TAlt value, delegate int(T lhs, TAlt rhs) comp)
		{
			int lo = 0;
		    int hi = length - 1;
			
			while (lo <= hi)
		    {
		        int i = (lo + hi) / 2;
				T midVal = arr[i];
				int c = comp(midVal, value);
		        if (c == 0) return i;
		        if (c < 0)
		            lo = i + 1;                    
		        else
		            hi = i - 1;
			}
			return ~lo;
		}

		public static int BinarySearch<T>(T[] arr, T value, delegate int(T lhs, T rhs) comp)
		{
			return BinarySearch(&arr.[Friend]GetRef(0), arr.mLength, value, comp);
		}

		public static int BinarySearch<T>(T[] arr, int idx, int length, T value, delegate int(T lhs, T rhs) comp)
		{
			Debug.Assert((uint)idx <= (uint)arr.mLength);
			Debug.Assert(idx + length <= arr.mLength);
			return BinarySearch(&arr.[Friend]GetRef(idx), length, value, comp);
		}

		public static void Clear<T>(T[] arr, int idx, int length)
		{
			for (int i = idx; i < idx + length; i++)
				arr[i] = default(T);
		}

		public static void Clear<T>(T* arr, int length)
		{
			for (int i = 0; i < length; i++)
				arr[i] = default(T);
		}

		public static void Copy<T, T2>(T[] sourceArray, T2[] destinationArray, int length) where T : var where T2 : var
		{
			Debug.Assert(sourceArray != null);
			Debug.Assert(destinationArray != null);

		    Copy(sourceArray, 0, destinationArray, 0, length);
		}

		public static void Copy<T, T2>(T[] arrayFrom, int srcOffset, T2[] arrayTo, int dstOffset, int length) where T : var where T2 : var
		{
			if (((Object)arrayTo == (Object)arrayFrom) && (dstOffset > srcOffset))
			{
				for (int i = length - 1; i >= 0; i--)
					arrayTo.[Friend]GetRef(i + dstOffset) = (T2)arrayFrom.[Friend]GetRef(i + srcOffset);
				return;
			}

			for (int i = 0; i < length; i++)
				arrayTo.[Friend]GetRef(i + dstOffset) = (T2)arrayFrom.[Friend]GetRef(i + srcOffset);
		}

		public static void Sort<T>(T[] array, Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(&array.[Friend]mFirstElement, null, array.[Friend]mLength, comp);
			sorter.[Friend]Sort(0, array.[Friend]mLength);
		}

		public static void Sort<T, T2>(T[] keys, T2[] items, Comparison<T> comp)
		{
			var sorter = Sorter<T, T2>(&keys.[Friend]mFirstElement, &items.[Friend]mFirstElement, keys.[Friend]mLength, comp);
			sorter.[Friend]Sort(0, keys.[Friend]mLength);
		}

		public static void Sort<T>(T[] array, int index, int count, Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(&array.[Friend]mFirstElement, null, array.[Friend]mLength, comp);
			sorter.[Friend]Sort(index, count);
		}

		// Reverses all elements of the given array. Following a call to this
		// method, an element previously located at index i will now be
		// located at index length - i - 1, where length is the
		// length of the array.
		public static void Reverse<T>(T[] arr)
		{
			Debug.Assert(arr != null);
							   
			Reverse(arr, 0, arr.Count);
		}

		// Reverses the elements in a range of an array. Following a call to this
		// method, an element in the range given by index and count
		// which was previously located at index i will now be located at
		// index index + (index + count - i - 1).
		// Reliability note: This may fail because it may have to box objects.
		public static void Reverse<T>(T[] arr, int index, int length)
		{
			Debug.Assert(arr != null);
			Debug.Assert(index >= 0);
			Debug.Assert(length >= 0);
			Debug.Assert(length >= arr.Count - index);
			
			int i = index;
			int j = index + length - 1;
			while (i < j)
			{
				let temp = arr[i];
				arr[i] = arr[j];
				arr[j] = temp;
				i++;
				j--;
			}
		}
	}

	[Ordered]
	class Array1<T> : Array, IEnumerable<T>
	{
		T mFirstElement;

		public T* Ptr
		{
			[Inline]
			get
			{
				return &mFirstElement;
			}
		}

		public this()
		{
		} 

		[Inline]
		ref T GetRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		ref T GetRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		public ref T this[int idx]
		{
			[Checked, Inline]
			get
			{
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}

			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[Index index]
		{
			[Checked, Inline]
			get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}

			[Unchecked, Inline]
			get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				return ref (&mFirstElement)[idx];
			}
		}

		public Span<T> this[IndexRange range]
		{
#if !DEBUG
			[Inline]
#endif
			get
			{
				return Span<T>(&mFirstElement, mLength)[range];
			}
		}

		[Inline]
		public T* CArray()
		{
			return &mFirstElement;
		}

		public void CopyTo(T[] arrayTo)
		{
			Debug.Assert(arrayTo.mLength >= mLength);
			Internal.MemCpy(&arrayTo.GetRef(0), &GetRef(0), strideof(T) * mLength, alignof(T));
		}

		public void CopyTo(T[] arrayTo, int srcOffset, int dstOffset, int length)
		{
			Debug.Assert(length >= 0);
			Debug.Assert((uint)srcOffset + (uint)length <= (uint)mLength);
			Debug.Assert((uint)dstOffset + (uint)length <= (uint)arrayTo.mLength);
			Internal.MemMove(&arrayTo.GetRef(dstOffset), &GetRef(srcOffset), strideof(T) * length, alignof(T));
		}

		public void CopyTo<T2>(T2[] arrayTo, int srcOffset, int dstOffset, int length) where T2 : operator explicit T
		{
			Debug.Assert(length >= 0);
			Debug.Assert((uint)srcOffset + (uint)length <= (uint)mLength);
			Debug.Assert((uint)dstOffset + (uint)length <= (uint)arrayTo.[Friend]mLength);

			if (((Object)arrayTo == (Object)this) && (dstOffset > srcOffset))
			{
				for (int i = length - 1; i >= 0; i--)
					arrayTo.GetRef(i + dstOffset) = (T2)GetRef(i + srcOffset);
				return;
			}

			for (int i = 0; i < length; i++)
				arrayTo.GetRef(i + dstOffset) = (T2)GetRef(i + srcOffset);
		}

		public void CopyTo(Span<T> destination)
		{
			Debug.Assert(destination.[Friend]mLength >= mLength);
			Internal.MemMove(destination.Ptr, &GetRef(0), strideof(T) * mLength, alignof(T));
		}

		public void CopyTo(Span<T> destination, int srcOffset)
		{
			Debug.Assert((uint)srcOffset + (uint)destination.[Friend]mLength <= (uint)mLength);
			Internal.MemMove(destination.Ptr, &GetRef(srcOffset), strideof(T) * (destination.[Friend]mLength - srcOffset), alignof(T));
		}

		public void CopyTo<T2>(Span<T2> destination, int srcOffset) where T2 : operator explicit T
		{
			//TODO: Handle src/dest overlap (MemMove)
			Debug.Assert((uint)srcOffset + (uint)destination.[Friend]mLength <= (uint)mLength);
			var ptr = destination.[Friend]mPtr;
			for (int i = 0; i < destination.[Friend]mLength; i++)
				ptr[i] = (T2)GetRef(i + srcOffset);
		}

		public Span<T>.Enumerator GetEnumerator()
		{
			return .(this);
		}

		public void SetAll(T value)
		{
			for (int i < mLength)
				(&mFirstElement)[i] = value;
		}

		public bool Contains(T value)
		{
			for (int i = 0; i < mLength; i++)
				if ((&mFirstElement)[i] == value)
					return true;
			return false;
		}

		public bool ContainsStrict(T value)
		{
			for (int i = 0; i < mLength; i++)
				if ((&mFirstElement)[i] === value)
					return true;
			return false;
		}

		public int IndexOf(T value)
		{
			for (int i = 0; i < mLength; i++)
				if ((&mFirstElement)[i] == value)
					return i;
			return -1;
		}

		public int IndexOfStrict(T value)
		{
			for (int i = 0; i < mLength; i++)
				if ((&mFirstElement)[i] === value)
					return i;
			return -1;
		}

        protected override void GCMarkMembers()
        {
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
            for (int i = 0; i < mLength; i++)
            {
				GC.Mark!((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array2<T> : Array, IEnumerable<T>
	{
		int_arsize mLength1;
		T mFirstElement;

		public T* Ptr
		{
			[Inline]
			get
			{
				return &mFirstElement;
			}
		}

		Array GetSelf()
		{
			return this;
		}

		public this()
		{
		} 

		public int GetLength(int dim)
		{
			if (dim == 0)
				return mLength / mLength1;
			else if (dim == 1)
				return mLength1;
			Runtime.FatalError();
		}

		[Inline]
		public ref T GetRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRef(int idx0, int idx1)
		{
			return ref (&mFirstElement)[idx0*mLength1 + idx1];
		}

		[Inline]
		public ref T GetRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRefChecked(int idx0, int idx1)
		{
			int idx = idx0*mLength1 + idx1;
			if (((uint)idx >= (uint)mLength) ||
				((uint)idx1 >= (uint)mLength1))
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		public ref T this[int idx]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[idx0*mLength1 + idx1];
			}
		}

		public ref T this[int idx]
		{
			[Checked]
			get
			{
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1]
		{
			[Checked]
			get
			{
				int idx = idx0*mLength1 + idx1;
				if (((uint)idx >= (uint)mLength) ||
					((uint)idx1 >= (uint)mLength1))
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		[Inline]
		public T* CArray()
		{
			return &mFirstElement;
		}	

		public Span<T>.Enumerator GetEnumerator()
		{
			return .(.(&mFirstElement, mLength));
		}

        protected override void GCMarkMembers()
        {
            for (int i = 0; i < mLength; i++)
            {
                GC.Mark!((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array3<T> : Array, IEnumerable<T>
	{
		int_arsize mLength1;
		int_arsize mLength2;
		T mFirstElement;

		public T* Ptr
		{
			[Inline]
			get
			{
				return &mFirstElement;
			}
		}

		Array GetSelf()
		{
			return this;
		}

		public this()
		{
		} 
		
		public int GetLength(int dim)
		{
			if (dim == 0)
				return mLength / (mLength1*mLength2);
			else if (dim == 1)
				return mLength1;
			else if (dim == 2)
				return mLength2;
			Runtime.FatalError();
		}

		[Inline]
		public ref T GetRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRef(int idx0, int idx1, int idx2)
		{
			return ref (&mFirstElement)[(idx0*mLength1 + idx1)*mLength2 + idx2];
		}

		[Inline]
		public ref T GetRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRefChecked(int idx0, int idx1, int idx2)
		{
			int idx = (idx0*mLength1 + idx1)*mLength2 + idx2;
			if (((uint)idx >= (uint)mLength) ||
				((uint)idx1 >= (uint)mLength1) ||
				((uint)idx2 >= (uint)mLength2))
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		public ref T this[int idx]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1, int idx2]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[(idx0*mLength1 + idx1)*mLength2 + idx2];
			}
		}

		public ref T this[int idx]
		{
			[Checked]
			get
			{
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1, int idx2]
		{
			[Checked]
			get
			{
				int idx = (idx0*mLength1 + idx1)*mLength2 + idx2;
				if (((uint)idx >= (uint)mLength) ||
					((uint)idx1 >= (uint)mLength1) ||
					((uint)idx2 >= (uint)mLength2))
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		[Inline]
		public T* CArray()
		{
			return &mFirstElement;
		}	

		public Span<T>.Enumerator GetEnumerator()
		{
			return .(.(&mFirstElement, mLength));
		}

	    protected override void GCMarkMembers()
	    {
	        for (int i = 0; i < mLength; i++)
	        {
	            GC.Mark!((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array4<T> : Array, IEnumerable<T>
	{
		int_arsize mLength1;
		int_arsize mLength2;
		int_arsize mLength3;
		T mFirstElement;

		public T* Ptr
		{
			[Inline]
			get
			{
				return &mFirstElement;
			}
		}

		Array GetSelf()
		{
			return this;
		}

		public this()
		{
		} 
		
		public int GetLength(int dim)
		{
			if (dim == 0)
				return mLength / (mLength1*mLength2*mLength3);
			else if (dim == 1)
				return mLength1;
			else if (dim == 2)
				return mLength2;
			else if (dim == 3)
				return mLength3;
			Runtime.FatalError();
		}

		[Inline]
		public ref T GetRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRef(int idx0, int idx1, int idx2, int idx3)
		{
			return ref (&mFirstElement)[((idx0*mLength1 + idx1)*mLength2 + idx2)*mLength3 + idx3];
		}

		[Inline]
		public ref T GetRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T GetRefChecked(int idx0, int idx1, int idx2, int idx3)
		{
			int idx = ((idx0*mLength1 + idx1)*mLength2 + idx2)*mLength3 + idx3;
			if (((uint)idx >= (uint)mLength) ||
				((uint)idx1 >= (uint)mLength1) ||
				((uint)idx2 >= (uint)mLength2) ||
				((uint)idx3 >= (uint)mLength3))
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		public ref T this[int idx]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1, int idx2, int idx3]
		{
			[Unchecked, Inline]
			get
			{
				return ref (&mFirstElement)[((idx0*mLength1 + idx1)*mLength2 + idx2)*mLength3 + idx3];
			}
		}

		public ref T this[int idx]
		{
			[Checked]
			get
			{
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		public ref T this[int idx0, int idx1, int idx2, int idx3]
		{
			[Checked]
			get
			{
				int idx = ((idx0*mLength1 + idx1)*mLength2 + idx2)*mLength3 + idx3;
				if (((uint)idx >= (uint)mLength) ||
					((uint)idx1 >= (uint)mLength1) ||
					((uint)idx2 >= (uint)mLength2) ||
					((uint)idx3 >= (uint)mLength3))
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
			}
		}

		[Inline]
		public T* CArray()
		{
			return &mFirstElement;
		}	

		public Span<T>.Enumerator GetEnumerator()
		{
			return .(.(&mFirstElement, mLength));
		}

	    protected override void GCMarkMembers()
	    {
	        for (int i = 0; i < mLength; i++)
	        {
	            GC.Mark!((&mFirstElement)[i]); 
			}
		}
	}
}
