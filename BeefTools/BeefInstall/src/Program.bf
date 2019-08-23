using System;

namespace BeefInstall
{
	class Program
	{
		bool HandleCommandLineParam(String key, String value)
		{
			return false;
		}

		void UnhandledCommandLine(String key, String value)
		{

		}

		void ParseCommandLine(String[] args)
		{
			for (var str in args)
			{
				int eqPos = str.IndexOf('=');
				if (eqPos == -1)
				{
					if (!HandleCommandLineParam(str, null))
						UnhandledCommandLine(str, null);
				}	
				else
				{
					var cmd = scope String(str, 0, eqPos);
					var param = scope String(str, eqPos + 1);
					if (!HandleCommandLineParam(cmd, param))
						UnhandledCommandLine(cmd, param);
				}
			}
		}

		void Run()
		{
			CabFile cabFile = scope .();
			cabFile.Init();
			cabFile.Copy();
		}

		static int Main(String[] args)
		{
			Program pg = new Program();
			pg.ParseCommandLine(args);
			pg.Run();
			delete pg;
			return 0;
		}
	}
}
