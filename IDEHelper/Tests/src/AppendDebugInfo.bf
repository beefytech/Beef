#pragma warning disable 168

using System;
using System.Reflection;

namespace Tests
{
	// BeefProj.toml raises AllocStackTraceDepth for these types, so in debug builds each allocation
	// carries a captured stack trace, followed by an append-tracking record when the constructor
	// appends objects that need marking
	class AppendDebugInfo
	{
		class GuardAlloc : IRawAllocator
		{
			const int GuardSize = 64;
			public int mLastSize;
			public uint8* mLastPtr;

			public void* Alloc(int size, int align)
			{
				mLastSize = size;
				mLastPtr = (uint8*)Internal.StdMalloc(size + GuardSize);
				Internal.MemSet(mLastPtr, 0, size + GuardSize);
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

		class Child
		{
			public Object mObj;
		}

		[Reflect(.DefaultConstructor), AlwaysInclude(AssumeInstantiated=true)]
		class Owner
		{
			public Child mChildA;
			public Child mChildB;

			[AllowAppend]
			public this()
			{
				Child childA = append Child();
				Child childB = append Child();
				mChildA = childA;
				mChildB = childB;
			}
		}

		[Test]
		public static void TestCustomAllocator()
		{
			GuardAlloc alloc = scope .();
			Owner owner = new:alloc Owner();
			Test.Assert(alloc.GuardIntact());
			Test.Assert((owner.mChildA != null) && (owner.mChildB != null));
			delete:alloc owner;
		}

		[Test]
		public static void TestAllocObject()
		{
			// AllocObject leaves the append-tracking record dirty, so the runtime has to clear it
			CustomObjectAlloc.ObjAllocator alloc = scope .();
			Owner owner = new:alloc Owner();
			Test.Assert(alloc.mLastType == typeof(Owner));
			Test.Assert(alloc.GuardIntact());
			Test.Assert((owner.mChildA != null) && (owner.mChildB != null));
			delete:alloc owner;
		}

		[Test]
		public static void TestCreateObject()
		{
			Owner owner = (Owner)typeof(Owner).CreateObject().Value;
			Test.Assert((owner.mChildA != null) && (owner.mChildB != null));
			delete owner;

			GuardAlloc alloc = scope .();
			owner = (Owner)typeof(Owner).CreateObject(alloc).Value;
			Test.Assert(alloc.GuardIntact());
			Test.Assert((owner.mChildA != null) && (owner.mChildB != null));
			delete:alloc owner;
		}
	}
}
