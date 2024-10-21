#pragma warning disable 168

using System.Diagnostics;
using System;
using System.Threading;
using System.IO;
using System.Collections;

namespace IDE.util;

class GitManager
{
	public enum Error
	{
		Unknown
	}

	public class GitInstance : RefCounted
	{
		public class TagInfo
		{
			public String mHash ~ delete _;
			public String mTag ~ delete _;
		}

		public GitManager mGitManager;
		public bool mFailed;
		public bool mDone;
		public bool mStarted;
		public bool mRemoved;

		public String mArgs ~ delete _;
		public String mPath ~ delete _;
		public float mProgress;
		public float mProgressRecv;
		public float mProgressDeltas;
		public float mProgressFiles;

		public Stopwatch mStopwatch = new .()..Start() ~ delete _;

		public SpawnedProcess mProcess ~ delete _;
		public Monitor mMonitor = new .() ~ delete _;
		public List<String> mDeferredOutput = new .() ~ DeleteContainerAndItems!(_);
		public List<TagInfo> mTagInfos = new .() ~ DeleteContainerAndItems!(_);

		public Thread mOutputThread ~ delete _;
		public Thread mErrorThread ~ delete _;

		public this(GitManager gitManager)
		{
			mGitManager = gitManager;
		}

		public ~this()
		{
			IDEUtils.SafeKill(mProcess);
			mOutputThread?.Join();
			mErrorThread?.Join();

			if (!mRemoved)
				mGitManager.mGitInstances.Remove(this);
		}

		public void Init(StringView args, StringView path)
		{
			mArgs = new .(args);
			if (path != default)
				mPath = new .(path);
		}

		public void Start()
		{
			if (mStarted)
				return;
			mStarted = true;

			ProcessStartInfo psi = scope ProcessStartInfo();

			String gitPath = scope .();
#if BF_PLATFORM_WINDOWS
			Path.GetAbsolutePath(gApp.mInstallDir, "git/cmd/git.exe", gitPath);
			if (!File.Exists(gitPath))
				gitPath.Clear();

			if (gitPath.IsEmpty)
			{
				Path.GetAbsolutePath(gApp.mInstallDir, "../../bin/git/cmd/git.exe", gitPath);
				if (!File.Exists(gitPath))
					gitPath.Clear();
			}

			if (gitPath.IsEmpty)
			{
				Path.GetAbsolutePath(gApp.mInstallDir, "../../../bin/git/cmd/git.exe", gitPath);
				if (!File.Exists(gitPath))
					gitPath.Clear();
			}
#endif
			if (gitPath.IsEmpty)
				gitPath.Set("git");

			psi.SetFileName(gitPath);
			psi.SetArguments(mArgs);
			if (mPath != null)
				psi.SetWorkingDirectory(mPath);
			psi.UseShellExecute = false;
			psi.RedirectStandardError = true;
			psi.RedirectStandardOutput = true;
			psi.CreateNoWindow = true;

			mProcess = new SpawnedProcess();
			if (mProcess.Start(psi) case .Err)
			{
				gApp.OutputErrorLine("Failed to execute Git");
				mFailed = true;
				return;
			}

			mOutputThread = new Thread(new => ReadOutputThread);
			mOutputThread.Start(false);

			mErrorThread = new Thread(new => ReadErrorThread);
			mErrorThread.Start(false);
		}

		public void ReadOutputThread()
		{
			FileStream fileStream = scope FileStream();
			if (mProcess.AttachStandardOutput(fileStream) case .Err)
				return;
			StreamReader streamReader = scope StreamReader(fileStream, null, false, 4096);

			int count = 0;
			while (true)
			{
				count++;
				var buffer = scope String();
				if (streamReader.ReadLine(buffer) case .Err)
					break;
				using (mMonitor.Enter())
				{
					mDeferredOutput.Add(new .(buffer));
				}
			}
		}

		public void ReadErrorThread()
		{
			FileStream fileStream = scope FileStream();
			if (mProcess.AttachStandardError(fileStream) case .Err)
				return;
			StreamReader streamReader = scope StreamReader(fileStream, null, false, 4096);

			while (true)
			{
				var buffer = scope String();
				if (streamReader.ReadLine(buffer) case .Err)
					break;

				using (mMonitor.Enter())
				{
					//mDeferredOutput.Add(new $"{mStopwatch.ElapsedMilliseconds / 1000.0:0.0}: {buffer}");
					mDeferredOutput.Add(new .(buffer));
				}
			}
		}

		public void Update()
		{
			using (mMonitor.Enter())
			{
				while (!mDeferredOutput.IsEmpty)
				{
					var line = mDeferredOutput.PopFront();
					defer delete line;
					//Debug.WriteLine($"GIT: {line}");

					if (line.StartsWith("Cloning into "))
					{
						// May be starting a submodule
						mProgressRecv = 0;
						mProgressDeltas = 0;
						mProgressFiles = 0;
					}

					if (line.StartsWith("remote: Counting objects"))
					{
						mProgressRecv = 0.001f;
					}

					if (line.StartsWith("Receiving objects: "))
					{
						var pctStr = line.Substring("Receiving objects: ".Length, 3)..Trim();
						mProgressRecv = float.Parse(pctStr).GetValueOrDefault() / 100.0f;
					}

					if (line.StartsWith("Resolving deltas: "))
					{
						var pctStr = line.Substring("Resolving deltas: ".Length, 3)..Trim();
						mProgressDeltas = float.Parse(pctStr).GetValueOrDefault() / 100.0f;
						mProgressRecv = 1.0f;
					}

					if (line.StartsWith("Updating files: "))
					{
						var pctStr = line.Substring("Updating files: ".Length, 3)..Trim();
						mProgressFiles = float.Parse(pctStr).GetValueOrDefault() / 100.0f;
						mProgressRecv = 1.0f;
						mProgressDeltas = 1.0f;
					}

					StringView version = default;

					int refTagIdx = line.IndexOf("\trefs/tags/");
					if (refTagIdx == 40)
						version = line.Substring(40 + "\trefs/tags/".Length);

					if ((line.Length == 45) && (line.EndsWith("HEAD")))
						version = "HEAD";

					if (!version.IsEmpty)
					{
						TagInfo tagInfo = new .();
						tagInfo.mHash = new .(line, 0, 40);
						tagInfo.mTag = new .(version);
						mTagInfos.Add(tagInfo);
					}
				}
			}

			float pct = 0;
			if (mProgressRecv > 0)
				pct = 0.1f + (mProgressRecv * 0.3f) + (mProgressDeltas * 0.4f) + (mProgressFiles * 0.2f);

			if (pct > mProgress)
			{
				mProgress = pct;
				//Debug.WriteLine($"Completed Pct: {pct}");
			}

			if (mProcess.WaitFor(0))
			{
				if (mProcess.ExitCode != 0)
					mFailed = true;
				mDone = true;
			}
		}

		public void Cancel()
		{
			if (!mProcess.WaitFor(0))
			{
				//Debug.WriteLine($"GitManager Cancel {mProcess.ProcessId}");
				IDEUtils.SafeKill(mProcess);
			}
		}
	}

	public const int sMaxActiveGitInstances = 4;

	public List<GitInstance> mGitInstances = new .() ~
		{
			for (var gitInstance in _)
				gitInstance.ReleaseRef();
			delete _;
		};

	public void Init()
	{
		//StartGit("-v");

		//Repository repository = Clone("https://github.com/llvm/llvm-project", "c:/temp/__LLVM");

		//Repository repository = Clone("https://github.com/Starpelly/raylib-beef", "c:/temp/__RAYLIB");
		/*while (true)
		{
			Thread.Sleep(500);
			Debug.WriteLine($"Repository {repository.mStatus} {repository.GetCompletedPct()}");
		}*/
	}

	public GitInstance StartGit(StringView cmd, StringView path = default)
	{
		//Debug.WriteLine($"GIT STARTING: {cmd} in {path}");

		GitInstance gitInst = new .(this);
		gitInst.Init(cmd, path);
		mGitInstances.Add(gitInst);
		return gitInst;
	}

	public GitInstance Clone(StringView url, StringView path)
	{
		return StartGit(scope $"clone -v --progress --recurse-submodules {url} \"{path}\"");
	}

	public GitInstance Checkout(StringView path, StringView hash)
	{
		return StartGit(scope $"checkout -b BeefManaged {hash}", path);
	}

	public GitInstance GetTags(StringView url)
	{
		return StartGit(scope $"ls-remote {url}");
	}

	public void Update()
	{
		for (var gitInstance in mGitInstances)
		{
			if (@gitInstance.Index >= sMaxActiveGitInstances)
				break;

			if (!gitInstance.mStarted)
				gitInstance.Start();
			gitInstance.Update();

			if (gitInstance.mDone)
			{
				@gitInstance.Remove();
				gitInstance.mRemoved = true;
				gitInstance.ReleaseRef();
			}
		}
	}
}