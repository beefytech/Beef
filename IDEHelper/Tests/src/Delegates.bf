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
		}
	}
}
