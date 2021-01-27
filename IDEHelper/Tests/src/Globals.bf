using System;

namespace Tests
{
	class Globals
	{
		public struct StructA : this(int64 a, int32 b)
		{
		}

		static int sVal0 = 123;
		static int sVal1 = 234;
		static int sVal2 = 345;

		public const String[3][2] cValStrs = .(("A", "B"), ("C", "D"), );
		public const String[3][2] cValStrs2 = .(.("A", "B"), .("C", "D"), );
		public static String[3][2] sValStrs = .(("A", "B"), ("C", "D"), );
		public static String[3][2] sValStrs2 = .(.("A", "B"), .("C", "D"), );

		public const int*[3][2] cValsInt = .((&sVal0, &sVal1), (&sVal2, ), );
		public static int*[3][2] sValsInt = .((&sVal0, &sVal1), (&sVal2, ), );

		public const StructA[2] cValsStruct = .(.(1, 2), .(3, 4));
		public static StructA[2] sValsStruct = .(.(1, 2), .(3, 4));

		public const StructA cValStruct = cValsStruct[0];
		public static StructA sValStruct = sValsStruct[0];

		[Test]
		public static void TestBasics()
		{
			const bool cEq0 = cValStrs[0][0] == "A";
			const bool cEq1 = cValStrs[0][1] == "A";
			Test.Assert(cEq0);
			Test.Assert(!cEq1);

			Test.Assert(cValStrs[0][0] === "A");
			Test.Assert(cValStrs[0][1] === "B");
			Test.Assert(cValStrs[1][0] === "C");
			Test.Assert(cValStrs[1][1] === "D");

			Test.Assert(cValStrs2[0][0] === "A");
			Test.Assert(cValStrs2[0][1] === "B");
			Test.Assert(cValStrs2[1][0] === "C");
			Test.Assert(cValStrs2[1][1] === "D");

			Test.Assert(sValStrs[0][0] === "A");
			Test.Assert(sValStrs[0][1] === "B");
			Test.Assert(sValStrs[1][0] === "C");
			Test.Assert(sValStrs[1][1] === "D");

			Test.Assert(*(cValsInt[0][0]) == 123);
			Test.Assert(*(cValsInt[0][1]) == 234);
			Test.Assert(*(cValsInt[1][0]) == 345);

			Test.Assert(cValsStruct[0].a == 1);
			Test.Assert(cValsStruct[0].b == 2);
			Test.Assert(cValsStruct[1].a == 3);
			Test.Assert(cValsStruct[1].b == 4);

			Test.Assert(sValsStruct[0].a == 1);
			Test.Assert(sValsStruct[0].b == 2);
			Test.Assert(sValsStruct[1].a == 3);
			Test.Assert(sValsStruct[1].b == 4);

			Test.Assert(cValStruct.a == 1);
			Test.Assert(cValStruct.b == 2);
			Test.Assert(sValStruct.a == 1);
			Test.Assert(sValStruct.b == 2);

			const int* iPtr = cValsInt[2][0];
			Test.Assert(iPtr == null);

			Test.Assert(LibSpace.MethodA() == 100);
			Test.Assert(LibSpace.MethodB() == 200);
		}
	}
}
