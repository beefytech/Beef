namespace System
{
	delegate TResult Func<TResult>();
	delegate TResult Func<T1, TResult>(T1 p1);
	delegate TResult Func<T1, T2, TResult>(T1 p1, T2 p2);
	delegate TResult Func<T1, T2, T3, TResult>(T1 p1, T2 p2, T3 p3);
}
