using System;

namespace Tests
{
	class VarArgs
	{
#if BF_PLATFORM_WINDOWS
		[CLink, Import("msvcrt.dll")]
#else
		[CLink]
#endif
		public static extern int32 sprintf(char8* dest, char8* fmt, ...);

		[Test]
		public static void TestBasics()
		{
			char8[256] cStr;
			sprintf(&cStr, "Test %d %0.1f %0.1f", 123, 1.2f, 2.3f);

			String str = scope .(&cStr);
			Test.Assert(str == "Test 123 1.2 2.3");
		}
	}
}
