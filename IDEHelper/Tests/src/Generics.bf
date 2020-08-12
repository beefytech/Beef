#pragma warning disable 168

using System;
using System.Collections;

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

		class Singleton<T> where T : Singleton<T>
		{
		    public static T mInstance;

		    protected this()
		    {
		        mInstance = (T)this;
		    }
		}

		class ClassC : Singleton<ClassC>
		{
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

		public static int MethodA<T>(T val) where T : var
		{
			return 1;
		}

		public static int MethodA<T>(T val) where T : ValueType
		{
			return 2;
		}

		public static int MethodA<T>(T val) where T : Enum
		{
			return 3;
		}

		public struct Entry
		{
			public static int operator<=>(Entry lhs, Entry rhs)
			{
				return 0;
			}
		}

		public static void Alloc0<T>() where T : new, delete, IDisposable
		{
			alloctype(T) val = new T();
			val.Dispose();
			delete val;
		}

		public static void Alloc1<T>() where T : new, delete, IDisposable, Object
		{
			alloctype(T) val = new T();
			T val2 = val;
			val.Dispose();
			delete val;
		}

		public static void Alloc2<T>() where T : new, delete, IDisposable, struct
		{
			alloctype(T) val = new T();
			T* val2 = val;
			val2.Dispose();
			delete val;
		}

		[Test]
		public static void TestBasics()
		{
			List<Entry> list = scope .();
			list.Sort();

			ClassA ca = scope .();
			ClassB cb = scope .();
			Test.Assert(LibA.LibA0.GetVal(ca) == 123);
			Test.Assert(LibA.LibA0.GetVal(cb) == 234);

			LibA.LibA0.Dispose(ca);
			LibA.LibA0.Dispose(cb);

			LibA.LibA0.Alloc<ClassA>();
			LibA.LibA0.Alloc<ClassB>();

			Test.Assert(MethodA("") == 1);
			Test.Assert(MethodA(1.2f) == 2);
			Test.Assert(MethodA(TypeCode.Boolean) == 3);

			ClassC cc = scope .();
			Test.Assert(ClassC.mInstance == cc);
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

		public static mixin TransformArray<Input, Output, InputSize>(Input[InputSize] array, delegate void(Input, ref Output) predicate) where InputSize : const int where Output : new, class
		{
			Output[2] output = default;
			for (int i = 0; i < array.Count; i++)
			{
				output[i] = scope:mixin Output();
				predicate(array[i], ref output[i]);
			}
			output
		}

		[Test]
		public static void TestSizedArrays()
		{
			int[2] arr = .(2, 4);

			delegate void(int, ref String) toString = scope (i, str) => { i.ToString(str); };

			List<String[2]> l2 = scope .();
			l2.Add(TransformArray!(arr, toString));

			Test.Assert(l2.Front[0] == "2");
			Test.Assert(l2.Front[1] == "4");
		}
	}
}
