using System;
using System.Diagnostics;
using System.Collections.Generic;

//#define DISABLE_THREADS

namespace System.Threading
{
	public delegate void WorkDelegate();

	enum WorkEntry
	{
		case WorkDelegate(WorkDelegate callback);
		case WorkItem(IThreadPoolWorkItem workItem);
	}

	struct ThreadStats
	{
		public volatile int32 mCount;
		public volatile int32 mMax;
		public volatile int32 mActive;
	}

	interface IThreadPoolWorkItem
	{
	    void ExecuteWorkItem();
	    void MarkAborted();
	}

	public static class ThreadPool
	{
		static int32 sConcurrency = 8;
		static bool sStarted = false;
		static List<Thread> sThreads = new List<Thread>() ~ delete _;
		static bool sShuttingDown;
		static Monitor sMonitor = new Monitor() ~ delete _;
		static Monitor sBindingMonitor = new Monitor() ~ delete _;
		static WaitEvent sThreadWakeEvent = new WaitEvent() ~ delete _;
		static WaitEvent sThreadClosedEvent = new WaitEvent() ~ delete _;

		static ThreadStats sWorkerThreadStats;
		static ThreadStats sIOThreadStats;
		static List<WorkEntry> mWorkEntries = new List<WorkEntry>() ~ delete _;

		static this()
		{
			//TODO: This is really bad, fix this pool! 
			sWorkerThreadStats.mMax = 4;
			sIOThreadStats.mMax = 4;
		}

		static ~this()
		{
			sShuttingDown = true;
			
			while (true)
			{
				sThreadWakeEvent.Set(true);

				using (sMonitor.Enter())
					if (sThreads.Count == 0)
						break;
				sThreadClosedEvent.WaitFor();
			}

			//Console.WriteLine("ThreadPool.~this done");
		}

		static void CheckSpawnWorkerThread()
		{
			using (sMonitor.Enter())
			{
				if ((sWorkerThreadStats.mActive == sWorkerThreadStats.mCount) && (sWorkerThreadStats.mCount < sWorkerThreadStats.mMax))
				{
					int threadId = sThreads.Count;
					sWorkerThreadStats.mCount++;
					sWorkerThreadStats.mActive++; // We know the thread will immediately become active

					Thread thread = new Thread(new () => { WorkerProc(threadId); });
					sThreads.Add(thread);
					thread.Start(false);
				}
			}
		}

		static bool TryPopCustomWorkItem(WorkEntry workEntry)
		{
			using (sMonitor.Enter())
			{
				return mWorkEntries.Remove(workEntry);
			}
		}

		public static bool TryPopCustomWorkItem(IThreadPoolWorkItem workItem)
		{
			return TryPopCustomWorkItem(.WorkItem(workItem));
		}

		public static bool QueueUserWorkItem(IThreadPoolWorkItem workItem)
		{
			using (sMonitor.Enter())
			{
				mWorkEntries.Add(.WorkItem(workItem));
				CheckSpawnWorkerThread();
				sThreadWakeEvent.Set();
			}
			return true;
		}

		public static bool QueueUserWorkItem(WorkDelegate waitCallback)
		{
#if DISABLE_THREADS
			waitCallback(userState);
			return true;
#endif

			Debug.AssertNotStack(waitCallback);
			using (sMonitor.Enter())
			{
				mWorkEntries.Add(.WorkDelegate(waitCallback));
				CheckSpawnWorkerThread();
				sThreadWakeEvent.Set();
			}
			return true;
		}

		public static int32 sBindCount = 0;
		public static int32 sReleaseBindCount = 0;

		static void Start()
		{
			sStarted = true;
		}

		static void ThreadDone()
		{
			using (sMonitor.Enter())
			{
				Thread currentThread = Thread.CurrentThread;
				sThreads.Remove(currentThread);
				delete currentThread;
				sThreadClosedEvent.Set();
			}
		}

		static void WorkerProc(int id)
		{
			let threadName = scope String();
			threadName.AppendF("ThreadPool Worker {0}", id);
			Thread.CurrentThread.SetName(threadName);

			while (true)
			{
				WorkEntry? workEntryOptional = null;
				using (sMonitor.Enter())
				{
					if (mWorkEntries.Count != 0)
						workEntryOptional = mWorkEntries.PopFront();
				}
				
				if (!workEntryOptional.HasValue)
				{
					sThreadWakeEvent.WaitFor();
					if (sShuttingDown)
						break;
					continue;
				}

				var workEntry = workEntryOptional.Value;
				switch (workEntry)
				{
				case .WorkDelegate(let callback):
					callback();
					delete callback;
				case .WorkItem(let workItem):
					workItem.ExecuteWorkItem();
				}
			}
			ThreadDone();
		}
	}
}
