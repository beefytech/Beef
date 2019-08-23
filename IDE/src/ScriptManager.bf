using System;
using System.Collections.Generic;
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

namespace IDE
{
	class ScriptManager
	{
		class Target
		{
			public class Cmd
			{
				public MethodInfo mMethodInfo;
				public Object mTargetObject;
			}

			public Dictionary<String, Target> mTargets = new .() ~ DeleteDictionyAndKeysAndItems!(_);
			public Dictionary<String, Cmd> mCmds = new .() ~ DeleteDictionyAndKeysAndItems!(_);
		}

		public class QueuedCmd
		{
			public String mCondition ~ delete _;

			public bool mHandled = true;
			public String mCmd ~ delete _;
			public String mSrcFile ~ delete _;
			public int mLineNum = -1;

			public int mIntParam;
			public bool mNoWait;
		}

		ScriptHelper mScriptHelper = new ScriptHelper() ~ delete _;
		Target mRoot = new Target() ~ delete _;
		Dictionary<String, Variant> mVars = new .() ~
			{
				for (var kv in _)
				{
					delete kv.key;
					kv.value.Dispose();
				}
				delete _;
			};
		List<QueuedCmd> mCmdList = new .() ~ DeleteContainerAndItems!(_);
		bool mFailed;
		public QueuedCmd mCurCmd;
		public Stopwatch mTimeoutStopwatch ~ delete _;
		public int mTimeoutMS;
		public String mExpectingError ~ delete _;
		public bool mHadExpectingError;
		public int mDoneTicks;

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

		public this()
		{
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
				errStr.AppendF(" at line {0} in {1}\n\t{2}", mCurCmd.mLineNum + 1, mCurCmd.mSrcFile, mCurCmd.mCmd);
			}
			gApp.Fail(errStr);

			//TODO:
			//gApp.mRunningTestScript = false;
		}

		public void Fail(StringView fmt, params Object[] args)
		{
			Fail(scope String()..AppendF(fmt, params args));
		}

		public void Clear()
		{
			DeleteAndClearItems!(mCmdList);
			mFailed = false;
		}

		public void QueueCommands(StreamReader streamReader, StringView filePath)
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
						queuedCmd.mSrcFile = new String(filePath);
						queuedCmd.mLineNum = lineNum;

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

		public void QueueCommandFile(StringView filePath)
		{
			let streamReader = scope StreamReader();
			if (streamReader.Open(filePath) case .Err)
			{
				Fail("Unable to open command file '{0}'", filePath);
				return;
			}

			QueueCommands(streamReader, filePath);
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

		public void Exec(StringView cmdLineView)
		{
			var cmdLineView;
			cmdLineView.Trim();

			if ((cmdLineView.StartsWith("#")) || (cmdLineView.IsEmpty))
				return;

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

			int parenPos = cmdLineView.IndexOf('(');
			if (parenPos != -1)
			{
				methodName = cmdLineView.Substring(0, parenPos);
				methodName.Trim();

				int endParenPos = cmdLineView.LastIndexOf(')');
				if (endParenPos == -1)
				{
					Fail("Missing argument end ')'");
					return;
				}

				var postStr = StringView(cmdLineView, endParenPos + 1);
				postStr.Trim();
				if ((!postStr.IsEmpty) && (!postStr.StartsWith("#")))
				{
					Fail("Invalid string following command");
					return;
				}

				bool inQuotes = false;
				int startIdx = parenPos;
				for (int idx = parenPos; idx <= endParenPos; idx++)
				{
					char8 c = cmdLineView[idx];
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
						StringView argView = cmdLineView.Substring(startIdx + 1, idx - startIdx - 1);

						argView.Trim();
						if (argView.IsEmpty)
							continue;

						if (argView.StartsWith("\""))
						{
							var str = scope:: String();
							if (argView.UnQuoteString(str) case .Err)
								Fail("Failed to unquote string");

							if (str.Contains('$'))
							{
								String newStr = scope:: .();
								String err = scope .();
								if (!gApp.DoResolveConfigString(null, null, null, str, err, newStr))
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
				methodName = cmdLineView;
			}

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
							if (mVars.TryAdd(scope String(varName), out keyPtr, out valuePtr))
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

		public void Update()
		{
			if (mFailed)
				return;

			if ((mTimeoutMS > 0) && (gApp.mRunningTestScript))
			{
				if (mTimeoutStopwatch.ElapsedMilliseconds >= mTimeoutMS)
					Fail("Script has timed out: {0}ms", mTimeoutStopwatch.ElapsedMilliseconds);
			}

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
					Exec(mCurCmd.mCmd);

				if (mCmdList.IsEmpty)
					break;

				if (!mCurCmd.mHandled)
					break; // Try again next update

				mCmdList.RemoveAt(0);
				delete mCurCmd;
				mCurCmd = null;
			}
		}
	}

	class ScriptHelper
	{
		public EditWidgetContent.LineAndColumn mMarkedPos;

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
				gApp.mScriptManager.Fail("Unable to locate project file '{0}'", outFilePath);
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
				gApp.mScriptManager.Fail("Unable to locate file '{0}'", outFilePath);
			}
		}

		SourceViewPanel GetActiveSourceViewPanel()
		{
			var sourceViewPanel = gApp.GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
			{
				gApp.mScriptManager.Fail("No active source view panel");
				return null;
			}
			sourceViewPanel.EnsureReady();
			return sourceViewPanel;
		}

		TextPanel GetActiveTextPanel()
		{
			var textPanel = gApp.GetActivePanel() as TextPanel;
			if (textPanel == null)
				gApp.mScriptManager.Fail("No active text panel");
			return textPanel;
		}

		public bool Evaluate(String evalStr, String outVal, DebugManager.EvalExpressionFlags expressionFlags = .AllowCalls | .AllowSideEffects)
		{
			var curCmd = gApp.mScriptManager.mCurCmd;

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

			var curCmd = gApp.mScriptManager.mCurCmd;
			if ((++curCmd.mIntParam <= wantTicks) || (length < 0)) // Negative is forever
				curCmd.mHandled = false;
		}

		[IDECommand]
		public void SleepTicks(int length)
		{
			int wantTicks = length;
			var curCmd = gApp.mScriptManager.mCurCmd;
			if ((++curCmd.mIntParam <= wantTicks) || (length < 0)) // Negative is forever
				curCmd.mHandled = false;
		}

		bool mIsFirstBreak = true;

		public bool IsPaused()
		{
			if (gApp.mLastActiveSourceViewPanel != null)
			{
				var sourceViewPanel = gApp.mLastActiveSourceViewPanel;

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

			if (gApp.mBfResolveCompiler.IsPerformingBackgroundOperation())
				return false;
			if (gApp.[Friend]mDeferredOpen != .None)
				return false;
			if (gApp.mWantsRehupCallstack)
				return false;
			if (gApp.mWantsClean || gApp.mWantsBeefClean)
				return false;

			if ((!gApp.IsCompiling) && (!gApp.AreTestsRunning()) && (!gApp.mDebugger.HasPendingDebugLoads()) &&
				((gApp.mExecutionPaused) || (!gApp.mDebugger.mIsRunning)))
			{
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

				Debug.Assert((runState == .NotStarted) || (runState == .Paused) || (runState == .Running_ToTempBreakpoint) ||
					(runState == .Exception) || (runState == .Breakpoint) || (runState == .Terminated));
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

			return false;
		}

		[IDECommand]
		public void WaitForPaused()
		{
			var curCmd = gApp.mScriptManager.mCurCmd;
			curCmd.mHandled = IsPaused();
		}

		[IDECommand]
		public void WaitForResolve()
		{
			var curCmd = gApp.mScriptManager.mCurCmd;
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
				gApp.mScriptManager.Fail("Failed to parse flags");
			}
		}

		[IDECommand]
		public void ShowFile(String fileName)
		{
			String filePath = scope String();
			FixSrcPath(fileName, filePath);

			gApp.ShowSourceFile(filePath);
		}

		[IDECommand]
		public void DelTree(String dirPath)
		{
			if (Utils.DelTree(dirPath) case .Err)
			{
				gApp.mScriptManager.Fail(scope String()..AppendF("Failed to deltree '{}'", dirPath));
			}
		}

		[IDECommand]
		public void CreateFile(String path, String text)
		{
			let fileStream = scope FileStream();
			if (fileStream.Create(path) case .Err)
			{
				gApp.mScriptManager.Fail("Failed to create file '{}'", path);
				return;
			}
			fileStream.Write(text);
		}

		[IDECommand]
		public void RenameFile(String origPath, String newPath)
		{
			if (File.Move(origPath, newPath) case .Err)
			{
				gApp.mScriptManager.Fail("Failed to move file '{}' to '{}'", origPath, newPath);
			}
		}

		[IDECommand]
		public void DeleteFile(String path)
		{
			if (File.Delete(path) case .Err)
			{
				gApp.mScriptManager.Fail("Failed to delete file '{}'", path);
			}
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
				gApp.mScriptManager.Fail("Failed to open file '{}'", origPath);
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
				gApp.mScriptManager.Fail("Failed to create temp file '{}'", tempPath);
				return;
			}
			tempStream.Write(content);
			tempStream.Close();

			if (File.Move(tempPath, newPath) case .Err)
			{
				gApp.mScriptManager.Fail("Failed to move file '{}' to '{}'", origPath, newPath);
				return;
			}

			if (File.Delete(origPath) case .Err)
			{
				gApp.mScriptManager.Fail("Failed to delete file '{}'", origPath);
				return;
			}
		}

		[IDECommand]
		public void OpenWorkspace(String dirPath)
		{
			gApp.[Friend]mDeferredOpen = .Workspace;
			var selectedPath = scope String..AppendF(dirPath);
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
				gApp.mScriptManager.Fail("No panel present");
				return;
			}

			for (var widget in panelHeader.mChildWidgets)
			{
				if (var button = widget as DarkButton)
				{
					if (button.Label == buttonName)
					{
						button.MouseClicked(0, 0, 0);
						return;
					}
				}
			}

			gApp.mScriptManager.Fail("Button '{0}' not found", buttonName);
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
				gApp.mScriptManager.Fail("Assert failed: {0} == {1}", outVal, evalResult);
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
				gApp.mScriptManager.Fail("Assert failed: {0} contains {1}", outVal, evalResult);
			}
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
					gApp.mScriptManager.Fail("Stack idx '{0}' is out of range", selectIdx);
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

			gApp.mScriptManager.Fail("Failed to find stack frame containing string '{}'", str);
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
				gApp.mScriptManager.Fail("Expect method name '{0}', got '{1}'", methodName, stackframeInfo);
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
				gApp.mScriptManager.Fail("No last breakpoint");
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
				gApp.mScriptManager.Fail("No last breakpoint");
				return;
			}
			switch (Enum.Parse<Breakpoint.HitCountBreakKind>(hitCountBreakKindStr))
			{
			case .Err:
				gApp.mScriptManager.Fail("Invalid break kind: '{0}'", hitCountBreakKindStr);
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
				gApp.mScriptManager.Fail("No text panel active");
				return null;
			}

			var ewc = textPanel.EditWidget.mEditWidgetContent as SourceEditWidgetContent;
			if (ewc == null)
			{
				gApp.mScriptManager.Fail("Not an autocomplete text view");
				return null;
			}

			if (ewc.mAutoComplete == null)
			{
				gApp.mScriptManager.Fail("No autocomplete content");
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
					gApp.mScriptManager.Fail("Autocomplete entry '{0}' not found", wantEntry);
				else
					gApp.mScriptManager.Fail("Autocomplete entry '{0}' found, but it shouldn't have been", wantEntry);
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
				gApp.mScriptManager.Fail("Autocomplete not showing expected values. Expected '{}', got '{}'.", wantsContents, contents);
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
				gApp.mScriptManager.Fail("Expected source file '{0}', got '{1}'", filePath, sourceViewPanel.mFilePath);
				return;
			}

			let atLine = sourceViewPanel.mEditWidget.mEditWidgetContent.CursorLineAndColumn.mLine + 1;
			if (atLine != lineNum)
			{
				gApp.mScriptManager.Fail("Expected line '{0}', got '{1}'", lineNum, atLine);
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
				gApp.mScriptManager.Fail("No active text panel");
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

			gApp.mScriptManager.Fail("Unable to find text '{0}'", findText);
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
				gApp.mScriptManager.Fail("Lines does not contain text '{0}'", findText);
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
				gApp.mScriptManager.mCurCmd.mHandled = false;
				return;
			}
			
			if (gApp.mLastCompileFailed)
				gApp.mScriptManager.Fail("Compile failed");
		}

		[IDECommand]
		public void ToggleCommentAt(String textFrom)
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			var ewc = sourceViewPanel.mEditWidget.mEditWidgetContent;
			GotoText(textFrom);

			if (gApp.mScriptManager.Failed)
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
			DeleteAndNullify!(gApp.mScriptManager.mExpectingError);
			gApp.mScriptManager.mExpectingError = new String(error);
			gApp.mScriptManager.mHadExpectingError = true;
		}

		[IDECommand]
		public void ExpectError()
		{
			if (gApp.mScriptManager.mExpectingError != null)
			{
				DeleteAndNullify!(gApp.mScriptManager.mExpectingError);
				gApp.mScriptManager.Fail("Expected error did not occur");
			}
		}

		[IDECommand]
		public void AssertFileErrors()
		{
			var textPanel = GetActiveSourceViewPanel();
			if (textPanel == null)
			{
				gApp.mScriptManager.Fail("No active text panel");
				return;
			}
			var ewc = textPanel.EditWidget.mEditWidgetContent;

			String lineText = scope String();
			for (int lineIdx = 0; lineIdx < ewc.GetLineCount(); lineIdx++)
			{
				lineText.Clear();
				ewc.GetLineText(lineIdx, lineText);

				ewc.GetLinePosition(lineIdx, var lineStart, var lineEnd);
				bool hasError = false;
				for (int i = lineStart; i < lineEnd; i++)
				{
					var flags = (SourceElementFlags)ewc.mData.mText[i].mDisplayFlags;
					if (flags.HasFlag(.Error))
						hasError = true;
				}

				bool expectedError = lineText.Contains("//FAIL");
				if (hasError != expectedError)
				{
					if (hasError)
						gApp.mScriptManager.Fail("Unexpected error at line {0} in {1}\n\t", lineIdx + 1, textPanel.mFilePath);
					else
						gApp.mScriptManager.Fail("Expected error at line {0} in {1} but didn't encounter one\n\t", lineIdx + 1, textPanel.mFilePath);
					return;
				}
			}
		}

		[IDECommand]
		public void Stop()
		{
			gApp.mScriptManager.Clear();
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
				let dbgHash = MD5.Hash(.((uint8*)dbgText.Ptr, dbgText.Length));

				String srcContent = scope .();
				File.ReadAllText(srcPath, srcContent, false);
				let srcHash = MD5.Hash(.((uint8*)srcContent.Ptr, srcContent.Length));

				if (dbgHash != srcHash)
				{
					String bkuPath = scope .();
					bkuPath.Append(gApp.mInstallDir, "/bku/");
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

		[IDECommand]
		public void WaitDialog()
		{
#if BF_PLATFORM_WINDOWS
			Windows.MessageBoxA((Windows.HWnd)0, "Waiting for user input", "Beef IDE", 0);
#endif
		}
	}
}
