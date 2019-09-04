using System; //abc
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme;
using Beefy.theme.dark;
using IDE.ui;
using Beefy.sys;
using Beefy.events;
using Beefy.geom;
using Beefy.res;
using System.Diagnostics;
using Beefy.utils;
using IDE.Debugger;
using IDE.Compiler;
using IDE.Util;
using System.Security.Cryptography;
using System.Text;
using IDE.util;

[AttributeUsage(.Method, .ReflectAttribute | .AlwaysIncludeTarget, ReflectUser=.All)]
struct IDECommandAttribute : Attribute
{

}

namespace IDE
{
    public class WindowData
    {
        public DrawLayer mOverlayLayer ~ delete _;
    }

	enum SourceShowType
	{
		ShowExisting,
		ShowExistingInActivePanel,
		New,
		Temp,			
	}

	enum Verbosity
	{
		Quiet,
		Minimal,
		Normal,
		Detailed,
		Diagnostics
	}

	class DeferredUserRequest
	{

	}

	class DeferredShowPCLocation : DeferredUserRequest
	{
		public int32 mStackIdx;

		public this(int32 stackIdx)
		{
			mStackIdx = stackIdx;
		}
	}

	enum BeefVerb
	{
		None,
		Open,
		New,
		OpenOrNew,
		Test
	}

	enum HotResolveState
	{
		None,
		Pending,
		PendingWithDataChanges
	}

	public class IDETabbedView : DarkTabbedView
	{
		public ~this()
		{
			if (gApp.mActiveDocumentsTabbedView == this)
				gApp.mActiveDocumentsTabbedView = null;
		}
	}

    public class IDEApp : BFApp
    {
		public static String sRTVersionStr = "042";

#if BF_PLATFORM_WINDOWS
		public static readonly String sPlatform64Name = "Win64";
		public static readonly String sPlatform32Name = "Win32";
#elif BF_PLATFORM_LINUX
		public static readonly String sPlatform64Name = "Linux64";
		public static readonly String sPlatform32Name = "Linux32";
#else
		public static readonly String sPlatform64Name = "Unknown64";
		public static readonly String sPlatform32Name = "Unknown32";
#endif

        public const uint32 cDialogOutlineLightColor = 0xFF404040;
        public const uint32 cDialogOutlineDarkColor = 0xFF202020;

		public static bool sExitTest;

		public Verbosity mVerbosity = .Detailed;
		public BeefVerb mVerb;
		public bool mDbgCompileDump;
		public int mDbgCompileIdx = -1;
		public String mDbgCompileDir ~ delete _;
		public String mDbgVersionedCompileDir ~ delete _;
		public DateTime mDbgHighestTime;

		//public ToolboxPanel mToolboxPanel;
		public Monitor mMonitor = new Monitor() ~ delete _;
        public Thread mMainThread;
        public PropertiesPanel mPropertiesPanel;
        public Font mTinyCodeFont ~ delete _;
        public Font mCodeFont ~ delete _;
        public Font mLargeFont ~ delete _;
		protected bool mInitialized;
		public bool mConfig_NoIR;
		public bool mFailed;
		public bool mLastTestFailed;
		public bool mLastCompileFailed;
		public bool mLastCompileSucceeded;
		public bool mLastCompileHadMessages;
		public bool mPauseOnExit;
		public bool mDbgDelayedAutocomplete;
		public BeefConfig mBeefConfig = new BeefConfig() ~ delete _;
		public List<String> mDeferredFails = new .() ~ DeleteContainerAndItems!(_);
		public String mInitialCWD = new .() ~ delete _;

		public Commands mCommands = new .() ~ delete _;
		public KeyChordState mKeyChordState ~ delete _;

        public WidgetWindow mMainWindow;
        public DarkDockingFrame mDockingFrame;
        public MainFrame mMainFrame;
        public GlobalUndoManager mGlobalUndoManager = new GlobalUndoManager() ~ delete _;
		public SourceControl mSourceControl = new SourceControl() ~ delete _;

        public WidgetWindow mPopupWindow;        
        
        public IDETabbedView mActiveDocumentsTabbedView;
        public static new IDEApp sApp;

		//public IPCHostManager mIPCHostManager;
        public Image mTransparencyGridImage ~ delete _;
        public Image mSquiggleImage ~ delete _;
        public Image mCircleImage ~ delete _;

        public OutputPanel mOutputPanel;
        public ImmediatePanel mImmediatePanel;
        public FindResultsPanel mFindResultsPanel;
        public WatchPanel mAutoWatchPanel;
        public WatchPanel mWatchPanel;
        public MemoryPanel mMemoryPanel;
        public CallStackPanel mCallStackPanel;
        public BreakpointPanel mBreakpointPanel;
		public ModulePanel mModulePanel;
        public ThreadPanel mThreadPanel;
		public ProfilePanel mProfilePanel;
        public ProjectPanel mProjectPanel;
		public ClassViewPanel mClassViewPanel;
		public Widget mLastActivePanel;
		public SourceViewPanel mLastActiveSourceViewPanel;
		public AutoCompletePanel mAutoCompletePanel;
        
        public Rect mRequestedWindowRect = Rect(64, 64, 1200, 1024);
        public WakaTime mWakaTime ~ delete _;

		public Settings mSettings = new Settings() ~ delete _;
        public Workspace mWorkspace = new Workspace() ~ delete _;
        public FileWatcher mFileWatcher = new FileWatcher() ~ delete _;
		public int mLastFileChangeId;
        public bool mHaveSourcesChangedInternallySinceLastCompile;
        public bool mHaveSourcesChangedExternallySinceLastCompile;

        public String mConfigName = new String() ~ delete _;
        public String mPlatformName = new String() ~ delete _;

        public Targets mTargets = new Targets() ~ delete _;
        public DebugManager mDebugger ~ delete _;
		public String mSymSrvStatus = new String() ~ delete _;
		public Action<String> mPendingDebugExprHandler = null;
		public bool mIsImmediateDebugExprEval;
        public BookmarkManager mBookmarkManager = new BookmarkManager() ~ delete _;
        public HistoryManager mHistoryManager = new HistoryManager() ~ delete _;
        public Stopwatch mCompileAndRunStopwatch ~ delete _;

		// The Beef build system only gets populated when a build is requested,
		//  but the Clang build system keeps up-to-date with the projects' files
		//  and watches for file changes
        public BfSystem mBfBuildSystem ~ delete _;
        public BfCompiler mBfBuildCompiler;
		public int mCompileSinceCleanCount;
		public BuildContext mBuildContext ~ delete _;
#if IDE_C_SUPPORT
        public ClangCompiler mDepClang ~ delete _;
#endif
		// The Beef resolve system is up-to-date with the projects' files, 
		//  but the Clang resolver only has open files in it
		public bool mNoResolve = false;
        public BfSystem mBfResolveSystem ~ delete _;
        public BfCompiler mBfResolveCompiler;
        public BfResolveHelper mBfResolveHelper ~ delete _;
#if IDE_C_SUPPORT
        public ClangCompiler mResolveClang;
#endif
        public SpellChecker mSpellChecker;
		public List<String> mExternalChangeDeferredOpen = new List<String>() ~ DeleteContainerAndItems!(_);
		public SettingHistoryManager mLaunchHistoryManager = new SettingHistoryManager(true) ~ delete _;
		public SettingHistoryManager mFindAndReplaceHistoryManager = new SettingHistoryManager(true) ~ delete _;

		List<Object> mIdleDeferredDeletes = new List<Object>() ~ delete _;
		public bool mCompilingBeef = false;
        public bool mWantsClean = false;
        public bool mWantsBeefClean = false;
		public bool mWantsRehupCallstack = false;
        public bool mDebugAutoBuild = false;
        public bool mDebugAutoRun = false;
		public bool mDisableBuilding;
		public int32 mDebugAutoShutdownCounter;
        public bool mTargetDidInitBreak = false;
        public bool mTargetHadFirstBreak = false;
        public bool mTargetStartWithStep = false;
        public Breakpoint mMainBreakpoint;
		public Breakpoint mMainBreakpoint2;
        public bool mInDisassemblyView;
		public bool mIsAttachPendingSourceShow;
		public bool mStepOverExternalFiles;

		public bool mRunningTestScript;
		public bool mExitWhenTestScriptDone = true;
		public ScriptManager mScriptManager = new ScriptManager() ~ delete _;
		public TestManager mTestManager;
        public bool mExecutionPaused = false;
		public HotResolveState mHotResolveState;
		public int mHotResolveTryIdx;
		public bool mDebuggerPerformingTask = false; // Executing an expression, loading from symbol server
        public int32 mForegroundTargetCountdown = 0;
		public int32 mDebuggerContinueIdx;

        public SysMenu mWindowMenu;
		public List<SourceViewPanel> mPostRemoveUpdatePanels = new .() ~ delete _;
		public List<String> mRecentlyDisplayedFiles = new List<String>() ~ DeleteContainerAndItems!(_);
        public List<SysMenu> mRecentlyDisplayedFilesMenuItems = new List<SysMenu>() ~ delete _;
        public Dictionary<BFWindow, WindowData> mWindowDatas = new Dictionary<BFWindow, WindowData>() ~ delete _;
		public Dictionary<String, FileEditData> mFileEditData = new Dictionary<String, FileEditData>() ~
            {
				for (var editDataPair in mFileEditData)
				{
					delete editDataPair.key;
					editDataPair.value.Deref();
				}
				delete _;
            };
		public int32 mFileDataDataRevision;
        
        /*public Point mLastAbsMousePos;        
		public Point mLastRelMousePos;
        public int32 mMouseStillTicks;
        public Widget mLastMouseWidget;*/
        public int32 mCloseDelay;
        public FileChangedDialog mFileChangedDialog;
        public FindAndReplaceDialog mFindAndReplaceDialog;
		public LaunchDialog mLaunchDialog;
        public SymbolReferenceHelper mSymbolReferenceHelper;
		public WrappedMenuValue mViewWhiteSpace = .(false);
		public bool mEnableGCCollect = true;
		public bool mDbgFastUpdate;
		public bool mTestEnableConsole = false;
		public bool mTestIncludeIgnored = false;
		public ProfileInstance mLongUpdateProfileId;
		public uint32 mLastLongUpdateCheck;
		public uint32 mLastLongUpdateCheckError;
		bool mRunTest;
		int mRunTestDelay;
		bool mEnableRunTiming = false; ///
		ProfileInstance mRunTimingProfileId;
		bool mDoingRunTiming;
		bool mProfileCompile = false;
		ProfileInstance mProfileCompileProfileId;

#if !CLI
		public IPCHelper mIPCHelper ~ delete _;
		public bool mIPCHadFocus;
#endif

		int32 mProcessAttachId;
#if BF_PLATFORM_WINDOWS
		Windows.EventHandle mProcessAttachHandle;
#endif
		String mCrashDumpPath ~ delete _;
		bool mShowedFirstDocument = false;

		class LaunchData
		{
			public String mTargetPath = new .() ~ delete _;
			public String mArgs ~ delete _;
			public String mWorkingDir ~ delete _;
			public bool mPaused = false;
		}
		LaunchData mLaunchData ~ delete _;

		DeferredUserRequest mDeferredUserRequest ~ delete _;
		enum DeferredOpenKind
		{
			None,
			File,
			Workspace,
			CrashDump,
			DebugSession
		}
        DeferredOpenKind mDeferredOpen;
		String mDeferredOpenFileName;

        public class ExecutionCmd
        {
            public bool mOnlyIfNotFailed;
        }

        public class BuildCompletedCmd : ExecutionCmd
        {
            public Stopwatch mStopwatch ~ delete _;
			public String mHotProjectName ~ delete _;
            public bool mFailed;
#if IDE_C_SUPPORT
            public HashSet<String> mClangCompiledFiles = new HashSet<String>() ~ DeleteContainerAndItems!(_);
#endif
        }

        class ProcessBfCompileCmd : ExecutionCmd
        {
            public BfPassInstance mBfPassInstance;
            public bool mRunAfter;
            public Project mHotProject;
            public Stopwatch mStopwatch ~ delete _;
			public Profiler mProfiler;
			public ProfileCmd mProfileCmd ~ delete _;
			public bool mHadBeef;

			public ~this()
			{
				NOP!();
			}
        }

		class ProfileCmd : ExecutionCmd
		{
			public int mThreadId;
			public String mDesc ~ delete _;
			public int mSampleRate;
		}

        class StartDebugCmd : ExecutionCmd
        {
			public bool mWasCompiled;

			public this()
			{
			}
        }

        public class TargetCompletedCmd : ExecutionCmd
        {
			public Project mProject;
			public bool mIsReady = true;

            public this(Project project)
            {
                mProject = project;
            }
        }

		public enum ArgsFileKind
		{
			None,
			UTF8,
			UTF16WithBom
		}

        public class ExecutionQueueCmd : ExecutionCmd
        {
            public String mFileName ~ delete _;
            public String mArgs  ~ delete _;
            public String mWorkingDir  ~ delete _;
			public Dictionary<String, String> mEnvVars ~ DeleteDictionyAndKeysAndItems!(_);
            public ArgsFileKind mUseArgsFile;
            public int32 mParallelGroup = -1;            
        }
        public List<ExecutionCmd> mExecutionQueue = new List<ExecutionCmd>() ~ DeleteContainerAndItems!(_);

        public class ExecutionInstance
        {
            public SpawnedProcess mProcess /*~ delete _*/;
            public List<String> mDeferredOutput = new List<String>() ~ DeleteContainerAndItems!(_);
            public Stopwatch mStopwatch = new Stopwatch() ~ delete _;
            public String mTempFileName ~ delete _;
            public int32 mParallelGroup = -1;
            public Task<String> mReadTask /*~ delete _*/;
            public Action<Task<String>> mOnReadTaskComplete /*~ delete _*/;
            public Task<String> mErrorTask /*~ delete _*/;
            public Action<Task<String>> mOnErrorTaskComplete /*~ delete _*/;
			public Monitor mMonitor = new Monitor() ~ delete _;

			public Thread mOutputThread;
			public Thread mErrorThread;

			public int? mExitCode;
			public bool mAutoDelete = true;
			public bool mCanceled;

			public ~this()
			{
				delete mProcess;
				/*if (mProcess != null)
					mProcess.Close();*/

				if (mOutputThread != null)
					mOutputThread.Join();
				delete mOutputThread;
				if (mErrorThread != null)
					mErrorThread.Join();
				delete mErrorThread;				

				delete mReadTask;
				delete mOnReadTaskComplete;
				delete mErrorTask;
				delete mOnErrorTaskComplete;
			}

			public void Cancel()
			{
				mCanceled = true;
				mProcess.Kill(0, .KillChildren);
			}
        }        
        List<ExecutionInstance> mExecutionInstances = new List<ExecutionInstance>() ~ DeleteContainerAndItems!(_);
        public int32 mHotIndex = 0;
        private int32 mStepCount;
        private int32 mNoDebugMessagesTick;

        public bool IsCompiling
        {
            get
            {
                return (mExecutionInstances.Count > 0) || (mExecutionQueue.Count > 0) || (mBuildContext != null);
            }
        }

		public bool EnableGCCollect
		{
			get
			{
				return mEnableGCCollect;
			}

			set
			{
				mEnableGCCollect = value;
				//GC.SetAutoCollectPeriod(mEnableGCCollect ? 2000 : -1);
				GC.SetAutoCollectPeriod(mEnableGCCollect ? 20 : -1);
				GC.SetCollectFreeThreshold(mEnableGCCollect ? 64*1024*1024 : -1);
			}
		}

        public this()
        {
            sApp = this;
			gApp = this;
			mMainThread = Thread.CurrentThread;

#if !CLI
			mDebugger = new DebugManager();
			mVerb = .OpenOrNew;
#endif
			//BfParser_Go();///

			mScriptManager.AddTarget(this);
        }

		public static this()
		{
			//Profiler.StartSampling();
		}

        public void GetBfSystems(List<BfSystem> systems)
        {
            systems.Add(mBfBuildSystem);
			if (mBfResolveSystem != null)
            	systems.Add(mBfResolveSystem);
        }

        public void GetBfCompilers(List<BfCompiler> compiler)
        {
            compiler.Add(mBfBuildCompiler);
            compiler.Add(mBfResolveCompiler);
        }

        public bool HaveSourcesChanged()
        {
            return mHaveSourcesChangedInternallySinceLastCompile || mHaveSourcesChangedExternallySinceLastCompile;
        }

		public void MarkWatchesDirty()
		{
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.MarkWatchesDirty(false);
				});
		}

        void PCChanged()
        {
            var disasmPanel = TryGetDisassemblyPanel();
            if (disasmPanel != null)
                disasmPanel.mLocationDirty = true;
            mDebugger.mCallStackDirty = true;
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.MarkWatchesDirty(true, !mTargetHadFirstBreak);
				});
            mMemoryPanel.MarkViewDirty();
            mCallStackPanel.MarkCallStackDirty();
            mThreadPanel.MarkThreadsDirty();
            mDebugger.CheckCallStack();


			// If we do a "PCChanged" from the execution of a method in the Immediate panel, don't move
			//  the focus away from the Immediate edit widget
			bool setFocus = true;
			for (var window in mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if ((widgetWindow != null) && (widgetWindow.mHasFocus))
				{
					if (widgetWindow.mFocusWidget is ImmediateWidget)
					{
						setFocus = false;
					}
				}
			}
			if (mRunningTestScript)
				setFocus = true;

            ShowPCLocation(mDebugger.mActiveCallStackIdx, false, false, false, setFocus);
        }

        public ~this()
        {
#if !CLI
			if (!mRunningTestScript)
			{
				mSettings.Save();
				SaveDefaultLayoutData();
			}
#endif

			/*WithTabs(scope (tabButton) =>
            	{
					var panel = tabButton.mContent as Panel;
					if (panel != null)
					{
						if (!panel.mAutoDelete)
						{
							// Is part of the panel list, clear mContent so we don't double-delete
							tabButton.mContent = null;
						}
					}
                });*/

			mixin RemoveAndDelete(var widget)
			{
				Widget.RemoveAndDelete(widget);
				widget = null;
			}
			
			RemoveAndDelete!(mProjectPanel);
			RemoveAndDelete!(mClassViewPanel);
			RemoveAndDelete!(mOutputPanel);
			RemoveAndDelete!(mImmediatePanel);
			RemoveAndDelete!(mFindResultsPanel);
			RemoveAndDelete!(mAutoWatchPanel);
			RemoveAndDelete!(mWatchPanel);
			RemoveAndDelete!(mMemoryPanel);
			RemoveAndDelete!(mCallStackPanel);
			RemoveAndDelete!(mBreakpointPanel);
			RemoveAndDelete!(mModulePanel);
			RemoveAndDelete!(mThreadPanel);
			RemoveAndDelete!(mProfilePanel);
			RemoveAndDelete!(mPropertiesPanel);
			RemoveAndDelete!(mAutoCompletePanel);

			if (mSymbolReferenceHelper != null)
				mSymbolReferenceHelper.Close();

			if (mBfBuildCompiler != null)
            	mBfBuildCompiler.CancelBackground();
			if (mBfResolveCompiler != null)
            	mBfResolveCompiler.CancelBackground();
#if IDE_C_SUPPORT
            mDepClang.CancelBackground();
            mResolveClang.CancelBackground();
#endif

            /*if (mMainBreakpoint != null)
            {
                mMainBreakpoint.Dispose();
                mMainBreakpoint = null;
            }*/

            /*delete mBfBuildCompiler;            
            delete mBfBuildSystem;
            delete mDepClang;
            
            
            delete mBfResolveCompiler;
            delete mBfResolveSystem;
            delete mResolveClang;

            delete mSpellChecker;

			//base.Dispose();

            delete mDebugger;
            delete ThemeFactory.mDefault;*/

			for (var val in mWindowDatas.Values)
				delete val;

			ProcessIdleDeferredDeletes();

			// Get rid of panels
			ProcessDeferredDeletes();

			delete mBfBuildCompiler;
			delete mBfResolveCompiler;
#if IDE_C_SUPPORT
			delete mResolveClang;
#endif
			delete mSpellChecker;

			// Clear these out, for delete ordering purposes
			ProcessDeferredDeletes();
        }

		public bool IsCrashDump
		{
			get
			{
				return mCrashDumpPath != null;
			}
		}

		public override void ShutdownCompleted()
		{
			base.ShutdownCompleted();
		}

        public override void Shutdown()
        {
			/*if (mIPCHostManager != null)
				mIPCHostManager.Dispose();*/

			if ((mDebugger != null) && (mDebugger.mIsRunning))
			{
				mDebugger.DisposeNativeBreakpoints();
				mDebugger.Detach();
				mDebugger.mIsRunning = false;
				mExecutionPaused = false;
			}

            base.Shutdown();                       
        }

        public override void Run()
        {
            base.Run();
        }

		public void Cmd_NewFileView()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				ShowSourceFile(sourceViewPanel.mFilePath, sourceViewPanel.mProjectSource, SourceShowType.New);
			}
		}

		public void IdleDeferDelete(Object obj)
		{
			mIdleDeferredDeletes.Add(obj);
		}

		void ProcessIdleDeferredDeletes()
		{
			if (((mBfBuildCompiler != null) && mBfBuildCompiler.HasQueuedCommands()) ||
				((mBfResolveCompiler != null) && (mBfResolveCompiler.HasQueuedCommands()))
#if IDE_C_SUPPORT
                || (mDepClang.HasQueuedCommands()) ||
				(mResolveClang.HasQueuedCommands())
#endif
                )
				return;

			for (var obj in mIdleDeferredDeletes)
				delete obj;
			mIdleDeferredDeletes.Clear();
		}

        public void DoOpenFile()
        {
#if !CLI
			String fullDir = scope .();
			let sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				Path.GetDirectoryPath(sourceViewPanel.mFilePath, fullDir);
			}
			else if ((gApp.mDebugger.mRunningPath != null) && (!mWorkspace.IsInitialized))
			{
				Path.GetDirectoryPath(gApp.mDebugger.mRunningPath, fullDir);
			}

			var fileDialog = scope OpenFileDialog();
			fileDialog.Title = "Open File";
			fileDialog.Multiselect = true;
			fileDialog.ValidateNames = true;
			if (!fullDir.IsEmpty)
				fileDialog.InitialDirectory = fullDir;
			mMainWindow.PreModalChild();
			if (fileDialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() == .OK)
			{
				for (String openFileName in fileDialog.FileNames)
				{
					//openFileName.Replace('\\', '/');
					AddRecentFile(.OpenedFile, openFileName);
					ShowSourceFile(openFileName);
				}
			}
#endif
        }

		public void DoOpenWorkspace()
		{		
#if !CLI
			if (mDeferredOpenFileName != null)
			{
				CloseWorkspace();

				mWorkspace.mDir = new String();
				Path.GetDirectoryPath(mDeferredOpenFileName, mWorkspace.mDir);
				mWorkspace.mName = new String();
				Path.GetFileName(mWorkspace.mDir, mWorkspace.mName);

				LoadWorkspace();
				FinishShowingNewWorkspace();

				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}

			var folderDialog = scope FolderBrowserDialog();
			if (folderDialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() == .OK)
			{
				var selectedPath = scope String..AppendF(folderDialog.SelectedPath);
				selectedPath.Append(Path.DirectorySeparatorChar);
				selectedPath.Append("BeefSpace.toml");
				String.NewOrSet!(mDeferredOpenFileName, selectedPath);
				mDeferredOpen = .Workspace;
			}

#endif
		}

		public void DoOpenDebugSession(StringView filePath)
		{
			let data = scope StructuredData();
			if (data.Load(filePath) case .Err)
			{
				CreateDefaultLayout();
				OutputErrorLine("Failed to load debug session '{}'", filePath);
				return;
			}

			AddRecentFile(.OpenedDebugSession, filePath);

			NewDebugSession(true);

			let project = mWorkspace.mProjects[0];
			project.mProjectPath.Set(filePath);

			LoadWorkspaceUserData(data);

			using (data.Open("DebugSession"))
			{
				project.Deserialize(data);
			}
		}

		public void DoOpenDebugSession()
		{		
#if !CLI
			if (mDeferredOpenFileName != null)
			{
				CloseWorkspace();

				DoOpenDebugSession(mDeferredOpenFileName);
				FinishShowingNewWorkspace(false);

				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}

			var fileDialog = scope OpenFileDialog();
			fileDialog.Title = "Open Debug Session";
			fileDialog.SetFilter("Debug Session (*.bfdbg)|*.bfdbg|All files (*.*)|*.*");
			fileDialog.ValidateNames = true;
			mMainWindow.PreModalChild();            
			if (fileDialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() == .OK)
			{
				for (String openFileName in fileDialog.FileNames)
				{
					String.NewOrSet!(mDeferredOpenFileName, openFileName);
					mDeferredOpen =.DebugSession;
					break;
				}
			}

#endif
		}

		public void OpenCrashDump(StringView path)
		{
			AddRecentFile(.OpenedCrashDump, path);

			StopDebugging();
			String.NewOrSet!(mCrashDumpPath, path);

			if (mDebugger.OpenMiniDump(mCrashDumpPath))
			{
				mDebugger.mIsRunning = true;
				mExecutionPaused = false; // Make this false so we can detect a Pause immediately
			}
			else
			{
				Fail(StackStringFormat!("Failed to load minidump '{0}'", mCrashDumpPath));
				DeleteAndNullify!(mCrashDumpPath);
			}
		}

		public void DoOpenCrashDump()
		{		
#if !CLI
			/*if (mDeferredOpenFileName != null)
			{

				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}*/

			var fileDialog = scope OpenFileDialog();
			fileDialog.Title = "Open Crash Dump";
			fileDialog.SetFilter("Crash Dump (*.dmp)|*.dmp|All files (*.*)|*.*");
			fileDialog.ValidateNames = true;
			mMainWindow.PreModalChild();            
			if (fileDialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() == .OK)
			{
				for (String openFileName in fileDialog.FileNames)
				{
					OpenCrashDump(openFileName);
					break;
				}
			}
#endif
		}

		public void CheckProjectRelativePath(String path)
		{
			if (path.Length < 2)
				return;

			if ((path[0] == '\\') || (path[0]  == '/') || (path[1] == ':'))
				return;

			for (var project in mWorkspace.mProjects)
			{
				String testPath = scope String();
				testPath.Append(project.mProjectDir);
                testPath.Append(Path.DirectorySeparatorChar);
                testPath.Append(path);
				if (File.Exists(testPath))
				{
					path.Set(testPath);
					return;
				}
			}
		}

		public void PerformAction(String cmdStr)
		{
			List<String> cmds = scope List<String>();

			int strIdx = 0;
			while (true)
			{
				String str = scope:: String();
				Utils.ParseSpaceSep(cmdStr, ref strIdx, str);
				if (str.IsEmpty)
					break;
				cmds.Add(str);
			}

			if (cmds.Count == 0)
				return;

			switch (cmds[0])
			{
			case "ShowCode":
				if (cmds.Count < 3)
					break;
				if (var sourceViewPanel = GetActiveSourceViewPanel(true))
					sourceViewPanel.RecordHistoryLocation();

				int32 char8Idx = int32.Parse(cmds[2]).GetValueOrDefault();
				var sourceViewPanel = ShowSourceFile(cmds[1], null, SourceShowType.Temp);
				if (sourceViewPanel == null)
				    return;
				var editWidgetContent = sourceViewPanel.mEditWidget.mEditWidgetContent;
				int line;
				int lineChar;
				editWidgetContent.GetLineCharAtIdx(char8Idx, out line, out lineChar);
				sourceViewPanel.ShowFileLocation(char8Idx, .Always);
			case "ShowCodeAddr":
				var sourceViewPanel = GetActiveSourceViewPanel(true);
				if (sourceViewPanel != null)
					sourceViewPanel.RecordHistoryLocation();

				int64 addr = int64.Parse(cmds[1], System.Globalization.NumberStyles.HexNumber).GetValueOrDefault();
				String fileName = scope String();
				int lineNum = 0;
				int column;
				int hotIdx;
				int defLineStart;
				int defLineEnd;
				mDebugger.GetCodeAddrInfo((int)addr, fileName, out hotIdx, out defLineStart, out defLineEnd, out lineNum, out column);
				if (fileName.Length > 0)
				{
					int showHotIdx = -1;
					if ((defLineStart == -1) || (sourceViewPanel == null) || (sourceViewPanel.HasTextChangedSinceCompile(defLineStart, defLineEnd, hotIdx)))
						showHotIdx = hotIdx;

					ShowSourceFileLocation(fileName, showHotIdx, hotIdx, lineNum, column, LocatorType.Always);
				}
				else if (addr != 0)
				{
					var disassemblyPanel = ShowDisassemblyPanel(true);
					disassemblyPanel.Show(addr);
				}
			}
		}

        public Dialog QuerySaveFiles(List<String> changedList, WidgetWindow window = null)
        {            
            Dialog aDialog;
            if (changedList.Count == 1)
            {
                aDialog = ThemeFactory.mDefault.CreateDialog("Save file?", StackStringFormat!("{0} has been modified. Save before closing?", changedList[0]), DarkTheme.sDarkTheme.mIconWarning);
            }
            else
            {
                String text = scope String("The following files have been modified: ");
                text.Join(", ", changedList.GetEnumerator());
                text .Append(". Save before closing?");
                aDialog = ThemeFactory.mDefault.CreateDialog("Save files?", text, DarkTheme.sDarkTheme.mIconWarning);
            }

            return aDialog;            
        }

		public bool CheckCloseWorkspace(BFWindow window, Action onSave, Action onNoSave, Action onCancel)
		{
			void DeleteDelegates()
			{
				delete onSave;
				delete onNoSave;
				delete onCancel;
			}

			if (mStopping)
			{
				DeleteDelegates();
				return true;
			}

			List<String> changedList = scope List<String>();

			if (mWorkspace.mHasChanged)
			{
				var path = scope String();
				GetWorkspaceFileName(path);
				var fileName = new String();
				Path.GetFileName(path, fileName);
				if (fileName.IsEmpty)
					fileName.Append("Workspace");
			    changedList.Add(fileName);
			}
			for (var project in mWorkspace.mProjects)
			    if (project.mHasChanged)
				{
					var fileName = new String();
					Path.GetFileName(project.mProjectPath, fileName);
					if (fileName.IsEmpty)
						fileName.Append(project.IsDebugSession ? "Debug Session" : "Project");
			        changedList.Add(fileName);
				}
			mWorkspace.WithProjectItems(scope (projectItem) =>
				{
					var projectSource = projectItem as ProjectSource;
					if (projectSource != null)
					{
						if ((projectSource.mEditData != null) && (projectSource.mEditData.HasTextChanged()))
						{
							var fileName = new String();
							Path.GetFileName(projectSource.mPath, fileName);														
							changedList.Add(fileName);
						}
					}
					
			    });
			WithSourceViewPanels(scope (sourceViewPanel) =>
			    {
			        if (sourceViewPanel.HasUnsavedChanges())
					{
						var fileName = scope String();
						Path.GetFileName(sourceViewPanel.mFilePath, fileName);
						if (!changedList.Contains(fileName))
			            	changedList.Add(new String(fileName));
					}
			    });

			if (changedList.Count == 0)
			{
				DeleteDelegates();
				if (!mRunningTestScript)
			    	SaveWorkspaceUserData(false);
			    return true;
			}

			var aDialog = QuerySaveFiles(changedList);
			DeleteAndClearItems!(changedList);

			aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) =>
				{
					onSave();
				} ~ delete onSave);
			aDialog.AddButton("Don't Save", new (evt) =>
				{
				    onNoSave();
				} ~ delete onNoSave);

			aDialog.mEscButton = aDialog.AddButton("Cancel", new (evt) =>
				{
					if (onCancel != null)
						onCancel();
				} ~ delete onCancel);
			aDialog.PopupWindow((WidgetWindow)window ?? mMainWindow);
			return false;
		}

        public bool AllowClose(BFWindow window)
        {
			if (mRunningTestScript)
				return true;

         	return CheckCloseWorkspace(window,
				new () =>
				{
					// We use a close delay after saving so the user can see we actually saved before closing down
					if (SaveAll())
						mCloseDelay = 30;
				},
				new () =>
				{
					SaveWorkspaceUserData(false);
					mMainWindow.Close(true);
				}, null);
        }

        public bool SecondaryAllowClose(BFWindow window)
        {
			if (mRunningTestScript)
				return true;

            List<String> changedList = scope List<String>();
			defer ClearAndDeleteItems(changedList);
            WithSourceViewPanels(scope (sourceViewPanel) =>
                {
                    if ((sourceViewPanel.mWidgetWindow == window) && (sourceViewPanel.HasUnsavedChanges()))
					{
						var fileName = new String();
						Path.GetFileName(sourceViewPanel.mFilePath, fileName);
                        changedList.Add(fileName);
					}
                });
            if (changedList.Count == 0)
                return true;
            var aDialog = QuerySaveFiles(changedList, (WidgetWindow)window);
            aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) =>
            {
                bool hadError = false;
				// We use a close delay after saving so the user can see we actually saved before closing down
				var _this = this;
                WithSourceViewPanels(scope [&] (sourceViewPanel) =>
                	{
                        if ((!hadError) && (sourceViewPanel.mWidgetWindow == window) && (sourceViewPanel.HasUnsavedChanges()))
                            if (!_this.SaveFile(sourceViewPanel))
                                hadError = true;
                    });
                if (hadError)
                    return;
                mMainWindow.SetForeground();
                window.Close(true);                
            });
            aDialog.AddButton("Don't Save", new (evt) =>
            {
                mMainWindow.SetForeground();
                window.Close(true);                
            });

            aDialog.mEscButton = aDialog.AddButton("Cancel");
            aDialog.PopupWindow((WidgetWindow)window ?? mMainWindow);
            return false;
        }

        public SourceViewPanel GetActiveSourceViewPanel(bool includeLastActive = false)
        {
			if (mRunningTestScript)
				return mLastActiveSourceViewPanel;

            var activePanel = GetActiveDocumentPanel();
            var sourceViewPanel = activePanel as SourceViewPanel;
			if (sourceViewPanel != null)
				return sourceViewPanel.GetActivePanel();
			if ((mLastActiveSourceViewPanel != null) && (includeLastActive))
				return mLastActiveSourceViewPanel.GetActivePanel();
			return null;
        }

        public TextPanel GetActiveTextPanel()
        {
            var activePanel = GetActivePanel();
            return activePanel as TextPanel;
        }

        public void RefreshVisibleViews(SourceViewPanel excludeSourceViewPanel = null)
        {
            WithSourceViewPanels(scope (sourceViewPanel) =>
                {
                    if ((sourceViewPanel.mParent != null) && (sourceViewPanel != excludeSourceViewPanel))
                        sourceViewPanel.QueueFullRefresh(true);
                });
        }

		public bool SaveFile(ProjectSource projectSource)
		{
			if (projectSource.mEditData.HasTextChanged())
			{
				for (var user in projectSource.mEditData.mEditWidget.mEditWidgetContent.mData.mUsers)
				{
					user.MarkDirty();
				}

				using (mFileWatcher.mMonitor.Enter())
				{
				    String path = scope String();
					projectSource.GetFullImportPath(path);
				    String text = scope String();
				    projectSource.mEditData.mEditWidget.GetText(text);
					if (!SafeWriteTextFile(path, text, true, projectSource.mEditData.mLineEndingKind))
						return false;

					mFileWatcher.OmitFileChange(path, text);
				    projectSource.mEditData.mLastFileTextVersion = projectSource.mEditData.mEditWidget.Content.mData.mCurTextVersionId;
#if IDE_C_SUPPORT
				    mDepClang.FileSaved(path);
#endif
				    
			        if (mWakaTime != null)			        
			            mWakaTime.QueueFile(path, projectSource.mProject.mProjectName, true);				    
				}
			}
			return true;
		}

		public bool SafeWriteTextFile(StringView path, StringView text, bool showErrors = true, LineEndingKind lineEndingKind = .Default)
		{
			if (mWorkspace.IsSingleFileWorkspace)
			{
				if (mWorkspace.mCompositeFile.IsIncluded(path))
				{
					mWorkspace.mCompositeFile.Set(path, text);
					return true;
				}
			}

			StringView useText = text;

			if (lineEndingKind != .Lf)
			{
				var str = scope:: String()..Append(text);
				if (lineEndingKind == .Cr)
					str.Replace('\n', '\r');
				else
					str.Replace("\n", "\r\n");
				useText = str;
			}

			if (Utils.WriteTextFile(path, useText) case .Err)
			{
				if (gApp.mSettings.mEditorSettings.mPerforceAutoCheckout)
					mSourceControl.Checkout(path);
				Thread.Sleep(10);
				if (Utils.WriteTextFile(path, useText) case .Err)
				{
					Thread.Sleep(100);
					if (Utils.WriteTextFile(path, useText) case .Err)
					{
						if (showErrors)
					    	Fail(StackStringFormat!("Failed to write file '{0}'", path));
					    return false;
					}
				}
			}

			return true;
		}

		public Result<void, FileError> LoadTextFile(String fileName, String outBuffer, bool autoRetry = true, delegate void() onPreFilter = null)
		{
			if (mWorkspace.IsSingleFileWorkspace)
			{
				if (mWorkspace.mCompositeFile.IsIncluded(fileName))
				{
					mWorkspace.mCompositeFile.Get(fileName, outBuffer);
					if (onPreFilter != null)
						onPreFilter();
					return .Ok;
				}
			}

			return Utils.LoadTextFile(fileName, outBuffer, autoRetry, onPreFilter);
		}

		public bool SaveFileAs(SourceViewPanel sourceViewPanel)
		{
#if !CLI
			SaveFileDialog dialog = scope .();
			//dialog.ValidateNames = true;

			if (sourceViewPanel.mFilePath != null)
			{
				String ext = scope .();
				Path.GetExtension(sourceViewPanel.mFilePath, ext);
				dialog.DefaultExt = ext;

				String fileName = scope .();
				Path.GetFileName(sourceViewPanel.mFilePath, fileName);
				dialog.FileName = fileName;
			}
			//dialog.SetFilter("Beef projects (BeefProj.toml)|BeefProj.toml");

			let activeWindow = GetActiveWindow();
			dialog.OverwritePrompt = true;
			if (dialog.ShowDialog(activeWindow).GetValueOrDefault() != .OK)
				return false;

			if (dialog.FileNames.Count != 1)
				return false;
			String filePath = dialog.FileNames[0];
			// Same path?
			if ((sourceViewPanel.mFilePath != null) && (Path.Equals(filePath, sourceViewPanel.mFilePath)))
				return true;

			if (!SaveFile(sourceViewPanel, filePath))
				return false;

			// Kinda hacky - we lose all view information...
			CloseDocument(sourceViewPanel);
			ShowSourceFile(filePath);
#endif
			return true;
		}

        public bool SaveFile(SourceViewPanel sourceViewPanel, String forcePath = null)
        {
            if ((sourceViewPanel.HasUnsavedChanges()) || (forcePath != null))
            {
				if ((forcePath == null) && (sourceViewPanel.mFilePath == null))
				{
					return SaveFileAs(sourceViewPanel);
				}

				var lineEndingKind = LineEndingKind.Default;
				if (sourceViewPanel.mEditData != null)
				{
					for (var user in sourceViewPanel.mEditData.mEditWidget.mEditWidgetContent.mData.mUsers)
					{
						user.MarkDirty();
					}
					lineEndingKind = sourceViewPanel.mEditData.mLineEndingKind;
				}

				// Lock file watcher to synchronize the 'file changed' notification so we don't 
				//  think a file was externally saved
                using (mFileWatcher.mMonitor.Enter())
                {
                    String path = forcePath ?? sourceViewPanel.mFilePath;
                    String text = scope String();
                    sourceViewPanel.mEditWidget.GetText(text);

					if (!SafeWriteTextFile(path, text, true, lineEndingKind))
						return false;

					mFileWatcher.OmitFileChange(path, text);
					if (forcePath == null)
                    	sourceViewPanel.FileSaved();
#if IDE_C_SUPPORT
                    mDepClang.FileSaved(path);
#endif

                    if (sourceViewPanel.mProjectSource != null)
                    {
                        if (mWakaTime != null)
                        {
                            mWakaTime.QueueFile(path, sourceViewPanel.mProjectSource.mProject.mProjectName, true);
                        }
                    }
                }
            }
            return true;
        }

        public ProjectSource FindProjectItem(ProjectFolder projectFolder, String relPath)
        {
            for (var childItem in projectFolder.mChildItems)
            {
                ProjectSource projectSource = childItem as ProjectSource;
                if (projectSource != null)
                {
                    if (String.Equals(projectSource.mPath, relPath, Environment.IsFileSystemCaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase))
                        return projectSource;
                }

                ProjectFolder childFolder = childItem as ProjectFolder;
                if (childFolder != null)
                {
                    projectSource = FindProjectItem(childFolder, relPath);
                    if (projectSource != null)
                        return projectSource;
                }
            }
            return null;
        }

        public Project FindProjectByName(String projectName)
        {
            for (var project in mWorkspace.mProjects)
                if (project.mProjectName == projectName)
                    return project;
            return null;
        }

        public ProjectSource FindProjectSourceItem(String filePath)
        {
            for (var project in mWorkspace.mProjects)
            {
                String relPath = scope String();
                project.GetProjectRelPath(filePath, relPath);

                var projectItem = FindProjectItem(project.mRootFolder, relPath);
                if (projectItem != null)
                    return projectItem;
            }
            return null;
        }

		///
       
        void SerializeWindow(StructuredData data, WidgetWindow window)
        {
            data.Add("X", window.mX);
            data.Add("Y", window.mY);
            data.Add("Width", window.mWindowWidth);
            data.Add("Height", window.mWindowHeight);
        }

        void SerializeTabbedView(StructuredData data, DarkTabbedView tabbedView, bool serializeDocs)
        {
            if (tabbedView == mActiveDocumentsTabbedView)
                data.Add("DefaultDocumentsTabbedView", true);

            data.Add("Type", "TabbedView");
            using (data.CreateArray("Tabs"))
            {
                for (var tabWidget in tabbedView.mTabs)
                {
					if (!serializeDocs)
					{
						if (tabWidget.mContent is SourceViewPanel)
							continue;
					}

                    using (data.CreateObject())
                    {
						if (tabWidget.mIsActive)
							data.Add("Active", true);

                        data.Add("TabLabel", tabWidget.mLabel);
                        data.Add("TabWidth", tabWidget.mWantWidth / DarkTheme.sScale);

						if (var watchPanel = tabWidget.mContent as WatchPanel)
						{
							watchPanel.Serialize(data, serializeDocs);
						}
                        else if (var panel = tabWidget.mContent as Panel)
                        {
                            panel.Serialize(data);
                        }
                        else
                        {
                            var innerTabbedView = tabWidget.mContent as DarkTabbedView;
                            if (innerTabbedView != null)
                            {                                
                                SerializeTabbedView(data, innerTabbedView, serializeDocs);
                            }
                        }
                    }
                }
            }
        }

        void SerializeDockingFrame(StructuredData data, DockingFrame dockingFrame, bool serializeDocs)
        {
            data.Add("Type", "DockingFrame");
            data.Add("SplitType", (int32)dockingFrame.mSplitType);
            using (data.CreateArray("DockedWidgets"))
            {
                for (var dockedWidget in dockingFrame.mDockedWidgets)
                {
                    using (data.CreateObject())
                    {
                        if (dockedWidget.mIsFillWidget)
                            data.Add("IsFillWidget", true);
						if (!dockedWidget.mAutoClose)
							data.Add("Permanent", true);
                        data.Add("RequestedWidth", dockedWidget.mRequestedWidth);
                        data.Add("RequestedHeight", dockedWidget.mRequestedHeight);
                        data.ConditionalAdd("SizePriority", dockedWidget.mSizePriority, 0);

                        if (dockedWidget is DarkDockingFrame)
                        {
                            var innerDockingFrame = (DarkDockingFrame)dockedWidget;
                            SerializeDockingFrame(data, innerDockingFrame, serializeDocs);
                        }

                        if (dockedWidget is DarkTabbedView)
                        {
                            var tabbedView = (DarkTabbedView)dockedWidget;
                            SerializeTabbedView(data, tabbedView, serializeDocs);
                        }
                    }
                }
            }
        }

		bool SaveDefaultLayoutData()
		{
			StructuredData sd = scope StructuredData();

			sd.CreateNew();
			sd.Add("FileVersion", 1);

			using (sd.CreateObject("MainWindow"))
			{
			    SerializeWindow(sd, mMainWindow);
			}

			using (sd.CreateObject("MainDockingFrame"))
			{
			    SerializeDockingFrame(sd, mDockingFrame, false);
			}

			String projectUserFileName = scope String();
			GetDefaultLayoutDataFileName(projectUserFileName);
			String dataStr = scope String();
			sd.ToTOML(dataStr);

			return SafeWriteTextFile(projectUserFileName, dataStr, true);
		}

		bool LoadDefaultLayoutData()
		{
			String projectUserFileName = scope String();
			GetDefaultLayoutDataFileName(projectUserFileName);
			
			StructuredData sd = scope .();
			if (sd.Load(projectUserFileName) case .Err)
				return false;

			return LoadWorkspaceUserData(sd);
		}

		void SaveWorkspaceUserData(StructuredData sd)
		{
			sd.Add("FileVersion", 1);
			sd.Add("LastConfig", mConfigName);
			sd.Add("LastPlatform", mPlatformName);

			using (sd.CreateObject("MainWindow"))
			{
			    SerializeWindow(sd, mMainWindow);
			}

			using (sd.CreateObject("MainDockingFrame"))
			{
			    SerializeDockingFrame(sd, mDockingFrame, true);
			}

			using (sd.CreateArray("RecentFilesList"))
			{
			    for (var recentFile in mRecentlyDisplayedFiles)
			        sd.Add(recentFile);
			}

			using (sd.CreateArray("Breakpoints"))
			{
			    for (var breakpoint in mDebugger.mBreakpointList)
			    {
			        if ((breakpoint.mFileName != null) || (breakpoint.mIsMemoryBreakpoint) || (breakpoint.mSymbol != null))
			        {
			            using (sd.CreateObject())
			            {                    
			                if (breakpoint.mFileName != null)
			                {
			                    sd.Add("File", breakpoint.mFileName);
			                    sd.Add("Line", breakpoint.mLineNum);
			                    sd.Add("Column", breakpoint.mColumn);
								if (breakpoint.mInstrOffset != -1)
									sd.Add("InstrOffset", breakpoint.mInstrOffset);
			                }
							if (breakpoint.mSymbol != null)
								sd.Add("Symbol", breakpoint.mSymbol);
			                if (breakpoint.mIsMemoryBreakpoint)
			                    sd.Add("MemoryWatchExpression", breakpoint.mMemoryWatchExpression);
							if (breakpoint.mCondition != null)
								sd.Add("Condition", breakpoint.mCondition);
							if (breakpoint.mLogging != null)
								sd.Add("Logging", breakpoint.mLogging);
							sd.ConditionalAdd("BreakAfterLogging", breakpoint.mBreakAfterLogging, false);
							sd.ConditionalAdd("HitCountBreak", breakpoint.mHitCountBreakKind, .None);
							sd.ConditionalAdd("HitCountTarget", breakpoint.mHitCountTarget, 0);
							if (breakpoint.mDisabled)
								sd.Add("Disabled", true);
							if (breakpoint.mThreadId != -1)
								sd.Add("HasThreadId", true);
			            }
			        }
			    }
			}

			using (sd.CreateArray("Bookmarks"))
			{
			    for (var bookmark in mBookmarkManager.mBookmarkList)
			    {
			        if (bookmark.mFileName != null)
			        {
			            using (sd.CreateObject())
			            {
			                sd.Add("File", bookmark.mFileName);
			                sd.Add("Line", bookmark.mLineNum);
			                sd.Add("Column", bookmark.mColumn);
			            }
			        }
			    }
			}

			using (sd.CreateObject("DebuggerDisplayTypes"))
			{
				var displayTypeNames = scope String();
				mDebugger.GetDisplayTypeNames(displayTypeNames);
			    var referenceIds = String.StackSplit!(displayTypeNames, '\n');
			    for (var referenceId in referenceIds)                
			    {                    
			        if (!referenceId.StartsWith("0", StringComparison.Ordinal))
			        {
			            DebugManager.IntDisplayType intDisplayType;
			            DebugManager.MmDisplayType mmDisplayType;
			            mDebugger.GetDisplayTypes(referenceId, out intDisplayType, out mmDisplayType);
			            using (sd.CreateObject(referenceId))
			            {
			                sd.Add("IntDisplayType", intDisplayType);
			                sd.Add("MmDisplayType", mmDisplayType);
			            }
			        }
			    }
			}

			using (sd.CreateArray("StepFilters"))
			{
			    for (var stepFilter in mDebugger.mStepFilterList.Values)
			    {
					if (stepFilter.mIsGlobal)
						continue;
					if (stepFilter.mKind == .Filtered)
						sd.Add(stepFilter.mFilter);
			    }
				sd.RemoveIfEmpty();
			}

			using (sd.CreateArray("StepNotFilters"))
			{
			    for (var stepFilter in mDebugger.mStepFilterList.Values)
			    {
					if (stepFilter.mIsGlobal)
						continue;
					if (stepFilter.mKind == .NotFiltered)
						sd.Add(stepFilter.mFilter);
			    }
				sd.RemoveIfEmpty();
			}
		}

        bool SaveWorkspaceUserData(bool showErrors = true)
        {
			if (mWorkspace.IsDebugSession)
			{
				bool hasUnsavedProjects = false;
				for (let project in mWorkspace.mProjects)
					if (project.mHasChanged)
						hasUnsavedProjects = true;

				// If we purposely abandoned changes then don't save now
				if (!hasUnsavedProjects)
					SaveDebugSession();
				return true;
			}

            StructuredData sd = scope StructuredData();

			sd.CreateNew();
			SaveWorkspaceUserData(sd);

            String projectUserFileName = scope String();
            GetWorkspaceUserDataFileName(projectUserFileName);
            String jsonString = scope String();
			sd.ToTOML(jsonString);

			if (projectUserFileName.IsEmpty)
				return false;

            return SafeWriteTextFile(projectUserFileName, jsonString, showErrors);
        }
        
        bool GetWorkspaceUserDataFileName(String outResult)
        {
			if (mWorkspace.mDir == null)
				return false;

			if (mWorkspace.mCompositeFile != null)
			{
				outResult.Append(mWorkspace.mCompositeFile.mFilePath, ".bfuser");
			}
			else
			{
            	outResult.Append(mWorkspace.mDir, "/BeefSpace_User.toml");
				var legacyName = scope String(mWorkspace.mDir, "/BeefUser.toml");

				if (File.Exists(legacyName))
				{
					File.Move(legacyName, outResult).IgnoreError();
				}
			}

			// Temporary for legacy
			if (!mInitialized)
			{
				String legacyName = scope String();
				legacyName.Append(mWorkspace.mDir, "/", mWorkspace.mName, ".bfuser");
				if (File.Exists(legacyName))
				{
					File.Move(legacyName, outResult).IgnoreError();
					File.Delete(legacyName).IgnoreError();
				}
			}

			return true;
        }

		void GetDefaultLayoutDataFileName(String outResult)
		{
			outResult.Append(mInstallDir, "/DefaultLayout.toml");
		}

        bool GetWorkspaceFileName(String outResult)
        {
			if (mWorkspace.mDir == null)
				return false;

			//LEGACY:
			/*if (!mInitialized)
			{
				outResult.Append(mWorkspace.mDir, "/", mWorkspace.mName, ".bfspace");
				if (File.Exists(outResult))
					return true;
				outResult.Clear();
			}*/

			outResult.Append(mWorkspace.mDir, "/BeefSpace.toml");
			IDEUtils.FixFilePath(outResult);
			return true;
        }

		///

        bool SaveWorkspace()
        {
			if (!mWorkspace.IsSingleFileWorkspace)
			{

#if !CLI
				if (mWorkspace.mDir == null)
				{
					/*SaveFileDialog dialog = scope .();
					dialog.ValidateNames = true;
					dialog.DefaultExt = ".toml";
					dialog.SetFilter("Beef projects (BeefProj.toml)|BeefProj.toml");
					dialog.FileName = "BeefProj.toml";
					if (dialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() != .OK)
						return false;
					
					bool matches = false;
					if (dialog.FileNames.Count == 1)
					{
						String fileName = scope .();
						Path.GetFileName(dialog.FileNames[0], fileName);
						matches = fileName == "BeefProj.toml";
					}
	
					if (!matches)
					{
						Fail("Workspace name must be 'BeefProj.toml'");
						return false;
					}
	
					mWorkspace.mDir = new String();
					Path.GetDirectoryPath(dialog.FileNames[0], mWorkspace.mDir);*/
	
					let activeWindow = GetActiveWindow();
	
					FolderBrowserDialog folderDialog = scope FolderBrowserDialog();
					//folderDialog.SelectedPath = fullDir;
					if (activeWindow != null)
						activeWindow.PreModalChild();
					if (folderDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() != .OK)
						return false;
					mWorkspace.mDir = new String(folderDialog.SelectedPath);
	
					String workspaceFileName = scope .();
					GetWorkspaceFileName(workspaceFileName);
					if (File.Exists(workspaceFileName))
					{
						Fail(scope String()..AppendF("A Beef workspace already exists at '{0}'", mWorkspace.mDir));
						DeleteAndNullify!(mWorkspace.mDir);
						return false;
					}
				}
#endif

				if (Directory.CreateDirectory(mWorkspace.mDir) case .Err)
				{
					Fail(StackStringFormat!("Failed to create workspace directory '{0}'", mWorkspace.mDir));
					return false;
				}
			}

            StructuredData data = scope StructuredData();
			data.CreateNew();
            mWorkspace.Serialize(data);

			String tomlString = scope String();
			data.ToTOML(tomlString);
			if (!mWorkspace.IsSingleFileWorkspace)
			{
	            String workspaceFileName = scope String();
	            GetWorkspaceFileName(workspaceFileName);
			
				if (!SafeWriteTextFile(workspaceFileName, tomlString))
				{
					Fail(StackStringFormat!("Failed to write workspace file '{0}'", workspaceFileName));
					return false;
				}
			}
			else
			{
				// If it's just the FileVersion then don't save anything...
				/*if (tomlString.Count('\n') < 2)
					tomlString.Clear();*/
				mWorkspace.mCompositeFile.Set("Workspace", tomlString);
			}
			mWorkspace.mHasChanged = false;
			MarkDirty();
            return true;
        }

		bool SaveDebugSession()
		{
			let project = mWorkspace.mProjects[0];

#if !CLI
			if (project.mProjectPath.IsEmpty)
			{
				SaveFileDialog dialog = scope .();
				let activeWindow = GetActiveWindow();

				let workspaceOptions = GetCurWorkspaceOptions();
				let options = GetCurProjectOptions(project);
				if (!options.mDebugOptions.mCommand.IsWhiteSpace)
				{
					String execCmd = scope .();
					ResolveConfigString(workspaceOptions, project, options, options.mDebugOptions.mCommand, "command", execCmd);

					String initialDir = scope .();
					Path.GetDirectoryPath(execCmd, initialDir);
					dialog.InitialDirectory = initialDir;
					dialog.SetFilter("Debug Session (*.bfdbg)|*.bfdbg");
					dialog.DefaultExt = ".bfdbg";

					String fileName = scope .();
					Path.GetFileNameWithoutExtension(execCmd, fileName);
					if (!fileName.IsEmpty)
					{
						fileName.Append(".bfdbg");
						dialog.FileName = fileName;
					}
				}

				dialog.OverwritePrompt = true;
				if (dialog.ShowDialog(activeWindow).GetValueOrDefault() != .OK)
					return false;
				
				project.mProjectPath.Set(dialog.FileNames[0]);
			}
#endif

			if (project.mProjectPath.IsEmpty)
				return false;

			StructuredData sd = scope StructuredData();

			sd.CreateNew();
			SaveWorkspaceUserData(sd);

			using (sd.CreateObject("DebugSession"))
			{
				project.Serialize(sd);
			}

			String jsonString = scope String();
			sd.ToTOML(jsonString);

			if (!SafeWriteTextFile(project.mProjectPath, jsonString, true))
				return false;

			project.mHasChanged = false;
			MarkDirty();

			return true;
		}
		void LoadDebugSession()
		{
			/*String path = scope String();
			if (!GetWorkspaceUserDataFileName(path))
				return false;
			var data = scope StructuredData();
			if (data.Load(path) case .Err)
				return false;

			if (!LoadWorkspaceUserData(data))
				return false;*/
		}

        public void AddProjectToWorkspace(Project project, bool addToProjectList = true)
        {
			if (addToProjectList)
            	mWorkspace.mProjects.Add(project);
            mBfBuildSystem.AddProject(project);
#if !CLI
			if (mBfResolveSystem != null)
            	mBfResolveSystem.AddProject(project);
#endif
			mWorkspace.ClearProjectNameCache();
        }

        public void AddNewProjectToWorkspace(Project project, VerSpecRecord verSpec = null)
        {
            AddProjectToWorkspace(project);
            mWorkspace.SetChanged();

			var relPath = scope String();
			Path.GetRelativePath(project.mProjectPath, mWorkspace.mDir, relPath);
			relPath.Replace("\\", "/");

			int lastSlash = relPath.LastIndexOf('/');
			if (lastSlash != -1)
				relPath.RemoveToEnd(lastSlash);

			/*var endStr = "/BeefProj.toml";
			if (relPath.EndsWith(endStr))
				relPath.RemoveToEnd(relPath.Length - endStr.Length);*/

			var projectSpec = new Workspace.ProjectSpec;
			projectSpec.mProjectName = new .(project.mProjectName);
			if (verSpec != null)
			{
				projectSpec.mVerSpec = verSpec;
			}
			else
			{
				projectSpec.mVerSpec = new .();
				projectSpec.mVerSpec.SetPath(relPath);
			}
			mWorkspace.mProjectSpecs.Add(projectSpec);

			var dep = new Project.Dependency();
			dep.mProjectName = new .("corlib");
			dep.mVerSpec = new .();
			dep.mVerSpec.SetSemVer("*");
			project.mDependencies.Add(dep);

            for (var checkProject in mWorkspace.mProjects)
            {
				int idx = checkProject.mDependencies.FindIndex(scope (dep) => dep.mProjectName == project.mProjectName);
                if (idx != -1)
                    ProjectOptionsChanged(checkProject);
            }
            ProjectOptionsChanged(project, false);
        }

        public void CreateProject(String projName, String projDir, Project.TargetType targetType)
        {
            Project project = new Project();
            project.mProjectName.Set(projName);
            project.mProjectPath.Set(projDir);
            Utils.GetDirWithSlash(project.mProjectPath);
			project.mProjectPath.Append("BeefProj.toml");
            project.mProjectDir.Set(projDir);
			project.mGeneralOptions.mTargetType = targetType;
			project.SetupDefault();
            project.SetChanged();
            //AddProjectToWorkspace(project);
			AddNewProjectToWorkspace(project);
			project.FinishCreate();

            mProjectPanel.InitProject(project);
            mProjectPanel.Sort();
            mWorkspace.FixOptions();
			mWorkspace.mHasChanged = true;

			mWorkspace.ClearProjectNameCache();
        	CurrentWorkspaceConfigChanged();
        }

		void StopDebugging()
		{
			if (mDebugger.mIsRunning)
			{
				mDebugger.StopDebugging();
				while (mDebugger.GetRunState() != .Terminated)
				{
					mDebugger.Update();
				}
				mDebugger.mIsRunning = false;
				mExecutionPaused = false;
			}
			mDebugger.DisposeNativeBreakpoints();
			mWantsRehupCallstack = false;
		}

		void CloseWorkspace()
		{
			StopDebugging();
			mDebugger.Reset();
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.Reset();
				});
			mMemoryPanel.MarkViewDirty();
			mCallStackPanel.MarkCallStackDirty();
			mThreadPanel.MarkThreadsDirty();

			if (mBfResolveCompiler != null)
			{
				mBfResolveCompiler.RequestCancelBackground();
				mBfResolveCompiler.CancelBackground();
				delete mBfResolveCompiler;
				delete mBfResolveSystem;
				delete mBfResolveHelper;
			}
			mBfBuildCompiler.RequestCancelBackground();
			mBfBuildCompiler.CancelBackground();
			delete mBfBuildCompiler;
			delete mBfBuildSystem;

			if (!mNoResolve)
			{
			    mBfResolveSystem = new BfSystem();
			    mBfResolveCompiler = mBfResolveSystem.CreateCompiler(true);
			    mBfResolveHelper = new BfResolveHelper();
			}

			mCompileSinceCleanCount = 0;
			mBfBuildSystem = new BfSystem();
			mBfBuildCompiler = mBfBuildSystem.CreateCompiler(false);
			mBfBuildCompiler.ClearBuildCache();

			/*var docPanels = scope List<Widget>();
			WithTabs(scope [&] (tab) =>
			{
			    var sourceViewPanel = tab.mContent as SourceViewPanel;
				if (sourceViewPanel != null)
				{	
					docPanels.Add(sourceViewPanel);
				}				
			});
			for (var docPanel in docPanels)
				CloseDocument(docPanel);*/

			while (mWindows.Count > 1)
				mWindows.Back.Close(true);

			void ResetPanel(Widget widget)
			{
				if (widget.mParent != null)
					widget.RemoveSelf();
			}

			if (!mRunningTestScript)
			{
				mActiveDocumentsTabbedView = null;
				ResetPanel(mProjectPanel);
				ResetPanel(mClassViewPanel);
				ResetPanel(mOutputPanel);
				ResetPanel(mImmediatePanel);
				ResetPanel(mFindResultsPanel);
				ResetPanel(mAutoWatchPanel);
				ResetPanel(mWatchPanel);
				ResetPanel(mMemoryPanel);
				ResetPanel(mCallStackPanel);
				ResetPanel(mBreakpointPanel);
				ResetPanel(mModulePanel);
				ResetPanel(mThreadPanel);
				ResetPanel(mProfilePanel);
				ResetPanel(mPropertiesPanel);
				ResetPanel(mAutoCompletePanel);
				mMainFrame.Reset();
			}

			mGlobalUndoManager.Clear();
			mHaveSourcesChangedExternallySinceLastCompile = false;
			mHaveSourcesChangedInternallySinceLastCompile = false;
			mIsImmediateDebugExprEval = false;

			//mActiveDocumentsTabbedView;
			if (mConfigName.IsEmpty)
				mConfigName.Set("Debug");
			if (mPlatformName.IsEmpty)
				mPlatformName.Set(sPlatform64Name);

			mDockingFrame = mMainFrame.mDockingFrame;
			//mMainFrame.AddedToParent;

			delete mWorkspace;
			mWorkspace = new Workspace();
		}

		void FinishShowingNewWorkspace(bool loadUserData = true)
		{
			if ((loadUserData) && (!mRunningTestScript))
			{
				if (!LoadWorkspaceUserData())
				    CreateDefaultLayout();
			}

			WorkspaceLoaded();
			UpdateTitle();
			UpdateRecentDisplayedFilesMenuItems();

			mProjectPanel.RebuildUI();

			ShowPanel(mOutputPanel, false);
			mMainFrame.RehupSize();
		}

		void CloseWorkspaceAndSetupNew()
		{
			CloseWorkspace();
			FinishShowingNewWorkspace();
		}

		// TryCloseWorkspaceAndSetupNew
		[IDECommand]
		void Cmd_CloseWorkspaceAndSetupNew()
		{
			bool done = CheckCloseWorkspace(mMainWindow,
				new () =>
				{
					// We use a close delay after saving so the user can see we actually saved before closing down
					if (SaveAll())
					{
						CloseWorkspaceAndSetupNew();
					}	
				},
				new () =>
				{
					CloseWorkspaceAndSetupNew();
				}, null);

			if (done)
			{
				CloseWorkspaceAndSetupNew();
			}

			//CloseWorkspace();
			//FinishShowingNewWorkspace();
		}

		public Result<void, StructuredData.Error> StructuredLoad(StructuredData data, StringView filePath)
		{
			if (mWorkspace.IsSingleFileWorkspace)
			{
				String dataStr = scope .();
				mWorkspace.mCompositeFile.Get(filePath, dataStr);
				Try!(data.LoadFromString(dataStr));
				return .Ok;
			}

			return data.Load(filePath);
		}

		public Result<void> StructuredSave(StringView filePath, StringView contents)
		{
			if (mWorkspace.IsSingleFileWorkspace)
			{
				mWorkspace.mCompositeFile.Set(filePath, contents);
				return .Ok;
			}

			if (gApp.SafeWriteTextFile(filePath, contents))
				return .Ok;
			else
				return .Err;
		}

        protected void LoadWorkspace()
        {
			scope AutoBeefPerf("IDEApp.LoadWorkspace");

			AddRecentFile(.OpenedWorkspace, mWorkspace.mDir);

            StructuredData data = null;
            
			String workspaceFileName = scope String();
			if (mWorkspace.IsSingleFileWorkspace)
			{
				if (mWorkspace.mCompositeFile.Load() case .Err)
				{
					OutputErrorLine("Failed to load workspace '{0}'", mWorkspace.mCompositeFile.FilePath);
				}
				workspaceFileName.Append("Workspace");
			}
			else
            	GetWorkspaceFileName(workspaceFileName);
            data = scope StructuredData(); //.LoadFromFile(workspaceFileName).GetValueOrDefault();

			LoadConfig();

			bool isNew = false;
			bool wantSave = false;
            
            if (StructuredLoad(data, workspaceFileName) case .Err(let err))
            {
				switch (err)
				{
				case .FormatError(int lineNum):
					OutputLineSmart("ERROR: Workspace format error in '{0}' on line {1}", workspaceFileName, lineNum);
					LoadFailed();
					return;
				case .FileError: // Assume 'file not found'
					if (mVerb == .OpenOrNew)
					{
						isNew = true;
					}
					else if (mVerb == .New)
					{
						isNew = true;
						wantSave = true;
					}
					else
					{
#if CLI
						OutputLineSmart("ERROR: Workspace '{0}' does not exist. Use the '-new' command line argument to create a new workspace.", workspaceFileName);
						LoadFailed();
						return;
#endif
					}
				default:
					OutputErrorLine("Failed to load workspace '{0}'", workspaceFileName);
					LoadFailed();
					return;
				}

				//Directory.CreateDirectory(mWorkspace.mDir).IgnoreError();

                int lastSlashPos = mWorkspace.mDir.LastIndexOf(IDEUtils.cNativeSlash);
                String projectName = scope String(mWorkspace.mDir, lastSlashPos + 1);

                String projectPath = scope String(mWorkspace.mDir);
                Utils.GetDirWithSlash(projectPath);
                projectPath.Append("BeefProj.toml");
				
                Project project = new Project();
				mWorkspace.mProjects.Add(project);
                if (!project.Load(projectPath))
				{
					project.mBeefGlobalOptions.mDefaultNamespace.Set(projectName);
					project.mBeefGlobalOptions.mStartupObject.Clear();
					project.mBeefGlobalOptions.mStartupObject.AppendF("{0}.Program", projectName);
					project.mProjectName.Set(projectName);
					project.SetupDefault();
					project.mNeedsCreate = true;
					project.mHasChanged = true;
					project.mProjectDir.Set(mWorkspace.mDir);
					project.mProjectPath.Set(mWorkspace.mDir);
					Utils.GetDirWithSlash(project.mProjectPath);
					project.mProjectPath.Append("BeefProj.toml");

					var verSpec = new VerSpecRecord();
					verSpec.SetSemVer("*");

					switch (AddProject("corlib", verSpec))
					{
					case .Ok(let libProject):
						var dep = new Project.Dependency();
						dep.mProjectName = new String("corlib");
						dep.mVerSpec = verSpec;
						project.mDependencies.Add(dep);
					default:
						delete verSpec;
					}
				}

				var projSpec = new Workspace.ProjectSpec;
				projSpec.mProjectName = new String(project.mProjectName);
				projSpec.mVerSpec = new VerSpecRecord();
				projSpec.mVerSpec.SetPath(".");
				mWorkspace.mProjectSpecs.Add(projSpec);

                mWorkspace.mStartupProject = project;
				mWorkspace.mHasChanged = true;
                AddProjectToWorkspace(project, false);

				ShowPanel(mOutputPanel, false);

				/*var str = scope String();
				Font.StrEncodeColor(0xfffef860, str);
				str.AppendF("Created new workspace in '{0}'", mWorkspace.mDir);
				Font.StrEncodePopColor(str);
				OutputLine(str);*/

				OutputLine("{0}Created new workspace in '{1}'{2}", Font.EncodeColor(0xfffef860), mWorkspace.mDir, Font.EncodePopColor());
				if (wantSave)
				{
					SaveWorkspace();
					project.Save();
				}
				else
				{
					mWorkspace.mNeedsCreate = true;
					OutputLine("{0}Use 'File\\Save All' to commit to disk.{1}", Font.EncodeColor(0xfffef860), Font.EncodePopColor());
				}


				/*for (int i < 10)
				{
					var testStr = scope String();

					for (int j < 10)
					{
						Font.StrEncodeColor(0xfffebd57, testStr);
						testStr.Append('A' + j);
						Font.StrEncodePopColor(testStr);
					}

					OutputLine(testStr);
				}*/
            }
			else
			{
				if (mVerb == .New)
				{
					OutputLineSmart("ERROR: Workspace '{0}' already exists, but '-new' argument was specified.", workspaceFileName);
					LoadFailed();
				}

				var startupProjectName = scope String();
				using (data.Open("Workspace"))
				{
					data.GetString("StartupProject", startupProjectName);
				}

				if (mWorkspace.IsSingleFileWorkspace)
				{
					var project = new Project();
					project.mProjectName.Set("Program");
					project.mGeneralOptions.mTargetType = .BeefConsoleApplication;
					Path.GetDirectoryPath(mWorkspace.mCompositeFile.FilePath, project.mProjectDir);
					project.DeferLoad("");
					mWorkspace.mProjects.Add(project);
				}

				if (data.Contains("Projects"))
				{
					for (var projectName in data.Enumerate("Projects"))
					{
						var projSpec = new Workspace.ProjectSpec;
						projSpec.mProjectName = new String(projectName);
						projSpec.mVerSpec = new VerSpecRecord();
						mWorkspace.mProjectSpecs.Add(projSpec);

						if (projSpec.mVerSpec.Parse(data) case .Err)
						{
							var err = scope String();
							err.AppendF("Unable to parse version specifier for {0} in {1}", projectName, workspaceFileName);
							Fail(err);
							LoadFailed();
							continue;
						}

						switch (AddProject(projectName, projSpec.mVerSpec))
						{
						case .Ok(let project):
							if ((!startupProjectName.IsEmpty) && (StringView.Compare(startupProjectName, projectName, true) == 0))
							{
								mWorkspace.mStartupProject = project;
							}
							project.mLocked = data.GetBool("Locked", project.mLockedDefault);
						case .Err:
							OutputLineSmart("ERROR: Unable to load project '{0}' specified in workspace", projectName);
							LoadFailed();
						default:
						}

						
					}
				}
				mWorkspace.Deserialize(data);
			}

			while (true)
			{
				bool hadLoad = false;
				for (int projectIdx = 0; projectIdx < mWorkspace.mProjects.Count; projectIdx++)
				{
					var project = mWorkspace.mProjects[projectIdx];
					if (project.mLoadDeferred)
					{
						hadLoad = true;

						var projectPath = project.mProjectPath;
						if (!project.Load(projectPath))
						{
							OutputErrorLine("Failed to load project '{0}' from '{1}'", project.mProjectName, projectPath);
							LoadFailed();
							project.mFailed = true;
						}

						AddProjectToWorkspace(project, false);
					}
				}
				if (!hadLoad)
					break;
			}

			mWorkspace.FinishDeserialize(data);
            mWorkspace.FixOptions(mConfigName, mPlatformName);
#if !CLI
			if (mBfResolveCompiler != null)
            	mBfResolveCompiler.QueueSetWorkspaceOptions(null, 0);
#endif

			MarkDirty();
        }

		public void RetryProjectLoad(Project project)
		{
			var projectPath = project.mProjectPath;
			if (!project.Load(projectPath))
			{
				Fail(scope String()..AppendF("Failed to load project '{0}' from '{1}'", project.mProjectName, projectPath));
				LoadFailed();
				project.mFailed = true;
			}
			else
			{
				project.mFailed = false;
				CurrentWorkspaceConfigChanged();
				mProjectPanel.RebuildUI();
			}
		}

		public enum ProjectAddError
		{
			InvalidVersion,
			InvalidVersionSpec,
			LoadFailed,
			NotFound
		}

		public Result<Project, ProjectAddError> AddProject(StringView projectName, VerSpecRecord verSpecRecord)
		{
			VerSpecRecord useVerSpecRecord = verSpecRecord;
			String verConfigDir = mWorkspace.mDir;

			for (var project in mWorkspace.mProjects)
			{
				if (project.mProjectName == projectName)
				{
					return project;
				}
			}

			for (int regEntryIdx = mBeefConfig.mRegistry.Count - 1; regEntryIdx >= 0; regEntryIdx--)
			{
				var regEntry = mBeefConfig.mRegistry[regEntryIdx];

				if (regEntry.mProjName == projectName)
				{
					useVerSpecRecord = regEntry.mLocation;
					verConfigDir = regEntry.mConfigFile.mConfigDir;
				}
			}

			var project = new Project();

			// For project locking, assume that only anything that is referenced with a path is editable
			project.mLockedDefault = !(verSpecRecord.mVerSpec case .Path);
			project.mLocked = project.mLockedDefault;

			mWorkspace.mProjects.Add(project);
			bool success = false;
			defer
			{
				if (!success)
				{
					mWorkspace.mProjects.Remove(project);
					delete project;
				}
			}

			String projectFilePath = null;

			switch (useVerSpecRecord.mVerSpec)
			{
			case .Path(let path):
				var relPath = scope String(path);
				IDEUtils.FixFilePath(relPath);
				if (!relPath.EndsWith(IDEUtils.cNativeSlash))
					relPath.Append(IDEUtils.cNativeSlash);
				var absPath = scope String();
				Path.GetAbsolutePath(relPath, verConfigDir, absPath);

				projectFilePath = scope:: String();
				projectFilePath.Append(absPath, "BeefProj.toml");
			case .SemVer(let semVer):
				//
			default:
				Fail("Invalid version specifier");
				return .Err(.InvalidVersionSpec);
			}

			if (projectFilePath == null)
			{
				return .Err(.NotFound);
			}

			project.mProjectName.Set(projectName);
			project.DeferLoad(projectFilePath);
			success = true;

			/*if (!project.Load(projectFilePath))
			{
				Fail(StackStringFormat!("Failed to load project {0}", projectFilePath));
				delete project;
				return .Err(.LoadFailed);
			}

			success = true;
			AddProjectToWorkspace(project, false);*/
			return .Ok(project);
		}

        protected void WorkspaceLoaded()
        {
			scope AutoBeefPerf("IDE.WorkspaceLoaded");

			List<String> platforms = scope List<String>();
			platforms.Add(IDEApp.sPlatform32Name);
			platforms.Add(IDEApp.sPlatform64Name);

			List<String> configs = scope List<String>();
			configs.Add("Debug");
			configs.Add("Release");
			configs.Add("Paranoid");
			configs.Add("Test");

			for (let platformName in platforms)
			{
				for (let configName in configs)
				{
					mWorkspace.FixOptions(configName, platformName);
				}
			}

			mWorkspace.FixOptions(mConfigName, mPlatformName);
#if !CLI
			PreConfigureBeefSystem(mBfResolveSystem, mBfResolveCompiler);
#endif
            for (var project in mWorkspace.mProjects)
            {
				project.mEnabled = IsProjectEnabled(project);
#if !CLI
				if (mBfResolveSystem != null)
                	SetupBeefProjectSettings(mBfResolveSystem, mBfResolveCompiler, project);
#endif
                project.mRootFolder.SortItems();
            }

			if (mWorkspace.IsSingleFileWorkspace)
			{
				AddToRecentDisplayedFilesList(CompositeFile.sMainFileName);
			}
        }

		///

        void DeserializeTabbedView(StructuredData data, IDETabbedView tabbedView)
        {
            if (data.GetBool("DefaultDocumentsTabbedView"))
                mActiveDocumentsTabbedView = tabbedView;

			SourceViewTab activeTab = null;
            for (data.Enumerate("Tabs"))
            {
                Panel panel = Panel.Create(data);
                if (panel == null)
                    continue;
                Debug.Assert(panel != null);

				bool isActive = data.GetBool("Active");
				
                var newTabButton = new SourceViewTab();
                newTabButton.Label = "";
                data.GetString("TabLabel", newTabButton.mLabel);
				newTabButton.mOwnsContent = panel.mAutoDelete;
				newTabButton.mTabWidthOffset = panel.TabWidthOffset;
                //newTabButton.mWantWidth = (float)Math.Round(data.GetFloat("TabWidth") * DarkTheme.sScale);
                newTabButton.mHeight = tabbedView.mTabHeight;
                newTabButton.mContent = panel;
                tabbedView.AddTab(newTabButton, tabbedView.GetTabCount());
				newTabButton.RehupScale(1.0f, 1.0f);
                
                newTabButton.mCloseClickedEvent.Add(new () => DocumentCloseClicked(panel));

				if (isActive)
					activeTab = newTabButton;
            }

			if (activeTab != null)
				activeTab.Activate(false);
        }

        void DeserializeDockingFrame(StructuredData data, DockingFrame dockingFrame)
        {
            dockingFrame.mSplitType = (DockingFrame.SplitType)data.GetInt("SplitType");
            for (var _docWid in data.Enumerate("DockedWidgets"))
            {
                //for (int32 dockedWidgetIdx = 0; dockedWidgetIdx < data.Count; dockedWidgetIdx++)
				//for (var dockedWidgetKV in data)
                {
                    //using (data.Open(dockedWidgetIdx))
					//using (data.Open(@dockedWidgetKV))
                    {
                        DockedWidget dockedWidget = null;
						IDETabbedView tabbedView = null;

                        String type = scope String();
                        data.GetString("Type", type);
                        if (type == "DockingFrame")
                        {
                            var innerDockingFrame = new DarkDockingFrame();
                            DeserializeDockingFrame(data, innerDockingFrame);
                            dockedWidget = innerDockingFrame;
                        }
                        else if (type == "TabbedView")
                        {
                            tabbedView = CreateTabbedView();
                            DeserializeTabbedView(data, tabbedView);
                            dockedWidget = tabbedView;
                        }

                        dockedWidget.mParentDockingFrame = dockingFrame;
                        dockedWidget.mIsFillWidget = data.GetBool("IsFillWidget");
						dockedWidget.mAutoClose = !data.GetBool("Permanent");
						if (dockedWidget.mIsFillWidget)
							dockedWidget.mHasFillWidget = true;
						if (dockedWidget.mHasFillWidget)
							dockingFrame.mHasFillWidget = true;

						if ((dockedWidget.mIsFillWidget) && (tabbedView != null) && (mActiveDocumentsTabbedView == null))
							mActiveDocumentsTabbedView = tabbedView;

						dockedWidget.mHasFillWidget = data.GetBool("HasFillWidget");
						dockedWidget.mSizePriority = data.GetFloat("SizePriority");
						dockedWidget.mRequestedWidth = data.GetFloat("RequestedWidth");
						dockedWidget.mRequestedHeight = data.GetFloat("RequestedHeight");
						dockedWidget.mWidth = dockedWidget.mRequestedWidth;
						dockedWidget.mHeight = dockedWidget.mRequestedHeight;

                        dockingFrame.AddWidget(dockedWidget);
                        dockingFrame.mDockedWidgets.Add(dockedWidget);
                    }
                }
            }
			dockingFrame.Rehup();
            dockingFrame.ResizeContent();
        }

        void DeserializeWindow(StructuredData data, WidgetWindow window)
        {
            int32 x = data.GetInt("X");
            int32 y = data.GetInt("Y");
            int32 width = data.GetInt("Width");
            int32 height = data.GetInt("Height");
			if ((width > 0) && (height > 0))
				mRequestedWindowRect = Rect(x, y, width, height);
        }

		bool LoadWorkspaceUserData(StructuredData data)
		{
			String configName = scope String();
			data.GetString("LastConfig", configName);
			if (!configName.IsEmpty)
			{
				 mConfigName.Set(configName);
			}

			String platformName = scope String();
			data.GetString("LastPlatform", platformName);
			if (!platformName.IsEmpty)
			{
			    Workspace.Config config;
			    mWorkspace.mConfigs.TryGetValue(mConfigName, out config);
			    if (config != null)
			    {
			        if (Utils.Contains(config.mPlatforms.Keys, platformName))
			            mPlatformName.Set(platformName);
			    }
			}

			using (data.Open("MainWindow"))
			    DeserializeWindow(data, mMainWindow);

			if (mMainWindow == null)
				mMainFrame.Resize(0, 0, mRequestedWindowRect.mWidth, mRequestedWindowRect.mHeight);

			using (data.Open("MainDockingFrame"))
			    DeserializeDockingFrame(data, mDockingFrame);

			DeleteAndClearItems!(mRecentlyDisplayedFiles);
			for (data.Enumerate("RecentFilesList"))
			{
				String fileStr = new String();
				data.GetCurString(fileStr);
				IDEUtils.FixFilePath(fileStr);
		        mRecentlyDisplayedFiles.Add(fileStr);
			}
    
			for (var _breakpoint in data.Enumerate("Breakpoints"))
		    {
	            String fileName = scope String();
	            data.GetString("File", fileName);
				IDEUtils.FixFilePath(fileName);
	            int32 lineNum = data.GetInt("Line");
	            int32 column = data.GetInt("Column");
	            int32 instrOffset = data.GetInt("InstrOffset", -1);
	            String memoryWatchExpression = scope String();
	            data.GetString("MemoryWatchExpression", memoryWatchExpression);
				Breakpoint breakpoint = null;
	            if (memoryWatchExpression.Length > 0)
	                breakpoint = mDebugger.CreateMemoryBreakpoint(memoryWatchExpression, (int)0, 0, null);
	            else if (fileName.Length > 0)
	                breakpoint = mDebugger.CreateBreakpoint(fileName, lineNum, column, instrOffset);
				else
				{
					String symbol = scope .();
					data.GetString("Symbol", symbol);
					if (!symbol.IsEmpty)
						breakpoint = mDebugger.CreateSymbolBreakpoint(symbol);
				}

				if (breakpoint != null)
				{
					String condition = scope String();
					data.GetString("Condition", condition);
					if (condition.Length > 0)
						breakpoint.SetCondition(condition);

					let logging = scope String();
					data.GetString("Logging", logging);
					bool breakAfterLogging = data.GetBool("BreakAfterLogging");
					breakpoint.SetLogging(logging, breakAfterLogging);

					let hitCountBreakKind = data.GetEnum<Breakpoint.HitCountBreakKind>("HitCountBreak");
					int hitCountTarget = data.GetInt("HitCountTarget");
					breakpoint.SetHitCountTarget(hitCountTarget, hitCountBreakKind);

					if (data.GetBool("Disabled"))
						mDebugger.SetBreakpointDisabled(breakpoint, true);
					if (data.GetBool("HasThreadId"))
						breakpoint.SetThreadId(0);
				}
		    }
			
			for (var _bookmark in data.Enumerate("Bookmarks"))
			{
	            String fileName = scope String();
	            data.GetString("File", fileName);
				IDEUtils.FixFilePath(fileName);
	            int32 lineNum = data.GetInt("Line");
	            int32 column = data.GetInt("Column");
	            mBookmarkManager.CreateBookmark(fileName, lineNum, column);
			}

			for (var referenceId in data.Enumerate("DebuggerDisplayTypes"))
			{
				var referenceIdStr = scope String(referenceId);
		        if (referenceIdStr.Length == 0)
		            referenceIdStr = null;
			        
	            var intDisplayType = data.GetEnum<DebugManager.IntDisplayType>("IntDisplayType");
	            var mmDisplayType = data.GetEnum<DebugManager.MmDisplayType>("MmDisplayType");                        
	            mDebugger.SetDisplayTypes(referenceIdStr, intDisplayType, mmDisplayType);
			}

			for (data.Enumerate("StepFilters"))
			{
				String filter = scope String();
				data.GetCurString(filter);
				if (!filter.IsEmpty)
					mDebugger.CreateStepFilter(filter, false, .Filtered);
			}

			for (data.Enumerate("StepNotFilters"))
			{
				String filter = scope String();
				data.GetCurString(filter);
				if (!filter.IsEmpty)
					mDebugger.CreateStepFilter(filter, false, .NotFiltered);
			}

			return true;
		}

        bool LoadWorkspaceUserData()
        {
			scope AutoBeefPerf("IDEApp.LoadWorkspaceUserData");
			//return false;

			String path = scope String();
			if (!GetWorkspaceUserDataFileName(path))
				return false;
            var data = scope StructuredData();
            if (data.Load(path) case .Err)
				return false;

			if (!LoadWorkspaceUserData(data))
				return false;
			return true;
        }

		/// 

		public void RehupStepFilters()
		{
			mDebugger.SetStepOverExternalFiles(mStepOverExternalFiles);
		}

        public void SaveClangFiles()
        {
            WithSourceViewPanels(scope (sourceViewPanel) =>
                {
                    if (sourceViewPanel.mIsClang)
                        SaveFile(sourceViewPanel);
                });
        }

		[IDECommand]
        public void SaveFile()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
                SaveFile(sourceViewPanel);
        }

		[IDECommand]
		public void SaveAs()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				SaveFileAs(sourceViewPanel);
			}
		}

		[IDECommand]
		public void OpenWorkspace()
		{
			mDeferredOpen = .Workspace;
		}

		void NewDebugSession(bool fromLoad)
		{
			if (!fromLoad)
				CloseWorkspaceAndSetupNew();
			mWorkspace.mIsDebugSession = true;

			/*DebugSessionProperties workspaceProperties = new .();
			workspaceProperties.PopupWindow(mMainWindow);
			workspaceProperties.StartNew();*/

			mPlatformName.Set(sPlatform64Name);
			mConfigName.Set("Debug");

			Project project = new .();
			project.mGeneralOptions.mTargetType = .CustomBuild;
			project.mProjectName.Set("Debug Session");
			AddProjectToWorkspace(project);
			mWorkspace.mStartupProject = project;
			if (!fromLoad)
				project.mHasChanged = true;

			gApp.mWorkspace.FixOptions(mConfigName, mPlatformName);

			mProjectPanel.RebuildUI();

			if (!fromLoad)
			{
				ProjectProperties dialog = new .(project);
				dialog.PopupWindow(mMainWindow);
				dialog.mNewDebugSessionCountdown = 60;
			}
		}

		[IDECommand]
		public void Cmd_NewDebugSession()
		{
#if !CLI
			/*SaveFileDialog dialog = scope .();
			
			let activeWindow = GetActiveWindow();
			dialog.OverwritePrompt = true;
			dialog.SetFilter("Debug Session (*.bfdbg)|*.bfdbg");
			dialog.DefaultExt = ".bfdbg";
			if (dialog.ShowDialog(activeWindow).GetValueOrDefault() != .OK)
				return;*/

			bool done = CheckCloseWorkspace(mMainWindow,
				new () =>
				{
					// We use a close delay after saving so the user can see we actually saved before closing down
					if (SaveAll())
					{
						NewDebugSession(false);
					}	
				},
				new () =>
				{
					NewDebugSession(false);
				}, null);

			if (done)
			{
				NewDebugSession(false);
			}
#endif
		}

		[IDECommand]
		public void Cmd_NewWorkspace()
		{
			OpenWorkspace();
		}

		[IDECommand]
		public void Cmd_NewProject()
		{
			mProjectPanel.[Friend]AddNewProject();
		}

		[IDECommand]
		public void Cmd_NewFile()
		{
			ShowSourceFile(null, null, .New);
		}

		[IDECommand]
		public void Cmd_OpenProject()
		{
			mProjectPanel.[Friend]ImportProject();
		}

		[IDECommand]
		public void OpenFile()
		{
			mDeferredOpen = .File;
		}

		[IDECommand]
		public void OpenCrashDump()
		{
			mDeferredOpen = .CrashDump;
		}

		[IDECommand]
        public bool SaveAll()
        {
            bool success = true;

            WithSourceViewPanels(scope [&] (sourceViewPanel) =>
                {
                    success &= SaveFile(sourceViewPanel);
                });
			mWorkspace.WithProjectItems(scope [&] (projectItem) =>
				{
					var projectSource = projectItem as ProjectSource;
					if (projectSource != null)
					{
						if ((projectSource.mEditData != null) && (projectSource.mEditData.HasTextChanged()))
						{
							success &= SaveFile(projectSource);
						}
					}
					
				});
            for (var project in mWorkspace.mProjects)
            {
                if (project.mHasChanged)
				{
					if (project.IsDebugSession)
						success &= SaveDebugSession();
					else
                    	project.Save();
				}
            }
            if ((mWorkspace.IsInitialized) && (!mWorkspace.IsDebugSession) &&
				((mWorkspace.mHasChanged) || (mWorkspace.mDir == null)))
                success &= SaveWorkspace();

			if (!mRunningTestScript)
            {
				if (!mWorkspace.IsDebugSession)
					success &= SaveWorkspaceUserData();
				mSettings.Save();
			}

			MarkDirty();

            return success;
        }

		[IDECommand]
		void Cmd_Exit()
		{
			mMainWindow.Close();
		}

        WidgetWindow GetCurrentWindow()
        {
			if (mRunningTestScript)
			{
				if (mLastActivePanel != null)
					return mLastActivePanel.mWidgetWindow;
				return mMainWindow;
			}

            for (var window in mWindows)
            {
                if (window.mHasFocus)
                {
                    var returnWindow = window;
					// With this "modal" flag, it caused errors to popup within a hoverwatch on a failed variable edit
                    while ((returnWindow.mParent != null) /*&& (returnWindow.mWindowFlags.HasFlag(BFWindow.Flags.Modal))*/)
                        returnWindow = returnWindow.mParent;
                    return (WidgetWindow)returnWindow;
                }
            }
            return mMainWindow;
        }

        public Dialog Fail(String text, Widget addWidget = null, WidgetWindow parentWindow = null)
        {
			// Always write to STDOUT even if we're running as a GUI, allowing cases like RunAndWait to pass us a stdout handle
			Console.Error.WriteLine("ERROR: {0}", text);

#if CLI
			mFailed = true;
			return null;
#endif

#unwarn
			if (mMainWindow == null)
			{
				mDeferredFails.Add(new String(text));
				return null;
			}


			if (mRunningTestScript)
			{
				if (mScriptManager.IsErrorExpected(text))
				{
					DeleteAndNullify!(mScriptManager.mExpectingError);
					OutputLine("Received expected error: {0}", text);
					return null;
				}

				ShowOutput();

				mFailed = true;
				OutputLineSmart("ERROR: {0}", text);
				Console.Error.WriteLine("ERROR: {0}", text);

				return null;
			}

#unwarn
			Debug.Assert(Thread.CurrentThread == mMainThread);

            if (mMainWindow == null)
            {
                Internal.FatalError(StackStringFormat!("FAILED: {0}", text));
            }

            Beep(MessageBeepType.Error);

            Dialog dialog = ThemeFactory.mDefault.CreateDialog("ERROR", text, DarkTheme.sDarkTheme.mIconError);
            dialog.mDefaultButton = dialog.AddButton("OK");
            dialog.mEscButton = dialog.mDefaultButton;
			dialog.mWindowFlags |= .Modal;
            dialog.PopupWindow(parentWindow ?? GetCurrentWindow());

			if (addWidget != null)
			{
				dialog.AddWidget(addWidget);
				addWidget.mY = dialog.mHeight - 60;
				addWidget.mX = 90;
			}
			return dialog;
        }

        public void MessageDialog(String title, String text)
        {            
            Dialog dialog = ThemeFactory.mDefault.CreateDialog(title, text);
            dialog.mDefaultButton = dialog.AddButton("OK");
            dialog.mEscButton = dialog.mDefaultButton;
            dialog.PopupWindow(mMainWindow);
        }

        public void DoQuickFind(bool isReplace)
        {
            var textPanel = GetActiveTextPanel();
            if (textPanel != null)
                textPanel.ShowQuickFind(isReplace);
			else
			{
				if (let activeWindow = GetActiveWindow() as WidgetWindow)
				{
					var widget = activeWindow.mFocusWidget;
					while (widget != null)
					{
						if (let watchStringEdit = widget as WatchStringEdit)
						{
							watchStringEdit.ShowQuickFind(isReplace);
							return;
						}

						widget = widget.mParent;
					}
				}
			}
        }

		[IDECommand]
		public void ShowAbout()
		{
			Dialog dialog = new AboutDialog();
			dialog.PopupWindow(mMainWindow);
		}

		[IDECommand]
		public void Cmd_Document__Find()
		{
			DoQuickFind(false);
		}

		[IDECommand]
		public void Cmd_Document__Replace()
		{
			DoQuickFind(true);
		}

        private void DoFindAndReplace(bool isReplace)
        {
			RecordHistoryLocation();

            if (mFindAndReplaceDialog != null)
            {
                mFindAndReplaceDialog.mWidgetWindow.SetForeground();
                return;
            }

            mFindAndReplaceDialog = new FindAndReplaceDialog(isReplace);
            mFindAndReplaceDialog.PopupWindow(mMainWindow);
            mFindAndReplaceDialog.mOnClosed.Add(new () => { mFindAndReplaceDialog = null; });
        }

		[IDECommand]
		public void Cmd_Find()
		{
			DoFindAndReplace(false);
		}

		[IDECommand]
		public void Cmd_Replace()
		{
			DoFindAndReplace(true);
		}

		[IDECommand]
		public void Cmd_FindPrev()
		{
			DoFindNext(-1);
		}

		[IDECommand]
		public void Cmd_FindNext()
		{
			DoFindNext(1);
		}

		[IDECommand]
		public void CursorToLineEnd()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
				sourceViewPanel.mEditWidget.mEditWidgetContent.CursorToLineEnd();
		}

		[IDECommand]
		public void CursorToLineStart()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
				sourceViewPanel.mEditWidget.mEditWidgetContent.CursorToLineStart(true);
		}

        public void DoFindNext(int32 dir = 1)
        {
            var textPanel = GetActiveTextPanel();
            if (textPanel != null)
			{
                textPanel.FindNext(dir);
			}
			else
			{
				if (let activeWindow = GetActiveWindow() as WidgetWindow)
				{
					var widget = activeWindow.mFocusWidget;
					while (widget != null)
					{
						if (let watchStringEdit = widget as WatchStringEdit)
						{
							watchStringEdit.FindNext(dir);
							return;
						}

						widget = widget.mParent;
					}
				}
			}
        }

		void DoShowNextDocumentPanel()
		{
			var activeDoumentPanel = GetActiveDocumentPanel();
			if ((activeDoumentPanel == null) && (mActiveDocumentsTabbedView != null))
			{
				var activeTab = mActiveDocumentsTabbedView.GetActiveTab();
				activeTab.Activate();
				return;
			}

			DarkTabbedView nextTabbedView = null;
			DarkTabbedView firstTabbedView = null;
			bool foundActiveTabbedView = false;

			WithDocumentTabbedViews(scope [&] (tabbedView) =>
                {
					if (tabbedView.mIsFillWidget)
					{
						if (firstTabbedView == null)
							firstTabbedView = tabbedView;
						if (tabbedView == mActiveDocumentsTabbedView)
							foundActiveTabbedView = true;
						else if ((foundActiveTabbedView) && (nextTabbedView == null))
							nextTabbedView = tabbedView;
					}
                });
			if (nextTabbedView == null)
				nextTabbedView = firstTabbedView;
			if (nextTabbedView != null)
			{
				var activeTab = nextTabbedView.GetActiveTab();
				activeTab.Activate();
			}
		}

		[IDECommand]
		public void Cmd_ShowCurrent()
		{
			var activePanel = GetActivePanel();

			if (var sourceViewPanel = activePanel as SourceViewPanel)
			{
				sourceViewPanel.ShowCurrent();
			}
			else if (var disassemblyPanel = activePanel as DisassemblyPanel)
			{
				disassemblyPanel.GoToSource();
			}
		}

		[IDECommand]
        public void Cmd_GotoLine()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
            {
				sourceViewPanel.GotoLine();
				return;
			}

			var activePanel = GetActivePanel();
			if (let memoryPanel = activePanel as MemoryPanel)
			{
				memoryPanel.GotoAddress();
			}
        }

		[IDECommand]
        public void Cmd_GotoMethod()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
                sourceViewPanel.GotoMethod();
        }

		[IDECommand]
		public void Cmd_RenameItem()
		{
			let activePanel = GetActivePanel();
			if (var projectPanel = activePanel as ProjectPanel)
            {
				projectPanel.TryRenameItem();
			}
			else if (var watchPanel = activePanel as WatchPanel)
			{
				watchPanel.TryRenameItem();
			}
		}

		[IDECommand]
        public void Cmd_RenameSymbol()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
                sourceViewPanel.RenameSymbol();
        }

		[IDECommand]
		public void Cmd_FindAllReferences()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			    sourceViewPanel.FindAllReferences();
		}

		[IDECommand]
		public void Cmd_FindClass()
		{
			var widgetWindow = GetCurrentWindow();
			if (widgetWindow != null)
			{
			    var dialog = new FindClassDialog();
			    dialog.PopupWindow(mMainWindow);
			}
		}

		[IDECommand]
		public void Cmd_ViewWhiteSpace()
		{
			mViewWhiteSpace.Toggle();
			MarkDirty();
		}

		[IDECommand]
		public void Cmd_ShowAutoComplete()
		{
			var activeTextPanel = GetActivePanel() as TextPanel;
			if (activeTextPanel != null)
			{
				var sewc = activeTextPanel.EditWidget.mEditWidgetContent as SourceEditWidgetContent;
				if (sewc != null)
					sewc.ShowAutoComplete(true);
			}
		}

		[IDECommand]
		public void Cmd_ShowFixit()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
				sourceViewPanel.FixitAtCursor();
		}

		[IDECommand]
		public void Cmd_PrevBookmark()
		{
			mBookmarkManager.PrevBookmark();
		}

		[IDECommand]
		public void Cmd_NextBookmark()
		{
			mBookmarkManager.NextBookmark();
		}

		[IDECommand]
		public void Cmd_ToggleBookmark()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
				sourceViewPanel.ToggleBookmarkAtCursor();
		}

		[IDECommand]
		public void Cmd_ClearBookmarks()
		{
			mBookmarkManager.Clear();
		}

		[IDECommand]
		public void Cmd_Clean()
		{
			if (IsCompiling)
			{
				Fail("Cannot clean while compilign");
				return; // Ignore
			}
			if (mDebugger.mIsRunning)
			{
				Fail("Cannot clean while running");
				return; // Ignore
			}
			mWantsClean = true;
		}

		[IDECommand]
		public void Cmd_CleanBeef()
		{
			if (IsCompiling)
			{
				Fail("Cannot clean while compilign");
				return; // Ignore
			}
			if (mDebugger.mIsRunning)
			{
				Fail("Cannot clean while running");
				return; // Ignore
			}
			mWantsBeefClean = true;
		}

		[IDECommand]
		public void Cmd_CompileFile()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{                            
			    if (sourceViewPanel.mProjectSource != null)
			    {
			        var filePath = scope String();
			        sourceViewPanel.mProjectSource.GetFullImportPath(filePath);
			        if ((filePath.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase)) ||
			            (filePath.EndsWith(".c", StringComparison.OrdinalIgnoreCase)))
			        {
			            ShowPanel(mOutputPanel, false);
			            SaveFile(sourceViewPanel);

			            var project = sourceViewPanel.mProjectSource.mProject;
			            Project.Options options = GetCurProjectOptions(project);
			            if (options != null)
			            {
			                Workspace.Options workspaceOptions = GetCurWorkspaceOptions();
			                CompileSource(project, workspaceOptions, options, filePath);
			            }
			        }
			    }
			}
		}
		[IDECommand]
		public void Cmd_MatchBrace()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
				sourceViewPanel.MatchBrace();
		}

		[IDECommand]
		public void Cmd_GotoNextItem()
		{
			var curOutputPanel = mOutputPanel;
			if ((mFindResultsPanel != null) && (mFindResultsPanel.mWidgetWindow != null))
			{
			    if (mFindResultsPanel.mLastFocusAppUpdateCnt > curOutputPanel.mLastFocusAppUpdateCnt)
			        curOutputPanel = mFindResultsPanel;
			}
			curOutputPanel.GotoNextSourceReference();
		}
		
		[IDECommand]
		public void Cmd_ZoomOut()
		{
			float scale = DarkTheme.sScale;
			if (scale > 0.30f)
			{
				if (scale < 0)
					scale -= 0.05f;
				else //if (scale < 2.0f)
					scale -= 0.10f;

				SetScale(scale);
			}
		}

		[IDECommand]
		public void Cmd_ZoomIn()
		{
			float scale = DarkTheme.sScale;
			if (scale < 4.0f)
			{
				if (scale < 0)
					scale += 0.02f;//0.05f;
				else //if (scale < 2.0f)
					scale += 0.04f;//0.10f;

				SetScale(scale);
			}
		}

		[IDECommand]
		public void Cmd_ShowFileExternally()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				ProcessStartInfo psi = scope ProcessStartInfo();
				psi.SetFileName("/bin/OpenFileLine");
				var args = scope String();
				args.AppendF("{0} {1}", sourceViewPanel.mFilePath, sourceViewPanel.mEditWidget.mEditWidgetContent.CursorLineAndColumn.mLine + 1);
				psi.SetArguments(args);
				psi.UseShellExecute = false;

				SpawnedProcess process = scope SpawnedProcess();
				process.Start(psi).IgnoreError();

				/*if (case .Ok(var process) = Process.Start(psi))
					delete process;*/
			}
		}

		[IDECommand]
		public void Cmd_ZoomReset()
		{
			SetScale(1.0f, true);
		}

		[IDECommand]
        public void Cmd_ReformatDocument()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
                sourceViewPanel.ReformatDocument();
        }

        void RemoveAllBreakpoints()
        {
			BfLog.LogDbg("IDEApp.RemoveAllBreakpoints\n");
            while (mDebugger.mBreakpointList.Count > 0)
                mDebugger.DeleteBreakpoint(mDebugger.mBreakpointList[0]);
        }

		[IDECommand]
        void Cmd_Break()
        {
            mDebugger.BreakAll();
        }

        public void ShowDisassemblyAtCursor()
        {
            if (!mDebugger.mIsRunning)
                return; // Ignore

            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
            {
                int line;
                int lineChar;
                sourceViewPanel.mEditWidget.Content.GetCursorLineChar(out line, out lineChar);
                
                var disassemblyPanel = ShowDisassemblyPanel();
                if (!disassemblyPanel.Show(sourceViewPanel.mFilePath, line, lineChar))
                    ShowRecentFile(1); // Go back a file
            }
        }

		[IDECommand]
        public void ShowDisassemblyAtStack()
        {
			var activePanel = GetActivePanel();
			if (var diassemblyPanel = activePanel as DisassemblyPanel)
			{
				diassemblyPanel.mStayInDisassemblyCheckbox.Checked ^= true;
				return;
			}

            if ((mDebugger.mIsRunning) && (mDebugger.IsPaused()))
            {
                ShowPCLocation(mDebugger.mActiveCallStackIdx, false, false, true);
            }
			else
			{
				ShowDisassemblyPanel(true);
			}
			mInDisassemblyView = true;
        }

        public void GoToDefinition()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
            {
                int line;
                int lineChar;
                sourceViewPanel.mEditWidget.Content.GetCursorLineChar(out line, out lineChar);

#if IDE_C_SUPPORT
                if (sourceViewPanel.mIsClang)
                {
                    String defFile = scope String();
                    int defLine;
                    int defColumn;
                    mResolveClang.CancelBackground();
                    
                    int defIdx = sourceViewPanel.mEditWidget.Content.GetTextIdx(line, lineChar);
                    if (mResolveClang.FindDefinition(sourceViewPanel.mFilePath, defIdx,
                        defFile, out defLine, out defColumn))
                    {
                        sourceViewPanel.RecordHistoryLocation();
                        sourceViewPanel = ShowSourceFileLocation(defFile, -1, -1, defLine, defColumn, LocatorType.Smart, true);
                        if (sourceViewPanel != null)
                            sourceViewPanel.RecordHistoryLocation();
                        return;
                    }
                }
                else
#endif
                {                    
                    ResolveParams resolveParams = scope ResolveParams();
                    sourceViewPanel.Classify(ResolveType.GoToDefinition, resolveParams);
                    if (resolveParams.mOutFileName != null)
                    {
                        sourceViewPanel.RecordHistoryLocation();
                        sourceViewPanel = ShowSourceFileLocation(resolveParams.mOutFileName, -1, -1, resolveParams.mOutLine, resolveParams.mOutLineChar, LocatorType.Smart, true);
                        sourceViewPanel.RecordHistoryLocation(true);
                        return;
                    }
                }

				if (mBfResolveCompiler.HasResolvedAll())
				{
	                Fail("Unable to locate definition");
				}
				else
				{
					sourceViewPanel.ShowSymbolReferenceHelper(.GoToDefinition);
				}
            }
        }

        public void StackPositionChanged()
        {                      
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.MarkWatchesDirty(false, true);
				});
            mMemoryPanel.MarkViewDirty();            
        }

        public void RefreshWatches()
        {
			//Debug.WriteLine("RefreshWatches");

			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.MarkWatchesDirty(false);
				});
			MarkDirty();

			for (var window in gApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if (var hoverWatch = widgetWindow.mRootWidget as HoverWatch)
				{
					hoverWatch.Refresh();
				}
			}
        }

        public void MemoryEdited()
        {
            RefreshWatches();
            mMemoryPanel.MarkViewDirty();
        }

        public void AddWatch(String watchExpr)
        {
			ShowWatches();
            mWatchPanel.AddWatchItem(watchExpr);
            mWatchPanel.MarkWatchesDirty(false);
        }

        public bool IsInDisassemblyMode(bool wantShowSource = false)
        {
			//return GetActiveDocumentPanel() is DisassemblyPanel;
            if (!mInDisassemblyView)
                return false;

            DisassemblyPanel disassemblyPanel = TryGetDisassemblyPanel();
            if (disassemblyPanel == null)
                return false;
            return ((disassemblyPanel.mStayInDisassemblyCheckbox != null) && (disassemblyPanel.mStayInDisassemblyCheckbox.Checked)) || (!wantShowSource);
        }

        DisassemblyPanel TryGetDisassemblyPanel(bool onlyIfVisible = true)
        {
            Debug.Assert(true);

            DisassemblyPanel disassemblyPanel = null;
            WithTabs(scope [&] (tabButton) =>
                {
                    if ((disassemblyPanel == null) && (tabButton.mContent is DisassemblyPanel))
                    {
                        var checkDisassemblyPanel = (DisassemblyPanel)tabButton.mContent;
                        if ((!onlyIfVisible) || (checkDisassemblyPanel.mWidgetWindow != null))
                            disassemblyPanel = checkDisassemblyPanel;
                    }
                });
            return disassemblyPanel;
        }

		[IDECommand]
		void Compile()
		{
			if (AreTestsRunning())
				return;
			if (mHotResolveState != .None)
				return;
			if (IsCompiling)
				return;

			if (mWorkspace.mProjects.IsEmpty)
			{
				Fail("No projects exist to compile. Create or load a project.");
				return;
			}

			if (mWorkspace.IsDebugSession)
			{
				bool hadCommands = false;
				for (let project in mWorkspace.mProjects)
				{
					if (project.mGeneralOptions.mTargetType == .CustomBuild)
					{
						let options = GetCurProjectOptions(project);
						if (options == null)
							continue;
						if ((!options.mBuildOptions.mPreBuildCmds.IsEmpty) || (!options.mBuildOptions.mPostBuildCmds.IsEmpty))
							hadCommands = true;
					}
					else
						hadCommands = true;
				}

				if (!hadCommands)
				{
					Fail("No build commands have been defined");
					return;
				}
			}

			if ((!mDebugger.mIsRunning) || (!mDebugger.mIsRunningCompiled))
			{                        
			    if (mExecutionQueue.Count == 0)
			    {
			        mOutputPanel.Clear();
			        OutputLine("Compiling...");
			        Compile(.Normal, null);
			    }
			}
			else
			{
			    mOutputPanel.Clear();
			    OutputLine("Hot Compiling...");
			    Project runningProject = mWorkspace.mStartupProject;
			    Compile(.Normal, runningProject);
			}
		}

		[IDECommand]
		void RunWithStep()
		{
			mTargetStartWithStep = true;
			CompileAndRun();
		}

		[IDECommand]
		void StepInto()
		{
			if (mDebugger.mIsRunning)
			{
			    if (mExecutionPaused)
			    {
			        DebuggerUnpaused();
			        mDebugger.StepInto(IsInDisassemblyMode());
			    }
			}
			else
			{
			    RunWithStep();
			}
		}

		[IDECommand]
		void StepOver()
		{
			mStepCount++;
			if (mDebugger.mIsRunning)
			{
			    if (mExecutionPaused)
			    {
			        DebuggerUnpaused();
			        mDebugger.StepOver(IsInDisassemblyMode());
			    }
			}
			else
			{
				if (mEnableRunTiming)
				{
					mRunTimingProfileId = Profiler.StartSampling("RunTiming");
				}

			    RunWithStep();
			}
		}

		[IDECommand]
		void StepOut()
		{
			if (mExecutionPaused)
			{
			    DebuggerUnpaused();
			    mDebugger.StepOut(IsInDisassemblyMode());                            
			}
		}

		[IDECommand]
		void Cmd_Continue()
		{
			if (mDebugger.mIsRunning)
			{                            
			    if (mDebugger.IsPaused())
			    {
			        DebuggerUnpaused();
			        mDebugger.Continue();
			    }                            
			}
		}

		[IDECommand]
		public void RunWithCompiling()
		{
			if (mDebugger.mIsRunning)
			{                            
			    if (mDebugger.IsPaused())
			    {
			        DebuggerUnpaused();
			        mDebugger.Continue();
			    }                            
			}
			else if (AreTestsRunning())
			{
				// Ignore
			}
			else
			{
			    mTargetStartWithStep = false;
			    CompileAndRun();
			}
		}

		[IDECommand]
		void RunWithoutCompiling()
		{
			if (!mDebugger.mIsRunning)
			{
			    OutputLine("Starting target without compiling...");
				mTargetStartWithStep = false;
			    var startDebugCmd = new StartDebugCmd();
				startDebugCmd.mWasCompiled = false;
			    startDebugCmd.mOnlyIfNotFailed = true;
			    mExecutionQueue.Add(startDebugCmd);
			}
		}

		[IDECommand]
		void RunToCursor()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();

			if (mDebugger.mRunToCursorBreakpoint != null)
			{
				BfLog.LogDbg("Deleting mRunToCursorBreakpoint\n");
				mDebugger.DeleteBreakpoint(mDebugger.mRunToCursorBreakpoint);
				mDebugger.mRunToCursorBreakpoint = null;
			}

			if (sourceViewPanel != null)
			{
				BfLog.LogDbg("Creating mRunToCursorBreakpoint\n");
				mDebugger.mRunToCursorBreakpoint = sourceViewPanel.ToggleBreakpointAtCursor(true, mDebugger.GetActiveThread());
			}
			else if (var disassemblyPanel = GetActiveDocumentPanel() as DisassemblyPanel)
			{
				mDebugger.mRunToCursorBreakpoint = disassemblyPanel.ToggleAddrBreakpointAtCursor(true, mDebugger.GetActiveThread());
			}

			if (mDebugger.mIsRunning)
			{                            
			    if (mDebugger.IsPaused())
			    {
			        DebuggerUnpaused();
			        mDebugger.Continue();
			    }
			}
			else
			{
			    mTargetStartWithStep = false;
			    CompileAndRun();
			}
		}

		[IDECommand]
		void SetNextStatement()
		{
			var documentPanel = GetActiveDocumentPanel();
			var sourceViewPanel = documentPanel as SourceViewPanel;
			var disassemblyPanel = GetActiveDocumentPanel() as DisassemblyPanel;

			if (mDebugger.mIsRunning)
			{
			    if (mExecutionPaused)
			    {
					if (gApp.mDebugger.mActiveCallStackIdx != 0)
					{
						gApp.Fail("Set Next Statement cannot only be used when the top of the callstack is selected");
						return;
					}

			        if (disassemblyPanel != null)
			        {
			            String sourceFileName = scope String();
			            int addr = disassemblyPanel.GetCursorAddress(sourceFileName);
			            if (addr != (int)0)
			            {
			                mDebugger.SetNextStatement(true, sourceFileName, addr, 0);
			                PCChanged();
			            }

			            DebuggerUnpaused();
			        }
			        else
			        {
						var activePanel = sourceViewPanel.GetActivePanel();

			            int lineIdx;
			            int lineCharIdx;
			            var editWidgetContent = activePanel.mEditWidget.Content;
			            editWidgetContent.GetLineCharAtIdx(editWidgetContent.CursorTextPos, out lineIdx, out lineCharIdx);

						/*int hotFileIdx = sourceViewPanel.[Friend]mHotFileIdx;
						sourceViewPanel.[Friend]RemapActiveToCompiledLine(hotFileIdx, ref lineIdx, ref lineCharIdx);*/

						int textPos = editWidgetContent.CursorTextPos - lineCharIdx;
						lineCharIdx = 0;

						// Find first non-space char8
						while ((textPos < editWidgetContent.mData.mTextLength) && (((char8)editWidgetContent.mData.mText[textPos].mChar).IsWhiteSpace))
						{
						    textPos++;
						    lineCharIdx++;
						}

						if (sourceViewPanel.[Friend]mOldVersionPanel == null)
						{
							int addr;
							String file = scope String();
							int hotIdx;
							int defLineStart;
							int defLineEnd;
							int line;
							int column;
							int language;
							int stackSize;
							mDebugger.CheckCallStack();
							String label = scope String();
							DebugManager.FrameFlags frameFlags;
							mDebugger.GetStackFrameInfo(0, label, out addr, file, out hotIdx, out defLineStart, out defLineEnd, out line, out column, out language, out stackSize, out frameFlags);

							if (hotIdx != -1)
								sourceViewPanel.[Friend]RemapActiveToCompiledLine(hotIdx, ref lineIdx, ref lineCharIdx);
						}

			            mDebugger.SetNextStatement(false, sourceViewPanel.mFilePath, (int)lineIdx, lineCharIdx);

			            PCChanged();
			            DebuggerUnpaused();
			        }
			    }                           
			}
		}

		void ToggleBreakpoint(WidgetWindow window, bool bindToThread = false)
		{
			var documentPanel = GetActiveDocumentPanel();

			if (var sourceViewPanel = documentPanel as SourceViewPanel)
			{
			    sourceViewPanel.ToggleBreakpointAtCursor(false, bindToThread ? gApp.mDebugger.GetActiveThread() : -1);
			}
			else if (var disassemblyPanel = documentPanel as DisassemblyPanel)
			{				
				disassemblyPanel.ToggleBreakpointAtCursor(false, bindToThread ? gApp.mDebugger.GetActiveThread() : -1);
			}
		}

		[IDECommand]
		void ToggleComment()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			var ewc = (SourceEditWidgetContent)sourceViewPanel.mEditWidget.mEditWidgetContent;
			ewc.ToggleComment();
		}

		[IDECommand]
		void ToggleBreakpoint()
		{
			ToggleBreakpoint(GetCurrentWindow(), false);
		}

		[IDECommand]
		void ToggleThreadBreakpoint()
		{
			ToggleBreakpoint(GetCurrentWindow(), true);
		}

		void CompileCurrentFile()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;
			
			if (sourceViewPanel.mProjectSource == null)
				return;

			if (!sourceViewPanel.mIsClang)
				return;

			var project = sourceViewPanel.mProjectSource.mProject;
			var options = GetCurProjectOptions(project);
			var workspaceOptions = GetCurWorkspaceOptions();
			CompileSource(project, workspaceOptions, options, sourceViewPanel.mFilePath, "-v");
		}

		[IDECommand]
        void CancelBuild()
        {            
            mBfBuildCompiler.RequestCancelBackground();
            if (IsCompiling)
            {
                OutputLine("Canceling Compilation...");
				//DeleteAndClearItems!(mExecutionQueue);
				for (var cmd in mExecutionQueue)
				{
#unwarn
					if (var processBfCompileCmd = cmd as ProcessBfCompileCmd)
					{
						
					}
					else if (var buildCompleteCmd = cmd as BuildCompletedCmd)
					{

					}
					else
					{
						delete cmd;
						@cmd.Remove();
					}
				}

				for (var executionInstance in mExecutionInstances)
				{
					executionInstance.Cancel();
				}
            }

			if ((mBuildContext != null) && (mBuildContext.mScriptManager != null))
				mBuildContext.mScriptManager.Cancel();
        }

        TabbedView FindTabbedView(DockingFrame dockingFrame, int32 xDir, int32 yDir)
        {
            bool useFirst = true;
            if (dockingFrame.mSplitType == DockingFrame.SplitType.Horz)
            {
                useFirst = xDir > 0;
            }
            else
                useFirst = yDir > 0;

            for (int32 pass = 0; pass < 2; pass++)
            {
                for (int32 i = 0; i < dockingFrame.mDockedWidgets.Count; i++)
                {
                    if ((useFirst) && (i == 0) && (pass == 0))
                        continue;

                    var widget = dockingFrame.mDockedWidgets[i];
                    if (widget is TabbedView)
                        return (TabbedView)widget;
                    DockingFrame childFrame = widget as DockingFrame;
                    if (childFrame != null)
                    {
                        TabbedView tabbedView = FindTabbedView(childFrame, xDir, yDir);
                        if (tabbedView != null)
                            return tabbedView;
                    }
                }
            }

            return null;
        }

		enum ShowTabResult
		{
			Existing,
			OpenedNew
		}

        ShowTabResult ShowTab(Widget tabContent, String name, bool ownsContent, bool setFocus)
        {
			var result = ShowTabResult.Existing;
            var tabButton = GetTab(tabContent);
            if (tabButton == null)
            {
				TabbedView tabbedView = null;
				if (var newPanel = tabContent as Panel)
				{
					WithTabs(scope [&] (tabButton) =>
	                    {
							if (newPanel.HasAffinity(tabButton.mContent))
							{
								tabbedView = tabButton.mTabbedView;
							}
	                    });
				}

				if (tabbedView == null)
                	tabbedView = FindTabbedView(mDockingFrame, -1, 1);
				if (tabbedView == null)
				{
					tabbedView = CreateTabbedView();
					mDockingFrame.AddDockedWidget(tabbedView, null, .Left);
				}
                if (tabbedView != null)
				{
                    tabButton = SetupTab(tabbedView, name, 100, tabContent, ownsContent);
					result = ShowTabResult.OpenedNew;
				}
            }
            if (tabButton != null)
			{
				tabButton.RehupScale(1.0f, 1.0f);
                tabButton.Activate(setFocus);
			}
			return result;
        }

		public void RecordHistoryLocation(bool includeLastActive = false, bool b = true)
		{
			var sourceViewPanel = GetActiveSourceViewPanel(includeLastActive);
			if (sourceViewPanel != null)
			    sourceViewPanel.RecordHistoryLocation();
		}

		void ShowPanel(Panel panel, String label, bool setFocus = true)
		{
			mLastActivePanel = panel;
			RecordHistoryLocation();
			ShowTab(panel, label, false, setFocus);
			if (setFocus)
				panel.FocusForKeyboard();
			if ((!panel.mWidgetWindow.mHasFocus) && (!mRunningTestScript))
				panel.mWidgetWindow.SetForeground();
		}

		[IDECommand]
		public void ShowWorkspacePanel()
		{
			ShowPanel(mProjectPanel, "Workspace");
		}

		[IDECommand]
		public void ShowClassViewPanel()
		{
			ShowPanel(mClassViewPanel, "Class View");
		}

		[IDECommand]
		public void ShowThreads()
		{
			ShowPanel(mThreadPanel, "Threads");
		}

		[IDECommand]
		public void ShowCallstack()
		{
		    ShowPanel(mCallStackPanel, "Call Stack");						
		}

		[IDECommand]
		public void ShowWatches()
		{
		    ShowPanel(mWatchPanel, "Watch");
		}

		[IDECommand]
        public void ShowAutoWatches()
        {
            ShowPanel(mAutoWatchPanel, "Auto Watches");
        }

		[IDECommand]
        public void ShowImmediatePanel()
        {            
            ShowPanel(mImmediatePanel, "Immediate");
        }

		[IDECommand]
        public void ShowBreakpoints()
        {
            ShowPanel(mBreakpointPanel, "Breakpoints");
        }

		[IDECommand]
		public void ShowModules()
		{
		    ShowPanel(mModulePanel, "Modules");
		}

		[IDECommand]
		public void ShowMemory()
		{
			ShowPanel(mMemoryPanel, "Memory");
		}

		[IDECommand]
		public void ShowProfilePanel()
		{
		    ShowPanel(mProfilePanel, "Profile");
		}

		[IDECommand]
		public void ShowQuickWatch()
		{
			QuickWatchDialog dialog = new .();

			var activePanel = GetActivePanel();
			if (let sourceViewPanel = activePanel as SourceViewPanel)
			{
				sourceViewPanel.RecordHistoryLocation();

				var debugExpr = scope String();
				var ewc = sourceViewPanel.EditWidget.Content;
				if (ewc.HasSelection())
					ewc.GetSelectionText(debugExpr);
				else
					sourceViewPanel.GetDebugExpressionAt(ewc.CursorTextPos, debugExpr);
				dialog.Init(debugExpr);
			}
			else if (let immediatePanel = activePanel as ImmediatePanel)
			{
				var debugExpr = scope String();
				immediatePanel.GetQuickExpression(debugExpr);
				dialog.Init(debugExpr);
			}
			else
			{
				dialog.Init(.());
			}

			if (activePanel != null)
				dialog.PopupWindow(activePanel.mWidgetWindow);
			else
				dialog.PopupWindow(mMainWindow);
		}

		[IDECommand]
		public void SelectConfig()
		{
			mMainFrame.mStatusBar.mConfigComboBox.ShowDropdown();
		}

		[IDECommand]
		public void SelectPlatform()
		{
			mMainFrame.mStatusBar.mPlatformComboBox.ShowDropdown();
		}

		[IDECommand]
		public void ShowSettings()
		{
		    var workspaceProperties = new SettingsDialog();
		    workspaceProperties.PopupWindow(mMainWindow);
		}

		[IDECommand]
		public void ShowKeyboardShortcuts()
		{
		    /*var workspaceProperties = new SettingsDialog();
		    workspaceProperties.PopupWindow(mMainWindow);*/
		}

		public void ShowFindResults(bool setFocus)
		{
		    ShowPanel(mFindResultsPanel, "Find Results", setFocus);
		}

		[IDECommand]
        public void ShowFindResults()
        {
            ShowFindResults(true);
        }

		[IDECommand]
		public void ShowOutput()
		{
		    ShowPanel(mOutputPanel, "Output");
		}

		[IDECommand]
		public void ShowAutoCompletePanel()
		{
		    ShowPanel(mAutoCompletePanel, "Autocomplete", false);
		}

		[IDECommand]
        private void OpenCorresponding()
        {
            var sourceViewPanel = GetActiveSourceViewPanel();
            if (sourceViewPanel != null)
            {
                String fileName = sourceViewPanel.mFilePath;
                String findFileName = null;
                int dotPos = fileName.LastIndexOf('.');
                if ((fileName.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase)) || (fileName.EndsWith(".c", StringComparison.OrdinalIgnoreCase)))
                {
                    findFileName = scope:: String(fileName, 0, dotPos);
                    findFileName.Append(".h");
                }
                else if ((fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase)) || (fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase)))
                {
                    findFileName = scope:: String(fileName, 0, dotPos);
                    findFileName.Append(".c");
                    if (!File.Exists(findFileName))
					{
                        findFileName = scope:: String(fileName, 0, dotPos);
                         findFileName.Append(".cpp");
					}
                }

                if (findFileName != null)
                {
                    if (File.Exists(findFileName))
                    {
                        ShowSourceFile(findFileName);
                    }
                    else
                    {
                        Fail("Unable to find corresponding file");
                    }
                }
            }
        }

		DarkTabbedView GetActiveTabbedView()
		{
			var activePanel = GetActivePanel();
			if (activePanel == null)
				return null;
			return activePanel.mParent as DarkTabbedView;
		}

		[IDECommand]
		void TabFirst()
		{
			var tabbedView = GetActiveTabbedView();
			if ((tabbedView == null) || (tabbedView.mTabs.IsEmpty))
				return;
			tabbedView.mTabs[0].Activate();
		}

		[IDECommand]
		void TabLast()
		{
			var tabbedView = GetActiveTabbedView();
			if ((tabbedView == null) || (tabbedView.mTabs.IsEmpty))
				return;
			tabbedView.mTabs.Back.Activate();
		}

		[IDECommand]
		void TabNext()
		{
			var tabbedView = GetActiveTabbedView();
			if ((tabbedView == null) || (tabbedView.mTabs.IsEmpty))
				return;
			for (var tab in tabbedView.mTabs)
			{
				if (tab.mIsActive)
				{
					if (@tab.Index < tabbedView.mTabs.Count - 1)
					{
						tabbedView.mTabs[@tab.Index + 1].Activate();
						return;
					}
				}
			}
			tabbedView.mTabs[0].Activate();
		}

		[IDECommand]
		void TabPrev()
		{
			var tabbedView = GetActiveTabbedView();
			if ((tabbedView == null) || (tabbedView.mTabs.IsEmpty))
				return;
			for (var tab in tabbedView.mTabs)
			{
				if (tab.mIsActive)
				{
					if (@tab.Index > 0)
					{
						tabbedView.mTabs[@tab.Index - 1].Activate();
						return;
					}
				}
			}
			tabbedView.mTabs.Back.Activate();
		}

		void DoErrorTest()
		{
			Dialog aDialog = ThemeFactory.mDefault.CreateDialog("ERROR", "This\nmultiline!\nLine 3.", DarkTheme.sDarkTheme.mIconError);
            aDialog.mDefaultButton = aDialog.AddButton("OK");
            aDialog.mEscButton = aDialog.mDefaultButton;
            aDialog.PopupWindow(GetCurrentWindow());
		}

		void ReportMemory()
		{
			mDebugger.FullReportMemory();
			if (mBfResolveSystem != null)
            	mBfResolveSystem.ReportMemory();
            mBfBuildSystem.ReportMemory();
            Internal.ReportMemory();
            GC.Report();
		}

		[IDECommand]
		void DoAttach()
		{
			var widgetWindow = GetCurrentWindow();
			if (widgetWindow != null)
			{
			    var attachDialog = new AttachDialog();
			    attachDialog.PopupWindow(mMainWindow);
			}
		}

		[IDECommand]
		void DoLaunch()
		{
			if (mLaunchDialog != null)
			{
			    mLaunchDialog.mWidgetWindow.SetForeground();
			    return;
			}

			mLaunchDialog = new LaunchDialog();
			mLaunchDialog.PopupWindow(mMainWindow);
			mLaunchDialog.mOnClosed.Add(new () => { mLaunchDialog = null; });
		}
		
		void DoProfile()
		{
			if (gApp.mProfilePanel.mUserProfiler != null)
			{
				ShowProfilePanel();
				gApp.mProfilePanel.StopProfiling();
			}
			else
			{
				ShowProfilePanel();
				gApp.mProfilePanel.StartProfiling();
			}
		}

		[IDECommand]
		void NavigateBackwards()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if ((sourceViewPanel != null) && (sourceViewPanel.HasFocus()))
			{
				var sourceEditWidgetContent = (SourceEditWidgetContent)sourceViewPanel.mEditWidget.mEditWidgetContent;
				if (sourceEditWidgetContent.IsAtCurrentHistory())
				{
					mHistoryManager.PrevHistory();
					return;
				}
			}

			mHistoryManager.GoToCurrentHistory();
		}

		[IDECommand]
		void NavigateForwards()
		{
			mHistoryManager.NextHistory();
		}

		void ExitTest()
		{
			sExitTest = true;
			Stop();
		}

		void ToggleCheck(IMenu menu, ref bool checkVal)
		{
			checkVal = !checkVal;
			var sysMenu = (SysMenu)menu;
			sysMenu.Modify(null, null, null, true, checkVal ? 1 : 0);
		}

		public bool AreTestsRunning()
		{
			return (mTestManager != null);
		}

		protected void RunTests(bool debug)
		{
			if (mOutputPanel != null)
			{
				ShowPanel(mOutputPanel, false);
				mOutputPanel.Clear();
			}

			if (AreTestsRunning())
			{
				OutputLineSmart("ERROR: Tests already running");
				return;
			}

			if ((mDebugger != null) && (mDebugger.mIsRunning))
			{
				OutputLineSmart("ERROR: Tests cannot be run while program is executing");
				return;
			}

			String prevConfigName = scope String(mConfigName);

			var workspaceOptions = GetCurWorkspaceOptions();
			if (workspaceOptions.mBuildKind != .Test)
			{
				mMainFrame.mStatusBar.SelectConfig("Test");
			}

			workspaceOptions = GetCurWorkspaceOptions();
			if (workspaceOptions.mBuildKind != .Test)
			{
				mMainFrame.mStatusBar.SelectConfig(prevConfigName);
				OutputLineSmart("ERROR: No valid Test workspace configuration exists");
				return;
			}

			mLastTestFailed = false;
			mTestManager = new TestManager();
			mTestManager.mPrevConfigName = new String(prevConfigName);
			mTestManager.mDebug = debug;
			mTestManager.mIncludeIgnored = mTestIncludeIgnored;

			if (mOutputPanel != null)
				mOutputPanel.Clear();
			OutputLine("Compiling for testing...");
			if (!Compile(.Test, null))
			{
				mTestManager.BuildFailed();
			}
		}

		[IDECommand]
		public void Cmd_RunAllTests()
		{
			RunTests(false);
		}

		[IDECommand]
		public void Cmd_TestEnableConsole()
		{
			let ideCommand = gApp.mCommands.mCommandMap["Test Enable Console"];
			ToggleCheck(ideCommand.mMenuItem, ref mTestEnableConsole);
		}

		[IDECommand]
		public void Cmd_TestIncludeIgnored()
		{
			let ideCommand = gApp.mCommands.mCommandMap["Test Include Ignored"];
			ToggleCheck(ideCommand.mMenuItem, ref mTestIncludeIgnored);
		}

        public void CreateMenu()
        {
			scope AutoBeefPerf("IDEApp.CreateMenu");

            SysMenu root = mMainWindow.mSysMenu;

			String keyStr = scope String();
			
			SysMenu AddMenuItem(SysMenu menu, String dispString, String cmdName, MenuItemUpdateHandler menuItemUpdateHandler = null,
            	SysBitmap bitmap = null, bool enabled = true,  int32 checkState = -1, bool radioCheck = false)
			{
				let ideCommand = mCommands.mCommandMap[cmdName];
				if (ideCommand != null)
				{
					keyStr.Clear();
					ideCommand.ToString(keyStr);
					keyStr.Insert(0, "#");
				}

				let itemMenu = menu.AddMenuItem(dispString, (ideCommand != null) ? keyStr : null, new (evt) => ideCommand.mAction(), menuItemUpdateHandler, bitmap, enabled, checkState, radioCheck);
				if (ideCommand != null)
				{
					ideCommand.mMenuItem = itemMenu;
				}

				return itemMenu;
			}

			//////////

            SysMenu subMenu = root.AddMenuItem("&File");
			let newMenu = subMenu.AddMenuItem("&New");
			AddMenuItem(newMenu, "New &Workspace", "New Workspace");
            AddMenuItem(newMenu, "New &Project", "New Project");
			AddMenuItem(newMenu, "New &Debug Session", "New Debug Session");
			AddMenuItem(newMenu, "New &File", "New File");

            let openMenu = subMenu.AddMenuItem("&Open");
			//openMenu.AddMenuItem("&Open Workspace...", GetCmdKey("Open Workspace"), new (evt) => { OpenWorkspace(); } );
			AddMenuItem(openMenu, "Open &Workspace...", "Open Workspace");
			AddMenuItem(openMenu, "Open &Project...", "Open Project");
			AddMenuItem(openMenu, "Open &Debug Session...", "Open Debug Session");
			AddMenuItem(openMenu, "Open &File...", "Open File");
			AddMenuItem(openMenu, "&Open File in Workspace", "Open File in Workspace");
			AddMenuItem(openMenu, "&Open Corresponding (cpp/h)", "Open Corresponding");
			AddMenuItem(openMenu, "Open &Crash Dump...", "Open Crash Dump");

			let recentMenu = subMenu.AddMenuItem("Open &Recent");
			mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedWorkspace].mMenu = recentMenu.AddMenuItem("Open Recent &Workspace");
			mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedDebugSession].mMenu = recentMenu.AddMenuItem("Open Recent &Debug Session");
			mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedFile].mMenu = recentMenu.AddMenuItem("Open Recent &File");
			mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedCrashDump].mMenu = recentMenu.AddMenuItem("Open Recent &Crash Dump");

            AddMenuItem(subMenu, "&Save File","Save File");
            AddMenuItem(subMenu, "Save &As...", "Save As");
			AddMenuItem(subMenu, "Save A&ll", "Save All");
			AddMenuItem(subMenu, "N&ew View into File", "New View Into File");
			let prefMenu = subMenu.AddMenuItem("&Preferences");
			//prefMenu.AddMenuItem("&Keyboard Shortcuts", null, new (evt) => { ShowKeyboardShortcuts(); });
			AddMenuItem(prefMenu, "&Settings", "Settings");
			AddMenuItem(subMenu, "Close Workspace", "Close Workspace");
            AddMenuItem(subMenu, "E&xit", "Exit");

			//////////

            subMenu = root.AddMenuItem("&Edit");
            AddMenuItem(subMenu, "Quick &Find...", "Find in Document");
            AddMenuItem(subMenu, "Quick &Replace...", "Replace in Document");
            AddMenuItem(subMenu, "Find in &Files...", "Find in Files");
            AddMenuItem(subMenu, "Replace in Files...", "Replace in Files");
			AddMenuItem(subMenu, "Find Prev", "Find Prev");
            AddMenuItem(subMenu, "Find Next", "Find Next");
			AddMenuItem(subMenu, "Show &Current", "Show Current");
			
            AddMenuItem(subMenu, "&Goto Line...", "Goto Line");
            AddMenuItem(subMenu, "Goto &Method...", "Goto Method");
            AddMenuItem(subMenu, "&Rename Symbol", "Rename Symbol");
			AddMenuItem(subMenu, "Find &All References", "Find All References");
			AddMenuItem(subMenu, "Find C&lass...", "Find Class");
			subMenu.AddMenuItem(null);

			var encodingMenu = subMenu.AddMenuItem("Encoding");
			var lineEndingMenu = encodingMenu.AddMenuItem("Line Ending");

			void AddLineEndingKind(String name, LineEndingKind lineEndingKind)
			{
				lineEndingMenu.AddMenuItem(name, null,
					new (menu) =>
					{
						var sysMenu = (SysMenu)menu;
						var sourceViewPanel = GetActiveSourceViewPanel();
						if (sourceViewPanel != null)
						{
							if (sourceViewPanel.mEditData.mLineEndingKind != lineEndingKind)
							{
								sourceViewPanel.EditWidget.Content.mData.mCurTextVersionId++;
								sourceViewPanel.mEditData.mLineEndingKind = lineEndingKind;
								sysMenu.mParent.UpdateChildItems();
							}
						}
					},
					new (menu) =>
					{
						var sysMenu = (SysMenu)menu;

						var sourceViewPanel = GetActiveSourceViewPanel();
						if (sourceViewPanel != null)
						{
							sysMenu.Modify(null, null, null, true, (sourceViewPanel.mEditData.mLineEndingKind == lineEndingKind) ? 1 : 0, true);
						}
						else
						{
							sysMenu.Modify(null, null, null, false, 0, true);
						}
					});
			}
			AddLineEndingKind("Windows", .CrLf);
			AddLineEndingKind("Unix", .Lf);
			AddLineEndingKind("Mac OS 9", .Cr);

			var bookmarkMenu = subMenu.AddMenuItem("Boo&kmarks");
			AddMenuItem(bookmarkMenu, "&Toggle Bookmark", "Bookmark Toggle");
			AddMenuItem(bookmarkMenu, "&Next Bookmark", "Bookmark Next");
			AddMenuItem(bookmarkMenu, "&Previous Bookmark", "Bookmark Prev");
			AddMenuItem(bookmarkMenu, "&Clear Bookmarks", "Bookmark Clear");

			var advancedEditMenu = subMenu.AddMenuItem("Advanced");
			AddMenuItem(advancedEditMenu, "Make Uppercase", "Make Uppercase");
			AddMenuItem(advancedEditMenu, "Make Lowercase", "Make Lowercase");
			mViewWhiteSpace.mMenu = AddMenuItem(advancedEditMenu, "View White Space", "View White Space", null, null, true, mViewWhiteSpace.Bool ? 1 : 0);
			AddMenuItem(advancedEditMenu, "Reformat Document", "Reformat Document");

			if (mSettings.mEnableDevMode)
			{
				subMenu.AddMenuItem(null);
				var internalEditMenu = subMenu.AddMenuItem("Internal");
				internalEditMenu.AddMenuItem("Hilight Cursor References", null, new (menu) => { ToggleCheck(menu, ref gApp.mSettings.mEditorSettings.mHiliteCursorReferences); }, null, null, true, gApp.mSettings.mEditorSettings.mHiliteCursorReferences ? 1 : 0);
				internalEditMenu.AddMenuItem("Delayed Autocomplete", null, new (menu) => { ToggleCheck(menu, ref gApp.mDbgDelayedAutocomplete); }, null, null, true, gApp.mDbgDelayedAutocomplete ? 1 : 0);
			}

			//////////

            subMenu = root.AddMenuItem("&View");
			AddMenuItem(subMenu, "Work&space Explorer", "Show Workspace Explorer");
			AddMenuItem(subMenu, "C&lass View", "Show Class View");
            AddMenuItem(subMenu, "&Immediate Window", "Show Immediate");
			AddMenuItem(subMenu, "&Threads", "Show Threads");
			AddMenuItem(subMenu, "&Call Stack", "Show Call Stack");
			AddMenuItem(subMenu, "&Watches", "Show Watches");
            AddMenuItem(subMenu, "&Auto Watches", "Show Auto Watches");
            AddMenuItem(subMenu, "&Breakpoints", "Show Breakpoints");
			AddMenuItem(subMenu, "&Memory", "Show Memory");
			AddMenuItem(subMenu, "Mo&dules", "Show Modules");
			AddMenuItem(subMenu, "&Output", "Show Output");
			AddMenuItem(subMenu, "&Find Results", "Show Find Results");
			AddMenuItem(subMenu, "&Profiler", "Show Profiler");
			AddMenuItem(subMenu, "A&utoComplete", "Show Autocomplete Panel");
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "Next Document Panel", "Next Document Panel");
			AddMenuItem(subMenu, "Navigate Backwards", "Navigate Backwards");
			AddMenuItem(subMenu, "Navigate Forwards", "Navigate Forwards");

			//////////

            subMenu = root.AddMenuItem("&Build");
			AddMenuItem(subMenu, "&Build Solution", "Build Solution");
            AddMenuItem(subMenu, "&Clean", "Clean");
            AddMenuItem(subMenu, "Clean Beef", "Clean Beef");
			//subMenu.AddMenuItem("Compile Current File", null, new (menu) => { CompileCurrentFile(); });
            AddMenuItem(subMenu, "Cancel Build", "Cancel Build", new (menu) => { menu.SetDisabled(!IsCompiling); });

			if (mSettings.mEnableDevMode)
			{
				var internalBuildMenu = subMenu.AddMenuItem("Internal");
	            internalBuildMenu.AddMenuItem("Autobuild (Debug)", null, new (menu) => { mDebugAutoBuild = !mDebugAutoBuild; });
	            internalBuildMenu.AddMenuItem("Autorun (Debug)", null, new (menu) => { mDebugAutoRun = !mDebugAutoRun; });
				internalBuildMenu.AddMenuItem("Disable Compiling", null, new (menu) => { ToggleCheck(menu, ref mDisableBuilding); }, null, null, true, mDisableBuilding ? 1 : 0);
			}

			//////////

            subMenu = root.AddMenuItem("&Debug");
			AddMenuItem(subMenu, "&Start Debugging", "Start Debugging");
			AddMenuItem(subMenu, "Start Wit&hout Debugging", "Start Without Debugging");
			AddMenuItem(subMenu, "&Launch Process...", "Launch Process");
			AddMenuItem(subMenu, "&Attach to Process...", "Attach to Process");
			AddMenuItem(subMenu, "&Stop Debugging", "Stop Debugging");
            AddMenuItem(subMenu, "Break All", "Break All");
            AddMenuItem(subMenu, "Remove All Breakpoints", "Remove All Breakpoints");
            AddMenuItem(subMenu, "Show &Disassembly", "Show Disassembly");
			AddMenuItem(subMenu, "&Quick Watch", "Show QuickWatch");
			AddMenuItem(subMenu, "&Profile", "Profile");
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "Step Into", "Step Into");
			AddMenuItem(subMenu, "Step Over", "Step Over");
			AddMenuItem(subMenu, "Step Out", "Step Out");
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "To&ggle Breakpoint", "Breakpoint Toggle");
			AddMenuItem(subMenu, "Toggle Thread Breakpoint", "Breakpoint Toggle Thread");
			var newBreakpointMenu = subMenu.AddMenuItem("New &Breakpoint");
			AddMenuItem(newBreakpointMenu, "&Memory Breakpoint...", "Breakpoint Memory");
			AddMenuItem(newBreakpointMenu, "&Symbol Breakpoint...", "Breakpoint Symbol");

			if (mSettings.mEnableDevMode)
			{
				var internalDebugMenu = subMenu.AddMenuItem("Internal");
				internalDebugMenu.AddMenuItem("Error Test", null, new (menu) => { DoErrorTest(); } );
				internalDebugMenu.AddMenuItem("Reconnect BeefPerf", null, new (menu) => { BeefPerf.RetryConnect(); } );
	            AddMenuItem(internalDebugMenu, "Report Memory", "Report Memory");
				internalDebugMenu.AddMenuItem("Crash", null, new (menu) => { Runtime.FatalError("Bad"); });
				internalDebugMenu.AddMenuItem("Exit Test", null, new (menu) => { ExitTest(); });
				internalDebugMenu.AddMenuItem("Run Test", null, new (menu) => { mRunTest = !mRunTest; });
				internalDebugMenu.AddMenuItem("GC Collect", null, new (menu) =>
	                {
	                    var profileId = Profiler.StartSampling().GetValueOrDefault();
						for (int i < 10)
							GC.Collect(false);
						if (profileId != 0)
							profileId.Dispose();
	                });
				internalDebugMenu.AddMenuItem("Enable GC Collect", null, new (menu) => { ToggleCheck(menu, ref mEnableGCCollect); EnableGCCollect = mEnableGCCollect; }, null, null, true, mEnableGCCollect ? 1 : 0);
				internalDebugMenu.AddMenuItem("Fast Updating", null, new (menu) => { ToggleCheck(menu, ref mDbgFastUpdate); EnableGCCollect = mDbgFastUpdate; }, null, null, true, mDbgFastUpdate ? 1 : 0);
				internalDebugMenu.AddMenuItem("Alloc String", null, new (menu) => { new String("Alloc String"); });
				internalDebugMenu.AddMenuItem("Perform Long Update Checks", null, new (menu) =>
	                {
						bool wantsLongUpdateCheck = mLongUpdateProfileId != 0;
	                    ToggleCheck(menu, ref wantsLongUpdateCheck);
						mLastLongUpdateCheck = 0;
						mLastLongUpdateCheckError = 0;
						if (wantsLongUpdateCheck)
							mLongUpdateProfileId = Profiler.StartSampling("LongUpdate");
						else
						{
							mLongUpdateProfileId.Dispose();
							mLongUpdateProfileId = 0;
						}
	                }, null, null, true, (mLongUpdateProfileId != 0) ? 1 : 0);
			}

			//////////

			var testMenu = root.AddMenuItem("&Test");
			var testRunMenu = testMenu.AddMenuItem("&Run");
			AddMenuItem(testRunMenu, "&All Tests", "Run All Tests");

			var testDebugMenu = testMenu.AddMenuItem("&Debug");
			AddMenuItem(testDebugMenu, "&All Tests", "Debug All Tests");

			AddMenuItem(testMenu, "Enable Console", "Test Enable Console", null, null, true, mTestEnableConsole ? 1 : 0);
			AddMenuItem(testMenu, "Include Ignored Tests", "Test Include Ignored", null, null, true, mTestIncludeIgnored ? 1 : 0);

			//////////

            mWindowMenu = root.AddMenuItem("&Window");
            AddMenuItem(mWindowMenu, "&Close", "Close Window");
			AddMenuItem(mWindowMenu, "&Close All", "Close All Windows");
			AddMenuItem(mWindowMenu, "&Split View", "Split View");

            subMenu = root.AddMenuItem("&Help");
            AddMenuItem(subMenu, "&About", "About");
        }

        IDETabbedView CreateTabbedView()
        {
            var tabbedView = new IDETabbedView();
            tabbedView.mSharedData.mOpenNewWindowDelegate.Add(new (fromTabbedView, newWindow) => SetupNewWindow(newWindow));
			tabbedView.mSharedData.mTabbedViewClosed.Add(new (tabbedView) =>
                {
					if (tabbedView == mActiveDocumentsTabbedView)
						mActiveDocumentsTabbedView = null;
                });
            return tabbedView;
        }

        void SetupNewWindow(WidgetWindow window)
        {
            window.mOnWindowKeyDown.Add(new => SysKeyDown);
            window.mOnWindowCloseQuery.Add(new => SecondaryAllowClose);
        }        

		DarkTabbedView FindDocumentTabbedView()
		{
			for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
			{
			    var window = mWindows[windowIdx];
			    var widgetWindow = window as WidgetWindow;
			    if (widgetWindow != null)
			    {
			        var darkDockingFrame = widgetWindow.mRootWidget as DarkDockingFrame;
			        if (widgetWindow == mMainWindow)
			            darkDockingFrame = mDockingFrame;

					//DarkTabbedView documentTabbedView = null;

					DarkTabbedView documentTabbedView = null;
			        if (darkDockingFrame != null)
			        {
			            darkDockingFrame.WithAllDockedWidgets(scope [&] (dockedWidget) =>
			                {
								bool hadSource = false;
			                    var tabbedView = dockedWidget as DarkTabbedView;
			                    if (tabbedView != null)
			                    {
									tabbedView.WithTabs(scope [&] (tab) =>
                                        {
											if (documentTabbedView != null)
												return;
											var content = tab.mContent;
											if ((content is SourceViewPanel) ||
												(content is DisassemblyPanel))
												hadSource = true;
                                        });
								}
								if (hadSource)
									documentTabbedView = tabbedView;
			                });
			        }
					if (documentTabbedView != null)
						return documentTabbedView;
			    }
			}
			return null;
		}

        public DarkTabbedView GetDefaultDocumentTabbedView()
        {
            if ((mActiveDocumentsTabbedView == null) || (mActiveDocumentsTabbedView.mParent == null))
            {
                mActiveDocumentsTabbedView = CreateTabbedView();
                mActiveDocumentsTabbedView.SetRequestedSize(150, 150);
				mActiveDocumentsTabbedView.mIsFillWidget = true;
				mActiveDocumentsTabbedView.mAutoClose = false;

				if ((mProjectPanel != null) && (mProjectPanel.mWidgetWindow != null))
				{
					if (var tabbedView = mProjectPanel.mParent as TabbedView)
					{
						if (var dockingFrame = tabbedView.mParent as DockingFrame)
						{
							dockingFrame.AddDockedWidget(mActiveDocumentsTabbedView, tabbedView, DockingFrame.WidgetAlign.Right);
							return mActiveDocumentsTabbedView;
						}
					}
				}
				
                mDockingFrame.AddDockedWidget(mActiveDocumentsTabbedView, null, DockingFrame.WidgetAlign.Right);
            }

            return mActiveDocumentsTabbedView;
        }

        void PopulateDocumentMenu(DarkTabbedView tabbedView, Menu menu)
        {
            WithTabs(scope (tab) =>
                {
                    var menuItem = menu.AddItem(tab.mLabel);
                    menuItem.mOnMenuItemSelected.Add(new (selMenuItem) =>
                        {
                            TabbedView.TabButton activateTab = tab;
                            activateTab.Activate();
                        });
                });
        }

        public void WithDocumentTabbedViews(Action<DarkTabbedView> func)
        {
            for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
            {
                var window = mWindows[windowIdx];
                var widgetWindow = window as WidgetWindow;
                if (widgetWindow != null)
                {
                    var darkDockingFrame = widgetWindow.mRootWidget as DarkDockingFrame;
                    if (widgetWindow == mMainWindow)
                        darkDockingFrame = mDockingFrame;

                    if (darkDockingFrame != null)
                    {
                        darkDockingFrame.WithAllDockedWidgets(scope (dockedWidget) =>
                            {
                                var tabbedView = dockedWidget as DarkTabbedView;
                                if (tabbedView != null)
                                    func(tabbedView);
                            });
                    }
                }
            }
        }

        public void EnsureDocumentArea()
        {
            GetDefaultDocumentTabbedView();            
        }

        public Widget GetActivePanel()
        {
			if (mRunningTestScript)
				return mLastActivePanel;

            for (var window in mWindows)
            {
				if (!window.mHasFocus)
					continue;

                var widgetWindow = window as WidgetWindow;
                if (widgetWindow != null)
                {
                    var focusWidget = widgetWindow.mFocusWidget;

					if ((focusWidget == null) && (var hoverWatch = widgetWindow.mRootWidget as HoverWatch))
					{
						return hoverWatch.mTextPanel;
					}

                    while ((focusWidget != null) && (focusWidget.mParent != null))
                    {
                        if (focusWidget.mParent is TabbedView)
                            return focusWidget;
                        focusWidget = focusWidget.mParent;
                    }
                }
            }
            return null;
        }

		public BFWindow GetActiveWindow()
		{
			for (let window in mWindows)
				if (window.mHasFocus)
					return window;
			return mMainWindow;
		}

		public Widget GetActiveDocumentPanel()
		{
		    var activePanel = GetActivePanel();
			if ((activePanel is SourceViewPanel) || (activePanel is DisassemblyPanel))
				return activePanel;
		    return null;
		}
        
        public void WithTabs(Action<TabbedView.TabButton> func)
        {
            WithDocumentTabbedViews(scope (documentTabbedView) =>
                {
                    documentTabbedView.WithTabs(func);
                });
        }

        public TabbedView.TabButton GetTab(Widget content)
        {
            TabbedView.TabButton tab = null;
            WithTabs(scope [&] (checkTab) =>
                {
                    if (checkTab.mContent == content)
                        tab = checkTab;
                });
            return tab;
        }        

        public void WithSourceViewPanels(Action<SourceViewPanel> func)
        {            
            WithTabs(scope (tab) =>
                {
                    var sourceViewPanel = tab.mContent as SourceViewPanel;
                    if (sourceViewPanel != null)
                        func(sourceViewPanel);
                });
        }

		TabbedView.TabButton SetupTab(TabbedView tabView, String name, float width, Widget content, bool ownsContent) // 2
		{
			TabbedView.TabButton tabButton = tabView.AddTab(name, width, content, ownsContent);
			if ((var panel = content as Panel) && (var darkTabButton = tabButton as DarkTabbedView.DarkTabButton))
			{
				darkTabButton.mTabWidthOffset = panel.TabWidthOffset;
			}
			tabButton.mCloseClickedEvent.Add(new () => CloseDocument(content)); // 1
			return tabButton;
		}

        public DisassemblyPanel ShowDisassemblyPanel(bool clearData = false)
        {
            DisassemblyPanel disassemblyPanel = null;
            WithTabs(scope [&] (tab) =>
                {
                    if ((disassemblyPanel == null) && (tab.mContent is DisassemblyPanel))
                    {
                        disassemblyPanel = (DisassemblyPanel)tab.mContent;
						disassemblyPanel.ClearQueuedData();
                        tab.Activate();                        
                    }
                });
            if (disassemblyPanel != null)
                return disassemblyPanel;
            
            TabbedView tabbedView = GetDefaultDocumentTabbedView();
            disassemblyPanel = new DisassemblyPanel();
			//diassemblyPanel.Show(filePath);

            var newTabButton = new SourceViewTab();
            newTabButton.Label = DisassemblyPanel.sPanelName;
            newTabButton.mWantWidth = newTabButton.GetWantWidth();
            newTabButton.mHeight = tabbedView.mTabHeight; 
            newTabButton.mContent = disassemblyPanel;
            tabbedView.AddTab(newTabButton);
            
            newTabButton.mCloseClickedEvent.Add(new () => CloseDocument(disassemblyPanel));
            newTabButton.Activate();
			//diassemblyPanel.FocusEdit();

			mLastActivePanel = disassemblyPanel;
            return disassemblyPanel;
        }

        public class SourceViewTab : DarkTabbedView.DarkTabButton
        {
            public float GetWantWidth()
            {
                return DarkTheme.sDarkTheme.mSmallFont.GetWidth(mLabel) + DarkTheme.GetScaled(40);
            }

			public override void Activate(bool setFocus)
			{
				base.Activate(setFocus);

				if ((mUpdateCnt > 0) && (mTabbedView.mUpdateCnt == 0) && (mTabbedView.mTabs.Count == 1))
				{
					//bool isDocument = mContent;

					// We were dropped onto a new tabbed view, mark it as a document frame

					bool isFillWidget = false;
					if (mContent is SourceViewPanel)
						isFillWidget = true;
					if (mContent is DisassemblyPanel)
						isFillWidget = true;

					if (isFillWidget)
					{
						mTabbedView.mIsFillWidget = true;
						mTabbedView.mHasFillWidget = true;
					}
				}
			}

            public override void Draw(Graphics g)
            {
                base.Draw(g);

				if (mWidth < mWantWidth / 2)
					return;

                var sourceViewPanel = mContent as SourceViewPanel;
                if (sourceViewPanel != null)
				{
	                if (sourceViewPanel.HasUnsavedChanges())
					{
						g.SetFont(IDEApp.sApp.mTinyCodeFont);
	                    g.DrawString("*", mWantWidth - DarkTheme.sUnitSize + GS!(2), 0);
					}
					else if (sourceViewPanel.mLoadFailed)
					{
						g.SetFont(IDEApp.sApp.mCodeFont);
						using (g.PushColor(0xFFFF8080))
							g.DrawString("!", mWantWidth - DarkTheme.sUnitSize, 0);
					}
				}
				else if (let findResultsPanel = mContent as FindResultsPanel)
				{
					if (findResultsPanel.IsSearching)
					{
						g.SetFont(IDEApp.sApp.mTinyCodeFont);
						String rotChars = @"/-\|";
						StringView sv = .(rotChars, (mUpdateCnt / 16) % 4, 1);
						g.DrawString(sv, mWantWidth - DarkTheme.sUnitSize, 0);
					}
				}
				else if (let profilePanel = mContent as ProfilePanel)
				{
					if (profilePanel.IsSamplingHidden)
					{
						//using (g.PushColor(((mUpdateCnt / 20) % 2 == 0) ? 0xFFF0F0F0 : 0xFFFFFFFF))
						using (g.PushColor(0x80FFFFFF))
							g.Draw(DarkTheme.sDarkTheme.GetImage(.RedDot), GS!(8), GS!(0));
					}
				}
            }

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                base.MouseDown(x, y, btn, btnCount);

				if (btn == 1)
				{
					Menu menu = new Menu();
					if (var sourceViewPanel = mContent as SourceViewPanel)
					{
						var item = menu.AddItem("Copy Full Path");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								gApp.SetClipboardText(sourceViewPanel.mFilePath);
							});
						item = menu.AddItem("Open Containing Folder");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								let directory = scope String();
								Path.GetDirectoryPath(sourceViewPanel.mFilePath, directory);

								ProcessStartInfo procInfo = scope ProcessStartInfo();
								procInfo.UseShellExecute = true;
								procInfo.SetFileName(directory);

								let process = scope SpawnedProcess();
								process.Start(procInfo).IgnoreError();
							});
					}

					if (menu.mItems.Count > 0)
					{
						SelfToRootTranslate(x, mHeight, var windowX, var windowY);
						MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
						menuWidget.Init(mWidgetWindow.mRootWidget, windowX, windowY);
					}
					else
						delete menu;
				}
                
                if ((mIsRightTab) && (btn == 0) && (btnCount > 1))
                {
                    IDEApp.sApp.MakeTabPermanent(this);
                }
            }

            public override void Update()
            {
                base.Update();
                Point point;
                if (DarkTooltipManager.CheckMouseover(this, 25, out point))
                {
                    var sourceViewPanel = mContent as SourceViewPanel;
                    if (sourceViewPanel != null)
                        DarkTooltipManager.ShowTooltip(sourceViewPanel.mFilePath, this, point.x, 14);
                }
            }
        }

        public SourceViewPanel FindSourceViewPanel(String filePath)
        {
			if (filePath == null)
				return null;
            String useFilePath = scope String(filePath);
            if (!IDEUtils.FixFilePath(useFilePath))
				return null;
            
            SourceViewPanel sourceViewPanel = null;

            WithTabs(scope [&] (tabButton) =>
                {
                    if ((sourceViewPanel == null) && (tabButton.mContent is SourceViewPanel))
                    {
                        var checkedResourceViewPanel = (SourceViewPanel)tabButton.mContent;
                        if (Path.Equals(checkedResourceViewPanel.mFilePath, useFilePath))
                            sourceViewPanel = checkedResourceViewPanel;
                    }
                });
            return sourceViewPanel;
        }

        void MakeTabPermanent(DarkTabbedView.DarkTabButton tabButton)
        {
            tabButton.mDragHelper.mAllowDrag = false;
            tabButton.mTextColor = Color.White;
            tabButton.mIsRightTab = false;            
            var darkTabbedView = (DarkTabbedView)tabButton.mTabbedView;
            darkTabbedView.SetRightTab(null, false);
            darkTabbedView.AddTab(tabButton);
            tabButton.Activate();
        }

        public SourceEditWidget CreateSourceEditWidget(SourceEditWidget refEditWidget = null)
        {         
            var editWidget = new SourceEditWidget(null, refEditWidget);
            editWidget.Content.mIsMultiline = true;
            editWidget.Content.mWordWrap = false;
            editWidget.InitScrollbars(true, true);

            var editWidgetContent = (SourceEditWidgetContent)editWidget.Content;
			//mEditWidget.mVertScrollbar.mScrollIncrement = editWidgetContent.mFont.GetLineSpacing();
            editWidgetContent.mHiliteColor = 0xFF384858;
            editWidgetContent.mUnfocusedHiliteColor = 0x80384858;

            return editWidget;
        }

		public bool CreateEditDataEditWidget(FileEditData editData, SourceHash.Kind hashKind = .MD5)
		{
			if (editData.mEditWidget != null)
				return true;
			var text = scope String();
			if (LoadTextFile(editData.mFilePath, text, true, scope () =>
				{
					for (int i < text.Length)
					{
						char8 c = text[i];
						if (c == '\r')
						{
							if ((i < text.Length - 1) && (text[i + 1] == '\n'))
								editData.mLineEndingKind = .CrLf;
							else
								editData.mLineEndingKind = .Cr;
							break;
						}
						else if (c == '\n')
						{
							editData.mLineEndingKind = .Lf;
							break;
						}
					}

					editData.mLoadedHash = SourceHash.Create(hashKind, text);
				} ) case .Err)
				return false;

			mFileWatcher.FileIsValid(editData.mFilePath);

			using (mMonitor.Enter())
			{
				if (editData.mEditWidget != null)
					return true;
				editData.mEditWidget = CreateSourceEditWidget();
				editData.mEditWidget.Content.AppendText(text);
				editData.mOwnsEditWidget = true;
				editData.mLastFileTextVersion = editData.mEditWidget.Content.mData.mCurTextVersionId;
				editData.mEditWidgetCreatedEvent();
			}
			mFileDataDataRevision++;
			return true;
		}

		public FileEditData GetEditData(String filePath, bool createEditData = true, bool createEditDataWidget = true, SourceHash.Kind hashKind = .MD5)
		{
			FileEditData editData;
			using (mMonitor.Enter())
			{
				String fixedFilePath = scope String(filePath);
				IDEUtils.FixFilePath(fixedFilePath);
				if (!Environment.IsFileSystemCaseSensitive)
					fixedFilePath.ToUpper();
				if (!mFileEditData.TryGetValue(fixedFilePath, out editData))
				{
					if (createEditData)
					{
						editData = new FileEditData();
						if (filePath.EndsWith("main.cs"))
						{
							NOP!();
						}
						editData.mFilePath = new String(filePath);
						mFileEditData.Add(new String(fixedFilePath), editData);
					}
				}
			}
			if (createEditDataWidget)
				CreateEditDataEditWidget(editData, hashKind);
			return editData;
		}

		public void RenameEditData(String oldPath, String newPath)
		{
			using (mMonitor.Enter())
			{
				String oldFixedFilePath = scope String(oldPath);
				IDEUtils.FixFilePath(oldFixedFilePath);
				if (!Environment.IsFileSystemCaseSensitive)
					oldFixedFilePath.ToUpper();

				String newFixedFilePath = scope String(newPath);
				IDEUtils.FixFilePath(newFixedFilePath);
				if (!Environment.IsFileSystemCaseSensitive)
					newFixedFilePath.ToUpper();

				String outKey;
				FileEditData editData;
				if (mFileEditData.TryGetValue(oldFixedFilePath, out outKey, out editData))
				{
					mFileEditData.Remove(oldFixedFilePath);
					delete outKey;

					editData.mFilePath.Set(newPath);

					String* newKeyPtr;
					FileEditData* newEditDataPtr;
					if (mFileEditData.TryAdd(newFixedFilePath, out newKeyPtr, out newEditDataPtr))
					{
						*newKeyPtr = new String(newFixedFilePath);
						*newEditDataPtr = editData;
					}
					else
					{
						let oldEditData = *newEditDataPtr;

						// This can happen if we rename a file to the name of a file that used to exist and is bound to
						//  another source view panel
						WithTabs(scope (tab) =>
							{
								if (var sourceViewPanel = tab.mContent as SourceViewPanel)
								{
									if (sourceViewPanel.mEditData == oldEditData)
									{
										tab.mTabbedView.RemoveTab(tab, true);
									}
								}
							});
						for (var projectSource in oldEditData.mProjectSources)
							projectSource.mEditData = editData;
						gApp.ProcessDeferredDeletes();
						oldEditData.Deref();

						//editData.Deref();
						*newEditDataPtr = editData;
					}
				}
			}
		}

		public void DeleteEditData(FileEditData editData)
		{
			if (editData.mOwnsEditWidget)
			{
				delete editData.mEditWidget;
				editData.mEditWidget = null;
				editData.mOwnsEditWidget = false;
			}
			//mFileEditData.Remove(editData);
			//delete editData;
		}

        public FileEditData GetEditData(ProjectSource projectSource, bool createEditWidget = true, SourceHash.Kind hashKind = .MD5)
        {
			using (mMonitor.Enter())
			{
	            if (projectSource.mEditData == null)
	            {
					String filePath = scope String();
					projectSource.GetFullImportPath(filePath);

					var editData = GetEditData(filePath, true, false);
					if (editData != null)
					{
						editData.mProjectSources.Add(projectSource);
						editData.Ref();
						projectSource.mEditData = editData;
					}
				}

					/*editData = CreateEditData(filePath);
	                /*if (projectSource.mSavedContent == null)
	                {
	                    editData = CreateEditData(filePath);
	                }
	                else
	                {
	                    editData = new FileEditData();
						editData.mFilePath = new String(filePath);
	                    editData.mEditWidget = CreateSourceEditWidget();
	                    editData.mEditWidget.Content.AppendText(projectSource.mSavedContent);
	                    editData.mEditWidget.Content.mData.mTextIdData.DuplicateFrom(projectSource.mSavedCharIdData);
						editData.mOwnsEditWidget = true;
	                }*/

					if (editData != null)
					{
						editData.Ref();
						mFileEditData.Add(editData);
						projectSource.mEditData = editData;
						projectSource.mEditData.mLastFileTextVersion = projectSource.mEditData.mEditWidget.Content.mData.mCurTextVersionId;
					}  
	            }
	            return projectSource.mEditData;*/
			}

			if (createEditWidget)
				CreateEditDataEditWidget(projectSource.mEditData);
			return projectSource.mEditData;
        }		

        public SourceViewPanel ShowSourceFile(String filePath, ProjectSource projectSource = null, SourceShowType showType = SourceShowType.ShowExisting, bool setFocus = true)
        {
			//TODO: PUT BACK!
			//return null;

#unwarn
			String useFilePath = filePath;
			var useProjectSource = projectSource;
			if ((useFilePath == null) && (useProjectSource != null))
			{
				useFilePath = scope:: String();
				useProjectSource.GetFullImportPath(useFilePath);
			}
			else if (useFilePath != null)
			{
				useFilePath = scope:: String(useFilePath);
			}

            
            if ((useFilePath != null) && (!IDEUtils.FixFilePath(useFilePath)))
				return null;
            if ((useFilePath == null) & (showType != .New))
                return null;

            SourceViewPanel sourceViewPanel = null;
			DarkTabbedView.DarkTabButton sourceViewPanelTab = null;

			if ((useProjectSource == null) && (useFilePath != null))
			{
				useProjectSource = FindProjectSourceItem(useFilePath);
				if (useProjectSource != null)
				{
					var projectSourcePath = scope:: String();
					useProjectSource.GetFullImportPath(projectSourcePath);
					useFilePath = projectSourcePath;
				}
			}

			if (showType != SourceShowType.New)
			{
				Action<TabbedView.TabButton> tabFunc = scope [&] (tabButton) =>
	                {
	                    var darkTabButton = (DarkTabbedView.DarkTabButton)tabButton;
	                    if (tabButton.mContent is SourceViewPanel)
	                    {						
	                        var checkSourceViewPanel = (SourceViewPanel)tabButton.mContent;
	
	                        if (checkSourceViewPanel.FileNameMatches(useFilePath))
	                        {
								if (sourceViewPanel != null)
								{
									// Already found one that matches our active tabbed view?
									if (sourceViewPanelTab.mTabbedView == mActiveDocumentsTabbedView)
										return;
								}

								sourceViewPanel = checkSourceViewPanel;
								sourceViewPanelTab = darkTabButton;
	                        }
	                    }
	                };

				if ((showType == .ShowExistingInActivePanel) && (mActiveDocumentsTabbedView != null))
				{
					mActiveDocumentsTabbedView.WithTabs(tabFunc);
				}
				else
					WithTabs(tabFunc);

				if (sourceViewPanelTab != null)
				{
					//matchedTabButton = tabButton;
					if ((sourceViewPanelTab.mIsRightTab) && (showType != SourceShowType.Temp))
					{
					    MakeTabPermanent(sourceViewPanelTab);
					}

					if ((useProjectSource != null) &&
					    (sourceViewPanel.mProjectSource != useProjectSource))
					{
						//TODO: Change project source in view
						sourceViewPanel.AttachToProjectSource(useProjectSource);
					    //sourceViewPanel.mProjectSource = useProjectSource;
					    //sourceViewPanel.QueueFullRefresh(true);
					}

					if ((sourceViewPanel.mWidgetWindow != null) && (!HasModalDialogs()) && (!mRunningTestScript))
					    sourceViewPanel.mWidgetWindow.SetForeground();
					sourceViewPanelTab.Activate(setFocus);
					sourceViewPanelTab.mTabbedView.FinishTabAnim();
					if (setFocus)
					    sourceViewPanel.FocusEdit();
				}
			}

            if (sourceViewPanel != null)
                return sourceViewPanel;

			//ShowSourceFile(filePath, projectSource, showTemp, setFocus);

            DarkTabbedView tabbedView = GetDefaultDocumentTabbedView();
            sourceViewPanel = new SourceViewPanel();
            bool success;
			if (useProjectSource != null)
            {                
                success = sourceViewPanel.Show(useProjectSource, !mInitialized);
            }
            else
                success = sourceViewPanel.Show(useFilePath, !mInitialized);

            if (!success)
            {
                sourceViewPanel.Close();
				delete sourceViewPanel;
                return null;
            }

            var newTabButton = new SourceViewTab();
            newTabButton.Label = "";
			if (useFilePath != null)
            	Path.GetFileName(useFilePath, newTabButton.mLabel);
			else
				newTabButton.mLabel.Set("untitled");
            newTabButton.mWantWidth = newTabButton.GetWantWidth();
            newTabButton.mHeight = tabbedView.mTabHeight;
            newTabButton.mContent = sourceViewPanel;

            if (showType == SourceShowType.Temp)
            {
                if (tabbedView.mRightTab != null)
                {
                    CloseDocument(tabbedView.mRightTab.mContent);
                    Debug.Assert(tabbedView.mRightTab == null);
                }

                newTabButton.mTextColor = 0xFFC8C8C8;
                newTabButton.mIsRightTab = true;
                tabbedView.SetRightTab(newTabButton);
            }
            else
                tabbedView.AddTab(newTabButton);
            newTabButton.mCloseClickedEvent.Add(new () => DocumentCloseClicked(sourceViewPanel));
            newTabButton.Activate(setFocus);
            if (setFocus)
                sourceViewPanel.FocusEdit();  

            return sourceViewPanel;            
        }

		int32 GetRecentFilesIdx(String filePath)
		{
			return mRecentlyDisplayedFiles.FindIndex(scope (item) => Path.Equals(item, filePath));
		}

		public void UpdateRecentFilesMenuItems(List<String> filesList)
		{

		}

        public void UpdateRecentDisplayedFilesMenuItems()
        {
            if (mWindowMenu == null)
                return;

			RecentFiles.UpdateMenu(mRecentlyDisplayedFiles, mWindowMenu, mRecentlyDisplayedFilesMenuItems, scope (idx, sysMenu) =>
				{
					sysMenu.mOnMenuItemSelected.Add(new (evt) => ShowRecentFile(idx));
				});
        }

		public void UpdateRecentFileMenuItems(RecentFiles.RecentKind recentKind)
		{
			let entry = mSettings.mRecentFiles.mRecents[(int)recentKind];
			if (entry.mMenu == null)
				return;

			RecentFiles.UpdateMenu(entry.mList, entry.mMenu, entry.mMenuItems, scope (idx, sysMenu) =>
				{
					sysMenu.mOnMenuItemSelected.Add(new (evt) => ShowRecentFile(recentKind, idx));
				});
		}

		public void UpdateRecentFileMenuItems()
		{
			if (mWindowMenu == null)
				return;

			for (RecentFiles.RecentKind recentKind = default; recentKind < RecentFiles.RecentKind.COUNT; recentKind++)
			{
				UpdateRecentFileMenuItems(recentKind);
			}
		}

		public void AddRecentFile(RecentFiles.RecentKind recentKind, StringView file)
		{
			mSettings.mRecentFiles.Add(recentKind, file);
			UpdateRecentFileMenuItems(recentKind);
		}

		public void AddToRecentDisplayedFilesList(String path)
		{
			//int idx = mRecentFilesList.IndexOf(path);
			RecentFiles.Add(mRecentlyDisplayedFiles, path);
			UpdateRecentDisplayedFilesMenuItems();
		}

        void ShowRecentFile(int idx, bool setFocus = true)
        {
            String sourceFile = mRecentlyDisplayedFiles[idx];
            if (sourceFile == DisassemblyPanel.sPanelName)
            {
                ShowDisassemblyPanel();
                return;
            }
            ShowSourceFile(sourceFile, null, SourceShowType.ShowExisting, setFocus);
        }

		void ShowRecentFile(RecentFiles.RecentKind recentKind, int idx, bool setFocus = true)
		{
		    String filePath = mSettings.mRecentFiles.mRecents[(int)recentKind].mList[idx];

			switch (recentKind)
			{
			case .OpenedFile:
				ShowSourceFile(filePath, null, SourceShowType.ShowExisting, setFocus);
			case .OpenedWorkspace:
				var selectedPath = scope String()..AppendF(filePath);
				selectedPath.Append(Path.DirectorySeparatorChar);
				selectedPath.Append("BeefSpace.toml");
				String.NewOrSet!(mDeferredOpenFileName, selectedPath);
				mDeferredOpen = .Workspace;
			case .OpenedCrashDump:
				String.NewOrSet!(mDeferredOpenFileName, filePath);
				mDeferredOpen = .CrashDump;
			case .OpenedDebugSession:
				String.NewOrSet!(mDeferredOpenFileName, filePath);
				mDeferredOpen = .DebugSession;
			default:
			}
		}

        void DocumentCloseClicked(Widget documentPanel)
        {
            var sourceViewPanel = documentPanel as SourceViewPanel;
            if (sourceViewPanel == null)
            {
                CloseDocument(documentPanel);
                return;
            }

			// This is a test
			// This is a test

            if ((!sourceViewPanel.HasUnsavedChanges()) || (!sourceViewPanel.IsLastViewOfData()))
            {
                CloseDocument(sourceViewPanel);
                return;
            }

			// This is a test

			String fileName = scope String();
			if (sourceViewPanel.mFilePath != null)
				Path.GetFileName(sourceViewPanel.mFilePath, fileName);
			else
				fileName.Append("untitled");
            Dialog aDialog = ThemeFactory.mDefault.CreateDialog("Save file?", StackStringFormat!("Save changes to '{0}' before closing?", fileName), DarkTheme.sDarkTheme.mIconWarning);
            aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) => { SaveFile(sourceViewPanel); CloseDocument(sourceViewPanel); }); 
            aDialog.AddButton("Don't Save", new (evt) => CloseDocument(sourceViewPanel));            
            aDialog.mEscButton = aDialog.AddButton("Cancel");
            aDialog.PopupWindow(mMainWindow);
        }

        public void CloseDocument(Widget documentPanel)
        {
            bool hasFocus = false;
            var sourceViewPanel = documentPanel as SourceViewPanel;

			if ((documentPanel.mWidgetWindow != null) && (documentPanel.mWidgetWindow.mFocusWidget != null))
			{
				if (documentPanel.mWidgetWindow.mFocusWidget.HasParent(documentPanel))
					hasFocus = true;
			}

            /*if (sourceViewPanel != null)            
                hasFocus = sourceViewPanel.mEditWidget.mHasFocus;*/

            if ((sourceViewPanel != null) && (sourceViewPanel.HasUnsavedChanges()))
            {
				// When we close a Beef file that has modified text, we need to revert by 
				//  reparsing from the actual source file
                if (sourceViewPanel.mIsBeefSource)
                {
                    mBfResolveHelper.DeferReparse(sourceViewPanel.mFilePath, null);
					//mBfResolveHelper.DeferRefreshVisibleViews(null);
                }

				var projectSource = sourceViewPanel.mProjectSource;
                if (projectSource != null)
                {					
                    var editData = GetEditData(projectSource, true);
                    if (editData != null)
                    {
                        var editWidgetContent = editData.mEditWidget.mEditWidgetContent;

						//TODO: Verify this, once we have multiple panes allowed within a single SourceViewContent
						if (editWidgetContent.mData.mUsers.Count == 1) // Is last view of data...
						{
							if ((editData != null) && (editData.mHadRefusedFileChange))
							{
								// If we didn't take an external file change then closing the file means we want to revert 
								//  our data to the version on disk
								sourceViewPanel.Reload();
							}
							else
							{
								// Undo until we either get to whatever the last saved state was, or until we 
								//  get to a global action like renaming a symbol - we need to leave those
								//  so the global undo actually works if invoked from another file
		                        while (editData.HasTextChanged())
		                        {
		                            var nextUndoAction = editWidgetContent.mData.mUndoManager.GetLastUndoAction();
									if (nextUndoAction == null)
										break;
		                            if (nextUndoAction is UndoBatchEnd)
		                            {
		                                var undoBatchEnd = (UndoBatchEnd)nextUndoAction;
		                                if (undoBatchEnd.Name.StartsWith("#"))
		                                {
		                                    break;
		                                }
		                            }
		                            editWidgetContent.mData.mUndoManager.Undo();
		                        }

								editWidgetContent.mData.mTextIdData.Prepare();
							}
						}
                    }
                }
            }

            DarkTabbedView tabbedView = null;
            DarkTabbedView.DarkTabButton tabButton = null;
            WithTabs(scope [&] (tab) =>
                {
                    if (tab.mContent == documentPanel)
                    {
                        tabbedView = (DarkTabbedView)tab.mTabbedView;
                        tabButton = (DarkTabbedView.DarkTabButton)tab;
                    }
                });

            Debug.Assert(tabbedView != null);
            if (tabbedView == null)
                return;

			int32 recentFileIdx = -1;
            if (sourceViewPanel != null)
            {
                sourceViewPanel.Dispose();
				if (sourceViewPanel.mFilePath != null)
					recentFileIdx = GetRecentFilesIdx(sourceViewPanel.mFilePath);
            }

			/*if (tabButton.mIsRightTab)
				tabbedView.SetRightTab(null);
			else*/
                        
            if (documentPanel is DisassemblyPanel)
				recentFileIdx = GetRecentFilesIdx(DisassemblyPanel.sPanelName);
                //mRecentFilesList.Remove(DisassemblyPanel.sPanelName);

			if (recentFileIdx != -1)
			{
				delete mRecentlyDisplayedFiles[recentFileIdx];
				mRecentlyDisplayedFiles.RemoveAt(recentFileIdx);
	            UpdateRecentDisplayedFilesMenuItems();
			}

			TabbedView.TabButton nextTab = null;
			bool foundRemovedTab = false;
			// Select the previous tab or the next one (if this is the first)
			tabbedView.WithTabs(scope [&] (checkTab) =>
                {
					if (checkTab == tabButton)
						foundRemovedTab = true;
					else if ((!foundRemovedTab) || (nextTab == null))
						nextTab = checkTab;
                });

			tabbedView.RemoveTab(tabButton);
			if (nextTab != null)
			{
				nextTab.Activate(hasFocus);
			}
			else if (tabbedView.mAutoClose)
			{
				tabbedView.mParentDockingFrame.RemoveDockedWidget(tabbedView);
				gApp.DeferDelete(tabbedView);

				var documentTabbedView = FindDocumentTabbedView();
				if (documentTabbedView != null)
				{
					Debug.Assert(documentTabbedView != tabbedView);
					// If there is some OTHER document window them show that and remove this empty one
					documentTabbedView.GetActiveTab().Activate();
				}
			}

			//var intDict = scope Dictionary<String, int>();

            /*if (mRecentFilesList.Count >= 1)
            {
				// Show second-last file
                ShowRecentFile(0, hasFocus);
            }*/
			//var newActiveTab = tabbedView.GetActiveTab();
			//if (newActiveTab != null)
				//newActiveTab.mContent.SetFocus();
        }

        void TryCloseCurrentDocument()
        {
            var activeDocumentPanel = GetActiveDocumentPanel();
            if (activeDocumentPanel != null)
            {
                if (activeDocumentPanel is SourceViewPanel)
                {
                    var sourceViewPanel = (SourceViewPanel)activeDocumentPanel;
                    DocumentCloseClicked(sourceViewPanel);
                    return;
                }
                CloseDocument(activeDocumentPanel);
				return;
            }

			var activePanel = GetActivePanel();
			if (activePanel != null)
				CloseDocument(activePanel);
        }

		void TryCloseAllDocuments()
		{
			var docPanels = scope List<Widget>();

			WithTabs(scope [&] (tab) =>
			{
				if ((tab.mContent is SourceViewPanel) || (tab.mTabbedView.mIsFillWidget))
				{	
					docPanels.Add(tab.mContent);
				}				
			});	
	    
			for (var docPanel in docPanels)
				DocumentCloseClicked(docPanel);
		}

		void SplitView()
		{
			var sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				sourceViewPanel.SplitView();
			}
		}

		[IDECommand]
        void CloseCurrentDocument()
        {
            var activeDocumentPanel = GetActiveDocumentPanel();
            if (activeDocumentPanel != null)
                CloseDocument(activeDocumentPanel);
        }        

        public SourceViewPanel ShowProjectItem(ProjectItem projectItem, bool showTemp = true, bool setFocus = true)
        {
            if (projectItem is ProjectSource)
            {
                var projectSource = (ProjectSource)projectItem;
				var fullPath = scope String();
				projectSource.GetFullImportPath(fullPath);
                return ShowSourceFile(fullPath, projectSource, showTemp ? SourceShowType.Temp : SourceShowType.ShowExistingInActivePanel, setFocus);
            }
            return null;
        }

        public SourceViewPanel ShowSourceFileLocation(String filePath, int showHotIdx, int refHotIdx, int line, int column, LocatorType hilitePosition, bool showTemp = false)
        {
            var sourceViewPanel = ShowSourceFile(filePath, null, showTemp ? SourceShowType.Temp : SourceShowType.ShowExisting);
            if (sourceViewPanel == null)
                return null;
            sourceViewPanel.ShowHotFileIdx(showHotIdx);
            sourceViewPanel.ShowFileLocation(refHotIdx, Math.Max(0, line), Math.Max(0, column), hilitePosition);
            return sourceViewPanel;
        }

        public SourceViewPanel ShowSourceFileLocation(String filePath, int32 cursorIdx, LocatorType hilitePosition)
        {
            var sourceViewPanel = ShowSourceFile(filePath);
            sourceViewPanel.ShowFileLocation(cursorIdx, hilitePosition);
            return sourceViewPanel;
        }

        public void ShowPCLocation(int32 stackIdx, bool onlyShowCurrent = false, bool wantShowSource = false, bool wantShowDisassembly = false, bool setFocus = true)
        {
			if (stackIdx == -1)
				return;

			ClearDeferredUserRequest();

            int addr;
            String filePath = scope String();
            int hotIdx;
            int defLineStart;
            int defLineEnd;
            int line;
            int column;
            int language;
            int stackSize;
            mDebugger.CheckCallStack();
            String entryName = scope String();
			DebugManager.FrameFlags frameFlags;

			for (int pass < 2)
			{
	            mDebugger.GetStackFrameInfo(stackIdx, entryName, out addr, filePath, out hotIdx, out defLineStart, out defLineEnd, out line, out column, out language, out stackSize, out frameFlags);

				if (!frameFlags.HasFlag(.HasPendingDebugInfo))
					break;
				
				int bangPos = entryName.IndexOf('!');
				if (bangPos == -1)
					break;
				
				String moduleName = scope String(entryName, 0, bangPos);

				int result = mDebugger.LoadDebugInfoForModule(moduleName);
				if (result == 0)
					break;

				// If we are backgrounding the load then we'll try to open this up afterward
				if (result == 2)
				{
					SetDeferredUserRequest(new DeferredShowPCLocation(stackIdx));
					return;
				}

				// Try again on result == 1
				filePath.Clear();
				entryName.Clear();
				mCallStackPanel.MarkCallStackDirty();
				mThreadPanel.MarkThreadsDirty();
			}

			bool useWantShowDisassembly = wantShowDisassembly;

            for (var breakpoint in mDebugger.mBreakpointList)
            {
                if (((breakpoint.mAddressRequested) || (breakpoint.mInstrOffset != -1)) &&
                    (breakpoint.ContainsAddress(addr)))
                    useWantShowDisassembly = true;
            }

			String aliasFilePath = null;
			String loadCmd = null;

			SourceHash hash = default;
			int hashPos = filePath.IndexOf('#');
			if (hashPos != -1)
			{
				let hashStr = StringView(filePath, hashPos + 1);

				if (hashStr.Length == 32)
				{
					if (MD5Hash.Parse(hashStr) case .Ok(let parsedHash))
					{
				        hash = .MD5(parsedHash);
					}
				}
				else
				{
					if (SHA256Hash.Parse(hashStr) case .Ok(let parsedHash))
					{
						hash = .SHA256(parsedHash);
					}
				}
				filePath.RemoveToEnd(hashPos);

				if (frameFlags.HasFlag(.CanLoadOldVersion))
				{
					aliasFilePath = scope:: String(filePath);

					String fileText = scope String();
					SourceHash fileHash = default;

					var hashKind = hash.GetKind();
					if (hashKind == .None)
						hashKind = .MD5;

					LoadTextFile(filePath, fileText, false, scope [&] () => { hash = SourceHash.Create(hashKind, fileText); }).IgnoreError();

					if (fileHash != hash)
					{
						String outFileInfo = scope String();
						mDebugger.GetStackFrameOldFileInfo(mDebugger.mActiveCallStackIdx, outFileInfo);

						var args = outFileInfo.Split!('\n');
						if (args.Count == 3)
						{
							filePath.Set(args[0]);
							loadCmd = scope:: String(args[1]);
						}
					}
				}
			}

			// If we attach to an executable and we happen to have the Diassembly panel shown, STILL just show the source instead of the disassembly at first
			if (mIsAttachPendingSourceShow)
			{
				mInDisassemblyView = false;
				mIsAttachPendingSourceShow = false;
			}

            bool hasSource = filePath.Length != 0;
            if ((IsInDisassemblyMode(wantShowSource)) || (!hasSource) || (useWantShowDisassembly))
            {
				if (frameFlags.HasFlag(.HasPendingDebugInfo))
				{
                    SetDeferredUserRequest(new DeferredShowPCLocation(stackIdx));
				}
				else
				{
	                var disassemblyPanel = ShowDisassemblyPanel(true);
					if (aliasFilePath != null)
						String.NewOrSet!(disassemblyPanel.mAliasFilePath, aliasFilePath);
	                disassemblyPanel.Show(addr, filePath, line, column, hotIdx, defLineStart, defLineEnd);
					disassemblyPanel.mSourceHash = hash;
				}
            }
            else if (filePath.Length > 0)
            {
				if ((IsCrashDump) && (!mShowedFirstDocument))
				{
					mShowedFirstDocument = true;
					ShowCallstack();
				}

                var sourceViewPanel = ShowSourceFile(filePath, null, SourceShowType.ShowExisting, setFocus);
                if (sourceViewPanel != null)
                {
					sourceViewPanel.mIsSourceCode = true; // It's always source code, even if there is no extension (ie: stl types like "vector")

					if ((aliasFilePath != null) && (sourceViewPanel.mAliasFilePath == null))
						String.NewOrSet!(sourceViewPanel.mAliasFilePath, aliasFilePath);

					if (sourceViewPanel.mLoadFailed)
					{
						sourceViewPanel.mLoadedHash = hash;
						if (loadCmd != null)
						{
							sourceViewPanel.SetLoadCmd(loadCmd);
						}
					}

                    int showHotIdx = -1;
                    if (!onlyShowCurrent)
                    {
						bool checkForChange = false;
						if (frameFlags.HasFlag(.WasHotReplaced))
							checkForChange = true;
						else
						{
							// If we make a trivial change (ie: characters in a comment) then we won't actually generate a new method
							//  So don't show as an "old file" if we don't actually have any file changes and we're on the current hot version
							if ((sourceViewPanel.mProjectSource != null) && (sourceViewPanel.mProjectSource.mHasChangedSinceLastSuccessfulCompile))
								checkForChange = true;
						}

						if (checkForChange)
                        {
                            if (sourceViewPanel.HasTextChangedSinceCompile(defLineStart, defLineEnd, hotIdx))
                                showHotIdx = hotIdx;
                        }
						/*else
						{
							if (sourceViewPanel.HasTextChangedSinceCompile(defLineStart, defLineEnd, -1))
								showHotIdx = mWorkspace.GetHighestCompileIdx();
						}*/
                    }

                    sourceViewPanel.ShowHotFileIdx(showHotIdx);
                    sourceViewPanel.ShowFileLocation(hotIdx, Math.Max(0, line), Math.Max(0, column), LocatorType.Smart);
                }                

                var disassemblyPanel = TryGetDisassemblyPanel(false);
                if (disassemblyPanel != null)
                    disassemblyPanel.Show(addr, filePath, line, column, hotIdx, defLineStart, defLineEnd);
            }
        }

		public override void UnhandledCommandLine(String key, String value)
		{
			Fail(StackStringFormat!("Unhandled command line param: {0}", key));
		}

        public override bool HandleCommandLineParam(String key, String value)
        {
			if (mLaunchData != null)
			{
				if (mLaunchData.mArgs != null)
				{
					if (!mLaunchData.mArgs.IsEmpty)
						mLaunchData.mArgs.Append(" ");
					mLaunchData.mArgs.Append(key);
					if (value != null)
						mLaunchData.mArgs.Append("=", value);
					return true;
				}

				if ((key == "--") && (value == null))
				{
					mLaunchData.mArgs = new .();
					return true;
				}
			}

            if (base.HandleCommandLineParam(key, value))
				return true;

			if (value == null)
			{
				switch (key)
				{
				case "-autoshutdown":
					mDebugAutoShutdownCounter = 200;
				case "-new":
					mVerb = .New;
				case "-testNoExit":
					mExitWhenTestScriptDone = false;
				case "-clean":
					mWantsClean = true;
				case "-dbgCompileDump":
					mDbgCompileDump = true;
				case "-launchPaused":
					if (mLaunchData != null)
						mLaunchData.mPaused = true;
					else
						Fail("'-launchPaused' can only be used after '-launch'");
				default:
					return false;
				}
				return true;
			}
			else
			{
				switch (key)
				{
#if BF_PLATFORM_WINDOWS
				case "-attachHandle":
					mProcessAttachHandle = (Windows.EventHandle)int32.Parse(value).GetValueOrDefault();
#endif
				case "-attachId":
	                mProcessAttachId = int32.Parse(value).GetValueOrDefault();
				case "-config":
					mConfigName.Set(value);
				case "-launch":
					if (mLaunchData == null)
						mLaunchData = new .();
					mLaunchData.mTargetPath.Set(value);
#if BF_PLATFORM_WINDOWS
				case "-minidump":
						String.NewOrSet!(mCrashDumpPath, value);
#endif
				case "-platform":
					mPlatformName.Set(value);
	            case "-test":
					Runtime.SetCrashReportKind(.PrintOnly);

					String absTestPath = scope String();
					if (mWorkspace.mDir != null)
						Path.GetAbsolutePath(value, mWorkspace.mDir, absTestPath);
					else
						Path.GetFullPath(value, absTestPath);

					mWantsClean = true;
					mRunningTestScript = true;
					mScriptManager.SetTimeoutMS(20*60*1000); // 20 minute timeout
					mScriptManager.QueueCommandFile(absTestPath);
				case "-verbosity":
					if (value == "quiet")
					    mVerbosity = .Quiet;
					else if (value == "minimal")
					    mVerbosity = .Minimal;
					else if (value == "normal")
					    mVerbosity = .Normal;
					else if (value == "detailed")
					    mVerbosity = .Detailed;
					//else if (value == "diagnostic")
					    //mVerbosity = .Diagnostic;
				case "-workspace","-proddir":
					var relDir = scope String(value);
					if ((relDir.EndsWith("\\")) || relDir.EndsWith("\""))
					relDir.RemoveToEnd(relDir.Length - 1);
					IDEUtils.FixFilePath(relDir);

					String fullDir = new String();
					Path.GetFullPath(relDir, fullDir);

					if ((File.Exists(fullDir)) || (IsBeefFile(fullDir)))
					{
						mWorkspace.mCompositeFile = new CompositeFile(fullDir);
						delete fullDir;
					}
					else
						mWorkspace.mDir = fullDir;
				case "-open":
					String.NewOrSet!(mDeferredOpenFileName, value);
					if (mDeferredOpenFileName.EndsWith(".bfdbg", .OrdinalIgnoreCase))
						mDeferredOpen = .DebugSession;
					else if (mDeferredOpenFileName.EndsWith(".dmp", .OrdinalIgnoreCase))
						mDeferredOpen = .CrashDump;
					else
						mDeferredOpen = .File;
				default:
					return false;
				}
				return true;
			}
        }

        class Board : Widget
        {
            public override void Draw(Graphics g)
            {
                base.Draw(g);

                using (g.PushColor(0xFFFFFFFF))
                    g.FillRect(0, 0, 80, 80);
            }
        }

        public void Output(String outStr)
        {
#if CLI
			Console.Write(outStr);
			return;
#endif
#unwarn
            outStr.Replace("\r", "");
            mOutputPanel.Write(outStr);
        }

		public void OutputSmart(String outStr)
		{
#if CLI
			Console.Write(outStr);
			return;
#endif
#unwarn
			if (outStr.Contains('\r'))
		    {
				let newStr = scope String()..Append(outStr);
				newStr.Replace("\r", "");
				mOutputPanel.WriteSmart(newStr);
			}
			else
		    	mOutputPanel.WriteSmart(outStr);
		}

        public void Output(String format, params Object[] args)
        {
            String outStr = format;
            if (args.Count > 0)
			{
                outStr = scope:: String();
                outStr.AppendF(outStr, params args);
			}
			else
				outStr = scope:: String(format);
            outStr.Replace("\r", "");
            mOutputPanel.Write(outStr);
        }

        public void OutputLine(String format, params Object[] args)
        {
            String outStr;
            if (args.Count > 0)
			{
				outStr = scope:: String();
                outStr.AppendF(format, params args);
			}
			else
				outStr = scope:: String(format);
            outStr.Replace("\r", "");			

			if (mRunningTestScript)
				Console.WriteLine(outStr);

#if CLI
			Console.WriteLine(outStr);
#else
            outStr.Append("\n");
            mOutputPanel.Write(outStr);
#endif
        }

		public void OutputErrorLine(String format, params Object[] args)
		{
			var errStr = scope String();
			errStr.Append("ERROR: ", format);
			OutputLineSmart(errStr, params args);
		}

		public void OutputLineSmart(String format, params Object[] args)
		{
		    String outStr;
		    if (args.Count > 0)
			{
				outStr = scope:: String();
		        outStr.AppendF(format, params args);
			}
			else
				outStr = scope:: String(format);
		    outStr.Replace("\r", "");
#if CLI
			Console.WriteLine(outStr);
#else
			outStr.Append("\n");
			if (mOutputPanel != null)
		    	mOutputPanel.WriteSmart(outStr);
#endif
		}

        public void OutputFormatted(String str, bool isDbgEvalOutput = false)
        {
#if CLI
			Console.Write(str);
			return;
#endif

#unwarn
			String useStr = str;

            while (true)
            {
                if (mDebugger.GetRunState() != .NotStarted)
                {
                    int locPos = useStr.IndexOf("^loc:");
                    if (locPos != -1)
                    {
                        String addrSB = scope String();
                        int i = locPos + 5;
                        for (; i < useStr.Length; i++)
                        {
                            char8 c = useStr[i];
                            if ((c == 'x') || ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')))
                            {
                                addrSB.Append(c);
                            }
                            else if (c == '\'')
                            {
								// Allow
                            }
                            else
                                break;
                        }

                        int64 memoryAddress = int64.Parse(addrSB, .HexNumber);
                        String srcLocation = scope String();
                        mDebugger.GetAddressSourceLocation((int)memoryAddress, srcLocation);

						var newStr = scope:: String();
                        newStr.Append(useStr, 0, locPos);
                        newStr.Append(srcLocation);
                        newStr.Append(useStr, i);
						useStr = newStr;

                        continue;
                    }
                }

                break;
            }

			if ((isDbgEvalOutput) && (mIsImmediateDebugExprEval))
				mImmediatePanel.WriteResult(useStr);

            OutputSmart(useStr);
        }

		public void SetScale(float scale, bool force = false)
		{
			var prevScale = DarkTheme.sScale;
			float useScale = Math.Clamp(scale, 0.25f, 4.0f);
			if ((prevScale == useScale) && (!force))
				return;

			DarkTheme.SetScale(useScale);
			RehupScale();

			for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
			{
			    var window = mWindows[windowIdx];
			    var widgetWindow = window as WidgetWindow;
			    if (widgetWindow != null)
			    {
					widgetWindow.mRootWidget.RehupScale(prevScale, DarkTheme.sScale);

			        var darkDockingFrame = widgetWindow.mRootWidget as DarkDockingFrame;
			        if (widgetWindow == mMainWindow)
			            darkDockingFrame = mDockingFrame;

			        if (darkDockingFrame != null)
			        {
			            darkDockingFrame.WithAllDockedWidgets(scope (dockedWidget) =>
			                {
			                    var tabbedView = dockedWidget as DarkTabbedView;
			                    if (tabbedView != null)
			                    {
									tabbedView.WithTabs(scope (tab) =>
										{
											var panel = tab.mContent as Panel;
											if ((panel != null) && (panel.mWidgetWindow == null))
											{
												panel.RehupScale(prevScale, DarkTheme.sScale);
											}    
										});
								}    
			                });
			        }

					widgetWindow.mRootWidget.RehupSize();
			    }
				widgetWindow.mIsDirty = true;
			}
		}

        void SysKeyDown(KeyDownEvent evt)
        {
            var window = (WidgetWindow)evt.mSender;                     

			if (evt.mKeyCode == .Tab)
			{
				var activePanel = GetActivePanel() as Panel;
				if (activePanel != null)
				{
					if (activePanel.HandleTab(window.IsKeyDown(.Shift) ? -1 : 1))
						return;
				}
			}

			var keyState = scope KeyState();
			keyState.mKeyCode = evt.mKeyCode;
			keyState.mKeyFlags = evt.mKeyFlags;

			var curKeyMap = mCommands.mKeyMap;
			bool hadChordState = mKeyChordState != null;
			if (mKeyChordState != null)
				curKeyMap = mKeyChordState.mCommandMap;
			DeleteAndNullify!(mKeyChordState);

			KeyState matchedKey;
			IDECommandBase commandBase;
			if (curKeyMap.mMap.TryGetValue(keyState, out matchedKey, out commandBase))
			{
				if (var commandMap = commandBase as CommandMap)
				{
					mKeyChordState = new .();
					mKeyChordState.mCommandMap = commandMap;
					mKeyChordState.mKeyState = matchedKey;
					evt.mHandled = true;
					return;
				}
				else if (var command = commandBase as IDECommand)
				{
					IDECommand.ContextFlags useFlags = .None;
					var activePanel = GetActivePanel();
					if (activePanel is SourceViewPanel)
						useFlags |= .Editor;
					else if (activePanel is DisassemblyPanel)
						useFlags |= .Editor;

					bool foundMatch = false;
					if (useFlags != .None)
					{
						var checkCommand = command;
						while (checkCommand != null)
						{
							if (checkCommand.mContextFlags.HasFlag(useFlags))
							{
								checkCommand.mAction();
								foundMatch = true;
							}
							checkCommand = checkCommand.mNext;
						}
					}

					if (!foundMatch)
					{
						var checkCommand = command;
						while (checkCommand != null)
						{
							if (checkCommand.mContextFlags == .None)
							{
								checkCommand.mAction();
								foundMatch = true;
							}
							checkCommand = checkCommand.mNext;
						}
					}

					if (foundMatch)
					{
						evt.mHandled = true;
						return;
					}
				}
			}
			else
			{
				// Not found
				if (hadChordState)
				{
					Beep(.Error);
					evt.mHandled = true;
					return;
				}
			}

            SourceViewPanel sourceViewPanel = null;
            Widget focusWidget = window.mFocusWidget;
            
            if (focusWidget is EditWidget)
                focusWidget = focusWidget.mParent;
            if (focusWidget is SourceViewPanel)                
                sourceViewPanel = (SourceViewPanel)focusWidget;
			//if (focusWidget is DisassemblyPanel)
				//break;            

            if (evt.mKeyFlags == 0) // No ctrl/shift/alt
            {
                switch (evt.mKeyCode)
                {
                case KeyCode.F2:
                    if (sourceViewPanel != null)
                    {
                        mBookmarkManager.NextBookmark();
                    }
                    else if (focusWidget is ProjectPanel)
                    {
                        var projectPanel = (ProjectPanel)focusWidget;
                        projectPanel.TryRenameItem();
                    }
                    break;
                default:
					//OutputLine("Unknown key: {0}", (int)evt.mKeycode);
                    break;
                }
            }
        }
    
        void ShowOpenFileInSolutionDialog()
        {
            var widgetWindow = GetCurrentWindow();
            if (widgetWindow != null)
            {
                var openFileInSolutionDialog = new OpenFileInSolutionDialog();
                openFileInSolutionDialog.PopupWindow(mMainWindow);
            }
        }

		void ChangeCase(bool toUpper)
		{
			let sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel == null)
				return;

			var ewc = sourceViewPanel.mEditWidget.mEditWidgetContent;
			if (!ewc.HasSelection())
				return;

			/*ewc.mSelection.Value.GetAsForwardSelect(var startPos, var endPos);
			for (int i = startPos; i < endPos; i++)
			{
				var c = ref ewc.mData.mText[i].mChar;
				if (toUpper)
					c = c.ToUpper;
				else
					c = c.ToLower;
			}*/

			var prevSel = ewc.mSelection.Value;

			var str = scope String();
			ewc.GetSelectionText(str);

			var prevStr = scope String();
			prevStr.Append(str);

			if (toUpper)
				str.ToUpper();
			else
				str.ToLower();

			if (str == prevStr)
				return;

			ewc.InsertAtCursor(str);

			ewc.mSelection = prevSel;
		}

		public bool IsFilteredOut(String fileName)
		{
			if (fileName.Contains('~'))
				return true;
			if (fileName.EndsWith(".TMP", .OrdinalIgnoreCase))
				return true;
			return false;
		}

        public static bool IsBeefFile(String fileName)
        {
            return fileName.EndsWith(".cs", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".bf", StringComparison.OrdinalIgnoreCase);
        }

        public static bool IsClangSourceFile(String fileName)
        {
#if IDE_C_SUPPORT
            return fileName.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".c", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".cc", StringComparison.OrdinalIgnoreCase);
#else
			return false;
#endif			
        }

		public static bool IsSourceCode(String fileName)
		{
			return fileName.EndsWith(".cs", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".bf", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".c", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".cc", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
		}

        public static bool IsClangSourceHeader(String fileName)
        {
            return fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
        }

        public void GetBeefPreprocessorMacros(List<String> macroList)
        {
            var workspaceOptions = GetCurWorkspaceOptions();            
            if (workspaceOptions.mRuntimeChecks)
                macroList.Add("BF_RUNTIME_CHECKS");
            if (workspaceOptions.mEnableObjectDebugFlags)
                macroList.Add("BF_ENABLE_OBJECT_DEBUG_FLAGS");
			if (workspaceOptions.mAllowHotSwapping)
				macroList.Add("BF_ALLOW_HOT_SWAPPING");

			bool is64Bits = workspaceOptions.mMachineType.PtrSize == 8;

			if (is64Bits)
			{
				if (workspaceOptions.mLargeStrings)
					macroList.Add("BF_LARGE_STRINGS");
				if (workspaceOptions.mLargeCollections)
					macroList.Add("BF_LARGE_COLLECTIONS");
			}

			// Only supported on Windows at the moment
			bool hasLeakCheck = false;
#if BF_PLATFORM_WINDOWS
            if (workspaceOptions.mEnableRealtimeLeakCheck && workspaceOptions.mEnableObjectDebugFlags)
			{
				hasLeakCheck = true;
                macroList.Add("BF_ENABLE_REALTIME_LEAK_CHECK");
			}
#endif
			if ((workspaceOptions.mAllocType == .Debug) || (hasLeakCheck))
				macroList.Add("BF_DEBUG_ALLOC");

			if (workspaceOptions.mEmitDynamicCastCheck)
				macroList.Add("BF_DYNAMIC_CAST_CHECK");

			if (workspaceOptions.mBuildKind == .Test)
				macroList.Add("BF_TEST_BUILD");

			macroList.Add("BF_LITTLE_ENDIAN");

			if (is64Bits)
				macroList.Add("BF_64_BIT");
			else
				macroList.Add("BF_32_BIT");

			//if (workspaceOptions.mE)
			macroList.Add("BF_HAS_VDATA_EXTENDER");			
        }

        public void GetClangResolveArgs(Project project, List<String> outResolveArgs)
        {            
            using (project.mMonitor.Enter())
            {
				//List<string> argsList = new List<string>();
                var options = GetCurProjectOptions(project);

                if (options == null)
                    return;

                outResolveArgs.Add(new String("-std=c++11"));

                if (options.mCOptions.mEnableBeefInterop)
                {
					var beefPreprocMacros = scope List<String>();
                    GetBeefPreprocessorMacros(beefPreprocMacros);
                    for (var beefPreprocMacro in beefPreprocMacros)
					{
                        outResolveArgs.Add(new String("-D", beefPreprocMacro));
					}
                }

                for (var preprocMacro in options.mCOptions.mPreprocessorMacros)
                    outResolveArgs.Add(new String("-D", preprocMacro));
                for (var includePath in options.mCOptions.mIncludePaths)
				{
                    //outResolveArgs.Add(new String("-I", includePath));
					var fullIncludePath = scope String();
					project.GetProjectFullPath(includePath, fullIncludePath);					
					outResolveArgs.Add(new String("-I", fullIncludePath));
				}

                String llvmDir = scope String(IDEApp.sApp.mInstallDir);
                IDEUtils.FixFilePath(llvmDir);
                llvmDir.Append("llvm/");
                outResolveArgs.Add(new String("-I", llvmDir, "lib/gcc/i686-w64-mingw32/5.2.0/include"));
                outResolveArgs.Add(new String("-I", llvmDir, "lib/gcc/i686-w64-mingw32/5.2.0/include-fixed"));
                outResolveArgs.Add(new String("-I", llvmDir, "i686-w64-mingw32/include"));
                outResolveArgs.Add(new String("-I", llvmDir, "i686-w64-mingw32/include/c++"));
                outResolveArgs.Add(new String("-I", llvmDir, "i686-w64-mingw32/include/c++/i686-w64-mingw32"));
                outResolveArgs.Add(new String("-I", llvmDir, "i686-w64-mingw32/include/c++/backward"));
                outResolveArgs.Add(new String("-I", llvmDir, "bin/../lib/clang/3.7.0/include"));
                
                outResolveArgs.Add(new String("-I", llvmDir, "i686-w64-mingw32/include"));

				outResolveArgs.Add(new String("-cc1"));
				outResolveArgs.Add(new String("-std=c++11"));
				outResolveArgs.Add(new String("-x"));
				outResolveArgs.Add(new String("c++"));
				/*outResolveArgs.Add(new String("-triple"));
				outResolveArgs.Add(new String("x86_64-pc-windows-gnu"));
				outResolveArgs.Add(new String("-target-cpu"));
				outResolveArgs.Add(new String("x86-64"));
				outResolveArgs.Add(new String("-target-feature"));
				outResolveArgs.Add(new String("+sse2"));*/
            }
        }

        public CompilerBase GetProjectCompilerForFile(String fileName)
        {            
            if (IsBeefFile(fileName))
                return mBfResolveCompiler;
#if IDE_C_SUPPORT
            if (IsClangSourceFile(fileName))
                return mDepClang;
#endif
            return null;
        }

        public CompilerBase GetResolveCompilerForFile(String fileName)
        {            
            if (IsBeefFile(fileName))
                return mBfResolveCompiler;
#if IDE_C_SUPPORT
            if (IsClangSourceFile(fileName))
                return mResolveClang;
#endif
            return null;
        }

        public void GetClangFiles(ProjectFolder projectFolder, List<ProjectSource> clangFiles)
        {            
            for (var item in projectFolder.mChildItems)
            {
                if (item is ProjectSource)
                {
                    var projectSource = (ProjectSource)item;

                    if (!IsClangSourceFile(projectSource.mPath))
                        continue;

                    clangFiles.Add(projectSource);                    
                }

                if (item is ProjectFolder)
                {
                    var innerProjectFolder = (ProjectFolder)item;
                    GetClangFiles(innerProjectFolder, clangFiles);
                }
            }            
        }

		/*void StopExecutionInstance()
		{
			if (mExecutionInstance != null)
			{
				try
				{
					mExecutionInstance.mProcess.Kill();
				}
				catch (Exception)
				{
				}
			}
		}*/

		const int cArgFileThreshold = 0x7800;

        public ExecutionQueueCmd QueueRun(String fileName, String args, String workingDir, ArgsFileKind argsFileKind = .None)
        {            
            var executionQueueCmd = new ExecutionQueueCmd();
            executionQueueCmd.mFileName = new String(fileName);
            executionQueueCmd.mArgs = new String(args);
            executionQueueCmd.mWorkingDir = new String(workingDir);
			if (args.Length > cArgFileThreshold)
			{
				// Only use UTF16 if we absolutely need to
				if ((argsFileKind == .UTF16WithBom) && (!args.HasMultibyteChars()))
					executionQueueCmd.mUseArgsFile = .UTF8;
				else
                	executionQueueCmd.mUseArgsFile = argsFileKind;
			}
            mExecutionQueue.Add(executionQueueCmd);
            return executionQueueCmd;
        }

        void GotExecutionOutputLine(ExecutionInstance executionInstance, String str)
        {
            if (!String.IsNullOrEmpty(str))
            {                
                using (mMonitor.Enter())
                {
                    executionInstance.mDeferredOutput.Add(new String(str));
                }
            }
        }

		static void ReadOutputThread(Object obj)
		{
			ExecutionInstance executionInstance = (ExecutionInstance)obj;

			FileStream fileStream = scope FileStream();
			if (executionInstance.mProcess.AttachStandardOutput(fileStream) case .Err)
				return;
			StreamReader streamReader = scope StreamReader(fileStream, null, false, 4096);

			while (true)
			{
				var buffer = scope String();
				if (streamReader.ReadLine(buffer) case .Err)
					break;				
				using (IDEApp.sApp.mMonitor.Enter())				
				    executionInstance.mDeferredOutput.Add(new String(buffer));				
			}
		}

		static void ReadErrorThread(Object obj)
		{
			ExecutionInstance executionInstance = (ExecutionInstance)obj;

			FileStream fileStream = scope FileStream();
			if (executionInstance.mProcess.AttachStandardError(fileStream) case .Err)
				return;
			StreamReader streamReader = scope StreamReader(fileStream, null, false, 4096);

			while (true)
			{
				var buffer = scope String();
				if (streamReader.ReadLine(buffer) case .Err)
					break;				

				using (IDEApp.sApp.mMonitor.Enter())				
				    executionInstance.mDeferredOutput.Add(new String(buffer));				
			}
		}

        public ExecutionInstance DoRun(String inFileName, String args, String workingDir, ArgsFileKind useArgsFile, Dictionary<String, String> envVars = null)
        {
			//Debug.Assert(executionInstance == null);

			String fileName = scope String(inFileName ?? "");
			QuoteIfNeeded(fileName);

            ProcessStartInfo startInfo = scope ProcessStartInfo();
            startInfo.UseShellExecute = false;
			if (!fileName.IsEmpty)
            	startInfo.SetFileName(fileName);
            startInfo.SetWorkingDirectory(workingDir);
            startInfo.SetArguments(args);
            startInfo.RedirectStandardOutput = true;
            startInfo.RedirectStandardError = true;
            startInfo.CreateNoWindow = true;
			if (envVars != null)
			{
				for (var envKV in envVars)
					startInfo.AddEnvironmentVariable(envKV.key, envKV.value);
			}

            var executionInstance = new ExecutionInstance();
			mExecutionInstances.Add(executionInstance);

            if (useArgsFile != .None)
            {
                String tempFileName = scope String();
                Path.GetTempFileName(tempFileName);

				Encoding encoding = Encoding.UTF8;
				if (useArgsFile == .UTF16WithBom)
					encoding = Encoding.UTF16WithBOM;

                var result = File.WriteAllText(tempFileName, args, encoding);
				if (result case .Err)
					OutputLine("Failed to create temporary param file");
                String arguments = scope String();
				arguments.Concat("@", tempFileName);
				startInfo.SetArguments(arguments);
				
                executionInstance.mTempFileName = new String(tempFileName);
            }

			if (mVerbosity >= .Detailed)
			{
				String showArgs = startInfo.mArguments;
				if ((mRunningTestScript) && (showArgs.Length > 1024))
				{
					showArgs = scope:: String(showArgs, 0, 1024);
					showArgs.Append("...");
				}

				if (!startInfo.mFileName.IsEmpty)
                	OutputLine("Executing: {0} {1}", startInfo.mFileName, showArgs);
				else
					OutputLine("Executing: {0}", showArgs);
			}

			//if (useArgsFile)
            {
                //if (mVerbosity >= .Detailed)
                    //OutputLine(" Args: {0}", args);
            }
            
			/*var processResult = Process.Start(startInfo);
			if (case .Err = processResult)
			{
				OutputLine("Failed to execute \"{0}\"", fileName);
				return executionInstance;
			}*/

            SpawnedProcess process = new SpawnedProcess();
			if (process.Start(startInfo) case .Err)
			{
				OutputLine("Failed to execute \"{0}\"", inFileName);
				delete process;
				return executionInstance;
			}

            executionInstance.mStopwatch.Start();
            executionInstance.mProcess = process;

			executionInstance.mOutputThread = new Thread(new => ReadOutputThread);
			executionInstance.mOutputThread.Start(executionInstance, false);

			executionInstance.mErrorThread = new Thread(new => ReadErrorThread);
			executionInstance.mErrorThread.Start(executionInstance, false);

            return executionInstance;
        }

		protected virtual void BeefCompileStarted()
		{

		}

		protected virtual void BeefCompileDone()
		{

		}

		protected virtual void CompileDone(bool succeeded)
		{

		}

		BuildCompletedCmd GetBuildCompletedCmd()
		{
			for (int idx = mExecutionQueue.Count - 1; idx >= 0; idx--)
			{
				if (var buildCompletedCmd = mExecutionQueue[idx] as BuildCompletedCmd)
					return buildCompletedCmd;
			}
			return null;
		}

        void UpdateExecution()
        {
            if (mExecutionInstances.Count > 0)
            {
                var executionInstance = mExecutionInstances[0];
                bool failed = false;
				bool isDone = false;

				using (mMonitor.Enter())
				{
				    for (var str in executionInstance.mDeferredOutput)
					{
				        OutputLine(str);
						delete str;
					}
				    executionInstance.mDeferredOutput.Clear();
				}

				if (executionInstance.mProcess == null)
				{
					// Didn't even get off the ground
					isDone = true;
					failed = true;
					executionInstance.mExitCode = -1;
				}
                else if (executionInstance.mProcess.HasExited)
                {
					isDone = true;

					if (executionInstance.mOutputThread != null)
					{
						if (!executionInstance.mOutputThread.Join(0))
							isDone = false;
					}

					if (executionInstance.mErrorThread != null)
					{
						if (!executionInstance.mErrorThread.Join(0))
							isDone = false;
					}

					if (isDone)
					{
						executionInstance.mExitCode = executionInstance.mProcess.ExitCode;
	                    if (executionInstance.mProcess.ExitCode != 0)
	                        failed = true;                                            
	                    executionInstance.mProcess.WaitFor(-1);
	                    executionInstance.mProcess.Close();

	                    executionInstance.mStopwatch.Stop();
	                    if (executionInstance.mParallelGroup == -1)
	                    {
							if (mVerbosity >= .Detailed)
	                        	OutputLine("Execution time: {0:0.00}s", executionInstance.mStopwatch.ElapsedMilliseconds / 1000.0f);
						}

						if (executionInstance.mCanceled)
							OutputLine("Execution Canceled");
						else if (failed)
							OutputLine("Execution Failed");

	                    if (executionInstance.mTempFileName != null)
	                    {
	                        File.Delete(executionInstance.mTempFileName).IgnoreError();
	                    }
					}
                }

                if (failed)
                {
                    if (mExecutionQueue.Count > 0)
                    {
                        var buildCompletedCmd = GetBuildCompletedCmd();
                        if (buildCompletedCmd != null)
                            buildCompletedCmd.mFailed = true;
                    }
                }

				if (isDone)
				{
					mExecutionInstances.RemoveAt(0);
					if (executionInstance.mAutoDelete)
						delete executionInstance;
				}

				/*if (failed)
				{
					OutputLine("COMPILATION FAILED");
					mExecutionQueue.Clear();
				}

				if ((executionInstance == null) && (mExecutionQueue.Count == 0))
				{                    
					OutputLine("Compilation finished.");
				}*/
            }

			bool buildFailed = false;
			if ((mBuildContext != null) && (mBuildContext.Failed))
				buildFailed = true;
			let buildCompleteCmd = GetBuildCompletedCmd();
			if ((buildCompleteCmd != null) && (buildCompleteCmd.mFailed))
				buildFailed = true;

            bool canExecuteNext = false;
            int32 parallelExecutionCount = 16;
            if ((mExecutionQueue.Count > 0) && (mExecutionInstances.Count < parallelExecutionCount))
            {
                var executionQueueCmd = mExecutionQueue[0] as ExecutionQueueCmd;
                if (executionQueueCmd != null)
                {
                    if (mExecutionInstances.Count > 0)
                    {
                        var executionInstance = mExecutionInstances[0];
                        if ((executionQueueCmd.mParallelGroup != -1) &&
                            (executionQueueCmd.mParallelGroup == executionInstance.mParallelGroup))
                            canExecuteNext = true;
                    }
                    else
                        canExecuteNext = true;
                }
                else if (mExecutionInstances.Count == 0)
                    canExecuteNext = true;
            }

            if (canExecuteNext)
            {
                var next = mExecutionQueue[0];

				bool waitForBuildClang = false;
#if IDE_C_SUPPORT
				waitForBuildClang = (mDepClang.mCompileWaitsForQueueEmpty) && (mDepClang.HasQueuedCommands());
#endif
                if ((next is ProcessBfCompileCmd) && (mBfBuildCompiler.HasQueuedCommands() || (waitForBuildClang)))
                    return;

				/*if (next is BuildCompletedCmd)
				{
					if (mBuildContext != null)
						return;
				}*/
				if (let targetCompletedCmd = next as TargetCompletedCmd)
				{
					if ((mBuildContext == null) || (!mBuildContext.Failed))
					{
						if (!targetCompletedCmd.mIsReady)
							return;
					}
				}

				defer delete next;
                mExecutionQueue.RemoveAt(0);

                bool ignoreCommand = false;
                if (next.mOnlyIfNotFailed)
                {
                    if (mExecutionQueue.Count > 0)
                    {
                        var buildCompletedCmd = GetBuildCompletedCmd();
                        if ((buildCompletedCmd != null) && (buildCompletedCmd.mFailed))
                            ignoreCommand = true;
                    }
                }

                if (ignoreCommand)
                {
					// Nothing
                }
                else if (next is ProcessBfCompileCmd)
                {
                    var processCompileCmd = (ProcessBfCompileCmd)next;

					mCompilingBeef = false;
					BeefCompileDone();

					//processCompileCmd.mStopwatch.Stop();
					if ((processCompileCmd.mHadBeef) && (mVerbosity >= .Normal))
                    	OutputLine("Beef compilation time: {0:0.00}s", processCompileCmd.mStopwatch.ElapsedMilliseconds / 1000.0f);

					var compileInstance = mWorkspace.mCompileInstanceList.Back;
					compileInstance.mCompileResult = .Failure;
                    if (processCompileCmd.mBfPassInstance.mCompileSucceeded)
                    {
						//foreach (var sourceViewPanel in GetSourceViewPanels())
                        WithSourceViewPanels(scope (sourceViewPanel) =>
                            {
                                sourceViewPanel.mHasChangedSinceLastCompile = false;
                            });
                        compileInstance.mCompileResult = .Success;
                    }
					else
					{
						compileInstance.mCompileResult = .Failure;
					}

                    ProcessBeefCompileResults(processCompileCmd.mBfPassInstance, processCompileCmd.mRunAfter, processCompileCmd.mHotProject, processCompileCmd.mStopwatch);
					if (mHotResolveState != .None)
					{
						if (compileInstance.mCompileResult == .Success)
							compileInstance.mCompileResult = .PendingHotLoad;
					}

					if (processCompileCmd.mProfileCmd != null)
					{
						mExecutionQueue.Add(processCompileCmd.mProfileCmd);
						processCompileCmd.mProfileCmd = null;
					}

					delete processCompileCmd.mStopwatch;
					processCompileCmd.mStopwatch = null;
                }
                else if (next is TargetCompletedCmd)
                {
					if (!buildFailed)
					{
	                    var targetCompletedCmd = (TargetCompletedCmd)next;
	                    targetCompletedCmd.mProject.mNeedsTargetRebuild = false;
						targetCompletedCmd.mProject.mForceCustomCommands = false;
					}
                }
                else if (next is StartDebugCmd)
                {
					if (!buildFailed)
					{
						var startDebugCmd = (StartDebugCmd)next;
	                    if (DebugProject(startDebugCmd.mWasCompiled))
	                    {
	                        OutputLine("Debugger started");
	                    }
	                    else
	                        OutputLine("Failed to start debugger");
					}
                }
                else if (next is ExecutionQueueCmd)
                {
                    var executionQueueCmd = (ExecutionQueueCmd)next;
                    var executionInstance = DoRun(executionQueueCmd.mFileName, executionQueueCmd.mArgs, executionQueueCmd.mWorkingDir, executionQueueCmd.mUseArgsFile, executionQueueCmd.mEnvVars);
                    executionInstance.mParallelGroup = executionQueueCmd.mParallelGroup;
                }
                else if (next is BuildCompletedCmd)
                {
                    var buildCompletedCmd = (BuildCompletedCmd)next;
#if IDE_C_SUPPORT
                    mWorkspace.WithProjectItems(scope (projectItem) =>
                        {
                            var projectSource = projectItem as ProjectSource;
                            if (projectItem != null)
                            {
								String fullPath = scope String();
								projectSource.GetFullImportPath(fullPath);
                                if (completedCompileCmd.mClangCompiledFiles.Contains(fullPath))
                                {
                                    mDepClang.QueueCheckDependencies(projectSource, ClangCompiler.DepCheckerType.Clang);
                                }
                            }                            
                        });
                    if (!completedCompileCmd.mFailed)
                        mDepClang.mDoDependencyCheck = false;
#endif
					if (buildFailed)
						buildCompletedCmd.mFailed = true;
					
					CompileResult(buildCompletedCmd.mHotProjectName, !buildCompletedCmd.mFailed);

                    if (buildCompletedCmd.mFailed)
                        OutputLineSmart("ERROR: BUILD FAILED");
					if ((mVerbosity >= .Detailed) && (buildCompletedCmd.mStopwatch != null))
                    	OutputLine("Total build time: {0:0.00}s", buildCompletedCmd.mStopwatch.ElapsedMilliseconds / 1000.0f);

					CompileDone(!buildCompletedCmd.mFailed);

					if (mTestManager != null)
					{
						if (buildCompletedCmd.mFailed)
							mTestManager.BuildFailed();
						else
							mTestManager.Start();
					}
                }
				else if (var profileCmd = next as ProfileCmd)
				{
					if (gApp.mDebugger.mIsRunning)
						mProfilePanel.StartProfiling(profileCmd.mThreadId, profileCmd.mDesc, profileCmd.mSampleRate);
				}
                else
                {
                    Runtime.FatalError("Unknown command");
                }
            }
        }

        public bool ParseSourceFile(BfSystem bfSystem, BfPassInstance passInstance, ProjectSource projectSource)
        {
            bool worked = true;

            if (!IsBeefFile(projectSource.mPath))
                return true;

			var fullPath = scope String();
			projectSource.GetFullImportPath(fullPath);
            String data = scope String();
            LoadTextFile(fullPath, data);
            if (data == null)
            {
                OutputErrorLine("FAILED TO LOAD FILE: {0}", fullPath);
                return true;
            }

            mFileWatcher.FileIsValid(fullPath);

            var bfParser = bfSystem.CreateParser(projectSource);
            bfParser.SetSource(data, fullPath);
            worked &= bfParser.Parse(passInstance, false);
            worked &= bfParser.Reduce(passInstance);
            worked &= bfParser.BuildDefs(passInstance, null, false);
            return worked;
        }

        public bool FindProjectSourceContent(ProjectSource projectSource, out IdSpan char8IdData, bool loadOnFail, String sourceContent)
        {
            char8IdData = IdSpan();
            var fullPath = scope String();
			projectSource.GetFullImportPath(fullPath);

			//SourceViewPanel sourceViewPanel = null;
            
            using (mMonitor.Enter())
            {
                if (projectSource.mEditData != null)
                {
					if (projectSource.mEditData.IsFileDeleted())
					{
						return false;
					}
					if (projectSource.mEditData.mEditWidget != null)
					{
						var idData = ref projectSource.mEditData.mEditWidget.Content.mData.mTextIdData;
						idData.Prepare();
	                    if (!projectSource.mEditData.mSavedCharIdData.Equals(idData))
	                    {
	                        char8IdData = projectSource.mEditData.mEditWidget.mEditWidgetContent.mData.mTextIdData.Duplicate();
	                        projectSource.mEditData.mEditWidget.GetText(sourceContent);
							return true;
	                    }
					}

					if (projectSource.mEditData.mSavedContent != null)
					{
					    char8IdData = projectSource.mEditData.mSavedCharIdData.Duplicate();
					    sourceContent.Set(projectSource.mEditData.mSavedContent);
						return true;
					}
                }
            }

            if (loadOnFail)
            {
                String text = scope String();
				bool isValid = false;
                if (LoadTextFile(fullPath, text) case .Ok)
				{	
                    mFileWatcher.FileIsValid(fullPath);
					isValid = true;
				}
				
                char8IdData = IdSpan.GetDefault((int32)text.Length);
				var editData = GetEditData(projectSource, false);
				using (mMonitor.Enter())
                {
					if (isValid)
					{
						editData.SetSavedData(text, char8IdData);
						sourceContent.Set(text);
					}
                    else
					{
						editData.SetSavedData(null, IdSpan());
						editData.mFileDeleted = true;
					}
				}
				return isValid;
            }

            return false;
        }

        public void SetInDisassemblyView(bool inDisassemblyView)
        {
			if (mShuttingDown)
				return;

            if (mInDisassemblyView != inDisassemblyView)
            {
				// We need to refresh auto watch panel so we can handle displaying registers when switching to disasm
                mAutoWatchPanel.MarkWatchesDirty(false);
                mInDisassemblyView = inDisassemblyView;
            }            
        }

        public bool ParseSourceFiles(BfSystem bfSystem, BfPassInstance passInstance, ProjectFolder projectFolder)
        {
            bool worked = true;

            for (var item in projectFolder.mChildItems)
            {
                if (item is ProjectSource)
                {
                    var projectSource = (ProjectSource)item;                    
                    worked &= ParseSourceFile(bfSystem, passInstance, projectSource);
                }

                if (item is ProjectFolder)
                {
                    var innerProjectFolder = (ProjectFolder)item;
                    worked &= ParseSourceFiles(bfSystem, passInstance, innerProjectFolder);
                }
            }
            return worked;
        }

		public bool HasPendingBeefFiles(bool forceQueue, ProjectFolder projectFolder)
		{
			bool hadBeef = false;       
			  
			for (var item in projectFolder.mChildItems)
			{
				if (item.mIncludeKind == .Ignore)
					continue;

			    if (item is ProjectSource)
			    {
			        var projectSource = (ProjectSource)item;
			        if (IsBeefFile(projectSource.mPath))
			        {
						if ((projectSource.HasChangedSinceLastCompile) || (forceQueue))
						{
							hadBeef = true;
						}
			        }
			    }

			    if (item is ProjectFolder)
			    {
			        var innerProjectFolder = (ProjectFolder)item;
			        hadBeef |= HasPendingBeefFiles(forceQueue, innerProjectFolder);
			    }
			}

			return hadBeef;
		}

        public bool QueueParseBeefFiles(BfCompiler bfCompiler, bool forceQueue, ProjectFolder projectFolder)
        {
            bool hadBeef = false;       
              
            for (var item in projectFolder.mChildItems)
            {
				if (item.mIncludeKind == .Ignore)
					continue;

                if (item is ProjectSource)
                {
                    var projectSource = (ProjectSource)item;
                    if (IsBeefFile(projectSource.mPath))
                    {
						// if it's the resolve compiler then we take this as an indication that we need to recompile this file
						//  later on when a build is requested, most likely because the project or workspace configuration has
						//  changed (such as preprocessor or other setting changes)
						if ((bfCompiler == null) || (bfCompiler.mIsResolveOnly))
						{
							projectSource.HasChangedSinceLastCompile = true;
						}
						else // Actual build
						{
							if ((projectSource.HasChangedSinceLastCompile) || (forceQueue))
							{
								// mHasChangedSinceLastCompile is safe to set 'false' here since it just determines whether or not
								//  we rebuild the TypeDefs from the sources.  It isn't affected by any compilation errors.
								projectSource.mHasChangedSinceLastCompile = false;
		                        bfCompiler.QueueProjectSource(projectSource);
								hadBeef = true;
							}
						}
                    }
                }

                if (item is ProjectFolder)
                {
                    var innerProjectFolder = (ProjectFolder)item;
                    hadBeef |= QueueParseBeefFiles(bfCompiler, forceQueue, innerProjectFolder);
                }
            }

            return hadBeef;
        }

#if IDE_C_SUPPORT
        public void QueueParseClangFiles(ClangCompiler clangCompiler, ProjectFolder projectFolder)
        {
            for (var item in projectFolder.mChildItems)
            {
                if (item is ProjectSource)
                {
                    var projectSource = (ProjectSource)item;
                    if (IsClangSourceFile(projectSource.mPath))
                        clangCompiler.QueueProjectSource(projectSource);
                }

                if (item is ProjectFolder)
                {
                    var innerProjectFolder = (ProjectFolder)item;
                    QueueParseClangFiles(clangCompiler, innerProjectFolder);
                }
            }
        }
#endif

        public Workspace.Options GetCurWorkspaceOptions()
        {            
            return mWorkspace.GetOptions(mConfigName, mPlatformName);
        }

		public Workspace.ConfigSelection GetCurConfigSelection(Project project)
		{
			var workspaceOptions = mWorkspace.GetOptions(mConfigName, mPlatformName);
			if (workspaceOptions == null)
			    return null;

			Workspace.ConfigSelection configSelection;
			workspaceOptions.mConfigSelections.TryGetValue(project, out configSelection);
			if ((configSelection == null) || (!configSelection.mEnabled))
			    return null;

			return configSelection;
		}

        public Project.Options GetCurProjectOptions(Project project)
        {
			if (project.mFailed)
				return null;
            
            Workspace.ConfigSelection configSelection = GetCurConfigSelection(project);
            if (configSelection == null)
                return null;
            return project.GetOptions(configSelection.mConfig, configSelection.mPlatform);
        }

		public bool IsProjectEnabled(Project project)
		{
			return GetCurProjectOptions(project) != null;
		}

		public bool IsProjectSourceEnabled(ProjectSource projectSource)
		{
			if (projectSource.mIncludeKind == .Ignore)
				return false;
			if (!IsProjectEnabled(projectSource.mProject))
				return false;
			return true;
		}

		public void PreConfigureBeefSystem(BfSystem bfSystem, BfCompiler bfCompiler)
		{
			if (!bfCompiler.mIsResolveOnly)
			{
				bfCompiler.WaitForBackground();
				bfSystem.ClearTypeOptions();
			}
		}

		// Project options are inherently thread safe.  Resolve-system project settings
		//  Can only be changed from the Resolve BfCompiler thread, and Build settings 
		//  are only changed before background compilation begins.  We also call this 
		//  during WorkspaceLoad, but the resolve threads aren't processing then.
        public bool SetupBeefProjectSettings(BfSystem bfSystem, BfCompiler bfCompiler, Project project)
        {
            bool success = true;
            var bfProject = bfSystem.GetBfProject(project);
            Project.Options options = GetCurProjectOptions(project);
            Workspace.Options workspaceOptions = GetCurWorkspaceOptions();
            if (options == null)
            {
				//Fail(StackStringFormat!("Failed to retrieve options for {0}", project.mProjectName));
                bfProject.SetDisabled(true);
                return false;
            }

            bfProject.SetDisabled(false);

            var preprocessorMacros = scope List<String>();
			void AddMacros(List<String> macros)
			{
				for (var macro in macros)
				{
					if (macro.StartsWith("!"))
					{
						let removeStr = scope String(macro, 1);
						preprocessorMacros.Remove(removeStr);
						continue;
					}
					preprocessorMacros.Add(macro);
				}
			}

			AddMacros(project.mBeefGlobalOptions.mPreprocessorMacros);
			AddMacros(options.mBeefOptions.mPreprocessorMacros);
			AddMacros(mWorkspace.mBeefGlobalOptions.mPreprocessorMacros);
			AddMacros(workspaceOptions.mPreprocessorMacros);
			GetBeefPreprocessorMacros(preprocessorMacros);
            
			var optimizationLevel = workspaceOptions.mBfOptimizationLevel;
            if (options.mBeefOptions.mOptimizationLevel != null)
                optimizationLevel = options.mBeefOptions.mOptimizationLevel.Value;

			if ((optimizationLevel == .OgPlus) && (mPlatformName != "Win64") && (bfCompiler == mBfBuildCompiler))
			{
				OutputLineSmart("WARNING: Project '{0}' has Og+ specified, which is only supported for Win64 targets.", project.mProjectName);
				optimizationLevel = .O0;
			}

			var ltoType = workspaceOptions.mLTOType;
			if (options.mBeefOptions.mLTOType != null)
				ltoType = options.mBeefOptions.mLTOType.Value;
			
			var targetType = project.mGeneralOptions.mTargetType;

			if (bfSystem != mBfResolveSystem)
			{
				if (options.mBuildOptions.mBuildKind == .Test)
				{
					if (workspaceOptions.mBuildKind == .Test)
					{
						targetType = .BeefTest;
						if (mTestManager != null)
							mTestManager.AddProject(project);
					}
					else
					{
						OutputLineSmart("ERROR: Project '{0}' has a Test configuration specified but the workspace is not using a Test configuration", project.mProjectName);
						success = false;
					}
				}
			}

            bfProject.SetOptions(targetType,
                project.mBeefGlobalOptions.mStartupObject,
                preprocessorMacros,
                optimizationLevel, ltoType, options.mBeefOptions.mMergeFunctions, options.mBeefOptions.mCombineLoads,
                options.mBeefOptions.mVectorizeLoops, options.mBeefOptions.mVectorizeSLP);

            List<Project> depProjectList = scope List<Project>();
            if (!GetDependentProjectList(project, depProjectList))
                success = false;
            bfProject.ClearDependencies();
            for (var depProject in depProjectList)
            {
                var depBfProject = bfSystem.mProjectMap[depProject];
                bfProject.AddDependency(depBfProject);
            }

			if (!bfCompiler.mIsResolveOnly)
			{
				for (let typeOption in project.mBeefGlobalOptions.mDistinctBuildOptions)
					bfSystem.AddTypeOptions(typeOption);
				for (let typeOption in options.mBeefOptions.mDistinctBuildOptions)
					bfSystem.AddTypeOptions(typeOption);
			}

            return success;
        }

        public void CurrentWorkspaceConfigChanged()
        {
#if IDE_C_SUPPORT
			for (var val in mDepClang.mProjectBuildString.Values)
				delete val;
			mDepClang.mProjectBuildString.Clear();
#endif

			if (mBfResolveCompiler != null)
			{
	            mBfResolveCompiler.QueueSetWorkspaceOptions(null, 0);
	            mBfResolveCompiler.QueueDeferredResolveAll();
			}

            mWorkspace.FixOptions(mConfigName, mPlatformName); 
            for (var project in mWorkspace.mProjects)
            {               
                ProjectOptionsChanged(project);
#if IDE_C_SUPPORT
                QueueParseClangFiles(mDepClang, project.mRootFolder);
#endif
            }
			mWorkspace.ClearProjectNameCache();
            mProjectPanel.RehupProjects();
        }

		/*public string GetClangDepConfigName(Project project)
		{
			string[] clangArgs = GetClangResolveArgs(project);
			string clangArgsStr = String.Join("\n", clangArgs);

			long hash = 0;
			for (int i = 0; i < clangArgsStr.Length; i++)            
				hash = (hash << 5) - hash + clangArgsStr[i];
			return String.Format("{0:X16}", hash);
		}*/

        public void ProjectOptionsChanged(Project project, bool reparseFiles = true)
        {
			bool isEnabled = IsProjectEnabled(project);
			mWorkspace.ClearProjectNameCache();

			if (mBfResolveCompiler != null)
			{
			    mBfResolveCompiler.QueueSetupProjectSettings(project);
			}

			if (isEnabled != project.mEnabled)
			{
				project.mEnabled = isEnabled;

				if (isEnabled)
				{
					QueueProjectItems(project);
				}
				else
				{
					RemoveProjectItems(project);
				}

				mBfResolveCompiler.QueueDeferredResolveAll();
				mBfResolveCompiler.QueueRefreshViewCommand();
				return;
			}

#if IDE_C_SUPPORT
            mDepClang.mDoDependencyCheck = true;
#endif
			if (mBfResolveCompiler != null)
			{
	            if (reparseFiles)
	                QueueParseBeefFiles(mBfResolveCompiler, false, project.mRootFolder);
	            mBfResolveCompiler.QueueDeferredResolveAll();
	            mBfResolveCompiler.QueueRefreshViewCommand();
			}
			else
			{
				if (reparseFiles)
					QueueParseBeefFiles(mBfResolveCompiler, false, project.mRootFolder);
			}
        }

		public void RenameProject(Project project, String newName)
		{
			mWantsBeefClean = true;

			for (var checkProject in mWorkspace.mProjects)
			{
				for (var dep in checkProject.mDependencies)
				{
					if (dep.mProjectName == project.mProjectName)
					{
						dep.mProjectName.Set(newName);
						checkProject.SetChanged();
					}
				}
			}

			for (var dep in mWorkspace.mProjectSpecs)
			{
				if (dep.mProjectName == project.mProjectName)
				{
					dep.mProjectName.Set(newName);
					mWorkspace.SetChanged();
				}
			}

			project.mProjectName.Set(newName);
			project.SetChanged();
			mWorkspace.ClearProjectNameCache();
		}

        public void RemoveProject(Project project)
        {
			RemoveProjectItems(project);

            mWorkspace.SetChanged();
            mWorkspace.mProjects.Remove(project);
#if IDE_C_SUPPORT
            using (mDepClang.mMonitor.Enter())
            {
                mDepClang.mProjectBuildString.Remove(project);
            }
#endif

            if (mWorkspace.mStartupProject == project)
                mWorkspace.mStartupProject = null;

            mWorkspace.FixOptions();
            CurrentWorkspaceConfigChanged();

			var bfCompilers = scope List<BfCompiler>();
			GetBfCompilers(bfCompilers);
            for (var bfCompiler in bfCompilers)
            {
                var bfProject = bfCompiler.mBfSystem.GetBfProject(project);
                bfProject.SetDisabled(true);
                bfCompiler.mBfSystem.RemoveBfProject(project);
                bfCompiler.QueueDeleteBfProject(bfProject);
            }

			for (var checkProject in mWorkspace.mProjects)
			{
				for (var dep in checkProject.mDependencies)
				{
					if (dep.mProjectName == project.mProjectName)
					{
						@dep.Remove();
						delete dep;
					}
				}
			}

			for (var dep in mWorkspace.mProjectSpecs)
			{
				if (dep.mProjectName == project.mProjectName)
				{
					@dep.Remove();
					delete dep;
				}
			}

			List<WidgetWindow> closeList = scope .();
			for (var window in gApp.mWindows)
			{
				if (var widgetWindow = window as WidgetWindow)
				{
					if (var projectProperties = widgetWindow.mRootWidget as ProjectProperties)
					{
						if (projectProperties.[Friend]mProject == project)
							closeList.Add(widgetWindow);
					}
				}
			}
			for (var window in closeList)
				window.Close(true);

			delete project;
        }

        BfPassInstance CompileBeef(Project hotProject, int32 hotIdx, bool lastCompileHadMessages, out bool hadBeef)
        {
			hadBeef = false;
			mCompilingBeef = true;
			BeefCompileStarted();

            bool success = true;
            BfSystem bfSystem = mBfBuildSystem;
            BfCompiler bfCompiler = mBfBuildCompiler;
            BfPassInstance passInstance = bfSystem.CreatePassInstance();
            bfCompiler.QueueSetPassInstance(passInstance);

            bfCompiler.QueueSetWorkspaceOptions(hotProject, hotIdx);
            
			Workspace.Options workspaceOptions = GetCurWorkspaceOptions();

			bool tryQueueFiles = true;
			bool doCompile = false;
			if (lastCompileHadMessages)
				doCompile = true;
			if ((!workspaceOptions.mIncrementalBuild) && (!lastCompileHadMessages))
			{
				tryQueueFiles = false;
				for (var project in mWorkspace.mProjects)
				{
					if (HasPendingBeefFiles(false, project.mRootFolder))
						tryQueueFiles = true;
				}
			}

			if (hotProject != null)
			{
				mWorkspace.mHadHotCompileSinceLastFullCompile = true;
			}
			else
			{
				if (mWorkspace.mHadHotCompileSinceLastFullCompile)
				{
					doCompile = true;
					mWorkspace.mHadHotCompileSinceLastFullCompile = false;
				}
			}

			if (mWorkspace.mForceNextCompile)
			{
				doCompile = true;
				mWorkspace.mForceNextCompile = false;
			}

			if (tryQueueFiles)
			{
				PreConfigureBeefSystem(bfSystem, bfCompiler);
	            for (var project in mWorkspace.mProjects)
	            {
	                if (SetupBeefProjectSettings(bfSystem, bfCompiler, project))
	                {
	                    doCompile |= QueueParseBeefFiles(bfCompiler, !workspaceOptions.mIncrementalBuild, project.mRootFolder);
	                }
	                else if (IsProjectEnabled(project))
	                    success = false;
	            }
			}

            if (!success)
                return null;

            for (var project in mWorkspace.mProjects)
            {
				if (!project.mGeneralOptions.mTargetType.IsBeef)
					continue;

				hadBeef = true;
                String projectBuildDir = scope String();
                GetWorkspaceBuildDir(projectBuildDir);
                projectBuildDir.Append("/", project.mProjectName);
                Directory.CreateDirectory(projectBuildDir).IgnoreError();
            }

			if (!hadBeef)
				doCompile = false;

            if (doCompile)
            {
				var dir = scope String();
				GetWorkspaceBuildDir(dir);
                bfCompiler.QueueCompile(dir);
            }
            else
            {
				bfCompiler.ClearResults();
                passInstance.mCompileSucceeded = true;
            }
            return passInstance;
        }

        public bool DoResolveConfigString(Workspace.Options workspaceOptions, Project project, Project.Options options, StringView configString, String error, String result)
        {
			int i = result.Length;
			result.Append(configString);

			bool hadError = false;

            for ( ; i < result.Length - 2; i++)
            {
                if ((result[i] == '$') && (result[i + 1] == '('))
                {
                    int parenPos = result.IndexOf(')', i + 2);
                    if (parenPos != -1)
					ReplaceBlock:
                    {
                        String replaceStr = scope String(result, i + 2, parenPos - i - 2);
                        String newString = null;

						if ((newString == null) && (project != null))
						{
							switch (replaceStr)
							{
							case "ProjectName":
								newString = project.mProjectName;
								break;
							}
						}

						if ((newString == null) && (options != null))
						{
							switch (replaceStr)
							{
							case "Arguments":
								newString = options.mDebugOptions.mCommandArguments;
							case "WorkingDir":
								newString = options.mDebugOptions.mWorkingDirectory;
							case "TargetDir":
								{
									if (project.IsDebugSession)
									{
										let targetPath = scope:ReplaceBlock String();
										DoResolveConfigString(workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, targetPath);
										newString = scope:ReplaceBlock String();
										Path.GetDirectoryPath(targetPath, newString);
										break;
									}

									String targetDir = scope String();
									DoResolveConfigString(workspaceOptions, project, options, options.mBuildOptions.mTargetDirectory, error, targetDir);
									newString = scope:ReplaceBlock String();
									Path.GetAbsolutePath(targetDir, project.mProjectDir, newString);
								}
							case "TargetPath":
		                        {
									if (project.IsDebugSession)
									{
										newString = scope:ReplaceBlock String();
										DoResolveConfigString(workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, newString);
										break;
									}

									String targetDir = scope String();
									DoResolveConfigString(workspaceOptions, project, options, options.mBuildOptions.mTargetDirectory, error, targetDir);
									newString = scope:ReplaceBlock String();
									Path.GetAbsolutePath(targetDir, project.mProjectDir, newString);
									Utils.GetDirWithSlash(newString);
									
									DoResolveConfigString(workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, newString);
#if BF_PLATFORM_WINDOWS
		                            if (project.mGeneralOptions.mTargetType == .BeefLib)
		                                newString.Append(".lib");
									else if (project.mGeneralOptions.mTargetType == .BeefDynLib)
		                                newString.Append(".dll");
		                            else if (project.mGeneralOptions.mTargetType != .CustomBuild)
		                                newString.Append(".exe");
#else
		                            if (project.mGeneralOptions.mTargetType == Project.TargetType.BeefLib)
		                                newString.Append(".so");
#endif
		                        }
		                    case "ProjectDir":
								if (project.IsDebugSession)
								{
									newString = scope:: String();
									Path.GetDirectoryPath(project.mProjectPath, newString);
								}
								else
		                        	newString = project.mProjectDir;
		                    case "BuildDir":
								newString = scope:: String();
		                        GetProjectBuildDir(project, newString);
								//Debug.WriteLine("BuildDir: {0}", newString);
							case "LinkFlags":
								newString = scope:ReplaceBlock String();

								if ((project.mGeneralOptions.mTargetType == .BeefConsoleApplication) ||
									(project.mGeneralOptions.mTargetType == .BeefWindowsApplication) ||
									(project.mGeneralOptions.mTargetType == .BeefDynLib) ||
									((options.mBuildOptions.mBuildKind == .Test) && (project == mWorkspace.mStartupProject)))
								{
#if BF_PLATFORM_WINDOWS
									String rtName = scope String();
									String dbgName = scope String();
									BuildContext.GetRtLibNames(workspaceOptions, options, false, rtName, dbgName);
									newString.Append(rtName);
									if (!dbgName.IsEmpty)
										newString.Append(" ", dbgName);
									switch (workspaceOptions.mAllocType)
									{
									case .JEMalloc:
										newString.Append(" jemalloc.lib");
									case .TCMalloc:
										newString.Append(" tcmalloc.lib");
									default:
									}
#else
								newString.Append("./libBeefRT_d.so -Wl,-rpath -Wl,.");
#endif
								}
							case "VSToolPath":
								if (workspaceOptions.mMachineType.PtrSize == 4)
									newString = gApp.mSettings.mVSSettings.mBin32Path;
								else
									newString = gApp.mSettings.mVSSettings.mBin64Path;
							case "VSToolPath_x86":
								newString = gApp.mSettings.mVSSettings.mBin32Path;
							case "VSToolPath_x64":
								newString = gApp.mSettings.mVSSettings.mBin64Path;
							}
						}

						if ((newString == null) && (mScriptManager != null))
						{
							switch (replaceStr)
							{
							case "ScriptDir":
								if ((mScriptManager != null) && (mScriptManager.mCurCmd != null))
								{
									newString = scope:: String();
									Path.GetDirectoryPath(mScriptManager.mCurCmd.mSrcFile, newString);
								}
							}
						}

						if (newString == null)
						{
	                        switch (replaceStr)
	                        {
	                        case "Configuration":
	                            newString = mConfigName;
	                        case "Platform":
	                            newString = mPlatformName;
	                        case "WorkspaceDir":
								if (mWorkspace.mDir != null)
	                            	newString = mWorkspace.mDir;
								else if (project.IsDebugSession)
								{
									newString = scope:: String();
									Path.GetDirectoryPath(project.mProjectPath, newString);
								}
							case "BeefPath":
								newString = gApp.mInstallDir;
	                        default:
	                        }
						}

						if (newString == null)
						{
							if (error != null)
								error.Set(replaceStr);
							hadError = true;
							break;
						}

						if (newString != null)
						{
							result.Remove(i, parenPos - i + 1);
							result.Insert(i, newString);
							i--;
						}
                    }
                }
            }

			return !hadError;
        }

        public bool ResolveConfigString(Workspace.Options workspaceOptions, Project project, Project.Options options, StringView configString, String errorContext, String outResult)
        {
            String errorString = scope String();
            if (!DoResolveConfigString(workspaceOptions, project, options, configString, errorString, outResult))
			{
                OutputErrorLine("Invalid macro in {0}: {1}", errorContext, errorString);
				return false;
			}
            return true;
        }

        public void GetWorkspaceBuildDir(String outResult)
        {
			if (mWorkspace.mDir == null)
				return;
            outResult.Append(mWorkspace.mDir, "/build/", mConfigName, "_", mPlatformName);
        }

        public void GetProjectBuildDir(Project project, String outResult)
        {
            GetWorkspaceBuildDir(outResult);
			if (!outResult.IsEmpty)
            	outResult.Append("/", project.mProjectName);
        }

        public void GetClangOutputFilePathWithoutExtension(ProjectSource projectSource, String outResult)
        {
            GetProjectBuildDir(projectSource.mProject, outResult);
            outResult.Append("/");
			String fullPath = scope String();
			projectSource.GetFullImportPath(fullPath);
            Path.GetFileNameWithoutExtension(fullPath, outResult);
        }

        public void GetClangBuildString(Project project, Project.Options options, Workspace.Options workspaceOptions, bool isC, String clangOptions)
        {
            bool isClang = options.mCOptions.mCompilerType == Project.CCompilerType.Clang;

            if (options.mCOptions.mEmitDebugInfo)
            {
                if (isClang)
				{
                    clangOptions.Append("-g -fstandalone-debug ");

					if (workspaceOptions.mToolsetType != .GNU)
						clangOptions.Append("-gcodeview ");

				}
                else
                    clangOptions.Append("-g ");
            }

			//TODO:
            var simd = workspaceOptions.mCSIMDSetting;
            if (options.mCOptions.mSIMD != null)
                simd = options.mCOptions.mSIMD.Value;

            switch (simd)
            {
            case .SSE:
                clangOptions.Append("-msse ");
                break;
            case .SSE2:
                clangOptions.Append("-msse2 ");
                break;
            case .SSE3:
                clangOptions.Append("-msse3 ");
                break;
            case .SSE4:
                clangOptions.Append("-msse4 ");
                break;
            case .SSE41:
                clangOptions.Append("-msse4.1 ");
                break;
            case .AVX:
                clangOptions.Append("-mavx ");
                break;
            case .AVX2:
                clangOptions.Append("-mavx2 ");
                break;
			default:
            }            

            if (options.mCOptions.mEnableBeefInterop)
            {
                var beefPreprocMacros = scope List<String>();
                GetBeefPreprocessorMacros(beefPreprocMacros);
                for (var beefPreprocMacro in beefPreprocMacros)                
                    clangOptions.Append("-D", beefPreprocMacro, " ");
            }

            if (options.mCOptions.mAllWarnings)
                clangOptions.Append("-Wall ");

            var optimizationLevel = options.mCOptions.mOptimizationLevel;
            if (optimizationLevel == Project.COptimizationLevel.FromWorkspace)
                optimizationLevel = (Project.COptimizationLevel)workspaceOptions.mCOptimizationLevel;
            clangOptions.AppendF("-{0} ", optimizationLevel);
            for (var preprocMacro in options.mCOptions.mPreprocessorMacros)
            {
                clangOptions.Append("-D", preprocMacro, " ");
            }
            for (var includePath in options.mCOptions.mIncludePaths)
            {
				var fullIncludePath = scope String();
				project.GetProjectFullPath(includePath, fullIncludePath);
                if (fullIncludePath.Contains(' '))
                    clangOptions.Append("\"-I", fullIncludePath, "\" ");
                else
                    clangOptions.Append("-I", fullIncludePath, " ");
            }

            if (options.mCOptions.mCompilerType == Project.CCompilerType.GCC)
            {
                if (workspaceOptions.mMachineType == Workspace.MachineType.x86)
                    clangOptions.Append("-m32 ");
                else
                    clangOptions.Append("-m64 ");
            }
            else
            {
				clangOptions.Append("--target=");
                GetTargetName(workspaceOptions, clangOptions);
				clangOptions.Append(" ");

				if (workspaceOptions.mToolsetType == .GNU)
				{
					//
				}
                else
				{
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\include\" ");
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\atlmfc\\include\" ");
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.14393.0\\ucrt\" ");
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.14393.0\\um\" ");
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.14393.0\\shared\" ");
					clangOptions.Append("-I\"C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.14393.0\\winrt\" ");
				}
            }

            if (options.mCOptions.mNoOmitFramePointers)
                clangOptions.Append("-fno-omit-frame-pointer ");

            if (options.mCOptions.mGenerateLLVMAsm)
                clangOptions.Append("-emit-llvm -S ");
            else
                clangOptions.Append("-c ");

            if (!isC)
            {
                if (!String.IsNullOrEmpty(options.mCOptions.mOtherCPPFlags))
                    clangOptions.Append(options.mCOptions.mOtherCPPFlags, " ") ;
                clangOptions.Append("-std=c++14 ");
            }
            else
            {
                if (!String.IsNullOrEmpty(options.mCOptions.mOtherCFlags))
                    clangOptions.Append(options.mCOptions.mOtherCFlags, " ");
            }            

        }

        static void QuoteIfNeeded(String str)
        {
            if (!str.Contains(' '))
                return;

			for (int32 i = 0; i < str.Length; i++)
			{
				char8 c = str[i];

				if ((c == '\\') || (c == '"'))
				{
					str.Insert(i, '\\');
					i++;
				}
			}

			str.Insert(0, '"');
			str.Append('"');
        }

        ExecutionQueueCmd CompileSource(Project project, Workspace.Options workspaceOptions, Project.Options options, String buildFileName, String addOptions = null)
        {
            String workspaceBuildDir = scope String();
            GetWorkspaceBuildDir(workspaceBuildDir);
            String projectBuildDir = scope String(workspaceBuildDir, "/", project.mProjectName);
            String baseName = scope String();
            Path.GetFileNameWithoutExtension(buildFileName, baseName);
            String objName = scope String(projectBuildDir, "/", baseName, (options.mCOptions.mGenerateLLVMAsm ? ".ll" : ".obj"));

            String llvmDir = scope String(IDEApp.sApp.mInstallDir);
            IDEUtils.FixFilePath(llvmDir);
            llvmDir.Append("llvm/");
            String gccExePath = "c:/mingw/bin/g++.exe";
            String clangCppExePath = scope String(llvmDir, "bin/clang++.exe");
			String clangCExePath = scope String(llvmDir, "bin/clang.exe");
            String clangOptions = scope String(buildFileName);
            QuoteIfNeeded(clangOptions);
            clangOptions.Append(" ");

            bool isC = false;

            var ext = scope String();
            Path.GetExtension(buildFileName, ext);
            if (ext == ".c")
                isC = true;            
            
			var buildStr = scope String();
			GetClangBuildString(project, options, workspaceOptions, isC, buildStr);
            clangOptions.Append(buildStr);

            int lastDot = objName.LastIndexOf('.');

			String depFileName = scope String(objName, 0, lastDot);
            QuoteIfNeeded(depFileName);
            depFileName.Append(".dep");
            clangOptions.Append("-MD -MF ", depFileName, " ");

			if (addOptions != null)
				clangOptions.Append(addOptions, " ");

            clangOptions.Append("-o ");
			String quotedObjName = scope String(objName);
            QuoteIfNeeded(quotedObjName);
			clangOptions.Append(quotedObjName);
			
            String compilerExePath = (options.mCOptions.mCompilerType == Project.CCompilerType.GCC) ? gccExePath : isC ? clangCExePath : clangCppExePath;            
            return QueueRun(compilerExePath, clangOptions, IDEApp.sApp.mInstallDir, .UTF8);
        }
		
		void SkipProjectCompile(Project project, Project hotProject)
		{
		    Project.Options options = GetCurProjectOptions(project);
		    if (options == null)
		        return;
		    
		    BfCompiler bfCompiler = mBfBuildCompiler;
		    var bfProject = mBfBuildSystem.mProjectMap[project];
		    bool bfHadOutputChanges;
		    List<String> bfFileNames = scope List<String>();
			bfCompiler.GetOutputFileNames(bfProject, false, out bfHadOutputChanges, bfFileNames);
			defer(scope) ClearAndDeleteItems(bfFileNames);
		    if (bfHadOutputChanges)
		        project.mNeedsTargetRebuild = true;
		}

		void GetTargetName(Workspace.Options workspaceOptions, String str)
		{
#if BF_PLATFORM_WINDOWS
			if (workspaceOptions.mToolsetType == .GNU)
			{
				if (workspaceOptions.mMachineType == Workspace.MachineType.x86)
				    str.Append("i686-pc-windows-gnu");
				else
				    str.Append("x86_64-pc-windows-gnu");
			}
			else
			{
				if (workspaceOptions.mMachineType == Workspace.MachineType.x86)
				    str.Append("i686-pc-windows-msvc");
				else
				    str.Append("x86_64-pc-windows-msvc");
			}
#elif BF_PLATFORM_LINUX
            if (workspaceOptions.mMachineType == Workspace.MachineType.x86)
                str.Append("i686-unknown-linux-gnu");
            else
                str.Append("x86_64-unknown-linux-gnu");
#else

#endif
		}

		void GetTargetPaths(Project project, Workspace.Options workspaceOptions, Project.Options options, List<String> outPaths)
		{
			String targetPath = scope String();
			ResolveConfigString(workspaceOptions, project, options, "$(TargetPath)", "Target path", targetPath);
			outPaths.Add(new String(targetPath));

#if BF_PLATFORM_WINDOWS
			if ((project.mGeneralOptions.mTargetType == .BeefConsoleApplication) ||
				(project.mGeneralOptions.mTargetType == .BeefWindowsApplication))
			{
				if (workspaceOptions.mToolsetType != .GNU)
				{
					String pdbPath = scope String();
					BuildContext.GetPdbPath(targetPath, workspaceOptions, options, pdbPath);
					outPaths.Add(new String(pdbPath));
				}
			}
#endif			
		}

		public class ArgBuilder
		{
			String mTarget;
			bool mDoLongBreak;
			int mLastBreak;
			HashSet<String> mLinkPaths = new HashSet<String>() ~ DeleteContainerAndItems!(_);

			public this(String target, bool doLongBreak)
			{
				mTarget = target;
				mDoLongBreak = doLongBreak;
				if (mDoLongBreak)
					mLastBreak = mTarget.LastIndexOf('\n');
				else
					mLastBreak = 0;
			}

			public void AddSep()
			{
				if (mDoLongBreak)
				{
					if (mTarget.Length - mLastBreak > 0x1F000)
					{
						mLastBreak = mTarget.Length;
						mTarget.Append('\n');
						return;
					}
				}
				mTarget.Append(' ');
			}
			
			public void AddFileName(String filePath)
			{
				IDEUtils.AppendWithOptionalQuotes(mTarget, filePath);

				/*int lastSlash = Math.Max(filePath.LastIndexOf('\\'), filePath.LastIndexOf('/'));
				if (lastSlash == -1)
				{
					IDEUtils.AppendWithOptionalQuotes(mTarget, filePath);
					return;
				}

				String fileDir = scope String(filePath, 0, lastSlash);
				String fileName = scope String(filePath, lastSlash + 1);

				String* strPtr = null;
				if (mLinkPaths.TryAdd(fileDir, out strPtr))
				{
					*strPtr = new String(fileDir);
					mTarget.Append("-libpath:");
					IDEUtils.AppendWithOptionalQuotes(mTarget, fileDir);
					AddSep();
				}

				IDEUtils.AppendWithOptionalQuotes(mTarget, fileName);*/
			}
		}

        Project FindProject(String projectName)
        {
            return mWorkspace.FindProject(projectName);
        }

        public bool GetDependentProjectList(Project project, List<Project> orderedProjectList, List<Project> projectStack = null)
        {
			var useProjectStack = projectStack;
            if ((useProjectStack != null) && (useProjectStack.Contains(project)))
            {
                String projectError = scope String("Circular dependency between projects: ");
                for (int32 i = 0; i < useProjectStack.Count; i++)
                {
                    if (i > 0)
                        projectError.Append(", ");
                    projectError.Append(useProjectStack[i].mProjectName);
                }
                OutputLine(projectError);
                return true;
            }

            if (orderedProjectList.Contains(project))
                return true;

            bool addSelf = true;
            if (useProjectStack == null)
            {
                useProjectStack = scope:: List<Project>();
                addSelf = false;
            }

            useProjectStack.Add(project);
            for (var dep in project.mDependencies)
            {
                Project depProject = FindProject(dep.mProjectName);
                if (depProject == null)
                {
                    OutputLine(StackStringFormat!("Unable to find project '{0}', a dependency of project '{1}'", dep.mProjectName, project.mProjectName));
                    return false;
                }
                if (!GetDependentProjectList(depProject, orderedProjectList, useProjectStack))
                    return false;
            }

            if (addSelf)
            {
                useProjectStack.RemoveAt(useProjectStack.Count - 1);
                orderedProjectList.Add(project);
            }
            return true;
        }

        void GetOrderedProjectList(List<Project> orderedProjectList)
        {
            List<Project> projectStack = scope List<Project>();
            for (var project in mWorkspace.mProjects)            
                GetDependentProjectList(project, orderedProjectList, projectStack);
        }

		public virtual void LoadFailed()
		{
			if (mRunningTestScript)
				mFailed = true;
		}

		public virtual void TestFailed()
		{
			mLastTestFailed = true;
		}

        protected virtual void CompileFailed()
        {
			if (mTestManager != null)
				mTestManager.BuildFailed();
			if (mVerbosity > .Quiet)
            	OutputLine("Compile failed.");
			mLastCompileFailed = true;

			if (mRunningTestScript)
			{
				if (!gApp.mScriptManager.mHadExpectingError)
					gApp.mScriptManager.Fail("Compile failed");
			}
			else
			{
				Beep(MessageBeepType.Error);
			}
        }

		void DbgCopyChangedFiles(DateTime cmpTime, StringView srcDir, StringView destDir)
		{
			bool isFirstFile = true;

			for (let fileEntry in Directory.EnumerateFiles(srcDir))
			{
				var fileWriteTime = fileEntry.GetLastWriteTime();

				String dates = scope .();
				cmpTime.ToString(dates);
				dates.Append("\n");
				fileWriteTime.ToString(dates);

				if (fileWriteTime > mDbgHighestTime)
					mDbgHighestTime = fileWriteTime;

				if (fileWriteTime < cmpTime)
				{
					continue;
				}

				String fileName = scope String();
				fileEntry.GetFileName(fileName);

				if (isFirstFile)
				{
					Directory.CreateDirectory(destDir).IgnoreError();
					isFirstFile = false;
				}

				String srcFile = scope .();
				srcFile.Append(srcDir);
				srcFile.Append("/");
				srcFile.Append(fileName);

				String destFile = scope .();
				destFile.Append(destDir);
				destFile.Append("/");
				destFile.Append(fileName);

				File.Copy(srcFile, destFile);
			}

			for (let fileEntry in Directory.EnumerateDirectories(srcDir))
			{
				String dirName = scope String();
				fileEntry.GetFileName(dirName);

				String newSrcDir = scope String();
				newSrcDir.Append(srcDir);
				newSrcDir.Append("/");
				newSrcDir.Append(dirName);

				String newDestDir = scope String();
				newDestDir.Append(destDir);
				newDestDir.Append("/");
				newDestDir.Append(dirName);

				DbgCopyChangedFiles(cmpTime, newSrcDir, newDestDir);
			}
		}

		void CompileResult(String hotProjectName, bool success)
		{
			if (mDbgCompileDir != null)
			{
				String compileResults = scope .();
				compileResults.AppendF("COMPILE {} {} {}: ", mDbgCompileIdx, mConfigName, mPlatformName);
				if (hotProjectName != null)
					compileResults.Append("Hot compile. ");
				if (!success)
					compileResults.Append("FAILED\n");
				else
					compileResults.Append("SUCCESS\n");

				String path = scope .();
				path.Append(mDbgCompileDir);
				path.Append("log.txt");
				File.WriteAllText(path, compileResults, true);

				var prevHighestTime = mDbgHighestTime;
				for (var project in mWorkspace.mProjects)
				{
					String buildDir = scope .();
					GetProjectBuildDir(project, buildDir);

					String toPath = scope .();
					toPath.Append(mDbgVersionedCompileDir);
					toPath.Append(project.mProjectName);
					toPath.Append("/");
					DbgCopyChangedFiles(prevHighestTime, buildDir, toPath);
				}
			}
		}

        void ProcessBeefCompileResults(BfPassInstance passInstance, bool runAfter, Project hotProject, Stopwatch startStopWatch)
        {
			bool didCompileSucceed = true;
			if (passInstance != null)
			{
				if (mProfileCompileProfileId != 0)
					mProfileCompileProfileId.Dispose();

	            while (true)
	            {
					String str = scope String();
	                if (!passInstance.PopOutString(str))
						break;

					if (mVerbosity == .Quiet)
						continue;

					if (str.StartsWith(":"))
					{
						int spacePos = str.IndexOf(' ');
						if (spacePos > 0)
						{
							bool wantsDisp = true;
							StringView msgType = StringView(str, 0, spacePos);
							if (msgType == ":warn")
							{
								mLastCompileHadMessages = true;
								wantsDisp = mVerbosity >= .Minimal;
							}
							else if (msgType == ":error")
							{
								mLastCompileHadMessages = true;
							}
							else if (msgType == ":low")
								wantsDisp = mVerbosity >= .Detailed;
							else if (msgType == ":med")
								wantsDisp = mVerbosity >= .Normal;
							if (!wantsDisp)
								continue;

							str.Remove(0, spacePos + 1);
						}
					}

					str.Append("\n");
					OutputSmart(str);
	                //OutputLine(str);
	            }

				if ((passInstance.mFailed) && (passInstance.mCompileSucceeded))
				{
					// This can happen if we can't load a Beef file
					CompileFailed();
					passInstance.mCompileSucceeded = false;
				}

	            didCompileSucceed = passInstance.mCompileSucceeded;

				if (didCompileSucceed)
				{
					mLastCompileSucceeded = true;
					mWorkspace.WithProjectItems(scope (item) =>
						{
							if (var projectSource = item as ProjectSource)
							{
							    if (IsBeefFile(projectSource.mPath))
							    {
									if (!projectSource.mHasChangedSinceLastCompile)
										projectSource.mHasChangedSinceLastSuccessfulCompile = false;
								}
							}
						});
				}
				else
					mLastCompileHadMessages = true;

	        	delete passInstance;
			}

			if ((hotProject != null) && (passInstance != null) && (didCompileSucceed))
			{
				if (!mDebugger.mIsRunning)
				{
					OutputSmart("Hot compile failed - target no longer running");
					CompileFailed();
					return;
				}

				DebugManager.HotResolveFlags hotResolveFlags = .ActiveMethods;

				if (mBfBuildCompiler.GetHasHotPendingDataChanges())
				{
					hotResolveFlags |= .Allocations;
					mHotResolveState = .PendingWithDataChanges;
				}
				else
				{
					mHotResolveState = .Pending;
				}
				
				mHotResolveTryIdx = 0;
					
				mDebugger.InitiateHotResolve(hotResolveFlags);
				return;
			}

			List<Project> orderedProjectList = scope List<Project>();
            GetOrderedProjectList(orderedProjectList);
            if (!didCompileSucceed)
            {
				// Failed, bail out
				for (var project in orderedProjectList)
				{
				    SkipProjectCompile(project, hotProject);                        
				}
				CompileResult((hotProject != null) ? hotProject.mProjectName : null, false);
				CompileFailed();
                return;
            }

            var completedCompileCmd = new BuildCompletedCmd();
			if (hotProject != null)
				completedCompileCmd.mHotProjectName = new String(hotProject.mProjectName);

			//TODO: Pass in
			//Project project = mWorkspace.mProjects[0];
            bool success = true;
            List<String> hotFileNames = scope List<String>();
			defer ClearAndDeleteItems(hotFileNames);
			Debug.Assert(mBuildContext == null);
			DeleteAndNullify!(mBuildContext);
			mBuildContext = new .();
			mBuildContext.mWorkspaceOptions = GetCurWorkspaceOptions();

            for (var project in orderedProjectList)
            {
                if (!mBuildContext.QueueProjectCompile(project, hotProject, completedCompileCmd, hotFileNames, runAfter))
                    success = false;
            }

            if (hotFileNames.Count > 0)
            {
				// Why were we rehupping BEFORE hotLoad?
                mDebugger.RehupBreakpoints(false, false);

				String[] entries = scope String[hotFileNames.Count];
				for (int32 i = 0; i < hotFileNames.Count; i++)
					entries[i] = hotFileNames[i];

				//

				mBfBuildCompiler.HotCommit();
                mDebugger.HotLoad(entries, mWorkspace.HotCompileIdx);
                /*if (mDebugger.IsPaused())
                    PCChanged();*/

				//mDebugger.RehupBreakpoints(false);

				RefreshWatches();
            }

            if (!success)
                CompileFailed();

            if ((runAfter) && (success))
            {
                var startDebugCmd = new StartDebugCmd();
				startDebugCmd.mWasCompiled = true;
                startDebugCmd.mOnlyIfNotFailed = true;
                mExecutionQueue.Add(startDebugCmd);
            }
            
			if (startStopWatch != null)
            {
				delete completedCompileCmd.mStopwatch;
				var stopwatch = new Stopwatch();
				stopwatch.CopyFrom(startStopWatch);
                completedCompileCmd.mStopwatch = stopwatch;
			}
            mExecutionQueue.Add(completedCompileCmd);
        }

        bool CompileAndRun()
        {
			if (AreTestsRunning())
				return false;
			if (IsCompiling)
				return false;

            if (!mExecutionQueue.IsEmpty)
            {
                if (var processCompileCmd = mExecutionQueue.Back as ProcessBfCompileCmd)
                {
                    processCompileCmd.mRunAfter = true;
                }

                return false;
            }

            mOutputPanel.Clear();            
            OutputLine("Compiling...");
            if (!Compile(.RunAfter))
				return false;
			return true;
        }

		public void QueueProfiling(int threadId, String desc, int sampleRate)
		{
			var profileCmd = new ProfileCmd();
			profileCmd.mThreadId = threadId;
			profileCmd.mDesc = new String(desc);
			profileCmd.mSampleRate = sampleRate;
			if (var processCompileCmd = mExecutionQueue.Back as ProcessBfCompileCmd)
			{
				processCompileCmd.mProfileCmd = profileCmd;
			}
		}

		[IDECommand]
		void StopRunning()
		{
			if (AreTestsRunning())
			{
				mTestManager.Stop();
				return;
			}

			if (mCrashDumpPath != null)
			{
				DeleteAndNullify!(mCrashDumpPath);
				mDebugger.Detach();
				mDebugger.mIsRunning = false;
				mExecutionPaused = false;
			}

			if (mDebugger.mIsRunning)
			{
				mDebugger.StopDebugging();
			}
		}

		enum CompileKind
		{
			Normal,
			RunAfter,
			Test
		}

        protected bool Compile(CompileKind compileKind = .Normal, Project hotProject = null)
        {
			Debug.Assert(mBuildContext == null);

			if (mDbgCompileDump)
			{
				mDbgCompileIdx++;

				String dbgBuildDir = scope .();
				dbgBuildDir.Append(mInstallDir);
				dbgBuildDir.Append("_dbg/");

				String exePath = scope .();
				Environment.GetExecutableFilePath(exePath);
				Path.GetFileNameWithoutExtension(exePath, dbgBuildDir);

				/*String exePath = scope .();
				Environment.GetExecutableFilePath(exePath);
				String exeName = scope .();
				Path.GetFileNameWithoutExtension(exePath, exeName);

				String dbgBuildDir = scope String(mInstallDir, "dbg_build_", exeName);*/

				if (mDbgCompileIdx == 0)
				{
					String tempDirName = scope .();
					tempDirName.Append(mInstallDir);
					tempDirName.AppendF("_dbg/_{0}", DateTime.Now.Ticks);

					if (Directory.Move(dbgBuildDir, tempDirName) case .Err(let err))
					{
						if (err != .NotFound)
						{
							Fail(scope String..AppendF("Failed to rename {0} to {1}", dbgBuildDir, tempDirName));
							return false;
						}
					}

					String delDirName = new String(tempDirName);
					ThreadPool.QueueUserWorkItem(new () =>
						{
							Utils.DelTree(delDirName).IgnoreError();
							delete delDirName;
						});

					
					bool worked = false;
					for (int i < 20)
					{
						if (Directory.CreateDirectory(dbgBuildDir) case .Ok)
						{
							worked = true;
							break;
						}
						Thread.Sleep(100);
					}
					Debug.Assert(worked);
				}
				dbgBuildDir.Append("/");
				if (mDbgCompileIdx == 0)
				{
					delete mDbgCompileDir;
					mDbgCompileDir = new String(dbgBuildDir);
					
					for (var project in mWorkspace.mProjects)
					{
						String buildDir = scope .();
						GetProjectBuildDir(project, buildDir);

						String toPath = scope .();
						toPath.Append(mDbgCompileDir);
						toPath.Append("/init/");
						toPath.Append(project.mProjectName);
						toPath.Append("/");
						DbgCopyChangedFiles(default, buildDir, toPath);
					}
				}

				dbgBuildDir.AppendF("{0}", mDbgCompileIdx);
				Directory.CreateDirectory(dbgBuildDir);

				DeleteAndNullify!(mDbgVersionedCompileDir);
				mDbgVersionedCompileDir = new String(dbgBuildDir);
				mDbgVersionedCompileDir.Append("/");
			}

			if ((AreTestsRunning()) && (compileKind != .Test))
			{
				return false;
			}

			var workspaceOptions = GetCurWorkspaceOptions();
			if (compileKind == .RunAfter)
			{
				if (workspaceOptions.mBuildKind == .Test)
				{
					OutputLineSmart("ERROR: Cannot directly run Test workspace configurations.  Use the 'Test' menu to run or debug tests.");
					return false;
				}
			}

			if (Workspace.PlatformType.GetFromName(mPlatformName) == .Windows)
			{
				if (!mSettings.mVSSettings.IsConfigured())
					mSettings.mVSSettings.SetDefaults();

				if (!mSettings.mVSSettings.IsConfigured())
				{
					String err = 
					"""
					Beef requires the Microsoft C++ build tools for Visual Studio 2013 or later, but they don't seem to be installed.

					Install just Microsoft Visual C++ Build Tools or the entire Visual Studio suite from:
					    https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019
					""";

#if CLI
					Fail(err);
#else
					DarkDialog dlg = new DarkDialog("Visual C++ Not Found", err, DarkTheme.sDarkTheme.mIconError);
					dlg.mWindowFlags |= .Modal;
					dlg.AddOkCancelButtons(new (dlg) =>
						{
							ProcessStartInfo psi = scope ProcessStartInfo();
							psi.SetFileName("https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019");
							psi.UseShellExecute = true;
							psi.SetVerb("Open");
							var process = scope SpawnedProcess();
							process.Start(psi).IgnoreError();
						},
						new (dlg) =>
						{

						});
					((DarkButton)dlg.mButtons[0]).Label = "Open Link";
					dlg.PopupWindow(GetActiveWindow() as WidgetWindow);
					MessageBeep(.Error);
#endif
				}
			}

			if (mProfileCompile)
				mProfileCompileProfileId = Profiler.StartSampling("Compile");

			if (mWantsBeefClean)
			{
				// We must finish cleaning before we can compile
				while (mWantsBeefClean)
				{
					UpdateCompilersAndDebugger();
				}
			}
			
			if ((!workspaceOptions.mIncrementalBuild) && (mCompileSinceCleanCount > 0) && (hotProject == null))
			{
				delete mBfBuildCompiler;
				delete mBfBuildSystem;
				mBfBuildSystem = new BfSystem();
				mBfBuildCompiler = mBfBuildSystem.CreateCompiler(false);

				for (var project in mWorkspace.mProjects)
				{
				    mBfBuildSystem.AddProject(project);
				}
			}

			bool lastCompileHadMessages = mLastCompileHadMessages;
			mLastCompileFailed = false;
			mLastCompileSucceeded = false;
			mLastCompileHadMessages = false;
			mCompileSinceCleanCount++;
            ShowPanel(mOutputPanel, false);

			if (mDisableBuilding)
			{
				OutputLineSmart("ERROR: Compiling disabled!");
				return false;
			}

            if (mExecutionQueue.Count > 0)
                return false;

            if ((mDebugger != null) && (mDebugger.mIsRunning) && (hotProject == null))
			{
				Debug.Assert(!mDebugger.mIsRunningCompiled);
				Debug.Assert(compileKind == .Normal);
			}

            mHaveSourcesChangedExternallySinceLastCompile = false;
            mHaveSourcesChangedInternallySinceLastCompile = false;
            mWorkspace.SetupProjectCompileInstance(hotProject != null);

			int32 hotIdx = mWorkspace.HotCompileIdx;
			if (hotProject == null)
			{
				Debug.Assert(hotIdx == 0);
			}

			// Set position for breakpoints here only on first one.
			//  For hotload we do it later.
			//if (!mDebugger.mIsRunning)
				//mDebugger.RehupBreakpoints(true);
            
            SaveClangFiles();            

            if (compileKind == .RunAfter)
            {
				DeleteAndNullify!(mCompileAndRunStopwatch);
                mCompileAndRunStopwatch = new Stopwatch();
                mCompileAndRunStopwatch.Start();
            }

            //mBfBuildCompiler.QueueStartTiming();
            mBfBuildCompiler.ClearCompletionPercentage();

            BfPassInstance passInstance = null;

			passInstance = CompileBeef(hotProject, hotIdx, lastCompileHadMessages, let hadBeef);
            if (passInstance == null)
            {
                CompileFailed();
                return false;
            }                

            ProcessBfCompileCmd processCompileCmd = new ProcessBfCompileCmd();
            processCompileCmd.mBfPassInstance = passInstance;
            processCompileCmd.mRunAfter = compileKind == .RunAfter;
            processCompileCmd.mHotProject = hotProject;
            processCompileCmd.mStopwatch = new Stopwatch();
            processCompileCmd.mStopwatch.Start();
			processCompileCmd.mHadBeef = hadBeef;
            mExecutionQueue.Add(processCompileCmd);                        

            return true;
        }

		void CheckDebugVisualizers()
		{
			scope AutoBeefPerf("CheckDebugVisualizers");

			String dbgVis = scope String();
			dbgVis.Append(mInstallDir, "BeefDbgVis.toml");

			mWorkspace.WithProjectItems(scope (projectItem) =>
				{
					var projectSource = projectItem as ProjectSource;
					if (projectSource == null)
						return;
					if ((projectSource.mName.StartsWith("BeefDbgVis")) &&
						(projectSource.mName.EndsWith(".toml")))
					{
						dbgVis.Append("\n");
						projectSource.GetFullImportPath(dbgVis);
					}
				});

			mDebugger.LoadDebugVisualizers(dbgVis);
			//mDebugger.LoadDebugVisualizers(scope String(mInstallDir, "BeefDbgVis.toml"));
		}

        bool DebugProject(bool wasCompiled)
        {
			mProfilePanel.Clear();

            if (mWorkspace.mStartupProject == null)
            {
                OutputLineSmart("ERROR: No startup project started");
                return false;
            }
			var project = mWorkspace.mStartupProject;
			var workspaceOptions = GetCurWorkspaceOptions();
            var options = GetCurProjectOptions(project);
            if (options == null)
            {
                OutputLineSmart("ERROR: Startup project '{0}' not enabled", mWorkspace.mStartupProject.mProjectName);
                return false;
            }

            mDebugger.ClearInvalidBreakpoints();

            mTargetDidInitBreak = false;
            mTargetHadFirstBreak = false;

			//options.mDebugOptions.mCommand

            String launchPath = scope String();
            ResolveConfigString(workspaceOptions, project, options, options.mDebugOptions.mCommand, "debug command", launchPath);
            String arguments = scope String();
            ResolveConfigString(workspaceOptions, project, options, "$(Arguments)", "debug command arguments", arguments);
            String workingDirRel = scope String();
            ResolveConfigString(workspaceOptions, project, options, "$(WorkingDir)", "debug working directory", workingDirRel);
			var workingDir = scope String();
			Path.GetAbsolutePath(workingDirRel, project.mProjectDir, workingDir);

			String targetPath = scope .();
			ResolveConfigString(workspaceOptions, project, options, "$(TargetPath)", "Target path", targetPath);

			IDEUtils.FixFilePath(launchPath);
			IDEUtils.FixFilePath(targetPath);

			if (workingDir.IsEmpty)
				Path.GetDirectoryPath(launchPath, workingDir);

			if (!Directory.Exists(workingDir))
			{
				OutputErrorLine(scope String()..AppendF("Unable to locate working directory '{0}'", workingDir));
				return false;
			}

			var envVars = scope Dictionary<String, String>();
			defer { for (var kv in envVars) { delete kv.key; delete kv.value; } }
			Environment.GetEnvironmentVariables(envVars);

			for (var envVar in options.mDebugOptions.mEnvironmentVars)
			{
				int eqPos = envVar.IndexOf('=', 1);
				if (eqPos == -1)
				{
					OutputErrorLine("Invalid environment variable: {0}", envVar);
				}
				else
				{
					Environment.SetEnvironmentVariable(envVars, StringView(envVar, 0, eqPos), StringView(envVar, eqPos + 1));
				}
			}

			var envBlock = scope List<char8>();
			Environment.EncodeEnvironmentVariables(envVars, envBlock);

			if (launchPath.IsWhiteSpace)
			{
				Fail(scope String()..AppendF("No debug command specified in '{}' properties", project.mProjectName));
				return false;
			}

            if (!mDebugger.OpenFile(launchPath, targetPath, arguments, workingDir, envBlock, wasCompiled))
            {
				DeleteAndNullify!(mCompileAndRunStopwatch);
                return false;
            }

			CheckDebugVisualizers();

			mDebugger.mIsRunning = true;
			WithSourceViewPanels(scope (sourceView) =>
				{
					sourceView.RehupAlias();
				});

            mDebugger.RehupBreakpoints(true);
            mDebugger.Run();
			mModulePanel.ModulesChanged();

            if (mCompileAndRunStopwatch != null)
            {
                mCompileAndRunStopwatch.Stop();
                OutputLine("Compile-to-debug time: {0:0.00}s", mCompileAndRunStopwatch.ElapsedMilliseconds / 1000.0f);
            }

            if ((mTargetStartWithStep) && (mMainBreakpoint == null))
            {
				// The idea is that we don't want to step into static initializers, so we 
				// temporarily break on _main and then we single step                                    
				//mMainBreakpoint = mDebugger.CreateSymbolBreakpoint("_ZN3Hey4Dude3Bro9TestClass4MainEv");
                if ((project.mGeneralOptions.mTargetType == Project.TargetType.BeefConsoleApplication) ||
                    (project.mGeneralOptions.mTargetType == Project.TargetType.BeefWindowsApplication))
                    mMainBreakpoint = mDebugger.CreateSymbolBreakpoint("-BeefMain");
                else
				{
                    mMainBreakpoint = mDebugger.CreateSymbolBreakpoint("-main");
#if BF_PLATFORM_WINDOWS
					//mMainBreakpoint2 = mDebugger.CreateSymbolBreakpoint("-WinMain");
#endif
				}
            }

            return true;
        }

		public void Attach(Process process, DebugManager.AttachFlags attachFlags)
		{
			UpdateTitle(process.ProcessName);
			OutputLine("Attaching to process id {0}: {1}", process.Id, process.ProcessName);

			mExecutionPaused = false;
			mTargetDidInitBreak = false;
			mTargetHadFirstBreak = false;

			if (mDebugger.mIsRunning)
			{
				Fail("Already debugging a target");
				return;
			}

			if (!mDebugger.Attach(process, attachFlags))
			{
				Fail("Failed to attach to process");
				return;
			}

			CheckDebugVisualizers();
			mDebugger.mIsRunning = true;
			mDebugger.RehupBreakpoints(true);
			mDebugger.Run();
			mIsAttachPendingSourceShow = true;
		}

        void ShowPanel(Widget panel, bool setFocus = true)
        {
            WithTabs(scope (tab) =>
                {
                    if (tab.mContent == panel)
                        tab.Activate(setFocus);
                });
        }

        public void CreateDefaultLayout()
        {
			//TODO:
			//mConfigName.Set("Dbg");

			if ((!mRunningTestScript) && (LoadDefaultLayoutData()))
			{
				return;
			}

            bool docOnlyView = false;
            
            TabbedView projectTabbedView = CreateTabbedView();
            SetupTab(projectTabbedView, "Workspace", 0, mProjectPanel, false);
            projectTabbedView.SetRequestedSize(200, 200);
			projectTabbedView.mWidth = 200;

            //TabbedView propertiesView = CreateTabbedView();
            //propertiesView.AddTab("Properties", 0, mPropertiesPanel, false);
            //propertiesView.SetRequestedSize(250, 250);

            if (!docOnlyView)
            {
                mDockingFrame.AddDockedWidget(projectTabbedView, null, DockingFrame.WidgetAlign.Left);                
                EnsureDocumentArea();
                //mDockingFrame.AddDockedWidget(propertiesView, null, DockingFrame.WidgetAlign.Right);                
            }            

            var outputTabbedView = CreateTabbedView();
            mDockingFrame.AddDockedWidget(outputTabbedView, null, DockingFrame.WidgetAlign.Bottom);
			outputTabbedView.SetRequestedSize(250, 250);
            
            SetupTab(outputTabbedView, "Output", 150, mOutputPanel, false);
            SetupTab(outputTabbedView, "Immediate", 150, mImmediatePanel, false);
            //outputTabbedView.AddTab("Find Results", 150, mFindResultsPanel, false);

            var watchTabbedView = CreateTabbedView();
            watchTabbedView.SetRequestedSize(250, 250);
            mDockingFrame.AddDockedWidget(watchTabbedView, outputTabbedView, DockingFrame.WidgetAlign.Left);

            SetupTab(watchTabbedView, "Auto", 150, mAutoWatchPanel, false);
            SetupTab(watchTabbedView, "Watch", 150, mWatchPanel, false);
            SetupTab(watchTabbedView, "Memory", 150, mMemoryPanel, false);
            
            SetupTab(outputTabbedView, "Call Stack", 150, mCallStackPanel, false);
            SetupTab(outputTabbedView, "Threads", 150, mThreadPanel, false);			
        }

        protected void CreateBfSystems()
        {
			scope AutoBeefPerf("IDEApp.CreateBfSystems");

#if !CLI
			if (!mNoResolve)
			{
	            mBfResolveSystem = new BfSystem();
	            mBfResolveCompiler = mBfResolveSystem.CreateCompiler(true);
	            mBfResolveHelper = new BfResolveHelper();
			}
#if IDE_C_SUPPORT
            mResolveClang = new ClangCompiler(true);
#endif
#endif

			mCompileSinceCleanCount = 0;
            mBfBuildSystem = new BfSystem();
            mBfBuildCompiler = mBfBuildSystem.CreateCompiler(false);
#if IDE_C_SUPPORT
            mDepClang = new ClangCompiler(false);
            mDepClang.mPairedCompiler = mResolveClang;
            mResolveClang.mPairedCompiler = mDepClang;
#endif
        }
        
		void UpdateTitle(StringView titleOverride = default)
		{
			String title = scope String();
			if ((mWorkspace != null) && (mWorkspace.mName != null))
			{
				title.Append(mWorkspace.mName);
				title.Append(" - ");
			}
			if (titleOverride.Ptr != null)
			{
				title.Append(titleOverride);
				title.Append(" - ");
			}
			title.Append("Beef IDE");
			mMainWindow.SetTitle(title);
		}

		public void CreateSpellChecker()
		{
			mSpellChecker = new SpellChecker();
			if (mSpellChecker.Init(scope String(mInstallDir, "en_US")) case .Err)
			{
				DeleteAndNullify!(mSpellChecker);
			}
		}

#if !CLI
        public override void Init()
        {
			scope AutoBeefPerf("IDEApp.Init");

			mCommands.Init();
			EnableGCCollect = mEnableGCCollect;

			if (mConfigName.IsEmpty)
				mConfigName.Set("Debug");
			if (mPlatformName.IsEmpty)
				mPlatformName.Set(sPlatform64Name);

			Directory.GetCurrentDirectory(mInitialCWD);

			mColorMatrix = Matrix4.Identity;

            base.Init();
			mSettings.Apply();

			//Yoop();

			/*for (int i = 0; i < 100*1024*1024; i++)
			{
				mTestString.Append("Hey, this is a test string!");
			}*/

			mAutoDirty = false;
            WidgetWindow.sOnWindowClosed.Add(new => HandleWindowClosed);

			//mProjectDir = BFApp.mApp.mInstallDir + "../../";

			if (!mRunningTestScript)
			{
				// User setting can affect automated testing, so use default settings
				mSettings.Load();
				mSettings.Apply();
			}

            DarkTheme aTheme = new DarkTheme();
            aTheme.Init();
            ThemeFactory.mDefault = aTheme;

			mTinyCodeFont = new Font();
			mCodeFont = new Font();

			//mCodeFont = Font.LoadFromFile(BFApp.sApp.mInstallDir + "fonts/SourceCodePro32.fnt");

			//mCodeFont = Font.LoadFromFile(BFApp.sApp.mInstallDir + "fonts/SegoeUI24.fnt");

            mMainFrame = new MainFrame();
            mDockingFrame = mMainFrame.mDockingFrame;            
            
            mSquiggleImage = Image.LoadFromFile(scope String(mInstallDir, "images/Squiggle.png"));
            mCircleImage = Image.LoadFromFile(scope String(mInstallDir, "images/Circle.png"));
			
			RehupScale();

            CreateBfSystems();
			if (mWantsClean)
			{
				mBfBuildCompiler.ClearBuildCache();
				mWantsClean = false;
			}

            mProjectPanel = new ProjectPanel();
			mProjectPanel.mAutoDelete = false;
			mClassViewPanel = new ClassViewPanel();
			mClassViewPanel.mAutoDelete = false;
            mOutputPanel = new OutputPanel(true);
			mOutputPanel.mAutoDelete = false;
            mImmediatePanel = new ImmediatePanel();
			mImmediatePanel.mAutoDelete = false;
            mFindResultsPanel = new FindResultsPanel();
			mFindResultsPanel.mAutoDelete = false;
            mAutoWatchPanel = new WatchPanel(true);            
			mAutoWatchPanel.mAutoDelete = false;
            mWatchPanel = new WatchPanel(false);
			mWatchPanel.mAutoDelete = false;
            mMemoryPanel = new MemoryPanel();
			mMemoryPanel.mAutoDelete = false;
            mCallStackPanel = new CallStackPanel();
			mCallStackPanel.mAutoDelete = false;
            mBreakpointPanel = new BreakpointPanel();
			mBreakpointPanel.mAutoDelete = false;
			mModulePanel = new ModulePanel();
			mModulePanel.mAutoDelete = false;
            mThreadPanel = new ThreadPanel();
			mThreadPanel.mAutoDelete = false;
			mProfilePanel = new ProfilePanel();
			mProfilePanel.mAutoDelete = false;
            mPropertiesPanel = new PropertiesPanel();
			mPropertiesPanel.mAutoDelete = false;
			mAutoCompletePanel = new AutoCompletePanel();
			mAutoCompletePanel.mAutoDelete = false;

			OutputLine("IDE Started. Build 1.");

			/*if (!mRunningTestScript)
            {
				LoadUserSettings(); // User setting can affect automated testing, so use default settings
				mSettings.Load();
				mSettings.Apply();
			}*/

            if (mWorkspace.mDir != null)
            {
                mWorkspace.mName = new String();
                Path.GetFileName(mWorkspace.mDir, mWorkspace.mName);
                LoadWorkspace();
            }
			else if (mWorkspace.IsSingleFileWorkspace)
			{
				mWorkspace.mName = new String();
				Path.GetFileNameWithoutExtension(mWorkspace.mCompositeFile.mFilePath, mWorkspace.mName);
				LoadWorkspace();
			}

            if ((mRunningTestScript) || (!LoadWorkspaceUserData()))
                CreateDefaultLayout();

            WorkspaceLoaded();

			//
			{
				BFWindow.Flags flags = .Border | .ThickFrame | .Resizable | .SysMenu |
	                .Caption | .Minimize | .Maximize | .QuitOnClose | .Menu;
				if (mRunningTestScript)
					flags |= .NoActivate;

				scope AutoBeefPerf("IDEApp.Init:CreateMainWindow");
	            mMainWindow = new WidgetWindow(null, "Beef IDE", (int32)mRequestedWindowRect.mX,
	                (int32)mRequestedWindowRect.mY, (int32)mRequestedWindowRect.mWidth, (int32)mRequestedWindowRect.mHeight,
	                flags, mMainFrame);
			}
			UpdateTitle();
            mMainWindow.SetMinimumSize(GS!(480), GS!(360));
            mMainWindow.mIsMainWindow = true;
            mMainWindow.mOnWindowKeyDown.Add(new => SysKeyDown);
            mMainWindow.mOnWindowCloseQuery.Add(new => AllowClose);
            CreateMenu();
            UpdateRecentDisplayedFilesMenuItems();
            if (mRecentlyDisplayedFiles.Count > 0)
                ShowRecentFile(0);
			//mIPCHostManager = IPCHostManager.sIPCHostManager = new IPCHostManager();
			//mIPCHostManager.Init();                                                

			//TODO: Temporary
			//mBfResolveCompiler.StartTiming();

            mProjectPanel.RebuildUI();

			//mDebugger.LoadDebugVisualizers(scope String(mInstallDir, "BeefDbgVis.toml"));

			/*foreach (var project in mWorkspace.mProjects)
				project.Save();*/
            
            //OutputLine("IDE Started. Build 1.");
			//mProjectPanel.ShowProjectProperties(mWorkspace.mProjects[0]);                  

			if (mProcessAttachId != 0)
			{
				Process debugProcess = scope Process();
				switch (debugProcess.GetProcessById(mProcessAttachId))
				{
				case .Ok:
				case .Err:
					Fail(StackStringFormat!("Unable to locate process id {0}", mProcessAttachId));
				}
				if (debugProcess.IsAttached)
				{
					DebugManager.AttachFlags attachFlags = .None;
					String titleName = scope String();
					UpdateTitle(titleName);
					OutputLine("Attaching to process id {0}: {1}", mProcessAttachId, titleName);

#if BF_PLATFORM_WINDOWS
					if (!mProcessAttachHandle.IsInvalid)
						attachFlags |= .ShutdownOnExit;
#endif
					
					Attach(debugProcess, attachFlags);
#if BF_PLATFORM_WINDOWS
					if (!mProcessAttachHandle.IsInvalid)
					{
						Windows.SetEvent(mProcessAttachHandle);
						mTargetDidInitBreak = true;
						mTargetHadFirstBreak = true;
					}
#endif
				}
			}
			else if (mCrashDumpPath != null)
			{
				if (mDebugger.OpenMiniDump(mCrashDumpPath))
				{
					mDebugger.mIsRunning = true;
					mExecutionPaused = false; // Make this false so we can detect a Pause immediately
					mIsAttachPendingSourceShow = true;
				}
				else
				{
					Fail(StackStringFormat!("Failed to load minidump '{0}'", mCrashDumpPath));
				}
			}
			else if (mLaunchData != null)
			{
				LaunchDialog.DoLaunch(null, mLaunchData.mTargetPath, mLaunchData.mArgs ?? "", mLaunchData.mWorkingDir ?? "", "", mLaunchData.mPaused, true);
			}

			mInitialized = true;
			//Profiler.StopSampling();

			//if (IsMinidump)
				//ShowOutput();

			bool needSave = false;
			/*if (mWorkspace.mIsLegacyWorkspace)
			{
				mWorkspace.mIsLegacyWorkspace = false;
				mWorkspace.mHasChanged = true;
				needSave = true;
			}*/
			/*for (var project in mWorkspace.mProjects)
			{
				if (project.mIsLegacyProject)
				{
					int slashPos = project.mProjectPath.LastIndexOf('\\');
					if (slashPos != -1)
					{
						project.mProjectPath.RemoveToEnd(slashPos + 1);
						project.mProjectPath.Append("BeefProj.toml");
						project.mHasChanged = true;
						project.mIsLegacyProject = false;
						needSave = true;
					}
					//"BeefProj.toml"
				}
			}*/
			if (needSave)
				SaveAll();

			ShowPanel(mOutputPanel, false);
			UpdateRecentFileMenuItems();
        }
#endif

		void LoadConfig()
		{
			delete mBeefConfig;
			mBeefConfig = new BeefConfig();

			if (mWorkspace.IsSingleFileWorkspace)
			{
				var dir = scope String();
				Path.GetDirectoryPath(mWorkspace.mCompositeFile.FilePath, dir);
				mBeefConfig.QueuePaths(dir);
			}
			else
				mBeefConfig.QueuePaths(mWorkspace.mDir);
			mBeefConfig.QueuePaths(mInstallDir);
			mBeefConfig.Load().IgnoreError();
		}

		void RehupScale()
		{
			if (mInitialized)
				Font.ClearCache();

			if (mCodeFont == null)
				return;

			float fontSize = DarkTheme.sScale * mSettings.mEditorSettings.mFontSize;
			float tinyFontSize = fontSize * 8.0f/9.0f;

			bool isFirstFont = true;
			for (let fontName in mSettings.mEditorSettings.mFonts)
			{
				if (isFirstFont)
				{
					mTinyCodeFont.Dispose(true);
					isFirstFont = !mTinyCodeFont.Load(fontName, tinyFontSize);
					mCodeFont.Dispose(true);
					mCodeFont.Load(fontName, fontSize);
				}
				else
				{
					mTinyCodeFont.AddAlternate(fontName, tinyFontSize);
					mCodeFont.AddAlternate(fontName, fontSize);
				}
			}

			if (mCodeFont.GetHeight() == 0)
			{
				mTinyCodeFont.Load("fonts/SourceCodePro-Regular.ttf", tinyFontSize);
				mCodeFont.Load("fonts/SourceCodePro-Regular.ttf", fontSize);
			}

			// Do we have an extended font?
			if (mCodeFont.GetWidth('😊') == 0)
			{
				mCodeFont.AddAlternate("Segoe UI", fontSize);
				mCodeFont.AddAlternate("Segoe UI Symbol", fontSize);
				mCodeFont.AddAlternate("Segoe UI Historic", fontSize);
				mCodeFont.AddAlternate("Segoe UI Emoji", fontSize);

				mTinyCodeFont.AddAlternate("Segoe UI", tinyFontSize);
				mTinyCodeFont.AddAlternate("Segoe UI Symbol", tinyFontSize);
				mTinyCodeFont.AddAlternate("Segoe UI Historic", tinyFontSize);
				mTinyCodeFont.AddAlternate("Segoe UI Emoji", tinyFontSize);

				/*mCodeFont.AddAlternate(new String("fonts/segoeui.ttf"), fontSize);
				mCodeFont.AddAlternate(new String("fonts/seguisym.ttf"), fontSize);
				mCodeFont.AddAlternate(new String("fonts/seguihis.ttf"), fontSize);

				mTinyCodeFont.AddAlternate(new String("fonts/segoeui.ttf"), tinyFontSize);
				mTinyCodeFont.AddAlternate(new String("fonts/seguisym.ttf"), tinyFontSize);
				mTinyCodeFont.AddAlternate(new String("fonts/seguihis.ttf"), tinyFontSize);*/
			}

			//mTinyCodeFont.Load(scope String(BFApp.sApp.mInstallDir, "fonts/SourceCodePro-Regular.ttf"), fontSize);

			float squiggleScale = DarkTheme.sScale;
			if (squiggleScale < 3.0f)
				squiggleScale = (float)Math.Round(squiggleScale);
			mSquiggleImage.Scale(squiggleScale);
			mCircleImage.Scale(DarkTheme.sScale);
		}

        void HandleWindowClosed(BFWindow window)
        {
            if (mWindowDatas.GetAndRemove(window) case .Ok((?, var windowData)))
			{
				delete windowData;
			}
        }

        WindowData GetWindowData(BFWindow window)
        {
            WindowData windowData;
            mWindowDatas.TryGetValue(window, out windowData);
            if (windowData == null)
            {
                windowData = new WindowData();
                mWindowDatas[window] = windowData;
            }
            return windowData;
        }

        public DrawLayer GetOverlayLayer(BFWindow window)
        {
            WindowData windowData = GetWindowData(window);
            if (windowData.mOverlayLayer == null)
                windowData.mOverlayLayer = new DrawLayer(window);
            return windowData.mOverlayLayer;
        }

        void UpdateDrawTracking()
        {            
        }

		public enum EvalResult
		{
			Normal,
			Pending
		}

		public EvalResult DebugEvaluate(String expectedType, String expr, String outVal, int cursorPos = -1, DebugManager.Language language = .NotSet, DebugManager.EvalExpressionFlags expressionFlags = .None, Action<String> pendingHandler = null)
		{
			defer
			{
				if (mPendingDebugExprHandler != pendingHandler)
					delete pendingHandler;
			}

			if (expressionFlags != .None)
			{
				NOP!();
			}

			mIsImmediateDebugExprEval = false;

			String evalStr = scope String();
			evalStr.Append(expr);

			if (expectedType != null)
			{
				evalStr.Append(", expectedType=");
				evalStr.Append(expectedType);
			}

			if (mDebugger.IsPaused())
			{
				String curMethodOwner = scope String();
				int32 stackLanguage = 0;
                mDebugger.GetStackMethodOwner(mDebugger.mActiveCallStackIdx, curMethodOwner, out stackLanguage);
				if ((curMethodOwner.Length > 0) && (stackLanguage == 2) && (mWorkspace.mStartupProject != null)) // Beef only
				{
					String namespaceSearch = scope String();
					if (mBfResolveSystem != null)
					{
						mBfResolveSystem.Lock(2);
						BfProject startupProject = mBfResolveSystem.GetBfProject(mWorkspace.mStartupProject);
						mBfResolveSystem.GetNamespaceSearch(curMethodOwner, namespaceSearch, startupProject);
						mBfResolveSystem.Unlock();
					}
					if (!namespaceSearch.IsEmpty)
					{
						evalStr.Append(", namespaceSearch=\"");
						int namespaceIdx = 0;
						for (var namespaceEntry in namespaceSearch.Split('\n'))
						{
							if (namespaceIdx > 0)
								evalStr.Append(",");
							evalStr.Append(namespaceEntry);
							namespaceIdx++;
						}
						evalStr.Append("\"");
					}
				}
			}

			mDebugger.Evaluate(evalStr, outVal, cursorPos, (int)language, expressionFlags);

			if (outVal == "!pending")
			{
				Debug.Assert(mPendingDebugExprHandler == null);
				mPendingDebugExprHandler = pendingHandler;
				return .Pending;
			}

			return .Normal;
		}

        void DebuggerPaused()
        {
            mDebugger.mActiveCallStackIdx = 0;
            mExecutionPaused = true;
			mDebugger.GetRunState();
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.SetDisabled(false);
				});
            mMemoryPanel.SetDisabled(false);
            mCallStackPanel.SetDisabled(false);
        }

		void WithWatchPanels(delegate void(WatchPanel watchPanel) dlg)
		{
			dlg(mWatchPanel);
			dlg(mAutoWatchPanel);
			for (let window in mWindows)
			{
				if (let widgetWindow = window as WidgetWindow)
				{
					if (let quickWatch = widgetWindow.mRootWidget as QuickWatchDialog)
					{
						dlg(quickWatch.[Friend]mWatchPanel);
					}
				}
			}
		}

        void DebuggerUnpaused()
        {
			// Leave target in background for a bit, incase we immediately
			//  hit another breakpoint
            mDebugger.mActiveCallStackIdx = 0;
            mForegroundTargetCountdown = 30; 
            mExecutionPaused = false;
			WithWatchPanels(scope (watchPanel) =>
				{
					watchPanel.SetDisabled(true);
				});
            mMemoryPanel.SetDisabled(true);
            mCallStackPanel.SetDisabled(true);

			// If hoverwatch is up, we need to close it so we refresh the value
            var sourceViewPanel = GetActiveSourceViewPanel();
            if ((sourceViewPanel != null) && (sourceViewPanel.mHoverWatch != null))
            {
                sourceViewPanel.mHoverWatch.Close();                
            }
			mDebuggerContinueIdx++;
        }

        public void StepIntoSpecific(int addr)
        {
            if (mExecutionPaused)
            {
                DebuggerUnpaused();
                mDebugger.StepIntoSpecific(addr);
            }            
        }

		void FinishPendingHotResolve()
		{
			List<uint8> typeData = scope .();
			String stackListStr = scope .();

			if (!mDebugger.GetHotResolveData(typeData, stackListStr))
				return;
			
			mBfBuildCompiler.HotResolve_Start((mHotResolveState == .Pending) ? .None : .HadDataChanges);
			mHotResolveState = .None;

			for (int typeId < typeData.Count)
			{
				if (typeData[typeId] != 0)
					mBfBuildCompiler.HotResolve_ReportType(typeId, .Heap);
			}

			String stackStr = scope .();
			for (let stackStrView in stackListStr.Split('\n'))
			{
				stackStr.Set(stackStrView);

				if (stackStr.StartsWith("D "))
				{
					stackStr.Remove(0, 2);
					mBfBuildCompiler.HotResolve_AddDelegateMethod(stackStr);
				}
				else
					mBfBuildCompiler.HotResolve_AddActiveMethod(stackStr);
			}

			String hotResult = scope String();
			mBfBuildCompiler.HotResolve_Finish(hotResult);

			if (hotResult.IsEmpty)
			{
				//OutputLineSmart("Hot type data changes have been resolved");
				ProcessBeefCompileResults(null, false, mWorkspace.mStartupProject, null);
			}
			else
			{
				if (mHotResolveTryIdx == 0)
				{
					OutputLineSmart("ERROR: Hot type data changes cannot be applied because of the following types");
					for (var line in hotResult.Split('\n'))
					{
						OutputLineSmart("   {0}", line);
					}
					OutputLineSmart("Revert changes or put the program into a state where the type is not actively being used");

					var compileInstance = mWorkspace.mCompileInstanceList.Back;
					Debug.Assert(compileInstance.mCompileResult == .PendingHotLoad);
					compileInstance.mCompileResult = .Failure;
				}
			}

			if (mHotResolveState == .None)
			{
				var compileInstance = mWorkspace.mCompileInstanceList.Back;
				if (compileInstance.mCompileResult == .PendingHotLoad)
					compileInstance.mCompileResult = .Success;
			}
		}

		void QueueProjectItems(Project project)
		{
			project.WithProjectItems(scope (projectItem) =>
				{
				    var projectSource = projectItem as ProjectSource;
				    if (projectSource != null)
				    {
						if (projectSource.mIncludeKind == .Ignore)
							return;
				        var resolveCompiler = GetProjectCompilerForFile(projectSource.mPath);
				        if (resolveCompiler == mBfResolveCompiler)
				            resolveCompiler.QueueProjectSource(projectSource);
						projectSource.mHasChangedSinceLastCompile = true;
				    }
				});
		}

		void RemoveProjectItems(Project project)
		{
			project.WithProjectItems(scope (projectItem) =>
				{
				    var projectSource = projectItem as ProjectSource;
				    if (projectSource != null)
				    {
				        var resolveCompiler = GetProjectCompilerForFile(projectSource.mPath);
				        if (resolveCompiler == mBfResolveCompiler)
				            resolveCompiler.QueueProjectSourceRemoved(projectSource);

						if (IsBeefFile(projectSource.mPath))
						{
							mBfBuildCompiler.QueueProjectSourceRemoved(projectSource);
						}
				    }
				});
		}

        void UpdateCompilersAndDebugger()
        {
			scope AutoBeefPerf("UpdateCompilersAndDebugger");

			var msg = scope String();

            while (true)
            {
            	DebugManager.RunState runState = .NotStarted;

				if (mDebugger != null)
                {
                	mDebugger.Update();

					runState = mDebugger.GetRunState();
					mDebuggerPerformingTask = (runState == .DebugEval) || (runState == .DebugEval_Done) || (runState == .SearchingSymSrv);

	                if (mDebugAutoRun)
	                {
	                    if (runState == DebugManager.RunState.NotStarted)
	                    {
							mOutputPanel.Clear();

	                        if ((mExecutionQueue.Count == 0) && (!mDebugger.mIsRunning))
	                        {
	                            mTargetStartWithStep = true;
	                            var startDbgCmd = new StartDebugCmd();
								startDbgCmd.mWasCompiled = true;
	                            startDbgCmd.mOnlyIfNotFailed = true;
	                            mExecutionQueue.Add(startDbgCmd);
	                        }
	                    }
	                    else if (runState == DebugManager.RunState.Paused)
	                    {
							mDebugger.Continue();
	                        //mDebugger.Terminate();
	                    }
	                }

					if ((mPendingDebugExprHandler != null) && (runState != .DebugEval) && (runState != .SearchingSymSrv))
					{
						var evalResult = scope String();
						mDebugger.EvaluateContinue(evalResult);
						if (evalResult != "!pending")
						{
							mPendingDebugExprHandler(evalResult);
							DeleteAndNullify!(mPendingDebugExprHandler);
						}
					}

					if (mHotResolveState != .None)
					{
						FinishPendingHotResolve();
					}
	            }

                if (mDebugAutoBuild)
                {
                    if ((!mBfBuildCompiler.HasQueuedCommands()) && (mExecutionInstances.Count == 0) && (mExecutionQueue.Count == 0))
                    {
                        OutputLine("Autobuilding...");
                        CompileBeef(null, 0, mLastCompileSucceeded, let hadBeef);
                    }
                }

				bool didClean = false;
                if (mWantsBeefClean)
                {
                    if ((!mBfBuildCompiler.HasQueuedCommands()) &&
						((mBfResolveCompiler == null) || (!mBfResolveCompiler.HasQueuedCommands())))
                    {
						didClean = true;
                        OutputLine("Cleaned Beef.");

                        delete mBfResolveCompiler;
                        delete mBfResolveSystem;
						delete mBfResolveHelper;
                        delete mBfBuildCompiler;
                        delete mBfBuildSystem;
						
						///
                        mDebugger.FullReportMemory();

						var workspaceBuildDir = scope String();
						GetWorkspaceBuildDir(workspaceBuildDir);
						if (Directory.Exists(workspaceBuildDir))
						{
							var result = Utils.DelTree(workspaceBuildDir, scope (fileName) =>
                                {
									return fileName.EndsWith("build.dat");
                                });
						    if (result case .Err)
							{
								var str = scope String("Failed to delete folder: ", workspaceBuildDir);
								Fail(str);
							}
						}

						//ResolveConfigString(project, options, "$(TargetPath)", error, targetPath);

						if (!mNoResolve)
						{
	                        mBfResolveSystem = new BfSystem();
	                        mBfResolveCompiler = mBfResolveSystem.CreateCompiler(true);
	                        mBfResolveHelper = new BfResolveHelper();
						}

						mCompileSinceCleanCount = 0;
                        mBfBuildSystem = new BfSystem();
                        mBfBuildCompiler = mBfBuildSystem.CreateCompiler(false);
						mBfBuildCompiler.ClearBuildCache();

						/*foreach (var project in mWorkspace.mProjects)
						{
						    mBfBuildSystem.AddProject(project);
							if (mBfResolveSystem != null)
						    	mBfResolveSystem.AddProject(project);
						}
                        
                        foreach (var project in mWorkspace.mProjects)
                        {
                            project.WithProjectItems(scope (projectItem) =>
                                {
                                    var projectSource = projectItem as ProjectSource;
                                    if (projectSource != null)
                                    {                                        
                                        var resolveCompiler = GetProjectCompilerForFile(projectSource.mPath);
                                        if (resolveCompiler == mBfResolveCompiler)
                                            resolveCompiler.QueueProjectSource(projectSource);
                                    }
                                });
                        }
                        mBfResolveCompiler.QueueDeferredResolveAll();
                        CurrentWorkspaceConfigChanged();*/

                        mWantsBeefClean = false;
                    }
                }

                if (mWantsClean)
                {
                    if ((!mBfBuildCompiler.HasQueuedCommands()) && (!mBfResolveCompiler.HasQueuedCommands())
#if IDE_C_SUPPORT
                        && (!mDepClang.HasQueuedCommands()) && (!mResolveClang.HasQueuedCommands())
#endif
                        )
                    {                        
                        OutputLine("Cleaned.");

						let workspaceOptions = GetCurWorkspaceOptions();

						delete mBfResolveHelper;
                        delete mBfResolveCompiler;
                        delete mBfResolveSystem;
#if IDE_C_SUPPORT
                        delete mResolveClang;
#endif

                        delete mBfBuildCompiler;
                        delete mBfBuildSystem;
#if IDE_C_SUPPORT
                        delete mDepClang;
#endif
						//

                        CreateBfSystems();
						mBfBuildCompiler.ClearBuildCache();

                        var workspaceBuildDir = scope String();
                        GetWorkspaceBuildDir(workspaceBuildDir);
                        if (Directory.Exists(workspaceBuildDir))
                        {
							var result = Utils.DelTree(workspaceBuildDir);
                            if (result case .Err)
							{
								var str = scope String("Failed to delete folder: ", workspaceBuildDir);
								//result.Exception.ToString(str);
                                Fail(str);
								//result.Dispose();
							}
                        }

						List<String> deleteFails = scope .();

						for (var project in mWorkspace.mProjects)
						{
							// Force running the "if files changed" commands
							project.mForceCustomCommands = true;
							project.mNeedsTargetRebuild = true;

							// Don't delete custom build artifacts
							if (project.mGeneralOptions.mTargetType == .CustomBuild)
								continue;

							List<String> projectFiles = scope .();
							defer ClearAndDeleteItems(projectFiles);

							let options = GetCurProjectOptions(project);
							if (options == null)
								continue;
							GetTargetPaths(project, workspaceOptions, options, projectFiles);

							for (let filePath in projectFiles)
							{
								if (File.Delete(filePath) case .Err(let errVal))
								{
									if (File.Exists(filePath))
										deleteFails.Add(new String(filePath));
								}
							}
						}

						if (!deleteFails.IsEmpty)
						{
							String str = scope String();
							if (deleteFails.Count == 1)
								str.AppendF("Failed to delete file: {0}", deleteFails[0]);
							else
							{
								str.Append("Failed to delete files:");
								for (let fileName in deleteFails)
								{
									str.Append("\n  ", fileName);
								}
							}
							Fail(str);
						}

						ClearAndDeleteItems(deleteFails);

						didClean = true;
                        mWantsClean = false;
						mDbgCompileIdx = -1;
						mDbgHighestTime = default;
                    }
                }

				if (didClean)
				{
					for (var project in mWorkspace.mProjects)
					{
					    mBfBuildSystem.AddProject(project);
						if (mBfResolveSystem != null)
					    	mBfResolveSystem.AddProject(project);
					}

					if (mBfResolveSystem != null)
					{
						PreConfigureBeefSystem(mBfResolveSystem, mBfResolveCompiler);
						for (var project in mWorkspace.mProjects)
						{
							SetupBeefProjectSettings(mBfResolveSystem, mBfResolveCompiler, project);
						}
					}

					if (mBfResolveSystem != null)
					{
						for (var project in mWorkspace.mProjects)
						{
							if (IsProjectEnabled(project))
								QueueProjectItems(project);
						}
						mBfResolveCompiler.QueueDeferredResolveAll();
					}

					CurrentWorkspaceConfigChanged();
				}

                mBfBuildSystem.Update();
                mBfBuildCompiler.Update();
                while (true)
                {
#if CLI
					if (mCompilingBeef)
						break;
#endif					

					msg.Clear();
                    if (!mBfBuildCompiler.PopMessage(msg))
						break;
                    OutputLine(msg);					
                }

				if (mBfResolveSystem != null)
				{
	                mBfResolveSystem.Update();
	                mBfResolveCompiler.Update();
	                mBfResolveCompiler.ClearMessages();
	                mBfResolveHelper.Update();
				}

#if IDE_C_SUPPORT
                mDepClang.Update();
                if ((!IsCompiling) && (!mDepClang.IsPerformingBackgroundOperation()))
                {
					// Only leave mCompileWaitsForQueueEmpty false for a fast initial build
                    mDepClang.mCompileWaitsForQueueEmpty = true;
                }

                while (true)
                {
					msg.Clear();
                    if (!mDepClang.PopMessage(msg))
						break;
                    OutputLine(msg);
                }

                mResolveClang.Update();
                mResolveClang.ClearMessages();
#endif

                bool wantDebuggerDetach = false;
                if ((mDebugger != null) && (runState == DebugManager.RunState.Terminated))
                {
                    wantDebuggerDetach = true;                    
                }

				String deferredMsgType = scope String();
				String deferredOutput = scope String();

                bool hadMessages = false;
                while (mDebugger != null)
                {
					msg.Clear();
                    if (!mDebugger.PopMessage(msg))
					{
						break;
					}
                    hadMessages = true;                    
                    int paramIdx = msg.IndexOf(' ');
					String cmd = scope String();
					String param = scope String();

					if (paramIdx > 0)
					{
                    	cmd.Append(msg, 0, paramIdx);
                    	param.Append(msg, paramIdx + 1);
					}
					else
						cmd.Append(msg);
                    if ((cmd == "msg") || (cmd == "dbgEvalMsg") || (cmd == "log"))
                    {
						if (deferredMsgType != cmd)
						{
							if (deferredOutput.Length > 0)
							{
								OutputFormatted(deferredOutput, deferredMsgType == "dbgEvalMsg");
								deferredOutput.Clear();
							}

							deferredMsgType.Set(cmd);
						}

						
						deferredOutput.Append(param);
                    }
                    else if (cmd == "error")
                    {
						if ((mRunningTestScript) && (!IsCrashDump) && (!mScriptManager.IsErrorExpected(param)))
							mScriptManager.Fail(param);

						bool isFirstMsg = true;

						String tempStr = scope String();
						bool focusOutput = false;

						while (true)
						{
							String errorMsg = null;

							int infoPos = param.IndexOf("\x01");
							if (infoPos == 0)
							{
								Widget addWidget = null;
								LeakWidget leakWidget = null;

								int endPos = param.IndexOf("\n");
								if (endPos == -1)
									break;
								String leakStr = scope String(param, 1, endPos - 1);
								param.Remove(0, endPos + 1);
								int itemIdx = 0;
								for (var itemView in leakStr.Split('\t'))
								{
									var item = scope String(itemView);
									if (itemIdx == 0)
									{
										if (item == "LEAK")
										{
											focusOutput = true;
											leakWidget = new LeakWidget(mOutputPanel);
											leakWidget.Resize(0, GS!(-2), DarkTheme.sUnitSize, DarkTheme.sUnitSize);
											addWidget = leakWidget;
										}
										else if (item == "TEXT")
										{
											OutputLine(scope String(leakStr, @itemView.MatchPos + 1));
											break;
										}
										else
											break;
									}
									else if (itemIdx == 1)
									{
										leakWidget.mExpr = new String(item);
									}
									else
									{
										//leakWidget.mStackAddrs.Add()
									}
									itemIdx++;
								}

								if (addWidget != null)
									mOutputPanel.AddInlineWidget(addWidget);

							}
							else if (infoPos != -1)
							{
								tempStr.Clear();
								tempStr.Append(param, 0, infoPos);
								errorMsg = tempStr;
								param.Remove(0, infoPos);
							}
							else
								errorMsg = param;

							if (errorMsg != null)
							{
								if (isFirstMsg)
								{
									OutputLineSmart(scope String("ERROR: ", errorMsg));
									if (gApp.mRunningTestScript)
									{
										// The 'OutputLineSmart' would already call 'Fail' when running test scripts
									}
									else
									{
										Fail(errorMsg);
									}
									isFirstMsg = false;
								}
								else
									Output(errorMsg);
							}

							if (infoPos == -1)
								break;
						}

						if (focusOutput)
							ShowOutput();
                    }
                    else if (cmd == "memoryBreak")
                    {
                        Breakpoint breakpoint = null;
                        int memoryAddress = (int)int64.Parse(param, System.Globalization.NumberStyles.HexNumber);
                        for (var checkBreakpoint in mDebugger.mBreakpointList)
                        {
                            if (checkBreakpoint.mMemoryAddress == memoryAddress)
                                breakpoint = checkBreakpoint;
                        }
                        String infoString = StackStringFormat!("Memory breakpoint hit: '0x{0:X08}'", (int64)memoryAddress);
						if (breakpoint != null)
							infoString = StackStringFormat!("Memory breakpoint hit: '0x{0:X08}' ({1})", (int64)memoryAddress, breakpoint.mMemoryWatchExpression);
						OutputLine(infoString);
						if (!mRunningTestScript)
						{
	                        MessageDialog("Breakpoint Hit", infoString);
	                        Beep(MessageBeepType.Information);
						}
                    }
					else if (cmd == "newProfiler")
					{
						var profiler = mDebugger.PopProfiler();						
						//ShowProfilePanel();
						mProfilePanel.Add(profiler);
					}
					else if (cmd == "showProfiler")
					{
						ShowProfilePanel();
					}
					else if (cmd == "clearOutput")
					{
						mOutputPanel.Clear();
					}
					else if (cmd == "dbgInfoLoaded")
					{
						//Debug.WriteLine("dbgInfoLoaded {0}", param);
						mWantsRehupCallstack = true;
					}
					else if (cmd == "rehupLoc")
					{
						mWantsRehupCallstack = true;
					}
					else if (cmd == "modulesChanged")
					{
						mModulePanel.ModulesChanged();
					}
					else if (cmd == "symsrv")
					{
						mSymSrvStatus.Set(param);
					}
                    else
                    {
                        Runtime.FatalError("Invalid debugger message type");
                    }
                }

				if (deferredOutput.Length > 0)
				{
				    OutputFormatted(deferredOutput, deferredMsgType == "dbgEvalMsg");
				}

				/*if (hadMessages)                
					mNoDebugMessagesTick = 0;                
				else if (IDEApp.sApp.mIsUpdateBatchStart)
					mNoDebugMessagesTick++;
				if (mNoDebugMessagesTick < 10)
					wantDebuggerDetach = false; // Don't detach if we have messages*/
                
                if (wantDebuggerDetach)
                {
					OutputLine("Detached debugger");

					UpdateTitle();
                    var disassemblyPanel = TryGetDisassemblyPanel(false);
                    if (disassemblyPanel != null)
                        disassemblyPanel.Disable();
                    mDebugger.DisposeNativeBreakpoints();
                    mDebugger.Detach();
                    mDebugger.mIsRunning = false;
					mExecutionPaused = false;
					mHotResolveState = .None;
					mDebuggerPerformingTask = false;
                    mWorkspace.StoppedRunning();
                    mBreakpointPanel.BreakpointsChanged();
					mModulePanel.ModulesChanged();
					if (mPendingDebugExprHandler != null)
					{
						mPendingDebugExprHandler(null);
						DeleteAndNullify!(mPendingDebugExprHandler);
					}
					mMemoryPanel.SetDisabled(true);
					WithWatchPanels(scope (watchPanel) =>
						{
							watchPanel.SetDisabled(true);
						});
					mIsAttachPendingSourceShow = false;
                }

                if ((mDebugger != null) && (mDebugger.IsPaused()) && (!mDebugger.HasPendingDebugLoads()))
                {
                    mForegroundTargetCountdown = 0;
                    if (!mExecutionPaused)
                    {
                        if (!mTargetDidInitBreak)
                        {
                            mTargetDidInitBreak = true;
							bool wantsContinue = true;
                            if (mTargetStartWithStep)
                            {
								if ((mMainBreakpoint != null) && (!mMainBreakpoint.IsBound()))
								{
									BfLog.LogDbg("Deleting mMainBreakpoint 1\n");
									// Couldn't bind to main breakpoint, bind to entry point address
									 // "." is the magic symbol for "entry point"
									String[] tryNames = scope .("WinMain", ".");

									for (let name in tryNames)
									{
										mDebugger.DeleteBreakpoint(mMainBreakpoint);
										mMainBreakpoint = mDebugger.CreateSymbolBreakpoint("WinMain");
										if ((name == ".") || (mMainBreakpoint.IsBound()))
											break;
									}
								}

								wantsContinue = true;
                            }
                            else
                            {
                                if (mMainBreakpoint != null)
                                {
									if (mMainBreakpoint.IsActiveBreakpoint())
										wantsContinue = true;
									BfLog.LogDbg("Deleting mMainBreakpoint 2\n");
									mDebugger.DeleteBreakpoint(mMainBreakpoint);
                                    //mMainBreakpoint.Dispose();
                                    mMainBreakpoint = null;
                                }
                            }

							if (wantsContinue)
							{
								DebuggerUnpaused();
								mDebugger.Continue();
							}
                        }
                        else
                        {                            
                            if (mMainBreakpoint != null)
                            {
								BfLog.LogDbg("Deleting mMainBreakpoint 3\n");
                                mDebugger.DeleteBreakpoint(mMainBreakpoint);
                                mMainBreakpoint = null;

                                int addr;
                                String fileName = null;
                                mDebugger.UpdateCallStack();
                                var stackInfo = scope String();
                                mDebugger.GetStackFrameInfo(0, out addr, fileName, stackInfo);
                                if (stackInfo.Contains("Main"))
                                {
									// Okay, NOW we can do a "step into"
                                    if (!IsInDisassemblyMode())
                                        mDebugger.StepInto(false);
                                    return;
                                } 
                            }

                            if ((!HasModalDialogs()) && (!mRunningTestScript))
                                mMainWindow.SetForeground();

							if (mRunTimingProfileId != 0)
							{
								mRunTimingProfileId.Dispose();
								mRunTimingProfileId = 0;
							}

							BeefPerf.Event("Debugger Paused", "");
                            DebuggerPaused();
                            PCChanged();
							mBreakpointPanel.MarkStatsDirty();
                            mTargetHadFirstBreak = true;

                            if (mDebugger.GetRunState() == DebugManager.RunState.Exception)
                            {
                                String exceptionLine = scope String();
                                mDebugger.GetCurrentException(exceptionLine);
                                var exceptionData = String.StackSplit!(exceptionLine, '\n');

                                String exHeader = StackStringFormat!("Exception {0}", exceptionData[1]);
                                if (exceptionData.Count >= 3)
                                    exHeader = exceptionData[2];

                                String exString = StackStringFormat!("{0} at {1}", exHeader, exceptionData[0]);

                                OutputLine(exString);
								if (!IsCrashDump)
                                	Fail(exString);
                            }
                        }
                    }
                    break;
                }
                else if (mDebuggerPerformingTask)
				{
					break;
				}
				else
                {
                    if (mForegroundTargetCountdown > 0)
                    {
                        if ((--mForegroundTargetCountdown == 0) && (mDebugger.mIsRunning))
                            mDebugger.ForegroundTarget();
                    }

                    if ((mDebugger != null) && (mExecutionPaused) && (mDebugger.mIsRunning))
                        DebuggerUnpaused();
                    break;
                }
            }

			if ((mDebugger != null) && ((mDebugger.IsPaused()) || (!mDebugger.mIsRunning)) && (!IsCompiling))
			{
				if (mDebugger.mRunToCursorBreakpoint != null)
				{
					BfLog.LogDbg("Stopped. Clearing mRunToCursorBreakpoint\n");
					mDebugger.DeleteBreakpoint(mDebugger.mRunToCursorBreakpoint);
					mDebugger.mRunToCursorBreakpoint = null;
				}
			}

			if ((mDebugger != null) && (mExecutionPaused) && (mDebugger.IsPaused()))
			{
				if (mWantsRehupCallstack)
				{
					mDebugger.RehupCallstack();
					if (var deferredShowPCRequest = mDeferredUserRequest as DeferredShowPCLocation)
					{
						//Debug.WriteLine("Had DeferredShowPCLocation");

						var stackIdx = deferredShowPCRequest.mStackIdx;
						IDEApp.sApp.mDebugger.mActiveCallStackIdx = stackIdx;
						ClearDeferredUserRequest();
						ShowPCLocation(stackIdx, false, true);
						StackPositionChanged();
					}
					mCallStackPanel.MarkCallStackDirty();
					mThreadPanel.MarkThreadsDirty();
					mModulePanel.ModulesChanged();

					mWantsRehupCallstack = false;
				}
			}

			if (mBuildContext != null)
			{
				bool isCompiling = (!mExecutionInstances.IsEmpty) || (!mExecutionQueue.IsEmpty);
				if (mBuildContext.mScriptManager != null)
				{
					mBuildContext.mScriptManager.Update();
					if ((mBuildContext.mScriptManager.HasQueuedCommands) && (!mBuildContext.mScriptManager.mFailed))
						isCompiling = true;
				}

				if (!isCompiling)
				{
					DeleteAndNullify!(mBuildContext);
				}
			}
        }

        public void ShowPassOutput(BfPassInstance bfPassInstance)
        {
            while (true)
            {
                var outString = scope String();
                if (!bfPassInstance.PopOutString(outString))
					break;
                OutputLine(outString);
            }
        }

        /*public bool CheckMouseover(Widget checkWidget, int32 wantTicks, out Point mousePoint)
        {
            mousePoint = Point(Int32.MinValue, Int32.MinValue);
            if (checkWidget != mLastMouseWidget) 
                return false;
            checkWidget.RootToSelfTranslate(mLastRelMousePos.x, mLastRelMousePos.y, out mousePoint.x, out mousePoint.y);
            return mMouseStillTicks == wantTicks;
        }

		void LastMouseWidgetDeleted(Widget widget)
		{
			if (mLastMouseWidget == widget)
				mLastMouseWidget = null;
		}

		void SetLastMouseWidget(Widget newWidget)
		{
			if (mLastMouseWidget != null)
				mLastMouseWidget.mDeletedHandler.Remove(scope => LastMouseWidgetDeleted, true);
			mLastMouseWidget = newWidget;
			if (mLastMouseWidget != null)
				mLastMouseWidget.mDeletedHandler.Add(new => LastMouseWidgetDeleted);
		}

        void UpdateMouseover()
        {
			if (mMouseStillTicks != -1)
            	mMouseStillTicks++;

            Widget overWidget = null;
            int32 numOverWidgets = 0;
            foreach (var window in mWindows)
            {
                var widgetWindow = window as WidgetWindow;
                
                widgetWindow.RehupMouse(false);
                var windowOverWidget = widgetWindow.mCaptureWidget ?? widgetWindow.mOverWidget;
                if ((windowOverWidget != null) && (widgetWindow.mAlpha == 1.0f) && (widgetWindow.mCaptureWidget == null))
                {
                    overWidget = windowOverWidget;
                    numOverWidgets++;
                    if (overWidget != mLastMouseWidget)
                    {
						SetLastMouseWidget(overWidget);                        
                        mMouseStillTicks = -1;
                    }

					float actualX = widgetWindow.mClientX + widgetWindow.mMouseX;
					float actualY = widgetWindow.mClientY + widgetWindow.mMouseY;
                    if ((mLastAbsMousePos.x != actualX) || (mLastAbsMousePos.y != actualY))
                    {
                        mMouseStillTicks = 0;
                        mLastAbsMousePos.x = actualX;
                        mLastAbsMousePos.y = actualY;
                    }
					mLastRelMousePos.x = widgetWindow.mMouseX;
					mLastRelMousePos.y = widgetWindow.mMouseY;
                }
            }

            if (overWidget == null)
            {     
           		SetLastMouseWidget(null);
                mMouseStillTicks = -1;
            }

            if (numOverWidgets > 1)
            {
				//int a = 0;
            }

            Debug.Assert(numOverWidgets <= 1);                        
        }*/

		public void FileRenamed(ProjectFileItem projectFileItem, String oldPath, String newPath)
		{
			String newFileName = scope String();
			Path.GetFileName(newPath, newFileName);

			RenameEditData(oldPath, newPath);

			WithTabs(scope (tab) =>
			    {
			        var sourceViewPanel = tab.mContent as SourceViewPanel;
			        if (sourceViewPanel != null)
			        {
			            if (Path.Equals(sourceViewPanel.mFilePath, oldPath))
			            {
							var sourceViewTab = (IDEApp.SourceViewTab)tab;

			                //TODO: We might have to resize the label here?
			                //sourceViewPanel.mFilePath.Set(newPath);
							sourceViewPanel.PathChanged(newPath);
			                tab.Label = newFileName;
							tab.mWantWidth = sourceViewTab.GetWantWidth();
							//tab.mTabbedView.Resize();
			            }
			        }
			    });

			var recentFileList = mRecentlyDisplayedFiles;
			for (int32 recentFileIdx = 0; recentFileIdx < recentFileList.Count; recentFileIdx++)
			{
			    if (recentFileList[recentFileIdx] == oldPath)
			        recentFileList[recentFileIdx].Set(newPath);
			}
			UpdateRecentDisplayedFilesMenuItems();

			if (IsBeefFile(newPath) != IsBeefFile(oldPath))
			{
				if ((var projectSource = projectFileItem as ProjectSource) && (IsProjectSourceEnabled(projectSource)))
				{
					if (IsBeefFile(newPath))
					{
						mBfResolveCompiler.QueueProjectSource(projectSource);
						mBfBuildCompiler.QueueProjectSource(projectSource);
					}
					else
					{
						mBfResolveCompiler.QueueProjectSourceRemoved(projectSource);
						mBfBuildCompiler.QueueProjectSourceRemoved(projectSource);
					}
					mBfResolveCompiler.QueueDeferredResolveAll();
					mBfResolveCompiler.QueueRefreshViewCommand();
				}
			}
		}

		public void OnWatchedFileChanged(ProjectItem projectItem, WatcherChangeTypes changeType, String newPath)
		{
			ProjectListViewItem listViewItem;
			mProjectPanel.mProjectToListViewMap.TryGetValue(projectItem, out listViewItem);

			String newName = null;
			if (newPath != null)
			{
				newName = scope ::String();
				Path.GetFileName(newPath, newName);
			}

			if (changeType == .Renamed)
			{
				if (projectItem.mIncludeKind == .Auto)
				{
	                let projectFileItem = projectItem as ProjectFileItem;

					listViewItem.Label = newName;

					String oldPath = scope String();
					projectFileItem.GetFullImportPath(oldPath);
					projectFileItem.Rename(newName);

					FileRenamed(projectFileItem, oldPath, newPath);
					mProjectPanel.SortItem((ProjectListViewItem)listViewItem.mParentItem);
				}
			}
			else if (changeType == .Deleted)
			{
				if (projectItem.mIncludeKind == .Auto)
				{
					mProjectPanel.DoDeleteItem(listViewItem, null, true);
				}
			}
			else if (changeType == .FileCreated)
			{				
				let projectFolder = projectItem as ProjectFolder;
				if (projectFolder.IsAutoInclude())
				{
					if (!projectFolder.mChildMap.ContainsKey(newName))
					{
						let projectSource = new ProjectSource();
						Path.GetFileName(newPath, projectSource.mName);
						projectSource.mPath = new String(projectFolder.mPath);
						projectSource.mPath.Append(Path.DirectorySeparatorChar);
						projectSource.mPath.Append(projectSource.mName);
						projectSource.mProject = projectFolder.mProject;
						projectSource.mParentFolder = projectFolder;
						projectFolder.AddChild(projectSource);
						projectFolder.MarkAsUnsorted();

						mProjectPanel.AddProjectItem(projectSource);
						mProjectPanel.QueueSortItem(listViewItem);
					}
				}
			}
			else if (changeType == .DirectoryCreated)
			{
				let projectFolder = projectItem as ProjectFolder;
				if (projectFolder.IsAutoInclude())
				{
					if (!projectFolder.mChildMap.ContainsKey(newName))
					{
						let newFolder = new ProjectFolder();
						Path.GetFileName(newPath, newFolder.mName);
						newFolder.mPath = new String(projectFolder.mPath);
						newFolder.mPath.Append(Path.DirectorySeparatorChar);
						newFolder.mPath.Append(newFolder.mName);
						newFolder.mProject = projectFolder.mProject;
						newFolder.mParentFolder = projectFolder;
						projectFolder.AddChild(newFolder);
						projectFolder.MarkAsUnsorted();

						mProjectPanel.AddProjectItem(newFolder);
						mProjectPanel.QueueSortItem(listViewItem);

						newFolder.mAutoInclude = true;
						mProjectPanel.QueueRehupFolder(newFolder);
					}
				}
			}
			else if (changeType == .Failed)
			{
				if (let projectFolder = projectItem as ProjectFolder)
					mProjectPanel.QueueRehupFolder(projectFolder);
			}
		}

		void UpdateWorkspace()
		{
			mFileWatcher.Update();

			bool appHasFocus = false;
			for (var window in mWindows)
			    appHasFocus |= window.mHasFocus;

			if (mRunningTestScript)
				appHasFocus = true;

			// Is this enough to get the behavior we want?	
			if (!appHasFocus)
				return;

			if (mLastFileChangeId != mFileWatcher.mChangeId)
			{
				if ((mFileChangedDialog != null) && (!mFileChangedDialog.mClosed))
				{
					mFileChangedDialog.Rehup();
				}
				mLastFileChangeId = mFileWatcher.mChangeId;
			}

			var app = gApp;
			Debug.Assert(app != null);

			while (true)
			{
				var dep = mFileWatcher.PopChangedDependency();
				if (dep == null)
					break;
				var projectSourceDep = dep as ProjectSource;
				if (projectSourceDep != null)
				{
					// We process these projectSources directly from the filename below
				}
			}

			while (/*(appHasFocus) && */((mFileChangedDialog == null) || (mFileChangedDialog.mClosed)))
			{
				while (mExternalChangeDeferredOpen.Count > 0)
				{
					String filePath = mExternalChangeDeferredOpen.PopFront();
					ShowSourceFile(filePath);
					delete filePath;
				}

				var changedFile = mFileWatcher.PopChangedFile();
			    if (changedFile == null)
			    {
			        if (mFileChangedDialog != null)
			            mFileChangedDialog = null;
			        break;
			    }
				String fileName = changedFile.mPath;
				defer
				{
					delete changedFile;
				}

#if IDE_C_SUPPORT
				if ((IsClangSourceFile(fileName)) || (IsClangSourceHeader(fileName)))
				{
					mDepClang.mDoDependencyCheck = true;
				}
#endif

				FileChanged(fileName);
				var editData = GetEditData(fileName, false, false);
				if (editData == null)
					continue;

				//TODO: Check to see if this file is in the dependency list of the executing project
				mHaveSourcesChangedExternallySinceLastCompile = true;
				editData.SetSavedData(null, IdSpan());

				if (editData.mQueuedContent == null)
					editData.mQueuedContent = new String();
				editData.mQueuedContent.Clear();
				if (LoadTextFile(fileName, editData.mQueuedContent, false) case .Err(let err))
				{
					if (err case .FileOpenError(.SharingViolation))
					{
						// Put back on the list and try later
						mFileWatcher.AddChangedFile(changedFile);
						changedFile = null;
						break;
					}
					editData.mFileDeleted = true;
				}

				using (mMonitor.Enter())
				{
					for (var projectSource in editData.mProjectSources)
					{
						projectSource.HasChangedSinceLastCompile = true;
					}
				}

			    var sourceViewPanel = FindSourceViewPanel(fileName);
			    if (sourceViewPanel != null)
			    {
					FileChangedDialog.DialogKind dialogKind = File.Exists(fileName) ? .Changed : .Deleted;

					if ((sourceViewPanel.mEditData != null) && (sourceViewPanel.mEditData.mFileDeleted))
						sourceViewPanel.mEditData.IsFileDeleted(); // Rehup

			        if ((sourceViewPanel.HasUnsavedChanges()) || (dialogKind == .Deleted))
			        {
			            if ((mFileChangedDialog != null) && (mFileChangedDialog.mApplyAllResult[(int)dialogKind].HasValue))
			            {
			                bool closeAll = mFileChangedDialog.mApplyAllResult[(int)dialogKind].Value;
			                if (closeAll)
								gApp.CloseDocument(sourceViewPanel);
			                continue;
			            }
			            else
			            {
			                mFileChangedDialog = new FileChangedDialog();
			                mFileChangedDialog.Show(sourceViewPanel);
			            }
			            break;
			        }
			        else
			        {
			            sourceViewPanel.Reload();
						//FileChanged(fileName);
			        }
			    }                
				else
				{
					editData.Reload();
					FileChanged(editData);
				}
			}

			if (mWorkspace.IsSingleFileWorkspace)
			{
				if (mWorkspace.mCompositeFile.HasPendingSave)
					mWorkspace.mCompositeFile.Save();
			}
		}

		public void FileChanged(String path)
		{
			OpenFileInSolutionDialog.ClearWriteTime(path);
		}

		public void FileChanged(FileEditData editData)
		{
			if (!IsBeefFile(editData.mFilePath))
				return;

			using (mMonitor.Enter())
			{
				for (var projectSource in editData.mProjectSources)
				{
					if (mBfResolveCompiler != null)
					{
						mBfResolveCompiler.QueueProjectSource(projectSource);
						mBfResolveCompiler.QueueDeferredResolveAll();
						mBfResolveCompiler.QueueRefreshViewCommand();
					}
				}
			}
		}

#if !CLI
		void UpdateIPC()
		{						
			bool hasFocus = false;
			for (var window in mWindows)
			{
				if (window.mHasFocus)
					hasFocus = true;
			}
			bool gotFocus = hasFocus && !mIPCHadFocus;
			mIPCHadFocus = hasFocus;
			//hasFocus = true;

			if (gotFocus)
			{
				// Keep trying to create IPCHelper until we finally succeed
				if (mIPCHelper == null)
				{					
					mIPCHelper = new IPCHelper();
					if (!mIPCHelper.Init("BF_IDE"))
						DeleteAndNullify!(mIPCHelper);					
					else
					{
#if !DEBUG
						OutputLine("IPC: Hosting");//
#endif
					}
				}
			}						

			if (mIPCHelper != null)
			{
				mIPCHelper.Update();

				var message = mIPCHelper.PopMessage();
				if (message != null)
				{
					defer delete message;

					var msgParts = String.StackSplit!(message, '|');
					if (msgParts.Count >= 0)
					{
						String cmd = scope String(msgParts[0]);
						if (cmd == "ShowFile")
						{
							// These loads any file changes before we try to do a goto
							mMainWindow.SetForeground();
							UpdateWorkspace();

							String fileName = scope String(msgParts[1]);
							int32 lineNum = int32.Parse(scope String(msgParts[2])).GetValueOrDefault();
							var sourceViewPanel = ShowSourceFileLocation(fileName, -1, IDEApp.sApp.mWorkspace.GetHighestCompileIdx(), lineNum - 1, 0, LocatorType.Always, false);
							if (sourceViewPanel != null)
								sourceViewPanel.mWidgetWindow.SetForeground();
						}
						else if (cmd == "StopIPC")
						{
#if !DEBUG
							OutputLine("IPC: Stopping");
#endif
							delete mIPCHelper;
							mIPCHelper = null;
						}
					}
				}				
			}			
		}
#endif

		void DoLongUpdateCheck()
		{
			uint32 timeNow = Utils.GetTickCount();
			if (mLastLongUpdateCheck != 0)
			{
				int ticksDiff = (int32)(timeNow - mLastLongUpdateCheck);
				if (ticksDiff > 200)
				{
					int lastErrTicks = (int32)(timeNow - mLastLongUpdateCheckError);
					if (lastErrTicks > 2000)
					{
						// Show this profile!
						mLongUpdateProfileId.Dispose();
						mLongUpdateProfileId = Profiler.StartSampling("LongUpdate");
						mLastLongUpdateCheckError = timeNow;
					}
				}
			}

			Profiler.ClearSampling();
			mLastLongUpdateCheck = Utils.GetTickCount();
		}

		public void SetDeferredUserRequest(DeferredUserRequest deferredUserRequest)
		{
			//Debug.WriteLine("SetDeferredUserRequest");

			delete mDeferredUserRequest;
			mDeferredUserRequest = deferredUserRequest;
		}

		public void ClearDeferredUserRequest()
		{
			//Debug.WriteLine("ClearDeferredUserRequest");
			DeleteAndNullify!(mDeferredUserRequest);
		}

        public override void Update(bool batchStart)
        {
			scope AutoBeefPerf("IDEApp.Update");

			/*using (mWorkspace.mMonitor.Enter())
			{

			}*/

			if (mDbgFastUpdate)
			{
				RefreshRate = 240;
				MarkDirty();
			}
			else
			{
				RefreshRate = 60;
			}

			bool hasFocus = false;
			for (let window in mWindows)
			{
				hasFocus |= window.mHasFocus;
			}

			if (mEnableGCCollect)
			{
				/*if (hasFocus)
					GC.SetAutoCollectPeriod(10);
				else
					GC.SetAutoCollectPeriod(2000);*/
			}

			while (!mDeferredFails.IsEmpty)
			{
				var error = mDeferredFails.PopFront();
				Fail(error);
				delete error;
			}

			if (mStopping)
				return;

			if (batchStart)
			{
				//BFApp_CheckMemory();
			}

#if !CLI
			UpdateIPC();
#endif

			if (mRunTest)
			{
				if (mRunTestDelay > 0)
				{
					mRunTestDelay--;
				}
				else if (IsCompiling)
				{

				}
				else if (mDebugger.mIsRunning)
				{                            
				    mDebugger.Terminate();
					mRunTestDelay = 30;
				}
				else
				{
				    mTargetStartWithStep = false;
				    CompileAndRun();
					mRunTestDelay = 200;
				}
			}

			if (mProfilePanel != null)
				mProfilePanel.ForcedUpdate();
			if (mLongUpdateProfileId != 0)
				DoLongUpdateCheck();

            if (mWakaTime != null)
                mWakaTime.Update();
			if (mFindResultsPanel != null)
            	mFindResultsPanel.Update();
			if (mBfResolveSystem != null)
            	mBfResolveSystem.PerfZoneStart("IDEApp.Update");
            UpdateWorkspace();
			if (ThemeFactory.mDefault != null)
            	ThemeFactory.mDefault.Update();
            base.Update(batchStart);

            DarkTooltipManager.UpdateTooltip();
            mGlobalUndoManager.Update();
            
            for (var project in mWorkspace.mProjects)
                project.Update();
            UpdateCompilersAndDebugger();
			if (mScriptManager != null)
				mScriptManager.Update();

			if (mTestManager != null)
			{
				mTestManager.Update();
				if (mTestManager.mIsDone)
				{
					if (mMainFrame != null)
						mMainFrame.mStatusBar.SelectConfig(mTestManager.mPrevConfigName);
					DeleteAndNullify!(mTestManager);
				}
			}
			
            UpdateExecution();
			if (mBfResolveSystem != null)
            	mBfResolveSystem.PerfZoneEnd();            

			void DoDeferredOpen(DeferredOpenKind openKind)
			{
				switch (openKind)
				{
				case .Workspace:
					DoOpenWorkspace();
				case .CrashDump:
					DoOpenCrashDump();
				default:
				}
			}

			var deferredOpen = mDeferredOpen;
			mDeferredOpen = .None;
			switch (deferredOpen)
			{
			case .File:
				DoOpenFile();
			case .CrashDump:
				DoOpenCrashDump();
			case .Workspace:

				if (mDeferredOpenFileName == null)
				{
					DoDeferredOpen(_);
					break;
				}

				if (CheckCloseWorkspace(mMainWindow,
					new () =>
					{
						DoDeferredOpen(_);
					},
					new () =>
					{
						DoDeferredOpen(_);
					},
					new () =>
					{
						DeleteAndNullify!(mDeferredOpenFileName);
					}
					))
				{
					DoDeferredOpen(_);
				}
			case .DebugSession:
				DoOpenDebugSession();
			default:
			}

            mHaveSourcesChangedInternallySinceLastCompile = false;
            WithSourceViewPanels(scope (sourceViewPanel) =>
                {
                    mHaveSourcesChangedInternallySinceLastCompile |= sourceViewPanel.mHasChangedSinceLastCompile;
                });

            if ((mCloseDelay > 0) && (--mCloseDelay == 0))
                mMainWindow.Close();

			ProcessIdleDeferredDeletes();

			/*if ((mUpdateCnt % (60 * 3) == 0) && (mEnableGCCollect))
			{
				scope AutoBeefPerf("GC.Collect");
				GC.Collect();
			}*/

			if (mDebugAutoShutdownCounter > 0)
			{
				if (--mDebugAutoShutdownCounter == 0)
					Stop();
			}

			if ((mRunningTestScript) && (!IsCompiling) && (!AreTestsRunning()))
			{
				if ((mFailed) || (!mScriptManager.HasQueuedCommands))
				{
					if (++mScriptManager.mDoneTicks >= 2)
					{
						OutputLine("Total script time: {0}ms", mScriptManager.mTimeoutStopwatch.ElapsedMilliseconds);

						if (mExitWhenTestScriptDone)
							Stop();
						else
							mRunningTestScript = false;
					}
				}
			}

			for (var sourceViewPanel in mPostRemoveUpdatePanels)
			{
				if (sourceViewPanel.NeedsPostRemoveUpdate)
				{
					sourceViewPanel.Update();
				}
				else
				{
					//Debug.WriteLine("Removing sourceViewPanel from mPostRemoveUpdatePanel {0} from IDEApp.Update", sourceViewPanel);
					sourceViewPanel.[Friend]mInPostRemoveUpdatePanels = false;
					@sourceViewPanel.Remove();
				}
			}

			if (IDEApp.sApp.mSpellChecker != null)
				IDEApp.sApp.mSpellChecker.CheckThreadDone();

			///
			//TODO: REMOVE
			//mDebugger.InitiateHotResolve(.ActiveMethods | .Allocations);
        }

        public override void Draw()
        {
			scope AutoBeefPerf("IDEApp.Draw");
#if CLI
			return;
#endif
#unwarn
			if (mBfResolveSystem != null)
			{
	            mBfResolveSystem.PerfZoneStart("IDEApp.Draw");
	            base.Draw();
	            mBfResolveSystem.PerfZoneEnd();
	            if (mBfResolveSystem.mIsTiming)
	            {
	                mBfResolveSystem.StopTiming();
	                mBfResolveSystem.DbgPrintTimings();
	            }
			}
			else
				base.Draw();
        }

        public void DrawSquiggle(Graphics g, float x, float y, float width)
        {
			Image squiggleImage = IDEApp.sApp.mSquiggleImage;
			int32 segSize = 30;
			float height = mSquiggleImage.mHeight;
            //int32 segSize = GS!(6 * 5);

            float curX = x;
            float curWidth = width;

            while (curWidth > 0)
            {
                float drawWidth = Math.Min(curWidth, segSize - (curX % segSize));

                float u1 = ((int32)curX % segSize) / (float)squiggleImage.mSrcWidth;
                float u2 = u1 + drawWidth / (float)squiggleImage.mSrcWidth;
                
                g.DrawQuad(squiggleImage, curX, y + (int)GS!(15), u1, 0, drawWidth, height, u2, 1.0f);

                curWidth -= drawWidth;
                curX += drawWidth;
            }
        }

        public enum MessageBeepType
        {
            Default = -1,
            Ok = 0x00000000,
            Error = 0x00000010,
            Question = 0x00000020,
            Warning = 0x00000030,
            Information = 0x00000040,
        }
        	
        public static void Beep(MessageBeepType type)
        {
#if BF_PLATFORM_WINDOWS
        	MessageBeep(type);
#endif        	
        }

#if BF_PLATFORM_WINDOWS
        [Import("user32.lib"), CLink, StdCall]
        public static extern bool MessageBeep(MessageBeepType type);
#endif
    }

	static
	{
		public static IDEApp gApp;

		/*public static mixin GS(int val)
		{
			(int)(val * DarkTheme.sScale)
		}

		public static mixin GS(float val)
		{
			float fVal = val * DarkTheme.sScale;
			fVal
		}*/
	}
}
