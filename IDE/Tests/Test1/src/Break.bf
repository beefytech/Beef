using System.Threading;

namespace IDETest
{
	class Break
	{
		public static void Infinite()
		{
			while (true)
			{
				//Thread.Sleep(100);
			}
		}

		public static void Test()
		{
			//Test_Start
			int a = 0;
			a++;
			a++;
			a++;
			if (a == -1)
				Infinite();
		}
	}
}
