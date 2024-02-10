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

		public static void FloatParseErrTest(StringView string)
		{
			Test.Assert(float.Parse(string) case .Err);
		}

		[Test]
		public static void TestErrors()
		{
			FloatParseErrTest("");
			FloatParseErrTest("-");
			FloatParseErrTest("+");
			FloatParseErrTest(".");
			FloatParseErrTest("+.");
			FloatParseErrTest("-.");
			FloatParseErrTest("E");
			FloatParseErrTest("e");
			FloatParseErrTest(".E");
			FloatParseErrTest(".e");
			FloatParseErrTest("-.E");
			FloatParseErrTest("-.e");
			FloatParseErrTest("+.E");
			FloatParseErrTest("+.e");
			FloatParseErrTest("5E-");
			FloatParseErrTest("5e-");
			FloatParseErrTest("6E+");
			FloatParseErrTest("6e+");
		}

		public static void MinMaxTest<T>(T expectedMinValue, T expectedMaxValue)
		where T : IMinMaxValue<T>
		where int : operator T <=> T
		{
			Test.Assert(T.MinValue == expectedMinValue);
			Test.Assert(T.MaxValue == expectedMaxValue);
		}

		[Test]
		public static void TestMinMax()
		{
			MinMaxTest<float>(Float.MinValue, Float.MaxValue);
			MinMaxTest<double>(Double.MinValue, Double.MaxValue);
		}
	}
}
