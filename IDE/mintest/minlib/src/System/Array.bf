// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections.Generic;
using System.Diagnostics;

namespace System
{
	typealias int_arsize = int;

	class Array
	{
		protected int_arsize mLength;

		public int Count
		{
			[Inline]
			set
			{
				// We only allow reducing the length - consider using System.Collections.Generic.List<T> when dynamic sizing is required
				Runtime.Assert(value <= mLength);
				mLength = value;
			}

			[Inline]
			get
			{
				return mLength;
			}
		}

		public bool IsEmpty
		{							
			get
			{
				return mLength == 0;
			}
		}

		public static int BinarySearch<T>(T* arr, int idx, int length, T value) where T : IComparable<T>
		{
			int lo = idx;
            int hi = idx + length - 1;
			
			while (lo <= hi)
            {
                int i = (lo + hi) / 2;
				T midVal = arr[i];
				int c = midVal.CompareTo(value);
                if (c == 0) return i;
                if (c < 0)
                    lo = i + 1;                    
                else
                    hi = i - 1;
			}
			return ~lo;
		}

		public static int BinarySearch<T>(T* arr, int idx, int length, T value, IComparer<T> comp)
		{
			int lo = idx;
            int hi = idx + length - 1;
			
			while (lo <= hi)
            {
                int i = (lo + hi) / 2;
				T midVal = arr[i];
				int c = comp.Compare(midVal, value);
                if (c == 0) return i;
                if (c < 0)
                    lo = i + 1;                    
                else
                    hi = i - 1;
			}
			return ~lo;
		}

		public static void Clear<T>(T[] arr, int idx, int length)
		{
			for (int i = idx; i < idx + length; i++)
				arr[i] = default(T);
		}

		public static void Copy<T, T2>(T[] arrayFrom, int srcOffset, T2[] arrayTo, int dstOffset, int length) where T : var where T2 : var
		{
			if (((Object)arrayTo == (Object)arrayFrom) && (dstOffset > srcOffset))
			{
				for (int i = length - 1; i >= 0; i--)
					arrayTo.getRef(i + dstOffset) = (T2)arrayFrom.getRef(i + srcOffset);
				return;
			}

			for (int i = 0; i < length; i++)
				arrayTo.getRef(i + dstOffset) = (T2)arrayFrom.getRef(i + srcOffset);
		}

		public static void Sort<T>(T[] array, Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(&array.[Friend]mFirstElement, null, array.[Friend]mLength, comp);
			sorter.Sort(0, array.[Friend]mLength);
		}

		public static void Sort<T, T2>(T[] keys, T2[] items, Comparison<T> comp)
		{
			var sorter = Sorter<T, T2>(&keys.[Friend]mFirstElement, &items.[Friend]mFirstElement, keys.[Friend]mLength, comp);
			sorter.Sort(0, keys.[Friend]mLength);
		}

		public static void Sort<T>(T[] array, int index, int count, Comparison<T> comp)
		{
			var sorter = Sorter<T, void>(&array.[Friend]mFirstElement, null, array.[Friend]mLength, comp);
			sorter.Sort(index, count);
		}
	}
	
	class Array1<T> : Array
	{
		T mFirstElement;

		public this()
		{
		} 
		
		[Inline]
		public ref T getRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
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

		public ref T this[int idx]
		{
			[Checked, Inline]
			get
			{
				if ((uint)idx >= (uint)mLength)
					Internal.ThrowIndexOutOfRange(1);
				return ref (&mFirstElement)[idx];
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
			Internal.MemCpy(&arrayTo.getRef(0), &getRef(0), strideof(T) * mLength, alignof(T));
		}

		public void CopyTo(T[] arrayTo, int srcOffset, int dstOffset, int length)
		{
			Debug.Assert(length >= 0);
			Debug.Assert((uint)srcOffset + (uint)length <= (uint)mLength);
			Debug.Assert((uint)dstOffset + (uint)length <= (uint)arrayTo.mLength);
			Internal.MemCpy(&arrayTo.getRef(dstOffset), &getRef(srcOffset), strideof(T) * length, alignof(T));
		}

		public void CopyTo<T2>(T2[] arrayTo, int srcOffset, int dstOffset, int length) where T2 : var
		{
			Debug.Assert(length >= 0);
			Debug.Assert((uint)srcOffset + (uint)length <= (uint)mLength);
			Debug.Assert((uint)dstOffset + (uint)length <= (uint)arrayTo.[Friend]mLength);

			if (((Object)arrayTo == (Object)this) && (dstOffset > srcOffset))
			{
				for (int i = length - 1; i >= 0; i--)
					arrayTo.getRef(i + dstOffset) = (T2)getRef(i + srcOffset);
				return;
			}

			for (int i = 0; i < length; i++)
				arrayTo.getRef(i + dstOffset) = (T2)getRef(i + srcOffset);
		}

        protected override void GCMarkMembers()
        {
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
            for (int i = 0; i < mLength; i++)
            {
				GC.Mark((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array2<T> : Array
	{
		int_arsize mLength1;
		T mFirstElement;

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
		public ref T getRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRef(int idx0, int idx1)
		{
			return ref (&mFirstElement)[idx0*mLength1 + idx1];
		}

		[Inline]
		public ref T getRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRefChecked(int idx0, int idx1)
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

        protected override void GCMarkMembers()
        {
            for (int i = 0; i < mLength; i++)
            {
                GC.Mark_Unbound((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array3<T> : Array
	{
		int_arsize mLength1;
		int_arsize mLength2;
		T mFirstElement;

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
		public ref T getRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRef(int idx0, int idx1, int idx2)
		{
			return ref (&mFirstElement)[(idx0*mLength1 + idx1)*mLength2 + idx2];
		}

		[Inline]
		public ref T getRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRefChecked(int idx0, int idx1, int idx2)
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

	    protected override void GCMarkMembers()
	    {
	        for (int i = 0; i < mLength; i++)
	        {
	            GC.Mark_Unbound((&mFirstElement)[i]); 
			}
		}
	}

	[Ordered]
	class Array4<T> : Array
	{
		int_arsize mLength1;
		int_arsize mLength2;
		int_arsize mLength3;
		T mFirstElement;

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
		public ref T getRef(int idx)
		{
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRef(int idx0, int idx1, int idx2, int idx3)
		{
			return ref (&mFirstElement)[((idx0*mLength1 + idx1)*mLength2 + idx2)*mLength3 + idx3];
		}

		[Inline]
		public ref T getRefChecked(int idx)
		{
			if ((uint)idx >= (uint)mLength)
				Internal.ThrowIndexOutOfRange(1);
			return ref (&mFirstElement)[idx];
		}

		[Inline]
		public ref T getRefChecked(int idx0, int idx1, int idx2, int idx3)
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

	    protected override void GCMarkMembers()
	    {
	        for (int i = 0; i < mLength; i++)
	        {
	            GC.Mark_Unbound((&mFirstElement)[i]); 
			}
		}
	}
}
