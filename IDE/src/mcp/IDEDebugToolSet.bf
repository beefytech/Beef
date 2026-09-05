#if !CLI
using System;
using System.Collections;
using System.IO;
using Beefy.mcp;
using Beefy.utils;
using Beefy.widgets;
using IDE.ui;
using IDE.Debugger;

namespace IDE
{
	// Driving the debugger: starting and stopping the target, breaking and stepping with the reply
	// held until the target has settled, breakpoints, expression evaluation, and the callstack,
	// threads, watches and locals views. Actions run the IDE's own commands so the UI follows.
	class IDEDebugToolSet : MCPToolSet
	{
		// ---------------------------------------------------------------------------------------
		// State
		// ---------------------------------------------------------------------------------------

		static void Finish(MCPCall call, StructuredData sd)
		{
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		// Paused and ready to inspect: the IDE has processed the stop and the callstack is current
		static bool IsPausedNow()
		{
			var debugger = gApp.mDebugger;
			return (debugger.mIsRunning) && (gApp.mExecutionPaused) && (debugger.IsPaused()) && (!gApp.mWantsRehupCallstack);
		}

		// A wait for the target to stop is over when it is paused again or has exited
		static bool IsSettled()
		{
			return (!gApp.mDebugger.mIsRunning) || (IsPausedNow());
		}

		static void AppendFrame(StructuredData sd, int32 frameIdx)
		{
			String frameInfo = scope .();
			String file = scope .();
			gApp.mDebugger.GetStackFrameInfo(frameIdx, frameInfo, var addr, file, var hotIdx, var defLineStart, var defLineEnd, var line, var column, var language, var stackSize, var flags);
			sd.Add("text", frameInfo);
			// The file may carry a '#hotIdx' suffix for hot-swapped code
			int hashPos = file.IndexOf('#');
			if (hashPos != -1)
				file.RemoveToEnd(hashPos);
			if (!file.IsEmpty)
			{
				sd.Add("file", file);
				sd.Add("line", line);
				sd.Add("column", column);
			}
			sd.Add("address", scope $"0x{addr:X}");
			sd.Add("stackSize", stackSize);
			String flagsStr = scope .();
			flags.ToString(flagsStr);
			if ((!flagsStr.IsEmpty) && (flagsStr != "None"))
				sd.Add("flags", flagsStr);
		}

		public static void AppendDebugState(StructuredData sd)
		{
			var debugger = gApp.mDebugger;
			sd.Add("running", debugger.mIsRunning);
			bool paused = IsPausedNow();
			sd.Add("paused", paused);
			String runState = scope .();
			debugger.GetRunState().ToString(runState);
			sd.Add("runState", runState);
			sd.Add("compiling", gApp.IsCompiling);
			sd.Add("lastCompileFailed", gApp.mLastCompileFailed);
			if (debugger.mIsRunning)
				sd.Add("processId", debugger.GetProcessId());
			if (paused)
			{
				sd.Add("activeFrame", debugger.mActiveCallStackIdx);
				sd.Add("activeThread", debugger.GetActiveThread());
				// Only meaningful when stopped on one; otherwise the buffer is stale
				if (debugger.GetRunState() == .Exception)
				{
					String exception = scope .();
					debugger.GetCurrentException(exception);
					if (!exception.IsEmpty)
						sd.Add("exception", exception);
				}
				using (sd.CreateObject("location"))
					AppendFrame(sd, debugger.mActiveCallStackIdx);
			}
			sd.Add("breakpointCount", debugger.mBreakpointList.Count);
		}

		// Replies with the debug state once the target has paused again or exited
		static void WaitForSettle(MCPCall call, int timeoutMS, StringView action)
		{
			call.Defer((timeoutMS > 0) ? timeoutMS : 30000, new (pollCall) =>
				{
					bool settled = IsSettled();
					if ((!settled) && (!pollCall.mTimedOut))
						return false;

					var sd = scope StructuredData();
					sd.CreateNew();
					sd.Add("action", action);
					sd.Add("completed", settled);
					if (pollCall.mTimedOut)
						sd.Add("timedOut", true);
					AppendDebugState(sd);
					Finish(pollCall, sd);
					return true;
				});
		}

		[MCPTool("debug_state", "Whether a target is running and paused, the run state, the active frame and thread, the current exception if stopped on one, the paused location, and the breakpoint count. Cheap; call it whenever you need to know where the debugger is.")]
		void DebugState(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			AppendDebugState(sd);
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Lifecycle
		// ---------------------------------------------------------------------------------------

		class StartWait
		{
			public int mStartOutputLen;
			public bool mSawActivity;
			public int mFrames;

			public bool Poll(MCPCall call)
			{
				mFrames++;
				var debugger = gApp.mDebugger;
				if ((gApp.IsCompiling) || (debugger.mIsRunning))
					mSawActivity = true;

				bool done = (mSawActivity) && (!gApp.IsCompiling) && ((debugger.mIsRunning) || (gApp.mLastCompileFailed));
				if ((!mSawActivity) && (mFrames >= 30))
					done = true;
				if ((!done) && (!call.mTimedOut))
					return false;

				var sd = scope StructuredData();
				sd.CreateNew();
				sd.Add("started", (mSawActivity) && (debugger.mIsRunning));
				if (call.mTimedOut)
					sd.Add("timedOut", true);
				if (!mSawActivity)
					sd.Add("note", "Nothing started. Check get_dialogs for a prompt and status for an open workspace with a startup project.");
				AppendDebugState(sd);
				IDEBuildToolSet.AppendOutputSince(sd, mStartOutputLen, 4000);
				Finish(call, sd);
				return true;
			}
		}

		[MCPTool("debug_start", "Build if needed and launch the startup project: mode debug attaches the debugger (Start Debugging, F5), run launches without it (Start Without Debugging), nocompile launches the existing build under the debugger. Returns once the target is running, or the build failed. If the debugger is already paused, mode debug continues execution instead. Follow with wait_for_break to wait for a breakpoint.")]
		void DebugStart(MCPCall call,
			[MCPParam("debug (default), run, or nocompile.")] String mode,
			[MCPParam("Timeout in milliseconds. Default 600000.")] int timeoutMS)
		{
			String command = "Start Debugging";
			if (String.Compare(mode, "run", true) == 0)
				command = "Start Without Debugging";
			else if (String.Compare(mode, "nocompile", true) == 0)
				command = "Start Without Compiling";
			else if ((!mode.IsEmpty) && (String.Compare(mode, "debug", true) != 0))
			{
				call.Error("mode must be debug, run or nocompile");
				return;
			}

			if (gApp.IsCompiling)
			{
				call.Error("A build is already running. Use wait_idle or cancel_build first.");
				return;
			}

			var wait = new StartWait();
			wait.mStartOutputLen = IDEBuildToolSet.OutputLength();
			IDEWorkspaceToolSet.RunCommand(command);
			call.Defer((timeoutMS > 0) ? timeoutMS : 600000, new (pollCall) =>
				{
					bool done = wait.Poll(pollCall);
					if (done)
						delete wait;
					return done;
				});
		}

		[MCPTool("debug_stop", "Stop the running target (Stop Debugging) and wait until it has exited.")]
		void DebugStop(MCPCall call,
			[MCPParam("Timeout in milliseconds. Default 30000.")] int timeoutMS)
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				call.Error("No target is running");
				return;
			}
			IDEWorkspaceToolSet.RunCommand("Stop Debugging");
			call.Defer((timeoutMS > 0) ? timeoutMS : 30000, new (pollCall) =>
				{
					if ((gApp.mDebugger.mIsRunning) && (!pollCall.mTimedOut))
						return false;
					var sd = scope StructuredData();
					sd.CreateNew();
					sd.Add("stopped", !gApp.mDebugger.mIsRunning);
					if (pollCall.mTimedOut)
						sd.Add("timedOut", true);
					AppendDebugState(sd);
					Finish(pollCall, sd);
					return true;
				});
		}

		[MCPTool("debug_break", "Pause the running target (Break All) and wait until it is stopped.")]
		void DebugBreak(MCPCall call,
			[MCPParam("Timeout in milliseconds. Default 10000.")] int timeoutMS)
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				call.Error("No target is running");
				return;
			}
			if (IsPausedNow())
			{
				call.Error("The target is already paused");
				return;
			}
			IDEWorkspaceToolSet.RunCommand("Break All");
			WaitForSettle(call, (timeoutMS > 0) ? timeoutMS : 10000, "break");
		}

		[MCPTool("debug_continue", "Resume the paused target. With wait=true the reply is held until it pauses again (a breakpoint, an exception) or exits; otherwise it returns at once.")]
		void DebugContinue(MCPCall call,
			[MCPParam("Wait for the next stop. Default false.")] bool wait,
			[MCPParam("Timeout in milliseconds when waiting. Default 60000.")] int timeoutMS)
		{
			if (!IsPausedNow())
			{
				call.Error("The target is not paused");
				return;
			}
			gApp.RunWithCompiling();
			if (!wait)
			{
				var sd = scope StructuredData();
				sd.CreateNew();
				sd.Add("resumed", true);
				AppendDebugState(sd);
				Finish(call, sd);
				return;
			}
			WaitForSettle(call, (timeoutMS > 0) ? timeoutMS : 60000, "continue");
		}

		[MCPTool("wait_for_break", "Wait until the running target pauses (breakpoint, exception, break) or exits, then return the debug state with the stop location.")]
		void WaitForBreak(MCPCall call,
			[MCPParam("Timeout in milliseconds. Default 60000.")] int timeoutMS)
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				call.Error("No target is running");
				return;
			}
			WaitForSettle(call, (timeoutMS > 0) ? timeoutMS : 60000, "wait");
		}

		class StepState
		{
			public String mCommand ~ delete _;
			public int mRemaining;
			public int mDone;

			public bool Poll(MCPCall call)
			{
				if ((!IsSettled()) && (!call.mTimedOut))
					return false;

				bool exited = !gApp.mDebugger.mIsRunning;
				if ((mRemaining > 0) && (!exited) && (!call.mTimedOut))
				{
					IDEWorkspaceToolSet.RunCommand(mCommand);
					mRemaining--;
					mDone++;
					return false;
				}

				var sd = scope StructuredData();
				sd.CreateNew();
				sd.Add("stepsIssued", mDone);
				if (call.mTimedOut)
					sd.Add("timedOut", true);
				if (exited)
					sd.Add("exited", true);
				AppendDebugState(sd);
				Finish(call, sd);
				return true;
			}
		}

		[MCPTool("debug_step", "Step the paused target: into, over or out, count times, waiting after each step until it has paused again. Returns the debug state with the new location.")]
		void DebugStep(MCPCall call,
			[MCPParam("into, over (default) or out.")] String kind,
			[MCPParam("How many steps. Default 1.")] int count,
			[MCPParam("Timeout in milliseconds for the whole sequence. Default 60000.")] int timeoutMS)
		{
			if (!IsPausedNow())
			{
				call.Error("The target is not paused; steps need a paused target.");
				return;
			}

			String command;
			if (String.Compare(kind, "into", true) == 0)
				command = "Step Into";
			else if (String.Compare(kind, "out", true) == 0)
				command = "Step Out";
			else if ((kind.IsEmpty) || (String.Compare(kind, "over", true) == 0))
				command = "Step Over";
			else
			{
				call.Error("kind must be into, over or out");
				return;
			}

			var state = new StepState();
			state.mCommand = new String(command);
			state.mRemaining = Math.Max(count, 1);
			// Issue the first step now; the poll issues the rest as each one settles
			IDEWorkspaceToolSet.RunCommand(command);
			state.mRemaining--;
			state.mDone++;

			call.Defer((timeoutMS > 0) ? timeoutMS : 60000, new (pollCall) =>
				{
					bool done = state.Poll(pollCall);
					if (done)
						delete state;
					return done;
				});
		}

		[MCPTool("run_to_cursor", "Run the paused target until it reaches a 0-based line in a file (Run To Cursor), waiting for it to stop there or elsewhere.")]
		void RunToCursor(MCPCall call,
			[MCPParam("Path of the file, absolute or workspace-relative.", true)] String file,
			[MCPParam("0-based line.", true)] int line,
			[MCPParam("Timeout in milliseconds. Default 60000.")] int timeoutMS)
		{
			if (!IsPausedNow())
			{
				call.Error("The target is not paused");
				return;
			}
			String absPath = scope .();
			IDEEditorToolSet.ResolvePath(file, absPath);
			var panel = gApp.ShowSourceFileLocation(absPath, -1, -1, line, 0, .Always);
			if (panel == null)
			{
				call.Error(scope $"Could not open {absPath}");
				return;
			}
			IDEWorkspaceToolSet.RunCommand("Run To Cursor");
			WaitForSettle(call, (timeoutMS > 0) ? timeoutMS : 60000, "run_to_cursor");
		}

		// ---------------------------------------------------------------------------------------
		// Breakpoints
		// ---------------------------------------------------------------------------------------

		static void AppendBreakpoint(StructuredData sd, Breakpoint breakpoint, int index)
		{
			sd.Add("index", index);
			if (breakpoint.mIsMemoryBreakpoint)
			{
				sd.Add("kind", "memory");
				if (breakpoint.mMemoryWatchExpression != null)
					sd.Add("expression", breakpoint.mMemoryWatchExpression);
			}
			else if (breakpoint.mSymbol != null)
			{
				sd.Add("kind", "symbol");
				sd.Add("symbol", breakpoint.mSymbol);
			}
			else
			{
				sd.Add("kind", "source");
				if (breakpoint.mFileName != null)
					sd.Add("file", breakpoint.mFileName);
				sd.Add("line", breakpoint.GetLineNum());
				sd.Add("column", breakpoint.mColumn);
			}
			if (breakpoint.mCondition != null)
				sd.Add("condition", breakpoint.mCondition);
			if (breakpoint.mLogging != null)
				sd.Add("logging", breakpoint.mLogging);
			if (breakpoint.mDisabled)
				sd.Add("disabled", true);
			sd.Add("bound", breakpoint.IsBound());
			sd.Add("hitCount", breakpoint.GetHitCount());
			if (breakpoint.mHitCountBreakKind != .None)
			{
				String kindStr = scope .();
				breakpoint.mHitCountBreakKind.ToString(kindStr);
				sd.Add("hitCountKind", kindStr);
				sd.Add("hitCountTarget", breakpoint.mHitCountTarget);
			}
			if (breakpoint.mThreadId != -1)
				sd.Add("threadId", breakpoint.mThreadId);
			String location = scope .();
			breakpoint.ToString_Location(location);
			sd.Add("location", location);
		}

		[MCPTool("get_breakpoints", "All breakpoints with their index, kind (source, symbol, memory), file and 0-based line, condition, enabled state, whether the debugger has bound them to code, and hit counts. The index is what remove_breakpoint and set_breakpoint take; it shifts when breakpoints are removed, so re-read after changes.")]
		void GetBreakpoints(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("breakpoints"))
			{
				for (var breakpoint in gApp.mDebugger.mBreakpointList)
				{
					using (sd.CreateObject())
						AppendBreakpoint(sd, breakpoint, @breakpoint.Index);
				}
			}
			Finish(call, sd);
		}

		static Breakpoint FindBreakpoint(MCPCall call, out int index)
		{
			index = -1;
			var list = gApp.mDebugger.mBreakpointList;
			if (call.HasArg("index"))
			{
				index = (int)call.GetInt("index");
				if ((index < 0) || (index >= list.Count))
				{
					call.Error("Breakpoint index out of range; call get_breakpoints for current indexes");
					return null;
				}
				return list[index];
			}
			if ((call.HasArg("file")) && (call.HasArg("line")))
			{
				String file = scope .();
				call.GetString("file", file);
				String absPath = scope .();
				IDEEditorToolSet.ResolvePath(file, absPath);
				int line = (int)call.GetInt("line");
				for (var breakpoint in list)
				{
					if ((breakpoint.mFileName != null) && (IDEEditorToolSet.PathsEqual(breakpoint.mFileName, absPath)) && (breakpoint.GetLineNum() == line))
					{
						index = @breakpoint.Index;
						return breakpoint;
					}
				}
				call.Error("No breakpoint at that file and line");
				return null;
			}
			call.Error("Pass index, or file and line");
			return null;
		}

		static void ApplyBreakpointOptions(MCPCall call, Breakpoint breakpoint)
		{
			if (call.HasArg("condition"))
			{
				String condition = scope .();
				call.GetString("condition", condition);
				breakpoint.SetCondition(condition.IsEmpty ? null : condition);
			}
			if (call.HasArg("hit_count"))
			{
				String kindStr = scope .();
				call.GetString("hit_kind", kindStr, "GreaterEquals");
				Breakpoint.HitCountBreakKind kind = .GreaterEquals;
				if (Enum.Parse<Breakpoint.HitCountBreakKind>(kindStr, true) case .Ok(let parsed))
					kind = parsed;
				breakpoint.SetHitCountTarget((int)call.GetInt("hit_count"), kind);
			}
			if (call.HasArg("enabled"))
				gApp.mDebugger.SetBreakpointDisabled(breakpoint, !call.GetBool("enabled", true));
		}

		[MCPTool("add_breakpoint", "Set a breakpoint on a 0-based line of a file, or on a symbol (function name), optionally with a condition and a hit count. Takes effect immediately, including in a running target.",
			"{'type':'object','properties':{'file':{'type':'string','description':'File path, absolute or workspace-relative. Use with line.'},'line':{'type':'integer','description':'0-based line.'},'column':{'type':'integer','description':'Column within the line. Default 0.'},'symbol':{'type':'string','description':'Break on entry to this function instead of a source location.'},'condition':{'type':'string','description':'Only break when this expression is true.'},'hit_count':{'type':'integer','description':'Hit count target.'},'hit_kind':{'type':'string','enum':['Equals','GreaterEquals','MultipleOf'],'description':'How hit_count is compared. Default GreaterEquals.'}}}")]
		void AddBreakpoint(MCPCall call)
		{
			Breakpoint breakpoint = null;
			if (call.HasArg("symbol"))
			{
				String symbol = scope .();
				call.GetString("symbol", symbol);
				breakpoint = gApp.mDebugger.CreateSymbolBreakpoint(symbol);
			}
			else if ((call.HasArg("file")) && (call.HasArg("line")))
			{
				String file = scope .();
				call.GetString("file", file);
				String absPath = scope .();
				IDEEditorToolSet.ResolvePath(file, absPath);
				breakpoint = gApp.mDebugger.CreateBreakpoint(absPath, (int)call.GetInt("line"), (int)call.GetInt("column", 0));
			}
			else
			{
				call.Error("Pass file and line, or symbol");
				return;
			}
			if (breakpoint == null)
			{
				call.Error("The debugger refused the breakpoint");
				return;
			}
			ApplyBreakpointOptions(call, breakpoint);

			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateObject("breakpoint"))
				AppendBreakpoint(sd, breakpoint, gApp.mDebugger.mBreakpointList.IndexOf(breakpoint));
			Finish(call, sd);
		}

		[MCPTool("set_breakpoint", "Change a breakpoint: enable or disable it, set or clear its condition (empty clears), or set a hit count target. Identify it by index from get_breakpoints, or by file and 0-based line.",
			"{'type':'object','properties':{'index':{'type':'integer'},'file':{'type':'string'},'line':{'type':'integer'},'enabled':{'type':'boolean'},'condition':{'type':'string'},'hit_count':{'type':'integer'},'hit_kind':{'type':'string','enum':['Equals','GreaterEquals','MultipleOf']}}}")]
		void SetBreakpoint(MCPCall call)
		{
			var breakpoint = FindBreakpoint(call, var index);
			if (breakpoint == null)
				return;
			ApplyBreakpointOptions(call, breakpoint);

			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateObject("breakpoint"))
				AppendBreakpoint(sd, breakpoint, index);
			Finish(call, sd);
		}

		[MCPTool("remove_breakpoint", "Delete a breakpoint by index from get_breakpoints, or by file and 0-based line.",
			"{'type':'object','properties':{'index':{'type':'integer'},'file':{'type':'string'},'line':{'type':'integer'}}}")]
		void RemoveBreakpoint(MCPCall call)
		{
			var breakpoint = FindBreakpoint(call, var index);
			if (breakpoint == null)
				return;
			gApp.mDebugger.DeleteBreakpoint(breakpoint);
			call.Result(scope $"{{\"removed\":{index},\"remaining\":{gApp.mDebugger.mBreakpointList.Count}}}");
		}

		[MCPTool("remove_all_breakpoints", "Delete every breakpoint.")]
		void RemoveAllBreakpoints(MCPCall call)
		{
			int count = gApp.mDebugger.mBreakpointList.Count;
			IDEWorkspaceToolSet.RunCommand("Remove All Breakpoints");
			call.Result(scope $"{{\"removed\":{count},\"remaining\":{gApp.mDebugger.mBreakpointList.Count}}}");
		}

		// ---------------------------------------------------------------------------------------
		// Evaluation
		// ---------------------------------------------------------------------------------------

		static void FinishEval(MCPCall call, StringView expression, String outVal)
		{
			if (outVal.StartsWith("!"))
			{
				// Errors come back as "!<flags>\t<message>"
				StringView message = outVal;
				int tabPos = outVal.LastIndexOf('\t');
				if (tabPos != -1)
					message = StringView(outVal, tabPos + 1);
				else
					message = StringView(outVal, 1);
				call.Error(scope $"Evaluation failed: {message}");
				return;
			}

			// First line is the value, second the type, later lines are flags and member info
			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("expression", expression);
			int lineIdx = 0;
			for (var lineView in outVal.Split('\n'))
			{
				if (lineIdx == 0)
					sd.Add("value", lineView);
				else if (lineIdx == 1)
					sd.Add("type", lineView);
				lineIdx++;
			}
			if (lineIdx > 2)
				sd.Add("extraLines", lineIdx - 2);
			Finish(call, sd);
		}

		[MCPTool("eval", "Evaluate an expression in the paused target's active frame (see select_frame) and return its value and type, as the Watch panel would show it. Allows calls and side effects unless allow_side_effects is false.")]
		void Eval(MCPCall call,
			[MCPParam("Expression in the language of the current frame.", true)] String expression,
			[MCPParam("Allow property access, calls and assignments. Default true.")] bool allow_side_effects,
			[MCPParam("Timeout in milliseconds. Default 10000.")] int timeoutMS)
		{
			if (!IsPausedNow())
			{
				call.Error("The debugger is not paused; expressions need a paused target.");
				return;
			}

			DebugManager.EvalExpressionFlags flags = .AllowProperties;
			if ((!call.HasArg("allow_side_effects")) || (allow_side_effects))
				flags |= .AllowCalls | .AllowSideEffects;

			String outVal = new String();
			gApp.mDebugger.Evaluate(expression, outVal, -1, -1, flags);
			if (!outVal.StartsWith("!pending"))
			{
				FinishEval(call, expression, outVal);
				delete outVal;
				return;
			}

			// Calls into the target finish asynchronously; keep asking until an answer arrives
			String expressionCopy = new String(expression);
			call.Defer((timeoutMS > 0) ? timeoutMS : 10000, new (pollCall) =>
				{
					outVal.Clear();
					gApp.mDebugger.EvaluateContinue(outVal);
					if (((outVal.StartsWith("!pending")) || (outVal.StartsWith("!Not paused"))) && (!pollCall.mTimedOut))
						return false;
					if (pollCall.mTimedOut)
						pollCall.Error("Evaluation did not finish in time");
					else
						FinishEval(pollCall, expressionCopy, outVal);
					delete outVal;
					delete expressionCopy;
					return true;
				});
		}

		// ---------------------------------------------------------------------------------------
		// Callstack and threads
		// ---------------------------------------------------------------------------------------

		// Frames arrive in batches; keep asking while the stack keeps growing
		static bool EnsureFrame(int frameIdx)
		{
			while (frameIdx >= gApp.mDebugger.GetCallStackCount())
			{
				int32 prevCount = gApp.mDebugger.GetCallStackCount();
				gApp.mDebugger.UpdateCallStack();
				if (gApp.mDebugger.GetCallStackCount() == prevCount)
					return false;
			}
			return true;
		}

		[MCPTool("get_callstack", "The paused target's call stack for the active thread: each frame's index, text, file and 0-based line, address and which frame is active. Frame indexes are what select_frame takes.")]
		void GetCallstack(MCPCall call,
			[MCPParam("Maximum frames. Default 50.")] int max)
		{
			if (!IsPausedNow())
			{
				call.Error("The debugger is not paused");
				return;
			}
			int maxFrames = (max > 0) ? max : 50;

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("activeFrame", gApp.mDebugger.mActiveCallStackIdx);
			int frameIdx = 0;
			bool truncated = false;
			using (sd.CreateArray("frames"))
			{
				while (EnsureFrame(frameIdx))
				{
					if (frameIdx >= maxFrames)
					{
						truncated = true;
						break;
					}
					using (sd.CreateObject())
					{
						sd.Add("index", frameIdx);
						AppendFrame(sd, (int32)frameIdx);
						if (frameIdx == gApp.mDebugger.mActiveCallStackIdx)
							sd.Add("active", true);
					}
					frameIdx++;
				}
			}
			sd.Add("frameCount", frameIdx);
			if (truncated)
				sd.Add("truncated", true);
			Finish(call, sd);
		}

		[MCPTool("select_frame", "Make a call stack frame the active one, so eval, get_locals and hover use its scope.")]
		void SelectFrame(MCPCall call,
			[MCPParam("Frame index from get_callstack.", true)] int index)
		{
			if (!IsPausedNow())
			{
				call.Error("The debugger is not paused");
				return;
			}
			if ((index < 0) || (!EnsureFrame(index)))
			{
				call.Error("Frame index out of range");
				return;
			}
			gApp.mDebugger.mActiveCallStackIdx = (int32)index;
			gApp.RefreshWatches();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("activeFrame", index);
			using (sd.CreateObject("location"))
				AppendFrame(sd, (int32)index);
			Finish(call, sd);
		}

		[MCPTool("get_threads", "The target's threads: id, name, current location, and which is active.")]
		void GetThreads(MCPCall call)
		{
			if (!gApp.mDebugger.mIsRunning)
			{
				call.Error("No target is running");
				return;
			}
			String threadInfo = scope .();
			gApp.mDebugger.GetThreadInfo(threadInfo);

			var sd = scope StructuredData();
			sd.CreateNew();
			int32 currentThreadId = -1;
			using (sd.CreateArray("threads"))
			{
				for (var infoLine in threadInfo.Split('\n'))
				{
					if (@infoLine.Pos == 0)
					{
						currentThreadId = int32.Parse(scope String(infoLine)).GetValueOrDefault();
						continue;
					}
					if (infoLine.IsEmpty)
						continue;
					var sections = infoLine.Split('\t');
					StringView idStr = sections.GetNext().GetValueOrDefault();
					StringView name = sections.GetNext().GetValueOrDefault();
					StringView location = sections.GetNext().GetValueOrDefault();
					int32 threadId = int32.Parse(scope String(idStr)).GetValueOrDefault();
					using (sd.CreateObject())
					{
						sd.Add("id", threadId);
						sd.Add("name", name);
						sd.Add("location", location);
						if (threadId == currentThreadId)
							sd.Add("active", true);
					}
				}
			}
			sd.Add("activeThread", currentThreadId);
			Finish(call, sd);
		}

		[MCPTool("select_thread", "Make a thread the active one, so the callstack, eval and locals refer to it.")]
		void SelectThread(MCPCall call,
			[MCPParam("Thread id from get_threads.", true)] int id)
		{
			if (!IsPausedNow())
			{
				call.Error("The debugger is not paused");
				return;
			}
			gApp.mDebugger.SetActiveThread((int32)id);
			gApp.mDebugger.mActiveCallStackIdx = 0;
			gApp.mDebugger.UpdateCallStack();
			gApp.RefreshWatches();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("activeThread", gApp.mDebugger.GetActiveThread());
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Watches and locals
		// ---------------------------------------------------------------------------------------

		static void AppendWatchItems(StructuredData sd, ListViewItem parentItem, int depth, String arrayName)
		{
			using (sd.CreateArray(arrayName))
			{
				if (parentItem.mChildItems == null)
					return;
				for (var childItem in parentItem.mChildItems)
				{
					var watchItem = childItem as WatchListViewItem;
					if ((watchItem == null) || (watchItem.mWatchEntry == null))
						continue;
					var watchEntry = watchItem.mWatchEntry;
					if ((watchEntry.mEvalStr == null) && (childItem.Label.IsEmpty))
						continue; // The empty row at the bottom of the Watch panel

					using (sd.CreateObject())
					{
						sd.Add("name", childItem.Label);
						if (watchEntry.mEvalStr != null)
							sd.Add("expression", watchEntry.mEvalStr);
						if ((childItem.mSubItems != null) && (childItem.mSubItems.Count > 1))
							sd.Add("value", childItem.GetSubItem(1).Label);
						if (watchEntry.mResultTypeStr != null)
							sd.Add("type", watchEntry.mResultTypeStr);
						if (!watchEntry.mHasValue)
							sd.Add("pending", true);
						if (watchEntry.mIsDeleted)
							sd.Add("deleted", true);
						if (watchItem.mDisabled)
							sd.Add("disabled", true);
						sd.Add("widget", childItem.mWidgetId);
						if (childItem.mChildItems != null)
						{
							sd.Add("expandable", true);
							sd.Add("open", childItem.IsOpen);
							if ((depth > 0) && (childItem.IsOpen))
								AppendWatchItems(sd, childItem, depth - 1, "children");
						}
					}
				}
			}
		}

		[MCPTool("get_watches", "The Watch panel: each watch expression with its current value and type. Values marked pending have not been evaluated yet; call wait_frames and read again. Expanded members are included for open items; use eval for a member that is not.")]
		void GetWatches(MCPCall call,
			[MCPParam("How many levels of expanded members to include. Default 2.")] int depth)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			AppendWatchItems(sd, gApp.mWatchPanel.mListView.GetRoot(), call.HasArg("depth") ? depth : 2, "watches");
			Finish(call, sd);
		}

		[MCPTool("get_locals", "The Auto Watch panel: the locals and parameters of the active frame with their values and types, as the debugger shows them. Values marked pending have not been evaluated yet; call wait_frames and read again.")]
		void GetLocals(MCPCall call,
			[MCPParam("How many levels of expanded members to include. Default 2.")] int depth)
		{
			if (!IsPausedNow())
			{
				call.Error("The debugger is not paused");
				return;
			}
			var sd = scope StructuredData();
			sd.CreateNew();
			AppendWatchItems(sd, gApp.mAutoWatchPanel.mListView.GetRoot(), call.HasArg("depth") ? depth : 2, "locals");
			Finish(call, sd);
		}

		[MCPTool("add_watch", "Add an expression to the Watch panel. It is evaluated whenever the target is paused; read it with get_watches.")]
		void AddWatch(MCPCall call,
			[MCPParam("Expression to watch.", true)] String expression)
		{
			var watchItem = gApp.mWatchPanel.AddWatchItem(expression);
			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("expression", expression);
			if (watchItem != null)
				sd.Add("widget", watchItem.mWidgetId);
			Finish(call, sd);
		}

		[MCPTool("remove_watch", "Remove a watch expression from the Watch panel.")]
		void RemoveWatch(MCPCall call,
			[MCPParam("The expression as get_watches reports it.", true)] String expression)
		{
			var root = gApp.mWatchPanel.mListView.GetRoot();
			WatchListViewItem match = null;
			if (root.mChildItems != null)
			{
				for (var childItem in root.mChildItems)
				{
					var watchItem = childItem as WatchListViewItem;
					if ((watchItem?.mWatchEntry?.mEvalStr != null) && (watchItem.mWatchEntry.mEvalStr == expression))
					{
						match = watchItem;
						break;
					}
				}
			}
			if (match == null)
			{
				call.Error("No watch with that expression. Call get_watches to see them.");
				return;
			}
			root.SelectItemExclusively(match);
			gApp.mWatchPanel.[Friend]DeleteSelectedItems();
			call.Result("{\"removed\":true}");
		}
	}
}
#endif
