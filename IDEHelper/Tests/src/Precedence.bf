using System;

namespace Tests
{
	class Precedence
	{
		[Test]
		static void Test()		   
		{
			int a = -1;
			bool b = true;
			bool c = true;
			if (a == -1 && b && c)
				a = 2;
			Test.Assert(a == 2);
			Test.Assert(1*10 + 2 + 3 + 5*100*10 + 6*10000 == 65015);
		}
	}
}
