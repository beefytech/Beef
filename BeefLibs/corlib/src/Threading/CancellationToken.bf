// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Threading
{
	struct CancellationToken
	{
		private CancellationTokenSource m_source;

		public static CancellationToken None
		{
		    get { return default(CancellationToken); }
		}

		public bool IsCancellationRequested 
		{
		    get
		    {
		        return m_source != null && m_source.IsCancellationRequested;
		    }
		}

		public bool CanBeCanceled
		{
		    get
		    {
		        return m_source != null && m_source.[Friend]CanBeCanceled;
		    }
		}
	}
}
