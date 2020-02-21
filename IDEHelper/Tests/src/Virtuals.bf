#pragma warning disable 168

using System;

namespace Tests
{
	class Virtuals
	{
		class ClassA
		{
			public virtual int Method0(int a = 111)
			{
				return a;
			}
		}

		class ClassB : ClassA
		{
			public override int Method0(int a = 222)
			{
				return a + 1000;
			}
		}

		class ClassC : ClassB
		{
			public override int Method0(int a)
			{
				return a + 2000;
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope ClassA();
			Test.Assert(ca.Method0() == 111);

			ClassB cb = scope ClassB();
			Test.Assert(cb.Method0() == 1222);

			cb = scope ClassC();
			Test.Assert(cb.Method0() == 2222);
		}
	}
}
