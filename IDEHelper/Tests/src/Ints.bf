#pragma warning disable 168

using System;

namespace Tests
{
	class Ints
	{
		[Test]
		public static void TestBasics()
		{
			int i;
			decltype(i + 100) j = 123;
			Test.Assert(typeof(decltype(j)) == typeof(int));
			int k = j + 10;
		}

		[Test]
		public static void TestUInt64()
		{
			var str = scope String();

			uint64 val = 0x11223344'55667788;

			str.AppendF("{0:X}", val);
			//Test.Assert(str == "1122334455667788");
		}

		[Test]
		public static void TestInt64()
		{
			Test.Assert(Math.Min((int64)-1, (int64)0) == -1);
			Test.Assert(Math.Max((int64)-1, (int64)0) == 0);
		}

		[Test]
		public static void TestLiterals()
		{
			Test.Assert(0b0111010110111100110100010101 == 123456789);
			Test.Assert(0o726746425 == 123456789);

			int i0 = 5;
			int i1 = i0 % 1;
			Test.Assert(i1 == 0);
		}
	}
}
