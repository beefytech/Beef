using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.IO;

namespace IDE
{
	class TestManager
	{
		public class ProjectInfo
		{
			public Project mProject;
			public String mTestExePath ~ delete _;
		}

		public class TestEntry
		{
			public String mName ~ delete _;
			public String mFilePath ~ delete _;
			public int mLine;
			public int mColumn;
			public bool mShouldFail;
			public bool mProfile;
			public bool mIgnore;
		}

		public class TestInstance
		{
			public SpawnedProcess mProcess ~ delete _;
			public Thread mThread ~ delete _;
			public int mProjectIdx;
			public List<TestEntry> mTestEntries = new .() ~ DeleteContainerAndItems!(_);
			public String mPipeName ~ delete _;
			public String mArgs ~ delete _;
			public String mWorkingDir ~ delete _;
			public NamedPipe mPipeServer ~ delete _;
			public int mShouldFailIdx = -1;
		}

		public bool mIsDone;
		public bool mIsRunning;
		public bool mWantsStop;
		public int mProjectInfoIdx = -1;
		public TestInstance mTestInstance ~ delete _;
		public List<ProjectInfo> mProjectInfos = new .() ~ DeleteContainerAndItems!(_);
		public List<String> mQueuedOutput = new .() ~ DeleteContainerAndItems!(_);
		public Monitor mMonitor = new Monitor() ~ delete _;
		public String mPrevConfigName ~ delete _;
		public bool mDebug;
		public bool mIncludeIgnored;
		public bool mFailed;

		public ~this()
		{
			if (mTestInstance != null)
			{
				mTestInstance.mThread.Join();
			}
		}

		public bool IsRunning()
		{
			return true;
		}

		public void AddProject(Project project)
		{
			var projectInfo = new ProjectInfo();
			projectInfo.mProject = project;
			mProjectInfos.Add(projectInfo);
		}

		public bool IsTesting(Project project)
		{
			return GetProjectInfo(project) != null;
		}

		public ProjectInfo GetProjectInfo(Project project)
		{
			int projectIdx = mProjectInfos.FindIndex(scope (info) => info.mProject == project);
			if (projectIdx == -1)
				return null;
			return mProjectInfos[projectIdx];
		}

		public void Start()
		{
			mIsRunning = true;
		}

		public void BuildFailed()
		{
			mIsDone = true;
		}

		ProjectInfo GetCurProjectInfo()
		{
			if ((mProjectInfoIdx >= 0) && (mProjectInfoIdx < mProjectInfos.Count))
				return mProjectInfos[mProjectInfoIdx];
			return null;
		}

		void QueueOutputLine(StringView str)
		{
			using (mMonitor.Enter())
				mQueuedOutput.Add(new String(str));
		}

		void QueueOutputLine(StringView str, params Object[] args)
		{
			using (mMonitor.Enter())
			{
				var formattedStr = new String();
				formattedStr.AppendF(str, params args);
				mQueuedOutput.Add(formattedStr);
			}
		}

		public void TestProc(TestInstance testInstance)
		{
			var curProjectInfo = GetCurProjectInfo();

			if (!mDebug)
			{
				var startInfo = scope ProcessStartInfo();
				startInfo.CreateNoWindow = !gApp.mTestEnableConsole;
				startInfo.SetFileName(curProjectInfo.mTestExePath);
				startInfo.SetArguments(testInstance.mArgs);
				startInfo.SetWorkingDirectory(testInstance.mWorkingDir);
				mTestInstance.mProcess = new SpawnedProcess();
				if (testInstance.mProcess.Start(startInfo) case .Err)
				{
					TestFailed();
					QueueOutputLine("ERROR: Failed execute '{0}'", curProjectInfo.mTestExePath);
					return;
				}
			}

			String clientStr = scope String();

			int curTestIdx = -1;
			int curTestRunCount = 0;
			bool testsFinished = false;
			bool failed = false;

			int exitCode = 0;

			while (true)
			{
				int doneCount = 0;

				for (int itr < 2)
				{
					bool hadData = false;

					uint8[1024] data;
					switch (testInstance.mPipeServer.TryRead(.(&data, 1024), 20))
					{
					case .Ok(let size):
					{
						clientStr.Append((char8*)&data, size);
						hadData = true;
					}
					default:
					}

					while (true)
					{
						int crPos = clientStr.IndexOf('\n');
						if (crPos == -1)
							break;

						String cmd = scope String();
						cmd.Append(clientStr, 0, crPos);
						clientStr.Remove(0, crPos + 1);

						/*String outStr = scope String();
						outStr.AppendF("CMD: {0}", cmd);
						QueueOutput(outStr);*/

						List<StringView> cmdParts = scope .(cmd.Split('\t'));
						switch (cmdParts[0])
						{
						case ":TestInit":
						case ":TestBegin":
						case ":TestQuery":
							if ((curTestIdx == -1) || (curTestRunCount > 0))
							{
								curTestIdx++;
								curTestRunCount = 0;
							}

							while (true)
							{
								if (curTestIdx < testInstance.mTestEntries.Count)
								{
									curTestRunCount++;
									bool skipEntry = false;

									let testEntry = testInstance.mTestEntries[curTestIdx];
									if (testEntry.mShouldFail)
									{
										skipEntry = testInstance.mShouldFailIdx != curTestIdx;
									}
									else if (testInstance.mShouldFailIdx != -1)
									{
										skipEntry = true;
									}

									if ((!skipEntry) && (testEntry.mIgnore) && (!mIncludeIgnored))
									{
										QueueOutputLine("Test Ignored: {0}", testEntry.mName);
										skipEntry = true;
									}

									if (skipEntry)
									{
										curTestIdx++;
										curTestRunCount = 0;
										continue;
									}

									var clientCmd = scope String();
									clientCmd.AppendF(":TestRun\t{0}\n", curTestIdx);
									if (testInstance.mPipeServer.Write(clientCmd) case .Err)
										failed = true;
								}
								else
								{
									if (testInstance.mPipeServer.Write(":TestFinish\n") case .Err)
										failed = true;
								}
								break;
							}
						case ":TestResult":
							int timeMS = int32.Parse(cmdParts[1]).Get();
							var testEntry = testInstance.mTestEntries[curTestIdx];
							if (testEntry.mShouldFail)
							{
								QueueOutputLine("ERROR: Test should have failed but didn't: {0} Time: {1}ms", testEntry.mName, timeMS);
								failed = true;
							}
							else
								QueueOutputLine("Test completed: {0} Time: {1}ms", testEntry.mName, timeMS);
						case ":TestFinish":
							testsFinished = true;
						default:
							Debug.Assert(cmdParts[0][0] != ':');

							let attribs = cmdParts[1];

							TestEntry testEntry = new TestEntry();
							testEntry.mName = new String(cmdParts[0]);
							testEntry.mFilePath = new String(cmdParts[2]);
							testEntry.mLine = int32.Parse(cmdParts[3]).Get();
							testEntry.mColumn = int32.Parse(cmdParts[4]).Get();

							testEntry.mShouldFail = attribs.Contains("Sf");
							testEntry.mProfile = attribs.Contains("Pr");
							testEntry.mIgnore = attribs.Contains("Ig");

							testInstance.mTestEntries.Add(testEntry);
						}
					}

					if (mWantsStop)
					{
						if (testInstance.mProcess != null)
							testInstance.mProcess.Kill();
					}

					if (!hadData)
					{
						bool processDone;
						if (testInstance.mProcess != null)
							processDone = testInstance.mProcess.WaitFor(0);
						else
							processDone = !gApp.mDebugger.mIsRunning;

						if (processDone)
						{
							if (testInstance.mProcess != null)
							{
								exitCode = testInstance.mProcess.ExitCode;
							}

							doneCount++;
						}
					}
				}

				if (doneCount == 2)
					break;

				if (failed)
				{
					TestFailed();
					break;
				}
			}

			if (mWantsStop)
			{
				QueueOutputLine("Tests aborted");
			}
			else if (!testsFinished)
			{
				var str = scope String();
				if (curTestIdx == -1)
				{
					str.AppendF("Failed to start tests");
				}
				else if (curTestIdx < testInstance.mTestEntries.Count)
				{
					var testEntry = testInstance.mTestEntries[curTestIdx];
					if (testInstance.mShouldFailIdx == curTestIdx)
					{
						// Success
						QueueOutputLine("Test expectedly failed: {0}", testEntry.mName);
					}
					else
					{
						str.AppendF("ERROR: Failed test '{0}' at line {2}:{3} in {1}", testEntry.mName, testEntry.mFilePath, testEntry.mLine + 1, testEntry.mColumn + 1);
					}
				}
				else
				{
					str.AppendF("ERROR: Failed to finish tests");
				}

				if (str.Length > 0)
				{
					var errStr = scope String();
					errStr.AppendF("ERROR: {0}", str);
					QueueOutputLine(errStr);
					TestFailed();
				}
			}
			else if (exitCode != 0)
			{
				if (exitCode != 0)
				{
					QueueOutputLine("ERROR: Test process exited with error code: {0}", exitCode);
					TestFailed();
				}
			}
		}

		public void Update()
		{
			using (mMonitor.Enter())
			{
				while (mQueuedOutput.Count > 0)
				{
					var str = mQueuedOutput.PopFront();
					gApp.OutputLineSmart(str);
					delete str;
				}
			}

			if ((!mIsRunning) || (mIsDone))
				return;

			if (mWantsStop)
			{
				if (gApp.mDebugger.mIsRunning)
					gApp.mDebugger.Terminate();
			}

			int nextShouldFailIdx = -1;
			bool doNext = true;
			var curProjectInfo = GetCurProjectInfo();
			if (curProjectInfo != null)
			{
				if (mTestInstance != null)
				{
					if (mTestInstance.mThread.Join(0))
					{
						for (int entryIdx = mTestInstance.mShouldFailIdx + 1; entryIdx < mTestInstance.mTestEntries.Count; entryIdx++)
						{
							let testEntry = mTestInstance.mTestEntries[entryIdx];
							if (testEntry.mShouldFail)
							{
								nextShouldFailIdx = entryIdx;
								break;
							}
						}

						DeleteAndNullify!(mTestInstance);
					}
					else
						doNext = false;
				}
			}
			else
			{
				Debug.Assert(mTestInstance == null);
			}

			if (doNext)
			{
				if (mWantsStop)
				{
					mIsDone = true;
					return;
				}

				Debug.Assert(mTestInstance == null);

				if (nextShouldFailIdx == -1)
				{
					mProjectInfoIdx++;
					if (mProjectInfoIdx >= mProjectInfos.Count)
					{
						mIsDone = true;
						return;
					}
				}

				mTestInstance = new TestInstance();
				mTestInstance.mProjectIdx = mProjectInfoIdx;
				mTestInstance.mShouldFailIdx = nextShouldFailIdx;

				curProjectInfo = GetCurProjectInfo();
				if (mTestInstance.mShouldFailIdx != -1)
					gApp.OutputLineSmart("Starting should-fail testing on {0}...", curProjectInfo.mProject.mProjectName);
				else
					gApp.OutputLineSmart("Starting testing on {0}...", curProjectInfo.mProject.mProjectName);
				
				mTestInstance.mThread = new Thread(new () => { TestProc(mTestInstance); } );

				mTestInstance.mPipeName = new String();
				mTestInstance.mPipeName.AppendF("__bfTestPipe{0}_{1}", Process.CurrentId, mTestInstance.mProjectIdx);

				mTestInstance.mArgs = new String();
				mTestInstance.mArgs.Append(mTestInstance.mPipeName);

				//mTestInstance.mWorkingDir = new String();
				//Path.GetDirectoryName(curProjectInfo.mTestExePath, mTestInstance.mWorkingDir);
				mTestInstance.mWorkingDir = new String(gApp.mInstallDir);

				mTestInstance.mPipeServer = new NamedPipe();
				if (mTestInstance.mPipeServer.Create(".", mTestInstance.mPipeName, .AllowTimeouts) case .Err)
				{
					QueueOutputLine("ERROR: Failed to create named pipe for test");
					TestFailed();
					return;
				}

				if (mDebug)
				{
					gApp.[Friend]CheckDebugVisualizers();

					var envVars = scope Dictionary<String, String>();
					defer { for (var kv in envVars) { delete kv.key; delete kv.value; } }
					Environment.GetEnvironmentVariables(envVars);

					var envBlock = scope List<char8>();
					Environment.EncodeEnvironmentVariables(envVars, envBlock);
					if (!gApp.mDebugger.OpenFile(curProjectInfo.mTestExePath, mTestInstance.mArgs, mTestInstance.mWorkingDir, envBlock, true))
					{
						QueueOutputLine("ERROR: Failed debug '{0}'", curProjectInfo.mTestExePath);
						TestFailed();
						return;
					}

					gApp.mDebugger.ClearInvalidBreakpoints();
					gApp.mTargetDidInitBreak = false;
					gApp.mTargetHadFirstBreak = false;

					gApp.mDebugger.RehupBreakpoints(true);
					gApp.mDebugger.Run();
					gApp.mDebugger.mIsRunning = true;
				}

				mTestInstance.mThread.Start(false);
			}
		}

		public void TestFailed()
		{
			gApp.TestFailed();
			mIsDone = true;
			mFailed = true;
		}

		public void Stop()
		{
			mWantsStop = true;
		}
	}
}
