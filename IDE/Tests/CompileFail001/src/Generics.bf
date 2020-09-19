#pragma warning disable 168

using System;

namespace System
{
	extension Array1<T>
	{
		public static bool operator==(Self lhs, Self rhs) where T : IOpEquals
		{
			return true;
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
			bool eq = val == val; //FAIL Generic argument 'T', declared to be 'TFoo' for 'TFoo[].operator==(TFoo[] lhs, TFoo[] rhs)', must implement 'System.IOpEquals'
		}

		public void Method4()
		{

		}

		interface IFaceA<T>
		{
			public void MethodA<M1>(T val, M1 val2, delegate T (M1 arg) val3);
		}

		class ClassA<T1, T2> : IFaceA<(T1, T2)> //FAIL 'IDETest.Generics.ClassA<T1, T2>' does not implement interface member 'IDETest.Generics.IFaceA<(T1, T2)>.MethodA<M1>((T1, T2) val, M1 val2, delegate (T1, T2)(M1 arg) val3)'
		{
			void MethodA<M>(int a)
			{

			}

			void MethodB()
			{
				function void() f = => MethodA<T2>; //FAIL Method 'IDETest.Generics.ClassA<T1, T2>.MethodA<T2>(int a)' does not match function 'function void()'
			}
		}
	}
}
