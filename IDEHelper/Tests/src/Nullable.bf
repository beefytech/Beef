#pragma warning disable 168

using System;
using System.Collections;

namespace Tests
{
	class Nullable
	{
		class ClassA
		{
			public int mA = 100;

			public int Prop
			{
				set
				{
					mA = value;
				}
			}

			public int GetVal()
			{
				return 123;
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			ca?.Prop = ca.GetVal();
			Test.Assert(ca.mA == 123);
			ca = null;
			ca?.Prop = ca.GetVal();
		}

		[Test]
		public static void TestPrimitives()
		{
			float? fn = 9.0f;
			int? intn = 100;
			int? intn2 = null;

			let fn2 = fn + intn;
			Test.Assert(fn2 == 109);

			let fn3 = fn + intn2;
			Test.Assert(fn3 == null);

			int i = intn ?? 200;
			Test.Assert(i == 100);

			i = intn2 ?? (int16)200;
			Test.Assert(i == 200);

			i = 300;
			Test.Assert(intn.TryGetValue(ref i));
			Test.Assert(i == 100);

			Test.Assert(!intn2.TryGetValue(ref i));
			Test.Assert(i == 100);
		}

		public static T DoAdd<T>(T lhs, T rhs) where T : operator T + T
		{
			return lhs + rhs;
		}

		[Test]
		public static void TestOperators()
		{
			int? iNull = null;
			bool? bNull = null;

			Test.Assert(!(iNull == 0));
			Test.Assert(!(iNull <= 0));
			Test.Assert(!(iNull >= 0));

			Test.Assert(!(bNull == false));
			Test.Assert(!(bNull == true));
			Test.Assert(bNull != true);
			Test.Assert(bNull != false);

			iNull = 100;
			bNull = false;

			Test.Assert(iNull >= 50);
			Test.Assert(!(iNull >= 150));
			Test.Assert(iNull < 150);
			Test.Assert(!(iNull < 50));
			Test.Assert(iNull == 100);
			Test.Assert(iNull != 99);
			Test.Assert(!(iNull != 100));

			Test.Assert(DoAdd(iNull, iNull) == 200);
			Test.Assert(DoAdd(iNull, null) == null);

			String str = "Abc";
			StringView? svn = str;

			iNull = null;
			int? iNull2 = 123;
			List<int> l = null;
			List<int> l2 = scope .();

			int a = iNull2 ?? l.Count;
			int b = iNull ?? l2.Count;
			var c = iNull ?? iNull2;

			Test.Assert(a == 123);
			Test.Assert(b == 0);
			Test.Assert(typeof(decltype(c)) == typeof(int?));

			iNull ??= iNull2;
			Test.Assert(iNull == 123);
		}
	}
}
