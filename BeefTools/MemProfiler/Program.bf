using System;

namespace MemProfiler
{
	class Program
	{
		public static int32 Main(String[] args)
		{
			String commandLine = scope String();
			commandLine.JoinInto(" ", args);						

			MPApp mApp = new MPApp();
			mApp.ParseCommandLine(commandLine);
			mApp.Init();
			mApp.Run();
			mApp.Shutdown();
			delete mApp;

			return 0;
		}
	}
}
