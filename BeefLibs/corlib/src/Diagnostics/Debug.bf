namespace System.Diagnostics
{
	class Debug
	{
#if !DEBUG
		[SkipCall]
#endif
		public static void Assert(bool condition, String error = Compiler.CallerExpression[0], String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum) 
		{
			if (!condition)
			{
				String failStr = scope .()..AppendF("Assert failed: {} at line {} in {}", error, line, filePath);
				Internal.FatalError(failStr, 1);
			}
		}

#if !DEBUG
		[SkipCall]
#endif
		public static void FatalError(String msg = "Fatal error encountered", String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum)
		{
			String failStr = scope .()..AppendF("{} at line {} in {}", msg, line, filePath);
			Internal.FatalError(failStr, 1);
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

		[CallingConvention(.Cdecl)]
		static extern void Write(char8* str, int strLen);

		public static void Write(String line)
		{
			Write(line.Ptr, line.Length);
		}

		public static void Write(String fmt, params Object[] args)
		{
			String str = scope String(4096);
			str.AppendF(fmt, params args);
			Write(str);
		}

		public static void Write(Object obj)
		{
			String str = scope String(4096);
			obj.ToString(str);
			Write(str);
		}

		public static void WriteLine()
		{
			Write((char8*)"\n", 1);
		}

		public static void WriteLine(StringView line)
		{
			String lineStr = scope String(Math.Min(line.Length, 4096));
			lineStr.Append(line);
			lineStr.Append('\n');
			Write(lineStr.Ptr, lineStr.Length);
		}

		public static void WriteLine(StringView strFormat, params Object[] args)
		{
			String paramStr = scope String(4096);
			paramStr.AppendF(strFormat, params args);
			paramStr.Append('\n');
			Write(paramStr.Ptr, paramStr.Length);
		}

		static bool gIsDebuggerPresent = IsDebuggerPresent;
		[LinkName("IsDebuggerPresent"), CallingConvention(.Stdcall)]
		static extern int32 Internal_IsDebuggerPresent();

		public static bool IsDebuggerPresent
		{
#if BF_PLATFORM_WINDOWS
			get => gIsDebuggerPresent = Internal_IsDebuggerPresent() != 0;
#else
			get => false;
#endif
		}

		[Intrinsic("debugtrap")]
		public static extern void Break();

		[NoDebug]
		public static void SafeBreak()
		{
			if (gIsDebuggerPresent)
				Break();
		}
	}
}
