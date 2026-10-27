using System;
using System.Collections;

namespace Tests
{
	class Lambdas
	{
		static void TestIt(List<float>.Enumerator e, int idx, float val)
		{
			Test.Assert(e.Index == idx);
			Test.Assert(e.Current == val);
		}

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

			List<float> iList = scope List<float>() { 10, 20, 30 };
			int idx = 0;
			for (var val in iList)
			{
				delegate void() dlg = scope () =>
				{
					TestIt(@val, idx, val);
				};
				dlg();
				++idx;
			}
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

		struct StructA
		{
			public int mA0;
			public this(int v) { mA0 = v; }
		}

		[Test]
		public static void TestStructRetCapture()
		{
			//TODO: Fix on AARCH64
#if !BF_MACHINE_AARCH64
			StructA sa = .(5);
			StructA saGetter() { return sa; }
			delegate StructA() myTest = scope => saGetter;
			var ret = myTest();
			Test.Assert(ret.mA0 == 5);

			delegate StructA() myTest2 = scope [&] () => { return saGetter(); };
			var ret2 = myTest2();
			Test.Assert(ret2.mA0 == 5);
#endif
		}

		static int CallWith<T>(T callback) where T : delegate int()
		{
			return callback();
		}

		static int GetValue(int val) => val;

		enum AtColor
		{
			Red,
			Green
		}

		// A local declared as '@enum' is named 'enum', so implicitly capturing it has to look it up without the '@'
		[Test]
		public static void TestAtNamedCapture()
		{
			let @value = GetValue(123);
			Test.Assert(CallWith(() => @value) == 123);

			int? maybe = GetValue(5);
			if (let @int = maybe)
				Test.Assert(CallWith(() => @int) == 5);

			AtColor? color = .Green;
			if (let @enum = color)
				Test.Assert(CallWith(() => (int)@enum) == 1);

			// Delegate lambdas capture into closure fields, which must keep the '@' as well
			delegate int() scoped = scope () => @value;
			Test.Assert(scoped() == 123);
			delegate int() heap = new () => @value;
			Test.Assert(heap() == 123);
			delete heap;

			// '@y' shadows 'y', and '@@y' reaches the shadowed one - the same inside a lambda as outside
			int y = GetValue(10);
			int @y = GetValue(20);
			Test.Assert((@y == 20) && (@@y == 10));
			delegate int() getNewer = scope () => @y;
			delegate int() getOlder = scope () => @@y;
			Test.Assert((getNewer() == 20) && (getOlder() == 10));
			Test.Assert(CallWith(() => @y) == 20);
		}

		// LibC's part of the SharedGlobals globals holds a closure capturing a LibC type; TestsB, which
		//  uses LibA's part but does not reference LibC, links only if it leaves LibC's part out
		[Test]
		public static void TestClosuresOfAnotherProjectsTypes()
		{
			SharedGlobals.LibCCaptured captured = .() { mValue = 7 };
			Test.Assert(SharedGlobals.LibCCaptures(captured) == 7);
			Test.Assert(SharedGlobals.LibAGlobal() == 3);
		}
	}
}
