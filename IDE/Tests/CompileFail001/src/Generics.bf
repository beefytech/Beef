#pragma warning disable 168

using System;
using System.Collections;

namespace System
{
	extension Array1<T>
	{
		public static Self operator+(Self lhs, Self rhs) where T : IDisposable
		{
			return lhs;
		}
	}
}

namespace IDETest
{
	class Generics
	{
		public void Method1<T>(T val) where T : Array
		{

		}

		public void Method2<T, T2>(T val, T2 val2)
		{
			Method1(val2); //FAIL 'T', declared to be 'T2'
		}

		public void Method3<TFoo>(ref TFoo[] val)
		{
			var res = val + val; //FAIL
		}

		public void Method4<TFoo>(ref TFoo[] val) where TFoo : IDisposable
		{
			var res = val + val;
		}

		interface IFaceA<T>
		{
			public void MethodA<M1>(T val, M1 val2, delegate T (M1 arg) val3);
		}

		class ClassA<T1, T2> : IFaceA<(T1, T2)> //FAIL 'IDETest.Generics.ClassA<T1, T2>' does not implement interface member 'void IDETest.Generics.IFaceA<(T1, T2)>.MethodA<M1>((T1, T2) val, M1 val2, delegate (T1, T2)(M1 arg) val3)'
		{
			void MethodA<M>(int a)
			{

			}

			void MethodB()
			{
				function void() f = => MethodA<T2>; //FAIL Method 'void IDETest.Generics.ClassA<T1, T2>.MethodA<T2>(int a)' does not match function 'function void()'
			}
		}

		static void Method5<A, B>() where A : IEnumerable<B>
		{

		}

		static void Method6<C, D, E, F>()
		{
			Method5<E, F>(); //FAIL Generic argument 'A', declared to be 'E' for 'IDETest.Generics.Method5<E, F>()', must implement 'System.Collections.IEnumerable<F>'
		}

		interface IFaceB<T>
		{
			void MethodA0();
		}

		extension IFaceB<T>
		{
			void MethodA1();
		}

		class ClassB<T> : IFaceB<T> //FAIL 'IDETest.Generics.ClassB<T>' does not implement interface member 'void IDETest.Generics.IFaceB<T>.MethodA0()'
		{

		}

		extension ClassB<T>
		{

		}

		public static void TestGen<T, TItem>(T val)
			where T : IEnumerator<TItem>
			where TItem : var
		{
			Console.WriteLine(typeof(decltype(val)).ToString(.. scope .()));
		}

		public static void TestPreGen<T>()
		{
			T a = default;
			TestGen(a); //FAIL Unable to determine generic argument 'TItem'
		}

		static void Method7<T>() where T : var where comptype(typeof(T)) : class
		{

		}

		public static void TestGenBug()
		{
			TestPreGen<List<int>>();
			Method7<int>(); //FAIL The type 'int' must be a reference type in order to use it as parameter 'comptype(typeof(T))' for 'IDETest.Generics.Method7<int>()'
		}

		public static void CircDepMethod<T, T2>() where T : T2 where T2 : T //FAIL
		{

		}

		class CircDep<T> where T : T //FAIL
		{

		}

		public class TestExt<T> where T : struct
		{
			public struct InnerA
			{

			}

			public struct InnerB<T2> where T2 : struct
			{

			}
		}

		extension TestExt<T>
			where T : Int
		{
			public int a = 0;

			public struct InnerC
			{
			}
		}

		static void TestExtMethod()
		{
			TestExt<String>.InnerA a; //FAIL
			TestExt<String>.InnerB<int> b; //FAIL
			TestExt<int>.InnerB<int> c;
			TestExt<int>.InnerC d;
			TestExt<float>.InnerC e; //FAIL
		}
	}
}

namespace System.Collections
{
    extension List<T> //FAIL
        where T : T
    {

    }
}
