namespace System
{
	interface IComparable<T>
	{
		int32 CompareTo(T other);
	}

	public interface IComparer<T>
	{
	    int Compare(T x, T y);
	}

	interface IOpEquatable
	{
		static bool operator==(Self lhs, Self other);
	}

	interface IOpEquatable<TRight>
	{
		static bool operator==(Self lhs, TRight other);
	}

	interface IOpComparable
	{
		static int operator<=>(Self lhs, Self rhs);
	}

	interface IOpAddable
	{
		static Self operator+(Self lhs, Self rhs);
	}

	interface IOpNegatable
	{									 
		static Self operator-(Self value);
	}

	interface IOpConvertibleTo<T>
	{
		static operator T(Self value);
	}

	interface IOpConvertibleFrom<T>
	{
		static operator Self(T value);
	}

	interface IIsNaN
	{
		bool IsNaN { get; }
	}

	interface ICanBeNaN : IIsNaN
	{
		
	}
}
