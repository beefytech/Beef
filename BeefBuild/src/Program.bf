using System;
using IDE.Util;
using System.Diagnostics;

namespace BeefBuild
{
	class Program
	{
		public static int32 Main(String[] args)		
		{
			for (let arg in args)
			{
				if (arg != "-help")
					continue;
				Console.WriteLine(
					"""
					BeefBuild [args]
					  If no arguments are specified, a build will occur using current working directory as the workspace.
					    -config=<config>        Sets the config (defaults to Debug)
					    -new                    Creates a new workspace and project
					    -platform=<platform>    Sets the platform (defaults to system platform)
					    -test=<path>            Executes test script
					    -verbosity=<verbosity>  Set verbosity level to: quiet/minimal/normal/detailed/diagnostic
					    -workspace=<path>       Sets workspace path (defaults to current working directory)
					""");
				return 0;
			}

			//TestZip2();
			String commandLine = scope String();
			commandLine.Join(" ", params args);

			BuildApp mApp = new BuildApp();	
			mApp.ParseCommandLine(commandLine);
			if (mApp.mFailed)
			{
				Console.Error.WriteLine("  Run with \"-help\" for a list of command-line arguments");
			}
			else
			{
				mApp.Init();
				mApp.Run();
			}
			mApp.Shutdown();
			int32 result = mApp.mFailed ? 1 : 0;

			delete mApp;

			return result;
		}
	}
}
