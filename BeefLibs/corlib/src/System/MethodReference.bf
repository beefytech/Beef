namespace System
{
	[AlwaysInclude]
	struct MethodReference<T>
	{
		T mVal;

		private this(T val)
		{
			mVal = val;
		}
	}
}
