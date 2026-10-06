using System.IO;
using System.Collections;

namespace System.Diagnostics
{
	class SpawnedProcess
	{
		public enum KillFlags
		{
			None = 0,
			KillChildren = 1
		}

		Platform.BfpSpawn* mSpawn;
		int mExitCode = 0;
		bool mIsDone;

		public int ExitCode
		{
			get
			{
				if (!mIsDone)
					WaitFor(-1);
				return mExitCode;
			}
		}

		public bool HasExited
		{
			get
			{
				if (!mIsDone)
					WaitFor(0);
				return mIsDone;
			}
		}

		public int ProcessId
		{
			get
			{
				if (mSpawn == null)
					return -1;
				return Platform.BfpSpawn_GetProcessId(mSpawn);
			}
		}

		public this()
		{
			mSpawn = null;
		}

		public ~this()
		{
			Close();
		}

		public Result<void> Start(ProcessStartInfo startInfo)
		{
			String fileName = startInfo.[Friend]mFileName;

			Platform.BfpSpawnFlags spawnFlags = .None;
			if (startInfo.ErrorDialog)
				spawnFlags |= .ErrorDialog;
			if (startInfo.UseShellExecute)
			{
                spawnFlags |= .UseShellExecute;
				if (!startInfo.[Friend]mVerb.IsEmpty)
					fileName = scope:: String(fileName, "|", startInfo.[Friend]mVerb);
			}
			if (startInfo.CreateNoWindow)
				spawnFlags |= .NoWindow;
			if (!startInfo.ActivateWindow)
				spawnFlags |= .NoActivateWindow;
			if (startInfo.RedirectStandardInput)
				spawnFlags |= .RedirectStdInput;
			if (startInfo.RedirectStandardOutput)
				spawnFlags |= .RedirectStdOutput;
			if (startInfo.RedirectStandardError)
				spawnFlags |= .RedirectStdError;

			//startInfo.mEnvironmentVariables

			List<char8> env = scope List<char8>();
			if (startInfo.mEnvironmentVariables != null)
				Environment.EncodeEnvironmentVariables(startInfo.mEnvironmentVariables, env);
			Span<char8> envSpan = env;

			Platform.BfpSpawnResult result = .Ok;
			mSpawn = Platform.BfpSpawn_Create(fileName, startInfo.[Friend]mArguments, startInfo.[Friend]mDirectory, envSpan.Ptr, spawnFlags, &result);

			if ((mSpawn == null) || (result != .Ok))
				return .Err;
			return .Ok;
		}

		public Result<void> AttachStandardInput(IFileStream stream)		
		{
			if (mSpawn == null)
				return .Err;
			Platform.BfpFile* bfpFile = null;
			Platform.BfpSpawn_GetStdHandles(mSpawn, &bfpFile, null, null);
			if (bfpFile == null)
				return .Err;
			stream.Attach(bfpFile);
			return .Ok;
		}

		public Result<void> AttachStandardOutput(IFileStream stream)
		{
			if (mSpawn == null)
				return .Err;
			Platform.BfpFile* bfpFile = null;
			Platform.BfpSpawn_GetStdHandles(mSpawn, null, &bfpFile, null);
			if (bfpFile == null)
				return .Err;
			stream.Attach(bfpFile);
			return .Ok;
		}

		public Result<void> AttachStandardError(IFileStream stream)
		{
			if (mSpawn == null)
				return .Err;
			Platform.BfpFile* bfpFile = null;
			Platform.BfpSpawn_GetStdHandles(mSpawn, null, null, &bfpFile);
			if (bfpFile == null)
				return .Err;
			stream.Attach(bfpFile);
			return .Ok;
		}

		public bool WaitFor(int waitMS = -1)
		{
			if (mSpawn == null)
				return true;

			if (!Platform.BfpSpawn_WaitFor(mSpawn, waitMS, &mExitCode, null))
			{
				return false;
			}
			mIsDone = true;
			return true;
		}

		public void Close()
		{
			if (mSpawn != null)
			{
                Platform.BfpSpawn_Release(mSpawn);
				mSpawn = null;
			}
		}

		public void Kill(int32 exitCode = 0, KillFlags killFlags = .None)
		{
			if (mSpawn != null)
			{
				Platform.BfpSpawn_Kill(mSpawn, exitCode, (Platform.BfpKillFlags)killFlags, null);
			}
		}
	}

#if TEST && !BF_PLATFORM_WINDOWS
	class SpawnedProcessTests
	{
		static void ReadAll(FileStream stream, String outText)
		{
			StreamReader reader = scope .(stream, null, false, 4096);
			reader.ReadToEnd(outText).IgnoreError();
		}

		[Test]
		public static void WorkingDirectory_AppliesOnlyToChild()
		{
			String cwdBefore = scope .();
			Directory.GetCurrentDirectory(cwdBefore);

			ProcessStartInfo info = scope .();
			info.UseShellExecute = false;
			info.RedirectStandardOutput = true;
			info.SetFileName("/bin/pwd");
			info.SetWorkingDirectory("/");

			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Ok);
			FileStream stdOut = scope .();
			Test.Assert(process.AttachStandardOutput(stdOut) case .Ok);
			String output = scope .();
			ReadAll(stdOut, output);
			output.Trim();

			String cwdAfter = scope .();
			Directory.GetCurrentDirectory(cwdAfter);
			// Restore before asserting so a failure doesn't leak into other tests
			Directory.SetCurrentDirectory(cwdBefore).IgnoreError();

			Test.Assert(process.ExitCode == 0);
			Test.Assert(output == "/");
			Test.Assert(cwdAfter == cwdBefore);
		}

		[Test]
		public static void WorkingDirectory_MissingFailsStart()
		{
			ProcessStartInfo info = scope .();
			info.UseShellExecute = false;
			info.SetFileName("/bin/pwd");
			info.SetWorkingDirectory("/nonexistent_bf_spawn_test_dir");

			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Err);
		}

		[Test]
		public static void RedirectedStreams_RoundTrip()
		{
			ProcessStartInfo info = scope .();
			info.UseShellExecute = false;
			info.RedirectStandardInput = true;
			info.RedirectStandardOutput = true;
			info.RedirectStandardError = true;
			info.SetFileName("/bin/sh");
			info.SetArguments("-c \"tr a-z A-Z; echo err >&2\"");

			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Ok);
			FileStream stdIn = scope .();
			FileStream stdOut = scope .();
			FileStream stdErr = scope .();
			Test.Assert(process.AttachStandardInput(stdIn) case .Ok);
			Test.Assert(process.AttachStandardOutput(stdOut) case .Ok);
			Test.Assert(process.AttachStandardError(stdErr) case .Ok);

			stdIn.WriteStrUnsized("hello").IgnoreError();
			stdIn.Close().IgnoreError();

			String output = scope .();
			ReadAll(stdOut, output);
			String error = scope .();
			ReadAll(stdErr, error);

			Test.Assert(process.ExitCode == 0);
			Test.Assert(output == "HELLO");
			Test.Assert(error == "err\n");
		}

		[Test]
		public static void RedirectedPipes_NotInheritedByOtherSpawns()
		{
			ProcessStartInfo catInfo = scope .();
			catInfo.UseShellExecute = false;
			catInfo.RedirectStandardInput = true;
			catInfo.SetFileName("/bin/cat");

			SpawnedProcess cat = scope .();
			Test.Assert(cat.Start(catInfo) case .Ok);
			FileStream catIn = scope .();
			Test.Assert(cat.AttachStandardInput(catIn) case .Ok);

			// An unrelated spawn made while cat's stdin pipe is open must not hold a copy of it
			ProcessStartInfo sleepInfo = scope .();
			sleepInfo.UseShellExecute = false;
			sleepInfo.SetFileName("/bin/sleep");
			sleepInfo.SetArguments("5");

			SpawnedProcess sleeper = scope .();
			Test.Assert(sleeper.Start(sleepInfo) case .Ok);

			catIn.Close().IgnoreError();
			bool catExited = cat.WaitFor(2000);

			sleeper.Kill();
			sleeper.WaitFor();
			if (!catExited)
			{
				cat.Kill();
				cat.WaitFor();
			}

			Test.Assert(catExited);
		}

		[Test]
		public static void StdHandles_OnlyRedirectedStreamsAttachOnce()
		{
			ProcessStartInfo info = scope .();
			info.UseShellExecute = false;
			info.RedirectStandardOutput = true;
			info.SetFileName("/bin/true");

			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Ok);

			// A wrongly attached stream would own fd 0, this process's stdin, so leak it rather than close it
			FileStream unredirected = new .();
			bool attachedUnredirected = process.AttachStandardError(unredirected) case .Ok;
			if (!attachedUnredirected)
				delete unredirected;

			FileStream first = scope .();
			bool attachedFirst = process.AttachStandardOutput(first) case .Ok;
			FileStream second = new .();
			bool attachedSecond = process.AttachStandardOutput(second) case .Ok;
			if (!attachedSecond)
				delete second;

			process.WaitFor();

			Test.Assert(!attachedUnredirected);
			Test.Assert(attachedFirst);
			Test.Assert(!attachedSecond);
		}

		[Test]
		public static void Close_ReleasesUntakenStdIn()
		{
			String markerPath = scope .();
			Path.GetTempPath(markerPath).IgnoreError();
			markerPath.Append("/bf_spawn_close_test.tmp");
			File.Delete(markerPath).IgnoreError();

			// sh only creates the marker once cat sees EOF on the stdin pipe nobody took
			ProcessStartInfo info = scope .();
			info.UseShellExecute = false;
			info.RedirectStandardInput = true;
			info.SetFileName("/bin/sh");
			info.SetArguments(scope String()..AppendF("-c \"cat > /dev/null; touch '{}'\"", markerPath));

			SpawnedProcess process = scope .();
			Test.Assert(process.Start(info) case .Ok);
			process.Close();

			bool markerCreated = false;
			for (int i = 0; i < 40; i++)
			{
				if (File.Exists(markerPath))
				{
					markerCreated = true;
					break;
				}
				System.Threading.Thread.Sleep(50);
			}
			File.Delete(markerPath).IgnoreError();

			Test.Assert(markerCreated);
		}
	}
#endif
}
