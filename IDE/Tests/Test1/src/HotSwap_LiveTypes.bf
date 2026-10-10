#pragma warning disable 168

using System;
using System.Reflection;

namespace IDETest
{
	class HotSwap_LiveTypes
	{
		class ObjAllocator
		{
			public void* AllocObject(TypeInstance type, int size)
			{
				void* ptr = Internal.StdMalloc(size);
				Internal.MemSet(ptr, 0, size);
				*(void**)ptr = (void*)type.[Friend]mTypeClassVData;
				return ptr;
			}

			public void Free(void* ptr)
			{
				Internal.StdFree(ptr);
			}
		}

		class RawAllocator
		{
			public void* Alloc(int size, int align)
			{
				void* ptr = Internal.StdMalloc(size);
				Internal.MemSet(ptr, 0, size);
				return ptr;
			}

			public void Free(void* ptr)
			{
				Internal.StdFree(ptr);
			}
		}

		class ClassA
		{
			public int mA = 1;
			/*ClassA_mA2
			public int mA2 = 2;
			*/
		}

		class ClassB
		{
			public int mB = 1;
			/*ClassB_mB2
			public int mB2 = 2;
			*/
		}

		static ObjAllocator sAlloc = new .() ~ delete _;
		static RawAllocator sRawAlloc = new .() ~ delete _;
		static ClassA sA;
		static ClassB sB;

		static void AllocA()
		{
			sA = new:sAlloc ClassA();
		}

		static void AllocB()
		{
			sB = new:sRawAlloc ClassB();
		}

		static void FreeA()
		{
			delete:sAlloc sA;
			sA = null;
		}

		static void FreeB()
		{
			delete:sRawAlloc sB;
			sB = null;
		}

		public static void Test()
		{
			AllocA();
			AllocB();
			//Test_A_live
			int step = 1;
			FreeB();
			//Test_B_freed
			step = 2;
			FreeA();
			//Test_A_freed
			step = 3;
		}
	}
}
