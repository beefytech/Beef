namespace System
{
	class Delegate
	{
	    void* mFuncPtr;
	    void* mTarget;

		public static bool Equals(Delegate a, Delegate b)
		{
			if ((Object)a == (Object)b)
				return true;
			if ((Object)a == null || (Object)b == null)
				return false;
			return (a.mFuncPtr == b.mFuncPtr) && (a.mTarget == b.mTarget);
		}

		public Result<void*> GetFuncPtr()
	    {
			if (mTarget != null)
				return .Err; //("Delegate target method must be static");
	        return mFuncPtr;
	    }

		public void* GetTarget()
		{
			return mTarget;
		}

	    public void SetFuncPtr(void* ptr, void* target = null)
		{
			mTarget = target;
			mFuncPtr = ptr;
		}
	}

	struct Function : int
	{

	}
}
