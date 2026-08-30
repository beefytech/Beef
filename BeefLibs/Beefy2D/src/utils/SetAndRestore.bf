namespace utils;

class SetAndRestore<T>
{
	T* mPtr;
	T mOldVal;

	public this(ref T ptr, T newVal)
	{
		mOldVal = ptr;
		ptr = newVal;
		mPtr = &ptr;
	}

	public ~this()
	{
		*mPtr = mOldVal;
	}
}