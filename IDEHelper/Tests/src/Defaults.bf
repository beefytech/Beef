using System;

namespace Tests
{
	class Defaults
	{
		struct StructA
		{
			public int mA = 123;
			public int mB = 234;

			public static implicit operator StructA(float f)
			{
				StructA val = .();
				val.mA = (.)f;
				return val;
			}

			public static implicit operator StructA(float[2] f)
			{
				StructA val = default;
				val.mA = (.)f[0];
				val.mB = (.)f[1];
				return val;
			}
		}

		public static int Default0(StructA sa = 45.6f)
		{
			return sa.mA + sa.mB * 1000;
		}

		public static int Default1(StructA sa = float[2](12.3f, 23.4f))
		{
			return sa.mA + sa.mB * 1000;
		}

		[Test]
		public static void CheckBasics()
		{
			int val = Default0();
			Test.Assert(val == 23012);
			val = Default1();
			Test.Assert(val == 23012);
		}
	}
}
