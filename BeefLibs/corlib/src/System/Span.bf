using System.Diagnostics;
using System.Collections.Generic;

namespace System
{
	struct Span<T>
	{
		protected T* mPtr;
		protected int mLength;

		public this()
		{
			mPtr = null;
			mLength = 0;
		}

		public this(T[] array)
		{
			mPtr = &array.getRef(0);
			mLength = array.[Friend]mLength;
		}

		public this(T[] array, int index)
		{
			mPtr = &array[index];
			mLength = array.[Friend]mLength - index;
		}

		public this(T[] array, int index, int length)
		{
			if (length == 0)
				mPtr = null;
			else
				mPtr = &array[index];
			mLength = length;
		}

		public this(T* memory, int length)
		{
			mPtr = memory;
			mLength = length;
		}

		/*public static implicit operator Span<T> (ArraySegment<T> arraySegment)
		{

		}*/

		public static implicit operator Span<T> (T[] array)
		{
			return Span<T>(array);
		}

		[Inline]
		public int Length
	    {
	        get
			{
				return mLength;
			}

			set mut
			{
				mLength = value;
			}
	    }

		[Inline]
		public T* Ptr
		{
			get
			{
				return mPtr;
			}

			set	mut
			{
				mPtr = value;
			}
		}

		[Inline]
		public T* EndPtr
		{
			get
			{
				return mPtr + mLength;
			}
		}

		[Inline]
		public bool IsEmpty
		{
			get
			{
				return mLength == 0;
			}
		}

		[Inline]
		public bool IsNull
		{
			get
			{
				return mPtr == null;
			}
		}

		[Inline]
		public ref T this[int index]
	    {
	        get
			{
				return ref mPtr[index];
			}
	    }

		public Span<T> Slice(int index)
		{
			Debug.Assert((uint)index <= (uint)mLength);
			Span<T> span;
			span.mPtr = mPtr + index;
			span.mLength = mLength - index;
			return span;
		}

		public Span<T> Slice(int index, int length)
		{
			Debug.Assert((uint)index + (uint)length <= (uint)mLength);
			Span<T> span;
			span.mPtr = mPtr + index;
			span.mLength = length;
			return span;
		}

		public void Adjust(int ofs) mut
		{
			Debug.Assert((uint)ofs <= (uint)mLength);
			mPtr += ofs;
			mLength -= ofs;
		}

		public void Clear() mut
		{
			mLength = 0;
		}

		public void RemoveFromStart(int length) mut
		{
			Debug.Assert((uint)length <= (uint)mLength);
			mPtr += length;
			mLength -= length;
		}

		public void RemoveToEnd(int startIdx) mut
		{
			RemoveFromEnd(mLength - startIdx);
		}

		public void RemoveFromEnd(int length) mut
		{
			mLength -= length;
		}

		public void CopyTo(T[] destination)
		{
			Internal.MemMove(&destination[0], mPtr, Internal.GetArraySize<T>(mLength), (int32)alignof(T));
		}

		public void CopyTo(Span<T> destination)
		{
			Internal.MemMove(destination.mPtr, mPtr, Internal.GetArraySize<T>(mLength), (int32)alignof(T));
		}

		public Span<uint8> ToRawData()
		{
			return Span<uint8>((uint8*)mPtr, mLength * sizeof(T));
		}

		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public struct Enumerator : IEnumerator<T>, IRefEnumerator<T>
		{
		    private Span<T> mList;
		    private int mIndex;
		    private T* mCurrent;

		    internal this(Span<T> list)
		    {
		        mList = list;
		        mIndex = 0;
		        mCurrent = null;
		    }

		    public void Dispose()
		    {
		    }

		    public bool MoveNext() mut
		    {
		        if ((uint(mIndex) < uint(mList.mLength)))
		        {
		            mCurrent = &mList.mPtr[mIndex];
		            mIndex++;
		            return true;
		        }			   
		        return MoveNextRare();
		    }

		    private bool MoveNextRare() mut
		    {
		    	mIndex = mList.mLength + 1;
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

			public int Length
			{
				get
				{
					return mList.mLength;
				}				
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

	//TODO: Make a ReadOnlySpan
}
