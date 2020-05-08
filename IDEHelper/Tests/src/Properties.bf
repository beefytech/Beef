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

			int mZ = 9;

			public this()
			{
			}
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
		}
	}
}
