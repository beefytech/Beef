#pragma warning disable 4554
using System;

namespace Tests
{
	class Precedence
	{
		[Test]
		static void Test()		   
		{
			{
				int a = -1;
				bool b = true;
				bool c = true;
				if (a == -1 && b && c)
					a = 2;
				Test.Assert(a == 2);
				Test.Assert(1*10 + 2 + 3 + 5*100*10 + 6*10000 == 65015);
			}

			{
				int a = 2;
				int c = 3;
				int e = 4;
				int g = 5;
				int b = 6;
				int d = 7;
				int f = 8;
				int h = 9;

				int val1 = a * b + c * d + e * f + g * h;
				Test.Assert(val1 == 110);
				int val2 = a * (int)b + c * d + e * f + g * h;
				Test.Assert(val2 == 110);
				int val3 = a | b * (int)a + b * c + d;
				Test.Assert(val3 == 39);
			}
		}
	}
}
