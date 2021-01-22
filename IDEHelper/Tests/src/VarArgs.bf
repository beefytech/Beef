#pragma warning disable 168

using System;
using System.Diagnostics;

namespace Tests
{
	class VarArgsTests
	{
#if BF_PLATFORM_WINDOWS
		[CLink, Import("msvcrt.dll")]
#else
		[CLink]
#endif
		public static extern int32 sprintf(char8* dest, char8* fmt, ...);

#if BF_PLATFORM_WINDOWS
		[CLink, Import("msvcrt.dll")]
#else
		[CLink]
#endif
		public static extern int32 vsprintf(char8* dest, char8* fmt, void* varArgs);

		public static (int, int, int) MethodA(...)
		{
			VarArgs vaArgs = .();
			vaArgs.Start!();
			int val = vaArgs.Get!<int>();
			int val2 = vaArgs.Get!<int>();
			double d = vaArgs.Get!<double>();
			int val3 = (int)d;
			vaArgs.End!();

			return (val, val2, val3);
		}

		public static (int, int, int) MethodB(int a, ...)
		{
			VarArgs vaArgs = .();
			vaArgs.Start!();
			int val = vaArgs.Get!<int>();
			int val2 = vaArgs.Get!<int>();
			int val3 = (int)vaArgs.Get!<double>();
			vaArgs.End!();

			return (val, val2, val3);
		}

		public static (int, int, int) MethodC(int a, ...)
		{
			uint8* data = scope uint8[a]*;

			VarArgs vaArgs = .();
			vaArgs.Start!();
			int val = vaArgs.Get!<int>();
			int val2 = vaArgs.Get!<int>();
			int val3 = (int)vaArgs.Get!<double>();
			vaArgs.End!();

			return (val, val2, val3);
		}

		public static int32 SPrintF(char8* dest, char8* fmt, ...)
		{
			VarArgs vaArgs = .();
			vaArgs.Start!();
			int32 result = vsprintf(dest, fmt, vaArgs.ToVAList());
			vaArgs.End!();
			return result;
		}

		[Test]
		public static void TestBasics()
		{
			char8[256] cStr;
			sprintf(&cStr, "Test %d %0.1f %0.1f", 123, 1.2f, 2.3f);
			String str = scope .(&cStr);
			Test.Assert(str == "Test 123 1.2 2.3");

			SPrintF(&cStr, "Test %d %0.1f %0.1f", 223, 2.2f, 3.3f);
			str.Set(StringView(&cStr));
			Test.Assert(str == "Test 223 2.2 3.3");

			// LLVM 32-bit varargs bug?
#if BF_32_BIT && BF_PLATFORM_WINDOWS
			Test.Assert(MethodA(    12, 23, 123.0f) case (12, 23, ?));
			Test.Assert(MethodB( 9, 22, 33, 223.0f) case (22, 33, ?));
			Test.Assert(MethodC(11, 32, 43, 323.0f) case (32, 43, ?));
#else
			Test.Assert(MethodA(    12, 23, 123.0f) == (12, 23, 123));
			Test.Assert(MethodB( 9, 22, 33, 223.0f) == (22, 33, 223));
			Test.Assert(MethodC(11, 32, 43, 323.0f) == (32, 43, 323));
#endif
		}
	}
}
