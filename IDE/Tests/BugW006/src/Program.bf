#pragma warning disable 168

using System;
using System.Collections;

namespace Bug
{
	class Program
	{
		//*Test_Definition
		public class Test<T, K> where K: enum
		{
			typealias x = function void (T t);
			//Test_Y
			typealias y = (K);
		}
		/*@*/


		public static int Main(String[] args)
		{
			return 0;
		}
	}
}
