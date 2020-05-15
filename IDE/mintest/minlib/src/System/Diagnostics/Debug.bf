namespace System.Diagnostics
{
	class Debug
	{
		//[System.Diagnostics.Conditional("DEBUG")]

#if !DEBUG
		[SkipCall]
#endif
        public static void Assert(bool condition) 
        {
			if (!condition)
				Internal.FatalError("Assert failed", 1);
        }

#if !DEBUG
		[SkipCall]
#endif
		public static void Assert(bool condition, StringView error) 
		{
			if (!condition)
				Internal.FatalError("Assert failed", 1);
		}

#if !DEBUG
		[CallingConvention(.Cdecl), SkipCall]
#else
		[CallingConvention(.Cdecl)]
#endif
		static extern void Write(char8* str, int strLen);

		public static void WriteLine(StringView line)
		{
			Write(line.Ptr, line.Length);
			Write("\n", 1);
		}

		/*public static void WriteLine(StringView strFormat, params Object[] args)
		{
			String paramStr = scope String();
			paramStr.FormatInto(strFormat, args);
			paramStr.Append('\n');
			Write(paramStr.Ptr, paramStr.Length);
		}*/

		[NoReturn]
		public static void FatalError(String str = "")
		{
		    Internal.FatalError(str, 1);
		}
	}
}
