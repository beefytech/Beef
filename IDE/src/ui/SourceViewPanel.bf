using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Diagnostics;
using System.IO;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.gfx;
using Beefy.events;
using Beefy.theme.dark;
using Beefy.utils;
using Beefy.geom;
using IDE.Debugger;
using IDE.Compiler;
using System.Security.Cryptography;
using IDE.util;
using Beefy2D.utils;

namespace IDE.ui
{
    public enum SourceElementType
    {
        Normal,
        Keyword,
        Literal,
        Identifier,
        Comment,
        Method,
		Type,
		PrimitiveType,
		Struct,
		GenericParam,
        RefType,
		Interface,
        Namespace,

        Disassembly_Text,
        Disassembly_FileName,

        Error,

		BuildError,
		BuildWarning,

		VisibleWhiteSpace
    }

    //[Flags]
    public enum SourceElementFlags
    {
        Error                   = 1,
        Warning                 = 2,
        IsAfter                 = 4,
        Skipped                 = 8,
        CompilerFlags_Mask      = 0x0F,

        SpellingError           = 0x10,
        Find_Matches            = 0x20,
		Find_CurrentSelection   = 0x40,
        SymbolReference         = 0x80,
        EditorFlags_Mask        = 0xF0,

        MASK = 0xFF
    }

    public class SourceEditWidget : DarkEditWidget
    {
        public TextPanel mPanel;

        public this(SourceViewPanel panel, SourceEditWidget refEditWidget = null)
            : base(new SourceEditWidgetContent((refEditWidget != null) ? refEditWidget.mEditWidgetContent : null))
        {
            mPanel = panel;
        }

        public this(SourceViewPanel panel, SourceEditWidgetContent content)
            : base(content)
        {
            mPanel = panel;
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
        }

		public override void MouseWheel(float x, float y, float deltaX, float deltaY)
		{
			var sewc = mEditWidgetContent as SourceEditWidgetContent;
			if ((sewc.mSourceViewPanel != null) && (sewc.mSourceViewPanel.mEmbedParent != null))
			{
				if (mHasFocus)
				{
					if ((mVertScrollbar != null) && (mVertScrollbar.mAllowMouseWheel))
					{
						mVertScrollbar.MouseWheel(x, y, 0, deltaY);
						return;
					}
				}

				var target = sewc.mSourceViewPanel.mEmbedParent.mEditWidget.mEditWidgetContent;
				SelfToOtherTranslate(target, x, y, var transX, var transY);
				target.MouseWheel(transX, transY, deltaX, deltaY);
				return;
			}


			base.MouseWheel(x, y, deltaX, deltaY);
		}

        public override void GotFocus()
        {
			//Debug.WriteLine("SourceViewPanel.GotFocus {0}", this);

			if (var tabbedView = mParent.mParent as IDETabbedView)
			{
				if (tabbedView.mIsFillWidget)
					gApp.mActiveDocumentsTabbedView = tabbedView;
			}

			var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidgetContent;
			// Just got focus, flip this on until we get a KeyDown
			sourceEditWidgetContent.mIgnoreKeyChar = true;

            if (mPanel != null)
			{
				mPanel.mLastFocusTick = IDEApp.sApp.mUpdateCnt;
                mPanel.EditGotFocus();
			}

			var sewc = mEditWidgetContent as SourceEditWidgetContent;
			if ((sewc.mSourceViewPanel != null) && (sewc.mSourceViewPanel.mEmbedParent != null))
			{
				mVertScrollbar.mAllowMouseWheel = true;
				mHorzScrollbar.mAllowMouseWheel = true;
			}

			DeleteAndNullify!(gApp.mDeferredShowSource);

            base.GotFocus();
        }

        public override void LostFocus()
        {
            if (mPanel != null)
                mPanel.EditLostFocus();

			var sewc = mEditWidgetContent as SourceEditWidgetContent;
			if ((sewc.mSourceViewPanel != null) && (sewc.mSourceViewPanel.mEmbedParent != null))
			{
				mVertScrollbar.mAllowMouseWheel = false;
				mHorzScrollbar.mAllowMouseWheel = false;
			}

            base.LostFocus();
        }

		protected override bool WantsUnfocus()
		{
			if (mWidgetWindow != null)
			{
				if (mWidgetWindow.mOverWidget is PanelSplitter)
					return false;
				if (mWidgetWindow.mOverWidget is TabbedView.TabButton)
					return false;
				if (mWidgetWindow.mOverWidget is DarkTabbedView.DarkTabButtonClose)
					return false;
			}

			return base.WantsUnfocus();
		}
    }

    public class TrackedTextElementView
    {
        public TrackedTextElement mTrackedElement;
        public PersistentTextPosition mTextPosition;
        public int32 mLastBoundLine = -1;
		public int32 mLastMoveIdx; // Compare to mTrackedElement.mMoveIdx

		public this(TrackedTextElement trackedEntry)
		{
			mTrackedElement = trackedEntry;
			mTrackedElement.AddRef();
		}

		public ~this()
		{
			mTrackedElement.Deref();
		}
    }    

	public class HoverResolveTask
	{
		public int32 mCursorPos;
		public String mResult ~ delete _;
		public int32? mLine;
		public int32? mLineChar;

		public ~this()
		{
			
		}
	}

	public class SourceFindTask
	{
		public WaitEvent mDoneEvent = new WaitEvent() ~ delete _;
		public SourceViewPanel mSourceViewPanel;
		public bool mCancelling;
		public String mFilePath ~ delete _;
		public String mFileName ~ delete _;
		public String mBackupFileName ~ delete _; // Didn't match hash
		public String mFoundPath ~ delete _;
		public Dictionary<String, String> mRemapMap = new .() ~ DeleteDictionaryAndKeysAndValues!(_);
		
		public ~this()
		{
			Cancel();
			WaitFor();
		}

		public void Cancel()
		{
			mCancelling = true;
		}

		public bool WaitFor(int waitMS = -1)
		{
			return mDoneEvent.WaitFor(waitMS);
		}

		void CheckFile(String filePath)
		{
			bool hashMatches = true;
			if (!(mSourceViewPanel.mWantHash case .None))
			{
				SourceHash.Kind hashKind = mSourceViewPanel.mWantHash.GetKind();
				
				var buffer = scope String();
				SourceHash hash = ?;
				if (gApp.LoadTextFile(filePath, buffer, true, scope [&] () => { hash = SourceHash.Create(hashKind, buffer); }) case .Ok)
				{
					if (hash != mSourceViewPanel.mWantHash)
						hashMatches = false;
				}
			}

			if (!hashMatches)
			{
				if (mBackupFileName == null)
					mBackupFileName = new String(filePath);
			}
			else if (mFoundPath == null)
				mFoundPath = new String(filePath);
		}

		void SearchPath(String dirPath)
		{
			if (!dirPath.EndsWith('*'))
			{
				for (var file in Directory.EnumerateFiles(dirPath))
				{
					var fileName = scope String();
					file.GetFileName(fileName);
					if (fileName.Equals(mFileName, .OrdinalIgnoreCase))
					{
						if (mFoundPath == null)
						{
							var filePath = scope String();
							file.GetFilePath(filePath);
							CheckFile(filePath);
						}
					}
				}
			}

			String dirSearchStr = scope String();
			dirSearchStr.Append(dirPath);
			if (!dirSearchStr.EndsWith('*'))
				dirSearchStr.Append("/*");
			for (var dir in Directory.Enumerate(dirSearchStr, .Directories))
			{
				var subPath = scope String();
				dir.GetFilePath(subPath);
				SearchPath(subPath);
			}
		}

		public void Run()
		{
			let dir = scope String();
			Path.GetDirectoryPath(mFilePath, dir);
			IDEUtils.FixFilePath(dir);
			if (!Environment.IsFileSystemCaseSensitive)
				dir.ToUpper();

			// Check for files relative to manually-specified alias paths
			for (var (origDir, localDir) in mRemapMap)
			{
				if ((mFilePath.Length > origDir.Length) && (dir.StartsWith(origDir)) && (mFilePath[origDir.Length] == Path.DirectorySeparatorChar))
				{
					let localPath = scope String();
					localPath.Append(localDir);
					localPath.Append(mFilePath, origDir.Length);
					if (File.Exists(localPath))
						CheckFile(localPath);
				}
			}

			if (mFoundPath == null)
			{
				for (let searchPath in gApp.mSettings.mDebuggerSettings.mAutoFindPaths)
				{
					if (!searchPath.Contains('@'))
						SearchPath(searchPath);
				}
			}

			mDoneEvent.Set(true);
		}
	}

	class QueuedAutoComplete
	{
		public char32 mKeyChar;
		public SourceEditWidgetContent.AutoCompleteOptions mOptions;
	}

	struct LinePointerDrawData
	{
		public Image mImage;
		public int32 mLine;
		public int32 mUpdateCnt;
		public int32 mDebuggerContinueIdx;
	}

	class CollapseRegionView
	{
		public const uint32 cStartFlag = 0x8000'0000;
		public const uint32 cMidFlag = 0x4000'0000;
		public const uint32 cEndFlag = 0x2000'0000;
		public const uint32 cIdMask = 0x0FFF'FFFF;

		public List<uint32> mCollapseIndices = new .() ~ delete _;
		public int32 mLineStart;
		public int32 mCollapseRevision;
		public int32 mTextVersionId;

		public uint32 GetCollapseValue(int param)
		{
			if (param < mLineStart)
				return 0;
			if (param - mLineStart < mCollapseIndices.Count)
				return mCollapseIndices[param - mLineStart];
			return 0;
		}
	}

	class QueuedCollapseData
	{
		public String mData = new .() ~ delete _;
		public String mBuildData ~ delete _;
		public int32 mTextVersion;
		public IdSpan mCharIdSpan ~ _.Dispose();
		public ResolveType mResolveType;
	}

	class QueuedEmitShowData
	{
		public int32 mPrevCollapseParseRevision;
		public int32 mTypeId = -1;
		public String mTypeName ~ delete _;
		public int32 mLine;
		public int32 mColumn;
	}

    public class SourceViewPanel : TextPanel
    {
        enum SourceDisplayId
        {
            Cleared,
            AutoComplete,
            SpellCheck,
            FullClassify,
			SkipResult
        }

		struct ParsedState
		{
			public IdSpan mIdSpan;
			public int32 mTextVersion = -1;

			public void Dispose() mut
			{
				mIdSpan.Dispose();
			}
		}

		public bool mAsyncAutocomplete = true;

        public SourceEditWidget mEditWidget;        
        public ProjectSource mProjectSource;
		public FileEditData mEditData;
		public FileRecovery.Entry mFileRecoveryEntry ~ delete _;
        public List<TrackedTextElementView> mTrackedTextElementViewList ~ DeleteContainerAndItems!(_);
        public List<BfPassInstance.BfError> mErrorList = new List<BfPassInstance.BfError>() ~ DeleteContainerAndItems!(_);
		public List<ResolveParams> mDeferredResolveResults = new .() ~ DeleteContainerAndItems!(_);
        public bool mTrackedTextElementViewListDirty;
        public String mFilePath ~ delete _;
		public int32 mEmitRevision = -1;
		public SourceEmbedKind mEmbedKind;
		public SourceViewPanel mEmbedParent;
		public bool mIsBinary;
		public String mAliasFilePath ~ delete _;
#if IDE_C_SUPPORT
        public ProjectSource mClangSource; // For headers, an implementing .cpp file
#endif
        public int32 mLastMRUVersion;
        public int32 mLastTextVersionId;
        public int32 mAutocompleteTextVersionId;
        public int32 mClassifiedTextVersionId;
		public int32 mLastRecoveryTextVersionId;
		public bool mLoadFailed;
		String mOldVerLoadCmd ~ delete _;
		HTTPRequest mOldVerHTTPRequest ~ delete _;
		IDEApp.ExecutionInstance mOldVerLoadExecutionInstance ~ { if (_ != null) _.mAutoDelete = true; };
		SourceFindTask mSourceFindTask ~ delete _;
		HoverResolveTask mHoverResolveTask ~ delete _;
		bool mWantsFastClassify;
        bool mWantsFullClassify; // This triggers a classify
        bool mWantsFullRefresh; // If mWantsFullClassify is set, mWantsFullRefresh makes the whole thing refresh
		bool mWantsCollapseRefresh;
		ParsedState mParsedState ~ _.Dispose(); // The current data the resolver is using from this file
		bool mRefireMouseOverAfterRefresh;
        bool mWantsBackgroundAutocomplete;
		bool mSkipFastClassify;
		QueuedAutoComplete mQueuedAutoComplete ~ delete _;
        public bool mWantsSpellCheck;
        int32 mTicksSinceTextChanged;
        int32 mErrorLookupTextIdx = -1;
		LinePointerDrawData mLinePointerDrawData;
		bool mIsDraggingLinePointer;
		Point? mMousePos;
	#if IDE_C_SUPPORT
        public String mClangHoverErrorData ~ delete mClangHoverErrorData;
#endif
		CollapseRegionView mCollapseRegionView = new .() ~ delete _;
		QueuedCollapseData mQueuedCollapseData ~ delete _;
		QueuedEmitShowData mQueuedEmitShowData ~ delete _;
		List<String> mExplicitEmitTypes = new .() ~ DeleteContainerAndItems!(_);

        public EditWidgetContent.CharData[] mProcessSpellCheckCharData ~ delete _;
        public IdSpan mProcessSpellCheckCharIdSpan ~ _.Dispose();
        //public int mBackgroundCursorIdx;
        
		String mQueuedLocation ~ delete _;
        bool mWantsClassifyAutocomplete;
		bool mWantsCurrentLocation;
        public bool mIsPerformingBackgroundClassify;
        public bool mClassifyPaused = false;        
        public bool mUseDebugKeyboard;
        //public int32 mLastFileTextVersion;
        public bool mHasChangedSinceLastCompile;
        public bool mIsSourceCode;
        public bool mIsBeefSource;
		public bool mIsClang;
#if IDE_C_SUPPORT
        public bool mClangSourceChanged;
        //public ClangCompiler mClangHelper;
        bool mDidClangSource;
#endif
        public bool mJustShown;
        public double mDesiredVertPos;
		public float mLockFlashPct;
        ResolveType mBackgroundResolveType;
        SourceViewPanel mOldVersionPanel;
		SourceViewPanel mCurrentVersionPanel;
		EditWidgetContent.LineAndColumn? mRequestedLineAndColumn;
		public SourceHash mWantHash;

		SourceViewPanel mSplitTopPanel; // This is only set if we are controlling a top panel
		PanelSplitter mSplitter;
		SourceViewPanel mSplitBottomPanel; // The primary owning panel is the bottom panel, this only set if we are the bottom panel
		
        int mHotFileIdx = -1;
        bool mIsOldCompiledVersion;
        static bool sPreviousVersionWarningShown;
        PanelHeader mPanelHeader;
        NavigationBar mNavigationBar;
        public SymbolReferenceHelper mRenameSymbolDialog;
        public int32 mBackgroundDelay = 0;
		public Monitor mMonitor = new Monitor() ~ delete _;
		bool mDidSpellCheck;
		int32 mSpellCheckJobCount;
		int32 mResolveJobCount;
		bool mWantsParserCleanup;
		bool mInPostRemoveUpdatePanels;
		bool mDeleteAfterPostRemoveUpdate;

		// For multi-view files...
		PersistentTextPosition mTrackCursorPos;
		PersistentTextPosition mTrackSelStart;
		PersistentTextPosition mTrackSelEnds;

		public override float TabWidthOffset
		{
			get
			{
				return 30;
			}
		}

		public bool NeedsPostRemoveUpdate
		{
			get
			{
				return !mDeferredResolveResults.IsEmpty || (mResolveJobCount > 0) || (mSpellCheckJobCount > 0);
			}
		}

        public override SourceEditWidget EditWidget
        {
            get
            {
                return mEditWidget;
            }
        }

        public CompilerBase ResolveCompiler
        {
            get
            {
#if IDE_C_SUPPORT
                if (mIsBeefSource)
                    return IDEApp.sApp.mBfResolveCompiler;
                return IDEApp.sApp.mResolveClang;
#else
				return IDEApp.sApp.mBfResolveCompiler;
#endif
            }
        }

        public BfCompiler BfResolveCompiler
        {
            get
            {
#if IDE_C_SUPPORT
                if (mIsBeefSource)
                    return IDEApp.sApp.mBfResolveCompiler;
                return null;
#else
				return IDEApp.sApp.mBfResolveCompiler;
#endif
            }
        }

#if IDE_C_SUPPORT
        public ClangCompiler ClangResolveCompiler
        {
            get
            {
                if (mIsClang)
                    return IDEApp.sApp.mResolveClang;
                return null;
            }
        }
#endif

        public BfSystem BfResolveSystem
        {
            get
            {
#if IDE_C_SUPPORT
                if (!mIsClang)
					return IDEApp.sApp.mBfResolveSystem;
				return null;
#else
				return IDEApp.sApp.mBfResolveSystem;
#endif
            }
        }

		public NavigationBar PrimaryNavigationBar
		{
			get
			{
				if (mSplitBottomPanel != null)
					return mSplitBottomPanel.mNavigationBar;
				return mNavigationBar;
			}
		}

		public bool IsReadOnly
		{
			get
			{
				if (gApp.mSettings.mEditorSettings.mLockEditing)
					return true;
				if ((mProjectSource != null) && (mProjectSource.mProject.mLocked))
					return true;

				if (gApp.mDebugger.mIsRunning)
				{
					switch (gApp.mSettings.mEditorSettings.mLockEditingWhenDebugging)
					{
					case .Always:
						return true;
					case .Never:
						break;
					case .WhenNotHotSwappable:
						if ((!mIsBeefSource) || (!gApp.mDebugger.mIsRunningCompiled))
						{
							return true;
						}
					}
				}

				return false;
			}
		}

		public ProjectSource FilteredProjectSource
		{
			get
			{
				if (mProjectSource == null)
					return null;
				if (mProjectSource.IsIgnored())
					return null;
				if (!gApp.IsProjectSourceEnabled(mProjectSource))
					return null;

				return mProjectSource;
			}
		}

        public this(SourceEmbedKind embedKind = .None)
        {
			mEmbedKind = embedKind;
            DebugManager debugManager = IDEApp.sApp.mDebugger;
            debugManager.mBreakpointsChangedDelegate.Add(new => BreakpointsChanged);

			if (mEmbedKind == .None)
			{
	            mNavigationBar = new NavigationBar(this);            
	            AddWidget(mNavigationBar); 
			}
			mAlwaysUpdateF = true;
        }
		public ~this()
		{
			if (mInPostRemoveUpdatePanels)
			{
				//Debug.WriteLine("Removing sourceViewPanel from mPostRemoveUpdatePanel {0} in ~this ", this);
				gApp.mPostRemoveUpdatePanels.Remove(this);
			}

            if (!mDisposed)
				Dispose();
			
			/*if (mOldVersionPanel != null)
			{
				Widget.RemoveAndDelete(mOldVersionPanel);
				mOldVersionPanel = null;
			}*/
		}

		SourceEditWidgetContent Content
		{
			get
			{
				return (SourceEditWidgetContent)mEditWidget.Content;
			}
		}

		void DoAutoComplete(char32 keyChar, SourceEditWidgetContent.AutoCompleteOptions options)
		{
			var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

			/*bool isValid = false;

			
			int cursorPos = editWidgetContent.mCursorTextPos;
			if (cursorPos > 0)
			{
				char8 c = editWidgetContent.mData.mText[cursorPos].mChar;
				if ((c.IsLetterOrDigit) || (c == '')
			}

			if (!isValid)
			{
				Debug.WriteLine("DoAutoComplete ignored");
				return;
			}*/

			if (ResolveCompiler == null)
				return;

			if (ResolveCompiler.mThreadWorkerHi.mThreadRunning)
			{
				ResolveCompiler.RequestFastFinish();

				//Debug.WriteLine("Deferred DoAutoComplete");
				DeleteAndNullify!(mQueuedAutoComplete);
				mQueuedAutoComplete = new .();
				mQueuedAutoComplete.mKeyChar = keyChar;
				mQueuedAutoComplete.mOptions = options;
				return;
			}

			//Debug.WriteLine("DoAutoComplete");

			if (editWidgetContent.mAutoComplete != null)
			{
				editWidgetContent.mAutoComplete.mInvokeOnly = options.HasFlag(.OnlyShowInvoke);
				//Debug.WriteLine("Setting invokeonly {0} {1}", editWidgetContent.mAutoComplete, editWidgetContent.mAutoComplete.mInvokeOnly);
			}

		    //Classify(options.HasFlag(.HighPriority) ? ResolveType.Autocomplete_HighPri : ResolveType.Autocomplete);

			ResolveParams resolveParams = new ResolveParams();
			if (gApp.mDbgTimeAutocomplete)
				resolveParams.mStopwatch = new .()..Start();
			if (gApp.mDbgPerfAutocomplete)
				resolveParams.mProfileInstance = Profiler.StartSampling("Autocomplete").GetValueOrDefault();
			resolveParams.mIsUserRequested = options.HasFlag(.UserRequested);
			resolveParams.mDoFuzzyAutoComplete = gApp.mSettings.mEditorSettings.mFuzzyAutoComplete;
			Classify(.Autocomplete, resolveParams);
			if (!resolveParams.mInDeferredList)
				delete resolveParams;

		    if (mIsClang)
		    {
		        // We classify afterwards so we can attempt to detect any bound functions
		        mWantsFullClassify = true;
		        mWantsClassifyAutocomplete = options.HasFlag(.UserRequested);
		    }
		}

		public void CancelResolve(ResolveType resolveType)
		{
			for (var resolveResults in mDeferredResolveResults)
			{
				if (resolveResults.mResolveType == resolveType)
				{
					resolveResults.mCancelled = true;
				}
			}
		}

		void CancelAutocomplete()
		{
			CancelResolve(.Autocomplete);
			DeleteAndNullify!(mQueuedAutoComplete);
		}

		public void CloseAutocomplete()
		{
			var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
			if (editWidgetContent.mAutoComplete != null)
				editWidgetContent.mAutoComplete.Close();
		}

        void SetupEditWidget()
        {
			if (mEditWidget.mParent == null)
            	AddWidget(mEditWidget);

            mEditWidget.mPanel = this;
            var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
			delete editWidgetContent.mOnGenerateAutocomplete;
            editWidgetContent.mOnGenerateAutocomplete = new => DoAutoComplete;
            editWidgetContent.mSourceViewPanel = this;
			
			delete editWidgetContent.mOnEscape;
            editWidgetContent.mOnEscape = new () => EscapeHandler();
			delete editWidgetContent.mOnFinishAsyncAutocomplete;
            editWidgetContent.mOnFinishAsyncAutocomplete = new () =>
	            {
					scope AutoBeefPerf("editWidgetContent.mOnFinishAsyncAutocomplete");

	                ProcessDeferredResolveResults(-1, true);
					if (mQueuedAutoComplete != null)
					{
						DoAutoComplete(mQueuedAutoComplete.mKeyChar, mQueuedAutoComplete.mOptions);
						DeleteAndNullify!(mQueuedAutoComplete);
						ProcessDeferredResolveResults(-1, true);
					}
	            };
			delete editWidgetContent.mOnCancelAsyncAutocomplete;
            editWidgetContent.mOnCancelAsyncAutocomplete = new () =>
	            {
					CancelAutocomplete();
	            };
        }

        AutoComplete GetAutoComplete()
        {
            return ((SourceEditWidgetContent)mEditWidget.Content).mAutoComplete;
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);
			if (mFilePath == null)
				return;

            data.Add("Type", "SourceViewPanel");
			String relPath = scope String();
			gApp.mWorkspace.GetWorkspaceRelativePath(mFilePath, relPath);
            data.Add("FilePath", relPath);
			if (mAliasFilePath != null)
			{
				String relAliasPath = scope .();
				gApp.mWorkspace.GetWorkspaceRelativePath(mAliasFilePath, relAliasPath);
				data.Add("AliasFilePath", relAliasPath);
			}
            data.ConditionalAdd("CursorPos", mEditWidget.Content.CursorTextPos, 0);
            data.ConditionalAdd("VertPos", mEditWidget.mVertScrollbar.mContentPos, 0);
            if (mProjectSource != null)
                data.Add("ProjectName", mProjectSource.mProject.mProjectName);
        }

        public override bool Deserialize(StructuredData data)
        {
            base.Deserialize(data);

            String relFilePath = scope String();
            data.GetString("FilePath", relFilePath);
			String absFilePath = scope String();
			gApp.mWorkspace.GetWorkspaceAbsPath(relFilePath, absFilePath);

            String projectName = scope String();
            data.GetString("ProjectName", projectName);

			var relAliasFilePath = scope String();
			data.GetString("AliasFilePath", relAliasFilePath);
			if (!relAliasFilePath.IsEmpty)
			{
				mAliasFilePath = new String();
				gApp.mWorkspace.GetWorkspaceAbsPath(relAliasFilePath, mAliasFilePath);
			}
            
            bool foundProjectSource = false;
            if (projectName != null)
            {
                var project = IDEApp.sApp.FindProjectByName(projectName);
                if (project != null)
                {
                    String relPath = scope String();
                    project.GetProjectRelPath(absFilePath, relPath);
                    var projectItem = IDEApp.sApp.FindProjectItem(project.mRootFolder, relPath);
                    if (projectItem != null)
                    {
                        if (!Show(projectItem, true))
							return false;
                        foundProjectSource = true;
                    }
                }
            }

            if (!foundProjectSource)
            {
                if (!Show(absFilePath, true))
                    return false;
            }
            int32 cursorPos = data.GetInt("CursorPos");
            mEditWidget.Content.mCursorTextPos = Math.Min(cursorPos, mEditWidget.mEditWidgetContent.mData.mTextLength);
            mDesiredVertPos = data.GetFloat("VertPos");

            return true;
        }

        public void QueueFullRefresh(bool configMayHaveChanged)
        {
#if IDE_C_SUPPORT
            if ((mIsClang) && (configMayHaveChanged))
            {
                // Force file to be recreated with potentially new Clang settings
                using (mMonitor.Enter())
                {
                    var clangCompiler = ClangResolveCompiler;
                    clangCompiler.QueueFileRemoved(mFilePath);
                    if (mClangSource != null)
                        mClangSource = null;                    
                    mDidClangSource = false;
                }
            }
#endif

			if ((configMayHaveChanged) && (mSplitTopPanel != null))
				mSplitTopPanel.QueueFullRefresh(configMayHaveChanged);

            mWantsFullClassify = true;
            mWantsFullRefresh = true;

            // We do this every time we autocomplete, so we don't want to set mDidClangSource to false here.  Why did we have that?
            //if (mIsClang)
                //mDidClangSource = false;
        }

		public void QueueCollapseRefresh()
		{
			mWantsCollapseRefresh = true;
		}

        public override bool EscapeHandler()
        {
            if (IDEApp.sApp.mSymbolReferenceHelper != null)
            {
                IDEApp.sApp.mSymbolReferenceHelper.Cancel();
				// If we press F3 and find a symbol, we want esc to close both the symbol reference and the quickfind
				if (mQuickFind == null)
                	return true;
            }

            if (base.EscapeHandler())
                return true;

			if (HasFocus(true))
			{
				if ((mSplitTopPanel != null) && (mSplitTopPanel.EscapeHandler()))
					return true;
				if ((mSplitBottomPanel != null) && (mSplitBottomPanel.EscapeHandler()))
					return true;
			}

            // Remove F4-selection from output panels
            IDEApp.sApp.mOutputPanel.EscapeHandler();
            IDEApp.sApp.mFindResultsPanel.EscapeHandler();

            return false;
        }
		
		public bool IsControllingEditData()
		{	
			// We are controlling if we have focus or, if none have focus, if we're the most recently added
			var users = Content.mData.mUsers;
			for (var user in users)
			{
				if (user.mEditWidget.mHasFocus)
					return mEditWidget == user.mEditWidget;
			}
			return mEditWidget == users[users.Count - 1].mEditWidget;
		}

        public bool HasUnsavedChanges()
        {
            //return mLastFileTextVersion != mEditWidget.Content.mData.mCurTextVersionId;
			if (mFilePath == null)
				return true;
			if (mEditData == null)
				return false;
			if (mEditData.mFileDeleted)
				return true;
			return mEditData.mLastFileTextVersion != mEditWidget.Content.mData.mCurTextVersionId;
        }

		public bool IsLastViewOfData()
		{
			int32 expectCount = 1;
			if (mSplitTopPanel != null)
				expectCount = 2;
			return Content.Data.mUsers.Count == expectCount;
		}
                
        public bool HasTextChangedSinceCompile(int defLineStart, int defLineEnd, int compileInstanceIdx)
        {
			/*if ((mProjectSource != null) && (!mProjectSource.mHasChangedSinceLastSuccessfulCompile))
				return false;*/

            if ((compileInstanceIdx == -1) && (!mHasChangedSinceLastCompile))
                return false;

			let projectSource = FilteredProjectSource;
            if (projectSource == null)
                return false;
            
            int32 char8IdStart;
            int32 char8IdEnd;
            IdSpan char8IdData = IDEApp.sApp.mWorkspace.GetProjectSourceSelectionCharIds(projectSource, compileInstanceIdx, defLineStart, defLineEnd, out char8IdStart, out char8IdEnd);
            if (char8IdData.IsEmpty)
                return false;

			bool doDebug = false;
			if (doDebug)
			{
				char8IdData.Dump();
				mEditWidget.mEditWidgetContent.mData.mTextIdData.Dump();
			}


            return !char8IdData.IsRangeEqual(mEditWidget.mEditWidgetContent.mData.mTextIdData.GetPrepared(), char8IdStart, char8IdEnd);
        }

		public void CheckSavedContents()
		{
			if (((mEditData != null)) && (mEditData.mLastFileTextVersion == mEditWidget.Content.mData.mCurTextVersionId) && (mEditData.mRecoveryHash.IsZero) &&
				(gApp.mSettings.mEditorSettings.mEnableFileRecovery != .No) 
#if !CLI				
				&& (!gApp.mFileRecovery.mDisabled)
#endif				
				)
			{
				String text = scope .();
				mEditWidget.GetText(text);
				mEditData.mRecoveryHash = MD5.Hash(.((uint8*)text.Ptr, text.Length));
			}
		}

        public void FileSaved()
        {
			ClearLoadFailed();
#if IDE_C_SUPPORT
            mClangSourceChanged = false;
#endif
            //mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
            if (mEditData != null)
			{
                mEditData.mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
				mEditData.mHadRefusedFileChange = false;
				mEditData.mFileDeleted = false;
			
				var editText = scope String();
				mEditWidget.GetText(editText);
				using (gApp.mMonitor.Enter())
				{
					mEditData.SetSavedData(editText, mEditWidget.mEditWidgetContent.mData.mTextIdData.GetPrepared());
				}
            }

			gApp.FileChanged(mFilePath);
			DeleteAndNullify!(mFileRecoveryEntry);
			mLastRecoveryTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;
			mEditData.mRecoveryHash = default;
        }

        void ClassifyThreadDone()
        {
            /*if (mProcessingPassInstance != null)
            {
                //Debug.WriteLine("Pass Instance {0} Done. Dispose? {1}", mProcessingPassInstance.mId, mWidgetWindow == null);

                // If we've closed the window by the time our pass instance is completed then just drop the results
                if (mWidgetWindow == null)
                {
                    delete mProcessingPassInstance;
                    mProcessingPassInstance = null;
                }
            }*/
            mIsPerformingBackgroundClassify = false;
			mResolveJobCount--;
        }

		void BackgroundResolve(ThreadStart threadStart)
		{
			
		}

        public bool Classify(ResolveType resolveType, ResolveParams resolveParams = null)
        {
            // Don't allow other classify calls interrupt a symbol rename
            if ((IDEApp.sApp.mSymbolReferenceHelper != null) && (IDEApp.sApp.mSymbolReferenceHelper.HasStarted) && (IDEApp.sApp.mSymbolReferenceHelper.mKind == SymbolReferenceHelper.Kind.Rename))
                return false;

			if (mEmbedKind != .None)
			{
				if ((mEmbedParent != null) &&
					((resolveType == .GoToDefinition) || (resolveType == .GetCurrentLocation)))
				{
					mEmbedParent.Classify(resolveType, resolveParams);
				}

				return true;
			}

			//Debug.WriteLine("Classify({0})", resolveType);

            if (!mIsSourceCode)
                return true;

			if (gApp.mDeterministic)
			{
				//return false;

				if (resolveType == .GetCurrentLocation)
					return false;
				if (resolveType == .GetSymbolInfo)
					return true;

				while ((BfResolveCompiler.mResolveAllWait != 0) ||
					(BfResolveCompiler.mThreadYieldCount != 0))
				{
					BfResolveCompiler.Update();
				}

				BfResolveCompiler.WaitForBackground();
			}

			if (resolveParams != null)
				resolveParams.mResolveType = resolveType;

			/*if (mWidgetWindow.IsKeyDown(.Control))
			{
				NOP!();
			}*/
			//Debug.WriteLine("Classify {0} {1}", this, resolveType);

			scope AutoBeefPerf("SourceViewPanel.Classify");

            /*if ((mIsClang) && (!mDidClangSource))
            {
                mDidClangSource = true;
                var clangCompiler = ClangResolveCompiler;
                if (mProjectSource != null)
                {
                    clangCompiler.QueueProjectSource(mProjectSource);
                    // Return because now we need to wait for this to finish
                    return false;
                }
            }*/

			var useResolveType = resolveType;
            var compiler = ResolveCompiler;
            var bfCompiler = BfResolveCompiler;
            BfSystem bfSystem = null;
            if (bfCompiler != null)
                bfSystem = IDEApp.sApp.mBfResolveSystem;
            /*if ((!mIsBeefSource) || (!bfSystem.HasParser(mFilePath)))
            {
                //DoFastClassify();
                //return;
            }*/

			if (compiler.IsPerformingBackgroundOperation())
			{
				compiler.RequestFastFinish();
			}

			if (bfSystem == null)
				return false;

            if (bfSystem != null)
                bfSystem.PerfZoneStart("Classify");

			let projectSource = FilteredProjectSource;

            //Debug.WriteLine("Classify {0}", doAutocomplete);
            //bfSystem.PerfZoneStart("CancelBackground");                            

            bool hasValidProjectSource = false;
            if (projectSource != null)
            {
                if (mIsBeefSource)
                {
                    var bfProject = bfSystem.GetBfProject(projectSource.mProject);
                    if (!bfProject.mDisabled)
                        hasValidProjectSource = true;
                }
                else
                    hasValidProjectSource = true;
            }

            mClassifyPaused = false;

            if (mIsClang)
            {
				if (resolveType == .Autocomplete)
					return true; // Don't do anything

                // Clang always just does a 'fast classify'
                DoFastClassify();
            }

            if (useResolveType == ResolveType.Autocomplete)
                mAutocompleteTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;

            bool wasHighPri = false;
            if (useResolveType == ResolveType.Autocomplete_HighPri)
            {
                wasHighPri = true;
                if (mIsClang) 
                {
                    compiler.CancelBackground();
					//delete mQueuedAutocompleteInfo;
                    //mQueuedAutocompleteInfo = null;
                    mWantsBackgroundAutocomplete = false;
                }
                useResolveType = ResolveType.Autocomplete;                
            }            
           
            bool doBackground = (useResolveType == .Classify) || (useResolveType == .ClassifyFullRefresh);
			if (mAsyncAutocomplete)
			{
				if ((useResolveType == .Autocomplete) || (useResolveType == .GetCurrentLocation) || (useResolveType == .GetSymbolInfo) ||
					(useResolveType == .GetResultString) || (useResolveType == .GoToDefinition))
					doBackground = true;
			}

			// If there's a long-running const eval then cancel that first
			if (!mDeferredResolveResults.IsEmpty)
			{
				for (var result in mDeferredResolveResults)
				{
					if (result.mResolveType == .ClassifyFullRefresh)
					{
						bfCompiler.RequestCancelBackground();
					}
				}
			}

            if (mIsClang)
            {
#if !IDE_C_SUPPORT
				return true;
#else
				if (useResolveType == ResolveType.GetCurrentLocation)
					doBackground = (useResolveType == ResolveType.GetCurrentLocation);

                if (useResolveType == ResolveType.Autocomplete)
                {
                    mWantsClassifyAutocomplete = GetAutoComplete() != null;
                    var autocomplete = GetAutoComplete();
                    if (autocomplete != null)
                        autocomplete.UpdateAsyncInfo();

                    if ((compiler.IsPerformingBackgroundOperation()) || (mQueuedAutocompleteInfo != null))
                    {
                        if ((mBackgroundResolveType == ResolveType.Autocomplete) && (!mIgnorePendingAsyncAutocomplete))
                        {
                            // We already have a relevant pending autocomplete
                            return true;
                        }

                        Debug.Assert(!wasHighPri);
                        mWantsBackgroundAutocomplete = true;
                        return true;
                    }

                    mIgnorePendingAsyncAutocomplete = false;
                    doBackground = true;
                }
                else if (useResolveType == ResolveType.GetNavigationData)
				{
#if IDE_C_SUPPORT
                    DoClangClassify(useResolveType, resolveParams);
					doBackground = false;
#else
					return false;
#endif
				}
#endif
            }

			let ewc = (SourceEditWidgetContent)mEditWidget.mEditWidgetContent;

			void FindEmbeds(ResolveParams resolveParams)
			{
				if (gApp.mSettings.mEditorSettings.mEmitCompiler != .Resolve)
					return;

				HashSet<FileEditData> foundEditData = scope .();
				Dictionary<String, String> remappedTypeNames = scope .();

				for (var embed in ewc.mEmbeds.Values)
				{
					if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
					{
						if (emitEmbed.mView != null)
						{
							var embedSourceViewPanel = emitEmbed.mView.mSourceViewPanel;
							bool embedHasFocus = embedSourceViewPanel.mEditWidget.mHasFocus;

							if (!embedHasFocus)
							{
								if ((useResolveType != .Classify) && (useResolveType != .ClassifyFullRefresh))
									continue;
							}

							if (foundEditData.Add(embedSourceViewPanel.mEditData))
							{
								ResolveParams.Embed embedSource = new .();

								String useTypeName = emitEmbed.mTypeName;
								if (!mExplicitEmitTypes.IsEmpty)
								{
									if (remappedTypeNames.TryAdd(useTypeName, var keyPtr, var valuePtr))
									{
										*valuePtr = useTypeName;
										for (var explicitTypeName in mExplicitEmitTypes)
										{
											if (IDEUtils.GenericEquals(useTypeName, explicitTypeName))
											{
												*valuePtr = explicitTypeName;
												break;
											}
										}
									}
									useTypeName = *valuePtr;
								}

								embedSource.mTypeName = new .(useTypeName);
								if (embedHasFocus)
									embedSource.mCursorIdx = (.)embedSourceViewPanel.mEditWidget.mEditWidgetContent.CursorTextPos;
								else
									embedSource.mCursorIdx = -1;
								resolveParams.mEmitEmbeds.Add(embedSource);

								//embedSource.mParser = bfSystem.FindParser(embedSourceViewPanel.mProjectSource);
							}
						}
					}
				}
			}

            if (doBackground)
            {
				ProcessDeferredResolveResults(0);

				var resolveParams;
				if (resolveParams == null)
				{
					resolveParams = new ResolveParams();
				}

				FindEmbeds(resolveParams);

				resolveParams.mResolveType = resolveType;
				resolveParams.mWaitEvent = new WaitEvent();
				resolveParams.mInDeferredList = true;
				mDeferredResolveResults.Add(resolveParams);

				var autoComplete = GetAutoComplete();
				if ((autoComplete != null) && (autoComplete.mIsDocumentationPass))
				{
					Debug.Assert(ewc.mAutoComplete != null);
					let selectedEntry = ewc.mAutoComplete.mAutoCompleteListWidget.mEntryList[ewc.mAutoComplete.mAutoCompleteListWidget.mSelectIdx];
					resolveParams.mDocumentationName = new String(selectedEntry.mEntryDisplay);
				}
				resolveParams.mTextVersion = Content.mData.mCurTextVersionId;

				compiler.CheckThreadDone(); // Process any pending thread done callbacks

				bool isHi = (resolveType != .ClassifyFullRefresh) && (resolveType != .Classify);
				if (isHi)
				{
					Debug.Assert(!bfCompiler.mThreadWorkerHi.mThreadRunning);
				}
				else
					Debug.Assert(!bfCompiler.mThreadWorker.mThreadRunning);

                if (bfSystem != null)
                    bfSystem.PerfZoneStart("DoBackground");
				//ProcessResolveData();

				bool hasFocus = mEditWidget.mHasFocus;

				//Debug.Assert(mProcessResolveCharData == null);
                DuplicateEditState(out resolveParams.mCharData, out resolveParams.mCharIdSpan);
				//Debug.WriteLine("Edit State: {0}", mProcessResolveCharData);

				if (hasFocus)
				{
	                if ((useResolveType == .Autocomplete) || (useResolveType == .GetSymbolInfo) || (mIsClang))
					{
						resolveParams.mOverrideCursorPos = (.)mEditWidget.Content.CursorTextPos;
						/*if (useResolveType == .Autocomplete)
							resolveParams.mOverrideCursorPos--;*/
					}
				}
                    
                //Debug.Assert(mCurParser == null);
                
                if ((mIsBeefSource) && (hasValidProjectSource) && (!isHi))
                    resolveParams.mParser = bfSystem.FindParser(projectSource);
                //if (mCurParser != null)
                {
					if (gApp.mWorkspace.mProjectLoadState != .Loaded)
					{
						resolveParams.mCancelled = true;
						resolveParams.mWaitEvent.Set(true);
						return true;
					}

					if (!isHi)
						Debug.Assert(!mIsPerformingBackgroundClassify);

                    mBackgroundResolveType = useResolveType;
                    mIsPerformingBackgroundClassify = true;
					mResolveJobCount++;
                    if (useResolveType == .Autocomplete)
                        compiler.DoBackgroundHi(new () => { DoClassify(.Autocomplete, resolveParams, true); }, new => ClassifyThreadDone);
						//BackgroundResolve(new () => { DoClassify(.Autocomplete, resolveParams); });
                    else if (useResolveType == .ClassifyFullRefresh)
					{
						if ((mProjectSource?.mLoadFailed == true) && (!mLoadFailed))
						{
							mProjectSource.mLoadFailed = false;
						}

						// To avoid "flashing" on proper colorization vs FastClassify, we wait a bit for the proper classifying to finish
						//  on initial show
						int maxWait = (mUpdateCnt <= 1) ? 50 : 0;
                        compiler.DoBackground(new () => { DoClassify(.ClassifyFullRefresh, resolveParams, false); },
							new () =>
							{
								ClassifyThreadDone();
							}, maxWait);
					}
                    else if (useResolveType == .GetCurrentLocation)
						compiler.DoBackgroundHi(new () => { DoClassify(.GetCurrentLocation, resolveParams, true); }, new => ClassifyThreadDone);
					else if (useResolveType == .GetSymbolInfo)
						compiler.DoBackgroundHi(new () => { DoClassify(.GetSymbolInfo, resolveParams, true); }, new => ClassifyThreadDone);
					else 
                        compiler.DoBackgroundHi(new () => { DoFullClassify(resolveParams); }, new => ClassifyThreadDone);
                }
                /*else
                {
                    // Not part of project
                    DoFastClassify();
                }*/

                if (bfSystem != null)
                    bfSystem.PerfZoneEnd();
            }
            else if ((mIsBeefSource) && (hasValidProjectSource))
            {
				var resolveParams;
				if (resolveParams == null)
					resolveParams = scope:: ResolveParams();

				FindEmbeds(resolveParams);

				/*if (useResolveType == ResolveType.Autocomplete)
				{
					Profiler.StartSampling();
					DoClassify(useResolveType, resolveParams);
					Profiler.StopSampling();
				}
				else*/
                	DoClassify(useResolveType, resolveParams, true);
				MarkDirty();
            }            

            if (bfSystem != null)
                bfSystem.PerfZoneEnd();
            return true;
        }

		public void DoParserCleanup()
		{
			var bfSystem = IDEApp.sApp.mBfResolveSystem;
			bfSystem.Lock(0);
			//bfSystem.RemoveDeletedParsers();
			bfSystem.RemoveOldData();
			bfSystem.Unlock();
		}

		public void DoRefreshCollapse(BfParser parser, int32 textVersion, IdSpan charIdSpan)
		{
			var bfCompiler = BfResolveCompiler;
			var bfSystem = BfResolveSystem;

			String explicitEmitTypeNames = scope .();
			for (var explicitType in mExplicitEmitTypes)
			{
				explicitEmitTypeNames.Append(explicitType);
				explicitEmitTypeNames.Append("\n");
			}

			var resolvePassData = parser.CreateResolvePassData(.None);
			defer delete resolvePassData;

			bfSystem.Lock(0);

			var collapseData = bfCompiler.GetCollapseRegions(parser, resolvePassData, explicitEmitTypeNames, .. scope .());

			String buildCollapseData = null;
			if ((gApp.mSettings.mEditorSettings.mEmitCompiler == .Build) && (!gApp.mBfBuildCompiler.IsPerformingBackgroundOperation()))
			{
				gApp.mBfBuildSystem.Lock(0);
				var buildParser = gApp.mBfBuildSystem.GetParser(mProjectSource);
				if (buildParser != null)
				{
					var buildResolvePassData = buildParser.CreateResolvePassData(.None);
					defer delete buildResolvePassData;
					buildCollapseData = gApp.mBfBuildCompiler.GetCollapseRegions(buildParser, buildResolvePassData, explicitEmitTypeNames, .. scope:: .());
				}
				else
				{
					buildCollapseData = "";
				}
				gApp.mBfBuildSystem.Unlock();
			}

			using (mMonitor.Enter())
			{
				DeleteAndNullify!(mQueuedCollapseData);
				mQueuedCollapseData = new .();
				mQueuedCollapseData.mData.Set(collapseData);
				if (buildCollapseData != null)
					mQueuedCollapseData.mBuildData = new String(buildCollapseData);
				mQueuedCollapseData.mTextVersion = textVersion;
				mQueuedCollapseData.mCharIdSpan = charIdSpan.Duplicate();

				//Debug.WriteLine($"DoRefreshCollapse finished TextVersion:{textVersion} IdSpan:{charIdSpan:D}");
			}
			bfSystem.Unlock();
		}

        public void DoFullClassify(ResolveParams resolveParams)
        {
            var bfCompiler = BfResolveCompiler;
            //var bfSystem = IDEApp.sApp.mBfResolveSystem;
            //bfCompiler.StartTiming();
			ResolveType resolveType = ResolveType.Classify;
			if (resolveParams != null)
				resolveType = resolveParams.mResolveType;
            DoClassify(resolveType, resolveParams);
            //bfCompiler.StopTiming();
            if (bfCompiler != null)
                bfCompiler.QueueDeferredResolveAll();
        }

        //[CallingConvention(.Stdcall), CLink]
        //static extern char8* BfDiff_DiffText(char8* text1, char8* text2);

        public void DoFastClassify()
        {
            if ((!mIsSourceCode) || (mSkipFastClassify))
                return;

			//Debug.WriteLine("DoFastClassify");

			scope AutoBeefPerf("SourceViewPanel.DoFastClassify");

			let projectSource = FilteredProjectSource;

            var bfSystem = IDEApp.sApp.mBfResolveSystem;
			if (bfSystem == null)
				return;
            //var compiler = ResolveCompiler;

            var charData = mEditWidget.Content.mData.mText;
            int charLen = Math.Min(charData.Count, mEditWidget.Content.mData.mTextLength);

            char8[] chars = new char8[charLen];
			defer delete chars;
            for (int32 i = 0; i < charLen; i++)
                chars[i] = (char8)charData[i].mChar;

            String text = scope String();
            text.Append(chars, 0, chars.Count);
            
            BfProject bfProject = null;
            if ((projectSource != null) && (mIsBeefSource))
            {
                bfProject = bfSystem.GetBfProject(projectSource.mProject);
            }
            var parser = bfSystem.CreateEmptyParser(bfProject);
			defer delete parser;
            var resolvePassData = parser.CreateResolvePassData();
			defer delete resolvePassData;
            var passInstance = bfSystem.CreatePassInstance("DoFastClassify");
			defer delete passInstance;
            parser.SetSource(text, mFilePath, -1);
            parser.SetIsClassifying();
            parser.Parse(passInstance, !mIsBeefSource);
            if (mIsBeefSource)
			{
				parser.SetEmbedKind(mEmbedKind);
                parser.Reduce(passInstance);
			}
            parser.ClassifySource(charData, !mIsBeefSource);
			mWantsParserCleanup = true;
        }

		public void DoFastClassify(int start, int length)
		{   
		    if (!mIsSourceCode)
		        return;
		    
		    var bfSystem = IDEApp.sApp.mBfResolveSystem;
			if (bfSystem == null)
				return;
		    //var compiler = ResolveCompiler;

		    //var char8Data =  mEditWidget.Content.mData.mText;

			let projectSource = FilteredProjectSource;

			int32 char8Len = mEditWidget.Content.mData.mTextLength;
			var char8Data = new EditWidgetContent.CharData[char8Len];
			defer delete char8Data;
			var curCharData = mEditWidget.Content.mData.mText;

		    char8[] chars = new char8[char8Len];
			defer delete chars;
		    for (int32 i = 0; i < char8Len; i++)
			{
				char8Data[i] = curCharData[i];
				chars[i] = (char8)char8Data[i].mChar;
			}

		    String text = scope String()..Append(StringView(chars, 0, chars.Count));
		    
		    BfProject bfProject = null;
		    if ((projectSource != null) && (mIsBeefSource))
		    {
		        bfProject = bfSystem.GetBfProject(projectSource.mProject);
		    }
		    var parser = bfSystem.CreateEmptyParser(bfProject);
			defer delete parser;
		    var resolvePassData = parser.CreateResolvePassData();
			defer delete resolvePassData;
		    var passInstance = bfSystem.CreatePassInstance("DoFastClassify");
			defer delete passInstance;
		    parser.SetSource(text, mFilePath, -1);
		    parser.SetIsClassifying();
		    parser.Parse(passInstance, !mIsBeefSource);
		    if (mIsBeefSource)
		        parser.Reduce(passInstance);
		    parser.ClassifySource(char8Data, !mIsBeefSource);
			mWantsParserCleanup = true;

			for (int i = start; i < start + length; i++)
			{
				curCharData[i] = char8Data[i];
			}
		}

        void HandleAutocompleteInfo(ResolveType resolveType, String autocompleteInfo, bool clearList, bool changedAfterInfo)
        {
            var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

			if (mWidgetWindow == null)
				return;

            bool wantOpen = (autocompleteInfo.Length > 0);
            if ((editWidgetContent.mAutoComplete != null) && (editWidgetContent.mAutoComplete.mIsAsync) &&
                (editWidgetContent.mAutoComplete.mInvokeWidget != null))
            {
                // Leave the existing invoke widget open
                wantOpen = true;
            }

            if (wantOpen)
            {
                if (editWidgetContent.mAutoComplete == null)
                {
                    editWidgetContent.mAutoComplete = new AutoComplete(mEditWidget);
                    //if (mIsClang)
                        //editWidgetContent.mAutoComplete.mIsAsync = true;
                    editWidgetContent.mAutoComplete.mOnAutoCompleteInserted.Add(new () => { Classify(resolveType); });
                    editWidgetContent.mAutoComplete.mOnClosed.Add(new () =>
                        {
                            //Debug.WriteLine("Autocomplete Closed");
                            if (editWidgetContent.mAutoComplete != null)
                            {
                                editWidgetContent.mAutoComplete = null;
                            }                            
                            // Any pending autocomplete is no longer valid
                            CancelAutocomplete();
                        });
                }
				if (editWidgetContent.mAutoComplete.mIsDocumentationPass)
					editWidgetContent.mAutoComplete.UpdateInfo(autocompleteInfo);
				else
                	editWidgetContent.mAutoComplete.SetInfo(autocompleteInfo, clearList, 0, changedAfterInfo);
            }
            else if ((editWidgetContent.mAutoComplete != null) && (!editWidgetContent.mAutoComplete.mIsDocumentationPass))
                editWidgetContent.mAutoComplete.Close();
        }

#if IDE_C_SUPPORT
        public void DoClangClassify(ResolveType resolveType, ResolveParams resolveParams = null)
        {
            var buildClang = IDEApp.sApp.mDepClang;
            var resolveClang = (ClangCompiler)ResolveCompiler;
            
			bool isBackground = (resolveType == ResolveType.Classify) || (resolveType == ResolveType.ClassifyFullRefresh) || (resolveType == ResolveType.Autocomplete) ||
                (resolveType == ResolveType.Autocomplete_HighPri) || (resolveType == ResolveType.GetCurrentLocation);

			if (!isBackground)
			{
				resolveClang.CancelBackground();
			}

			if (resolveType == ResolveType.GetCurrentLocation)
			{
				NOP!();
			}

			if (isBackground)
				Debug.Assert(mIsPerformingBackgroundClassify);

            var char8Data = (!isBackground) ? mEditWidget.Content.mData.mText : mProcessResolveCharData;
			Debug.Assert(char8Data != null);
            int char8Len = 0;
            if (char8Data != null)
                char8Len = (!isBackground) ? mEditWidget.Content.mData.mTextLength : mProcessResolveCharData.Length;
            mBackgroundCursorIdx = (int32)Math.Min(mBackgroundCursorIdx, char8Len);

            bool didClangSource = false;
        	using (mMonitor.Enter())
            {
                didClangSource = mDidClangSource;
                mDidClangSource = true;
            }

            //bool needsNewClangSource = IDEUtils.IsHeaderFile(mFilePath) && (mClangSource == null);

            //if ((!didClangSource) || (needsNewClangSource))
            if (!didClangSource)
            {
                bool handled = false;

                //if (needsNewClangSource)
                if (IDEUtils.IsHeaderFile(mFilePath))
                {
                    // Wait for buildClang to stop
                    buildClang.CancelBackground();
                    // Process the buildClang commands in this thread (if needed)
                    buildClang.ProcessQueue();

                    ProjectSource projectSource = null;

                    String findStr = scope String("/");
                    Path.GetFileNameWithoutExtension(mFilePath, findStr);
                    findStr.Append(".");

                    String findFilePath = scope String(mFilePath);
                    if (!Utils.IsFileSystemCaseSensitive)
                        findFilePath.ToUpper;

                    //foreach (var checkProjectSource in resolveClang.mSourceMRUList)
                    for (int mruIdx = resolveClang.mSourceMRUList.Count - 1; mruIdx >= 0; mruIdx--)
                    {
                        var checkProjectSource = resolveClang.mSourceMRUList[mruIdx];
                        ClangCompiler.FileEntry fileEntry = null;
                        buildClang.mProjectFileSet.TryGetValue(checkProjectSource, out fileEntry);
                        if (fileEntry != null)
                        {
                            if (fileEntry.mCDepFileRefs.Contains(findFilePath))
                            {
                                projectSource = checkProjectSource;
                                break;
                            }
                        }
                    }

                    // First try to find a file with the same base name, then try to
                    for (int32 pass = 0; pass < 2; pass++)
                    {
                        if (projectSource != null)
                            break;                        

                        for (var projectPair in buildClang.mProjectFileSet)
                        {
                            var checkProjectSource = projectPair.Key;
                            var fileData = projectPair.Value;
                            if ((fileData.mCDepFileRefs != null) &&
                                (((pass == 1) || (checkProjectSource.mPath.IndexOf(findStr, true) != -1))))
                            {
                                if (fileData.mCDepFileRefs.Contains(findFilePath))
                                    projectSource = checkProjectSource;
                            }
                            //projectPair.Value.mCDepFileRefs.Contains();
                        }
                    }

                    String headerPrefix = scope String();
                    if (projectSource != null)
                    {
                        mClangSource = projectSource;
                        IdSpan idSpan;
                        String fileContent = scope String();
                        IDEApp.sApp.FindProjectSourceContent(mClangSource, out idSpan, true, fileContent);
						idSpan.Dispose();

                        String clangArgs = scope String();
                        resolveClang.GetClangArgs(mClangSource, clangArgs);
						String fullImportPath = scope String();
						mClangSource.GetFullImportPath(fullImportPath);
                        int32 includePos = resolveClang.CDepGetIncludePosition(fullImportPath, fileContent, mFilePath, clangArgs);                        
                        if (includePos != -1)                        
                            fileContent.Substring(0, includePos, headerPrefix);
                        resolveClang.AddTranslationUnit(mFilePath, headerPrefix, clangArgs);
                    }
                    else
                    {
                        resolveClang.AddTranslationUnit(mFilePath, "", "");
                    }

                    handled = true;
                }

                if ((!handled) && (!didClangSource))
                {
					Debug.Assert(char8Data != null);
                    if (mProjectSource != null)
                    {
                        resolveClang.AddTranslationUnit(mProjectSource, null, char8Data, char8Len);
                    }
                }
            }

            if (resolveType == ResolveType.GetNavigationData)
            {
                resolveClang.CancelBackground();
                resolveParams.mNavigationData = new String();
                resolveClang.GetNavigationData(mFilePath, resolveParams.mNavigationData);
                return;
            }

			if (resolveType == ResolveType.GetCurrentLocation)
			{
				String result = new String();
				resolveClang.GetCurrentLocation(mFilePath, result, mBackgroundCursorIdx);
				delete mQueuedLocation;
				mQueuedLocation = result;
				return;
			}

            //string fileName;
            //string headerFileName = null;            
            if (resolveType == ResolveType.Autocomplete)
            {                
				Debug.Assert(mQueuedAutocompleteInfo == null);
                mQueuedAutocompleteInfo = new String();
                resolveClang.Autocomplete(mFilePath, char8Data, char8Len, mBackgroundCursorIdx, mQueuedAutocompleteInfo);
                if ((mBackgroundCursorIdx != -1) && (mQueuedAutocompleteInfo == null))
                    mQueuedAutocompleteInfo = "";
                if (mIgnorePendingAsyncAutocomplete)
				{
					delete mQueuedAutocompleteInfo;
                    mQueuedAutocompleteInfo = null;
				}
            }
            else
            {
                bool ignoreErrors = (mProjectSource == null) && (mClangSource == null);
                /*if (IDEUtils.IsHeaderFile(mFilePath))
                    ignoreErrors = mClangSource == null;
                else
                    ignoreErrors = mProjectSource == null;*/                

                String results = scope String();
                resolveClang.Classify(mFilePath, char8Data, char8Len, mBackgroundCursorIdx, mErrorLookupTextIdx, ignoreErrors, results);
                if (results != null)
                {
                    String autocompleteResult = scope String();					
                    for (var resultLine in String.StackSplit!(results, '\n'))
                    {
						var resultLineSplit = scope List<StringView>();
                        resultLine.Split(resultLineSplit, scope char8[] {'\t'}, 2);
                        if (scope String(resultLineSplit[0]) == "diag")
                        {
							delete mClangHoverErrorData;
                            mClangHoverErrorData = new String(resultLineSplit[1]);
                        }
                        else if (resultLine != "")
                        {							
                            autocompleteResult.Append(resultLine, "\n");
                        }
                    }
					if ((mWantsClassifyAutocomplete) && (autocompleteResult.Length > 0))
					{
						Debug.Assert(mQueuedClassifyInfo == null);						
					    mQueuedClassifyInfo = new String();
                        autocompleteResult.MoveTo(mQueuedClassifyInfo);
					}
                }
                
                mErrorLookupTextIdx = -1;                
            }            

            var bfSystem = IDEApp.sApp.mBfResolveSystem;
            bfSystem.RemoveDeletedParsers();

			if (isBackground)
				Debug.Assert(mIsPerformingBackgroundClassify);
        }
#endif

		void HandleResolveResult(ResolveType resolveType, String autocompleteInfo, ResolveParams resolveParams)
		{
			if (resolveType == ResolveType.GetSymbolInfo)
			{
				if (!resolveParams.mCancelled)
			    	gApp.mSymbolReferenceHelper?.SetSymbolInfo(autocompleteInfo);
			}
			else if (resolveType == ResolveType.GoToDefinition)
			{
				if (!resolveParams.mCancelled)
					gApp.mSymbolReferenceHelper?.SetSymbolInfo(autocompleteInfo);
			    /*var autocompleteLines = String.StackSplit!(autocompleteInfo, '\n');
			    for (var autocompleteLine in autocompleteLines)
			    {
			        var lineData = String.StackSplit!(autocompleteLine, '\t');
			        if (scope String(lineData[0]) == "defLoc")
			        {
			            resolveParams.mOutLine = int32.Parse(lineData[2]);
			            resolveParams.mOutLineChar = int32.Parse(lineData[3]);
			            resolveParams.mOutFileName = new String(lineData[1]);
			        }
			    }*/
			}
			else if (resolveType == ResolveType.GetNavigationData)
			{
			    resolveParams.mNavigationData = new String(autocompleteInfo);
			}
			else if (resolveType == ResolveType.GetResultString)
			{
				if ((mHoverResolveTask != null) && (mHoverResolveTask.mCursorPos == resolveParams.mOverrideCursorPos))
				{
					mHoverResolveTask.mResult = new String(autocompleteInfo ?? "");
				}

				resolveParams.mResultString = new String(autocompleteInfo ?? "");
			}
			else if (resolveType == ResolveType.GetCurrentLocation)
			{
				PrimaryNavigationBar.SetLocation(autocompleteInfo);
			}
			else if ((resolveType == .Autocomplete) || (resolveType == .GetFixits))
			{
				if ((resolveParams != null) && (resolveType == .GetFixits))
				{
					resolveParams.mNavigationData = new String(autocompleteInfo);
				}
				else if ((resolveParams == null) || (!resolveParams.mCancelled))
				{
					bool changedAfterInfo = (resolveParams != null) && (resolveParams.mTextVersion != Content.mData.mCurTextVersionId);

					var autoComplete = GetAutoComplete();
					if ((autoComplete != null) && (resolveParams != null))
						autoComplete.mIsDocumentationPass = resolveParams.mDocumentationName != null;
				    HandleAutocompleteInfo(resolveType, autocompleteInfo, true, changedAfterInfo);
					autoComplete = GetAutoComplete();
					if (autoComplete != null)
					{
						autoComplete.mIsDocumentationPass = false;
						if (resolveParams != null)
							autoComplete.mIsUserRequested = resolveParams.mIsUserRequested;
						if (resolveType == .GetFixits)
							autoComplete.mIsUserRequested = true;
					}
				}
			}
		}

        // fullRefresh means we rebuild types even if the hashes haven't changed - this is required for
        //  a classifier pass on a newly-opened file
        public void DoClassify(ResolveType resolveType, ResolveParams resolveParams = null, bool isInterrupt = false)
        {
			scope AutoBeefPerf("SourceViewPanel.DoClassify");

			if (gApp.mDbgDelayedAutocomplete)
				Thread.Sleep(250);

			if ((resolveType == .Classify) || (resolveType == .ClassifyFullRefresh))
				gApp.mErrorsPanel.SetNeedsResolveAll();

			if ((resolveType.IsClassify) && (!resolveParams.mCharIdSpan.IsEmpty))
			{
				//Debug.WriteLine($"DoClassify {resolveType} TextVersion:{resolveParams.mTextVersion} IdSpan:{resolveParams.mCharIdSpan:D}");

				mParsedState.mIdSpan.DuplicateFrom(ref resolveParams.mCharIdSpan);
				mParsedState.mTextVersion = resolveParams.mTextVersion;
			}

			/*if (resolveType == .Autocomplete)
			{
				Thread.Sleep(250);
			}*/

#if IDE_C_SUPPORT
            if (mIsClang)
            {
                DoClangClassify(resolveType, resolveParams);
                return;
            }
#endif

            var bfSystem = IDEApp.sApp.mBfResolveSystem;
			if (bfSystem == null)
				return;
            var bfCompiler = BfResolveCompiler;
            //var compiler = ResolveCompiler;
            
            bool isBackground = (resolveType == .Classify) || (resolveType == .ClassifyFullRefresh) ||
				(resolveType == .GetResultString) || (resolveType == .GoToDefinition);
            bool fullRefresh = resolveType == ResolveType.ClassifyFullRefresh;

			if (!isBackground)
				Debug.Assert(isInterrupt);

			if (mAsyncAutocomplete)
			{
				if ((resolveType == .Autocomplete) || (resolveType == .GetCurrentLocation) || (resolveType == .GetSymbolInfo))
				{
					isBackground = true;
				}
			}

			/*if ((mFilePath.Contains("mainA.bf")) && (isBackground))
			{
				Thread.Sleep(1000);
			}*/

			// If we think we're not running in the background, make sure
			Debug.Assert((Thread.CurrentThread == gApp.mMainThread) == !isBackground);

            if (isInterrupt)
                bfSystem.NotifyWillRequestLock(1);

            bool isFastClassify = false;
			BfParser parser;

			ProjectSource projectSource = FilteredProjectSource;

			int cursorPos = mEditWidget.mEditWidgetContent.CursorTextPos;

			if ((resolveParams != null) && (resolveParams.mOverrideCursorPos != -1))
				cursorPos = resolveParams.mOverrideCursorPos;

			bool emitHasCursor = false;
			if (resolveParams != null)
			{
				for (var embed in resolveParams.mEmitEmbeds)
				{
					if (embed.mCursorIdx != -1)
					{
						emitHasCursor = true;
						cursorPos = -1;
					}
				}
			}

            if ((!isBackground) && (projectSource != null))
			{
			    bfSystem.PerfZoneStart("CreateParser");
			    parser = bfSystem.CreateParser(projectSource, false);
			    bfSystem.PerfZoneEnd();
			}
			else if ((resolveParams != null) && (resolveParams.mParser != null))
			{
			    parser = bfSystem.CreateNewParserRevision(resolveParams.mParser);
			}
			else if (projectSource != null)
			{
				bfSystem.PerfZoneStart("CreateParser");
				parser = bfSystem.CreateParser(projectSource, false);
				bfSystem.PerfZoneEnd();
			}
			else
			{
			    // This only happens when we're editing source that isn't in our project
			    isFastClassify = true;
			    parser = bfSystem.CreateEmptyParser((BfProject)null);
			}

			EditWidgetContent.CharData[] charData = null;
			int charLen = 0;
			if ((resolveParams != null) && (resolveParams.mCharData != null))
			{
				charData = resolveParams.mCharData;
				charLen = resolveParams.mCharData.Count;
			}
			if (charData == null)
			{
				Debug.Assert(!isBackground);
				charData = mEditWidget.Content.mData.mText;
				charLen = mEditWidget.Content.mData.mTextLength;
			}

            /*var char8Data = (!isBackground) ? mEditWidget.Content.mData.mText : mProcessResolveCharData;
            int char8Len = Math.Min(char8Data.Count, mEditWidget.Content.mData.mTextLength);*/

            if (!isBackground)
                bfSystem.PerfZoneStart("DoClassify.CreateChars");

			
			//text.Append(chars, 0, chars.Count);

            char8* chars = new char8[charLen]* (?);
			defer delete chars;
            for (int32 i = 0; i < charLen; i++)
            {
                charData[i].mDisplayPassId = (int32)SourceDisplayId.Cleared;
                chars[i] = (char8)charData[i].mChar;
            }

			int textVersion = -1;
			if (resolveParams != null)
				textVersion = resolveParams.mTextVersion;

            if (!isBackground)
            {
                bfSystem.PerfZoneEnd();
                bfSystem.PerfZoneStart("SetSource");
            }
			parser.SetIsClassifying();
            parser.SetSource(.(chars, charLen), mFilePath, textVersion);
            if (!isBackground)
            {
                bfSystem.PerfZoneEnd();
                bfSystem.PerfZoneStart("DoClassify.DoWork");
            }

			//int cursorPos = mEditWidget.mEditWidgetContent.CursorTextPos;
			/*if (resolveType == ResolveType.Autocomplete)
				cursorPos--;*/
			/*if (resolveParams != null)
			{
				 if (resolveParams.mOverrideCursorPos != -1)
					cursorPos = resolveParams.mOverrideCursorPos;
			}*/

            if ((resolveType == ResolveType.GetNavigationData) || (resolveType == ResolveType.GetFixits))
                parser.SetAutocomplete(-1);
            else
			{
				bool setAutocomplete = ((!isBackground) && (resolveType != ResolveType.RenameSymbol));
				if ((resolveType == .Autocomplete) || (resolveType == .GetCurrentLocation) || (resolveType == .GetSymbolInfo) ||
					(resolveType == .GoToDefinition) || (resolveType == .GetResultString))
					setAutocomplete = true;
				if (setAutocomplete)
				{
					if (emitHasCursor)
						parser.SetAutocomplete(-2);
					else
                		parser.SetAutocomplete(Math.Max(emitHasCursor ? -1 : 0, cursorPos));
				}
			}
            /*else (!isFullClassify) -- do we ever need to do this?
                parser.SetCursorIdx(mEditWidget.mEditWidgetContent.CursorTextPos);*/

			String passInstanceName = scope String("DoClassify ");
			resolveType.ToString(passInstanceName);
			if (projectSource != null)
			    passInstanceName.Append(":", projectSource.mName);
			var passInstance = bfSystem.CreatePassInstance(passInstanceName);
			passInstance.SetClassifierPassId(!isBackground ? (uint8)SourceDisplayId.AutoComplete : (uint8)SourceDisplayId.FullClassify);
			if (isBackground)
			{
			    //Debug.Assert(mProcessingPassInstance == null);
			    //mProcessingPassInstance = passInstance;
				Debug.Assert(resolveParams.mPassInstance == null);
				resolveParams.mPassInstance = passInstance;
			}

			bool doFuzzyAutoComplete = resolveParams?.mDoFuzzyAutoComplete ?? false;

			var resolvePassData = parser.CreateResolvePassData(resolveType, doFuzzyAutoComplete);
			if (resolveParams != null)
			{
			    if (resolveParams.mLocalId != -1)
			        resolvePassData.SetLocalId(resolveParams.mLocalId);
			    if (resolveParams.mTypeDef != null)
			        resolvePassData.SetSymbolReferenceTypeDef(resolveParams.mTypeDef);
			    if (resolveParams.mFieldIdx != -1)
			        resolvePassData.SetSymbolReferenceFieldIdx(resolveParams.mFieldIdx);
			    if (resolveParams.mMethodIdx != -1)
			        resolvePassData.SetSymbolReferenceMethodIdx(resolveParams.mMethodIdx);
			    if (resolveParams.mPropertyIdx != -1)
			        resolvePassData.SetSymbolReferencePropertyIdx(resolveParams.mPropertyIdx);
			}

			if ((resolveParams != null) && (resolveParams.mDocumentationName != null))
				resolvePassData.SetDocumentationRequest(resolveParams.mDocumentationName);
			parser.Parse(passInstance, !mIsBeefSource);
			parser.Reduce(passInstance);

			if ((mIsBeefSource) &&
				((resolveType == .Classify) || (resolveType == .ClassifyFullRefresh)))
			{
				gApp.mErrorsPanel.ClearParserErrors(mFilePath);
				if (!isFastClassify)
					gApp.mErrorsPanel.ProcessPassInstance(passInstance, .Parse);
			}

			if (isInterrupt)
			{
			    bfSystem.PerfZoneEnd();
			    bfSystem.PerfZoneStart("Lock");
				var sw = scope Stopwatch(true);
				sw.Start();
			    bfSystem.Lock(1);
			    bfSystem.PerfZoneEnd();
			}
			else
			{
			    bfSystem.Lock(0);
			}

			if (!isFastClassify)
			    parser.BuildDefs(passInstance, resolvePassData, fullRefresh);

			// For Fixits we do want to parse the whole file but we need the cursorIdx bound still to
			//  locate the correct fixit info
			if (resolveType == ResolveType.GetFixits)
				parser.SetCursorIdx(Math.Max(0, cursorPos));

            if ((resolveType == ResolveType.ClassifyFullRefresh) && (mUseDebugKeyboard))
            {
                //Debug.WriteLine("Classify Paused...");
                mClassifyPaused = true;
                while (mClassifyPaused)
                    Thread.Sleep(20);
                //Debug.WriteLine("Classify Continuing."); 
            }

			//Debug.WriteLine($"Classify {resolveType}");

            /*if (resolveType == ResolveType.RenameLocalSymbol)
            {
                // We do want cursor info for replacing the symbol, we just didn't want it for reducing 
                //  and building defs
                parser.SetAutocomplete(Math.Max(0, mEditWidget.mEditWidgetContent.CursorTextPos - 1));
            }*/

			if ((!isFastClassify) && (bfCompiler != null))
            {
				parser.CreateClassifier(passInstance, resolvePassData, charData);

				if ((resolveParams != null) && (gApp.mSettings.mEditorSettings.mEmitCompiler == .Resolve))
				{
					for (var emitEmbedData in resolveParams.mEmitEmbeds)
					{
						resolvePassData.AddEmitEmbed(emitEmbedData.mTypeName, emitEmbedData.mCursorIdx);
					}
				}

                if (bfCompiler.ClassifySource(passInstance, resolvePassData))
				{
					if ((resolveType == .Classify) || (resolveType == .ClassifyFullRefresh))
					{
						String explicitEmitTypeNames = scope .();
						for (var explicitType in mExplicitEmitTypes)
						{
							explicitEmitTypeNames.Append(explicitType);
							explicitEmitTypeNames.Append("\n");
						}

						bool allowCollapseData = resolveType == .ClassifyFullRefresh;
						if (allowCollapseData)
						{
							var collapseData = bfCompiler.GetCollapseRegions(parser, resolvePassData, explicitEmitTypeNames, .. scope .());
							using (mMonitor.Enter())
							{
								DeleteAndNullify!(mQueuedCollapseData);
								mQueuedCollapseData = new .();
								mQueuedCollapseData.mData.Set(collapseData);
								mQueuedCollapseData.mTextVersion = resolveParams.mTextVersion;
								mQueuedCollapseData.mCharIdSpan = resolveParams.mCharIdSpan.Duplicate();
								mQueuedCollapseData.mResolveType = resolveType;
							}
						}
						else
						{
							QueueFullRefresh(false);
						}
					}

					if (resolveParams != null)
					{
						for (var emitEmbedData in resolveParams.mEmitEmbeds)
						{
							var data = resolvePassData.GetEmitEmbedData(emitEmbedData.mTypeName, var srcLength, var revision);
							if ((srcLength < 0) && (resolveType == .ClassifyFullRefresh))
								srcLength = 0;
							if (srcLength >= 0)
							{
								emitEmbedData.mCharData = new EditWidgetContent.CharData[srcLength+1] (?);
								emitEmbedData.mCharData[srcLength] = default;
								Internal.MemCpy(emitEmbedData.mCharData.Ptr, data, srcLength * strideof(EditWidgetContent.CharData));
							}
						}
					}
				}
				else
                {
					//DeleteAndNullify!(mProcessResolveCharData);
					//mProcessResolveCharIdSpan.Dispose();

					resolveParams.mCancelled = true;
                    if ((resolveType == ResolveType.Classify) || (resolveType == ResolveType.ClassifyFullRefresh))
					{
                        QueueFullRefresh(false);
					}
                    bfCompiler.QueueDeferredResolveAll();
                }

				parser.FinishClassifier(resolvePassData);
            }
            else
            {
                parser.ClassifySource(charData, !mIsBeefSource);
            }

            if (!isBackground)
            {
                String autocompleteInfo = scope String();
                bfCompiler.GetAutocompleteInfo(autocompleteInfo);
				HandleResolveResult(resolveType, autocompleteInfo, resolveParams);
            }
			else if (resolveParams != null)
			{
				resolveParams.mAutocompleteInfo = new String();
				bfCompiler.GetAutocompleteInfo(resolveParams.mAutocompleteInfo);
			}

            if (!isBackground)            
                bfSystem.PerfZoneStart("Cleanup");
            delete resolvePassData;

            if ((!isBackground) || (isFastClassify))
            {
                //bool isAutocomplete = (resolveType == ResolveType.Autocomplete) || (resolveType == ResolveType.Autocomplete_HighPri);
                //if (isAutocomplete)
                if ((!isFastClassify) && (!isBackground) && (resolveType == ResolveType.Autocomplete))
                    InjectErrors(passInstance, mEditWidget.mEditWidgetContent.mData.mText, mEditWidget.mEditWidgetContent.mData.mTextIdData.GetPrepared(), true, false);
                //IDEApp.sApp.ShowPassOutput(passInstance);
                
                delete passInstance;
				if ((resolveParams != null) && (resolveParams.mPassInstance == passInstance))
					resolveParams.mPassInstance = null;
            }
            else
            {
				if (!isInterrupt)
                	bfSystem.RemoveOldData();
            }

			if ((resolveParams == null) || (resolveParams.mParser == null))
			{
				delete parser;
				mWantsParserCleanup = true;
			}
            
            bfSystem.Unlock();

            if (!isBackground)
                bfSystem.PerfZoneEnd();

			if ((resolveParams != null) && (resolveParams.mWaitEvent != null))
				resolveParams.mWaitEvent.Set(true);

            //Debug.WriteLine("Classify Done {0}", !isBackground);

            /*for (int i = 0; i < text.Length; i++)
                mEditWidget.Content.mText[i].mTypeNum = (ushort)elementTypeArray[i];            */            
        }

        int32 GetBraceNum(EditWidgetContent.CharData c)
        {
            if (c.mDisplayTypeId != 0)
                return 0;
            switch ((char8)c.mChar)
            {
                case '{':
                    return 1;
                case '[':
                    return 2;
                case '(':
                    return 3;
                case '<':
                    return 4;
                case '}':
                    return -1;
                case ']':
                    return -2;
                case ')':
                    return -3;
                case '>':
                    return -4;
            }
            return 0;
        }

        public void MatchBrace()
        {
            var textData = mEditWidget.Content.mData.mText;
            int32[] braceCounts = scope int32[4];

            int cursorIdx = mEditWidget.Content.CursorTextPos;
            int braceIdx = cursorIdx;
            int braceNum = GetBraceNum(textData[braceIdx]);
            if ((braceNum == 0) && (braceIdx > 0))
            {
                braceIdx--;
                cursorIdx--;
                braceNum = GetBraceNum(textData[braceIdx]);
            }
            if (braceNum != 0)
            {
                int bracePairNum = -braceNum;
                int searchIdx = braceIdx;
                int searchDir = braceNum / Math.Abs(braceNum);
                int braceDepth = 0;

                while ((searchIdx >= 0) && (searchIdx < textData.Count))
                {
                    int32 checkBraceNum = GetBraceNum(textData[searchIdx]);
                    if ((checkBraceNum != 0) && ((braceNum == 4) || (braceNum == -4)))
                    {
                        if (checkBraceNum > 0)
                            braceCounts[checkBraceNum - 1]++;
                        else if (checkBraceNum < 0)
                        {
                            int32 decBraceNum = -checkBraceNum - 1;
                            braceCounts[decBraceNum]--;
                            if ((decBraceNum != 3) && (braceNum != checkBraceNum) && (braceCounts[decBraceNum] < 0))
                                return;
                        }
                    }

                    if (textData[searchIdx].mDisplayTypeId != 0)
                        checkBraceNum = 0;
                    if ((checkBraceNum == braceNum) || (checkBraceNum == bracePairNum))
                    {
                        braceDepth += checkBraceNum;
                        if (braceDepth == 0)
                        {
                            if (searchDir > 0)
                                searchIdx++; // Put cursor after close

                            if ((braceNum == 4) || (braceNum == -4))
                            {
                                // If we're matching <>'s then make sure the ) and } counts are zero
                                if ((braceCounts[0] != 0) || (braceCounts[1] != 0) || (braceCounts[2] != 0))
                                    break;
                            }

                            int cursorStartPos = cursorIdx;
                            if (searchDir < 0)
                                cursorStartPos++;
                            mEditWidget.Content.CursorTextPos = searchIdx;
                            if (mWidgetWindow.IsKeyDown(KeyCode.Shift))
                            {
                                mEditWidget.Content.mSelection = EditSelection(cursorStartPos, mEditWidget.Content.CursorTextPos);
                            }
                            else
                                mEditWidget.Content.mSelection = null;
                            mEditWidget.Content.CursorMoved();
                            mEditWidget.Content.EnsureCursorVisible();
                            break;
                        }
                    }

                    searchIdx += searchDir;
                }
            }
        }

        public bool FileNameMatches(String fileName)
        {
			if (mFilePath == null)
				return false;
			if ((mAliasFilePath != null) && (Path.Equals(fileName, mAliasFilePath)))
				return true;
            return Path.Equals(fileName, mFilePath);
        }

        public void HilitePosition(LocatorType hilitePosition, int32 prevLine = -1)
        {
            if (hilitePosition == LocatorType.None)
                return;

            if ((hilitePosition == LocatorType.Smart) && (prevLine != -1) && (!mJustShown))
            {
                int32 newLine = mEditWidget.Content.CursorLineAndColumn.mLine;
                if (Math.Abs(prevLine - newLine) < 16)
                    return;
            }

            float x;
            float y;
            mEditWidget.Content.GetTextCoordAtCursor(out x, out y);

            var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

            LocatorAnim.Show(hilitePosition, mEditWidget.Content, x, y + sourceEditWidgetContent.mFont.GetLineSpacing() / 2);
        }

        public void ShowFileLocation(int cursorIdx, LocatorType hilitePosition)
        {   
         	var activePanel = GetActivePanel();

			if (activePanel != this)
			{
				activePanel.ShowFileLocation(cursorIdx, hilitePosition);
				return;
			}

			if (mEditWidget == null)
				return;

            int32 prevLine = mEditWidget.Content.CursorLineAndColumn.mLine;
			
            mEditWidget.Content.mSelection = null;

			int wantCursorPos = Math.Min(mEditWidget.Content.mData.mTextLength - 1, cursorIdx);
			if (wantCursorPos >= 0)
            	mEditWidget.Content.CursorTextPos = wantCursorPos;
            mEditWidget.Content.CursorMoved();
            mEditWidget.Content.EnsureCursorVisible(true, true);
			mEditWidget.Content.mCursorImplicitlyMoved = true;
            if (mJustShown) // Jump to whatever position we're scrolling to
            {
                mEditWidget.mVertPos.mPct = 1.0f;
                mEditWidget.UpdateContentPosition();
            }
            
            HilitePosition(hilitePosition, prevLine);
        }

        public void ShowFileLocation(int refHotIdx, int line, int column, LocatorType hilitePosition)
        {
			mRequestedLineAndColumn = EditWidgetContent.LineAndColumn(line, column);
                     
            //int prevLine = mEditWidget.Content.CursorLineAndColumn.mLine;
            var content = mEditWidget.Content;
			var useLine = line;

			ProjectSource projectSource = FilteredProjectSource;

            if (projectSource != null)
            {
                bool allowThrough = false;
                bool worked = false;
                if (refHotIdx == -1)
                {
                    int char8Idx = content.GetTextIdx(useLine, column);
                    ShowFileLocation(char8Idx, hilitePosition);
                    worked = true;
                }
                else
                {                    
                    int32 char8Id = IDEApp.sApp.mWorkspace.GetProjectSourceCharId(projectSource, refHotIdx, useLine, column);
                    if (char8Id != 0)
                    {
                        int char8Idx = content.GetCharIdIdx(char8Id);
                        if (char8Idx != -1)
                        {
                            ShowFileLocation(char8Idx, hilitePosition);
                            worked = true;
                        }
                        else if (IDEApp.sApp.mWorkspace.GetProjectSourceCompileInstance(projectSource, refHotIdx) == null)
                        {
                            allowThrough = true;
                        }
                    }
                }

                if (mOldVersionPanel != null)
                {
                    float scrollTopDelta = mEditWidget.Content.GetCursorScreenRelY();
                    mOldVersionPanel.ShowFileLocation(refHotIdx, useLine, column, hilitePosition);
                    if (mOldVersionPanel.mJustShown)
                    {
                        mOldVersionPanel.mEditWidget.Content.SetCursorScreenRelY(scrollTopDelta - mOldVersionPanel.mPanelHeader.mHeight);
                        mOldVersionPanel.mEditWidget.Content.EnsureCursorVisible(true, true);
                    }
                    return;
                }

                if (!allowThrough)
                {
                    if (!worked)
                        IDEApp.Beep(IDEApp.MessageBeepType.Error);
                    return;
                }
            }
            
            useLine = Math.Min(useLine, content.GetLineCount() - 1);

            int cursorIdx = content.GetTextIdx(useLine, column);
            ShowFileLocation(cursorIdx, hilitePosition);

            /*content.MoveCursorTo(line, column, true);
            //content.EnsureCursorVisible(true, true);
            if (mJustShown) // Jump to whatever position we're scrolling to
            {
                mEditWidget.mVertPos.mPct = 1.0f;
                mEditWidget.UpdateContentPosition();
            }
            if (hilitePosition)
                HilitePosition(prevLine);*/
        }

		void RemoveEditWidget()
		{

		}

		public void AttachToProjectSource(ProjectSource projectSource)
		{
			mProjectSource = projectSource;
			if (mEditData != null)
			{
				if (mProjectSource.mEditData == null)
				{
					mEditData.Ref();
					mProjectSource.mEditData = mEditData;
					mEditData.mProjectSources.Add(mProjectSource);
					// Rehup mFileDeleted if necessary
					if (mEditData.mFileDeleted)
						mEditData.IsFileDeleted();
				}
			}
			QueueFullRefresh(true);
		}

		public void DetachFromProjectItem(bool fileDeleted)
		{
			if (mProjectSource == null)
				return;

			if (fileDeleted)
			{
				// We manually add this change record because it may not get caught since the watch dep may be gone
				// This will allow the "File Deleted" dialog to show.
				var changeRecord = new FileWatcher.ChangeRecord();
				changeRecord.mChangeType = .Deleted;
				changeRecord.mPath = new String(mFilePath);
				gApp.mFileWatcher.AddChangedFile(changeRecord);
			}

			ProcessDeferredResolveResults(-1);

			//Debug.Assert(mEditData != null);

			//gApp.mFileEditData.Add(mEditData);
            //mProjectSource.mEditData = null;
			mProjectSource = null;
			QueueFullRefresh(true);

			if (mOldVersionPanel != null)
				mOldVersionPanel.DetachFromProjectItem(false);
			if (mSplitTopPanel != null)
				mSplitTopPanel.DetachFromProjectItem(false);
		}

		public void CloseEdit()
		{
			var sewc = mEditWidget.mEditWidgetContent as SourceEditWidgetContent;
			for (var embed in sewc.mEmbeds.Values)
			{
				if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
				{
					if (emitEmbed.mView != null)
					{
						emitEmbed.mView.RemoveSelf();
						DeleteAndNullify!(emitEmbed.mView);
						sewc.mCollapseNeedsUpdate = true;
					}
				}
			}

			mEditWidget.mPanel = null;

			var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
			if (editWidgetContent.mAutoComplete != null)
				editWidgetContent.mAutoComplete.Close();
			DeleteAndNullify!(editWidgetContent.mOnGenerateAutocomplete);
			DeleteAndNullify!(editWidgetContent.mOnEscape);
			DeleteAndNullify!(editWidgetContent.mOnFinishAsyncAutocomplete);
			DeleteAndNullify!(editWidgetContent.mOnCancelAsyncAutocomplete);
			editWidgetContent.mSourceViewPanel = null;
			mEditWidget.RemoveSelf();

			if ((mEditData != null) && (mEditData.mEditWidget == mEditWidget))
			{
				// if there's another view of this window open, remap the mEditData.mEditWidget to that one
				//  That allows us to delete our current edit widget
				for (var user in mEditData.mEditWidget.mEditWidgetContent.mData.mUsers)
				{
					if ((user != mEditWidget.mEditWidgetContent) && (var sourceEditWidget = user.mEditWidget as SourceEditWidget))
					{
						mEditData.mEditWidget = sourceEditWidget;
					}
				}
			}

			if ((mEditData != null) && (mEditData.mEditWidget == mEditWidget))
			{
				//Debug.WriteLine("Dispose mEditData.mEditWidget = {0}", mEditWidget);
				Debug.Assert(mEditWidget.mParent == null);
				mEditData.mOwnsEditWidget = true;
			}
			else
			{
				//Debug.WriteLine("Dispose deleting mEditWidget {0}", mEditWidget);
				delete mEditWidget;
			}
			mEditWidget = null;

			if ((mProjectSource == null) && (mEditData != null) && (mEditData.mOwnsEditWidget) && (mEditData.mEditWidget.mEditWidgetContent.mData.mUsers.Count == 1))
			{
				// This isn't needed anymore...
				gApp.DeleteEditData(mEditData);
			}
			mEditData = null;
		}

        public override void Dispose()
        {
			if (mDisposed)
				return;

			if (mProjectSource?.mEditData?.HasTextChanged() == true)
			{
				mProjectSource.ClearEditData();
			}

			ProcessDeferredResolveResults(-1);

			if (IDEApp.sApp.mLastActiveSourceViewPanel == this)
				IDEApp.sApp.mLastActiveSourceViewPanel = null;

			if (IDEApp.sApp.mLastActivePanel == this)
				IDEApp.sApp.mLastActivePanel = null;

			if (Content.Data.mCurQuickFind != null)
			{
				//Content.Data.mCurQuickFind.Close();
				Content.Data.mCurQuickFind = null;
			}

			if (mFilePath != null)            
				IDEApp.sApp.mFileWatcher.RemoveWatch(mFilePath);

            CloseEdit();

            /*if (mProjectSource != null)
                mProjectSource.ClearUnsavedData();*/

            if (mOldVersionPanel != null)
                mOldVersionPanel.Dispose();
			if (mSplitTopPanel != null)
				mSplitTopPanel.Dispose();

            DebugManager debugManager = IDEApp.sApp.mDebugger;
            debugManager.mBreakpointsChangedDelegate.Remove(scope => BreakpointsChanged, true);

#if IDE_C_SUPPORT
            if (mIsClang)
            {
                var clangCompiler = ClangResolveCompiler;
                clangCompiler.QueueFileRemoved(mFilePsath);                
            }
#endif

			if (IDEApp.sApp.mBfResolveHelper != null)
            	IDEApp.sApp.mBfResolveHelper.SourceViewPanelClosed(this);

            if (mResolveJobCount > 0)
            {
				//TODO: Make a way we can cancel only our specific job
                ResolveCompiler.CancelBackground();
                Debug.Assert(mResolveJobCount == 0);
            }

			if (mSpellCheckJobCount > 0)
			{
				IDEApp.sApp.mSpellChecker.CancelBackground();
				Debug.Assert(mSpellCheckJobCount == 0);
			}

            if (mRenameSymbolDialog != null)
                mRenameSymbolDialog.Close();

            base.Dispose();
        }

        public void Close()
        {

        }

		SourceViewPanel GetOldVersionPanel()
		{
			if (mSplitBottomPanel != null)
				return mSplitBottomPanel.mOldVersionPanel;
			return mOldVersionPanel;
		}

		public SourceViewPanel GetActivePanel()
		{
			if (mSplitTopPanel != null)
			{
				if (mSplitTopPanel.mLastFocusTick > mLastFocusTick)
				{					
					return mSplitTopPanel.GetActivePanel();
				}
			}

			if (mOldVersionPanel != null)
			{			    
			    return mOldVersionPanel.GetActivePanel();
			}

			return this;
		}

        public void FocusEdit()
        {
			if (mWidgetWindow == null)
				return;
			let activePanel = GetActivePanel();
            activePanel.mEditWidget?.SetFocus();

			if (!mWidgetWindow.mHasFocus)
				EditGotFocus();
        }

        public override void SetFocus()
        {			
            //mEditWidget.SetFocus();
			FocusEdit();
        }

		public bool HasFocus(bool selfOnly = false)
		{
			if (!selfOnly)
			{
				if ((mSplitTopPanel != null) && (mSplitTopPanel.mEditWidget.mHasFocus))
					return true;
				if ((mSplitBottomPanel != null) && (mSplitBottomPanel.mEditWidget.mHasFocus))
					return true;
				if ((mOldVersionPanel != null) && (mOldVersionPanel.mEditWidget.mHasFocus))
					return true;
			}
			if (mEditWidget.mHasFocus)
				return true;
			if (mQuickFind != null)
				return mQuickFind.HasFocus();
			return false;
		}

		public void SyncWithWorkspacePanel()
		{
			if (gApp.mProjectPanel.[Friend]mProjectToListViewMap.TryGet(mProjectSource, var matchKey, var projectListViewItem))
			{
				var checkLVItem = projectListViewItem.mParentItem;
				while (checkLVItem != null)
				{
					checkLVItem.Open(true);
					checkLVItem = (ProjectListViewItem)checkLVItem.mParentItem;
				}

				projectListViewItem.mListView.GetRoot().SelectItemExclusively(projectListViewItem);
				projectListViewItem.mListView.EnsureItemVisible(projectListViewItem, false);
			}
		}

        public override void EditGotFocus()
        {
			if ((mFilePath != null) && (mEmbedKind == .None))
				gApp.AddToRecentDisplayedFilesList(mFilePath);
			if (mLoadFailed)
				return;
#if IDE_C_SUPPORT
            if (mIsClang)
            {
                if (IDEUtils.IsHeaderFile(mFilePath))
                {
                    // If a different cpp file has gotten focus since we have last been here, then we want to
                    //  re-evaluate the mClangSource so we can potentially switch to using that
                    //if (mClangSource != null)
                    {
                        var resolveClang = IDEApp.sApp.mResolveClang;
                        using (resolveClang.mMonitor.Enter())
                        {                            
                            if (mLastMRUVersion != resolveClang.mProjectSourceVersion)
                            {
                                //mClangSource = null;
                                QueueFullRefresh(true);
                                mLastMRUVersion = resolveClang.mProjectSourceVersion;
                            }
                        }
                    }
                }
                else if (mProjectSource != null)
                    IDEApp.sApp.mResolveClang.UpdateMRU(mProjectSource);
            }
#endif
            var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            sourceEditWidgetContent.mCursorStillTicks = 0;
			sourceEditWidgetContent.mCursorBlinkTicks = 0;

			if (mProjectSource != null)
			{
                var editData = gApp.GetEditData(mProjectSource, true);
				editData.mEditWidget = mEditWidget;
			}

			if (mEmbedKind == .None)
			{
				gApp.mLastActiveSourceViewPanel = this;
				gApp.mLastActivePanel = this;

				if ((gApp.mSettings.mEditorSettings.mSyncWithWorkspacePanel) && (mProjectSource != null))
				{
					SyncWithWorkspacePanel();
				}
			}
        }

        public override void EditLostFocus()
        {
#if IDE_C_SUPPORT
            if (mClangSourceChanged)
            {
                IDEApp.sApp.mDepClang.FileSaved(mFilePath);
                mClangSourceChanged = false;
            }
#endif
        }

		protected override void RemovedFromWindow()
		{
			if (mHoverWatch != null)
				mHoverWatch.Close();

			if (mRenameSymbolDialog != null)
				mRenameSymbolDialog.Cancel();

			if (mEditWidget != null)
			{
				var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
				if (sourceEditWidgetContent.mAutoComplete != null)
					sourceEditWidgetContent.mAutoComplete.Close();
			}

			base.RemovedFromWindow();
			CloseOldVersion();

			if ((NeedsPostRemoveUpdate) && (!mInPostRemoveUpdatePanels))
			{
				//Debug.WriteLine("Adding sourceViewPanel to mPostRemoveUpdatePanel {0}", this);
				mInPostRemoveUpdatePanels = true;
				gApp.mPostRemoveUpdatePanels.Add(this);
			}
		}

        public override void AddedToParent()
        {
            base.AddedToParent();
            mWantsFullClassify = true;
            mWantsFullRefresh = true;
            mJustShown = true;
            mWantsClassifyAutocomplete = false;
            
            if (mUpdateCnt == 0)
            {
                //DoFastClassify();
				mWantsFastClassify = true;
                mWantsFullClassify = true;
            }

            if (mDesiredVertPos != 0)
            {
                mEditWidget.Content.RecalcSize();
                mEditWidget.mVertPos.Set(mDesiredVertPos, true);                
                mEditWidget.UpdateContentPosition();
                mDesiredVertPos = 0;
            }
            //IDEApp.sApp.mInDisassemblyView = false;

            if ((mProjectSource != null) && (IDEApp.sApp.mWakaTime != null))
            {
                IDEApp.sApp.mWakaTime.QueueFile(mFilePath, mProjectSource.mProject.mProjectName, false);
            }

			IDEApp.sApp.mLastActiveSourceViewPanel = this;
			IDEApp.sApp.mLastActivePanel = this;
        }

        void CheckTrackedElementChanges()
        {
            if (mHotFileIdx != -1)
                return;

			bool hadBreakpointChanges = false;

            // Update tracked positions
            if (mTrackedTextElementViewList != null)
            {
                var editContent = (SourceEditWidgetContent)mEditWidget.Content;

                for (var trackedElementView in mTrackedTextElementViewList)
                {                    
                    int32 startContentIdx = -1;
					if (trackedElementView.mTrackedElement.mIsDead)
						continue;
                    var breakpoint = trackedElementView.mTrackedElement as Breakpoint;
                    var trackedElement = trackedElementView.mTrackedElement;

                    if (trackedElement.mSnapToLineStart)
                    {
                        int32 lineLeft = trackedElementView.mTextPosition.mIndex;
						if (lineLeft < editContent.mData.mTextLength)
						{
	                        repeat
	                        {
	                            if (!((char8)editContent.mData.mText[lineLeft].mChar).IsWhiteSpace)
	                                startContentIdx = lineLeft;                            
	                            lineLeft--;
	                        }
	                        while ((lineLeft > 0) && (editContent.mData.mText[lineLeft].mChar != '\n'));
						}

						if (startContentIdx == -1)
						{
							lineLeft = trackedElementView.mTextPosition.mIndex;

							repeat
							{
								if (lineLeft >= editContent.mData.mTextLength)
									break;

							    if (!((char8)editContent.mData.mText[lineLeft].mChar).IsWhiteSpace)
								{
							        startContentIdx = lineLeft;
									break;
								}
							    lineLeft++;
							}
							while ((lineLeft > 0) && (editContent.mData.mText[lineLeft].mChar != '\n'));
						}
                    }
                    else
                        startContentIdx = trackedElementView.mTextPosition.mIndex;

                    if ((startContentIdx == -1) || (trackedElementView.mTextPosition.mWasDeleted))
                    {
                        if (breakpoint != null)
                        {
							BfLog.LogDbg("Tracked element deleted. Deleting breakpoint\n");
                            IDEApp.sApp.mDebugger.DeleteBreakpoint(breakpoint);
                            continue;
                        }
                        else
                        {
                            trackedElementView.mTextPosition.mWasDeleted = false;
                            if (startContentIdx == -1)
                                startContentIdx = trackedElementView.mTextPosition.mIndex;
                        }
                    }

                    if (trackedElementView.mLastMoveIdx != trackedElement.mMoveIdx)
					{
						UpdateTrackedElementView(trackedElementView);
						continue;
					}

                    trackedElementView.mTextPosition.mIndex = startContentIdx;

                    int line;
                    int lineCharIdx;
                    mEditWidget.Content.GetLineCharAtIdx(trackedElementView.mTextPosition.mIndex, out line, out lineCharIdx);

					if ((breakpoint != null) && (breakpoint.mLineNum != line))
						hadBreakpointChanges = true;
                    //RemapActiveToCompiledLine(0, ref line, ref lineCharIdx);
                    trackedElementView.mTrackedElement.Move(line, lineCharIdx);
                }
            }

			if (hadBreakpointChanges)
				IDEApp.sApp.mDebugger.mBreakpointsChangedDelegate();
        }

        void UpdateTrackedElements()
        {
            if (mHotFileIdx != -1)
                return;
			if (!mIsBeefSource)
				return;

            for (var breakpointView in GetTrackedElementList())
            {
                var trackedElement = breakpointView.mTrackedElement;
                var breakpoint = trackedElement as Breakpoint;                
                //if ((breakpoint != null) && (breakpoint.mNativeBreakpoint != null))

				//TODO: we're only doing this for BOUND breakpoints.  Otherwise if we create a new method and set a
				// breakpoint on it but the new method hasn't actually been compile yet, then it causes the breakpoint
				// to go crazy
				if ((breakpoint != null) && (breakpoint.mNativeBreakpoint != null))
                {                                                            
                    int32 breakpointLineNum = breakpoint.GetLineNum();

                    if (breakpointLineNum != breakpointView.mLastBoundLine)
                    {
                        breakpointView.mLastBoundLine = breakpointLineNum;
                        UpdateTrackedElementView(breakpointView);

                        int compileIdx = IDEApp.sApp.mWorkspace.GetHighestCompileIdx();
                        if (compileIdx != -1)
                        {
                            int column = -1;
                            breakpoint.mLineNum = breakpointLineNum;
							int line = breakpoint.mLineNum;
                            RemapCompiledToActiveLine(compileIdx, ref line, ref column);
							breakpoint.mLineNum = (int32)line;
                        }
                    }                                                                    
                }
            }
        }

        void BreakpointsChanged()
        {
            mTrackedTextElementViewListDirty = true;
        }

		public void EnsureTrackedElementsValid()
		{
			if (mTrackedTextElementViewListDirty)
			{
			    // Update the old one and then clear it and then update the new one
				//TODO: The old one could contain deleted breakpoints and such...  Under what circumstances should be do this?
			    CheckTrackedElementChanges();
			    GetTrackedElementList();
			}
			CheckTrackedElementChanges();
		}

        public void ClearTrackedElements()
        {
            if (mTrackedTextElementViewList != null)
            {
                var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
                for (var breakpoint in mTrackedTextElementViewList)
				{
                    editWidgetContent.PersistentTextPositions.Remove(breakpoint.mTextPosition);
					delete breakpoint.mTextPosition;
				}
				DeleteContainerAndItems!(mTrackedTextElementViewList);
            }
            mTrackedTextElementViewList = null;
        }

        public void UpdateTrackedElementView(TrackedTextElementView trackedElementView)
        {            
            var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            var trackedElement = trackedElementView.mTrackedElement;

            if (trackedElementView.mTextPosition != null)            
            {
                editWidgetContent.PersistentTextPositions.Remove(trackedElementView.mTextPosition);            
				delete trackedElementView.mTextPosition;
			}

            int lineStart;
            int lineEnd;
            mEditWidget.Content.GetLinePosition(trackedElement.mLineNum, out lineStart, out lineEnd);
            
            trackedElementView.mTextPosition = new PersistentTextPosition((int32)Math.Min(editWidgetContent.mData.mTextLength, lineStart + trackedElement.mColumn));
            editWidgetContent.PersistentTextPositions.Add(trackedElementView.mTextPosition);

			trackedElementView.mLastMoveIdx = trackedElement.mMoveIdx;
        }

        List<TrackedTextElementView> GetTrackedElementList()
        {
			if ((mEmbedKind != .None) && (mEditWidget.mEditWidgetContent.mData.mTextLength == 0))
			{
				if (mTrackedTextElementViewList == null)
					mTrackedTextElementViewList = new .();
				return mTrackedTextElementViewList;
			}

            if (mTrackedTextElementViewListDirty)
            {
                ClearTrackedElements();
                mTrackedTextElementViewListDirty = false;
            }

            DebugManager debugManager = IDEApp.sApp.mDebugger;
            if (mTrackedTextElementViewList == null)
            {
                String findFileName = mFilePath;
				String srcFileName = mAliasFilePath ?? mFilePath;
                
                mTrackedTextElementViewList = new List<TrackedTextElementView>();
				if (mFilePath == null)
					return mTrackedTextElementViewList;
				
				for (var folder in IDEApp.sApp.mBookmarkManager.mBookmarkFolders)
				{
				    for (var bookmark in folder.mBookmarkList)
				    {
				        if (Path.Equals(bookmark.mFileName, findFileName))
				        {
				            var bookmarkView = new TrackedTextElementView(bookmark);
				            UpdateTrackedElementView(bookmarkView);
				            mTrackedTextElementViewList.Add(bookmarkView);
				        }
				    }
				}

                for (var breakpoint in debugManager.mBreakpointList)
                {
                    if ((breakpoint.mFileName != null) && (Path.Equals(breakpoint.mFileName, srcFileName)))
                    {
                        var breakpointView = new TrackedTextElementView(breakpoint);
                        UpdateTrackedElementView(breakpointView);
                        mTrackedTextElementViewList.Add(breakpointView);
                    }
                }                

                //////
                for (var historyEntry in IDEApp.sApp.mHistoryManager.mHistoryList)
                {
                    if (Path.Equals(historyEntry.mFileName, findFileName))
                    {
                        var historyView = new TrackedTextElementView(historyEntry);
                        UpdateTrackedElementView(historyView);
                        mTrackedTextElementViewList.Add(historyView);
                    }
                }
            }

            return mTrackedTextElementViewList;
        }

		void CloseHeader()
		{
			if (mPanelHeader != null)
			{
				mPanelHeader.RemoveSelf();
				gApp.DeferDelete(mPanelHeader);
				mPanelHeader = null;
				ResizeComponents();
			}
		}

		void ClearLoadFailed()
		{
			if (mLoadFailed)
			{
				mLoadFailed = false;
				CloseHeader();
			}
		}

		public void CheckBinary()
		{
			mIsBinary = false;

			let data = mEditWidget.mEditWidgetContent.mData;
			for (int i < data.mTextLength)
			{
				if (data.mText[i].mChar <= '\x08')
				{
					data.mText[i].mChar = ' ';
					mIsBinary = true;
				}
			}

			if (mIsBinary)
			{
				for (int i < data.mTextLength)
				{
					if (data.mText[i].mChar >= '\x80')
					{
						data.mText[i].mChar = ' ';
						mIsBinary = true;
					}
				}
			}

			if (mIsBinary)
			{
				mEditWidget.mEditWidgetContent.mIsReadOnly = true;
			}
		}

        public bool Show(String filePath, bool silentFail = false, FileEditData fileEditData = null)
        {
			scope AutoBeefPerf("SourceViewPanel.Show");

			ClearLoadFailed();

			Debug.Assert(!mDisposed);

			String useFilePath = null;
			var useFileEditData = fileEditData;
			if (filePath != null)
			{
				Debug.Assert(!filePath.Contains("//"));

	            useFilePath = scope:: String(filePath);
	            IDEUtils.FixFilePath(useFilePath);

	            if (mProjectSource == null)
	            {
	                mProjectSource = IDEApp.sApp.FindProjectSourceItem(useFilePath);
	            }
			}

			if ((useFileEditData == null) && (useFilePath != null))
			{
	            if (mProjectSource != null)
				{
	                useFileEditData = IDEApp.sApp.GetEditData(mProjectSource, true);
				}
				else
				{
					useFileEditData = gApp.GetEditData(useFilePath, true, true);
				}
				if (useFileEditData.mEditWidget == null)
					useFileEditData = null;
			}

			mEditData = useFileEditData;
            if (useFileEditData != null)
            {
				if (useFileEditData.mEditWidget != null)
				{
					if (mEditWidget != null)
					{
						mEditWidget.RemoveSelf();
						delete mEditWidget;
						mEditWidget = null;
					}

					//Debug.Assert(projectSourceEditData.mOwnsEditWidget);
					if (useFileEditData.mOwnsEditWidget)
					{
		                mEditWidget = useFileEditData.mEditWidget;
						useFileEditData.mOwnsEditWidget = false;

						//Debug.WriteLine("Taking over EditWidget {0} Parent: {1}", mEditWidget, mEditWidget.mParent);
						Debug.Assert(mEditWidget.mParent == null);
					}
					else
					{
						mEditWidget = IDEApp.sApp.CreateSourceEditWidget(useFileEditData.mEditWidget);
						mEditWidget.Content.RecalcSize();

						//Debug.WriteLine("Creating new EditWidget {0}", mEditWidget);
					}
				}
                //mLastFileTextVersion = useFileEditData.mLastFileTextVersion;
            }

			if (mEditWidget == null)
            {
                mEditWidget = IDEApp.sApp.CreateSourceEditWidget();
            }
            SetupEditWidget();

			DeleteAndNullify!(mFilePath);
			if (useFilePath != null)
			{
	            mFilePath = new String(useFilePath);
	            IDEApp.sApp.mFileWatcher.WatchFile(mFilePath);
	            
	            var ext = scope String();
	            Path.GetExtension(useFilePath, ext);
				/*if (ext.Length == 0)
					return false;*/
	            ext.ToLower();
	            mIsSourceCode = IDEApp.IsSourceCode(useFilePath);
	            mIsBeefSource = IDEApp.IsBeefFile(useFilePath);
			}

            //TODO: Find mProjectSource

            if (mIsSourceCode && !mIsBeefSource)
            {
                mIsClang = true;
                //((SourceEditWidgetContent)mEditWidget.Content).mAsyncAutocomplete = true;
            }

            mJustShown = true;
            mWantsFullRefresh = true;
            mWantsFullClassify = true;

            //mCurBfParser = IDEApp.sApp.mBfResolveSystem.CreateParser(mFilePath);
            if (useFileEditData == null)
            {
                var text = scope String();
				SourceHash hash;
				if (mFilePath == null)
				{
					// Nothing
				}
                else if (gApp.LoadTextFile(mFilePath, text, true, scope [&] () => { hash = SourceHash.Create(.MD5, text); } ) case .Err)
                {
					if (silentFail)
					{
						Close();
						return false;
					}

					mEditWidget.mEditWidgetContent.mIsReadOnly = true;
                    FileOpenFailed();
                }
				else
				{
	                IDEApp.sApp.mFileWatcher.FileIsValid(mFilePath);

	                mEditWidget.Content.AppendText(text);

					using (gApp.mMonitor.Enter())
					{
		                if ((mEditData != null) && (!mEditData.mSavedCharIdData.IsEmpty))
		                {
		                    int char8IdDataLength = mEditData.mSavedCharIdData.GetTotalLength();
		                    if (char8IdDataLength == mEditWidget.Content.mData.mTextLength)
		                    {
		                        // The saved char8IdData matches the length of our text, so we can use it
		                        //  This is important for text mapping after a tab has been closed and then 
		                        //  the files is reopened
		                        mEditWidget.Content.mData.mTextIdData.DuplicateFrom(ref mEditData.mSavedCharIdData);
		                    }
		                }
					}

	                //mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
				}
            }
			else
			{
				//LoadedHash = useFileEditData.mLoadedHash;

				// Sanity check for when we have the saved data cached already
				if (useFileEditData.IsFileDeleted())
				{
					if (silentFail)
					{
						Close();
						return false;
					}

					FileOpenFailed();
				}
			}

			CheckBinary();

 			InitSplitter();

			if ((mEditData != null) && (mFilePath != null))
				Debug.Assert(Path.Equals(mFilePath, mEditData.mFilePath));
			mLastRecoveryTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;

            return true;
        }

		public void RehupAlias()
		{
			if ((gApp.mDebugger.mIsRunning) && (mAliasFilePath != null))
			{
				gApp.mDebugger.SetAliasPath(mAliasFilePath, mFilePath);
			}
		}

		void BrowseForFile()
		{
#if !CLI
			var fileDialog = scope System.IO.OpenFileDialog();
			

			var initialDir = scope String(IDEApp.sApp.mWorkspace.mDir);
			//initialDir.Replace('/', '\\');

			var fileName = scope String();
			Path.GetFileName(mFilePath, fileName);

			var title = scope String();
			title.AppendF("Open {0}", fileName);
			fileDialog.Title = title;
			fileDialog.Multiselect = false;

			var ext = scope String();
			Path.GetExtension(mFilePath, ext);

			fileDialog.InitialDirectory = initialDir;
			fileDialog.ValidateNames = true;
			fileDialog.DefaultExt = ext;

			var filter = scope String();
			filter.AppendF("{0}|{0}|All files (*.*)|*.*", fileName, ext);

			fileDialog.SetFilter(filter);
			mWidgetWindow.PreModalChild();
			if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
			{
			    for (String filePath in fileDialog.FileNames)
			    {
					mFilePath.Set(filePath);
					RehupAlias();
					RetryLoad();
					break;
			    }
			}
#endif
		}

		void FileOpenFailed()
		{
			bool fileFromNetwork = mOldVerLoadCmd?.StartsWith("http", .OrdinalIgnoreCase) == true;

			mLoadFailed = true;
			//mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
			mPanelHeader = new PanelHeader();
			String fileName = scope String();
			Path.GetFileName(mFilePath, fileName);
			String headerStr = scope String();
			headerStr.AppendF("Source file '{0}' is unavailable. ", fileName);
			if (fileFromNetwork)
				headerStr.Append("Failed to retrieve from network.");
			else
				headerStr.AppendF("Requested path is '{0}'", mFilePath);
			mPanelHeader.Label = headerStr;
			if (!sPreviousVersionWarningShown)
			{
			    mPanelHeader.Flash();
			    sPreviousVersionWarningShown = true;
			}

			bool hasBookmarks = false;
			bool hasBreakpoints = false;

			var trackedElements = GetTrackedElementList();
			for (var element in trackedElements)
			{
				hasBookmarks |= element.mTrackedElement is Bookmark;
				hasBreakpoints |= element.mTrackedElement is Breakpoint;
			}

			if (hasBookmarks)
			{
				var button = mPanelHeader.AddButton("Remove Bookmarks");
				button.mOnMouseClick.Add(new (evt) =>
					{
						button.mVisible = false;
						var trackedElements = GetTrackedElementList();
						for (var element in trackedElements)
						{
							if (var bookmark = element.mTrackedElement as Bookmark)
							{
								gApp.mBookmarkManager.DeleteBookmark(bookmark);
							}
						}
					});
			}

			if (fileFromNetwork)
			{
				var button = mPanelHeader.AddButton("Retry");
				button.mOnMouseClick.Add(new (evt) =>
					{
						LoadOldVer();
					});
			}
			else
			{
				var button = mPanelHeader.AddButton("Retry");
				button.mOnMouseClick.Add(new (evt) =>
				    {
						Reload();
				    });
	
				button = mPanelHeader.AddButton("Auto Find");
				button.mOnMouseClick.Add(new (evt) =>
				    {
						AutoFind();
				    });
			}

			var button = mPanelHeader.AddButton("Browse...");
			button.mOnMouseClick.Add(new (evt) =>
			    {
					BrowseForFile();
			    });

			mPanelHeader.Flash();
			AddWidget(mPanelHeader);
		}

		void AutoFind()
		{
			delete mSourceFindTask;
			mSourceFindTask = new SourceFindTask();
			mSourceFindTask.mSourceViewPanel = this;
			mSourceFindTask.mFilePath = new String(mFilePath);
			mSourceFindTask.mFileName = new String();
			Path.GetFileName(mFilePath, mSourceFindTask.mFileName);

			gApp.WithSourceViewPanels(scope (sourceViewPanel) =>
				{
					if (sourceViewPanel.mAliasFilePath != null)
					{
						var origDir = scope String();
						Path.GetDirectoryPath(sourceViewPanel.mAliasFilePath, origDir);
						IDEUtils.FixFilePath(origDir);
						if (!Environment.IsFileSystemCaseSensitive)
							origDir.ToUpper();

						var localDir = scope String();
						Path.GetDirectoryPath(sourceViewPanel.mFilePath, localDir);

						if (mSourceFindTask.mRemapMap.TryAdd(origDir, var keyPtr, var valuePtr))
						{
							*keyPtr = new String(origDir);
							*valuePtr = new String(localDir);
						}
					}
				});

			ThreadPool.QueueUserWorkItem(new => mSourceFindTask.Run);

			CloseHeader();

			mPanelHeader = new PanelHeader();
			String fileName = scope String();
			Path.GetFileName(mFilePath, fileName);
			String headerStr = scope String();
			headerStr.AppendF("Finding {0}...", fileName);
			mPanelHeader.Label = headerStr;

			var button = mPanelHeader.AddButton("Cancel");
			button.mOnMouseClick.Add(new (evt) =>
				{
					mSourceFindTask.Cancel();
				});
			
			mPanelHeader.mButtonsOnBottom = true;
			
			AddWidget(mPanelHeader);
			ResizeComponents();
		}

		public void ShowWrongHash()
		{
			CloseHeader();

			mPanelHeader = new PanelHeader();
			mPanelHeader.mKind = .WrongHash;
			String fileName = scope String();
			Path.GetFileName(mFilePath, fileName);
			String headerStr = scope String();
			headerStr.AppendF("Warning: This file is not an exact match for the file this program was built with", fileName);
			mPanelHeader.Label = headerStr;

			var button = mPanelHeader.AddButton("Ok");
			button.mOnMouseClick.Add(new (evt) =>
				{
					CloseHeader();
				});

			button = mPanelHeader.AddButton("Browse...");
			button.mOnMouseClick.Add(new (evt) =>
				{
					BrowseForFile();
				});

			mPanelHeader.mButtonsOnBottom = true;

			AddWidget(mPanelHeader);
			ResizeComponents();
		}

		public void SetLoadCmd(String loadCmd)
		{
			bool isRepeat = mOldVerLoadCmd != null;

			CloseHeader();
			
			if (mOldVerLoadCmd == null)
				mOldVerLoadCmd = new String(loadCmd);

			if (loadCmd.StartsWith("http", .OrdinalIgnoreCase))
			{
				LoadOldVer();
				return;
			}
			// For testing a long command...
			//mOldVerLoadCmd.Set("/bin/sleep.exe 10");

			mPanelHeader = new PanelHeader();

			String fileName = scope String();
			Path.GetFileName(mFilePath, fileName);
			String headerStr = scope String();
			if (isRepeat)
				headerStr.AppendF("{0} Failed to retrieve file '{1}'.{2} The following command can be rerun to retry:\n{3}\nWARNING: This is a security risk if this PDB comes from an untrusted source.", Font.EncodeColor(0xFFFF8080), fileName, Font.EncodePopColor(), mOldVerLoadCmd);
			else
				headerStr.AppendF("The file '{0}' can be loaded from a source server by executing the embedded command:\n{1}\nWARNING: This is a security risk if this PDB comes from an untrusted source.", fileName, mOldVerLoadCmd);
			mPanelHeader.Label = headerStr;
			mPanelHeader.mTooltipText = new String(mOldVerLoadCmd);

			var button = mPanelHeader.AddButton("Run");
			button.mOnMouseClick.Add(new (evt) =>
				{
					LoadOldVer();
				});
			button = mPanelHeader.AddButton("Always Run");
			button.mOnMouseClick.Add(new (evt) =>
				{
					LoadOldVer();
				});

			let checkbox = new BoundCheckbox(ref gApp.mStepOverExternalFiles);
			checkbox.Label = "Step over external files";
			mPanelHeader.AddWidget(checkbox);
			checkbox.mOnValueChanged.Add(new () => { gApp.RehupStepFilters(); });

			mPanelHeader.mOnResized.Add(new (widget) =>
				{
					checkbox.Resize(GS!(10), GS!(60), checkbox.CalcWidth(), GS!(20));
				});

			//button = mPanelHeader.AddButton("Run On This ");
			//button = mPanelHeader.AddButton("Never Run");

			mPanelHeader.mButtonsOnBottom = true;
			mPanelHeader.mBaseHeight = 84;
			AddWidget(mPanelHeader);
			ResizeComponents();
		}

		void LoadOldVer()
		{
			if (mOldVerLoadCmd.StartsWith("http", .OrdinalIgnoreCase))
			{
				DeleteAndNullify!(mOldVerHTTPRequest);
				mOldVerHTTPRequest = new HTTPRequest();
				mOldVerHTTPRequest.GetFile(mOldVerLoadCmd, mFilePath);
			}
			else
			{
				Debug.Assert(mOldVerLoadExecutionInstance == null);
				mOldVerLoadExecutionInstance = gApp.DoRun(null, mOldVerLoadCmd, gApp.mInstallDir, .None);
				mOldVerLoadExecutionInstance?.mAutoDelete = false;
			}

			CloseHeader();

			mPanelHeader = new PanelHeader();
			String fileName = scope String();
			Path.GetFileName(mFilePath, fileName);
			String headerStr = scope String();
			headerStr.AppendF("Retrieving {0} via command: {1}", fileName, mOldVerLoadCmd);
			mPanelHeader.Label = headerStr;
			mPanelHeader.mTooltipText = new String(mOldVerLoadCmd);

			var button = mPanelHeader.AddButton("Cancel");
			button.mOnMouseClick.Add(new (evt) =>
				{
					if (mOldVerLoadExecutionInstance != null)
						mOldVerLoadExecutionInstance.Cancel();
					if (mOldVerHTTPRequest != null)
					{
						DeleteAndNullify!(mOldVerHTTPRequest);
					}
				});
			button = mPanelHeader.AddButton("Always Run");
			button.mOnMouseClick.Add(new (evt) =>
				{
					LoadOldVer();
				});
			//button = mPanelHeader.AddButton("Run On This ");
			//button = mPanelHeader.AddButton("Never Run");

			mPanelHeader.mButtonsOnBottom = true;
			//mPanelHeader.mBaseHeight = 72;
			AddWidget(mPanelHeader);
			ResizeComponents();
		}

		void InitSplitter()
		{
			if (mEmbedKind != .None)
				return;

			mSplitter = new PanelSplitter(mSplitTopPanel, this);
			mSplitter.mSplitAction = new => SplitView;
			mSplitter.mUnsplitAction = new => UnsplitView;
			AddWidget(mSplitter);
		}

        public void Reload_Old()
        {
            var text = scope String();
            if (gApp.LoadTextFile(mFilePath, text) case .Err)
            {
                gApp.Fail(StackStringFormat!("Failed to open file '{0}'", mFilePath));
                return;
            }

            //mEditWidget.Content.ClearText();

            int line;
            int lineChar;
            mEditWidget.Content.GetCursorLineChar(out line, out lineChar);
            //float vertPos = mEditWidget.mVertPos.v;

            text.Replace("\r", "");

            var replaceSourceAction = new ReplaceSourceAction(mEditWidget.Content, text, true);
            mEditWidget.Content.mData.mUndoManager.Add(replaceSourceAction);
            replaceSourceAction.Redo();

            //replaceSourceActionClearTrackedElements();

            //UndoBatchStart undoBatchStart = new UndoBatchStart("reload");
            //mEditWidget.Content.mUndoManager.Add(undoBatchStart);
            //mEditWidget.Content.SelectAll();
            //mEditWidget.Content.DeleteSelection();
            //mEditWidget.Content.InsertAtCursor(text);


            /*mEditWidget.Content.MoveCursorTo(line, lineChar, true);
            mEditWidget.VertScrollTo(vertPos);            

            mEditWidget.mVertPos.mPct = 1.0f;
            mEditWidget.UpdateContentPosition();*/

            QueueFullRefresh(false);

            //mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
        }

        /*struct TextLineSegment
        {
            public int mIndex;
            public int mLength;
            public String mLine;
        }*/

        public void Reload()
        {
			bool needsFreshLoad = mLoadFailed;
			if ((mEditData != null) && (!Path.Equals(mFilePath, mEditData.mFilePath)))
			{
				// This can happen if we do an Auto Find for source, which finds an incorrect file but then we Browse
				// to the correct version
				CloseEdit();
				needsFreshLoad = true;
			}

			//TODO: Why did we ClearTrackedElements here? It caused reloads to not move breakpoints and stuff...
			//ClearTrackedElements();

			if (needsFreshLoad)
			{
				Show(mFilePath);
				ResizeComponents();
				return;
			}

			var editWidgetContent = (SourceEditWidgetContent)mEditWidget.mEditWidgetContent;
			Debug.Assert(!editWidgetContent.mIgnoreSetHistory);
			editWidgetContent.mIgnoreSetHistory = true;
			if (mEditData != null)
			{
				mEditData.Reload();
				gApp.FileChanged(mEditData);
			}
			else
			{
				editWidgetContent.Reload(mFilePath);
			}
			editWidgetContent.mIgnoreSetHistory = false;

			if (mEmitRevision == -1) // This causes a rehup loop if there's an emission error if we don't do this check
				QueueFullRefresh(false);
#if IDE_C_SUPPORT
			mClangSourceChanged = false;
#endif
			//mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
			if (mEditData != null)
			    mEditData.mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;

			CheckBinary();
        }

		void CheckAdjustFile()
		{
			var wantHash = mWantHash;
			if (wantHash case .None)
			{
				if (mEditData != null)
				{
					if (!mEditData.mSHA256Hash.IsZero)
						wantHash = .SHA256(mEditData.mSHA256Hash);
					else if (!mEditData.mMD5Hash.IsZero)
						wantHash = .MD5(mEditData.mMD5Hash);
				}
			}

			if (wantHash case .None)
				return;

			String text = scope .();
			if (File.ReadAllText(mFilePath, text, true) case .Err)
				return;
			
			SourceHash textHash = SourceHash.Create(wantHash.GetKind(), text);
			if (textHash == wantHash)
				return;

			if (text.Contains('\r'))
			{
				text.Replace("\r", "");
			}
			else
			{
				text.Replace("\n", "\r\n");
			}
			textHash = SourceHash.Create(wantHash.GetKind(), text);
			if (textHash == wantHash)
			{
				if (File.WriteAllText(mFilePath, text) case .Err)
				{
					gApp.mFileWatcher.OmitFileChange(mFilePath, text);
					return;
				}
			}
		}

		void RetryLoad()
		{
			CloseHeader();

			Reload();
			if (mRequestedLineAndColumn != null)
				ShowFileLocation(-1, mRequestedLineAndColumn.Value.mLine, mRequestedLineAndColumn.Value.mColumn, .Always);
			FocusEdit();

			if ((!(mWantHash case .None)) && (mEditData != null) && (!mEditData.CheckHash(mWantHash)))
				ShowWrongHash();

			if (!mLoadFailed)
			{
				gApp.RehupStepFilters();
			}
		}

		public void RefusedReload()
		{
			if (mEditData != null)
			{
				mEditData.mHadRefusedFileChange = true;
				DeleteAndNullify!(mEditData.mQueuedContent);
			}
		}

        public bool Show(ProjectItem projectItem, bool silentFail = false)
        {
            mProjectSource = (ProjectSource)projectItem;
            if (projectItem is ProjectSource)            
			{
				var fullPath = scope String();
				mProjectSource.GetFullImportPath(fullPath);
                return Show(fullPath, silentFail);
			}
            return false;
        }        

		public void PathChanged(String path)
		{
			if (mFilePath != null)            
				IDEApp.sApp.mFileWatcher.RemoveWatch(mFilePath);
			mFilePath.Set(path);
			IDEApp.sApp.mFileWatcher.WatchFile(path);
			mWantsFullRefresh = true;

			bool wasSource = mIsBeefSource;
			mIsBeefSource = IDEApp.IsBeefFile(mFilePath);
			mIsSourceCode = IDEApp.IsSourceCode(mFilePath);

			if ((!mIsSourceCode) && (wasSource))
			{
				var ewd = mEditWidget.mEditWidgetContent.mData;
				for (int i < ewd.mTextLength)
				{
					ewd.mText[i].mDisplayFlags = 0;
					ewd.mText[i].mDisplayTypeId = 0;
				}
			}
			MarkDirty();
		}

		public void SplitView()
		{
			if (mPanelHeader != null)
				return;

			if (mSplitTopPanel != null)
				return;

			if ((mProjectSource == null) && (mFilePath == null))
			{
				//TODO: We don't allow splitting of new files
				return;
			}
			
			// User requested from menu
			if (mSplitter.mSplitPct <= 0)
				mSplitter.mSplitPct = 0.3f;

			mSplitTopPanel = new SourceViewPanel();
			mSplitTopPanel.mSplitBottomPanel = this;			
			mSplitter.mTopPanel = mSplitTopPanel;

			if (mProjectSource != null)
				mSplitTopPanel.Show(mProjectSource);
			else
				mSplitTopPanel.Show(mFilePath, false, mEditData);
			mSplitTopPanel.mDesiredVertPos = mEditWidget.mVertPos.v;
			mSplitTopPanel.Content.CursorLineAndColumn = Content.CursorLineAndColumn;

			QueueFullRefresh(false);
			mSplitTopPanel.QueueFullRefresh(false);

			AddWidget(mSplitTopPanel);			

			var ewc = (SourceEditWidgetContent)mEditWidget.mEditWidgetContent;
			var topEWC = (SourceEditWidgetContent)mSplitTopPanel.mEditWidget.mEditWidgetContent;

			for (var entry in ewc.mOrderedCollapseEntries)
			{
				if (!entry.mIsOpen)
				{
					topEWC.SetCollapseOpen(@entry.Index, false, true);
				}
			}
			topEWC.RehupLineCoords();

			ResizeComponents();

			// Match scroll positions			
			//mSplitTopPanel.mEditWidget.UpdateScrollbars();
			//mSplitTopPanel.mEditWidget.mVertScrollbar.ScrollTo(mEditWidget.mVertPos.v);
			//mSplitTopPanel.Content.CursorLineAndColumn = Content.CursorLineAndColumn;
		}

		public void UnsplitView()
		{
			//Debug.WriteLine("UnsplitView {0}\n", this);

			mSplitter.mTopPanel = null;
			mSplitTopPanel.Dispose();
			Widget.RemoveAndDelete(mSplitTopPanel);
			mSplitTopPanel = null;

			ResizeComponents();
		}

        void ShowOld(SourceViewPanel sourceViewPanel, int hotFileIdx)
        {
            mEditWidget = IDEApp.sApp.CreateSourceEditWidget();
            SetupEditWidget();

            var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

            sourceEditWidgetContent.SetOldVersionColors(true);

            String.NewOrSet!(mFilePath, sourceViewPanel.mFilePath);
            // We don't set mProjectSource because we don't want to actually perform a Classify, since
            //  this old version will reference NEW class info, which is a version mismatch
            //  mProjectSource = sourceViewPanel.mProjectSource;
            mIsSourceCode = sourceViewPanel.mIsSourceCode;
            mIsBeefSource = sourceViewPanel.mIsBeefSource;
            mJustShown = true;
            mWantsFullClassify = true;
            mWantsFullClassify = true;

            var projectSourceCompileInstance = IDEApp.sApp.mWorkspace.GetProjectSourceCompileInstance(sourceViewPanel.mProjectSource, hotFileIdx);
            mEditWidget.Content.AppendText(projectSourceCompileInstance.mSource);
            Debug.Assert(mEditWidget.Content.mData.mTextLength == projectSourceCompileInstance.mSource.Length);
            mEditWidget.Content.mData.mTextIdData.DuplicateFrom(ref projectSourceCompileInstance.mSourceCharIdData);
            mEditWidget.Content.mIsReadOnly = true;
            mHotFileIdx = hotFileIdx;
            mIsOldCompiledVersion = mHotFileIdx < IDEApp.sApp.mWorkspace.GetHighestCompileIdx();
			mCurrentVersionPanel = sourceViewPanel;

            mPanelHeader = new PanelHeader();
            if (mIsOldCompiledVersion)
                mPanelHeader.Label = "A previous version of this method is currently executing.  The new version will be used when next called.";
            else
                mPanelHeader.Label = "This method has changed since compiling.  Recompile to hot swap changes.";
            if (!sPreviousVersionWarningShown)
            {
                mPanelHeader.Flash();
                sPreviousVersionWarningShown = true;
            }
            var button = mPanelHeader.AddButton("Show &Current");
	        button.mOnMouseClick.Add(new (evt) =>
	            {
					ShowCurrent();
	                /*var app = IDEApp.sApp;
	                int callStackIdx = app.mDebugger.mSelectedCallStackIdx;
					float scrollTopDelta = mEditWidget.Content.GetCursorScreenRelY();
					bool isPaused = app.mDebugger.IsPaused();
	
					// Do another setPCLocation to make sure our cursor is at the PC position
					if (isPaused)
		                app.ShowPCLocation(callStackIdx, true);	
	                sourceViewPanel.mJustShown = true;
	                sourceViewPanel.CloseOldVersion();
					if (isPaused)
	                	app.ShowPCLocation(callStackIdx, true);
	                sourceViewPanel.mEditWidget.Content.SetCursorScreenRelY(scrollTopDelta + mPanelHeader.mHeight);*/
	            });
            AddWidget(mPanelHeader);

			InitSplitter();
        }

		public void ShowCurrent()
		{
			if (mOldVersionPanel != null)
			{
				mOldVersionPanel.ShowCurrent();
				return;
			}
			if (mCurrentVersionPanel == null)
				return;

			var app = IDEApp.sApp;
			int32 callStackIdx = app.mDebugger.mActiveCallStackIdx;
			float scrollTopDelta = mEditWidget.Content.GetCursorScreenRelY();
			bool isPaused = app.mDebugger.IsPaused();

			// Do another setPCLocation to make sure our cursor is at the PC position
			if (isPaused)
			    app.ShowPCLocation(callStackIdx, true);	
			mCurrentVersionPanel.mJustShown = true;
			mCurrentVersionPanel.CloseOldVersion();
			if (isPaused)
				app.ShowPCLocation(callStackIdx, true);
			mCurrentVersionPanel.mEditWidget.Content.SetCursorScreenRelY(scrollTopDelta + mPanelHeader.mHeight);
		}

        void CloseOldVersion()
        {
			if (mEditWidget == null)
			{
				// What to do?
			}	

            if (mOldVersionPanel != null)
            {
				if (mEditWidget != null)
                	mEditWidget.mVisible = true;
                if (mOldVersionPanel.mParent != null)
                    mOldVersionPanel.RemoveSelf();
                mOldVersionPanel.Dispose();
				BFApp.sApp.DeferDelete(mOldVersionPanel);
				//delete mOldVersionPanel;
                mOldVersionPanel = null;
				if (mWidgetWindow != null)
				{
					FocusEdit();
					ResizeComponents();
				}
            }
        }        

        public void ShowHotFileIdx(int hotFileIdx)
        {
			if (mLoadFailed)
				return;

            bool isOldCompiledVersion = (hotFileIdx != -1) && (hotFileIdx < IDEApp.sApp.mWorkspace.GetHighestCompileIdx());
            if ((mOldVersionPanel != null) && (mOldVersionPanel.mHotFileIdx == hotFileIdx) && (mOldVersionPanel.mIsOldCompiledVersion == isOldCompiledVersion))
                return;
            CloseOldVersion();
            //if (hotFileIdx != -1)
			if (isOldCompiledVersion)
            {
                mEditWidget.mVisible = false;
                mOldVersionPanel = new SourceViewPanel();
                if (mProjectSource != null)
                {
                    mOldVersionPanel.ShowOld(this, hotFileIdx);
                    AddWidget(mOldVersionPanel);
                    mOldVersionPanel.FocusEdit();
                    ResizeComponents();
                }
            }          
        }

        public void GetCursorPosition(out int32 line, out int32 column)
        {
            var lineAndCol = mEditWidget.Content.CursorLineAndColumn;
            line = lineAndCol.mLine;
            column = lineAndCol.mColumn;
        }

		int GetDrawLineNum(Breakpoint breakpoint)
		{
			int breakpointLineNum;

			/*if (mIsBeefSource)
				breakpointLineNum = breakpoint.GetLineNum();
			else
				breakpointLineNum = breakpoint.mLineNum;*/
			// Why did we have "mIsBeefSource" check? This broke our ability to 'move' the breakpoint down
			//  onto the actual executable line
			breakpointLineNum = breakpoint.GetLineNum();

			int drawLineNum = breakpointLineNum;
			
			// We want to use "IsBound" instead of "HasNativeBreakpoint" because otherwise when we hot-create a new method
			//  and then put a breakpoint on it then it'll remap as if it was bound
			if ((mIsBeefSource) && (breakpoint.mNativeBreakpoint != null))
			{
				int compileIdx = gApp.mWorkspace.GetHighestSuccessfulCompileIdx();
				//drawLineNum = RemapCompiledToActiveLine(/*breakpoint.mLineNum*/breakpointLineNum);
				if (compileIdx != -1)
				{
					int drawLineColumn = 0;
					RemapCompiledToActiveLine(compileIdx, ref drawLineNum, ref drawLineColumn);
					if (mHotFileIdx != -1)
					{
						drawLineNum = RemapActiveLineToHotLine(drawLineNum);
					}
				}
			}
			return drawLineNum;
		}

        public Breakpoint ToggleBreakpointAtCursor(Breakpoint.SetKind setKind = .Toggle, Breakpoint.SetFlags flags = .None, int threadId = -1)
        {
			var activePanel = GetActivePanel();
			if (activePanel != this)
				return activePanel.ToggleBreakpointAtCursor(setKind, flags, threadId);

            if (mOldVersionPanel != null)
            {                
                return null;
            }

			int lineIdx;
			int lineCharIdx;
			mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);
			return ToggleBreakpointAt(lineIdx, lineCharIdx, setKind, flags, threadId);
		}

		public Breakpoint ToggleBreakpointAt(int lineIdx, int lineCharIdx, Breakpoint.SetKind setKind = .Toggle, Breakpoint.SetFlags flags = .None, int threadId = -1)
		{
			var lineIdx;
			var lineCharIdx;

            DebugManager debugManager = IDEApp.sApp.mDebugger;

			HashSet<Breakpoint> breakpoints = scope .();

            bool hadBreakpoint = false;
			if (setKind != .Force)
			{
	            /*WithTrackedElementsAtCursor<Breakpoint>(IDEApp.sApp.mDebugger.mBreakpointList, scope [&] (breakpoint) =>
	                {
						BfLog.LogDbg("SourceViewPanel.ToggleBreakpointAtCursor deleting breakpoint\n");
	                    debugManager.DeleteBreakpoint(breakpoint);
	                    hadBreakpoint = true;
	                });*/

				for (var breakpointView in GetTrackedElementList())
				{
					var trackedElement = breakpointView.mTrackedElement;
					if (var breakpoint = trackedElement as Breakpoint)
					{
						int drawLineNum = GetDrawLineNum(breakpoint);
						if (drawLineNum == lineIdx)
						{
							hadBreakpoint = true;
							if (setKind == .Toggle)
							{
								BfLog.LogDbg("SourceViewPanel.ToggleBreakpointAtCursor deleting breakpoint\n");
								debugManager.DeleteBreakpoint(breakpoint);
							}
							else
								breakpoints.Add(breakpoint);
						}
					}
				}
			}

			Breakpoint newBreakpoint = null;
            if ((!hadBreakpoint) && (setKind != .MustExist))
            {
				RecordHistoryLocation();

                var editWidgetContent = mEditWidget.Content;
                int textPos = mEditWidget.Content.CursorTextPos - lineCharIdx;
                lineCharIdx = 0;

                // Find first non-space char
                while ((textPos < editWidgetContent.mData.mTextLength) && (((char8)editWidgetContent.mData.mText[textPos].mChar).IsWhiteSpace))
                {
                    textPos++;
                    lineCharIdx++;
                }

				int requestedActiveLineIdx = lineIdx;
                int curCompileIdx = IDEApp.sApp.mWorkspace.GetHighestCompileIdx();
                bool foundPosition = false;
				if (gApp.mDebugger.mIsRunning)
                	foundPosition = RemapActiveToCompiledLine(curCompileIdx, ref lineIdx, ref lineCharIdx);
				bool createNow = foundPosition || !mIsBeefSource; // Only be strict about Beef source

				String filePath = mAliasFilePath ?? mFilePath;
				if (filePath == null)
					return null;
                newBreakpoint = debugManager.CreateBreakpoint_Create(filePath, lineIdx, lineCharIdx, -1);
				newBreakpoint.mThreadId = threadId;
				debugManager.CreateBreakpoint_Finish(newBreakpoint, createNow);
                int newDrawLineNum = GetDrawLineNum(newBreakpoint);

                if (setKind != .Force)
				{
	                for (int32 breakIdx = 0; breakIdx < IDEApp.sApp.mDebugger.mBreakpointList.Count; breakIdx++)
	                {
	                    var checkBreakpoint = IDEApp.sApp.mDebugger.mBreakpointList[breakIdx];
	                    if ((checkBreakpoint != newBreakpoint) && 
	                        (checkBreakpoint.mFileName == newBreakpoint.mFileName))
	                    {
							int checkDrawLineNum = GetDrawLineNum(checkBreakpoint);
							if (checkDrawLineNum == newDrawLineNum)
							{
								BfLog.LogDbg("SourceViewPanel.ToggleBreakpointAtCursor duplicate breakpoint.  Deleting breakpoint\n");

		                        // This ended up on the same line as another breakpoint after binding. Hilite the other breakpoint to show there's already one there.
		                        debugManager.DeleteBreakpoint(newBreakpoint);
								newBreakpoint = null;
								var ewc = mEditWidget.mEditWidgetContent;
								LocatorAnim.Show(.Always, mEditWidget, ewc.mX + -GS!(15), ewc.mY + newDrawLineNum * ewc.GetLineHeight(0) + GS!(12));

								breakpoints.Add(checkBreakpoint);
								break;
							}
	                    }
	                }
				}

				// If we are hot compiling, and the binding of the breakpoint moves the breakpoint (down a few lines presumably), but the text between
				//  the requested position and the bound position has had changes that haven't been compiled in yet, then we undo the binding so we can
				//  rebind when we complete the hot compile
				if ((newBreakpoint != null) && (gApp.mDebugger.mIsRunning) && (mIsBeefSource) && (mProjectSource != null))
				{
					int boundLineNum = newBreakpoint.GetLineNum();
					int boundColumn = 0;
					RemapCompiledToActiveLine(curCompileIdx, ref boundLineNum, ref boundColumn);
					if (requestedActiveLineIdx != boundLineNum)
					{
						int startCheckIdx = editWidgetContent.GetTextIdx(requestedActiveLineIdx, 0);
						int endCheckIdx = editWidgetContent.GetTextIdx(boundLineNum, 0);

						var textIdData = editWidgetContent.mData.mTextIdData.GetPrepared();
						int32 startId = textIdData.GetIdAtIndex(startCheckIdx);
						int32 endId = textIdData.GetIdAtIndex(endCheckIdx);

						if (var projectSourceInstance = gApp.mWorkspace.GetProjectSourceCompileInstance(mProjectSource, curCompileIdx))
						{
							if (!textIdData.IsRangeEqual(projectSourceInstance.mSourceCharIdData, startId, endId))
							{
								newBreakpoint.DisposeNative();
								newBreakpoint.mLineNum = (.)requestedActiveLineIdx;
							}
						}
					}
				}

				if (newBreakpoint != null)
					breakpoints.Add(newBreakpoint);
            }

			if (((flags.HasFlag(.Configure)) || (flags.HasFlag(.Disable))) &&
				(!breakpoints.IsEmpty))
			{
				gApp.mBreakpointPanel.Update();
				gApp.mBreakpointPanel.SelectBreakpoints(breakpoints);
				if (flags.HasFlag(.Configure))
					gApp.mBreakpointPanel.ConfigureBreakpoints(mWidgetWindow);
				if (flags.HasFlag(.Disable))
					gApp.mBreakpointPanel.SetBreakpointsDisabled(null);
			}

			return newBreakpoint;
        }

        public void ToggleBookmarkAtCursor()
        {
            if ((mHotFileIdx != -1) || (mOldVersionPanel != null))
                return;
			if (mFilePath == null)
				return;

			var activePanel = GetActivePanel();
			if (activePanel != this)
			{
				activePanel.ToggleBookmarkAtCursor();
				return;
			}

            BookmarkManager bookmarkManager = IDEApp.sApp.mBookmarkManager;

            int lineIdx;
            int lineCharIdx;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);

            bool hadBookmark = false;
            
			for (var folder in IDEApp.sApp.mBookmarkManager.mBookmarkFolders)
			{
				WithTrackedElementsAtCursor<Bookmark>(folder.mBookmarkList, scope [&] (bookmark) =>
				    {
				        bookmarkManager.DeleteBookmark(bookmark);
				        hadBookmark = true;
				    });
			}
            
            if (!hadBookmark)
            {
                var editWidgetContent = mEditWidget.Content;
                int textPos = mEditWidget.Content.CursorTextPos - lineCharIdx;
                lineCharIdx = 0;

                // Find first non-space char8
                while ((textPos < editWidgetContent.mData.mTextLength) && (((char8)editWidgetContent.mData.mText[textPos].mChar).IsWhiteSpace))
                {
                    textPos++;
                    lineCharIdx++;
                }

                bookmarkManager.CreateBookmark(mFilePath, lineIdx, lineCharIdx);
            }
        }
        
        void RemapCompiledToActiveLine(int compileInstanceIdx, ref int lineNum, ref int column)
        {
			let projectSource = FilteredProjectSource;
            if (projectSource != null)
            {
                int32 char8Id = IDEApp.sApp.mWorkspace.GetProjectSourceCharId(projectSource, compileInstanceIdx, lineNum, column);                
                int char8Idx = mEditWidget.Content.GetCharIdIdx(char8Id);
                if (char8Idx != -1)
                {
                    mEditWidget.Content.GetLineCharAtIdx(char8Idx, out lineNum, out column);
                }
            }
        }

        bool RemapActiveToCompiledLine(int compileInstanceIdx, ref int lineNum, ref int column)
        {
			var projectSource = FilteredProjectSource;
			if (mIsOldCompiledVersion)
				projectSource = ((SourceViewPanel)mParent).FilteredProjectSource;

            if (projectSource != null)
            {
                int char8Id = mEditWidget.Content.GetSourceCharIdAtLineChar(lineNum, column);
                if (char8Id == 0)
                    return false;
                return IDEApp.sApp.mWorkspace.GetProjectSourceCharIdPosition(projectSource, compileInstanceIdx, char8Id, ref lineNum, ref column);
            }
            return true;
        }

        int RemapActiveLineToHotLine(int line)
        {
            if (mHotFileIdx == -1)
                return line;

            var activePanel = (SourceViewPanel)mParent;
            int32 char8Id = activePanel.mEditWidget.Content.GetSourceCharIdAtLineChar(line, -1);
            if (char8Id == -1)
                return -1;
            int remapIdx = mEditWidget.Content.GetCharIdIdx(char8Id);
            if (remapIdx == -1)
                return -1;
            int remapLine;
            int remapLineChar;
            mEditWidget.Content.GetLineCharAtIdx(remapIdx, out remapLine, out remapLineChar);
            return remapLine;
        }

		enum LineFlags : uint8
		{
			None,
			BreakpointCountMask = 0x7F,
			Boomkmark = 0x80
		}
		
		static float sDrawLeftAdjust = GS!(12);

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            // If we're trying to time autocomplete
            /*var bfSystem = IDEApp.sApp.mBfResolveSystem;
            if (bfSystem.mIsTiming)
            {
                bfSystem.StopTiming();
                bfSystem.DbgPrintTimings();
            }*/

            if (GetOldVersionPanel() != null)
                return;

			if (mLoadFailed)
				return;

            var ewc = (SourceEditWidgetContent)mEditWidget.Content;
			ewc.GetTextData();
			
            g.SetFont(IDEApp.sApp.mTinyCodeFont);

            using (g.PushClip(0, mEditWidget.mY, mWidth, mEditWidget.mHeight - GS!(20)))
            {
                using (g.PushTranslate(0, mEditWidget.mY + mEditWidget.Content.Y + GS!(2)))
                {
					float editX = GetEditX();

					float lineSpacing = ewc.mFont.GetLineSpacing();
					int cursorLineNumber = mEditWidget.mEditWidgetContent.CursorLineAndColumn.mLine;
					bool hiliteCurrentLine = mEditWidget.mHasFocus;

					var jumpEntry = ewc.mLineCoordJumpTable[Math.Clamp((int)(-mEditWidget.Content.Y / ewc.GetJumpCoordSpacing()), 0, ewc.mLineCoordJumpTable.Count - 1)];
					int lineStart = jumpEntry.min;
					jumpEntry = ewc.mLineCoordJumpTable[Math.Clamp((int)((-mEditWidget.Content.Y + mHeight) / ewc.GetJumpCoordSpacing()), 0, ewc.mLineCoordJumpTable.Count - 1)];
					int lineEnd = jumpEntry.max - 1;

					if (ewc.mLineRange != null)
					{
						lineStart = Math.Max(lineStart, ewc.mLineRange.Value.Start);
						lineEnd = Math.Min(lineEnd, ewc.mLineRange.Value.End);
						lineStart = Math.Min(lineStart, lineEnd);
					}

					int drawLineCount = lineEnd - lineStart + 1;

					ewc.RefreshCollapseRegions();
					if ((mCollapseRegionView.mLineStart != lineStart) || (mCollapseRegionView.mCollapseIndices.Count != drawLineCount) ||
						(mCollapseRegionView.mCollapseRevision != ewc.mCollapseParseRevision) || (mCollapseRegionView.mTextVersionId != ewc.mCollapseTextVersionId))
					{
						mCollapseRegionView.mLineStart = (.)lineStart;
						mCollapseRegionView.mCollapseIndices.Clear();
						Internal.MemSet(mCollapseRegionView.mCollapseIndices.GrowUnitialized(drawLineCount), 0, drawLineCount * sizeof(int32));
						mCollapseRegionView.mCollapseRevision = ewc.mCollapseParseRevision;
						mCollapseRegionView.mTextVersionId = ewc.mCollapseTextVersionId;

						List<int32> collapseStack = scope .(16);
						int32 curIdx = 0;
						for (int line in lineStart...lineEnd)
						{
							uint32 indexVal = 0;

							while (curIdx < ewc.mOrderedCollapseEntries.Count)
							{
								var entry = ewc.mOrderedCollapseEntries[curIdx];
								if (entry.mAnchorLine > line)
									break;
								if (!entry.mDeleted)
								{
									indexVal = (uint32)curIdx | CollapseRegionView.cStartFlag;
									collapseStack.Add(curIdx);
								}
								curIdx++;
							}

							while (!collapseStack.IsEmpty)
							{
								var entry = ewc.mOrderedCollapseEntries[collapseStack.Back];
								if (line < entry.mEndLine)
									break;
								if (indexVal == 0)
									indexVal = (uint32)collapseStack.Back | CollapseRegionView.cEndFlag;
								collapseStack.PopBack();
							}

							if ((indexVal == 0) && (!collapseStack.IsEmpty))
								indexVal = (uint32)collapseStack.Back | CollapseRegionView.cMidFlag;
							mCollapseRegionView.mCollapseIndices[line - lineStart] = indexVal;
							
						}
					}

					if ((mMousePos != null) && (mMousePos.Value.x >= mEditWidget.mX - GS!(13)) && (mMousePos.Value.x < mEditWidget.mX - GS!(0)) &&  (mMousePos.Value.y < mHeight - GS!(20)))
					{
						int lineClick = GetLineAt(0, mMousePos.Value.y);
						uint32 collapseVal = mCollapseRegionView.GetCollapseValue(lineClick);
						if (collapseVal != 0)
						{
							var entry = ewc.mOrderedCollapseEntries[collapseVal & CollapseRegionView.cIdMask];
							float startY = ewc.mLineCoords[entry.mAnchorLine];
							float endY = ewc.mLineCoords[entry.mEndLine + 1];

							using (g.PushColor(gApp.mSettings.mUISettings.mColors.mCurrentLineNumberHilite))
								g.FillRect(0, GS!(2) + startY, editX - GS!(10), endY - startY);

							hiliteCurrentLine = false;
						}
					}

					if (hiliteCurrentLine)
					{
						using (g.PushColor(gApp.mSettings.mUISettings.mColors.mCurrentLineNumberHilite))
						{
							int hiliteLineNum = cursorLineNumber;
							while (ewc.IsLineCollapsed(hiliteLineNum))
								hiliteLineNum--;
							g.FillRect(0, GS!(2) + ewc.mLineCoords[hiliteLineNum], editX - GS!(10), lineSpacing);
						}
					}

					if (lineEnd <= lineStart)
					{
						return;
					}

					LineFlags[] lineFlags = scope LineFlags[lineEnd - lineStart];

                    for (var breakpointView in GetTrackedElementList())
                    {
                        var trackedElement = breakpointView.mTrackedElement;
                        var breakpoint = trackedElement as Breakpoint;
                        var bookmark = trackedElement as Bookmark;
                        if (breakpoint != null)
                        {
							int drawLineNum = GetDrawLineNum(breakpoint);
							
							if ((drawLineNum < lineStart) || (drawLineNum >= lineEnd))
								continue;
							if (ewc.IsLineCollapsed(drawLineNum))
								continue;
							var curLineFlags = ref lineFlags[drawLineNum - lineStart];
							int breakpointCount = (.)(curLineFlags & .BreakpointCountMask);
							curLineFlags++;

							float iconX = Math.Max(GS!(-2), mEditWidget.mX - GS!(24) - sDrawLeftAdjust) + breakpointCount*-GS!(2);
							float iconY = 0 + ewc.mLineCoords[drawLineNum] + (lineSpacing - DarkTheme.sUnitSize + GS!(5)) / 2;

							// Just leave last digit visible
							/*using (g.PushColor(0xFF595959))
								g.FillRect(4, iconY, editX - 14, 20);*/

							using (g.PushColor((breakpointCount % 2 == 0) ? 0xFFFFFFFF : 0xFFC0C0C0))
	                            using (g.PushTranslate(iconX, iconY))
	                            {                                
	                                breakpoint.Draw(g, mIsOldCompiledVersion);
	                            }
                        }
                        else if (bookmark != null)
                        {
							
                            if (mHotFileIdx == -1)
							{
								int32 drawLineNum = bookmark.mLineNum;
								if ((drawLineNum < lineStart) || (drawLineNum >= lineEnd))
									continue;
								if (ewc.IsLineCollapsed(drawLineNum))
									continue;
								//hadLineIcon[drawLineNum - lineStart] = true;
								Image image = DarkTheme.sDarkTheme.GetImage(bookmark.mIsDisabled ? .IconBookmarkDisabled : .IconBookmark);
                                g.Draw(image, Math.Max(GS!(-5), mEditWidget.mX - GS!(30) - sDrawLeftAdjust),
									0 + bookmark.mLineNum * lineSpacing);

								var curLineFlags = ref lineFlags[drawLineNum - lineStart];
								curLineFlags |= .Boomkmark;
								//FAIL
							}
                        }
                        
                        ////
                        /*var historyEntry = trackedElement as HistoryEntry;
                        if (historyEntry != null)
                        {                                                        
                            if (mHotFileIdx == -1)
                            {
                                float xPos;
                                float yPos;
                                darkEditWidgetContent.GetTextCoordAtLineChar(historyEntry.mLineNum, historyEntry.mColumn, out xPos, out yPos);

                                using (g.PushColor(0x60FFFFFF))
                                    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.IconBookmark), mEditWidget.mX + xPos,
                                            0 + historyEntry.mLineNum * lineSpacing);
                            }
                        }*/
                    }

					if ((gApp.mSettings.mEditorSettings.mShowLineNumbers) && (mEmbedKind == .None))
					{
						String lineStr = scope String(16);
						using (g.PushColor(0x80FFFFFF))
						{
						    for (int lineIdx = lineStart; lineIdx < lineEnd; lineIdx++)
						    {
								float drawHeight = ewc.mLineCoords[lineIdx + 1] - ewc.mLineCoords[lineIdx];
								if (drawHeight < lineSpacing * 0.25f)
									continue;

								lineStr.Clear();

								int maxLineChars = Int32.MaxValue;

								let curLineFlags = lineFlags[lineIdx - lineStart];
								if ((uint8)(curLineFlags & .BreakpointCountMask) > 0)
									maxLineChars = 1;
								else if (curLineFlags.HasFlag(.Boomkmark))
									maxLineChars = 2;

								switch (maxLineChars)
								{
								case 0:
								case 1: lineStr.AppendF("{0}", (lineIdx + 1) % 10);
								case 2: lineStr.AppendF("{0}", (lineIdx + 1) % 100);
								default: lineStr.AppendF("{0}", lineIdx + 1);
								}
						        g.DrawString(lineStr, 0, GS!(2) + ewc.mLineCoords[lineIdx], FontAlign.Right, editX - GS!(14));
						    }
						}
					}
						
				    for (int lineIdx = lineStart; lineIdx < lineEnd; lineIdx++)
				    {
						int collapseLookup = lineIdx - mCollapseRegionView.mLineStart;
						if ((collapseLookup >= 0) && (collapseLookup < mCollapseRegionView.mCollapseIndices.Count))
						{
							float drawHeight = ewc.mLineCoords[lineIdx + 1] - ewc.mLineCoords[lineIdx];
							if (drawHeight < lineSpacing * 0.25f)
								continue;

							float boxAdjustTop = Math.Floor((lineSpacing - DarkTheme.sUnitSize)/2);
							float boxAdjustBot = Math.Ceiling((lineSpacing - DarkTheme.sUnitSize)/2);

							uint32 collapseIdx = mCollapseRegionView.mCollapseIndices[lineIdx - mCollapseRegionView.mLineStart];
							if ((collapseIdx & CollapseRegionView.cStartFlag) != 0)
							{
								var entry = ewc.mOrderedCollapseEntries[collapseIdx & CollapseRegionView.cIdMask];
								g.Draw(DarkTheme.sDarkTheme.GetImage(entry.mIsOpen ? .CollapseOpened : .CollapseClosed), editX - GS!(16), ewc.mLineCoords[lineIdx] + boxAdjustTop + GS!(2));

								int nextCollapseIdx = (collapseIdx & CollapseRegionView.cIdMask) + 1;
								if ((entry.mIsOpen) && (nextCollapseIdx < ewc.mOrderedCollapseEntries.Count))
								{
									// Draw line between two collapse boxes in a row
									var nextEntry = ewc.mOrderedCollapseEntries[nextCollapseIdx];
									if (nextEntry.mAnchorLine == lineIdx + 1)
									{
										using (g.PushColor(0xFFA5A5A5))
											g.FillRect(editX - (int)GS!(7.5f), ewc.mLineCoords[lineIdx] + boxAdjustTop + (int)GS!(15.5f), (int)GS!(1.5f), (lineSpacing - GS!(10)) + GS!(2));
									}
								}
							}
							else if ((collapseIdx & CollapseRegionView.cEndFlag) != 0)
							{
								using (g.PushColor(0xFFA5A5A5))
								{
									g.FillRect(editX - (int)GS!(7.5f), ewc.mLineCoords[lineIdx] - (int)GS!(0.5f), (int)GS!(1.5f), lineSpacing);
									g.FillRect(editX - (int)GS!(7.5f), ewc.mLineCoords[lineIdx] + lineSpacing - (int)GS!(1.5f), GS!(5), (int)GS!(1.5f));
								}
							}
							else if (collapseIdx != 0)
							{
								using (g.PushColor(0xFFA5A5A5))
								{
									g.FillRect(editX - (int)GS!(7.5f), ewc.mLineCoords[lineIdx] - boxAdjustBot - GS!(5), (int)GS!(1.5f), lineSpacing + boxAdjustBot + boxAdjustTop + (int)GS!(12f));
								}
							}
						}
				    }

                    if (IDEApp.sApp.mExecutionPaused)
                    {
                        int addr;
                        String fileName = scope String(Path.[Friend]MaxPath);
                        int hotIdx;
                        int defLineStart;
                        int defLineEnd;
                        int lineNum;
                        int column;
                        int language;
                        int stackSize;
                        DebugManager.FrameFlags flags;
                        IDEApp.sApp.mDebugger.GetStackFrameInfo(IDEApp.sApp.mDebugger.mActiveCallStackIdx, null, out addr, fileName, out hotIdx, out defLineStart, out defLineEnd, out lineNum, out column, out language, out stackSize, out flags);
                        IDEUtils.FixFilePath(fileName);
						int hashPos = fileName.IndexOf('#');
						if (hashPos != -1)
							fileName.RemoveToEnd(hashPos);
                        if (FileNameMatches(fileName))
                        {                            
                            RemapCompiledToActiveLine(hotIdx, ref lineNum, ref column);
							
                            Image img;
                            if (IDEApp.sApp.mDebugger.mActiveCallStackIdx == 0)
							{
								if (flags.HasFlag(.Optimized))
									img = DarkTheme.sDarkTheme.GetImage(.LinePointer_Opt);
								else
									img = DarkTheme.sDarkTheme.GetImage(.LinePointer);
							}
							else
								img = DarkTheme.sDarkTheme.GetImage(.LinePointer_Prev);

							// If our step/continue doesn't actually change the line-pointer position, then we
							//  want to give just the littlest 'flash' of the cursor to indicate to the user
							//  that something actually happened. The most common case is a breakpoint that
							//  gets hit over and over F5 (continue).
							bool doDraw = false;
							if ((mLinePointerDrawData.mImage != img) ||
								(mLinePointerDrawData.mLine != lineNum))
							{
								mLinePointerDrawData.mImage = img;
								mLinePointerDrawData.mLine = (.)lineNum;
								doDraw = true;
							}
							else if ((mLinePointerDrawData.mDebuggerContinueIdx == gApp.mDebuggerContinueIdx) ||
								(gApp.mUpdateCnt - mLinePointerDrawData.mUpdateCnt >= 3))
							{
								doDraw = true;
							}

							if ((lineNum < lineStart) || (lineNum >= lineEnd))
								doDraw = false;

							if (doDraw)
							{
								mLinePointerDrawData.mUpdateCnt = gApp.mUpdateCnt;
								mLinePointerDrawData.mDebuggerContinueIdx = gApp.mDebuggerContinueIdx;
								g.Draw(img, mEditWidget.mX - GS!(20) - sDrawLeftAdjust,
									0 + ewc.GetLineY(lineNum, 0));
							}

							if (mMousePos != null && mIsDraggingLinePointer)
							{
								int dragLineNum = GetLineAt(0, mMousePos.Value.y);
								if (dragLineNum >= 0 && dragLineNum != lineNum)
								{
									using (g.PushColor(0x7FFFFFFF))
										g.Draw(img, mEditWidget.mX - GS!(20) - sDrawLeftAdjust,
											0 + ewc.GetLineY(dragLineNum, 0));
								}
							}
                        }
                    }
                }
            }

			bool drawLock = (mSplitBottomPanel == null) && (mEmbedKind == .None);
			if (drawLock)
			{
				IDEUtils.DrawLock(g, mEditWidget.mX - GS!(20), mHeight - GS!(20), IsReadOnly, mLockFlashPct);
			}

            /*using (g.PushColor(0x80FF0000))
                g.FillRect(0, 0, mWidth, mHeight);*/
        }

        /*void UpdateCharData()
        {
            var bfSystem = IDEApp.sApp.mBfResolveSystem;
            bfSystem.PerfZoneStart("UpdateCharData");

            // Inject new char8 attributes into text
            uint highestSrcCharId = 0;
            for (int i = 0; i < mProcessingCharData.Length; i++)
            {
                uint char8Id = mProcessingCharData[i].mCharId;
                if (char8Id > highestSrcCharId)
                    highestSrcCharId = char8Id;
            }

            int srcIdx = 0;
            int destIdx = 0;
            var destText = mEditWidget.Content.mText;
            int destTextLength = mEditWidget.Content.mTextLength;
            while ((srcIdx < mProcessingCharData.Length) && (destIdx < destTextLength))
            {
                if (destText[destIdx].mCharId > highestSrcCharId)
                {
                    // This is new text since we did the background compile, skip
                    destIdx++;
                    continue;
                }

                if (mProcessingCharData[srcIdx].mCharId != destText[destIdx].mCharId)
                {
                    // Id doesn't match, character must have been deleted
                    srcIdx++;
                    continue;
                }

                Debug.Assert(destText[destIdx].mChar == mProcessingCharData[srcIdx].mChar);

                if (destText[destIdx].mDisplayPassId == (byte)SourceDisplayId.AutoComplete)
                {
                    // Autocomplete beat us to it
                    destText[destIdx].mDisplayPassId = (byte)SourceDisplayId.Cleared;                    
                }
                else
                {
                    byte prevFlags = destText[destIdx].mDisplayFlags;
                    destText[destIdx] = mProcessingCharData[srcIdx];
                    destText[destIdx].mDisplayFlags = (byte)
                        ((prevFlags & (byte)SourceElementFlags.EditorFlags_Mask) |
                        (destText[destIdx].mDisplayFlags & (byte)SourceElementFlags.CompilerFlags_Mask));                    
                }
                srcIdx++;
                destIdx++;                    
            }            

            mProcessingCharData = null;            

            bfSystem.PerfZoneEnd();
        }*/

        void UpdateCharData(ref EditWidgetContent.CharData[] charData, ref IdSpan charIdData, uint8 replaceFlags, bool flagsOnly)
        {
			//Debug.WriteLine("UpdateCharData: {0}", char8Data);

			if (mEditWidget == null)
				return;

			scope AutoBeefPerf("SourceViewPanel.UpdateCharData");

			charIdData.Prepare();
			var editTextIdData = ref mEditWidget.Content.mData.mTextIdData;
			editTextIdData.Prepare();

            // Inject new char8 attributes into text
            int32 highestSrcCharId = 0;
            int32 srcCharId = 1;

            //string dbgStr = "";
            int srcEncodeIdx = 0;
            //dbgStr += "Src: ";
            while (true)
            {
                int32 cmd = Utils.DecodeInt(charIdData.mData, ref srcEncodeIdx);                                
                if (cmd > 0)
                {
                    srcCharId = cmd;
                }
                else
                {
                    srcCharId += -cmd;
                    highestSrcCharId = Math.Max(highestSrcCharId, srcCharId - 1);
                    if (cmd == 0)
                        break;
                }
                //dbgStr += " " + cmd;                
            }

            int destEncodeIdx = 0;
            /*dbgStr += "  Dest: ";
            while (true)
            {
                int cmd = Utils.DecodeInt(mProcessCharIdData, ref destEncodeIdx);
                if (cmd == 0)
                    break;
                dbgStr += " " + cmd;                
            }*/
            //Debug.WriteLine(dbgStr);

            int32 srcIdx = 0;
            int32 destIdx = 0;
            var destText = mEditWidget.Content.mData.mText;
            int32 destTextLength = mEditWidget.Content.mData.mTextLength;

            srcEncodeIdx = 0;            
            int32 srcSpanLeft = 0;
            srcCharId = 1;
            destEncodeIdx = 0;
            int32 destSpanLeft = 0;
            int32 destCharId = 1;

            while ((srcIdx < charData.Count) && (destIdx < destTextLength))
            {
                while (srcSpanLeft == 0)
                {
                    int32 cmd = Utils.DecodeInt(charIdData.mData, ref srcEncodeIdx);
                    if (cmd > 0)
                        srcCharId = cmd;
                    else
                        srcSpanLeft = -cmd;
                }

                while (destSpanLeft == 0)
                {
                    int32 cmd = Utils.DecodeInt(editTextIdData.mData, ref destEncodeIdx);
                    if (cmd > 0)
                        destCharId = cmd;
                    else
                        destSpanLeft = -cmd;
                }
                
                if (destCharId > highestSrcCharId)
                {
                    // This is new text since we did the background compile, skip
                    destIdx++;
                    destSpanLeft--;
                    destCharId++;
                    continue;
                }
                
                if (srcCharId != destCharId)
                {
                    // Id doesn't match, character must have been deleted
                    srcIdx++;
                    srcSpanLeft--;
                    srcCharId++;
                    continue;
                }
                
                Debug.Assert(destCharId == srcCharId);
				Debug.Assert(destText[destIdx].mChar == charData[srcIdx].mChar);
				
                if (destText[destIdx].mDisplayPassId == (uint8)SourceDisplayId.AutoComplete)
                {
                    // Autocomplete beat us to it
                    destText[destIdx].mDisplayPassId = (uint8)SourceDisplayId.Cleared;
                }
				else if (charData[srcIdx].mDisplayPassId == (uint8)SourceDisplayId.SkipResult)
				{
					//
				}
                else
                {
                    uint8 prevFlags = destText[destIdx].mDisplayFlags;
					if (!flagsOnly)
                    	destText[destIdx] = charData[srcIdx];
                    destText[destIdx].mDisplayFlags = (uint8)
                            ((prevFlags & ~replaceFlags) |
                            (charData[srcIdx].mDisplayFlags & replaceFlags));
                }

                srcIdx++;
                srcSpanLeft--;
                srcCharId++;
                destIdx++;
                destSpanLeft--;
                destCharId++;
            }

			delete charData;
            charData = null;
            charIdData.Dispose();
        }

        public override void ShowQuickFind(bool isReplace)
        {
			//RecordHistoryLocation();
			var activePanel = GetActivePanel();
			if (Content.Data.mCurQuickFind != null)
			{
				Content.Data.mCurQuickFind.Close();
			}
			
			if (activePanel != this)
			{
				activePanel.ShowQuickFind(isReplace);
				return;
			}

            /*if (mOldVersionPanel != null)
            {
                mOldVersionPanel.ShowQuickFind(isReplace);
                return;
            }*/

            if (mRenameSymbolDialog != null)            
                mRenameSymbolDialog.Close();            
            
            base.ShowQuickFind(isReplace);

			Content.Data.mCurQuickFind = mQuickFind;
        }

        public override void FindNext(int32 dir = 1)
        {
			var activePanel = GetActivePanel();
			if (activePanel != this)
			{
				activePanel.FindNext();
				return;
			}

            if (mOldVersionPanel != null)
            {
                mOldVersionPanel.FindNext(dir);
                return;
            }

            base.FindNext(dir);
        }

        public void ReformatDocument(bool ignoreSelection = false)
        {
			if (!mIsBeefSource)
				return;

            var bfSystem = IDEApp.sApp.mBfResolveSystem;
			if (bfSystem == null)
				return;
            var parser = bfSystem.CreateEmptyParser(null);
			defer delete parser;
            var text = scope String();
            mEditWidget.GetText(text);
            parser.SetSource(text, mFilePath, -1);
            var passInstance = bfSystem.CreatePassInstance();
			defer delete passInstance;
            parser.Parse(passInstance, false);
            parser.Reduce(passInstance);
			mWantsParserCleanup = true;
            bool performSanityCheck = false;
#if !DEBUG
            performSanityCheck = false;
#endif

            if (performSanityCheck)
            {
                int32[] char8Mapping;
                var newText = scope String();
                parser.Reformat(-1, -1, out char8Mapping, newText); // Just reprint without reformatting first            

                int32 lineNum = 0;
                int32 lineStart = 0;
                for (int32 i = 0; i < Math.Min(newText.Length, text.Length); i++)
                {
                    if (text[i] == '\n')
                    {
                        lineNum++;
                        lineStart = i + 1;
                    }
                    if (text[i] != newText[i])
                    {
                        IDEApp.sApp.OutputLine("Reformat had a difference at line {0}", (lineNum + 1));

                        int nextCr = text.IndexOf('\n', lineStart);
                        IDEApp.sApp.OutputLine(" {0}", scope String(text, lineStart, nextCr - lineStart));

                        nextCr = newText.IndexOf('\n', lineStart);
                        IDEApp.sApp.OutputLine(" {0}", scope String(newText, lineStart, nextCr - lineStart));

                        break;
                    }
                }
            }

            if ((mEditWidget.Content.HasSelection()) && (!ignoreSelection))
                parser.ReformatInto(mEditWidget, mEditWidget.Content.mSelection.Value.MinPos, mEditWidget.Content.mSelection.Value.MaxPos);
            else
                parser.ReformatInto(mEditWidget, 0, text.Length);

            //mEditWidget.SetText(newText);
        }

        public void GotoLine()
        {                        
            GoToLineDialog aDialog = new GoToLineDialog("Go To Line", StackStringFormat!("Line Number ({0}-{1})", 1, mEditWidget.Content.GetLineCount()));
            aDialog.Init(this);                        
            aDialog.PopupWindow(mWidgetWindow);
        }

        public void GotoMethod()
        {
			if (mSplitBottomPanel != null)
				mSplitBottomPanel.GotoMethod();
			else
            	mNavigationBar.ShowDropdown();            
        }

		public void FixitAtCursor()
		{
			if (!mIsBeefSource)
				return;

			//TODO: Make better, do async, etc...			
			DoClassify(ResolveType.GetFixits, null, true);
		}

        public void ShowSymbolReferenceHelper(SymbolReferenceHelper.Kind symbolReferenceKind)
        {
            if (gApp.mSymbolReferenceHelper != null)
			{
                gApp.mSymbolReferenceHelper.Close();
			}

            SymbolReferenceHelper symbolReferenceHelper = new SymbolReferenceHelper();
			//symbolReferenceHelper.[Friend]GCMarkMembers();
			//Debug.WriteLine("SymbolReferenceHelper {0} ResolveParams:{1}", symbolReferenceHelper, symbolReferenceHelper.[Friend]mResolveParams);

            mRenameSymbolDialog = symbolReferenceHelper;
            gApp.mSymbolReferenceHelper = symbolReferenceHelper;
			//BfResolveCompiler.mThreadWorkerHi.WaitForBackground(); // We need to finish up anything on the hi thread worker so we can queue this
            symbolReferenceHelper.Init(this, symbolReferenceKind);
            if (!symbolReferenceHelper.mFailed)
            {
                AddWidget(symbolReferenceHelper);
                ResizeComponents();
            }
            else
            {
                mRenameSymbolDialog?.Close();
            }

			if ((symbolReferenceKind == .Rename) && (let autoComplete = GetAutoComplete()))
				autoComplete.Close();
        }

        public void RenameSymbol()
        {
			if (mQuickFind != null)
				mQuickFind.Close();

			var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
			if (!sourceEditWidgetContent.CheckReadOnly())
            	ShowSymbolReferenceHelper(SymbolReferenceHelper.Kind.Rename);
        }

		public void FindAllReferences()
		{
			ShowSymbolReferenceHelper(SymbolReferenceHelper.Kind.FindAllReferences);
		}

        public override void SetVisible(bool visible)
        {
            base.SetVisible(visible);
            mWantsFullClassify = true;
            mWantsClassifyAutocomplete = false;
        }

        public override void RecordHistoryLocation(bool ignoreIfClose = false)
        {
            var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            sourceEditWidgetContent.RecordHistoryLocation(ignoreIfClose);
        }        

		bool CheckLeftMouseover()
		{
			if (mWidgetWindow == null)
				return false;
			if (!mWidgetWindow.mHasMouseInside)
				return false;

			Point mousePos;
			bool mouseoverFired = DarkTooltipManager.CheckMouseover(this, 10, out mousePos);

			String tooltipStr = scope String();

			if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mRelWidget == this))
				mouseoverFired = true;

			if (!mouseoverFired)
				return false;

			float leftAdjust = GS!(12);

			float editX = GetEditX();
			if ((mousePos.x < editX - GS!(24) - leftAdjust) || (mousePos.x > editX - GS!(5) - leftAdjust))
				return false;

			int mouseLine = GetLineAt(mousePos.x, mousePos.y);

			for (var breakpointView in GetTrackedElementList())
			{
			    var trackedElement = breakpointView.mTrackedElement;
			    var breakpoint = trackedElement as Breakpoint;
			    //var bookmark = trackedElement as Bookmark;

				if (breakpoint != null)
				{
					int breakpointLineNum;
					if (mIsBeefSource)
				    	breakpointLineNum = breakpoint.GetLineNum();
					else
						breakpointLineNum = breakpoint.mLineNum;
					int drawLineNum = breakpointLineNum;

					if (drawLineNum == mouseLine)
					{
						if (!tooltipStr.IsEmpty)
							tooltipStr.Append("\n\n");

						breakpoint.ToString_Location(tooltipStr);

						if (breakpoint.mThreadId != -1)
							tooltipStr.AppendF("\nThread: {0}", breakpoint.mThreadId);

						tooltipStr.Append("\nHits: ");
						breakpoint.ToString_HitCount(tooltipStr);

						if (breakpoint.mLogging != null)
							tooltipStr.Append("\nLog: ", breakpoint.mLogging);
					}	
				}				  
			}

			if (tooltipStr.IsEmpty)
				return false;

			if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mText == tooltipStr))
				return true;

			DarkTooltipManager.ShowTooltip(tooltipStr, this, mousePos.x, mousePos.y);
			return true;
		}

		public void GetDebugExpressionAt(int textIdx, String debugExpr)
		{
			BfSystem bfSystem = IDEApp.sApp.mBfResolveSystem;
			let parser = bfSystem.CreateEmptyParser(null);

			var text = scope String();
			mEditWidget.GetText(text);
			parser.SetSource(text, mFilePath, -1);
			parser.SetAutocomplete(textIdx);
			let passInstance = bfSystem.CreatePassInstance("Mouseover");
			parser.SetCompleteParse();
			parser.Parse(passInstance, !mIsBeefSource);
			parser.Reduce(passInstance);
			if (parser.GetDebugExpressionAt(textIdx, debugExpr))
			{
			    if (debugExpr.StartsWith("`"))
					debugExpr[0] = ':';
			    else if (debugExpr.StartsWith(":"))
			        debugExpr.Clear();
			}
			delete passInstance;
			delete parser;
		}

		public void UpdateMouseover(bool mouseoverFired, bool mouseInbounds, int line, int lineChar, bool isManual = false)
		{
#unwarn
		    CompilerBase compiler = ResolveCompiler;

			bool hasClangHoverErrorData = false;
			var editWidgetContent = mEditWidget.Content;

#if IDE_C_SUPPORT
			hasClangHoverErrorData = mClangHoverErrorData != null;
#endif
		    
	        String debugExpr = null;

	        BfSystem bfSystem = IDEApp.sApp.mBfResolveSystem;                
	        BfPassInstance passInstance = null;
	        BfParser parser = null;
	        int textIdx = -1;

	        bool isOverMessage = false;
	        if (mouseInbounds)
	        {
	            textIdx = editWidgetContent.GetTextIdx(line, lineChar);
	            
	            int startIdx = editWidgetContent.GetTextIdx(line, lineChar);

	            uint8 checkFlags = (uint8)SourceElementFlags.Error;
	            if (!IDEApp.sApp.mDebugger.mIsRunning)
	            {
	                // Prioritize debug info over warning when we are debugging
	                checkFlags |= (uint8)SourceElementFlags.Warning; 
	            }
	            if ((editWidgetContent.mData.mText[startIdx].mDisplayFlags & checkFlags) != 0)
	                isOverMessage = true;

				bool doSimpleMouseover = false;

	            if ((editWidgetContent.mSelection != null) &&
	                (textIdx >= editWidgetContent.mSelection.Value.MinPos) &&
	                (textIdx < editWidgetContent.mSelection.Value.MaxPos))
	            {
	                debugExpr = scope:: String();
	                editWidgetContent.GetSelectionText(debugExpr);
	            }
	            else if (mIsBeefSource)
	            {
					if (bfSystem != null)
					{
						parser = bfSystem.CreateEmptyParser(null);
					
	                    var text = scope String();
	                    mEditWidget.GetText(text);
	                    parser.SetSource(text, mFilePath, -1);
	                    parser.SetAutocomplete(textIdx);
	                    passInstance = bfSystem.CreatePassInstance("Mouseover");
						parser.SetCompleteParse();
	                    parser.Parse(passInstance, !mIsBeefSource);
	                    parser.Reduce(passInstance);
	                    debugExpr = scope:: String();
	                    if (parser.GetDebugExpressionAt(textIdx, debugExpr))
	                    {
	                        if (debugExpr.StartsWith("`"))
								debugExpr[0] = ':';
	                        else if (debugExpr.StartsWith(":"))
	                            debugExpr = null;
	                    }
					}
	            }
	            else if (mIsClang)
					doSimpleMouseover = true;

				if (doSimpleMouseover)
				SimpleMouseover: do
	            {
	                int endIdx = startIdx;
	                String sb = scope:: String();
	                bool isInvalid = false;
	                bool prevWasSpace = false;

					if (editWidgetContent.mData.mText[startIdx].mChar.IsWhiteSpace)
						break;

	                startIdx--;
	                while (startIdx > 0)
	                {
	                    var char8Data = editWidgetContent.mData.mText[startIdx];
	                    if (char8Data.mDisplayTypeId == (uint8)SourceElementType.Comment)
	                    {
	                        if (startIdx == endIdx - 1)
	                        {
	                            // Inside comment
	                            isInvalid = true;
	                            break;
	                        }
	                    }
	                    else
	                    {
	                        char8 c = (char8)char8Data.mChar;
	                        if ((c == ' ') || (c == '\t'))
	                        {
	                            // Ignore
	                            prevWasSpace = true;
	                        }
							else if (c == '\n')
							{
								break;
							}
	                        else
	                        {
	                            if (c == '>')
	                            {
	                                // Is this "->"?
	                                if ((startIdx > 1) && ((char8)editWidgetContent.mData.mText[startIdx - 1].mChar == '-'))
	                                {
	                                    sb.Insert(0, "->");
	                                    startIdx--;
	                                }
	                                else
	                                    break;
	                            }
	                            else if (c == ':')
	                            {
	                                // Is this "::"?
	                                if ((startIdx > 1) && ((char8)editWidgetContent.mData.mText[startIdx - 1].mChar == ':'))
	                                {
	                                    sb.Insert(0, "::");
	                                    startIdx--;
	                                }
	                                else
	                                    break;
	                            }
	                            else if (c == '.')
	                                sb.Insert(0, c);
	                            else if ((c == '_') || (c.IsLetterOrDigit))
	                            {
	                                if (prevWasSpace)
	                                    break;
	                                sb.Insert(0, c);
	                            }
	                            else
	                                break;

	                            prevWasSpace = false;
	                        }
	                    }
	                    startIdx--;
	                }
					
	                prevWasSpace = false;
	                while ((endIdx < editWidgetContent.mData.mTextLength) && (endIdx > startIdx))
	                {
	                    var char8Data = editWidgetContent.mData.mText[endIdx];
	                    if (char8Data.mDisplayTypeId == (uint8)SourceElementType.Comment)
	                    {
	                        // Ignore
	                        prevWasSpace = true;
	                    }
	                    else
	                    {
	                        char8 c = (char8)char8Data.mChar;
	                        if ((c == ' ') || (c == '\t'))
	                        {
	                            // Ignore
	                            prevWasSpace = true;
	                        }
	                        else if ((c == '_') || (c.IsLetterOrDigit))
	                        {
	                            if (prevWasSpace)
	                                break;

	                            sb.Append(c);
	                        }
	                        else
	                            break;

	                        prevWasSpace = false;
	                    }
	                    endIdx++;
	                }

	                if (!isInvalid)
	                    debugExpr = sb;
	            }
	        }

	        bool triedShow = false;

	        if (mHoverWatch != null)
	        {
	            if (debugExpr != null)                        
	            {
	                if (mHoverWatch.mEvalString != debugExpr)
					{
	                    mHoverWatch.Close();
						mHoverWatch = null;
					}
	                else
	                    triedShow = true;
	            }
	        }

	        if (((mHoverWatch == null) && (mouseoverFired)) || (debugExpr == null) || (hasClangHoverErrorData) || (mHoverResolveTask?.mResult != null))
	        {
	            float x;
	            float y;
	            editWidgetContent.GetTextCoordAtLineChar(line, lineChar, out x, out y);

				bool hasHoverWatchOpen = (mHoverWatch != null) && (mHoverWatch.mListView != null);
	            if (mHoverWatch == null)
				{
	                mHoverWatch = new HoverWatch();
				}

	            if (debugExpr != null)
	                triedShow = true;

				bool didShow = false;
				//if ((debugExpr == "var") || (debugExpr == "let"))

				String origDebugExpr = null;

				bool handlingHoverResolveTask = false;

				if ((debugExpr != null) || (isOverMessage))
				{
					if (mHoverResolveTask != null)
					{
						if (mHoverResolveTask.mCursorPos != textIdx)
						{
							DeleteAndNullify!(mHoverResolveTask);
						}
					}

					if ((!String.IsNullOrEmpty(mHoverResolveTask?.mResult)))
					{
						origDebugExpr = scope:: String();
						origDebugExpr.Set("");

						debugExpr.Set(mHoverResolveTask.mResult);

						if (debugExpr.StartsWith(':'))
						{
							int docsPos = debugExpr.IndexOf('\x03');
							if (docsPos != -1)
							{
								String docs = scope String()..Append(debugExpr, docsPos + 1);
								debugExpr.RemoveToEnd(docsPos);

								DocumentationParser docParser = scope .(docs);
								var showString = docParser.ShowDocString;
								if (!String.IsNullOrEmpty(showString))
								{
									debugExpr.AppendF("\n{}", Font.EncodeColor(0xFFC0C0C0));
									debugExpr.Append(showString);
								}
							}
						}
					}

					if (mHoverResolveTask?.mResult != null)
					{
						handlingHoverResolveTask = true;
						DeleteAndNullify!(mHoverResolveTask);
					}

					if (!triedShow)
					{
				        mHoverWatch.Show(this, x, y, debugExpr, debugExpr);
						triedShow = true;
					}
				}

	            /*if ((!didShow) &&
					((debugExpr == null) || (isOverMessage) || (!mHoverWatch.Show(this, x, y, origDebugExpr ?? debugExpr, debugExpr))))*/
				if (!didShow)
	            {
					if ((debugExpr != null) && (!isOverMessage))
					{
						if ((mHoverWatch.mIsShown) && (!debugExpr.StartsWith(':')))
							didShow = true;
						else
							didShow = mHoverWatch.Show(this, x, y, origDebugExpr ?? debugExpr, debugExpr);
					}

					if ((handlingHoverResolveTask) && (mHoverWatch.mIsShown))
					{
						// Keep existing content
						didShow = true;
					}

					if ((mHoverResolveTask == null) &&
						((debugExpr == null) || (!debugExpr.StartsWith(':'))))
					{

						if (((!gApp.mDebugger.mIsRunning) || (!mHoverWatch.HasDisplay)) && // Don't show extended information for debug watches
							(!handlingHoverResolveTask) && (ResolveCompiler != null) && (!ResolveCompiler.mThreadWorkerHi.mThreadRunning) && (gApp.mSettings.mEditorSettings.mHiliteCursorReferences) && (!gApp.mDeterministic))
						{
							ResolveParams resolveParams = new .();
							resolveParams.mOverrideCursorPos = (int32)textIdx;
							Classify(ResolveType.GetResultString, resolveParams);
							//Debug.WriteLine($"GetResultString {resolveParams} {resolveParams.mInDeferredList}");
							if (!resolveParams.mInDeferredList)
								delete resolveParams;

							mHoverResolveTask = new HoverResolveTask();
							mHoverResolveTask.mCursorPos = (int32)textIdx;
							if (isManual)
							{
								mHoverResolveTask.mLine = (.)line;
								mHoverResolveTask.mLineChar = (.)lineChar;
							}
						}
					}

#if IDE_C_SUPPORT
	                if ((mIsClang) && (textIdx != -1))
	                {
	                    bool hasErrorFlag = (mEditWidget.Content.mData.mText[textIdx].mDisplayFlags != 0);

	                    if (hasErrorFlag)
	                    {
	                        if (!compiler.IsPerformingBackgroundOperation())
	                        {
	                            bool hadValidError = false;
	                            if (mClangHoverErrorData != null)
	                            {
	                                String[] stringParts = String.StackSplit!(mClangHoverErrorData, '\t');
	                                int startIdx = (int32)int32.Parse(stringParts[0]);
	                                int endIdx = (int32)int32.Parse(stringParts[1]);
	                                if ((textIdx >= startIdx) && (textIdx < endIdx))
	                                {
	                                    hadValidError = true;
	                                    triedShow = true;
	                                    mHoverWatch.Show(this, x, y, scope String(":", stringParts[2]));
										if (debugExpr != null)
	                                    	mHoverWatch.mEvalString.Set(debugExpr); // Set to old debugStr for comparison
										else
											mHoverWatch.mEvalString.Clear();
	                                }

	                            }

	                            if (!hadValidError)
	                            {
	                                mErrorLookupTextIdx = (int32)textIdx;
	                                Classify(ResolveType.Classify);
	                                triedShow = false;
	                            }
	                        }
	                    }
	                    else
	                    {
	                        triedShow = false;
							delete mClangHoverErrorData;
	                        mClangHoverErrorData = null;
	                    }
	                }
#endif

	                if ((parser != null) && (mIsBeefSource) && (!didShow) && (mHoverWatch != null) && (!mHoverWatch.mIsShown))
					ErrorScope:
	                {
						//TODO: Needed this?
	                    /*var resolvePassData = parser.CreateResolvePassData();
						defer (scope) delete resolvePassData;
	                    bfSystem.NotifyWillRequestLock(1);
	                    bfSystem.Lock(1);
	                    parser.BuildDefs(passInstance, resolvePassData, false);
	                    BfResolveCompiler.ClassifySource(passInstance, parser, resolvePassData, null);*/
	                    
	                    BfPassInstance.BfError bestError = scope BfPassInstance.BfError();

	                    for (var bfError in mErrorList)
	                    {
							if ((bfError.mWhileSpecializing.HasFlag(.Type)) && (mEmbedKind == .None))
	                            continue;
							if ((bfError.mWhileSpecializing.HasFlag(.Method)) && (mEmbedKind != .Method))
								continue;

	                        if ((textIdx >= bfError.mSrcStart) && (textIdx < bfError.mSrcEnd))
	                        {
	                            if ((bestError.mError == null) || (bestError.mIsWarning) || (bestError.mIsPersistent))
	                                bestError = bfError;                                    
	                        }
	                    }
	                    
	                    String showMouseoverString = null;
	                    if (bestError.mError != null)
	                    {
							int maxLen = 16*1024;
							if (bestError.mError.Length > maxLen)
	                        	showMouseoverString = scope:: String()..Concat(":", StringView(bestError.mError, 0, maxLen), "...");
							else
								showMouseoverString = scope:: String()..Concat(":", bestError.mError);

							if (bestError.mMoreInfo != null)
							{
								for (var moreInfo in bestError.mMoreInfo)
								{
									if (moreInfo.mLine != -1)
										showMouseoverString.AppendF("\n@{}\t{}:{}\t{}", moreInfo.mFilePath, moreInfo.mLine, moreInfo.mColumn, moreInfo.mError);
									else if (moreInfo.mFilePath != null)
										showMouseoverString.AppendF("\n@{0}\t{1}\t{2}", moreInfo.mFilePath, moreInfo.mSrcStart, moreInfo.mError);
									else
										showMouseoverString.AppendF("\n{}", moreInfo.mError);
								}
							}
	                    }
						else
						{
							var flags = (SourceElementFlags)editWidgetContent.mData.mText[textIdx].mDisplayFlags;
							if ((flags.HasFlag(.Error)) || (flags.HasFlag(.Warning)))
							{
								mWantsFullRefresh = true;
								mRefireMouseOverAfterRefresh = true;
							}
						}

	                    if (showMouseoverString != null)
	                    {
	                        triedShow = true;
	                        mHoverWatch.Show(this, x, y, showMouseoverString, showMouseoverString);
							if (debugExpr != null)
	                        	mHoverWatch.mEvalString.Set(debugExpr); // Set to old debugStr for comparison
	                    }
						else
	                        triedShow = false;
	                }
	            }
				if (!hasHoverWatchOpen)
	            	mHoverWatch?.mOpenMousePos = DarkTooltipManager.sLastRelMousePos;
	        }

			// Not used?
			if ((mHoverWatch != null) && (mHoverWatch.mTextPanel != this))
			{
				mHoverWatch.Close();
				mHoverWatch = null;
			}

	        if (mHoverWatch != null)
	        {
				if ((!triedShow) && (!IDEApp.sApp.HasPopupMenus()))
				{
					if (mHoverWatch.mCloseDelay > 0)
					{
						mHoverWatch.mCloseDelay--;
						mHoverWatch.mCloseCountdown = 20;
					}
	                else
	                {
	                    mHoverWatch.Close();
	                    mHoverWatch = null;
#if IDE_C_SUPPORT
						delete mClangHoverErrorData;
	                    mClangHoverErrorData = null;
#endif
	                }
				}
				else
				{
					mHoverWatch.mCloseCountdown = 0;
				}
	        }            
	        
	        if (passInstance != null)
	            delete passInstance;
	        if (parser != null)
	        {
	            delete parser;
	            mWantsParserCleanup = true;
	        }
		}

		public void UpdateMouseover()
		{
			if (mWidgetWindow == null)
				return;

			if (CheckLeftMouseover())
			{
				return;
			}

			if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mRelWidget == this))
				DarkTooltipManager.CloseTooltip();

			if ((!CheckAllowHoverWatch()) && (mHoverResolveTask?.mResult == null))
			{
				return;
			}

		    /*if ((mHoverWatch != null) && (mHoverWatch.mCloseDelay > 0))
		        return;*/
		    var editWidgetContent = mEditWidget.Content;
		    Point mousePos;
		    bool mouseoverFired = DarkTooltipManager.CheckMouseover(editWidgetContent, 10, out mousePos);

			if (mouseoverFired)
			{
				
			}

#unwarn
		    CompilerBase compiler = ResolveCompiler;

			bool hasClangHoverErrorData = false;

#if IDE_C_SUPPORT
			hasClangHoverErrorData = mClangHoverErrorData != null;
#endif

			if (mHoverResolveTask != null)
			{
				if (mHoverResolveTask.mLine != null)
				{
					UpdateMouseover(true, true, mHoverResolveTask.mLine.Value, mHoverResolveTask.mLineChar.Value, true);
					return;
				}
			}
			
		    if (((mouseoverFired) || (mHoverWatch != null) || (hasClangHoverErrorData) || (mHoverResolveTask?.mResult != null)) && 
		        (mousePos.x >= 0))
		    {
		        int line;
		        int lineChar;
		        float overflowX;
		        if (editWidgetContent.GetLineCharAtCoord(mousePos.x, mousePos.y, out line, out lineChar, out overflowX))
				{
					UpdateMouseover(mouseoverFired, true, line, lineChar);
				}
				else
				{
					UpdateMouseover(mouseoverFired, false, line, lineChar);
				}
			}
		}

        void DuplicateEditState(out EditWidgetContent.CharData[] char8Data, out IdSpan char8IdData)
        {
            var srcCharData = mEditWidget.Content.mData.mText;
            char8Data = new EditWidgetContent.CharData[mEditWidget.Content.mData.mTextLength];
			var editIdData = ref mEditWidget.Content.mData.mTextIdData;
			editIdData.Prepare();
            char8IdData = editIdData.Duplicate();
            for (int32 i = 0; i < char8Data.Count; i++)
            {
                srcCharData[i].mDisplayPassId = (uint8)SourceDisplayId.Cleared;
                char8Data[i] = srcCharData[i];
            }
        }

        void DoSpellCheck()
        {
            String sb = scope String();
            var spellChecker = IDEApp.sApp.mSpellChecker;
            int32 wordStart = -1;
            bool skipWord = false;
            bool isSectionText = true;
            int32 spanSectionIdx = -1;
            bool skipNextChar = false;
			bool isVerbatimString = false;
			bool prevWasLetter = false;
			SourceElementType prevElementType = .Normal;
            for (int32 i = 0; i < mProcessSpellCheckCharData.Count; i++)
            {
                mProcessSpellCheckCharData[i].mDisplayFlags = 0;
                mProcessSpellCheckCharData[i].mDisplayPassId = (uint8)SourceDisplayId.SpellCheck;

                if (skipNextChar)
                {
                    skipNextChar = false;
                    continue;
                }

				if (spellChecker == null)
					continue;

                var char8Data = mProcessSpellCheckCharData[i];
				char8 c = (char8)char8Data.mChar;
                var elementType = (SourceElementType)char8Data.mDisplayTypeId;
                bool endString = false;
				if (elementType == .Literal)
				{
					if (prevElementType != .Literal)
						isVerbatimString = (c == '@');
				}
				else
					isVerbatimString = false;

                if ((elementType == SourceElementType.Comment) || (elementType == SourceElementType.Literal))
                {
                    if (i >= spanSectionIdx)
                    {
                        isSectionText = true;
                        spanSectionIdx = i;
                        while (spanSectionIdx < mProcessSpellCheckCharData.Count)
                        {
                            var checkCharData = mProcessSpellCheckCharData[spanSectionIdx];
                            var checkElementType = (SourceElementType)checkCharData.mDisplayTypeId;
                            if (checkElementType != elementType)
                                break;
                            char8 checkC = (char8)checkCharData.mChar;
                            if (checkC == '\n')
                                break;
                            if ((checkC == '*') || (checkC == ';'))
                                isSectionText = false;
							if (checkC >= '\x80') // Don't process high characters
								isSectionText = false;
                            spanSectionIdx++;
                        }
                    }                    

                    if ((c == '\\') && (elementType == SourceElementType.Literal) && (!isVerbatimString))
                    {                        
                        endString = true;
                        skipNextChar = true;                        
                    }

					bool isLetter = c.IsLetter;

                    if ((isLetter) || ((c == '\'') && (prevWasLetter)))
                    {
                        if (i > 0)
                        {
                            char8 prevC = (char8)mProcessSpellCheckCharData[i - 1].mChar;
                            if ((prevC == '.') || (prevC.IsNumber))
                            {
                                // Looks like extension
                                skipWord = true;
                            }
                        }

                        if ((c.IsUpper) && (wordStart != -1))
                            skipWord = true;
                        if (wordStart == -1)
                            wordStart = i;                      
                    }
                    else if (c == '_')
                    {
                        skipWord = true;
                    }
                    else if (c == '.')
                    {
                        if (i < mProcessSpellCheckCharData.Count - 1)
                        {
                            char8 nextC = (char8)mProcessSpellCheckCharData[i + 1].mChar;
                            if (nextC.IsLetter)
                            {
                                // Looks like a filename
                                skipWord = true;
                            }
                        }
                        endString = true;
                    }
                    else 
                    {
                        endString = true;
                    }

                    if (c == '\n')
                        spanSectionIdx = i;
					prevWasLetter = isLetter;
                }
                else
                {
                    endString = true;
					prevWasLetter = false;
                }
                
                if ((i == mProcessSpellCheckCharData.Count - 1) && (!endString))
                {
                    // Process last word
                    i++;
                    endString = true;
                }

                if ((endString) && (wordStart != -1))
                {
                    int32 wordLen = i - wordStart;
                    if (wordLen <= 1)
                        skipWord = true;
                    if (!isSectionText)
                        skipWord = true;
                    if (!skipWord)
                    {
                        sb.Clear();

						if (mProcessSpellCheckCharData[i - 1].mChar == '\'')
							i--;
                        for (int32 wordCharIdx = wordStart; wordCharIdx < i; wordCharIdx++)
                            sb.Append(mProcessSpellCheckCharData[wordCharIdx].mChar);
                        String word = sb;
                        bool hasSpellingError = (word.Length > 1) && (!IDEApp.sApp.mSpellChecker.IsWord(word));
                        if (hasSpellingError)
                        {
							word.ToLower();
                            using (spellChecker.mMonitor.Enter())                            
                                hasSpellingError = !spellChecker.mIgnoreWordList.Contains(word);
                        }
                        
                        if (hasSpellingError)
                        {
                            for (int32 wordCharIdx = wordStart; wordCharIdx < i; wordCharIdx++)
                            {                                
                                mProcessSpellCheckCharData[wordCharIdx].mDisplayFlags = (uint8)SourceElementFlags.SpellingError;
                            }
                        }
                    }
                    skipWord = false;                
                    wordStart = -1;
                }

				prevElementType = elementType;
            }
        }

		void SpellCheckDone()
		{
			mSpellCheckJobCount--;
		}

        void StartSpellCheck()
        {
			if (gApp.mSpellChecker == null)
			{
				if (mDidSpellCheck)
				{
					var data = mEditWidget.Content.mData;
					for (int i < data.mTextLength)
					{
						data.mText[i].mDisplayFlags &= ~((uint8)SourceElementFlags.SpellingError);
					}
				}
				mDidSpellCheck = false;
				return;
			}

            DuplicateEditState(out mProcessSpellCheckCharData, out mProcessSpellCheckCharIdSpan);

			mDidSpellCheck = true;
			mSpellCheckJobCount++;
            IDEApp.sApp.mSpellChecker.DoBackground(new => DoSpellCheck, new => SpellCheckDone);
        }
		
		void AddHistory()
		{

		}

		/*bool ProcessResolveData()
		{
			scope AutoBeefPerf("SourceViewPanel.ProcessResolveData");

			bool canDoBackground = true;

			if ((mProcessResolveCharData != null) && (mProcessingPassInstance != null))
			{
				MarkDirty();
			    InjectErrors(mProcessingPassInstance, mProcessResolveCharData, mProcessResolveCharIdSpan, false);
			    canDoBackground = false;
			}

			if (mProcessResolveCharData != null)
			{
				for (int i < mProcessResolveCharData.Count)
				{
					
				}

				MarkDirty();
			    UpdateCharData(ref mProcessResolveCharData, ref mProcessResolveCharIdSpan, (uint8)SourceElementFlags.CompilerFlags_Mask);
			    canDoBackground = false;
			}

			if (mProcessingPassInstance != null)
			{
				MarkDirty();
			    if (mProcessingPassInstance.HadSignatureChanges())
			        mWantsFullRefresh = true;

			    delete mProcessingPassInstance;
			    mProcessingPassInstance = null;
			}

			return canDoBackground;
		}*/

		public void EnsureReady()
		{
			if (mWantsFastClassify)
			{
				DoFastClassify();
				mWantsFastClassify = false;
			}
		}

		public bool HasDeferredResolveResults()
		{
			using (mMonitor.Enter())
			{
				return !mDeferredResolveResults.IsEmpty;
			}
		}

		void ProcessDeferredResolveResults(int waitTime, bool autocompleteOnly = false)
		{
			//bool canDoBackground = true;

			int checkIdx = 0;

			while (true)
			{
				ResolveParams resolveResult = null;
				using (mMonitor.Enter())
				{
					if (checkIdx >= mDeferredResolveResults.Count)
						break;
					resolveResult = mDeferredResolveResults[checkIdx];
				}

				if ((autocompleteOnly) && (resolveResult.mResolveType != .Autocomplete))
				{
					checkIdx++;
					continue;
				}

				if (!resolveResult.mWaitEvent.WaitFor(0))
				{
					if (waitTime != 0)
						ResolveCompiler.RequestFastFinish();
				}

				if (!resolveResult.mWaitEvent.WaitFor(waitTime))
				{
					checkIdx++;
					continue;
				}

				using (mMonitor.Enter())
					mDeferredResolveResults.RemoveAt(checkIdx);

				//Debug.WriteLine($"HandleResolveResult {resolveResult}");

				HandleResolveResult(resolveResult.mResolveType, resolveResult.mAutocompleteInfo, resolveResult);

				if (resolveResult.mStopwatch != null)
				{
					resolveResult.mStopwatch.Stop();
					if (var autoComplete = GetAutoComplete())
						Debug.WriteLine($"Autocomplete {resolveResult.mStopwatch.ElapsedMilliseconds}ms entries: {(autoComplete.mAutoCompleteListWidget?.mEntryList.Count).GetValueOrDefault()}");
				}

				//Debug.WriteLine("ProcessDeferredResolveResults finished {0}", resolveResult.mResolveType);

				//bool checkIt = (mFilePath.Contains("Program.bf")) && (mEditWidget.mEditWidgetContent.mData.mCurTextVersionId > 3);
				/*var data = ref mEditWidget.Content.mData.mText[10018];
				if (checkIt)
				{
					Debug.Assert(resolveResult.mCharData[10018].mDisplayTypeId == 8);
					Debug.Assert(data.mDisplayTypeId == 8);
				}
				uint8* ptr = &data.mDisplayTypeId;*/

				scope AutoBeefPerf("SourceViewPanel.ProcessResolveData");

				bool canDoBackground = true;

				bool wantsData = (!resolveResult.mCancelled) && (resolveResult.mResolveType != .GetCurrentLocation) && (resolveResult.mResolveType != .GetSymbolInfo);
				if (wantsData)
				{
					bool filterErrors = !resolveResult.mEmitEmbeds.IsEmpty;

					if ((resolveResult.mCharData != null) && (resolveResult.mPassInstance != null))
					{
						bool isAutocomplete = (resolveResult.mResolveType == .Autocomplete) || (resolveResult.mResolveType == .Autocomplete_HighPri);

						MarkDirty();
					    InjectErrors(resolveResult.mPassInstance, resolveResult.mCharData, resolveResult.mCharIdSpan, isAutocomplete, filterErrors);
					    canDoBackground = false;
					}

					if (resolveResult.mCharData != null)
					{
						MarkDirty();
					    UpdateCharData(ref resolveResult.mCharData, ref resolveResult.mCharIdSpan, (uint8)SourceElementFlags.CompilerFlags_Mask, false);
					    canDoBackground = false;
					}

					if ((!resolveResult.mEmitEmbeds.IsEmpty) && (resolveResult.mResolveType.IsClassify))
					{
						let ewc = (SourceEditWidgetContent)mEditWidget.mEditWidgetContent;

						Dictionary<String, SourceEditWidgetContent.EmitEmbed.View> emitViewDict = scope .();
						Dictionary<String, String> remappedTypeNames = scope .();

						for (var embed in ewc.mEmbeds.Values)
						{
							if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
							{
								String useTypeName = emitEmbed.mTypeName;
								if (!mExplicitEmitTypes.IsEmpty)
								{
									if (remappedTypeNames.TryAdd(useTypeName, var keyPtr, var valuePtr))
									{
										*valuePtr = useTypeName;
										for (var explicitTypeName in mExplicitEmitTypes)
										{
											if (IDEUtils.GenericEquals(useTypeName, explicitTypeName))
											{
												*valuePtr = explicitTypeName;
												break;
											}
										}
									}
									emitEmbed.mTypeName.Set(*valuePtr);
								}

								if (emitEmbed.mView != null)
								{
									if (emitViewDict.TryAdd(emitEmbed.mTypeName, var keyPtr, var valuePtr))
									{
										*valuePtr = emitEmbed.mView;
									}
									else if (emitEmbed.mView.mTypeName != emitEmbed.mTypeName)
									{
										emitEmbed.mView.RemoveSelf();
										DeleteAndNullify!(emitEmbed.mView);
										ewc.mCollapseNeedsUpdate = true;
									}
								}
							}
						}

						for (var embed in resolveResult.mEmitEmbeds)
						{
							if (embed.mCharData == null)
								continue;

							if (emitViewDict.GetAndRemove(embed.mTypeName) case .Ok((var name, var emitEmbedView)))
							{
								var emitEmbed = emitEmbedView.mEmitEmbed;

								if (emitEmbedView.mEmitEmbed.mTypeName != emitEmbedView.mTypeName)
								{
									int focusIdx = -1;
									if (emitEmbedView.mSourceViewPanel.mEditWidget.mHasFocus)
										focusIdx = 0;
									else if (emitEmbedView.mGenericTypeCombo?.mEditWidget.mHasFocus == true)
										focusIdx = 1;
									else if (emitEmbedView.mGenericMethodCombo?.mEditWidget.mHasFocus == true)
										focusIdx = 2;

									emitEmbedView.RemoveSelf();
									DeleteAndNullify!(emitEmbed.mView);

									emitEmbedView = new .(emitEmbed);
									emitEmbed.mView = emitEmbedView;
									mEditWidget.mEditWidgetContent.AddWidget(emitEmbed.mView);

									var sewc = mEditWidget.mEditWidgetContent as SourceEditWidgetContent;
									sewc.RehupLineCoords();

									if (focusIdx == 0)
										emitEmbedView.mSourceViewPanel.mEditWidget.SetFocus();
									else if (focusIdx == 1)
										emitEmbedView.mGenericTypeCombo?.mEditWidget.SetFocus();
									else if (focusIdx == 2)
										emitEmbedView.mGenericMethodCombo?.mEditWidget.SetFocus();
								}

								var firstSourceViewPanel = emitEmbedView.mSourceViewPanel;

								var firstEmbedEWC = firstSourceViewPanel.mEditWidget.mEditWidgetContent;

								var prevCursorLineAndColumn = firstEmbedEWC.CursorLineAndColumn;

								var editData = firstSourceViewPanel.mEditWidget.mEditWidgetContent.mData;
								if (editData.mTextLength == 0)
									DeleteAndNullify!(firstSourceViewPanel.mTrackedTextElementViewList);

								delete editData.mText;
								editData.mText = embed.mCharData;
								editData.mTextLength = (.)embed.mCharData.Count - 1;
								embed.mCharData = null;
								editData.mTextIdData.Dispose();
								editData.mTextIdData = IdSpan();
								editData.mNextCharId = 0;
								editData.mTextIdData.Insert(0, editData.mTextLength, ref editData.mNextCharId);

								firstSourceViewPanel.mEmitRevision = embed.mRevision;
								firstSourceViewPanel.InjectErrors(resolveResult.mPassInstance, editData.mText, editData.mTextIdData, false, true);

								for (var user in editData.mUsers)
								{
									if (var embedEWC = user as SourceEditWidgetContent)
									{
										var sourceViewPanel = embedEWC.mSourceViewPanel;

										sourceViewPanel.mEditWidget.mEditWidgetContent.ContentChanged();
										// We have a full classify now, FastClassify will just mess it up
										sourceViewPanel.mSkipFastClassify = true; 

										if (prevCursorLineAndColumn.mLine >= firstEmbedEWC.GetLineCount())
											embedEWC.CursorLineAndColumn = .(firstEmbedEWC.GetLineCount() - 1, prevCursorLineAndColumn.mColumn);
									}
								}
							}
						}
					}

					if (resolveResult.mPassInstance != null)
					{
						MarkDirty();
					    if (resolveResult.mPassInstance.HadSignatureChanges())
						{
					        mWantsFullRefresh = true;
						}
					}
				}

				/*if (checkIt)
					Debug.Assert(data.mDisplayTypeId == 8);*/

				//Debug.WriteLine($"Deleting {resolveResult}");
				delete resolveResult;
			}
		}

        public override void Update()
        {
            base.Update();

			scope AutoBeefPerf("SourceViewPanel.Update");

			EnsureReady();

			if (mPanelHeader != null)
			{
				if (mPanelHeader.mKind == .WrongHash)
				{
					if (!gApp.mDebugger.mIsRunning)
					{
						// No longer makes sense if we're not even running
						CloseHeader();
					}
				}
			}

			if (mProjectSource == null)
			{
				if (mEditData != null)
					Debug.Assert(mEditData.mProjectSources.IsEmpty);
			}
			else
			{
				if (mEditData != null)
					Debug.Assert(mEditData.mProjectSources.Contains(mProjectSource));
			}

			let projectSource = FilteredProjectSource;

			/*if (mWidgetWindow.IsKeyDown(.Control) && mWidgetWindow.IsKeyDown(.Alt))
				QueueFullRefresh(false);*/

			if (mEditData != null)
				Debug.Assert(Path.Equals(mFilePath, mEditData.mFilePath));


			bool hasFocus = HasFocus(false);
			bool selfHasFocus = HasFocus(true);

			if (mSourceFindTask != null)
			{
				if (mSourceFindTask.WaitFor(0))
				{
					String foundPath = null;
					bool isWrongHash = false;
					if (mSourceFindTask.mFoundPath != null)
					{
						foundPath = mSourceFindTask.mFoundPath;
					}
					else if (mSourceFindTask.mBackupFileName != null)
					{
						foundPath = mSourceFindTask.mBackupFileName;
						isWrongHash = true;
					}

					if (foundPath != null)
					{
						mFilePath.Set(foundPath);
						RehupAlias();
					}

					DeleteAndNullify!(mSourceFindTask);

					RetryLoad();
				}
			}

			/*if (mEditData != null)
			{
				Debug.Assert(!mEditData.mOwnsEditWidget);
				Debug.Assert(!mEditData.mFilePath.Contains("\\\\"));
				if (mEditData.mFilePath.IndexOf("BfModule.cpp", true) != -1)
				{
					Debug.Assert(mProjectSource != null);
				}
			}*/

            if (GetEditX() != mEditWidget.mX)
                ResizeComponents();

            if (!IDEApp.sApp.mDebugger.mIsRunning)
                CloseOldVersion();

			if (mOldVerLoadExecutionInstance != null)
			{
				if (mOldVerLoadExecutionInstance.mExitCode != null)
				{
					if ((int)mOldVerLoadExecutionInstance.mExitCode == 0)
					{
						CheckAdjustFile();
                        RetryLoad();
					}
					else
						SetLoadCmd(mOldVerLoadCmd);
					delete mOldVerLoadExecutionInstance;
					mOldVerLoadExecutionInstance = null;
				}
			}

			if (mOldVerHTTPRequest != null)
			{
				let result = mOldVerHTTPRequest.GetResult();
				if (result != .NotDone)
				{
					if (result == .Failed)
					{
						String errorMsg = scope .();
						errorMsg.AppendF("Failed to retrieve source from {}", mOldVerLoadCmd);

						String errorReason = scope .();
						mOldVerHTTPRequest.GetLastError(errorReason);

						if (!errorReason.IsEmpty)
							errorMsg.AppendF(" ({})", errorReason);

						gApp.OutputErrorLine(errorMsg);
					}

					CheckAdjustFile();
					RetryLoad();
					DeleteAndNullify!(mOldVerHTTPRequest);
				}
			}

			if (gApp.mIsUpdateBatchStart)
            	UpdateMouseover();
            
            var compiler = ResolveCompiler;
            var bfSystem = BfResolveSystem;
            var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

            if (bfSystem != null)
                bfSystem.PerfZoneStart("Update");

            if ((compiler != null) && (!compiler.IsPerformingBackgroundOperation()))
                mBackgroundResolveType = ResolveType.None;

            bool canDoBackground = (compiler != null) && (!compiler.IsPerformingBackgroundOperation()) && (!compiler.HasQueuedCommands());
#if IDE_C_SUPPORT
            if (mIsClang)
            {
                var buildClang = IDEApp.sApp.mDepClang;
                if ((buildClang.IsPerformingBackgroundOperation()) && (mWantsFullClassify))
                {
                    // Don't let buildClang get in the way of autocompletion
                    buildClang.mThreadYieldCount = 20;
                    canDoBackground = false;
                }
            }
#endif

			if (mResolveJobCount > 0)
			{
				if (compiler != null)
					Debug.Assert((compiler.IsPerformingBackgroundOperation()) || (compiler.mThreadWorker.mOnThreadDone != null) || (compiler.mThreadWorkerHi.mOnThreadDone != null));
			}

            if (mBackgroundDelay > 0)
            {
                --mBackgroundDelay;
                canDoBackground = false;
            }

            if (mIsPerformingBackgroundClassify)
            {
                canDoBackground = false;
            }
            else
            {
                // Handle finishing old backgrounds             
				//canDoBackground &= ProcessResolveData();
            }                           

            if (canDoBackground)
            {
                // Check for starting new backgrounds

                // Wait longer for Clang since it'll delay autocompletions whereas Beef can be interrupted
                int32 classifyDelayTicks = (mIsBeefSource || (mUpdateCnt < 40)) ? 2 : 40;
				if (mUpdateCnt <= 1)
					classifyDelayTicks = 0;
                
                if (mWantsBackgroundAutocomplete)
                {
                    Classify(ResolveType.Autocomplete);
                    mWantsBackgroundAutocomplete = false;
					canDoBackground = false;
                }
                else if ((mWantsFullClassify) && (mTicksSinceTextChanged >= classifyDelayTicks) 
                    //TODO: Debug, remove
                        /*&& (mWidgetWindow.IsKeyDown(KeyCode.Alt))*/)
                {
					if (IsControllingEditData())
					{
	                    if (Classify(mWantsFullRefresh ? ResolveType.ClassifyFullRefresh : ResolveType.Classify))
	                    {
	                        mWantsFullClassify = false;
	                        mWantsFullRefresh = false;
							mWantsCollapseRefresh = false;
	                    }
						canDoBackground = false;
					}
                }
				else if (mWantsCollapseRefresh)
				{
					if (!compiler.IsPerformingBackgroundOperation())
					{
						bfSystem?.Log("SourceViewPanel handling mWantsCollapseRefresh");
						mWantsCollapseRefresh = false;
						/*Classify(.GetCollapse);*/

						if ((projectSource != null) && (mIsBeefSource))
						do
						{
					        var bfProject = bfSystem.GetBfProject(projectSource.mProject);
					        if (bfProject.mDisabled)
								break;
							
					        var bfParser = bfSystem.FindParser(projectSource);
							if (bfParser == null)
								break;

							var data = mEditWidget.mEditWidgetContent.mData;
							if (mParsedState.mTextVersion == -1)
							{
								mParsedState.mIdSpan.DuplicateFrom(ref data.mTextIdData);
								mParsedState.mTextVersion = data.mCurTextVersionId;
							}

							//Debug.WriteLine($"Queueing DoRefreshCollapse TextVersion:{mParsedState.mTextVersion} IdSpan:{mParsedState.mIdSpan:D}");
							compiler.DoBackground(new () =>
								{
									DoRefreshCollapse(bfParser, mParsedState.mTextVersion, mParsedState.mIdSpan);
								});
						}
					}
				}
				else if (mWantsParserCleanup)
				{
					if (!compiler.IsPerformingBackgroundOperation())
					{
						mWantsParserCleanup = false;
						compiler.DoBackground(new => DoParserCleanup);
						canDoBackground = false;
					}
				}
            }
            else if ((mWantsFullClassify) && (selfHasFocus))
            {
                // Already changed - cancel and restart
				if (compiler != null)
                	compiler.RequestCancelBackground();
                //Debug.WriteLine(String.Format("Cancel From: {0}", mFilePath));
            }
            
            if ((IDEApp.sApp.mSpellChecker == null) || (!IDEApp.sApp.mSpellChecker.IsPerformingBackgroundOperation()))
            {
                if (mProcessSpellCheckCharData != null)
				{
                    UpdateCharData(ref mProcessSpellCheckCharData, ref mProcessSpellCheckCharIdSpan, (uint8)SourceElementFlags.SpellingError, true);
					delete mProcessSpellCheckCharData;
					mProcessSpellCheckCharData = null;
					mProcessSpellCheckCharIdSpan.Dispose();
				}
                if ((mTicksSinceTextChanged >= 60) && (mWantsSpellCheck))
                {
					if ((IsControllingEditData()) && (mEmbedKind == .None))
                    	StartSpellCheck();
                    mWantsSpellCheck = false;
                }
            }            

#if IDE_C_SUPPORT
            if ((mAutocompleteTextVersionId != mEditWidget.Content.mData.mCurTextVersionId) && (mIsClang))
            {
                // We used an edit that didn't fire the autocompletion, and thus didn't do a FastClassify                
				if (IsControllingEditData())
                	DoFastClassify();
                mAutocompleteTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;
            }
#endif

            if ((mLastTextVersionId != mEditWidget.Content.mData.mCurTextVersionId) ||
                (mClassifiedTextVersionId != mEditWidget.Content.mData.mCurTextVersionId))
            {
                if ((mIsBeefSource) && (projectSource != null))
                {
                    // If this file is included in multiple projects then we need to
                    //  reparse these contents in the context of the other projects
					if ((IsControllingEditData()) && (IDEApp.sApp.mBfResolveHelper != null))
                    	IDEApp.sApp.mBfResolveHelper.DeferReparse(mFilePath, this);
                }

#if IDE_C_SUPPORT
                if (mIsClang)
                {
                    mClangSourceChanged = true;
                    IDEApp.sApp.mResolveClang.mProjectSourceVersion++;
                }
#endif

                if (mClassifiedTextVersionId != mEditWidget.Content.mData.mCurTextVersionId)
                {
                    mWantsSpellCheck = true;
                    mWantsFullClassify = true;
                    mClassifiedTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;

                    if ((mProjectSource != null) && (IDEApp.sApp.mWakaTime != null) && (IsControllingEditData()))
                    {
                        IDEApp.sApp.mWakaTime.QueueFile(mFilePath, mProjectSource.mProject.mProjectName, false);
                    }
                }
                mLastTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;
                mTicksSinceTextChanged = 0;
				if (mProjectSource != null)
					mProjectSource.HasChangedSinceLastCompile = true;
				
                EnsureTrackedElementsValid();
                UpdateHasChangedSinceLastCompile();
                if (mHoverWatch != null)
				{
                    mHoverWatch.Close();
					Debug.Assert(mHoverWatch == null);
				}
            }
			else
			{
				// If we do a non-autocomplete change that modifies a type signature, that will generate a Classify which detects
				//  the signature change afterward, and then we need to do a full classify after that
				if (mWantsFullRefresh)
					mWantsFullClassify = true;
	            if (IDEApp.sApp.mIsUpdateBatchStart)
	                mTicksSinceTextChanged++;
			}

			if ((mLastRecoveryTextVersionId != mEditWidget.Content.mData.mCurTextVersionId) && (mTicksSinceTextChanged >= 16))
			{
#if !CLI
				DeleteAndNullify!(mFileRecoveryEntry);
				if ((mFilePath != null) && (mEditData != null) && (!mEditData.mRecoveryHash.IsZero) && (gApp.mSettings.mEditorSettings.mEnableFileRecovery != .No) && (!gApp.mFileRecovery.mDisabled))
				{
					String contents = scope .();
					mEditWidget.GetText(contents);
					mFileRecoveryEntry = new .(gApp.mFileRecovery, mFilePath, mEditData.mRecoveryHash, contents, mEditWidget.mEditWidgetContent.CursorTextPos);
				}
#endif

				mLastRecoveryTextVersionId = mEditWidget.Content.mData.mCurTextVersionId;
			}

            //TODO: This is just a test!
            /*if (Rand.Float() <so 0.05f)
                Classify(false, true);
            if (Rand.Float() < 0.05f)
            {
                mWantsFullClassify = true;
                if (Rand.Float() < 0.5f)
                    mWantsFullRefresh = true;
            }*/

            UpdateTrackedElements();

            if (bfSystem != null)
                bfSystem.PerfZoneEnd();
            mJustShown = false;

            if (hasFocus)
            {
                IDEApp.sApp.SetInDisassemblyView(false);
                /*if ((IDEApp.sApp.mRenameSymbolDialog != null) && (mRenameSymbolDialog == null))
                {
                    // Someone else has one open, cancel it
                    IDEApp.sApp.mRenameSymbolDialog.Close();
                }*/
            }

			// When files changes we move it to the permanent area
            if ((HasUnsavedChanges()) && (gApp.mSymbolReferenceHelper?.IsRenaming != true))
            {
                var parent = mParent;
                while (parent != null)
                {
                    var tabbedView = mParent as TabbedView;
                    if (tabbedView != null)
                    {
                        var activeTab = (DarkTabbedView.DarkTabButton)tabbedView.GetActiveTab();
                        //This isn't true if we have a split view
                        // Debug.Assert(activeTab.mContent == this);
                        if (activeTab.mIsRightTab)
                        {
                            IDEApp.sApp.ShowSourceFile(mFilePath, mProjectSource, SourceShowType.ShowExisting, false);
                        }
                        break;
                    }
                    parent = parent.mParent;
                }
            }

			// This potentially closes the symbol reference helper if there's nothing going on
            if ((selfHasFocus) /*&& (sourceEditWidgetContent.mCursorStillTicks == 0)*/)
            {
				var symbolReferenceHelper = IDEApp.sApp.mSymbolReferenceHelper;
				if ((symbolReferenceHelper != null) && //(symbolReferenceHelper.HasStarted) && (!symbolReferenceHelper.IsStarting) &&
                    (symbolReferenceHelper.mKind == SymbolReferenceHelper.Kind.ShowFileReferences))
                {
                    int cursorIdx = sourceEditWidgetContent.CursorTextPos;

                    bool hasFlag = false;
					if (!sourceEditWidgetContent.mVirtualCursorPos.HasValue)
					{
	                    for (int32 ofs = -1; ofs <= 0; ofs++)
	                    {
	                        if ((cursorIdx + ofs >= 0) && (cursorIdx + ofs < sourceEditWidgetContent.mData.mTextLength) &&
	                            ((sourceEditWidgetContent.mData.mText[cursorIdx + ofs].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) != 0))
	                            hasFlag = true;
	                    }
					}

                    if ((!hasFlag) /*|| 
                        ((!mEditWidget.mHasFocus) && (!Utils.FileNameEquals(symbolReferenceHelper.mSourceViewPanel.mFilePath, mFilePath)))*/)
                    {
						if ((symbolReferenceHelper.HasStarted) && (!symbolReferenceHelper.IsStarting))
						{
							symbolReferenceHelper.Close();
						}
						else
                        {
							//symbolReferenceHelper.mWantsClose = true;
							//Debug.WriteLine("Queued close...");
						}
                    }                    
                }
            }

			if ((gApp.mSymbolReferenceHelper != null) && (gApp.mSymbolReferenceHelper.mWantsClose) &&
				(gApp.mSymbolReferenceHelper.HasStarted) && (!gApp.mSymbolReferenceHelper.IsStarting))
			{
				gApp.mSymbolReferenceHelper.Close();
			}

			// Set this to 'false' for debugging to remove the hilighting of all symbol references under the cursor
			if (BFApp.sApp.mIsUpdateBatchStart)
            	sourceEditWidgetContent.mCursorStillTicks++;

            if ((gApp.mSettings.mEditorSettings.mHiliteCursorReferences) && (!gApp.mDeterministic) && (HasFocus(true)) &&
				((mProjectSource != null) || (mEmbedKind != .None)) /*&& (IDEApp.sApp.mSymbolReferenceHelper == null)*/)
            {
                if ((mEditWidget.mHasFocus) && (mIsBeefSource) && (sourceEditWidgetContent.mCursorStillTicks == 10) && (!sourceEditWidgetContent.mCursorImplicitlyMoved) && (!sourceEditWidgetContent.mVirtualCursorPos.HasValue))
                {
					var symbolReferenceHelper = IDEApp.sApp.mSymbolReferenceHelper;
                    if (symbolReferenceHelper == null)
                    {
						if ((compiler != null) && (!compiler.mThreadWorkerHi.mThreadRunning) && (mProjectSource != null))
						{
                        	ShowSymbolReferenceHelper(SymbolReferenceHelper.Kind.ShowFileReferences);
						}
						else
						{
							sourceEditWidgetContent.mCursorStillTicks--; // Try again later (kindof a hack)
						}
                    }
					else if (symbolReferenceHelper.mWantsClose)
					{
						//Debug.WriteLine("Delayed...");
						sourceEditWidgetContent.mCursorStillTicks--; // Try again later (kindof a hack)
					}
					else
					{
						// Still trying to show the old location
						sourceEditWidgetContent.mCursorStillTicks--; // Try again later (kindof a hack)
					}
                }

				if (sourceEditWidgetContent.mCursorStillTicks == 5)
				{
					mWantsCurrentLocation = true;
				}

				if ((mWantsCurrentLocation) && (compiler != null) && (!compiler.mThreadWorkerHi.mThreadRunning))
				{
					bool canClassify = true;
#if IDE_C_SUPPORT
	                if (mIsClang)
	                    canClassify = canDoBackground;
#endif
					if (canClassify)
					{
						Classify(ResolveType.GetCurrentLocation);
						canDoBackground = false;
						mWantsCurrentLocation = false;
					}
				}

				if ((mQueuedLocation != null) && (!compiler.IsPerformingBackgroundOperation()))
				{
					PrimaryNavigationBar.SetLocation(mQueuedLocation);
					DeleteAndNullify!(mQueuedLocation);
				}
            }

			if ((mQueuedAutoComplete != null) & (compiler != null) && (!compiler.mThreadWorkerHi.mThreadRunning))
			{
				DoAutoComplete(mQueuedAutoComplete.mKeyChar, mQueuedAutoComplete.mOptions);
				DeleteAndNullify!(mQueuedAutoComplete);
			}

			if (mLockFlashPct != 0)
			{
				mLockFlashPct += 0.02f;
				if (mLockFlashPct >= 1.0f)
					mLockFlashPct = 0;
				MarkDirty();
			}

			if ((mEmitRevision >= 0) && ((mUpdateCnt % 30) == 0))
				CheckEmitRevision();

			var ewc = (SourceEditWidgetContent)mEditWidget.Content;
			using (mMonitor.Enter())
			{
				if (mQueuedCollapseData != null)
				{
					if (gApp.mSettings.mEditorSettings.mEmitCompiler == .Build)
					{
						if (mQueuedCollapseData.mBuildData != null)
						{
							bool foundData = false;

							using (gApp.mMonitor.Enter())
							{                
							    var projectSourceCompileInstance = gApp.mWorkspace.GetProjectSourceCompileInstance(projectSource, gApp.mWorkspace.HotCompileIdx);
								if (projectSourceCompileInstance != null)
								{
									foundData = true;
									ewc.ParseCollapseRegions(mQueuedCollapseData.mBuildData, mQueuedCollapseData.mTextVersion, ref projectSourceCompileInstance.mSourceCharIdData, null);

									HashSet<EditWidgetContent.Data> dataLoaded = scope .();

									for (var embed in ewc.mEmbeds.Values)
									{
										if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
										{
											if (emitEmbed.mView != null)
											{
												if (dataLoaded.Add(emitEmbed.mView.mSourceViewPanel.mEditWidget.mEditWidgetContent.mData))
												{
													emitEmbed.mView.mSourceViewPanel.mSkipFastClassify = false;
													emitEmbed.mView.mSourceViewPanel.Reload();
													emitEmbed.mView.mSourceViewPanel.mWantsFastClassify = true;
												}
											}
										}
									}
								}
							}

							if (!foundData)
							{
								for (var embed in ewc.mEmbeds.Values)
								{
									if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
									{
										if (emitEmbed.mView != null)
										{
											emitEmbed.mView.mSourceViewPanel.mEditWidget.mEditWidgetContent.ClearText();
										}
									}
								}
							}
						}
					}
					
					ewc.ParseCollapseRegions(mQueuedCollapseData.mData, mQueuedCollapseData.mTextVersion, ref mQueuedCollapseData.mCharIdSpan, mQueuedCollapseData.mResolveType);
				}
				DeleteAndNullify!(mQueuedCollapseData);
			}

			UpdateQueuedEmitShowData();

			// Process after mQueuedCollapseData so mCharIdSpan is still valid
			ProcessDeferredResolveResults(0);

#if !CLI
			if (ewc.mCollapseDBDirty)
			{
				MemoryStream memStream = scope .();

				String text = scope .();
				mEditWidget.GetText(text);
				var hash = MD5.Hash(.((uint8*)text.Ptr, text.Length));
				memStream.Write(hash);

				bool hadData = false;

				for (var kv in ewc.mOrderedCollapseEntries)
				{
					if (kv.mIsOpen != kv.DefaultOpen)
					{
						hadData = true;
						memStream.Write(kv.mAnchorIdx);
					}
				}

				String filePath = scope .(mFilePath);
				IDEUtils.MakeComparableFilePath(filePath);

				if (!hadData)
					gApp.mFileRecovery.DeleteDB(filePath);
				else
					gApp.mFileRecovery.SetDB(filePath, memStream.Memory);
				ewc.mCollapseDBDirty = false;
			}
#endif
        }

		public override void UpdateF(float updatePct)
		{
			base.UpdateF(updatePct);

			var ewc = (SourceEditWidgetContent)mEditWidget.Content;
			if (ewc.mCollapseNeedsUpdate)
				ewc.UpdateCollapse(updatePct);
		}

		public void UpdateQueuedEmitShowData()
		{
			var compiler = ResolveCompiler;
			var bfSystem = BfResolveSystem;
			var ewc = (SourceEditWidgetContent)mEditWidget.Content;

			if (mQueuedEmitShowData == null)
				return;
		
			if (mQueuedEmitShowData.mTypeId == -1)
			{
				if (!compiler.IsPerformingBackgroundOperation())
				{
					var bfCompiler = compiler as BfCompiler;
					if (bfCompiler != null)
					{
						bfSystem.Lock(0);
						defer bfSystem.Unlock();

						mQueuedEmitShowData.mTypeId = (.)bfCompiler.GetTypeId(mQueuedEmitShowData.mTypeName);
					}
				}
			}

			if (mQueuedEmitShowData.mTypeId != -1)
			{
				Find: do
				{
					for (var embed in ewc.mEmbeds.Values)
					{
					 	if (var emitEmbed = embed as SourceEditWidgetContent.EmitEmbed)
						{
							if ((emitEmbed.mTypeName == mQueuedEmitShowData.mTypeName) && (mQueuedEmitShowData.mLine >= emitEmbed.mStartLine) &&
								(mQueuedEmitShowData.mLine < emitEmbed.mEndLine))
							{
								if (emitEmbed.mView == null)
								{
									emitEmbed.mIsOpen = true;
									ewc.mCollapseNeedsUpdate = true;
								}
								else
								{
									var embedEditWidget = emitEmbed.mView.mSourceViewPanel.mEditWidget;
									var embedEWC = embedEditWidget.mEditWidgetContent;
									if (embedEWC.mData.mTextLength > 0)
									{
										int idx = embedEWC.GetTextIdx(mQueuedEmitShowData.mLine, mQueuedEmitShowData.mColumn);
										emitEmbed.mView.mSourceViewPanel.FocusEdit();
										if (idx >= 0)
											emitEmbed.mView.mSourceViewPanel.ShowFileLocation(idx, .Always);
										DeleteAndNullify!(mQueuedEmitShowData);
									}
								}

								break Find;
							}
						}
					}

					// Couldn't find it even after a refresh
					if (ewc.mCollapseParseRevision > mQueuedEmitShowData.mPrevCollapseParseRevision)
						DeleteAndNullify!(mQueuedEmitShowData);
				}
			}
		}

        void InjectErrors(BfPassInstance processingPassInstance, EditWidgetContent.CharData[] processResolveCharData, IdSpan processCharIdSpan, bool keepPersistentErrors, bool filterErrors)
        {
            if (keepPersistentErrors)
            {                
                for (int errorIdx = mErrorList.Count - 1; errorIdx >= 0; errorIdx--)
                {
                    var bfError = mErrorList[errorIdx];
                    if (bfError.mIsPersistent)
                    {
                        
                    }
                    else
                    {
						delete bfError;
                        mErrorList.RemoveAt(errorIdx);                        
                    }
                }
            }
            else
            {
				ClearAndDeleteItems(mErrorList);
            }
            
            int32 errorCount = processingPassInstance.GetErrorCount();
            mErrorList.Capacity = mErrorList.Count + errorCount;

			bool hadNonDeferredErrors = false;

            for (int32 errorIdx = 0; errorIdx < errorCount; errorIdx++)
            {
                BfPassInstance.BfError bfError = new BfPassInstance.BfError();
                processingPassInstance.GetErrorData(errorIdx, bfError);

				if (filterErrors)
				{
					bool matches = Path.Equals(bfError.mFilePath, mFilePath);

					if (!matches)
					{
						delete bfError;
						continue;
					}
				}

				if (!bfError.mIsDeferred)
					hadNonDeferredErrors = true;

				for (int32 moreInfoIdx < bfError.mMoreInfoCount)
				{
					BfPassInstance.BfError moreInfo = new BfPassInstance.BfError();
					processingPassInstance.GetMoreInfoErrorData(errorIdx, moreInfoIdx, moreInfo);
					if (bfError.mMoreInfo == null)
						bfError.mMoreInfo = new List<BfPassInstance.BfError>();
					bfError.mMoreInfo.Add(moreInfo);
				}

                mErrorList.Add(bfError);
            }

            if (processResolveCharData != null)
            {
                IdSpan dupSpan = IdSpan();

                for (var bfError in mErrorList)
                {
                    int srcStart = bfError.mSrcStart;
                    int srcEnd = bfError.mSrcEnd;

                    if ((bfError.mWhileSpecializing.HasFlag(.Type)) && (mEmbedKind == .None))
					    continue;
					if ((bfError.mWhileSpecializing.HasFlag(.Method)) && (mEmbedKind != .Method))
						continue;

					if ((bfError.mIsDeferred) && (hadNonDeferredErrors))
						continue;

                    if (bfError.mIsPersistent)
                    {
                        if (bfError.mIdSpan.IsEmpty)
                        {
                            // New error - set current span
                            if (dupSpan.IsEmpty)
							{
	                        	dupSpan = processCharIdSpan.Duplicate();
								bfError.mOwnsSpan = true;
							}

                            bfError.mIdSpan = dupSpan;
                        }
                        else
                        {
                            // Old error - remap position
                            int32 startId = bfError.mIdSpan.GetIdAtIndex(srcStart);
                            int32 endId = bfError.mIdSpan.GetIdAtIndex(srcEnd);

                            srcStart = processCharIdSpan.GetIndexFromId(startId);
                            srcEnd = processCharIdSpan.GetIndexFromId(endId);
                        }
                    }

                    if (srcStart == -1)
                        continue;
                    srcEnd = Math.Min(srcEnd, processResolveCharData.Count);
                    
                    uint8 flagsOr = bfError.mIsWarning ? (uint8)SourceElementFlags.Warning : (uint8)SourceElementFlags.Error;
                    if (bfError.mIsAfter)
                        flagsOr |= (uint8)SourceElementFlags.IsAfter;                    
                    for (int i = srcStart; i < srcEnd; i++)
                        processResolveCharData[i].mDisplayFlags |= flagsOr;                    
                }
            }

			if ((mRefireMouseOverAfterRefresh) && (!mWantsFullRefresh))
			{
				mRefireMouseOverAfterRefresh = false;
				DarkTooltipManager.RefireMouseOver();
			}
        }

        void UpdateHasChangedSinceLastCompile()
        {
            mHasChangedSinceLastCompile = false;
            if (mProjectSource == null)
                return;
            int compileIdx = IDEApp.sApp.mWorkspace.GetHighestSuccessfulCompileIdx();
            var projectSourceCompileInstance = IDEApp.sApp.mWorkspace.GetProjectSourceCompileInstance(mProjectSource, compileIdx);
            if (projectSourceCompileInstance == null)
                return;
            //var sourceCharIdData = mEditWidget.Content.mTextIdData;
			mEditWidget.Content.mData.mTextIdData.Prepare();
            mHasChangedSinceLastCompile = !mEditWidget.Content.mData.mTextIdData.Equals(projectSourceCompileInstance.mSourceCharIdData);            
        }

        float GetEditX()
        {
			if ((!gApp.mSettings.mEditorSettings.mShowLineNumbers) || (mEmbedKind != .None))
				return GS!(24);

            var font = IDEApp.sApp.mTinyCodeFont;

            float lineWidth = Math.Max(font.GetWidth(ToStackString!(mEditWidget.Content.GetLineCount())) + GS!(24), GS!(32));
            return Math.Max(GS!(24), lineWidth);
        }

        protected override void ResizeComponents()
        {
			float topY = 0;
			if (mSplitBottomPanel == null)
			{
				if (mNavigationBar != null)
				{
	            	mNavigationBar.Resize(GS!(2), 0, Math.Max(mWidth - GS!(2), 0), GS!(22));
					topY = GS!(24);
				}
			}
			else
			{
				mNavigationBar?.mVisible = false;
			}

			// Always leave enough to read the first 3 lines
			if ((mHeight < GS!(88)) && (mSplitBottomPanel == null))
				mHeight = GS!(88);

			float splitterHeight = GS!(3);

			float splitTopHeight = 0;
			float splitBotHeight = Math.Max(mHeight - topY, 0);
			if (mSplitTopPanel != null)
			{
				float splitTotal = Math.Max(splitBotHeight - splitterHeight, 0);
				splitTopHeight = (int32)(splitTotal * mSplitter.mSplitPct);
				float minTopSize = 0;
				float minBotSize = GS!(64);
				splitTopHeight = Math.Clamp(splitTopHeight, minTopSize, Math.Max(splitTotal - minBotSize, 0));
				splitBotHeight = Math.Max(splitTotal - splitTopHeight, 0);
			}

			if (mSplitTopPanel != null)
			{								
				mSplitTopPanel.Resize(0, topY, mWidth, splitTopHeight);
				mSplitter.Resize(0, topY + splitTopHeight - 1, mWidth, splitterHeight + 2);
				topY += splitTopHeight + splitterHeight;
			}
			else if (mSplitTopPanel == null)
			{
				mSplitter?.Resize(mWidth - GS!(20), GS!(22 - 1), GS!(20), splitterHeight + GS!(2));
			}						

			mSplitter?.SetVisible(mPanelHeader == null);

            float editX = GetEditX();
            if (mPanelHeader != null)
            {
                mPanelHeader.Resize(editX, topY, mWidth - editX, GS!(mPanelHeader.mBaseHeight));
                mEditWidget.Resize(editX, topY + GS!(mPanelHeader.mBaseHeight), mWidth - editX, splitBotHeight - GS!(mPanelHeader.mBaseHeight));
            }
            else
                mEditWidget.Resize(editX, topY, Math.Max(mWidth - editX, 0), splitBotHeight);
			mEditWidget.SetVisible(mEditWidget.mHeight > 0);
			mEditWidget.mClipGfx = mEditWidget.mHeight < GS!(20);

            float rightCornerY = mEditWidget.mY + GS!(2);

            if (mRenameSymbolDialog != null)
            {
                float renameWidth = GS!(240);
                float renameHeight = GS!(56);
                mRenameSymbolDialog.Resize(mWidth - renameWidth - GS!(10), mEditWidget.mY + GS!(2), renameWidth, renameHeight);
                rightCornerY += renameHeight + GS!(20);
            }

            if (mQuickFind != null)
            {
                /*float findWidth = GS!(200);
                float findHeight = mQuickFind.mIsReplace ? GS!(84) : GS!(58);
                mQuickFind.Resize(mWidth - findWidth - GS!(10), mEditWidget.mY + GS!(2), findWidth, findHeight);*/
				mQuickFind.ResizeSelf();
                //rightCornerY += findHeight + GS!(20);
            }

            if (mOldVersionPanel != null)    
			{	        	
	        	mOldVersionPanel.Resize(0, 0, mWidth, mHeight);
				if (mSplitTopPanel != null)
					mSplitTopPanel.mVisible = false;
				mEditWidget.mVisible = false;
			}
			else if (mSplitTopPanel != null)
			{
				mSplitTopPanel.mVisible = true;
				mEditWidget.mVisible = true;

				//Why did we have this? There were some cases where some text was "peeking through" something, but setting this to false
				// breaks the normal split-window functionality
				//mEditWidget.mVisible = false;
			}
        }        

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			var ewc = (SourceEditWidgetContent)mEditWidget.Content;

			if ((btn == 0) && (x >= mEditWidget.mX - GS!(13)) && (x < mEditWidget.mX - GS!(0)) && (y < mHeight - GS!(20)))
			{
				int lineClick = GetLineAt(0, y);
				if (lineClick >= mCollapseRegionView.mLineStart)
				{
					int relLine = lineClick - mCollapseRegionView.mLineStart;
					if ((relLine < mCollapseRegionView.mCollapseIndices.Count) && (!ewc.mOrderedCollapseEntries.IsEmpty))
					{
						uint32 collapseVal = mCollapseRegionView.mCollapseIndices[relLine];
						if ((((collapseVal & CollapseRegionView.cStartFlag) != 0) && (btnCount == 1)) ||
							(btnCount > 1))
						{
							int collapseIndex = collapseVal & CollapseRegionView.cIdMask;

							var entry = ewc.mOrderedCollapseEntries[collapseIndex];
							ewc.SetCollapseOpen(collapseIndex, !entry.mIsOpen);
						}
						return;
					}
				}
			}

			if (mSplitBottomPanel != null)
			{
				return;
			}

			float lockX = mEditWidget.mX - GS!(20);
			float lockY = mHeight - GS!(20);

			if ((mEmbedKind == .None) && (Rect(lockX, lockY, GS!(20), GS!(20)).Contains(x, y)))
			{
				Menu menu = new Menu();
				var menuItem = menu.AddItem("Lock Editing");
				if (gApp.mSettings.mEditorSettings.mLockEditing)
					menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						gApp.mSettings.mEditorSettings.mLockEditing = !gApp.mSettings.mEditorSettings.mLockEditing;
					});

				if (mProjectSource != null)
				{
					menuItem = menu.AddItem("Lock Project");
					if (mProjectSource.mProject.mLocked)
						menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
					menuItem.mOnMenuItemSelected.Add(new (item) =>
						{
							if (mProjectSource.mProject.mLocked && mProjectSource.mProject.mLockedDefault)
							{
								let dialog = new DarkDialog("Unlock Project?",
									"This project is locked because it may be a shared library, and editing shared libraries may have unwanted effects on other programs that use it.\n\nAre you sure you want to unlock it?",
									DarkTheme.sDarkTheme.mIconWarning);
								dialog.mWindowFlags |= .Modal;
								dialog.AddYesNoButtons(new (dlg) =>
									{
										mProjectSource.mProject.mLocked = false;
										gApp.mWorkspace.SetChanged();
									});
								dialog.PopupWindow(mWidgetWindow);
								return;
							}
							mProjectSource.mProject.mLocked = !mProjectSource.mProject.mLocked;
							gApp.mWorkspace.SetChanged();
						});
				}

				menuItem = menu.AddItem("Lock while Debugging");
				void AddLockType(String name, Settings.EditorSettings.LockWhileDebuggingKind kind)
				{
					var subItem = menuItem.AddItem(name);
					if (gApp.mSettings.mEditorSettings.mLockEditingWhenDebugging == kind)
						subItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
					subItem.mOnMenuItemSelected.Add(new (evt) =>
						{
							gApp.mSettings.mEditorSettings.mLockEditingWhenDebugging = kind;
						});
				}

				AddLockType("Never", .Never);
				AddLockType("Always", .Always);
				AddLockType("When not Hot Swappable", .WhenNotHotSwappable);

				MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
				menuWidget.Init(this, x, y, .AllowScrollable);
			}
		}

		public override void MouseUp(float x, float y, int32 btn)
		{
			base.MouseUp(x, y, btn);

			if (mIsDraggingLinePointer)
			{
				mIsDraggingLinePointer = false;

				float origX;
				float origY;
				RootToSelfTranslate(mWidgetWindow.mMouseDownX, mWidgetWindow.mMouseDownY, out origX, out origY);

				int newLine = GetLineAt(0, y);
				if (newLine >= 0 && newLine != GetLineAt(origX, origY))
				{
					gApp.mDebugger.SetNextStatement(false, mFilePath, (int)newLine, 0);

					gApp.[Friend]PCChanged();
					gApp.[Friend]DebuggerUnpaused();
				}
			}
		}

		public int GetLineAt(float x, float y)
		{
			if (x > mEditWidget.mX - GS!(4))
				return -1;

			var ewc = (SourceEditWidgetContent)mEditWidget.Content;

			float relY = y - mEditWidget.mY - mEditWidget.Content.Y - GS!(3);
			if (relY < 0)
				return -1;
			int resultIdx = ewc.mLineCoords.BinarySearch(relY);
			if (resultIdx < 0)
				return ~resultIdx - 1;
			return resultIdx;
		}

		public bool SelectBreakpointsAtLine(int selectLine)
		{
			if (selectLine == -1)
				return false;

			HashSet<Breakpoint> selectedBreakpoints = scope HashSet<Breakpoint>();

			for (var breakpointView in GetTrackedElementList())
			{
			    var trackedElement = breakpointView.mTrackedElement;
			    var breakpoint = trackedElement as Breakpoint;
			    if (breakpoint != null)
			    {
					int drawLineNum = RemapActiveLineToHotLine(breakpoint.mLineNum);
					if (selectLine == drawLineNum)
					{
						selectedBreakpoints.Add(breakpoint);
					}
				}
			}

			if (selectedBreakpoints.IsEmpty)
				return false;
			
			gApp.mBreakpointPanel.Update();
			gApp.mBreakpointPanel.SelectBreakpoints(selectedBreakpoints);
			return true;
		}

		public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
		{
			base.MouseClicked(x, y, origX, origY, btn);

			if (btn == 0)
			{
				if ((x >= GS!(3)) && (x < mEditWidget.mX - GS!(14)))
				{
					int lineMouseDown = GetLineAt(origX, origY);
					int lineClick = GetLineAt(x, y);
					if (lineClick >= 0 && lineMouseDown == lineClick)
					{
						ToggleBreakpointAt(lineClick, 0);
					}
					return;
				}
			}

			if (btn == 1)
			{
				int lineClick = GetLineAt(x, y);
				if (SelectBreakpointsAtLine(lineClick))
				{
#unwarn
					var menuWidget = gApp.mBreakpointPanel.ShowRightClickMenu(this, x, y, true);
				}
			}
		}

		public override void MouseLeave()
		{
			base.MouseLeave();
			mMousePos = null;
		}

		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);
			mMousePos = .(x, y);

			if (!mIsDraggingLinePointer && IDEApp.sApp.mExecutionPaused && gApp.mDebugger.mActiveCallStackIdx == 0 && mMouseFlags.HasFlag(.Left))
			{         
				SourceEditWidgetContent ewc = (.)mEditWidget.Content;
				Rect linePointerRect = .(
					mEditWidget.mX - GS!(20) - sDrawLeftAdjust,
					0 + ewc.GetLineY(mLinePointerDrawData.mLine, 0),
					GS!(15),
					GS!(15)
				);

				float origX;
                float origY;
                RootToSelfTranslate(mWidgetWindow.mMouseDownX, mWidgetWindow.mMouseDownY, out origX, out origY);

				if (linePointerRect.Contains(origX, origY - mEditWidget.mY - mEditWidget.Content.Y - GS!(3)))
				{
					mIsDraggingLinePointer = true;
				}
			}
			else if (mIsDraggingLinePointer)
			{
				SourceEditWidgetContent ewc = (.)mEditWidget.Content;
				float linePos = ewc.GetLineY(GetLineAt(0, mMousePos.Value.y), 0);
				Rect visibleRange = mEditWidget.GetVisibleContentRange();

				if (visibleRange.Top > linePos)
					mEditWidget.mVertScrollbar.ScrollTo(linePos);
				else if (visibleRange.Bottom - mEditWidget.mHorzScrollbar.mHeight < linePos)
					mEditWidget.mVertScrollbar.ScrollTo(linePos - visibleRange.mHeight + ewc.GetLineHeight(0));
			}
		}

        public override void DrawAll(Graphics g)
        {
			DarkEditWidgetContent darkEditWidgetContent = (DarkEditWidgetContent)mEditWidget.Content;
			darkEditWidgetContent.mViewWhiteSpaceColor = gApp.mViewWhiteSpace.Bool ? darkEditWidgetContent.mTextColors[(int)SourceElementType.VisibleWhiteSpace] : 0;

			/*using (g.PushColor(0x50FFFFFF))
				g.FillRect(0, 0, mWidth, mHeight);*/

            //IDEApp.sApp.mBfResolveSystem.PerfZoneStart("SourceViewPanel.DrawAll");
            base.DrawAll(g);            

            if (mHotFileIdx != -1)
            {                
                /*using (g.PushColor(0x50D0D0D0))                
                    g.FillRect(0, 0, mWidth, mHeight);*/
            }

            //using (g.PushClip(0, 0, mWidth, mHeight))
            //{
            //    using (g.PushTranslate(0, mEditWidget.Content.Y))
            //    {
            //        foreach (var breakpointView in GetTrackedElementList())
            //        {
            //            /*var breakpoint = breakpointView.mTrackedElement as Breakpoint;
            //            using (g.PushColor((breakpoint.GetAddress() != 0) ? Color.White : 0x8080FFFF))
            //            {                            
            //                var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            //                float breakX;
            //                float breakY;
            //                sourceEditWidgetContent.GetTextCoordAtLineChar(breakpoint.mLineNum, breakpoint.mColumn, out breakX, out breakY);
            //                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RedDot), mEditWidget.mX + breakX, mEditWidget.mY + breakY);
            //            }*/

            //            var historyEntry = breakpointView.mTrackedElement as HistoryEntry;
            //            if (historyEntry != null)
            //            {
            //                using (g.PushColor(0x8080FFFF))
            //                {
            //                    var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            //                    float breakX;
            //                    float breakY;
            //                    sourceEditWidgetContent.GetTextCoordAtLineChar(historyEntry.mLineNum, historyEntry.mColumn, out breakX, out breakY);
            //                    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.IconBookmark), mEditWidget.mX + breakX, mEditWidget.mY + breakY);
            //                }
            //            }
            //        }
            //    }
            //}

            //IDEApp.sApp.mBfResolveSystem.PerfZoneEnd();
        }
		

		public void WithTrackedElementsAtCursor<T>(List<T> trackedElementList, delegate void(T) func) where T : TrackedTextElement
		{
		    int lineIdx;
		    int lineCharIdx;
		    mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);

		    int32 idx = 0;            
		    while (idx < trackedElementList.Count)
		    {
		        var trackedElement = trackedElementList[idx];
		        if (trackedElement.mLineNum == lineIdx)
		        {
		            int prevSize = trackedElementList.Count;
		            func(trackedElement);                    
		            if (trackedElementList.Count >= prevSize)
		                idx++;
		        }
		        else
		            idx++;
		    }
		}

		public bool HasTextAtCursor()
		{
			let ewc = mEditWidget.mEditWidgetContent;
			int textPos = mEditWidget.mEditWidgetContent.CursorTextPos;
			if (textPos >= ewc.mData.mTextLength)
				return false;

			for (int offset = -1; offset <= 0; offset++)
			{
				int checkPos = textPos + offset;
				if (checkPos < 0)
					continue;

				let c = ewc.mData.mText[checkPos].mChar;
				if ((c.IsLetterOrDigit) || (c == '_') || (c == '@'))
					return true;
				let elementType = (SourceElementType)ewc.mData.mText[checkPos].mDisplayTypeId;
				if (elementType == .Method)
					return true;
			}

			return false;
		}

		public void CheckEmitRevision()
		{
			if (mEmitRevision != -1)
			{
				var compiler = gApp.mBfResolveCompiler;

				if ((compiler != null) && (!compiler.IsPerformingBackgroundOperation()))
				{
					compiler.mBfSystem.Lock(0);
					int32 version = compiler.GetEmitVersion(mFilePath);
					compiler.mBfSystem.Unlock();

					if ((version >= 0) && (version != mEmitRevision))
					{
						mEmitRevision = version;
						Reload();
					}
				}
			}
		}

		public SourceViewPanel GetFocusedEmbeddedView()
		{
			if (mEditWidget.mHasFocus)
				return this;

			var editWidget = mWidgetWindow.mFocusWidget as SourceEditWidget;
			if (editWidget != null)
			{
				var sewc = editWidget.mEditWidgetContent as SourceEditWidgetContent;
				if (sewc.mSourceViewPanel?.mEmbedParent == this)
					return sewc.mSourceViewPanel;
			}

			return this;
		}

		public bool AddExplicitEmitType(StringView typeName)
		{
			for (var checkTypeName in mExplicitEmitTypes)
			{
				if (IDEUtils.GenericEquals(checkTypeName, typeName))
				{
					if (checkTypeName == typeName)
						return false;

					checkTypeName.Set(typeName);
					return true;
				}
			}

			mExplicitEmitTypes.Add(new .(typeName));
			return true;
		}
    }
}
