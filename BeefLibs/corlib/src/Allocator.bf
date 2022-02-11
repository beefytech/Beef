using System.Reflection;
using System.Threading;

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

	struct AllocWrapper<T> where T : new, delete
	{
		alloctype(T) mVal;
		public alloctype(T) Val
		{
			get mut
			{
				if (mVal != default)
					return mVal;
				var newVal = new T();
				let prevValue = Interlocked.CompareExchange(ref mVal, default, newVal);
				if (prevValue != default)
				{
					delete newVal;
					return prevValue;
				}
				return newVal;
			}
		}

		public void Dispose() mut
		{
			delete mVal;
			mVal = default;
		}
	}
}

