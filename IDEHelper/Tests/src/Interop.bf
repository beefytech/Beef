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
			public static extern int32 sVal;

			[LinkName(.CPP)]
			public extern int32 MethodA0(int32 arg0) mut;
			[LinkName(.CPP)]
			public extern StructA MethodA1(StructA sa, int32 arg0) mut;
			[LinkName(.CPP)]
			[return: MangleConst]
			public extern ref float MethodA2([MangleConst]ref float val);
			[LinkName(.CPP)]
			[return: MangleConst]
			public extern ref float MethodA3(in float val);
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

		[CRepr]
		public struct StructK
		{
			public float mX;
			public float mY;
		}

		[CRepr]
		struct StructL
		{
			public float mX;
			public float mY;
			public float mZ;
		}

		[CRepr]
		struct StructM
		{
			public float mX;
			public float mY;
			public float mZ;
			public float mW;
		}

		[CRepr]
		struct StructN
		{
			public float mX;
			public float mY;
			public float mZ;
			public float mW;
			public float mU;
		}

		[CRepr]
		struct StructO
		{
			public float mX;
			public int32 mY;
		}

		[CRepr]
		struct StructP
		{
			public float mX;
			public float mY;
			public int32 mZ;
		}

		[CRepr]
		struct StructQ
		{
			public float mX;
			public float mY;
			public int32 mZ;
			public int32 mW;
		}

		[CRepr]
		struct StructR
		{
			public double mX;
			public double mY;
		}

		[CRepr]
		struct StructS
		{
			public float mX;
			public double mY;
		}

		[CRepr]
		struct StructT
		{
			public double mX;
			public double mY;
			public double mZ;
		}

		[CRepr]
		struct StructU
		{
			public StructK mK;
		}

		[CRepr]
		struct StructV
		{
			public int64 mX;
			public int16 mY;
		}

		[CRepr]
		struct StructW
		{
			public float mX;
		}

		[LinkName(.C)]
		public static extern int32 Func0(int32 a, int32 b);
		[LinkName(.C)]
		public static extern int32 Func0K(int32 a, StructK b);
		[LinkName(.C)]
		public static extern int32 Func0L(int32 a, StructL b);
		[LinkName(.C)]
		public static extern int32 Func0M(int32 a, StructM b);
		[LinkName(.C)]
		public static extern int32 Func0N(int32 a, StructN b);
		[LinkName(.C)]
		public static extern int32 Func0O(int32 a, StructO b);
		[LinkName(.C)]
		public static extern int32 Func0P(int32 a, StructP b);
		[LinkName(.C)]
		public static extern int32 Func0Q(int32 a, StructQ b);
		[LinkName(.C)]
		public static extern int32 Func0R(int32 a, StructR b);
		[LinkName(.C)]
		public static extern int32 Func0S(int32 a, StructS b);
		[LinkName(.C)]
		public static extern int32 Func0T(int32 a, StructT b);
		[LinkName(.C)]
		public static extern int32 Func0U(int32 a, StructU b);
		[LinkName(.C)]
		public static extern int32 Func0V(int32 a, StructV b);
		[LinkName(.C)]
		public static extern int32 Func0W(int32 a, StructW b);
		[LinkName(.C)]
		public static extern int32 Func0KM(StructK a, StructM b, StructK c);

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

		[LinkName(.C)]
		public static extern  double Func5(float[2] v0, float[3] v1);

		static int32 LocalFunc0K(int32 a, StructK b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0L(int32 a, StructL b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0M(int32 a, StructM b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0N(int32 a, StructN b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0O(int32 a, StructO b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0P(int32 a, StructP b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0Q(int32 a, StructQ b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0R(int32 a, StructR b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0S(int32 a, StructS b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0T(int32 a, StructT b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0U(int32 a, StructU b) => a + (int32)b.mK.mX * 100 + (int32)b.mK.mY * 10000;
		static int32 LocalFunc0V(int32 a, StructV b) => a + (int32)b.mX * 100 + (int32)b.mY * 10000;
		static int32 LocalFunc0W(int32 a, StructW b) => a + (int32)b.mX * 100;
		static int32 LocalFunc0KM(StructK a, StructM b, StructK c) => (int32)a.mX + (int32)b.mX * 100 + (int32)c.mX * 1000;

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

			StructK sk = .() { mX = 3, mY = 4};
			StructK sk2 = .() { mX = 11, mY = 12 };
			StructL sl = .() { mX = 3, mY = 4};
			StructM sm = .() { mX = 5, mY = 6, mZ = 7, mW = 8 };
			StructN sn = .() { mX = 3, mY = 4};
			StructO so = .() { mX = 3, mY = 4};
			StructP sp = .() { mX = 3, mY = 4};
			StructQ sq = .() { mX = 3, mY = 4};
			StructR sr = .() { mX = 3, mY = 4};
			StructS ss = .() { mX = 3, mY = 4};
			StructT st = .() { mX = 3, mY = 4};
			StructU su = .() { mK = .(){mX = 3, mY = 4}};
			StructV sv = .() { mX = 3, mY = 4};
			StructW sw = .() { mX = 3 };

			void StartTest(String str)
			{
				//Console.WriteLine(str);
			}

			Test.Assert(StructA.sVal == 1234);
			float f = 123;
			Test.Assert(sa0.MethodA2(ref f) == 123);
			Test.Assert(sa0.MethodA3(f) == 123);

			StartTest("Func0");
			Test.Assert(Func0(12, 34) == 3412);
			StartTest("Func0K");
			Test.Assert(Func0K(12, sk) == 40312);
			StartTest("Func0L");
			Test.Assert(Func0L(12, sl) == 40312);
			StartTest("Func0M");
			Test.Assert(Func0M(12, sm) == 60512);
			StartTest("Func0N");
			Test.Assert(Func0N(12, sn) == 40312);
			StartTest("Func0O");
			Test.Assert(Func0O(12, so) == 40312);
			StartTest("Func0P");
			Test.Assert(Func0P(12, sp) == 40312);
			StartTest("Func0Q");
			Test.Assert(Func0Q(12, sq) == 40312);
			StartTest("Func0R");
			Test.Assert(Func0R(12, sr) == 40312);
			StartTest("Func0S");
			Test.Assert(Func0S(12, ss) == 40312);
			StartTest("Func0T");
			Test.Assert(Func0T(12, st) == 40312);
			StartTest("Func0U");
			Test.Assert(Func0U(12, su) == 40312);
			StartTest("Func0V");
			Test.Assert(Func0V(12, sv) == 40312);
			StartTest("Func0W");
			Test.Assert(Func0W(12, sw) == 312);
			StartTest("Func0KM");
			Test.Assert(Func0KM(sk, sm, sk2) == 11503);

			StartTest("LocalFunc0K");
			Test.Assert(LocalFunc0K(12, sk) == 40312);
			StartTest("LocalFunc0L");
			Test.Assert(LocalFunc0L(12, sl) == 40312);
			StartTest("LocalFunc0M");
			Test.Assert(LocalFunc0M(12, sm) == 60512);
			StartTest("LocalFunc0N");
			Test.Assert(LocalFunc0N(12, sn) == 40312);
			StartTest("LocalFunc0O");
			Test.Assert(LocalFunc0O(12, so) == 40312);
			StartTest("LocalFunc0P");
			Test.Assert(LocalFunc0P(12, sp) == 40312);
			StartTest("LocalFunc0Q");
			Test.Assert(LocalFunc0Q(12, sq) == 40312);
			StartTest("LocalFunc0R");
			Test.Assert(LocalFunc0R(12, sr) == 40312);
			StartTest("LocalFunc0S");
			Test.Assert(LocalFunc0S(12, ss) == 40312);
			StartTest("LocalFunc0T");
			Test.Assert(LocalFunc0T(12, st) == 40312);
			StartTest("LocalFunc0U");
			Test.Assert(LocalFunc0U(12, su) == 40312);
			StartTest("LocalFunc0V");
			Test.Assert(LocalFunc0V(12, sv) == 40312);
			StartTest("LocalFunc0W");
			Test.Assert(LocalFunc0W(12, sw) == 312);
			StartTest("Func0KM");
			Test.Assert(LocalFunc0KM(sk, sm, sk2) == 11503);

			StartTest("Func1A");
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

			var val5 = Func5(.(1, 2), .(3, 4, 5));
			Test.Assert(Math.Round(val5) == 54321);
		}
	}
}
