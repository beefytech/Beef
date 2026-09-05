#if !CLI
using System;
using System.Collections;
using System.Diagnostics;
using System.IO;
using Beefy.mcp;
using Beefy.utils;
using Beefy.widgets;
using Beefy.theme.dark;
using IDE.ui;

namespace IDE
{
	// The tools every Beef-based IDE needs regardless of what it edits: what state the IDE is in,
	// the script escape hatch, and the waits that let a client sequence actions against an
	// application that does its real work over many frames. Domain tool sets (editor, build,
	// debugger, UI) build on these; a derived IDE adds its own through IDEApp.RegisterMCPTools.
	class IDECoreToolSet : MCPToolSet
	{
		// Scripts run through their own ScriptManager so they get the same queueing and idle gating
		// (compile finished, debugger settled) that -test scripts rely on, without touching the
		// test runner's manager. Soft-fail: a failing command is reported to the client rather
		// than as a dialog.
		public ScriptManager mScriptManager = new ScriptManager() ~ delete _;
		String mLastScriptError = new String() ~ delete _;

		public this()
		{
			mScriptManager.mSoftFail = true;
			mScriptManager.mOnFail.Add(new (err) => { mLastScriptError.Set(err); });
		}

		public override void Update()
		{
			mScriptManager.Update();
		}

		[MCPTool("status", "Current IDE state: the exe path and age (so you can tell which IDE build you are talking to), workspace and projects, config/platform, whether a build is running, whether the IDE is idle, debugger state, the active document and cursor, and any open dialogs. Call this first, and again whenever an action did not behave as expected -- an open dialog is the usual reason.")]
		void Status(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();

			String exePath = scope .();
			Environment.GetExecutableFilePath(exePath);
			sd.Add("exePath", exePath);
			if (File.GetLastWriteTime(exePath) case .Ok(let lastWrite))
				sd.Add("exeAgeSeconds", (DateTime.Now - lastWrite).TotalSeconds);
			sd.Add("mcpPort", mServer.mPort);
			sd.Add("frame", mServer.mFrameCount);
			sd.Add("deterministic", gApp.mDeterministic);
			sd.Add("runningTestScript", gApp.mRunningTestScript);

			using (sd.CreateObject("workspace"))
			{
				sd.Add("initialized", gApp.mWorkspace.IsInitialized);
				sd.Add("isDebugSession", gApp.mWorkspace.IsDebugSession);
				sd.Add("name", gApp.mWorkspace.mName ?? "");
				sd.Add("dir", gApp.mWorkspace.mDir ?? "");
				sd.Add("config", gApp.mConfigName);
				sd.Add("platform", gApp.mPlatformName);
				using (sd.CreateArray("projects"))
				{
					for (var project in gApp.mWorkspace.mProjects)
						sd.Add(project.mProjectName);
				}
			}

			sd.Add("compiling", gApp.IsCompiling);
			sd.Add("idle", mScriptManager.IsIdle());
			sd.Add("scriptRunning", mScriptManager.HasQueuedCommands);
			if (!mLastScriptError.IsEmpty)
				sd.Add("lastScriptError", mLastScriptError);
			if (mServer.mLastError != null)
			{
				sd.Add("runtimeErrorCount", mServer.mErrorCount);
				sd.Add("lastRuntimeError", mServer.mLastError);
			}

			using (sd.CreateObject("debugger"))
			{
				sd.Add("running", gApp.mDebugger.mIsRunning);
				sd.Add("paused", gApp.mExecutionPaused);
				String runState = scope .();
				gApp.mDebugger.GetRunState().ToString(runState);
				sd.Add("runState", runState);
			}

			var sourceViewPanel = gApp.GetActiveSourceViewPanel(true);
			if (sourceViewPanel != null)
			{
				using (sd.CreateObject("activeDocument"))
				{
					sd.Add("file", sourceViewPanel.mFilePath ?? "");
					var lineAndColumn = sourceViewPanel.mEditWidget.mEditWidgetContent.CursorLineAndColumn;
					sd.Add("line", lineAndColumn.mLine);
					sd.Add("column", lineAndColumn.mColumn);
					sd.Add("hasFocus", sourceViewPanel.HasFocus());
				}
			}

			using (sd.CreateArray("dialogs"))
			{
				for (var window in gApp.mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if (widgetWindow == null)
						continue;
					var dialog = widgetWindow.mRootWidget as Dialog;
					if (dialog == null)
						continue;

					using (sd.CreateObject())
					{
						sd.Add("title", dialog.mTitle ?? "");
						sd.Add("text", dialog.mText ?? "");
						using (sd.CreateArray("buttons"))
						{
							for (var button in dialog.mButtons)
							{
								if (var darkButton = button as DarkButton)
									sd.Add(darkButton.mLabel ?? "");
							}
						}
					}
				}
			}

			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		[MCPTool("run_script", "Run IDE script commands, one per line, in the same command language as the IDE's -test scripts: for example ShowFile(\"src/Program.bf\"), GotoText(\"main\"), MoveCursor(10, 0), InsertText(\"x\"), ToggleCommentAt(\"foo\"), StepOver(), AssertEvalEquals(\"a\", \"1\"), AddWatch(\"x\"). Commands are queued, and each waits for the IDE to be idle (compiles finished, debugger settled) before it runs. Returns when the queue drains or a command fails; the failure message names the command.")]
		void RunScript(MCPCall call,
			[MCPParam("Script text, one command per line. Lines starting with # are comments.", true)] String commands,
			[MCPParam("Timeout in milliseconds. Default 60000.")] int timeoutMS)
		{
			if (mScriptManager.HasQueuedCommands)
			{
				call.Error("A script is already running. Wait for status.scriptRunning to clear, then try again.");
				return;
			}

			mLastScriptError.Clear();
			mScriptManager.Clear();
			mScriptManager.QueueCommands(commands, "mcp", .None);

			call.Defer((timeoutMS > 0) ? timeoutMS : 60000, new (pollCall) => PollScript(pollCall));
		}

		bool PollScript(MCPCall call)
		{
			if (mScriptManager.Failed)
			{
				call.Error(mLastScriptError.IsEmpty ? "Script failed" : mLastScriptError);
				mScriptManager.Clear();
				return true;
			}

			if (call.mTimedOut)
			{
				int remaining = mScriptManager.[Friend]mCmdList.Count;
				mScriptManager.Clear();
				call.Error(scope $"Script timed out with {remaining} command(s) still queued; the rest were discarded. The IDE may be waiting on something -- check status for open dialogs or a running build.");
				return true;
			}

			if (mScriptManager.HasQueuedCommands)
				return false;

			call.Result("{\"ok\":true}");
			return true;
		}

		[MCPTool("wait_idle", "Wait until the IDE is idle: no compile running, background resolve finished, nothing queued for execution, and the debugger not mid-step. Returns idle true, or idle false if the timeout passes first. Use this after starting a build or a debugger step before reading results.")]
		void WaitIdle(MCPCall call,
			[MCPParam("Timeout in milliseconds. Default 30000.")] int timeoutMS)
		{
			call.Defer((timeoutMS > 0) ? timeoutMS : 30000, new (pollCall) =>
				{
					if (mScriptManager.IsIdle())
					{
						pollCall.Result("{\"idle\":true}");
						return true;
					}
					if (pollCall.mTimedOut)
					{
						pollCall.Result("{\"idle\":false}");
						return true;
					}
					return false;
				});
		}

		[MCPTool("wait_frames", "Wait for the IDE to run a number of update frames, so UI that settles over frames (tooltips, animations, deferred layout, autocomplete) has happened before you read or capture it.")]
		void WaitFrames(MCPCall call,
			[MCPParam("Number of frames to wait. Default 1.")] int frames)
		{
			int targetFrame = mServer.mFrameCount + Math.Max(frames, 1);
			call.Defer(30000, new (pollCall) =>
				{
					if (mServer.mFrameCount < targetFrame)
						return false;
					pollCall.Result(scope $"{{\"frame\":{mServer.mFrameCount}}}");
					return true;
				});
		}

		[MCPTool("mcp_self_test", "Diagnostic: deliberately trip a Beef assert inside the IDE to confirm the MCP error catcher works. A healthy setup reports the assert in this call's result and in status.lastRuntimeError, and the IDE keeps running; the same path is what turns a real assert during any tool into a report instead of a crash dialog.")]
		void SelfTest(MCPCall call)
		{
			Runtime.Assert(false, "MCP self-test assert");
			call.Result("{\"survived\":true}");
		}

		[MCPTool("get_output", "Read the Output panel text, where builds, the debugger and the IDE itself log. Pass the 'next' value from a previous call as 'from' to read only what was appended since, so a build or debug session can be followed incrementally.")]
		void GetOutput(MCPCall call,
			[MCPParam("Character offset to read from. 0 or omitted reads everything.")] int from)
		{
			String text = scope .();
			gApp.mOutputPanel.mOutputWidget.GetText(text);
			int start = Math.Clamp(from, 0, text.Length);

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("text", StringView(text, start));
			sd.Add("next", text.Length);
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}
	}
}
#endif
