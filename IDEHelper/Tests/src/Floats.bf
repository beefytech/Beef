using System;

namespace Tests
{
	class Floats
	{
		public static void FloatParseTest(StringView string, float expectedResult)
		{
			float result = float.Parse(string);
			Test.Assert(expectedResult == result);
		}

		[Test]
		public static void TestBasics()
		{
			FloatParseTest("1.2", 1.2f);
			FloatParseTest("-0.2", -0.2f);
			FloatParseTest("2.5E2", 2.5E2f);
			FloatParseTest("2.7E-10", 2.7E-10f);
			FloatParseTest("-0.17E-7", -0.17E-7f);
			FloatParseTest("8.7e6", 8.7e6f);
			FloatParseTest("3.3e-11", 3.3e-11f);
			FloatParseTest("0.002e5", 0.002e5f);
		}
	}
}
