using System;
using Beefy.theme.dark;
using Beefy.gfx;
using System.Collections;
using System.Diagnostics;
using Beefy.geom;

namespace IDE.ui
{
	class SettingsDialog : PropertiesDialog
	{
		enum CategoryType
		{
			UI,
		    Editor,
			Keys,
			Compiler,
		    Debugger,
		    VisualStudio,
			Wasm,

		    COUNT
		}

		PropPage[] mPropPages = new PropPage[(int32)CategoryType.COUNT] ~
		{
			for (let propPage in _)
				delete propPage;
			delete _;
		};

		[Reflect]
		class KeyEntry
		{
			public String mCommand = new String() ~ delete _;
			public List<KeyState> mKeys = new List<KeyState>() ~ DeleteContainerAndItems!(_);
			public IDECommand.ContextFlags mContextFlags;
			public bool mHasConflict;
		}

		List<KeyEntry> mKeyEntries ~ DeleteContainerAndItems!(_);
		bool mHideVSHelper;

		protected override float TopY
		{
			get
			{
				return GS!(6);
			}
		}

		public this()
		{
		    mTitle = new String("Settings Properties");

		    var root = (DarkListViewItem)mCategorySelector.GetRoot();
		    var item = AddCategoryItem(root, "UI");
		    item.Focused = true;
			AddCategoryItem(root, "Editor");
			AddCategoryItem(root, "Keys");
			AddCategoryItem(root, "Compiler");
		    AddCategoryItem(root, "Debugger");
			AddCategoryItem(root, "Visual Studio");
			AddCategoryItem(root, "Wasm");

			if (!gApp.mSettings.mVSSettings.IsConfigured())
				gApp.mSettings.mVSSettings.SetDefaults();
			if (gApp.mSettings.mVSSettings.IsConfigured())
				mHideVSHelper = true;
		}

		void PopulateUIOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mUISettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (category, propEntry) = AddPropertiesItem(root, "General");
			category.mIsBold = true;
			category.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, cHeaderColor);

			AddPropertiesItem(category, "Scale", "mScale");
			AddPropertiesItem(category, "Theme", "mTheme");
			AddPropertiesItem(category, "Insert New Tabs", "mInsertNewTabs");
			AddPropertiesItem(category, "Show Startup Panel", "mShowStartupPanel");

			category.Open(true, true);
		}

		void PopulateEditorOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mEditorSettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (category, propEntry) = AddPropertiesItem(root, "General");
			category.mIsBold = true;
			category.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, cHeaderColor);
			AddPropertiesItem(category, "Font", "mFonts");
			AddPropertiesItem(category, "Font Size", "mFontSize");
			AddPropertiesItem(category, "Autocomplete", "mAutoCompleteShowKind");
			AddPropertiesItem(category, "Autocomplete Require Control", "mAutoCompleteRequireControl");
			AddPropertiesItem(category, "Autocomplete Require Tab", "mAutoCompleteRequireTab");
			AddPropertiesItem(category, "Autocomplete on Enter", "mAutoCompleteOnEnter");
			AddPropertiesItem(category, "Autocomplete Show Documentation", "mAutoCompleteShowDocumentation");
			AddPropertiesItem(category, "Fuzzy Autocomplete", "mFuzzyAutoComplete");
			AddPropertiesItem(category, "Show Locator Animation", "mShowLocatorAnim");
			AddPropertiesItem(category, "Hilite Symbol at Cursor", "mHiliteCursorReferences");
			AddPropertiesItem(category, "Hilite Current Line", "mHiliteCurrentLine");

			(?, propEntry) = AddPropertiesItem(category, "Spell Check", "mSpellCheckEnabled");
			var resetButton = new DarkButton();
			resetButton.Label = "Reset";
			resetButton.mOnMouseClick.Add(new (btn) =>
				{
					SpellChecker.ResetWordList();
					if (gApp.mSpellChecker != null)
					{
						delete gApp.mSpellChecker;
						gApp.CreateSpellChecker();
					}
				});
			var valueItem = propEntry.mListViewItem.GetSubItem(1);
			valueItem.AddWidget(resetButton);
			valueItem.mOnResized.Add(new (evt) =>
				{
					float btnWidth = DarkTheme.sDarkTheme.mSmallFont.GetWidth(resetButton.Label) + GS!(20);
					resetButton.Resize(GetValueEditWidth(valueItem) - btnWidth - GS!(28), GS!(1), btnWidth, GS!(16));
				});

			AddPropertiesItem(category, "Show Line Numbers", "mShowLineNumbers");
			AddPropertiesItem(category, "Free Cursor Movement", "mFreeCursorMovement");
			AddPropertiesItem(category, "Enable File Recovery", "mEnableFileRecovery");
			AddPropertiesItem(category, "Sync with Workspace Panel", "mSyncWithWorkspacePanel");
			category.Open(true, true);

			(category, propEntry) = AddPropertiesItem(root, "Formatting");
			category.mIsBold = true;
			category.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, cHeaderColor);
			AddPropertiesItem(category, "Format on Save", "mFormatOnSave");
			AddPropertiesItem(category, "Tabs or Spaces", "mTabsOrSpaces");
			AddPropertiesItem(category, "Tab Size", "mTabSize");
			AddPropertiesItem(category, "Wrap Comments at Column", "mWrapCommentsAt");
			AddPropertiesItem(category, "Indent Case Labels", "mIndentCaseLabels");
			AddPropertiesItem(category, "Left Align Preprocessor", "mLeftAlignPreprocessor");
			category.Open(true, true);
		}

		void PopulateCompilerOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mCompilerSettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (category, propEntry) = AddPropertiesItem(root, "General");
			category.mIsBold = true;
			category.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, cHeaderColor);
			AddPropertiesItem(category, "Worker Threads", "mWorkerThreads");
			category.Open(true, true);
		}

		void PopulateWasmOptions()
		{
			mCurPropertiesTarget = gApp.mSettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			var (category, propEntry) = AddPropertiesItem(root, "General");
			category.mIsBold = true;
			category.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, cHeaderColor);
			AddPropertiesItem(category, "Emscripten Path", "mEmscriptenPath", null, .BrowseForFolder);
			category.Open(true, true);
		}

		void CommandContextToString(IDECommand.ContextFlags contextFlags, String str)
		{
			bool isFirst = true;

			void AddFlagStr(StringView flagStr)
			{
				if (isFirst)
				{
					str.Append(", ");
					isFirst = false;
				}
				str.Append(flagStr);
			}

			if (contextFlags.HasFlag(.Editor))
				AddFlagStr("Editor");
		}

		void UpdateKeyStates()
		{
			Debug.Assert((CategoryType)mPropPage.mCategoryType == .Keys);

			Dictionary<String, KeyEntry> mappedEntries = scope .();

			for (var propEntries in mPropPage.mPropEntries.Values)
			{
				var propEntry = propEntries[0];
				let keyEntry = (KeyEntry)propEntry.mTarget;
				keyEntry.mHasConflict = false;

				let keys = propEntry.mCurValue.Get<List<KeyState>>();
				if (keys.Count == 0)
					continue;

				let keyEntryStr = new String();
				KeyState.ToString(keys, keyEntryStr);
				keyEntryStr.Append(" ");
				CommandContextToString(keyEntry.mContextFlags, keyEntryStr);

				String* keyPtr;
				KeyEntry* valuePtr;
				if (mappedEntries.TryAdd(keyEntryStr, out keyPtr, out valuePtr))
				{
					*valuePtr = keyEntry;
				}
				else
				{
					let other = *valuePtr;
					other.mHasConflict = true;
					keyEntry.mHasConflict = true;
					delete keyEntryStr;
				}
			}

			// Do chord prefix search
			for (var propEntries in mPropPage.mPropEntries.Values)
			{
				var propEntry = propEntries[0];
				let keyEntry = (KeyEntry)propEntry.mTarget;
				let origKeys = propEntry.mCurValue.Get<List<KeyState>>();
				var keys = scope List<KeyState>(origKeys.GetEnumerator());
				while (keys.Count > 1)
				{
					keys.PopBack();
					let keyEntryStr = scope String();
					KeyState.ToString(keys, keyEntryStr);
					keyEntryStr.Append(" ");
					CommandContextToString(keyEntry.mContextFlags, keyEntryStr);

					if (mappedEntries.TryGet(keyEntryStr, var keyPtr, var valuePtr))
					{
						let other = valuePtr;
						other.mHasConflict = true;
						keyEntry.mHasConflict = true;
					}
				}
			}

			for (let keyEntryStr in mappedEntries.Keys)
				delete keyEntryStr;

			for (var propEntries in mPropPage.mPropEntries.Values)
			{
				var propEntry = propEntries[0];
				let keyEntry = (KeyEntry)propEntry.mTarget;

				let listViewItem = (DarkListViewItem)propEntry.mListViewItem.GetSubItem(1);
				listViewItem.mTextColor = Color.Mult(DarkTheme.COLOR_TEXT, keyEntry.mHasConflict ? 0xFFFF8080 : 0xFFFFFFFF);
			}
		}

		void PopulateKeyOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mKeySettings;
			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			
			var map = scope Dictionary<String, KeyEntry>();
			mKeyEntries = new List<KeyEntry>();

			for (var kv in gApp.mCommands.mCommandMap)
			{
				var keyEntry = new KeyEntry();
				keyEntry.mCommand.Set(kv.key);
				keyEntry.mContextFlags = kv.value.mContextFlags;
				
				mKeyEntries.Add(keyEntry);
				map[keyEntry.mCommand] = keyEntry;
			}

			for (var mappedEntry in gApp.mSettings.mKeySettings.mEntries)
			{
				KeyEntry keyEntry;
				if (!map.TryGetValue(mappedEntry.mCommand, out keyEntry))
					continue;

				for (let keyState in mappedEntry.mKeys)
					keyEntry.mKeys.Add(keyState.Clone());
			}

			mKeyEntries.Sort(scope (lhs, rhs) => String.Compare(lhs.mCommand, rhs.mCommand, true));

			for (var keyEntry in mKeyEntries)
			{
				mCurPropertiesTarget = keyEntry;
				var (category, propEntry) = AddPropertiesItem(root, keyEntry.mCommand, "mKeys");
			}

			UpdateKeyStates();
		}

		void PopulateVSOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mVSSettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			/*var (category, propEntry) = AddPropertiesItem(root, "General");
			category.mIsBold = true;
			category.mTextColor = cHeaderColor;*/

			var (category, propEntry) = AddPropertiesItem(root, "Tool Path (x86)", "mBin32Path", null, .BrowseForFolder);
			propEntry.mRelPath = new String(gApp.mInstallDir);
			(category, propEntry) = AddPropertiesItem(root, "Tool Path (x64)", "mBin64Path", null, .BrowseForFolder);
			propEntry.mRelPath = new String(gApp.mInstallDir);
			(category, propEntry) = AddPropertiesItem(root, "Library Paths (x86)", "mLib32Paths");
			propEntry.mRelPath = new String(gApp.mInstallDir);
			(category, propEntry) = AddPropertiesItem(root, "Library Paths (x64)", "mLib64Paths");
			propEntry.mRelPath = new String(gApp.mInstallDir);

			//category.Open(true, true);
		}

		void PopulateDebuggerOptions()
		{
			mCurPropertiesTarget = gApp.mSettings.mDebuggerSettings;

			var root = (DarkListViewItem)mPropPage.mPropertiesListView.GetRoot();
			
			AddPropertiesItem(root, "Use Symbol Servers", "mUseSymbolServers");
			AddPropertiesItem(root, "Symbol Cache Path", "mSymCachePath");
			AddPropertiesItem(root, "Symbol File Locations", "mSymbolSearchPath");
			AddPropertiesItem(root, "Auto Find Paths", "mAutoFindPaths");
			AddPropertiesItem(root, "Profile Sample Rate", "mProfileSampleRate");
			AddPropertiesItem(root, "Auto Evaluate Properties", "mAutoEvaluateProperties");
		}

		protected override void ResetSettings()
		{
			base.ResetSettings();

			var targetDict = scope Dictionary<Object, Object>();
			switch ((CategoryType)mPropPage.mCategoryType)
			{
			case .UI:
				Settings.UISettings uiSettings = scope .();
				uiSettings.SetDefaults();
				targetDict[gApp.mSettings.mUISettings] = uiSettings;
				UpdateFromTarget(targetDict);
			case .Editor:
				Settings.EditorSettings editorSettings = scope .();
				editorSettings.SetDefaults();
				targetDict[gApp.mSettings.mEditorSettings] = editorSettings;
				UpdateFromTarget(targetDict);
			case .Keys:
				Settings.KeySettings keySettings = scope .();
				keySettings.SetDefaults();
				Dictionary<String, KeyEntry> defaultKeyMap = scope .();
				for (var keySettingEntry in keySettings.mEntries)
				{
					KeyEntry keyEntry = scope:: .();
					keyEntry.mCommand.Set(keySettingEntry.mCommand);
					for (let keyState in keySettingEntry.mKeys)
						keyEntry.mKeys.Add(keyState.Clone());
					defaultKeyMap[keyEntry.mCommand] = keyEntry;
				}

				for (var keyEntry in mKeyEntries)
				{
					KeyEntry defaultKeyEntry;
					defaultKeyMap.TryGetValue(keyEntry.mCommand, out defaultKeyEntry);
					if (defaultKeyEntry == null)
					{
						defaultKeyEntry = scope:: .();
					}

					targetDict[keyEntry] = defaultKeyEntry;
				}
				UpdateFromTarget(targetDict);
			case .Compiler:
				Settings.CompilerSettings compilerSettings = scope .();
				compilerSettings.SetDefaults();
				targetDict[gApp.mSettings.mCompilerSettings] = compilerSettings;
				UpdateFromTarget(targetDict);
			case .Debugger:
				Settings.DebuggerSettings debuggerSettings = scope .();
				debuggerSettings.SetDefaults();
				targetDict[gApp.mSettings.mDebuggerSettings] = debuggerSettings;
				UpdateFromTarget(targetDict);
			case .VisualStudio:
				Settings.VSSettings vsSettings = scope .();
				vsSettings.SetDefaults();
				targetDict[gApp.mSettings.mVSSettings] = vsSettings;
				UpdateFromTarget(targetDict);
			default:
			}
		}

		protected override void ShowPropPage(int32 categoryTypeInt)
		{
			base.ShowPropPage(categoryTypeInt);

			let categoryType = (CategoryType)categoryTypeInt;

			if (mPropPages[(int32)categoryType] == null)
			{
				CreatePropPage(categoryTypeInt, .AllowSearch | .AllowReset);
				mPropPages[categoryTypeInt] = mPropPage;
				mPropPage.mPropertiesListView.InitScrollbars(false, true);
				//mPropPage.mPropertiesListView.mAutoFocus = true;
				mPropPage.mPropertiesListView.mShowColumnGrid = true;
				mPropPage.mPropertiesListView.mShowGridLines = true;

				switch ((CategoryType)categoryTypeInt)
				{
				case .UI:
					PopulateUIOptions();
				case .Editor:
					PopulateEditorOptions();
				case .Keys:
					PopulateKeyOptions();
				case .Compiler:
					PopulateCompilerOptions();
				case .Debugger:
					PopulateDebuggerOptions();
				case .VisualStudio:
					PopulateVSOptions();
				case .Wasm:
					PopulateWasmOptions();
				default:
					/*mCurPropertiesTarget = gApp.mSettings.mEditorSettings;
					PopulateEditorOptions();*/
				}
			}
			mPropPage = mPropPages[(int32)categoryType];
			AddPropPageWidget();
			ResizeComponents();
		}

		protected override void UpdatePropertyValue(PropEntry[] propEntries)
		{
			base.UpdatePropertyValue(propEntries);
			var propEntry = propEntries[0];
			if (propEntry.mTarget is KeyEntry)
				UpdateKeyStates();
		}

		protected override bool ApplyChanges()
		{
		    bool hadChanges = false;
            for (var propPage in mPropPages)
            {
                if (propPage == null)
                    continue;
				bool pageHadChanges = false;
                ApplyChanges(propPage, ref pageHadChanges);
				if (pageHadChanges)
				{
					hadChanges = true;
					if ((CategoryType)@propPage == .VisualStudio)
					{
						Settings.VSSettings defaultSettings = scope .();
						defaultSettings.SetDefaults();
						gApp.mSettings.mVSSettings.mManuallySet = !defaultSettings.Equals(gApp.mSettings.mVSSettings);
					}
				}
            }

			if (hadChanges)
			{
				if (mKeyEntries != null)
				{
					gApp.mSettings.mKeySettings.Clear();
					for (let inEntry in mKeyEntries)
					{
						if (inEntry.mKeys.IsEmpty)
							continue;

						let outEntry = new Settings.KeySettings.Entry();
						outEntry.mCommand = new String(inEntry.mCommand);

						let keyArr = new KeyState[inEntry.mKeys.Count];
						for (int i < inEntry.mKeys.Count)
							keyArr[i] = inEntry.mKeys[i].Clone();
						outEntry.mKeys = keyArr;

						gApp.mSettings.mKeySettings.mEntries.Add(outEntry);
					}
				}

				gApp.mSettings.Apply();
				gApp.mSettings.Save();
			}

		    return true;
		}

		public override void CalcSize()
		{
		    mWidth = GS!(660);
		    mHeight = GS!(512);
		}

		public override void Update()
		{
		    base.Update();
			CheckForChanges(mPropPages);

			if (mPropPage?.mCategoryType == 4)
			{
				if (mWidgetWindow.mFocusWidget?.HasParent(mPropPage.mPropertiesListView) == true)
				{
					mHideVSHelper = true;
				}
				
			}
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);

			if ((mPropPage?.mCategoryType == 4) && (!mHideVSHelper))
			{
				var rect = mPropPage.mPropertiesListView.GetRect();
				rect.Top += GS!(120);

				rect.Inflate(-GS!(20), -GS!(20));
				rect.mWidth -= GS!(10);

				g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
				String helpStr = "NOTE: These settings will be auto-detected if you have Visual Studio 2013 or later installed. You should not need to manually configure these settings.";
				rect.mHeight = g.mFont.GetWrapHeight(helpStr, rect.mWidth - GS!(40)) + GS!(40);

				using (g.PushClip(0, 0, mWidth, mPropPage.mPropertiesListView.mY + mPropPage.mPropertiesListView.mHeight))
				{
					using (g.PushColor(0x80000000))
						g.FillRect(rect.mX, rect.mY, rect.mWidth, rect.mHeight);

					g.DrawString(helpStr, rect.mX + GS!(20), rect.mY + GS!(20), .Left, rect.mWidth - GS!(40), .Wrap);
				}
			}
		}
	}
}
