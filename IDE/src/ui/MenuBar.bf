using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.geom;
using IDE;
using System;
using System.Diagnostics;

namespace IDE.ui;

public class MenuBar : Widget
{
	public class Button : ButtonWidget
	{
		private String mLabel ~ delete _;

		public StringView Label
		{
			get
			{
				return mLabel;
			}

			set
			{
				String.NewOrSet!(mLabel, value);
			}
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			if (mMouseOver)
			{
				using (g.PushColor(0x40FFFFFF))
				{
					g.FillRect(0, 0, mWidth, mHeight);
				}
			}

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			if (mLabel != null)
			{
			    using (g.PushColor(mDisabled ? 0x80FFFFFF : Color.White))
			    {
					using (g.PushColor(DarkTheme.COLOR_TEXT))
						DarkTheme.DrawUnderlined(g, mLabel, GS!(2), (mHeight - GS!(20)) / 2, .Centered, mWidth - GS!(4), .Truncate);
			    }
			}
		}
	}

	public Button mFileButton;
	public Button mEditButton;
	public Button mViewButton;
	public Button mBuildButton;
	public Button mDebugButton;
	public Button mTestButton;
	public Button mWindowButton;
	public Button mHelpButton;

	public this()
	{
		mFileButton = new Button();
		mFileButton.Label = "File";
		mFileButton.mOnMouseClick.Add(new (evt) => ShowFileMenu());
		AddWidget(mFileButton);

		mEditButton = new Button();
		mEditButton.Label = "Edit";
		mEditButton.mOnMouseClick.Add(new (evt) => ShowEditMenu());
		AddWidget(mEditButton);

		mViewButton = new Button();
		mViewButton.Label = "View";
		mViewButton.mOnMouseClick.Add(new (evt) => ShowViewMenu());
		AddWidget(mViewButton);

		mBuildButton = new Button();
		mBuildButton.Label = "Build";
		mBuildButton.mOnMouseClick.Add(new (evt) => ShowBuildMenu());
		AddWidget(mBuildButton);

		mDebugButton = new Button();
		mDebugButton.Label = "Debug";
		mDebugButton.mOnMouseClick.Add(new (evt) => ShowDebugMenu());
		AddWidget(mDebugButton);

		mTestButton = new Button();
		mTestButton.Label = "Test";
		mTestButton.mOnMouseClick.Add(new (evt) => ShowTestMenu());
		AddWidget(mTestButton);

		mWindowButton = new Button();
		mWindowButton.Label = "Window";
		mWindowButton.mOnMouseClick.Add(new (evt) => ShowWindowMenu());
		AddWidget(mWindowButton);

		mHelpButton = new Button();
		mHelpButton.Label = "Help";
		mHelpButton.mOnMouseClick.Add(new (evt) => ShowHelpMenu());
		AddWidget(mHelpButton);
	}

	Menu AddMenuItem(Menu menu, StringView label, StringView command, bool disabled = false, bool isChecked = false)
	{
		let labelStr = scope String(label);
		let hasCommand = gApp.mCommands.mCommandMap.TryGetAlt(command, var matchKey, var ideCommand);

		if(hasCommand)
		{
			labelStr.Append("|");
			ideCommand.ToString(labelStr);
		}

		return AddMenuItem(menu, labelStr, hasCommand ? new (evt) => ideCommand.mAction() : null, disabled, isChecked);
	}

	Menu AddMenuItem(Menu menu, StringView label, delegate void(Menu menu) action, bool disabled = false, bool isChecked = false)
	{
		Menu item = menu.AddItem(label);
		item.SetDisabled(disabled);

		if (isChecked)
		{
			item.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
		}

		if (action != null)
		{
			item.mOnMenuItemSelected.Add(action);
		}

		return item;
	}

	void ShowFileMenu()
	{
		let subMenu = new Menu();
		let newMenu = subMenu.AddItem("New");
		AddMenuItem(newMenu, "New Workspace", "New Workspace");
		AddMenuItem(newMenu, "New Project", "New Project");
		AddMenuItem(newMenu, "New Debug Session", "New Debug Session");
		AddMenuItem(newMenu, "New File", "New File");

		let openMenu = subMenu.AddItem("Open");
		AddMenuItem(openMenu, "Open Workspace...", "Open Workspace");
		AddMenuItem(openMenu, "Open Project...", "Open Project");
		AddMenuItem(openMenu, "Open Debug Session...", "Open Debug Session");
		AddMenuItem(openMenu, "Open File...", "Open File");
		AddMenuItem(openMenu, "Open File in Workspace", "Open File in Workspace");
		AddMenuItem(openMenu, "Open Corresponding (cpp/h)", "Open Corresponding");
		AddMenuItem(openMenu, "Open Crash Dump...", "Open Crash Dump");

		let recentMenu = subMenu.AddItem("Open Recent");
		recentMenu.SetDisabled(true); //TODO: recent

		AddMenuItem(subMenu, "Save File", "Save File", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Save As...", "Save As", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Save All", "Save All");
		let prefMenu = subMenu.AddItem("Preferences");
		AddMenuItem(prefMenu, "Settings", "Settings");
		AddMenuItem(prefMenu, "Reload Settings", "Reload Settings");
		AddMenuItem(prefMenu, "Reset UI", "Reset UI");
		AddMenuItem(prefMenu, "Safe Mode", "Safe Mode Toggle", false, gApp.mSafeMode);
		AddMenuItem(subMenu, "Close Workspace", "Close Workspace", !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Exit", "Exit");

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mFileButton, 0, mHeight);
	}

	void ShowEditMenu()
	{
		let subMenu = new Menu();
		AddMenuItem(subMenu, "Quick Find...", "Find in Document", gApp.GetActivePanel() == null);
		AddMenuItem(subMenu, "Quick Replace...", "Replace in Document", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Find in Files...", "Find in Files");
		AddMenuItem(subMenu, "Replace in Files...", "Replace in Files");
		AddMenuItem(subMenu, "Find Prev", "Find Prev", gApp.GetActivePanel() == null);
		AddMenuItem(subMenu, "Find Next", "Find Next", gApp.GetActivePanel() == null);
		AddMenuItem(subMenu, "Show Current", "Show Current");

		AddMenuItem(subMenu, "Goto Line...", "Goto Line", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Goto Method...", "Goto Method", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Rename Symbol", "Rename Symbol", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Show Fixit", "Show Fixit", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Find All References", "Find All References", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Find Class...", "Find Class");
		subMenu.AddItem(null);
		var encodingMenu = subMenu.AddItem("Encoding");
		var lineEndingMenu = encodingMenu.AddItem("Line Ending");
		void AddLineEndingKind(String name, LineEndingKind lineEndingKind)
		{
			let item = lineEndingMenu.AddItem(name);
			let sourceViewPanel = gApp.GetActiveSourceViewPanel(true);

			if (sourceViewPanel != null)
			{
				if (sourceViewPanel.mEditData.mLineEndingKind == lineEndingKind)
				{
					item.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
				}
				item.mOnMenuItemSelected.Add(new (evt) => 
				{
					if (sourceViewPanel.mEditData.mLineEndingKind != lineEndingKind)
					{
						sourceViewPanel.EditWidget.Content.mData.mCurTextVersionId++;
						sourceViewPanel.mEditData.mLineEndingKind = lineEndingKind;
					}
				});
			}
		}
		AddLineEndingKind("Windows", .CrLf);
		AddLineEndingKind("Unix", .Lf);
		AddLineEndingKind("Mac OS 9", .Cr);

		var bookmarkMenu = subMenu.AddItem("Bookmarks");
		AddMenuItem(bookmarkMenu, "Toggle Bookmark", "Bookmark Toggle");
		AddMenuItem(bookmarkMenu, "Next Bookmark", "Bookmark Next");
		AddMenuItem(bookmarkMenu, "Previous Bookmark", "Bookmark Prev");
		AddMenuItem(bookmarkMenu, "Clear Bookmarks", "Bookmark Clear");

		var comptimeMenu = subMenu.AddItem("Comptime");
		var emitViewCompiler = comptimeMenu.AddItem("Emit View Compiler");
		AddMenuItem(emitViewCompiler, "Resolve", new (evt) => gApp.SetEmbedCompiler(.Resolve), false, gApp.mSettings.mEditorSettings.mEmitCompiler == .Resolve);
		AddMenuItem(emitViewCompiler, "Build", new (evt) => gApp.SetEmbedCompiler(.Build), false, gApp.mSettings.mEditorSettings.mEmitCompiler == .Build);

		var advancedEditMenu = subMenu.AddItem("Advanced");
		AddMenuItem(advancedEditMenu, "Duplicate Line", "Duplicate Line");
		AddMenuItem(advancedEditMenu, "Move Line Up", "Move Line Up");
		AddMenuItem(advancedEditMenu, "Move Line Down", "Move Line Down");
		AddMenuItem(advancedEditMenu, "Move Statement Up", "Move Statement Up");
		AddMenuItem(advancedEditMenu, "Move Statement Down", "Move Statement Down");
		advancedEditMenu.AddItem(null);
		AddMenuItem(advancedEditMenu, "Make Uppercase", "Make Uppercase");
		AddMenuItem(advancedEditMenu, "Make Lowercase", "Make Lowercase");
		AddMenuItem(advancedEditMenu, "Comment Block", "Comment Block");
		AddMenuItem(advancedEditMenu, "Comment Lines", "Comment Lines");
		AddMenuItem(advancedEditMenu, "Comment Toggle", "Comment Toggle");
		AddMenuItem(advancedEditMenu, "Uncomment Selection", "Uncomment Selection");
		AddMenuItem(advancedEditMenu, "Reformat Document", "Reformat Document");
		AddMenuItem(advancedEditMenu, "View White Space", "View White Space", false, gApp.mViewWhiteSpace.Bool);

		if (gApp.mSettings.mEnableDevMode)
		{
			subMenu.AddItem(null);
			var internalEditMenu = subMenu.AddItem("Internal");
			AddMenuItem(internalEditMenu, "Hilight Cursor References", new (menu) => { gApp.mSettings.mEditorSettings.mHiliteCursorReferences = !gApp.mSettings.mEditorSettings.mHiliteCursorReferences; }, false, gApp.mSettings.mEditorSettings.mHiliteCursorReferences);
			AddMenuItem(internalEditMenu, "Delayed Autocomplete", new (menu) => { gApp.mDbgDelayedAutocomplete = !gApp.mDbgDelayedAutocomplete; }, false, gApp.mDbgDelayedAutocomplete);
			AddMenuItem(internalEditMenu, "Time Autocomplete",  new (menu) => { gApp.mDbgTimeAutocomplete = !gApp.mDbgTimeAutocomplete; }, false, gApp.mDbgTimeAutocomplete);
			AddMenuItem(internalEditMenu, "Perf Autocomplete", new (menu) => { gApp.mDbgPerfAutocomplete = !gApp.mDbgPerfAutocomplete; }, false, gApp.mDbgPerfAutocomplete);
			AddMenuItem(internalEditMenu, "Dump Undo Buffer", new (menu) =>
			{
				if (var panel = gApp.GetActiveSourceViewPanel())
				{
					var str = panel.mEditWidget.mEditWidgetContent.mData.mUndoManager.ToString(.. scope .());
					Debug.WriteLine(str);
				}
			}, false, gApp.mDbgPerfAutocomplete);
		}

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mEditButton, 0, mHeight);
	}

	void ShowViewMenu()
	{
		let subMenu = new Menu();
		AddMenuItem(subMenu, "AutoComplete", "Show Autocomplete Panel");
		AddMenuItem(subMenu, "Auto Watches", "Show Auto Watches");
		AddMenuItem(subMenu, "Bookmarks", "Show Bookmarks");
		AddMenuItem(subMenu, "Breakpoints", "Show Breakpoints");
		AddMenuItem(subMenu, "Call Stack", "Show Call Stack");
		AddMenuItem(subMenu, "Class View", "Show Class View");
		AddMenuItem(subMenu, "Diagnostics", "Show Diagnostics");
		AddMenuItem(subMenu, "Errors", "Show Errors");
		AddMenuItem(subMenu, "Find Results", "Show Find Results");
		AddMenuItem(subMenu, "Terminal", "Show Terminal");
		AddMenuItem(subMenu, "Console", "Show Console");
		AddMenuItem(subMenu, "Immediate Window", "Show Immediate");
		AddMenuItem(subMenu, "Memory", "Show Memory");
		AddMenuItem(subMenu, "Modules", "Show Modules");
		AddMenuItem(subMenu, "Output", "Show Output");
		AddMenuItem(subMenu, "Profiler", "Show Profiler");
		AddMenuItem(subMenu, "Threads", "Show Threads");
		AddMenuItem(subMenu, "Watches", "Show Watches");
		AddMenuItem(subMenu, "Workspace Explorer", "Show Workspace Explorer");
		subMenu.AddItem(null);
		AddMenuItem(subMenu, "Next Document Panel", "Next Document Panel");
		AddMenuItem(subMenu, "Navigate Backwards", "Navigate Backwards");
		AddMenuItem(subMenu, "Navigate Forwards", "Navigate Forwards");

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mViewButton, 0, mHeight);
	}

	void ShowBuildMenu()
	{
		let subMenu = new Menu();
		AddMenuItem(subMenu, "Build Workspace", "Build Workspace", !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Debug Comptime", "Debug Comptime", gApp.mDebugger.mIsRunning || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Clean", "Clean", gApp.mDebugger.mIsRunning || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Clean Beef", "Clean Beef", gApp.mDebugger.mIsRunning || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Compile Current File", new (menu) => { gApp.[Friend]CompileCurrentFile(); });
		AddMenuItem(subMenu, "Cancel Build", "Cancel Build", !gApp.IsCompiling);
		AddMenuItem(subMenu, "Verbose", new (menu) =>
		{
			if (gApp.mVerbosity != .Diagnostic)
				gApp.mVerbosity = .Diagnostic;
			else
				gApp.mVerbosity = .Normal;
		}, false, (gApp.mVerbosity == .Diagnostic));
		
		if (gApp.mSettings.mEnableDevMode)
		{
			var internalBuildMenu = subMenu.AddItem("Internal");
			AddMenuItem(internalBuildMenu, "Autobuild (Debug)", new (menu) => { gApp.mDebugAutoBuild = !gApp.mDebugAutoBuild; });
			AddMenuItem(internalBuildMenu, "Autorun (Debug)", new (menu) => { gApp.mDebugAutoRun = !gApp.mDebugAutoRun; });
			AddMenuItem(internalBuildMenu, "Disable Compiling", new (menu) => { gApp.mDisableBuilding = !gApp.mDisableBuilding; }, false, gApp.mDisableBuilding);
		}

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mBuildButton, 0, mHeight);		
	}

	void ShowDebugMenu()
	{
		let subMenu = new Menu();
		AddMenuItem(subMenu, "Start Debugging", "Start Debugging", true); // TODO: Debugging status
		AddMenuItem(subMenu, "Start Without Debugging", "Start Without Debugging", gApp.mDebugger.mIsRunning || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Start Without Compiling", "Start Without Compiling", gApp.mDebugger.mIsRunning || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Launch Process...", "Launch Process", gApp.mDebugger.mIsRunning);
		AddMenuItem(subMenu, "Attach to Process...", "Attach to Process", gApp.mDebugger.mIsRunning);
		AddMenuItem(subMenu, "Stop Debugging", "Stop Debugging", !gApp.mDebugger.mIsRunning && (gApp.mTestManager == null));
		AddMenuItem(subMenu, "Break All", "Break All", !gApp.mDebugger.mIsRunning || gApp.mExecutionPaused);
		AddMenuItem(subMenu, "Remove All Breakpoints", "Remove All Breakpoints");
		AddMenuItem(subMenu, "Show Disassembly", "Show Disassembly");
		AddMenuItem(subMenu, "Quick Watch", "Show QuickWatch", !gApp.mDebugger.mIsRunning || !gApp.mExecutionPaused);
		AddMenuItem(subMenu, "Profile", "Profile", !gApp.mWorkspace.IsInitialized);
		subMenu.AddItem(null);
		AddMenuItem(subMenu, "Step Into", "Step Into", (gApp.mDebugger.mIsRunning && !gApp.mExecutionPaused) || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Step Over", "Step Over", (gApp.mDebugger.mIsRunning && !gApp.mExecutionPaused) || !gApp.mWorkspace.IsInitialized);
		AddMenuItem(subMenu, "Step Out", "Step Out", !gApp.mDebugger.mIsRunning || !gApp.mExecutionPaused);
		subMenu.AddItem(null);
		AddMenuItem(subMenu, "Toggle Breakpoint", "Breakpoint Toggle", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(subMenu, "Toggle Thread Breakpoint", "Breakpoint Toggle Thread", gApp.GetActiveDocumentPanel() == null);
		var newBreakpointMenu = subMenu.AddItem("New Breakpoint");
		AddMenuItem(newBreakpointMenu, "Memory Breakpoint...", "Breakpoint Memory", !gApp.mDebugger.mIsRunning);
		AddMenuItem(newBreakpointMenu, "Symbol Breakpoint...", "Breakpoint Symbol");
		//TODO: dev mode

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mDebugButton, 0, mHeight);				
	}

	void ShowTestMenu()
	{
		let testMenu = new Menu();
		var testRunMenu = testMenu.AddItem("Run");
		testRunMenu.SetDisabled(!gApp.mWorkspace.IsInitialized);
		AddMenuItem(testRunMenu, "Normal Tests", "Run Normal Tests");
		AddMenuItem(testRunMenu, "All Tests", "Run All Tests");

		var testDebugMenu = testMenu.AddItem("Debug");
		testDebugMenu.SetDisabled(!gApp.mWorkspace.IsInitialized);
		AddMenuItem(testDebugMenu, "Normal Tests", "Debug Normal Tests");
		AddMenuItem(testDebugMenu, "All Tests", "Debug All Tests");
		testDebugMenu.AddItem(null);
		AddMenuItem(testDebugMenu, "Break on Failure", new (evt) => { gApp.mTestBreakOnFailure = !gApp.mTestBreakOnFailure; }, false, gApp.mTestBreakOnFailure);
		AddMenuItem(testMenu, "Enable Console", "Test Enable Console", false, gApp.mTestEnableConsole);

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(testMenu);
		menuWidget.Init(mTestButton, 0, mHeight);		
	}

	void ShowWindowMenu()
	{
		let windowMenu = new Menu();
		AddMenuItem(windowMenu, "Close Document", "Close Document", gApp.GetLastActiveDocumentPanel() == null);
		AddMenuItem(windowMenu, "Close Panel", "Close Panel", gApp.GetActivePanel() == null);
		AddMenuItem(windowMenu, "Close All", "Close All Panels");
		AddMenuItem(windowMenu, "Close All Except Current", "Close All Panels Except");
		AddMenuItem(windowMenu, "New View into File", "View New", gApp.GetActiveDocumentPanel() == null);
		AddMenuItem(windowMenu, "Split View", "View Split", gApp.GetActiveDocumentPanel() == null);

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(windowMenu);
		menuWidget.Init(mWindowButton, 0, mHeight);	
	}

	void ShowHelpMenu()
	{
		let subMenu = new Menu();
		AddMenuItem(subMenu, "About", "About");

		let menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(subMenu);
		menuWidget.Init(mHelpButton, 0, mHeight);		
	}

	void ResizeComponents()
	{
		let font = DarkTheme.sDarkTheme.mSmallFont;
		let padding = GS!(20);
		float offset = 0;


		void ResizeButton(Button button)
		{
			let width = font.GetWidth(button.Label) + padding;
			button.Resize(offset, 0, width, mHeight);
			offset += width;
		}

		ResizeButton(mFileButton);
		ResizeButton(mEditButton);
		ResizeButton(mViewButton);
		ResizeButton(mBuildButton);
		ResizeButton(mDebugButton);
		ResizeButton(mTestButton);
		ResizeButton(mWindowButton);
		ResizeButton(mHelpButton);
	}

	public override void Resize(float x, float y, float width, float height)
	{
		base.Resize(x, y, width, height);
		ResizeComponents();
	}

	public override void Draw(Graphics g)
	{
		using (g.PushColor(DarkTheme.COLOR_BKG))
		{
			g.FillRect(0, 0, mWidth, mHeight);
		}
	}
}