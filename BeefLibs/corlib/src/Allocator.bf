namespace System
{
	interface IRawAllocator
	{
		void* Alloc(int size, int align);
		void Free(void* ptr);
	}

	struct StdAllocator : IRawAllocator
	{
		public void* Alloc(int size, int align)
		{
			return Internal.StdMalloc(size);
		}

		public void Free(void* ptr)
		{
			Internal.StdFree(ptr);
		}
	}
}
