using System;

namespace Tests;

class Opaques
{
	struct StructA
	{
		public int mA;
		public int mB;
	}

	struct StructB;

	public static void Modify(this ref StructB @this, int addA, int addB)
	{
		StructB* sbPtr = &@this;
		StructA* saPtr = (.)(void*)sbPtr;
		saPtr.mA += addA;
		saPtr.mB += addB;
	}

	[Test]
	public static void TestBasics()
	{
		StructA sa = .() { mA = 123, mB = 234 };
		StructB* sb = (.)&sa;
		sb.Modify(1000, 2000);
		Test.Assert(sa.mA == 1123);
		Test.Assert(sa.mB == 2234);
	}
}