#pragma warning disable 168

using System;
using System.Collections;

namespace Bug
{
	class Program
	{
		public static void CallExtra()
		{
			/*CallExtra_Call
			int val = Extra();
			*/
		}

		static void Main()
		{
			int ig = 111;
			//Test_Start
			CallExtra();
		}
	}
}
