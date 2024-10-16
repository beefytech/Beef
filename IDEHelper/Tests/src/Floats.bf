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

		[Test]
		public static void TestCmp()
		{
			float fNeg = -1;
			float fNan = float.NaN;

			if (fNeg < 0)
			{
			}
			else
			{
				Test.FatalError();
			}
			if (fNeg > 0)
				Test.FatalError();

			if (fNan < 0)
				Test.FatalError();
			if (fNan <= 0)
				Test.FatalError();
			if (fNan > 0)
				Test.FatalError();
			if (fNan >= 0)
				Test.FatalError();
			if (fNan == 0)
				Test.FatalError();
			if (fNan != 0)
			{

			}
			else
			{
				Test.FatalError();
			}

			if (fNan == fNan)
				Test.FatalError();

			if (fNan != fNan)
			{

			}
			else
			{
				Test.FatalError();
			}

			bool b0 = fNan < 0;
			bool b1 = fNan > 0;
			bool b2 = fNan == fNan;
			bool b3 = fNan != fNan;
			bool b4 = fNan != 0;

			Test.Assert(!b0);
			Test.Assert(!b1);
			Test.Assert(!b2);
			Test.Assert(b3);
			Test.Assert(b4);
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
