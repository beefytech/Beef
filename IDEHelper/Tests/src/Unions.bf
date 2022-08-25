using System;

namespace Tests
{
	class Unions
	{
		// Not really a union
		[Union]
		struct UnionA
		{
			public int32 mInt32;
		}

		[Union]
		struct UnionB
		{
			public int32 mInt32;
			public float mFloat;
		}

		[Union]
		struct UnionC
		{
			public int32 mInt32 = 0;
			public float mFloat;

			public this()
			{
			}
		}

		[Union]
		struct UnionD : UnionC
		{
			public int16 mInt16;
		}

		[Test]
		static void TestBasics()
		{
			UnionA ua = .();
			ua.mInt32 = 123;
			Test.Assert(sizeof(UnionA) == 4);

			UnionB ub = .();
			ub.mInt32 = 123;
			*((float*)&ub.mInt32) = 1.2f;
			Test.Assert(ub.mFloat == 1.2f);
			Test.Assert(sizeof(UnionB) == 4);

			UnionD ud = .();
			ud.mInt32 = 123;
			ud.mInt16 = 234;
			Test.Assert(sizeof(UnionD) == 6);
			Test.Assert(alignof(UnionD) == 4);
			Test.Assert(((int16*)&ud)[2] == 234);
		}
	}
}
