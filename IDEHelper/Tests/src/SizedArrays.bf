using System;

namespace Tests
{
	class SizedArrays
	{
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
	}
}
