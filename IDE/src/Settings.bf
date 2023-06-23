using System.Collections;
using System;
using Beefy.gfx;
using Beefy.geom;
using Beefy.widgets;
using System.Threading;
using Beefy.utils;
using IDE.util;
using Beefy.theme.dark;
using System.IO;
using IDE.ui;
using System.Diagnostics;

namespace IDE
{
	[Reflect(.All | .ApplyToInnerTypes)]
	class Settings
	{
		public class VSSettings
		{
			public String mBin32Path = new .() ~ delete _;
			public String mBin64Path = new .() ~ delete _;
			public List<String> mLib32Paths = new .() ~ DeleteContainerAndItems!(_);
			public List<String> mLib64Paths = new .() ~ DeleteContainerAndItems!(_);
			public bool mManuallySet;

			public void Serialize(StructuredData sd)
			{
				sd.Add("ManuallySet", mManuallySet);
				sd.Add("Bin32Path", mBin32Path);
				sd.Add("Bin64Path", mBin64Path);
				using (sd.CreateArray("Lib32Paths"))
				{
					for (let str in mLib32Paths)
						sd.Add(str);
				}
				using (sd.CreateArray("Lib64Paths"))
				{
					for (let str in mLib64Paths)
						sd.Add(str);
				}
			}

			public void Deserialize(StructuredData sd)
			{
				mManuallySet = sd.GetBool("ManuallySet");
				if ((!mManuallySet) && (!mBin64Path.IsEmpty))
					return;
				
				sd.GetString("Bin32Path", mBin32Path);
				sd.GetString("Bin64Path", mBin64Path);

				void ReadPaths(String pathName, List<String> paths)
				{
					HashSet<String> newPaths = scope .();
					List<String> prevPaths = scope .();
					for (var str in paths)
						prevPaths.Add(str);
					paths.Clear();

					for ( sd.Enumerate(pathName))
					{
						var str = new String();
						sd.GetCurString(str);
						if (newPaths.Add(str))
							paths.Add(str);
						else
							delete str;
					}

					for (var path in prevPaths)
					{
						if (!newPaths.Contains(path))
							paths.Add(path);
						else
							delete path;
					}
				}

				ReadPaths("Lib32Paths", mLib32Paths);
				ReadPaths("Lib64Paths", mLib64Paths);
			}

			[CLink, CallingConvention(.Stdcall)]
			static extern char8* VSSupport_Find();

			public bool IsConfigured()
			{
				return !mLib32Paths.IsEmpty || !mLib64Paths.IsEmpty || !mBin32Path.IsEmpty || !mBin64Path.IsEmpty;
			}

			public void SetDefaults()
			{
				mManuallySet = false;
#if BF_PLATFORM_WINDOWS
				StringView vsInfo = .(VSSupport_Find());

				ClearAndDeleteItems(mLib32Paths);
				ClearAndDeleteItems(mLib64Paths);

				for (var infoStr in vsInfo.Split('\n'))
				{
					if (infoStr.IsEmpty)
						continue;

					var infos = infoStr.Split('\t');
					switch (infos.GetNext().Get())
					{
					case "TOOL32":
						mBin32Path.Set(infos.GetNext());
					case "TOOL64":
						mBin64Path.Set(infos.GetNext());
					case "LIB32":
						mLib32Paths.Add(new String(infos.GetNext()));
					case "LIB64":
						mLib64Paths.Add(new String(infos.GetNext()));
					}
				}
#endif
			}

			bool Equals(List<String> lhs, List<String> rhs)
			{
				if (lhs.Count != rhs.Count)
					return false;
				for (int idx < lhs.Count)
					if (lhs[idx] != rhs[idx])
						return false;
				return true;
			}

			public bool Equals(VSSettings vsSettings)
			{
				return
					(Equals(mLib32Paths, vsSettings.mLib32Paths)) &&
					(Equals(mLib64Paths, vsSettings.mLib64Paths)) &&
					(mBin32Path == vsSettings.mBin32Path) &&
					(mBin64Path == vsSettings.mBin64Path);
			}
		}

		public class DebuggerSettings
		{
			public enum SymbolServerKind
			{
				Yes,
				No,
				Ask
			}

			public SymbolServerKind mUseSymbolServers = .Yes;
			public String mSymCachePath = new .("C:\\SymCache") ~ delete _;
			public List<String> mSymbolSearchPath = new .() ~ DeleteContainerAndItems!(_);
			public List<String> mAutoFindPaths = new .() ~ DeleteContainerAndItems!(_);
			public int32 mProfileSampleRate = 1000;
			public bool mAutoEvaluateProperties = false;

			public void Serialize(StructuredData sd)
			{
				sd.Add("UseSymbolServers", mUseSymbolServers);
				sd.Add("SymCachePath", mSymCachePath);
				using (sd.CreateArray("SymbolSearchPath"))
				{
					for (let str in mSymbolSearchPath)
						sd.Add(str);
				}
				using (sd.CreateArray("AutoFindPaths"))
				{
					for (let str in mAutoFindPaths)
						sd.Add(str);
				}

				using (sd.CreateArray("StepFilters"))
				{
					for (var stepFilter in gApp.mDebugger.mStepFilterList.Values)
					{
						if (!stepFilter.mIsGlobal)
							continue;
						if (stepFilter.mKind == .Filtered)
							sd.Add(stepFilter.mFilter);
					}
					sd.RemoveIfEmpty();
				}

				using (sd.CreateArray("StepNotFilters"))
				{
					for (var stepFilter in gApp.mDebugger.mStepFilterList.Values)
					{
						if (!stepFilter.mIsGlobal)
							continue;
						if (stepFilter.mKind == .NotFiltered)
							sd.Add(stepFilter.mFilter);
					}
					sd.RemoveIfEmpty();
				}
				sd.Add("ProfileSampleRate", mProfileSampleRate);
				sd.Add("AutoEvaluateProperties", mAutoEvaluateProperties);
			}

			public void Deserialize(StructuredData sd)
			{
				sd.Get("UseSymbolServers", ref mUseSymbolServers);
				sd.Get("SymCachePath", mSymCachePath);
				ClearAndDeleteItems(mSymbolSearchPath);
				for ( sd.Enumerate("SymbolSearchPath"))
				{
					var str = new String();
					sd.GetCurString(str);
					mSymbolSearchPath.Add(str);
				}
				ClearAndDeleteItems(mAutoFindPaths);
				for ( sd.Enumerate("AutoFindPaths"))
				{
					var str = new String();
					sd.GetCurString(str);
					mAutoFindPaths.Add(str);
				}

				if (gApp.mDebugger != null)
				{
					for ( sd.Enumerate("StepFilters"))
					{
						String filter = scope String();
						sd.GetCurString(filter);
						gApp.mDebugger.CreateStepFilter(filter, true, .Filtered);
					}

					for ( sd.Enumerate("StepNotFilters"))
					{
						String filter = scope String();
						sd.GetCurString(filter);
						gApp.mDebugger.CreateStepFilter(filter, true, .NotFiltered);
					}
				}
				sd.Get("ProfileSampleRate", ref mProfileSampleRate);
				sd.Get("AutoEvaluateProperties", ref mAutoEvaluateProperties);
			}

			public void Apply()
			{
#if CLI
				return;
#endif

#unwarn
				String symbolServerPath = scope String()..Join("\n", mSymbolSearchPath.GetEnumerator());
				if (mUseSymbolServers == .No)
				{
					gApp.mDebugger.SetSymSrvOptions("", "", .Disable);
				}
				gApp.mDebugger.SetSymSrvOptions(mSymCachePath, symbolServerPath, .None);

				String remapStr = scope .();
				for (var entry in mAutoFindPaths)
				{
					if (entry.Contains('@'))
					{
						if (!remapStr.IsEmpty)
							remapStr.Append("\n");
						remapStr.Append(entry);
					}
				}
				remapStr.Replace('@', '=');
				gApp.mDebugger.SetSourcePathRemap(remapStr);

				mProfileSampleRate = Math.Clamp(mProfileSampleRate, 10, 10000);
			}

			public void SetDefaults()
			{
				/*String appDataPath = scope String();
				Platform.GetStrHelper(appDataPath, scope (outPtr, outSize, outResult) =>
					{
						Platform.BfpDirectory_GetSysDirectory(.AppData_Local, outPtr, outSize,
				(Platform.BfpFileResult*)outResult);
					});*/

				mSymbolSearchPath.Add(new String("https://msdl.microsoft.com/download/symbols"));
				mAutoFindPaths.Add(new String(@"C:\Program Files (x86)\Microsoft Visual Studio*"));
				mAutoFindPaths.Add(new String(@"C:\Program Files (x86)\Windows Kits\10\Source"));
			}
		}

		public class CompilerSettings
		{
			public int32 mWorkerThreads = 6;

			public void Serialize(StructuredData sd)
			{
				sd.Add("WorkerThreads", mWorkerThreads);
			}

			public void Deserialize(StructuredData sd)
			{
				sd.Get("WorkerThreads", ref mWorkerThreads);
			}

			public void SetDefaults()
			{
				Platform.BfpSystemResult result;
				mWorkerThreads = Platform.BfpSystem_GetNumLogicalCPUs(&result);
			}
		}

		public class Colors
		{
			public Color mText = 0xFFFFFFFF;
			public Color mWindow = 0xFF44444D;
			public Color mBackground = 0xFF1C1C24;
			public Color mSelectedOutline = 0xFFCFAE11;
			public Color mMenuFocused = 0xFFE5A910;
			public Color mMenuSelected = 0xFFCB9B80;
			public Color mAutoCompleteSubText = 0xFFB0B0B0;
			public Color mAutoCompleteDocText = 0xFFC0C0C0;
			public Color mAutoCompleteActiveText = 0xFFB0B0FF;
			public Color mWorkspaceDisabledText = 0xFFA0A0A0;
			public Color mWorkspaceFailedText = 0xFFE04040;
			public Color mWorkspaceManualIncludeText = 0xFFE0E0FF;
			public Color mWorkspaceIgnoredText = 0xFF909090;
			public Color mWorkspaceCutText = 0xFFC0B0B0;

			public Color mCode = 0xFFFFFFFF;
			public Color mKeyword = 0xFFE1AE9A;
			public Color mLiteral = 0XFFC8A0FF;
			public Color mIdentifier = 0xFFFFFFFF;
			public Color mComment = 0xFF75715E;
			public Color mMethod = 0xFFA6E22A;
			public Color mType = 0xFF66D9EF;
			public Color mPrimitiveType = 0xFF66D9EF;
			public Color mStruct = 0xFF66D9EF;
			public Color mGenericParam = 0xFF66D9EF;
			public Color mRefType = 0xFF66A0EF;
			public Color mInterface = 0xFF9A9EEB;
			public Color mNamespace = 0xFF7BEEB7;
			public Color mDisassemblyText = 0xFFB0B0B0;
			public Color mDisassemblyFileName = 0XFFFF0000;
			public Color mError = 0xFFFF0000;
			public Color mBuildError = 0xFFFF8080;
			public Color mBuildWarning = 0xFFFFFF80;
			public Color mVisibleWhiteSpace = 0xFF9090C0;
			public Color mCurrentLineHilite = 0xFF4C4C54;
			public Color mCurrentLineNumberHilite = 0x18FFFFFF;
			public Color mCharPairHilite = 0x1DFFFFFF;

			public void Deserialize(StructuredData sd)
			{
				void GetColor(String name, ref Color color)
				{
					sd.Get(name, ref *(int32*)&color);

					if ((color & 0xFF000000) == 0)
						color |= 0xFF000000;
				}

				GetColor("Text", ref mText);
				GetColor("Window", ref mWindow);
				GetColor("Background", ref mBackground);
				GetColor("SelectedOutline", ref mSelectedOutline);
				GetColor("MenuFocused", ref mMenuFocused);
				GetColor("MenuSelected", ref mMenuSelected);
				GetColor("AutoCompleteSubText", ref mAutoCompleteSubText);
				GetColor("AutoCompleteDocText", ref mAutoCompleteDocText);
				GetColor("AutoCompleteActiveText", ref mAutoCompleteActiveText);
				GetColor("WorkspaceDisabledText", ref mWorkspaceDisabledText);
				GetColor("WorkspaceFailedText", ref mWorkspaceFailedText);
				GetColor("WorkspaceManualIncludeText", ref mWorkspaceManualIncludeText);
				GetColor("WorkspaceIgnoredText", ref mWorkspaceIgnoredText);
				GetColor("CurrentLineHilite", ref mCurrentLineHilite);

				GetColor("Code", ref mCode);
				GetColor("Keyword", ref mKeyword);
				GetColor("Literal", ref mLiteral);
				GetColor("Identifier", ref mIdentifier);
				GetColor("Comment", ref mComment);
				GetColor("Method", ref mMethod);
				if (sd.Contains("Type"))
				{
					GetColor("Type", ref mType);
					if (!sd.Contains("PrimitiveType"))
						mPrimitiveType = mType;
					if (!sd.Contains("Struct"))
						mStruct = mType;
					if (!sd.Contains("RefType"))
						mRefType = mType;
					if (!sd.Contains("Interface"))
						mInterface = mType;
					if (!sd.Contains("GenericParam"))
						mGenericParam = mType;
				}
				GetColor("PrimitiveType", ref mPrimitiveType);
				GetColor("Struct", ref mStruct);
				GetColor("RefType", ref mRefType);
				GetColor("Interface", ref mInterface);
				GetColor("GenericParam", ref mGenericParam);
				GetColor("Namespace", ref mNamespace);
				GetColor("DisassemblyText", ref mDisassemblyText);
				GetColor("DisassemblyFileName", ref mDisassemblyFileName);
				GetColor("Error", ref mError);
				GetColor("BuildError", ref mBuildError);
				GetColor("BuildWarning", ref mBuildWarning);
				GetColor("VisibleWhiteSpace", ref mVisibleWhiteSpace);
				GetColor("CurrentLineHilite", ref mCurrentLineHilite);
				GetColor("CurrentLineNumberHilite", ref mCurrentLineNumberHilite);
				GetColor("CharPairHilite", ref mCharPairHilite);
			}

			public void Apply()
			{
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Normal] = mCode;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Keyword] = mKeyword;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Literal] = mLiteral;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Identifier] = mIdentifier;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Comment] = mComment;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Method] = mMethod;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Type] = mType;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.PrimitiveType] = mPrimitiveType;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Struct] = mStruct;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.GenericParam] = mGenericParam;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.RefType] = mRefType;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Interface] = mInterface;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Namespace] = mNamespace;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Disassembly_Text] = mDisassemblyText;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Disassembly_FileName] = mDisassemblyFileName;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.Error] = mError;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.BuildError] = mBuildError;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.BuildWarning] = mBuildWarning;
				SourceEditWidgetContent.sTextColors[(.)SourceElementType.VisibleWhiteSpace] = mVisibleWhiteSpace;

				DarkTheme.COLOR_TEXT = mText;
				DarkTheme.COLOR_WINDOW = mWindow;
				DarkTheme.COLOR_BKG = mBackground;
				DarkTheme.COLOR_SELECTED_OUTLINE = mSelectedOutline;
				DarkTheme.COLOR_MENU_FOCUSED = mMenuFocused;
				DarkTheme.COLOR_MENU_SELECTED = mMenuSelected;
				DarkTheme.COLOR_CURRENT_LINE_HILITE = mCurrentLineHilite;
				DarkTheme.COLOR_CHAR_PAIR_HILITE = mCharPairHilite;
			}
		}

		public class UISettings
		{
			public enum InsertNewTabsKind
			{
				LeftOfExistingTabs,
				RightOfExistingTabs,
			}

			public Colors mColors = new .() ~ delete _;
			public float mScale = 100;
			public InsertNewTabsKind mInsertNewTabs = .LeftOfExistingTabs;
			public List<String> mTheme = new .() ~ DeleteContainerAndItems!(_);
			public bool mShowStartupPanel = true;

			public void SetDefaults()
			{
				DeleteAndNullify!(mColors);
				mColors = new .();
				mScale = 100;
				mInsertNewTabs = .LeftOfExistingTabs;
				ClearAndDeleteItems(mTheme);
				mShowStartupPanel = true;
			}

			public void Apply()
			{
				DeleteAndNullify!(mColors);
				mColors = new .();

				if (DarkTheme.sDarkTheme == null)
					return;

				for (int scale < 3)
					DarkTheme.sDarkTheme.mUIFileNames[scale].Clear();

				void LoadTheme(StringView themeFilePath)
				{
					if (!File.Exists(themeFilePath))
						return;

					StructuredData sd = scope .();
					if (sd.Load(themeFilePath) case .Err(var err))
					{
						gApp.OutputErrorLine($"Failed to load theme file '{themeFilePath}': {err}");
						return;
					}

					using (sd.Open("Colors"))
						mColors.Deserialize(sd);
				}

				String imgCreatePath = scope String(gApp.mInstallDir, "images/ImgCreate.exe");
				let imgCreateExeTime = File.GetLastWriteTime(imgCreatePath).GetValueOrDefault();

				for (let theme in mTheme)
				{
					String relPath = scope .()..Append(gApp.mInstallDir, "themes/");
					String absPath = scope .();
					Path.GetAbsolutePath(theme, relPath, absPath);

					if (absPath.EndsWith(".TOML", .OrdinalIgnoreCase))
					{
						LoadTheme(absPath);
						continue;
					}

					absPath.Append("/");

					bool needsRebuild = false;
					let origImageTime = File.GetLastWriteTime(scope String(absPath, "../../images/DarkUI.png")).GetValueOrDefault();

					if (origImageTime != default)
					{
						DateTime maxSrcImgTime = default;
						DateTime minDestImgTime = default;

						for (int scale < 3)
						{
							String srcImgPath = scope .(absPath);
							String srcImgPath2 = scope .(absPath);
							String destImgPath = scope .(absPath);
							switch (scale)
							{
							case 0:
								srcImgPath.Append("UI.psd");
								srcImgPath2.Append("UI.png");
								destImgPath.Append("cache/UI.png");
							case 1:
								srcImgPath.Append("UI_2.psd");
								srcImgPath2.Append("UI_2.png");
								destImgPath.Append("cache/UI_2.png");
							case 2:
								srcImgPath.Append("UI_4.psd");
								srcImgPath2.Append("UI_2.png");
								destImgPath.Append("cache/UI_2.png");
							}
							maxSrcImgTime = Math.Max(maxSrcImgTime, Math.Max(File.GetLastWriteTime(srcImgPath).GetValueOrDefault(), File.GetLastWriteTime(srcImgPath2).GetValueOrDefault()));
							let destImageTime = File.GetLastWriteTime(destImgPath).GetValueOrDefault();
							if (scale == 0)
								minDestImgTime = destImageTime;
							else
								minDestImgTime = Math.Min(minDestImgTime, destImageTime);
						}

						if (maxSrcImgTime > minDestImgTime)
							needsRebuild = true;
						if (origImageTime > minDestImgTime)
							needsRebuild = true;
						if (imgCreateExeTime > minDestImgTime)
							needsRebuild = true;
					}

					if (needsRebuild)
					{
						ProcessStartInfo procInfo = scope ProcessStartInfo();
						procInfo.UseShellExecute = false;
						procInfo.RedirectStandardError = true;
						procInfo.RedirectStandardOutput = true;
						procInfo.SetFileName(imgCreatePath);
						procInfo.SetWorkingDirectory(absPath);
						procInfo.CreateNoWindow = true;

						SpawnedProcess process = scope SpawnedProcess();
						if (process.Start(procInfo) case .Ok)
						{
							//Windows.MessageBoxA(default, "Rebuilding theme images...", "Rebuilding Theme", Windows.MB_OK);
							process.WaitFor();
						}
					}

					for (int scale < 3)
					{
						String imgPath = scope .(absPath);
						switch (scale)
						{
						case 0: imgPath.Append("cache/UI.png");
						case 1: imgPath.Append("cache/UI_2.png");
						case 2: imgPath.Append("cache/UI_4.png");
						}

						if (File.Exists(imgPath))
						{
							DarkTheme.sDarkTheme.mUIFileNames[scale].Set(imgPath);
						}
					}

					String themeFilePath = scope .()..Append(absPath, "theme.toml");
					LoadTheme(themeFilePath);
					String userFilePath = scope .()..Append(absPath, "user.toml");
					LoadTheme(userFilePath);
				}

				mColors.Apply();
			}

			public void Serialize(StructuredData sd)
			{
				sd.Add("Scale", mScale);
				using (sd.CreateArray("Theme"))
				{
					for (let str in mTheme)
						sd.Add(str);
				}
				sd.Add("InsertNewTabs", mInsertNewTabs);
				sd.Add("ShowStartupPanel", mShowStartupPanel);
			}

			public void Deserialize(StructuredData sd)
			{
				sd.Get("Scale", ref mScale);
				ClearAndDeleteItems(mTheme);
				for (sd.Enumerate("Theme"))
				{
					var str = new String();
					sd.GetCurString(str);
					mTheme.Add(str);
				}
				sd.Get("InsertNewTabs", ref mInsertNewTabs);
				sd.Get("ShowStartupPanel", ref mShowStartupPanel);
			}
		}

		public class EditorSettings
		{
			public enum LockWhileDebuggingKind
			{
				Never,
				Always,
				WhenNotHotSwappable
			}

			public enum AutoCompleteShowKind
			{
				Popup,
				Panel,
				PanelIfVisible,
				None
			}

			public enum FileRecoveryKind
			{
				No,
				Yes,
				BackupOnly
			}

			public enum TabsOrSpaces
			{
				Tabs,
				Spaces
			}

			public enum CompilerKind
			{
				Resolve,
				Build
			}

			public List<String> mFonts = new .() ~ DeleteContainerAndItems!(_);
			public float mFontSize = 12;
			public AutoCompleteShowKind mAutoCompleteShowKind = .PanelIfVisible;
			public bool mAutoCompleteRequireControl = true;
			public bool mAutoCompleteRequireTab = false;
			public bool mAutoCompleteOnEnter = true;
			public bool mAutoCompleteShowDocumentation = true;
			public bool mFuzzyAutoComplete = false;
			public bool mShowLocatorAnim = true;
			public bool mHiliteCursorReferences = true;
			public bool mHiliteCurrentLine = false;
			public bool mLockEditing;
			public LockWhileDebuggingKind mLockEditingWhenDebugging = .WhenNotHotSwappable;// Only applicable for
			public CompilerKind mEmitCompiler;
			// non-Beef sources
			public bool mPerforceAutoCheckout = true;
			public bool mSpellCheckEnabled = true;
			public bool mShowLineNumbers = true;
			public bool mFreeCursorMovement;
			public FileRecoveryKind mEnableFileRecovery = .Yes;
			public bool mSyncWithWorkspacePanel = false;
			public int32 mWrapCommentsAt = 0;
			public bool mFormatOnSave = false;
			public bool mIndentCaseLabels = false;
			public bool mLeftAlignPreprocessor = true;
			public TabsOrSpaces mTabsOrSpaces = .Tabs;
			public int32 mTabSize = 4;

			public void Serialize(StructuredData sd)
			{
				using (sd.CreateArray("Fonts"))
				{
					for (let str in mFonts)
						sd.Add(str);
				}
				sd.Add("FontSize", mFontSize);
				sd.Add("AutoCompleteShowKind", mAutoCompleteShowKind);
				sd.Add("AutoCompleteRequireControl", mAutoCompleteRequireControl);
				sd.Add("AutoCompleteRequireTab", mAutoCompleteRequireTab);
				sd.Add("AutoCompleteOnEnter", mAutoCompleteOnEnter);
				sd.Add("AutoCompleteShowDocumentation", mAutoCompleteShowDocumentation);
				sd.Add("FuzzyAutoComplete", mFuzzyAutoComplete);
				sd.Add("ShowLocatorAnim", mShowLocatorAnim);
				sd.Add("HiliteCursorReferences", mHiliteCursorReferences);
				sd.Add("HiliteCurrentLine", mHiliteCurrentLine);
				sd.Add("LockEditing", mLockEditing);
				sd.Add("LockEditingWhenDebugging", mLockEditingWhenDebugging);
				sd.Add("EmitCompiler", mEmitCompiler);
				sd.Add("PerforceAutoCheckout", mPerforceAutoCheckout);
				sd.Add("SpellCheckEnabled", mSpellCheckEnabled);
				sd.Add("ShowLineNumbers", mShowLineNumbers);
				sd.Add("FreeCursorMovement", mFreeCursorMovement);
				sd.Add("EnableFileRecovery", mEnableFileRecovery);
				sd.Add("SyncWithWorkspacePanel", mSyncWithWorkspacePanel);
				sd.Add("WrapCommentsAt", mWrapCommentsAt);
				sd.Add("FormatOnSave", mFormatOnSave);
				sd.Add("TabsOrSpaces", mTabsOrSpaces);
				sd.Add("TabSize", mTabSize);
				sd.Add("IndentCaseLabels", mIndentCaseLabels);
				sd.Add("LeftAlignPreprocessor", mLeftAlignPreprocessor);
			}

			public void Deserialize(StructuredData sd)
			{
				ClearAndDeleteItems(mFonts);
				for (sd.Enumerate("Fonts"))
				{
					var str = new String();
					sd.GetCurString(str);
					mFonts.Add(str);
				}
				sd.Get("UIScale", ref gApp.mSettings.mUISettings.mScale); // Legacy
				sd.Get("FontSize", ref mFontSize);
				sd.Get("AutoCompleteShowKind", ref mAutoCompleteShowKind);
				sd.Get("AutoCompleteRequireControl", ref mAutoCompleteRequireControl);
				sd.Get("AutoCompleteRequireTab", ref mAutoCompleteRequireTab);
				sd.Get("AutoCompleteOnEnter", ref mAutoCompleteOnEnter);
				sd.Get("AutoCompleteShowDocumentation", ref mAutoCompleteShowDocumentation);
				sd.Get("FuzzyAutoComplete", ref mFuzzyAutoComplete);
				sd.Get("ShowLocatorAnim", ref mShowLocatorAnim);
				sd.Get("HiliteCursorReferences", ref mHiliteCursorReferences);
				sd.Get("HiliteCurrentLine", ref mHiliteCurrentLine);
				sd.Get("LockEditing", ref mLockEditing);
				sd.Get("LockEditingWhenDebugging", ref mLockEditingWhenDebugging);
				sd.Get("EmitCompiler", ref mEmitCompiler);
				sd.Get("PerforceAutoCheckout", ref mPerforceAutoCheckout);
				sd.Get("SpellCheckEnabled", ref mSpellCheckEnabled);
				sd.Get("ShowLineNumbers", ref mShowLineNumbers);
				sd.Get("FreeCursorMovement", ref mFreeCursorMovement);
				sd.GetEnum<FileRecoveryKind>("EnableFileRecovery", ref mEnableFileRecovery);
				sd.Get("SyncWithWorkspacePanel", ref mSyncWithWorkspacePanel);
				sd.Get("WrapCommentsAt", ref mWrapCommentsAt);
				sd.Get("FormatOnSave", ref mFormatOnSave);
				sd.Get("TabsOrSpaces", ref mTabsOrSpaces);
				sd.Get("TabSize", ref mTabSize);
				sd.Get("IndentCaseLabels", ref mIndentCaseLabels);
				sd.Get("LeftAlignPreprocessor", ref mLeftAlignPreprocessor);
			}

			public void SetDefaults()
			{
				mFonts.Add(new String("fonts/SourceCodePro-Regular.ttf"));
				mFonts.Add(new String("Segoe UI"));
				mFonts.Add(new String("?Segoe UI Symbol"));
				mFonts.Add(new String("?Segoe UI Historic"));
				mFonts.Add(new String("?Segoe UI Emoji"));
			}

			public void Apply()
			{
				mTabSize = Math.Clamp(mTabSize, 1, 16);

				if (mSpellCheckEnabled)
				{
					if (gApp.mSpellChecker == null)
						gApp.CreateSpellChecker();
				}
				else
				{
					if (gApp.mSpellChecker != null)
						DeleteAndNullify!(gApp.mSpellChecker);
				}

				
			}
		}

		public class KeySettings
		{
			public class Entry
			{
				public KeyState[] mKeys ~ DeleteContainerAndItems!(_);
				public String mCommand ~ delete _;
			}

			public List<Entry> mEntries = new .() ~ DeleteContainerAndItems!(_);

			public void Clear()
			{
				ClearAndDeleteItems(mEntries);
			}

			void Add(StringView cmd, StringView keys)
			{
				let entry = new Entry();
				entry.mCommand = new String(cmd);

				let keyList = scope List<KeyState>();
				KeyState.Parse(keys, keyList);

				let keyArr = new KeyState[keyList.Count];
				keyList.CopyTo(keyArr);
				entry.mKeys = keyArr;

				mEntries.Add(entry);
			}

			public void SetDefaults()
			{
				Add("Autocomplete", "Ctrl+Space");
				Add("Bookmark Next", "F2");
				Add("Bookmark Prev", "Shift+F2");
				Add("Bookmark Toggle", "Ctrl+F2");
				Add("Bookmark Clear", "Ctrl+K, Ctrl+L");
				Add("Break All", "Ctrl+Alt+Break");
				Add("Breakpoint Configure", "Alt+F9");
				Add("Breakpoint Disable", "Ctrl+F9");
				Add("Breakpoint Toggle", "F9");
				Add("Breakpoint Toggle Thread", "Shift+F9");
				Add("Build Workspace", "F7");
				Add("Cancel Build", "Ctrl+Break");
				Add("Close Document", "Ctrl+W");
				Add("Collapse All", "Ctrl+M, Ctrl+A");
				Add("Collapse To Definition", "Ctrl+M, Ctrl+O");
				Add("Collapse Redo", "Ctrl+M, Ctrl+Y");
				Add("Collapse Toggle", "Ctrl+M, Ctrl+M");
				Add("Collapse Toggle All", "Ctrl+M, Ctrl+L");
				Add("Collapse Undo", "Ctrl+M, Ctrl+Z");
				Add("Compile File", "Ctrl+F7");
				Add("Comment Block", "Ctrl+K, Ctrl+C");
				Add("Comment Lines", "Ctrl+K, Ctrl+/");
				Add("Comment Toggle", "Ctrl+K, Ctrl+T");
				Add("Debug Comptime", "Alt+F7");
				Add("Duplicate Line", "Ctrl+D");
				Add("Find Class", "Alt+Shift+L");
				Add("Find in Document", "Ctrl+F");
				Add("Find in Files", "Ctrl+Shift+F");
				Add("Find Next", "F3");
				Add("Find Prev", "Shift+F3");
				Add("Goto Definition", "F12");
				Add("Goto Line", "Ctrl+G");
				Add("Goto Method", "Alt+M");
				Add("Goto Next Item", "F4");
				Add("Make Lowercase", "Ctrl+U");
				Add("Make Uppercase", "Ctrl+Shift+U");
				Add("Match Brace Select", "Ctrl+Shift+RBracket");
				Add("Match Brace", "Ctrl+RBracket");
				Add("Move Line Down", "Alt+Shift+Down");
				Add("Move Line Up", "Alt+Shift+Up");
				Add("Move Statement Down", "Ctrl+Shift+Down");
				Add("Move Statement Up", "Ctrl+Shift+Up");
				Add("Navigate Backwards", "Alt+Left");
				Add("Navigate Forwards", "Alt+Right");
				Add("Next Document Panel", "Ctrl+Comma");
				Add("Open Corresponding", "Alt+O");
				Add("Open File in Workspace", "Shift+Alt+O");
				Add("Open File", "Ctrl+O");
				Add("Open Workspace", "Ctrl+Shift+O");
				Add("Quick Info", "Ctrl+K, Ctrl+I");
				Add("Recent File Next", "Ctrl+Tab");
				Add("Recent File Prev", "Shift+Ctrl+Tab");
				Add("Reformat Document", "Ctrl+K, Ctrl+D");
				Add("Remove All Breakpoints", "Ctrl+Shift+F9");
				Add("Rename Symbol", "Ctrl+R");
				Add("Rename Item", "F2");
				Add("Replace in Document", "Ctrl+H");
				Add("Replace in Files", "Ctrl+Shift+H");
				Add("Report Memory", "Ctrl+Alt+Shift+M");
				Add("Run To Cursor", "Ctrl+F10");
				Add("Run Without Compiling", "Ctrl+Shift+F5");
				Add("Save All", "Ctrl+Shift+S");
				Add("Save File", "Ctrl+S");
				Add("Scope Prev", "Alt+Up");
				Add("Scope Next", "Alt+Down");
				Add("Set Next Statement", "Ctrl+Shift+F10");
				Add("Show Auto Watches", "Ctrl+Alt+A");
				Add("Show Autocomplete Panel", "Ctrl+Alt+E");
				Add("Show Breakpoints", "Ctrl+Alt+B");
				Add("Show Call Stack", "Ctrl+Alt+C");
				Add("Show Class View", "Ctrl+Alt+L");
				Add("Show Current", "Alt+C");
				Add("Show Errors", "Ctrl+Alt+R");
				Add("Show Diagnostics", "Ctrl+Alt+D");
				Add("Show Error Next", "Ctrl+Shift+F12");
				Add("Show File Externally", "Ctrl+Tilde");
				Add("Show Find Results", "Ctrl+Alt+F");
				Add("Show Fixit", "Ctrl+Period");
				Add("Show Immediate", "Ctrl+Alt+I");
				Add("Show Memory", "Ctrl+Alt+M");
				Add("Show Modules", "Ctrl+Alt+U");
				Add("Show Output", "Ctrl+Alt+O");
				Add("Show Profiler", "Ctrl+Alt+P");
				Add("Show QuickWatch", "Shift+Alt+W");
				Add("Show Threads", "Ctrl+Alt+T");
				Add("Show Watches", "Ctrl+Alt+W");
				Add("Show Workspace Explorer", "Ctrl+Alt+S");
				Add("Start Debugging", "F5");
				Add("Start Without Debugging", "Ctrl+F5");
				Add("Start Without Compiling", "Alt+F5");
				Add("Step Into", "F11");
				Add("Step Out", "Shift+F11");
				Add("Step Over", "F10");
				Add("Stop Debugging", "Shift+F5");
				Add("Sync With Workspace Panel", "Ctrl+[, S");
				Add("Tab First", "Ctrl+Alt+Home");
				Add("Tab Last", "Ctrl+Alt+End");
				Add("Tab Next", "Ctrl+Alt+PageDown");
				Add("Tab Prev", "Ctrl+Alt+PageUp");
				Add("Uncomment Selection", "Ctrl+K, Ctrl+U");
				Add("Zoom In", "Ctrl+Equals");
				Add("Zoom Out", "Ctrl+Minus");
				Add("Zoom Reset", "Ctrl+0");
			}

			public void Apply()
			{
#if CLI
				return;
#endif

#unwarn
				for (var cmd in gApp.mCommands.mCommandMap.Values)
					cmd.mNext = null;
				gApp.mCommands.mKeyMap.Clear();

				for (let entry in mEntries)
				{
					var curCmdMap = gApp.mCommands.mKeyMap;

					for (let keyState in entry.mKeys)
					{
						bool isChordEntry = @keyState < entry.mKeys.Count - 1;
						IDECommand ideCommand = null;

						if (!isChordEntry)
						{
							if (!gApp.mCommands.mCommandMap.TryGetValue(entry.mCommand, out ideCommand))
							{
								gApp.OutputLineSmart("ERROR: Unable to locate IDE command {0}", entry.mCommand);
								break;// Boo
							}
							ideCommand.mParent = curCmdMap;
							ideCommand.mBoundKeyState = keyState;
						}

						KeyState* keyStatePtr;
						IDECommandBase* valuePtr;
						if (curCmdMap.mMap.TryAdd(keyState, out keyStatePtr, out valuePtr))
						{
							if (isChordEntry)
							{
								let newCmdMap = new CommandMap();
								newCmdMap.mParent = curCmdMap;
								newCmdMap.mBoundKeyState = keyState;
								curCmdMap = newCmdMap;
								*valuePtr = curCmdMap;
							}
							else
							{
								*valuePtr = ideCommand;
							}
						}
						else
						{
							if (isChordEntry)
							{
								curCmdMap = (*valuePtr) as CommandMap;
								if (curCmdMap == null)
								{
									gApp.OutputLineSmart("ERROR: The same key is bound for '{0}' and as part of a key chord", entry.mCommand);
									break;
								}
							}
							else
							{
								var prevIDECommand = (*valuePtr) as IDECommand;
								if (prevIDECommand != null)
								{
									var checkPrevCmd = prevIDECommand;
									while (true)
									{
										if (checkPrevCmd.mContextFlags == ideCommand.mContextFlags)
											gApp.OutputLineSmart("ERROR: The same key is bound for '{0}' and '{1}'", checkPrevCmd.mName, entry.mCommand);
										if (checkPrevCmd.mNext == null)
										{
											curCmdMap.FailValues.Add(ideCommand);
											break;
										}
										checkPrevCmd = checkPrevCmd.mNext;
									}
									checkPrevCmd.mNext = ideCommand;
								}
								else
									gApp.OutputLineSmart("ERROR: The same key is bound for '{0}' and as part of a key chord", entry.mCommand);
							}
						}
					}
				}

				String keyStr = scope String();
				for (var ideCommand in gApp.mCommands.mCommandMap.Values)
				{
					if (ideCommand.mMenuItem == null)
						continue;

					if (ideCommand.mBoundKeyState != null)
					{
						keyStr.Clear();
						keyStr.Append("#");
						ideCommand.mBoundKeyState.ToString(keyStr);
						ideCommand.mMenuItem.SetHotKey(keyStr);
					}
					else
						ideCommand.mMenuItem.SetHotKey(.());
				}
			}

			public void Serialize(StructuredData sd)
			{
				let mKeyEntries = scope List<IDECommand>();
				for (var kv in gApp.mCommands.mCommandMap)
				{
					mKeyEntries.Add(kv.value);
				}

				mKeyEntries.Sort(scope (lhs, rhs) => String.Compare(lhs.mName, rhs.mName, true));
				for (var ideCommand in mKeyEntries)
				{
					String keyStr = scope .();
					ideCommand.ToString(keyStr);
					sd.Add(ideCommand.mName, keyStr);
				}
			}

			public void Deserialize(StructuredData sd)
			{
				HashSet<String> usedCommands = scope .();
				List<String> allocatedStrs = scope .();
				defer { ClearAndDeleteItems(allocatedStrs); }

				List<Entry> newEntries = new .();
				for (let cmdStr in sd.Enumerate())
				{
					var keyStr = sd.GetCurrent() as String;
					if (keyStr == null)
						continue;
					if (keyStr.IsWhiteSpace)
					{
						var str = new String(cmdStr);
						usedCommands.Add(str);
						allocatedStrs.Add(str);
						continue;
					}

					let entry = new Entry();
					entry.mCommand = new String(cmdStr);

					// Fix for command rename
					if ((cmdStr == "Close Panel") && (!sd.Contains("Close Document")))
						entry.mCommand.Set("Close Document");

					let keyList = scope List<KeyState>();
					KeyState.Parse(keyStr, keyList);

					let keyArr = new KeyState[keyList.Count];
					keyList.CopyTo(keyArr);
					entry.mKeys = keyArr;

					newEntries.Add(entry);
					usedCommands.Add(entry.mCommand);
				}

				for (var entry in mEntries)
				{
					if (usedCommands.Contains(entry.mCommand))
						continue;
					newEntries.Add(entry);
					mEntries[@entry.Index] = null;
				}

				DeleteContainerAndItems!(mEntries);
				mEntries = newEntries;
			}
		}

		public struct TutorialsFinished
		{
			public bool mCtrlCursor;
			public bool mRanDebug;
			public bool mDependencies;
		}

		public bool mLoadedSettings;
		public String mSettingFileText ~ delete _;
		public DateTime mSettingFileDateTime;

		public UISettings mUISettings = new .() ~ delete _;
		public EditorSettings mEditorSettings = new .() ~ delete _;
		public CompilerSettings mCompilerSettings = new .() ~ delete _;
		public VSSettings mVSSettings = new .() ~ delete _;
		public DebuggerSettings mDebuggerSettings = new .() ~ delete _;
		public KeySettings mKeySettings = new .() ~ delete _;
		public RecentFiles mRecentFiles = new RecentFiles() ~ delete _;
		public String mWakaTimeKey = new .() ~ delete _;
		public String mEmscriptenPath = new .() ~ delete _;
		public bool mEnableDevMode;
		public TutorialsFinished mTutorialsFinished = .();

		public this()
		{
			SetDefaults();
		}

		public this(Settings prevSettings)
		{
			Swap!(mRecentFiles, prevSettings.mRecentFiles);

			for (var recent in mRecentFiles.mRecents)
			{
				recent.mList.ClearAndDeleteItems();
				for (var item in recent.mMenuItems)
					item.Dispose();
				recent.mMenuItems.ClearAndDeleteItems();
			}
		}

		public void SetDefaults()
		{
			mVSSettings.SetDefaults();
			mUISettings.SetDefaults();
			mEditorSettings.SetDefaults();
			mCompilerSettings.SetDefaults();
			mDebuggerSettings.SetDefaults();
			mKeySettings.SetDefaults();
		}

		void GetSettingsPath(String outPath)
		{
			outPath.Append(gApp.mInstallDir, "/BeefSettings.toml");
		}

		public void Save()
		{
			String path = scope .();
			GetSettingsPath(path);

			let sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("FileVersion", 1);
			using (sd.CreateObject("UI"))
				mUISettings.Serialize(sd);
			using (sd.CreateObject("Editor"))
				mEditorSettings.Serialize(sd);
			using (sd.CreateObject("Keys"))
				mKeySettings.Serialize(sd);
			using (sd.CreateObject("Compiler"))
				mCompilerSettings.Serialize(sd);
			using (sd.CreateObject("Debugger"))
				mDebuggerSettings.Serialize(sd);
			using (sd.CreateObject("VisualStudio"))
				mVSSettings.Serialize(sd);
			using (sd.CreateObject("Wasm"))
				sd.Add("EmscriptenPath", mEmscriptenPath);

			using (sd.CreateObject("RecentFiles"))
			{
				for (RecentFiles.RecentKind recentKind = default; recentKind < RecentFiles.RecentKind.COUNT; recentKind++)
				{
					String name = scope .();
					recentKind.ToString(name);
					using (sd.CreateArray(name))
					{
						for (var recentFile in mRecentFiles.GetRecentList(recentKind))
							sd.Add(recentFile);
					}
				}
			}

			using (sd.CreateObject("Options"))
			{
				sd.Add("WakaTimeKey", mWakaTimeKey);
				sd.Add("EnableDevMode", mEnableDevMode);
			}

			using (sd.CreateObject("TutorialsFinished"))
			{
				sd.Add("CtrlCursor", mTutorialsFinished.mCtrlCursor);
				sd.Add("RanDebug", mTutorialsFinished.mRanDebug);
				sd.Add("Dependencies", mTutorialsFinished.mDependencies);
			}

			String dataStr = scope String();
			sd.ToTOML(dataStr);

			if ((mSettingFileText == null) || (mSettingFileText != dataStr))
			{
				String.NewOrSet!(mSettingFileText, dataStr);
				gApp.SafeWriteTextFile(path, dataStr);

				if (File.GetLastWriteTime(path) case .Ok(let dt))
					mSettingFileDateTime = dt;
			}
		}

		public bool WantsReload()
		{
			if (mSettingFileDateTime == default)
				return false;

			String path = scope .();
			GetSettingsPath(path);

			if (File.GetLastWriteTime(path) case .Ok(let dt))
			{
				if (dt != mSettingFileDateTime)
					return true;
			}

			return false;
		}

		public void Load()
		{
			String path = scope .();
			GetSettingsPath(path);

			let sd = scope StructuredData();
			if (sd.Load(path) case .Err)
				return;

			if (File.GetLastWriteTime(path) case .Ok(let dt))
				mSettingFileDateTime = dt;

			String.NewOrSet!(mSettingFileText, sd.[Friend]mSource);
			mSettingFileText.Replace("\r\n", "\n");
			mSettingFileText.Replace('\r', '\n');

			mLoadedSettings = true;
			using (sd.Open("UI"))
				mUISettings.Deserialize(sd);
			using (sd.Open("Editor"))
				mEditorSettings.Deserialize(sd);
			using (sd.Open("Keys"))
				mKeySettings.Deserialize(sd);
			using (sd.Open("Compiler"))
				mCompilerSettings.Deserialize(sd);
			using (sd.Open("Debugger"))
				mDebuggerSettings.Deserialize(sd);
			using (sd.Open("VisualStudio"))
				mVSSettings.Deserialize(sd);
			using (sd.Open("Wasm"))
				sd.Get("EmscriptenPath", mEmscriptenPath);

			using (sd.Open("RecentFiles"))
			{
				for (RecentFiles.RecentKind recentKind = default; recentKind < RecentFiles.RecentKind.COUNT; recentKind++)
				{
					let recentList = mRecentFiles.GetRecentList(recentKind);

					String name = scope .();
					recentKind.ToString(name);
					for ( sd.Enumerate(name))
					{
						String fileStr = new String();
						sd.GetCurString(fileStr);
						IDEUtils.FixFilePath(fileStr);
						recentList.Add(fileStr);
					}
				}
			}

			using (sd.Open("Options"))
			{
				sd.Get("WakaTimeKey", mWakaTimeKey);
				sd.Get("EnableDevMode", ref mEnableDevMode);
			}

			using (sd.Open("TutorialsFinished"))
			{
				sd.Get("CtrlCursor", ref mTutorialsFinished.mCtrlCursor);
				sd.Get("RanDebug", ref mTutorialsFinished.mRanDebug);
				if (!sd.Get("Dependencies", ref mTutorialsFinished.mDependencies))
					mTutorialsFinished.mDependencies = true;
			}
		}

		public void Apply()
		{
			gApp.mSettings.mUISettings.mScale = Math.Clamp(gApp.mSettings.mUISettings.mScale, 50, 400);
			gApp.mSettings.mEditorSettings.mFontSize = Math.Clamp(gApp.mSettings.mEditorSettings.mFontSize, 6.0f, 72.0f);

			mUISettings.Apply();
			mEditorSettings.Apply();

			Font.ClearFontNameCache();
			gApp.PhysSetScale(gApp.mSettings.mUISettings.mScale / 100.0f, true);

			DeleteAndNullify!(gApp.mKeyChordState);

			mKeySettings.Apply();
			mDebuggerSettings.Apply();
			

			for (var window in gApp.mWindows)
			{
				if (var widgetWindow = window as WidgetWindow)
				{
					widgetWindow.mRootWidget.RehupSize();
				}
			}

			for (let value in gApp.mFileEditData.Values)
				if (value.mEditWidget != null)
					((SourceEditWidgetContent)value.mEditWidget.Content).mHiliteCurrentLine = gApp.mSettings.mEditorSettings.mHiliteCurrentLine;

			if (!mWakaTimeKey.IsEmpty)
			{
				if (gApp.mWakaTime == null)
					gApp.mWakaTime = new WakaTime(mWakaTimeKey);
			}
			else
			{
				DeleteAndNullify!(gApp.mWakaTime);
			}
		}
	}
}
							