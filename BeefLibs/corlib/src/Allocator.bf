using System.Reflection;
using System.Threading;

namespace System
{
	interface IRawAllocator
	{
		void* Alloc(int size, int align) mut;
		void Free(void* ptr) mut;
	}

	interface ITypedAllocator : IRawAllocator
	{
		void* AllocTyped(Type type, int size, int align) mut;
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

	class SingleAllocator : ITypedAllocator
	{
		void* mPtr;
		int mSize;
		Type mType;

		[AllowAppend]
		public this(int size)
		{
			void* ptr = append uint8[size]*(?);
			mPtr = ptr;
			mSize = size;
		}

		public ~this()
		{
			if ((mType != null) && (mType.IsObject))
			{
				Object obj = Internal.UnsafeCastToObject(mPtr);
				delete:null obj;
			}

			if (mSize < 0)
				delete mPtr;
		}

		protected virtual void* AllocLarge(int size, int align)
		{
			return new uint8[size]*;
		}

		protected virtual void FreeLarge(void* ptr)
		{
			delete ptr;
		}

		public void* Alloc(int size, int align)
		{
			return AllocTyped(typeof(void), size, align);
		}

		public void Free(void* ptr)
		{
			Runtime.Assert(ptr == mPtr);
			mType = typeof(void);
		}

		[Optimize]
		public void* AllocTyped(Type type, int size, int align)
		{
			Runtime.Assert(mType == null, "SingleAllocator has been used for multiple allocations");

			mType = type;
			do
			{
				if (size > mSize)
					break;
				void* usePtr = (void*)(int)Math.Align((int)mPtr, align);
				if ((uint8*)usePtr + size > (uint8*)mPtr + mSize)
					break;
				mPtr = usePtr;
				mSize = 0;
				return mPtr;
			}
			mSize = -1;
			mPtr = AllocLarge(size, align);
			return mPtr;
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

