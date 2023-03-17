using System;
using System.Collections;
using System.Reflection;
using System.IO;
using System.Diagnostics;
using IDE.ui;
using Beefy;
using Beefy.widgets;
using IDE.Debugger;
using Beefy.theme.dark;
using System.Threading;
using System.Security.Cryptography;
using IDE.Compiler;

namespace IDE
{
	class ScriptManager
	{
		public static ScriptManager sActiveManager;

		public class Context : RefCounted
		{
			public Dictionary<String, Variant> mVars = new .() ~
			{
				for (var kv in _)
				{
					delete kv.key;
					kv.value.Dispose();
				}
				delete _;
			};
		}

		class Target
		{
			public class Cmd
			{
				public MethodInfo mMethodInfo;
				public Object mTargetObject;
			}

			public Dictionary<String, Target> mTargets = new .() ~ DeleteDictionaryAndKeysAndValues!(_);
			public Dictionary<String, Cmd> mCmds = new .() ~ DeleteDictionaryAndKeysAndValues!(_);
		}

		public enum CmdFlags
		{
			None,
			NoLines,
			NoWait
		}

		public class QueuedCmd
		{
			public String mCondition ~ delete _;

			public CmdFlags mFlags;
			public bool mHandled = true;
			public String mCmd ~ delete _;
			public String mSrcFile ~ delete _;
			public int mLineNum = -1;

			public int mIntParam;
			public int mExecIdx;
			public bool mNoWait;
			public Stopwatch mStopWatch ~ delete _;
		}

		ScriptHelper mScriptHelper = new ScriptHelper(this) ~ delete _;
		Target mRoot = new Target() ~ delete _;
		public Context mContext ~ _.ReleaseRef();

		List<QueuedCmd> mCmdList = new .() ~ DeleteContainerAndItems!(_);
		public bool mFailed;
		public bool mCancelled;
		public QueuedCmd mCurCmd;
		public Stopwatch mTimeoutStopwatch ~ delete _;
		public int mTimeoutMS;
		public List<String> mExpectingErrors ~ DeleteContainerAndItems!(_);
		public bool mHadExpectingError;
		public int mDoneTicks;
		public bool mIsBuildScript;
		public bool mSoftFail;
		public Verbosity mVerbosity = .Quiet;

		public bool Failed
		{
			get
			{
				return mFailed;
			}
		}

		public bool HasQueuedCommands
		{
			get
			{
				return !mCmdList.IsEmpty;
			}
		}

		public this(Context context = null)
		{
			if (context != null)
			{
				context.AddRef();
				mContext = context;
			}
			else
				mContext = new Context();

			AddTarget(mScriptHelper);

			//Exec("OutputLine(\"Hey bro!\", 2)");
		}

		public void MarkNotHandled()
		{
			if (mCurCmd != null)
				mCurCmd.mHandled = false;
		}

		public void AddTarget(Object targetObject)
		{
			var targetType = targetObject.GetType();
			for (var methodInfo in targetType.GetMethods(.Instance | .Public | .NonPublic))
			{
				var methodName = methodInfo.Name;
				if (methodName.StartsWith("Cmd_"))
					methodName = .(methodName, 4);

				Target curTarget = mRoot;

				while (true)
				{
					int splitPos = methodName.IndexOf("__");
					if (splitPos != -1)
					{
						StringView cmdPart = .(methodName, 0, splitPos);

						String* keyPtr;
						Target* targetPtr;
						if (curTarget.mTargets.TryAdd(scope String(cmdPart), out keyPtr, out targetPtr))
						{
							*keyPtr = new String(cmdPart);
							*targetPtr = new Target();
						}
						curTarget = *targetPtr;

						methodName.RemoveFromStart(splitPos + 2);
					}
					else
					{
						String* keyPtr;
						Target.Cmd* cmdPtr;
						if (curTarget.mCmds.TryAdd(scope String(methodName), out keyPtr, out cmdPtr))
						{
							*keyPtr = new String(methodName);
							*cmdPtr = new .();

							let cmd = *cmdPtr;
							cmd.mMethodInfo = methodInfo;
							cmd.mTargetObject = targetObject;
						}
						break;
					}
				}
			}
		}

		public void Fail(StringView err)
		{
			if (mFailed)
				return;

			mFailed = true;
			var errStr = scope String(err);
			if (mCurCmd != null)
			{
				if (mCurCmd.mFlags.HasFlag(.NoLines))
					errStr.AppendF(" in {}\n\t{}", mCurCmd.mSrcFile, mCurCmd.mCmd);
				else
					errStr.AppendF(" at line {} in {}\n\t{}", mCurCmd.mLineNum + 1, mCurCmd.mSrcFile, mCurCmd.mCmd);
			}

			if (mSoftFail)
				gApp.OutputErrorLine(errStr);
			else
				gApp.Fail(errStr);

			//TODO:
			//gApp.mRunningTestScript = false;
		}

		public bool IsErrorExpected(StringView err, bool remove = true)
		{
			if (mExpectingErrors == null)
				return false;
			for (let checkErr in mExpectingErrors)
			{
				if (err.Contains(checkErr))
				{
					if (remove)
					{
						delete checkErr;
						@checkErr.Remove();
						if (mExpectingErrors.IsEmpty)
							DeleteAndNullify!(mExpectingErrors);
					}
					return true;
				}
			}
			return false;
		}

		public void Fail(StringView fmt, params Object[] args)
		{
			Fail(scope String()..AppendF(fmt, params args));
		}

		public void Clear()
		{
			ClearAndDeleteItems(mCmdList);
			mFailed = false;
			mCurCmd = null;
		}

		public void QueueCommands(StreamReader streamReader, StringView filePath, CmdFlags flags)
		{
			int lineNum = 0;

			for (var lineResult in streamReader.Lines)
			Line:
			{
				switch (lineResult)
				{
				case .Ok(var line):
					line.Trim();
					if ((!line.IsEmpty) && (!line.StartsWith("#")))
					{
						QueuedCmd queuedCmd = new .();
						queuedCmd.mFlags = flags;
						queuedCmd.mSrcFile = new String(filePath);
						queuedCmd.mLineNum = lineNum;

						if (flags.HasFlag(.NoWait))
							queuedCmd.mNoWait = true; 

						if (line.StartsWith("nowait "))
						{
							queuedCmd.mNoWait = true;
							line.RemoveFromStart("no wait".Length);
						}

						if (line.StartsWith("if "))
						{
							int parenCount = 0;
							int strPos = 2;
							while (strPos < line.Length)
							{
								char8 c = line[strPos];
								if (c == '(')
								{
									parenCount++;
								}
								else if (c == ')')
								{
									parenCount--;
									if (parenCount == 0)
										break;
								}
								strPos++;
							}

							queuedCmd.mCondition = new String(line, 4, strPos - 4);
							line.RemoveFromStart(strPos + 1);
							line.Trim();
						}

						queuedCmd.mCmd = new String(line);
						mCmdList.Add(queuedCmd);
					}
					lineNum++;
				case .Err:
					Fail("Failed reading from file '{0}'", filePath);
				}
			}
		}

		public void QueueCommands(StringView cmds, StringView filePath, CmdFlags flags)
		{
			StringStream strStream = scope .(cmds, .Reference);
			StreamReader reader = scope .(strStream);
			QueueCommands(reader, filePath, flags); 
		}

		public void QueueCommandFile(StringView filePath)
		{
			let streamReader = scope StreamReader();
			if (streamReader.Open(filePath) case .Err)
			{
				Fail("Unable to open command file '{0}'", filePath);
				return;
			}

			QueueCommands(streamReader, filePath, .None);
		}

		public void SetTimeoutMS(int timeoutMS)
		{
			if (mTimeoutStopwatch == null)
			{
				mTimeoutStopwatch = new .();
				mTimeoutStopwatch.Start();
			}
			mTimeoutMS = timeoutMS;
		}

		public void Exec(StringView cmd)
		{
			var cmd;
			cmd.Trim();

			if ((cmd.StartsWith("#")) || (cmd.IsEmpty))
				return;

			if (cmd.StartsWith("%exec "))
			{
				mScriptHelper.ExecuteRaw(scope String(cmd, "%exec ".Length));
				return;
			}

			if (cmd.StartsWith("%targetComplete "))
			{
				let projectName = cmd.Substring("%targetComplete ".Length);
				
				if (gApp.mExecutionQueue.IsEmpty)
					return;

				bool matched = false;
				if (var targetCompleteCmd = gApp.mExecutionQueue[0] as IDEApp.TargetCompletedCmd)
				{
					if ((targetCompleteCmd.mProject.mProjectName == projectName) &&
						(!targetCompleteCmd.mIsReady))
					{
						targetCompleteCmd.mIsReady = true;
						matched = true;
					}
				}

				if (!matched)
				{
					mCurCmd.mHandled = false;
				}
				return;
			}

			if (mCurCmd.mExecIdx == 0)
			{
				if (mVerbosity >= .Normal)
				{
					gApp.OutputLine("Executing Command: {}", cmd);
					if (mVerbosity >= .Detailed)
					{
						mCurCmd.mStopWatch = new .(true);
					}
				}
			}

			StringView varName = .();

			/*int eqPos = cmdLineView.IndexOf('=');
			if (eqPos != -1)
			{
				varName = StringView(cmdLineView, eqPos);
				varName.Trim();
				cmdLineView.RemoveFromStart(eqPos + 1);
				cmdLineView.Clear();
			}*/

			StringView methodName;
			List<Object> args = scope .();

			int parenPos = cmd.IndexOf('(');
			if (parenPos != -1)
			{
				methodName = cmd.Substring(0, parenPos);
				methodName.Trim();

				int endParenPos = cmd.LastIndexOf(')');
				if (endParenPos == -1)
				{
					Fail("Missing argument end ')'");
					return;
				}

				var postStr = StringView(cmd, endParenPos + 1);
				postStr.Trim();
				if ((!postStr.IsEmpty) && (!postStr.StartsWith("#")))
				{
					Fail("Invalid string following command");
					return;
				}

				Workspace.Options workspaceOptions = null;
				Project project = null;
				Project.Options projectOptions = null;

				bool inQuotes = false;
				int startIdx = parenPos;
				for (int idx = parenPos; idx <= endParenPos; idx++)
				{
					char8 c = cmd[idx];
					if (c == '\\')
					{
						// Skip past slashed strings
						idx++;
						continue;
					}
					else if (c == '"')
					{
						inQuotes ^= true;
					}
					else if (((c == ',') || (c == ')')) && (!inQuotes))
					{
						StringView argView = cmd.Substring(startIdx + 1, idx - startIdx - 1);

						argView.Trim();
						if (argView.IsEmpty)
							continue;

						else if ((argView.StartsWith("\"")) || (argView.StartsWith("@\"")))
						{
							var str = scope:: String();

							if (argView.StartsWith("@"))
							{
								str.Append(argView, 2, argView.Length - 3);
							}
							else if (argView.UnQuoteString(str) case .Err)
								Fail("Failed to unquote string");

							if (str.Contains('$'))
							{
								if (workspaceOptions == null)
								{
									workspaceOptions = gApp.GetCurWorkspaceOptions();
									if (mCurCmd.mSrcFile?.StartsWith("project ") == true)
									{
										String projectName = scope String()..Append(mCurCmd.mSrcFile, "Project ".Length);
										project = gApp.mWorkspace.FindProject(projectName);
										if (project != null)
											projectOptions = gApp.GetCurProjectOptions(project);
									}
								}

								String newStr = scope:: .();
								String err = scope .();
								if (!gApp.DoResolveConfigString("", workspaceOptions, project, projectOptions, str, err, newStr))
								{
									Fail(scope String()..AppendF("Unknown macro string '{}' in '{}'", err, str));
								}
								str = newStr;
							}

							args.Add(str);
						}
						else if (argView.EndsWith('f'))
						{
							switch (float.Parse(argView))
							{
							case .Ok(let val):
								args.Add(scope:: box val);
							case .Err:
								Fail("Failed to parse float");
								return;
							}
						}
						else if (argView.Contains('.'))
						{
							switch (double.Parse(argView))
							{
							case .Ok(let val):
								args.Add(scope:: box val);
							case .Err:
								Fail("Failed to parse double");
								return;
							}
						}
						else if ((argView == "false") || (argView == "true"))
						{
							args.Add(scope:: box (argView == "true"));
						}
						else // Integer					   
						{
							switch (int.Parse(argView))
							{
							case .Ok(let val):
								args.Add(scope:: box val);
							case .Err:
								Fail("Failed to parse int");
								return;
							}
						}

						startIdx = idx;
					}
				}
				
				/*for (var argView in cmdLineView.Substring(parenPos + 1, endParenPos - parenPos - 1).Split(','))
				{
					HandleArg(argView);
				}*/
			}
			else
			{
				methodName = cmd;
			}

			if (mFailed)
				return;

			Target curTarget = mRoot;
			for (var cmdPart in methodName.Split('.'))
			{
				if (@cmdPart.HasMore)
				{
					if (!curTarget.mTargets.TryGetValue(scope String(cmdPart), out curTarget))
					{
						Fail("Unable to find target '{0}'", cmdPart);
						return;
					}
				}
				else
				{
					Target.Cmd cmd;
					if (!curTarget.mCmds.TryGetValue(scope String(cmdPart), out cmd))
					{
						Fail("Unable to find command '{0}'", cmdPart);
						return;
					}
					Object[] argsArr = scope Object[args.Count];
					args.CopyTo(argsArr);
					switch (cmd.mMethodInfo.Invoke(cmd.mTargetObject, params argsArr))
					{
					case .Err:
						Fail("Failed to invoke command");
						return;
					case .Ok(var result):
						if (!varName.IsEmpty)
						{
							String* keyPtr;
							Variant* valuePtr;
							if (mContext.mVars.TryAdd(scope String(varName), out keyPtr, out valuePtr))
								*keyPtr = new String(varName);
							else
								valuePtr.Dispose();
							*valuePtr = result;
						}
						else
							result.Dispose();
					}
				}
			}
		}

		public bool CheckCondition(StringView condition)
		{
			StringView curStr = .();
			BumpAllocator tempAlloc = scope .();

			StringView NextToken()
			{
				if (curStr.IsEmpty)
					return .();

				char8* idStart = null;
				while (true)
				{
					char8 c = 0;
					if (!curStr.IsEmpty)
					{
						c = curStr[0];
						curStr.RemoveFromStart(1);
					}
					if (c == ' ')
						continue;
					char8 nextC = 0;
					if (!curStr.IsEmpty)
						nextC = curStr[0];

					if (idStart != null)
					{
						if (*idStart == '"')
						{
							if (c == '"')
								return .(idStart, curStr.Ptr - idStart);
						}
						else if (!nextC.IsLetterOrDigit)
						{
							return .(idStart, curStr.Ptr - idStart);
						}
					}
					else
					{
						if (c == 0)
							return .();
						if ((c == '!') && (nextC == '='))
						{
							curStr.RemoveFromStart(1);
							return StringView(curStr.Ptr - 2, 2);
						}
						if ((c == '=') && (nextC == '='))
						{
							curStr.RemoveFromStart(1);
							return StringView(curStr.Ptr - 2, 2);
						}
						idStart = curStr.Ptr - 1;
					}
				}
			}

			Object Evaluate(StringView token)
			{
				if (token.StartsWith("\""))
					return new:tempAlloc String(token, 1, token.Length - 2);
				if (token == "platform")
					return gApp.mPlatformName;
				else if (token == "config")
					return gApp.mConfigName;
				else if (token == "optlevel")
				{
					var workspaceOptions = gApp.GetCurWorkspaceOptions();
					if (workspaceOptions != null)
					{
						String str = new:tempAlloc .();
						workspaceOptions.mBfOptimizationLevel.ToString(str);
						return str;
					}
				}
				return null;
			}

			bool Compare(Object obj, Object obj2)
			{
				if (obj is String)
				{
					return (String)obj == (String)obj2;
				}
				return obj == obj2;
			}

			Object Evaluate()
			{
				StringView tok = NextToken();
				if (tok.IsEmpty)
					return null;
				Object val = Evaluate(tok);
				if (val == null)
					return val;

				while (true)
				{
					StringView op = NextToken();
					if (op.IsEmpty)
						return val;

					StringView rhsToken = NextToken();
					Object rhs = Evaluate(rhsToken);
					if (rhs == null)
						return val;

					if (op == "!=")
						val = new:tempAlloc box !Compare(val, rhs);
					else if (op == "==")
						val = new:tempAlloc box Compare(val, rhs);
				}
			}

			curStr = condition;
			Object result = Evaluate();
			if (result is bool)
			{
				bool success = (bool)result;
				return success;
			}
			else
			{
				Fail("Invalid result from expression");
			}
			return false;
		}

		public void Cancel()
		{
			mCancelled = true;
			ClearAndDeleteItems(mCmdList);
		}

		public void Update()
		{
			if (mFailed)
				return;

			if ((mTimeoutMS > 0) && (gApp.mRunningTestScript))
			{
				if (mTimeoutStopwatch.ElapsedMilliseconds >= mTimeoutMS)
					Fail("Script has timed out: {:0.00}s", mTimeoutStopwatch.ElapsedMilliseconds / 1000.0f);
			}

			ScriptManager.sActiveManager = this;
			while ((!mCmdList.IsEmpty) && (!mFailed))
			{
				mCurCmd = mCmdList[0];
				mCurCmd.mHandled = true;
				if (!mCurCmd.mNoWait)
				{
					if (!mScriptHelper.IsPaused())
						break;
					// Only do a wait for the initial execution
					//  This is required for things like AssertEvalEquals that will be handled internally by repeated
					//  calls where 'mHandled = false' is set
					mCurCmd.mNoWait = true;
				}

				bool doExec = true;
				if (mCurCmd.mCondition != null)
					doExec = CheckCondition(mCurCmd.mCondition);
				if (doExec)
				{
					Exec(mCurCmd.mCmd);
					mCurCmd?.mExecIdx++;
				}

				if (mCmdList.IsEmpty)
					break;

				if (!mCurCmd.mHandled)
					break; // Try again next update

				if (mCurCmd.mStopWatch != null)
				{
					mCurCmd.mStopWatch.Stop();
					if (mCurCmd.mStopWatch.ElapsedMilliseconds > 10)
						gApp.OutputLine("Command Time: {:0.00}s", mCurCmd.mStopWatch.ElapsedMilliseconds / 1000.0f);
				}

				mCmdList.RemoveAt(0);
				delete mCurCmd;
				mCurCmd = null;
			}
			ScriptManager.sActiveManager = null;
		}
	}

	class ScriptHelper
	{
		public EditWidgetContent.LineAndColumn mMarkedPos;
		public ScriptManager mScriptManager;
		bool mIsFirstBreak = true;
		bool mWaitForExecutionPaused = true;

		public this(ScriptManager scriptManager)
		{
			mScriptManager = scriptManager;
		}

		void FixFilePath(String filePath, ProjectFolder folder)
		{
			for (var entry in folder.mChildItems)
			{
				if (var projectSource = entry as ProjectSource)
				{
					var fullPath = scope String();
					projectSource.GetFullImportPath(fullPath);
					if (fullPath.EndsWith(filePath, .OrdinalIgnoreCase))
					{
						if (filePath.Length != fullPath.Length)
						{
							let prevC = fullPath[fullPath.Length - filePath.Length - 1];
							if ((prevC != '\\') && (prevC != '/'))
								continue; // Not a full path match
						}

						filePath.Set(fullPath); // Matched!
					}
				}
				else if (var childFolder = entry as ProjectFolder)
				{
					FixFilePath(filePath, childFolder);
				}
			}
		}

		void FixSrcPath(String fileName, String outFilePath)
		{
			outFilePath.Append(fileName);
			IDEUtils.FixFilePath(outFilePath);

			if (File.Exists(outFilePath))
				return;

			if (!File.Exists(outFilePath))
			{
				for (var project in gApp.mWorkspace.mProjects)
				{
					FixFilePath(outFilePath, project.mRootFolder);
				}
			}

			if (!File.Exists(outFilePath))
			{
				mScriptManager.Fail("Unable to locate project file '{0}'", outFilePath);
			}
		}

		void FixFilePath(String fileName, String outFilePath)
		{
			outFilePath.Append(fileName);
			if (File.Exists(outFilePath))
				return;

			outFilePath.Clear();
			Path.GetAbsolutePath(fileName, gApp.mInstallDir, outFilePath);

			if (!File.Exists(outFilePath))
			{
				mScriptManager.Fail("Unable to locate file '{0}'", outFilePath);
			}
		}

		SourceViewPanel GetActiveSourceViewPanel()
		{
			var sourceViewPanel = gApp.GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
			{
				mScriptManager.Fail("No active source view panel");
				return null;
			}
			sourceViewPanel.EnsureReady();
			return sourceViewPanel;
		}

		TextPanel GetActiveTextPanel()
		{
			var textPanel = gApp.GetActivePanel() as TextPanel;
			if (textPanel == null)
				mScriptManager.Fail("No active text panel");
			return textPanel;
		}

		public bool Evaluate(String evalStr, String outVal, DebugManager.EvalExpressionFlags expressionFlags = .AllowCalls | .AllowSideEffects)
		{
			var curCmd = ScriptManager.sActiveManager.mCurCmd;

			if (curCmd.mIntParam == 1) // Pending
			{
				gApp.mDebugger.EvaluateContinue(outVal);
				if (outVal.StartsWith("!Not paused"))
				{
					curCmd.mHandled = false;
					return false;
				}
			}
			else
			{
				gApp.mDebugger.Evaluate(evalStr, outVal, -1, (int)-1, expressionFlags);
			}

			if (outVal.StartsWith("!pending"))
			{
				curCmd.mIntParam = 1;
				curCmd.mHandled = false;
				return false;
			}

			if (outVal.StartsWith("!"))
			{
				int tabPos = outVal.LastIndexOf('\t');
				if (tabPos != -1)
				{
					outVal.Remove(0, tabPos + 1);
					outVal.Insert(0, "ERROR:'");
					outVal.Append("'");
				}
				return true;
			}

			if (expressionFlags.HasFlag(.MemoryWatch))
				return true;

			int splitPos = outVal.IndexOf('\n');
			if (splitPos != -1)
				outVal.RemoveToEnd(splitPos);

			//int tabPos = outVal.IndexOf('\t');
			//splitPos = (int)Math.Min((uint)tabPos, (uint)splitPos);

			return true;
		}

		[IDECommand]
		public void SetWaitForExecutionPaused(bool value)
		{
			mWaitForExecutionPaused = value;
		}

		[IDECommand]
		public void OutputLine(Object obj)
		{
			gApp.OutputLine("SCRIPT: {0}", obj);
		}

		[IDECommand]
		public void Assert(bool val)
		{
			if (!val)
			{
				gApp.OutputLine("SCRIPT ASSERT FAILED");
			}
		}

		[IDECommand]
		public void Sleep(int length)
		{
			int wantTicks = length * gApp.RefreshRate / 1000;

			var curCmd = ScriptManager.sActiveManager.mCurCmd;
			if ((++curCmd.mIntParam <= wantTicks) || (length < 0)) // Negative is forever
				curCmd.mHandled = false;
		}

		[IDECommand]
		public void SleepTicks(int length)
		{
			int wantTicks = length;
			var curCmd = ScriptManager.sActiveManager.mCurCmd;
			if ((++curCmd.mIntParam <= wantTicks) || (length < 0)) // Negative is forever
				curCmd.mHandled = false;
		}

		public bool IsPaused()
		{
			/*if (gApp.mWatchPanel.mVisible)
			{
				bool hasPendingWatch = false;

				gApp.mWatchPanel.mListView.GetRoot().WithItems(scope [&] (item) =>
					{
						var watchListViewItem = (WatchListViewItem)item;
						if (!watchListViewItem.mVisible)
							return;
						if (watchListViewItem.mMustUpdateBeforeEvaluate)
							hasPendingWatch = true;
						if ((watchListViewItem.mWatchEntry != null) && (!watchListViewItem.mWatchEntry.mHasValue))
						{
							var parentWatchListViewItem = watchListViewItem.mParentItem as WatchListViewItem;
							if ((!watchListViewItem.mDisabled) || (parentWatchListViewItem == null) || (watchListViewItem.mWatchEntry.mIsNewExpression))
								hasPendingWatch = true;
						}
					});
				if (hasPendingWatch)
					return false;
			}*/

			if (!ScriptManager.sActiveManager.mIsBuildScript)
			{
				if (gApp.mLastActiveSourceViewPanel != null)
				{
					var sourceViewPanel = gApp.mLastActiveSourceViewPanel;
					if (sourceViewPanel.HasFocus())
					{
						if (sourceViewPanel.[Friend]mOldVerLoadExecutionInstance != null)
							return false;
						if (!sourceViewPanel.mDeferredResolveResults.IsEmpty)
							return false;

						if (sourceViewPanel.[Friend]mWantsFastClassify)
							return false;
						if (sourceViewPanel.[Friend]mWantsFullClassify)
							return false;
						if (sourceViewPanel.[Friend]mWantsFullRefresh) 
							return false;
					}
				}

				if ((gApp.mBfResolveCompiler != null) && (gApp.mBfResolveCompiler.IsPerformingBackgroundOperation()))
					return false;
				if (gApp.[Friend]mDeferredOpen != .None)
					return false;
				if ((gApp.mExecutionPaused) && (gApp.mDebugger.IsPaused()))
				{
					if (gApp.mWantsRehupCallstack)
						return false;
				}
			}

			if (gApp.mWantsClean || gApp.mWantsBeefClean)
				return false;

			if (gApp.IsCompiling)
			{
				if (!ScriptManager.sActiveManager.mIsBuildScript)
					return false;
			}

			if (!gApp.[Friend]mExecutionInstances.IsEmpty)
				return false;

			if (!gApp.[Friend]mExecutionQueue.IsEmpty)
			{
				var nextCmd = gApp.mExecutionQueue[0];
				if (!(nextCmd is IDEApp.ScriptCmd))
					return false;
			}

			if ((gApp.mDebugger == null) || (ScriptManager.sActiveManager.mIsBuildScript))
				return true;

			if ((!ScriptManager.sActiveManager.mIsBuildScript) && (gApp.AreTestsRunning()))
				return false;

			if (gApp.mDebugger.HasPendingDebugLoads())
				return false;

			if (mWaitForExecutionPaused)
			{
				if ((!gApp.mExecutionPaused) && (gApp.mDebugger.mIsRunning))
					return false;
			}

			var runState = gApp.mDebugger.GetRunState();
			if (runState == .Terminating)
			{
				return false;
			}

			if (runState == .SearchingSymSrv)
			{
				return false;
			}

			if (runState == .DebugEval)
			{
				return false;
			}

			if (runState == .Running_ToTempBreakpoint)
				return false;

			if ((!mWaitForExecutionPaused) && (runState == .Running))
			{

			}
			else
			{
				Debug.Assert((runState == .NotStarted) || (runState == .Paused) || (runState == .Running_ToTempBreakpoint) ||
					(runState == .Exception) || (runState == .Breakpoint) || (runState == .Terminated));
			}
			/*if (runState == .Paused)
			{
				NOP!();
			}
			else if ((runState == .Paused) || (runState == .Exception) || (runState == .Breakpoint))*/
			{
				if ((runState != .NotStarted) && (mIsFirstBreak))
				{
					//Debug.Assert((runState == .Breakpoint) || (gApp.IsCrashDump));
					mIsFirstBreak = false;
				}

				//TEMPORARY TEST:
				//Debug.Assert(runState == .Breakpoint);
				return true;
			}
		}

		[IDECommand]
		public void WaitForPaused()
		{
			var curCmd = ScriptManager.sActiveManager.mCurCmd;
			curCmd.mHandled = IsPaused();
		}

		[IDECommand]
		public void WaitForResolve()
		{
			var curCmd = ScriptManager.sActiveManager.mCurCmd;
			curCmd.mHandled = IsPaused() && (!gApp.mBfResolveCompiler.IsPerformingBackgroundOperation());
		}

		[IDECommand]
		public void OpenCrashDump(String fileName)
		{
			String filePath = scope String();
			FixFilePath(fileName, filePath);
			gApp.OpenCrashDump(filePath);
		}

		[IDECommand]
		public void SetSymSrvOptions(String symCacheDir, String symSrvStr, String flagsStr)
		{
			switch (Enum.Parse<DebugManager.SymSrvFlags>(flagsStr))
			{
			case .Ok(let flags):
				gApp.mDebugger.SetSymSrvOptions(symCacheDir, symSrvStr, flags);
			case .Err:
				mScriptManager.Fail("Failed to parse flags");
			}
		}

		[IDECommand]
		public void ShowFile(String fileName)
		{
			String filePath = scope String();
			FixSrcPath(fileName, filePath);

			String fixedFilePath = scope .();
			Path.GetAbsolutePath(filePath, gApp.mWorkspace.mDir, fixedFilePath);

			gApp.ShowSourceFile(fixedFilePath);
		}

		[IDECommand]
		public void DelTree(String dirPath)
		{
			if (Utils.DelTree(dirPath) case .Err)
			{
				mScriptManager.Fail(scope String()..AppendF("Failed to deltree '{}'", dirPath));
			}
		}

		[IDECommand]
		public void CreateFile(String path, String text)
		{
			let fileStream = scope FileStream();
			if (fileStream.Create(path) case .Err)
			{
				mScriptManager.Fail("Failed to create file '{}'", path);
				return;
			}
			fileStream.Write(text);
		}

		[IDECommand]
		public void RenameFile(String origPath, String newPath)
		{
			if (File.Move(origPath, newPath) case .Err)
			{
				mScriptManager.Fail("Failed to move file '{}' to '{}'", origPath, newPath);
			}
		}

		[IDECommand]
		public void DeleteFile(String path)
		{
			if (File.Delete(path) case .Err)
			{
				mScriptManager.Fail("Failed to delete file '{}'", path);
			}
		}

		[IDECommand]
		public void Echo(String str)
		{
			gApp.OutputLine(str);
		}

		[IDECommand]
		public void CopyFilesIfNewer(String srcPath, String destPath)
		{
			int copyCount = 0;
			int foundCount = 0;

			void Do(String srcPath, String destPath)
			{
				bool checkedDestDir = false;
				
				for (var entry in Directory.Enumerate(srcPath, .Directories | .Files))
				{
					foundCount++;

					if (mScriptManager.mFailed)
						return;

					String srcFilePath = scope .();
					entry.GetFilePath(srcFilePath);

					String srcFileName = scope .();
					entry.GetFileName(srcFileName);

					String destFilePath = scope .();
					Path.GetAbsolutePath(srcFileName, destPath, destFilePath);

					if (entry.IsDirectory)
					{
						srcFilePath.AppendF("{}*", Path.DirectorySeparatorChar);
						Do(srcFilePath, destFilePath);
						continue;
					}

					DateTime srcDate;
					if (!(File.GetLastWriteTime(srcFilePath) case .Ok(out srcDate)))
						continue;

					bool wantCopy = true;
					if (File.GetLastWriteTime(destFilePath) case .Ok(let destDate))
					{
						wantCopy = srcDate > destDate;
					}

					if (!wantCopy)
						continue;

					if (!checkedDestDir)
					{
						if (Directory.CreateDirectory(destPath) case .Err)
						{
							mScriptManager.Fail("Failed to create directory '{}'", destPath);
							return;
						}
					}

					if (File.Copy(srcFilePath, destFilePath) case .Err)
					{
						mScriptManager.Fail("Failed to copy '{}' to '{}'", srcFilePath, destFilePath);
						return;
					}

					copyCount++;
				}
			}

			Do(srcPath, destPath);

			if (foundCount == 0)
			{
				String srcDirPath = scope .();
				Path.GetDirectoryPath(srcPath, srcDirPath);
				if (!Directory.Exists(srcDirPath))
				{
					mScriptManager.Fail("Source directory does not exist: {}", srcDirPath);
				}
				else if ((!srcDirPath.Contains('*')) && (!srcDirPath.Contains('?')) && (!File.Exists(srcDirPath)))
				{
					mScriptManager.Fail("Source file does not exist: {}", srcPath);
				}
			}

			if ((!mScriptManager.mFailed) && (copyCount > 0) && (mScriptManager.mVerbosity >= .Normal))
			{
				if (mScriptManager.mCurCmd.mStopWatch != null)
				{
					mScriptManager.mCurCmd.mStopWatch.Stop();
					gApp.OutputLine("{} files copied from '{}' to '{}' in {:0.00}s", foundCount, srcPath, destPath, mScriptManager.mCurCmd.mStopWatch.ElapsedMilliseconds / 1000.0f);
					DeleteAndNullify!(mScriptManager.mCurCmd.mStopWatch);
				}
				else
					gApp.OutputLine("{} files copied from '{}' to '{}'", foundCount, srcPath, destPath);
			}
		}

		public Project GetProject()
		{
			if (!mScriptManager.mCurCmd.mSrcFile.StartsWith("project "))
			{
				mScriptManager.Fail("Only usable in the context of a project");
				return null;
			}
			let projectName = scope String()..Append(mScriptManager.mCurCmd.mSrcFile, "Project ".Length);
			let project = gApp.mWorkspace.FindProject(projectName);
			if (project == null)
			{
				mScriptManager.Fail("Unable to find project '{}'", projectName);
				return null;
			}
			return project;
		}

		[IDECommand]
		public void RemoveProject(String projectName)
		{
			let project = gApp.mWorkspace.FindProject(projectName);
			if (project == null)
			{
				mScriptManager.Fail("Unable to find project");
				return;
			}

			bool success = gApp.mProjectPanel.mProjectToListViewMap.TryGetValue(project.mRootFolder, var projectItem);
			if (!success)
			{
				mScriptManager.Fail("Unable to find project in panel");
				return;
			}

			gApp.mProjectPanel.mListView.GetRoot().SelectItemExclusively(projectItem);
			gApp.mProjectPanel.[Friend]RemoveSelectedItems(false);
		}

		[IDECommand]
		public void CopyToDependents(String srcPath)
		{
			let depProject = GetProject();
			if (depProject == null)
				return;

			List<Project> depProjectList = scope .();
			gApp.GetDependentProjectList(depProject, depProjectList);

			for (let checkProject in gApp.mWorkspace.mProjects)
			{
				if (checkProject.HasDependency(depProject.mProjectName))
				{
					List<String> targetPaths = scope .();
					defer ClearAndDeleteItems(targetPaths);

					let workspaceOptions = gApp.GetCurWorkspaceOptions();
					let options = gApp.GetCurProjectOptions(checkProject);
					gApp.[Friend]GetTargetPaths(checkProject, gApp.mPlatformName, workspaceOptions, options, targetPaths);

					if ((checkProject.mGeneralOptions.mTargetType == .BeefLib) && (options.mBuildOptions.mBuildKind != .DynamicLib))
						continue;

					if (!targetPaths.IsEmpty)
					{
						String targetDirPath = scope .();
						Path.GetDirectoryPath(targetPaths[0], targetDirPath);

						bool CopyFile(String srcPath)
						{
							String fileName = scope .();
							Path.GetFileName(srcPath, fileName);

							String destPath = scope .();
							Path.GetAbsolutePath(fileName, targetDirPath, destPath);

							if (File.CopyIfNewer(srcPath, destPath) case .Err)
							{
								mScriptManager.Fail("Failed to copy file '{}' to '{}'", srcPath, destPath);
								return false;
							}
							return true;
						}

						if (srcPath.Contains('*'))
						{
							String dirPath = scope .();
							String wildcard = scope .();
							Path.GetDirectoryPath(srcPath, dirPath);
							Path.GetFileName(srcPath, wildcard);

							for (let entry in Directory.EnumerateFiles(dirPath, wildcard))
							{
								String foundPath = scope .();
								entry.GetFilePath(foundPath);
								if (!CopyFile(foundPath))
									return;
							}
						}
						else
						{
							if (!CopyFile(srcPath))
								return;
						}
					}
				}
			}
		}

		[IDECommand]
		public void ExecuteRaw(String cmd)
		{
			var exePath = scope String();
			int spacePos;
			if (cmd.StartsWith("\""))
			{
				spacePos = cmd.IndexOf('"', 1) + 1;
				if (spacePos != -1)
					exePath.Append(cmd, 1, spacePos - 2);
			}
			else
			{
				spacePos = cmd.IndexOf(' ');
				if (spacePos != -1)
					exePath.Append(cmd, 0, spacePos);
			}

			if ((spacePos == -1) && (!cmd.IsEmpty))
			{
				mScriptManager.Fail("Invalid command '{0}' in '{1}'", cmd, mScriptManager.mCurCmd.mSrcFile);
				return;
			}

			if (spacePos > 0)
			{
				var exeArgs = scope String();
				exeArgs.Append(cmd, spacePos + 1);
				IDEApp.RunFlags runFlags = .None;
				if (!exePath.EndsWith(".exe", .OrdinalIgnoreCase))
					runFlags = .ShellCommand;

				// Hande re-encoded embedded newlines
				if (exeArgs.Contains('\v'))
				{
					exeArgs.Replace('\v', '\n');
					runFlags = .BatchCommand;
				}

				gApp.DoRun(exePath, exeArgs, gApp.mInstallDir, .None, null, null, runFlags);
			}
		}

		[IDECommand]
		public void Execute(String cmd)
		{
			ExecuteRaw(cmd);
		}

		[IDECommand]
		public void ExecuteCommandFile(String path)
		{
			mScriptManager.QueueCommandFile(path);
		}

		[IDECommand]
		public void SetFileWatcherDelay(int32 delay)
		{
			FileWatcher.sDbgFileCreateDelay = delay;
		}

		[IDECommand]
		public void RenameFile_TempRenameDelete(String origPath, String newPath)
		{
			String content = scope .();
			if (File.ReadAllText(origPath, content, true) case .Err)
			{
				mScriptManager.Fail("Failed to open file '{}'", origPath);
				return;
			}

			String tempPath = scope .();
			
			while (true)
			{
				tempPath.Clear();
				Path.GetDirectoryPath(origPath, tempPath);
				tempPath.Append(Path.DirectorySeparatorChar);
				tempPath.Append("_");
				Rand.Int().ToString(tempPath);
				tempPath.Append(".tmp");
				if (!File.Exists(tempPath))
					break;
			}

			FileStream tempStream = scope .();
			if (tempStream.Create(tempPath) case .Err)
			{
				mScriptManager.Fail("Failed to create temp file '{}'", tempPath);
				return;
			}
			tempStream.Write(content);
			tempStream.Close();

			if (File.Move(tempPath, newPath) case .Err)
			{
				mScriptManager.Fail("Failed to move file '{}' to '{}'", origPath, newPath);
				return;
			}

			if (File.Delete(origPath) case .Err)
			{
				mScriptManager.Fail("Failed to delete file '{}'", origPath);
				return;
			}
		}

		[IDECommand]
		public void OpenWorkspace(String dirPath)
		{
			gApp.[Friend]mDeferredOpen = .Workspace;
			var selectedPath = scope String()..AppendF(dirPath);
			selectedPath.Append(Path.DirectorySeparatorChar);
			selectedPath.Append("BeefSpace.toml");
			IDEUtils.FixFilePath(selectedPath);
			gApp.[Friend]mDeferredOpenFileName = new String(selectedPath);
		}

		[IDECommand]
		public void ClickPanelButton(String buttonName)
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			var panelHeader = sourceViewPanel.[Friend]mPanelHeader;
			if (panelHeader == null)
			{
				mScriptManager.Fail("No panel present");
				return;
			}

			for (var widget in panelHeader.mChildWidgets)
			{
				if (var button = widget as DarkButton)
				{
					if (button.Label == buttonName)
					{
						button.MouseClicked(0, 0, 0, 0, 0);
						return;
					}
				}
			}

			mScriptManager.Fail("Button '{0}' not found", buttonName);
		}

		[IDECommand]
		public void SelectLine(int lineNum)
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				sourceViewPanel.ShowFileLocation(-1, lineNum - 1, 0, .None);
			}
		}

		[IDECommand]
		public void AssertEvalEquals(String evalStr, String evalResult)
		{
			String outVal = scope String();
			if (!Evaluate(evalStr, outVal))
				return;

			if (outVal != evalResult)
			{
				mScriptManager.Fail("Assert failed: {0} == {1}", outVal, evalResult);
			}
		}

		[IDECommand]
		public void AssertEvalContains(String evalStr, String evalResult)
		{
			String outVal = scope String();
			if (!Evaluate(evalStr, outVal))
				return;

			if (!outVal.Contains(evalResult))
			{
				mScriptManager.Fail("Assert failed: {0} contains {1}", outVal, evalResult);
			}
		}

		[IDECommand]
		public void AssertTypeInfo(int compilerId, String typeName, String wantTypeInfo)
		{
			String typeInfo = scope String();
			var compiler = (compilerId == 0) ? gApp.mBfResolveCompiler : gApp.mBfBuildCompiler;
			var system = (compilerId == 0) ? gApp.mBfResolveSystem : gApp.mBfBuildSystem;
			system.Lock(0);
			compiler.GetTypeInfo(typeName, typeInfo);
			system.Unlock();

			if (typeInfo != wantTypeInfo)
				mScriptManager.Fail("Assert failed: {0} == {1}", typeInfo, wantTypeInfo);
		}

		[IDECommand]
		public void AddWatch(String evalStr)
		{
			gApp.mWatchPanel.AddWatchItem(evalStr);
		}

		[IDECommand]
		public void SelectWatch(String str)
		{
			UpdateWatches();

			int foundIdx = 0;
			gApp.mWatchPanel.mListView.GetRoot().WithItems(scope [?] (item) =>
				{
					if (item.mLabel == str)
					{
						if (foundIdx == 0)
							item.Focused = true;
						else
							item.Selected = true;
						foundIdx++;
					}
					else
						item.Selected = false;
				});
			if (foundIdx == 0)
				mScriptManager.Fail("Unable to find watch '{}'", str);
		}

		[IDECommand]
		public void FocusWatchDir(int dir)
		{
			if (dir < 0)
			{
				for (int idx < -dir)
					gApp.mWatchPanel.mListView.KeyDown(.Up, false);
			}
			else
			{
				for (int idx < dir)
					gApp.mWatchPanel.mListView.KeyDown(.Down, false);
			}
		}

		[IDECommand]
		public void AssertSelectedWatchEquals(String val)
		{
			UpdateWatches();

			int foundIdx = 0;
			gApp.mWatchPanel.mListView.GetRoot().WithItems(scope [?] (item) =>
				{
					let watchItem = (WatchListViewItem)item;
					if (watchItem.Selected)
					{
						foundIdx++;
						ForceWatchItem(watchItem);

						let valueWatchItem = (WatchListViewItem)watchItem.GetSubItem(1);
						if (valueWatchItem.Label != val)
							mScriptManager.Fail("Assert failed: {} == {}", valueWatchItem.Label, val);
					}
				});
			if (foundIdx == 0)
				mScriptManager.Fail("No watches selected");
		}

		[IDECommand]
		public void UpdateWatches()
		{
			gApp.mWatchPanel.CheckClearDirtyWatches();

			gApp.mWatchPanel.mListView.GetRoot().WithItems(scope [&] (item) =>
				{
					let watchItem = (WatchListViewItem)item;
					if (!watchItem.mVisible)
						return;
					ForceWatchItem(watchItem);
				});
		}

		void ForceWatchItem(WatchListViewItem item)
		{
			item.CalculatedDesiredHeight();

			if (item.mMustUpdateBeforeEvaluate)
				item.Update();

			if ((item.mWatchEntry != null) && (!item.mWatchEntry.mHasValue))
			{
				item.mWatchOwner.UpdateWatch(item);
			}
		}

		[IDECommand]
		public void OpenSelectedWatches()
		{
			gApp.mWatchPanel.mListView.GetRoot().WithItems(scope (item) =>
				{
					if (item.Selected)
					{
						let watchItem = (WatchListViewItem)item;
						ForceWatchItem(watchItem);
						item.Open(true, true);
						item.CalculatedDesiredHeight();

						watchItem.WithItems(scope (subItem) =>
							{
								var watchSubItem = (WatchListViewItem)subItem;
								if (watchSubItem.mMustUpdateBeforeEvaluate)
									watchSubItem.Update();
							});
					}
				});
		}

		[IDECommand]
		public void CloseSelectedWatches()
		{
			gApp.mWatchPanel.mListView.GetRoot().WithItems(scope (item) =>
				{
					if (item.Selected)
						item.Open(false, true);
				});
		}

		[IDECommand]
		public void ImmediateEvaluate(String evalStr)
		{
			gApp.ShowImmediatePanel();
			gApp.mImmediatePanel.[Friend]mImmediateWidget.mEditWidgetContent.InsertAtCursor(evalStr);
			gApp.mImmediatePanel.[Friend]mImmediateWidget.KeyChar('\n');
		}

		[IDECommand]
		public void SelectCallStackIdx(int selectIdx)
		{
			while (true)
			{
				int stackCount = gApp.mDebugger.GetCallStackCount();
				if (selectIdx < stackCount)
					break;
				gApp.mDebugger.UpdateCallStack();
				if (stackCount == gApp.mDebugger.GetCallStackCount())
				{
					mScriptManager.Fail("Stack idx '{0}' is out of range", selectIdx);
				}	
			}

			gApp.mDebugger.mActiveCallStackIdx = (.)selectIdx;
		}

		[IDECommand]
		public void SelectCallStackWithStr(String str)
		{
			int32 stackIdx = 0;
			while (true)
			{
				int stackCount = gApp.mDebugger.GetCallStackCount();
				if (stackIdx >= stackCount)
				{
					gApp.mDebugger.UpdateCallStack();
					if (stackIdx >= gApp.mDebugger.GetCallStackCount())
						break;
				}

				String file = scope .();
				String stackFrameInfo = scope .();
				gApp.mDebugger.GetStackFrameInfo(stackIdx, var addr, file, stackFrameInfo);
				if (stackFrameInfo.Contains(str))
				{
					gApp.mDebugger.mActiveCallStackIdx = (.)stackIdx;
					return;
				}
				stackIdx++;
			}

			mScriptManager.Fail("Failed to find stack frame containing string '{}'", str);
		}

		public bool AssertRunning()
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				mScriptManager.Fail("Expected target to be running");
				return false;
			}
			return true;
		}

		public bool AssertDebuggerPaused()
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				mScriptManager.Fail("Expected target to be running");
				return false;
			}

			if (!gApp.mDebugger.IsPaused())
			{
				mScriptManager.Fail("Expected target to be paused");
				return false;
			}

			return true;
		}

		[IDECommand]
		public void StepInto()
		{
			if (!AssertDebuggerPaused())
				return;
			gApp.[Friend]StepInto();
		}

		[IDECommand]
		public void StepOver()
		{
			if (!AssertDebuggerPaused())
				return;
			gApp.[Friend]StepOver();
		}

		[IDECommand]
		public void SelectThread(String threadName)
		{
			String threadInfo = scope .();
			gApp.mDebugger.GetThreadInfo(threadInfo);

			for (var infoLine in threadInfo.Split('\n'))
			{
				if (@infoLine.Pos == 0)
					continue;

				var infoSections = infoLine.Split('\t');
				StringView id = infoSections.GetNext().GetValueOrDefault();
				StringView name = infoSections.GetNext().GetValueOrDefault();

				if ((threadName.IsEmpty) || (threadName == name) || (threadName == id))
				{
					gApp.mDebugger.SetActiveThread(int32.Parse(id).GetValueOrDefault());
					break;
				}
			}
		}

		[IDECommand]
		public void AssertCurrentMethod(String methodName)
		{
			int addr;
			String fileName = scope String();
			String stackframeInfo = scope String();
			gApp.mDebugger.GetStackFrameInfo(gApp.mDebugger.mActiveCallStackIdx, out addr, fileName, stackframeInfo);

			if (methodName != stackframeInfo)
			{
				mScriptManager.Fail("Expect method name '{0}', got '{1}'", methodName, stackframeInfo);
			}
		}

		[IDECommand]
		public void CreateMemoryBreakpoint(String evalStr)
		{
			String outVal = scope String();
			if (!Evaluate(evalStr, outVal, .AllowCalls | .AllowSideEffects | .MemoryWatch))
				return;

			var vals = scope List<StringView>(outVal.Split('\n'));
			int64 addr = (int)int64.Parse(scope String(vals[0]), System.Globalization.NumberStyles.HexNumber);
			int32 byteCount = int32.Parse(scope String(vals[1]));
			String addrType = scope String(vals[2]);
#unwarn
			var breakpoint = gApp.mDebugger.CreateMemoryBreakpoint(evalStr, addr, byteCount, addrType);
		}

		[IDECommand]
		public void Crash()
		{
			int* a = null;
			*a = 123;
		}

		[IDECommand]
		public void Leak()
		{
			new String("This string leaked");
		}

		[IDECommand]
		public void BreakpointSetCondition(String condition)
		{
			var lastBreakpoint = gApp.mDebugger.mBreakpointList.Back;
			if (lastBreakpoint == null)
			{
				mScriptManager.Fail("No last breakpoint");
				return;
			}
			lastBreakpoint.SetCondition(condition);
		}

		[IDECommand]
		public void BreakpointSetHitCountTarget(int hitCountTarget, String hitCountBreakKindStr)
		{
			var lastBreakpoint = gApp.mDebugger.mBreakpointList.Back;
			if (lastBreakpoint == null)
			{
				mScriptManager.Fail("No last breakpoint");
				return;
			}
			switch (Enum.Parse<Breakpoint.HitCountBreakKind>(hitCountBreakKindStr))
			{
			case .Err:
				mScriptManager.Fail("Invalid break kind: '{0}'", hitCountBreakKindStr);
			case .Ok(let hitCountBreakKind):
				lastBreakpoint.SetHitCountTarget(hitCountTarget, hitCountBreakKind);
			}
		}

		[IDECommand]
		public void InsertImmediateText(String text)
		{
			gApp.ShowImmediatePanel();
			var ewc = (SourceEditWidgetContent)gApp.mImmediatePanel.EditWidget.mEditWidgetContent;

			ewc.InsertAtCursor(text);
			gApp.mImmediatePanel.Update();
			ewc.mOnGenerateAutocomplete(0, .UserRequested);
		}

		AutoComplete GetAutocomplete()
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
			{
				mScriptManager.Fail("No text panel active");
				return null;
			}

			var ewc = textPanel.EditWidget.mEditWidgetContent as SourceEditWidgetContent;
			if (ewc == null)
			{
				mScriptManager.Fail("Not an autocomplete text view");
				return null;
			}

			if (ewc.mAutoComplete == null)
			{
				mScriptManager.Fail("No autocomplete content");
				return null;
			}

			return ewc.mAutoComplete;
		}

		[IDECommand]
		public bool AssertAutocompleteEntry(StringView wantEntry)
		{
			var wantEntry;
			bool wantsFind = true;
			if (wantEntry.StartsWith("!"))
			{
				wantsFind = false;
				wantEntry.RemoveFromStart(1);
			}

			var autoComplete = GetAutocomplete();
			if (autoComplete == null)
			{
				return false;
			}

			bool found = false;
			if (autoComplete.mAutoCompleteListWidget != null)
			{
				for (var entry in autoComplete.mAutoCompleteListWidget.mEntryList)
				{
					if (entry.mEntryDisplay == wantEntry)
						found = true;
				}
			}

			if (found != wantsFind)
			{
				if (wantsFind)
					mScriptManager.Fail("Autocomplete entry '{0}' not found", wantEntry);
				else
					mScriptManager.Fail("Autocomplete entry '{0}' found, but it shouldn't have been", wantEntry);
				return false;
			}
			return true;
		}

		[IDECommand]
		public void AssertDbgAutocomplete(String text)
		{
			var lastActivePanel = gApp.mLastActivePanel;
			InsertImmediateText(text);
			for (let entry in text.Split('\n'))
			{
				if (!entry.IsEmpty)
				{
					if (!AssertAutocompleteEntry(entry))
						break;
				}
			}
			ClearImmediate();
			gApp.mLastActivePanel = lastActivePanel;
		}

		
		[IDECommand]
		public bool AssertAutocompleteEquals(String insertText, String wantsContents)
		{
			InsertImmediateText(insertText);
			var autoComplete = GetAutocomplete();
			if (autoComplete == null)
			{
				return false;
			}

			String contents = scope .();

			if (autoComplete.mAutoCompleteListWidget != null)
			{
				for (var entry in autoComplete.mAutoCompleteListWidget.mEntryList)
				{
					if (!contents.IsEmpty)
						contents.Append("\n");
					contents.Append(entry.mEntryDisplay);
				}
			}

			ClearImmediate();

			if (contents != wantsContents)
			{
				mScriptManager.Fail("Autocomplete not showing expected values. Expected '{}', got '{}'.", wantsContents, contents);
				return false;
			}
			return true;
		}

		[IDECommand]
		public void CloseAutocomplete()
		{
			var autoComplete = GetAutocomplete();
			if (autoComplete == null)
				return;
			autoComplete.Close();
		}

		[IDECommand]
		public void ClearImmediate()
		{
			gApp.mImmediatePanel.Clear();
		}

		[IDECommand]
		public void AssertIsAt(String fileName, int lineNum)
		{
			String filePath = scope String();
			FixSrcPath(fileName, filePath);

			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			if (!Path.Equals(filePath, sourceViewPanel.mFilePath))
			{
				mScriptManager.Fail("Expected source file '{0}', got '{1}'", filePath, sourceViewPanel.mFilePath);
				return;
			}

			let atLine = sourceViewPanel.mEditWidget.mEditWidgetContent.CursorLineAndColumn.mLine + 1;
			if (atLine != lineNum)
			{
				mScriptManager.Fail("Expected line '{0}', got '{1}'", lineNum, atLine);
				return;
			}
		}

		[IDECommand]
		public void AssertIsAtColumn(String fileName, int column)
		{
			String filePath = scope String();
			FixSrcPath(fileName, filePath);

			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			if (!Path.Equals(filePath, sourceViewPanel.mFilePath))
			{
				mScriptManager.Fail("Expected source file '{0}', got '{1}'", filePath, sourceViewPanel.mFilePath);
				return;
			}

			let atColumn = sourceViewPanel.mEditWidget.mEditWidgetContent.CursorLineAndColumn.mColumn + 1;
			if (atColumn != column)
			{
				mScriptManager.Fail("Expected column '{0}', got '{1}'", column, atColumn);
				return;
			}
		}

		[IDECommand]
		public void GotoTextSkip(String findText, int skipIdx)
		{
			var skipIdx;

			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
			{
				mScriptManager.Fail("No active text panel");
				return;
			}
			var ewc = textPanel.EditWidget.mEditWidgetContent;

			for (int32 startIdx < ewc.mData.mTextLength)
			{
			    bool isEqual = true;
			    for (int32 i = 0; i < findText.Length; i++)
			    {
			        if (ewc.mData.mText[i + startIdx].mChar != findText[i])
			        {
			            isEqual = false;
			            break;
			        }
			    }
			    if (isEqual)
			    {
					if (skipIdx > 0)
					{
						skipIdx--;
					}
					else
					{
				        ewc.CursorTextPos = startIdx;
				        return;
					}
			    }
			}

			mScriptManager.Fail("Unable to find text '{0}'", findText);
		}

		[IDECommand]
		public void GotoText(String findText)
		{
			GotoTextSkip(findText, 0);
		}

		[IDECommand]
		public void AssertLineContains(String findText)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
			int line = ewc.CursorLineAndColumn.mLine;

			var lineText = scope String();
			ewc.GetLineText(line, lineText);

			if (!lineText.Contains(findText))
			{
				mScriptManager.Fail("Lines does not contain text '{0}'", findText);
			}
		}

		[IDECommand]
		public void MoveCursor(int line, int column)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
			ewc.CursorLineAndColumn = .(line, column);
		}

		[IDECommand]
		public void AdjustCursor(int relColumn, int relLine)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
			var cursorPos = ewc.CursorLineAndColumn;
			cursorPos.mLine += (.)relLine;
			cursorPos.mColumn += (.)relColumn;
			ewc.CursorLineAndColumn = cursorPos;
		}

		[IDECommand]
		public void InsertText(String text)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;
			textPanel.EditWidget.mEditWidgetContent.InsertAtCursor(text);
		}

		[IDECommand]
		public void DeleteText()
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;
			textPanel.EditWidget.mEditWidgetContent.DeleteSelection();
		}

		[IDECommand]
		public void DeleteTextBackward(int count)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;
			for (int i < count)
				textPanel.EditWidget.mEditWidgetContent.Backspace();
		}

		[IDECommand]
		public void MarkPosition()
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
		 	mMarkedPos = ewc.CursorLineAndColumn;
		}

		[IDECommand]
		public void SelectToMark()
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
			var prevPos = ewc.CursorLineAndColumn;

			EditSelection sel;
			ewc.CursorLineAndColumn = mMarkedPos;
			sel.mStartPos = (.)ewc.CursorTextPos;
			ewc.CursorLineAndColumn = prevPos;
			sel.mEndPos = (.)ewc.CursorTextPos;
			ewc.mSelection = sel;
		}

		[IDECommand]
		public void RemoveSelection()
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel == null)
				return;

			var ewc = textPanel.EditWidget.mEditWidgetContent;
			ewc.mSelection = null;
		}

		[IDECommand]
		public void ToggleCommentBetween(String textFrom, String textTo)
		{
			GotoText(textFrom);
			AdjustCursor(1, 0);
			MarkPosition();
			GotoText(textTo);
			SelectToMark();
			gApp.[Friend]ToggleComment();
		}

		[IDECommand]
		public void AssertCompileSucceeded()
		{
			if (gApp.IsCompiling)
			{
				ScriptManager.sActiveManager.mCurCmd.mHandled = false;
				return;
			}
			
			if (gApp.mLastCompileFailed)
				mScriptManager.Fail("Compile failed");
		}

		[IDECommand]
		public void ToggleCommentAt(String textFrom)
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			var ewc = sourceViewPanel.mEditWidget.mEditWidgetContent;
			GotoText(textFrom);

			if (mScriptManager.Failed)
				return;


			char8 nextC = 0;

			int32 checkIdx = (.)ewc.CursorTextPos;
			while (checkIdx > 0)
			{
				if (ewc.mData.mText[checkIdx - 1].mDisplayTypeId != (.)SourceElementType.Comment)
					break;

				char8 c = ewc.mData.mText[checkIdx - 1].mChar;

				// If we have comments back-to-back then we need to do this...
				if ((c == '/') && (nextC == '*'))
				{
					checkIdx--;
					if (checkIdx > 1)
					{
						char8 prevC = ewc.mData.mText[checkIdx - 1].mChar;
						if (prevC == '/')
							checkIdx--;
					}
					break;
				}
				nextC = c;

				checkIdx--;
			}

			int32 startPos = checkIdx;
			if (ewc.mData.mText[startPos + 1].mChar == '*')
			{
				ewc.CursorTextPos = startPos;
				ewc.InsertAtCursor("/");

				char8 prevC = 0;

				int depth = 1;

				checkIdx = (.)ewc.CursorTextPos + 2;
				while (checkIdx < ewc.mData.mTextLength)
				{
					char8 c = ewc.mData.mText[checkIdx].mChar;
					if (ewc.mData.mText[checkIdx].mDisplayTypeId != (.)SourceElementType.Comment)
						break;

					// If we have comments back-to-back then we need to do this...
					if ((c == '/') && (prevC == '*'))
					{
						--depth;
						if (depth == 0)
						{
							checkIdx++;
							break;
						}
					}
					else if ((c == '*') && (prevC == '/'))
						depth++;

					prevC = c;
					checkIdx++;
				}
				ewc.CursorTextPos = checkIdx - 2;
				ewc.InsertAtCursor("/*@");
			}
			else
			{
				var text = ewc.mData.mText;

				ewc.mSelection = EditSelection(startPos, startPos + 1);
				ewc.DeleteSelection();

				checkIdx = (.)startPos;
				while (checkIdx < ewc.mData.mTextLength - 4)
				{
					if (text[checkIdx].mDisplayTypeId == (.)SourceElementType.Comment)
					{
						if ((text[checkIdx + 0].mChar == '/') &&
							(text[checkIdx + 1].mChar == '*') &&
							(text[checkIdx + 2].mChar == '@') &&
							(text[checkIdx + 3].mChar == '*') &&
							(text[checkIdx + 4].mChar == '/'))
						{
							ewc.mSelection = EditSelection(checkIdx, checkIdx + 3);
							ewc.DeleteSelection();
							break;
						}
					}
					checkIdx++;
				}
			}
		}

		[IDECommand]
		public void SetExpectError(String error)
		{
		
			if (ScriptManager.sActiveManager.mExpectingErrors == null)
				ScriptManager.sActiveManager.mExpectingErrors = new .();
			ScriptManager.sActiveManager.mExpectingErrors.Add(new String(error));
			ScriptManager.sActiveManager.mHadExpectingError = true;
		}

		[IDECommand]
		public void ClearExpectError()
		{
			if (ScriptManager.sActiveManager.mExpectingErrors != null)
			{
				DeleteContainerAndItems!(ScriptManager.sActiveManager.mExpectingErrors);
				ScriptManager.sActiveManager.mExpectingErrors = null;
			}
			ScriptManager.sActiveManager.mHadExpectingError = false;
		}

		[IDECommand]
		public void ExpectError()
		{
			if (ScriptManager.sActiveManager.mExpectingErrors != null)
			{
				DeleteContainerAndItems!(ScriptManager.sActiveManager.mExpectingErrors);
				ScriptManager.sActiveManager.mExpectingErrors = null;
				mScriptManager.Fail("Expected error did not occur");
			}
		}

		[IDECommand]
		public void AssertFileErrors()
		{
			var textPanel = GetActiveSourceViewPanel();
			if (textPanel == null)
			{
				mScriptManager.Fail("No active text panel");
				return;
			}

			List<BfPassInstance.BfError> errorList = scope .();
			bool hasErrors = false;
			BfPassInstance.BfError FindError(int line)
			{
				if (!hasErrors)
				{
					for (var error in gApp.mErrorsPanel.mResolveErrors)
					{
						if (error.mFilePath == textPanel.mFilePath)
						{
							errorList.Add(error);
							errorList.Sort(scope (lhs, rhs) => lhs.mLine - rhs.mLine);
						}
					}
				}

				int idx = errorList.BinarySearchAlt(line, scope (lhs, rhs) => lhs.mLine - line);
				if (idx >= 0)
					return errorList[idx];
				return null;
			}

			var ewc = textPanel.EditWidget.mEditWidgetContent;

			String lineText = scope String();
			for (int lineIdx = 0; lineIdx < ewc.GetLineCount(); lineIdx++)
			{
				lineText.Clear();
				ewc.GetLineText(lineIdx, lineText);

				ewc.GetLinePosition(lineIdx, var lineStart, var lineEnd);
				
				void FindError(bool warning)
				{
					bool hasError = false;
					for (int i = lineStart; i < lineEnd; i++)
					{
						var flags = (SourceElementFlags)ewc.mData.mText[i].mDisplayFlags;
						if (flags.HasFlag(warning ? .Warning : .Error))
							hasError = true;
					}

					String kind = warning ? "warning" : "error";
					int failIdx = lineText.IndexOf(warning ? "//WARN" : "//FAIL");
					bool expectedError = failIdx != -1;
					if (hasError == expectedError)
					{
						if (expectedError)
						{
							String wantsError = scope String(lineText, failIdx + "//FAIL".Length);
							wantsError.Trim();
							if (!wantsError.IsEmpty)
							{
								bool foundErrorText = false;
								if (var error = FindError(lineIdx))
								{
									if (error.mIsWarning == warning)
									{
										if (error.mError.Contains(wantsError))
											foundErrorText = true;
										if (error.mMoreInfo != null)
										{
											for (var moreInfo in error.mMoreInfo)
												if (moreInfo.mError.Contains(wantsError))
													foundErrorText = true;
										}
									}
								}
								if (!foundErrorText)
								{
									mScriptManager.Fail($"Line {lineIdx + 1} {kind} in {textPanel.mFilePath} did not contain {kind} text '{wantsError}'\n\t");
								}
							}
						}
					}
					else
					{
						if (hasError)
							mScriptManager.Fail($"Unexpected {kind} at line {lineIdx + 1} in {textPanel.mFilePath}\n\t");
						else
							mScriptManager.Fail($"Expected {kind} but didn't encounter one at line {lineIdx + 1} in {textPanel.mFilePath}\n\t");
						return;
					}
				}

				FindError(false);
				FindError(true);
			}
		}

		[IDECommand]
		public void Stop()
		{
			mScriptManager.Clear();
		}

		[IDECommand]
		public void Exit()
		{
			gApp.Stop();
		}

		[IDECommand]
		public void RestoreDebugFiles(String dbgPath)
		{
			for (var entry in Directory.EnumerateFiles(dbgPath))
			{
				String dbgFilePath = scope .();
				entry.GetFilePath(dbgFilePath);
				if (!dbgFilePath.EndsWith(".bf"))
					continue;

				String dbgContent = scope .();
				File.ReadAllText(dbgFilePath, dbgContent, false);
				if (!dbgContent.StartsWith("//@"))
					continue;
				int barPos = dbgContent.IndexOf('|');

				String srcPath = scope .();
				srcPath.Append(dbgContent, 3, barPos - 3);

				int crPos = dbgContent.IndexOf('\n');
				StringView dbgText = .(dbgContent.Ptr + crPos + 1, dbgContent.Length - crPos - 1);
				//let dbgHash = MD5.Hash(.((uint8*)dbgText.Ptr, dbgText.Length));

				String srcContent = scope .();
				File.ReadAllText(srcPath, srcContent, false);
				let srcHash = MD5.Hash(.((uint8*)srcContent.Ptr, srcContent.Length));

				//if (dbgHash != srcHash)
				{
					String bkuPath = scope .();
					bkuPath.Append(gApp.mInstallDir, "/bku/");
					Directory.CreateDirectory(bkuPath).IgnoreError();

					Path.GetFileNameWithoutExtension(dbgFilePath, bkuPath);
					bkuPath.Append("_");
					srcHash.ToString(bkuPath);
					Path.GetExtension(dbgFilePath, bkuPath);

					gApp.SafeWriteTextFile(bkuPath, srcContent);
					gApp.SafeWriteTextFile(srcPath, dbgText);
				}
			}
		}

		[IDECommand]
		public void RestoreDebugFilesSpan(String dbgPath, int fromIdx, int endIdx)
		{																		   
			for (int i = fromIdx; i <= endIdx; i++)
			{
				String versionedPath = scope .();
				versionedPath.AppendF("{}/{}/", dbgPath, i);
				RestoreDebugFiles(versionedPath);
			}
		}

		class DbgFileCtx
		{
			public String mFile ~ delete _;
			public int mIdx;
		}

		DbgFileCtx mDbgFileCtx ~ delete _;

		[IDECommand]
		public void StartDebugFiles(String dbgPath)
		{																		   
			DeleteAndNullify!(mDbgFileCtx);
			mDbgFileCtx = new DbgFileCtx();
			mDbgFileCtx.mFile = new String(dbgPath);
			ContinueDebugFiles();
		}

		[IDECommand]
		public void ContinueDebugFiles()
		{
			if (mDbgFileCtx == null)
			{
				mScriptManager.Fail("StartDebugFiles required");
				return;
			}

			gApp.OutputLine("Compiling debug files {0}", mDbgFileCtx.mIdx); 
			gApp.ShowOutput();
			RestoreDebugFilesSpan(mDbgFileCtx.mFile, mDbgFileCtx.mIdx, mDbgFileCtx.mIdx);
			mDbgFileCtx.mIdx++;
		}

		[IDECommand]
		public void WaitDialog()
		{
#if BF_PLATFORM_WINDOWS
			Windows.MessageBoxA((Windows.HWnd)0, "Waiting for user input", "Beef IDE", 0);
#endif
		}

		[IDECommand]
		public void UndoFill()
		{
			var documentPanel = gApp.GetLastActiveDocumentPanel();
			if (var sourceViewPanel = documentPanel as SourceViewPanel)
			{
				int count = 0;
				for (int i < 400)
				{
					for (char8 c = 'A'; c <= 'Z'; c++)
					{
						String str = scope .(32);
						if (count++ % 131 == 0)
							str.Append("\n//");
						str.Append(c);

						sourceViewPanel.mEditWidget.mEditWidgetContent.mData.mUndoManager.[Friend]mSkipNextMerge = true;
						sourceViewPanel.mEditWidget.mEditWidgetContent.InsertAtCursor(str);
					}
				}

			}
		}

		[IDECommand]
		public void SetVal(String valName, String value)
		{
			bool added = mScriptManager.mContext.mVars.TryAdd(valName, var keyPtr, var valuePtr);
			if (added)
				*keyPtr = new String(valName);
			else
				valuePtr.Dispose();
			*valuePtr = Variant.Create<String>(new String(value), true);
		}

		[IDECommand]
		public void ReadFile(String filePath, String valName)
		{
			String value = scope .();
			if (File.ReadAllText(filePath, value) case .Err)
			{
				mScriptManager.Fail(scope String()..AppendF("Failed to read file '{}'", filePath));
				return;
			}

			bool added = mScriptManager.mContext.mVars.TryAdd(valName, var keyPtr, var valuePtr);
			if (added)
				*keyPtr = new String(valName);
			else
				valuePtr.Dispose();
			*valuePtr = Variant.Create<String>(new String(value), true);
		}

		[IDECommand]
		public void AddProjectItem(String projectName, String folderPath, String filePath)
		{
			var project = gApp.FindProjectByName(projectName);
			if (project == null)
			{
				mScriptManager.Fail(scope String()..AppendF("Failed to find project '{}'", projectName));
				return;
			}

			ProjectFolder foundFolder = null;
			if (folderPath == "")
				foundFolder = project.mRootFolder;
			else
			{
				project.WithProjectItems(scope [&] (projectItem) =>
					{
						if (var projectFolder = projectItem as ProjectFolder)
						{
							var relDir = scope String();
							projectFolder.GetRelDir(relDir);
							if (Path.Equals(relDir, folderPath))
								foundFolder = projectFolder;
						}
					});
			}

			if (foundFolder == null)
			{
				mScriptManager.Fail(scope String()..AppendF("Failed to find project folder '{}'", folderPath));
				return;
			}

			IDEUtils.FixFilePath(filePath);
			gApp.mProjectPanel.ImportFiles(foundFolder, scope .(filePath));
		}
	}
}
