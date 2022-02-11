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
			public int32 mVal2;
		}

		[Test]
		static void TestBasics()
		{
			Test.Assert(sizeof(StructA) == 8);
			StructA sa = .();
			sa.A = 0x12;
			sa.B = 7;
			sa.C = .C;
			sa.D = 9;
			sa.E = 0x22;
			sa.F = 0x33;
			sa.G = 0x44;
			sa.H = 0x55;
			Test.Assert(sa.A == 0x12);
			Test.Assert(sa.B == 7);
			Test.Assert(sa.C == .C);
			Test.Assert(sa.D == 9);

			Test.Assert(sa.mVal == 0x0025FF12);
			Test.Assert(sa.mVal2 == 0x22334455);
		}
	}
}