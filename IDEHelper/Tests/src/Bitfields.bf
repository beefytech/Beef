using System;
namespace Tests
{
	class Bitfields
	{
		enum EnumA
		{
			A = -64,
			B = 12,
			C = 63
		}

		struct StructA
		{
			[Bitfield<uint8>(.Public, "A")]
			[Bitfield<uint8>(.Public, .Bits(3), "B")]
			[Bitfield<EnumA>(.Public, "C")]
			[Bitfield(.Public, .ValueRange(0..<12), "D")]
			public int32 mVal;

			[Bitfield<uint8>(.Public, .AutoRev, "E")]
			[Bitfield<uint8>(.Public, .BitsRev(8), "F")]
			[Bitfield<uint8>(.Public, .ValueRangeRev(0..<256), "G")]
			[Bitfield<uint8>(.Public, .BitsAt(8, 0), "H")]
			public static int32 sVal2;
		}

		[Test]
		static void TestBasics()
		{
			Test.Assert(sizeof(StructA) == 4);
			StructA sa = .();
			sa.A = 0x12;
			sa.B = 7;
			sa.C = .C;
			sa.D = 9;
			StructA.E = 0x22;
			StructA.F = 0x33;
			StructA.G = 0x44;
			StructA.H = 0x55;
			Test.Assert(sa.A == 0x12);
			Test.Assert(sa.B == 7);
			Test.Assert(sa.C == .C);
			Test.Assert(sa.D == 9);

			Test.Assert(sa.mVal == 0x0025FF12);
			Test.Assert(StructA.sVal2 == 0x22334455);
		}
	}
}