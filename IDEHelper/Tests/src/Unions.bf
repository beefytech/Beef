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
		}
	}
}
