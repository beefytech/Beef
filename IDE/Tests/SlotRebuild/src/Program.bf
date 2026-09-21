using System;
using System.IO;

namespace SlotRebuild
{
	class Program
	{
		public static void Main()
		{
			int result = Virtuals.Test();

			// The test script asserts on this file rather than on anything read through the
			//  debugger. With stale interface slot offsets the process dies before reaching
			//  Main, so the file is never written
			File.WriteAllText("slot_sentinel.txt", scope $"ok {result}").IgnoreError();
		}
	}
}
