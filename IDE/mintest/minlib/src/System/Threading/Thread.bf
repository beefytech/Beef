namespace System.Threading
{
    using System;
    using System.Diagnostics;
    
    public delegate Object InternalCrossContextDelegate(Object[] args);
    
    public delegate void ThreadStart();
    public delegate void ParameterizedThreadStart(Object obj);

    public sealed class Thread
    {
        private int mInternalThread;
        private int32 mPriority;
        public int32 mMaxStackSize;
        private String mName ~ delete _;
        private Delegate mDelegate;
        
        private Object mThreadStartArg;

        bool mAutoDelete;
        public static Thread sMainThread = new Thread() ~ delete _;
        
        [StaticInitPriority(101)]
        struct RuntimeThreadInit
        {
            static Object Thread_Alloc()
            {
                return Thread.[Friend]CreateEmptyThread();
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

            static void Thread_StartProc(Object threadObj)
            {
                Thread thread = (Thread)threadObj;

				if (thread.mName != null)
					thread.InformThreadNameChange(thread.mName);

                int32 stackStart = 0;
                thread.SetStackStart((void*)&stackStart);
                if (thread.mDelegate is ThreadStart)
                {
                    ((ThreadStart)thread.mDelegate)();
                }
                else 
                {
                    ((ParameterizedThreadStart)thread.mDelegate)(thread.mThreadStartArg);
                }
            }

            public static this()
            {
                var cb = ref Runtime.BfRtCallbacks.sCallbacks;

                cb.[Friend]mThread_Alloc = => Thread_Alloc;
                cb.[Friend]mThread_GetMainThread = => Thread_GetMainThread;
                cb.[Friend]mThread_ThreadProc = => Thread_StartProc;
                cb.[Friend]mThread_GetInternalThread = => Thread_GetInternalThread;
                cb.[Friend]mThread_SetInternalThread = => Thread_SetInternalThread;
                cb.[Friend]mThread_IsAutoDelete = => Thread_IsAutoDelete;
                cb.[Friend]mThread_AutoDelete = => Thread_AutoDelete;
                cb.[Friend]mThread_GetMaxStackSize = => Thread_GetMaxStackSize;
            }
        }

		private this()
		{

		}

        public this(ThreadStart start)
        {
            Debug.Assert(start != null);
            SetStart((Delegate)start, 0);  //0 will setup Thread with default stackSize
        }
        
        public this(ThreadStart start, int32 maxStackSize)
        {            
            Debug.Assert(start != null);
            if (0 > maxStackSize)
                Runtime.FatalError();            
            SetStart((Delegate)start, maxStackSize);
        }

        public this(ParameterizedThreadStart start)
        {
            Debug.Assert(start != null);
            SetStart((Delegate)start, 0);
        }
        
        public this(ParameterizedThreadStart start, int32 maxStackSize)
        {
            Debug.Assert(start != null);
			Debug.Assert(maxStackSize > 0);
            SetStart((Delegate)start, maxStackSize);
        }

        static this()
        {
#unwarn
            RuntimeThreadInit runtimeThreadInitRef = ?;
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

		[CallingConvention(.Cdecl)]
        extern void ManualThreadInit();
		[CallingConvention(.Cdecl)]
        extern void StartInternal();
		[CallingConvention(.Cdecl)]
        extern void SetStackStart(void* ptr);

        public void Start(bool autoDelete = true)
        {
            mAutoDelete = autoDelete;
            StartInternal();
        }
        
        public void Start(Object parameter, bool autoDelete = true)
        {
            mAutoDelete = autoDelete;
            if (mDelegate is ThreadStart)
            {
                Runtime.FatalError();
            }
            mThreadStartArg = parameter;
            StartInternal();
        }

#if PLATFORM_WINDOWS
		[CLink]
		static extern int32 _tls_index; 
#endif

		public static int ModuleTLSIndex
		{
			get
			{
#if PLATFORM_WINDOWS
				return _tls_index;
#else
				return 0;
#endif
			}
		}

        public void Suspend() { SuspendInternal(); }
        private extern void SuspendInternal();

        public void Resume() { ResumeInternal(); }
        private extern void ResumeInternal();

        public void Interrupt() { InterruptInternal(); }
        private extern void InterruptInternal();

        public ThreadPriority Priority
        {
            get { return (ThreadPriority)GetPriorityNative(); }
            set { SetPriorityNative((int32)value); }
        }
        private extern int32 GetPriorityNative();
        private extern void SetPriorityNative(int32 priority);

        extern bool GetIsAlive();
        public bool IsAlive
        {
            get
            {
                return GetIsAlive();
            }
        }

        extern bool GetIsThreadPoolThread();
        public bool IsThreadPoolThread
        {
            get
            {
                return GetIsThreadPoolThread();
            }
        }

        private extern bool JoinInternal(int32 millisecondsTimeout);
        
        public void Join()
        {
            JoinInternal(Timeout.Infinite);
        }
        
        public bool Join(int32 millisecondsTimeout)
        {
            return JoinInternal(millisecondsTimeout);
        }
        
        public bool Join(TimeSpan timeout)
        {
            int64 tm = (int64)timeout.TotalMilliseconds;
            if (tm < -1 || tm > (int64)Int32.MaxValue)
                Runtime.FatalError();
            
            return Join((int32)tm);
        }

        private static extern void SleepInternal(int32 millisecondsTimeout);
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

        private static extern void SpinWaitInternal(int32 iterations);
        
        public static void SpinWait(int iterations)
        {
            SpinWaitInternal((int32)iterations);
        }

		[CallingConvention(.Cdecl)]
        private static extern bool YieldInternal();
        
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

        extern int32 GetThreadId();

        public int32 Id
        {
            get
            {
                return GetThreadId();
            }
        }

        private static extern Thread GetCurrentThreadNative();
        
        void SetStart(Delegate start, int32 maxStackSize)
        {
            mDelegate = start;
            mMaxStackSize = maxStackSize;
        }

        
        public ~this()
        {
            // Make sure we're not deleting manually it mAutoDelete is set
            Debug.Assert((!mAutoDelete) || (CurrentThread == this));
            // Delegate to the unmanaged portion.
            InternalFinalize();
            // Thread owns delegate
            delete mDelegate;
        }
        
        private extern void InternalFinalize();

        public bool IsBackground
        {
            get { return IsBackgroundNative(); }
            set { SetBackgroundNative(value); }
        }
        private extern bool IsBackgroundNative();
        private extern void SetBackgroundNative(bool isBackground);

		public extern void SetJoinOnDelete(bool joinOnDelete);

        public ThreadState ThreadState
        {
            get { return (ThreadState)GetThreadStateNative(); }
        }
        
        private extern int32 GetThreadStateNative();

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
        
        private extern void InformThreadNameChange(String name);
        public static extern void MemoryBarrier();
    }
}
