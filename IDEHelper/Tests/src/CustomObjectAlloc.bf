#pragma warning disable 168

using System;
using System.Reflection;

namespace Tests
{
	class CustomObjectAlloc
	{
		// An allocator with an AllocObject method creates the object itself: it's given the type and
		// the full allocation size (including any debug info), and must set up the object's vdata
		public class ObjAllocator
		{
			const int GuardSize = 64;
			public TypeInstance mLastType;
			public int mLastSize;
			public uint8* mLastPtr;

			public void* AllocObject(TypeInstance type, int size)
			{
				mLastType = type;
				mLastSize = size;
				mLastPtr = (uint8*)Internal.StdMalloc(size + GuardSize);
				// Only the object's own fields are zeroed, the rest is left dirty
				Internal.MemSet(mLastPtr, 0xCC, size);
				Internal.MemSet(mLastPtr, 0, type.InstanceSize);
				Internal.MemSet(mLastPtr + size, 0, GuardSize);
				*(void**)mLastPtr = (void*)type.[Friend]mTypeClassVData;
				return mLastPtr;
			}

			public void Free(void* ptr)
			{
				Internal.StdFree(ptr);
			}

			public bool GuardIntact()
			{
				for (int i < GuardSize)
					if (mLastPtr[mLastSize + i] != 0)
						return false;
				return true;
			}
		}

		class ClassA
		{
			public int mA = 123;
		}

		[Test]
		public static void TestBasics()
		{
			ObjAllocator alloc = scope .();
			ClassA ca = new:alloc ClassA();
			Test.Assert(alloc.mLastType == typeof(ClassA));
			Test.Assert(ca.mA == 123);
			Test.Assert(alloc.GuardIntact());
			delete:alloc ca;
		}
	}
}
