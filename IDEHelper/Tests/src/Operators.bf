using System;

namespace Tests
{
	class Operators
	{
		struct StructA
		{
			public int mA;

			public static StructA operator+(StructA lhs, StructA rhs)
			{
				StructA res;
				res.mA = lhs.mA + rhs.mA;
				return res;
			}

			public static StructA operator-(StructA lhs, StructA rhs)
			{
				StructA res;
				res.mA = lhs.mA - rhs.mA;
				return res;
			}
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa0 = default;
			sa0.mA = 1;
			StructA sa1 = default;
			sa1.mA = 2;

			StructA sa2 = sa0 + sa1;
			Test.Assert(sa2.mA == 3);
		}
	}
}
