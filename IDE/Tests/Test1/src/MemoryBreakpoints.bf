#pragma warning disable 168

namespace IDETest
{
	class MemoryBreakpointTester
	{
		int mA;

		public static void Test()
		{
			//MemoryBreakpointTester_Test
			MemoryBreakpointTester mbt = scope .();

			int a = 0;
			mbt.mA++;
			a++;
			a++;
			mbt.mA++;
			a++;
			a++;
		}
	}
}
