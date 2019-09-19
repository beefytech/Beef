namespace System.Threading
{
	static class Volatile
	{
		//static volatile int32 gAtomicIdx;

		public static T Read<T>(ref T location)
		{
			T value = location;
			Interlocked.Fence();
			return value;
		}

		public static void Write<T>(ref T location, T value)
		{
			Interlocked.Fence();
			location = value;
		}

		/*public static void MoveAtomic(void* dest, void* src, int size)
		{
			while (true)
			{
				int startAtomicIdx = gAtomicIdx;
				Thread.MemoryBarrier();
				Internal.MemCpy(dest, src, size);
				Thread.MemoryBarrier();
				int endAtomicIdx = Interlocked.Increment(ref gAtomicIdx);
				if (endAtomicIdx == startAtomicIdx)
					break;
			}
		}

		public static T ReadAtomic<T>(ref T location)
		{
			if (sizeof(T) > sizeof(int))
			{
				if (sizeof(T) == sizeof(int64)) 
				{
					// Handle 64-bit values on 32-bit machines
					var location;
					int64 result = Interlocked.CompareExchange(ref *(int64*)&location, 0, 0);
					return *(T*)&result;
				}

				var location;
				T value = ?;
				MoveAtomic(&value, &location, sizeof(T));
				Thread.MemoryBarrier();
				return value;
			}

			T value = location;
			Thread.MemoryBarrier();
			return value;
		}*/
	}
}
