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

			public int BaseIndex
			{
				get
				{
					return base[2];
				}
			}
		}

		class ClassC : ClassB
		{

		}

		struct StructA
		{
			public int mA;

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

			public int this[params int[] indices]
			{
				get mut
				{
					int total = 0;
					for (var i in indices)
						total += i;
					mA += total;
					return total;
				}

				set mut
				{
					for (var i in indices)
						mA += i;
					mA += value * 1000;
				}
			}
		}

		class Foo
		{
		    public virtual int this[int i] => i;
		}

		class Bar : Foo
		{
		    public override int this[int i] => i*i;
		    public Span<int> this[Range r] => default;
		}

		[Test]
		public static void TestBasics()
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
			value = cb.BaseIndex;
			Test.Assert(value == 123);

			StructA sa = default;
			let sa2 = sa;

			Test.Assert(sa[0] == 2);
			Test.Assert(sa2[0] == 1);

			sa[3, 4, 5] = 9;
			Test.Assert(sa.mA == 9012);
			int a = sa[3, 4, 5];
			Test.Assert(a == 12);
			Test.Assert(sa.mA == 9024);

			Bar bar = scope .();
			Test.Assert(bar[3] == 9);
		}
	}
}
