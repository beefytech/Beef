#pragma warning disable 168

namespace IDETest
{
	class Assembly
	{
		public static int IncVal(ref int val)
		{
			val++;
			return val;
		}

		public static void Test1()
		{
			int a = 0;
			int d = IncVal(ref a) + IncVal(ref a) + IncVal(ref a);
		}

		public static void Test()
		{
			//AssemblyTester_Test
			Test1();
		}
	}
}
