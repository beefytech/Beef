#pragma warning disable 168

using System;
using System.Collections;
using System.Diagnostics;

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
			{
				if (i == 5)
					Test.Assert(total == 1+2+3+4);
				total += i;
			}
			Test.Assert(total == 1+2+3+4+5+6+7+8+9);

			total = 0;
			for (int i in 1...10)
			{
				if (i == 5)
					Test.Assert(total == 1+2+3+4);
				total += i;
			}
			Test.Assert(total == 1+2+3+4+5+6+7+8+9+10);

			total = 0;
			for (int i in (1..<10).Reversed)
			{
				if (i == 5)
					Test.Assert(total == 9+8+7+6);
				total += i;

			}
			Test.Assert(total == 9+8+7+6+5+4+3+2+1);

			total = 0;
			for (int i in (1...10).Reversed)
			{
				if (i == 5)
					Test.Assert(total == 10+9+8+7+6);
				total += i;

			}
			Test.Assert(total == 10+9+8+7+6+5+4+3+2+1);

			Test.Assert(!(1...3).Contains(0));
			Test.Assert((1...3).Contains(1));
			Test.Assert((1...3).Contains(2));
			Test.Assert((1...3).Contains(3));
			Test.Assert(!(1...3).Contains(4));

			Test.Assert(!(1..<3).Contains(0));
			Test.Assert((1..<3).Contains(1));
			Test.Assert((1..<3).Contains(2));
			Test.Assert(!(1..<3).Contains(3));

			Test.Assert((1...3).Contains(1...3));
			Test.Assert((1...3).Contains(1...2));
			Test.Assert(!(1...3).Contains(1...4));
			Test.Assert((1...3).Contains(2..<3));
			Test.Assert((1...3).Contains(2..<4));
			Test.Assert(!(1...3).Contains(2..<5));
			Test.Assert(!(1..<3).Contains(1...3));
			Test.Assert((1..<3).Contains(1..<3));
			Test.Assert(!(1..<3).Contains(1..<4));

			List<int> iList = scope .() { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
			int itrCount = 0;
			total = 0;
			for (int i in iList[...])
			{
				itrCount++;
				total += i;
			}
			Test.Assert(itrCount == 10);
			Test.Assert(total == 10+20+30+40+50+60+70+80+90+100);

			total = 0;
			itrCount = 0;
			for (int i in iList[...].Reversed)
			{
				itrCount++;
				total += i;
			}
			Test.Assert(itrCount == 10);
			Test.Assert(total == 10+20+30+40+50+60+70+80+90+100);

			total = 0;
			for (int i in iList[1...])
				total += i;
			Test.Assert(total == 20+30+40+50+60+70+80+90+100);

			total = 0;
			for (int i in iList[...^2])
				total += i;
			Test.Assert(total == 10+20+30+40+50+60+70+80+90);

			total = 0;
			for (int i in iList[..<^2])
				total += i;
			Test.Assert(total == 10+20+30+40+50+60+70+80);

			int itr = 0;
			total = 0;
			for (int i in iList[...^2][1...^2].Reversed)
			{
				if (itr == 1)
					Test.Assert(i == 70);
				total += i;
				++itr;
			}
			Test.Assert(total == 80+70+60+50+40+30+20);

			var str = scope String();
			(2...^3).ToString(str);
			Test.Assert(str == "2...^3");

			int[] iEmptyArr = scope .();
			var emptySpan = iEmptyArr[...];

			for (var i in emptySpan.Reversed)
				Test.FatalError();
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
