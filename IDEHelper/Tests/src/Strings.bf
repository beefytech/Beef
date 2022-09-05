using System;
using System.Interop;

namespace Tests
{
	class Strings
	{
		const String cString = "Abc";

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

			static c_wchar[?] cWStr = "Test".ToConstNativeW();
			c_wchar* wStr = &cWStr;
			Test.Assert(wStr[0] == 'T');
			Test.Assert(wStr[1] == 'e');
			Test.Assert(wStr[2] == 's');
			Test.Assert(wStr[3] == 't');
			Test.Assert(wStr[4] == '\0');

			StringView sv = "Abcd";
			sv.Length--;
			Test.Assert(sv == "Abc");

			char8[?] arr0 = "Abcd";
			char8[?] arr1 = cString;
			Test.Assert(arr0.Count == 4);
			Test.Assert(arr1.Count == 3);
		}
	}
}
