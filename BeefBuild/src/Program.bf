using System;
using IDE.Util;
using System.Diagnostics;

namespace BeefBuild
{
	class Program
	{
		public virtual BuildApp CreateApp()
		{
			return new BuildApp();
		}

		public int32 DoMain(String[] args)
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
					    -project=<name>         Constrain build to a specific project within workspace
					    -define=<name>          Add workspace preprocessor define
					    -args                   Arguments to pass to the compiled program (must come after -run, all following args are passed through)
					""");
				return 0;
			}

			//TestZip2();
			String commandLine = scope String();
			commandLine.Join(" ", args);

			BuildApp app = CreateApp();
			app.ParseCommandLine(commandLine);
			if (app.mVerb == .GetVersion)
			{
				Console.WriteLine("BeefBuild {}", IDE.IDEApp.cVersion);
			}
			else
			{
				if (app.mFailed)
				{
					Console.Error.WriteLine("  Run with \"-help\" for a list of command-line arguments");
				}
				else
				{
					app.Init();
					app.Run();
				}
			}
			app.Shutdown();
			int32 result = app.mFailed ? 1 : 0;
			if (app.mTargetExitCode != null)
				result = (int32)app.mTargetExitCode.Value;

			delete app;

			return result;
		}

		public static int32 Main(String[] args)		
		{
			Program pg = scope .();
			return pg.DoMain(args);
		}
	}
}
