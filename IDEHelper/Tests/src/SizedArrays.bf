using System;

namespace Tests
{
	class SizedArrays
	{
		public static int[8] iArr = .(123, 234, 345, );

		[Test]
		static void TestBasics()
		{
			int[3][2] val0 = .((1, 2), (3, 4), (5, 6));
			Test.Assert(sizeof(decltype(val0)) == sizeof(int)*6);

			Test.Assert(val0[0][0] == 1);
			Test.Assert(val0[0][1] == 2);
			Test.Assert(val0[1][0] == 3);

			int[2] val1 = .(7, 8);
			val0[1] = val1;

			int[3][2] val2 = .((1, 2), (7, 8), (5, 6));
			Test.Assert(val0 == val2);
			val2[0][0] = 9;
			Test.Assert(val0 != val2);
			Test.Assert(val2[1] == val1);
		}

		[Test]
		static void TestStatic()
		{
			Test.Assert(iArr[0] == 123);
			Test.Assert(iArr[1] == 234);
			Test.Assert(iArr[2] == 345);
			Test.Assert(iArr[3] == 0);

			iArr[0] += 1000;
			iArr[2] += 2000;
			iArr[3] += 3000;
			iArr[3] += 4000;

			Test.Assert(iArr[0] == 1123);
			Test.Assert(iArr[1] == 2234);
			Test.Assert(iArr[2] == 3345);
			Test.Assert(iArr[3] == 4000);
		}
	}
}
