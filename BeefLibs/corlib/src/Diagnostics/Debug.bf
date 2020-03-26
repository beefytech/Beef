namespace System.Diagnostics
{
	class Debug
	{
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
		public static void Assert(bool condition, String error) 
		{
			if (!condition)
				Internal.FatalError(error, 1);
		}

#if !DEBUG
		[SkipCall]
#endif
		public static void FatalError(String msg = "Fatal error encountered")
		{
			Internal.FatalError(msg, 1);
		}

#if !DEBUG
		[SkipCall]
#endif
		public static void AssertNotStack(Object obj)
		{
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			if ((obj != null) && (obj.[Friend]GetFlags() & 8 != 0))
				Internal.FatalError("Assert failed", 1);
#endif
		}

		static extern void Write(char8* str, int strLen);

		public static void Write(String line)
		{
			Write(line.Ptr, line.Length);
		}

		public static void Write(String fmt, params Object[] args)
		{
			String str = scope String(256);
			str.AppendF(fmt, params args);
			Write(str);
		}

		public static void Write(Object obj)
		{
			String str = scope String(256);
			obj.ToString(str);
			Write(str);
		}

		public static void WriteLine()
		{
			Write("\n", 1);
		}

		public static void WriteLine(StringView line)
		{
			Write(line.Ptr, line.Length);
			Write("\n", 1);
		}

		public static void WriteLine(StringView strFormat, params Object[] args)
		{
			String paramStr = scope String();
			paramStr.AppendF(strFormat, params args);
			paramStr.Append('\n');
			Write(paramStr.Ptr, paramStr.Length);
		}
	}
}
