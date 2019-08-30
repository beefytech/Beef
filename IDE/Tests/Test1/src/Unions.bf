#pragma warning disable 168

using System;

namespace IDETest
{
	class Unions
	{
		struct InnerA
		{
			public int32 mInt0;
			public int32 mInt1;
		}

		struct InnerB
		{
			public InnerA mInnerA;
		}

		[Union]
		struct UStruct
		{
			public InnerB mInnerB;
		}

		[Union]
		struct UStruct2
		{
			public InnerB mInnerB;
			public int64 mFullInt;
		}

		public static void UseUnion(UStruct us)
		{
			int a = us.mInnerB.mInnerA.mInt0;
			int b = us.mInnerB.mInnerA.mInt1;
		}

		public static void UseUnion2(UStruct2 us)
		{
			int a2 = us.mInnerB.mInnerA.mInt0;
			int b2 = us.mInnerB.mInnerA.mInt1;
		}

		public static void Test()
		{
			//Test_Start
			UStruct us;
			us.mInnerB.mInnerA.mInt0 = 12;
			us.mInnerB.mInnerA.mInt1 = 23;
			UseUnion(us);

			UStruct2 us2;
			us2.mFullInt = 0x11223344'55667788;
			UseUnion2(us2);
		}
	}
}
