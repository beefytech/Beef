using System.Collections;
using System.Diagnostics;

namespace System
{
	enum Index
	{
		case FromFront(int offset);
		case FromEnd(int offset);
	}

	struct IndexRange
	{
		public Index mStart;
		public Index mEnd;
		public bool mIsClosed;

		public this()
		{
			this = default;
		}

		public this(Index start, Index end, bool isClosed=true)
		{
			mStart = start;
			mEnd = end;
			mIsClosed = isClosed;
		}

		public this(int start, Index end, bool isClosed=true)
		{
			mStart = .FromFront(start);
			mEnd = end;
			mIsClosed = isClosed;
		}

		public this(Index start, int end, bool isClosed=true)
		{
			mStart = start;
			mEnd = .FromFront(end);
			mIsClosed = isClosed;
		}

		public this(int start, int end, bool isClosed=true)
		{
			mStart = .FromFront(start);
			mEnd = .FromFront(end);
			mIsClosed = isClosed;
		}
	}

	struct Range
	{
		protected int mStart;
		protected int mEnd;

		public this()
		{
			mStart = 0;
			mEnd = 0;
		}

		public this(int start, int end)
		{
			Debug.Assert(end >= start);
			mStart = start;
			mEnd = end;
		}
	}

	struct ClosedRange
	{
		protected int mStart;
		protected int mEnd;

		public this()
		{
			mStart = 0;
			mEnd = 0;
		}

		public this(int start, int end)
		{
			Debug.Assert(end >= start);
			mStart = start;
			mEnd = end;
		}
	}
}
