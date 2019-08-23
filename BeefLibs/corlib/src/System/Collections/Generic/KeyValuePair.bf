namespace System.Collections.Generic
{
	public struct KeyValuePair<TKey, TValue>
	{
		private TKey mKey;
		private TValue mValue;

		public this(TKey key, TValue value)
		{
			this.mKey = key;
			this.mValue = value;
		}

		public TKey Key
		{
			get { return mKey; }
		}

		public TValue Value
		{
			get { return mValue; }
		}

		public override void ToString(String strOut)
		{
			strOut.Append('[');
			if (Key != null)
			{
				Key.ToString(strOut);
			}
			strOut.Append(", ");
			if (Value != null)
			{
				Value.ToString(strOut);
			}
			strOut.Append(']');
		}
	}
}
