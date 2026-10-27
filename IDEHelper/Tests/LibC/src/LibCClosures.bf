using System;

namespace SharedGlobals
{
	// Globals of a namespace LibA has globals in too: one globals type, its LibC part in a module
	//  of its own. TestsB, which uses LibA's part but does not reference LibC, must not take this
	//  part (and the closure in it, capturing a LibC type) into its link
	struct LibCCaptured
	{
		public int32 mValue;
	}

	static
	{
		public static int32 LibCCaptures(LibCCaptured captured)
		{
			delegate int32() get = scope () => captured.mValue;
			return get();
		}
	}
}
