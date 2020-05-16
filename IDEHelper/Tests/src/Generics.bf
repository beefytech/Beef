using System;

namespace Tests
{
	class Generics
	{
		class ClassA : IDisposable, LibA.IVal
		{
			int LibA.IVal.Val
			{
				get
				{
					return 123;
				}

				set
				{

				}
			}

			void IDisposable.Dispose()
			{

			}
		}

		class ClassB : IDisposable, LibA.IVal
		{
			public int Val
			{
				get
				{
					return 234;
				}

				set
				{

				}
			}

			public void Dispose()
			{

			}
		}

		static void DoDispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		struct Disposer<T>
		{
			static void UseDispose(IDisposable disp)
			{

			}

			static void DoDisposeA(mut T val) where T : IDisposable
			{
				val.Dispose();
				UseDispose(val);
			}

			static void DoDisposeB(mut T val) where T : IDisposable
			{
				val.Dispose();
			}
		}

		[Test]
		public static void TestGenericDelegates()
		{
			delegate void(String v) dlg = scope => StrMethod;
			CallGenericDelegate(dlg);
			CallGenericDelegate<String>(scope => StrMethod);
		}

		public static void CallGenericDelegate<T>(delegate void(T v) dlg)
		{
		}

		public static void StrMethod(String v)
		{
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			ClassB cb = scope .();
			Test.Assert(LibA.LibA0.GetVal(ca) == 123);
			Test.Assert(LibA.LibA0.GetVal(cb) == 234);

			LibA.LibA0.Dispose(ca);
			LibA.LibA0.Dispose(cb);

			LibA.LibA0.Alloc<ClassA>();
			LibA.LibA0.Alloc<ClassB>();
		}
	}

	class ConstGenerics
	{
		public static float GetSum<TCount>(float[TCount] vals) where TCount : const int
		{
			float total = 0;
			for (int i < vals.Count)
				total += vals[i];
			return total;
		}

		[Test]
		public static void TestBasics()
		{
			float[5] fVals = .(10, 20, 30, 40, 50);

			float totals = GetSum(fVals);
			Test.Assert(totals == 10+20+30+40+50);
		}
	}
}
