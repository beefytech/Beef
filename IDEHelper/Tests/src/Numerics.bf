#pragma warning disable 168

using System;
using System.Numerics;

namespace Tests
{
	class Numerics
	{
		[Test, UseLLVM]
		public static void TestBasics()
		{
			float4 v0 = .(1, 2, 3, 4);
			float4 v1 = .(10, 100, 1000, 10000);

			float4 v2 = v0 * v1;
			Test.Assert(v2 === .(10, 200, 3000, 40000));
			Test.Assert(v2 !== .(10, 200, 3000, 9));
			Test.Assert(v2.x == 10);
			Test.Assert(v2.y == 200);
			Test.Assert(v2.z == 3000);
			Test.Assert(v2.w == 40000);

			float4 v3 = v0.wzyx;
			Test.Assert(v3 === .(4, 3, 2, 1));

			Result<uint16> r0 = 123;
			Result<int16> r1 = 2000;
			Result<uint32> r2 = 3000;

			uint16 v4 = r0 + 123;
			var v5 = r0 + 2;
			Test.Assert(v5.GetType() == typeof(uint16));
			var v6 = r0 + r0;
			Test.Assert(v6.GetType() == typeof(uint16));
			var v7 = r0 + r1;
			Test.Assert(v7.GetType() == typeof(int));
			var v8 = r0 + r2;
			Test.Assert(v8.GetType() == typeof(uint32));
			var v9 = r2 + r0;
			Test.Assert(v9.GetType() == typeof(uint32));

			Test.Assert(uint8.Parse("255") == 255);
			Test.Assert(uint8.Parse("0xFF", .AllowHexSpecifier) == 255);
			Test.Assert(uint8.Parse("256") == .Err(.Overflow));
			Test.Assert(uint8.Parse("789") == .Err(.Overflow));
			Test.Assert(uint8.Parse("999") == .Err(.Overflow));

			Test.Assert(int8.Parse("-128") == -128);
			Test.Assert(int8.Parse("-129") == .Err(.Overflow));
			Test.Assert(int8.Parse("127") == 127);
			Test.Assert(int8.Parse("128") == .Err(.Overflow));

			Test.Assert(uint16.Parse("65535") == 65535);
			Test.Assert(uint16.Parse("0xFFFF", .AllowHexSpecifier) == 65535);
			Test.Assert(uint16.Parse("65536") == .Err(.Overflow));
			Test.Assert(uint16.Parse("70000") == .Err(.Overflow));
			Test.Assert(uint16.Parse("80000") == .Err(.Overflow));
			Test.Assert(uint16.Parse("90000") == .Err(.Overflow));

			Test.Assert(int16.Parse("-32768") == -32768);
			Test.Assert(int16.Parse("-32769") == .Err(.Overflow));
			Test.Assert(int16.Parse("32767") == 32767);
			Test.Assert(int16.Parse("32768") == .Err(.Overflow));

			Test.Assert(uint32.Parse("4294967295") == 4294967295);
			Test.Assert(uint32.Parse("0xFFFFFFFF", .AllowHexSpecifier) == 4294967295);
			Test.Assert(uint32.Parse("4294967296") == .Err(.Overflow));
			Test.Assert(uint32.Parse("5'000'000'000") == .Err(.Overflow));

			Test.Assert(int32.Parse("-2147483648") == -2147483648);
			Test.Assert(int32.Parse("-2147483649") == .Err(.Overflow));
			Test.Assert(int32.Parse("2147483647") == 2147483647);
			Test.Assert(int32.Parse("2147483648") == .Err(.Overflow));
			Test.Assert(int32.Parse("3000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("4000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("5000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("6000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("-3000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("-4000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("-5000000000") == .Err(.Overflow));
			Test.Assert(int32.Parse("-6000000000") == .Err(.Overflow));
		}
	}
}
