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

			int mZ = 9;

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

		[Test]
		public static void TestBasics()
		{
			StructB sb = .();
			StructA sa = sb.B;
			Test.Assert(sa.mA == 111);
			sb.B = .(222);
			sa = sb.B;
			Test.Assert(sa.mA == 222);

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
		}
	}
}
