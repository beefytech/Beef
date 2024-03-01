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
					    -generate               Generates startup code for an empty project
					    -new                    Creates a new workspace and project
					    -platform=<platform>    Sets the platform (defaults to system platform)
					    -run                    Compile and run the startup project in the workspace
					    -test                   Run tests in the workspace
					    -verbosity=<verbosity>  Set verbosity level to: quiet/minimal/normal/detailed/diagnostic
					    -version                Get version
					    -workspace=<path>       Sets workspace path (defaults to current working directory)
					""");
				return 0;
			}

			//TestZip2();
			String commandLine = scope String();
			commandLine.Join(" ", params args);

			BuildApp mApp = new BuildApp();	
			mApp.ParseCommandLine(commandLine);
			if (mApp.mVerb == .GetVersion)
			{
				Console.WriteLine("BeefBuild {}", IDE.IDEApp.cVersion);
			}
			else
			{
				if (mApp.mFailed)
				{
					Console.Error.WriteLine("  Run with \"-help\" for a list of command-line arguments");
				}
				else
				{
					mApp.Init();
					mApp.Run();
				}
			}
			mApp.Shutdown();
			int32 result = mApp.mFailed ? 1 : 0;
			if (mApp.mTargetExitCode != null)
				result = (int32)mApp.mTargetExitCode.Value;

			delete mApp;

			return result;
		}
	}
}
