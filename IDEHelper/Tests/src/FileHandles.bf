#pragma warning disable 168

using System;
using System.Diagnostics;
using System.IO;

namespace Tests
{
	class FileHandles
	{
#if BF_PLATFORM_LINUX
		// A file the runtime opens is closed in a process it starts, as a Windows handle is not
		// inherited: opened without O_CLOEXEC, every child (a build, a shell) held each file the
		// parent had open.
		[Test]
		public static void TestOpenFilesAreNotInherited()
		{
			String path = "FileHandles_inherit_test.txt";
			FileStream file = scope .();
			Test.Assert(file.Create(path) case .Ok);

			ProcessStartInfo info = scope .();
			info.SetFileName("/bin/sh");
			info.SetArguments("-c \"ls -l /proc/self/fd\"");
			info.UseShellExecute = false;
			info.RedirectStandardOutput = true;
			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Ok);
			FileStream output = scope .();
			Test.Assert(process.AttachStandardOutput(output) case .Ok);
			String listing = scope .();
			StreamReader reader = scope .(output);
			Test.Assert(reader.ReadToEnd(listing) case .Ok);
			process.WaitFor();

			file.Close();
			File.Delete(path).IgnoreError();
			Test.Assert(listing.Contains("/proc/")); // it listed its descriptors
			Test.Assert(!listing.Contains("FileHandles_inherit_test"));
		}
#endif
	}
}
