#pragma warning disable 168

using System;

namespace Bug
{
	class Program
	{
		static void Main()
		{
			//*Main_Start
			SDL2.gApp = null; //FAIL
			/*@*/
		}
	}
}
