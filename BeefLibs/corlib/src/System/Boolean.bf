namespace System
{
	struct Boolean : bool, IHashable
	{
		public override void ToString(String strBuffer)
		{
		    strBuffer.Append(((bool)this) ? "true" : "false");
		}

		int IHashable.GetHashCode()
		{
			return ((bool)this) ? 1 : 0;
		}
	}
}
