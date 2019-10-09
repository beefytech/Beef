namespace System
{
	[AlwaysInclude]
	struct Pointer : IHashable
	{
		void* mVal;

		public int GetHashCode()
		{
			return (int)mVal;
		}

		[AlwaysInclude]
		Object GetBoxed()
		{
			return new box this;
		}
	}

	struct Pointer<T> : IHashable
	{
		T* mVal;

		public int GetHashCode()
		{
			return (int)mVal;
		}
	}
}
