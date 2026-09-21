using System;

namespace SlotRebuild
{
	class Program
	{
		public static void Main()
		{
			// A failed assert exits non-zero, and so does a crash before reaching Main, which is
			//  what a stale interface slot table causes. Either fails the test script's Execute
			Runtime.Assert(Virtuals.Test() == 2099 + 234);
		}
	}
}
