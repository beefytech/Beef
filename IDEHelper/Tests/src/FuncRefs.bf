using System;
using System.Collections;

namespace Tests
{
	class FuncRefs2
	{
		public static int Do10Ext<T>(T func, int val) where T : delegate int(int a)
		{
			int total = 0;
			for (int i < 10)
				total += func(val);
			return total;
		}
	}

	class FuncRefs
	{
		static int FuncA(int val)
		{
			return val * 10;
		}

		static float FuncA(float val)
		{
			return val * -10;
		}

		static int Do10<T>(T func, int val) where T : delegate int(int a)
		{
			int total = 0;
			for (int i < 5)
				total += func(val);

			delegate int(int a) dlg = scope => func;
			for (int i < 5)
				total += dlg(val);

			return total;
		}

		static float Do10f<T>(T func, float val) where T : delegate float(float a)
		{
			float total = 0;
			for (int i < 10)
				total += func(val);
			return total;
		}

		static T DoOnListA<T, TDlg>(List<T> val, TDlg dlg) where TDlg : delegate T(T a)
		{
			return dlg(val[0]);
		}

		static T DoOnListB<T, TDlg>(TDlg dlg, List<T> val) where TDlg : delegate T(T a)
		{
			return dlg(val[0]);
		}

		[Test]
		public static void TestBasics()
		{
			int val0 = Do10(scope => FuncA, 3);
			Test.Assert(val0 == 300);

			int val1 = Do10(=> FuncA, 3);
			Test.Assert(val1 == 300);

			int val2 = FuncRefs2.Do10Ext(=> FuncA, 3);
			Test.Assert(val2 == 300);

			float val3 = Do10f(scope => FuncA, 0.34f);
			Test.Assert(val3 == -34);

			float val4 = Do10f(=> FuncA, 0.34f);
			Test.Assert(val4 == -34);

			List<float> fList = scope .() { 1.2f, 2.3f };
			Test.Assert(DoOnListA(fList, (val) => val + 100) == 101.2f);
			Test.Assert(DoOnListB((val) => val + 200, fList) == 201.2f);
		}

		struct MethodRefHolder<T> where T : delegate int(int num)
		{
			public T mVal;

			public void Set(T val) mut
			{
				mVal = val;
			}

			public int Use(int num)
			{
				return mVal(num);
			}

			[SkipCall]
			public void Dispose()
			{

			}
		}

		extension MethodRefHolder<T> where T : Delegate
		{
			public void Dispose()
			{
				delete mVal;
			}
		}

		static int Use<T>(T val, int num) where T : delegate int(int num)
		{
			T copied = val;
			copied(num);	    
			int result = val(num);
			
			return result;
		}

		static MethodRefHolder<T> Bind<T>(T val) where T : delegate int(int num)
		{
			MethodRefHolder<T> bind = .();
			bind.Set(val);
			return bind;
		}

		class Class
		{
			public int32 mA = 100;
			public int16 mB = 200;

			public void Test()
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				Test.Assert(Use(=> LFunc, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(=> LFunc);
				
				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}

			public void TestDlg()
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				delegate int(int num) dlg = new => LFunc;

				Test.Assert(Use(dlg, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(dlg);

				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}
		}

		struct Valueless
		{
			public void Test() mut
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					a += GetVal();
					return splat.mB;
				}

				Test.Assert(Use(=> LFunc, 10) == 400);
				Test.Assert(a == 10*2+20*2);

				var bind = Bind(=> LFunc);

				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10*2+20*2 + 30+20);

				bind.Dispose();
			}

			public void TestDlg()
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					a += GetVal();
					return splat.mB;
				}

				delegate int(int num) dlg = new => LFunc;

				Test.Assert(Use(dlg, 10) == 400);
				Test.Assert(a == 10*2+20*2);

				var bind = Bind(dlg);

				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10*2+20*2 + 30+20);

				bind.Dispose();
			}

			public int GetVal()
			{
				return 20;
			}
		}

		struct Splattable
		{
			public int32 mA = 100;
			public int16 mB = 200;

			public void Test() mut
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				Test.Assert(Use(=> LFunc, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(=> LFunc);
				
				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}

			public void TestDlg() mut
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				delegate int(int num) dlg = new => LFunc;

				Test.Assert(Use(dlg, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(dlg);

				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}
		}

		struct NonSplattable
		{
			public int32 mA = 100;
			public int16 mB = 200;
			public int64 mC = 300;
			public int64 mD = 400;

			public void Test() mut
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				Test.Assert(Use(=> LFunc, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(=> LFunc);
				
				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}

			public void TestDlg() mut
			{
				int a = 0;
				Splattable splat;
				splat.mA = 300;
				splat.mB = 400;

				int LFunc(int p)
				{
					a += p;
					mA += splat.mA;
					return splat.mB;
				}

				delegate int(int num) dlg = new => LFunc;

				Test.Assert(Use(dlg, 10) == 400);
				Test.Assert(a == 10+10);
				Test.Assert(mA == 100+300+300);

				var bind = Bind(dlg);

				Test.Assert(bind.Use(30) == 400);
				Test.Assert(a == 10+10 + 30);
				Test.Assert(mA == 100+300+300 + 300);

				bind.Dispose();
			}
		}

		[Test]
		public static void ClassTestA()
		{
			Class val = scope Class();
			val.Test();
			val = scope Class();
			val.TestDlg();
		}

		[Test]
		public static void ValuelessTestA()
		{
			Valueless val = .();
			val.Test();
			val = .();
			val.TestDlg();
		}

		[Test]
		public static void SplattableTestA()
		{
			Splattable val = .();
			val.Test();
			val = .();
			val.TestDlg();
		}

		[Test]
		public static void NonSplattableTestA()
		{
			NonSplattable val = .();
			val.Test();
			val = .();
			val.TestDlg();
		}
	}
}
