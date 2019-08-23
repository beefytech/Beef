namespace System
{
	public delegate void Action();
	public delegate void Action<T>(T obj);
	public delegate void Action<T1, T2>(T1 p1, T2 p2);
	public delegate void Action<T1, T2, T3>(T1 p1, T2 p2, T3 p3);
	public delegate void Action<T1, T2, T3, T4>(T1 p1, T2 p2, T3 p3, T4 p4);

	public delegate int Comparison<T>(T lhs, T rhs);
}
