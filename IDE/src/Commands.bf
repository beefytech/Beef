using System;
using System.Collections;
using Beefy.widgets;
using Beefy.sys;

namespace IDE
{
	public class KeyState : IHashable
	{
		public KeyCode mKeyCode;
		public KeyFlags mKeyFlags;
		
		public int GetHashCode()
		{
			return (int)mKeyCode | (int)mKeyFlags << 16;
		}

		public static bool operator==(KeyState val1, KeyState val2)
		{
			return (val1.mKeyCode == val2.mKeyCode) &&
				(val1.mKeyFlags == val2.mKeyFlags);
		}

		public override void ToString(String strBuffer)
		{
			if (mKeyFlags.HasFlag(.Ctrl))
				strBuffer.Append("Ctrl+");
			if (mKeyFlags.HasFlag(.Alt))
				strBuffer.Append("Alt+");
			if (mKeyFlags.HasFlag(.Shift))
				strBuffer.Append("Shift+");
			mKeyCode.ToString(strBuffer);
		}

		public static void ToString(List<KeyState> keyStates, String strBuffer)
		{
			for (int i < keyStates.Count)
			{
				if (i > 0)
					strBuffer.Append(", ");
				keyStates[i].ToString(strBuffer);
			}
		}

		public static bool Parse(StringView keys, List<KeyState> keyList)
		{
			bool success = true;
			for (let keyStateStr in keys.Split(','))
			{
				let keyState = new KeyState();
				for (var keyStr in keyStateStr.Split('+'))
				{
					keyStr.Trim();
					if (keyStr.Equals("Ctrl", true))
						keyState.mKeyFlags |= .Ctrl;
					else if (keyStr.Equals("Alt", true))
						keyState.mKeyFlags |= .Alt;
					else if (keyStr.Equals("Shift", true))
						keyState.mKeyFlags |= .Shift;
					else
					{
						let result = KeyCode.Parse(keyStr);
						if (result case .Ok(let keyCode))
							keyState.mKeyCode = keyCode;
						else
							success = false;
					}
				}
				keyList.Add(keyState);
			}
			return success;
		}

		public KeyState Clone()
		{
			var dup = new KeyState();
			dup.mKeyCode = mKeyCode;
			dup.mKeyFlags = mKeyFlags;
			return dup;
		}
	}

	class IDECommandBase
	{
		public CommandMap mParent;
		public KeyState mBoundKeyState;

		public override void ToString(String strBuffer)
		{
			if (mParent == null)
				return;
			mParent.ToString(strBuffer);
			if (mBoundKeyState != null)
			{
				if (!strBuffer.IsEmpty)
					strBuffer.Append(", ");
				mBoundKeyState.ToString(strBuffer);
			}
		}
	}

	class IDECommand : IDECommandBase
	{
		public enum ContextFlags
		{
			None = 0,
			MainWindow = 1,
			Editor = 2,
		}

		public String mName ~ delete _;
		public Action mAction ~ delete _;
		public SysMenu mMenuItem;
		public ContextFlags mContextFlags;

		public IDECommand mNext;
	}

	class CommandMap : IDECommandBase
	{
		public Dictionary<KeyState, IDECommandBase> mMap = new .() ~ delete _;

		public void Clear()
		{
			for (let val in mMap.Values)
			{
				if (var cmdMap = val as CommandMap)
					delete cmdMap;
				else
				{
					var ideCommand = (IDECommand)val;
					val.mBoundKeyState = null;
					val.mParent = null;
					ideCommand.mNext = null;
				}
			}
			mMap.Clear();
		}

		public ~this()
		{
			Clear();
		}
	}

	class KeyChordState
	{
		public KeyState mKeyState;
		public CommandMap mCommandMap;
	}

	class Commands
	{
		public Dictionary<String, IDECommand> mCommandMap = new .() ~
			{
				for (let val in _.Values)
					delete val;
				delete _;
			};
		public CommandMap mKeyMap = new .() ~ delete _;

		void Add(StringView name, Action act, IDECommand.ContextFlags contextFlags = .MainWindow)
		{
			let cmd = new IDECommand();
			cmd.mName = new String(name);
			cmd.mAction = act;
			cmd.mContextFlags = contextFlags;
			mCommandMap[cmd.mName] = cmd;
		}

		public void Init()
		{
			Add("About", new => gApp.ShowAbout);
			Add("Autocomplete", new => gApp.Cmd_ShowAutoComplete, .None);
			Add("Bookmark Next", new => gApp.Cmd_NextBookmark, .Editor);
			Add("Bookmark Prev", new => gApp.Cmd_PrevBookmark, .Editor);
			Add("Bookmark Toggle", new => gApp.Cmd_ToggleBookmark, .Editor);
			Add("Bookmark Clear", new => gApp.Cmd_ClearBookmarks, .Editor);
			Add("Break All", new => gApp.[Friend]Cmd_Break);
			Add("Breakpoint Configure", new () => gApp.ConfigureBreakpoint());
			Add("Breakpoint Disable", new () => gApp.DisableBreakpoint());
			Add("Breakpoint Memory", new () => { gApp.mBreakpointPanel.AddMemoryBreakpoint(gApp.[Friend]GetCurrentWindow()); });
			Add("Breakpoint Symbol", new () => { gApp.mBreakpointPanel.AddSymbolBreakpoint(gApp.[Friend]GetCurrentWindow()); });
			Add("Breakpoint Toggle Thread", new => gApp.[Friend]ToggleThreadBreakpoint, .Editor);
			Add("Breakpoint Toggle", new => gApp.[Friend]ToggleBreakpoint, .Editor);
			Add("Build Solution", new => gApp.[Friend]Compile);
			Add("Cancel Build", new => gApp.[Friend]CancelBuild);
			Add("Clean Beef", new => gApp.Cmd_CleanBeef);
			Add("Clean", new => gApp.Cmd_Clean);
			Add("Close All Windows", new () => { gApp.[Friend]TryCloseAllDocuments(); });
			Add("Close Window", new () => { gApp.[Friend]TryCloseCurrentDocument(); });
			Add("Close Workspace", new => gApp.[Friend]Cmd_CloseWorkspaceAndSetupNew);
			Add("Compile File", new => gApp.Cmd_CompileFile);
			Add("Debug All Tests", new () => { gApp.[Friend]RunTests(true, true); });
			Add("Debug Normal Tests", new () => { gApp.[Friend]RunTests(false, true); });
			Add("Exit", new => gApp.[Friend]Cmd_Exit);
			Add("Find All References", new => gApp.Cmd_FindAllReferences);
			Add("Find Class", new => gApp.Cmd_FindClass);
			Add("Find in Document", new => gApp.Cmd_Document__Find);
			Add("Find in Files", new => gApp.Cmd_Find);
			Add("Find Next", new => gApp.Cmd_FindNext);
			Add("Find Prev", new => gApp.Cmd_FindPrev);
			Add("Goto Definition", new () => gApp.GoToDefinition(true));
			Add("Goto Line", new => gApp.Cmd_GotoLine);
			Add("Goto Method", new => gApp.Cmd_GotoMethod);
			Add("Goto Next Item", new => gApp.Cmd_GotoNextItem);
			Add("Launch Process", new => gApp.[Friend]DoLaunch);
			Add("Make Lowercase", new () => { gApp.[Friend]ChangeCase(false); });
			Add("Make Uppercase", new () => { gApp.[Friend]ChangeCase(true); });
			Add("Match Brace Select", new => gApp.Cmd_MatchBrace);
			Add("Match Brace", new => gApp.Cmd_MatchBrace);
			Add("Navigate Backwards", new => gApp.[Friend]NavigateBackwards);
			Add("Navigate Forwards", new => gApp.[Friend]NavigateForwards);
			Add("New Debug Session", new => gApp.[Friend]Cmd_NewDebugSession);
			Add("New File", new => gApp.Cmd_NewFile);
			Add("New Project", new => gApp.Cmd_NewProject);
			Add("New Workspace", new => gApp.Cmd_NewWorkspace);
			Add("Next Document Panel", new => gApp.[Friend]DoShowNextDocumentPanel);
			Add("Open Corresponding", new => gApp.[Friend]OpenCorresponding);
			Add("Open Crash Dump", new => gApp.OpenCrashDump);
			Add("Open Debug Session", new => gApp.DoOpenDebugSession);
			Add("Open File in Workspace", new => gApp.[Friend]ShowOpenFileInSolutionDialog);
			Add("Open File", new => gApp.OpenFile);
			Add("Open Project", new => gApp.Cmd_OpenProject);
			Add("Open Workspace", new => gApp.OpenWorkspace);
			Add("Profile", new => gApp.[Friend]DoProfile);
			Add("Quick Info", new => gApp.Cmd_QuickInfo);
			Add("Reformat Document", new => gApp.Cmd_ReformatDocument);
			Add("Remove All Breakpoints", new => gApp.[Friend]RemoveAllBreakpoints);
			Add("Rename Item", new => gApp.Cmd_RenameItem);
			Add("Rename Symbol", new => gApp.Cmd_RenameSymbol);
			Add("Replace in Document", new => gApp.Cmd_Document__Replace);
			Add("Replace in Files", new => gApp.Cmd_Replace);
			Add("Report Memory", new => gApp.[Friend]ReportMemory);
			Add("Run All Tests", new () => { gApp.[Friend]RunTests(true, false); });
			Add("Run Normal Tests", new () => { gApp.[Friend]RunTests(false, false); });
			Add("Run To Cursor", new => gApp.[Friend]RunToCursor);
			Add("Run Without Compiling", new => gApp.[Friend]RunWithoutCompiling);
			Add("Save All", new () => { gApp.SaveAll(); });
			Add("Save As", new () => { gApp.SaveAs(); });
			Add("Save File", new => gApp.SaveFile);
			Add("Select Configuration", new => gApp.SelectConfig);
			Add("Select Platform", new => gApp.SelectPlatform);
			Add("Set Next Statement", new => gApp.[Friend]SetNextStatement);
			Add("Settings", new => gApp.ShowSettings);
			Add("Show Auto Watches", new => gApp.ShowAutoWatches);
			Add("Show Autocomplete Panel", new => gApp.ShowAutoCompletePanel);
			Add("Show Breakpoints", new => gApp.ShowBreakpoints);
			Add("Show Call Stack", new => gApp.ShowCallstack);
			Add("Show Class View", new => gApp.ShowClassViewPanel);
			Add("Show Current", new => gApp.Cmd_ShowCurrent);
			Add("Show Disassembly", new => gApp.[Friend]ShowDisassemblyAtStack);
			Add("Show Errors", new => gApp.[Friend]ShowErrors);
			Add("Show Error Next", new => gApp.ShowErrorNext);
			Add("Show File Externally", new => gApp.Cmd_ShowFileExternally);
			Add("Show Find Results", new => gApp.ShowFindResults);
			Add("Show Fixit", new => gApp.Cmd_ShowFixit);
			Add("Show Immediate", new => gApp.ShowImmediatePanel);
			Add("Show Memory", new => gApp.ShowMemory);
			Add("Show Modules", new => gApp.ShowModules);
			Add("Show Output", new => gApp.ShowOutput);
			Add("Show Profiler", new => gApp.ShowProfilePanel);
			Add("Show QuickWatch", new => gApp.ShowQuickWatch);
			Add("Show Threads", new => gApp.ShowThreads);
			Add("Show Watches", new => gApp.ShowWatches);
			Add("Show Workspace Explorer", new => gApp.ShowWorkspacePanel);
			Add("Start Debugging", new => gApp.[Friend]RunWithCompiling);
			Add("Start Without Debugging", new => gApp.[Friend]RunWithoutCompiling);
			Add("Step Into", new => gApp.[Friend]StepInto);
			Add("Step Out", new => gApp.[Friend]StepOut);
			Add("Step Over", new => gApp.[Friend]StepOver);
			Add("Stop Debugging", new => gApp.[Friend]StopRunning);
			Add("Tab First", new => gApp.[Friend]TabFirst);
			Add("Tab Last", new => gApp.[Friend]TabLast);
			Add("Tab Next", new => gApp.[Friend]TabNext);
			Add("Tab Prev", new => gApp.[Friend]TabPrev);
			Add("View New", new => gApp.Cmd_ViewNew);
			Add("View Split", new => gApp.[Friend]ViewSplit);
			Add("View White Space", new => gApp.Cmd_ViewWhiteSpace);
			Add("Zoom In", new => gApp.Cmd_ZoomIn);
			Add("Zoom Out", new => gApp.Cmd_ZoomOut);
			Add("Zoom Reset", new => gApp.Cmd_ZoomReset);
			Add("Attach to Process", new => gApp.[Friend]DoAttach);

			Add("Test Enable Console", new => gApp.Cmd_TestEnableConsole);
		}
	}
}
