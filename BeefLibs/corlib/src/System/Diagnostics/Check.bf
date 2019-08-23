namespace System.Diagnostics
{
	class Check
	{

		[Unchecked, SkipCall]
		public static void Assert(bool condition) 
		{
		}

		[Checked]
		public static void Assert(bool condition) 
		{
			if (!condition)
				Internal.FatalError("Assert failed", 1);
		}

		[Unchecked, SkipCall]
		public static void Assert(bool condition, String error) 
		{
		}

		[Checked]
		public static void Assert(bool condition, String error) 
		{
			if (!condition)
				Internal.FatalError(error, 1);
		}

		[NoReturn]
		[SkipCall]
		public static void FatalError(String msg = "Fatal error encountered")
		{
			Internal.FatalError(msg, 1);
		}
	}
}
