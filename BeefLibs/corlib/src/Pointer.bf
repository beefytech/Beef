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
			strBuffer.AppendF("0x{0:A}", (uint)(void*)mVal);
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
			typeof(T).GetFullName(strBuffer);
			strBuffer.AppendF("*)0x{0:A}", (uint)(void*)mVal);
		}
	}
}
