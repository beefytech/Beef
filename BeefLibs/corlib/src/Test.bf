namespace System
{
	class Test
	{
		[NoReturn]
		public static void FatalError(String msg = "Test fatal error encountered")
		{
			Internal.FatalError(msg, 1);
		}

		public static void Assert(bool condition) 
		{
			if (!condition)
				Internal.FatalError("Test Assert failed", 1);
		}

		public static void Assert(bool condition, String error) 
		{
			if (!condition)
				Internal.FatalError(error, 2);
		}
	}
}
