namespace IDETest
{
	class Methods
	{
		class ClassA
		{
			public int TEST_MethodA()
			{
				return 200;
			}

			public int TEST_MethodB()
			{
				return 201;
			}

			/*ClassA_MethodC
			public int TEST_MethodC()
			{
				return 202;
			}
			*/

			public static int TEST_StaticMethodA()
			{
				return 100;
			}

			public static int TEST_StaticMethodB()
			{
				return 101;
			}

			/*ClassA_StaticMethodC
			public static int TEST_StaticMethodC()
			{
				return 102;
			}
			*/
		}

		public static void DoTest()
		{
			/*DoTest_Body
			ClassA ca = scope .();
			ca.TEST_MethodB();
			ca.TEST_MethodC();
			ClassA.TEST_StaticMethodB();
			ClassA.TEST_StaticMethodC();
			*/
		}

		public static void Test()
		{
			//Test_Start
			ClassA ca = scope .();
			ca.TEST_MethodA();
			ClassA.TEST_StaticMethodA();
			DoTest();
		}
	}
}
