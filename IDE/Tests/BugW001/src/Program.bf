#pragma warning disable 168

using System;

namespace BugW001
{
	class Program
	{
		static void Main()
		{
			//Main_Start
			Result<void*> result = .();
			/*Result_Get
			void* vp = result;
			*/
		}
	}
}
