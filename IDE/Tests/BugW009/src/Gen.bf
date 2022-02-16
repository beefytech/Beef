using System;

namespace Bug
{
	class Gen
	{
		public static Type Get()
		{
			//*ClassA
			return typeof(ClassA);
			/*@*/

			/*ClassB
			return typeof(ClassB);
			*/
		}
	}
}