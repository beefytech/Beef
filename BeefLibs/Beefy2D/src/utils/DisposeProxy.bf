using System;
using System.Collections;
using System.Text;

namespace Beefy.utils
{
    public delegate void DisposeProxyDelegate();

    public class DisposeProxy : IDisposable
    {
        public DisposeProxyDelegate mDisposeProxyDelegate;

        public this(DisposeProxyDelegate theDelegate = null)
        {
            mDisposeProxyDelegate = theDelegate;
        }

		public ~this()
		{
			delete mDisposeProxyDelegate;
		}

        public void Dispose()
        {
            mDisposeProxyDelegate();
        }
    }
}
