#pragma warning disable 168

using System;
using System.Collections;

namespace Tests
{
	class Constraints
	{
		class Dicto : Dictionary<int, float>
		{
		   
		}

		public static bool Method1<T>(IEnumerator<T> param1)
		{
		    return true;
		}

		public static bool Method2<TEnumerator, TElement>(TEnumerator param1) where TEnumerator : IEnumerator<TElement>
		{
		    for (let val in param1)
			{
				
			}

			return true;
		}

		public static bool Method3<K, V>(Dictionary<K, V> param1) where K : IHashable
		{
			Method1(param1.GetEnumerator());
			Method1((IEnumerator<(K key, V value)>)param1.GetEnumerator());
		    return Method2<Dictionary<K, V>.Enumerator, (K key, V value)>(param1.GetEnumerator());
		}

		struct StructA
		{

		}

		class ClassA<T> where float : operator T * T where char8 : operator implicit T
		{
			public static float DoMul(T lhs, T rhs)
			{
				char8 val = lhs;
				return lhs * rhs;
			}
		}

		extension ClassA<T> where double : operator T - T where StructA : operator explicit T
		{
			public static double DoSub(T lhs, T rhs)
			{
				StructA sa = (StructA)lhs;
				return lhs - rhs;
			}
		}

		extension ClassA<T> where int16 : operator T + T where int8 : operator implicit T
		{
			public static double DoAdd(T lhs, T rhs)
			{
				int8 val = lhs;
				double d = lhs * rhs;
				return lhs + rhs;
			}
		}

		public static void Test0<T>(T val)
			where float : operator T * T where char8 : operator implicit T
			where int16 : operator T + T where int8 : operator implicit T
		{
			ClassA<T> ca = scope .();
			ClassA<T>.DoMul(val, val);
 			ClassA<T>.DoAdd(val, val);
		}

		[Test]
		public static void TestBasics()
		{
			Dicto dicto = scope .();
			Method3(dicto);
		}
	}
}
