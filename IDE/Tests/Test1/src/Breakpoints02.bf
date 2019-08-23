#pragma warning disable 168

namespace IDETest
{
	class Breakpoints02
	{
		static void MethodA()
		{
			int a = 0;
			for (int i < 3)
			{
				a++;
				//MethodA_1
				a++;
				//MethodA_2
				a++;
				a++;
			}
		}

		public static void Test()
		{
			//Test_Start
			MethodA();
		}
	}
}
