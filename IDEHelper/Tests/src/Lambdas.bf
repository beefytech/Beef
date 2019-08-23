using System;

namespace Tests
{
	class Lambdas
	{
		[Test]
		static void TestBasics()
		{
			int a = 1;

			Action act = scope [&] () =>
			{
				Action act2 = scope [&] () =>
				{
					a += 100;
				};
				act2();
			};
			act();

			Test.Assert(a == 101);
		}

		static int Add3<T>(T func) where T : delegate int()
		{
			return func() + func() + func();
		}

		[Test]
		static void TestValueless()
		{
			Test.Assert(Add3(() => 100) == 300);

			int a = 20;
			int result = Add3(() => a++);
			Test.Assert(result == 63);
		}
	}
}
