using System;

namespace Tests
{
	class Initializers
	{
		struct StructA
		{
			public int mA = 123;
			public int mB;
			public int mC;
			public int mD;

			public int ValC
			{
				set mut
				{
					mC = value;
				}
			}

			public void Add(float val, float? val2 = 234) mut
			{
				mD += (int)val;
				mD += (int)val2 * 10;
			}

			public void Add(int val) mut
			{
				mD += (int)val * 1000;
			}
		}

		[Test]
		public static void TestBasics()
		{
			StructA sa = .() { mB = 345, ValC = 456, 567.8f, 789};
			Test.Assert(sa.mA == 123);
			Test.Assert(sa.mB == 345);
			Test.Assert(sa.mC == 456);
			Test.Assert(sa.mD == 791907);
		}
	}
}
