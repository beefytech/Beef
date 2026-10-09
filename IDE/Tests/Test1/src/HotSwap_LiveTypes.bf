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

		class ClassA
		{
			public int mA = 1;
			/*ClassA_mA2
			public int mA2 = 2;
			*/
		}

		static ObjAllocator sAlloc = new .() ~ delete _;
		static ClassA sA;

		static void AllocA()
		{
			sA = new:sAlloc ClassA();
		}

		static void FreeA()
		{
			delete:sAlloc sA;
			sA = null;
		}

		public static void Test()
		{
			AllocA();
			//Test_A_live
			int step = 1;
			FreeA();
			//Test_A_freed
			step = 2;
		}
	}
}
