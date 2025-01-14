namespace System
{
	struct Void : void, IHashable
	{
		public override void ToString(String strBuffer)
		{
			strBuffer.Append("void");
		}

		public int GetHashCode()
		{
			return 0;
		}
	}
}
