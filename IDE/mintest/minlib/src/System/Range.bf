using System.Collections;
using System.Diagnostics;

namespace System
{
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
