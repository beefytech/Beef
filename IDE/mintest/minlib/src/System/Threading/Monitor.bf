// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
//
// <OWNER>[....]</OWNER>
/*=============================================================================
**
** Class: Monitor
**
**
** Purpose: Synchronizes access to a shared resource or region of code in a multi-threaded 
**             program.
**
**
=============================================================================*/


namespace System.Threading
{
    using System;
    using System.Threading;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;

    public class Monitor
    {
		public struct MonitorLockInstance : IDisposable
		{
		    public Monitor mMonitor;
		    public void Dispose()
		    {
		        mMonitor.Exit();
		    }
		}

		Platform.BfpCritSect* mCritSect;

		public this()
		{
			mCritSect = Platform.BfpCritSect_Create();
		}

        public ~this()
        {
            Platform.BfpCritSect_Release(mCritSect);
		}

        
        /*=========================================================================
        ** Obtain the monitor lock of obj. Will block if another thread holds the lock
        ** Will not block if the current thread holds the lock,
        ** however the caller must ensure that the same number of Exit
        ** calls are made as there were Enter calls.
        **
        ** Exceptions: ArgumentNullException if object is null.
        =========================================================================*/
        
        public MonitorLockInstance Enter()
        {
            MonitorLockInstance monitorLockInstance;
            monitorLockInstance.mMonitor = this;
			Platform.BfpCritSect_Enter(mCritSect);
            return monitorLockInstance;
        }

        private static void ThrowLockTakenException()
        {
            Debug.FatalError();
        }
        
        /*=========================================================================
        ** Release the monitor lock. If one or more threads are waiting to acquire the
        ** lock, and the current thread has executed as many Exits as
        ** Enters, one of the threads will be unblocked and allowed to proceed.
        **
        ** Exceptions: ArgumentNullException if object is null.
        **             SynchronizationLockException if the current thread does not
        **             own the lock.
        =========================================================================*/
        public void Exit()
		{
			Platform.BfpCritSect_Leave(mCritSect);
		}
    
        /*=========================================================================
        ** Similar to Enter, but will never block. That is, if the current thread can
        ** acquire the monitor lock without blocking, it will do so and TRUE will
        ** be returned. Otherwise FALSE will be returned.
        **
        ** Exceptions: ArgumentNullException if object is null.
        =========================================================================*/
        public bool TryEnter()
		{
			return Platform.BfpCritSect_TryEnter(mCritSect, 0);
		}
        
    
        /*=========================================================================
        ** Version of TryEnter that will block, but only up to a timeout period
        ** expressed in milliseconds. If timeout == Timeout.Infinite the method
        ** becomes equivalent to Enter.
        **
        ** Exceptions: ArgumentNullException if object is null.
        **             ArgumentException if timeout < 0.
        =========================================================================*/
        public bool TryEnter(int millisecondsTimeout)
		{
			return Platform.BfpCritSect_TryEnter(mCritSect, (int32)millisecondsTimeout);
		}
        
        private static int32 MillisecondsTimeoutFromTimeSpan(TimeSpan timeout)
        {
            int64 tm = int64(timeout.TotalMilliseconds);
            if (tm < -1 || tm > int64(Int32.MaxValue))
                Debug.FatalError();
            return int32(tm);
        }
        
        public bool TryEnter(TimeSpan timeout)
        {
            return TryEnter(MillisecondsTimeoutFromTimeSpan(timeout));
        }
    }
}
