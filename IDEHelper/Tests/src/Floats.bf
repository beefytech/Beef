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

		public static void RoundTripTest(float value, StringView expected)
		{
			String str = scope .();
			value.ToString(str, "R", null);
			Test.Assert(str == expected);
		}

		public static void RoundTripTest(double value, StringView expected)
		{
			String str = scope .();
			value.ToString(str, "R", null);
			Test.Assert(str == expected);
		}

		[Test]
		public static void TestToStringRoundTrip()
		{
			RoundTripTest(0.0f, "0");
			RoundTripTest(float.NegativeZero, "-0");
			RoundTripTest(1.0f, "1");
			RoundTripTest(-1.0f, "-1");
			RoundTripTest(0.6f, "0.6");
			RoundTripTest(0.1f, "0.1");
			RoundTripTest(0.3f, "0.3");
			RoundTripTest(2.5f, "2.5");
			RoundTripTest(1.0f / 3.0f, "0.33333334");
			RoundTripTest(3.1415927f, "3.1415927");
			RoundTripTest(100.0f, "100");
			RoundTripTest(1234567.0f, "1234567");
			RoundTripTest(12345678.0f, "1.2345678e+07");
			RoundTripTest(123456789.0f, "1.2345679e+08");
			RoundTripTest(0.0001f, "0.0001");
			RoundTripTest(0.00001f, "1e-05");
			RoundTripTest(float.MaxValue, "3.4028235e+38");
			RoundTripTest(float.MinValue, "-3.4028235e+38");
			RoundTripTest(float.Epsilon, "1e-45");
			RoundTripTest(1.1754944e-38f, "1.1754944e-38");
			// Exact decimal midpoint - correctly rounded parsers get back to
			// 33554448 through the round-to-nearest-even tie break
			RoundTripTest(33554448.0f, "3.355445e+07");
			RoundTripTest(float.PositiveInfinity, "Infinity");
			RoundTripTest(float.NegativeInfinity, "-Infinity");
			RoundTripTest(float.NaN, "NaN");

			RoundTripTest(0.0, "0");
			RoundTripTest(double.NegativeZero, "-0");
			RoundTripTest(0.6, "0.6");
			RoundTripTest(0.1, "0.1");
			RoundTripTest(0.1 + 0.2, "0.30000000000000004");
			RoundTripTest(2.5, "2.5");
			RoundTripTest(1.0 / 3.0, "0.3333333333333333");
			RoundTripTest(3.141592653589793, "3.141592653589793");
			RoundTripTest(1000000000000000.0, "1000000000000000");
			RoundTripTest(10000000000000000.0, "1e+16");
			RoundTripTest(double.MaxValue, "1.7976931348623157e+308");
			RoundTripTest(double.MinValue, "-1.7976931348623157e+308");
			RoundTripTest(double.Epsilon, "5e-324");
			RoundTripTest(2.2250738585072014e-308, "2.2250738585072014e-308");
			RoundTripTest(1e23, "1e+23");
			RoundTripTest(6.02214076e23, "6.02214076e+23");
			RoundTripTest(double.PositiveInfinity, "Infinity");
			RoundTripTest(double.NegativeInfinity, "-Infinity");
			RoundTripTest(double.NaN, "NaN");
		}

		public static void FloatParseRoundTripTest(float value)
		{
			String str = scope .();
			value.ToString(str, "R", null);
			float parsed = float.Parse(str);
			float orig = value;
			Test.Assert(*(uint32*)&parsed == *(uint32*)&orig);
		}

		public static void DoubleParseRoundTripTest(double value)
		{
			String str = scope .();
			value.ToString(str, "R", null);
			double parsed = double.Parse(str);
			double orig = value;
			Test.Assert(*(uint64*)&parsed == *(uint64*)&orig);
		}

		[Test]
		public static void TestParseRoundTrip()
		{
			FloatParseRoundTripTest(0.0f);
			FloatParseRoundTripTest(float.NegativeZero);
			FloatParseRoundTripTest(0.6f);
			FloatParseRoundTripTest(0.1f);
			FloatParseRoundTripTest(1.0f / 3.0f);
			FloatParseRoundTripTest(3.1415927f);
			FloatParseRoundTripTest(1234567.0f);
			FloatParseRoundTripTest(123456789.0f);
			FloatParseRoundTripTest(0.00001f);
			FloatParseRoundTripTest(float.MaxValue);
			FloatParseRoundTripTest(float.MinValue);
			FloatParseRoundTripTest(float.Epsilon);
			FloatParseRoundTripTest(33554448.0f);
			FloatParseRoundTripTest(float.PositiveInfinity);
			FloatParseRoundTripTest(float.NegativeInfinity);

			DoubleParseRoundTripTest(0.0);
			DoubleParseRoundTripTest(double.NegativeZero);
			DoubleParseRoundTripTest(0.6);
			DoubleParseRoundTripTest(0.1);
			DoubleParseRoundTripTest(0.1 + 0.2);
			DoubleParseRoundTripTest(1.0 / 3.0);
			DoubleParseRoundTripTest(3.141592653589793);
			DoubleParseRoundTripTest(10000000000000000.0);
			DoubleParseRoundTripTest(double.MaxValue);
			DoubleParseRoundTripTest(double.MinValue);
			DoubleParseRoundTripTest(double.Epsilon);
			DoubleParseRoundTripTest(2.2250738585072014e-308);
			DoubleParseRoundTripTest(double.PositiveInfinity);
			DoubleParseRoundTripTest(double.NegativeInfinity);

			// Exact midpoint decimals resolve via round-to-nearest-even
			FloatParseTest("33554450", 33554448.0f);
			FloatParseTest("33554450.000000000000001", 33554452.0f);
			FloatParseTest("0.3", 0.3f);
			FloatParseTest("3.4028235e+38", float.MaxValue);
			FloatParseTest("1e-45", float.Epsilon);
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

		[NoInline]
		static float Opaque(float val) => val;

		[NoInline]
		static double Opaque(double val) => val;

		[NoInline]
		static int64 Opaque(int64 val) => val;

		[NoInline]
		static uint64 Opaque(uint64 val) => val;

		static uint32 Bits(float val)
		{
			var v = val;
			return *(uint32*)&v;
		}

		static uint64 Bits(double val)
		{
			var v = val;
			return *(uint64*)&v;
		}

		const float cChain = 120 + (280f / 3 + 10);

		[Test]
		public static void TestFoldAddChain()
		{
			float a = Opaque(280f);
			float b = Opaque(3f);
			float runtime = 120 + (a / b + 10);
			float local = 120 + (280f / 3 + 10);
			Test.Assert(Bits(cChain) == Bits(runtime));
			Test.Assert(Bits(local) == Bits(runtime));
			Test.Assert(Bits(120 + (280f / 3 + 10)) == Bits(runtime));
		}

		[Test]
		public static void TestFoldSubChain()
		{
			float a = Opaque(399f);
			float b = Opaque(90f);
			float c = Opaque(32.6f);
			Test.Assert(Bits(399f / 90f - 32.6f) == Bits(a / b - c));
		}

		[Test]
		public static void TestFoldMulChain()
		{
			float a = Opaque(75.1f);
			float b = Opaque(377f);
			float c = Opaque(353f);
			Test.Assert(Bits(75.1f / 377f * 353f) == Bits(a / b * c));
		}

		[Test]
		public static void TestFoldDivChain()
		{
			float a = Opaque(167.6f);
			float b = Opaque(101.1f);
			float c = Opaque(191.2f);
			Test.Assert(Bits(167.6f * 101.1f / 191.2f) == Bits(a * b / c));
		}

		[Test]
		public static void TestFoldRemChain()
		{
			float a = Opaque(253f);
			float b = Opaque(24f);
			float c = Opaque(5f);
			Test.Assert(Bits(253f / 24f % 5f) == Bits(a / b % c));
		}

		[Test]
		public static void TestFoldNegChain()
		{
			float a = Opaque(322f);
			float b = Opaque(6f);
			float c = Opaque(64f);
			Test.Assert(Bits(-(322f / 6f) - 64f) == Bits(-(a / b) - c));
		}

		[Test]
		public static void TestFoldIntOperandChain()
		{
			float a = Opaque(86.5f);
			Test.Assert(Bits(86.5f / 3 + 7) == Bits(a / 3 + 7));
		}

		[Test]
		public static void TestFoldOverflowChain()
		{
			float a = Opaque(3e38f);
			Test.Assert(Bits(3e38f * 10 / 10) == Bits(a * 10 / 10));
		}

		[Test]
		public static void TestFoldDoubleChain()
		{
			double a = Opaque(1.0);
			double b = Opaque(3.0);
			Test.Assert(Bits((1.0 / 3.0 + 10) * 7) == Bits((a / b + 10) * 7));
		}

		[Test]
		public static void TestFoldIntToFloatCast()
		{
			int64 s = Opaque(9007199791611905L);
			uint64 u = Opaque(9223372586610589697UL);
			Test.Assert(Bits((float)9007199791611905L) == Bits((float)s));
			Test.Assert(Bits((float)9223372586610589697UL) == Bits((float)u));
		}
	}
}
