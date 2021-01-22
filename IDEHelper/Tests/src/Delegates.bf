#pragma warning disable 168

using System;

namespace System
{
	extension Event<T>
	{
		public implicit void operator+=(T dlg) mut
		{
			Add(dlg);
		}
	}
}

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

		class ClassB<T>
		{
			public delegate int DelegateB(T val);
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

			ClassB<int8>.DelegateB dlg2 = scope (val) => val + 123;
			Test.Assert(dlg2(3) == 126);

			void LocalEventHandler(Object sender, EventArgs e)
			{

			}

			Event<EventHandler> e = .();
			e += new (sender, e) => {};
			e += new => LocalEventHandler;
			e.Dispose();
		}

		public static void Modify(ref int a, ref Splattable b)
		{
			a += 1000;
			b.mA += 2000;
			b.mB += 3000;
		}

		[Test]
		public static void TestRefs()
		{
			delegate void(ref int a, ref Splattable b) dlg = scope => Modify;

			int a = 123;
			Splattable splat = .();
			splat.mA = 234;
			splat.mB = 345;

			dlg(ref a, ref splat);
			Test.Assert(a == 1123);
			Test.Assert(splat.mA == 2234);
			Test.Assert(splat.mB == 3345);
		}

		public static void TestCasting()
		{
			delegate int(int, int) dlg0 = null;
			delegate int(int a, int b) dlg1 = dlg0;
			delegate int(int a2, int b2) dlg2 = (.)dlg1;

			function int(int, int) func0 = null;
			function int(int a, int b) func1 = func0;
			function int(int a2, int b2) func2 = (.)func1;
		}
	}
}
