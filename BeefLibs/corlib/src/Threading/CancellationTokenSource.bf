// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Threading
{
	class CancellationTokenSource
	{
		private const int CANNOT_BE_CANCELED = 0;
		private const int NOT_CANCELED = 1;
		private const int NOTIFYING = 2;
		private const int NOTIFYINGCOMPLETE = 3;

		private volatile int m_state;

		public bool IsCancellationRequested
		{
		    get { return m_state >= NOTIFYING; }
		}

		bool CanBeCanceled
		{
		    get { return m_state != CANNOT_BE_CANCELED; }
		}
	}
}
