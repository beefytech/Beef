#if !CLI
using System;
using System.Collections;
using Beefy.mcp;
using Beefy.utils;
using IDE.ui;

namespace IDE
{
	// Building. The build itself is the IDE's own Build Workspace command; what this adds is
	// completion -- the reply arrives when the build has finished -- and the result: whether it
	// succeeded and what it wrote to the Output panel.
	class IDEBuildToolSet : MCPToolSet
	{
		public static int OutputLength()
		{
			return gApp.mOutputPanel.mOutputWidget.mEditWidgetContent.mData.mTextLength;
		}

		// The Output panel text appended since startLen, capped at the tail so a chatty build does
		// not swamp the reply; outputNext lets get_output pick up where this left off
		public static void AppendOutputSince(StructuredData sd, int startLen, int maxChars = 12000)
		{
			String text = scope .();
			gApp.mOutputPanel.mOutputWidget.GetText(text);
			// The IDE clears the Output panel when a build starts, so a shorter text means
			// everything in it is new
			int start = (text.Length < startLen) ? 0 : startLen;
			StringView appended = StringView(text, start);
			if (appended.Length > maxChars)
			{
				appended = StringView(appended, appended.Length - maxChars);
				sd.Add("outputTruncated", true);
			}
			sd.Add("output", appended);
			sd.Add("outputNext", text.Length);
		}

		// One build being waited on
		class BuildWait
		{
			public int mStartOutputLen;
			public bool mStarted;
			public int mFrames;

			public bool Poll(MCPCall call)
			{
				mFrames++;
				if (gApp.IsCompiling)
					mStarted = true;

				bool finished = (mStarted) && (!gApp.IsCompiling);
				// A build that never got going (nothing to build, a dialog, no workspace) would
				// otherwise leave us waiting for the whole timeout
				if ((!mStarted) && (mFrames >= 30))
					finished = true;
				if ((!finished) && (!call.mTimedOut))
					return false;

				var sd = scope StructuredData();
				sd.CreateNew();
				sd.Add("started", mStarted);
				if (call.mTimedOut)
				{
					sd.Add("timedOut", true);
					sd.Add("stillCompiling", gApp.IsCompiling);
				}
				else if (mStarted)
					sd.Add("succeeded", !gApp.mLastCompileFailed);
				else
					sd.Add("note", "No build started. Check get_dialogs for a prompt, and status for whether a workspace is open.");
				sd.Add("errorCount", gApp.mErrorsPanel.mErrorCount);
				sd.Add("warningCount", gApp.mErrorsPanel.mWarningCount);
				AppendOutputSince(sd, mStartOutputLen);

				String json = scope .();
				sd.ToJSON(json);
				call.Result(json);
				return true;
			}
		}

		[MCPTool("build", "Build the workspace (the Build Workspace command) and wait for it to finish. Returns whether it succeeded, the Output panel text the build produced, and the current error and warning counts. Build errors are in the output text; get_errors shows the background compiler's view.")]
		void Build(MCPCall call,
			[MCPParam("Timeout in milliseconds. Default 600000.")] int timeoutMS)
		{
			if (gApp.IsCompiling)
			{
				call.Error("A build is already running. Use wait_idle to wait for it, or cancel_build.");
				return;
			}

			var wait = new BuildWait();
			wait.mStartOutputLen = OutputLength();
			if (!IDEWorkspaceToolSet.RunCommand("Build Workspace"))
			{
				delete wait;
				call.Error("The Build Workspace command is missing");
				return;
			}

			call.Defer((timeoutMS > 0) ? timeoutMS : 600000, new (pollCall) =>
				{
					bool done = wait.Poll(pollCall);
					if (done)
						delete wait;
					return done;
				});
		}

		[MCPTool("cancel_build", "Cancel the build in progress, if any.")]
		void CancelBuild(MCPCall call)
		{
			bool wasCompiling = gApp.IsCompiling;
			IDEWorkspaceToolSet.RunCommand("Cancel Build");
			call.Result(scope $"{{\"wasCompiling\":{wasCompiling ? "true" : "false"}}}");
		}

		[MCPTool("clean", "Delete the build cache so the next build is a full rebuild (the Clean command). Pass beef=true for Clean Beef, which also discards the compiler's incremental state.")]
		void Clean(MCPCall call,
			[MCPParam("Run Clean Beef instead of Clean. Default false.")] bool beef)
		{
			if (gApp.IsCompiling)
			{
				call.Error("A build is running; cancel it or wait for it first.");
				return;
			}
			IDEWorkspaceToolSet.RunCommand(beef ? "Clean Beef" : "Clean");
			call.Result("{\"ok\":true}");
		}
	}
}
#endif
