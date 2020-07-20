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
		}
	}
}
