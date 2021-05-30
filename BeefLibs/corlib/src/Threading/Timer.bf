using System.Diagnostics;

namespace System.Threading
{
	public delegate void TimerCallback(Object state);

	public sealed class ConsoleTimer : IDisposable
	{
		private const uint32 MAX_SUPPORTED_TIMEOUT = (uint)0xfffffffe;
		private int64 _interval;
		private TimerCallback _callback = null;
		private Thread _thread = null;
		private WaitEvent _cancelEvent = new WaitEvent(false);
		private WaitEvent _threadExited = new WaitEvent(false);
		private Monitor _lock = new Monitor();

		private this() { } // Hide parameterless .ctor

		public this(TimerCallback callback) : this(callback, -1) { }

		public this(TimerCallback callback, int interval)
		{
			if (interval < -1)
				Runtime.FatalError("Fatal error: Invalid argument `interval`, value must be equal to, or greater then -1.");

			TimerSetup(callback, (int64)interval);
		}

		public this(TimerCallback callback, int64 interval)
		{
			if (interval < -1 )
				Runtime.FatalError("Fatal error: Invalid argument `interval`, value must be equal to, or greater then -1.");

			if (interval > MAX_SUPPORTED_TIMEOUT)
				Runtime.FatalError(scope $"Fatal error: Invalid argument `interval`, value must be lower then {MAX_SUPPORTED_TIMEOUT}.");

			TimerSetup(callback, interval);
		}

		public this(TimerCallback callback, TimeSpan interval) : this(callback, (int64)interval.TotalMilliseconds) { }

		public this(TimerCallback callback, uint32 interval)
		{
			if (interval > MAX_SUPPORTED_TIMEOUT)
				Runtime.FatalError(scope $"Fatal error: Invalid argument `interval`, value must be lower then {MAX_SUPPORTED_TIMEOUT}.");

			TimerSetup(callback, (int64)interval);
		}

		~this()
		{
			if (_thread != null)
				Dispose();
		}
		
		public void Dispose()
		{
			using (_lock.Enter())
			{
				_cancelEvent.Set(true);
			}

			_threadExited.WaitFor();
			_thread.Join();
			delete _thread;
			_thread = null;
		}

		private void TimerSetup(TimerCallback callback, int64 interval)
		{
			_callback = callback;
			_interval = interval;
			_thread = new Thread(new => ThreadExecute);
			_thread.Start(false);
		}
		
		public void Change(int interval)
		{
			if (interval < -1)
				Runtime.FatalError("Fatal error: Invalid argument `interval`, value must be equal to, or greater then -1.");

			Change((int64)interval);
		}

		public void Change(int64 interval)
		{
			if (interval < -1 )
				Runtime.FatalError("Fatal error: Invalid argument `interval`, value must be equal to, or greater then -1.");

			if (interval > MAX_SUPPORTED_TIMEOUT)
				Runtime.FatalError(scope $"Fatal error: Invalid argument `interval`, value must be lower then {MAX_SUPPORTED_TIMEOUT}.");
			
			using (_lock.Enter())
			{
				_cancelEvent.Set(true);
				_interval = interval;
			}

			_threadExited.WaitFor();
			_cancelEvent.Reset();
			_thread.Start(false);
		}

		public void Change(TimeSpan interval) =>
			Change((int64)interval.TotalMilliseconds);

		public void Change(uint32 interval)
		{
			if (interval > MAX_SUPPORTED_TIMEOUT)
				Runtime.FatalError(scope $"Fatal error: Invalid argument `interval`, value must be lower then {MAX_SUPPORTED_TIMEOUT}.");
			
			Change((int64)interval);
		}

		private void ThreadExecute()
		{
			using (_lock.Enter())
			{
				_threadExited.Reset();
			}

			Stopwatch sw = scope Stopwatch();
			int64 lastProcTime = 0;
			int64 waitInterval;

			while (_thread.ThreadState == .Running) {
				if (_cancelEvent.WaitFor(0))
					break; // Terminate thread when _cancelEvent is set

				using (_lock.Enter())
				{
					waitInterval = _interval - lastProcTime;
				}

				if (waitInterval < 0)
					waitInterval = 0;

				if (_cancelEvent.WaitFor(waitInterval))
					break; // Terminate thread when _cancelEvent is set

				sw.Restart();
				_callback(this);
				sw.Stop();
				lastProcTime = sw.ElapsedMilliseconds;
			}

			using (_lock.Enter())
			{
				_threadExited.Set(true);
			}
		}
	}
}
