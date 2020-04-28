namespace System.Collections.Generic
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
		public void CopyTo(T[] arr, int index);
		public bool Remove(T item);
	}
}
