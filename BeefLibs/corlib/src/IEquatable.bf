namespace System
{
	interface IEquatable
	{
		bool Equals(Object val);
	}

	interface IEquatable<T>
	{
		bool Equals(T val2);
	}
}
