using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy.utils;

namespace IDE
{
	class ThreadWorkerContext
	{
		public Thread mThread;
		public bool mThreadRunning;
		public Action mOnThreadDone ~ delete _;
		[ThreadStatic]
		public static bool mBpSetThreadName;
		WaitEvent mWaitEvent = new WaitEvent() ~ delete _;

		public void DoBackground(ThreadStart threadStart, Action onThreadDone = null, int maxWait = 0)
		{
		    Debug.Assert(Thread.CurrentThread == IDEApp.sApp.mMainThread);

			/*if (gApp.mMainWindow.IsKeyDown(.Control))
			{
				NOP!();
			}*/

		    Debug.Assert(mOnThreadDone == null);
		    //mBfSystem.PerfZoneStart("BfCompiler.ThreadStart");

			BeefPerf.Event("DoBackground:starting", "");

		    mOnThreadDone = onThreadDone;
		    mThreadRunning = true;
			mWaitEvent.Reset();

		    ThreadPool.QueueUserWorkItem(new () =>
		    {
				if (!mBpSetThreadName)
				{
					String name = scope .();
					Thread.CurrentThread.GetName(name);
					BeefPerf.SetThreadName(name);
					mBpSetThreadName = true;
				}

				BeefPerf.Event("DoBackground:threadStart", "");
		        mThreadRunning = true;
		        mThread = Thread.CurrentThread;
		        threadStart();
				delete threadStart;
		        mThread = null;
		        mThreadRunning = false;
				BeefPerf.Event("DoBackground:threadEnd", "");

				mWaitEvent.Set();
		    });

			if (maxWait != 0)
			{
				if (mWaitEvent.WaitFor(maxWait))
					CheckThreadDone();
			}
		    //mBfSystem.PerfZoneEnd();
		}

		public void WaitForBackground()
		{
			if (mThreadRunning)
			{
				scope AutoBeefPerf("CancelBackground");

			    //mBfSystem.PerfZoneStart("BfCompiler.CancelBackground");
			    while (mThreadRunning)
			    {
			        // Lame
			        Thread.Sleep(20);
			    }
			    //mBfSystem.PerfZoneEnd();                                
			}

			if ((mOnThreadDone != null) && (Thread.CurrentThread == IDEApp.sApp.mMainThread))
			{
			    mOnThreadDone();
			    delete mOnThreadDone;
				mOnThreadDone = null;
			}
		}

		public void CheckThreadDone()
		{
			if ((!mThreadRunning) && (mOnThreadDone != null))
			{
			    mOnThreadDone();
				delete mOnThreadDone;
			    mOnThreadDone = null;
			}
		}
	}

    public abstract class CommandQueueManager
    {        
        protected class Command
        {

        }

		public ThreadWorkerContext mThreadWorker = new .() ~ delete _;
		public ThreadWorkerContext mThreadWorkerHi = new .() ~ delete _;

        protected List<Command> mCommandQueue = new List<Command>() ~ DeleteContainerAndItems!(_);
        protected bool mShuttingDown;
        public bool mAllowThreadStart = true;
		public Monitor mMonitor = new Monitor() ~ delete _;

		public bool ThreadRunning
		{
			get
			{
				return mThreadWorker.mThreadRunning || mThreadWorkerHi.mThreadRunning;
			}
		}

        public ~this()
        {
            using (mMonitor.Enter())
            {
                mShuttingDown = true;
				ClearAndDeleteItems(mCommandQueue);
            }
            CancelBackground();
        }

        public virtual void RequestCancelBackground()
        {

        }

        public void CancelBackground()
        {
            RequestCancelBackground();
            WaitForBackground();
        }

		public void WaitForBackground()
		{
			mThreadWorker.WaitForBackground();
			mThreadWorkerHi.WaitForBackground();
		}

        protected void QueueCommand(Command command)
        {
            using (mMonitor.Enter())
            {
                mCommandQueue.Add(command);
            }
        }

        public bool HasQueuedCommands()
        {
            using (mMonitor.Enter())
            {
                return (mThreadWorker.mThreadRunning) || (mThreadWorkerHi.mThreadRunning) || (mCommandQueue.Count > 0);
            }
        }

        public int32 GetCommandQueueSize()
        {
            using (mMonitor.Enter())
            {
                return (int32)mCommandQueue.Count;
            }
        }

        protected abstract void DoProcessQueue();

        public virtual void ProcessQueue()
        {
            DoProcessQueue();
        }

        public virtual void StartQueueProcessThread()
        {
            DoBackground(new => DoProcessQueue);
        }

        public bool IsPerformingBackgroundOperation()
        {
            return (mThreadWorker.mThreadRunning || mThreadWorkerHi.mThreadRunning);
        }

        public void DoBackground(ThreadStart threadStart, Action onThreadDone = null, int maxWait = 0)
        {
			CancelBackground();
            mThreadWorker.DoBackground(threadStart, onThreadDone, maxWait);
        }

		public void DoBackgroundHi(ThreadStart threadStart, Action onThreadDone = null)
		{
		 	mThreadWorkerHi.DoBackground(threadStart, onThreadDone);
		}

		public void CheckThreadDone()
		{
			mThreadWorker.CheckThreadDone();
			mThreadWorkerHi.CheckThreadDone();
		}

        public virtual void Update()
        {
            if (!mThreadWorker.mThreadRunning && !mThreadWorkerHi.mThreadRunning)
            {
             	CheckThreadDone();

                if (mAllowThreadStart)
                {                    
                    if (mCommandQueue.Count > 0)
                    {
                        StartQueueProcessThread();
                    }
                }
            }
        }
    }
}
