#pragma warning disable 168

using System;

namespace Tests
{
	class Append
	{
		class ClassA
		{
			int32 mA = 0x11223344;

			[AllowAppend]
			public this()
			{
				uint8* ptr = append uint8[3]*;
				ptr[0] = 0x55;
				ptr[1] = 0x66;
				ptr[2] = 0x77;
			}
		}

		class ClassB
		{
			[AllowAppend]
			public this(int16 i, int32 j)
			{
				uint8* ptr = append uint8[i]*;
				for (int k < i)
					ptr[k] = (uint8)(0xA0 | k);
			}
		}

		class ClassC : ClassB
		{
			public static int sCallCount;

			static int GetIntVal()
			{
				sCallCount++;
				return 123;
			}

			[AllowAppend]
			public this(int16 i) : base(i, (int32)GetIntVal())
			{
				uint32* val = append uint32[2]*;
				val[0] = 0x11223344;
				val[1] = 0x55667788;
			}
		}

		class ClassD
		{
			int16 mA = 0x1122;

			[AllowAppend]
			public this()
			{
				uint32* val = append uint32[1]*;
				uint8* val2 = append uint8[1]*;
				val[0] = 0x33445566;
				//Foof();

				val2[0] = 0xBB;
			}
		}

		class ClassE : ClassD
		{
			[AllowAppend]
			public this()
			{
				uint64* val = append uint64[1]*;
				val[0] = 0x1020304050607080L;
			}
		}

		class ClassF
		{
			public int mA = 123;
			public append String mB = .(mA);
			public int mC = 234;
		}

		static void CheckData(Object obj, int lastAllocSize, uint8[] data)
		{
			int objSize = typeof(Object).InstanceSize;
			Test.Assert(lastAllocSize == data.Count + objSize);
			uint8* ptr = (uint8*)Internal.UnsafeCastToPtr(obj) + objSize;
			for (int i < data.Count)
				Test.Assert(ptr[i] == data[i]);
		}

		[Test]
		public static void Test()
		{
			TrackedAlloc trackedAlloc = scope .();

			int objSize = typeof(Object).InstanceSize;

			let ca = new:trackedAlloc ClassA();
			CheckData(ca, trackedAlloc.mLastAllocSize, scope .(
				0x44, 0x33, 0x22, 0x11,  0x55, 0x66, 0x77));
			delete:trackedAlloc ca;

			let cc = new:trackedAlloc ClassC(10);
			Test.Assert(ClassC.sCallCount == 1); // This ensures we had a constant evaluation of the size
			CheckData(cc, trackedAlloc.mLastAllocSize, scope .(
				0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11,
				0x88, 0x77, 0x66, 0x55));
			delete:trackedAlloc cc;

			let ce = new:trackedAlloc ClassE();
			CheckData(cc, trackedAlloc.mLastAllocSize, scope .(
				0x22, 0x11, 0x00, 0x00, 0x66, 0x55, 0x44, 0x33, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x80, 0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10
				));
			delete:trackedAlloc ce;

			int sizeDiff = Math.Abs(typeof(ClassF).InstanceSize - (1024 + sizeof(int)*5));
			Test.Assert(sizeDiff < 32);

			ClassF cf = scope .();
			cf.mB.Append("Abc");
			Test.Assert(cf.mA == 123);
			Test.Assert(cf.mB == "Abc");
			Test.Assert(cf.mB.AllocSize == 1024);
			Test.Assert(cf.mC == 234);
			cf.mB.Append('!', 2048);
		}
	}
}
