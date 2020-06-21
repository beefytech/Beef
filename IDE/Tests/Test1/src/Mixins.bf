using System;

#pragma warning disable 168

namespace IDETest
{
	class Mixins
	{
		class ClassA
		{
			public String mStr ~ delete _;

			public mixin Zorf(int a)
			{
				a + mStr.Length
			}

			public mixin Zorf(int a, int b)
			{
				a + b + mStr.Length
			}
		}

		public static mixin MixA(int a)
		{
			int b = a + 10;
			b + 100
		}

		public static mixin MixB(int a)
		{
			int c = MixA!(a + 10000);
			int d = MixA!(a + 20000);
			int e = MixA!(a + 30000);
		}

		public static mixin MixC(int a)
		{
			int b = 100;
			MixB!(b);
			MixB!(b + 1000);
		}

		public static void Test()
		{
			//Test_Start
			ClassA ca = scope .();
			ca.mStr = new String("Boof");

			DeleteAndNullify!(ca.mStr);
			int a = 123;
			MixC!(1);

			ca.mStr = new String("Zorpie");
			int val = ca.Zorf!(100);
			val = ca.Zorf!(200, 300);
		}
	}
}
