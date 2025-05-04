using System;
using System.Security.Cryptography;
using System.Text;
using System.Collections;
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
using Beefy.sys;
using Beefy.events;
using Beefy.geom;
using Beefy.res;
using System.Diagnostics;
using Beefy.utils;
using IDE.Debugger;
using IDE.Compiler;
using IDE.Util;
using IDE.ui;
using IDE.util;

[AttributeUsage(.Method, .ReflectAttribute | .AlwaysIncludeTarget, ReflectUser = .All)]
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
		Default,
		Quiet,
		Minimal,
		Normal,
		Detailed,
		Diagnostic
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
		Test,
		Run,
		Update,
		GetVersion,
		CleanCache
	}

	enum HotResolveState
	{
		None,
		Pending,
		PendingWithDataChanges
	}

	public class IDETabbedView : DarkTabbedView
	{
		public this(SharedData sharedData) : base(sharedData)
		{
			if (sharedData == null)
			{
				mSharedData.mOpenNewWindowDelegate.Add(new (fromTabbedView, newWindow) => gApp.SetupNewWindow(newWindow, true));
				mSharedData.mTabbedViewClosed.Add(new (tabbedView) =>
					{
						if (tabbedView == gApp.mActiveDocumentsTabbedView)
							gApp.mActiveDocumentsTabbedView = null;
					});
			}
		}

		public ~this()
		{
			if (gApp.mActiveDocumentsTabbedView == this)
				gApp.mActiveDocumentsTabbedView = null;
		}

		public override TabbedView CreateTabbedView(SharedData sharedData)
		{
			IDETabbedView tabbedView = new IDETabbedView(sharedData);
			return tabbedView;
		}
	}

	public class IDEApp : BFApp
	{
		public static String sRTVersionStr = "042";
		public const String cVersion = "0.43.6";

#if BF_PLATFORM_WINDOWS
		public static readonly String sPlatform64Name = "Win64";
		public static readonly String sPlatform32Name = "Win32";
		#elif BF_PLATFORM_LINUX
		public static readonly String sPlatform64Name = "Linux64";
		public static readonly String sPlatform32Name = "Linux32";
		#elif BF_PLATFORM_MACOS
		public static readonly String sPlatform64Name = "macOS";
		public static readonly String sPlatform32Name = null;
#else
		public static readonly String sPlatform64Name = "Unknown64";
		public static readonly String sPlatform32Name = "Unknown32";
#endif

		public static bool sExitTest;

		public Verbosity mVerbosity = .Default;
		public BeefVerb mVerb;
		public bool mDbgCompileDump;
		public int mDbgCompileIdx = -1;
		public String mDbgCompileDir ~ delete _;
		public String mDbgVersionedCompileDir ~ delete _;
		public DateTime mDbgHighestTime;
		public bool mForceFirstRun;
		public bool mIsFirstRun;
		public bool mSafeMode;
		public String mDeferredRelaunchCmd ~ delete _;
		public int? mTargetExitCode;
		public FileVersionInfo mVersionInfo ~ delete _;

		//public ToolboxPanel mToolboxPanel;
		public Monitor mMonitor = new Monitor() ~ delete _;
		public Thread mMainThread;
		public PropertiesPanel mPropertiesPanel;
		public Font mTinyCodeFont ~ delete _;
		public Font mCodeFont ~ delete _;
		public Font mTermFont ~ delete _;
		protected bool mInitialized;
		public bool mConfig_NoIR;
		public bool mFailed;
		public bool mLastTestFailed;
		public bool mLastCompileFailed;
		public bool mLastCompileSucceeded;
		public bool mLastCompileHadMessages;
		public bool mPauseOnExit;
		public bool mDbgDelayedAutocomplete;
		public bool mDbgTimeAutocomplete;
		public bool mDbgPerfAutocomplete;
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
		public GitManager mGitManager = new .() ~ delete _;

		public WidgetWindow mPopupWindow;
		public RecentFileSelector mRecentFileSelector;

		public IDETabbedView mActiveDocumentsTabbedView;
		public static new IDEApp sApp;

		public Image mTransparencyGridImage ~ delete _;
		public Image mSquiggleImage ~ delete _;
		public Image mCircleImage ~ delete _;
		public bool mWantShowOutput;

		public OutputPanel mOutputPanel;
#if BF_PLATFORM_WINDOWS
		public TerminalPanel mTerminalPanel;
		public ConsolePanel mConsolePanel;
#endif
		public ImmediatePanel mImmediatePanel;
		public FindResultsPanel mFindResultsPanel;
		public WatchPanel mAutoWatchPanel;
		public WatchPanel mWatchPanel;
		public MemoryPanel mMemoryPanel;
		public CallStackPanel mCallStackPanel;
		public ErrorsPanel mErrorsPanel;
		public BreakpointPanel mBreakpointPanel;
		public DiagnosticsPanel mDiagnosticsPanel;
		public ModulePanel mModulePanel;
		public ThreadPanel mThreadPanel;
		public ProfilePanel mProfilePanel;
		public ProjectPanel mProjectPanel;
		public ClassViewPanel mClassViewPanel;
		public Widget mLastActivePanel;
		public SourceViewPanel mLastActiveSourceViewPanel;
		public AutoCompletePanel mAutoCompletePanel;
		public BookmarksPanel mBookmarksPanel;

		public Rect mRequestedWindowRect = Rect(64, 64, 1200, 1024);
		public BFWindow.ShowKind mRequestedShowKind;
		public WakaTime mWakaTime ~ delete _;

		public PackMan mPackMan = new PackMan() ~ delete _;
		public HashSet<String> mWantUpdateVersionLocks ~ DeleteContainerAndItems!(_);
		public Settings mSettings = new Settings() ~ delete _;
		public Workspace mWorkspace = new Workspace() ~ delete _;
		public FileWatcher mFileWatcher = new FileWatcher() ~ delete _;
#if !CLI
		public FileRecovery mFileRecovery = new FileRecovery() ~ delete _;
#endif
		public int mLastFileChangeId;
		public bool mHaveSourcesChangedInternallySinceLastCompile;
		public bool mHaveSourcesChangedExternallySinceLastCompile;

		public String mConfigName = new String() ~ delete _;
		public String mPlatformName = new String() ~ delete _;

		public String mSetConfigName ~ delete _;
		public String mSetPlatformName ~ delete _;

		public Targets mTargets = new Targets() ~ delete _;
		public DebugManager mDebugger ~ delete _;
		public String mSymSrvStatus = new String() ~ delete _;
		public delegate void(String) mPendingDebugExprHandler = null;
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
		public bool mDeterministic = false;
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
		public bool mStartedWithTestScript;
		public bool mExitWhenTestScriptDone = true;
		public ScriptManager mScriptManager = new ScriptManager() ~ delete _;
		public TestManager mTestManager ~ delete _;
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
		public bool mAppHasFocus;
		public int32 mCloseDelay;
		public FileChangedDialog mFileChangedDialog;
		public FindAndReplaceDialog mFindAndReplaceDialog;
		public LaunchDialog mLaunchDialog;
		public SymbolReferenceHelper mSymbolReferenceHelper;
		public WrappedMenuValue mViewWhiteSpace = .(false);
		public bool mEnableGCCollect = true;
		public bool mDbgFastUpdate;
		public bool mTestEnableConsole = false;
		public bool mTestBreakOnFailure = true;
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

		Monitor mDebugOutputMonitor = new .() ~ delete _;
		String mDebugOutput = new .() ~ delete _;

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
			public String mTargetPath ~ delete _;
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
			NewWorkspace,
			NewWorkspaceOrProject,
			CrashDump,
			DebugSession
		}
		DeferredOpenKind mDeferredOpen;
		String mDeferredOpenFileName;

		public class ExecutionCmd
		{
			public bool mOnlyIfNotFailed;
		}

		public class WriteEmitCmd : ExecutionCmd
		{
			public String mProjectName ~ delete _;
			public String mPath ~ delete _;
		}

		public class EmsdkInstalledCmd : ExecutionCmd
		{
			public String mPath ~ delete _;
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
			public BfPassInstance mBfPassInstance ~ delete _;
			public CompileKind mCompileKind;
			public Project mHotProject;
			public Stopwatch mStopwatch ~ delete _;
			public Profiler mProfiler;
			public ProfileCmd mProfileCmd ~ delete _;
			public bool mHadBeef;

			public ~this()
			{
			}
		}

		class ProfileCmd : ExecutionCmd
		{
			public int mThreadId;
			public String mDesc ~ delete _;
			public int mSampleRate;
		}

		class OpenDebugConsoleCmd : ExecutionCmd
		{
		}

		class StartDebugCmd : ExecutionCmd
		{
			public bool mConnectedToConsole;
			public int32 mConsoleProcessId;
			public bool mWasCompiled;
			public bool mHotCompileEnabled;

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

			public ~this()
			{
			}
		}

		public class ScriptCmd : ExecutionCmd
		{
			public String mCmd ~ delete _;
			public String mPath ~ delete _;
		}

		public enum ArgsFileKind
		{
			None,
			UTF8,
			UTF16WithBom
		}

		public class ExecutionQueueCmd : ExecutionCmd
		{
			public String mReference ~ delete _;
			public String mFileName ~ delete _;
			public String mArgs  ~ delete _;
			public String mWorkingDir  ~ delete _;
			public Dictionary<String, String> mEnvVars ~ DeleteDictionaryAndKeysAndValues!(_);
			public ArgsFileKind mUseArgsFile;
			public int32 mParallelGroup = -1;
			public bool mIsTargetRun;
			public String mStdInData ~ delete _;
			public RunFlags mRunFlags;
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
			public delegate void(Task<String>) mOnReadTaskComplete /*~ delete _*/;
			public Task<String> mErrorTask /*~ delete _*/;
			public delegate void(Task<String>) mOnErrorTaskComplete /*~ delete _*/;
			public Monitor mMonitor = new Monitor() ~ delete _;
			public String mStdInData ~ delete _;

			public Thread mOutputThread;
			public Thread mErrorThread;
			public Thread mInputThread;

			public int? mExitCode;
			public bool mAutoDelete = true;
			public bool mCanceled;
			public bool mIsTargetRun;
			public bool mDone;
			public bool mSmartOutput;

			public ~this()
			{
				delete mProcess;
				/*if (mProcess != null)
					mProcess.Close();*/

				if (mInputThread != null)
					mInputThread.Join();
				delete mInputThread;

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

			public void Release()
			{
				if (!mDone)
					mAutoDelete = true;
				else
					delete this;
			}
		}
		List<ExecutionInstance> mExecutionInstances = new List<ExecutionInstance>() ~ DeleteContainerAndItems!(_);
		public int32 mHotIndex = 0;
		private int32 mStepCount;
		private int32 mNoDebugMessagesTick;

		public class DeferredShowSource
		{
			public String mFilePath ~ delete _;
			public int32 mShowHotIdx;
			public int32 mRefHotIdx;
			public int32 mLine;
			public int32 mColumn;
			public LocatorType mHilitePosition;
			public bool mShowTemp;
		}
		public DeferredShowSource mDeferredShowSource ~ delete _;

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
				GC.SetCollectFreeThreshold(mEnableGCCollect ? 64 * 1024 * 1024 : -1);
			}
		}

		public bool MenuHasFocus
		{
			get
			{
				for (var window in mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if ((widgetWindow != null) && (widgetWindow.mHasFocus))
					{
						if (widgetWindow.mRootWidget is MenuContainer)
							return true;
					}
				}
				return false;
			}
		}

		public Workspace.PlatformType CurrentPlatform
		{
			get
			{
				Workspace.Options workspaceOptions = GetCurWorkspaceOptions();
				return Workspace.PlatformType.GetFromName(mPlatformName, workspaceOptions.mTargetTriple);
			}
		}

		[CallingConvention(.Stdcall), CLink]
		static extern void IDEHelper_ProgramStart();
		[CallingConvention(.Stdcall), CLink]
		static extern void IDEHelper_ProgramDone();

		public this()
		{
			ThreadPool.MaxStackSize = 8 * 1024 * 1024;

			sApp = this;
			gApp = this;
			mMainThread = Thread.CurrentThread;

			IDEHelper_ProgramStart();

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
			if (mBfBuildCompiler != null)
				compiler.Add(mBfBuildCompiler);
			if (mBfResolveCompiler != null)
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
			if (!mStartedWithTestScript && !mForceFirstRun)
			{
				mSettings.Save();
				SaveDefaultLayoutData();
			}
			mFileRecovery.WorkspaceClosed();
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
#if BF_PLATFORM_WINDOWS
			RemoveAndDelete!(mTerminalPanel);
			RemoveAndDelete!(mConsolePanel);
#endif
			RemoveAndDelete!(mImmediatePanel);
			RemoveAndDelete!(mFindResultsPanel);
			RemoveAndDelete!(mAutoWatchPanel);
			RemoveAndDelete!(mWatchPanel);
			RemoveAndDelete!(mMemoryPanel);
			RemoveAndDelete!(mCallStackPanel);
			RemoveAndDelete!(mBreakpointPanel);
			RemoveAndDelete!(mDiagnosticsPanel);
			RemoveAndDelete!(mModulePanel);
			RemoveAndDelete!(mThreadPanel);
			RemoveAndDelete!(mProfilePanel);
			RemoveAndDelete!(mPropertiesPanel);
			RemoveAndDelete!(mAutoCompletePanel);
			RemoveAndDelete!(mBookmarksPanel);

			if (mSymbolReferenceHelper != null)
				mSymbolReferenceHelper.Close();

			if (mBfBuildCompiler != null)
			{
				mBfBuildCompiler.RequestFastFinish();
				mBfBuildCompiler.CancelBackground();
			}
			if (mBfResolveCompiler != null)
			{
				mBfResolveCompiler.RequestFastFinish();
				mBfResolveCompiler.CancelBackground();
			}

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

			if (mErrorsPanel?.mParent != null)
				mErrorsPanel.RemoveSelf();

			ProcessIdleDeferredDeletes();

			// Get rid of panels
			ProcessDeferredDeletes();

			delete mBfBuildCompiler;
			delete mBfResolveCompiler;
#if IDE_C_SUPPORT
			delete mResolveClang;
#endif
			delete mSpellChecker;

			//NOTE: this must be done after the resolve compiler has been destroyed
			RemoveAndDelete!(mErrorsPanel);

			// Clear these out, for delete ordering purposes
			ProcessDeferredDeletes();

			if (mDeferredRelaunchCmd != null)
			{
				ProcessStartInfo psi = scope ProcessStartInfo();
				psi.SetFileNameAndArguments(mDeferredRelaunchCmd);
				psi.UseShellExecute = false;

				SpawnedProcess process = scope SpawnedProcess();
				process.Start(psi).IgnoreError();
			}
		}

		public bool IsCrashDump
		{
			get
			{
				return mCrashDumpPath != null;
			}
		}

		void WithStandardPanels(delegate void(Panel panel) dlg)
		{
			dlg(mProjectPanel);
			dlg(mClassViewPanel);
			dlg(mOutputPanel);
#if BF_PLATFORM_WINDOWS
			dlg(mTerminalPanel);
			dlg(mConsolePanel);
#endif
			dlg(mImmediatePanel);
			dlg(mFindResultsPanel);
			dlg(mAutoWatchPanel);
			dlg(mWatchPanel);
			dlg(mMemoryPanel);
			dlg(mCallStackPanel);
			dlg(mErrorsPanel);
			dlg(mBreakpointPanel);
			dlg(mDiagnosticsPanel);
			dlg(mModulePanel);
			dlg(mThreadPanel);
			dlg(mProfilePanel);
			dlg(mPropertiesPanel);
			dlg(mAutoCompletePanel);
			dlg(mBookmarksPanel);
		}

		public override void ShutdownCompleted()
		{
			base.ShutdownCompleted();
		}

		public override void Shutdown()
		{
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

		public void Cmd_ViewNew()
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
			if (mDeferredOpenFileName != null)
			{
				for (let filePath in mDeferredOpenFileName.Split('|'))
				{
					AddRecentFile(.OpenedFile, filePath);
					ShowSourceFile(scope String()..Reference(filePath));
				}
				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}

			String fullDir = scope .();
			let sourceViewPanel = GetActiveSourceViewPanel();
			if (sourceViewPanel != null)
			{
				if (sourceViewPanel.mFilePath != null)
					Path.GetDirectoryPath(sourceViewPanel.mFilePath, fullDir);
			}
			else if ((gApp.mDebugger.mRunningPath != null) && (!mWorkspace.IsInitialized))
			{
				Path.GetDirectoryPath(gApp.mDebugger.mRunningPath, fullDir);
			}

			var fileDialog = scope OpenFileDialog();
			fileDialog.Title = "Open File";
			fileDialog.SetFilter("All files (*.*)|*.*");
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

		public void OpenWorkspace(StringView openFileName)
		{
			CloseWorkspace();

			mWorkspace.mDir = new String();
			Path.GetDirectoryPath(openFileName, mWorkspace.mDir);
			mWorkspace.mName = new String();
			Path.GetFileName(mWorkspace.mDir, mWorkspace.mName);

			LoadWorkspace(.OpenOrNew);
			FinishShowingNewWorkspace();
		}

		public void DoOpenWorkspace(bool isCreating = false)
		{
#if !CLI
			if (mDeferredOpenFileName != null)
			{
				String prevFilePath = scope .(mDeferredOpenFileName);
				mDeferredOpenFileName.Clear();
				Path.GetActualPathName(prevFilePath, mDeferredOpenFileName);
				OpenWorkspace(mDeferredOpenFileName);
				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}

			var folderDialog = scope FolderBrowserDialog(isCreating ? .Save : .Open);
			var initialDir = scope String();
			if (mInstallDir.Length > 0)
				Path.GetDirectoryPath(.(mInstallDir, 0, mInstallDir.Length - 1), initialDir);
			initialDir.Concat(Path.DirectorySeparatorChar, "Samples");
			folderDialog.SelectedPath = initialDir;
			//folderDialog.
			if (folderDialog.ShowDialog(GetActiveWindow()).GetValueOrDefault() == .OK)
			{
				var selectedPath = scope String()..AppendF(folderDialog.SelectedPath);
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
					mDeferredOpen = .DebugSession;
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

			CheckDebugVisualizers();
			if (mDebugger.OpenMiniDump(mCrashDumpPath))
			{
				mDebugger.mIsRunning = true;
				mExecutionPaused = false; // Make this false so we can detect a Pause immediately
			}
			else
			{
				Fail(scope String()..AppendF("Failed to load minidump '{0}'", mCrashDumpPath));
				DeleteAndNullify!(mCrashDumpPath);
			}
		}

		public void DoOpenCrashDump()
		{
#if !CLI
			if (mDeferredOpenFileName != null)
			{
				OpenCrashDump(mDeferredOpenFileName);
				DeleteAndNullify!(mDeferredOpenFileName);
				return;
			}

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

				StringView loc = cmds[2];

				int line = -1;
				int lineChar = -1;

				int colonIdx = loc.IndexOf(':');
				if (colonIdx != -1)
				{
					line = int.Parse(loc.Substring(0, colonIdx)).GetValueOrDefault();
					lineChar = int.Parse(loc.Substring(colonIdx + 1)).GetValueOrDefault();
				}
				else
				{
					int32 charIdx = int32.Parse(loc).GetValueOrDefault();
					var fileEditData = GetEditData(cmds[1]);
					fileEditData?.mEditWidget?.mEditWidgetContent.GetLineCharAtIdx(charIdx, out line, out lineChar);
				}
				ShowSourceFileLocation(cmds[1], -1, -1, line, lineChar, .Smart, true);
			case "ShowCodeAddr":
				var sourceViewPanel = GetActiveSourceViewPanel(true);
				if (sourceViewPanel != null)
					sourceViewPanel.RecordHistoryLocation();

				int64 addr = int64.Parse(cmds[1], System.Globalization.NumberStyles.HexNumber).GetValueOrDefault();
				int64 inlineCallAddr = 0;
				if (cmds.Count > 2)
					inlineCallAddr = int64.Parse(cmds[2], System.Globalization.NumberStyles.HexNumber).GetValueOrDefault();

				String fileName = scope String();
				int lineNum = 0;
				int column;
				int hotIdx;
				int defLineStart;
				int defLineEnd;
				mDebugger.GetCodeAddrInfo((int)addr, (int)inlineCallAddr, fileName, out hotIdx, out defLineStart, out defLineEnd, out lineNum, out column);
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
				aDialog = ThemeFactory.mDefault.CreateDialog("Save file?", scope String()..AppendF("Save changes to '{0}' before closing?", changedList[0]), DarkTheme.sDarkTheme.mIconWarning);
			}
			else
			{
				String text = scope String("The following files have been modified: ");
				text.Join(", ", changedList.GetEnumerator());
				text.Append(". Save before closing?");
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
						if (fileName.IsWhiteSpace)
							fileName.Set("untitled");
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
			ClearAndDeleteItems(changedList);

			aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) =>
				{
					onSave();
				} ~ delete onSave);
			aDialog.AddButton("Don't Save", new (evt) =>
				{
					OutputLine("Changes not saved.");
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

			void CloseTabs()
			{
				WithDocumentTabbedViewsOf(window, scope (tabbedView) =>
					{
						tabbedView.CloseTabs(false, true, true);
					});
			}

			void CloseWindow()
			{
				mMainWindow.SetForeground();
				window.Close(true);
			}

			List<String> changedList = scope List<String>();
			defer ClearAndDeleteItems(changedList);
			WithSourceViewPanelsOf(window, scope (sourceViewPanel) =>
				{
					if (sourceViewPanel.HasUnsavedChanges())
					{
						var fileName = new String();
						Path.GetFileName(sourceViewPanel.mFilePath, fileName);
						changedList.Add(fileName);
					}
				});

			if (changedList.Count == 0)
			{
				CloseTabs();
				return true;
			}
			var aDialog = QuerySaveFiles(changedList, (WidgetWindow)window);
			aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) =>
				{
					bool hadError = false;
					// We use a close delay after saving so the user can see we actually saved before closing down
					var _this = this;
					WithSourceViewPanelsOf(window, scope [&] (sourceViewPanel) =>
						{
							if ((!hadError) && (sourceViewPanel.HasUnsavedChanges()))
								if (!_this.SaveFile(sourceViewPanel))
									hadError = true;
						});
					if (hadError)
						return;
					CloseTabs();
					CloseWindow();
				});
			aDialog.AddButton("Don't Save", new (evt) =>
				{
					var _this = this;
					WithSourceViewPanelsOf(window, scope [&] (sourceViewPanel) =>
						{
							if (sourceViewPanel.HasUnsavedChanges())
								_this.RevertSourceViewPanel(sourceViewPanel);
						});
					CloseTabs();
					CloseWindow();
				});

			aDialog.mEscButton = aDialog.AddButton("Cancel");
			aDialog.PopupWindow((WidgetWindow)window ?? mMainWindow);
			return false;
		}

		public SourceViewPanel GetActiveSourceViewPanel(bool includeLastActive = false, bool includeEmbeds = false)
		{
			if (mRunningTestScript)
				return mLastActiveSourceViewPanel;

			var activePanel = GetActiveDocumentPanel();
			var sourceViewPanel = activePanel as SourceViewPanel;
			if (sourceViewPanel != null)
				sourceViewPanel = sourceViewPanel.GetActivePanel();
			if ((mLastActiveSourceViewPanel != null) && (includeLastActive))
				sourceViewPanel = mLastActiveSourceViewPanel.GetActivePanel();
			if ((sourceViewPanel != null) && (includeEmbeds))
				sourceViewPanel = sourceViewPanel.GetFocusedEmbeddedView();
			return sourceViewPanel;
		}

		public TextPanel GetActiveTextPanel()
		{
			var activePanel = GetActivePanel();
			return activePanel as TextPanel;
		}

		public void RefreshVisibleViews(SourceViewPanel excludeSourceViewPanel = null, CompilerBase.ViewRefreshKind viewRefreshKind = .FullRefresh)
		{
			WithSourceViewPanels(scope (sourceViewPanel) =>
				{
					if ((sourceViewPanel.mParent != null) && (sourceViewPanel != excludeSourceViewPanel))
					{
						if (viewRefreshKind == .Collapse)
							sourceViewPanel.QueueCollapseRefresh();
						else
							sourceViewPanel.QueueFullRefresh(true);
					}
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
					if (!SafeWriteTextFile(path, text, true, projectSource.mEditData.mLineEndingKind, scope (outStr) =>
						{
							projectSource.mEditData.BuildHash(outStr);
						}))
					{
						return false;
					}

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

		public bool SafeWriteTextFile(StringView path, StringView text, bool showErrors = true, LineEndingKind lineEndingKind = .Default, delegate void(StringView str) strOutDlg = null)
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

			Debug.Assert(!text.Contains('\r'));

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
							Fail(scope String()..AppendF("Failed to write file '{0}'", path));
						return false;
					}
				}
			}

			if (strOutDlg != null)
				strOutDlg(useText);

			for (var entry in mWorkspace.mProjectFileEntries)
			{
				if (entry.mPath == path)
					entry.UpdateLastWriteTime();
			}

			return true;
		}

		public void SetEmbedCompiler(Settings.EditorSettings.CompilerKind emitCompiler)
		{
			gApp.mSettings.mEditorSettings.mEmitCompiler = emitCompiler;
			mBfResolveCompiler?.QueueRefreshViewCommand(.Collapse);
			if (emitCompiler == .Resolve)
				mBfResolveCompiler?.QueueRefreshViewCommand(.FullRefresh);
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

			if (fileName.StartsWith("$Emit$"))
			{
				String useFileName = fileName;

				BfCompiler compiler = (gApp.mSettings.mEditorSettings.mEmitCompiler == .Resolve) ? mBfResolveCompiler : mBfBuildCompiler;

				if (useFileName.StartsWith("$Emit$Build$"))
				{
					useFileName = scope:: $"$Emit${useFileName.Substring("$Emit$Build$".Length)}";
					compiler = mBfBuildCompiler;
				}
				else if (useFileName.StartsWith("$Emit$Resolve$"))
				{
					useFileName = scope:: $"$Emit${useFileName.Substring("$Emit$Resolve$".Length)}";
					compiler = mBfResolveCompiler;
				}

				if (!compiler.IsPerformingBackgroundOperation())
					compiler.GetEmitSource(useFileName, outBuffer);

				if (onPreFilter != null)
					onPreFilter();
				return .Ok;
			}
			else if (fileName.StartsWith("$Emit"))
			{
				if (mDebugger.GetEmitSource(fileName, outBuffer))
					return .Ok;
			}

			return Utils.LoadTextFile(fileName, outBuffer, autoRetry, onPreFilter);
		}

		public bool SaveFileAs(SourceViewPanel sourceViewPanel)
		{
#if !CLI
			String fullDir = scope .();
			if (sourceViewPanel.mFilePath != null)
				Path.GetDirectoryPath(sourceViewPanel.mFilePath, fullDir);
			else if (!mWorkspace.mDir.IsWhiteSpace)
				fullDir.Set(mWorkspace.mDir);

			SaveFileDialog dialog = scope .();
			dialog.SetFilter("All files (*.*)|*.*");
			//dialog.ValidateNames = true;
			if (!fullDir.IsEmpty)
				dialog.InitialDirectory = fullDir;

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

		public bool SaveFile(FileEditData editData, String forcePath = null)
		{
			var lineEndingKind = LineEndingKind.Default;
			if (editData != null)
			{
				for (var user in editData.mEditWidget.mEditWidgetContent.mData.mUsers)
				{
					user.MarkDirty();
				}
				lineEndingKind = editData.mLineEndingKind;
			}

			// Lock file watcher to synchronize the 'file changed' notification so we don't
			//  think a file was externally saved
			using (mFileWatcher.mMonitor.Enter())
			{
				String path = forcePath ?? editData.mFilePath;
				String text = scope String();
				editData.mEditWidget.GetText(text);

				if (!SafeWriteTextFile(path, text, true, lineEndingKind, scope (outStr) =>
					{
						editData.BuildHash(outStr);
					}))
				{
					return false;
				}

				editData.GetFileTime();

				editData.mLastFileTextVersion = editData.mEditWidget.Content.mData.mCurTextVersionId;
				mFileWatcher.OmitFileChange(path, text);
				if (forcePath == null)
				{
					for (var user in editData.mEditWidget.mEditWidgetContent.mData.mUsers)
					{
						if (var sourceEditWidgetContent = user as SourceEditWidgetContent)
						{
							var sourceViewPanel = sourceEditWidgetContent.mSourceViewPanel;
							sourceViewPanel?.FileSaved();
						}
					}
				}
#if IDE_C_SUPPORT
				mDepClang.FileSaved(path);
#endif
			}

			return true;
		}

		public bool SaveFile(SourceViewPanel sourceViewPanel, String forcePath = null)
		{
			if ((sourceViewPanel.HasUnsavedChanges()) || (forcePath != null))
			{
				if (gApp.mSettings.mEditorSettings.mFormatOnSave)
				{
					sourceViewPanel.ReformatDocument(true);
				}

				if ((forcePath == null) && (sourceViewPanel.mFilePath == null))
				{
					return SaveFileAs(sourceViewPanel);
				}

				if (sourceViewPanel.mEditData == null)
				{
					sourceViewPanel.mEditData = GetEditData(forcePath, true, false);
					sourceViewPanel.mEditData.mEditWidget = sourceViewPanel.mEditWidget;
				}

				if (!SaveFile(sourceViewPanel.mEditData, forcePath))
					return false;

				if (sourceViewPanel.mProjectSource != null)
				{
					if (mWakaTime != null)
					{
						String path = forcePath ?? sourceViewPanel.mFilePath;
						mWakaTime.QueueFile(path, sourceViewPanel.mProjectSource.mProject.mProjectName, true);
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
			data.Add("X", window.mNormX);
			data.Add("Y", window.mNormY);
			data.Add("Width", window.mNormWidth);
			data.Add("Height", window.mNormHeight);
			data.Add("ShowKind", window.mShowKind);
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
						if (tabWidget.mContent is DisassemblyPanel)
							continue;
						if (tabWidget.mContent is WelcomePanel)
							continue;
						if (tabWidget.mContent is StartupPanel)
							continue;
					}

					using (data.CreateObject())
					{
						var panel = tabWidget.mContent as Panel;
						if (!panel.WantsSerialization)
							continue;

						if (tabWidget.mIsActive)
							data.Add("Active", true);

						data.Add("TabLabel", tabWidget.mLabel);
						data.Add("TabWidth", tabWidget.mWantWidth / DarkTheme.sScale);
						data.Add("TabIsPinned", tabWidget.mIsPinned);

						if (var watchPanel = tabWidget.mContent as WatchPanel)
						{
							watchPanel.Serialize(data, serializeDocs);
						}
						else if (panel != null)
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
			if (mMainWindow == null)
				return true;

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
				if (mMainWindow != null)
					SerializeWindow(sd, mMainWindow);
				else
				{
					sd.Add("X", mRequestedWindowRect.mX);
					sd.Add("Y", mRequestedWindowRect.mY);
					sd.Add("Width", mRequestedWindowRect.mWidth);
					sd.Add("Height", mRequestedWindowRect.mHeight);
					sd.Add("ShowKind", mRequestedShowKind);
				}
			}

			using (sd.CreateObject("MainDockingFrame"))
			{
				SerializeDockingFrame(sd, mDockingFrame, true);
			}

			using (sd.CreateArray("RecentFilesList"))
			{
				for (var recentFile in mRecentlyDisplayedFiles)
				{
					String relPath = scope .();
					mWorkspace.GetWorkspaceRelativePath(recentFile, relPath);
					sd.Add(relPath);
				}
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
								String relPath = scope .();
								mWorkspace.GetWorkspaceRelativePath(breakpoint.mFileName, relPath);
								sd.Add("File", relPath);
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

			using (sd.CreateArray("BookmarkFolders"))
			{
				for (var folder in mBookmarkManager.mBookmarkFolders)
				{
					using (sd.CreateObject())
					{
						sd.Add("Title", folder.mTitle);

						using (sd.CreateArray("Bookmarks"))
						{
							for (var bookmark in folder.mBookmarkList)
							{
								if (bookmark.mFileName != null)
								{
									using (sd.CreateObject())
									{
										String relPath = scope .();
										mWorkspace.GetWorkspaceRelativePath(bookmark.mFileName, relPath);
										sd.Add("File", relPath);
										sd.Add("Line", bookmark.mLineNum);
										sd.Add("Column", bookmark.mColumn);
										sd.Add("Title", bookmark.mTitle);
										if (bookmark.mIsDisabled)
											sd.Add("Disabled", true);
									}
								}
							}
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
						DebugManager.FloatDisplayType floatDisplayType;
						String formatStr = scope .();
						mDebugger.GetDisplayTypes(referenceId, formatStr, out intDisplayType, out mmDisplayType, out floatDisplayType);
						using (sd.CreateObject(referenceId))
						{
							if (!formatStr.IsEmpty)
								sd.Add("FormatStr", formatStr);
							sd.ConditionalAdd("IntDisplayType", intDisplayType);
							sd.ConditionalAdd("MmDisplayType", mmDisplayType);
							sd.ConditionalAdd("FloatDisplayType", floatDisplayType);
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

			using (sd.CreateObject("OutputFilters"))
			{
				IDE.Debugger.DebugManager.OutputFilterFlags outputFilterFlags = 0;
				if (gApp.mDebugger != null)
				{
					outputFilterFlags = gApp.mDebugger.GetOutputFilterFlags();
				}

				sd.Add("ModuleLoadMessages", outputFilterFlags.HasFlag(.ModuleLoadMessages));
				sd.Add("ModuleUnloadMessages", outputFilterFlags.HasFlag(.ModuleUnloadMessages));
				sd.Add("ProcessExitMessages", outputFilterFlags.HasFlag(.ProcessExitMessages));
				sd.Add("ThreadCreateMessages", outputFilterFlags.HasFlag(.ThreadCreateMessages));
				sd.Add("ThreadExitMessages", outputFilterFlags.HasFlag(.ThreadExitMessages));
				sd.Add("SymbolLoadMessages", outputFilterFlags.HasFlag(.SymbolLoadMessages));
				sd.Add("ProgramOutput", outputFilterFlags.HasFlag(.ProgramOutput));
			}
		}

		bool SaveWorkspaceUserData(bool showErrors = true)
		{
			// Don't save if we didn't finish creating the workspace
			if (mWorkspace.mNeedsCreate)
				return false;
			if (!mWorkspace.IsInitialized)
				return false;

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

		bool SaveWorkspaceLockData(bool force = false)
		{
			if ((mWorkspace.mProjectLockMap.IsEmpty) && (!force))
				return true;

			StructuredData sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("FileVersion", 1);
			using (sd.CreateObject("Locks"))
			{
				List<String> projectNames = scope .(mWorkspace.mProjectLockMap.Keys);
				projectNames.Sort();

				for (var projectName in projectNames)
				{
					var lock = mWorkspace.mProjectLockMap[projectName];
					switch (lock)
					{
					case .Git(let url, let tag, let hash):
						using (sd.CreateObject(projectName))
						{
							using (sd.CreateObject("Git"))
							{
								sd.Add("URL", url);
								sd.Add("Tag", tag);
								sd.Add("Hash", hash);
							}
						}
					default:
					}
				}
			}

			String jsonString = scope String();
			sd.ToTOML(jsonString);

			String lockFileName = scope String();
			GetWorkspaceLockFileName(lockFileName);
			if (lockFileName.IsEmpty)
				return false;
			return SafeWriteTextFile(lockFileName, jsonString);
		}

		bool LoadWorkspaceLockData()
		{
			String lockFilePath = scope String();
			GetWorkspaceLockFileName(lockFilePath);
			if (lockFilePath.IsEmpty)
				return true;

			var sd = scope StructuredData();
			if (sd.Load(lockFilePath) case .Err)
				return false;

			for (var projectName in sd.Enumerate("Locks"))
			{
				Workspace.Lock lock = default;
				if (sd.Contains("Git"))
				{
					using (sd.Open("Git"))
					{
						var url = sd.GetString("URL", .. new .());
						var tag = sd.GetString("Tag", .. new .());
						var hash = sd.GetString("Hash", .. new .());
						lock = .Git(url, tag, hash);
					}
				}

				mWorkspace.SetLock(projectName, lock);
			}

			return true;
		}

		bool GetWorkspaceLockFileName(String outResult)
		{
			if (mWorkspace.mDir == null)
				return false;
			if (mWorkspace.mCompositeFile != null)
				outResult.Append(mWorkspace.mCompositeFile.mFilePath, ".bfuser");
			else
				outResult.Append(mWorkspace.mDir, "/BeefSpace_Lock.toml");
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
			if (mWorkspace.mNeedsCreate)
				mWorkspace.mNeedsCreate = false;

			if (!mWorkspace.IsSingleFileWorkspace)
			{
#if !CLI
				if (mWorkspace.mDir == null)
				{
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
					Fail(scope String()..AppendF("Failed to create workspace directory '{0}'", mWorkspace.mDir));
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
					Fail(scope String()..AppendF("Failed to write workspace file '{0}'", workspaceFileName));
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

				String execCmd = scope .();
				ResolveConfigString(mPlatformName, workspaceOptions, project, options, options.mDebugOptions.mCommand, "command", execCmd);

				if (!execCmd.IsWhiteSpace)
				{
					String initialDir = scope .();
					Path.GetDirectoryPath(execCmd, initialDir).IgnoreError();
					if (!initialDir.IsWhiteSpace)
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

			if (!mWorkspace.mLoading)
			{
				for (var checkProject in mWorkspace.mProjects)
				{
					int idx = checkProject.mDependencies.FindIndex(scope (dep) => dep.mProjectName == project.mProjectName);
					if (idx != -1)
						ProjectOptionsChanged(checkProject);
				}
				ProjectOptionsChanged(project, false);
			}
		}

		public void AddNewProjectToWorkspace(Project project, VerSpec verSpec = .None)
		{
			AddProjectToWorkspace(project);
			mWorkspace.SetChanged();

			var relPath = scope String();
			Path.GetRelativePath(project.mProjectPath, mWorkspace.mDir, relPath);
			relPath.Replace("\\", "/");

			int lastSlash = relPath.LastIndexOf('/');
			if (lastSlash != -1)
				relPath.RemoveToEnd(lastSlash);
			else
				relPath.Set(".");

			/*var endStr = "/BeefProj.toml";
			if (relPath.EndsWith(endStr))
				relPath.RemoveToEnd(relPath.Length - endStr.Length);*/

			var projectSpec = new Workspace.ProjectSpec();
			projectSpec.mProjectName = new .(project.mProjectName);
			if (verSpec != .None)
			{
				projectSpec.mVerSpec = verSpec.Duplicate();
			}
			else
			{
				projectSpec.mVerSpec = .Path(new String(relPath));
			}
			mWorkspace.mProjectSpecs.Add(projectSpec);

			var dep = new Project.Dependency();
			dep.mProjectName = new .("corlib");
			dep.mVerSpec = .SemVer(new .("*"));
			project.mDependencies.Add(dep);
		}

		public void ProjectCreated(Project project)
		{
			mProjectPanel.InitProject(project, mProjectPanel.GetSelectedWorkspaceFolder());
			mProjectPanel.Sort();
			mWorkspace.FixOptions();
			mWorkspace.mHasChanged = true;

			mWorkspace.ClearProjectNameCache();
			CurrentWorkspaceConfigChanged();
		}

		public Project CreateProject(String projName, String projDir, Project.TargetType targetType)
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
			AddNewProjectToWorkspace(project);
			project.FinishCreate();

			ProjectCreated(project);

			return project;
		}

		void StopDebugging()
		{
			if (mDebugger.mIsRunning)
			{
				if (mDebugger.mIsComptimeDebug)
					CancelBuild();
				mDebugger.StopDebugging();
				while (mDebugger.GetRunState() != .Terminated)
				{
					if (mDebugger.mIsComptimeDebug)
					{
						if (!mBfBuildCompiler.IsPerformingBackgroundOperation())
							break;
					}
					mDebugger.Update();
				}
				mDebugger.mIsRunning = false;
				mExecutionPaused = false;
			}
			mDebugger.DisposeNativeBreakpoints();
			mWantsRehupCallstack = false;

			if (mDebugger.mIsComptimeDebug)
				mDebugger.Detach();
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

			WithSourceViewPanels(scope (sourceViewPanel) =>
				{
					sourceViewPanel.Dispose();
				});

			if (!mRunningTestScript)
			{
				mActiveDocumentsTabbedView = null;
				WithStandardPanels(scope (panel) =>
					{
						ResetPanel(panel);
					});
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

#if !CLI
			mFileRecovery.WorkspaceClosed();
#endif

			delete mWorkspace;
			mWorkspace = new Workspace();

			mErrorsPanel.Clear();

			mBookmarksPanel.Clear();

			mBookmarkManager.Clear();

			mPackMan.CancelAll();

			OutputLine("Workspace closed.");
		}

		void FinishShowingNewWorkspace(bool loadUserData = true)
		{
			if ((loadUserData) && (!mRunningTestScript))
			{
				if (!LoadWorkspaceUserData())
				{
					CreateDefaultLayout();
				}
			}

			WorkspaceLoaded();
			UpdateTitle();
			UpdateRecentDisplayedFilesMenuItems();

			mProjectPanel.RebuildUI();

			ShowPanel(mOutputPanel, false);
			mMainFrame.RehupSize();

			if ((mSettings.mUISettings.mShowStartupPanel) && (!mWorkspace.IsInitialized))
				ShowStartup();
			else
				ShowStartupFile();

#if !CLI
			if (mMainWindow != null)
				mFileRecovery.CheckWorkspace();
#endif
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

		[IDECommand]
		void CollapseAll()
		{
			GetActiveSourceEditWidgetContent()?.CollapseAll();
		}

		[IDECommand]
		void CollapseToDefinition()
		{
			GetActiveSourceEditWidgetContent()?.CollapseToDefinition();
		}

		[IDECommand]
		void CollapseRedo()
		{
		}

		[IDECommand]
		void CollapseToggle()
		{
			GetActiveSourceEditWidgetContent()?.CollapseToggle();
		}

		[IDECommand]
		void CollapseToggleAll()
		{
			GetActiveSourceEditWidgetContent()?.CollapseToggleAll();
		}

		[IDECommand]
		void CollapseUndo()
		{
		}

		[IDECommand]
		void DeleteAllRight()
		{
			GetActiveSourceEditWidgetContent()?.DeleteAllRight();
		}

		[IDECommand]
		void DuplicateLine()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.DuplicateLine();
		}

		[IDECommand]
		void CommentBlock()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.CommentBlock();
		}

		[IDECommand]
		void CommentLines()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.CommentLines();
		}

		[IDECommand]
		void CommentToggle()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.ToggleComment();
		}

		[IDECommand]
		void UncommentSelection()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.ToggleComment(false);
		}

		[IDECommand]
		void ComplexIdSpan()
		{
			if (var sourceViewPanel = GetLastActiveDocumentPanel() as SourceViewPanel)
			{
				var sewc = sourceViewPanel.mEditWidget.mEditWidgetContent as SourceEditWidgetContent;
				uint8[] newData = new uint8[sewc.mData.mTextLength * 4];

				var idData = ref sewc.mData.mTextIdData;
				/*idData.Prepare();

				int encodeIdx = 0;
				int decodeIdx = 0;
				int charId = 1;
				int charIdx = 0;
				while (true)
				{
					int32 cmd = Utils.DecodeInt(idData.mData, ref decodeIdx);
					if (cmd > 0)
					{
						charId = cmd;
						Utils.EncodeInt(newData, ref encodeIdx, charId);
					}
					else
					{
						int32 spanSize = -cmd;

						charId += spanSize;
						charIdx += spanSize;

						if (cmd == 0)
						{
							Utils.EncodeInt(newData, ref encodeIdx, 0);
							break;
						}

						while (spanSize > 65)
						{
							Utils.EncodeInt(newData, ref encodeIdx, -64);
							spanSize -= 64;
						}
						Utils.EncodeInt(newData, ref encodeIdx, -spanSize);
					}
				}*/

				int encodeIdx = 0;
				int sizeLeft = sewc.mData.mTextLength;
				while (sizeLeft > 0)
				{
					int writeLength = Math.Min(sizeLeft, 64);
					Utils.EncodeInt(newData, ref encodeIdx, sewc.mData.mNextCharId);
					Utils.EncodeInt(newData, ref encodeIdx, -writeLength);
					sewc.mData.mNextCharId += (.)writeLength;
					sewc.mData.mNextCharId++;
					sizeLeft -= writeLength;
				}
				Utils.EncodeInt(newData, ref encodeIdx, 0);

				IdSpan newSpan = .(newData, (.)encodeIdx);

				//Runtime.Assert(newSpan.Equals(idData));

				idData.Dispose();
				idData = newSpan;
			}
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

		public void CheckDependenciesLoaded()
		{
			for (var project in mWorkspace.mProjects)
				project.CheckDependenciesLoaded();
		}

		void FlushDeferredLoadProjects(bool addToUI = false)
		{
			bool hasDeferredProjects = false;
			bool loadFailed = false;

			while (true)
			{
				bool hadLoad = false;
				for (int projectIdx = 0; projectIdx < mWorkspace.mProjects.Count; projectIdx++)
				{
					var project = mWorkspace.mProjects[projectIdx];

					if (project.mDeferState == .Searching)
					{
						if (mPackMan.mFailed)
						{
							// Just let it fail now
							LoadFailed();
							project.mDeferState = .None;
							project.mFailed = true;
							loadFailed = true;
						}
						else
						{
							hasDeferredProjects = true;
						}
					}

					if ((project.mDeferState == .ReadyToLoad) || (project.mDeferState == .Pending))
					{
						hadLoad = true;

						var projectPath = project.mProjectPath;

						if (project.mDeferState == .Pending)
						{
							hasDeferredProjects = true;
							project.mDeferState = .Searching;
						}
						else if (!project.Load(projectPath))
						{
							OutputErrorLine("Failed to load project '{0}' from '{1}'", project.mProjectName, projectPath);
							LoadFailed();
							project.mFailed = true;
						}

						AddProjectToWorkspace(project, false);
						if (addToUI)
							mProjectPanel?.InitProject(project, null);
					}
				}
				if (!hadLoad)
					break;
			}

			if (hasDeferredProjects)
			{
				mWorkspace.mProjectLoadState = .Preparing;
			}
			else
			{
				mWorkspace.mProjectLoadState = .Loaded;
				SaveWorkspaceLockData();
				CheckDependenciesLoaded();
			}

			if (loadFailed)
			{
				mProjectPanel?.RebuildUI();
			}
		}

		public void CancelWorkspaceLoading()
		{
			mPackMan.CancelAll();
			FlushDeferredLoadProjects();
		}

		protected void LoadWorkspace(BeefVerb verb)
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

			mWorkspace.mLoading = true;
			defer
			{
				mWorkspace.mLoading = false;
			}

			var startupProjectName = scope String();

			if (StructuredLoad(data, workspaceFileName) case .Err(let err))
			{
				mBeefConfig.Refresh();

				switch (err)
				{
				case .FormatError(int lineNum):
					OutputErrorLine("Workspace format error in '{0}' on line {1}", workspaceFileName, lineNum);
					LoadFailed();
					return;
				case .FileError: // Assume 'file not found'
					if (verb == .OpenOrNew)
					{
						isNew = true;
					}
					else if (verb == .New)
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
				//int lastSlashPos = mWorkspace.mDir.LastIndexOf(IDEUtils.cNativeSlash);
				String projectName = mWorkspace.mName;
				Debug.Assert(!projectName.IsWhiteSpace);

				String projectPath = scope String(mWorkspace.mDir);
				Utils.GetDirWithSlash(projectPath);
				projectPath.Append("BeefProj.toml");

				Project project = new Project();
				mWorkspace.mProjects.Add(project);
				project.mProjectPath.Set(projectPath);
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

					project.FinishCreate(false);

					VerSpec verSpec = .SemVer(new .("*"));
					defer verSpec.Dispose();

					switch (AddProject("corlib", verSpec))
					{
					case .Ok(let libProject):
						var dep = new Project.Dependency();
						dep.mProjectName = new String("corlib");
						dep.mVerSpec = verSpec.Duplicate();
						project.mDependencies.Add(dep);
					default:
					}
				}

				var projSpec = new Workspace.ProjectSpec();
				projSpec.mProjectName = new String(project.mProjectName);
				projSpec.mVerSpec = .Path(new String("."));
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

				OutputWarnLine("Created new workspace in '{0}'", mWorkspace.mDir);
				if (wantSave)
				{
					SaveWorkspace();
					project.Save();
				}
				else
				{
					mWorkspace.mNeedsCreate = true;
					OutputLine("Use 'File\\Save All' to commit to disk.");
				}
			}
			else
			{
				LoadWorkspaceLockData();
				mWorkspace.mProjectFileEntries.Add(new .(workspaceFileName));

				if (mVerb == .New)
				{
					OutputErrorLine("Workspace '{0}' already exists, but '-new' argument was specified.", workspaceFileName);
					LoadFailed();
				}

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
						var projSpec = new Workspace.ProjectSpec();
						projSpec.mProjectName = new String(projectName);
						mWorkspace.mProjectSpecs.Add(projSpec);

						if (projSpec.mVerSpec.Parse(data) case .Err)
						{
							var errStr = scope String();
							errStr.AppendF("Unable to parse version specifier for {0} in {1}", projectName, workspaceFileName);
							Fail(errStr);
							LoadFailed();
							continue;
						}

						switch (AddProject(projectName, projSpec.mVerSpec))
						{
						case .Ok(let project):
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

			FlushDeferredLoadProjects();

			for (var project in mWorkspace.mProjects)
			{
				if ((!startupProjectName.IsEmpty) && (StringView.Compare(startupProjectName, project.mProjectName, true) == 0))
				{
					mWorkspace.mStartupProject = project;
				}
			}

			mWorkspace.FinishDeserialize(data);
			mWorkspace.FixOptions(mConfigName, mPlatformName);
#if !CLI
			if (mBfResolveCompiler != null)
				mBfResolveCompiler.QueueSetWorkspaceOptions(null, 0);
#endif

			String relaunchCmd = scope .();
			GetRelaunchCmd(true, relaunchCmd);
			Platform.BfpSystem_SetCrashRelaunchCmd(relaunchCmd);

			MarkDirty();
		}

		protected void ReloadWorkspace()
		{
			SaveWorkspaceUserData(false);

			String workspaceDir = scope .(mWorkspace.mDir);
			String workspaceName = scope .(mWorkspace.mName);

			CloseWorkspace();
			mWorkspace.mDir = new String(workspaceDir);
			mWorkspace.mName = new String(workspaceName);
			LoadWorkspace(.Open);
			FinishShowingNewWorkspace();
		}

		public void GetRelaunchCmd(bool safeMode, String outRelaunchCmd)
		{
			if (mWorkspace.mDir != null)
			{
				outRelaunchCmd.Append("\"");
				Environment.GetExecutableFilePath(outRelaunchCmd);
				outRelaunchCmd.Append("\" -workspace=\"");
				outRelaunchCmd.Append(mWorkspace.mDir);
				outRelaunchCmd.Append("\"");
			}

			if (safeMode)
				outRelaunchCmd.Append(" -safe");
		}

		public void RetryProjectLoad(Project project, bool reloadConfig)
		{
			if (reloadConfig)
				LoadConfig();

			var projectPath = project.mProjectPath;
			if (!project.Load(projectPath))
			{
				Fail(scope String()..AppendF("Failed to load project '{0}' from '{1}'", project.mProjectName, projectPath));
				LoadFailed();
				project.mFailed = true;
				FlushDeferredLoadProjects();
				mProjectPanel?.RebuildUI();
			}
			else
			{
				FlushDeferredLoadProjects();
				mWorkspace.FixOptions();

				project.mFailed = false;
				mProjectPanel?.RebuildUI();
				CurrentWorkspaceConfigChanged();
			}
		}

		public enum ProjectAddError
		{
			InvalidVersion,
			InvalidVersionSpec,
			LoadFailed,
			NotFound
		}

		public Result<Project, ProjectAddError> AddProject(StringView projectName, VerSpec verSpec)
		{
			VerSpec useVerSpec = verSpec;
			String verConfigDir = mWorkspace.mDir;

			if (let project = mWorkspace.FindProject(projectName))
			{
				switch (useVerSpec)
				{
				case .Git(let url, let ver):
					if (ver != null)
						mPackMan.UpdateGitConstraint(url, ver);
				default:
				}

				return project;
			}

			if (useVerSpec case .SemVer)
			{
				// First pass we just try to use the 'expected' project name
				FindLoop:for (int pass < 2)
				{
					using (mBeefConfig.mRegistry.mMonitor.Enter())
					{
						BeefConfig.RegistryEntry matchedEntry = null;

						for (int regEntryIdx = mBeefConfig.mRegistry.mEntries.Count - 1; regEntryIdx >= 0; regEntryIdx--)
						{
							var regEntry = mBeefConfig.mRegistry.mEntries[regEntryIdx];

							if ((regEntry.mProjName == projectName) && (!regEntry.mParsedConfig))
								mBeefConfig.mRegistry.ParseConfig(regEntry);

							if (regEntry.mProjName == projectName)
							{
								// Prioritize a lib file over a non-lib
								if ((matchedEntry == null) ||
									((!matchedEntry.mTargetType.IsLib) && (regEntry.mTargetType.IsLib)))
									matchedEntry = regEntry;
							}
						}

						if (matchedEntry != null)
						{
							useVerSpec = matchedEntry.mLocation;
							verConfigDir = matchedEntry.mConfigFile.mConfigDir;
							break FindLoop;
						}
					}
					mBeefConfig.mRegistry.WaitFor();
				}
			}

			var project = new Project();

			// For project locking, assume that only anything that is referenced with a path is editable
			project.mLockedDefault = !(verSpec case .Path);
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
			bool isDeferredLoad = false;

			switch (useVerSpec)
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
			case .Git(let url, let ver):

				var checkPath = scope String();
				if (mPackMan.CheckLock(projectName, checkPath, var projectFailed))
				{
					projectFilePath = scope:: String(checkPath);
				}
				else
				{
					mPackMan.GetWithVersion(projectName, url, ver);
					isDeferredLoad = true;
				}
			default:
				Fail("Invalid version specifier");
				return .Err(.InvalidVersionSpec);
			}

			if ((projectFilePath == null) && (!isDeferredLoad))
				return .Err(.NotFound);

			if (isDeferredLoad)
			{
				mWorkspace.mProjectLoadState = .Preparing;
			}

			project.mProjectName.Set(projectName);
			project.DeferLoad(projectFilePath);
			success = true;
			mWorkspace.AddProjectToCache(project);

			/*if (!project.Load(projectFilePath))
			{
				Fail(scope String()..AppendF("Failed to load project {0}", projectFilePath));
				delete project;
				return .Err(.LoadFailed);
			}

			success = true;
			AddProjectToWorkspace(project, false);*/
			return .Ok(project);
		}

		public void UpdateProjectVersionLocks(params Span<StringView> projectNames)
		{
			bool removedLock = false;

			for (var projectName in projectNames)
			{
				if (var kv = gApp.mWorkspace.mProjectLockMap.GetAndRemoveAlt(projectName))
				{
					removedLock = true;
					delete kv.key;
					kv.value.Dispose();
				}
			}

			if (removedLock)
			{
				if (SaveAll())
				{
					SaveWorkspaceLockData(true);
					CloseOldBeefManaged();
					ReloadWorkspace();
				}
			}
		}

		public void UpdateProjectVersionLocks(Span<String> projectNames)
		{
			List<StringView> svNames = scope .();
			for (var name in projectNames)
				svNames.Add(name);
			UpdateProjectVersionLocks(params (Span<StringView>)svNames);
		}

		public void NotifyProjectVersionLocks(Span<String> projectNames)
		{
			if (projectNames.IsEmpty)
				return;

			String message = scope .();
			message.Append((projectNames.Length == 1) ? "Project " : "Projects ");
			for (var projectName in projectNames)
			{
				if (@projectName.Index > 0)
					message.Append(", ");
				message.AppendF($"'{projectName}'");
			}

			message.Append((projectNames.Length == 1) ? " has " : " have ");

			message.AppendF("modified version constraints. Use 'Update Version Lock' in the project or workspace right-click menus to apply the new constraints.");
			MessageDialog("Version Constraints Modified", message, DarkTheme.sDarkTheme.mIconWarning);
		}

		protected void WorkspaceLoaded()
		{
			scope AutoBeefPerf("IDE.WorkspaceLoaded");

			if (!Environment.IsFileSystemCaseSensitive)
			{
				// Make sure we have the correct actual path
				String newPath = new String();
				Path.GetActualPathName(mWorkspace.mDir, newPath);
				delete mWorkspace.mDir;
				mWorkspace.mDir = newPath;
			}

			List<String> platforms = scope List<String>();
			if (IDEApp.sPlatform32Name != null)
				platforms.Add(IDEApp.sPlatform32Name);
			if (IDEApp.sPlatform64Name != null)
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
			if (mBfResolveSystem != null)
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

			SourceViewTabButton activeTab = null;
			for ( data.Enumerate("Tabs"))
			{
				Panel panel = Panel.Create(data);
				if (panel == null)
					continue;
				Debug.Assert(panel != null);

				bool isActive = data.GetBool("Active");

				var newTabButton = new SourceViewTabButton();
				newTabButton.Label = "";
				data.GetString("TabLabel", newTabButton.mLabel);
				newTabButton.mIsPinned = data.GetBool("TabIsPinned");
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
			{
				mRequestedWindowRect = Rect(x, y, width, height);
			}
			mRequestedShowKind = data.GetEnum<BFWindow.ShowKind>("ShowKind");
		}

		bool LoadWorkspaceUserData(StructuredData data)
		{
			//
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
					mPlatformName.Set(platformName);
				}
			}

			using (data.Open("MainWindow"))
				DeserializeWindow(data, mMainWindow);

			if (mMainWindow == null)
				mMainFrame.Resize(0, 0, mRequestedWindowRect.mWidth, mRequestedWindowRect.mHeight);

			using (data.Open("MainDockingFrame"))
				DeserializeDockingFrame(data, mDockingFrame);

			ClearAndDeleteItems(mRecentlyDisplayedFiles);
			for ( data.Enumerate("RecentFilesList"))
			{
				String relPath = scope String();
				data.GetCurString(relPath);
				IDEUtils.FixFilePath(relPath);
				String absPath = new String();
				mWorkspace.GetWorkspaceAbsPath(relPath, absPath);
				mRecentlyDisplayedFiles.Add(absPath);
			}

			for (var _breakpoint in data.Enumerate("Breakpoints"))
			{
				String relPath = scope String();
				data.GetString("File", relPath);
				IDEUtils.FixFilePath(relPath);
				String absPath = scope String();
				mWorkspace.GetWorkspaceAbsPath(relPath, absPath);
				int32 lineNum = data.GetInt("Line");
				int32 column = data.GetInt("Column");
				int32 instrOffset = data.GetInt("InstrOffset", -1);
				String memoryWatchExpression = scope String();
				data.GetString("MemoryWatchExpression", memoryWatchExpression);
				Breakpoint breakpoint = null;
				if (memoryWatchExpression.Length > 0)
					breakpoint = mDebugger.CreateMemoryBreakpoint(memoryWatchExpression, (int)0, 0, null);
				else if (absPath.Length > 0)
					breakpoint = mDebugger.CreateBreakpoint(absPath, lineNum, column, instrOffset);
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

			for (var _bookmarkFolder in data.Enumerate("BookmarkFolders"))
			{
				String title = scope String();
				data.GetString("Title", title);

				BookmarkFolder folder = null;

				if (!String.IsNullOrWhiteSpace(title))
					folder = mBookmarkManager.CreateFolder(title);

				for (var _bookmark in data.Enumerate("Bookmarks"))
				{
					String relPath = scope String();
					data.GetString("File", relPath);
					IDEUtils.FixFilePath(relPath);
					String absPath = scope String();
					mWorkspace.GetWorkspaceAbsPath(relPath, absPath);
					int32 lineNum = data.GetInt("Line");
					int32 column = data.GetInt("Column");
					String bookmarkTitle = scope String();
					data.GetString("Title", bookmarkTitle);

					bool isDisabled = data.GetBool("Disabled", false);

					mBookmarkManager.CreateBookmark(absPath, lineNum, column, isDisabled, bookmarkTitle, folder);
				}
			}

			// Legacy loading
			for (var _bookmark in data.Enumerate("Bookmarks"))
			{
				String relPath = scope String();
				data.GetString("File", relPath);
				IDEUtils.FixFilePath(relPath);
				String absPath = scope String();
				mWorkspace.GetWorkspaceAbsPath(relPath, absPath);
				int32 lineNum = data.GetInt("Line");
				int32 column = data.GetInt("Column");

				bool isDisabled = data.GetBool("Disabled", false);

				mBookmarkManager.CreateBookmark(absPath, lineNum, column, isDisabled, null, null);
			}

			mBookmarkManager.RecalcCurId();

			for (var referenceId in data.Enumerate("DebuggerDisplayTypes"))
			{
				var referenceIdStr = scope String(referenceId);
				if (referenceIdStr.Length == 0)
					referenceIdStr = null;

				String formatStr = scope .();
				data.GetString("FormatStr", formatStr);
				var intDisplayType = data.GetEnum<DebugManager.IntDisplayType>("IntDisplayType");
				var mmDisplayType = data.GetEnum<DebugManager.MmDisplayType>("MmDisplayType");
				var floatDisplayType = data.GetEnum<DebugManager.FloatDisplayType>("FloatDisplayType");
				mDebugger.SetDisplayTypes(referenceIdStr, formatStr, intDisplayType, mmDisplayType, floatDisplayType);
			}

			for ( data.Enumerate("StepFilters"))
			{
				String filter = scope String();
				data.GetCurString(filter);
				if (!filter.IsEmpty)
					mDebugger.CreateStepFilter(filter, false, .Filtered);
			}

			for ( data.Enumerate("StepNotFilters"))
			{
				String filter = scope String();
				data.GetCurString(filter);
				if (!filter.IsEmpty)
					mDebugger.CreateStepFilter(filter, false, .NotFiltered);
			}

			using (data.Open("OutputFilters"))
			{
				IDE.Debugger.DebugManager.OutputFilterFlags outputFilterFlags = 0;

				outputFilterFlags |= data.GetBool("ModuleLoadMessages", false) ? .ModuleLoadMessages : 0;
				outputFilterFlags |= data.GetBool("ModuleUnloadMessages", false) ? .ModuleUnloadMessages : 0;
				outputFilterFlags |= data.GetBool("ProcessExitMessages", false) ? .ProcessExitMessages : 0;
				outputFilterFlags |= data.GetBool("ThreadCreateMessages", false) ? .ThreadCreateMessages : 0;
				outputFilterFlags |= data.GetBool("ThreadExitMessages", false) ? .ThreadExitMessages : 0;
				outputFilterFlags |= data.GetBool("SymbolLoadMessages", false) ? .SymbolLoadMessages : 0;
				outputFilterFlags |= data.GetBool("ProgramOutput", false) ? .ProgramOutput : 0;

				if (gApp.mDebugger != null)
				{
					gApp.mDebugger.SetOutputFilterFlags(outputFilterFlags);
				}
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
			mDeferredOpen = .NewWorkspaceOrProject;
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
#if !CLI
				if (!mWorkspace.IsDebugSession)
					success &= SaveWorkspaceUserData();
				if (mSettings.mLoadedSettings)
					mSettings.Save();
#endif
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
			var text;
			if (text.Contains('\t'))
			{
				text = scope:: String()..Append(text);
				text.Replace("\t", "    ");
			}

			// Always write to STDOUT even if we're running as a GUI, allowing cases like RunAndWait to pass us a stdout handle
			Console.Error.WriteLine("ERROR: {0}", text).IgnoreError();

#if CLI
			mFailed = true;
			return null;
#endif

#unwarn
			if ((mMainWindow == null) || (mShuttingDown))
			{
				mDeferredFails.Add(new String(text));
				return null;
			}


			if (mRunningTestScript)
			{
				if (mScriptManager.IsErrorExpected(text))
				{
					OutputLine("Received expected error: {0}", text);
					return null;
				}

				ShowOutput();

				mFailed = true;
				OutputLineSmart("ERROR: {0}", text);
				Console.Error.WriteLine("ERROR: {0}", text).IgnoreError();

				return null;
			}

#unwarn
			Debug.Assert(Thread.CurrentThread == mMainThread);

			if (mMainWindow == null)
			{
				Internal.FatalError(scope String()..AppendF("FAILED: {0}", text));
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

		public void MessageDialog(String title, String text, Image icon = null)
		{
			Dialog dialog = ThemeFactory.mDefault.CreateDialog(title, text, icon);
			dialog.mDefaultButton = dialog.AddButton("OK");
			dialog.mEscButton = dialog.mDefaultButton;
			dialog.PopupWindow(mMainWindow);
		}

		public void DoQuickFind(bool isReplace)
		{
			var textPanel = GetActiveTextPanel();
			if (textPanel != null)
			{
				textPanel.ShowQuickFind(isReplace);
				return;
			}
			else
			{
				if (let activeWindow = GetActiveWindow())
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

			var activePanel = GetActivePanel();
			if (var watchPanel = activePanel as WatchPanel)
			{
				watchPanel.mListView.ShowFind();
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
				if (let activeWindow = GetActiveWindow())
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

			var activePanel = GetActivePanel();
			if (var watchPanel = activePanel as WatchPanel)
			{
				watchPanel.mListView.FindNext(dir);
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
				if (!nextTabbedView.mWidgetWindow.mHasFocus)
					nextTabbedView.mWidgetWindow.SetForeground();
				var activeTab = nextTabbedView.GetActiveTab();
				if (activeTab != null)
				{
					activeTab.Activate();

					if (let sourceViewPanel = activeTab.mContent as SourceViewPanel)
					{
						sourceViewPanel.HilitePosition(.Always);
					}
				}
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
				sourceViewPanel.EditWidget.Content.RemoveSecondaryTextCursors();
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
			else if (var bookmarksPanel = activePanel as BookmarksPanel)
			{
				bookmarksPanel.TryRenameItem();
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
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
			{
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
			mBookmarkManager.PrevBookmark(false);
		}

		[IDECommand]
		public void Cmd_PrevBookmarkInFolder()
		{
			mBookmarkManager.PrevBookmark(true);
		}

		[IDECommand]
		public void Cmd_NextBookmark()
		{
			mBookmarkManager.NextBookmark(false);
		}

		[IDECommand]
		public void Cmd_NextBookmarkInFolder()
		{
			mBookmarkManager.NextBookmark(true);
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
				Fail("Cannot clean while compiling");
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
				Fail("Cannot clean while compiling");
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
		public void Cmd_MoveLine(VertDir dir)
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.MoveLine(dir);
		}

		[IDECommand]
		public void Cmd_MoveStatement(VertDir dir)
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.MoveStatement(dir);
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
			if (scale > 0.25f)
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
					scale += 0.02f; //0.05f;
				else //if (scale < 2.0f)
					scale += 0.04f; //0.10f;

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
		public void Cmd_QuickInfo()
		{
			var sourceViewPanel = GetActiveSourceViewPanel(true);
			if (sourceViewPanel != null)
			{
				if (sourceViewPanel.mEditWidget.mEditWidgetContent.GetCursorLineChar(var line, var lineChar))
					sourceViewPanel.UpdateMouseover(true, true, line, lineChar, true);
			}
		}

		[IDECommand]
		public void Cmd_ReformatDocument()
		{
			var sourceViewPanel = GetActiveSourceViewPanel(true);
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

		public void GoToDefinition(bool force)
		{
			var sourceViewPanel = GetActiveSourceViewPanel(false, true);
			if (sourceViewPanel != null)
			{
				if (!force)
				{
					if ((!sourceViewPanel.mIsBeefSource) || (sourceViewPanel.mProjectSource == null))
						return;
				}

				if ((!sourceViewPanel.mEditWidget.Content.GetCursorLineChar(var line, var lineChar)) && (!force))
					return;

				if (!sourceViewPanel.HasTextAtCursor())
					return;

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
				/*{
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
				else*/
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
			WithTabs(scope [?] (tabButton) =>
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
			Compile(.Normal);
		}

		void Compile(CompileKind compileKind)
		{
			CompilerLog("IDEApp.Compile");
			for (let project in gApp.mWorkspace.mProjects)
			{
				if (project.mDeferState != .None)
				{
					OutputErrorLine($"Project '{project.mProjectName}' is still loading.");
					return;
				}

				if (project.mFailed)
				{
					OutputErrorLine("Project '{}' is not loaded. Retry loading by right clicking on the project in the Workspace panel and selecting 'Retry Load'", project.mProjectName);
					return;
				}
			}

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
					if (compileKind == .DebugComptime)
						OutputLine("Compiling with comptime debugging...");
					else
						OutputLine("Compiling...");
					Compile(compileKind, null);
				}
			}
			else if ((mDebugger.mIsRunning) && (!mDebugger.HasLoadedTargetBinary()))
			{
				Compile(.WhileRunning, null);
			}
			else
			{
				mOutputPanel.Clear();
				OutputLine("Hot Compiling...");
				Project runningProject = mWorkspace.mStartupProject;
				Compile(compileKind, runningProject);
			}
		}

		[IDECommand]
		void RunWithStep()
		{
			mTargetStartWithStep = true;
			CompileAndRun(true);
		}

		[IDECommand]
		void StepInto()
		{
			if (mDebugger.mIsRunning)
			{
				if ((mExecutionPaused) && (mDebugger.IsPaused()))
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
				if ((mExecutionPaused) && (mDebugger.IsPaused()))
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
			if ((mExecutionPaused) && (mDebugger.IsPaused()))
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
				CompileAndRun(true);
			}
		}

		[IDECommand]
		public void DebugComptime()
		{
			if (mDebugger.mIsRunning)
				return;

			if (IsCompiling)
				return;

			CheckDebugVisualizers();
			Compile(.DebugComptime);
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
		void RunWithoutDebugging()
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
				CompileAndRun(false);
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
				mDebugger.mRunToCursorBreakpoint = sourceViewPanel.ToggleBreakpointAtCursor(.Force, .None, mDebugger.GetActiveThread());
			}
			else if (var disassemblyPanel = GetActiveDocumentPanel() as DisassemblyPanel)
			{
				mDebugger.mRunToCursorBreakpoint = disassemblyPanel.ToggleAddrBreakpointAtCursor(.Force, .None, mDebugger.GetActiveThread());
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
				CompileAndRun(true);
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
					else if (sourceViewPanel != null)
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

		void ToggleBreakpoint(WidgetWindow window, Breakpoint.SetKind setKind, Breakpoint.SetFlags setFlags, bool bindToThread = false)
		{
			var documentPanel = GetActiveDocumentPanel();

			if (var sourceViewPanel = documentPanel as SourceViewPanel)
			{
				sourceViewPanel = sourceViewPanel.GetFocusedEmbeddedView();
				sourceViewPanel.ToggleBreakpointAtCursor(setKind, setFlags, bindToThread ? gApp.mDebugger.GetActiveThread() : -1);
			}
			else if (var disassemblyPanel = documentPanel as DisassemblyPanel)
			{
				disassemblyPanel.ToggleBreakpointAtCursor(setKind, setFlags, bindToThread ? gApp.mDebugger.GetActiveThread() : -1);
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
			ToggleBreakpoint(GetCurrentWindow(), .Toggle, .None);
		}

		[IDECommand]
		public void ConfigureBreakpoint()
		{
			if (var breakpointPanel = GetActivePanel() as BreakpointPanel)
			{
				breakpointPanel.ConfigureBreakpoints(breakpointPanel.mWidgetWindow);
				return;
			}

			ToggleBreakpoint(GetCurrentWindow(), .EnsureExists, .Configure);
		}

		[IDECommand]
		public void DisableBreakpoint()
		{
			if (var breakpointPanel = GetActivePanel() as BreakpointPanel)
			{
				breakpointPanel.SetBreakpointsDisabled(null);
				return;
			}

			ToggleBreakpoint(GetCurrentWindow(), .MustExist, .Disable);
		}

		[IDECommand]
		void ToggleThreadBreakpoint()
		{
			ToggleBreakpoint(GetCurrentWindow(), .Toggle, .None, true);
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

		public void RecordHistoryLocation(bool includeLastActive = false)
		{
			var sourceViewPanel = GetActiveSourceViewPanel(includeLastActive);
			if (sourceViewPanel != null)
				sourceViewPanel.RecordHistoryLocation();
		}

		void ShowPanel(Panel panel, String label, bool setFocus = true)
		{
			if (!mInitialized)
				return;
#if !CLI
			if (setFocus)
				mLastActivePanel = panel;
			RecordHistoryLocation();
			ShowTab(panel, label, false, setFocus);
			if (setFocus)
				panel.FocusForKeyboard();

			if ((!panel.mWidgetWindow.mHasFocus) && (!mRunningTestScript))
			{
				bool hasFocus = false;
				BFWindow activeWindow = GetActiveWindow(true);
				BFWindow checkWindow = activeWindow;
				while (checkWindow != null)
				{
					if (checkWindow == panel.mWidgetWindow)
					{
						activeWindow.SetForeground();
						hasFocus = true;
						break;
					}
					checkWindow = checkWindow.mParent;
				}

				if (!hasFocus)
					panel.mWidgetWindow.SetForeground();
			}
#endif
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
		public void ShowErrors()
		{
			ShowPanel(mErrorsPanel, "Errors");
		}

		[IDECommand]
		public void ShowErrorNext()
		{
			mErrorsPanel.ShowErrorNext();
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
		public void ShowTerminal()
		{
#if BF_PLATFORM_WINDOWS
			ShowPanel(mTerminalPanel, "Terminal");
#endif
		}

		[IDECommand]
		public void ShowConsole()
		{
#if BF_PLATFORM_WINDOWS
			ShowPanel(mConsolePanel, "Console");
#endif
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
		public void ShowDiagnostics()
		{
			ShowPanel(mDiagnosticsPanel, "Diagnostics");
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
		public void ShowBookmarks()
		{
			ShowPanel(mBookmarksPanel, "Bookmarks");
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
					sourceViewPanel.GetDebugExpressionAt(ewc.mTextCursors.Front.mCursorTextPos, debugExpr);
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
		public void SelectConfig(String config)
		{
			mMainFrame.mStatusBar.SelectConfig(config);
		}

		[IDECommand]
		public void SelectConfig()
		{
			mMainFrame.mStatusBar.mConfigComboBox.ShowDropdown();
		}

		[IDECommand]
		public void SelectPlatform(String platform)
		{
			mMainFrame.mStatusBar.SelectPlatform(platform);
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
		public void ReloadSettings()
		{
			var prevSettings = mSettings;
			defer delete prevSettings;
			mSettings = new .(prevSettings);

			DeleteAndNullify!(mKeyChordState);

			mSettings.Load();
			mSettings.Apply();
			UpdateRecentFileMenuItems();
			UpdateRecentDisplayedFilesMenuItems();
		}

		public void CheckReloadSettings()
		{
			if (mSettings.WantsReload())
				ReloadSettings();
		}

		[IDECommand]
		public void ResetUI()
		{
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
				WithStandardPanels(scope (panel) =>
					{
						ResetPanel(panel);
					});
				mMainFrame.Reset();
			}

			mDockingFrame = mMainFrame.mDockingFrame;

			CreateDefaultLayout(false);
		}

		[IDECommand]
		public void SafeModeToggle()
		{
			mSafeMode = !mSafeMode;
			mNoResolve = mSafeMode;
			mWantsBeefClean = true;
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
			ShowPanel(mOutputPanel, "Output", false);
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

		void SyncWithWorkspacePanel()
		{
			var activeSourceViewPanel = GetActiveSourceViewPanel();
			if (activeSourceViewPanel != null)
				activeSourceViewPanel.SyncWithWorkspacePanel();
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
			TabbedView.TabButton activateTab = tabbedView.mTabs[0];
			for (var tab in tabbedView.mTabs)
			{
				if (tab.mIsActive)
				{
					if (@tab.Index < tabbedView.mTabs.Count - 1)
					{
						activateTab = tabbedView.mTabs[@tab.Index + 1];
						break;
					}
				}
			}
			activateTab.Activate();
			if (var sourceViewPanel = activateTab.mContent as SourceViewPanel)
				sourceViewPanel.HilitePosition(.Extra);
		}

		[IDECommand]
		void TabPrev()
		{
			var tabbedView = GetActiveTabbedView();
			if ((tabbedView == null) || (tabbedView.mTabs.IsEmpty))
				return;
			TabbedView.TabButton activateTab = tabbedView.mTabs.Back;
			for (var tab in tabbedView.mTabs)
			{
				if (tab.mIsActive)
				{
					if (@tab.Index > 0)
					{
						activateTab = tabbedView.mTabs[@tab.Index - 1];
						break;
					}
				}
			}
			activateTab.Activate();
			if (var sourceViewPanel = activateTab.mContent as SourceViewPanel)
				sourceViewPanel.HilitePosition(.Extra);
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
				if ((sourceViewPanel?.mQuickFind?.mIsShowingMatches == true) && (sourceViewPanel.mQuickFind.mFindEditWidget.mHasFocus))
				{
					sourceViewPanel.SetFocus();
					return;
				}

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

		void ScopePrev()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.ScopePrev();
		}

		void ScopeNext()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
				sewc.ScopeNext();
		}

		void ScrollDown()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
			{
				var scrollbar = sewc.mEditWidget.mVertScrollbar;
				if (scrollbar != null)
					scrollbar.Scroll(+1 * scrollbar.GetScrollIncrement());
			}
		}

		void ScrollUp()
		{
			var sewc = GetActiveSourceEditWidgetContent();
			if (sewc != null)
			{
				var scrollbar = sewc.mEditWidget.mVertScrollbar;
				if (scrollbar != null)
					scrollbar.Scroll(-1 * scrollbar.GetScrollIncrement());
			}
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

		public Menu AddMenuItem(Menu menu, StringView label, StringView command = default)
		{
			var command;
			if (command.IsEmpty)
				command = label;
			String labelStr = scope String(label);
			if (mCommands.mCommandMap.TryGetAlt(command, var matchKey, var ideCommand))
			{
				labelStr.Append("|");
				ideCommand.ToString(labelStr);
			}
			return menu.AddItem(labelStr);
		}

		public bool AreTestsRunning()
		{
			return (mTestManager != null);
		}

		[IDECommand]
		protected void RunTests(bool includeIgnored, bool debug)
		{
			var workspaceOptions = GetCurWorkspaceOptions();
			if (CurrentPlatform == .Wasm)
			{
				if (workspaceOptions.mBuildKind != .Test)
					mMainFrame.mStatusBar.SelectConfig("Test");
				CompileAndRun(true);
				return;
			}

			if (mOutputPanel != null)
			{
				ShowPanel(mOutputPanel, false);
				mOutputPanel.Clear();
			}

			if (AreTestsRunning())
			{
				OutputErrorLine("Tests already running");
				return;
			}

			if ((mDebugger != null) && (mDebugger.mIsRunning))
			{
				OutputErrorLine("Tests cannot be run while program is executing");
				return;
			}

			String prevConfigName = scope String(mConfigName);

			if (workspaceOptions.mBuildKind != .Test)
			{
				mMainFrame.mStatusBar.SelectConfig("Test");
			}

			workspaceOptions = GetCurWorkspaceOptions();
			if (workspaceOptions.mBuildKind != .Test)
			{
				mMainFrame.mStatusBar.SelectConfig(prevConfigName);
				OutputErrorLine("No valid Test workspace configuration exists");
				return;
			}

			var platformType = Workspace.PlatformType.GetFromName(gApp.mPlatformName, workspaceOptions.mTargetTriple);

			mLastTestFailed = false;
			mTestManager = new TestManager();
			mTestManager.mPrevConfigName = new String(prevConfigName);
			mTestManager.mDebug = debug && (platformType != .Wasm);
			mTestManager.mIncludeIgnored = includeIgnored;

			if (mOutputPanel != null)
				mOutputPanel.Clear();
			OutputLine("Compiling for testing...");
			if (!Compile(.Test, null))
			{
				mTestManager.BuildFailed();
			}
			if (!mTestManager.HasProjects)
			{
				OutputLineSmart("WARNING: No projects have a test configuration specified");
			}
		}

		[IDECommand]
		public void Cmd_TestEnableConsole()
		{
			let ideCommand = gApp.mCommands.mCommandMap["Test Enable Console"];
			ToggleCheck(ideCommand.mMenuItem, ref mTestEnableConsole);
		}

		[IDECommand]
		public void Cmd_SelectNextMatch()
		{
			GetActiveSourceEditWidgetContent()?.SelectNextMatch();
		}

		[IDECommand]
		public void Cmd_SkipCurrentMatchAndSelectNext()
		{
			GetActiveSourceEditWidgetContent()?.SkipCurrentMatchAndSelectNext();
		}

		public void UpdateMenuItem_HasActivePanel(IMenu menu)
		{
			menu.SetDisabled(GetActivePanel() == null);
		}

		public void UpdateMenuItem_HasActiveDocument(IMenu menu)
		{
			menu.SetDisabled(GetActiveDocumentPanel() == null);
		}

		public void UpdateMenuItem_HasLastActiveDocument(IMenu menu)
		{
			menu.SetDisabled(GetLastActiveDocumentPanel() == null);
		}

		public void UpdateMenuItem_HasWorkspace(IMenu menu)
		{
			menu.SetDisabled(!gApp.mWorkspace.IsInitialized);
		}

		public void UpdateMenuItem_DebugPaused(IMenu menu)
		{
			menu.SetDisabled(!mDebugger.mIsRunning || !mExecutionPaused);
		}

		public void UpdateMenuItem_DebugPausedOrStopped_HasWorkspace(IMenu menu)
		{
			if (mDebugger.mIsRunning)
				menu.SetDisabled(!mExecutionPaused);
			else
				menu.SetDisabled(!mWorkspace.IsInitialized);
		}

		public void UpdateMenuItem_DebugNotPaused(IMenu menu)
		{
			menu.SetDisabled(!mDebugger.mIsRunning || mExecutionPaused);
		}

		public void UpdateMenuItem_DebugRunning(IMenu menu)
		{
			menu.SetDisabled(!mDebugger.mIsRunning);
		}

		public void UpdateMenuItem_DebugOrTestRunning(IMenu menu)
		{
			menu.SetDisabled(!mDebugger.mIsRunning && (mTestManager == null));
		}

		public void UpdateMenuItem_DebugStopped_HasWorkspace(IMenu menu)
		{
			menu.SetDisabled(mDebugger.mIsRunning || !mWorkspace.IsInitialized);
		}

		public void UpdateMenuItem_DebugStopped(IMenu menu)
		{
			menu.SetDisabled(mDebugger.mIsRunning);
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

			AddMenuItem(subMenu, "&Save File", "Save File", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Save &As...", "Save As", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Save A&ll", "Save All");
			let prefMenu = subMenu.AddMenuItem("&Preferences");
			AddMenuItem(prefMenu, "&Settings", "Settings");
			AddMenuItem(prefMenu, "Reload Settings", "Reload Settings");
			AddMenuItem(prefMenu, "Reset UI", "Reset UI");
			AddMenuItem(prefMenu, "Safe Mode", "Safe Mode Toggle", new (menu) => { menu.SetCheckState(mSafeMode ? 1 : 0); }, null, true, mSafeMode ? 1 : 0);
			AddMenuItem(subMenu, "Close Workspace", "Close Workspace", new => UpdateMenuItem_HasWorkspace);
			AddMenuItem(subMenu, "E&xit", "Exit");

			//////////

			subMenu = root.AddMenuItem("&Edit");
			AddMenuItem(subMenu, "Quick &Find...", "Find in Document", new => UpdateMenuItem_HasActivePanel);
			AddMenuItem(subMenu, "Quick &Replace...", "Replace in Document", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Find in &Files...", "Find in Files");
			AddMenuItem(subMenu, "Replace in Files...", "Replace in Files");
			AddMenuItem(subMenu, "Find Prev", "Find Prev", new => UpdateMenuItem_HasActivePanel);
			AddMenuItem(subMenu, "Find Next", "Find Next", new => UpdateMenuItem_HasActivePanel);
			AddMenuItem(subMenu, "Show &Current", "Show Current");

			AddMenuItem(subMenu, "&Goto Line...", "Goto Line", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Goto &Method...", "Goto Method", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "&Rename Symbol", "Rename Symbol", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Show Fi&xit", "Show Fixit", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Find &All References", "Find All References", new => UpdateMenuItem_HasActiveDocument);
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

			var comptimeMenu = subMenu.AddMenuItem("Comptime");
			var emitViewCompiler = comptimeMenu.AddMenuItem("Emit View Compiler");
			var subItem = emitViewCompiler.AddMenuItem("Resolve", null,
				new (menu) => { SetEmbedCompiler(.Resolve); },
				new (menu) => { menu.SetCheckState((mSettings.mEditorSettings.mEmitCompiler == .Resolve) ? 1 : 0); },
				null, true, (mSettings.mEditorSettings.mEmitCompiler == .Resolve) ? 1 : 0);
			subItem = emitViewCompiler.AddMenuItem("Build", null,
				new (menu) => { SetEmbedCompiler(.Build); },
				new (menu) => { menu.SetCheckState((mSettings.mEditorSettings.mEmitCompiler == .Build) ? 1 : 0); },
				null, true, (mSettings.mEditorSettings.mEmitCompiler == .Build) ? 1 : 0);

			var advancedEditMenu = subMenu.AddMenuItem("Advanced");
			AddMenuItem(advancedEditMenu, "Duplicate Line", "Duplicate Line");
			AddMenuItem(advancedEditMenu, "Move Line Up", "Move Line Up");
			AddMenuItem(advancedEditMenu, "Move Line Down", "Move Line Down");
			AddMenuItem(advancedEditMenu, "Move Statement Up", "Move Statement Up");
			AddMenuItem(advancedEditMenu, "Move Statement Down", "Move Statement Down");
			advancedEditMenu.AddMenuItem(null);
			AddMenuItem(advancedEditMenu, "Make Uppercase", "Make Uppercase");
			AddMenuItem(advancedEditMenu, "Make Lowercase", "Make Lowercase");
			AddMenuItem(advancedEditMenu, "Comment Block", "Comment Block");
			AddMenuItem(advancedEditMenu, "Comment Lines", "Comment Lines");
			AddMenuItem(advancedEditMenu, "Comment Toggle", "Comment Toggle");
			AddMenuItem(advancedEditMenu, "Uncomment Selection", "Uncomment Selection");
			AddMenuItem(advancedEditMenu, "Reformat Document", "Reformat Document");
			mViewWhiteSpace.mMenu = AddMenuItem(advancedEditMenu, "View White Space", "View White Space", null, null, true, mViewWhiteSpace.Bool ? 1 : 0);

			if (mSettings.mEnableDevMode)
			{
				subMenu.AddMenuItem(null);
				var internalEditMenu = subMenu.AddMenuItem("Internal");
				internalEditMenu.AddMenuItem("Hilight Cursor References", null, new (menu) => { ToggleCheck(menu, ref gApp.mSettings.mEditorSettings.mHiliteCursorReferences); }, null, null, true, gApp.mSettings.mEditorSettings.mHiliteCursorReferences ? 1 : 0);
				internalEditMenu.AddMenuItem("Delayed Autocomplete", null, new (menu) => { ToggleCheck(menu, ref gApp.mDbgDelayedAutocomplete); }, null, null, true, gApp.mDbgDelayedAutocomplete ? 1 : 0);
				internalEditMenu.AddMenuItem("Time Autocomplete", null, new (menu) => { ToggleCheck(menu, ref gApp.mDbgTimeAutocomplete); }, null, null, true, gApp.mDbgTimeAutocomplete ? 1 : 0);
				internalEditMenu.AddMenuItem("Perf Autocomplete", null, new (menu) => { ToggleCheck(menu, ref gApp.mDbgPerfAutocomplete); }, null, null, true, gApp.mDbgPerfAutocomplete ? 1 : 0);
				internalEditMenu.AddMenuItem("Dump Undo Buffer", null, new (menu) =>
					{
						if (var panel = GetActiveSourceViewPanel())
						{
							var str = panel.mEditWidget.mEditWidgetContent.mData.mUndoManager.ToString(.. scope .());
							Debug.WriteLine(str);
						}
					}, null, null, true, gApp.mDbgPerfAutocomplete ? 1 : 0);
			}

			//////////

			subMenu = root.AddMenuItem("&View");
			AddMenuItem(subMenu, "AutoComplet&e", "Show Autocomplete Panel");
			AddMenuItem(subMenu, "&Auto Watches", "Show Auto Watches");
			AddMenuItem(subMenu, "Boo&kmarks", "Show Bookmarks");
			AddMenuItem(subMenu, "&Breakpoints", "Show Breakpoints");
			AddMenuItem(subMenu, "&Call Stack", "Show Call Stack");
			AddMenuItem(subMenu, "C&lass View", "Show Class View");
			AddMenuItem(subMenu, "&Diagnostics", "Show Diagnostics");
			AddMenuItem(subMenu, "E&rrors", "Show Errors");
			AddMenuItem(subMenu, "&Find Results", "Show Find Results");
			AddMenuItem(subMenu, "&Terminal", "Show Terminal");
			AddMenuItem(subMenu, "Co&nsole", "Show Console");
			AddMenuItem(subMenu, "&Immediate Window", "Show Immediate");
			AddMenuItem(subMenu, "&Memory", "Show Memory");
			AddMenuItem(subMenu, "Mod&ules", "Show Modules");
			AddMenuItem(subMenu, "&Output", "Show Output");
			AddMenuItem(subMenu, "&Profiler", "Show Profiler");
			AddMenuItem(subMenu, "T&hreads", "Show Threads");
			AddMenuItem(subMenu, "&Watches", "Show Watches");
			AddMenuItem(subMenu, "Work&space Explorer", "Show Workspace Explorer");
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "Next Document Panel", "Next Document Panel");
			AddMenuItem(subMenu, "Navigate Backwards", "Navigate Backwards");
			AddMenuItem(subMenu, "Navigate Forwards", "Navigate Forwards");

			//////////

			subMenu = root.AddMenuItem("&Build");
			AddMenuItem(subMenu, "&Build Workspace", "Build Workspace", new => UpdateMenuItem_HasWorkspace);
			AddMenuItem(subMenu, "&Debug Comptime", "Debug Comptime", new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(subMenu, "&Clean", "Clean", new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(subMenu, "Clean Beef", "Clean Beef", new => UpdateMenuItem_DebugStopped_HasWorkspace);
			//subMenu.AddMenuItem("Compile Current File", null, new (menu) => { CompileCurrentFile(); });
			AddMenuItem(subMenu, "Cancel Build", "Cancel Build", new (menu) => { menu.SetDisabled(!IsCompiling); });

			subMenu.AddMenuItem("Verbose", null, new (menu) =>
				{
					if (mVerbosity != .Diagnostic)
						mVerbosity = .Diagnostic;
					else
						mVerbosity = .Normal;
					var sysMenu = (SysMenu)menu;
					sysMenu.Modify(null, null, null, true, (mVerbosity == .Diagnostic) ? 1 : 0);
				}, null, null, true, (mVerbosity == .Diagnostic) ? 1 : 0);

			if (mSettings.mEnableDevMode)
			{
				var internalBuildMenu = subMenu.AddMenuItem("Internal");
				internalBuildMenu.AddMenuItem("Autobuild (Debug)", null, new (menu) => { mDebugAutoBuild = !mDebugAutoBuild; });
				internalBuildMenu.AddMenuItem("Autorun (Debug)", null, new (menu) => { mDebugAutoRun = !mDebugAutoRun; });
				internalBuildMenu.AddMenuItem("Disable Compiling", null, new (menu) => { ToggleCheck(menu, ref mDisableBuilding); }, null, null, true, mDisableBuilding ? 1 : 0);
			}

			//////////

			subMenu = root.AddMenuItem("&Debug");
			AddMenuItem(subMenu, "&Start Debugging", "Start Debugging", new (item) =>
				{
					SysMenu sysMenu = (.)item;
					if (mDebugger.mIsRunning)
						sysMenu.Modify("&Continue", sysMenu.mHotKey, null, mDebugger.IsPaused());
					else
						sysMenu.Modify("&Start Debugging", sysMenu.mHotKey, null, mWorkspace.IsInitialized);
				});
			AddMenuItem(subMenu, "Start Wit&hout Debugging", "Start Without Debugging", new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(subMenu, "Start With&out Compiling", "Start Without Compiling", new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(subMenu, "&Launch Process...", "Launch Process", new => UpdateMenuItem_DebugStopped);
			AddMenuItem(subMenu, "&Attach to Process...", "Attach to Process", new => UpdateMenuItem_DebugStopped);
			AddMenuItem(subMenu, "&Stop Debugging", "Stop Debugging", new => UpdateMenuItem_DebugOrTestRunning);
			AddMenuItem(subMenu, "Break All", "Break All", new => UpdateMenuItem_DebugNotPaused);
			AddMenuItem(subMenu, "Remove All Breakpoints", "Remove All Breakpoints");
			AddMenuItem(subMenu, "Show &Disassembly", "Show Disassembly");
			AddMenuItem(subMenu, "&Quick Watch", "Show QuickWatch", new => UpdateMenuItem_DebugPaused);
			AddMenuItem(subMenu, "&Profile", "Profile", new => UpdateMenuItem_HasWorkspace);
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "Step Into", "Step Into", new => UpdateMenuItem_DebugPausedOrStopped_HasWorkspace);
			AddMenuItem(subMenu, "Step Over", "Step Over", new => UpdateMenuItem_DebugPausedOrStopped_HasWorkspace);
			AddMenuItem(subMenu, "Step Out", "Step Out", new => UpdateMenuItem_DebugPaused);
			subMenu.AddMenuItem(null);
			AddMenuItem(subMenu, "To&ggle Breakpoint", "Breakpoint Toggle", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(subMenu, "Toggle Thread Breakpoint", "Breakpoint Toggle Thread", new => UpdateMenuItem_HasActiveDocument);
			var newBreakpointMenu = subMenu.AddMenuItem("New &Breakpoint");
			AddMenuItem(newBreakpointMenu, "&Memory Breakpoint...", "Breakpoint Memory", new => UpdateMenuItem_DebugRunning);
			AddMenuItem(newBreakpointMenu, "&Symbol Breakpoint...", "Breakpoint Symbol");

			if (mSettings.mEnableDevMode)
			{
				var internalDebugMenu = subMenu.AddMenuItem("Internal");
				internalDebugMenu.AddMenuItem("Error Test", null, new (menu) => { DoErrorTest(); });
				internalDebugMenu.AddMenuItem("Reconnect BeefPerf", null, new (menu) => { BeefPerf.RetryConnect(); });
				AddMenuItem(internalDebugMenu, "Report Memory", "Report Memory");
				internalDebugMenu.AddMenuItem("Crash", null, new (menu) => { int* ptr = null; *ptr = 123; });
				internalDebugMenu.AddMenuItem("Show Welcome", null, new (menu) => { ShowWelcome(); });
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
			var testRunMenu = testMenu.AddMenuItem("&Run", null, null, new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(testRunMenu, "&Normal Tests", "Run Normal Tests");
			AddMenuItem(testRunMenu, "&All Tests", "Run All Tests");


			var testDebugMenu = testMenu.AddMenuItem("&Debug", null, null, new => UpdateMenuItem_DebugStopped_HasWorkspace);
			AddMenuItem(testDebugMenu, "&Normal Tests", "Debug Normal Tests");
			AddMenuItem(testDebugMenu, "&All Tests", "Debug All Tests");
			testDebugMenu.AddMenuItem(null);
			testDebugMenu.AddMenuItem("Break on Failure", null, new (menu) =>
				{
					ToggleCheck(menu, ref mTestBreakOnFailure);
				}, null, null, true, mTestBreakOnFailure ? 1 : 0);

			AddMenuItem(testMenu, "Enable Console", "Test Enable Console", null, null, true, mTestEnableConsole ? 1 : 0);

			//////////

			mWindowMenu = root.AddMenuItem("&Window");
			AddMenuItem(mWindowMenu, "&Close Document", "Close Document", new => UpdateMenuItem_HasLastActiveDocument);
			AddMenuItem(mWindowMenu, "Close &Panel", "Close Panel", new => UpdateMenuItem_HasActivePanel);
			AddMenuItem(mWindowMenu, "&Close All", "Close All Panels");
			AddMenuItem(mWindowMenu, "Close All Except Current", "Close All Panels Except");
			AddMenuItem(mWindowMenu, "&New View into File", "View New", new => UpdateMenuItem_HasActiveDocument);
			AddMenuItem(mWindowMenu, "&Split View", "View Split", new => UpdateMenuItem_HasActiveDocument);

			subMenu = root.AddMenuItem("&Help");
			AddMenuItem(subMenu, "&About", "About");
		}

		IDETabbedView CreateTabbedView()
		{
			return new IDETabbedView(null);
		}

		public void SetupNewWindow(WidgetWindow window, bool isMainWindow)
		{
			window.mOnWindowKeyDown.Add(new => SysKeyDown);
			window.mOnWindowKeyUp.Add(new => SysKeyUp);
			window.mOnMouseUp.Add(new => MouseUp);
			if (isMainWindow)
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

		public void WithDocumentTabbedViewsOf(BFWindow window, delegate void(DarkTabbedView) func)
		{
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

		public void WithDocumentTabbedViews(delegate void(DarkTabbedView) func)
		{
			for (let window in mWindows)
				WithDocumentTabbedViewsOf(window, func);
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

		public SourceEditWidgetContent GetActiveSourceEditWidgetContent()
		{
			let activeWindow = GetActiveWindow();
			if (activeWindow.mFocusWidget != null)
			{
				if (let editWidget = activeWindow.mFocusWidget as EditWidget)
				{
					let sewc = editWidget.mEditWidgetContent as SourceEditWidgetContent;
					if (sewc != null)
					{
						if (sewc.mEditWidget.mHasFocus)
							return sewc;
						return null;
					}
				}
			}

			var activeTextPanel = GetActivePanel() as TextPanel;
			if (activeTextPanel != null)
			{
				let sewc = activeTextPanel.EditWidget.mEditWidgetContent as SourceEditWidgetContent;
				if ((sewc != null) && (sewc.mEditWidget.mHasFocus))
					return sewc;
			}

			return null;
		}

		public WidgetWindow GetActiveWindow(bool allowModal = false)
		{
			for (let window in mWindows)
				if (window.mHasFocus)
				{
					var result = window;
					while ((result.mWindowFlags.HasFlag(.Modal)) && (result.mParent != null) && (!allowModal))
						result = result.mParent;
					return result as WidgetWindow;
				}
			return mMainWindow;
		}

		public Widget GetActiveDocumentPanel()
		{
			var activePanel = GetActivePanel();
			if ((activePanel is SourceViewPanel) || (activePanel is DisassemblyPanel))
				return activePanel;
			return null;
		}

		public Widget GetLastActiveDocumentPanel()
		{
			var activePanel = GetActiveDocumentPanel();
			if (activePanel != null)
				return activePanel;
			if (mActiveDocumentsTabbedView != null)
			{
				let activeTab = mActiveDocumentsTabbedView.GetActiveTab();
				if (activeTab != null)
				{
					var lastActivePanel = activeTab.mContent;
					if ((lastActivePanel is SourceViewPanel) || (lastActivePanel is DisassemblyPanel))
						return lastActivePanel;
				}
			}
			return null;
		}

		public void WithTabsOf(BFWindow window, delegate void(TabbedView.TabButton) func)
		{
			WithDocumentTabbedViewsOf(window, scope (documentTabbedView) =>
				{
					documentTabbedView.WithTabs(func);
				});
		}

		public void WithTabs(delegate void(TabbedView.TabButton) func)
		{
			for (let window in mWindows)
				WithTabsOf(window, func);
		}

		public TabbedView.TabButton GetTab(Widget content)
		{
			TabbedView.TabButton tab = null;
			WithTabs(scope [?] (checkTab) =>
				{
					if (checkTab.mContent == content)
						tab = checkTab;
				});
			return tab;
		}

		public void WithSourceViewPanelsOf(BFWindow window, delegate void(SourceViewPanel) func)
		{
			WithTabsOf(window, scope (tab) =>
				{
					var sourceViewPanel = tab.mContent as SourceViewPanel;
					if (sourceViewPanel != null)
						func(sourceViewPanel);
				});
		}

		public void WithSourceViewPanels(delegate void(SourceViewPanel) func)
		{
			for (let window in mWindows)
				WithSourceViewPanelsOf(window, func);
		}

		TabbedView.TabButton SetupTab(TabbedView tabView, String name, float width, Widget content, bool ownsContent) // 2
		{
			TabbedView.TabButton tabButton = tabView.AddTab(name, width, content, ownsContent, GetTabInsertIndex(tabView));
			if ((var panel = content as Panel) && (var darkTabButton = tabButton as DarkTabbedView.DarkTabButton))
			{
				darkTabButton.mTabWidthOffset = panel.TabWidthOffset;
			}
			tabButton.mCloseClickedEvent.Add(new () => CloseDocument(content)); // 1
			return tabButton;
		}

		public DisassemblyPanel ShowDisassemblyPanel(bool clearData = false, bool setFocus = false)
		{
			DisassemblyPanel disassemblyPanel = null;
			TabbedView.TabButton disassemblyTab = null;

			WithTabs(scope [&] (tab) =>
				{
					if ((disassemblyPanel == null) && (tab.mContent is DisassemblyPanel))
					{
						disassemblyTab = tab;
						disassemblyPanel = (DisassemblyPanel)tab.mContent;
					}
				});

			if (disassemblyTab != null)
			{
				var window = disassemblyTab.mWidgetWindow;
				if ((setFocus) && (window != null) && (!HasModalDialogs()) && (!mRunningTestScript))
					window.SetForeground();
			}

			if (disassemblyPanel != null)
			{
				disassemblyPanel.ClearQueuedData();
				disassemblyTab.Activate();
				return disassemblyPanel;
			}

			TabbedView tabbedView = GetDefaultDocumentTabbedView();
			disassemblyPanel = new DisassemblyPanel();
			//diassemblyPanel.Show(filePath);

			var newTabButton = new SourceViewTabButton();
			newTabButton.Label = DisassemblyPanel.sPanelName;
			newTabButton.mWantWidth = newTabButton.GetWantWidth();
			newTabButton.mHeight = tabbedView.mTabHeight;
			newTabButton.mContent = disassemblyPanel;
			tabbedView.AddTab(newTabButton, GetTabInsertIndex(tabbedView));

			newTabButton.mCloseClickedEvent.Add(new () => CloseDocument(disassemblyPanel));
			newTabButton.Activate();
			//diassemblyPanel.FocusEdit();

			mLastActivePanel = disassemblyPanel;
			return disassemblyPanel;
		}

		int GetTabInsertIndex(TabbedView tabs)
		{
			if (mSettings.mUISettings.mInsertNewTabs == .RightOfExistingTabs)
			{
				return tabs.mTabs.Count;
			}

			// Find right-most non-pinned tab
			// after which we will put our new tab
			int index = 0;
			for (index = 0; index < tabs.mTabs.Count; index++)
			{
				if (tabs.mTabs[index].mIsPinned == false)
				{
					break;
				}
			}

			return index;
		}

		public class SourceViewTabButton : DarkTabbedView.DarkTabButton
		{
			public bool mIsTemp;

			public float GetWantWidth()
			{
				return DarkTheme.sDarkTheme.mSmallFont.GetWidth(mLabel) + DarkTheme.GetScaled(40);
			}

			public override void Activate(bool setFocus = true)
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

				if (mIsTemp)
				{
					using (g.PushColor(0x80404070))
						g.FillRect(0, 0, mWidth, mHeight);
				}
			}

			public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
			{
				if ((mIsRightTab) && (btn == 0) && (btnCount > 1))
				{
					IDEApp.sApp.MakeTabPermanent(this);
					return;
				}

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
						item = menu.AddItem("Open in Terminal");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								let directory = scope String();
								Path.GetDirectoryPath(sourceViewPanel.mFilePath, directory);

								ProcessStartInfo procInfo = scope ProcessStartInfo();
								procInfo.UseShellExecute = true;
								procInfo.SetFileName(gApp.mSettings.mWindowsTerminal);
								procInfo.SetWorkingDirectory(directory);

								let process = scope SpawnedProcess();
								process.Start(procInfo).IgnoreError();
							});
						item = menu.AddItem("Show in Workspace Panel");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								sourceViewPanel.SyncWithWorkspacePanel();
							});
						item = menu.AddItem("Close");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								mCloseClickedEvent();
							});
						item = menu.AddItem("Close All Except This");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								mTabbedView.CloseTabs(false, false, true);
							});
						item = menu.AddItem("Close All Except Pinned");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								mTabbedView.CloseTabs(false, true, false);
							});

						item = menu.AddItem(this.mIsPinned ? "Unpin Tab" : "Pin Tab");
						item.mOnMenuItemSelected.Add(new (menu) =>
							{
								if (mIsRightTab)
									IDEApp.sApp.MakeTabPermanent(this);

								mTabbedView.TogglePinned(this);
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
			}

			public override void Update()
			{
				base.Update();
				Point point;
				if (DarkTooltipManager.CheckMouseover(this, 25, out point))
				{
					var sourceViewPanel = mContent as SourceViewPanel;
					if ((sourceViewPanel != null) && (sourceViewPanel.mFilePath != null))
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
			darkTabbedView.AddTab(tabButton, GetTabInsertIndex(darkTabbedView));
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
			editWidgetContent.mHiliteColor = mSettings.mUISettings.mColors.mCodeHilite;
			editWidgetContent.mUnfocusedHiliteColor = mSettings.mUISettings.mColors.mCodeHiliteUnfocused;
			editWidgetContent.mHiliteCurrentLine = mSettings.mEditorSettings.mHiliteCurrentLine;

			return editWidget;
		}

		public bool CreateEditDataEditWidget(FileEditData editData)
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
							char8 nextC = 0;
							if (i < text.Length - 1)
							{
								nextC = text[++i];
								if (nextC == 0)
								{
									if (i < text.Length - 2)
										nextC = text[++i];
								}
							}

							if (nextC == '\n')
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
					editData.BuildHash(text);
				}) case .Err)
				return false;
			editData..GetFileTime();

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

		public FileEditData GetEditData(String filePath, bool createEditData = true, bool createEditDataWidget = true)
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
						editData.mFilePath = new String(filePath);
						mFileEditData.Add(new String(fixedFilePath), editData);
					}
				}
			}
			if (createEditDataWidget)
				CreateEditDataEditWidget(editData);
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
				if (mFileEditData.TryGet(oldFixedFilePath, out outKey, out editData))
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

		public (SourceViewPanel panel, TabbedView.TabButton tabButton) ShowSourceFile(String filePath, ProjectSource projectSource = null, SourceShowType showType = SourceShowType.ShowExisting, bool setFocus = true)
		{
			DeleteAndNullify!(mDeferredShowSource);

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

			int32 emitRevision = -1;

			if (useFilePath != null)
			{
				int barPos = useFilePath.IndexOf('|');
				if (barPos != -1)
				{
					emitRevision = int32.Parse(useFilePath.Substring(barPos + 1)).Value;
					useFilePath.RemoveToEnd(barPos);
				}
			}

			if ((useFilePath != null) && (!IDEUtils.FixFilePath(useFilePath)))
				return (null, null);
			if ((useFilePath == null) & (showType != .New))
				return (null, null);

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

			void ActivateWindow(WidgetWindow window)
			{
				if ((setFocus) && (window != null) && (!HasModalDialogs()) && (!mRunningTestScript))
					window.SetForeground();
			}

			if (showType != SourceShowType.New)
			{
				delegate void(TabbedView.TabButton) tabFunc = scope [&] (tabButton) =>
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

					ActivateWindow(sourceViewPanelTab.mWidgetWindow);
					sourceViewPanelTab.Activate(setFocus);
					sourceViewPanelTab.mTabbedView.FinishTabAnim();
					if (setFocus)
						sourceViewPanel.FocusEdit();

					sourceViewPanel.CheckEmitRevision();
				}
			}

			if (sourceViewPanel != null)
				return (sourceViewPanel, sourceViewPanelTab);

			//ShowSourceFile(filePath, projectSource, showTemp, setFocus);

			DarkTabbedView tabbedView = GetDefaultDocumentTabbedView();
			ActivateWindow(tabbedView.mWidgetWindow);
			sourceViewPanel = new SourceViewPanel();
			bool success;
			if (useProjectSource != null)
			{
				success = sourceViewPanel.Show(useProjectSource, !mInitialized);
			}
			else
				success = sourceViewPanel.Show(useFilePath, !mInitialized);
			sourceViewPanel.mEmitRevision = emitRevision;
			if (emitRevision != -1)
				sourceViewPanel.mEditWidget.mEditWidgetContent.mIsReadOnly = true;

			if (!success)
			{
				sourceViewPanel.Close();
				delete sourceViewPanel;
				return (null, null);
			}

			var newTabButton = new SourceViewTabButton();
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
				let prevAutoClose = tabbedView.mAutoClose;
				defer { tabbedView.mAutoClose = prevAutoClose; }
				tabbedView.mAutoClose = false;

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
				tabbedView.AddTab(newTabButton, GetTabInsertIndex(tabbedView));
			newTabButton.mCloseClickedEvent.Add(new () => DocumentCloseClicked(sourceViewPanel));
			newTabButton.Activate(setFocus);
			if ((setFocus) && (sourceViewPanel.mWidgetWindow != null))
				sourceViewPanel.FocusEdit();

			return (sourceViewPanel, newTabButton);
		}

		int GetRecentFilesIdx(String filePath)
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

			RecentFiles.UpdateMenu(mRecentlyDisplayedFiles, mWindowMenu, mRecentlyDisplayedFilesMenuItems, true, scope (idx, sysMenu) =>
				{
					sysMenu.mOnMenuItemSelected.Add(new (evt) => ShowRecentFile(idx));
				});
		}

		public void UpdateRecentFileMenuItems(RecentFiles.RecentKind recentKind)
		{
			let entry = mSettings.mRecentFiles.mRecents[(int)recentKind];
			if (entry.mMenu == null)
				return;

			RecentFiles.UpdateMenu(entry.mList, entry.mMenu, entry.mMenuItems, false, scope (idx, sysMenu) =>
				{
					sysMenu.mOnMenuItemSelected.Add(new (evt) => ShowRecentFile(recentKind, idx));
				});

			entry.mMenu.SetDisabled(entry.mMenuItems.Count == 0);
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
			RecentFiles.Add(mRecentlyDisplayedFiles, path, 20);
			UpdateRecentDisplayedFilesMenuItems();
		}

		public void ShowRecentFileNext()
		{
			if (mRecentFileSelector == null)
			{
				mRecentFileSelector = new RecentFileSelector();
				mRecentFileSelector.Show();
			}
			else
				mRecentFileSelector.Next();
		}

		public void ShowRecentFilePrev()
		{
			if (mRecentFileSelector == null)
			{
				mRecentFileSelector = new RecentFileSelector();
				mRecentFileSelector.Show();
			}
			else
				mRecentFileSelector.Prev();
		}

		void ShowRecentFile(int idx, bool setFocus = true, bool checkIfExists = false)
		{
			if (idx >= mRecentlyDisplayedFiles.Count)
				return;

			String sourceFile = mRecentlyDisplayedFiles[idx];
			if (sourceFile == DisassemblyPanel.sPanelName)
			{
				ShowDisassemblyPanel();
				return;
			}
			if ((checkIfExists) && (!File.Exists(sourceFile)))
				return;
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
			Dialog aDialog = ThemeFactory.mDefault.CreateDialog("Save file?", scope String()..AppendF("Save changes to '{0}' before closing?", fileName), DarkTheme.sDarkTheme.mIconWarning);
			aDialog.mDefaultButton = aDialog.AddButton("Save", new (evt) => { SaveFile(sourceViewPanel); CloseDocument(sourceViewPanel); });
			aDialog.AddButton("Don't Save", new (evt) => CloseDocument(sourceViewPanel));
			aDialog.mEscButton = aDialog.AddButton("Cancel");
			aDialog.PopupWindow(mMainWindow);
		}

		void RevertSourceViewPanel(SourceViewPanel sourceViewPanel)
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
				RevertSourceViewPanel(sourceViewPanel);

			DarkTabbedView tabbedView = null;
			DarkTabbedView.DarkTabButton tabButton = null;
			WithTabs(scope [?] (tab) =>
				{
					if (tab.mContent == documentPanel)
					{
						tabbedView = (DarkTabbedView)tab.mTabbedView;
						tabButton = (DarkTabbedView.DarkTabButton)tab;
					}
				});

			if (tabbedView == null)
				return;

			int recentFileIdx = -1;
			if (sourceViewPanel != null)
			{
				if (sourceViewPanel.[Friend]NeedsPostRemoveUpdate)
				{
					sourceViewPanel.[Friend]mDeleteAfterPostRemoveUpdate = true;
					tabButton.mContent.RemoveSelf();
					tabButton.mContent = null;
				}
				else
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

			if (tabButton.mIsActive)
			{
				// If this succeeds then tabbUtton.mIsActive will be false, otherwise we do the 'nextTab' logic below
				ShowRecentFile(0, hasFocus, true);
			}

			TabbedView.TabButton nextTab = null;
			bool foundRemovedTab = false;
			// Select the previous tab or the next one (if this is the first)
			if (tabButton.mIsActive)
			{
				tabbedView.WithTabs(scope [&] (checkTab) =>
					{
						if (checkTab == tabButton)
							foundRemovedTab = true;
						else if ((!foundRemovedTab) || (nextTab == null))
							nextTab = checkTab;
					});
			}
			else
			{
				nextTab = tabbedView.GetActiveTab();
			}

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
			var activeDocumentPanel = GetLastActiveDocumentPanel();
			if (activeDocumentPanel != null)
			{
				if (activeDocumentPanel is SourceViewPanel)
				{
					var sourceViewPanel = (SourceViewPanel)activeDocumentPanel;
					DocumentCloseClicked(sourceViewPanel);
					return;
				}
				CloseDocument(activeDocumentPanel);
			}
		}

		void TryCloseCurrentPanel()
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

		void TryCloseAllDocuments(bool closeCurrent)
		{
			var docPanels = scope List<Widget>();

			Widget skipDocumentPanel = null;
			if (!closeCurrent)
				skipDocumentPanel = GetActiveDocumentPanel();

			WithTabs(scope [&] (tab) =>
				{
					if ((tab.mContent is SourceViewPanel) || (tab.mTabbedView.mIsFillWidget))
					{
						if (tab.mContent != skipDocumentPanel)
							docPanels.Add(tab.mContent);
					}
				});

			for (var docPanel in docPanels)
				DocumentCloseClicked(docPanel);
		}

		void ViewSplit()
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

		public void CloseOldBeefManaged()
		{
			List<SourceViewPanel> pendingClosePanels = scope .();
			WithSourceViewPanels(scope (sourceViewPanel) =>
				{
					if (sourceViewPanel.mProjectSource != null)
					{
						var checkHash = gApp.mPackMan.GetHashFromFilePath(sourceViewPanel.mFilePath, .. scope .());
						if (!checkHash.IsEmpty)
						{
							bool foundHash = false;

							if (gApp.mWorkspace.mProjectLockMap.TryGet(sourceViewPanel.mProjectSource.mProject.mProjectName, ?, var lock))
							{
								if (lock case .Git(let url, let tag, let hash))
								{
									if (hash == checkHash)
										foundHash = true;
								}
							}

							if (!foundHash)
								pendingClosePanels.Add(sourceViewPanel);
						}
					}
				});

			for (var sourceViewPanel in pendingClosePanels)
				CloseDocument(sourceViewPanel);
		}

		public SourceViewPanel ShowProjectItem(ProjectItem projectItem, bool showTemp = true, bool setFocus = true)
		{
			if (projectItem is ProjectSource)
			{
				var projectSource = (ProjectSource)projectItem;
				var fullPath = scope String();
				projectSource.GetFullImportPath(fullPath);
				return ShowSourceFile(fullPath, projectSource, showTemp ? SourceShowType.Temp : SourceShowType.ShowExistingInActivePanel, setFocus).panel;
			}
			return null;
		}

		public SourceViewPanel ShowSourceFileLocation(String filePath, int showHotIdx, int refHotIdx, int line, int column, LocatorType hilitePosition, bool showTemp = false)
		{
			var useFilePath = filePath;

			if (filePath.StartsWith("$Emit$"))
			{
				if ((mBfBuildCompiler.IsPerformingBackgroundOperation()) || (mBfResolveCompiler.IsPerformingBackgroundOperation()))
				{
					DeleteAndNullify!(mDeferredShowSource);
					mDeferredShowSource = new DeferredShowSource()
						{
							mFilePath = new .(filePath),
							mShowHotIdx = (.)showHotIdx,
							mRefHotIdx = (.)refHotIdx,
							mLine = (.)line,
							mColumn = (.)column,
							mHilitePosition = hilitePosition,
							mShowTemp = showTemp
						};
					return null;
				}

				String embedFilePath;
				bool isViewValid = true;
				StringView typeName;
				int embedLine;
				int embedLineChar;

				if (filePath.StartsWith("$Emit$Resolve$"))
				{
					if (gApp.mSettings.mEditorSettings.mEmitCompiler == .Resolve)
					{
						var itr = filePath.Split('$');
						itr.GetNext();
						itr.GetNext();
						itr.GetNext();
						typeName = itr.GetNext().Value;

						mBfResolveCompiler.mBfSystem.Lock(0);
						embedFilePath = mBfResolveCompiler.GetEmitLocation(typeName, line, .. scope:: .(), out embedLine, out embedLineChar, var embedHash);
						mBfResolveCompiler.mBfSystem.Unlock();

						useFilePath = scope:: $"$Emit${useFilePath.Substring("$Emit$Resolve$".Length)}";
					}
					else
						isViewValid = false;
				}
				else if (filePath.StartsWith("$Emit$Build$"))
				{
					if (gApp.mSettings.mEditorSettings.mEmitCompiler == .Build)
					{
						var itr = filePath.Split('$');
						itr.GetNext();
						itr.GetNext();
						itr.GetNext();
						typeName = itr.GetNext().Value;

						mBfBuildCompiler.mBfSystem.Lock(0);
						embedFilePath = mBfBuildCompiler.GetEmitLocation(typeName, line, .. scope:: .(), out embedLine, out embedLineChar, var embedHash);
						mBfBuildCompiler.mBfSystem.Unlock();

						useFilePath = scope:: $"$Emit${useFilePath.Substring("$Emit$Build$".Length)}";
					}
					else
						isViewValid = false;
				}
				else
				{
					var itr = filePath.Split('$');
					itr.GetNext();
					itr.GetNext();
					typeName = itr.GetNext().Value;

					mBfBuildCompiler.mBfSystem.Lock(0);
					embedFilePath = mBfBuildCompiler.GetEmitLocation(typeName, line, .. scope:: .(), out embedLine, out embedLineChar, var embedHash);
					mBfBuildCompiler.mBfSystem.Unlock();

					if (gApp.mSettings.mEditorSettings.mEmitCompiler == .Resolve)
					{
						mBfResolveCompiler.mBfSystem.Lock(0);
						mBfResolveCompiler.GetEmitLocation(typeName, line, scope .(), var resolveLine, var resolveLineChar, var resolveHash);
						mBfResolveCompiler.mBfSystem.Unlock();

						if ((resolveLine != embedLine) || (resolveLineChar != embedLineChar) || (embedHash != resolveHash))
						{
							isViewValid = false;
							useFilePath = scope:: $"$Emit$Build${useFilePath.Substring("$Emit$".Length)}";
						}
					}
				}

				if ((isViewValid) && (!embedFilePath.IsEmpty))
				{
					var sourceViewPanel = ShowSourceFile(scope .(embedFilePath), null, showTemp ? SourceShowType.Temp : SourceShowType.ShowExisting).panel;
					if (sourceViewPanel == null)
						return null;

					var sewc = sourceViewPanel.mEditWidget.mEditWidgetContent as SourceEditWidgetContent;
					var data = sewc.mData as SourceEditWidgetContent.Data;

					QueuedEmitShowData emitShowData = new .();
					emitShowData.mPrevCollapseParseRevision = data.mCollapseParseRevision;
					emitShowData.mTypeName = new .(typeName);
					emitShowData.mLine = (.)line;
					emitShowData.mColumn = (.)column;
					DeleteAndNullify!(sourceViewPanel.[Friend]mQueuedEmitShowData);
					sourceViewPanel.[Friend]mQueuedEmitShowData = emitShowData;
					sourceViewPanel.ShowFileLocation(refHotIdx, embedLine, embedLineChar, .None);

					if (typeName.Contains('<'))
					{
						if (sourceViewPanel.AddExplicitEmitType(typeName))
							sourceViewPanel.QueueFullRefresh(false);
					}

					if (!sourceViewPanel.[Friend]mWantsFullRefresh)
						sourceViewPanel.UpdateQueuedEmitShowData();

					sourceViewPanel.EditWidget?.Content.RemoveSecondaryTextCursors();
					return sourceViewPanel;
				}
			}

			var (sourceViewPanel, tabButton) = ShowSourceFile(useFilePath, null, showTemp ? SourceShowType.Temp : SourceShowType.ShowExisting);
			if (sourceViewPanel == null)
				return null;
			if (((filePath.StartsWith("$")) && (var svTabButton = tabButton as SourceViewTabButton)))
				svTabButton.mIsTemp = true;
			sourceViewPanel.ShowHotFileIdx(showHotIdx);
			sourceViewPanel.ShowFileLocation(refHotIdx, Math.Max(0, line), Math.Max(0, column), hilitePosition);
			sourceViewPanel.EditWidget?.Content.RemoveSecondaryTextCursors();
			return sourceViewPanel;
		}

		public SourceViewPanel ShowSourceFileLocation(String filePath, int32 cursorIdx, LocatorType hilitePosition)
		{
			var sourceViewPanel = ShowSourceFile(filePath).panel;
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
			bool checkForOldFileInfo = false;
			if (hashPos != -1)
			{
				let hashStr = StringView(filePath, hashPos + 1);
				hash = SourceHash.Create(hashStr);
				filePath.RemoveToEnd(hashPos);

				if (frameFlags.HasFlag(.CanLoadOldVersion))
				{
					aliasFilePath = scope:: String(filePath);

					String fileText = scope String();
					SourceHash fileHash = default;

					var hashKind = hash.GetKind();
					if (hashKind == .None)
						hashKind = .MD5;

					LoadTextFile(filePath, fileText, false, scope [&] () => { fileHash = SourceHash.Create(hashKind, fileText); }).IgnoreError();

					if (fileHash != hash)
						checkForOldFileInfo = true;
				}
			}
			else if (filePath.StartsWith("$Emit"))
			{
				// Check this later
			}
			else
			{
				if (!File.Exists(filePath))
					checkForOldFileInfo = true;
			}

			if (checkForOldFileInfo)
			{
				String outFileInfo = scope String();
				mDebugger.GetStackFrameOldFileInfo(mDebugger.mActiveCallStackIdx, outFileInfo);

				var args = outFileInfo.Split!('\n');
				if (args.Count == 3)
				{
					aliasFilePath = scope:: String(filePath);

					filePath.Set(args[0]);
					loadCmd = scope:: String(args[1]);
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
					var disassemblyPanel = ShowDisassemblyPanel(true, setFocus);
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

				if (filePath.StartsWith("$"))
				{
					ShowSourceFileLocation(filePath, hotIdx, hotIdx, line, column, .Smart);
					return;
				}

				var (sourceViewPanel, tabButton) = ShowSourceFile(filePath, null, SourceShowType.ShowExisting, setFocus);
				if (sourceViewPanel != null)
				{
					sourceViewPanel.mIsSourceCode = true; // It's always source code, even if there is no extension (ie: stl types like "vector")

					if ((aliasFilePath != null) && (var svTabButton = tabButton as SourceViewTabButton))
					{
						svTabButton.mIsTemp = true;
					}

					if ((aliasFilePath != null) && (sourceViewPanel.mAliasFilePath == null))
					{
						String.NewOrSet!(sourceViewPanel.mAliasFilePath, aliasFilePath);
					}

					if (sourceViewPanel.mLoadFailed)
					{
						sourceViewPanel.mWantHash = hash;
						if (loadCmd != null)
						{
							sourceViewPanel.SetLoadCmd(loadCmd);
						}
					}
					else if ((gApp.mDebugger.mIsRunningWithHotSwap) && (sourceViewPanel.mIsBeefSource))
					{
						// No 'wrong hash' warnings
					}
					else if (((hash != .None) && (sourceViewPanel.mEditData != null) && (!sourceViewPanel.mEditData.CheckHash(hash))) ||
						(sourceViewPanel.mHasChangedSinceLastCompile) && (mDebugger?.mIsComptimeDebug != true))
					{
						sourceViewPanel.ShowWrongHash();
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
			if (File.Exists(key))
			{
				DragDropFile(key);
				return;
			}
			Fail(scope String()..AppendF("Unhandled command line param: {0}", key));
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
					mVerb = .Open;
				case "-testNoExit":
					mExitWhenTestScriptDone = false;
				case "-firstRun":
					mForceFirstRun = true;
					mIsFirstRun = true;
				case "-clean":
					mWantsClean = true;
				case "-dbgCompileDump":
					mDbgCompileDump = true;
				case "-launch":
					if (mLaunchData == null)
						mLaunchData = new .();
				case "-launchPaused":
					if (mLaunchData != null)
						mLaunchData.mPaused = true;
					else
						Fail("'-launchPaused' can only be used after '-launch'");
				case "-safe":
#if !CLI && BF_PLATFORM_WINDOWS
					if (Windows.MessageBoxA(default, "Start the IDE in safe mode? This will disable code intelligence features.", "SAFE MODE?",
						Windows.MB_ICONQUESTION | Windows.MB_YESNO) != Windows.IDYES)
					{
						break;
					}
#endif
					fallthrough;
				case "-forceSafe":
					mSafeMode = true;
					mNoResolve = true;
#if !CLI
				case "-noRecover":
					mFileRecovery?.mDisabled = true;
#endif
				case "-deterministic":
					mDeterministic = true;
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
					if (mSetConfigName == null)
						mSetConfigName = new String();
					mSetConfigName.Set(value);
				case "-launch":
					if (mLaunchData == null)
						mLaunchData = new .();
					String.NewOrSet!(mLaunchData.mTargetPath, value);
				case "-launchDir":
					if (mLaunchData != null)
						String.NewOrSet!(mLaunchData.mWorkingDir, value);
					else
						Fail("'-launchDir' can only be used after '-launch'");
#if BF_PLATFORM_WINDOWS
				case "-minidump":
						String.NewOrSet!(mCrashDumpPath, value);
#endif
				case "-platform":
					if (mSetPlatformName == null)
						mSetPlatformName = new String();
					mSetPlatformName.Set(value);
				case "-test":
					Runtime.SetCrashReportKind(.PrintOnly);

					String absTestPath = scope String();
					if (mWorkspace.mDir != null)
						Path.GetAbsolutePath(value, mWorkspace.mDir, absTestPath);
					else
						Path.GetFullPath(value, absTestPath);

					mWantsClean = true;
					mRunningTestScript = true;
					mScriptManager.SetTimeoutMS(20 * 60 * 1000); // 20 minute timeout
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
					else if (value == "diagnostic")
						mVerbosity = .Diagnostic;
					else
						Fail(scope String()..AppendF("Invalid verbosity option: {}", value));
				case "-workspace","-proddir":
					var relDir = scope String(value);
					if ((relDir.EndsWith("\\")) || relDir.EndsWith("\""))
						relDir.RemoveToEnd(relDir.Length - 1);
					IDEUtils.FixFilePath(relDir);

					String fullDir = new String();
					Path.GetFullPath(relDir, fullDir);

					if (fullDir.EndsWith("BeefSpace.toml", .OrdinalIgnoreCase))
						fullDir.RemoveFromEnd("BeefSpace.toml".Length);

					//TODO: Properly implement 'composite files'
					/*if ((File.Exists(fullDir)) ||
						((IsBeefFile(fullDir)) && (!Directory.Exists(fullDir))))
					{
						mWorkspace.mCompositeFile = new CompositeFile(fullDir);
						delete fullDir;
					}
					else*/
						mWorkspace.mDir = fullDir;
				case "-file":
					DragDropFile(value);
				default:
					return false;
				}
				return true;
			}
		}

		public void DragDropFile(StringView filePath)
		{
			let prevDeferredOpen = mDeferredOpen;

			if (filePath.EndsWith(".bfdbg", .OrdinalIgnoreCase))
				mDeferredOpen = .DebugSession;
			else if (filePath.EndsWith(".dmp", .OrdinalIgnoreCase))
				mDeferredOpen = .CrashDump;
			else if (filePath.EndsWith("BeefSpace.toml", .OrdinalIgnoreCase))
				mDeferredOpen  = .Workspace;
			else
				mDeferredOpen = .File;

			if (prevDeferredOpen == .File && mDeferredOpen == .File)
			{
				if (String.IsNullOrEmpty(mDeferredOpenFileName))
					String.NewOrSet!(mDeferredOpenFileName, filePath);
				else
					mDeferredOpenFileName.AppendF("|{}", filePath);
			}
			else if (prevDeferredOpen != .None && prevDeferredOpen != mDeferredOpen)
			{
				mDeferredOpen = prevDeferredOpen;
			}
			else
			{
				String.NewOrSet!(mDeferredOpenFileName, filePath);
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

		public virtual void Output(String outStr)
		{
#if CLI
			Console.Write(outStr);
			return;
#endif
#unwarn
			outStr.Replace("\r", "");
			mOutputPanel.Write(outStr);
		}

		public virtual void OutputSmart(String outStr)
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

		public virtual void Output(String format, params Object[] args)
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

#if CLI
			Console.Write(outStr);
#else
			mOutputPanel.Write(outStr);
#endif
		}

		public void CompilerLog(String format, params Object[] args)
		{
			var str = scope String();
			str..AppendF(format, params args);
			if (mBfBuildSystem != null)
				mBfBuildSystem.Log(str);
			if (mBfResolveSystem != null)
				mBfResolveSystem.Log(str);
		}

		public virtual void OutputLine(String format, params Object[] args)
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

		public virtual void OutputErrorLine(StringView format, params Object[] args)
		{
			mWantShowOutput = true;
			var errStr = scope String();
			errStr.Append("ERROR: ");
			errStr.Append(format);
			OutputLineSmart(errStr, params args);
		}

		public virtual void OutputWarnLine(StringView format, params Object[] args)
		{
			var warnStr = scope String();
			warnStr.AppendF(format, params args);

#if CLI
			var outStr = warnStr;
#else
			var outStr = scope String();
			outStr.AppendF("{0}{1}{2}", Font.EncodeColor(0xfffef860), warnStr, Font.EncodePopColor());
#endif

			OutputLine(outStr);
		}

		public virtual void OutputLineSmart(StringView format, params Object[] args)
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
			if (outStr.StartsWith("ERROR:"))
				mFailed = true;
			Console.WriteLine(outStr);
#else
			outStr.Append("\n");
			if (mOutputPanel != null)
				mOutputPanel.WriteSmart(outStr);
#endif
		}

		public virtual void OutputFormatted(String str, bool isDbgEvalOutput = false)
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

		public void PhysSetScale(float scale, bool force = false)
		{
			var prevScale = DarkTheme.sScale;
			float useScale = Math.Clamp(scale, 0.5f, 4.0f);
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

		public void SetScale(float scale, bool force = false)
		{
			PhysSetScale(scale, force);
			gApp.mSettings.mUISettings.mScale = DarkTheme.sScale * 100.0f;
		}

		void MouseUp(MouseEvent evt)
		{
			if (evt.mBtn == 3)
				NavigateBackwards();
			else if (evt.mBtn == 4)
				NavigateForwards();
		}

		void SysKeyDown(KeyDownEvent evt)
		{
			if (evt.mKeyCode != .Alt)
			{
				NOP!();
			}

			if (!evt.mKeyFlags.HeldKeys.HasFlag(.Alt))
			{
#if BF_PLATFORM_WINDOWS
				mConsolePanel.SysKeyDown(evt);
				mTerminalPanel.SysKeyDown(evt);
#endif
			}

			if (evt.mHandled)
				return;

			var window = (WidgetWindow)evt.mSender;

			if (window.mFocusWidget is KeysEditWidget)
			{
				return;
			}

			IDECommand.ContextFlags useFlags = .None;
			var activeWindow = GetActiveWindow();
			while (activeWindow.mParent != null)
				activeWindow = activeWindow.mParent as WidgetWindow;
			if (activeWindow == null)
				return;

			bool isMainWindow = activeWindow.mRootWidget is MainFrame;
			bool isWorkWindow = isMainWindow || (activeWindow.mRootWidget is DarkDockingFrame);

			var activePanel = GetActivePanel() as Panel;
			if (activePanel is SourceViewPanel)
				useFlags |= .Editor;
			else if (activePanel is DisassemblyPanel)
				useFlags |= .Editor;

			if (isMainWindow)
				useFlags |= .MainWindow;
			if (isWorkWindow)
				useFlags |= .WorkWindow;

			if (evt.mKeyCode == .Tab)
			{
				if (activePanel != null)
				{
					if (activePanel.HandleTab(window.IsKeyDown(.Shift) ? -1 : 1))
						return;
				}
			}

			if ((mKeyChordState != null) && (evt.mKeyCode.IsModifier))
			{
				// Ignore
			}
			else
			{
				var keyState = scope KeyState();
				keyState.mKeyCode = evt.mKeyCode;
				keyState.mKeyFlags = evt.mKeyFlags.HeldKeys;

				var curKeyMap = mCommands.mKeyMap;

				bool hadChordState = mKeyChordState != null;
				if (mKeyChordState != null)
					curKeyMap = mKeyChordState.mCommandMap;
				var prevKeyChordState = mKeyChordState;
				defer delete prevKeyChordState;
				mKeyChordState = null;

				KeyState matchedKey;
				IDECommandBase commandBase;

				bool hadMatch = curKeyMap.mMap.TryGet(keyState, out matchedKey, out commandBase);
				if ((!hadMatch) && (prevKeyChordState != null))
				{
					// If we have a "Ctrl+A, Ctrl+B" style sequence then also try to match that against "Ctrl+A, B"
					KeyState rawKeyState = keyState;
					rawKeyState.mKeyFlags &= ~prevKeyChordState.mKeyState.mKeyFlags;
					hadMatch = curKeyMap.mMap.TryGet(rawKeyState, out matchedKey, out commandBase);
				}

				if (hadMatch)
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
						bool foundMatch = false;
						if (useFlags != .None)
						{
							var checkCommand = command;
							while (checkCommand != null)
							{
								bool matches = checkCommand.mContextFlags == .None;
								if (checkCommand.mContextFlags.HasFlag(.Editor))
									matches |= useFlags.HasFlag(.Editor);
								if (checkCommand.mContextFlags.HasFlag(.MainWindow))
									matches |= useFlags.HasFlag(.MainWindow);
								if (checkCommand.mContextFlags.HasFlag(.WorkWindow))
									matches |= useFlags.HasFlag(.WorkWindow);

								if (matches)
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
			}

			SourceViewPanel sourceViewPanel = null;
			Widget focusWidget = window.mFocusWidget;

			if (focusWidget is EditWidget)
				focusWidget = focusWidget.mParent;
			if (focusWidget is SourceViewPanel)
				sourceViewPanel = (SourceViewPanel)focusWidget;
			//if (focusWidget is DisassemblyPanel)
				//break;

			if (evt.mKeyFlags.HeldKeys == 0) // No ctrl/shift/alt
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

		void SysKeyUp(KeyCode keyCode)
		{
#if BF_PLATFORM_WINDOWS
			mConsolePanel.SysKeyUp(keyCode);
			mTerminalPanel.SysKeyUp(keyCode);
#endif
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
			for (var cursor in ewc.mTextCursors)
			{
				ewc.SetTextCursor(cursor);
				if (!ewc.HasSelection())
					continue;

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
					continue;

				ewc.CreateMultiCursorUndoBatch("IDEApp.ChangeCase()");
				ewc.InsertAtCursor(str);

				ewc.mSelection = prevSel;
			}
			ewc.CloseMultiCursorUndoBatch();
			ewc.SetPrimaryTextCursor();
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
			if (fileName.StartsWith('$'))
				return true;
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
			if (fileName.StartsWith('$'))
				return true;
			return fileName.EndsWith(".cs", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".bf", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".c", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".cc", StringComparison.OrdinalIgnoreCase) ||
				fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
		}

		public static bool IsClangSourceHeader(String fileName)
		{
			return fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
		}

		public void GetBeefPreprocessorMacros(DefinesSet macroList)
		{
			var workspaceOptions = GetCurWorkspaceOptions();
			var targetTriple = scope String();

			if (TargetTriple.IsTargetTriple(gApp.mPlatformName))
				targetTriple.Set(gApp.mPlatformName);
			else
				Workspace.PlatformType.GetTargetTripleByName(gApp.mPlatformName, .GNU, targetTriple);
			if (!workspaceOptions.mTargetTriple.IsEmpty)
				targetTriple.Set(workspaceOptions.mTargetTriple);

			if (targetTriple.StartsWith("x86_64-"))
				macroList.Add("BF_MACHINE_X64");
			else if (targetTriple.StartsWith("i686-"))
				macroList.Add("BF_MACHINE_X86");
			else if ((targetTriple.StartsWith("arm64")) || (targetTriple.StartsWith("aarch64")))
				macroList.Add("BF_MACHINE_AARCH64");
			else if (targetTriple.StartsWith("armv"))
				macroList.Add("BF_MACHINE_ARM");
			else if (targetTriple.StartsWith("wasm32"))
				macroList.Add("BF_MACHINE_WASM32");
			else if (targetTriple.StartsWith("wasm64"))
				macroList.Add("BF_MACHINE_WASM64");
			else
				macroList.Add("BF_MACHINE_X64"); // Default

			let platform = Workspace.PlatformType.GetFromName(mPlatformName, targetTriple);
			if (platform != .Unknown)
			{
				String def = scope .();
				def.Append("BF_PLATFORM_");
				platform.ToString(def);
				def.ToUpper();
				macroList.Add(def);
			}

			if (workspaceOptions.mRuntimeChecks)
				macroList.Add("BF_RUNTIME_CHECKS");
			if (workspaceOptions.mEnableObjectDebugFlags)
				macroList.Add("BF_ENABLE_OBJECT_DEBUG_FLAGS");
			if (workspaceOptions.mAllowHotSwapping)
				macroList.Add("BF_ALLOW_HOT_SWAPPING");

			bool is64Bits = Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 8;

			if (is64Bits)
			{
				if (workspaceOptions.mLargeStrings)
					macroList.Add("BF_LARGE_STRINGS");
				if (workspaceOptions.mLargeCollections)
					macroList.Add("BF_LARGE_COLLECTIONS");
			}

			if (workspaceOptions.mRuntimeKind == .Disabled)
				macroList.Add("BF_RUNTIME_DISABLE");
			if ((workspaceOptions.mRuntimeKind == .Reduced) || (workspaceOptions.mRuntimeKind == .Disabled))
				macroList.Add("BF_RUNTIME_REDUCED");
			if (workspaceOptions.mReflectKind == .Minimal)
				macroList.Add("BF_REFLECT_MINIMAL");

			// Only supported on Windows at the moment
			bool hasLeakCheck = false;
			if (workspaceOptions.LeakCheckingEnabled)
			{
				hasLeakCheck = true;
				macroList.Add("BF_ENABLE_REALTIME_LEAK_CHECK");
			}

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
					var beefPreprocMacros = scope DefinesSet();
					GetBeefPreprocessorMacros(beefPreprocMacros);
					for (var beefPreprocMacro in beefPreprocMacros.mDefines)
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

		const int cArgFileThreshold = 0x2000 - 1;

		public ExecutionQueueCmd QueueRun(StringView fileName, StringView args, StringView workingDir, ArgsFileKind argsFileKind = .None)
		{
			var executionQueueCmd = new ExecutionQueueCmd();
			executionQueueCmd.mFileName = new String(fileName);
			executionQueueCmd.mArgs = new String(args);
			executionQueueCmd.mWorkingDir = new String(workingDir);
			if (fileName.Length + args.Length + 1 > cArgFileThreshold)
			{
				// Only use UTF16 if we absolutely need to
				if ((argsFileKind == .UTF16WithBom) && (!args.HasMultibyteChars))
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

		static void WriteInputThread(Object obj)
		{
			ExecutionInstance executionInstance = (ExecutionInstance)obj;

			FileStream fileStream = scope FileStream();
			if (executionInstance.mProcess.AttachStandardInput(fileStream) case .Err)
				return;

			WriteLoop:while (!executionInstance.mStdInData.IsEmpty)
			{
				switch (fileStream.TryWrite(.((.)executionInstance.mStdInData.Ptr, executionInstance.mStdInData.Length)))
				{
				case .Ok(int len):
					executionInstance.mStdInData.Remove(0, len);
				case .Err:
					break WriteLoop;
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

			int count = 0;
			while (true)
			{
				count++;
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

		void ReadDebugOutputThread(Object obj)
		{
			FileStream fileStream = (.)obj;

			int count = 0;
			Loop:while (true)
			{
				uint8[4096] data = ?;
				switch (fileStream.TryRead(data, -1))
				{
				case .Ok(let len):
					if (len == 0)
						break Loop;
					using (mDebugOutputMonitor.Enter())
					{
						for (int i < len)
							mDebugOutput.Append((char8)data[i]);
					}
				case .Err:
					break Loop;
				}

				/*var buffer = scope String();
				if (streamReader.Read(buffer) case .Err)
					break;
				using (mDebugOutputMonitor.Enter())
					mDebugOutput.Add(new String(buffer));*/

				count++;
			}

			delete fileStream;
		}

		/*static void ReadDebugErrorThread(Object obj)
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
		}*/

		public enum RunFlags
		{
			None,
			ShellCommand = 1,
			BatchCommand = 2,
			NoRedirect = 4,
			NoWait = 8,
		}

		public ExecutionInstance DoRun(StringView inFileName, StringView args, StringView workingDir, ArgsFileKind useArgsFile, Dictionary<String, String> envVars = null, String stdInData = null, RunFlags runFlags = .None, String reference = null)
		{
			//Debug.Assert(executionInstance == null);

			String fileName = scope String(inFileName);
			QuoteIfNeeded(fileName);

			ProcessStartInfo startInfo = scope ProcessStartInfo();
			startInfo.UseShellExecute = false;
			if (!fileName.IsEmpty)
				startInfo.SetFileName(fileName);
			startInfo.SetWorkingDirectory(workingDir);
			startInfo.SetArguments(args);

			if ((!runFlags.HasFlag(.NoRedirect)) && (!runFlags.HasFlag(.NoWait)))
			{
				startInfo.RedirectStandardOutput = true;
				startInfo.RedirectStandardError = true;
				startInfo.RedirectStandardInput = true;
				startInfo.CreateNoWindow = true;
			}


			var executionInstance = new ExecutionInstance();

#if BF_PLATFORM_WINDOWS
			if (runFlags.HasFlag(.BatchCommand))
			{
				String tempFileName = scope String();
				Path.GetTempFileName(tempFileName);
				tempFileName.Append(".bat");

				String shellArgs = scope .();
				IDEUtils.AppendWithOptionalQuotes(shellArgs, fileName);
				shellArgs.Append(" ");
				shellArgs.Append(args);

				var result = File.WriteAllText(tempFileName, shellArgs, Encoding.UTF8);
				if (result case .Err)
					OutputLine("Failed to create temporary batch file");

				startInfo.SetFileName(tempFileName);
				startInfo.SetArguments("");

				executionInstance.mTempFileName = new String(tempFileName);
			}
			else if (runFlags.HasFlag(.ShellCommand))
			{
				String shellArgs = scope .();
				shellArgs.Append("/s ");
				shellArgs.Append("/c ");
				shellArgs.Append("\"");
				IDEUtils.AppendWithOptionalQuotes(shellArgs, fileName);
				if (!args.IsEmpty)
				{
					shellArgs.Append(" ");
					shellArgs.Append(args);
				}
				shellArgs.Append("\"");
				startInfo.SetFileName("cmd.exe");
				startInfo.SetArguments(shellArgs);
			}
#endif

			if (envVars != null)
			{
				for (var envKV in envVars)
					startInfo.AddEnvironmentVariable(envKV.key, envKV.value);
			}

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

				delete executionInstance.mTempFileName;
				executionInstance.mTempFileName = new String(tempFileName);
			}

			if (mVerbosity >= .Detailed)
			{
				String showArgs = startInfo.[Friend]mArguments;
				if ((mRunningTestScript) && (showArgs.Length > 1024))
				{
					showArgs = scope:: String(showArgs, 0, 1024);
					showArgs.Append("...");
				}

				if (!startInfo.[Friend]mFileName.IsEmpty)
				{
					if (reference != null)
						OutputLine($"Executing ({reference}): {startInfo.[Friend]mFileName} {showArgs}");
					else
						OutputLine($"Executing: {startInfo.[Friend]mFileName} {showArgs}");

					if ((mVerbosity >= .Diagnostic) && (useArgsFile != .None))
						OutputLine("Arg file contents: {0}", args);
					if ((mVerbosity >= .Diagnostic) && (stdInData != null))
						OutputLine("StdIn data: {0}", stdInData);
				}
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
				OutputErrorLine("Failed to execute \"{0}\"", inFileName);
				delete process;
				delete executionInstance;
				return null;
			}

			if (runFlags.HasFlag(.NoWait))
			{
				delete process;
				delete executionInstance;
				return null;
			}

			mExecutionInstances.Add(executionInstance);
			executionInstance.mStopwatch.Start();
			executionInstance.mProcess = process;

			if (startInfo.RedirectStandardOutput)
			{
				executionInstance.mOutputThread = new Thread(new => ReadOutputThread);
				executionInstance.mOutputThread.Start(executionInstance, false);
			}

			if (startInfo.RedirectStandardError)
			{
				executionInstance.mErrorThread = new Thread(new => ReadErrorThread);
				executionInstance.mErrorThread.Start(executionInstance, false);
			}

			if ((startInfo.RedirectStandardInput) && (stdInData != null))
			{
				executionInstance.mStdInData = new String(stdInData);
				executionInstance.mInputThread = new Thread(new => WriteInputThread);
				executionInstance.mInputThread.Start(executionInstance, false);
			}

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
						if (executionInstance.mSmartOutput)
							OutputLineSmart(str);
						else
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
						if (executionInstance.mIsTargetRun)
						{
							mTargetExitCode = executionInstance.mExitCode;
						}
						else
						{
							if (executionInstance.mParallelGroup == -1)
							{
								if (mVerbosity >= .Detailed)
									OutputLine("Execution time: {0:0.00}s", executionInstance.mStopwatch.ElapsedMilliseconds / 1000.0f);
							}

							if (executionInstance.mCanceled)
								OutputLine("Execution Canceled");
							else if (failed)
								OutputLine("Execution Failed");
						}

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
					executionInstance.mDone = true;
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

				if (let scriptCmd = next as ScriptCmd)
				{
					if (mBuildContext?.mScriptManager != null)
					{
						if (scriptCmd.mCmd != null)
						{
							mBuildContext.mScriptManager.QueueCommands(scriptCmd.mCmd, scriptCmd.mPath, .NoLines);
							DeleteAndNullify!(scriptCmd.mCmd);
						}

						if ((mBuildContext.mScriptManager.HasQueuedCommands) && (!mBuildContext.mScriptManager.mFailed))
							return;
					}
				}

#if BF_PLATFORM_WINDOWS
				if (let startDebugCmd = next as StartDebugCmd)
				{
					if ((mSettings.mDebugConsoleKind == .Native) && (mSettings.mKeepNativeConsoleOpen))
					{
						if (!startDebugCmd.mConnectedToConsole)
						{
							if (startDebugCmd.mConsoleProcessId == 0)
							{
								int32 processId = 0;

								List<Process> processList = scope .();
								Process.GetProcesses(processList);
								defer processList.ClearAndDeleteItems();

								for (var process in processList)
								{
									if ((process.ProcessName.Contains("BeefCon.exe")) || (process.ProcessName.Contains("BeefCon_d.exe")))
									{
										var title = process.GetMainWindowTitle(.. scope .());
										if (title.EndsWith("Debug Console"))
										{
											processId = process.Id;
										}
									}
								}

								if (processId == 0)
								{
									var beefConExe = scope $"{gApp.mInstallDir}/BeefCon.exe";

									ProcessStartInfo procInfo = scope ProcessStartInfo();
									procInfo.UseShellExecute = false;
									procInfo.SetFileName(beefConExe);
									procInfo.SetArguments(scope $"{Process.CurrentId}");
									procInfo.ActivateWindow = false;

									var process = scope SpawnedProcess();
									if (process.Start(procInfo) case .Ok)
									{
										processId = (.)process.ProcessId;
									}
								}

								startDebugCmd.mConsoleProcessId = processId;
							}

							if (startDebugCmd.mConsoleProcessId != 0)
							{
								if (WinNativeConsoleProvider.AttachConsole(startDebugCmd.mConsoleProcessId))
								{
									// Worked
									WinNativeConsoleProvider.ClearConsole();
									mConsolePanel.mBeefConAttachState = .Attached(startDebugCmd.mConsoleProcessId);
									startDebugCmd.mConnectedToConsole = true;
								}
								else
								{
									// Keep trying to attach
									return;
								}
							}
						}
					}
				}
#endif
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
					if (let targetCompletedCmd = next as TargetCompletedCmd)
					{
						String projectBuildDir = scope String();
						gApp.GetProjectBuildDir(targetCompletedCmd.mProject, projectBuildDir);
						gApp.mBfBuildCompiler.SetBuildValue(projectBuildDir, "Link", "FAILED");
						gApp.mBfBuildCompiler.WriteBuildCache(projectBuildDir);
					}
				}
				else if (next is ProcessBfCompileCmd)
				{
					var processCompileCmd = (ProcessBfCompileCmd)next;

					mCompilingBeef = false;
					BeefCompileDone();

					//processCompileCmd.mStopwatch.Stop();
					if ((processCompileCmd.mHadBeef) && (mVerbosity >= .Normal))
						OutputLine("Beef compilation time: {0:0.00}s", processCompileCmd.mStopwatch.ElapsedMilliseconds / 1000.0f);

					if (!mWorkspace.mCompileInstanceList.IsEmpty)
					{
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

						ProcessBeefCompileResults(processCompileCmd.mBfPassInstance, processCompileCmd.mCompileKind, processCompileCmd.mHotProject, processCompileCmd.mStopwatch);
						processCompileCmd.mBfPassInstance = null;

						if (mHotResolveState != .None)
						{
							if (compileInstance.mCompileResult == .Success)
								compileInstance.mCompileResult = .PendingHotLoad;
						}
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
						if (StartupProject(true, startDebugCmd.mWasCompiled))
						{
							OutputLine("Debugger started");
						}
						else
							OutputLine("Failed to start debugger");

/*#if BF_PLATFORM_WINDOWS
						if ((mSettings.mDebugConsoleKind == .Native) && (mSettings.mKeepNativeConsoleOpen))
						{
							BeefConConsoleProvider.Pipe pipe = scope .();
							//pipe.Connect(Process.CurrentId, )
							pipe.Connect(123, -startDebugCmd.mConsoleProcessId).IgnoreError();

							pipe.StartMessage(.Attached);
							pipe.Stream.Write((int32)mDebugger.GetProcessId());
							pipe.EndMessage();
						}
					#endif*/
					}
				}
				else if (next is ExecutionQueueCmd)
				{
					var executionQueueCmd = (ExecutionQueueCmd)next;

					ReplaceVariables(executionQueueCmd.mFileName);
					ReplaceVariables(executionQueueCmd.mArgs);
					ReplaceVariables(executionQueueCmd.mWorkingDir);
					if (executionQueueCmd.mEnvVars != null)
					{
						for (let kv in executionQueueCmd.mEnvVars)
							ReplaceVariables(kv.value);
					}

					var executionInstance = DoRun(executionQueueCmd.mFileName, executionQueueCmd.mArgs, executionQueueCmd.mWorkingDir, executionQueueCmd.mUseArgsFile,
						executionQueueCmd.mEnvVars, executionQueueCmd.mStdInData, executionQueueCmd.mRunFlags, executionQueueCmd.mReference);
					if (executionInstance != null)
					{
						executionInstance.mParallelGroup = executionQueueCmd.mParallelGroup;
						executionInstance.mIsTargetRun = executionQueueCmd.mIsTargetRun;
					}
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
						OutputLineSmart("ERROR: BUILD FAILED. Total build time: {0:0.00}s", buildCompletedCmd.mStopwatch.ElapsedMilliseconds / 1000.0f);
					else if ((mVerbosity >= .Detailed) && (buildCompletedCmd.mStopwatch != null))
						OutputLineSmart("SUCCESS: Build completed with no errors. Total build time: {0:0.00}s", buildCompletedCmd.mStopwatch.ElapsedMilliseconds / 1000.0f);

					if (mDebugger?.mIsComptimeDebug == true)
						DebuggerComptimeStop();

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
				else if (var scriptCmd = next as ScriptCmd)
				{
					// Already handled
					(void)scriptCmd;
				}
				else if (var writeEmitCmd = next as WriteEmitCmd)
				{
					String projectName = new String(writeEmitCmd.mProjectName);
					String filePath = new String(writeEmitCmd.mPath);

					mBfBuildCompiler.DoBackground(new () =>
						{
							if (var project = mWorkspace.FindProject(projectName))
							{
								var bfProject = mBfBuildSystem.GetBfProject(project);
								if (bfProject != null)
								{
									mBfBuildSystem.Lock(0);
									mBfBuildCompiler.WriteEmitData(filePath, bfProject);
									mBfBuildSystem.Unlock();
								}
							}
						}
						~
						{
							delete projectName;
							delete filePath;
						});
				}
				else if (var emsdkCmd = next as EmsdkInstalledCmd)
				{
					gApp.mSettings.mEmscriptenPath.Set(emsdkCmd.mPath);
					gApp.mSettings.Save();
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
			bfParser.SetSource(data, fullPath, -1);
			worked &= bfParser.Parse(passInstance, false);
			worked &= bfParser.Reduce(passInstance);
			worked &= bfParser.BuildDefs(passInstance, null, false);
			return worked;
		}

		public bool FindProjectSourceContent(ProjectSource projectSource, out IdSpan char8IdData, bool loadOnFail, String sourceContent, SourceHash* sourceHash)
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
							if (sourceHash != null)
							{
								*sourceHash = SourceHash.Create(.MD5, sourceContent, projectSource.mEditData.mLineEndingKind);
								if (*sourceHash case .MD5(let md5Hash))
									projectSource.mEditData.mMD5Hash = md5Hash;
							}
							return true;
						}
					}

					if (projectSource.mEditData.mSavedContent != null)
					{
						char8IdData = projectSource.mEditData.mSavedCharIdData.Duplicate();
						sourceContent.Set(projectSource.mEditData.mSavedContent);
						if ((!projectSource.mEditData.mMD5Hash.IsZero) && (sourceHash != null))
							*sourceHash = .MD5(projectSource.mEditData.mMD5Hash);
						return true;
					}
				}
			}

			if (loadOnFail)
			{
				String text = scope String();
				bool isValid = false;
				if (LoadTextFile(fullPath, text, true, scope [?] () => { if (sourceHash != null) *sourceHash = SourceHash.Create(.MD5, text); }) case .Ok)
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
					if (sourceHash != null)
					{
						if (*sourceHash case .MD5(let md5Hash))
							editData.mMD5Hash = md5Hash;
					}
					editData.GetFileTime();
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

		public bool QueueParseBeefFiles(BfCompiler bfCompiler, bool forceQueue, ProjectFolder projectFolder, Project hotProject)
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

							if (bfCompiler != null)
							{
								// Process change in resolve compiler
								bfCompiler.QueueProjectSource(projectSource, .None, !bfCompiler.mIsResolveOnly);
							}
						}
						else // Actual build
						{
							bool wantsHashRefresh = false;
							if ((hotProject == null) && (projectSource.mWasBuiltWithOldHash))
								wantsHashRefresh = true;

							if ((projectSource.HasChangedSinceLastCompile) || (projectSource.mLoadFailed) || (forceQueue) || (wantsHashRefresh))
							{
								// mHasChangedSinceLastCompile is safe to set 'false' here since it just determines whether or not
								//  we rebuild the TypeDefs from the sources.  It isn't affected by any compilation errors.
								projectSource.mHasChangedSinceLastCompile = false;
								projectSource.mWasBuiltWithOldHash = false;

								SourceHash sourceHash = .None;

								if (hotProject != null)
								{
									if (!mWorkspace.mCompileInstanceList.IsEmpty)
									{
										let compileInstance = mWorkspace.GetProjectSourceCompileInstance(projectSource, 0);
										if (compileInstance != null)
											sourceHash = compileInstance.mSourceHash;
									}
								}

								bfCompiler.QueueProjectSource(projectSource, sourceHash, !bfCompiler.mIsResolveOnly);
								hadBeef = true;
							}
						}
					}
				}

				if (item is ProjectFolder)
				{
					var innerProjectFolder = (ProjectFolder)item;
					hadBeef |= QueueParseBeefFiles(bfCompiler, forceQueue, innerProjectFolder, hotProject);
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
			ProjectItem checkItem = projectSource;
			while (checkItem != null)
			{
				if (checkItem.mIncludeKind == .Manual)
					break;
				if (checkItem.mIncludeKind == .Ignore)
					return false;
				checkItem = checkItem.mParentFolder;
			}

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
				//Fail(scope String()..AppendF("Failed to retrieve options for {0}", project.mProjectName));
				bfProject.SetDisabled(true);
				return false;
			}

			bfProject.SetDisabled(false);

			let platform = Workspace.PlatformType.GetFromName(mPlatformName, workspaceOptions.mTargetTriple);

			var preprocessorMacros = scope DefinesSet();
			void AddMacros(List<String> macros)
			{
				for (var macro in macros)
				{
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

			bool isWin64 = true;
			if (!workspaceOptions.mTargetTriple.IsWhiteSpace)
				isWin64 = workspaceOptions.mTargetTriple.StartsWith("x86_64-pc-windows");
			else
				isWin64 = mPlatformName == "Win64";

			if ((optimizationLevel == .OgPlus) && (!isWin64) && (bfCompiler == mBfBuildCompiler))
			{
				OutputLineSmart("WARNING: Project '{0}' has Og+ specified, which is only supported for Win64 targets.", project.mProjectName);
				optimizationLevel = .O0;
			}

			BuildOptions.LTOType ltoType = .None;

			if (!bfCompiler.mIsResolveOnly)
			{
				ltoType = workspaceOptions.mLTOType;
				if (options.mBeefOptions.mLTOType != null)
					ltoType = options.mBeefOptions.mLTOType.Value;
			}

			var targetType = project.mGeneralOptions.mTargetType;

			if (bfSystem != mBfResolveSystem)
			{
				if (options.mBuildOptions.mBuildKind == .NotSupported)
				{
					OutputErrorLine("Project '{0}' is marked as 'not supported' for this platform/configuration", project.mProjectName);
					success = false;
				}
				else if (options.mBuildOptions.mBuildKind == .Test)
				{
					if (workspaceOptions.mBuildKind == .Test)
					{
						targetType = .BeefTest;
						if (mTestManager != null)
						{
							String workingDirRel = scope String();
							ResolveConfigString(mPlatformName, workspaceOptions, project, options, "$(WorkingDir)", "debug working directory", workingDirRel);
							var workingDir = scope String();
							Path.GetAbsolutePath(workingDirRel, project.mProjectDir, workingDir);
							mTestManager.AddProject(project, workingDir);
						}
					}
					else
					{
						OutputErrorLine("Project '{0}' has a Test configuration specified but the workspace is not using a Test configuration", project.mProjectName);
						success = false;
					}
				}
				else if (options.mBuildOptions.mBuildKind == .StaticLib)
				{
					if (project.mGeneralOptions.mTargetType.IsBeefApplication)
						targetType = .BeefApplication_StaticLib;
					else if (project.mGeneralOptions.mTargetType == .BeefLib)
						targetType = .BeefLib_Static;
				}
				else if (options.mBuildOptions.mBuildKind == .DynamicLib)
				{
					if (project.mGeneralOptions.mTargetType.IsBeefApplication)
						targetType = .BeefApplication_DynamicLib;
					else if (project.mGeneralOptions.mTargetType == .BeefLib)
						targetType = .BeefLib_Dynamic;
				}
			}

			var relocType = options.mBeefOptions.mRelocType;

			if (relocType == .NotSet)
			{
				if ((platform != .Windows) && (platform != .Wasm))
					relocType = .PIC;
			}

			bfProject.SetOptions(targetType,
				project.mBeefGlobalOptions.mStartupObject,
				preprocessorMacros.mDefines,
				optimizationLevel, ltoType, relocType, options.mBeefOptions.mPICLevel,
				options.mBeefOptions.mMergeFunctions, options.mBeefOptions.mCombineLoads,
				options.mBeefOptions.mVectorizeLoops, options.mBeefOptions.mVectorizeSLP);

			List<Project> depProjectList = scope List<Project>();
			if (!GetDependentProjectList(project, depProjectList))
				success = false;
			bfProject.ClearDependencies();
			for (var depProject in depProjectList)
			{
				if (bfSystem.mProjectMap.TryGetValue(depProject, var depBfProject))
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
			mProjectPanel?.RehupProjects();
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

				mBfResolveCompiler?.QueueDeferredResolveAll();
				mBfResolveCompiler?.QueueRefreshViewCommand(.FullRefresh);
				return;
			}

#if IDE_C_SUPPORT
			mDepClang.mDoDependencyCheck = true;
#endif
			if (mBfResolveCompiler != null)
			{
				if (IsProjectEnabled(project))
				{
					if (reparseFiles)
						QueueParseBeefFiles(mBfResolveCompiler, false, project.mRootFolder, null);
					mBfResolveCompiler.QueueDeferredResolveAll();
					mBfResolveCompiler.QueueRefreshViewCommand();
				}
			}
			else
			{
				if (reparseFiles)
					QueueParseBeefFiles(mBfResolveCompiler, false, project.mRootFolder, null);
			}
		}

		public void RenameProject(Project project, String newName)
		{
			mWantsBeefClean = true;

			var checkDeclName = (project.mProjectName !== project.mProjectNameDecl) && (mWorkspace.FindProject(project.mProjectNameDecl) == project);

			for (var checkProject in mWorkspace.mProjects)
			{
				for (var dep in checkProject.mDependencies)
				{
					if ((dep.mProjectName == project.mProjectName) ||
						((checkDeclName) && (dep.mProjectName == project.mProjectNameDecl)))
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
			if (project.mProjectNameDecl != project.mProjectName)
				delete project.mProjectNameDecl;
			project.mProjectNameDecl = project.mProjectName;
			project.SetChanged();
			mWorkspace.ClearProjectNameCache();

			mProjectPanel.RebuildUI();
		}

		public void RemoveProject(Project project)
		{
			RemoveProjectItems(project);

			if (mWorkspace.mProjectLockMap.GetAndRemove(project.mProjectName) case .Ok(let kv))
			{
				delete kv.key;
				kv.value.Dispose();
				if (mWorkspace.mProjectLockMap.IsEmpty)
					SaveWorkspaceLockData(true);
			}

			project.mDeleted = true;
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
						checkProject.SetChanged();
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

			IdleDeferDelete(project);
		}

		BfPassInstance CompileBeef(Project hotProject, int32 hotIdx, bool lastCompileHadMessages, out bool hadBeef)
		{
			CompilerLog("IDEApp.CompileBeef");

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

			bool needsComptime = bfCompiler.GetLastHadComptimeRebuilds();

			if (mDebugger?.mIsComptimeDebug == true)
				needsComptime = true;

			if ((!workspaceOptions.mIncrementalBuild) && (!lastCompileHadMessages))
			{
				tryQueueFiles = false;
				for (var project in mWorkspace.mProjects)
				{
					if (HasPendingBeefFiles(false, project.mRootFolder))
						tryQueueFiles = true;
				}
			}

			if (needsComptime)
				tryQueueFiles = true;

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
						doCompile |= QueueParseBeefFiles(bfCompiler, !workspaceOptions.mIncrementalBuild, project.mRootFolder, hotProject);
					}
					else if (IsProjectEnabled(project))
						success = false;
				}
			}

			if (needsComptime)
				doCompile = true;

			if (!success)
			{
				bfCompiler.QueueDeletePassInstance(passInstance);
				return null;
			}

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
				for (var project in mWorkspace.mProjects)
				{
					// Regenerate these
					DeleteContainerAndItems!(project.mCurBfOutputFileNames);
					project.mCurBfOutputFileNames = null;
				}

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

		public bool DoResolveConfigString(String platformName, Workspace.Options workspaceOptions, Project project, Project.Options options, StringView configString, String error, String result)
		{
			int startIdx = result.Length;
			int i = startIdx;
			result.Append(configString);

			bool hadError = false;

			for (; i < result.Length - 2; i++)
			{
				if ((result[i] == '$') && (result[i + 1] == '('))
				{
					int parenPos = -1;
					int openCount = 1;
					bool inString = false;
					char8 prevC = 0;
					for (int checkIdx = i + 2; checkIdx < result.Length; checkIdx++)
					{
						char8 c = result[checkIdx];
						if (inString)
						{
							if (prevC == '\\')
							{
								// Slashed char
								prevC = 0;
								continue;
							}

							if (c == '"')
								inString = false;
						}
						else
						{
							if (c == '"')
								inString = true;
							else if (c == '(')
								openCount++;
							else if (c == ')')
							{
								openCount--;
								if (openCount == 0)
								{
									parenPos = checkIdx;
									break;
								}
							}
						}

						prevC = c;
					}

					if (parenPos != -1)
						ReplaceBlock:
							do
						{
							String replaceStr = scope String(result, i + 2, parenPos - i - 2);
							String newString = null;

							if (replaceStr.Contains(' '))
							{
								String cmd = scope .();

								List<String> args = scope .();

								for (let str in replaceStr.Split(' ', .RemoveEmptyEntries))
								{
									if (cmd.IsEmpty)
										cmd.Set(str);
									else
									{
										String arg = scope:ReplaceBlock .();
										if (str.StartsWith("\""))
										{
											String unresolvedStr = scope .();
											str.UnQuoteString(unresolvedStr);
											if (!DoResolveConfigString(platformName, workspaceOptions, project, options, unresolvedStr, error, arg))
												return false;
										}
										else
											arg.Append(str);
										args.Add(arg);
									}
								}

								String cmdErr = null;

								switch (cmd)
								{
								case "Slash":
									if (args.Count == 1)
									{
										newString = scope:ReplaceBlock .();
										args[0].Quote(newString);
									}
									else
										cmdErr = "Invalid number of arguments";
								case "Var":
									break ReplaceBlock;
								case "Arguments",
									"BuildDir",
									"LinkFlags",
									"ProjectDir",
									"ProjectName",
									"TargetDir",
									"TargetPath",
									"WorkingDir":
									var selProject = mWorkspace.FindProject(args[0]);
									if (selProject != null)
									{
										Workspace.Options selWorkspaceOptions = gApp.GetCurWorkspaceOptions();
										Project.Options selOptions = gApp.GetCurProjectOptions(selProject);
										String selConfigString = scope $"$({cmd})";
										replaceStr.Clear();
										newString = scope:ReplaceBlock .();
										DoResolveConfigString(platformName, selWorkspaceOptions, selProject, selOptions, selConfigString, error, newString);
									}
									else
										cmdErr = "Unable to find project";
								default:
									cmdErr = "Invalid command";
								}

								if (newString == null)
								{
									if (error != null)
									{
										if (cmdErr != null)
											error.Set(cmdErr);
										else
											error.Set(replaceStr);
									}
									hadError = true;
									break ReplaceBlock;
								}
							}

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
									if (mLaunchData?.mArgs != null)
										newString = mLaunchData.mArgs;
									else
										newString = options.mDebugOptions.mCommandArguments;
								case "WorkingDir":
									if (mLaunchData?.mWorkingDir != null)
										newString = mLaunchData.mWorkingDir;
									else
										newString = options.mDebugOptions.mWorkingDirectory;
									IDEUtils.FixFilePath(newString);
								case "TargetDir":
									{
										if (project.IsDebugSession)
										{
											let targetPath = scope:ReplaceBlock String();
											DoResolveConfigString(platformName, workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, targetPath);
											newString = scope:ReplaceBlock String();
											Path.GetDirectoryPath(targetPath, newString);
											break;
										}

										String targetDir = scope String();
										DoResolveConfigString(platformName, workspaceOptions, project, options, options.mBuildOptions.mTargetDirectory, error, targetDir);
										newString = scope:ReplaceBlock String();
										Path.GetAbsolutePath(targetDir, project.mProjectDir, newString);
										IDEUtils.FixFilePath(newString);
									}
								case "TargetPath":
									{
										if (project.IsDebugSession)
										{
											newString = scope:ReplaceBlock String();
											DoResolveConfigString(platformName, workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, newString);
											break;
										}

										String targetDir = scope String();
										DoResolveConfigString(platformName, workspaceOptions, project, options, options.mBuildOptions.mTargetDirectory, error, targetDir);
										newString = scope:ReplaceBlock String();
										Path.GetAbsolutePath(targetDir, project.mProjectDir, newString);
										Utils.GetDirWithSlash(newString);

										if (!DoResolveConfigString(platformName, workspaceOptions, project, options, options.mBuildOptions.mTargetName, error, newString))
											return false;

										let platformType = Workspace.PlatformType.GetFromName(platformName, workspaceOptions.mTargetTriple);

										switch (platformType)
										{
										case .Windows:
											if (options.mBuildOptions.mBuildKind == .DynamicLib)
												newString.Append(".dll");
											else if ((options.mBuildOptions.mBuildKind == .StaticLib) || (project.mGeneralOptions.mTargetType == .BeefLib))
												newString.Append(".lib");
											else if (project.mGeneralOptions.mTargetType != .CustomBuild)
												newString.Append(".exe");
										case .macOS:
											if (options.mBuildOptions.mBuildKind == .DynamicLib)
												newString.Append(".dylib");
											else if (options.mBuildOptions.mBuildKind == .StaticLib)
												newString.Append(".a");
										case .Wasm:
											if (!newString.Contains('.'))
												newString.Append(".html");
										default:
											if (options.mBuildOptions.mBuildKind == .DynamicLib)
												newString.Append(".so");
											else if (options.mBuildOptions.mBuildKind == .StaticLib)
												newString.Append(".a");
										}
									}
									IDEUtils.FixFilePath(newString);
								case "ProjectDir":
									if (project.IsDebugSession)
									{
										newString = scope:ReplaceBlock String();
										Path.GetDirectoryPath(project.mProjectPath, newString);
									}
									else
										newString = project.mProjectDir;
									IDEUtils.FixFilePath(newString);
								case "BuildDir":
									newString = scope:ReplaceBlock String();
									GetProjectBuildDir(project, newString);
									IDEUtils.FixFilePath(newString);
									//Debug.WriteLine("BuildDir: {0}", newString);
								case "LinkFlags":
									newString = scope:ReplaceBlock String();

									bool isBeefDynLib = (project.mGeneralOptions.mTargetType == .BeefLib) && (options.mBuildOptions.mBuildKind == .DynamicLib);

									if ((project.mGeneralOptions.mTargetType == .BeefConsoleApplication) ||
										(project.mGeneralOptions.mTargetType == .BeefGUIApplication) ||
										(isBeefDynLib) ||
										(options.mBuildOptions.mBuildKind == .Test))
									{
										let platformType = Workspace.PlatformType.GetFromName(platformName, workspaceOptions.mTargetTriple);
										String rtName = scope String();
										String dbgName = scope String();
										String allocName = scope String();
										BuildContext.GetRtLibNames(platformType, workspaceOptions, options, false, rtName, dbgName, allocName);

										switch (platformType)
										{
										case .Windows:
											newString.Append(rtName);
											if (!dbgName.IsEmpty)
												newString.Append(" ", dbgName);
											if (!allocName.IsEmpty)
												newString.Append(" ", allocName);
										case .macOS:
											newString.AppendF("./{} -Wl,-rpath -Wl,@executable_path", rtName);
										case .iOS:
										case .Linux:
											newString.AppendF("./{} -lpthread -ldl -Wl,-rpath -Wl,$ORIGIN", rtName);
										case .Wasm:
											newString.Append("\"");
											newString.Append(mInstallDir);
											newString.Append("Beef", IDEApp.sRTVersionStr, "RT");
											newString.Append((Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 4) ? "32" : "64");
											newString.Append("_wasm");
											if (project.mWasmOptions.mEnableThreads)
												newString.Append("_pthread");
											newString.Append(".a\"");
										default:
										}
									}
								case "VSToolPath":
									if (Workspace.PlatformType.GetPtrSizeByName(platformName) == 4)
										newString = gApp.mSettings.mVSSettings.mBin32Path;
									else
										newString = gApp.mSettings.mVSSettings.mBin64Path;
									IDEUtils.FixFilePath(newString);
								case "VSToolPath_x86":
									newString = gApp.mSettings.mVSSettings.mBin32Path;
									IDEUtils.FixFilePath(newString);
								case "VSToolPath_x64":
									newString = gApp.mSettings.mVSSettings.mBin64Path;
									IDEUtils.FixFilePath(newString);
								case "EmccPath":
									newString = scope:ReplaceBlock String();
									newString.AppendF($"{gApp.mSettings.mEmscriptenPath}/upstream/emscripten/emcc.bat");
								}
							}

							if ((newString == null) && (mScriptManager != null))
							{
								switch (replaceStr)
								{
								case "ScriptDir":
									if ((mScriptManager != null) && (mScriptManager.mCurCmd != null))
									{
										newString = scope:ReplaceBlock String();
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
										newString = scope:ReplaceBlock String();
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

		public void ReplaceVariables(String result)
		{
			int i = 0;
			for (; i < result.Length - 2; i++)
			{
				if ((result[i] == '$') && (result[i + 1] == '('))
				{
					int parenPos = -1;
					int openCount = 1;
					bool inString = false;
					char8 prevC = 0;
					for (int checkIdx = i + 2; checkIdx < result.Length; checkIdx++)
					{
						char8 c = result[checkIdx];
						if (inString)
						{
							if (prevC == '\\')
							{
								// Slashed char
								prevC = 0;
								continue;
							}

							if (c == '"')
								inString = false;
						}
						else
						{
							if (c == '"')
								inString = true;
							else if (c == '(')
								openCount++;
							else if (c == ')')
							{
								openCount--;
								if (openCount == 0)
								{
									parenPos = checkIdx;
									break;
								}
							}
						}

						prevC = c;
					}

					if (parenPos != -1)
						ReplaceBlock:
							do
						{
							String replaceStr = scope String(result, i + 2, parenPos - i - 2);

							if (!replaceStr.StartsWith("Var "))
								continue;

							String varName = scope String(replaceStr, 4);
							String newString = null;

							if (mScriptManager.mContext.mVars.TryGetValue(varName, var value))
							{
								if (value.VariantType == typeof(String))
								{
									newString = scope:ReplaceBlock String(value.Get<String>());
								}
							}

							if (newString == null)
							{
								if (mBuildContext != null)
								{
									if (mBuildContext.mScriptContext.mVars.TryGetValue(varName, out value))
									{
										if (value.VariantType == typeof(String))
										{
											newString = scope:ReplaceBlock String(value.Get<String>());
										}
									}
								}
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
		}

		public bool ResolveConfigString(String platformName, Workspace.Options workspaceOptions, Project project, Project.Options options, StringView configString, String errorContext, String outResult)
		{
			String errorString = scope String();
			if (!DoResolveConfigString(platformName, workspaceOptions, project, options, configString, errorString, outResult))
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
				var beefPreprocMacros = scope DefinesSet();
				GetBeefPreprocessorMacros(beefPreprocMacros);
				for (var beefPreprocMacro in beefPreprocMacros.mDefines)
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
				if (Workspace.PlatformType.GetPtrSizeByName(gApp.mPlatformName) == 4)
					clangOptions.Append("-m32 ");
				else
					clangOptions.Append("-m64 ");
			}
			else
			{
				clangOptions.Append("--target=");
				if (TargetTriple.IsTargetTriple(gApp.mPlatformName))
					clangOptions.Append(gApp.mPlatformName);
				else
					Workspace.PlatformType.GetTargetTripleByName(gApp.mPlatformName, workspaceOptions.mToolsetType, clangOptions);
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
					clangOptions.Append(options.mCOptions.mOtherCPPFlags, " ");
				clangOptions.Append("-std=c++14 ");
			}
			else
			{
				if (!String.IsNullOrEmpty(options.mCOptions.mOtherCFlags))
					clangOptions.Append(options.mCOptions.mOtherCFlags, " ");
			}
		}

		protected static void QuoteIfNeeded(String str)
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
			BfSystem bfSystem = mBfBuildSystem;

			var bfProject = mBfBuildSystem.mProjectMap[project];
			bool bfHadOutputChanges;
			List<String> bfFileNames = scope List<String>();

			bfSystem.Lock(0);
			bfCompiler.GetOutputFileNames(bfProject, .None, out bfHadOutputChanges, bfFileNames);
			bfSystem.Unlock();

			defer ClearAndDeleteItems(bfFileNames);
			if (bfHadOutputChanges)
				project.mNeedsTargetRebuild = true;
		}

		void GetTargetPaths(Project project, String platformName, Workspace.Options workspaceOptions, Project.Options options, List<String> outPaths)
		{
			String targetPath = scope String();
			ResolveConfigString(platformName, workspaceOptions, project, options, "$(TargetPath)", "Target path", targetPath);
			outPaths.Add(new String(targetPath));

#if BF_PLATFORM_WINDOWS
			if ((project.mGeneralOptions.mTargetType == .BeefConsoleApplication) ||
				(project.mGeneralOptions.mTargetType == .BeefGUIApplication))
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
				OutputErrorLine(projectError);
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
					OutputLine(scope String()..AppendF("Unable to find project '{0}', a dependency of project '{1}'", dep.mProjectName, project.mProjectName));
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

		protected virtual void CompileFailed(Stopwatch stopwatch)
		{
			if (mTestManager != null)
				mTestManager.BuildFailed();
			if (mVerbosity > .Quiet)
				OutputLineSmart("ERROR-SOFT: Compile failed. Total build time: {0:0.00}s", stopwatch.ElapsedMilliseconds / 1000.0f);
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

			if (mDebugger?.mIsComptimeDebug == true)
				DebuggerComptimeStop();
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

		void ProcessBeefCompileResults(BfPassInstance passInstance, CompileKind compileKind, Project hotProject, Stopwatch startStopWatch)
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
					CompileFailed(startStopWatch);
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
					OutputErrorLine("Hot compile failed - target no longer running");
					CompileFailed(startStopWatch);
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
				CompileFailed(startStopWatch);
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
				if (!mBuildContext.QueueProjectCompile(project, hotProject, completedCompileCmd, hotFileNames, compileKind))
					success = false;
			}

			for (var project in orderedProjectList)
			{
				if (!mBuildContext.QueueProjectPostBuild(project, hotProject, completedCompileCmd, hotFileNames, compileKind))
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
				CompileFailed(startStopWatch);

			if (success)
			{
				var options = GetCurWorkspaceOptions();

				if (compileKind == .DebugAfter)
				{
					var startDebugCmd = new StartDebugCmd();
					startDebugCmd.mWasCompiled = true;
					startDebugCmd.mOnlyIfNotFailed = true;
					startDebugCmd.mHotCompileEnabled = options.mAllowHotSwapping;
					mExecutionQueue.Add(startDebugCmd);
				}
				else if (compileKind == .RunAfter)
				{
					StartupProject(false, true);
				}
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

		bool CompileAndRun(bool debug)
		{
			if (AreTestsRunning())
				return false;
			if (IsCompiling)
				return false;

			if (!mExecutionQueue.IsEmpty)
			{
				if (var processCompileCmd = mExecutionQueue.Back as ProcessBfCompileCmd)
				{
					processCompileCmd.mCompileKind = debug ? .DebugAfter : .RunAfter;
				}

				return false;
			}

			if (mInitialized)
				DeleteAndNullify!(mLaunchData);

			mOutputPanel.Clear();
			OutputLine("Compiling...");
			if (!Compile(debug ? .DebugAfter : .RunAfter, null))
				return false;
			return true;
		}

		public void QueueProfiling(int threadId, String desc, int sampleRate)
		{
			if (mExecutionQueue.IsEmpty)
				return;
			var profileCmd = new ProfileCmd();
			profileCmd.mThreadId = threadId;
			profileCmd.mDesc = new String(desc);
			profileCmd.mSampleRate = sampleRate;
			if (var processCompileCmd = mExecutionQueue.Back as ProcessBfCompileCmd)
			{
				delete processCompileCmd.mProfileCmd;
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

			bool hasTempFiles = false;
			WithTabs(scope [&] (tabButton) =>
				{
					if (var svTabButton = tabButton as SourceViewTabButton)
					{
						if (svTabButton.mIsTemp)
							hasTempFiles = true;
					}
				});

			if (hasTempFiles)
			{
				var dialog = ThemeFactory.mDefault.CreateDialog("Close Temp Files",
					"Do you want to close temporary files opened from the debugger?");
				dialog.mDefaultButton = dialog.AddButton("Yes", new (evt) =>
					{
						List<SourceViewTabButton> closeTabs = scope .();
						WithTabs(scope [&] (tabButton) =>
							{
								if (var svTabButton = tabButton as SourceViewTabButton)
								{
									if (svTabButton.mIsTemp)
										closeTabs.Add(svTabButton);
								}
							});
						for (var tab in closeTabs)
						{
							CloseDocument(tab.mContent);
						}
					});
				dialog.AddButton("No", new (evt) =>
					{
					});
				dialog.PopupWindow(GetActiveWindow());
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
				if (mDebugger.mIsComptimeDebug)
					CancelBuild();

				mDebugger.StopDebugging();
			}
		}

		public void AutoGenerateStartupCode(Project project)
		{
			// We have to save this to ensure any new project is actually created. Maybe overkill,
			// but best to stay conservative since this path won't be heavily tested
			if (!SaveAll())
				return;

			String namespaceName = scope .();
			String className = scope .();
			String startupStr = project.mBeefGlobalOptions.mStartupObject;

			int dotPos = startupStr.LastIndexOf('.');
			if (dotPos != -1)
			{
				namespaceName.Append(startupStr, 0, dotPos);
				className.Append(startupStr, dotPos + 1);
			}
			else
			{
				namespaceName.Append(project.mProjectName);
				className.Append(startupStr);
			}

			String startupCode = scope .();
			startupCode.AppendF(
				"""
				using System;

				namespace {};

				class {}
				{{
					public static int Main(String[] args)
					{{
						return 0;
					}}
				}}
				""", namespaceName, className);

			String srcPath = scope .();
			project.mRootFolder.GetFullImportPath(srcPath);
			Directory.CreateDirectory(srcPath).IgnoreError();

			srcPath.Append(Path.DirectorySeparatorChar);
			srcPath.Append("Program.bf");

			if (!SafeWriteTextFile(srcPath, startupCode))
				return;

			OnWatchedFileChanged(project.mRootFolder, .FileCreated, srcPath);

			if (project.IsEmpty)
				return;

			let projectSource = project.mRootFolder.mChildItems[0] as ProjectSource;
			if (projectSource == null)
				return;

#if !CLI
			ShowSourceFile(srcPath);
#endif
		}

		public bool IsVisualStudioRequired
		{
			get
			{
				var workspaceOptions = GetCurWorkspaceOptions();
				if (Workspace.PlatformType.GetFromName(mPlatformName, workspaceOptions.mTargetTriple) != .Windows)
					return false;
				if (workspaceOptions.mToolsetType != .LLVM)
					return true;

				for (var project in mWorkspace.mProjects)
				{
					if ((project.mGeneralOptions.mTargetType != .BeefConsoleApplication) &&
						(project.mGeneralOptions.mTargetType != .BeefGUIApplication) &&
						(project.mGeneralOptions.mTargetType != .BeefApplication_DynamicLib) &&
						(project.mGeneralOptions.mTargetType != .BeefApplication_StaticLib))
					{
						continue;
					}

					var options = GetCurProjectOptions(project);
					if (options == null)
						continue;
					if (options.mBuildOptions.mCLibType != .SystemMSVCRT)
						return true;
				}
				return false;
			}
		}

		protected bool Compile(CompileKind compileKind, Project hotProject)
		{
			Debug.Assert(mBuildContext == null);

			if (mWorkspace.mStartupProject != null)
			{
				if ((mWorkspace.mStartupProject.IsEmpty) && (!mWorkspace.IsDebugSession))
				{
#if !CLI
					DarkDialog dlg = new DarkDialog("Initialize Project?",
						scope String()..AppendF("Project '{}' does not contain any source code. Do you want to auto-generate some startup code?", mWorkspace.mStartupProject.mProjectName)
						, DarkTheme.sDarkTheme.mIconError);
					dlg.mWindowFlags |= .Modal;
					dlg.AddYesNoButtons(new (dlg) =>
						{
							AutoGenerateStartupCode(mWorkspace.mStartupProject);
						},
						new (dlg) =>
						{
						});
					dlg.PopupWindow(GetActiveWindow());
#else
					OutputErrorLine("The project '{}' does not contain any source code. Run with '-generate' to auto-generate some startup code.", mWorkspace.mStartupProject.mProjectName);
#endif
					OutputLine("Aborted - no startup project code found.");
					return false;
				}
			}

			if ((compileKind != .Test) && (mWorkspace.mStartupProject != null) && (mWorkspace.mStartupProject.mGeneralOptions.mTargetType == .BeefTest))
			{
				OutputErrorLine("Test project '{}' has been selected as the Startup Project. Use the 'Test' menu to run or debug tests.", mWorkspace.mStartupProject.mProjectName);
				return false;
			}

			var workspaceOptions = GetCurWorkspaceOptions();

			var platform = Workspace.PlatformType.GetFromName(mPlatformName, workspaceOptions.mTargetTriple);
			let hostPlatform = Workspace.PlatformType.GetHostPlatform();
			if (platform == .Unknown)
			{
				OutputErrorLine("Failed to compiler for unknown platform '{}'", mPlatformName);
				return false;
			}

			bool canCompile = false;
			if (platform == hostPlatform)
			{
				canCompile = true;
			}
			else
			{
				canCompile = false;
			}

			canCompile = platform == hostPlatform;
			switch (platform)
			{
			case .iOS:
				canCompile = hostPlatform == .macOS;
			case .Android:
				canCompile = true;
			case .Unknown:
				canCompile = true;
			case .Linux:
				if (hostPlatform == .Windows)
					canCompile = true; // Use WSL
			case .Wasm:
				canCompile = true;
			default:
			}

			if (!canCompile)
			{
				OutputErrorLine("Cannot compile for platform '{}' from host platform '{}'", platform, hostPlatform);
				return false;
			}

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
							Fail(scope String()..AppendF("Failed to rename {0} to {1}", dbgBuildDir, tempDirName));
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

			if ((compileKind == .RunAfter) || (compileKind == .DebugAfter))
			{
				if ((workspaceOptions.mBuildKind == .Test) && (platform != .Wasm))
				{
					OutputErrorLine("Cannot directly run Test workspace configurations. Use the 'Test' menu to run or debug tests.");
					return false;
				}
			}

			if ((Workspace.PlatformType.GetFromName(mPlatformName) == .Windows) && (IsVisualStudioRequired))
			{
				if (!mSettings.mVSSettings.IsConfigured())
					mSettings.mVSSettings.SetDefaults();

				if (!mSettings.mVSSettings.IsConfigured())
				{
					String err =
						"""
						Beef requires the Microsoft C++ build tools for Visual Studio 2013 or later, but they don't seem to be installed.

						Install just Microsoft Visual C++ Build Tools or the entire Visual Studio suite from:
						    https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
						""";

#if CLI
					Fail(err);
#else
					DarkDialog dlg = new DarkDialog("Visual C++ Not Found", err, DarkTheme.sDarkTheme.mIconError);
					dlg.mWindowFlags |= .Modal;
					dlg.AddOkCancelButtons(new (dlg) =>
						{
							ProcessStartInfo psi = scope ProcessStartInfo();
							psi.SetFileName("https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022");
							psi.UseShellExecute = true;
							psi.SetVerb("Open");
							var process = scope SpawnedProcess();
							process.Start(psi).IgnoreError();
						},
						new (dlg) =>
						{
						});
					((DarkButton)dlg.mButtons[0]).Label = "Open Link";
					dlg.PopupWindow(GetActiveWindow());
					Beep(.Error);
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

			if (compileKind == .DebugComptime)
			{
				mTargetDidInitBreak = true;
				mTargetStartWithStep = false;
				mDebugger.ComptimeAttach(mBfBuildCompiler);
				mDebugger.RehupBreakpoints(true);
				mBfBuildCompiler.ForceRebuild();
			}

			bool lastCompileHadMessages = mLastCompileHadMessages;
			mLastCompileFailed = false;
			mLastCompileSucceeded = false;
			mLastCompileHadMessages = false;
			mCompileSinceCleanCount++;
			ShowPanel(mOutputPanel, false);

			if (mDisableBuilding)
			{
				OutputErrorLine("Compiling disabled!");
				return false;
			}

			if (mExecutionQueue.Count > 0)
				return false;

			if ((mDebugger != null) && (mDebugger.mIsRunning) && (hotProject == null) && (compileKind != .WhileRunning))
			{
				Debug.Assert(!mDebugger.mIsRunningCompiled);
				Debug.Assert((compileKind == .Normal) || (compileKind == .DebugComptime));
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

			if ((compileKind == .RunAfter) || (compileKind == .DebugAfter))
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
				CompileFailed(scope .());
				return false;
			}

			ProcessBfCompileCmd processCompileCmd = new ProcessBfCompileCmd();
			processCompileCmd.mBfPassInstance = passInstance;
			processCompileCmd.mCompileKind = compileKind;
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

		bool StartupProject(bool doDebug, bool wasCompiled)
		{
			mProfilePanel.Clear();

			if (mWorkspace.mStartupProject == null)
			{
				OutputErrorLine("No startup project started");
				return false;
			}
			var project = mWorkspace.mStartupProject;
			var workspaceOptions = GetCurWorkspaceOptions();
			var options = GetCurProjectOptions(project);
			if (options == null)
			{
				OutputErrorLine("Startup project '{0}' not enabled", mWorkspace.mStartupProject.mProjectName);
				return false;
			}

			mDebugger.ClearInvalidBreakpoints();

			mTargetDidInitBreak = false;
			mTargetHadFirstBreak = false;

			//options.mDebugOptions.mCommand

			String launchPathRel = scope String();
			ResolveConfigString(mPlatformName, workspaceOptions, project, options, options.mDebugOptions.mCommand, "debug command", launchPathRel);
			String arguments = scope String();
			ResolveConfigString(mPlatformName, workspaceOptions, project, options, "$(Arguments)", "debug command arguments", arguments);
			String workingDirRel = scope String();
			ResolveConfigString(mPlatformName, workspaceOptions, project, options, "$(WorkingDir)", "debug working directory", workingDirRel);
			var workingDir = scope String();
			Path.GetAbsolutePath(workingDirRel, project.mProjectDir, workingDir);

			String launchPath = scope String();
			Path.GetAbsolutePath(launchPathRel, workingDir, launchPath);

			String targetPath = scope .();
			ResolveConfigString(mPlatformName, workspaceOptions, project, options, "$(TargetPath)", "Target path", targetPath);

			IDEUtils.FixFilePath(launchPath);
			IDEUtils.FixFilePath(targetPath);

			if (launchPath.IsEmpty)
			{
				OutputErrorLine("No debug target path was specified");
				return false;
			}

			if (workingDir.IsEmpty)
				Path.GetDirectoryPath(launchPath, workingDir).IgnoreError();

			if (launchPath.EndsWith(".html"))
			{
				arguments.Set(scope $"\"{launchPath}\"");
				launchPath.Set(scope $"{gApp.mInstallDir}/WasmLaunch.exe");
			}

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

			if (!doDebug)
			{
				let runCmd = QueueRun(launchPath, arguments, workingDir, .None);
				runCmd.mRunFlags |= .NoWait;
				return true;
			}

			if (mSettings.mDebugConsoleKind == .Embedded)
			{
				ShowConsole();
#if BF_PLATFORM_WINDOWS
				mConsolePanel.Attach();
#endif
			}

			if (mSettings.mDebugConsoleKind == .RedirectToImmediate)
			{
				ShowImmediatePanel();
			}

			DebugManager.OpenFileFlags openFileFlags = .None;

			if ((mSettings.mDebugConsoleKind == .RedirectToImmediate) || (mSettings.mDebugConsoleKind == .RedirectToOutput))
				openFileFlags |= .RedirectStdOutput | .RedirectStdError;

			if (!mDebugger.OpenFile(launchPath, targetPath, arguments, workingDir, envBlock, wasCompiled, workspaceOptions.mAllowHotSwapping, openFileFlags))
			{
#if BF_PLATFORM_WINDOWS
				if (!mSettings.mAlwaysEnableConsole)
					mConsolePanel.Detach();
#endif
				DeleteAndNullify!(mCompileAndRunStopwatch);
				return false;
			}

			CheckDebugVisualizers();

			mDebugger.mIsRunning = true;
			mDebugger.IncrementSessionIdx();
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
					(project.mGeneralOptions.mTargetType == Project.TargetType.BeefGUIApplication))
					mMainBreakpoint = mDebugger.CreateSymbolBreakpoint("-BeefStartProgram");
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
			mDebugger.IncrementSessionIdx();
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

		void ShowStartupFile()
		{
			bool hasSourceShown = false;
			WithSourceViewPanels(scope [&] (panel) =>
				{
					hasSourceShown = true;
				});
			if (hasSourceShown)
				return;

			if (mWorkspace.mStartupProject != null)
			{
				bool didShow = false;

				mWorkspace.mStartupProject.WithProjectItems(scope [&] (item) =>
					{
						if (didShow)
							return;

						if ((item.mName.Equals("main.bf", .OrdinalIgnoreCase)) ||
							(item.mName.Equals("program.bf", .OrdinalIgnoreCase)))
						{
							ShowProjectItem(item, false);
							didShow = true;
						}
					});
			}
		}

		public void CreateDefaultLayout(bool allowSavedLayout = true)
		{
			//TODO:
			//mConfigName.Set("Dbg");

			if ((allowSavedLayout) && (!mRunningTestScript) && (!mIsFirstRun) && (LoadDefaultLayoutData()))
			{
				return;
			}

			bool docOnlyView = false;

			TabbedView projectTabbedView = CreateTabbedView();
			SetupTab(projectTabbedView, "Workspace", 0, mProjectPanel, false);
			projectTabbedView.SetRequestedSize(GS!(200), GS!(200));
			projectTabbedView.mWidth = GS!(200);

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
			outputTabbedView.SetRequestedSize(GS!(250), GS!(250));

			SetupTab(outputTabbedView, "Output", GS!(150), mOutputPanel, false);
			SetupTab(outputTabbedView, "Immediate", GS!(150), mImmediatePanel, false);
			//outputTabbedView.AddTab("Find Results", 150, mFindResultsPanel, false);

			var watchTabbedView = CreateTabbedView();
			watchTabbedView.SetRequestedSize(GS!(250), GS!(250));
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

			String extraStr = scope .();

			String exePath = scope .();
			Environment.GetExecutableFilePath(exePath);
			if (exePath.Contains(@"\host\"))
				extraStr.Append("host");

			String exeFileName = scope .();
			Path.GetFileNameWithoutExtension(exePath, exeFileName);
			int slashPos = exeFileName.IndexOf('_');
			if (slashPos != -1)
			{
				if (!extraStr.IsEmpty)
					extraStr.Append(" ");
				extraStr.Append(exeFileName, slashPos + 1);
			}

			if (!extraStr.IsEmpty)
				title.AppendF(" [{}]", extraStr);

			mMainWindow.SetTitle(title);
		}

		public void CreateSpellChecker()
		{
#if !CLI
			mSpellChecker = new SpellChecker();
			if (mSpellChecker.Init(scope String(mInstallDir, "en_US")) case .Err)
			{
				DeleteAndNullify!(mSpellChecker);
			}
#endif
		}

		public FileVersionInfo GetVersionInfo(out DateTime exeTime)
		{
			exeTime = default;

			if (mVersionInfo == null)
			{
				String exeFilePath = scope .();
				Environment.GetExecutableFilePath(exeFilePath);
				mVersionInfo = new .();
				mVersionInfo.GetVersionInfo(exeFilePath).IgnoreError();
				if (!String.IsNullOrEmpty(mVersionInfo.FileVersion))
					Debug.Assert(mVersionInfo.FileVersion.StartsWith(cVersion));
#if BF_PLATFORM_WINDOWS
				exeTime = File.GetLastWriteTime(exeFilePath).GetValueOrDefault();
#endif
			}
			return mVersionInfo;
		}

#if !CLI
		public override void Init()
		{
			scope AutoBeefPerf("IDEApp.Init");
			//int zag = 123;

			if (mVerbosity == .Default)
				mVerbosity = .Detailed;

			mStartedWithTestScript = mRunningTestScript;

			mCommands.Init();
			EnableGCCollect = mEnableGCCollect;

			if (mConfigName.IsEmpty)
				mConfigName.Set("Debug");
			if (mPlatformName.IsEmpty)
				mPlatformName.Set(sPlatform64Name ?? sPlatform32Name);

			Directory.GetCurrentDirectory(mInitialCWD);

#if DEBUG
			//mColorMatrix = Matrix4.Identity;
#endif

			base.Init();
			mSettings.Apply();

			mGitManager.Init();

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
				if (!mIsFirstRun)
					mSettings.Load();
				mSettings.Apply();
				mIsFirstRun = !mSettings.mLoadedSettings;
#if !CLI && BF_PLATFORM_WINDOWS
				if (!mSettings.mTutorialsFinished.mRanDebug)
				{
					let exePath = scope String();
					Environment.GetExecutableFilePath(exePath);
					if (exePath.EndsWith("_d.exe", .OrdinalIgnoreCase))
					{
						if (Windows.MessageBoxA(default, "Are you sure you want to run the debug build of the Beef IDE? This is useful for debugging Beef issues but execution speed will be much slower.", "RUN DEBUG?",
							Windows.MB_ICONQUESTION | Windows.MB_YESNO) != Windows.IDYES)
						{
							Stop();
							return;
						}

					}
					mSettings.mTutorialsFinished.mRanDebug = true;
				}
#endif
			}

			Font.AddFontFailEntry("Segoe UI", scope String()..AppendF("{}fonts/NotoSans-Regular.ttf", mInstallDir));

			DarkTheme aTheme = new DarkTheme();
			mSettings.mUISettings.Apply(); // Apply again to set actual theme
			aTheme.Init();
			ThemeFactory.mDefault = aTheme;

			mTinyCodeFont = new Font();
			mCodeFont = new Font();
			mTermFont = new Font();

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
#if BF_PLATFORM_WINDOWS
			mTerminalPanel = new TerminalPanel();
			mTerminalPanel .Init();
			mTerminalPanel.mAutoDelete = false;
			mConsolePanel = new ConsolePanel();
			mConsolePanel.Init();
			mConsolePanel.mAutoDelete = false;
#endif
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
			mDiagnosticsPanel = new DiagnosticsPanel();
			mDiagnosticsPanel.mAutoDelete = false;
			mErrorsPanel = new ErrorsPanel();
			mErrorsPanel.mAutoDelete = false;
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
			mBookmarksPanel = new BookmarksPanel();
			mBookmarksPanel.mAutoDelete = false;

			GetVersionInfo(var exeDate);
			let localExeDate = exeDate.ToLocalTime();

			String exeDateStr = scope .();
			localExeDate.ToShortDateString(exeDateStr);
			exeDateStr.Append(" at ");
			localExeDate.ToShortTimeString(exeDateStr);

			OutputLine("IDE Started. Version {} built {}.", mVersionInfo.FileVersion, exeDateStr);

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
				LoadWorkspace(mVerb);
			}
			else if (mWorkspace.IsSingleFileWorkspace)
			{
				mWorkspace.mName = new String();
				Path.GetFileNameWithoutExtension(mWorkspace.mCompositeFile.mFilePath, mWorkspace.mName);
				LoadWorkspace(mVerb);
			}

			bool loadedWorkspaceUserData = false;

			if ((!mRunningTestScript) && (LoadWorkspaceUserData()))
			{
				loadedWorkspaceUserData = true;
			}

			if (mSetConfigName != null)
			{
				mConfigName.Set(mSetConfigName);
				DeleteAndNullify!(mSetConfigName);
			}

			if (mSetPlatformName != null)
			{
				mPlatformName.Set(mSetPlatformName);
				DeleteAndNullify!(mSetPlatformName);
			}

			WorkspaceLoaded();

			if ((mIsFirstRun) && (!loadedWorkspaceUserData))
			{
				GetWorkspaceRect(var workX, var workY, var workWidth, var workHeight);

				int32 height = (int32)(workHeight * 0.85f);
				int32 width = Math.Min(4 * height / 3, (int32)(workWidth * 0.85f));

				mRequestedWindowRect = .(workX + (workWidth - width) / 2, workY + (workHeight - height) / 2, width, height);
			}

			if (!loadedWorkspaceUserData)
				CreateDefaultLayout();

			//
			{
				BFWindow.Flags flags = .Border | .ThickFrame | .Resizable | .SysMenu |
					.Caption | .Minimize | .Maximize | .QuitOnClose | .Menu | .PopupPosition | .AcceptFiles;
				if (mRunningTestScript)
					flags |= .NoActivate;

				if (mRequestedShowKind == .Maximized || mRequestedShowKind == .ShowMaximized)
					flags |= .ShowMaximized;

				scope AutoBeefPerf("IDEApp.Init:CreateMainWindow");
				mMainWindow = new WidgetWindow(null, "Beef IDE", (int32)mRequestedWindowRect.mX,
					(int32)mRequestedWindowRect.mY, (int32)mRequestedWindowRect.mWidth, (int32)mRequestedWindowRect.mHeight,
					flags, mMainFrame);
			}

			if (mIsFirstRun)
			{
				// If this is our first time running, set up a scale based on DPI
				int dpi = mMainWindow.GetDPI();
				if (dpi >= 120)
				{
					mSettings.mUISettings.mScale = 100 * Math.Min(dpi / 96.0f, 4.0f);
					mSettings.Apply();
				}
			}

			UpdateTitle();
			mMainWindow.SetMinimumSize(GS!(480), GS!(360));
			mMainWindow.mIsMainWindow = true;
			mMainWindow.mOnMouseUp.Add(new => MouseUp);
			mMainWindow.mOnWindowKeyDown.Add(new => SysKeyDown);
			mMainWindow.mOnWindowKeyUp.Add(new => SysKeyUp);
			mMainWindow.mOnWindowCloseQuery.Add(new => AllowClose);
			mMainWindow.mOnDragDropFile.Add(new => DragDropFile);
			CreateMenu();
			UpdateRecentDisplayedFilesMenuItems();
			if (mRecentlyDisplayedFiles.Count > 0)
				ShowRecentFile(0);

			mProjectPanel.RebuildUI();

			if (mProcessAttachId != 0)
			{
				Process debugProcess = scope Process();
				switch (debugProcess.GetProcessById(mProcessAttachId))
				{
				case .Ok:
				case .Err:
					Fail(scope String()..AppendF("Unable to locate process id {0}", mProcessAttachId));
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
					mDebugger.IncrementSessionIdx();
					mExecutionPaused = false; // Make this false so we can detect a Pause immediately
					mIsAttachPendingSourceShow = true;
				}
				else
				{
					Fail(scope String()..AppendF("Failed to load minidump '{0}'", mCrashDumpPath));
				}
			}
			else if (mLaunchData != null)
			{
				if (mLaunchData.mTargetPath == null)
				{
					if (mLaunchData.mPaused)
						RunWithStep();
					else
						CompileAndRun(true);
				}
				else
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
			ShowStartupFile();
#if !CLI
			mFileRecovery.CheckWorkspace();
#endif

			if ((mIsFirstRun) && (!mWorkspace.IsInitialized))
				ShowWelcome();

			if ((mSettings.mUISettings.mShowStartupPanel) && (!mIsFirstRun) && (!mWorkspace.IsInitialized))
				ShowStartup();
		}
#endif
		void ShowWelcome()
		{
			WelcomePanel welcomePanel = new .();
			TabbedView tabbedView = GetDefaultDocumentTabbedView();
			let tabButton = SetupTab(tabbedView, "Welcome", 0, welcomePanel, true);
			tabButton.Activate();
		}

		void ShowStartup()
		{
			StartupPanel startupPanel = new .();
			TabbedView tabbedView = GetDefaultDocumentTabbedView();
			let tabButton = SetupTab(tabbedView, "Startup", 0, startupPanel, true);
			tabButton.Activate();
		}

		public void CheckLoadConfig()
		{
			if (mBeefConfig.mLibsChanged)
				LoadConfig();
		}

		protected void LoadConfig()
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
			float tinyFontSize = fontSize * 8.0f / 9.0f;

			String err = scope String();
			void FontFail(StringView name)
			{
				if (!err.IsEmpty)
					err.Append("\n");
				err.AppendF("Failed to load font '{}'", name);
			}

			bool isFirstFont = true;
			for (var fontName in mSettings.mEditorSettings.mFonts)
				FontLoop:
				{
					bool isOptional;
					if (isOptional = fontName.StartsWith("?"))
						fontName = scope:FontLoop String(fontName, "?".Length);

					if (isFirstFont)
					{
						mTinyCodeFont.Dispose(true);
						isFirstFont = !mTinyCodeFont.Load(fontName, tinyFontSize);
						mCodeFont.Dispose(true);
						if (!mCodeFont.Load(fontName, fontSize))
							FontFail(fontName);
					}
					else
					{
						mTinyCodeFont.AddAlternate(fontName, tinyFontSize).IgnoreError();
						if ((mCodeFont.AddAlternate(fontName, fontSize) case .Err) && (!isOptional))
							FontFail(fontName);
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
				mCodeFont.AddAlternate("Segoe UI Symbol", fontSize).IgnoreError();
				mCodeFont.AddAlternate("Segoe UI Historic", fontSize).IgnoreError();
				mCodeFont.AddAlternate("Segoe UI Emoji", fontSize).IgnoreError();

				mTinyCodeFont.AddAlternate("Segoe UI", tinyFontSize);
				mTinyCodeFont.AddAlternate("Segoe UI Symbol", tinyFontSize).IgnoreError();
				mTinyCodeFont.AddAlternate("Segoe UI Historic", tinyFontSize).IgnoreError();
				mTinyCodeFont.AddAlternate("Segoe UI Emoji", tinyFontSize).IgnoreError();

				/*mCodeFont.AddAlternate(new String("fonts/segoeui.ttf"), fontSize);
				mCodeFont.AddAlternate(new String("fonts/seguisym.ttf"), fontSize);
				mCodeFont.AddAlternate(new String("fonts/seguihis.ttf"), fontSize);

				mTinyCodeFont.AddAlternate(new String("fonts/segoeui.ttf"), tinyFontSize);
				mTinyCodeFont.AddAlternate(new String("fonts/seguisym.ttf"), tinyFontSize);
				mTinyCodeFont.AddAlternate(new String("fonts/seguihis.ttf"), tinyFontSize);*/
			}

			mTermFont.Load("Cascadia Mono Regular", fontSize);

			if (!err.IsEmpty)
			{
				OutputErrorLine(err);
				Fail(err);
			}

			//mTinyCodeFont.Load(scope String(BFApp.sApp.mInstallDir, "fonts/SourceCodePro-Regular.ttf"), fontSize);

			float squiggleScale = DarkTheme.sScale;
			if (squiggleScale < 3.0f)
				squiggleScale = (float)Math.Round(squiggleScale);
			mSquiggleImage.Scale(squiggleScale);
			mCircleImage.Scale(DarkTheme.sScale);

			mMainWindow?.SetMinimumSize(GS!(480), GS!(360));

			/*for (var window in gApp.mWindows)
			{

				window.SetMinimumSize(GS!());
			}*/
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

		public EvalResult DebugEvaluate(String expectedType, String expr, String outVal, int cursorPos = -1, DebugManager.Language language = .NotSet,
			DebugManager.EvalExpressionFlags expressionFlags = .None, delegate void(String) pendingHandler = null)
		{
			defer
			{
				if (mPendingDebugExprHandler !== pendingHandler)
					delete pendingHandler;
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

		void DebuggerComptimeStop()
		{
			mDebugger.DisposeNativeBreakpoints();
			mDebugger.Detach();
		}

		void DebuggerPaused()
		{
			mDebugger.IncrementStateIdx();
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
			if (mWatchPanel == null)
				return;
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
				ProcessBeefCompileResults(null, .Normal, mWorkspace.mStartupProject, null);
			}
			else
			{
				if (mHotResolveTryIdx == 0)
				{
					OutputErrorLine("Hot type data changes cannot be applied because of the following types");
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
							resolveCompiler.QueueProjectSource(projectSource, .None, false);
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
						if ((resolveCompiler == mBfResolveCompiler) && (resolveCompiler != null))
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

					Platform.BfpFile* stdOut = null;
					Platform.BfpFile* stdError = null;
					mDebugger.GetStdHandles(null, &stdOut, &stdError);
					if (stdOut != null)
					{
						FileStream fileStream = new FileStream();
						fileStream.Attach(stdOut);
						Thread thread = new Thread(new => ReadDebugOutputThread);
						thread.Start(fileStream, true);
					}
					if (stdError != null)
					{
						FileStream fileStream = new FileStream();
						fileStream.Attach(stdError);
						Thread thread = new Thread(new => ReadDebugOutputThread);
						thread.Start(fileStream, true);
					}

					using (mDebugOutputMonitor.Enter())
					{
						if (!mDebugOutput.IsEmpty)
						{
							mDebugOutput.Replace("\r", "");
							if (mSettings.mDebugConsoleKind == .RedirectToOutput)
								mOutputPanel.Write(mDebugOutput);
							if (mSettings.mDebugConsoleKind == .RedirectToImmediate)
								mImmediatePanel.Write(mDebugOutput);
							mDebugOutput.Clear();
						}
					}

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
					mBfBuildCompiler?.CancelBackground();
					mBfResolveCompiler?.CancelBackground();

					if ((!mBfBuildCompiler.HasQueuedCommands()) &&
						((mBfResolveCompiler == null) || (!mBfResolveCompiler.HasQueuedCommands())))
					{
						didClean = true;
						OutputLine("Cleaned Beef.");

						if (mErrorsPanel != null)
							mErrorsPanel.ClearParserErrors(null);

						DeleteAndNullify!(mBfResolveCompiler);
						DeleteAndNullify!(mBfResolveSystem);
						DeleteAndNullify!(mBfResolveHelper);
						DeleteAndNullify!(mBfBuildCompiler);
						DeleteAndNullify!(mBfBuildSystem);

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
					mBfBuildCompiler?.CancelBackground();
					mBfResolveCompiler?.CancelBackground();

					if ((!mBfBuildCompiler.HasQueuedCommands()) && (!mBfResolveCompiler.HasQueuedCommands())
					#if IDE_C_SUPPORT
						&& (!mDepClang.HasQueuedCommands()) && (!mResolveClang.HasQueuedCommands())
#endif
						)
					{
						OutputLine("Cleaned.");

						if (mErrorsPanel != null)
							mErrorsPanel.ClearParserErrors(null);

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
							let options = GetCurProjectOptions(project);
							if (options == null)
								continue;
							if (!options.mBuildOptions.mCleanCmds.IsEmpty)
							{
								if (mBuildContext == null)
									mBuildContext = new BuildContext();
								mBuildContext.QueueProjectCustomBuildCommands(project, "", .Always, options.mBuildOptions.mCleanCmds);
							}

							// Force running the "if files changed" commands
							project.mForceCustomCommands = true;
							project.mNeedsTargetRebuild = true;

							// Don't delete custom build artifacts
							if (project.mGeneralOptions.mTargetType == .CustomBuild)
								continue;

							List<String> projectFiles = scope .();
							defer ClearAndDeleteItems(projectFiles);

							GetTargetPaths(project, mPlatformName, workspaceOptions, options, projectFiles);

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

					CurrentWorkspaceConfigChanged();

					if (mBfResolveSystem != null)
					{
						/*for (var project in mWorkspace.mProjects)
						{
							if (IsProjectEnabled(project))
								QueueProjectItems(project);
						}*/
						mBfResolveCompiler.QueueDeferredResolveAll();
					}
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
					OutputLineSmart(msg);
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
					StringView cmd = default;
					StringView param = default;

					if (paramIdx > 0)
					{
						cmd = msg.Substring(0, paramIdx);
						param = msg.Substring(paramIdx + 1);
					}
					else
						cmd = msg;

					bool isOutput = (cmd == "msg") || (cmd == "dbgEvalMsg") || (cmd == "log");
					if (cmd == "msgLo")
					{
						if (mVerbosity < .Diagnostic)
							continue;
						isOutput = true;
					}

					if (isOutput)
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
					else if ((cmd == "error") || (cmd == "errorsoft"))
					{
						if ((mRunningTestScript) && (!IsCrashDump) && (!mScriptManager.IsErrorExpected(param, false)))
							mScriptManager.Fail(param);

						bool isFirstMsg = true;

						String tempStr = scope String();
						bool focusOutput = false;

						while (true)
						{
							StringView errorMsg = default;

							int infoPos = param.IndexOf("\x01");
							if (infoPos == 0)
							{
								Widget addWidget = null;
								LeakWidget leakWidget = null;

								int endPos = param.IndexOf("\n");
								if (endPos == -1)
									break;
								String leakStr = scope String(param, 1, endPos - 1);
								param.RemoveFromStart(endPos + 1);
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
								param.RemoveFromStart(infoPos);
							}
							else
								errorMsg = param;

							if (!errorMsg.IsEmpty)
							{
								if (isFirstMsg)
								{
									if (mTestManager != null)
									{
										// Give test manager time to flush
										Thread.Sleep(100);
										mTestManager.Update();
										mOutputPanel.Update();
									}

									OutputLineSmart(scope String("ERROR: ", scope String(errorMsg)));
									if ((gApp.mRunningTestScript) || (cmd == "errorsoft"))
									{
										// The 'OutputLineSmart' would already call 'Fail' when running test scripts
									}
									else
									{
										Fail(scope String(errorMsg));
									}
									isFirstMsg = false;
								}
								else
									Output(scope String(errorMsg));
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
						String infoString = scope .();
						if (breakpoint != null)
							infoString.AppendF("Memory breakpoint hit: '0x{0:X08}' ({1})", (int64)memoryAddress, breakpoint.mMemoryWatchExpression);
						else
							infoString.AppendF("Memory breakpoint hit: '0x{0:X08}'", (int64)memoryAddress);
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
					else if (cmd == "script")
					{
						String absTestPath = scope String();
						if (mWorkspace.mDir != null)
							Path.GetAbsolutePath(param, mWorkspace.mDir, absTestPath);
						else
							Path.GetFullPath(param, absTestPath);
						if (mScriptManager.mFailed)
							mScriptManager.Clear();
						mScriptManager.mSoftFail = true;
						mScriptManager.SetTimeoutMS(20 * 60 * 1000); // 20 minute timeout
						mScriptManager.QueueCommandFile(absTestPath);
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
#if BF_PLATFORM_WINDOWS
					if (!mSettings.mAlwaysEnableConsole)
						mConsolePanel.Detach();
#endif
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
								if (stackInfo.Contains("BeefStartProgram"))
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

							switch (mDebugger.GetRunState())
							{
							case .Exception:
								String exceptionLine = scope String();
								mDebugger.GetCurrentException(exceptionLine);
								var exceptionData = String.StackSplit!(exceptionLine, '\n');

								String exHeader = scope String()..AppendF("Exception {0}", exceptionData[1]);
								if (exceptionData.Count >= 3)
									exHeader = exceptionData[2];

								String exString = scope String()..AppendF("{0} at {1}", exHeader, exceptionData[0]);

								OutputLine(exString);
								if (!IsCrashDump)
									Fail(exString);
							default:
							}
						}
					}
					break;
				}
				else if ((mDebugger != null) && (mDebugger.GetRunState() == .TargetUnloaded))
				{
					if (mWorkspace.mHadHotCompileSinceLastFullCompile)
					{
						// Had hot compiles - we need to recompile!
						Compile(.WhileRunning);
					}
					else
					{
						mDebugger.Continue();
					}
					mWorkspace.StoppedRunning();
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
						{
#if BF_PLATFORM_WINDOWS
							if (mConsolePanel.mBeefConAttachState case .Connected(let processId))
								mDebugger.ForegroundTarget(processId);
							else
								mDebugger.ForegroundTarget(0);
#endif
						}
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
				mBuildContext.mUpdateCnt++;

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

			if ((mBuildContext == null) && (!IsCompiling))
				DeleteAndNullify!(mLaunchData);

			mErrorsPanel?.UpdateAlways();
#if BF_PLATFORM_WINDOWS
			if ((mConsolePanel != null) && (mConsolePanel.mBeefConAttachState case .Attached(let consoleProcessId)))
			{
				if (!mDebugger.mIsRunning)
				{
					mConsolePanel.Detach();
				}
				else
				{
					int32 debugProcessId = mDebugger.GetProcessId();
					if (debugProcessId != 0)
					{
						BeefConConsoleProvider.Pipe pipe = scope .();
						pipe.Connect(Process.CurrentId, -consoleProcessId).IgnoreError();
						//pipe.Connect(Process.CurrentId, )
						//pipe.Connect(123, -consoleProcessId).IgnoreError();

						pipe.StartMessage(.Attached);
						pipe.Stream.Write(debugProcessId);
						pipe.EndMessage();
						mConsolePanel.Detach();

						mConsolePanel.mBeefConAttachState = .Connected(consoleProcessId);
						mDebugger.ForegroundTarget(consoleProcessId);
					}
				}
			}
#endif
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

			for (var entry in mRecentlyDisplayedFiles)
			{
				if (Path.Equals(entry, oldPath))
				{
					entry.Set(newPath);
					UpdateRecentFileMenuItems();
					break;
				}
			}

			RenameEditData(oldPath, newPath);

			WithTabs(scope (tab) =>
				{
					var sourceViewPanel = tab.mContent as SourceViewPanel;
					if (sourceViewPanel != null)
					{
						if (Path.Equals(sourceViewPanel.mFilePath, oldPath))
						{
							var sourceViewTab = (IDEApp.SourceViewTabButton)tab;

							sourceViewPanel.PathChanged(newPath);
							tab.Label = newFileName;
							float newWidth = sourceViewTab.GetWantWidth();
							if (newWidth != tab.mWantWidth)
							{
								tab.mWantWidth = newWidth;
								tab.mTabbedView.mNeedResizeTabs = true;
							}
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
						mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
						mBfBuildCompiler.QueueProjectSource(projectSource, .None, true);
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
			CompilerLog("IDEApp.OnWatchedFileChanged {} {} {}", projectItem.mName, changeType, newPath);

			ProjectListViewItem listViewItem = null;
			mProjectPanel?.mProjectToListViewMap.TryGetValue(projectItem, out listViewItem);

			String newName = null;
			if (newPath != null)
			{
				newName = scope:: String();
				Path.GetFileName(newPath, newName);
			}

			if (changeType == .Renamed)
			{
				if (projectItem.mIncludeKind == .Auto)
				{
					let projectFileItem = projectItem as ProjectFileItem;

					if (listViewItem != null)
						listViewItem.Label = newName;

					String oldPath = scope String();
					projectFileItem.GetFullImportPath(oldPath);
					projectFileItem.Rename(newName);

					FileRenamed(projectFileItem, oldPath, newPath);
					mProjectPanel?.SortItem((ProjectListViewItem)listViewItem.mParentItem);
				}
			}
			else if (changeType == .Deleted)
			{
				if (projectItem.mIncludeKind == .Auto)
				{
					mProjectPanel?.DoDeleteItem(listViewItem, null, .ForceRemove);
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

						if (mProjectPanel != null)
						{
							mProjectPanel.AddProjectItem(projectSource);
							mProjectPanel.QueueSortItem(listViewItem);
						}
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

						if (mProjectPanel != null)
						{
							mProjectPanel.AddProjectItem(newFolder);
							mProjectPanel.QueueSortItem(listViewItem);
						}

						newFolder.mAutoInclude = true;
						if (mProjectPanel != null)
							mProjectPanel.QueueRehupFolder(newFolder);
					}
				}
			}
			else if (changeType == .Failed)
			{
				if (mBfResolveCompiler?.HasRebuildFileWatches() == true)
				{
					mBfResolveCompiler.AddChangedDirectory("*");
					mBfResolveCompiler.QueueDeferredResolveAll();
				}
				if (mBfBuildCompiler?.HasRebuildFileWatches() == true)
					mBfBuildCompiler.AddChangedDirectory("*");

				if (mProjectPanel != null)
				{
					if (let projectFolder = projectItem as ProjectFolder)
						mProjectPanel.QueueRehupFolder(projectFolder);
				}

				// Manually scan project files for changes
				mWorkspace.WithProjectItems(scope (projectItem) =>
					{
						var projectSource = projectItem as ProjectSource;
						if (projectSource != null)
						{
							bool changed = false;
							var editData = projectSource.mEditData;
							if (editData != null)
							{
								if (File.GetLastWriteTime(editData.mFilePath) case .Ok(let fileTime))
								{
									if (fileTime != projectSource.mEditData.mFileTime)
									{
										if (!projectSource.mEditData.mMD5Hash.IsZero)
										{
											var text = scope String();
											if (File.ReadAllText(editData.mFilePath, text, true) case .Ok)
											{
												var hash = MD5.Hash(.((.)text.Ptr, text.Length));
												if (hash != projectSource.mEditData.mMD5Hash)
												{
													changed = true;
												}
											}
										}
										else
											changed = true;
									}

									if (changed)
										mFileWatcher.FileChanged(editData.mFilePath);
									projectSource.mEditData.mFileTime = fileTime;
								}
							}
						}
					});
			}
		}

		void UpdateWorkspace()
		{
			mFileWatcher.Update();
#if !CLI
			mFileRecovery.Update();
#endif

			bool appHasFocus = false;
			for (var window in mWindows)
				appHasFocus |= window.mHasFocus;

			if ((appHasFocus) && (!mAppHasFocus))
			{
				CheckReloadSettings();

				bool hadChange = false;
				for (var entry in mWorkspace.mProjectFileEntries)
				{
					if (entry.HasFileChanged())
					{
						if (!hadChange)
						{
							String text = scope .();

							if (entry.mProjectName != null)
								text.AppendF($"The '{entry.mProjectName}' project file has been modified externally.");
							else
								text.Append("The workspace file has been modified externally.");

							text.Append("\n\nDo you want to reload the workspace?");

							var dialog = ThemeFactory.mDefault.CreateDialog("Reload Workspace?",
								text);
							dialog.AddYesNoButtons(new (dlg) =>
								{
									ReloadWorkspace();
								},
								new (dlg) =>
								{
								});
							dialog.PopupWindow(GetActiveWindow());
							hadChange = true;
						}
					}
				}
			}
			mAppHasFocus = appHasFocus;

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

			if (mSymbolReferenceHelper?.mKind == .Rename)
				return;

			var app = gApp;
			Debug.Assert(app != null);

			while (true)
			{
				using (mFileWatcher.mMonitor.Enter())
				{
					var dep = mFileWatcher.PopChangedDependency();
					if (dep == null)
						break;
					var projectSourceDep = dep as ProjectSource;
					if (projectSourceDep != null)
					{
						// We process these projectSources directly from the filename below
					}
					if (var path = dep as String)
					{
						StringView usePath = path;
						if (usePath.EndsWith('*'))
							usePath.RemoveFromEnd(1);
						if (mBfResolveCompiler != null)
						{
							mBfResolveCompiler.AddChangedDirectory(usePath);
							mBfResolveCompiler.QueueDeferredResolveAll();
						}
						if (mBfBuildCompiler != null)
							mBfBuildCompiler.AddChangedDirectory(usePath);
					}
				}
			}

			if ((mFileChangedDialog != null) && (mFileChangedDialog.mDialogKind == .Deleted))
			{
				for (let fileName in mFileChangedDialog.mFileNames)
				{
					mFileWatcher.RemoveChangedFile(fileName);
				}
			}

			while ( /*(appHasFocus) && */((mFileChangedDialog == null) || (mFileChangedDialog.mClosed)))
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
				if (!editData.mProjectSources.IsEmpty)
					mHaveSourcesChangedExternallySinceLastCompile = true;
				editData.SetSavedData(null, IdSpan());

				if (editData.mQueuedContent == null)
					editData.mQueuedContent = new String();
				editData.mQueuedContent.Clear();
				if (LoadTextFile(fileName, editData.mQueuedContent, false, scope () =>
					{
						editData.BuildHash(editData.mQueuedContent);
					}) case .Err(let err))
				{
					if (err case .OpenError(.SharingViolation))
					{
						// Put back on the list and try later
						mFileWatcher.AddChangedFile(changedFile);
						changedFile = null;
						break;
					}
					editData.mFileDeleted = true;
				}
				editData.GetFileTime();

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
						mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
						mBfResolveCompiler.QueueDeferredResolveAll();
						mBfResolveCompiler.QueueRefreshViewCommand();
					}
				}
			}
		}

#if !CLI && BF_PLATFORM_WINDOWS
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
						OutputLine("IPC: Hosting"); //
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

		void UpdateDeferredOpens()
		{
			void DoDeferredOpen(DeferredOpenKind openKind)
			{
				switch (openKind)
				{
				case .NewWorkspaceOrProject:
					var dialog = ThemeFactory.mDefault.CreateDialog("Create Workspace?",
						"Do you want to create an empty workspace or a new project?");
					dialog.AddButton("Empty Workspace", new (evt) =>
						{
							mDeferredOpen = .NewWorkspace;
						});
					dialog.AddButton("New Project", new (evt) =>
						{
							CloseWorkspace();
							FinishShowingNewWorkspace();
							Cmd_NewProject();
						});
					dialog.PopupWindow(GetActiveWindow());
				case .NewWorkspace:

					if (mDeferredOpenFileName != null)
					{
						let directoryPath = scope String();
						Path.GetDirectoryPath(mDeferredOpenFileName, directoryPath);

						if (File.Exists(mDeferredOpenFileName))
						{
							var dialog = ThemeFactory.mDefault.CreateDialog("Already Exists",
								scope String()..AppendF("A Beef workspace already exists at '{}'. Do you want to open it?", directoryPath),
								DarkTheme.sDarkTheme.mIconWarning);
							dialog.mDefaultButton = dialog.AddButton("Yes", new (evt) =>
								{
									DoOpenWorkspace(true);
								});
							dialog.AddButton("No", new (evt) =>
								{
									DeleteAndNullify!(mDeferredOpenFileName);
								});
							dialog.PopupWindow(GetActiveWindow());
							return;
						}
						else if (!IDEUtils.IsDirectoryEmpty(directoryPath))
						{
							var dialog = ThemeFactory.mDefault.CreateDialog("Already Exists",
								scope String()..AppendF("A non-empty directory already exists at '{}'. Are you sure you want to create a workspace there?", directoryPath),
								DarkTheme.sDarkTheme.mIconWarning);
							dialog.mDefaultButton = dialog.AddButton("Yes", new (evt) =>
								{
									DoOpenWorkspace(true);
								});
							dialog.AddButton("No", new (evt) =>
								{
									DeleteAndNullify!(mDeferredOpenFileName);
								});
							dialog.PopupWindow(GetActiveWindow());
							return;
						}
					}

					DoOpenWorkspace(true);
					if (mDeferredOpen != .None)
						mDeferredOpen = _;
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
			case .Workspace,.NewWorkspace,.NewWorkspaceOrProject:

				if ((mDeferredOpenFileName == null) && (deferredOpen == .Workspace))
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
		}

		void VerifyModifiedBuffers()
		{
			if (mSymbolReferenceHelper?.mKind == .Rename)
				return;

			mWorkspace.WithProjectItems(scope (projectItem) =>
				{
					var projectSource = projectItem as ProjectSource;
					if (projectSource != null)
					{
						if ((projectSource.mEditData != null) && (projectSource.mEditData.HasTextChanged()))
						{
							var sourceViewPanel = projectSource.mEditData?.mEditWidget.mPanel as SourceViewPanel;
							Runtime.Assert(sourceViewPanel != null, "Source marked as modified with no SourceViewPanel");
						}
					}
				});
		}

		public override void Update(bool batchStart)
		{
			scope AutoBeefPerf("IDEApp.Update");

			/*using (mWorkspace.mMonitor.Enter())
			{

			}*/

			if (mUpdateCnt % 120 == 0)
				VerifyModifiedBuffers();

			if (mWantShowOutput)
			{
				ShowOutput();
				mWantShowOutput = false;
			}

			if (mDbgFastUpdate)
			{
				RefreshRate = 240;
				MarkDirty();
			}
			else
			{
				RefreshRate = 60;
			}
#if BF_PLATFORM_WINDOWS
			if (mTerminalPanel != null)
			{
				// Detach terminal if the panel is closed
				var terminalTabButton = GetTab(mTerminalPanel);
				if (terminalTabButton == null)
				{
					mTerminalPanel.Detach();
				}
			}
#endif
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

#if !CLI && BF_PLATFORM_WINDOWS
			if (mSettings.mEnableDevMode)
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
					CompileAndRun(true);
					mRunTestDelay = 200;
				}
			}

			if (mProfilePanel != null)
				mProfilePanel.ForcedUpdate();
			if (mLongUpdateProfileId != 0)
				DoLongUpdateCheck();

			mGitManager.Update();
			mPackMan.Update();
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
			mDiagnosticsPanel?.UpdateStats();
			if (mScriptManager != null)
				mScriptManager.Update();

#if BF_PLATFORM_WINDOWS
			if (mConsolePanel != null)
			{
				if ((mSettings.mAlwaysEnableConsole) ||
					((mSettings.mDebugConsoleKind == .Embedded) && (mDebugger.mIsRunning)))
					mConsolePanel.Attach();
				else
					mConsolePanel.Detach();
			}
#endif
			if (mTestManager != null)
			{
				mTestManager.Update();
				if ((mTestManager.mIsDone) && (mTestManager.mQueuedOutput.IsEmpty))
				{
					if (mMainFrame != null)
						mMainFrame.mStatusBar.SelectConfig(mTestManager.mPrevConfigName);
					DeleteAndNullify!(mTestManager);
				}
			}

			UpdateExecution();
			if (mBfResolveSystem != null)
				mBfResolveSystem.PerfZoneEnd();

			UpdateDeferredOpens();

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
					if (sourceViewPanel.[Friend]mDeleteAfterPostRemoveUpdate)
						DeferDelete(sourceViewPanel);
				}
			}

			if (IDEApp.sApp.mSpellChecker != null)
				IDEApp.sApp.mSpellChecker.CheckThreadDone();

			if ((mDeferredShowSource != null) && (!mBfBuildCompiler.IsPerformingBackgroundOperation()) &&
				(mBfResolveCompiler?.IsPerformingBackgroundOperation() != true))
			{
				var deferredShowSource = mDeferredShowSource;
				mDeferredShowSource = null;
				defer delete deferredShowSource;
				ShowSourceFileLocation(deferredShowSource.mFilePath, deferredShowSource.mShowHotIdx, deferredShowSource.mRefHotIdx,
					deferredShowSource.mLine, deferredShowSource.mColumn, deferredShowSource.mHilitePosition, deferredShowSource.mShowTemp);
			}

			///
			//TODO: REMOVE
			//mDebugger.InitiateHotResolve(.ActiveMethods | .Allocations);
		}

		public override void Draw(bool forceDraw)
		{
			scope AutoBeefPerf("IDEApp.Draw");
#if CLI
			return;
#endif
#unwarn
			if (mBfResolveSystem != null)
			{
				mBfResolveSystem.PerfZoneStart("IDEApp.Draw");
				base.Draw(forceDraw);
				mBfResolveSystem.PerfZoneEnd();
				if (mBfResolveSystem.mIsTiming)
				{
					mBfResolveSystem.StopTiming();
					mBfResolveSystem.DbgPrintTimings();
				}
			}
			else
				base.Draw(forceDraw);
		}

		public void DrawSquiggle(Graphics g, float x, float y, float width)
		{
			Image squiggleImage = IDEApp.sApp.mSquiggleImage;
			int32 segSize = 30;
			float height = mSquiggleImage.mHeight;

			float curX = x;
			float curWidth = width;

			float drawY = y + gApp.mCodeFont.GetHeight();
			while (curWidth > 0)
			{
				float drawWidth = Math.Min(curWidth, segSize - (curX % segSize));

				float u1 = ((int32)curX % segSize) / (float)squiggleImage.mSrcWidth;
				float u2 = u1 + drawWidth / (float)squiggleImage.mSrcWidth;

				g.DrawQuad(squiggleImage, curX, drawY, u1, 0, drawWidth, height, u2, 1.0f);

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
#if BF_PLATFORM_WINDOWS && !CLI
			MessageBeep(type);
#endif
		}

#if BF_PLATFORM_WINDOWS
		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
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
