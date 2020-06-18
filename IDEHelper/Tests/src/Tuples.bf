#pragma warning disable 168

using System;

namespace Tests
{
	class Tuples
	{
		public static void Add(ref (int32, float) val)
		{
			val.0 += 100;
			val.1 += 200;
		}

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

			Add(ref tVal1);
			Test.Assert(tVal1 == (a: 102, b: 203));
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
  