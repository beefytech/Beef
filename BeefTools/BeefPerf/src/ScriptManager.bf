using System;
using System.Collections.Generic;
using System.Reflection;
using System.IO;
using System.Diagnostics;
using Beefy;
using Beefy.widgets;
using Beefy.theme.dark;

namespace BeefPerf
{
	[AttributeUsage(.Method, .ReflectAttribute | .AlwaysIncludeTarget, ReflectUser=.All)]
	struct BpCommandAttribute : Attribute
	{

	}


	class ScriptManager
	{
		class Target
		{
			public class Cmd
			{
				public MethodInfo mMethodInfo;
				public Object mTargetObject;
			}

			public Dictionary<String, Target> mTargets = new .() ~ DeleteDictionaryAndKeysAndItems!(_);
			public Dictionary<String, Cmd> mCmds = new .() ~ DeleteDictionaryAndKeysAndItems!(_);
		}

		public class QueuedCmd
		{
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
		public bool mRunningCommand;

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
				for (var cmdPart in methodName.Split('_'))
				{
					if (@cmdPart.HasMore)
					{
						String* keyPtr;
						Target* targetPtr;
						if (curTarget.mTargets.TryAdd(scope String(cmdPart), out keyPtr, out targetPtr))
						{
							*keyPtr = new String(cmdPart);
							*targetPtr = new Target();
						}
						curTarget = *targetPtr;
					}
					else
					{
						String* keyPtr;
						Target.Cmd* cmdPtr;
						if (curTarget.mCmds.TryAdd(scope String(cmdPart), out keyPtr, out cmdPtr))
						{
							*keyPtr = new String(cmdPart);
							*cmdPtr = new .();

							let cmd = *cmdPtr;
							cmd.mMethodInfo = methodInfo;
							cmd.mTargetObject = targetObject;
						}
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
		}

		public void Fail(StringView fmt, params Object[] args)
		{
			Fail(scope String()..AppendF(fmt, params args));
		}

		public void Clear()
		{
			DeleteAndClearItems!(mCmdList);
		}

		public void QueueCommandFile(StringView filePath)
		{
			int lineNum = 0;

			let streamReader = scope StreamReader();
			if (streamReader.Open(filePath) case .Err)
			{
				Fail("Unable to open command file '{0}'", filePath);
				return;
			}

			for (var lineResult in streamReader.Lines)
			{
				switch (lineResult)
				{
				case .Ok(var line):
					line.Trim();
					if ((!line.IsEmpty) && (!line.StartsWith("#")))
					{
						QueuedCmd queuedCmd = new .();
						if (line.StartsWith("nowait "))
						{
							queuedCmd.mNoWait = true;
							line.RemoveFromStart("no wait".Length);
						}

						queuedCmd.mCmd = new String(line);
						queuedCmd.mSrcFile = new String(filePath);
						queuedCmd.mLineNum = lineNum;
						mCmdList.Add(queuedCmd);
					}
					lineNum++;
				case .Err:
					Fail("Failed reading from file '{0}'", filePath);
				}
			}
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

			bool inQuote = false;
			for (int i < cmdLineView.Length - 1)
			{
				char8 c = cmdLineView[i];
				if (!inQuote)
				{
					if (c == '"')
						inQuote = true;
					else if (c == ';')
					{
						Exec(.(cmdLineView, 0, i));
						cmdLineView.RemoveFromStart(i + 1);
						cmdLineView.Trim();
						i = -1;
						continue;
					}
				}
				else
				{
					if (c == '\\')
					{
						i++;
						continue;
					}
					if (c == '"')
					{
						inQuote = false;
						continue;
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

				bool isLiteralString = false;
				bool inQuotes = false;
				int startIdx = parenPos;
				for (int idx = parenPos; idx <= endParenPos; idx++)
				{
					char8 c = cmdLineView[idx];
					if ((c == '\\') && (!isLiteralString))
					{
						// Skip past slashed strings
						idx++;
						continue;
					}
					else if (c == '"')
					{
						if (!inQuotes)
						{
							isLiteralString = ((idx > 0) && (cmdLineView[idx - 1] == '@'));
						}
						inQuotes ^= true;
					}
					else if (((c == ',') || (c == ')')) && (!inQuotes))
					{
						StringView argView = cmdLineView.Substring(startIdx + 1, idx - startIdx - 1);

						argView.Trim();
						if (argView.IsEmpty)
							continue;

						if ((argView.StartsWith("\"")) || (isLiteralString))
						{
							var str = scope::String();
							if (argView.UnQuoteString(str) case .Err)
								Fail("Failed to unquote string");
							args.Add(str);
							isLiteralString = false;
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
					bool prevRunningCommand = mRunningCommand;
					mRunningCommand = true;
					defer { mRunningCommand = prevRunningCommand; }

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

		public void Update()
		{
			if (mFailed)
				return;

			/*if ((mTimeoutMS > 0) && (gApp.mRunningTestScript))
			{
				if (mTimeoutStopwatch.ElapsedMilliseconds >= mTimeoutMS)
					Fail("Script has timed out: {0}ms", mTimeoutStopwatch.ElapsedMilliseconds);
			}*/

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

		public bool IsPaused()
		{
			return true;
		}

		[BpCommand]
		public void Nop()
		{

		}

		[BpCommand]
		public void Close()
		{
			gApp.Stop();
		}

		[BpCommand]
		public void CloseIfAutoOpened()
		{
			if (gApp.mIsAutoOpened)
				gApp.Stop();
		}

		[BpCommand]
		public void SelectLastSession()
		{
			if (gApp.mSessions.IsEmpty)
			{
				gApp.Fail("No sessions recorded");
				return;
			}
			gApp.SetSession(gApp.mSessions.Back);
		}

		[BpCommand]
		public void SaveSession(String outPath)
		{
			if (gApp.mCurSession == null)
			{
				gApp.Fail("No session selected");
				return;
			}
			if (gApp.mCurSession.Save(outPath) case .Err)
				gApp.mScriptManager.Fail("Unable to save file '{0}'", outPath);
		}

		[BpCommand]
		public void SaveEntrySummary(String entryName, String outPath)
		{
			if (gApp.mCurSession == null)
			{
				gApp.Fail("No session selected");
				return;
			}
			gApp.mBoard.mPerfView.SaveEntrySummary(entryName, outPath);
		}

		[BpCommand]
		public void Stop()
		{
			gApp.mScriptManager.Clear();
		}

		[BpCommand]
		public void Exit()
		{
			gApp.Stop();
		}
	}
}
