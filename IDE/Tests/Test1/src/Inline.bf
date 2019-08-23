#pragma warning disable 168

using System;

namespace IDETest
{
	class InlineTester
	{
		[Inline]
		public static int TimesTwo(int a)
		{
			return a * 2;
		}

		[Inline]
		public static int Add(int a, int b)
		{
			int retVal = TimesTwo(a);
			retVal += TimesTwo(b);
			return retVal;
		}

		public static int PlusOne(int a)
		{
			return a + 1;
		}

		public static mixin MixB(var argC)
		{
			argC = PlusOne(argC);
			argC
		}

		public static mixin MixA(var argA, var argB)
		{
			int z = MixB!(argA);
			argA + argB
		}

		public static void TestInlines()
		{
			int a = 123;
			int b = 234;
			int d = Add(a, b);
		}

		public static void Test()
		{
			int a = 10;
			int b = 2;

			//InlineTester
			int c = MixA!(a, b);

			TestInlines();
		}
	}


}
