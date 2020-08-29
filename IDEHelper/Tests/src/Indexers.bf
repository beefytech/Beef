using System;

namespace Tests
{
	class Indexers
	{
		class ClassA
		{
			public virtual int this[int index]
			{
				get
				{
					return 123;
				}
			}
		}

		class ClassB : ClassA
		{
			public override int this[int index]
			{
				get
				{
					return 234;
				}
			}
		}

		class ClassC : ClassB
		{

		}

		struct StructA
		{
			int mA;

			public int this[int index]
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
		}

		[Test]
		public static void Hey()
		{
			ClassB cc = scope ClassC();
			ClassB cb = cc;
			ClassA ca = cb;
			int value = cc[0];
			Test.Assert(value == 234);
			value = cb[0];
			Test.Assert(value == 234);
			value = ca[0];
			Test.Assert(value == 234);

			StructA sa = default;
			let sa2 = sa;
			Test.Assert(sa[0] == 2);
			Test.Assert(sa2[0] == 1);
		}
	}
}
