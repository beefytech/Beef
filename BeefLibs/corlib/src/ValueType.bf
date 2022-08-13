namespace System
{
	struct ValueType
    {
		[NoShow(true)]
		public static extern bool Equals<T>(T val1, T val2);
	}
}
