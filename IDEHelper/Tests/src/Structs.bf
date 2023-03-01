#pragma warning disable 168

using System;
using System.Collections;

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
			public int8 mA;
			public int32 mB;
			public int8 mC;

			public int A
			{
				get mut
				{
					return mA;
				}
			}

			public int B
			{
				get
				{
					return mB;
				}
			}
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

		[CRepr]
		struct Color
		{
			public uint8 mR, mG, mB, mA;

			public uint8 R
			{
				get mut
				{
					return mR;
				}
			}

			public uint8 G
			{
				get
				{
					return mG;
				}
			}
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

		[Packed]
		struct StructJ
		{
			int8 mA;
			int32 mB;
		}

		struct StructK
		{
			Dictionary<int, StructK> dict;
		}

		struct StructL : this(int a, int b);

		struct StructM : this(readonly int a, readonly int b)
		{
			public int c;
			this
			{
				c = 100;
			}
		}

		public struct StructN
		{
			public int mA = 123;
		}

		public struct StructO
		{
			public StructN mA;
		}

		public static StructN GetStructN()
		{
			var sn = StructN();
			var so = scope StructO();
			so.mA = sn;
			return sn;
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

			sb0 = StructB { mA = 2 };
			Test.Assert(sb0.mA == 2);
			Test.Assert(sb0.mB == 0);

			sb0 = .{ mA = 3, mB = 4 };
			Test.Assert(sb0.mA == 3);
			Test.Assert(sb0.mB == 4);

			StructL sl = .(12, 23);
			Test.Assert(sl.a == 12);
			Test.Assert(sl.b == 23);

			StructM sm = .(12, 23);
			[IgnoreErrors]
			{
				sm.a += 100;
				sm.b += 100;
				sm.c += 100;
			}
			Test.Assert(sm.a == 12);
			Test.Assert(sm.b == 23);
			Test.Assert(sm.c == 200);

			StructN sn = GetStructN();
			Test.Assert(sn.mA == 123);

			List<StructD> list = scope .();
			for (int i < 10)
				list.Add(default);
			var ptr0 = &list[0];
			var ptr9 = &list[9];

			int count = ptr9 - ptr0;
			Test.Assert(count == 9);

			var ptr9B = ptr0 + 9;
			count = ptr9B - ptr0;
			Test.Assert(ptr9 == ptr9B);
			Test.Assert(count == 9);
		}

		[Align(16)]
		struct StructP
		{
			int32 mA;
			[Align(8)]
			int8 mB;
		}

		[Align(16), Ordered]
		struct StructQ
		{
			int32 mA;
			[Align(8)]
			int8 mB;
		}

		[Packed(2), Ordered]
		struct StructR
		{
			int8 mA;
			int32 mB;
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

			Test.Assert(sizeof(StructJ) == 5);
			Test.Assert(alignof(StructJ) == 1);
			Test.Assert(strideof(StructJ) == 5);

			Test.Assert(sizeof(StructP) == 8);
			Test.Assert(alignof(StructP) == 16);
			Test.Assert(strideof(StructP) == 16);

			Test.Assert(sizeof(StructQ) == 9);
			Test.Assert(alignof(StructQ) == 16);
			Test.Assert(strideof(StructQ) == 16);

			Test.Assert(sizeof(StructR) == 6);
			Test.Assert(alignof(StructR) == 2);
			Test.Assert(strideof(StructR) == 6);
		}

		public int Test<T>(T val)
		{
			return 11;
		}

		static int Test<T, T2>(T val) where T : Span<T2>
		{
			return 22;
		}

		[Test]
		static void TestStringView()
		{
			StringView sv = "Hey";
			Span<char8> span = sv;
			Test.Assert(Test(sv) == 22);
		}

		[Test]
		static void TestProperties()
		{
			StructC sc = .();
			sc.mA = 11;
			sc.mB = 22;
			sc.mC = 33;
			Test.Assert(sc.A == 11);
			Test.Assert(sc.B == 22);

			Color clr;
			clr.mR = 10;
			clr.mG = 20;
			clr.mB = 30;
			clr.mA = 40;
			Test.Assert(clr.R == 10);
			Test.Assert(clr.G == 20);
		}

		struct SplitStruct : this(int32 mA, float mB)
		{
			int16 mC = 123;
		}

		[Test]
		static void TestSplitList()
		{
			SplitList<SplitStruct> sl = scope .();
			sl.Add(.(1, 2));
			sl.Add(.(3, 4));

			Test.Assert(sl[0].mA == 1);
			Test.Assert(sl[0].mB == 2);
			Test.Assert(sl[0].mC == 123);
			Test.Assert(sl[1].mA == 3);
			Test.Assert(sl[1].mB == 4);
			Test.Assert(sl[1].mC == 123);
			Test.Assert(sl.Data.mA[0] == 1);
			Test.Assert(sl.Data.mA[1] == 3);
			Test.Assert(sl.Data.mB[0] == 2);
			Test.Assert(sl.Data.mB[1] == 4);
			Test.Assert(sl.Data.mC[0] == 123);
			Test.Assert(sl.Data.mC[1] == 123);

			SplitStruct s = sl[0];
			Test.Assert(s == .(1, 2));

			for (var entry in sl)
			{
				Test.Assert(entry.mA == @entry.Index * 2 + 1);
			}
		}
	}
}
