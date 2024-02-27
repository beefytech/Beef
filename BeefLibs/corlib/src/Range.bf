using System.Collections;
using System.Diagnostics;

namespace System
{
	interface RangeExpression
	{

	}

	enum Index
	{
		case FromFront(int offset);
		case FromEnd(int offset);

		public override void ToString(String strBuffer)
		{
			switch (this)
			{
			case .FromFront(let offset):
				offset.ToString(strBuffer);
			case .FromEnd(let offset):
				strBuffer.Append('^');
				offset.ToString(strBuffer);
			}
		}

		[Inline]
		public int Get(int size)
		{
			switch (this)
			{
			case .FromFront(let offset): return offset;
			case .FromEnd(let offset): return size - offset;
			}
		}
	}

	struct Range : RangeExpression, IEnumerable<int>
	{
		protected int mStart;
		protected int mEnd;

		public this()
		{
			mStart = 0;
			mEnd = 0;
		}

		[Inline]
		public this(int start, int end)
		{
			Debug.Assert(end >= start);
			mStart = start;
			mEnd = end;
		}

		public int Length
	    {
	    	[Inline]
	        get
			{
				return mEnd - mStart;
			}

			[Inline]
			set mut
			{
				mEnd = mStart + value;
			}
	    }
		
		public int Start
		{
			[Inline]
			get
			{
				return mStart;
			}

			[Inline]
			set	mut
			{
				mStart = value;
			}
		}
		
		public int End
		{
			[Inline]
			get
			{
				return mEnd;
			}

			set mut
			{
				mEnd = value;
			}
		}
		
		public bool IsEmpty
		{
			[Inline]
			get
			{
				return mEnd == mStart;
			}
		}

		public ReverseEnumerator Reversed
		{
			[Inline]
			get
			{
				return ReverseEnumerator(mEnd - 1, mStart);
			}
		}
		
		public bool Contains(int idx)
		{
			return (idx >= mStart) && (idx < mEnd);
		}

		public bool Contains(Range val)
		{
			return (val.[Friend]mStart >= mStart) && (val.[Friend]mEnd <= mEnd);
		}

		public bool Contains(ClosedRange val)
		{
			return (val.[Friend]mStart >= mStart) && (val.[Friend]mEnd <= mEnd - 1);
		}

		public void Clear() mut
		{
			mStart = 0;
			mEnd = 0;
		}

		public static operator IndexRange(Range list)
		{
			return .(.FromFront(list.mStart), .FromFront(list.mEnd), false);
		}

		[Inline]
		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"{mStart}..<{mEnd}");
		}

		public struct Enumerator : IEnumerator<int>
		{
		    private int mEnd;
		    private int mIndex;

			[Inline]
		    public this(Range range)
		    {
		        mIndex = range.mStart - 1;
		        mEnd = range.mEnd;
		    }

		    public void Dispose()
		    {
		    }

		    public ref int Index
		    {
		        get mut
		        {
		            return ref mIndex;
		        }
		    }

			public int End => mEnd;

			[Inline]
			public Result<int> GetNext() mut
			{
				if (mIndex + 1 >= mEnd)
					return .Err;
				return ++mIndex;
			}
		}

		public struct ReverseEnumerator : IEnumerator<int>
		{
		    private int mEnd;
		    private int mIndex;

			[Inline]
		    public this(int start, int end)
		    {
		        mIndex = start + 1;
		        mEnd = end;
		    }

		    public void Dispose()
		    {
		    }

		    public ref int Index
		    {
		        get mut
		        {
		            return ref mIndex;
		        }
		    }

			public int End => mEnd;

			[Inline]
			public Result<int> GetNext() mut
			{
				if (mIndex <= mEnd)
					return .Err;
				return --mIndex;
			}
		}
	}

	struct Range<T> where T : operator T + T where T : operator T - T where bool : operator T >= T
	{
		protected T mStart;
		protected T mEnd;

		public this()
		{
			mStart = default;
			mEnd = default;
		}

		[Inline]
		public this(T start, T end)
		{
			Debug.Assert(end >= start);
			mStart = start;
			mEnd = end;
		}

		public T Length
	    {
	    	[Inline]
	        get
			{
				return mEnd - mStart;
			}

			[Inline]
			set mut
			{
				mEnd = mStart + value;
			}
	    }
		
		public T Start
		{
			[Inline]
			get
			{
				return mStart;
			}

			[Inline]
			set	mut
			{
				mStart = value;
			}
		}
		
		public T End
		{
			[Inline]
			get
			{
				return mEnd;
			}

			set mut
			{
				mEnd = value;
			}
		}
		
		public bool IsEmpty
		{
			[Inline]
			get
			{
				return mEnd == mStart;
			}
		}

		public bool Contains(T idx)
		{
			return (idx >= mStart) && !(idx >= mEnd);
		}

		public bool Contains(Range<T> val)
		{
			return (val.[Friend]mStart >= mStart) && (val.[Friend]mEnd <= mEnd);
		}

		public void Clear() mut
		{
			mStart = default;
			mEnd = default;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"{mStart}..<{mEnd}");
		}
	}

	extension Range<T> where T : operator T + T where T : operator T - T where T : operator implicit int8 where bool : operator T >= T
	{
		[Inline]
		public RangeEnumerator<T> GetEnumerator()
		{
			return RangeEnumerator<T>(this);
		}

		public ReverseRangeEnumerator<T> Reversed
		{
			[Inline]
			get
			{
				return ReverseRangeEnumerator<T>(mEnd + -1, mStart);
			}
		}
	}

	public struct RangeEnumerator<T> : IEnumerator<T> where T : operator T + T where T : operator T - T where T : operator implicit int8 where bool : operator T >= T
	{
	    private T mEnd;
	    private T mIndex;

		[Inline]
	    public this(Range<T> range)
	    {
	        mIndex = range.[Friend]mStart + -1;
	        mEnd = range.[Friend]mEnd;
	    }

	    public void Dispose()
	    {
	    }

	    public ref T Index
	    {
	        get mut
	        {
	            return ref mIndex;
	        }
	    }

		public T End => mEnd;

		[Inline]
		public Result<T> GetNext() mut
		{
			if (mIndex + (int8)1 >= mEnd)
				return .Err;
			mIndex = mIndex + (int8)1;
			return mIndex;
		}
	}

	public struct ReverseRangeEnumerator<T> : IEnumerator<T>  where T : operator T + T where T : operator T - T where T : operator implicit int8 where bool : operator T >= T
	{
	    private T mEnd;
	    private T mIndex;

		[Inline]
	    public this(T start, T end)
	    {
	        mIndex = start + (int8)1;
	        mEnd = end;
	    }

	    public void Dispose()
	    {
	    }

	    public ref T Index
	    {
	        get mut
	        {
	            return ref mIndex;
	        }
	    }

		public T End => mEnd;

		[Inline]
		public Result<T> GetNext() mut
		{
			if (mIndex <= mEnd)
				return .Err;
			mIndex = mIndex + (int8)(-1);
			return mIndex;
		}
	}

	struct IndexRange : RangeExpression
	{
		protected Index mStart;
		protected Index mEnd;
		protected bool mIsClosed;

		public this()
		{
			this = default;
		}

		[Inline]
		public this(Index start, Index end, bool isClosed=true)
		{
			mStart = start;
			mEnd = end;
			mIsClosed = isClosed;
		}

		[Inline]
		public this(int start, Index end, bool isClosed=true)
		{
			mStart = .FromFront(start);
			mEnd = end;
			mIsClosed = isClosed;
		}

		[Inline]
		public this(Index start, int end, bool isClosed=true)
		{
			mStart = start;
			mEnd = .FromFront(end);
			mIsClosed = isClosed;
		}

		[Inline]
		public this(int start, int end, bool isClosed=true)
		{
			Debug.Assert(end >= start);
			mStart = .FromFront(start);
			mEnd = .FromFront(end);
			mIsClosed = isClosed;
		}

		public Index Start
		{
			[Inline]
			get
			{
				return mStart;
			}

			[Inline]
			set	mut
			{
				mStart = value;
			}
		}

		public Index End
		{
			[Inline]
			get
			{
				return mEnd;
			}

			set mut
			{
				mEnd = value;
			}
		}

		public bool IsClosed
		{
			[Inline]
			get
			{
				return mIsClosed;
			}

			set mut
			{
				mIsClosed = value;
			}
		}

		public override void ToString(String strBuffer)
		{
			mStart.ToString(strBuffer);
			if (mIsClosed)
				strBuffer.Append("...");
			else
				strBuffer.Append("..<");
			mEnd.ToString(strBuffer);
		}

		public int GetLength(int size)
		{
			int length = mEnd.Get(size) - mStart.Get(size);
			if (mIsClosed)
				length++;
			return length;
		}
	}

	struct ClosedRange : RangeExpression, IEnumerable<int>
	{
		protected int mStart;
		protected int mEnd;

		public this()
		{
			mStart = 0;
			mEnd = 0;
		}

		[Inline]
		public this(int start, int end)
		{
			Debug.Assert(end >= start);
			mStart = start;
			mEnd = end;
		}

		public int Length
	    {
	    	[Inline]
	        get
			{
				return mEnd - mStart;
			}

			[Inline]
			set mut
			{
				mEnd = mStart + value;
			}
	    }
		
		public int Start
		{
			[Inline]
			get
			{
				return mStart;
			}

			[Inline]
			set	mut
			{
				mStart = value;
			}
		}
		
		public int End
		{
			[Inline]
			get
			{
				return mEnd;
			}

			set mut
			{
				mEnd = value;
			}
		}
		
		public bool IsEmpty
		{
			[Inline]
			get
			{
				return mEnd == mStart;
			}
		}

		public Range.ReverseEnumerator Reversed
		{
			[Inline]
			get
			{
				return Range.ReverseEnumerator(mEnd, mStart);
			}
		}
		
		public bool Contains(int idx)
		{
			return (idx >= mStart) && (idx <= mEnd);
		}

		public bool Contains(Range val)
		{
			return (val.[Friend]mStart >= mStart) && (val.[Friend]mEnd - 1 <= mEnd);
		}

		public bool Contains(ClosedRange val)
		{
			return (val.[Friend]mStart >= mStart) && (val.[Friend]mEnd <= mEnd);
		}

		public void Clear() mut
		{
			mStart = 0;
			mEnd = 0;
		}

		public static operator IndexRange(ClosedRange list)
		{
			return .(.FromFront(list.mStart), .FromFront(list.mEnd), true);
		}

		[Inline]
		public Enumerator GetEnumerator()
		{
			return Enumerator(this);
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"{mStart}...{mEnd}");
		}

		public struct Enumerator : IEnumerator<int>
		{
		    private int mEnd;
		    private int mIndex;

			[Inline]
		    public this(ClosedRange range)
		    {
		        mIndex = range.mStart - 1;
		        mEnd = range.mEnd;
		    }

		    public void Dispose()
		    {
		    }

		    public ref int Index
		    {
		        get mut
		        {
		            return ref mIndex;
		        }
		    }

			public int End => mEnd;

			[Inline]
			public Result<int> GetNext() mut
			{
				if (mIndex >= mEnd)
					return .Err;
				return ++mIndex;
			}
		}
	}
}
