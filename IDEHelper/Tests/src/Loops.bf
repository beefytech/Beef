#pragma warning disable 168

using System;
using System.Collections;

namespace Tests
{
	class Loops
	{
		struct StructA
		{
			public int32 mA;
			public int32 mB;
		}

		class EnumeratorTest : IEnumerator<int32>, IDisposable
		{
			public int mDispCount;

			public void Dispose()
			{
				mDispCount++;
			}

			public Result<int32> GetNext()
			{
				return .Err;
			}
		}

		[Test]
		public static void TestBasics()
		{
			while (false)
			{

			}

			StructA[0] zeroLoop = default;
			for (var val in zeroLoop)
			{
				StructA sa = val;
				int idx = @val;
			}

			var e = scope EnumeratorTest();
			for (var val in e)
			{
			}
			Test.Assert(e.mDispCount == 1);
			for (var val in e)
			{
				break;
			}
			Test.Assert(e.mDispCount == 2);
			TestEnumerator1(e);
			Test.Assert(e.mDispCount == 3);
			TestEnumerator2(e);
			Test.Assert(e.mDispCount == 4);
		}

		public static void TestEnumerator1(EnumeratorTest e)
		{
			for (var val in e)
			{
				return;
			}
		}

		public static void TestEnumerator2(EnumeratorTest e)
		{
			for (var val in e)
			{
				return;
			}
		}
	}
}
