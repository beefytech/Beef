//
using System;
using System.Threading;

namespace BeefPerf
{
	class Program
	{
		public static int32 Main(String[] args)
		{
			BPApp mApp = new BPApp();
			mApp.ParseCommandLine(args);
			if (!mApp.mShuttingDown)
			{
				mApp.Init();
				mApp.Run();
				mApp.Shutdown();
			}
			bool failed = mApp.mFailed;
			delete mApp;
			return failed ? 1 : 0;
		}
	}
}
