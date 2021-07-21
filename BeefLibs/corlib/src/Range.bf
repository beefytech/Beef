using System.Collections;
using System.Diagnostics;

namespace System
{
	interface RangeExpression
	{

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
		
		public bool Contains(int idx)
		{
			return (idx >= mStart) && (idx < mStart);
		}

		public void Clear() mut
		{
			mStart = 0;
			mEnd = 0;
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
		
		public bool Contains(int idx)
		{
			return (idx >= mStart) && (idx < mStart);
		}

		public void Clear() mut
		{
			mStart = 0;
			mEnd = 0;
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
