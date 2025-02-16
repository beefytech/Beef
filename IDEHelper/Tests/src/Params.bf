using System;

namespace Tests;

class Params
{
	class ClassA<T> where T : Tuple
	{
		public static int Test(delegate int(char8 a, params T) dlg, params T par)
		{
			return dlg('A', params par);
		}
	}

	class ClassB : ClassA<(int a, float b)>
	{

	}

	[Test]
	public static void TestBasics()
	{
		int val = ClassB.Test(scope (a, __a, b) =>
			{
				return (.)a + (.)__a + (.)b;
			}, 10, 2.3f);
		Test.Assert(val == 65+10+2);
	}
}