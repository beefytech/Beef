using System;

namespace Tests
{
	class Interop
	{
		[CRepr]
		public struct StructA
		{
			public int32 mA;

			[LinkName(.CPP)]
			public extern int32 MethodA0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructA MethodA1(StructA sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructB
		{
			public int32 mA;
			public int8 mB;

			[LinkName(.CPP)]
			public extern int32 MethodB0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructB MethodB1(StructB sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructC
		{
			public int8 mA;
			public int32 mB;

			[LinkName(.CPP)]
			public extern int32 MethodC0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructC MethodC1(StructC sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructD
		{
			public int32 mA;
			public int32 mB;

			[LinkName(.CPP)]
			public extern int32 MethodD0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructD MethodD1(StructD sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructE
		{
			public int32 mA;
			public int32 mB;
			public int32 mC;

			[LinkName(.CPP)]
			public extern int32 MethodE0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructE MethodE1(StructE sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructF
		{
			public int8 mA;
			public int8 mB;
			public int8 mC;

			[LinkName(.CPP)]
			public extern int32 MethodF0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructF MethodF1(StructF sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructG
		{
			public int8 mA;
			public int8 mB;
			public int8 mC;
			public int8 mD;

			[LinkName(.CPP)]
			public extern int32 MethodG0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructG MethodG1(StructG sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructH
		{
			public int64 mA;
			public int64 mB;
			public int64 mC;

			[LinkName(.CPP)]
			public extern int32 MethodH0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructH MethodH1(StructH sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructI
		{
			public int8 mA;
			public int8 mB;
			public int8 mC;
			public int8 mD;
			public int8 mE;

			[LinkName(.CPP)]
			public extern int32 MethodI0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructI MethodI1(StructI sa, int32 arg0) mut;
		}

		[CRepr]
		public struct StructJ
		{
			public char8* mPtr;
			public int mLength;

			[LinkName(.CPP)]
			public extern int32 MethodJ0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructJ MethodJ1(StructJ sa, int32 arg0) mut;
		}

		[LinkName(.C)]
		public static extern int32 Func0(int32 a, int32 b);

		[LinkName(.C)]
		public static extern int32 Func1A(StructA arg0, StructA arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1B(StructB arg0, StructB arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1C(StructC arg0, StructC arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1D(StructD arg0, StructD arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1E(StructE arg0, StructE arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1F(StructF arg0, StructF arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1G(StructG arg0, StructG arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1H(StructH arg0, StructH arg1, int32 arg2);
		[LinkName(.C)]
		public static extern int32 Func1I(StructI arg0, StructI arg1, int32 arg2);

		[LinkName(.C)]
		public static extern StructA Func2A(StructA arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructB Func2B(StructB arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructC Func2C(StructC arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructD Func2D(StructD arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructE Func2E(StructE arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructF Func2F(StructF arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructG Func2G(StructG arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructH Func2H(StructH arg0, int32 arg2);
		[LinkName(.C)]
		public static extern StructI Func2I(StructI arg0, int32 arg2);

		[LinkName(.C)]
		public static extern StructJ Func4J(StructJ arg0, StructJ arg1, StructJ arg2, StructJ arg3);

		[Test]
		public static void TestBasics()
		{
			//return;

			StructA sa0 = default;
			sa0.mA = 10;
			StructA sa1 = default;
			sa1.mA = 11;

			StructB sb0 = default;
			sb0.mA = 20;
			StructB sb1 = default;
			sb1.mA = 21;

			StructC sc0 = default;
			sc0.mA = 30;
			StructC sc1 = default;
			sc1.mA = 31;

			StructD sd0 = default;
			sd0.mA = 40;
			StructD sd1 = default;
			sd1.mA = 41;

			StructE se0 = default;
			se0.mA = 50;
			StructE se1 = default;
			se1.mA = 51;

			StructF sf0 = default;
			sf0.mA = 60;
			StructF sf1 = default;
			sf1.mA = 61;

			StructG sg0 = default;
			sg0.mA = 70;
			StructG sg1 = default;
			sg1.mA = 71;

			StructH sh0 = default;
			sh0.mA = 80;
			StructH sh1 = default;
			sh1.mA = 81;

			StructI si0 = default;
			si0.mA = 90;
			StructI si1 = default;
			si1.mA = 91;

			Test.Assert(Func0(12, 34) == 3412);

			Test.Assert(Func1A(sa0, sa1, 12) == 121110);
			Test.Assert(sa0.MethodA0(12) == 1012);
			Test.Assert(sa0.MethodA1(sa1, 12).mA == 33);
			Test.Assert(Func2A(sa0, 12).mA == 22);

			Test.Assert(Func1B(sb0, sb1, 12) == 122120);
			Test.Assert(sb0.MethodB0(12) == 2012);
			Test.Assert(sb0.MethodB1(sb1, 12).mA == 53);
			Test.Assert(Func2B(sb0, 12).mA == 32);

			Test.Assert(Func1C(sc0, sc1, 12) == 123130);
			Test.Assert(sc0.MethodC0(12) == 3012);
			Test.Assert(sc0.MethodC1(sc1, 12).mA == 73);
			Test.Assert(Func2C(sc0, 12).mA == 42);

			Test.Assert(Func1D(sd0, sd1, 12) == 124140);
			Test.Assert(sd0.MethodD0(12) == 4012);
			Test.Assert(sd0.MethodD1(sd1, 12).mA == 93);
			Test.Assert(Func2D(sd0, 12).mA == 52);

			Test.Assert(Func1E(se0, se1, 12) == 125150);
			Test.Assert(se0.MethodE0(12) == 5012);
			Test.Assert(se0.MethodE1(se1, 12).mA == 113);
			Test.Assert(Func2E(se0, 12).mA == 62);

			Test.Assert(Func1F(sf0, sf1, 12) == 126160);
			Test.Assert(sf0.MethodF0(12) == 6012);
			Test.Assert(sf0.MethodF1(sf1, 12).mA == (int8)133);
			Test.Assert(Func2F(sf0, 12).mA == 72);

			Test.Assert(Func1G(sg0, sg1, 12) == 127170);
			Test.Assert(sg0.MethodG0(12) == 7012);
			Test.Assert(sg0.MethodG1(sg1, 12).mA == (int8)153);
			Test.Assert(Func2G(sg0, 12).mA == 82);

			Test.Assert(Func1H(sh0, sh1, 12) == 128180);
			Test.Assert(sh0.MethodH0(12) == 8012);
			Test.Assert(sh0.MethodH1(sh1, 12).mA == 173);
			Test.Assert(Func2H(sh0, 12).mA == 92);

			Test.Assert(Func1I(si0, si1, 12) == 129190);
			Test.Assert(si0.MethodI0(12) == 9012);
			Test.Assert(si0.MethodI1(si1, 12).mA == (int8)193);
			Test.Assert(Func2I(si0, 12).mA == 102);

			StructJ sj0;
			sj0.mPtr = "ABC";
			sj0.mLength = 3;
			StructJ sj1;
			sj1.mPtr = "DEFG";
			sj1.mLength = 4;
			StructJ sj2;
			sj2.mPtr = "HIJKL";
			sj2.mLength = 5;
			StructJ sj3;
			sj3.mPtr = "MNOPQR";
			sj3.mLength = 6;
			var sjRet = Func4J(sj0, sj1, sj2, sj3);
			Test.Assert(sjRet.mLength == 6050403);
		}
	}
}
