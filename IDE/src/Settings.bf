using System.Collections.Generic;
using System;
using Beefy.gfx;
using Beefy.geom;
using Beefy.widgets;
using System.Threading;
using Beefy.utils;
using IDE.util;

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
				ClearAndDeleteItems(mLib32Paths);
				for (sd.Enumerate("Lib32Paths"))
				{
					var str = new String();
					sd.GetCurString(str);
					mLib32Paths.Add(str);
				}
				ClearAndDeleteItems(mLib64Paths);
				for (sd.Enumerate("Lib64Paths"))
				{
					var str = new String();
					sd.GetCurString(str);
					mLib64Paths.Add(str);
				}
			}

			[CLink, StdCall]
			static extern char8* VSSupport_Find();

			public bool IsConfigured()
			{
				return !mLib32Paths.IsEmpty || !mLib64Paths.IsEmpty || !mBin32Path.IsEmpty || !mBin64Path.IsEmpty;
			}

			public void SetDefaults()
			{
#if BF_PLATFORM_WINDOWS
				StringView vsInfo = .(VSSupport_Find());

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
			}

			public void Deserialize(StructuredData sd)
			{
				sd.Get("UseSymbolServers", ref mUseSymbolServers);
				sd.Get("SymCachePath", mSymCachePath);
				ClearAndDeleteItems(mSymbolSearchPath);
				for (sd.Enumerate("SymbolSearchPath"))
				{
					var str = new String();
					sd.GetCurString(str);
					mSymbolSearchPath.Add(str);
				}
				ClearAndDeleteItems(mAutoFindPaths);
				for (sd.Enumerate("AutoFindPaths"))
				{
					var str = new String();
					sd.GetCurString(str);
					mAutoFindPaths.Add(str);
				}

				if (gApp.mDebugger != null)
				{
					for (sd.Enumerate("StepFilters"))
					{
						String filter = scope String();
						sd.GetCurString(filter);
						gApp.mDebugger.CreateStepFilter(filter, true, .Filtered);
					}

					for (sd.Enumerate("StepNotFilters"))
					{
						String filter = scope String();
						sd.GetCurString(filter);
						gApp.mDebugger.CreateStepFilter(filter, true, .NotFiltered);
					}
				}
				sd.Get("ProfileSampleRate", ref mProfileSampleRate);
			}

			public void Apply()
			{
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
						Platform.BfpDirectory_GetSysDirectory(.AppData_Local, outPtr, outSize, (Platform.BfpFileResult*)outResult);
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

			public class Colors
			{
				public Color mUIColorR = Color.Get(255, 0, 0);
				public Color mUIColorG = Color.Get(0, 255, 0);
				public Color mUIColorB = Color.Get(0, 0, 255);
				public Color mText = 0xFFFFFFFF;
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

				public void Serialize(StructuredData sd)
				{
					
				}

				public void Deserialize(StructuredData sd)
				{
					
				}
			}

			public List<String> mFonts = new .() ~ DeleteContainerAndItems!(_);
			public float mFontSize = 12;
			public float mUIScale = 100;
			public Colors mColors = new .() ~ delete _;
			public AutoCompleteShowKind mAutoCompleteShowKind = .PanelIfVisible;
			public bool mAutoCompleteRequireTab = false;
			public bool mAutoCompleteShowDocumentation = true;
			public bool mShowLocatorAnim = true;
			public bool mHiliteCursorReferences = true;
			public bool mLockEditing;
			public LockWhileDebuggingKind mLockEditingWhenDebugging = .WhenNotHotSwappable; // Only applicable for non-Beef sources
			public bool mPerforceAutoCheckout = true;
			public bool mSpellCheckEnabled = true;
			public bool mShowLineNumbers = true;

			public void Serialize(StructuredData sd)
			{
				using (sd.CreateArray("Fonts"))
				{
					for (let str in mFonts)
						sd.Add(str);
				}
				sd.Add("FontSize", mFontSize);
				sd.Add("UIScale", mUIScale);
				sd.Add("AutoCompleteShowKind", mAutoCompleteShowKind);
				sd.Add("AutoCompleteRequireTab", mAutoCompleteRequireTab);
				sd.Add("AutoCompleteShowDocumentation", mAutoCompleteShowDocumentation);
				sd.Add("ShowLocatorAnim", mShowLocatorAnim);
				sd.Add("HiliteCursorReferences", mHiliteCursorReferences);
				sd.Add("LockEditing", mLockEditing);
				sd.Add("LockEditingWhenDebugging", mLockEditingWhenDebugging);
				sd.Add("PerforceAutoCheckout", mPerforceAutoCheckout);
				sd.Add("SpellCheckEnabled", mSpellCheckEnabled);
				sd.Add("ShowLineNumbers", mShowLineNumbers);

				using (sd.CreateObject("Colors"))
					mColors.Serialize(sd);
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
				sd.Get("FontSize", ref mFontSize);
				sd.Get("UIScale", ref mUIScale);
				sd.Get("AutoCompleteShowKind", ref mAutoCompleteShowKind);
				sd.Get("AutoCompleteRequireTab", ref mAutoCompleteRequireTab);
				sd.Get("AutoCompleteShowDocumentation", ref mAutoCompleteShowDocumentation);
				sd.Get("ShowLocatorAnim", ref mShowLocatorAnim);
				sd.Get("HiliteCursorReferences", ref mHiliteCursorReferences);
				sd.Get("LockEditing", ref mLockEditing);
				sd.Get("LockEditingWhenDebugging", ref mLockEditingWhenDebugging);
				sd.Get("PerforceAutoCheckout", ref mPerforceAutoCheckout);
				sd.Get("SpellCheckEnabled", ref mSpellCheckEnabled);
				sd.Get("ShowLineNumbers", ref mShowLineNumbers);

				using (sd.Open("Colors"))
					mColors.Deserialize(sd);
			}

			public void SetDefaults()
			{
				mFonts.Add(new String("fonts/SourceCodePro-Regular.ttf"));
				mFonts.Add(new String("Segoe UI"));
				mFonts.Add(new String("Segoe UI Symbol"));
				mFonts.Add(new String("Segoe UI Historic"));
				mFonts.Add(new String("Segoe UI Emoji"));
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
				Add("Breakpoint Toggle Thread", "Ctrl+F9");
				Add("Breakpoint Toggle", "F9");
				Add("Build Solution", "F7");
				Add("Cancel Build", "Ctrl+Break");
				Add("Close Window", "Ctrl+W");
				Add("Compile File", "Ctrl+F7");
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
				Add("Set Next Statement", "Ctrl+Shift+F10");
				Add("Show Current", "Alt+C");
				Add("Show Auto Watches", "Ctrl+Alt+A");
				Add("Show Autocomplete Panel", "Ctrl+Alt+U");
				Add("Show Breakpoints", "Ctrl+Alt+B");
				Add("Show Call Stack", "Ctrl+Alt+C");
				Add("Show Class View", "Ctrl+Alt+L");
				Add("Show File Externally", "Ctrl+Tilde");
				Add("Show Find Results", "Ctrl+Alt+F");
				Add("Show Fixit", "Ctrl+Period");
				Add("Show Immediate", "Ctrl+Alt+I");
				Add("Show Memory", "Ctrl+Alt+M");
				Add("Show Modules", "Ctrl+Alt+D");
				Add("Show Output", "Ctrl+Alt+O");
				Add("Show Profiler", "Ctrl+Alt+P");
				Add("Show QuickWatch", "Shift+Alt+W");
				Add("Show Threads", "Ctrl+Alt+T");
				Add("Show Watches", "Ctrl+Alt+W");
				Add("Show Workspace Explorer", "Ctrl+Alt+S");
				Add("Start Debugging", "F5");
				Add("Start Without Debugging", "Ctrl+F5");
				Add("Step Into", "F11");
				Add("Step Out", "Shift+F11");
				Add("Step Over", "F10");
				Add("Stop Debugging", "Shift+F5");
				Add("Tab First", "Ctrl+Alt+Home");
				Add("Tab Last", "Ctrl+Alt+End");
				Add("Tab Next", "Ctrl+Alt+PageDown");
				Add("Tab Prev", "Ctrl+Alt+PageUp");
				Add("Zoom In", "Ctrl+Equals");
				Add("Zoom Out", "Ctrl+Minus");
				Add("Zoom Reset", "Ctrl+0");
			}

			public void Apply()
			{
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
								break; // Boo
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

		public CompilerSettings mCompilerSettings = new .() ~ delete _;
		public EditorSettings mEditorSettings = new .() ~ delete _;
		public VSSettings mVSSettings = new .() ~ delete _;
		public DebuggerSettings mDebuggerSettings = new .() ~ delete _;
		public KeySettings mKeySettings = new .() ~ delete _;
		public RecentFiles mRecentFiles = new RecentFiles() ~ delete _;
		public String mWakaTimeKey = new .() ~ delete _;

		public this()
		{
			SetDefaults();
		}

		public void SetDefaults()
		{
			mVSSettings.SetDefaults();
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

			sd.Add("WakaTimeKey", mWakaTimeKey);

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

			using (sd.Open("RecentFiles"))
			{
				for (RecentFiles.RecentKind recentKind = default; recentKind < RecentFiles.RecentKind.COUNT; recentKind++)
				{
					let recentList = mRecentFiles.GetRecentList(recentKind);

					String name = scope .();
					recentKind.ToString(name);
					for (sd.Enumerate(name))
					{
						String fileStr = new String();
						sd.GetCurString(fileStr);
						IDEUtils.FixFilePath(fileStr);
					    recentList.Add(fileStr);
					}
				}
			}

			sd.Get("WakaTimeKey", mWakaTimeKey);
		}

		public void Apply()
		{
			gApp.mSettings.mEditorSettings.mUIScale = Math.Clamp(gApp.mSettings.mEditorSettings.mUIScale, 25, 400);
			gApp.mSettings.mEditorSettings.mFontSize = Math.Clamp(gApp.mSettings.mEditorSettings.mFontSize, 6.0f, 72.0f);

			Font.ClearFontNameCache();
			gApp.SetScale(gApp.mSettings.mEditorSettings.mUIScale / 100.0f, true);

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
							