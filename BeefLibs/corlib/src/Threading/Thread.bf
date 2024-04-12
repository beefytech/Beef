namespace System.Threading
{
    using System;
    using System.Diagnostics;
    
    public delegate Object InternalCrossContextDelegate(Object[] args);
    
    public delegate void ThreadStart();
    public delegate void ParameterizedThreadStart(Object obj);

	[StaticInitPriority(100)]
    public sealed class Thread
    {
        private int mInternalThread;
        private ThreadPriority mPriority = .Normal;
        public int32 mMaxStackSize;
        private String mName ~ delete _;
        private Delegate mDelegate;
        
        private Object mThreadStartArg;

        bool mAutoDelete = true;
		bool mJoinOnDelete;

		static Monitor sMonitor = new .() ~ delete _;
		static Event<delegate void()> sOnExit ~ _.Dispose();
		Event<delegate void()> mOnExit ~ _.Dispose();

		public static Thread sMainThread ~ delete _;

        [StaticInitPriority(102)]
        struct RuntimeThreadInit
        {
            static Object Thread_Alloc()
            {
                return Thread.CreateEmptyThread();
            }

            static Object Thread_GetMainThread()
            {
                return Thread.[Friend]sMainThread;
            }

            static void* Thread_GetInternalThread(Object thread)
            {
                return (void*)((Thread)thread).[Friend]mInternalThread;
            }

            static void Thread_SetInternalThread(Object thread, void* internalThread)
            {
#if BF_ENABLE_REALTIME_LEAK_CHECK
				if (internalThread != null)
					GC.[Friend]AddPendingThread(internalThread);
#endif
                ((Thread)thread).[Friend]mInternalThread = (int)internalThread;
            }

            static bool Thread_IsAutoDelete(Object thread)
            {
                return ((Thread)thread).[Friend]mAutoDelete;
            }

            static void Thread_AutoDelete(Object thread)
            {
                delete thread;
            }

            static int32 Thread_GetMaxStackSize(Object thread)
            {
                return ((Thread)thread).mMaxStackSize;
            }

			static void Thread_Exiting()
			{
				using (sMonitor.Enter())
				{
					sOnExit();
				}
			}

            static void Thread_StartProc(Object threadObj)
            {
                Thread thread = (Thread)threadObj;

				if (thread.mName != null)
					thread.InformThreadNameChange(thread.mName);
				if (thread.mPriority != .Normal)
					thread.SetPriorityNative((.)thread.mPriority);

                int32 stackStart = 0;
                thread.SetStackStart((void*)&stackStart);

				var dlg = thread.mDelegate;
				var threadStartArg = thread.mThreadStartArg;
				thread.mDelegate = null;

				thread.ThreadStarted();

                if (dlg is ThreadStart)
                {
                    ((ThreadStart)dlg)();
                }
                else 
                {
                    ((ParameterizedThreadStart)dlg)(threadStartArg);
                }
				delete dlg;
            }

            public static this()
            {
                var cb = ref Runtime.BfRtCallbacks.[Friend]sCallbacks;

                cb.[Friend]mThread_Alloc = => Thread_Alloc;
                cb.[Friend]mThread_GetMainThread = => Thread_GetMainThread;
                cb.[Friend]mThread_ThreadProc = => Thread_StartProc;
                cb.[Friend]mThread_GetInternalThread = => Thread_GetInternalThread;
                cb.[Friend]mThread_SetInternalThread = => Thread_SetInternalThread;
                cb.[Friend]mThread_IsAutoDelete = => Thread_IsAutoDelete;
                cb.[Friend]mThread_AutoDelete = => Thread_AutoDelete;
                cb.[Friend]mThread_GetMaxStackSize = => Thread_GetMaxStackSize;
				cb.[Friend]mThread_Exiting = => Thread_Exiting;

				Runtime.[Friend, NoStaticCtor]sThreadInit = => Thread.Init;
            }

			public static void Check()
			{
			}
        }

		public static this()
		{
			RuntimeThreadInit.Check();
		}

		private this()
		{

		}

        public this(ThreadStart start)
        {
            if (start == null)
            {
                Runtime.FatalError();
            }            
            SetStart((Delegate)start, 0);  //0 will setup Thread with default stackSize
        }
        
        public this(ThreadStart start, int maxStackSize)
        {            
            if (start == null)
            {
                Runtime.FatalError();
            }
            if (0 > maxStackSize)
                Runtime.FatalError();            
            SetStart((Delegate)start, (.)maxStackSize);
        }

        public this(ParameterizedThreadStart start)
        {
            if (start == null)
            {
                Runtime.FatalError();
            }            
            SetStart((Delegate)start, 0);
        }
        
        public this(ParameterizedThreadStart start, int32 maxStackSize)
        {
            if (start == null)
            {
                Runtime.FatalError();
            }
            if (0 > maxStackSize)
                Runtime.FatalError();            
            SetStart((Delegate)start, maxStackSize);
        }

        static void Init()
        {
#unwarn
            RuntimeThreadInit runtimeThreadInitRef = ?;
			sMainThread = new Thread();
            sMainThread.ManualThreadInit();
        }

        static Thread CreateEmptyThread()
        {
            return new Thread();
        }

        public bool AutoDelete
        {
            set
            {
                // Changing AutoDelete from another thread while we are running is a race condition
                Runtime.Assert((CurrentThread == this) || (!IsAlive));
                mAutoDelete = value;
            }

            get
            {
                return mAutoDelete;
            }
        }

		public void AddExitNotify(delegate void() dlg)
		{
			using (sMonitor.Enter())
			{
				mOnExit.Add(dlg);
			}
		}

		public Result<void> RemovedExitNotify(delegate void() dlg, bool delegateDelegate = false)
		{
			using (sMonitor.Enter())
			{
				return mOnExit.Remove(dlg, delegateDelegate);
			}
		}

		public static void AddGlobalExitNotify(delegate void() dlg)
		{
			using (sMonitor.Enter())
			{
				sOnExit.Add(dlg);
			}
		}

		public static Result<void> RemoveGlobalExitNotify(delegate void() dlg, bool delegateDelegate = false)
		{
			using (sMonitor.Enter())
			{
				return sOnExit.Remove(dlg, delegateDelegate);
			}
		}

	public void Start()
	{
		StartInternal();
	}

        public void Start(bool autoDelete = true)
        {
            	mAutoDelete = autoDelete;
            	Start();
        }

	public void Start(Object parameter)
	{
		if (mDelegate is ThreadStart)
		{
			Runtime.FatalError();
		}
		mThreadStartArg = parameter;
		StartInternal();
	}
        
        public void Start(Object parameter, bool autoDelete = true)
        {
            mAutoDelete = autoDelete;
            Start(parameter);
        }

#if BF_PLATFORM_WINDOWS && !BF_RUNTIME_DISABLE
		[CLink]
		static extern int32 _tls_index; 
#endif

		public static int ModuleTLSIndex
		{
			get
			{
#if BF_PLATFORM_WINDOWS && !BF_RUNTIME_DISABLE
				return _tls_index;
#else
				return 0;
#endif
			}
		}

        public ThreadPriority Priority
        {
            get
			{
				if (mInternalThread != 0)
					return (ThreadPriority)GetPriorityNative();
				return mPriority;
			}
            set
			{
				mPriority = value;
				if (mInternalThread != 0)
					SetPriorityNative((int32)value);
			}
        }

        public bool IsAlive
        {
            get
            {
                return GetIsAlive();
            }
        }

		
        public bool IsThreadPoolThread
        {
            get
            {
                return GetIsThreadPoolThread();
            }
        }

        public void Join()
        {
            JoinInternal(Timeout.Infinite);
        }
        
        public bool Join(int millisecondsTimeout)
        {
            return JoinInternal((.)millisecondsTimeout);
        }
        
        public bool Join(TimeSpan timeout)
        {
            int64 tm = (int64)timeout.TotalMilliseconds;
            if (tm < -1 || tm > (int64)Int32.MaxValue)
                Runtime.FatalError();
            
            return Join((int32)tm);
        }

        public static void Sleep(int32 millisecondsTimeout)
        {
            SleepInternal(millisecondsTimeout);
        }
        
        public static void Sleep(TimeSpan timeout)
        {
            int64 tm = (int64)timeout.TotalMilliseconds;
            if (tm < -1 || tm > (int64)Int32.MaxValue)
                Runtime.FatalError();
            Sleep((int32)tm);
        }

        public static void SpinWait(int iterations)
        {
            SpinWaitInternal((int32)iterations);
        }
        
        public static bool Yield()
        {
            return YieldInternal();
        }
        
        public static Thread CurrentThread
        {
            get
            {
                return GetCurrentThreadNative();
            }
        }

        public int Id
        {
            get
            {
                return GetThreadId();
            }
        }

		public static int CurrentThreadId => Platform.BfpThread_GetCurrentId();
        
        void SetStart(Delegate ownStartDelegate, int32 maxStackSize)
        {
            mDelegate = ownStartDelegate;
            mMaxStackSize = maxStackSize;
        }

        public ~this()
        {
			using (sMonitor.Enter())
			{
				mOnExit();
				sOnExit();
			}

			if (mJoinOnDelete)
				Join();

            // Make sure we're not deleting manually if mAutoDelete is set
            Debug.Assert((!mAutoDelete) || (CurrentThread == this));
            // Delegate to the unmanaged portion.
            InternalFinalize();
            // Thread owns delegate
            delete mDelegate;
        }

        public bool IsBackground
        {
            get { return IsBackgroundNative(); }
            set { SetBackgroundNative(value); }
        }
		
		public void SetJoinOnDelete(bool joinOnDelete)
		{
			mJoinOnDelete = joinOnDelete;
		}

        public ThreadState ThreadState
        {
            get { return (ThreadState)GetThreadStateNative(); }
        }

        public void SetName(String name)
        {
            if (name == null)
            {
                delete mName;
                mName = null;
            }
            else
            {
                String.NewOrSet!(mName, name);
            }
			if (mInternalThread != 0)
            	InformThreadNameChange(name);
        }

        public void GetName(String outName)
        {
            if (mName != null)
                outName.Append(mName);
        }

#if !BF_RUNTIME_DISABLE
        [CallingConvention(.Cdecl)]
        private extern void InformThreadNameChange(String name);
		[CallingConvention(.Cdecl)]
		private extern bool IsBackgroundNative();
		[CallingConvention(.Cdecl)]
		private extern void SetBackgroundNative(bool isBackground);
		[CallingConvention(.Cdecl)]
		private extern void InternalFinalize();
		[CallingConvention(.Cdecl)]
		private static extern Thread GetCurrentThreadNative();
		[CallingConvention(.Cdecl)]
		private extern int32 GetPriorityNative();
		[CallingConvention(.Cdecl)]
		private extern void SetPriorityNative(int32 priority);
		[CallingConvention(.Cdecl)]
		extern bool GetIsThreadPoolThread();
		[CallingConvention(.Cdecl)]
		extern int GetThreadId();
		[CallingConvention(.Cdecl)]
		private extern int32 GetThreadStateNative();
		private static extern void SpinWaitInternal(int32 iterations);
		private static extern void SleepInternal(int32 millisecondsTimeout);
		private extern bool JoinInternal(int32 millisecondsTimeout);
		private static extern bool YieldInternal();
		extern void ManualThreadInit();
		extern void StartInternal();
		extern void SetStackStart(void* ptr);
		extern void ThreadStarted();
		public static extern void RequestExitNotify();
		public extern void Suspend();
		public extern void Resume();
		extern bool GetIsAlive();
#else
		private void InformThreadNameChange(String name) {}
		private bool IsBackgroundNative() => false;
		private void SetBackgroundNative(bool isBackground) {}
		private void InternalFinalize() {}
		private static Thread GetCurrentThreadNative() => null;
		private int32 GetPriorityNative() => 0;
		private void SetPriorityNative(int32 priority) {}
		bool GetIsThreadPoolThread() => false;
		int GetThreadId() => 0;
		private int32 GetThreadStateNative() => 0;
		private static void SpinWaitInternal(int32 iterations) {}
		private static void SleepInternal(int32 millisecondsTimeout) {}
		private bool JoinInternal(int32 millisecondsTimeout) => false;
		private static bool YieldInternal() => false;
		void ManualThreadInit() {}
		void StartInternal() {}
		void SetStackStart(void* ptr) {}
		void ThreadStarted() {}
		public static void RequestExitNotify() {}
		public void Suspend() {}
		public void Resume() {}
		bool GetIsAlive() => false;
#endif
    }
}
