using System;

namespace Tests
{
	class MathIntrinsics
	{
		// Keep parameterized entry points available for inspecting generated IR.
		[Export]
		public static float SqrtFloat(float x) => Math.Sqrt(x);
		[Export]
		public static double SqrtDouble(double x) => Math.Sqrt(x);
		[Export]
		public static float PowFloat(float x, float y) => Math.Pow(x, y);
		[Export]
		public static double PowDouble(double x, double y) => Math.Pow(x, y);
		[Export]
		public static float FmaFloat(float x, float y, float z) => Math.FusedMultiplyAdd(x, y, z);
		[Export]
		public static double FmaDouble(double x, double y, double z) => Math.FusedMultiplyAdd(x, y, z);

		static bool CheckBindings()
		{
			function float(float) sqrtFloat = => Math.Sqrt;
			function double(double) sqrtDouble = => Math.Sqrt;
			function float(float, float) powFloat = => Math.Pow;
			function double(double, double) powDouble = => Math.Pow;
			function float(float, float, float) fmaFloat = => Math.FusedMultiplyAdd;
			function double(double, double, double) fmaDouble = => Math.FusedMultiplyAdd;
			const float ef = 1.0f / 8388608.0f;
			const double ed = 1.0 / 4503599627370496.0;
			return (sqrtFloat(9.0f) == 3.0f) && (sqrtDouble(16.0) == 4.0) &&
				(powFloat(2.0f, 3.0f) == 8.0f) && (powDouble(3.0, 2.0) == 9.0) &&
				(fmaFloat(2.0f, 3.0f, 4.0f) == 10.0f) && (fmaDouble(2.0, 3.0, 4.0) == 10.0) &&
				(fmaFloat(1.0f + ef, 1.0f - ef, -1.0f) == -(ef * ef)) &&
				(fmaDouble(1.0 + ed, 1.0 - ed, -1.0) == -(ed * ed));
		}

		[Test]
		public static void TestBasics()
		{
			Test.Assert(SqrtFloat(9.0f) == 3.0f);
			Test.Assert(SqrtDouble(16.0) == 4.0);
			Test.Assert(PowFloat(2.0f, 3.0f) == 8.0f);
			Test.Assert(PowDouble(3.0, 2.0) == 9.0);
			Test.Assert(CheckBindings());

			const float sqrtFloat = Math.Sqrt(9.0f);
			const double sqrtDouble = Math.Sqrt(16.0);
			const float powFloat = Math.Pow(2.0f, 3.0f);
			const double powDouble = Math.Pow(3.0, 2.0);
			const bool bindings = CheckBindings();
			Test.Assert((sqrtFloat == 3.0f) && (sqrtDouble == 4.0));
			Test.Assert((powFloat == 8.0f) && (powDouble == 9.0));
			Test.Assert(bindings);
		}

		[Test]
		public static void TestFusedRounding()
		{
			// (1 + e) * (1 - e) - 1 is -e^2 with one rounding, but zero
			// when the product is rounded first. All values below are exact.
			const float ef = 1.0f / 8388608.0f;
			const double ed = 1.0 / 4503599627370496.0;
			Test.Assert(FmaFloat(1.0f + ef, 1.0f - ef, -1.0f) == -(ef * ef));
			Test.Assert(FmaDouble(1.0 + ed, 1.0 - ed, -1.0) == -(ed * ed));
			const float fusedFloat = Math.FusedMultiplyAdd(1.0f + ef, 1.0f - ef, -1.0f);
			const double fusedDouble = Math.FusedMultiplyAdd(1.0 + ed, 1.0 - ed, -1.0);
			Test.Assert(fusedFloat == -(ef * ef));
			Test.Assert(fusedDouble == -(ed * ed));

			// The unrounded product may overflow the destination format.
			Test.Assert(FmaFloat(float.MaxValue, 2.0f, -float.MaxValue) == float.MaxValue);
			Test.Assert(FmaDouble(double.MaxValue, 2.0, -double.MaxValue) == double.MaxValue);
		}

		[Test]
		public static void TestSpecialValues()
		{
			Test.Assert(BitConverter.Convert<float, int32>(SqrtFloat(-0.0f)) < 0);
			Test.Assert(BitConverter.Convert<double, int64>(SqrtDouble(-0.0)) < 0);
			Test.Assert(SqrtFloat(-1.0f).IsNaN);
			Test.Assert(SqrtDouble(-1.0).IsNaN);
			Test.Assert(SqrtFloat(float.PositiveInfinity) == float.PositiveInfinity);
			Test.Assert(SqrtDouble(double.PositiveInfinity) == double.PositiveInfinity);
			Test.Assert(PowFloat(-2.0f, 3.0f) == -8.0f);
			Test.Assert(PowDouble(-2.0, 3.0) == -8.0);
			Test.Assert(PowFloat(0.0f, -1.0f) == float.PositiveInfinity);
			Test.Assert(PowDouble(0.0, -1.0) == double.PositiveInfinity);
			Test.Assert(BitConverter.Convert<float, int32>(FmaFloat(-0.0f, 1.0f, -0.0f)) < 0);
			Test.Assert(BitConverter.Convert<double, int64>(FmaDouble(-0.0, 1.0, -0.0)) < 0);
			Test.Assert(FmaFloat(float.PositiveInfinity, 0.0f, 1.0f).IsNaN);
			Test.Assert(FmaDouble(double.PositiveInfinity, 0.0, 1.0).IsNaN);
			Test.Assert(FmaFloat(float.NaN, 1.0f, 1.0f).IsNaN);
			Test.Assert(FmaDouble(double.NaN, 1.0, 1.0).IsNaN);
		}
	}
}
