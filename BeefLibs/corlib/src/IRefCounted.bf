using System.Threading;
using System.Diagnostics;

namespace System
{
	interface IRefCounted
	{
		void AddRef();
		void ReleaseRef();
	}

	class RefCounted : IRefCounted
	{
		protected int32 mRefCount = 1;

		public ~this()
		{
			Debug.Assert(mRefCount == 0);
		}

		public int RefCount
		{
			get
			{
				return mRefCount;
			}
		}

		public void DeleteUnchecked()
		{
			mRefCount = 0;
			delete this;
		}

		public void AddRef()
		{
			Interlocked.Increment(ref mRefCount);
		}

		public void ReleaseRef()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			if (refCount == 0)
			{
				delete this;
			}
		}

		public void ReleaseLastRef()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount == 0);
			if (refCount == 0)
			{
				delete this;
			}
		}

		public int ReleaseRefNoDelete()
		{
			int refCount = Interlocked.Decrement(ref mRefCount);
			Debug.Assert(refCount >= 0);
			return refCount;
		}
	}
}
