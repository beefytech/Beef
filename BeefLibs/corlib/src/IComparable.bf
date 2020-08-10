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

	[Obsolete("Consider operator constraint such as `where bool : operator T == T`", false)]
	interface IOpEquals
	{
		public static bool operator==(Self val1, Self val2);
	}

	[Obsolete("Consider operator constraint such as `where bool : operator T == T2`", false)]
	interface IOpEquals<T>
	{
		public static bool operator==(Self val1, T val2);
	}

	[Obsolete("Consider operator constraint such as `where int : operator T <=> T`", false)]
	interface IOpComparable
	{
		static int operator<=>(Self lhs, Self rhs);
	}

	[Obsolete("Consider operator constraint such as `where T : operator T + T`", false)]
	interface IOpAddable
	{
		static Self operator+(Self lhs, Self rhs);
	}

	[Obsolete("Consider operator constraint such as `where T : operator T - T`", false)]
	interface IOpSubtractable
	{
		static Self operator-(Self lhs, Self rhs);
	}

	[Obsolete("Consider operator constraint such as `where T : operator T * T`", false)]
	interface IOpMultiplicable 
	{
		static Self operator*(Self lhs, Self rhs);
	}

	[Obsolete("Consider operator constraint such as `where T : operator T / T`", false)]
	interface IOpDividable
	{
		static Self operator/(Self lhs, Self rhs);
	}

	[Obsolete("Consider operator constraint such as `where T : operator -T`", false)]
	interface IOpNegatable
	{
		static Self operator-(Self value);
	}

	[Obsolete("Consider operator constraint such as `where T : operator implicit T2`", false)]
	interface IOpConvertibleTo<T>
	{
		static operator T(Self value);
	}

	[Obsolete("Consider operator constraint such as `where T : operator implicit T2`", false)]
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
