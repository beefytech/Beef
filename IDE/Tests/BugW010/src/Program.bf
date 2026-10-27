#pragma warning disable 168

using System;

namespace Bug
{
	class Program
	{
		// Only reachable through GetValue, so removing the call below drops this whole module from the executable
		class Source
		{
			public static int GetValue() => 42;
		}

		static int GetTotal()
		{
			int total = 0;
			/*UseSource
			total = Source.GetValue();
			*/
			return total;
		}

		public static void Main()
		{
			//Start
			int total = GetTotal();
		}
	}
}
