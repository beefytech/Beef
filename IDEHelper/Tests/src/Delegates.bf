using System;

namespace Tests
{
	class Delegates
	{
		struct Valueless
		{
			public int MutMethod(int a) mut
			{
				return 210 + a;
			}

			public int NonMutMethod(int a)
			{
				return 210 + a;
			}
		}

		struct Splattable
		{
			public int32 mA = 10;
			public int16 mB = 200;

			public int MutMethod(int a) mut
			{
				mB += 100;
				return mA + mB + a;
			}

			public int NonMutMethod(int a)
			{
				return mA + mB + a;
			}

			public void TestLambda() mut
			{
				delegate int(ref int a, ref int b) dlg = scope [&] (a, b) => 
				{
					a += 20;
					b += 30;
					mA++;
					return mA + a + b;
				};

				mA = 100;
				int testA = 8;
				int testB = 9;
				Test.Assert(dlg(ref testA, ref testB) == 100+1 + 20+8 + 30+9);
				Test.Assert(testA == 28);
				Test.Assert(testB == 39);
				Test.Assert(mA == 101);
			}
		}

		struct NonSplattable
		{
			public int32 mA = 10;
			public int16 mB = 200;
			public int64 mC = 300;
			public int64 mD = 400;

			public int MutMethod(int a) mut
			{
				mB += 100;
				return mA + mB + a;
			}

			public int NonMutMethod(int a)
			{
				return mA + mB + a;
			}
		}

		class ClassA
		{
			public int mA;

			public void TestLambda()
			{
				delegate int(ref int a, ref int b) dlg = scope (a, b) => 
				{
					a += 20;
					b += 30;
					mA++;
					return mA + a + b;
				};

				mA = 100;
				int testA = 8;
				int testB = 9;
				Test.Assert(dlg(ref testA, ref testB) == 100+1 + 20+8 + 30+9);
				Test.Assert(testA == 28);
				Test.Assert(testB == 39);
				Test.Assert(mA == 101);
			}
		}

		[Test]
		public static void TestBasics()
		{
			delegate int(int a) dlg;

			Valueless val0 = .();
			dlg = scope => val0.MutMethod;
			Test.Assert(dlg(9) == 219);
			dlg = scope => val0.NonMutMethod;
			Test.Assert(dlg(9) == 219);

			Splattable val1 = .();
			dlg = scope => val1.MutMethod;
			Test.Assert(dlg(9) == 319);
			dlg = scope => val1.NonMutMethod;
			Test.Assert(dlg(9) == 219);
			Test.Assert(val1.mB == 200);

			dlg = scope => (ref val1).MutMethod;
			Test.Assert(dlg(9) == 319);
			dlg = scope => (ref val1).NonMutMethod;
			Test.Assert(dlg(9) == 319);
			Test.Assert(val1.mB == 300);

			NonSplattable val2 = .();
			dlg = scope => val2.MutMethod;
			Test.Assert(dlg(9) == 319);
			dlg = scope => val2.NonMutMethod;
			Test.Assert(dlg(9) == 219);
			Test.Assert(val2.mB == 200);

			dlg = scope => (ref val2).MutMethod;
			Test.Assert(dlg(9) == 319);
			dlg = scope => (ref val2).NonMutMethod;
			Test.Assert(dlg(9) == 319);
			Test.Assert(val2.mB == 300);

			val1.TestLambda();
			ClassA ca = scope .();
			ca.TestLambda();
		}
	}
}
