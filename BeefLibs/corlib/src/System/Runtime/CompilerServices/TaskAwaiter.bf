// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading.Tasks;
using System.Diagnostics.Contracts;
using System.Threading;

namespace System.Runtime.CompilerServices
{
	public struct TaskAwaiter
	{
		private readonly Task m_task;

		internal this(Task task)
		{
		    //Contract.Requires(task != null, "Constructing an awaiter requires a task to await.");
		    m_task = task;
		}

		public bool IsCompleted 
		{
		    get { return m_task.IsCompleted; }
		}

		public void GetResult()
		{
		    ValidateEnd(m_task);
		}

		internal static void ValidateEnd(Task task)
		{
		    // Fast checks that can be inlined.
		    if (task.IsWaitNotificationEnabledOrNotRanToCompletion)
		    {
		        // If either the end await bit is set or we're not completed successfully,
		        // fall back to the slower path.
		        HandleNonSuccessAndDebuggerNotification(task);
		    }
		}

		private static void HandleNonSuccessAndDebuggerNotification(Task task)
		{
		    // NOTE: The JIT refuses to inline ValidateEnd when it contains the contents
		    // of HandleNonSuccessAndDebuggerNotification, hence the separation.

		    // Synchronously wait for the task to complete.  When used by the compiler,
		    // the task will already be complete.  This code exists only for direct GetResult use,
		    // for cases where the same exception propagation semantics used by "await" are desired,
		    // but where for one reason or another synchronous rather than asynchronous waiting is needed.
		    if (!task.IsCompleted)
		    {
		        bool taskCompleted = task.InternalWait(Timeout.Infinite, default(CancellationToken));
		        Contract.Assert(taskCompleted, "With an infinite timeout, the task should have always completed.");
		    }

		    // Now that we're done, alert the debugger if so requested
		    //TODO: What?
            //task.NotifyDebuggerOfWaitCompletionIfNecessary();

		    // And throw an exception if the task is faulted or canceled.
		    if (!task.IsRanToCompletion)
            {    
				ThrowUnimplemented();
            	//ThrowForNonSuccess(task);
			}
		}
	}

	public struct TaskAwaiter<TResult>
	{
		private readonly Task<TResult> m_task;

		internal this(Task<TResult> task)
		{
		    //Contract.Requires(task != null, "Constructing an awaiter requires a task to await.");
		    m_task = task;
		}

		public bool IsCompleted 
		{
		    get { return m_task.IsCompleted; }
		}

		public TResult GetResult()
		{
		    TaskAwaiter.ValidateEnd(m_task);
		    return m_task.ResultOnSuccess;
		}
	}
}
