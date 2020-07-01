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
		}
	}
}
