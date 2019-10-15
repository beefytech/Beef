using IDE;
using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
using System.Collections.Generic;
using IDE.Util;

namespace BeefBuild
{
	class BuildApp : IDEApp
	{
		const int cProgressSize = 30;
		int mProgressIdx = 0;		
		public bool mIsTest;
		public bool mIsFailTest;
		public bool mDidRun;

		/*void Test()
		{
			/*CURL.Easy easy = new CURL.Easy();
			easy.SetOpt(.URL, "http://raw.githubusercontent.com/alexcrichton/curl-rust/master/Cargo.toml");
			easy.SetOpt(.Verbose, true);
			easy.Perform();*/
			Transfer transfer = new Transfer();
			//transfer.Setup("https://curl.haxx.se/download/curl-7.57.0.zip");
			transfer.Setup("https://secure-appldnld.apple.com/itunes12/091-56359-20171213-EDF2198A-E039-11E7-9A9F-D21A1E4B8CED/iTunes64Setup.exe");
			transfer.PerformBackground();

			while (transfer.IsRunning)
			{
				Thread.Sleep(100);
				Console.WriteLine("{0}/{1} @{2}Kps", transfer.BytesReceived, transfer.TotalBytes, transfer.BytesPerSecond / 1024);
			}

#unwarn
			let result = transfer.GetResult();
		}*/

		public this()
		{
			//mConfigName.Clear();
			//mPlatformName.Clear();
			//Test();
		}

		public override void Init()
		{
			GetVersionInfo();

			if (mVerbosity == .Default)
				mVerbosity = .Normal;

			if (mConfigName.IsEmpty)
			{
				mConfigName.Set(mIsTest ? "Test" : "Debug");
			}

			if (mPlatformName.IsEmpty)
			{
				mPlatformName.Set(sPlatform64Name);
			}

			mMainThread = Thread.CurrentThread;

			if (mConfigName.IsEmpty)
				Fail("Config not specified");
			if (mPlatformName.IsEmpty)
				Fail("Platform not specified");

			base.Init();

			mSettings.Load();
			mSettings.Apply();

			mInitialized = true;
			CreateBfSystems();
			if (mWantsClean)
			{
				mBfBuildCompiler.ClearBuildCache();
				mWantsClean = false;
			}

			if (mWorkspace.mDir == null)
			{
				mWorkspace.mDir = new String();
				Directory.GetCurrentDirectory(mWorkspace.mDir);
			}

			if (mWorkspace.mDir != null)
			{
			    mWorkspace.mName = new String();
			    Path.GetFileName(mWorkspace.mDir, mWorkspace.mName);
			    LoadWorkspace(mVerb);                
			}
			else
				Fail("Workspace not specified");

			if (mFailed)
				return;

			WorkspaceLoaded();

			if (mIsTest)
			{
				RunTests(false);
			}
			else if (mVerb != .New)
				Compile(.Normal, null);
		}

		public override bool HandleCommandLineParam(String key, String value)
		{
			if (key.StartsWith("--"))
				key.Remove(0, 1);

			if (value == null)
			{
				switch (key)
				{
				case "-new":
					mVerb = .New;
					return true;
				case "-run":
					if (mVerbosity == .Default)
						mVerbosity = .Minimal;
					mVerb = .Run;
					return true;
				case "-test":
					mIsTest = true;
					return true;
				case "-testfail":
					mIsFailTest = true;
					return true;
				case "-clean":
					mWantsClean = true;
					return true;
				case "-noir":
					mConfig_NoIR = true;
					return true;
				case "-version":
					mVerb = .GetVersion;
					return true;
				}
			}
			else
			{
				if ((key == "-proddir") || (key == "-workspace"))
				{
				    var relDir = scope String(value);
					if ((relDir.EndsWith("\\")) || relDir.EndsWith("\""))
						relDir.RemoveToEnd(relDir.Length - 1); //...
				    IDEUtils.FixFilePath(relDir);

					String fullDir = new String();
					Path.GetFullPath(relDir, fullDir);

					mWorkspace.mDir = fullDir;
					return true;
				}

				switch (key)
				{
				case "-config":
					mConfigName.Set(value);
					return true;
				case "-platform":
					mPlatformName.Set(value);
					return true;
				case "-verbosity":
				    if (value == "quiet")
					    mVerbosity = .Quiet;
					else if (value == "minimal")
					    mVerbosity = .Minimal;
					else if (value == "normal")
					    mVerbosity = .Normal;
					else if (value == "detailed")
					    mVerbosity = .Detailed;
					else if (value == "diagnostic")
					    mVerbosity = .Diagnostic;
					else
						Fail(scope String()..AppendF("Invalid verbosity option: {}", value));
					return true;
				}
			}

#if BF_PLATFORM_WINDOWS
			if (key == "-wait")
			{
				Windows.MessageBoxA((Windows.HWnd)0, "Wait2", "Wait", 0);
				return true;
			}
#endif //BF_PLATFORM_WINDOWS
			
			return false;
		}

		protected override void BeefCompileStarted()
		{
			base.BeefCompileStarted();

			if (mVerbosity >= .Normal)
			{
				if (cProgressSize > 0)
				{
					String str = scope String();
					str.Append("[");
					str.Append(' ', cProgressSize);
					str.Append("]");
					str.Append('\b', cProgressSize + 1);
					Console.Write(str);
				}
			}
		}

		void WriteProgress(float pct)
		{
			if (mVerbosity >= .Normal)
			{
				int progressIdx = (int)Math.Round(pct * cProgressSize);
				while (progressIdx > mProgressIdx)
				{
					mProgressIdx++;
					Console.Write("*");
				}
			}
		}

		protected override void BeefCompileDone()
		{
			base.BeefCompileDone();
			WriteProgress(1.0f);
			if (mVerbosity >= .Normal)
				Console.WriteLine("");
		}

		public override void LoadFailed()
		{
			mFailed = true;
		}

		public override void TestFailed()
		{
			mFailed = true;
		}

		protected override void CompileFailed()
		{
			base.CompileFailed();
			mFailed = true;
		}

		protected override void CompileDone(bool succeeded)
		{
			if (!succeeded)
				mFailed = true;
		}

		public override void Update(bool batchStart)
		{
			base.Update(batchStart);

			if (mCompilingBeef)
			{
				WriteProgress(mBfBuildCompiler.GetCompletionPercentage());
			}

			if ((!IsCompiling) && (!AreTestsRunning()))
			{
				if ((mVerb == .Run) && (!mDidRun))
				{
					let curPath = scope String();
					Directory.GetCurrentDirectory(curPath);

					let workspaceOptions = gApp.GetCurWorkspaceOptions();
					let options = gApp.GetCurProjectOptions(mWorkspace.mStartupProject);
					let targetPaths = scope List<String>();
					defer ClearAndDeleteItems(targetPaths);
					this.[Friend]GetTargetPaths(mWorkspace.mStartupProject, gApp.mPlatformName, workspaceOptions, options, targetPaths);
					if (targetPaths.IsEmpty)
						return;

					ExecutionQueueCmd executionCmd = QueueRun(targetPaths[0], "", curPath);
					executionCmd.mIsTargetRun = true;
					mDidRun = true;
					return;
				}

				Stop();
			}
		}
	}
}
