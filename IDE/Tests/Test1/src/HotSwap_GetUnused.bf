#pragma warning disable 168

namespace IDETest
{
	class HotSwap_GetUnusued
	{
		class ClassA
		{
			public void MethodA()
			{

			}

			public int MethodB()
			{
				return 123;
			}
		}

		public static void DoTest()
		{
			ClassA ca = scope .();
			ca.MethodA();
			/*DoTest_MethodB
			int val = ca.MethodB();
			*/
		}

		public static void Test()
		{
			int a = 123;
			//Test
			DoTest();
		}
	}
}
