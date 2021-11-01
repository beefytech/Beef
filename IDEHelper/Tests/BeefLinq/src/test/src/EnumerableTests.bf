//#define INCLUDE_FAILURES

using System.Collections;
using System;
namespace System.Linq
{
	public class EnumerableTests
	{
		[Test]
		public static void Any()
		{
			let data = scope List<int>();

			var actual = data.Any();
			Test.Assert(!actual);

			actual = data.Any((it) => it == 2);
			Test.Assert(!actual);

			data.Add(1);
			data.Add(2);
			data.Add(3);
			data.Add(4);

			actual = data.Any((it) => it == 2);
			Test.Assert(actual);

			actual = data.Any();
			Test.Assert(actual);

			data.RemoveAt(1);
			actual = data.Any((it) => it == 2);
			Test.Assert(!actual);
		}

		[Test]
		public static void All()
		{
			let data = scope List<int>();

			var actual = data.All((it) => it == 2);
			Test.Assert(!actual);

			data.Add(2);
			data.Add(2);
			data.Add(2);
			data.Add(2);

			actual = data.All((it) => it == 2);
			Test.Assert(actual);

			data.Add(3);
			actual = data.All((it) => it == 2);
			Test.Assert(!actual);
		}


		struct ContainsTest : IEnumerator<int>
		{
			int mState = 0;

			public Result<int> GetNext() mut
			{
				if (mState > 3)
					return .Err;

				return .Ok(mState++);
			}
		}

		[Test]
		public static void Contains()
		{
			let data = ContainsTest();

			var actual = data.Contains(2);
			Test.Assert(actual);

			actual = data.Contains(4);
			Test.Assert(!actual);
		}


		[Test]
		public static void Average()
		{
			let data = scope List<int>() { 1, 1, 2, 2, 4 };
			{
				let actual = data.Average();
				Test.Assert(actual == 2);
			}
			{
				let actual = data.GetEnumerator().Average();
				Test.Assert(actual == 2);
			}
		}


		[Test]
		public static void Max()
		{
			{
				let data = scope List<int>();

				var actual = data.Max();
				Test.Assert(actual == default);

				data.Add(3);
				actual = data.Max();
				Test.Assert(actual == 3);

				data.Add(1);
				actual = data.Max();
				Test.Assert(actual == 3);
			}
			{
				let data = scope List<int>();

				var actual = data.GetEnumerator().Max();
				Test.Assert(actual == default);

				data.Add(3);
				actual = data.GetEnumerator().Max();
				Test.Assert(actual == 3);

				data.Add(1);
				actual = data.GetEnumerator().Max();
				Test.Assert(actual == 3);
			}
		}

		[Test]
		public static void Min()
		{
			let data = scope List<int>();

			var actual = data.Min();
			Test.Assert(actual == default);

			data.Add(3);
			actual = data.Min();
			Test.Assert(actual == 3);

			data.Add(1);
			actual = data.Min();
			Test.Assert(actual == 1);
		}

		[Test]
		public static void Sum()
		{
			{
				let data = scope List<int>() { 1, 2, 3, 4 };
				let actual = data.Sum();
				Test.Assert(actual == 10);
			}
			{
				let data = scope List<int>() { 1, 2, 3, 4 };
				let actual = data.Sum();
				Test.Assert(actual == 10);
			}
			{
				let data = scope List<int?>() { 1, null, 3, 4 };
				let actual = data.Sum();
				Test.Assert(actual == null);
			}
			{
				let data = scope List<int?>();
				let actual = data.Sum();
				Test.Assert(actual == null);
			}
		}

		[Test]
		public static void ElementAt()
		{
			let data = scope List<int>() { 1, 2, 3 };
			let actual = data.ElementAt(1);
			Test.Assert(actual == 1);
		}

		[Test]
		public static void First()
		{
			let data = scope List<int>() { 1, 2, 3 };
			let actual = data.First();
			Test.Assert(actual == 1);
		}

		[Test]
		public static void FirstOrDefault()
		{
			let data = scope List<int>();
			let actual = data.FirstOrDefault();
			Test.Assert(actual == default);
		}

		[Test]
		public static void Last()
		{
			let data = scope List<int>() { 1, 2, 3 };
			let actual = data.Last();
			Test.Assert(actual == 3);
		}

		[Test]
		public static void LastOrDefault()
		{
			let data = scope List<int>();
			let actual = data.LastOrDefault();
			Test.Assert(actual == default);
		}

		[Test]
		public static void Take()
		{
			let data = scope List<int>();
			for (var i < 20) data.Add(i);

			let actual = data.Take(10).ToList(.. scope .());

			let expected = scope List<int>();
			for (var i < 10) expected.Add(i);

			Test.Assert(actual.Count == 10);
			Test.Assert(actual.SequenceEquals(expected) == true);
		}

		[Test]
		public static void Skip()
		{
			let data = scope List<int>();
			for (var i < 20) data.Add(i);

			let actual = data.Skip(10).ToList(.. scope .());

			let expected = scope List<int>();
			for (var i < 10) expected.Add(i + 10);

			Test.Assert(actual.Count == 10);
			Test.Assert(actual.SequenceEquals(expected) == true);
		}

		[Test]
		public static void Empty()
		{
			let actual = Enumerable.Empty<int>().ToList(.. scope .());
			Test.Assert(actual.Count == 0);
		}

		[Test]
		public static void Range()
		{
			{
				let actual = Enumerable.Range(10).ToList(.. scope .());
				let expected = scope List<int>();
				for (var i < 10) expected.Add(i);
				Test.Assert(actual.SequenceEquals(expected) == true);
			}
			{
				let actual = Enumerable.Range(10, 20).ToList(.. scope .());
				let expected = scope List<int>();
				for (var i < 10) expected.Add(i + 10);
				Test.Assert(actual.SequenceEquals(expected) == true);
			}
		}

		[Test]
		public static void Map()
		{
			{
				let data = scope List<int>() { 0, 5, 10 };
				let actual = data.Map(0f, 1f).ToList(.. scope .());
				let expected = scope List<float>() { 0f, 0.5f, 1f };

				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				let data = scope List<int>() { 0, 5, 10 };
				let actual = data.Map(0, 100).ToList(.. scope .());
				let expected = scope List<int>();
				expected.Add(0);
				expected.Add(50);
				expected.Add(100);

				Test.Assert(actual.SequenceEquals(expected));
			}
		}


		[Test]
		public static void Select()
		{
			let data = scope List<(int x, int y, float z, float w)>() { (1, 2, 3, 4), (4, 3, 2, 1) };
			let actual = data.Select((it) => (x: it.x, y: it.y)).ToList(.. scope .());
			let expected = scope List<(int x, int y)>();
			expected.Add((1, 2));
			expected.Add((4, 3));

			Test.Assert(actual.Count == 2);
			Test.Assert(actual.SequenceEquals(expected));
		}

		[Test]
		public static void Where()
		{
			let data = scope List<(int x, int y, float z, float w)>() { (1, 2, 3, 4), (4, 3, 2, 1) };
			let actual = data.Where((it) => it.x == 1).ToList(.. scope .());
			Test.Assert(actual.Count == 1);
			Test.Assert(actual[0] == (1, 2, 3, 4));
		}

		[Test]
		public static void TakeWhile()
		{
			let data = scope List<int>() { 1, 1, 2, 4 };
			let actual = data.TakeWhile((it) => it == 1).ToList(.. scope .());
			let expected = scope List<int>();
			expected.Add(1);
			expected.Add(1);
			Test.Assert(actual.Count == 2);
		}


		[Test]
		public static void SkipWhile()
		{
			let data = scope List<int>() { 1, 1, 2, 3 };
			let actual = data.SkipWhile((it) => it == 1).ToList(.. scope .());
			let expected = scope List<int>();
			expected.Add(2);
			expected.Add(3);
			Test.Assert(actual.Count == expected.Count);
		}

		[Test]
		public static void Repeat()
		{
			let actual = Enumerable.Repeat(10, 10).ToList(.. scope .());
			let expected = scope List<int>();
			for (var i < 10)
				expected.Add(10);

			Test.Assert(actual.SequenceEquals(expected));
		}

		[Test]
		public static void Distinct()
		{
			let data = scope List<int>() { 1, 1, 2, 3 };
			let actual = data.Distinct().ToList(.. scope .());
			let expected = scope List<int>();
			expected.Add(1);
			expected.Add(2);
			expected.Add(3);

			Test.Assert(actual.Count == expected.Count);
			Test.Assert(actual.SequenceEquals(expected));
		}

		[Test]
		public static void Reverse()
		{
			let data = scope List<int>() { 1, 1, 2, 3 };
			let actual = data.Reverse().ToList(.. scope .());
			let expected = scope List<int>();
			expected.Add(3);
			expected.Add(2);
			expected.Add(1);
			expected.Add(1);

			Test.Assert(actual.Count == expected.Count);
			Test.Assert(actual.SequenceEquals(expected));
		}

		[Test]
		public static void DefaultIfEmpty()
		{
			{
				let data = scope List<int?>();
				let actual = data.DefaultIfEmpty().ToList(.. scope .());
				let expected = scope List<int?>();
				expected.Add(null);
				Test.Assert(actual.Count == 1);
				Test.Assert(actual[0] == null);
			}
			{
				let data = scope List<int>();
				let actual = data.DefaultIfEmpty().ToList(.. scope .());
				let expected = scope List<int>();
				expected.Add(10);
				Test.Assert(actual.Count == 1);
				Test.Assert(actual[0] == 0);
			}
			{
				let data = scope List<int>();
				let actual = data.DefaultIfEmpty(10).ToList(.. scope .());
				let expected = scope List<int>();
				expected.Add(10);
				Test.Assert(actual.Count == 1);
				Test.Assert(actual[0] == 10);
			}
		}


		[Test]
		public static void Aggregate()
		{
			{
				let data = scope List<int>() { 1, 2, 3, 4, 5 };
				let actual = data.Aggregate((sum, next) => sum + next);
				Test.Assert(actual == 15);
			}
			{
				let data = scope List<int>() { 1, 2, 3, 4, 5 };
				let actual = data.Aggregate(5, (sum, next) => sum + next);
				Test.Assert(actual == 20);
			}

			/*{
				let data = scope List<int>() { 1, 2, 3, 4, 5 };
				let actual = data.Aggregate( (sum, next) => sum + next, (result) => result * 1000f);
				Test.Assert(actual == 15000f);
			}*/
			{
				let data = scope List<int>() { 1, 2, 3, 4, 5 };
				let actual = data.Aggregate(5, (sum, next) => sum + next, (result) => result * 1000f);
				Test.Assert(actual == 20000f);
			}
		}

#region ToXYZ methods
		[Test]
		public static void ToDictionary()
		{
			{
				let data = scope List<(int x, float y)>() { (1, 2f), (4, 3f) };
				let actual = data.ToDictionary((it) => it.x, .. scope .());

				Test.Assert(actual.Count == 2);
				Test.Assert(actual.Contains((1, (1, 2f))));
				Test.Assert(actual.Contains((4, (4, 3f))));
			}
			{
				let data = scope List<(int x, float y)>() { (1, 2f), (4, 3f) };
				let actual = data.ToDictionary((it) => it.x, (it) => it.y, .. scope .());

				Test.Assert(actual.Count == 2);
				Test.Assert(actual.Contains((1, 2f)));
				Test.Assert(actual.Contains((4, 3f)));
			}
		}

		[Test]
		public static void ToHashSet()
		{
			let data = scope List<int>() { 1, 2, 2 };
			let actual = data.ToHashSet(.. scope .());

			Test.Assert(actual.Count == 2);
			Test.Assert(actual.Contains(1));
			Test.Assert(actual.Contains(2));
		}
#endregion

#region GroupBy

		/*public static mixin GroupBy<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKey key)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
		{
			let groupByMemory = scope:mixin GroupByMemory();
			return InternalGroupBy(groupByMemory, items, key);
		}*/

		[Test]
		public static void GroupBy()
		{
			{
				let data = scope List<(int x, int y, int z)>() { (0, 1, 9), (0, 2, 8), (2, 4, 5), (1, 1, 1), (2, 2, 2) };
				let actual = data.GroupBy((key) => key.x, scope .()).ToList(.. scope .());

				Test.Assert(actual.Count == 3);

				var i = 0;
				for (var it in actual)
				{
					switch (it.Key)
					{
					case 0: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (0, 1, 9), (0, 2, 8) })); i |= 1;
					case 1: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (1, 1, 1) })); i |= 2;
					case 2: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (2, 4, 5), (2, 2, 2) })); i |= 4;
					}
				}

				Test.Assert(i == 7);
			}

			{
				let data = scope List<(int x, int y, int z)>() { (0, 1, 9), (0, 2, 8), (2, 4, 5), (1, 1, 1), (2, 2, 2) };

				var i = 0;
				for (var it in  data.GroupBy((key) => key.x))
				{
					switch (it.Key)
					{
					case 0: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (0, 1, 9), (0, 2, 8) })); i |= 1;
					case 1: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (1, 1, 1) })); i |= 2;
					case 2: Test.Assert(it.SequenceEquals(scope List<(int x, int y, int z)>() { (2, 4, 5), (2, 2, 2) })); i |= 4;
					}
				}

				Test.Assert(i == 7);
			}
		}

#endregion

		[Test]
		public static void Union()
		{
			let data = scope List<int>() { 0, 1, 2 };
			let other = scope List<int>() { 3, 4, 5 };
			let actual = data.Union(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[6](0, 1, 2, 3, 4, 5)));
		}

		[Test]
		public static void Intersect()
		{
			let data = scope List<int>() { 0, 1, 2 };
			let other = scope List<int>() { 1, 5, 2 };
			let actual = data.Intersect(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[2](1, 2)));
		}

		[Test]
		public static void Except()
		{
			let data = scope List<int>() { 0, 1, 2 };
			let other = scope List<int>() { 0, 5, 2 };
			let actual = data.Except(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[1](1)));
		}

		[Test]
		public static void Zip()
		{
			let data = scope List<int>() { 0, 1, 2, 3 };
			let other = scope List<int>() { 1, 2, 3 };
			let actual = data.Zip(other, (first, second) => first + second).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[3](1, 3, 5)));
		}

		[Test]
		public static void Concat()
		{
			let data = scope List<int>() { 0, 1, 2, 3 };
			let other = scope List<int>() { 1, 2, 3 };
			let actual = data.Concat(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[?](0, 1, 2, 3, 1, 2, 3)));
		}

		[Test]
		public static void Append()
		{
			let data = scope List<int>() { 0, 1, 2 };
			let other = scope List<int>() { 1, 2, 3, 3 };
			let actual = data.Append(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[?](0, 1, 2, 1, 2, 3, 3)));
		}

		[Test]
		public static void Prepend()
		{
			let data = scope List<int>() { 0, 1, 2 };
			let other = scope List<int>() { 1, 2, 3, 3 };
			let actual = data.Prepend(other).ToList(.. scope .());

			Test.Assert(actual.SequenceEquals(int[?](1, 2, 3, 3, 0, 1, 2)));
		}

		[Test]
		public static void OrderBy()
		{
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 2), (1, 3), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x, (l, r) => l - r).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 2), (1, 3), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				//orderby has some temp allocations, this test is just to make sure those temp allocations don't fail
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x, (l, r) => l - r).OrderBy((it) => it.x, (l, r) => r - l).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (3, 2), (2, 0), (1, 2), (1, 3), (0, 4) };
				Test.Assert(actual.SequenceEquals(expected));
			}
		}

		[Test]
		public static void OrderByDescending()
		{
			//this method shouldn't be using reverse, but I'm being lazy
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderByDescending((it) => it.x).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 3), (1, 2), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected.Reverse()));
			}
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderByDescending((it) => it.x, (l, r) => l - r).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 3), (1, 2), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected.Reverse()));
			}
			{
				//orderby has some temp allocations, this test is just to make sure those temp allocations don't fail
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderByDescending((it) => it.x, (l, r) => l - r).OrderBy((it) => it.x, (l, r) => r - l).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (3, 2), (2, 0), (1, 2), (1, 3), (0, 4) };
				Test.Assert(actual.SequenceEquals(expected));
			}
		}

		[Test]
		public static void ThenBy()
		{
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x).ThenBy((it) => it.y).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 2), (1, 3), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x, (l, r) => l - r).ThenBy((it) => it.y).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 2), (1, 3), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				//orderby has some temp allocations, this test is just to make sure those temp allocations don't fail
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x, (l, r) => l - r).OrderBy((it) => it.x, (l, r) => r - l).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (3, 2), (2, 0), (1, 2), (1, 3), (0, 4) };
				Test.Assert(actual.SequenceEquals(expected));
			}
		}

		
		[Test]
		public static void ThenByDescending()
		{
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x).ThenByDescending((it) => it.y).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 3), (1, 2), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
			{
				let data = scope List<(int x, int y)>() { (1, 2), (1, 3), (3, 2), (0, 4), (2, 0) };
				let actual = data.OrderBy((it) => it.x, (l, r) => l - r).ThenByDescending((it) => it.y).ToList(.. scope .());

				let expected = scope List<(int x, int y)>() { (0, 4), (1, 3), (1, 2), (2, 0), (3, 2) };
				Test.Assert(actual.SequenceEquals(expected));
			}
		}

		[Test]
		public static void SelectMany()
		{
			{
				let data = scope List<List<int>>()
					{
						scope List<int>(),
						scope List<int>(),
						scope List<int>(),
						scope List<int>(),
						scope List<int>()
					};

				for (var i < 5)
					for (var k < 2)
						data[i].Add(i * 2 + k);

				let actual = data.SelectMany((x) => x).ToList(.. scope .());
				let actual2 = data.SelectMany((x) => x.GetEnumerator()).ToList(.. scope .());

				Test.Assert(actual.SequenceEquals(scope int[](0, 1, 2, 3, 4, 5, 6, 7, 8, 9)));
				Test.Assert(actual2.SequenceEquals(scope int[](0, 1, 2, 3, 4, 5, 6, 7, 8, 9)));
			}
		}

#region Failures
#if INCLUDE_FAILURES
		[Test(ShouldFail = true)]
		public static void ElementAtSequenceError()
		{
			let data = scope List<int>(){ 1, 2, 3};
			data.ElementAt(4);
		}

		[Test(ShouldFail = true)]
		public static void FirstFatalOnEmpty()
		{
			let data = scope List<int>();
			data.First();
		}

		[Test(ShouldFail = true)]
		public static void LastFatalOnEmpty()
		{
			let data = scope List<int>();

			data.Last();
		}
#endif
#endregion

		[AttributeUsage(.Method)]
		public struct MyTestAttribute : Attribute
		{
		}

		[Reflect(.Methods), AlwaysInclude(IncludeAllMethods = true)]
		public struct ReflectionTest
		{
			[MyTest, AlwaysInclude, Reflect]
			public int HelloWorld() { return 1; }
		}

#region Reported bugs

		/*[Test]
		public static void HitGetMethodsReflectionIssue()
		{
			let actual = typeof(ReflectionTest).GetMethods().Where((m) => m.GetCustomAttribute<MyTestAttribute>() case .Ok).ToList(.. scope .());
			Test.Assert(actual.Count == 1);
		}*/

		[Test]
		public static void HigCallingMutatingIssue()
		{
			int[] test1 = scope .(10, 11, 10, 12, 13, 14, -1);
			let actual = test1.Reverse().Where((x) => x > 0 && x % 2 == 0).Sum();

			Test.Assert(actual == 46);
		}	
#endregion
	}
}
