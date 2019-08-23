namespace System
{
	struct Tuple<T1, T2> : IHashable where T1 : IHashable where T2 : IHashable
	{
		public T1 Item1;
		public T2 Item2;

		public this()
		{
			Item1 = default(T1);
			Item2 = default(T2);
		}

		public this(T1 item1, T2 item2)
		{
			Item1 = item1;
			Item2 = item2;
		}

		public int GetHashCode()
		{
			int h1 = Item1.GetHashCode();
			int h2 = Item2.GetHashCode();
			return (((h1 << 5) + h1) ^ h2);
		}
	}
}
