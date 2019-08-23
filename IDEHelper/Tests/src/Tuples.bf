#pragma warning disable 168

using System;

namespace Tests
{
	class Tuples
	{
		[Test]
		public static void TestBasic()
		{
			let (a, b, c) = (1, 2, 3);
			Test.Assert(a == 1);
			Test.Assert(b == 2);
			Test.Assert(c == 3);
			Test.Assert(typeof(decltype(a)) == typeof(int));

			(int32, float) tVal1 = (1, 2.0f);
			// Allow conversion from named to unnamed
			tVal1 = (int32 a, float b)(2, 3.0f);

			let v0 = tVal1.0;
			Test.Assert(v0 == 2);
		}

		class ValClass
		{
			public int mA;

			public int Prop
			{
				set
				{
					mA = value;
				}
			}
		}

		[Test]
		public static void TestDecompose()
		{
			ValClass vc = scope .();

			uint8 zz = 202;
			(var val0, vc.Prop) = (101, zz);
			Test.Assert(val0 == 101);
			Test.Assert(vc.mA == 202);

			let tup0 = (111, 222);
			(int a, int b) tup1 = tup0;
			Test.Assert(tup0.0 == 111);
			Test.Assert(tup0.1 == 222);
		}
	}
}
  