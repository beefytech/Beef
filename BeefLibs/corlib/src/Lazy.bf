using System.Threading;

namespace System
{
	enum LazyThreadMode
	{
		None,
		Lock,
		Lockless
	}

	class Lazy<T>
	{
		protected struct Entry
		{
			public SelfOuter mSingleton;
			public T mValue;
		}

		protected Monitor mMonitor ~ delete _;
		protected LazyThreadMode mThreadMode;
		protected volatile int mInitId;
		protected T mValue;
		delegate T() mCreateDlg ~ delete _;
		delegate void(T value) mReleaseDlg ~ delete _;

		public this()
		{
			
		}

		public this(LazyThreadMode threadMode)
		{
			mThreadMode = threadMode;
			Init();
		}

		public this(LazyThreadMode threadMode, delegate T() createDlg = null, delegate void(T value) releaseDlg = null)
		{
			mThreadMode = threadMode;
			mCreateDlg = createDlg;
			mReleaseDlg = releaseDlg;
			Init();
		}

		public this(delegate void(T value) releaseDlg) : this()
		{
			mReleaseDlg = releaseDlg;
		}

		void Init()
		{
			switch (mThreadMode)
			{
			case .None:
			case .Lock:
				mMonitor = new Monitor();
			case .Lockless:
			}
			
		}

		public ~this()
		{
			ReleaseValue(mut mValue);
		}

		protected T DefaultCreateValue()
		{
			return default;
		}

		protected T DefaultCreateValue() where T : struct, new
		{
			return T();
		}

		protected T DefaultCreateValue() where T : class
		{
			Runtime.FatalError("No create delegate specified and no public default constructor is available");
		}

		protected T DefaultCreateValue() where T : class, new
		{
			return new T();
		}

		protected virtual T CreateValue()
		{
			return DefaultCreateValue();
		}

		protected void DefaultReleaseValue(mut T val)
		{

		}

		protected void DefaultReleaseValue(mut T val) where T : struct, IDisposable
		{
			val.Dispose();
		}

		protected void DefaultReleaseValue(mut T val) where T : class
		{
			delete (Object)val;
		}

		protected virtual void ReleaseValue(mut T val)
		{
			DefaultReleaseValue(mut val);
		}

		public ref T Value
		{
			get
			{
				if (mInitId == -1)
					return ref mValue;

				switch (mThreadMode)
				{
				case .None:
					mValue = CreateValue();
				case .Lock:
					using (mMonitor.Enter())
					{
						if (mInitId != -1)
						{
							mValue = CreateValue();
							Interlocked.Fence();
							mInitId = -1;
						}
					}
				case .Lockless:
					int threadId = Thread.CurrentThreadId;
					while (true)
					{
						int initId = Interlocked.CompareExchange(ref mInitId, 0, threadId);
						if (initId == -1)
							break;

						if (initId == 0)
						{
							Interlocked.Fence();
							mValue = CreateValue();
							Interlocked.Fence();
							mInitId = -1;
							break;
						}
					}
				}
				return ref mValue;
			}
		}

		public bool IsValueCreated => mInitId != 0;

		public static ref T operator->(Self self) => ref self.[Inline]Value;

		public override void ToString(String strBuffer)
		{
			if (IsValueCreated)
				strBuffer.AppendF($"Value: {Value}");
			else
				strBuffer.AppendF($"No Value");
		}
	}

	class LazyTLS<T>
	{
		protected struct Entry
		{
			public SelfOuter mSingleton;
			public T mValue;
		}

		void* mData;
		delegate T() mCreateDlg ~ delete _;
		delegate void(T value) mReleaseDlg ~ delete _;

		public this()
		{
			InitTLS();
		}

		public this(delegate T() createDlg = null, delegate void(T value) releaseDlg = null)
		{
			mCreateDlg = createDlg;
			mReleaseDlg = releaseDlg;
			InitTLS();
		}

		void InitTLS()
		{
			mData = Platform.BfpTLS_Create((ptr) =>
				{
					var entry = (Entry*)ptr;
					if (entry.mSingleton.mReleaseDlg != null)
						entry.mSingleton.mReleaseDlg(entry.mValue);
					else
						entry.mSingleton.ReleaseValue(mut entry.mValue);
					delete entry;
				});
		}

		public ~this()
		{
			Platform.BfpTLS_Release((.)mData);
		}

		protected T DefaultCreateValue()
		{
			return default;
		}

		protected T DefaultCreateValue() where T : struct, new
		{
			return T();
		}

		protected T DefaultCreateValue() where T : class
		{
			Runtime.FatalError("No create delegate specified and no public default constructor is available");
		}

		protected T DefaultCreateValue() where T : class, new
		{
			return new T();
		}

		protected virtual T CreateValue()
		{
			return DefaultCreateValue();
		}

		protected void DefaultReleaseValue(mut T val)
		{

		}

		protected void DefaultReleaseValue(mut T val) where T : struct, IDisposable
		{
			val.Dispose();
		}


		protected void DefaultReleaseValue(mut T val) where T : class
		{
			delete (Object)val;
		}

		protected virtual void ReleaseValue(mut T val)
		{
			DefaultReleaseValue(mut val);
		}

		public ref T Value
		{
			get
			{
				void* ptr = Platform.BfpTLS_GetValue((.)mData);
				if (ptr != null)
					return ref ((Entry*)ptr).mValue;

				Entry* entry = new Entry();
				entry.mSingleton = this;
				if (mCreateDlg != null)
					entry.mValue = mCreateDlg();
				else
					entry.mValue = CreateValue();
				Platform.BfpTLS_SetValue((.)mData, entry);
				return ref entry.mValue;
			}
		}

		public bool IsValueCreated => Platform.BfpTLS_GetValue((.)mData) != null;

		public static ref T operator->(Self self) => ref self.[Inline]Value;

		public override void ToString(String strBuffer)
		{
			if (IsValueCreated)
				strBuffer.AppendF($"Value: {Value}");
			else
				strBuffer.AppendF($"No Value");
		}
	}
}
