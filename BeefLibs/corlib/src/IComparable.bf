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

	public class CompareWrapper<T> : IComparer<T> where T : IOpComparable
	{
		public int Compare(T x, T y)
		{
			return x <=> y;
		}
	}

	interface INumeric
	{
		static Self operator+(Self lhs, Self rhs);
	}

	interface IInteger : INumeric
	{
	}

	interface IUnsigned : INumeric
	{

	}

	interface ISigned
	{

	}

	interface IFloating
	{
	}

	interface IOpEquals
	{
		public static bool operator==(Self val1, Self val2);
	}

	interface IOpEquals<T>
	{
		public static bool operator==(Self val1, T val2);
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
