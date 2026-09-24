#pragma warning disable 168

using System;
using System.Collections;

namespace Tests
{
	class Constraints
	{
		struct Vector2<T>
		{
			public T mX;
			public T mY;
		}

		extension Vector2<T> where T : float
		{
			public T LengthSquared => mX * mX + mY * mY;
		    public T Length => Math.Sqrt(LengthSquared);
			public T NegX = -mX;
		}

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

		struct StringViewEnumerator<TS, C> : IEnumerator<StringView>
			where C : const int 
			where TS : StringView[C]
		{
			private TS mStrings;
			private int mIdx;

			public this(TS strings)
			{
				mStrings = strings;
				mIdx = -1;
			}
			
			public StringView Current
			{
				get
				{
					return mStrings[mIdx];
				}
			}

			public bool MoveNext() mut
			{
				return ++mIdx != mStrings.Count;
			}

			public Result<StringView> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		// A type can satisfy a constraint that names itself through a base several levels up. These used to be
		//  rejected (and exhaust the stack) because validating CRTPEnd against CRTPNode<CRTPEnd> re-entered itself
		//  before CRTPEnd's base chain was in place
		class CRTPNode<T> where T : CRTPNode<T>
		{
			public virtual int Get() => 1;
		}

		class CRTPMid<T> : CRTPNode<T> where T : CRTPMid<T>
		{
			public override int Get() => 2;
		}

		class CRTPEnd : CRTPMid<CRTPEnd>
		{
			public override int Get() => 3;
		}

		class CRTPOther : CRTPMid<CRTPEnd>
		{
		}

		class MutualA<T> : MutualB<T> where T : IDisposable
		{
		}

		class MutualB<T> where T : IDisposable
		{
		}

		struct Disposable : IDisposable
		{
			public void Dispose()
			{
			}
		}

		[Test]
		public static void TestCRTPChain()
		{
			CRTPNode<CRTPEnd> node = scope CRTPEnd();
			Test.Assert(node.Get() == 3);
			CRTPMid<CRTPEnd> mid = scope CRTPOther();
			Test.Assert(mid.Get() == 2);
			MutualA<Disposable> mutual = scope .();
			Test.Assert(mutual != null);
		}

		[Test]
		public static void TestBasics()
		{
			Dicto dicto = scope .();
			Method3(dicto);
		}
	}
}
