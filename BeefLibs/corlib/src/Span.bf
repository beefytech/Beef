using System.Diagnostics;
using System.Collections;

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
			mPtr = &array.[Friend]GetRef(0);
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
		
		public int Length
	    {
	    	[Inline]
	        get
			{
				return mLength;
			}

			[Inline]
			set mut
			{
				mLength = value;
			}
	    }
		
		public T* Ptr
		{
			[Inline]
			get
			{
				return mPtr;
			}

			[Inline]
			set	mut
			{
				mPtr = value;
			}
		}
		
		public T* EndPtr
		{
			[Inline]
			get
			{
				return mPtr + mLength;
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
		
		public bool IsNull
		{
			[Inline]
			get
			{
				return mPtr == null;
			}
		}
		
		public ref T this[int index]
	    {
	    	[Inline]
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

		    public this(Span<T> list)
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

#if BF_RUNTIME_CHECKS
#define BF_OPTSPAN_LENGTH
#endif

	struct OptSpan<T>
	{
		protected T* mPtr;
#if BF_OPTSPAN_LENGTH
		protected int mLength;
#endif

		public this()
		{
			mPtr = null;
#if BF_OPTSPAN_LENGTH
			mLength = 0;
#endif
		}

		public this(T[] array)
		{
			mPtr = &array.[Friend]GetRef(0);
#if BF_OPTSPAN_LENGTH
			mLength = array.[Friend]mLength;
#endif
		}

		public this(T[] array, int index)
		{
			mPtr = &array[index];
#if BF_OPTSPAN_LENGTH
			mLength = array.[Friend]mLength - index;
#endif
		}

		public this(T[] array, int index, int length)
		{
			if (length == 0)
				mPtr = null;
			else
				mPtr = &array[index];
#if BF_OPTSPAN_LENGTH
			mLength = length;
#endif
		}

		public this(T* memory, int length)
		{
			mPtr = memory;
#if BF_OPTSPAN_LENGTH
			mLength = length;
#endif
		}

		public static implicit operator OptSpan<T> (T[] array)
		{
			return OptSpan<T>(array);
		}

		
		public T* Ptr
		{
			[Inline]
			get
			{
				return mPtr;
			}
		}

		public ref T this[int index]
	    {
			[Inline, Checked]
	        get
			{
#if BF_OPTSPAN_LENGTH
				Debug.Assert((uint)index < (uint)mLength);
#endif
				return ref mPtr[index];
			}

			[Inline, Unchecked]
			get
			{
				return ref mPtr[index];
			}
	    }

		public OptSpan<T> Slice(int index, int length)
		{
			OptSpan<T> span;
			span.mPtr = mPtr + index;
#if BF_OPTSPAN_LENGTH
			Debug.Assert((uint)index + (uint)length <= (uint)mLength);
			span.mLength = length;
#else
			Debug.Assert(index >= 0);
#endif
			return span;
		}

		public void Adjust(int ofs) mut
		{
			mPtr += ofs;
#if BF_OPTSPAN_LENGTH
			Debug.Assert((uint)ofs <= (uint)mLength);
			mLength -= ofs;
#endif
		}

		public OptSpan<uint8> ToRawData()
		{
#if BF_OPTSPAN_LENGTH
			return OptSpan<uint8>((uint8*)mPtr, mLength * alignof(T));
#else
			return OptSpan<uint8>((uint8*)mPtr, 0);
#endif			
		}
	}

	//TODO: Make a ReadOnlySpan
}
