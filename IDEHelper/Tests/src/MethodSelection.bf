using System;
using System.Collections;

namespace LibA
{
	extension Handler
	{
		public static int Handle(Tests.MethodSelection.StructA val)
		{
			return 4;
		}

		public static int Handle(LibA.LibAStruct val)
		{
			return 4;
		}

		public static int Handle(List<Tests.MethodSelection.StructA> val)
		{
			return 4;
		}
	}
}

namespace Tests
{
	class MethodSelection
	{
		public struct StructA
		{
			int mA;
		}

		// Enumerates different values than its span holds, so a result shows which AddRange overload ran
		class SpanProbe : IEnumerable<int32>
		{
			public List<int32> mEnumVals = new .() { 100, 200 } ~ delete _;
			public List<int32> mSpanVals = new .() { 1, 2, 3 } ~ delete _;

			public List<int32>.Enumerator GetEnumerator() => mEnumVals.GetEnumerator();
			public static implicit operator Span<int32>(SpanProbe probe) => probe.mSpanVals;
		}

		public static int MethodA(int8 a)
		{
			return 1;
		}

		public static int MethodA(uint8 a)
		{
			return 2;
		}

		public static int MethodA(int16 a)
		{
			return 3;
		}

		public static int MethodA(int32 a)
		{
			return 4;
		}

		public static int MethodB<T>(T foo) where T : class, delete
		{
		    return 1;
		}

		public static int MethodB<T>(T foo) where T : struct
		{
		    return 2;
		}

		public static int MethodB<K, V>((K key, V value) foo) where K : var where V : var
		{
		    return 3;
		}

		public static int MethodC<T>(T val) where T : struct
		{
		    return MethodB(val);
		}

		public static int MethodD<T>(ref T[] x)
		{
			return 1;
		}

		public static int MethodD<T>(ref T[][] x)
		{
			return 2;
		}

		public static int MethodE<T>(T val, int val2)
		{
			return 1;
		}

		public static int MethodE<T, TVal>(T val, TVal val2) where TVal : const int
		{
			return 2;
		}

		public static int MethodE<T>(List<T> val, int val2)
		{
			return 3;
		}

		public static int MethodE<T, TVal>(List<T> val, TVal val2) where TVal : const int
		{
			return 4;
		}

		[Test]
		public static void TestBasics()
		{
			Test.Assert(LibA.LibA0.GetOverload0<int8>() == 1);
			Test.Assert(LibA.LibA0.GetOverload0<int16>() == 0);
			Test.Assert(LibA.LibA0.GetOverload0<int32>() == 0);
			Test.Assert(LibA.LibA0.GetOverload0<int64>() == 0);

			Test.Assert(LibB.LibB0.GetOverload0<int8>() == 1);
			Test.Assert(LibB.LibB0.GetOverload0<int16>() == 2);
			Test.Assert(LibB.LibB0.GetOverload0<int32>() == 0);
			Test.Assert(LibB.LibB0.GetOverload0<int64>() == 0);

			Test.Assert(LibC.LibC0.GetOverload0<int8>() == 1);
			Test.Assert(LibC.LibC0.GetOverload0<int16>() == 3);
			Test.Assert(LibC.LibC0.GetOverload0<int32>() == 3);
			Test.Assert(LibC.LibC0.GetOverload0<int64>() == 0);

			StructA sa = .();
			List<StructA> sal = null;
			LibA.LibAStruct las = .();
			Test.Assert(LibA.Handler.HandleT(sa) == 4);
			Test.Assert(LibA.Handler.HandleT(sal) == 4);
			Test.Assert(LibA.Handler.HandleT(las) == 0);

			Test.Assert(MethodA(1) == 1);
			Test.Assert(MethodA(240) == 2);
			Test.Assert(MethodA(1000) == 3);
			Test.Assert(MethodA(1000000) == 4);

			Test.Assert(MethodB(11) == 2);
			Test.Assert(MethodB(("A", "B")) == 3);
			Test.Assert(MethodC(("A", "B")) == 3);

			int[][] arrArr = scope int[1][];
			Test.Assert(MethodD(ref arrArr) == 2);

			int a = 100;
			Test.Assert(MethodE(sa, a) == 1);
			Test.Assert(MethodE(sa, 100) == 2);
			Test.Assert(MethodE(sal, a) == 3);
			Test.Assert(MethodE(sal, 200) == 4);
		}

		[Test]
		public static void TestAddRangeSpan()
		{
			var list = scope List<int32>();
			list.AddRange(scope SpanProbe());
			Test.Assert((list.Count == 3) && (list[0] == 1) && (list[2] == 3));

			// Grows mid-append, which only the block copy survives
			list.AddRange(list);
			Test.Assert((list.Count == 6) && (list[3] == 1) && (list[5] == 3));

			int32[] arr = scope int32[](8, 9);
			list.Clear();
			list.AddRange(arr);
			Test.Assert((list.Count == 2) && (list[0] == 8) && (list[1] == 9));

			var other = scope List<int32>() { 5, 6, 7 };
			list.Clear();
			list.AddRange(.(other.Ptr, 2));
			Test.Assert((list.Count == 2) && (list[1] == 6));

			var hashSet = scope HashSet<int32>() { 42 };
			list.Clear();
			list.AddRange(hashSet);
			Test.Assert((list.Count == 1) && (list[0] == 42));
		}
	}
}
