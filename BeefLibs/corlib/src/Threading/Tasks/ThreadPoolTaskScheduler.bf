// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Threading.Tasks
{
	class ThreadPoolTaskScheduler : TaskScheduler
	{
		/// Constructs a new ThreadPool task scheduler object
		internal this()
		{
		}

		// static delegate for threads allocated to handle LongRunning tasks.
		//private static readonly ParameterizedThreadStart s_longRunningThreadWork = new => LongRunningThreadWork;

		private static void LongRunningThreadWork(Object obj)
		{
		    //Contract.Requires(obj != null, "TaskScheduler.LongRunningThreadWork: obj is null");
		    Task t = obj as Task;
		    //Contract.Assert(t != null, "TaskScheduler.LongRunningThreadWork: t is null");
		    t.ExecuteEntry(false);
		}

		/// Schedules a task to the ThreadPool.
		/// @param task The task to schedule.
		protected internal override void QueueTask(Task task)
		{
		    if ((task.Options & TaskCreationOptions.LongRunning) != 0)
		    {
		        // Run LongRunning tasks on their own dedicated thread.
		        Thread thread = new Thread(new => LongRunningThreadWork);
		        thread.IsBackground = true; // Keep this thread from blocking process shutdown
		        thread.Start(task);
		    }
		    else
		    {
		        // Normal handling for non-LongRunning tasks.
		        //bool forceToGlobalQueue = ((task.Options & TaskCreationOptions.PreferFairness) != 0);
		        ThreadPool.QueueUserWorkItem(task);

		    }
		}

		/// This internal function will do this:
		///   (1) If the task had previously been queued, attempt to pop it and return false if that fails.
		///   (2) Propagate the return value from Task.ExecuteEntry() back to the caller.
		/// 
		/// IMPORTANT NOTE: TryExecuteTaskInline will NOT throw task exceptions itself. Any wait code path using this function needs
		/// to account for exceptions that need to be propagated, and throw themselves accordingly.
		protected override bool TryExecuteTaskInline(Task task, bool taskWasPreviouslyQueued)
		{
		    // If the task was previously scheduled, and we can't pop it, then return false.
		    if (taskWasPreviouslyQueued && !ThreadPool.TryPopCustomWorkItem(task))
		        return false;

		    // Propagate the return value of Task.ExecuteEntry()
		    bool rval = false;
		    //try
		    {
		        rval = task.ExecuteEntry(false); // handles switching Task.Current etc.
		    }
		    /*finally
		    {
		        //   Only call NWIP() if task was previously queued
		        if(taskWasPreviouslyQueued) NotifyWorkItemProgress();
		    }*/

		    return rval;
		}

		protected internal override bool TryDequeue(Task task)
		{
		    // just delegate to TP
		    return ThreadPool.TryPopCustomWorkItem(task);
		}

		/*protected override IEnumerable<Task> GetScheduledTasks()
		{
		    return FilterTasksFromWorkItems(ThreadPool.GetQueuedWorkItems());
		}

		private IEnumerable<Task> FilterTasksFromWorkItems(IEnumerable<IThreadPoolWorkItem> tpwItems)
		{
		    for (IThreadPoolWorkItem tpwi in tpwItems)
		    {
		        if (tpwi is Task)
		        {
		            yield return (Task)tpwi;
		        }
		    }
		}*/

		/// Notifies the scheduler that work is progressing (no-op).
		internal override void NotifyWorkItemProgress()
		{
		    //ThreadPool.NotifyWorkItemProgress();
		}

		/// This is the only scheduler that returns false for this property, indicating that the task entry codepath is unsafe (CAS free)
		/// since we know that the underlying scheduler already takes care of atomic transitions from queued to non-queued.
		internal override bool RequiresAtomicStartTransition
		{
		    get { return false; }
		}
	}
}
