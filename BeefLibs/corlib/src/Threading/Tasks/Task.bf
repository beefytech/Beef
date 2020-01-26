// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Collections.Generic;
using System.Diagnostics.Contracts;
using System.Diagnostics;

namespace System.Threading.Tasks
{
	class Task : IAsyncResult, IThreadPoolWorkItem
	{
		[ThreadStatic]
		internal static Task t_currentTask;  // The currently executing task.

		internal Object m_action ~ delete _;    // The body of the task.  Might be Action<object>, Action<TState> or Action.  Or possibly a Func.
		// If m_action is set to null it will indicate that we operate in the
		// "externally triggered completion" mode, which is exclusively meant 
		// for the signalling Task<TResult> (aka. promise). In this mode,
		// we don't call InnerInvoke() in response to a Wait(), but simply wait on
		// the completion event which will be set when the Future class calls Finish().
		// But the event would now be signalled if Cancel() is called

		internal Object m_stateObject; // A state object that can be optionally supplied, passed to action.
		internal TaskScheduler m_taskScheduler; // The task scheduler this task runs under. 
		internal readonly Task m_parent; // A task's parent, or null if parent-less.
		internal volatile int32 m_stateFlags;

		// m_continuationObject is set to this when the task completes.
		private static readonly Object s_taskCompletionSentinel = new Object() ~ delete _;

		// State constants for m_stateFlags;
		// The bits of m_stateFlags are allocated as follows:
		//   0x40000000 - TaskBase state flag
		//   0x3FFF0000 - Task state flags
		//   0x0000FF00 - internal TaskCreationOptions flags
		//   0x000000FF - publicly exposed TaskCreationOptions flags
		//
		// See TaskCreationOptions for bit values associated with TaskCreationOptions

		private const int CANCELLATION_REQUESTED = 0x1;//

		private const int32 OptionsMask = 0xFFFF; // signifies the Options portion of m_stateFlags bin: 0000 0000 0000 0000 1111 1111 1111 1111
		internal const int32 TASK_STATE_STARTED = 0x10000;                                       //bin: 0000 0000 0000 0001 0000 0000 0000 0000
		internal const int32 TASK_STATE_DELEGATE_INVOKED = 0x20000;                              //bin: 0000 0000 0000 0010 0000 0000 0000 0000
		internal const int32 TASK_STATE_DISPOSED = 0x40000;                                      //bin: 0000 0000 0000 0100 0000 0000 0000 0000
		internal const int32 TASK_STATE_EXCEPTIONOBSERVEDBYPARENT = 0x80000;                     //bin: 0000 0000 0000 1000 0000 0000 0000 0000
		internal const int32 TASK_STATE_CANCELLATIONACKNOWLEDGED = 0x100000;                     //bin: 0000 0000 0001 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_FAULTED = 0x200000;                                      //bin: 0000 0000 0010 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_CANCELED = 0x400000;                                     //bin: 0000 0000 0100 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_WAITING_ON_CHILDREN = 0x800000;                          //bin: 0000 0000 1000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_RAN_TO_COMPLETION = 0x01000000;                           //bin: 0000 0001 0000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_WAITINGFORACTIVATION = 0x02000000;                        //bin: 0000 0010 0000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_COMPLETION_RESERVED = 0x04000000;                         //bin: 0000 0100 0000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_THREAD_WAS_ABORTED = 0x08000000;                          //bin: 0000 1000 0000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_WAIT_COMPLETION_NOTIFICATION = 0x10000000;               //bin: 0001 0000 0000 0000 0000 0000 0000 0000
		//This could be moved to InternalTaskOptions enum
		internal const int32 TASK_STATE_EXECUTIONCONTEXT_IS_NULL = 0x20000000;                   //bin: 0010 0000 0000 0000 0000 0000 0000 0000
		internal const int32 TASK_STATE_TASKSCHEDULED_WAS_FIRED = 0x40000000;                    //bin: 0100 0000 0000 0000 0000 0000 0000 0000

		// A mask for all of the final states a task may be in
		private const int32 TASK_STATE_COMPLETED_MASK = TASK_STATE_CANCELED | TASK_STATE_FAULTED | TASK_STATE_RAN_TO_COMPLETION;

		// Can be null, a single continuation, a list of continuations, or s_taskCompletionSentinel,
		// in that order. The logic arround this object assumes it will never regress to a previous state.
		private volatile Object m_continuationObject ~ { if (_ != s_taskCompletionSentinel) delete _; };
		private Object mOldContinuationObject ~ delete _;
		protected Monitor mMonitor = new Monitor() ~ delete _;

		protected enum DetachState : int32
		{
			Deatched = 1,
			Done = 2
		}
		protected DetachState mDetachState = default;

		internal static Task InternalCurrent
		{
		    get { return t_currentTask; }
		}

		public bool IsCompleted
		{
		    get
		    {
		        int32 stateFlags = m_stateFlags; // enable inlining of IsCompletedMethod by "cast"ing away the volatility
		        return IsCompletedMethod(stateFlags);
		    }
		}

		private static bool IsCompletedMethod(int32 flags)
		{
		    return (flags & TASK_STATE_COMPLETED_MASK) != 0;
		}

		WaitEvent IAsyncResult.AsyncWaitHandle
		{
		    // Although a slim event is used internally to avoid kernel resource allocation, this function
		    // forces allocation of a true WaitHandle when called.
		    get
		    {
		        bool isDisposed = (m_stateFlags & TASK_STATE_DISPOSED) != 0;
		        if (isDisposed)
		        {
		            //throw new ObjectDisposedException(null, Environment.GetResourceString("Task_ThrowIfDisposed"));
					Runtime.FatalError();
		        }
		        //return CompletedEvent.WaitHandle;
				ThrowUnimplemented();
		    }
		}

		public Object AsyncState
		{
		    get { return m_stateObject; }
		}

		internal class ContingentProperties
		{
			internal CancellationToken m_cancellationToken;
			internal volatile int m_internalCancellationRequested;
			//internal CancellationTokenRegistration* m_cancellationRegistration;
			internal int m_completionCountdown;
			internal volatile WaitEvent m_completionEvent ~ delete _;

			internal void SetCompleted()
			{
			    var mres = m_completionEvent;
			    if (mres != null) mres.Set();
			}

			internal void DeregisterCancellationCallback()
			{
			    /*if (m_cancellationRegistration != null)
			    {
			        // Harden against ODEs thrown from disposing of the CTR.
			        // Since the task has already been put into a final state by the time this
			        // is called, all we can do here is suppress the exception.
			        try { m_cancellationRegistration.Value.Dispose(); }
			        catch (ObjectDisposedException) { }
			        m_cancellationRegistration = null;
			    }*/
			}
		}

		internal volatile ContingentProperties m_contingentProperties;

		internal bool IsCancellationRequested
		{
		    get
		    {
		        // check both the internal cancellation request flag and the CancellationToken attached to this task
		        var props = m_contingentProperties;
		        return props != null &&
		            (props.m_internalCancellationRequested == CANCELLATION_REQUESTED ||
		             props.m_cancellationToken.IsCancellationRequested);
		    }
		}

		internal bool IsRanToCompletion
		{
		    get { return (m_stateFlags & TASK_STATE_COMPLETED_MASK) == TASK_STATE_RAN_TO_COMPLETION; }
		}

		internal TaskScheduler ExecutingTaskScheduler
		{
		    get { return m_taskScheduler; }
		}

		/*internal CancellationToken CancellationToken
		{
		    get
		    {
		        var props = m_contingentProperties;
		        return (props == null) ? default(CancellationToken) : props.m_cancellationToken;
		    }
		}*/

		internal TaskCreationOptions Options
		{
		    get
		    {
		        int32 stateFlags = m_stateFlags; // "cast away" volatility to enable inlining of OptionsMethod
		        return OptionsMethod(stateFlags);
		    }
		}

		// Similar to Options property, but allows for the use of a cached flags value rather than
		// a read of the volatile m_stateFlags field.
		internal static TaskCreationOptions OptionsMethod(int32 flags)
		{
		    Contract.Assert((OptionsMask & 1) == 1, "OptionsMask needs a shift in Options.get");
		    return (TaskCreationOptions)(flags & OptionsMask);
		}

		public bool IsCanceled
		{
		    get
		    {
		        // Return true if canceled bit is set and faulted bit is not set
		        return (m_stateFlags & (TASK_STATE_CANCELED | TASK_STATE_FAULTED)) == TASK_STATE_CANCELED;
		    }
		}

		public TaskCreationOptions CreationOptions
		{
			get { return Options & (TaskCreationOptions)(~InternalTaskOptions.InternalOptionsMask); }
		}

		internal bool IsCancellationAcknowledged
		{
		    get { return (m_stateFlags & TASK_STATE_CANCELLATIONACKNOWLEDGED) != 0; }
		}

		bool IAsyncResult.CompletedSynchronously
		{
		    get
		    {
		        return false;
		    }
		}

		internal bool IsDelegateInvoked
		{
		    get
		    {
		        return (m_stateFlags & TASK_STATE_DELEGATE_INVOKED) != 0;
		    }
		}

		internal bool IsWaitNotificationEnabledOrNotRanToCompletion
		{
		    //[MethodImpl(MethodImplOptions.AggressiveInlining)]
		    get
		    {
		        return (m_stateFlags & (Task.TASK_STATE_WAIT_COMPLETION_NOTIFICATION | Task.TASK_STATE_RAN_TO_COMPLETION))
		                != Task.TASK_STATE_RAN_TO_COMPLETION;
		    }
		}

		protected this()
		{

		}

		public this(Action action)
		    : this(action, null, null, default(CancellationToken), TaskCreationOptions.None, InternalTaskOptions.None, null)
		{
		    
		}

		public this(Action action, CancellationToken cancellationToken)
		    : this(action, null, null, cancellationToken, TaskCreationOptions.None, InternalTaskOptions.None, null)
		{
		    
		}

		public this(Action<Object> action, Object state, CancellationToken cancellationToken, TaskCreationOptions creationOptions)
		    : this(action, state, Task.InternalCurrentIfAttached(creationOptions), cancellationToken, creationOptions, InternalTaskOptions.None, null)
		{
		    //StackCrawlMark stackMark = StackCrawlMark.LookForMyCaller;
		    //PossiblyCaptureContext(ref stackMark);
		}

		internal this(Delegate action, Object state, Task parent, CancellationToken cancellationToken,
            TaskCreationOptions creationOptions, InternalTaskOptions internalOptions, TaskScheduler scheduler)
        {
            if (action == null)
            {
                Runtime.FatalError();
            }
            //Contract.EndContractBlock();

            // This is readonly, and so must be set in the constructor
            // Keep a link to your parent if: (A) You are attached, or (B) you are self-replicating.
            if (((creationOptions & TaskCreationOptions.AttachedToParent) != 0) ||
                ((internalOptions & InternalTaskOptions.SelfReplicating) != 0)
                )
            {
                m_parent = parent;
            }

            TaskConstructorCore(action, state, cancellationToken, creationOptions, internalOptions, scheduler);
        }

		public ~this()
		{
			mDetachState = default;
			InternalWait(-1, default(CancellationToken));
		}

		void SetDetachFlag(DetachState detachState)
		{
			while (true)
			{
				let oldDetachState = mDetachState;
				DetachState newDetachState = oldDetachState | detachState;
				if (Interlocked.CompareExchange(ref mDetachState, oldDetachState, newDetachState) == oldDetachState)
				{
					if (newDetachState == .Deatched | .Done)
					{
						delete this;
						return;
					}
					break;
				}
			}
		}

		public void Detach()
		{
			SetDetachFlag(.Deatched);
		}

		void Detach_SetDone()
		{
			SetDetachFlag(.Done);
		}

		void IThreadPoolWorkItem.ExecuteWorkItem()
		{
		    ExecuteEntry(false);
		}

		void IThreadPoolWorkItem.MarkAborted()
		{
		    // If the task has marked itself as Completed, then it either a) already observed this exception (so we shouldn't handle it here)
		    // or b) completed before the exception ocurred (in which case it shouldn't count against this Task).
		    if (!IsCompleted)
		    {
				//TODO:
		        //HandleException(tae);
		        //FinishThreadAbortedTask(true, false);
		    }
		}

		internal void FinishStageTwo()
		{
		    //AddExceptionsFromChildren();

		    // At this point, the task is done executing and waiting for its children,
		    // we can transition our task to a completion state.  
		    int32 completionState;
		    /*if (ExceptionRecorded)
		    {
		        completionState = TASK_STATE_FAULTED;
		        if (AsyncCausalityTracer.LoggingOn)
		            AsyncCausalityTracer.TraceOperationCompletion(CausalityTraceLevel.Required, this.Id, AsyncCausalityStatus.Error);

		        if (Task.s_asyncDebuggingEnabled)
		        {
		            RemoveFromActiveTasks(this.Id);
		        }
		    }
		    else*/ if (IsCancellationRequested && IsCancellationAcknowledged)
		    {
		        // We transition into the TASK_STATE_CANCELED final state if the task's CT was signalled for cancellation, 
		        // and the user delegate acknowledged the cancellation request by throwing an OCE, 
		        // and the task hasn't otherwise transitioned into faulted state. (TASK_STATE_FAULTED trumps TASK_STATE_CANCELED)
		        //
		        // If the task threw an OCE without cancellation being requestsed (while the CT not being in signaled state),
		        // then we regard it as a regular exception

		        completionState = TASK_STATE_CANCELED;
		        /*if (AsyncCausalityTracer.LoggingOn)
		            AsyncCausalityTracer.TraceOperationCompletion(CausalityTraceLevel.Required, this.Id, AsyncCausalityStatus.Canceled);

		        if (Task.s_asyncDebuggingEnabled)
		        {
		            RemoveFromActiveTasks(this.Id);
		        }*/
		    }
		    else
		    {
		        completionState = TASK_STATE_RAN_TO_COMPLETION;
		        /*if (AsyncCausalityTracer.LoggingOn)
		            AsyncCausalityTracer.TraceOperationCompletion(CausalityTraceLevel.Required, this.Id, AsyncCausalityStatus.Completed);*/

		        /*if (Task.s_asyncDebuggingEnabled)
		        {
		            RemoveFromActiveTasks(this.Id);
		        }*/
		    }

		    // Use Interlocked.Exchange() to effect a memory fence, preventing
		    // any SetCompleted() (or later) instructions from sneak back before it.
		    Interlocked.Exchange(ref m_stateFlags, m_stateFlags | completionState);

		    // Set the completion event if it's been lazy allocated.
		    // And if we made a cancellation registration, it's now unnecessary.
		    var cp = m_contingentProperties;
		    if (cp != null)
		    {
		        cp.SetCompleted();
		        cp.DeregisterCancellationCallback();
		    }

		    // ready to run continuations and notify parent.
		    FinishStageThree();
		}

		internal void FinishStageThree()
		{
			delete m_action;
		    // Release the action so that holding this task object alive doesn't also
		    // hold alive the body of the task.  We do this before notifying a parent,
		    // so that if notifying the parent completes the parent and causes
		    // its synchronous continuations to run, the GC can collect the state
		    // in the interim.  And we do it before finishing continuations, because
		    // continuations hold onto the task, and therefore are keeping it alive.
		    m_action = null;

		    // Notify parent if this was an attached task
		    if (m_parent != null
		         && ((m_parent.CreationOptions & TaskCreationOptions.DenyChildAttach) == 0)
		         && (((TaskCreationOptions)(m_stateFlags & OptionsMask)) & TaskCreationOptions.AttachedToParent) != 0)
		    {
		        m_parent.ProcessChildCompletion(this);
		    }

		    // Activate continuations (if any).
		    FinishContinuations();
		}

		void LogFinishCompletionNotification()
		{
			//TODO:
		}

		internal void FinishContinuations()
		{
		    // Atomically store the fact that this task is completing.  From this point on, the adding of continuations will
		    // result in the continuations being run/launched directly rather than being added to the continuation list.
		    Object continuationObject = Interlocked.Exchange(ref m_continuationObject, s_taskCompletionSentinel);
		    //TplEtwProvider.Log.RunningContinuation(Id, continuationObject);

		    // If continuationObject == null, then we don't have any continuations to process
		    if (continuationObject != null)
		    {
				mOldContinuationObject = continuationObject;
		        /*if (AsyncCausalityTracer.LoggingOn)
		            AsyncCausalityTracer.TraceSynchronousWorkStart(CausalityTraceLevel.Required, this.Id, CausalitySynchronousWork.CompletionNotification);*/

		        // Skip synchronous execution of continuations if this task's thread was aborted
#unwarn
		        bool bCanInlineContinuations = !(((m_stateFlags & TASK_STATE_THREAD_WAS_ABORTED) != 0) ||
		                                          (Thread.CurrentThread.ThreadState == ThreadState.AbortRequested) ||
		                                          ((m_stateFlags & (int)TaskCreationOptions.RunContinuationsAsynchronously) != 0));

		        // Handle the single-Action case
		        Action singleAction = continuationObject as Action;
		        if (singleAction != null)
		        {
					Runtime.FatalError();
		            //AwaitTaskContinuation.RunOrScheduleAction(singleAction, bCanInlineContinuations, ref t_currentTask);
#unwarn
		            LogFinishCompletionNotification();
		            return;
		        }

		        // Handle the single-ITaskCompletionAction case
		        ITaskCompletionAction singleTaskCompletionAction = continuationObject as ITaskCompletionAction;
		        if (singleTaskCompletionAction != null)
		        {
		            singleTaskCompletionAction.Invoke(this);
		            LogFinishCompletionNotification();
		            return;
		        }

		        // Handle the single-TaskContinuation case
				//TODO:
		        /*TaskContinuation singleTaskContinuation = continuationObject as TaskContinuation;
		        if (singleTaskContinuation != null)
		        {
		            singleTaskContinuation.Run(this, bCanInlineContinuations);
		            LogFinishCompletionNotification();
		            return;
		        }*/

		        // Not a single; attempt to cast as list
		        List<Object> continuations = continuationObject as List<Object>;
		        if (continuations == null)
		        {
		            LogFinishCompletionNotification();
		            return;  // Not a single or a list; just return
		        }

		        //
		        // Begin processing of continuation list
		        //

		        // Wait for any concurrent adds or removes to be retired
		        /*lock (continuations){ }
		        int continuationCount = continuations.Count;

		        // Fire the asynchronous continuations first ...
		        for (int i = 0; i < continuationCount; i++)
		        {
		            // Synchronous continuation tasks will have the ExecuteSynchronously option,
		            // and we're looking for asynchronous tasks...
		            var tc = continuations[i] as StandardTaskContinuation;
		            if (tc != null && (tc.m_options & TaskContinuationOptions.ExecuteSynchronously) == 0)
		            {
		                TplEtwProvider.Log.RunningContinuationList(Id, i, tc);
		                continuations[i] = null; // so that we can skip this later
		                tc.Run(this, bCanInlineContinuations);
		            }
		        }

		        // ... and then fire the synchronous continuations (if there are any).
		        // This includes ITaskCompletionAction, AwaitTaskContinuations, and
		        // Action delegates, which are all by default implicitly synchronous.
		        for (int i = 0; i < continuationCount; i++)
		        {
		            object currentContinuation = continuations[i];
		            if (currentContinuation == null) continue;
		            continuations[i] = null; // to enable free'ing up memory earlier
		            TplEtwProvider.Log.RunningContinuationList(Id, i, currentContinuation);

		            // If the continuation is an Action delegate, it came from an await continuation,
		            // and we should use AwaitTaskContinuation to run it.
		            Action ad = currentContinuation as Action;
		            if (ad != null)
		            {
		                AwaitTaskContinuation.RunOrScheduleAction(ad, bCanInlineContinuations, ref t_currentTask);
		            }
		            else
		            {
		                // If it's a TaskContinuation object of some kind, invoke it.
		                TaskContinuation tc = currentContinuation as TaskContinuation;
		                if (tc != null)
		                {
		                    // We know that this is a synchronous continuation because the
		                    // asynchronous ones have been weeded out
		                    tc.Run(this, bCanInlineContinuations);
		                }
		                // Otherwise, it must be an ITaskCompletionAction, so invoke it.
		                else
		                {
		                    Contract.Assert(currentContinuation is ITaskCompletionAction, "Expected continuation element to be Action, TaskContinuation, or ITaskContinuationAction");
		                    var action = (ITaskCompletionAction)currentContinuation;
		                    action.Invoke(this);
		                }
		            }
		        }*/

		        LogFinishCompletionNotification();
		    }
		}

		internal void ProcessChildCompletion(Task childTask)
		{
		    Contract.Requires(childTask != null);
		    Contract.Requires(childTask.IsCompleted, "ProcessChildCompletion was called for an uncompleted task");

		    Contract.Assert(childTask.m_parent == this, "ProcessChildCompletion should only be called for a child of this task");

		    var props = m_contingentProperties;

		    // if the child threw and we haven't observed it we need to save it for future reference
		    /*if (childTask.IsFaulted && !childTask.IsExceptionObservedByParent)
		    {
		        // Lazily initialize the child exception list
		        if (props.m_exceptionalChildren == null)
		        {
		            Interlocked.CompareExchange(ref props.m_exceptionalChildren, null, new List<Task>());
		        }

		        // In rare situations involving AppDomainUnload, it's possible (though unlikely) for FinishStageTwo() to be called
		        // multiple times for the same task.  In that case, AddExceptionsFromChildren() could be nulling m_exceptionalChildren
		        // out at the same time that we're processing it, resulting in a NullReferenceException here.  We'll protect
		        // ourselves by caching m_exceptionChildren in a local variable.
		        List<Task> tmp = props.m_exceptionalChildren;
		        if (tmp != null)
		        {
		            lock (tmp)
		            {
		                tmp.Add(childTask);
		            }
		        }

		    }*/

		    if (Interlocked.Decrement(ref props.m_completionCountdown) == 0)
		    {
		        // This call came from the final child to complete, and apparently we have previously given up this task's right to complete itself.
		        // So we need to invoke the final finish stage.

		        FinishStageTwo();
		    }
		}

		internal void Finish(bool bUserDelegateExecuted)
		{
			if (!bUserDelegateExecuted)
			{
			    // delegate didn't execute => no children. We can safely call the remaining finish stages
			    FinishStageTwo();
			}
			else
			{
			    var props = m_contingentProperties;

			    if (props == null || // no contingent properties means no children, so it's safe to complete ourselves
			        (props.m_completionCountdown == 1 && !IsSelfReplicatingRoot) ||
			        // Count of 1 => either all children finished, or there were none. Safe to complete ourselves 
			        // without paying the price of an Interlocked.Decrement.
			        // However we need to exclude self replicating root tasks from this optimization, because
			        // they can have children joining in, or finishing even after the root task delegate is done.
			        Interlocked.Decrement(ref props.m_completionCountdown) == 0) // Reaching this sub clause means there may be remaining active children,
			    // and we could be racing with one of them to call FinishStageTwo().
			    // So whoever does the final Interlocked.Dec is responsible to finish.
			    {
			        FinishStageTwo();
			    }
			    else
			    {
			        // Apparently some children still remain. It will be up to the last one to process the completion of this task on their own thread.
			        // We will now yield the thread back to ThreadPool. Mark our state appropriately before getting out.

			        // We have to use an atomic update for this and make sure not to overwrite a final state, 
			        // because at this very moment the last child's thread may be concurrently completing us.
			        // Otherwise we risk overwriting the TASK_STATE_RAN_TO_COMPLETION, _CANCELED or _FAULTED bit which may have been set by that child task.
			        // Note that the concurrent update by the last child happening in FinishStageTwo could still wipe out the TASK_STATE_WAITING_ON_CHILDREN flag, 
			        // but it is not critical to maintain, therefore we dont' need to intruduce a full atomic update into FinishStageTwo

			        AtomicStateUpdate(TASK_STATE_WAITING_ON_CHILDREN, TASK_STATE_FAULTED | TASK_STATE_CANCELED | TASK_STATE_RAN_TO_COMPLETION);
			    }

			    // Now is the time to prune exceptional children. We'll walk the list and removes the ones whose exceptions we might have observed after they threw.
			    // we use a local variable for exceptional children here because some other thread may be nulling out m_contingentProperties.m_exceptionalChildren 
			    /*List<Task> exceptionalChildren = props != null ? props.m_exceptionalChildren : null;

			    if (exceptionalChildren != null)
			    {
			        lock (exceptionalChildren)
			        {
			            exceptionalChildren.RemoveAll(s_IsExceptionObservedByParentPredicate); // RemoveAll has better performance than doing it ourselves
			        }
			    }*/
			}
		}

		internal bool FireTaskScheduledIfNeeded(TaskScheduler ts)
		{
		    /*var etwLog = TplEtwProvider.Log;
		    if (etwLog.IsEnabled() && (m_stateFlags & Task.TASK_STATE_TASKSCHEDULED_WAS_FIRED) == 0)
		    {
		        m_stateFlags |= Task.TASK_STATE_TASKSCHEDULED_WAS_FIRED;

		        Task currentTask = Task.InternalCurrent;
		        Task parentTask = this.m_parent;
		        etwLog.TaskScheduled(ts.Id, currentTask == null ? 0 : currentTask.Id,
		                             this.Id, parentTask == null ? 0 : parentTask.Id, (int)this.Options,
		                             System.Threading.Thread.GetDomainID());
		        return true;
		    }
		    else*/
		        return false;
		}

		internal void TaskConstructorCore(Object action, Object state, CancellationToken cancellationToken,
		    TaskCreationOptions creationOptions, InternalTaskOptions internalOptions, TaskScheduler scheduler)
		{
		    m_action = action;
		    m_stateObject = state;
		    m_taskScheduler = scheduler;

		    // Check for validity of options
		    if ((creationOptions &
		            ~(TaskCreationOptions.AttachedToParent |
		              TaskCreationOptions.LongRunning |
		              TaskCreationOptions.DenyChildAttach |
		              TaskCreationOptions.HideScheduler |
		              TaskCreationOptions.PreferFairness |
		              TaskCreationOptions.RunContinuationsAsynchronously)) != 0)
		    {
		        Runtime.FatalError();
		    }

#if DEBUG
		    // Check the validity of internalOptions
		    int32 illegalInternalOptions = 
		            (int32) (internalOptions &
		                    ~(InternalTaskOptions.SelfReplicating |
		                      InternalTaskOptions.ChildReplica |
		                      InternalTaskOptions.PromiseTask |
		                      InternalTaskOptions.ContinuationTask |
		                      InternalTaskOptions.LazyCancellation |
		                      InternalTaskOptions.QueuedByRuntime));
		    Contract.Assert(illegalInternalOptions == 0, "TaskConstructorCore: Illegal internal options");
#endif

		    // Throw exception if the user specifies both LongRunning and SelfReplicating
		    if (((creationOptions & TaskCreationOptions.LongRunning) != 0) &&
		        ((internalOptions & InternalTaskOptions.SelfReplicating) != 0))
		    {
		        Runtime.FatalError();
		    }

		    // Assign options to m_stateAndOptionsFlag.
		    Contract.Assert(m_stateFlags == 0, "TaskConstructorCore: non-zero m_stateFlags");
		    Contract.Assert((((int32)creationOptions) | OptionsMask) == OptionsMask, "TaskConstructorCore: options take too many bits");
		    var tmpFlags = (int32)creationOptions | (int32)internalOptions;
		    if ((m_action == null) || ((internalOptions & InternalTaskOptions.ContinuationTask) != 0))
		    {
		        // For continuation tasks or TaskCompletionSource.Tasks, begin life in the 
		        // WaitingForActivation state rather than the Created state.
		        tmpFlags |= TASK_STATE_WAITINGFORACTIVATION;
		    }
		    m_stateFlags = tmpFlags; // one write to the volatile m_stateFlags instead of two when setting the above options

		    // Now is the time to add the new task to the children list 
		    // of the creating task if the options call for it.
		    // We can safely call the creator task's AddNewChild() method to register it, 
		    // because at this point we are already on its thread of execution.

		    if (m_parent != null
		        && ((creationOptions & TaskCreationOptions.AttachedToParent) != 0)
		        && ((m_parent.CreationOptions & TaskCreationOptions.DenyChildAttach) == 0)
		        )
		    {
		        m_parent.AddNewChild();
		    }

		    // if we have a non-null cancellationToken, allocate the contingent properties to save it
		    // we need to do this as the very last thing in the construction path, because the CT registration could modify m_stateFlags
			//TODO:
		    /*if (cancellationToken.CanBeCanceled)
		    {
		        Contract.Assert((internalOptions &
		            (InternalTaskOptions.ChildReplica | InternalTaskOptions.SelfReplicating | InternalTaskOptions.ContinuationTask)) == 0,
		            "TaskConstructorCore: Did not expect to see cancelable token for replica/replicating or continuation task.");

		        AssignCancellationToken(cancellationToken, null, null);
		    }*/
		}

		private bool WrappedTryRunInline()
		{
		    if (m_taskScheduler == null)
		        return false;
			return m_taskScheduler.TryRunInline(this, true);
		}

		private bool SpinWait(int millisecondsTimeout)
		{
		    if (IsCompleted) return true;

		    if (millisecondsTimeout == 0)
		    {
		        // For 0-timeouts, we just return immediately.
		        return false;
		    }

		    //This code is pretty similar to the custom spinning in MRES except there is no yieling after we exceed the spin count
		    int spinCount = Platform.IsSingleProcessor ? 1 : System.Threading.SpinWait.YIELD_THRESHOLD; //spin only once if we are running on a single CPU
		    for (int i = 0; i < spinCount; i++)
		    {
		        if (IsCompleted)
		        {
		            return true;
		        }

		        if (i == spinCount / 2)
		        {
		            Thread.Yield();
		        }
		        else
		        {
		            Thread.SpinWait(Platform.ProcessorCount * (4 << i));
		        }

		    }

		    return IsCompleted;
		}

		public class SetOnInvokeMres : ITaskCompletionAction
		{
			WaitEvent mWaitEvent = new WaitEvent() ~ delete _;

			public void Invoke(Task completingTask)
			{
				mWaitEvent.Set();
			}

			public Result<bool> Wait(int timeout, CancellationToken cancellationToken)
			{
				if (cancellationToken.IsCancellationRequested)
					return .Err;
				return mWaitEvent.WaitFor(timeout);
			}
		}

		private bool SpinThenBlockingWait(int millisecondsTimeout, CancellationToken cancellationToken)
		{
		    bool infiniteWait = millisecondsTimeout == Timeout.Infinite;
		    uint startTimeTicks = infiniteWait ? 0 : (uint)Environment.TickCount;
		    bool returnValue = SpinWait(millisecondsTimeout);
		    if (!returnValue)
		    {
		        var mres = new SetOnInvokeMres();
		        //try
		        {
		            AddCompletionAction(mres, true);
		            if (infiniteWait)
		            {
						//TODO: Handle cancelled
		                returnValue = mres.Wait(Timeout.Infinite, cancellationToken);
		            }
		            else
		            {
		                uint elapsedTimeTicks = ((uint)Environment.TickCount) - startTimeTicks;
		                if (elapsedTimeTicks < (uint)millisecondsTimeout)
		                {
		                    returnValue = mres.Wait((int)millisecondsTimeout - (int)elapsedTimeTicks, cancellationToken);
		                }
		            }
		        }
		        /*finally
		        {
		            if (!IsCompleted) RemoveContinuation(mres);
		            // Don't Dispose of the MRES, because the continuation off of this task may
		            // still be running.  This is ok, however, as we never access the MRES' WaitHandle,
		            // and thus no finalizable resources are actually allocated.
		        }*/
		    }
		    return returnValue;
		}

		/// The core wait function, which is only accesible internally. It's meant to be used in places in TPL code where 
		/// the current context is known or cached.
		//[MethodImpl(MethodImplOptions.NoOptimization)]  // this is needed for the parallel debugger
		internal bool InternalWait(int millisecondsTimeout, CancellationToken cancellationToken)
		{
			Debug.Assert(!mDetachState.HasFlag(.Deatched));

		    // ETW event for Task Wait Begin
		    /*var etwLog = TplEtwProvider.Log;
		    bool etwIsEnabled = etwLog.IsEnabled();
		    if (etwIsEnabled)
		    {
		        Task currentTask = Task.InternalCurrent;
		        etwLog.TaskWaitBegin(
		            (currentTask != null ? currentTask.m_taskScheduler.Id : TaskScheduler.Default.Id), (currentTask != null ? currentTask.Id : 0),
		            this.Id, TplEtwProvider.TaskWaitBehavior.Synchronous, 0, System.Threading.Thread.GetDomainID());
		    }*/

		    bool returnValue = IsCompleted;

		    // If the event hasn't already been set, we will wait.
		    if (!returnValue)
		    {
		        // Alert a listening debugger that we can't make forward progress unless it slips threads.
		        // We call NOCTD for two reasons:
		        //    1. If the task runs on another thread, then we'll be blocked here indefinitely.
		        //    2. If the task runs inline but takes some time to complete, it will suffer ThreadAbort with possible state corruption,
		        //       and it is best to prevent this unless the user explicitly asks to view the value with thread-slipping enabled.
		        //Debugger.NotifyOfCrossThreadDependency();

		        // We will attempt inline execution only if an infinite wait was requested
		        // Inline execution doesn't make sense for finite timeouts and if a cancellation token was specified
		        // because we don't know how long the task delegate will take.
		        if (millisecondsTimeout == Timeout.Infinite && !cancellationToken.CanBeCanceled &&
		            WrappedTryRunInline() && IsCompleted) // TryRunInline doesn't guarantee completion, as there may be unfinished children.
		        {
		            returnValue = true;
		        }
		        else
		        {
		            returnValue = SpinThenBlockingWait(millisecondsTimeout, cancellationToken);
		        }
		    }

		    Contract.Assert(IsCompleted || millisecondsTimeout != Timeout.Infinite);

		    // ETW event for Task Wait End
		    /*if (etwIsEnabled)
		    {
		        Task currentTask = Task.InternalCurrent;
		        if (currentTask != null)
		        {
		            etwLog.TaskWaitEnd(currentTask.m_taskScheduler.Id, currentTask.Id, this.Id);
		        }
		        else
		        {
		            etwLog.TaskWaitEnd(TaskScheduler.Default.Id, 0, this.Id);
		        }
		        // logically the continuation is empty so we immediately fire
		        etwLog.TaskWaitContinuationComplete(this.Id);
		    }*/

		    return returnValue;

			//TODO:
			/*while (true)
			{
				if (IsCompleted)
					break;
				Thread.Sleep(0);
			}

			return true;*/
		}

		public void Start()
		{
		    Start(TaskScheduler.Current);
		}

		public void Start(TaskScheduler scheduler)
		{
		    // Read the volatile m_stateFlags field once and cache it for subsequent operations
		    //int flags = m_stateFlags;

		    // Need to check this before (m_action == null) because completed tasks will
		    // set m_action to null.  We would want to know if this is the reason that m_action == null.
		    /*if (IsCompletedMethod(flags))
		    {
		        throw new InvalidOperationException(Environment.GetResourceString("Task_Start_TaskCompleted"));
		    }*/

		    if (scheduler == null)
		    {
		        //throw new ArgumentNullException("scheduler");
		    }

		    /*var options = OptionsMethod(flags);
		    if ((options & (TaskCreationOptions)InternalTaskOptions.PromiseTask) != 0)
		    {
		        //throw new InvalidOperationException(Environment.GetResourceString("Task_Start_Promise"));
		    }
		    if ((options & (TaskCreationOptions)InternalTaskOptions.ContinuationTask) != 0)
		    {
		        //throw new InvalidOperationException(Environment.GetResourceString("Task_Start_ContinuationTask"));
		    }*/

		    // Make sure that Task only gets started once.  Or else throw an exception.
		    if (Interlocked.CompareExchange(ref m_taskScheduler, null, scheduler) != null)
		    {
		        //throw new InvalidOperationException(Environment.GetResourceString("Task_Start_AlreadyStarted"));
		    }

		    ScheduleAndStart(true);
		}

		internal bool AtomicStateUpdate(int32 newBits, int32 illegalBits)
		{
		    // This could be implemented in terms of:
		    //     internal bool AtomicStateUpdate(int newBits, int illegalBits, ref int oldFlags);
		    // but for high-throughput perf, that delegation's cost is noticeable.

		    SpinWait sw = .();
		    repeat
		    {
		        int32 oldFlags = m_stateFlags;
		        if ((oldFlags & illegalBits) != 0) return false;
		        if (Interlocked.CompareExchange(ref m_stateFlags, oldFlags, oldFlags | newBits) == oldFlags)
		        {
		            return true;
		        }
		        sw.SpinOnce();
		    }
			while (true);
		}

		internal bool AtomicStateUpdate(int32 newBits, int32 illegalBits, ref int32 oldFlags)
		{
		    SpinWait sw = .();
		    repeat
		    {
		        oldFlags = m_stateFlags;
		        if ((oldFlags & illegalBits) != 0) return false;
		        if (Interlocked.CompareExchange(ref m_stateFlags, oldFlags, oldFlags | newBits) == oldFlags)
		        {
		            return true;
		        }
		        sw.SpinOnce();
		    }
			while (true);
		}

		// ASSUMES THAT A SUCCESSFUL CANCELLATION HAS JUST OCCURRED ON THIS TASK!!!
		// And this method should be called at most once per task.
		internal void CancellationCleanupLogic()
		{
			Interlocked.Exchange(ref m_stateFlags, m_stateFlags | TASK_STATE_CANCELED);

		}

		internal bool MarkStarted()
		{
		    return AtomicStateUpdate(TASK_STATE_STARTED, TASK_STATE_CANCELED | TASK_STATE_STARTED);
		}

		internal void ScheduleAndStart(bool needsProtection)
		{
		    Contract.Assert(m_taskScheduler != null, "expected a task scheduler to have been selected");
		    Contract.Assert((m_stateFlags & TASK_STATE_STARTED) == 0, "task has already started");

		    // Set the TASK_STATE_STARTED bit
		    if (needsProtection)
		    {
		        if (!MarkStarted())
		        {
		            // A cancel has snuck in before we could get started.  Quietly exit.
		            return;
		        }
		    }
		    else
		    {
		        m_stateFlags |= TASK_STATE_STARTED;
		    }

		    /*if (s_asyncDebuggingEnabled)
		    {
		        AddToActiveTasks(this);
		    }

		    if (AsyncCausalityTracer.LoggingOn && (Options & (TaskCreationOptions)InternalTaskOptions.ContinuationTask) == 0)
		    {
		        //For all other task than TaskContinuations we want to log. TaskContinuations log in their constructor
		        AsyncCausalityTracer.TraceOperationCreation(CausalityTraceLevel.Required, this.Id, "Task: "+((Delegate)m_action).Method.Name, 0);
		    }*/


			m_taskScheduler.InternalQueueTask(this);

		    /*try
		    {
		        // Queue to the indicated scheduler.
		        m_taskScheduler.InternalQueueTask(this);
		    }
		    catch (ThreadAbortException tae)
		    {
		        AddException(tae);
		        FinishThreadAbortedTask(true, false);
		    }
		    catch (Exception e)
		    {
		        // The scheduler had a problem queueing this task.  Record the exception, leaving this task in
		        // a Faulted state.
		        TaskSchedulerException tse = new TaskSchedulerException(e);
		        AddException(tse);
		        Finish(false);

		        // Now we need to mark ourselves as "handled" to avoid crashing the finalizer thread if we are called from StartNew()
		        // or from the self replicating logic, because in both cases the exception is either propagated outside directly, or added
		        // to an enclosing parent. However we won't do this for continuation tasks, because in that case we internally eat the exception
		        // and therefore we need to make sure the user does later observe it explicitly or see it on the finalizer.

		        if ((Options & (TaskCreationOptions)InternalTaskOptions.ContinuationTask) == 0)
		        {
		            // m_contingentProperties.m_exceptionsHolder *should* already exist after AddException()
		            Contract.Assert(
		                (m_contingentProperties != null) &&
		                (m_contingentProperties.m_exceptionsHolder != null) &&
		                (m_contingentProperties.m_exceptionsHolder.ContainsFaultList),
		                    "Task.ScheduleAndStart(): Expected m_contingentProperties.m_exceptionsHolder to exist " +
		                    "and to have faults recorded.");

		            m_contingentProperties.m_exceptionsHolder.MarkAsHandled(false);
		        }
		        // re-throw the exception wrapped as a TaskSchedulerException.
		        throw tse;
		    }*/
		}

		internal bool ExecuteEntry(bool bPreventDoubleExecution)
		{
		    if (bPreventDoubleExecution || ((Options & (TaskCreationOptions)InternalTaskOptions.SelfReplicating) != 0))
		    {
		        int32 previousState = 0;

		        // Do atomic state transition from queued to invoked. If we observe a task that's already invoked,
		        // we will return false so that TaskScheduler.ExecuteTask can throw an exception back to the custom scheduler.
		        // However we don't want this exception to be throw if the task was already canceled, because it's a
		        // legitimate scenario for custom schedulers to dequeue a task and mark it as canceled (example: throttling scheduler)
		        if (!AtomicStateUpdate(TASK_STATE_DELEGATE_INVOKED,
		                               TASK_STATE_DELEGATE_INVOKED | TASK_STATE_COMPLETED_MASK,
		                               ref previousState) && (previousState & TASK_STATE_CANCELED) == 0)
		        {
		            // This task has already been invoked.  Don't invoke it again.
		            return false;
		        }
		    }
		    else
		    {
		        // Remember that we started running the task delegate.
		        m_stateFlags |= TASK_STATE_DELEGATE_INVOKED;
		    }

		    if (!IsCancellationRequested && !IsCanceled)
		    {
		        ExecuteWithThreadLocal(ref t_currentTask);
		    }
		    else if (!IsCanceled)
		    {
		        int prevState = Interlocked.Exchange(ref m_stateFlags, m_stateFlags | TASK_STATE_CANCELED);
		        if ((prevState & TASK_STATE_CANCELED) == 0)
		        {
		            CancellationCleanupLogic();
		        }
		    }

			Detach_SetDone();
			/*if (mDetachState == .Deatched_Done)
			{
				delete this;
			}*/

		    return true;
		}

		protected virtual void InnerInvoke()
		{
			var action = m_action as Action;
			if (action != null)
			{
			    action();
			    return;
			}
			var actionWithState = m_action as Action<Object>;
			if (actionWithState != null)
			{
			    actionWithState(m_stateObject);
			    return;
			}
		}

		private void Execute()
		{
			InnerInvoke();
		}

		private void ExecuteWithThreadLocal(ref Task currentTaskSlot)
		{
			Execute();
			Finish(true);
		}

		public void Wait()
		{
#if DEBUG
		    bool waitResult =
#endif
		    Wait(Timeout.Infinite, default(CancellationToken));

#if DEBUG
		    Contract.Assert(waitResult, "expected wait to succeed");
#endif
		}

		public bool Wait(int millisecondsTimeout, CancellationToken cancellationToken)
		{
		    // Return immediately if we know that we've completed "clean" -- no exceptions, no cancellations
		    // and if no notification to the debugger is required
		    if (!IsWaitNotificationEnabledOrNotRanToCompletion) // (!DebuggerBitSet && RanToCompletion)
		        return true;

		    // Wait, and then return if we're still not done.
		    if (!InternalWait(millisecondsTimeout, cancellationToken))
		        return false;

		    if (IsWaitNotificationEnabledOrNotRanToCompletion) // avoid a few unnecessary volatile reads if we completed successfully
		    {
		        // Notify the debugger of the wait completion if it's requested such a notification
		        //TODO: NotifyDebuggerOfWaitCompletionIfNecessary();

		        // If cancellation was requested and the task was canceled, throw an 
		        // OperationCanceledException.  This is prioritized ahead of the ThrowIfExceptional
		        // call to bring more determinism to cases where the same token is used to 
		        // cancel the Wait and to cancel the Task.  Otherwise, there's a ---- between
		        // whether the Wait or the Task observes the cancellation request first,
		        // and different exceptions result from the different cases.
		        //TODO: if (IsCanceled) cancellationToken.ThrowIfCancellationRequested();

		        // If an exception occurred, or the task was cancelled, throw an exception.
		        //TODO: ThrowIfExceptional(true);
		    }

		    Contract.Assert((m_stateFlags & TASK_STATE_FAULTED) == 0, "Task.Wait() completing when in Faulted state.");

		    return true;
		}

		/// Internal function that will be called by a new child task to add itself to 
		/// the children list of the parent (this).
		/// 
		/// Since a child task can only be created from the thread executing the action delegate
		/// of this task, reentrancy is neither required nor supported. This should not be called from
		/// anywhere other than the task construction/initialization codepaths.
		internal void AddNewChild()
		{
			ThrowUnimplemented();
			//TODO:

		    /*Contract.Assert(Task.InternalCurrent == this || this.IsSelfReplicatingRoot, "Task.AddNewChild(): Called from an external context");

		    var props = EnsureContingentPropertiesInitialized(needsProtection: true);

		    if (props.m_completionCountdown == 1 && !IsSelfReplicatingRoot)
		    {
		        // A count of 1 indicates so far there was only the parent, and this is the first child task
		        // Single kid => no fuss about who else is accessing the count. Let's save ourselves 100 cycles
		        // We exclude self replicating root tasks from this optimization, because further child creation can take place on 
		        // other cores and with bad enough timing this write may not be visible to them.
		        props.m_completionCountdown++;
		    }
		    else
		    {
		        // otherwise do it safely
		        Interlocked.Increment(ref props.m_completionCountdown);
		    } */
		}

		/*internal ContingentProperties EnsureContingentPropertiesInitialized(bool needsProtection)
		{
		    var props = m_contingentProperties;
		    return props != null ? props : EnsureContingentPropertiesInitializedCore(needsProtection);
		}*/

		internal bool IsSelfReplicatingRoot
		{
		    get
		    {
		        // Return true if self-replicating bit is set and child replica bit is not set
		        return (Options & (TaskCreationOptions)(InternalTaskOptions.SelfReplicating | InternalTaskOptions.ChildReplica))
		            == (TaskCreationOptions)InternalTaskOptions.SelfReplicating;
		    }
		}

		public TaskAwaiter GetAwaiter()
		{
		    return TaskAwaiter(this);
		}

		internal static Task InternalCurrentIfAttached(TaskCreationOptions creationOptions)
		{
		    return (creationOptions & TaskCreationOptions.AttachedToParent) != 0 ? InternalCurrent : null;
		}

		internal void AddCompletionAction(ITaskCompletionAction action)
		{
		    AddCompletionAction(action, false);
		}

		private void AddCompletionAction(ITaskCompletionAction action, bool addBeforeOthers)
		{
		    if (!AddTaskContinuation(action, addBeforeOthers))
		        action.Invoke(this); // run the action directly if we failed to queue the continuation (i.e., the task completed)
		}

		// Support method for AddTaskContinuation that takes care of multi-continuation logic.
		// Returns true if and only if the continuation was successfully queued.
		// THIS METHOD ASSUMES THAT m_continuationObject IS NOT NULL.  That case was taken
		// care of in the calling method, AddTaskContinuation().
		private bool AddTaskContinuationComplex(Object tc, bool addBeforeOthers)
		{
		    //Contract.Requires(tc != null, "Expected non-null tc object in AddTaskContinuationComplex");

		    Object oldValue = m_continuationObject;

		    // Logic for the case where we were previously storing a single continuation
		    if ((oldValue != s_taskCompletionSentinel) && (!(oldValue is List<Object>)))
		    {
		        // Construct a new TaskContinuation list
		        List<Object> newList = new List<Object>();

		        // Add in the old single value
		        newList.Add(oldValue);

		        // Now CAS in the new list
		        var switchedList = Interlocked.CompareExchange(ref m_continuationObject, oldValue, newList);
				if (switchedList == oldValue)
				{

				}

		        // We might be racing against another thread converting the single into
		        // a list, or we might be racing against task completion, so resample "list"
		        // below.
		    }

		    // m_continuationObject is guaranteed at this point to be either a List or
		    // s_taskCompletionSentinel.
		    List<Object> list = m_continuationObject as List<Object>;
		    Contract.Assert((list != null) || (m_continuationObject == s_taskCompletionSentinel),
		        "Expected m_continuationObject to be list or sentinel");

		    // If list is null, it can only mean that s_taskCompletionSentinel has been exchanged
		    // into m_continuationObject.  Thus, the task has completed and we should return false
		    // from this method, as we will not be queuing up the continuation.
		    if (list != null)
		    {
		        using (mMonitor.Enter())
		        {
		            // It is possible for the task to complete right after we snap the copy of
		            // the list.  If so, then fall through and return false without queuing the
		            // continuation.
		            if (m_continuationObject != s_taskCompletionSentinel)
		            {
		                // Before growing the list we remove possible null entries that are the
		                // result from RemoveContinuations()
		                if (list.Count == list.Capacity)
		                {
		                    list.RemoveAll(s_IsTaskContinuationNullPredicate);
		                }

		                if (addBeforeOthers)
		                    list.Insert(0, tc);
		                else
		                    list.Add(tc);

		                return true; // continuation successfully queued, so return true.
		            }
		        }
		    }

		    // We didn't succeed in queuing the continuation, so return false.
		    return false;
		}

		private readonly static Predicate<Object> s_IsTaskContinuationNullPredicate = (new (tc) => (tc == null)) ~ delete _;

		// Record a continuation task or action.
		// Return true if and only if we successfully queued a continuation.
		private bool AddTaskContinuation(Object tc, bool addBeforeOthers)
		{
		    //Contract.Requires(tc != null);

		    // Make sure that, if someone calls ContinueWith() right after waiting for the predecessor to complete,
		    // we don't queue up a continuation.
		    if (IsCompleted) return false;

		    // Try to just jam tc into m_continuationObject
		    if ((m_continuationObject != null) || (Interlocked.CompareExchange(ref m_continuationObject, null, tc) != null))
		    {
		        // If we get here, it means that we failed to CAS tc into m_continuationObject.
		        // Therefore, we must go the more complicated route.
		        return AddTaskContinuationComplex(tc, addBeforeOthers);
		    }
		    else return true;
		}
	}

	internal interface ITaskCompletionAction
	{
	    void Invoke(Task completingTask);
	}

	public enum TaskCreationOptions
	{
	    /// Specifies that the default behavior should be used.
	    None = 0x0,

	    /// A hint to a <see cref="System.Threading.Tasks.TaskScheduler">TaskScheduler</see> to schedule a
	    /// task in as fair a manner as possible, meaning that tasks scheduled sooner will be more likely to
	    /// be run sooner, and tasks scheduled later will be more likely to be run later.
	    PreferFairness = 0x01,

	    /// Specifies that a task will be a long-running, course-grained operation. It provides a hint to the
	    /// <see cref="System.Threading.Tasks.TaskScheduler">TaskScheduler</see> that oversubscription may be
	    /// warranted. 
	    LongRunning = 0x02,

	    /// Specifies that a task is attached to a parent in the task hierarchy.
	    AttachedToParent = 0x04,

	    /// Specifies that an InvalidOperationException will be thrown if an attempt is made to attach a child task to the created task.
	    DenyChildAttach = 0x08,

	    /// Prevents the ambient scheduler from being seen as the current scheduler in the created task.  This means that operations
	    /// like StartNew or ContinueWith that are performed in the created task will see TaskScheduler.Default as the current scheduler.
	    HideScheduler = 0x10,

	    // 0x20 is already being used in TaskContinuationOptions

	    /// Forces continuations added to the current task to be executed asynchronously.
	    /// This option has precedence over TaskContinuationOptions.ExecuteSynchronously
	    RunContinuationsAsynchronously = 0x40
	}

	internal enum InternalTaskOptions
	{
	    /// Specifies "No internal task options"
	    None,

	    /// Used to filter out internal vs. public task creation options.
	    InternalOptionsMask = 0x0000FF00,

	    ChildReplica = 0x0100,
	    ContinuationTask = 0x0200,
	    PromiseTask = 0x0400,
	    SelfReplicating = 0x0800,

	    /// Store the presence of TaskContinuationOptions.LazyCancellation, since it does not directly
	    /// translate into any TaskCreationOptions.
	    LazyCancellation = 0x1000,

	    /// Specifies that the task will be queued by the runtime before handing it over to the user. 
	    /// This flag will be used to skip the cancellationtoken registration step, which is only meant for unstarted tasks.
	    QueuedByRuntime = 0x2000,

	    /// Denotes that Dispose should be a complete nop for a Task.  Used when constructing tasks that are meant to be cached/reused.
	    DoNotDispose = 0x4000
	}
}
