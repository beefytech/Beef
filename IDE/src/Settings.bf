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

			public void Serialize(StructuredData sd)
			{
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
			public Color mWindow = 0xFF595959;
			public Color mBackground = 0xFF262626;
			public Color mSelectedOutline = 0xFFE6A800;
			public Color mMenuFocused = 0xFFFFA000;
			public Color mMenuSelected = 0xFFD0A070;

			public Color mCode = 0xFFFFFFFF;
			public Color mKeyword = 0xFFE1AE9A;
			public Color mLiteral = 0XFFC8A0FF;
			public Color mIdentifier = 0xFFFFFFFF;
			public Color mType = 0XFF66D9EF;
			public Color mComment = 0XFF75715E;
			public Color mMethod = 0XFFA6E22A;
			public Color mTypeRef = 0XFF66D9EF;
			public Color mNamespace = 0xFF7BEEB7;
			public Color mDisassemblyText = 0xFFB0B0B0;
			public Color mDisassemblyFileName = 0XFFFF0000;
			public Color mError = 0xFFFF0000;
			public Color mBuildError = 0xFFFF8080;
			public Color mBuildWarning = 0xFFFFFF80;
			public Color mVisibleWhiteSpace = 0xFF9090C0;

			public void Deserialize(StructuredData sd)
			{
				void GetColor(String name, ref Color color)
				{
					sd.Get(name, ref *(int32*)&color);
				}

				GetColor("Text", ref mText);
				GetColor("Window", ref mWindow);
				GetColor("Background", ref mBackground);
				GetColor("SelectedOutline", ref mSelectedOutline);
				GetColor("MenuFocused", ref mMenuFocused);
				GetColor("MenuSelected", ref mMenuSelected);

				GetColor("Code", ref mCode);
				GetColor("Keyword", ref mKeyword);
				GetColor("Literal", ref mLiteral);
				GetColor("Identifier", ref mIdentifier);
				GetColor("Type", ref mType);
				GetColor("Comment", ref mComment);
				GetColor("Method", ref mMethod);
				GetColor("TypeRef", ref mTypeRef);
				GetColor("Namespace", ref mNamespace);
				GetColor("DisassemblyText", ref mDisassemblyText);
				GetColor("DisassemblyFileName", ref mDisassemblyFileName);
				GetColor("Error", ref mError);
				GetColor("BuildError", ref mBuildError);
				GetColor("BuildWarning", ref mBuildWarning);
				GetColor("VisibleWhiteSpace", ref mVisibleWhiteSpace);
			}

			public void Apply()
			{
				SourceEditWidgetContent.sTextColors[0] = mCode;
				SourceEditWidgetContent.sTextColors[1] = mKeyword;
				SourceEditWidgetContent.sTextColors[2] = mLiteral;
				SourceEditWidgetContent.sTextColors[3] = mIdentifier;
				SourceEditWidgetContent.sTextColors[4] = mType;
				SourceEditWidgetContent.sTextColors[5] = mComment;
				SourceEditWidgetContent.sTextColors[6] = mMethod;
				SourceEditWidgetContent.sTextColors[7] = mTypeRef;
				SourceEditWidgetContent.sTextColors[8] = mNamespace;
				SourceEditWidgetContent.sTextColors[9] = mDisassemblyText;
				SourceEditWidgetContent.sTextColors[10] = mDisassemblyFileName;
				SourceEditWidgetContent.sTextColors[11] = mError;
				SourceEditWidgetContent.sTextColors[12] = mBuildError;
				SourceEditWidgetContent.sTextColors[13] = mBuildWarning;
				SourceEditWidgetContent.sTextColors[14] = mVisibleWhiteSpace;

				DarkTheme.COLOR_TEXT = mText;
				DarkTheme.COLOR_WINDOW = mWindow;
				DarkTheme.COLOR_BKG = mBackground;
				DarkTheme.COLOR_SELECTED_OUTLINE = mSelectedOutline;
				DarkTheme.COLOR_MENU_FOCUSED = mMenuFocused;
				DarkTheme.COLOR_MENU_SELECTED = mMenuSelected;
			}
		}

		public class UISettings
		{
			public Colors mColors = new .() ~ delete _;
			public float mScale = 100;
			public List<String> mTheme = new .() ~ DeleteContainerAndItems!(_);

			public void SetDefaults()
			{
				DeleteAndNullify!(mColors);
				mColors = new .();
				mScale = 100;
				ClearAndDeleteItems(mTheme);
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
					if (sd.Load(themeFilePath) case .Err)
						return;

					using (sd.Open("Colors"))
						mColors.Deserialize(sd);
				}

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
							String destImgPath = scope .(absPath);
							switch (scale)
							{
							case 0:
								srcImgPath.Append("UI.psd");
								destImgPath.Append("UI.png");
							case 1:
								srcImgPath.Append("UI_2.psd");
								destImgPath.Append("UI_2.png");
							case 2:
								srcImgPath.Append("UI_4.psd");
								destImgPath.Append("UI_2.png");
							}
							maxSrcImgTime = Math.Max(maxSrcImgTime, File.GetLastWriteTime(srcImgPath).GetValueOrDefault());
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
					}

					if (needsRebuild)
					{
						String imgCreatePath = scope String(absPath, "../../images/ImgCreate.exe");

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
						case 0: imgPath.Append("UI.png");
						case 1: imgPath.Append("UI_2.png");
						case 2: imgPath.Append("UI_4.png");
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

			public List<String> mFonts = new .() ~ DeleteContainerAndItems!(_);
			public float mFontSize = 12;
			public AutoCompleteShowKind mAutoCompleteShowKind = .PanelIfVisible;
			public bool mAutoCompleteRequireControl = true;
			public bool mAutoCompleteRequireTab = false;
			public bool mAutoCompleteOnEnter = true;
			public bool mAutoCompleteShowDocumentation = true;
			public bool mShowLocatorAnim = true;
			public bool mHiliteCursorReferences = true;
			public bool mLockEditing;
			public LockWhileDebuggingKind mLockEditingWhenDebugging = .WhenNotHotSwappable;// Only applicable for
			// non-Beef sources
			public bool mPerforceAutoCheckout = true;
			public bool mSpellCheckEnabled = true;
			public bool mShowLineNumbers = true;
			public bool mFreeCursorMovement;

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
				sd.Add("ShowLocatorAnim", mShowLocatorAnim);
				sd.Add("HiliteCursorReferences", mHiliteCursorReferences);
				sd.Add("LockEditing", mLockEditing);
				sd.Add("LockEditingWhenDebugging", mLockEditingWhenDebugging);
				sd.Add("PerforceAutoCheckout", mPerforceAutoCheckout);
				sd.Add("SpellCheckEnabled", mSpellCheckEnabled);
				sd.Add("ShowLineNumbers", mShowLineNumbers);
				sd.Add("FreeCursorMovement", mFreeCursorMovement);
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
				sd.Get("ShowLocatorAnim", ref mShowLocatorAnim);
				sd.Get("HiliteCursorReferences", ref mHiliteCursorReferences);
				sd.Get("LockEditing", ref mLockEditing);
				sd.Get("LockEditingWhenDebugging", ref mLockEditingWhenDebugging);
				sd.Get("PerforceAutoCheckout", ref mPerforceAutoCheckout);
				sd.Get("SpellCheckEnabled", ref mSpellCheckEnabled);
				sd.Get("ShowLineNumbers", ref mShowLineNumbers);
				sd.Get("FreeCursorMovement", ref mFreeCursorMovement);
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
				Add("Build Solution", "F7");
				Add("Cancel Build", "Ctrl+Break");
				Add("Close Window", "Ctrl+W");
				Add("Compile File", "Ctrl+F7");
				Add("Comment Selection", "Ctrl+K, Ctrl+C");
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
				Add("Navigate Backwards", "Alt+Left");
				Add("Navigate Forwards", "Alt+Right");
				Add("Next Document Panel", "Ctrl+Comma");
				Add("Open Corresponding", "Alt+O");
				Add("Open File in Workspace", "Shift+Alt+O");
				Add("Open File", "Ctrl+O");
				Add("Open Workspace", "Ctrl+Shift+O");
				Add("Quick Info", "Ctrl+K, Ctrl+I");
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
									break;
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
											break;
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
				defer { DeleteAndClearItems!(allocatedStrs); }

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
		}

		public bool mLoadedSettings;

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
			}

			String dataStr = scope String();
			sd.ToTOML(dataStr);
			gApp.SafeWriteTextFile(path, dataStr);
		}

		public void Load()
		{
			String path = scope .();
			GetSettingsPath(path);

			let sd = scope StructuredData();
			if (sd.Load(path) case .Err)
				return;

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
			}
		}

		public void Apply()
		{
			gApp.mSettings.mUISettings.mScale = Math.Clamp(gApp.mSettings.mUISettings.mScale, 50, 400);
			gApp.mSettings.mEditorSettings.mFontSize = Math.Clamp(gApp.mSettings.mEditorSettings.mFontSize, 6.0f, 72.0f);

			mUISettings.Apply();

			Font.ClearFontNameCache();
			gApp.PhysSetScale(gApp.mSettings.mUISettings.mScale / 100.0f, true);

			DeleteAndNullify!(gApp.mKeyChordState);

			mKeySettings.Apply();
			mDebuggerSettings.Apply();
			mEditorSettings.Apply();

			for (var window in gApp.mWindows)
			{
				if (var widgetWindow = window as WidgetWindow)
				{
					widgetWindow.mRootWidget.RehupSize();
				}
			}

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
							