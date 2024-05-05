namespace System
{
	[AlwaysInclude]
	class Delegate : IHashable
	{
	    void* mFuncPtr;
	    void* mTarget;

		public static bool Equals(Delegate a, Delegate b)
		{
			if (a === null)
				return b === null;
			return a.Equals(b);
		}

		public virtual bool Equals(Delegate val)
		{
			if (this === val)
				return true;
			if (val == null)
				return false;
			return (mFuncPtr == val.mFuncPtr) && (mTarget == val.mTarget);
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

		protected override void GCMarkMembers()
		{
			// Note- this is safe even if mTarget is not an object, because the GC does object address validation
			GC.Mark(Internal.UnsafeCastToObject(mTarget));
		}

		public int GetHashCode()
		{
			return (int)mFuncPtr;
		}

		[Commutable]
		public static bool operator==(Delegate a, Delegate b)
		{
			if (a === null)
				return b === null;
			return a.Equals(b);
		}
	}

	delegate void Action();

	[AlwaysInclude]
	struct Function : int
	{

	}
}
