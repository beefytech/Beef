// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections;
using System.Diagnostics;

namespace System.Threading.Tasks
{
	public class Task<TResult> : Task
	{
		protected TResult m_result; // The value itself, if set.
		protected bool mHasCompleted;
		protected int32 mRefCount = 1;		

		TResult ResultOnSuccess
		{
		    get
		    {
		        //Contract.Assert(!IsWaitNotificationEnabledOrNotRanToCompletion, "Should only be used when the task completed successfully and there's no wait notification enabled");
		        return m_result; 
		    }
		}

		public TResult Result
		{
		    get { return this.[Friend]IsWaitNotificationEnabledOrNotRanToCompletion ? GetResultCore(true) : m_result; }
		}

		protected this()
		{

		}

		public this(delegate TResult(Object) func, Object state, CancellationToken cancellationToken, TaskCreationOptions creationOptions)
		    : this(func, state, Task.[Friend]InternalCurrentIfAttached(creationOptions), cancellationToken,
		            creationOptions, InternalTaskOptions.None, null)
		{
		    //StackCrawlMark stackMark = StackCrawlMark.LookForMyCaller;
		    //PossiblyCaptureContext(ref stackMark);
		}

		this(delegate TResult() valueSelector, Task parent, CancellationToken cancellationToken,
		    TaskCreationOptions creationOptions, InternalTaskOptions internalOptions, TaskScheduler scheduler) :
		    base(valueSelector, null, parent, cancellationToken, creationOptions, internalOptions, scheduler)
		{
		    if ((internalOptions & InternalTaskOptions.SelfReplicating) != 0)
		    {
		        Runtime.FatalError();
		    }
		}

		this(Delegate valueSelector, Object state, Task parent, CancellationToken cancellationToken,
		    TaskCreationOptions creationOptions, InternalTaskOptions internalOptions, TaskScheduler scheduler) :
		    base(valueSelector, state, parent, cancellationToken, creationOptions, internalOptions, scheduler)
		{
		    if ((internalOptions & InternalTaskOptions.SelfReplicating) != 0)
		    {
		        Runtime.FatalError();
		    }
		}

		public new TaskAwaiter<TResult> GetAwaiter()
		{
		    return TaskAwaiter<TResult>(this);
		}

		public void Ref()
		{
			//Interlocked.Increment(ref mRefCount);
			mRefCount++;
		}

		public void Deref()
		{
			//if (Interlocked.Decrement(ref mRefCount) == 1)

			if (--mRefCount == 0)
				delete this;
		}

		public void Dispose()
		{
			Deref();
		}

		// Implements Result.  Result delegates to this method if the result isn't already available.
		protected TResult GetResultCore(bool waitCompletionNotification)
		{
		    // If the result has not been calculated yet, wait for it.
		    if (!IsCompleted) this.[Friend]InternalWait(Timeout.Infinite, default(CancellationToken)); // won't throw if task faulted or canceled; that's handled below

		    // Notify the debugger of the wait completion if it's requested such a notification
		    //TODO: Implement
            //if (waitCompletionNotification) NotifyDebuggerOfWaitCompletionIfNecessary();

		    // Throw an exception if appropriate.
		    //TODO: ? if (!IsRanToCompletion) ThrowIfExceptional(true);

		    // We shouldn't be here if the result has not been set.
		    //Contract.Assert(IsRanToCompletion, "Task<T>.Result getter: Expected result to have been set.");

		    return m_result;
		}

		List<delegate void(Task<TResult>)> mContinuations = new .() ~ delete _;

		public Task ContinueWith(delegate void(Task<TResult>) continuationAction)
		{
			bool callDirectly = false;
			using (mMonitor.Enter())
			{
				if (!mHasCompleted)				
                	mContinuations.Add(continuationAction);								
				else
					callDirectly = true;				
			}
			if (callDirectly)
			{
				// The task has already completed, call directly
				Ref();
				continuationAction(this);
				Deref();
			}
			return null;
			//TODO: Not correct implementation
		    //StackCrawlMark stackMark = StackCrawlMark.LookForMyCaller;
		    //return ContinueWith(continuationAction, TaskScheduler.Current, default(CancellationToken), TaskContinuationOptions.None, ref stackMark);
		}

		public void Notify(bool allowDelete = true)
		{
			var continueList = scope List<delegate void(Task<TResult>)>(16);
			using (mMonitor.Enter())
			{
				mHasCompleted = true;
				for (var action in mContinuations)
					continueList.Add(action);
			}
			
			Ref();
			for (var action in continueList)
				action(this);
			Deref();			
		}
	}
}
