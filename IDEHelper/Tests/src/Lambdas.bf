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

		[Test]
		static void LambdaWithDtor()
		{
			int a = 10;
			int b = 20;

			//
			{
				delegate void() dlg = scope [&] () =>
				{
					a++;
				}
				~
				{
					b++;
				};
				dlg();
			}

			Test.Assert(a == 11);
			Test.Assert(b == 21);

			delegate void() dlg = new [&] () =>
			{
				a += 100;
			}
			~
			{
				b += 200;
			};
			dlg();
			Test.Assert(a == 111);
			Test.Assert(b == 21);
			delete dlg;
			Test.Assert(b == 221);
		}
	}
}
