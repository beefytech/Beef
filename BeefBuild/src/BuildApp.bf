using IDE;
using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
using System.Collections;
using IDE.Util;

namespace BeefBuild
{
	class BuildApp : IDEApp
	{
		public enum MainVerbState
		{
			None,
			UpdateList,
			End
		}

		const int cProgressSize = 30;
		int mProgressIdx = 0;		
		public bool mIsTest;
		public bool mTestIncludeIgnored;
		public bool mDidRun;
		public bool mWantsGenerate = false;
		public bool mHandledVerb;
		public String mRunArgs ~ delete _;
		MainVerbState mMainVerbState;

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
			GetVersionInfo(var exeTime);

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

			if (mWantsGenerate)
			{
				if (mWorkspace.mStartupProject != null)
				{
					if (mWorkspace.mStartupProject.IsEmpty)
						AutoGenerateStartupCode(mWorkspace.mStartupProject);
					else
						OutputErrorLine("The project '{}' is not empty, but '-generate' was specified.", mWorkspace.mStartupProject.mProjectName);
				}
			}
		}

		public override bool HandleCommandLineParam(String key, String value)
		{
			if (mRunArgs != null)
			{
				if (!mRunArgs.IsEmpty)
					mRunArgs.Append(" ");
				if (value != null)
				{
					String qKey = scope .(key);
					String qValue = scope .(value);
					IDEApp.QuoteIfNeeded(qKey);
					IDEApp.QuoteIfNeeded(qValue);
					mRunArgs.Append(qKey);
					mRunArgs.Append('=');
					mRunArgs.Append(qValue);
				}
				else
				{
					String qKey = scope .(key);
					IDEApp.QuoteIfNeeded(qKey);
					mRunArgs.Append(qKey);
				}
				return true;
			}

			if (key.StartsWith("--"))
				key.Remove(0, 1);

			if (value == null)
			{
				switch (key)
				{
				case "-args":
					if (mRunArgs == null)
						mRunArgs = new .();
					return true;
				case "-new":
					mVerb = .New;
					return true;
				case "-generate":
					mWantsGenerate = true;
					return true;
				case "-run":
					if (mVerbosity == .Default)
						mVerbosity = .Minimal;
					mVerb = .Run;
					return true;
				case "-test":
					mIsTest = true;
					return true;
				case "-testall":
					mIsTest = true;
					mTestIncludeIgnored = true;
					return true;
				case "-clean":
					mWantsClean = true;
					return true;
				case "-noir":
					mConfig_NoIR = true;
					return true;
				case "-update":
					if (mWantUpdateVersionLocks == null)
						mWantUpdateVersionLocks = new .();
					return true;
				case "-version":
					mVerb = .GetVersion;
					return true;
				case "-crash":
					Runtime.FatalError("-crash specified on command line");
				}

				if (!key.StartsWith('-'))
				{
					switch (mMainVerbState)
					{
					case .None:
						mMainVerbState = .End;
						switch (key)
						{
						case "build":
							mVerb = .None;
						case "new":
							mVerb = .New;
						case "generate":
							mWantsGenerate = true;
						case "run":
							if (mVerbosity == .Default)
								mVerbosity = .Minimal;
							mVerb = .Run;
						case "test":
							mIsTest = true;
						case "testall":
							mIsTest = true;
							mTestIncludeIgnored = true;
						case "clean":
							mWantsClean = true;
						case "version":
							mVerb = .GetVersion;
						case "crash":
							Runtime.FatalError("-crash specified on command line");
						case "update":
							mVerb = .Update;
							mWantUpdateVersionLocks = new .();
							mMainVerbState = .UpdateList;
						default:
							mMainVerbState = .None;
						}
						if (mMainVerbState != .None)
							return true;
					case .UpdateList:
						mWantUpdateVersionLocks.Add(new .(key));
						return true;
					case .End:
						return false;
					default:
					}
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
				case "-update":
					if (mWantUpdateVersionLocks == null)
						mWantUpdateVersionLocks = new .();
					mWantUpdateVersionLocks.Add(new .(value));
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

			if (mWorkspace.mProjectLoadState != .Loaded)
			{
				// Wait for workspace to complete loading
			}
			else
			{
				if ((!mFailed) && (!mHandledVerb))
				{
					mHandledVerb = true;
					if (mIsTest)
					{
						RunTests(mTestIncludeIgnored, false);
					}
					else if (mVerb == .Update)
					{
						// No-op here
					}
					else if (mVerb != .New)
						Compile(.Normal, null);
				}

				if (mCompilingBeef)
				{
					WriteProgress(mBfBuildCompiler.GetCompletionPercentage());
				}

				if ((!IsCompiling) && (!AreTestsRunning()))
				{
					if ((mVerb == .Run) && (!mDidRun) && (!mFailed))
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

						ExecutionQueueCmd executionCmd = QueueRun(targetPaths[0], mRunArgs ?? "", curPath);
						executionCmd.mIsTargetRun = true;
						mDidRun = true;
						return;
					}

					Stop();
				}
			}
		}
	}
}
