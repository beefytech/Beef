#pragma warning disable 168

using System;
using System.Numerics;

namespace Tests
{
	class Numerics
	{
		// No UseLLVM: exercise the native Og+ vector path as well.
		[Test]
		public static void TestNativeVectors()
		{
			float4 a = .(12, 24, 36, 48);
			float4 b = .(3, 4, 6, 8);
			Test.Assert((a / b) === .(4, 6, 6, 6));
			Test.Assert((10.0f - b) === .(7, 6, 4, 2));
			Test.Assert(bool4.MoveMask(a > b) == 15);
			b.WZYX = .(1, 2, 3, 4);
			Test.Assert(b === .(4, 3, 2, 1));
			Test.Assert(float4.Sqrt(a).x > 3);
		}

		// Read at runtime so the optimizer cannot fold the vector operations below
		static int32 sVecZero = 0;

		struct VecHolder
		{
			public int32 mPad;
			public float4 mV;
		}

		struct IntVecHolder
		{
			public int32 mPad;
			public int32_4 mV;
		}

		// An indexed setter's value parameter comes ahead of its index (this, value, idx), which code generation
		//  used to swap - the LLVM path generated invalid IR and the native path never stored at all
		static mixin CheckVectorLanes()
		{
			int32 rt = sVecZero;

			float4 f = default;
			for (int lane < 4)
				f[lane] = (float)(100 + lane * 10 + rt);
			for (int lane < 4)
				Test.Assert(f[lane] == (float)(100 + lane * 10));
			Test.Assert(f === .(100, 110, 120, 130));
			f[2] = -1.0f + rt;
			Test.Assert(f === .(100, 110, -1, 130));

			float4 g = default;
			g[0] = 5.5f + rt;
			g[1] = 6.5f + rt;
			g[2] = 7.5f + rt;
			g[3] = 8.5f + rt;
			Test.Assert(g === .(5.5f, 6.5f, 7.5f, 8.5f));
			Test.Assert((g[0] == 5.5f) && (g[3] == 8.5f));

			// Index and value numbers differ, so swapped setter arguments cannot look right
			int32_4 n = default;
			for (int32 lane < 4)
				n[lane] = 1000 + lane * 7 + rt;
			for (int32 lane < 4)
				Test.Assert(n[lane] == 1000 + lane * 7);

			int32_4 m = default;
			m[3] = 41 + rt;
			m[2] = 42 + rt;
			m[1] = 43 + rt;
			m[0] = 44 + rt;
			Test.Assert(m === .(44, 43, 42, 41));
			Test.Assert((m[0] == 44) && (m[3] == 41));
		}

		// Legacy SSE packed arithmetic faults on a memory operand that is not 16-byte aligned, and vectors in
		//  memory - through a pointer, embedded in a struct, or in a stack slot - can be 4-byte aligned
		static mixin CheckUnalignedVectorOperands()
		{
			float rt = sVecZero;

			uint8[96] buffer = default;
			float4* p = (float4*)(void*)((((int)(void*)&buffer + 15) & ~15) + 4);
			*p = .(1 + rt, 2, 3, 4);
			float4 k = .(2 + rt, 2, 2, 2);
			Test.Assert((*p + k + *p) === .(4, 6, 8, 10));
			Test.Assert((k * *p - *p) === .(1, 2, 3, 4));
			Test.Assert((k * *p) === .(2, 4, 6, 8));
			Test.Assert(((k * *p) / *p) === .(2, 2, 2, 2));

			VecHolder[3] holders = default;
			holders[1].mV = .(1 + rt, 2, 3, 4);
			float4 k3 = .(3 + rt, 3, 3, 3);
			Test.Assert((k3 + holders[1].mV) === .(4, 5, 6, 7));
			Test.Assert((holders[1].mV * k3 - holders[1].mV) === .(2, 4, 6, 8));
			Test.Assert((k3 / holders[1].mV) === .(3, 1.5f, 1, 0.75f));
			VecHolder* holderPtr = &holders[2];
			holderPtr.mV = .(5 + rt, 6, 7, 8);
			Test.Assert((holderPtr.mV * k3 - holderPtr.mV) === .(10, 12, 14, 16));

			IntVecHolder[3] ints = default;
			ints[1].mV = .(1 + (int32)rt, 2, 3, 4);
			int32_4 ik = .(10 + (int32)rt, 20, 30, 40);
			Test.Assert((ik + ints[1].mV) === .(11, 22, 33, 44));
			Test.Assert((ik - ints[1].mV) === .(9, 18, 27, 36));
			Test.Assert((ik * ints[1].mV) === .(10, 40, 90, 160));
		}

		// Its own method so the frame layout puts the address-taken vector in a slot that is not 16-byte aligned
		static mixin CheckStackLocalVector(float4* outPtr)
		{
			int8 pad = 1;
			float rt = sVecZero;
			float4 dx = .(1 + rt, 2, 3, 4);
			float4 scale = .(3 + rt, 3, 3, 3);
			float4* addr = &dx; // forces dx into a stack slot
			*outPtr = *addr;
			Test.Assert((dx * scale - dx) === .(2, 4, 6, 8));
			Test.Assert((dx + scale + dx) === .(5, 7, 9, 11));
			Test.Assert(((dx * scale) / dx) === .(3, 3, 3, 3));
			Test.Assert(pad == 1);
		}

		static void StackLocalVector(float4* outPtr)
		{
			CheckStackLocalVector!(outPtr);
		}

		[UseLLVM]
		static void StackLocalVectorLLVM(float4* outPtr)
		{
			CheckStackLocalVector!(outPtr);
		}

		// A struct whose only field is a vector is lowered to that vector, its this included. The copy a method
		//  keeps of a lowered this is written with an aligned vector store, so it needs the struct's alignment
		[Align(16)]
		struct LoweredVec
		{
			public float4 mV;

			public this(float4 v) { mV = v; }

			[Inline]
			public float SumInline() => mV.x + mV.y + mV.z + mV.w;

			public float Sum() => mV.x + mV.y + mV.z + mV.w;

			[Inline]
			public LoweredVec ScaledInline(float s) => .(mV * s);
		}

		static float SumLoweredThis(LoweredVec v)
		{
			int8 pad = 1;
			return v.SumInline() + v.Sum() + v.ScaledInline(2.0f).SumInline() + pad - 1;
		}

		// Read at runtime so shift counts are not constant-folded
		static int sShiftCount3 = 3;
		static int sShiftCount16 = 16;
		static int sShiftCount32 = 32;
		static int sShiftCountNeg = -1;

		static int32_4 ScalarSar(int32_4 v, int count)
		{
			int32_4 r = default;
			for (int32 lane < 4)
				r[lane] = v[lane] >> count;
			return r;
		}

		static int32_4 ScalarShl(int32_4 v, int count)
		{
			int32_4 r = default;
			for (int32 lane < 4)
				r[lane] = (int32)((uint32)v[lane] << count);
			return r;
		}

		// int32_4 shifts used to be encoded with 16-bit lanes natively (PSRAW rather than PSRAD), a runtime count
		//  was read from the wrong register, and the LLVM path did not lower the shift intrinsics at all. Counts at
		//  or past the lane width saturate the same way on both backends: >> fills with the sign bit, << gives zero
		static mixin CheckVectorShifts()
		{
			int32_4 v = .(0x12345678, -1, -123456789, (int32)0x80000001);
			Test.Assert((v >> 0) === ScalarSar(v, 0));
			Test.Assert((v >> 1) === ScalarSar(v, 1));
			Test.Assert((v >> 16) === ScalarSar(v, 16));
			Test.Assert((v >> 31) === ScalarSar(v, 31));
			Test.Assert((v << 1) === ScalarShl(v, 1));
			Test.Assert((v << 16) === ScalarShl(v, 16));
			Test.Assert((v << 31) === ScalarShl(v, 31));
			Test.Assert((v >> sShiftCount3) === ScalarSar(v, 3));
			Test.Assert((v >> sShiftCount16) === ScalarSar(v, 16));
			Test.Assert((v << sShiftCount3) === ScalarShl(v, 3));
			Test.Assert((v << sShiftCount16) === ScalarShl(v, 16));

			int32_4 signFill = .(0, -1, -1, -1);
			Test.Assert((v >> sShiftCount32) === signFill);
			Test.Assert((v >> sShiftCountNeg) === signFill);
			Test.Assert((v << sShiftCount32) === default(int32_4));
			Test.Assert((v << sShiftCountNeg) === default(int32_4));

			// The integer hash that exposed this, against its scalar form
			int32_4 hash = .(0, 1, -1, -123456789);
			hash = (hash ^ ((hash >> 16) & 0xFFFF)) * 0x7FEB352D;
			uint32 scalar = (uint32)-123456789;
			scalar = (scalar ^ (scalar >> 16)) &* 0x7FEB352D;
			Test.Assert(hash[3] == (int32)scalar);
		}

		[Test]
		public static void TestVectorShifts()
		{
			CheckVectorShifts!();
		}

		[Test, UseLLVM]
		public static void TestVectorShiftsLLVM()
		{
			CheckVectorShifts!();
		}

		[Test]
		public static void TestVectorLanes()
		{
			CheckVectorLanes!();
		}

		[Test, UseLLVM]
		public static void TestVectorLanesLLVM()
		{
			CheckVectorLanes!();
		}

		[Test]
		public static void TestUnalignedVectorOperands()
		{
			CheckUnalignedVectorOperands!();
			float4 sink = default;
			StackLocalVector(&sink);
		}

		[Test, UseLLVM]
		public static void TestUnalignedVectorOperandsLLVM()
		{
			CheckUnalignedVectorOperands!();
			float4 sink = default;
			StackLocalVectorLLVM(&sink);
		}

		[Test]
		public static void TestLoweredThisAlignment()
		{
			float rt = sVecZero;
			Test.Assert(SumLoweredThis(.(.(1 + rt, 2, 3, 4))) == 40);
		}

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

			float4 v3 = v0.WZYX;
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
