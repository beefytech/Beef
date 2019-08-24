using System;
using IDE.Util;
using System.IO;

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

		Result<void> ExtractTo(ZipFile zipFile, StringView destDir, StringView subStr)
		{
			String fileName = scope .();
			String destPath = scope .();

			for (int i < zipFile.GetNumFiles())
			{
				ZipFile.Entry entry = scope .();
				if (zipFile.SelectEntry(i, entry) case .Err)
					continue;

				fileName.Clear();
				entry.GetFileName(fileName);

				if (!fileName.StartsWith(subStr))
					continue;

				destPath.Clear();
				destPath.Append(destDir);
				destPath.Append('/');
				destPath.Append(fileName);

				if (entry.IsDirectory)
				{
					if (Directory.CreateDirectory(destPath) case .Err)
						return .Err;
				}
				else
				{
					if (entry.ExtractToFile(destPath) case .Err)
						return .Err;
				}
			}

			return .Ok;
		}

		void Run()
		{
			ZipFile zipFile = scope .();
			zipFile.Open(@"c:\\temp\\build_1827.zip");
			ExtractTo(zipFile, @"c:\temp\unzip", .());

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
