namespace System
{
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

	interface IOpSubtractable
	{
		static Self operator-(Self lhs, Self rhs);
	}

	interface IOpMultiplicable 
	{
		static Self operator*(Self lhs, Self rhs);
	}

	interface IOpDividable
	{
		static Self operator/(Self lhs, Self rhs);
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

	public delegate int Comparison<T>(T lhs, T rhs);
}
