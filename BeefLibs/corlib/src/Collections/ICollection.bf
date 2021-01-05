namespace System.Collections
{
	interface ICollection<T>
	{
		public abstract int Count
		{
			get;
		}

		public void Add(T item);
		public void Clear();
		public bool Contains(T item);
		public void CopyTo(Span<T> span);
		public bool Remove(T item);
	}
}
