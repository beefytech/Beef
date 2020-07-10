#pragma warning disable 168

using System;

namespace Tests
{
	class Functions
	{
		class ClassA
		{
			int mA = 123;

			public int GetA(float f)
			{
				return mA + (int)f;
			}

			public int GetT<T>(T val) where T : var
			{
				return mA + (int)val;
			}
		}

		struct StructA
		{
			int mA = 123;
			int mB = 234;

			public int GetA(float f)
			{
				return mA + mB*100 + (int)f;
			}

			public int GetA2(float f) mut
			{
				return mA + mB*100 + (int)f;
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			StructA sa = .();

			function int (ClassA this, float f) func0 = => ca.GetA;
			function int (ClassA this, float) func0b = func0;
			Test.Assert(func0(ca, 100.0f) == 223);
			func0 = => ca.GetT<float>;
			Test.Assert(func0(ca, 100.0f) == 223);

			function int (StructA this, float f) func1 = => sa.GetA;
			Test.Assert(func1(sa, 100.0f) == 23623);

			function int (mut StructA this, float f) func2 = => sa.GetA2;
			Test.Assert(func2(mut sa, 100.0f) == 23623);
		}
	}
}
