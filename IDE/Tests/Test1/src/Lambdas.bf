#pragma warning disable 168

namespace IDETest
{
	class LambdaTester
	{
		public int mA = 123;
		public int mB = 234;

		public void Test1()
		{
			int outerVar = 88;

			delegate int(int argA0) dlg = scope (argA1) =>
			{
				int a = mA;
				int b = outerVar;
				return 222;
			};

			//LambdaTest_Test1
			dlg(99);
		}

		public static void Test()
		{
			LambdaTester lt = scope .();
			lt.Test1();
		}
	}
}
