#pragma warning disable 168

using System;
using System.Globalization;

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

		public static void Int64ParseTest(StringView string, int64 expectedResult, NumberStyles style = .Number)
		{
			int64 result = int64.Parse(string, style);
			Test.Assert(expectedResult == result);
		}

		[Test]
		public static void TestInt64Parse()
		{
			Int64ParseTest("1234567890", 1234567890L);
			Int64ParseTest("+1234567890", 1234567890L);
			Int64ParseTest("-9876543210", -9876543210L);
			Int64ParseTest("0x123456789abcdef", 81985529216486895L, .HexNumber);
			Int64ParseTest("0X123456789ABCDEF", 81985529216486895L, .HexNumber);
			Int64ParseTest("+0x123456789abcdef", 81985529216486895L, .HexNumber);
			Int64ParseTest("-0x76543210fedcba", -33306621262093498L, .HexNumber);
		}

		public static void Int64ParseErrorTest(StringView string, NumberStyles style = .Number)
		{
			Test.Assert(int64.Parse(string, style) case .Err);
		}

		[Test]
		public static void TestInt64ParseError()
		{
			Int64ParseErrorTest("");
			Int64ParseErrorTest("-");
			Int64ParseErrorTest("+");
			Int64ParseErrorTest("0x", .HexNumber);
			Int64ParseErrorTest("0X", .HexNumber);
			Int64ParseErrorTest("+0x", .HexNumber);
			Int64ParseErrorTest("+0X", .HexNumber);
			Int64ParseErrorTest("-0x", .HexNumber);
			Int64ParseErrorTest("-0X", .HexNumber);
		}

		public static void Uint64ParseTest(StringView string, uint64 expectedResult, NumberStyles style = .Number)
		{
			uint64 result = uint64.Parse(string, style);
			Test.Assert(expectedResult == result);
		}

		[Test]
		public static void TestUint64Parse()
		{
			Uint64ParseTest("1234567890", 1234567890UL);
			Uint64ParseTest("+9876543210", 9876543210UL);
			Uint64ParseTest("0x123456789abcdef", 81985529216486895UL, .HexNumber);
			Uint64ParseTest("0X123456789ABCDEF", 81985529216486895UL, .HexNumber);
			Uint64ParseTest("+0xfedcba9876543210", 18364758544493064720UL, .HexNumber);
		}

		public static void Uint64ParseErrorTest(StringView string, NumberStyles style = .Number)
		{
			Test.Assert(uint64.Parse(string, style) case .Err);
		}

		[Test]
		public static void TestUint64ParseError()
		{
			Uint64ParseErrorTest("");
			Uint64ParseErrorTest("+");
			Uint64ParseErrorTest("0x", .HexNumber);
			Uint64ParseErrorTest("0X", .HexNumber);
			Uint64ParseErrorTest("+0x", .HexNumber);
			Uint64ParseErrorTest("+0X", .HexNumber);
		}

		public static void MinMaxTest<T>(T expectedMinValue, T expectedMaxValue)
		where T : IMinMaxValue<T>
		where int : operator T <=> T
		{
			Test.Assert(T.MinValue == expectedMinValue);
			Test.Assert(T.MaxValue == expectedMaxValue);
		}

		[Test]
		public static void TestMinMax()
		{
			MinMaxTest<int>(Int.MinValue, Int.MaxValue);
			MinMaxTest<int8>(Int8.MinValue, Int8.MaxValue);
			MinMaxTest<int16>(Int16.MinValue, Int16.MaxValue);
			MinMaxTest<int32>(Int32.MinValue, Int32.MaxValue);
			MinMaxTest<int64>(Int64.MinValue, Int64.MaxValue);

			MinMaxTest<uint>(UInt.MinValue, UInt.MaxValue);
			MinMaxTest<uint8>(UInt8.MinValue, UInt8.MaxValue);
			MinMaxTest<uint16>(UInt16.MinValue, UInt16.MaxValue);
			MinMaxTest<uint32>(UInt32.MinValue, UInt32.MaxValue);
			MinMaxTest<uint64>(UInt64.MinValue, UInt64.MaxValue);
		}
	}
}
