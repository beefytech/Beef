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
	}
}
