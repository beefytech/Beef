#pragma warning disable 168

using System;

namespace Tests
{
	class Properties
	{
		struct StructA
		{
			public int mA = 111;

			public this()
			{
			}

			public this(int a)
			{
				mA = a;
			}
		}

		struct StructB
		{
			public StructA B { get; set mut; }

			int mZ = 9;

			public this()
			{
				B = .();
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

		[Test]
		public static void TestBasics()
		{
			StructB sb = .();
			StructA sa = sb.B;
			Test.Assert(sa.mA == 111);
			sb.B = .(222);
			sa = sb.B;
			Test.Assert(sa.mA == 222);

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
		}
	}
}
