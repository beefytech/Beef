using System;
using System.Collections;

namespace Tests
{
	class Generics2
	{
		struct TestFunc<T, Del>
		{
			private int mId;
			private Del mComparer;

			public static TestFunc<T, Del> Create(int id, Del comparer)
			{
				return .()
				{
					mId = id,
					mComparer = comparer
				};
			}

			public bool CheckDlg(T item)
			{
				return false;
			}

			public bool CheckDlg(T item) where Del : delegate bool(T)
			{
				return mComparer(item);
			}

			public bool CheckDlg(T item) where Del : delegate bool(int, T)
			{
				return mComparer(mId, item);
			}

			public bool CallCheck(T val)
			{
				return CheckDlg(val);
			}
		}

		struct Iterator
		{
			public static Iterator<decltype(default(TCollection).GetEnumerator()), TSource> Wrap<TCollection, TSource>(TCollection items)
				where TCollection : concrete, IEnumerable<TSource>
			{
				return .(items.GetEnumerator());
			}
		}

		struct Iterator<TEnum, TSource> : IDisposable
			where TEnum : concrete, IEnumerator<TSource>
		{
			public TEnum mEnum;

			public this(TEnum items)
			{
				mEnum = items;
			}

			[SkipCall]
			public void Dispose() { }
		}

		public static bool SequenceEquals<TLeft, TRight, TSource>(this TLeft left, TRight right)
			where TLeft : concrete, IEnumerable<TSource>
			where TRight : concrete, IEnumerable<TSource>
			where bool : operator TSource == TSource
		{
			using (let iterator0 = Iterator.Wrap<TLeft, TSource>(left))
			{
				var e0 = iterator0.mEnum;
				using (let iterator1 = Iterator.Wrap<TRight, TSource>(right))
				{
					var e1 = iterator1.mEnum;
					while (true)
					{
						switch (e0.GetNext())
						{
						case .Ok(let i0):
							switch (e1.GetNext())
							{
							case .Ok(let i1):
								if (i0 != i1)
									return false;
							case .Err:
								return false;
							}
						case .Err:
							return e1.GetNext() case .Err;
						}
					}
				}
			}
		}

		class IFaceA<T0, T1> where T0 : Dictionary<T1, int> where T1 : IHashable
		{
			Dictionary<T1, int> mDict;
		}

		public static void MethodA<T0, T1>() where T0 : Dictionary<T1, int> where T1 : IHashable
		{

		}

		typealias BigNum<N> = BigNum<N,const 0>;
		public struct BigNum<ArgN, ExponentCells> where ArgN : const int where ExponentCells : const int64
		{
		    static int CalculateN() => Math.Max(1,(int)ArgN);
		    public const int N = CalculateN();
		}

		public static int Test<T>(T param1, params Span<int> param2)
			where T : const String
		{
			int total = param1.Length;
			for (int val in param2)
				total += val;
			return total;
		}

		[Test]
		public static void TestBasics()
		{
			let testF = TestFunc<String, delegate bool(String)>.Create(10, scope (s) => s == "Str");
			Test.Assert(testF.CallCheck("Str"));
			Test.Assert(!testF.CallCheck("Str2"));

			List<int32> iList = scope .() { 1, 2, 3 };
			Span<int32> iSpan = iList;
			Test.Assert(iList.SequenceEquals(iSpan));
			iList.Add(4);
			Test.Assert(!iList.SequenceEquals(iSpan));

			Test.Assert(BigNum<const 3>.N == 3);
			Test.Assert(Test("test", 1, 2, 3) == 10);
		}
	}
}
