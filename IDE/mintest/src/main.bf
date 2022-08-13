
//zab1234

// Zoop
//using IDE;
using System;
//using System.Threading;
using System.Collections;
using System.Diagnostics;
using System.Collections;
using System.Collections;
using System.Threading;

namespace Hey.Dude.Bro
{
	struct Zoff
	{
		public void operator<<=(float a)
		{

		}
	}

	struct Color
	{
		public float r, g, b, a;
	}

	class TestClass
	{
		/*static void TestFunc()
		{
			Zonk(=> LocalMethod);
			void LocalMethod()
			{
				int a = zzz;
			}
		}
	
		static void Zonk<T>(T dlg) where T : delegate void()
		{
	
		}*/

		//private const uint16[] DebugIndexes = scope uint16[](0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 6, 5, 6, 7);

		

		[Import(@"C:\Beef\BeefTools\TestDLL\x64\Debug\TestDLL.dll"), LinkName("Test2")]
		public static extern void Test2(int32 a, int32 b, int32 c, int32 d, function Color(int32 a, int32 b) func);

		public static Color GetColor(int32 a, int32 b)
		{
			Color c;
			c.r = 1.2f;
			c.g = 2.3f;
			c.b = 3.4f;
			c.a = 4.5f;
			return c;
		}

		public static int Main(String[] args)
		{
			Test2(1, 2, 3, 4, => GetColor);

			//Blurg.Hey();
			return 1;
		}
	}
}
