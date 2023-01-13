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
}
