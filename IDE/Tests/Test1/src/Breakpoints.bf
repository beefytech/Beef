#pragma warning disable 168

namespace IDETest
{
	class Breakpoints
	{
		public static void Recurse(int a)
		{
			int b = 234;
			//Recurse_C
			int c = 345;

			if (a == 10)
				return;

			Recurse(a + 1);
			int d = 100 + a;
		}

		public static void Test()
		{
			//BreakpointTester_Test
			int a = 0;
			int b = 0;

			while (a < 20)
			{
				//BreakpointTester_LoopA
				a++;
			}

			//BreakpointTester_Recurse
			Recurse(0);
		}
	}
}
