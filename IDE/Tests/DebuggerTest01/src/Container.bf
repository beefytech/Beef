using System.Collections.Generic;
using System;

namespace Test
{
	class Container
	{
		static void TestB()
		{
			List<int> list = scope .();
			for (int i < 5)
				list.Add(i);

			//TestB
			list.Remove(2);
		}

		static void TestA()
		{
			List<String> list = scope .();
			list.Add("A");
			list.Add("AB");
			list.Add("ABC");
			list.Add("ABCD");
			list.Add("ABCDE");
			list.Add("ABCDEF");

			for (int i < 100)
			{
				list.Add(scope:: String()..AppendF("Str{}", i));
			}

			//TestA
			list.RemoveAt(3);
			TestB();
		}

		public static void Test()
		{
			TestA();
		}
	}
}
