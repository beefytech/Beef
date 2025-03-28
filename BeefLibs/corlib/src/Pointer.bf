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

		public override void ToString(String strBuffer)
		{
			strBuffer.Append("0x");
			NumberFormatter.AddrToString((uint)(void*)mVal, strBuffer);
		}
	}

	struct Pointer<T> : IHashable
	{
		T* mVal;

		public int GetHashCode()
		{
			return (int)(void*)mVal;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append("(");
			typeof(T).ToString(strBuffer);
			strBuffer.Append("*)0x");
			NumberFormatter.AddrToString((uint)(void*)mVal, strBuffer);
		}
	}
}
