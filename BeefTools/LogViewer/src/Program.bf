using System;

namespace LogViewer
{
	class Program
	{
		static int32 Main(String[] args)
		{
			/*String commandLine = scope String();
			commandLine.JoinInto(" ", args);*/						

			LVApp app = new .();
			app.ParseCommandLine(args);
			app.Init();
			app.Run();
			app.Shutdown();
			delete app;

			return 0;
		}
	}
}
