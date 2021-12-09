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
