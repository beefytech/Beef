#pragma warning disable 168

using System;

namespace Tests
{
	class Properties
	{
		struct StructA
		{
			public int mA = 111;

			public static int sAutoProp { get; set; }

			public this()
			{
			}

			public this(int a)
			{
				mA = a;
			}

			public static void IncAutoProp()
			{
				Test.Assert(sAutoProp == 0);
				int v = ++sAutoProp;
				Test.Assert(v == 1);
				Test.Assert(sAutoProp == 1);
				int v2 = sAutoProp++;
				Test.Assert(v == 1);
				Test.Assert(sAutoProp == 2);
			}
		}

		struct StructB
		{
			public StructA B { get; set mut; }
			public ref StructA B2 { get mut; set mut; }

			int mZ = 9;

			public this()
			{
				B = .();
#unwarn
				B.mA += 1000;

				StructA sa = .();
				sa.mA += 2000;
				B2 = sa;
				B2.mA += 3000;
			}
		}

		struct StructC
		{
			public StructA B { get; }

			public int C => 123;
			[SkipCall]
			public int D => 123;

			public int E
			{
				get
				{
					return 1;
				}

				get mut
				{
					return 2;
				}
			}

			int mZ = 9;

			public int GetVal()
			{
				return 3;
			}

			public int GetVal() mut
			{
				return 4;
			}

			public this()
			{
				B = .();
			}
		}

		struct StructD
		{
			public int Val => 123;
		}

		struct StructE : StructD
		{
			
		}

		class ClassB
		{
			public StructA B { get; set; }
			public int IVal { get => 1; }
			public virtual int IVal2 { get => 2; }

			int mZ = 9;

			public this()
			{
			}
		}

		class ClassC : ClassB
		{
			public new int IVal { get => base.IVal + 100; }
			public override int IVal2 { get => base.IVal2 + 100; }
		}

		abstract class ClassD
		{
			public int32 Val { get; set; }
		}

		class ClassE : ClassD
		{

		}

		static void MethodC<T>(T val) where T : ClassD
		{
			val.Val = 999;
		}

		static int MethodD<T>(T val) where T : ClassD
		{
			return val.Val;
		}

		static void TestVal(int val)
		{

		}

		static void TestVal(float val)
		{

		}

		[Test]
		public static void TestBasics()
		{
			StructB sb = .();
			StructA sa = sb.B;
			StructA sa2 = sb.B2;
			Test.Assert(sa.mA == 111);
			sb.B = .(222);
			sa = sb.B;
			Test.Assert(sa.mA == 222);
			Test.Assert(sa2.mA == 5111);

			StructC sc = default;
			Test.Assert(sc.C == 123);
			Test.Assert(sc.D == 0);
			let sc2 = sc;
			Test.Assert(sc.E == 2);
			Test.Assert(sc.GetVal() == 4);
			Test.Assert(sc2.E == 1);
			Test.Assert(sc2.GetVal() == 3);

			ClassB cb = scope .();
			sa = cb.B;
			Test.Assert(sa.mA == 0);
			cb.B = .(333);
			sa = cb.B;
			Test.Assert(sa.mA == 333);

			ClassC cc = scope .();
			Test.Assert(cc.IVal == 101);
			Test.Assert(cc.IVal2 == 102);
			ClassB cb2 = cc;
			Test.Assert(cb2.IVal == 1);
			Test.Assert(cb2.IVal2 == 102);

			ClassE ce = scope .();
			MethodC(ce);
			Test.Assert(ce.Val == 999);
			Test.Assert(MethodD(ce) == 999);

			StructA.IncAutoProp();
			int ap = ++StructA.sAutoProp;
			Test.Assert(ap == 3);
			int ap2 = StructA.sAutoProp++;
			Test.Assert(ap2 == 3);
			Test.Assert(StructA.sAutoProp == 4);

			StructE* se = scope .();
			StructE*[2] seArr = .(se, se);
			Test.Assert((seArr[0].Val + seArr[1].Val) == 123*2);
		}
	}
}
