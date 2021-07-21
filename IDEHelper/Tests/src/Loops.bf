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

		static int sGetValCount = 0;

		static int GetVal()
		{
			return 10 + sGetValCount++ / 2;
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

			int iterations = 0;
			for (int i < GetVal())
			{
				iterations++;
			}
			Test.Assert(iterations == 19);
			Test.Assert(sGetValCount == 20);

			int total = 0;
			for (int i in 1..<10)
				total += i;
			Test.Assert(total == 1+2+3+4+5+6+7+8+9);

			total = 0;
			for (int i in 1...10)
				total += i;
			Test.Assert(total == 1+2+3+4+5+6+7+8+9+10);
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
