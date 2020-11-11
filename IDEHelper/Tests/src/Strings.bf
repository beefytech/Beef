using System;

namespace Tests
{
	class Strings
	{
		static void FormatString(String outString, String format, params Object[] args)
		{
			outString.AppendF(format, params args);
		}

		[Test]
		public static void TestBasics()
		{
			var str0 = scope $@"AB\C";
			Test.Assert(str0 == "AB\\C");
			var str1 = scope @$"\A{100+200}B{100+200:X}";
			Test.Assert(str1 == "\\A300B12C");
			var str2 = scope String();
			FormatString(str2, $"\a{200+300}B{200+300:X}");
			Test.Assert(str2 == "\a500B1F4");
		}
	}
}
