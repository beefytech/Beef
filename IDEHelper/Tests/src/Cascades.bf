using System;

namespace Tests
{
	class Cascades
	{
		public static void MethodA(int a, float b)
		{

		}

		public static void MethodB(int a, out float b)
		{
			b = 100;
		}

		[Test]
		public static void TestBasics()
		{
			int a = MethodA(.. 12, 2.3f);
			Test.Assert(a == 12);
			var (b, c) = MethodA(.. 12, .. 2.3f);
			Test.Assert(b == 12);
			Test.Assert(c == 2.3f);
			var d = MethodA(.. 12, .. 2.3f);
			Test.Assert(d == (12, 2.3f));
			var f = ref MethodB(12, .. var e);
			e += 23;
			Test.Assert(e == (int)123);
			Test.Assert(f == (int)123);
		}
	}
}
