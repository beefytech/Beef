namespace System
{
	struct Void : void
	{
		public override void ToString(String strBuffer)
		{
			strBuffer.Append("void");
		}
	}
}
