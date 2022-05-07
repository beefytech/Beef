using System;

extension LibClassA
{
	public int32 mC = GetVal(9, 10000, "TestsB.LibClassA.mC");

	public new this()
	{
		PrintF("TestB.LibClassA()\n");
		mB += 10000;
	}

	public new int GetVal2()
	{
		return 11;
	}
}


namespace TestsB
{
	class TestsB0
	{
		[Test]
		static void TestSharedData()
		{
			LibClassA ca = scope LibClassA(123);
			Test.Assert(ca.mA == 7);
			// From LibB. We don't have LibC as a dep so we can access this member.
			Test.Assert(ca.mB == 1008);
			Test.Assert(ca.mC == 9);
			Test.Assert(ca.GetVal2() == 11);

			ca = scope LibClassA();
			Test.Assert(ca.mA == 7);
			Test.Assert(ca.mB == 10008);
			Test.Assert(ca.mC == 9);

			// Should call the int32 ctor, not the unreachable LibC int8 ctor
			ca = scope LibClassA((int8)123);
			Test.Assert(ca.mA == 7);
			Test.Assert(ca.mB == 1008);
			Test.Assert(ca.mC == 9);
			Test.Assert(ca.GetVal2() == 11);

			LibA.LibA0 la0 = scope .();
			int la0a = la0.GetA();
			Test.Assert(la0a == 2);
		}
	}

}

