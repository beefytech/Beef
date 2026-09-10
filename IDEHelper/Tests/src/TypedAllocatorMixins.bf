using System;
using System.Collections;

namespace Tests
{
	class TypedAllocatorMixins
	{
		static int sDestroyed;

		class TrackedList : List<int32>
		{
			[AllowAppend]
			public this(int capacity) : base(capacity) {}

			public ~this()
			{
				Test.Assert(Count == 2000);
				sDestroyed++;
			}
		}

		// Custom-allocated objects need destruction separately from buffer release.
		static mixin ScopedAllocTyped<T>(int size, int align)
		{
			void* data;
			if (size <= 128)
			{
				data = scope:mixin [Align(align)] uint8[size]* (?);
				if (typeof(T).IsObject)
					defer:mixin delete:null Internal.UnsafeCastToObject(data);
			}
			else
			{
				data = new [Align(align)] uint8[size]* (?);
				defer:mixin delete data;
				// Defers run in reverse order: destructor first, buffer release second.
				if (typeof(T).IsObject)
					defer:mixin delete:null Internal.UnsafeCastToObject(data);
			}
			data
		}

		static void AllocateList(int capacity)
		{
			TrackedList list = new:ScopedAllocTyped! .(capacity);
			for (int32 i < 2000)
				list.Add(i);
			Test.Assert(sDestroyed == 0);
		}

		class Allocator
		{
			public mixin Alloc<T>(int size, int align)
			{
				void* data = new [Align(align)] uint8[size]* (?);
				defer:mixin delete data;
				if (typeof(T).IsObject)
					defer:mixin delete:null Internal.UnsafeCastToObject(data);
				data
			}
		}

		static void GenericCaller<T>()
		{
			mixin LocalAlloc<TAlloc>(int size, int align)
			{
				Test.Assert(typeof(T) == typeof(int32));
				Test.Assert(typeof(TAlloc) == typeof(int32*));
				void* data = new [Align(align)] uint8[size]* (?);
				defer:mixin delete data;
				data
			}
			int32* values = new:LocalAlloc! int32[64]*;
			values[63] = 123;
			Test.Assert(values[63] == 123);
		}

		[Test, UseLLVM]
		static void TestLLVMAndAllocatorObject()
		{
			sDestroyed = 0;
			{
				TrackedList list = new:ScopedAllocTyped! .(1000);
				for (int32 i < 2000)
					list.Add(i);
				Test.Assert(sDestroyed == 0);
			}
			Test.Assert(sDestroyed == 1);
			Allocator allocator = scope .();
			{
				TrackedList list = new:allocator .(1000);
				for (int32 i < 2000)
					list.Add(i);
				Test.Assert(sDestroyed == 1);
			}
			Test.Assert(sDestroyed == 2);
			GenericCaller<int32>();
		}

		[Test]
		static void TestLifetime()
		{
			sDestroyed = 0;
			AllocateList(1000);
			Test.Assert(sDestroyed == 1);
			sDestroyed = 0;
			AllocateList(0);
			Test.Assert(sDestroyed == 1);
			// Raw object-reference storage must not be mistaken for an Object.
			Object* refs = new:ScopedAllocTyped! Object[256]*;
			Test.Assert(refs[0] == null);
			int32* ints = new:ScopedAllocTyped! int32[256]*;
			ints[255] = 42;
			Test.Assert(ints[255] == 42);
			// Keep the non-generic allocator contract working.
			uint8* bytes = new:ScopedAlloc! uint8[256]*;
			bytes[255] = 123;
			Test.Assert(bytes[255] == 123);
		}
	}
}
