using System;
using System.Diagnostics;

namespace System.Threading
{    
    public class Monitor
    {
        public struct MonitorLockInstance
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

		/// Acquires the monitor lock. Will block if another thread holds the lock.
		///
		/// Multiple calls to Enter can be issued, and an equivalent number of Exits
		/// must be issued to allow another thread to enter.
        public MonitorLockInstance Enter()
        {
            MonitorLockInstance monitorLockInstance;
            monitorLockInstance.mMonitor = this;
            Platform.BfpCritSect_Enter(mCritSect);
            return monitorLockInstance;
        }
        
		/// Releases the monitor lock.
		///
		/// Other threads will be able to enter the monitor unless this thread has issued
		/// multiple Enters which have not all be Exited.
        public void Exit()
        {
            Platform.BfpCritSect_Leave(mCritSect);
        }
    
        /// Attempt to enter the monitor without waiting.
		/// @return true if the monitor was entered
        public bool TryEnter()
        {
            return Platform.BfpCritSect_TryEnter(mCritSect, 0);
        }
        
		/// Blocks up to a timeout, or if millisecondsTimeout is -1, will wait forever.
		/// @return true if the monitor was entered
        public bool TryEnter(int millisecondsTimeout)
        {
            return Platform.BfpCritSect_TryEnter(mCritSect, (int32)millisecondsTimeout);
        }
        
        private static int32 MillisecondsTimeoutFromTimeSpan(TimeSpan timeout)
        {
            int64 tm = int64(timeout.TotalMilliseconds);
            Debug.Assert((uint64)tm <= Int32.MaxValue);                
            return int32(tm);
        }
        
        public bool TryEnter(TimeSpan timeout)
        {
            return TryEnter(MillisecondsTimeoutFromTimeSpan(timeout));
        }
    }
}
