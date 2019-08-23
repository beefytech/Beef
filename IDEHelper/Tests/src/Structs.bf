using System;

namespace Tests
{
	class Structs
	{
		struct StructA
		{

		}

		struct StructB
		{
			public int mA;
			public int mB;

			public this()
			{
				mA = 0;
				mB = 0;
			}

			public this(int a, int b)
			{
				mA = a;
				mB = b;
			}
		}

		struct StructC
		{
			int8 mA;
			int32 mB;
			int8 mC;
		}

		[Ordered]
		struct StructD
		{
			int8 mA;
			int32 mB;
			int8 mC;
		}

		[CRepr]
		struct StructE
		{
			int8 mA;
			int32 mB;
			int8 mC;
		}

		struct StructF : StructC
		{
			int8 mD;
		}

		struct StructG : StructD
		{
			int8 mD;
		}

		struct StructH : StructE
		{
			int8 mD;
		}

		[CRepr]
		struct StructI : StructE
		{
			int8 mD;
		}

		[Test]
		static void TestBasics()
		{
			Test.Assert(sizeof(StructA) == 0);

			StructB sb0 = .(1, 2);
			StructB sb1;
			sb1.mA = 1;
			sb1.mB = 2;
			Test.Assert(sb0 == sb1);
		}

		[Test]
		static void TestLayouts()
		{
			Test.Assert(sizeof(StructC) == 6);
			Test.Assert(alignof(StructC) == 4);
			Test.Assert(strideof(StructC) == 8);

			Test.Assert(sizeof(StructD) == 9);
			Test.Assert(alignof(StructD) == 4);
			Test.Assert(strideof(StructD) == 12);

			Test.Assert(sizeof(StructE) == 12);
			Test.Assert(alignof(StructE) == 4);
			Test.Assert(strideof(StructE) == 12);

			Test.Assert(sizeof(StructF) == 7);
			Test.Assert(alignof(StructF) == 4);
			Test.Assert(strideof(StructF) == 8);

			Test.Assert(sizeof(StructG) == 10);
			Test.Assert(alignof(StructG) == 4);
			Test.Assert(strideof(StructG) == 12);

			Test.Assert(sizeof(StructH) == 13);
			Test.Assert(alignof(StructH) == 4);
			Test.Assert(strideof(StructH) == 16);

			Test.Assert(sizeof(StructI) == 16);
			Test.Assert(alignof(StructI) == 4);
			Test.Assert(strideof(StructI) == 16);
		}
	}
}
