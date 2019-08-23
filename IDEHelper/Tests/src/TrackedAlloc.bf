using System;

namespace Tests
{
	class TrackedAlloc
	{
		public int mLastAllocSize;

		public void* Alloc(int size, int align)
		{
			mLastAllocSize = size;
			let ptr = new uint8[size]*;
			Internal.MemSet(ptr, 0, size);
			return ptr;
		}

		public void Free(void* ptr)
		{
			delete ptr;
		}
	}
}
