#pragma warning disable 168

using System.Diagnostics;

namespace MemoryBreakTest
{
	class Tester
	{
		public int mA;
	}

	class Program
	{
		public static void Main()
		{
			Tester t = scope .();
			Debug.Break();

			int a = 0;
			t.mA++;
			a++;
			t.mA++;
			a++;
		}
	}
}
