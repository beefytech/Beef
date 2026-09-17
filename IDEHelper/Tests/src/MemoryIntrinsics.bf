using System;

namespace Tests
{
	class MemoryIntrinsics
	{
		[Export]
		public static int Compare(void* lhs, void* rhs, int length) => Internal.MemCmp(lhs, rhs, length);

		[Export]
		public static int CompareFour(void* lhs, void* rhs) => Internal.MemCmp(lhs, rhs, 4);

		public static bool CheckComparisons()
		{
			uint8[6] lhs = .(0xAA, 0, 0x7F, 0x80, 0xFF, 1);
			uint8[6] rhs = .(0xBB, 0, 0x7F, 0x80, 0xFF, 2);
			if ((Compare(null, null, 0) != 0) ||
				(Compare(&lhs[0], &rhs[0], 0) != 0) ||
				(Compare(&lhs[0], &lhs[0], lhs.Count) != 0) ||
				(CompareFour(&lhs[1], &rhs[1]) != 0) ||
				(Compare(&lhs[1], &rhs[1], 5) >= 0) ||
				(Compare(&rhs[1], &lhs[1], 5) <= 0))
				return false;

			// Byte ordering is unsigned and determined by the first difference.
			rhs[2] = 0x80;
			rhs[3] = 0;
			if ((CompareFour(&lhs[1], &rhs[1]) >= 0) ||
				(CompareFour(&rhs[1], &lhs[1]) <= 0))
				return false;

			uint8[257] largeA = default;
			uint8[257] largeB = default;
			largeB[256] = 0xFF;
			return (Compare(&largeA[0], &largeB[0], 256) == 0) &&
				(Compare(&largeA[0], &largeB[0], 257) < 0) &&
				(Compare(&largeB[0], &largeA[0], 257) > 0);
		}

		[Test]
		public static void TestMemCmp()
		{
			Test.Assert(CheckComparisons());
			const bool comptime = CheckComparisons();
			Test.Assert(comptime);
		}
	}
}
