namespace System
{
	[AlwaysInclude]
	struct SizedArray<T, CSize> where CSize : const int
	{
		T[CSize] mVal;

		public int Count
		{
			[Inline]
			get
			{
				return CSize;
			}
		}	

		public explicit static operator T[CSize] (Self val)
		{
			return val.mVal;
		}

		public override void ToString(String strBuffer) mut
		{
			if (typeof(T) == typeof(char8))
			{
				strBuffer.Append((char8*)&mVal, CSize);
				return;
			}

			strBuffer.Append('(');
			for (int i < CSize)
			{
				if (i != 0)
					strBuffer.Append(", ");
				mVal[i].ToString(strBuffer);
			}
			strBuffer.Append(')');
		}
	}

}
