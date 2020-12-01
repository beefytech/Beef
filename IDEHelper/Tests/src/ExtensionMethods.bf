using System;
using System.Collections;

using static Tests.ExtensionMethods.ClassB;

namespace Tests
{
	class ExtensionMethods
	{
		public static int Remove(this String str, float val, float val2)
		{
			return 123;
		}

		class ClassA
		{
			public static void Test()
			{
				Test.Assert("Abc".Remove(1.2f, 2.3f) == 123);
			}
		}
		
		public class ClassB
		{
			public static int Flop(this String str, int a)
			{
				return a + 1000;
			}

		}

		[Test]
		public static void TestBasics()
		{
			ClassA.Test();

			Test.Assert("Test".Flop(234) == 1234);

			List<int> iList = scope .();
			iList.Add(3);
			iList.Add(20);
			iList.Add(100);

			Test.Assert(iList.Total() == 123);

			float a = 1.2f;
			float b = 2.3f;
			Test.Assert(a.CompareIt(b) < 0);
		}
	}

	static
	{
		public static int CompareIt<T>(this T self, T other)
			where bool: operator T < T
		{
			if(self < other)
				return -1;
			else if(self > other)
				return 1;

			return 0;
		}

		public static T Total<T>(this List<T> list) where T : operator T + T
		{
			T total = default;
			for (let val in list)
				total += val;
			return total;
		}
	}
}

