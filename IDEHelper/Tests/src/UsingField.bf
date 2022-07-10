using System;

namespace Tests
{
	class UsingField
	{
		class ClassA
		{
			public int mA0 = 1;
			public static int sA0 = 2;
		}

		class ClassB
		{
			public int mB0 = 3;
			public static int sB0 = 4;
		}

		class ClassC
		{
			public int mC0 = 12;
			public int mC1 = 23;

			public int PropC0 => 123;
			public int MethodC0() => 234;

			using public static ClassA sA = new .() ~ delete _;
			ClassB sB = new .() ~ delete _;

			using public ClassB B => sB;
		}

		class ClassD
		{
			public using protected append ClassC mC;
			private using append ClassC mC2;

			public int mD0 = 34;

			public this()
			{
				mC.mC0 += 10000;
				mC2.mC0 += 20000;

			}
		}

		[Union]
		struct Vector2
		{
			public struct Coords
			{
				public float mX;
				public float mY;
			}

			public float[2] mValues;
			public using Coords mCoords;

			public this(float x, float y)
			{
				mX = x;
				mY = y;
			}
		}

		[Test]
		public static void Test()
		{
			ClassD cd = scope .();
			cd.mC0 = 123;
			Test.Assert(cd.PropC0 == 123);
			Test.Assert(cd.MethodC0() == 234);
			Test.Assert(cd.mD0 == 34);

			Test.Assert(ClassD.sA0 == 2);
			Test.Assert(ClassD.sB0 == 4);

			Vector2 vec = .(1.2f, 2.3f);
			Test.Assert(sizeof(Vector2) == 8);
			Test.Assert(vec.mX == 1.2f);
			Test.Assert(vec.mY == 2.3f);
			Test.Assert(vec.mValues[0] == 1.2f);
			Test.Assert(vec.mValues[1] == 2.3f);
		}
	}
}