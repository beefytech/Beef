using System;

namespace Bug
{
	class Gen
	{
		public static Type Get()
		{
			//*Void
			return typeof(void);
			/*@*/

			/*String
			return typeof(String);
			*/
		}
	}
}