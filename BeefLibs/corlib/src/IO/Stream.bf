// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics;
using System.Diagnostics.Contracts;
using System.Threading;
using System.Threading.Tasks;

namespace System.IO
{
	abstract class Stream
	{
		private ReadWriteTask _activeReadWriteTask;
		private SemaphoreSlim _asyncActiveSemaphore;

		public enum SeekKind
		{
			Absolute,
			Relative,
			FromEnd
		}

		public abstract int64 Position
		{
			get;
			set;
		}

		public abstract int64 Length
		{
			get;
		}

		public abstract bool CanRead
		{
			get;
		}

		public abstract bool CanWrite
		{
			get;
		}

		public bool IsEmpty
		{
			get
			{
				return Length == 0;
			}
		}

		public virtual Result<void> Seek(int64 pos, SeekKind seekKind = .Absolute)
		{
			if (seekKind == .Absolute)
				Position = pos;
			else
				Runtime.FatalError();
			return .Ok;
		}

		public abstract Result<int> TryRead(Span<uint8> data);
		public abstract Result<int> TryWrite(Span<uint8> data);
		public abstract void Close();

		public Result<T> Read<T>() where T : struct
		{
			T val = ?;
			int size = Try!(TryRead(.((uint8*)&val, sizeof(T))));
			if (size != sizeof(T))
				return .Err;
			return .Ok(val);
		}

		public Result<void> Write<T>(T val) where T : struct
		{
			var val;
			int size = Try!(TryWrite(.((uint8*)&val, sizeof(T))));
			if (size != sizeof(T))
				return .Err;
			return .Ok;
		}

		public Result<void> Write<T, T2>(T val) where T : Span<T2>
		{
			int trySize = val.Length * sizeof(T2);
			int size = Try!(TryWrite(.((uint8*)val.Ptr, trySize)));
			if (size != trySize)
				return .Err;
			return .Ok;
		}

		public Result<void> WriteStrSized32(StringView val)
		{
			int trySize = val.Length;
			Try!(Write((int32)trySize));
			int size = Try!(TryWrite(.((uint8*)val.Ptr, trySize)));
			if (size != trySize)
				return .Err;
			return .Ok;
		}

		public Result<void> WriteStrUnsized(StringView val)
		{
			int trySize = val.Length;
			int size = Try!(TryWrite(.((uint8*)val.Ptr, trySize)));
			if (size != trySize)
				return .Err;
			return .Ok;
		}

		public Result<void> Write(String val)
		{
			int trySize = val.Length;
			int size = Try!(TryWrite(Span<uint8>((uint8*)val.Ptr, trySize)));
			if (size != trySize)
				return .Err;
			return .Ok;
		}

		public virtual void Flush()
		{
		}

		public void Align(int alignSize)
		{
			int64 pos = Length;
			int64 alignAdd = alignSize - (pos % alignSize);
			if (alignAdd == alignSize)
				return;

			int64 emptyData = 0;
			while (alignAdd > 0)
			{
				int64 writeSize = Math.Min(alignAdd, sizeof(decltype(emptyData)));
				TryWrite(.((uint8*)&emptyData, (int)writeSize));
				alignAdd -= writeSize;
			}
		}

		public virtual Result<int> CopyTo(Stream destStream)
		{
			uint8[4096] buffer;
			int totalBytes = 0;

			while (true)
			{
				int readBytes = Try!(TryRead(.(&buffer, sizeof(decltype(buffer)))));
				Try!(destStream.TryWrite(.(&buffer, readBytes)));
				if (readBytes <= 0)
					break;
				totalBytes += readBytes;
			}

			return .Ok(totalBytes);
		}

		public virtual IAsyncResult BeginRead(uint8[] buffer, int offset, int count, AsyncCallback callback, Object state)
		{
		    //Contract.Ensures(Contract.Result<IAsyncResult>() != null);
		    return BeginReadInternal(buffer, offset, count, callback, state, false);
		}

		// Task used by BeginRead / BeginWrite to do Read / Write asynchronously.
		// A single instance of this task serves four purposes:
		// 1. The work item scheduled to run the Read / Write operation
		// 2. The state holding the arguments to be passed to Read / Write
		// 3. The IAsyncResult returned from BeginRead / BeginWrite
		// 4. The completion action that runs to invoke the user-provided callback.
		// This last item is a bit tricky.  Before the AsyncCallback is invoked, the
		// IAsyncResult must have completed, so we can't just invoke the handler
		// from within the task, since it is the IAsyncResult, and thus it's not
		// yet completed.  Instead, we use AddCompletionAction to install this
		// task as its own completion handler.  That saves the need to allocate
		// a separate completion handler, it guarantees that the task will
		// have completed by the time the handler is invoked, and it allows
		// the handler to be invoked synchronously upon the completion of the
		// task.  This all enables BeginRead / BeginWrite to be implemented
		// with a single allocation.
		private sealed class ReadWriteTask : Task<int>, ITaskCompletionAction
		{
		    readonly bool _isRead;
		    Stream _stream;
		    uint8 [] _buffer;
		    int _offset;
		    int _count;
		    private AsyncCallback _callback;
		    //private ExecutionContext _context;

		    void ClearBeginState() // Used to allow the args to Read/Write to be made available for GC
		    {
		        _stream = null;
		        _buffer = null;
		    }

		    public this(
		        bool isRead,
		        Func<Object, int> func, Object state,
		        Stream stream, uint8[] buffer, int offset, int count, AsyncCallback callback) :
		        	base(func, state, CancellationToken.None, TaskCreationOptions.DenyChildAttach)
		    {
		        Contract.Requires(func != null);
		        Contract.Requires(stream != null);
		        Contract.Requires(buffer != null);
		        Contract.EndContractBlock();

		        //StackCrawlMark stackMark = StackCrawlMark.LookForMyCaller;

		        // Store the arguments
		        _isRead = isRead;
		        _stream = stream;
		        _buffer = buffer;
		        _offset = offset;
		        _count = count;

		        // If a callback was provided, we need to:
		        // - Store the user-provided handler
		        // - Capture an ExecutionContext under which to invoke the handler
		        // - Add this task as its own completion handler so that the Invoke method
		        //   will run the callback when this task completes.
		        if (callback != null)
		        {
		            _callback = callback;
		            /*_context = ExecutionContext.Capture(ref stackMark, 
		                ExecutionContext.CaptureOptions.OptimizeDefaultCase | ExecutionContext.CaptureOptions.IgnoreSyncCtx);*/
		            base.AddCompletionAction(this);
		        }
		    }

		    private static void InvokeAsyncCallback(Object completedTask)
		    {
		        var rwc = (ReadWriteTask)completedTask;
		        var callback = rwc._callback;
		        rwc._callback = null;
		        callback(rwc);
		    }
		    
		    void ITaskCompletionAction.Invoke(Task completingTask)
		    {
	            var callback = _callback;
	            _callback = null;
	            callback(completingTask);
		    }
		}

		IAsyncResult BeginReadInternal(uint8[] buffer, int offset, int count, AsyncCallback callback, Object state, bool serializeAsynchronously)
		{
			// To avoid a race with a stream's position pointer & generating ---- 
			// conditions with internal buffer indexes in our own streams that 
			// don't natively support async IO operations when there are multiple 
			// async requests outstanding, we will block the application's main
			// thread if it does a second IO request until the first one completes.

			//TODO: Implement
			/*var semaphore = EnsureAsyncActiveSemaphoreInitialized();
			Task semaphoreTask = null;
			if (serializeAsynchronously)
			{
			    semaphoreTask = semaphore.WaitAsync();
			}
			else
			{
			    semaphore.Wait();
			}*/

			// Create the task to asynchronously do a Read.  This task serves both
			// as the asynchronous work item and as the IAsyncResult returned to the user.
			var asyncResult = new ReadWriteTask(true /*isRead*/, new (obj) =>
			{
			    // The ReadWriteTask stores all of the parameters to pass to Read.
			    // As we're currently inside of it, we can get the current task
			    // and grab the parameters from it.
			    var thisTask = Task.[Friend]InternalCurrent as ReadWriteTask;
			    Contract.Assert(thisTask != null, "Inside ReadWriteTask, InternalCurrent should be the ReadWriteTask");

			    // Do the Read and return the number of bytes read
			    int bytesRead = thisTask.[Friend]_stream.TryRead(.(thisTask.[Friend]_buffer, thisTask.[Friend]_offset, thisTask.[Friend]_count));
			    thisTask.[Friend]ClearBeginState(); // just to help alleviate some memory pressure
			    return bytesRead;
			}, state, this, buffer, offset, count, callback);

			// Schedule it

			//TODO:
			/*if (semaphoreTask != null)
			    RunReadWriteTaskWhenReady(semaphoreTask, asyncResult);
			else
			    RunReadWriteTask(asyncResult);*/


			return asyncResult; // return it
		}

		public virtual Result<int> EndRead(IAsyncResult asyncResult)
		{
		    if (asyncResult == null)
		        Runtime.FatalError();
		    //Contract.Ensures(Contract.Result<int>() >= 0);
		    Contract.EndContractBlock();

		    var readTask = _activeReadWriteTask;

		    if (readTask == null)
		    {
		        Runtime.FatalError();
		    }
		    else if (readTask != asyncResult)
		    {
		        Runtime.FatalError();
		    }
		    else if (!readTask.[Friend]_isRead)
		    {
		        Runtime.FatalError();
		    }
		    
		    
		    int result = readTask.GetAwaiter().GetResult(); // block until completion, then get result / propagate any exception
		    
		    _activeReadWriteTask = null;
		    Contract.Assert(_asyncActiveSemaphore != null, "Must have been initialized in order to get here.");
		    _asyncActiveSemaphore.Release();
			return result;
		}
	}
}
