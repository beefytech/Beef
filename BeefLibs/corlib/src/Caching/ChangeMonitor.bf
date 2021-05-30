// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Threading;

namespace System.Caching
{
	public delegate void OnChangedCallback(Object state);

	public abstract class ChangeMonitor : IDisposable
	{
		private const int INITIALIZED = 0x01;  // initialization complete
		private const int CHANGED = 0x02;      // dependency changed
		private const int INVOKED = 0x04;      // OnChangedCallback has been invoked
		private const int DISPOSED = 0x08;     // Dispose(true) called, or about to be called
		private readonly static Object NOT_SET = new Object();

		private SafeBitVector32 _flags;
		private OnChangedCallback _onChangedCallback;
		private Object _onChangedState = NOT_SET;

		/// The helper routines (OnChangedHelper and DisposeHelper) are used to prevent an infinite loop, where Dispose calls OnChanged and OnChanged calls Dispose.
		private void DisposeHelper()
		{
			// if not initialized, return without doing anything.
			if (_flags[INITIALIZED] && _flags.ChangeValue(DISPOSED, true))
				Dispose(true);
		}

		/// The helper routines (OnChangedHelper and DisposeHelper) are used to prevent an infinite loop, where Dispose calls OnChanged and OnChanged calls Dispose.
		private void OnChangedHelper(Object state)
		{
			_flags[CHANGED] = true;
			// the callback is only invoked once, after NotifyOnChanged is called, so remember "state" on the first call and use it when invoking the callback
			Interlocked.CompareExchange(ref _onChangedState, state, NOT_SET);
			OnChangedCallback onChangedCallback = _onChangedCallback;

			if (onChangedCallback != null && _flags.ChangeValue(INVOKED, true))// only invoke the callback once
				onChangedCallback(_onChangedState);
		}

		/// Derived classes must implement this.  When "disposing" is true, all managed and unmanaged resources are disposed and any references to this
		/// object are released so that the ChangeMonitor can be garbage collected.  It is guaranteed that ChangeMonitor.Dispose() will only invoke Dispose(bool disposing) once.
		protected abstract void Dispose(bool disposing);

		/// Derived classes must call InitializationComplete
		protected void InitializationComplete()
		{
			_flags[INITIALIZED] = true;
			// If the dependency has already changed, or someone tried to dispose us, then call Dispose now.
			Runtime.Assert(_flags[INITIALIZED], "It is critical that INITIALIZED is set before CHANGED is checked below");

			if (_flags[CHANGED])
				Dispose();
		}

		/// Derived classes call OnChanged when the dependency changes.  Optionally, they may pass state which will be passed to the OnChangedCallback.
		/// The OnChangedCallback is only invoked once, and only after NotifyOnChanged is called by the cache implementer.
		/// OnChanged is also invoked when the instance is disposed, but only has an affect if the callback has not already been invoked.
		protected void OnChanged(Object state)
		{
			OnChangedHelper(state);
			// OnChanged will also invoke Dispose, but only after initialization is complete
			Runtime.Assert(_flags[CHANGED], "It is critical that CHANGED is set before INITIALIZED is checked below.");

			if (_flags[INITIALIZED])
				DisposeHelper();
		}

		/// set to true when the dependency changes, specifically, when OnChanged is called.
		public bool HasChanged { get { return _flags[CHANGED]; } }

		/// set to true when this instance is disposed, specifically, after Dispose(bool disposing) is called by Dispose().
		public bool IsDisposed { get { return _flags[DISPOSED]; } }

		/// a unique ID representing this ChangeMonitor, typically consisting of the dependency names and last-modified times.
		public abstract String UniqueId { get; }

		/// Dispose must be called to release the ChangeMonitor.  In order to prevent derived classes from overriding Dispose, it is not an explicit interface implementation.
		///
		/// Before cache insertion, if the user decides not to do a cache insert, they must call this to dispose the dependency; otherwise, the ChangeMonitor will
		/// be referenced and unable to be garbage collected until the dependency changes.
		///
		/// After cache insertion, the cache implementer must call this when the cache entry is removed, for whatever reason.  Even if an exception is thrown during insert.
		///
		/// After cache insertion, the user should not call Dispose.
		/// However, since there's no way to prevent this, doing so will invoke the OnChanged event handler, if it hasn't already been invoked, and the cache entry will be notified as if the 
		/// dependency has changed.
		///
		/// Dispose() will only invoke the Dispose(bool disposing) method of derived classes once, the first time it is called.
		/// Subsequent calls to Dispose() perform no operation.  After Dispose is called, the IsDisposed property will be true.
		public void Dispose()
		{
			OnChangedHelper(null);
			// If not initialized, throw, so the derived class understands that it must call InitializeComplete before Dispose.
			Runtime.Assert(_flags[CHANGED], "It is critical that CHANGED is set before INITIALIZED is checked below.");

			if (!_flags[INITIALIZED])
				Runtime.FatalError("Fatal error: Initialization has not completed yet.  The InitializationComplete method must be invoked before Dispose is invoked.");

			DisposeHelper();
		}

		/// Cache implementers must call this to be notified of any dependency changes.
		/// NotifyOnChanged can only be invoked once, and will throw InvalidOperationException on subsequent calls.  The OnChangedCallback is guaranteed to be called exactly once.
		/// It will be called when the dependency changes, or if it has already changed, it will be called immediately (on the same thread??).
		public void NotifyOnChanged(OnChangedCallback onChangedCallback)
		{
			Runtime.Assert(onChangedCallback != null);

			if (Interlocked.CompareExchange(ref _onChangedCallback, onChangedCallback, null) != null)
				Runtime.FatalError("Fatal error: The method has already been invoked, and can only be invoked once.");

			// if it already changed, raise the event now.
			if (_flags[CHANGED])
				OnChanged(null);
		}
	}
}
