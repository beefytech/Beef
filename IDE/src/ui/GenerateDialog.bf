using Beefy.theme.dark;
using Beefy.widgets;
using System;
using System.Collections;
using Beefy.gfx;
using Beefy.events;
using IDE.Compiler;
using System.Threading;
using System.Security.Cryptography;
using Beefy.theme;

namespace IDE.ui
{
	class GenerateListView : DarkListView
	{
		public GenerateDialog mNewClassDialog;
	}

	class GenerateKindBar : DarkComboBox
	{
	    public class Entry
		{
			public String mTypeName ~ delete _;
			public String mName ~ delete _;
		}

	    public static Dictionary<String, int32> sMRU = new Dictionary<String, int32>() ~ delete _;
	    public static int32 sCurrentMRUIndex = 1;

		public GenerateDialog mNewClassDialog;
	    public List<Entry> mEntries = new List<Entry>() ~ DeleteContainerAndItems!(_);
	    public List<Entry> mShownEntries = new List<Entry>() ~ delete _;
	    public String mFilterString ~ delete _;
		public String mCurLocation = new String() ~ delete _;
		public bool mIgnoreChange = false;

	    public this(GenerateDialog dialog)
	    {
			mNewClassDialog = dialog;
	        mLabelAlign = FontAlign.Left;
	        Label = "";
	        mLabelX = GS!(16);
	        mPopulateMenuAction.Add(new => PopulateNavigationBar);
	        MakeEditable();
	        mEditWidget.mOnContentChanged.Add(new => NavigationBarChanged);
			mEditWidget.mOnGotFocus.Add(new (widget) => mEditWidget.mEditWidgetContent.SelectAll());
			mEditWidget.mEditWidgetContent.mWantsUndo = false;
			mEditWidget.mOnSubmit.Add(new => mNewClassDialog.[Friend]EditSubmitHandler);
			mFocusDropdown = false;
	    }

		public ~this()
		{
		}

		static ~this()
		{
			for (var key in sMRU.Keys)
				delete key;
		}

		public void SetLocation(String location)
		{
			if (mCurMenuWidget == null)
			{
				mIgnoreChange = true;
				mEditWidget.SetText(location);
				mEditWidget.mEditWidgetContent.SelectAll();
				// SetText can attempt to scroll to the right to make the cursor position visible.  Just scroll back to the start.
				mEditWidget.HorzScrollTo(0);
				//mNewClassDialog.SelectKind();
				mIgnoreChange = false;
			}
		}
	    private void PopulateNavigationBar(Menu menu)
	    {
			List<StringView> findStrs = null;
			if (mFilterString != null)
			 	findStrs = scope:: List<StringView>(mFilterString.Split(' '));

	        EntryLoop: for (int32 entryIdx = 0; entryIdx < mEntries.Count; entryIdx++)            
	        {
	            var entry = mEntries[entryIdx];
				if (findStrs != null)
				{
					for (let findStr in findStrs)
					{
					    if (entry.mName.IndexOf(findStr, true) == -1)
					        continue EntryLoop;
					}
				}

	            mShownEntries.Add(entry);
	            var menuItem = menu.AddItem(entry.mName);
	            menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mNewClassDialog.mPendingUIFocus = true;
						ShowEntry(entryIdx, entry);
					});
	        }
	    }		

	    void ShowEntry(int32 entryIdx, Entry entry)
	    {
	        mEditWidget.SetText(entry.mName);
			mEditWidget.mEditWidgetContent.SelectAll();
			mCurMenuWidget?.Close();
	    }

	    private void NavigationBarChanged(EditEvent theEvent)
	    {
			if (mIgnoreChange)
				return;

	        var editWidget = (EditWidget)theEvent.mSender;
	        var searchText = scope String();
	        editWidget.GetText(searchText);
			searchText.Trim();
	        mFilterString = searchText;
	        ShowDropdown();
	        mFilterString = null;
	    }

		bool mIgnoreShowDropdown;

	    public override MenuWidget ShowDropdown()
	    {
			if (mIgnoreShowDropdown)
				return null;
			mIgnoreShowDropdown = true;
			defer { mIgnoreShowDropdown = false; }

	        if (!mEditWidget.mHasFocus)
	            SetFocus();

	        if (mFilterString == null)
	            mEditWidget.Content.SelectAll();

	        mShownEntries.Clear();
	        base.ShowDropdown();

	        int32 bestItem = -1;
	        int32 bestPri = -1;
	        var menuWidget = (DarkMenuWidget)mCurMenuWidget;

	        for (int32 itemIdx = 0; itemIdx < menuWidget.mItemWidgets.Count; itemIdx++)
	        {
	            var menuItemWidget = (DarkMenuItem)menuWidget.mItemWidgets[itemIdx];

	            int32 pri;
	            sMRU.TryGetValue(menuItemWidget.mMenuItem.mLabel, out pri);
	            if (pri > bestPri)
	            {
	                bestItem = itemIdx;
	                bestPri = pri;
	            }
	        }

	        if (bestItem != -1)
	        {
				mCurMenuWidget.mOnSelectionChanged.Add(new => SelectionChanged);
	            mCurMenuWidget.SetSelection(bestItem);
	        }

			return menuWidget;
	    }

		void SelectionChanged(int selIdx)
		{
			if (mEditWidget.mEditWidgetContent.HasSelection())
			{
				bool prevIgnoreShowDropdown = mIgnoreShowDropdown;
				mIgnoreShowDropdown = true;
				mEditWidget.SetText("");
				mIgnoreShowDropdown = prevIgnoreShowDropdown;
			}
		}

	    public override void MenuClosed()
	    {
	    }
	}

	class GenerateDialog : IDEDialog
	{
		public class UIEntry
		{
			public String mName ~ delete _;
			public String mData ~ delete _;
			public String mLabel ~ delete _;
			public Widget mWidget;
		}

		public enum ThreadState
		{
			None,
			Executing,
			Done
		}

		public bool mPendingGenList;
		public GenerateKindBar mKindBar;
		public ThreadState mThreadState;
		public int mThreadWaitCount;
		public String mNamespace ~ delete _;
		public String mProjectName ~ delete _;
		public ProjectItem mProjectItem ~ _.ReleaseRef();
		public String mFolderPath ~ delete _;
		public List<UIEntry> mUIEntries = new .() ~ DeleteContainerAndItems!(_);
		public GenerateKindBar.Entry mSelectedEntry;
		public GenerateKindBar.Entry mPendingSelectedEntry;
		public String mUIData ~ delete _;
		public float mUIHeight = 0;
		public OutputPanel mOutputPanel;
		public bool mPendingUIFocus;
		public bool mSubmitting;
		public bool mSubmitQueued;
		public bool mRegenerating;

		public this(ProjectItem projectItem, bool allowHashMismatch = false)
		{
			var project = projectItem.mProject;
			mProjectItem = projectItem;
			mProjectItem.AddRef();

			mNamespace = new .();

			var projectFolder = projectItem as ProjectFolder;
			var projectSource = projectItem as ProjectSource;
			if (projectSource != null)
			{
				projectFolder = projectSource.mParentFolder;
				mRegenerating = true;
			}

			projectFolder.GetRelDir(mNamespace); mNamespace.Replace('/', '.'); mNamespace.Replace('\\', '.'); mNamespace.Replace(" ", "");
			if (mNamespace.StartsWith("src."))
			{
			    mNamespace.Remove(0, 4);
				if (!project.mBeefGlobalOptions.mDefaultNamespace.IsWhiteSpace)
				{
					mNamespace.Insert(0, ".");
					mNamespace.Insert(0, project.mBeefGlobalOptions.mDefaultNamespace);
				}
			}
			else if (projectItem.mParentFolder == null)
			{
				mNamespace.Clear();
				mNamespace.Append(project.mBeefGlobalOptions.mDefaultNamespace);
			}
			else
				mNamespace.Clear();

			mFolderPath = projectFolder.GetFullImportPath(.. new .());
			mProjectName = new String(projectItem.mProject.mProjectName);

			mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable | .PopupPosition;

			AddOkCancelButtons(new (evt) =>
				{
					Submit();
					evt.mCloseDialog = false;
				}, null, 0, 1);

			Title = "Generate File";

			mKindBar = new GenerateKindBar(this);
			AddWidget(mKindBar);
			mKindBar.mEditWidget.mOnContentChanged.Add(new (theEvent) => { SelectKind(); });

			if (mRegenerating)
			{
				mSubmitQueued = true;
				mKindBar.SetVisible(false);

				SourceViewPanel sourceViewPanel = gApp.ShowProjectItem(projectSource, false);

				String filePath = projectSource.GetFullImportPath(.. scope .());

				String text = scope .();
				sourceViewPanel.mEditWidget.GetText(text);

				StringView generatorName = default;
				StringView hash = default;

				int dataIdx = -1;
				for (var line in text.Split('\n'))
				{
					if (!line.StartsWith("// "))
					{
						dataIdx = @line.MatchPos + 1;
						break;
					}
					int eqPos = line.IndexOf('=');
					if (eqPos == -1)
						break;
					StringView key = line.Substring(3, eqPos - 3);
					StringView value = line.Substring(eqPos + 1);
					if (key == "Generator")
						generatorName = value;
					else if (key == "GenHash")
						hash = value;
					else
					{
						UIEntry uiEntry = new .();
						uiEntry.mName = new .(key);
						uiEntry.mData = new .(value);
						mUIEntries.Add(uiEntry);
					}
				}

				if ((generatorName == default) || (hash == default))
				{
					Close();
					gApp.Fail(scope $"File '{filePath}' was not generated by a generator that includes regeneration information");
					return;
				}

				if ((dataIdx != -1) && (!allowHashMismatch))
				{
					var origHash = MD5Hash.Parse(hash).GetValueOrDefault();

					StringView dataStr = text.Substring(dataIdx);
					var checkHash = MD5.Hash(.((.)dataStr.Ptr, dataStr.Length));

					if (origHash != checkHash)
					{
						Close();
						Dialog dialog = ThemeFactory.mDefault.CreateDialog("Regenerate?", "This file has been modified since it was generated. Are you sure you want to regenerate?", DarkTheme.sDarkTheme.mIconWarning);
						dialog.AddButton("Yes", new (evt) =>
							{
								gApp.mProjectPanel.Regenerate(true);
								//dialog.Close();
							});
						dialog.AddButton("No", new (evt) =>
							{
								//dialog.Close();
							});
						dialog.PopupWindow(gApp.GetActiveWindow());
						return;
					}
				}

				GenerateKindBar.Entry entry = new .();
				entry.mName = new .(generatorName);
				entry.mTypeName = new .(generatorName);
				mKindBar.mEntries.Add(entry);
				mPendingSelectedEntry = entry;
			}
			else
				mPendingGenList = true;

			mKindBar.mMouseVisible = false;
			mTabWidgets.Add(mKindBar.mEditWidget);
		}

		public ~this()
		{
			var bfCompiler = gApp.mBfResolveCompiler;
			if (mThreadState == .Executing)
			{
				bfCompiler.WaitForBackground();
			}
		}

		public void SelectKind()
		{
			GenerateKindBar.Entry foundEntry = null;

			String text = mKindBar.mEditWidget.GetText(.. scope .());
			for (var entry in mKindBar.mEntries)
				if (entry.mName == text)
					foundEntry = entry;

			if (foundEntry == null)
				return;

			if (mSelectedEntry == foundEntry)
				return;

			mPendingSelectedEntry = foundEntry;
		}

		public void ThreadProc()
		{
			var bfSystem = gApp.mBfResolveSystem;
			var bfCompiler = gApp.mBfResolveCompiler;
			
			String outStr = scope String();

			bfSystem.Lock(0);
			defer bfSystem.Unlock();

			if (mSelectedEntry != null)
			{
				String args = scope .();
				var project = gApp.mWorkspace.FindProject(mProjectName);
				if (project == null)
					return;
				using (gApp.mMonitor.Enter())
				{
					args.AppendF(
						$"""
						ProjectName\t{mProjectName}
						ProjectDir\t{project.mProjectPath}
						FolderDir\t{mFolderPath}
						Namespace\t{mNamespace}
						DefaultNamespace\t{project.mBeefGlobalOptions.mDefaultNamespace}
						WorkspaceName\t{gApp.mWorkspace.mName}
						WorkspaceDir\t{gApp.mWorkspace.mDir}
						DateTime\t{DateTime.Now}

						""");

					if (mSubmitting)
					{
						args.AppendF($"Generator\t{mSelectedEntry.mTypeName}\n");
						for (var uiEntry in mUIEntries)
						{
							String data = scope .();
							if (uiEntry.mData != null)
							{
								data.Append(uiEntry.mData);
							}
							else if (var editWidget = uiEntry.mWidget as EditWidget)
							{
								editWidget.GetText(data);
							}
							else if (var comboBox = uiEntry.mWidget as DarkComboBox)
							{
								comboBox.GetLabel(data);
							}
							else if (var checkBox = uiEntry.mWidget as CheckBox)
							{
								checkBox.Checked.ToString(data);
							}
							data.Replace('\n', '\r');
							args.AppendF($"{uiEntry.mName}\t{data}\n");
						}
					}
				}

				mUIData = new String();
				if (mSubmitting)
					bfCompiler.GetGeneratorGenData(mSelectedEntry.mTypeName, args, mUIData);
				else
					bfCompiler.GetGeneratorInitData(mSelectedEntry.mTypeName, args, mUIData);
			}
			else
			{
				bfCompiler.GetGeneratorTypeDefList(outStr);

				for (var line in outStr.Split('\n', .RemoveEmptyEntries))
				{
					if (line.StartsWith("!error"))
					{
						ShowError(line.Substring(7));

						RehupMinSize();
						continue;
					}

					var entry = new GenerateKindBar.Entry();
					var partItr = line.Split('\t');
					entry.mTypeName = new String(partItr.GetNext().Value);
					if (partItr.GetNext() case .Ok(let val))
						entry.mName = new String(val);
					else
					{
						entry.mName = new String(entry.mTypeName);
						int termPos = entry.mName.LastIndexOf('.');
						if (termPos != -1)
							entry.mName.Remove(0, termPos + 1);
						termPos = entry.mName.LastIndexOf('+');
						if (termPos != -1)
							entry.mName.Remove(0, termPos + 1);
					}
					mKindBar.mEntries.Add(entry);
				}
			}
		}

		public override void CalcSize()
		{
		    mWidth = GS!(320);
		    mHeight = GS!(96);
			mMinWidth = mWidth;
		}

		protected override void RehupMinSize()
		{
			mWidgetWindow.SetMinimumSize(GS!(240), (.)mUIHeight + GS!(24), true);
		}

		void ShowError(StringView error)
		{
			if (mOutputPanel == null)
			{
				mOutputPanel = new OutputPanel();
				AddWidget(mOutputPanel);
				ResizeComponents();
			}
			String str = scope .();
			str.Append(error);
			str.Replace('\r', '\n');
			str.Append("\n");
			mOutputPanel.WriteSmart(str);
		}

		public override void Update()
		{
			base.Update();

			if ((!mKindBar.mEditWidget.mHasFocus) && (mWidgetWindow.mHasFocus))
			{
				var sel = mPendingSelectedEntry ?? mSelectedEntry;
				String editText = mKindBar.mEditWidget.GetText(.. scope .());
				if ((sel != null) && (editText != sel.mName))
				{
					mKindBar.mIgnoreChange = true;
					mKindBar.mEditWidget.SetText(sel.mName);
					mKindBar.mIgnoreChange = false;
				}
			}

			if (mThreadState == .Done)
			{
				if (mSelectedEntry != null)
				{
					List<UIEntry> oldEntries = scope .();
					Dictionary<String, UIEntry> entryMap = scope .();
					if (!mSubmitting)
					{
						for (var uiEntry in mUIEntries)
						{
							if (!entryMap.TryAdd(uiEntry.mName, uiEntry))
								oldEntries.Add(uiEntry);
						}
						mUIEntries.Clear();
					}

					if (mUIData != null)
					{
						if (mOutputPanel != null)
						{
							mOutputPanel.RemoveSelf();
							DeleteAndNullify!(mOutputPanel);
						}

						String fileName = default;
						StringView genText = default;
						bool hadError = false;

						if (mUIData.IsEmpty)
						{
							gApp.Fail("Generator failed to return results");
						}

						LinesLoop: for (var line in mUIData.Split('\n', .RemoveEmptyEntries))
						{
							var partItr = line.Split('\t');
							var kind = partItr.GetNext().Value;

							switch (kind)
							{
							case "!error":
								ShowError(line.Substring(7));
							case "addEdit":
								if (mSubmitting)
									break;
								UIEntry uiEntry = new UIEntry();
								uiEntry.mName = partItr.GetNext().Value.UnQuoteString(.. new .());
								uiEntry.mLabel = partItr.GetNext().Value.UnQuoteString(.. new .());
								var defaultValue = partItr.GetNext().Value.UnQuoteString(.. scope .());
								DarkEditWidget editWidget = new DarkEditWidget();
								uiEntry.mWidget = editWidget;
								editWidget.SetText(defaultValue);
								editWidget.mEditWidgetContent.SelectAll();
								editWidget.mOnSubmit.Add(new => EditSubmitHandler);
								AddWidget(editWidget);
								mUIEntries.Add(uiEntry);
								mTabWidgets.Add(editWidget);
							case "addCombo":
								if (mSubmitting)
									break;
								UIEntry uiEntry = new UIEntry();
								uiEntry.mName = partItr.GetNext().Value.UnQuoteString(.. new .());
								uiEntry.mLabel = partItr.GetNext().Value.UnQuoteString(.. new .());
								var defaultValue = partItr.GetNext().Value.UnQuoteString(.. scope .());
								List<String> choices = new List<String>();
								DarkComboBox comboBox = new DarkComboBox();
								while (partItr.GetNext() case .Ok(let val))
								{
									choices.Add(val.UnQuoteString(.. new .()));
								}
								comboBox.mOnDeleted.Add(new (widget) => { DeleteContainerAndItems!(choices); });
								comboBox.mPopulateMenuAction.Add(new (menu) =>
									{
										for (var choice in choices)
										{
											var item = menu.AddItem(choice);
											item.mOnMenuItemSelected.Add(new (menu) =>
												{
												   	comboBox.Label = menu.mLabel;
												});
										}
									});
								uiEntry.mWidget = comboBox;
								comboBox.Label = defaultValue;
								AddWidget(comboBox);
								mUIEntries.Add(uiEntry);
								mTabWidgets.Add(comboBox);
							case "addCheckbox":
								if (mSubmitting)
									break;
								UIEntry uiEntry = new UIEntry();
								uiEntry.mName = partItr.GetNext().Value.UnQuoteString(.. new .());
								uiEntry.mLabel = partItr.GetNext().Value.UnQuoteString(.. new .());
								var defaultValue = partItr.GetNext().Value;
								DarkCheckBox checkbox = new DarkCheckBox();
								uiEntry.mWidget = checkbox;
								checkbox.Label = uiEntry.mLabel;
								checkbox.Checked = defaultValue == "True";
								AddWidget(checkbox);
								mUIEntries.Add(uiEntry);
								mTabWidgets.Add(checkbox);
							case "error":
								hadError = true;
								gApp.Fail(line.Substring(6).UnQuoteString(.. scope .()));
							case "fileName":
								fileName = line.Substring(9).UnQuoteString(.. scope:: .());
							case "options":
							case "data":
								genText = .(mUIData, @line.MatchPos + 1);
								break LinesLoop;
							}
						}
						
						ResizeComponents();
						RehupMinSize();

						if (fileName?.EndsWith(".bf", .OrdinalIgnoreCase) == true)
							fileName.RemoveFromEnd(3);

						if ((fileName != null) && (!mRegenerating))
						{
							for (char8 c in fileName.RawChars)
							{
								if (!c.IsLetterOrDigit)
								{
									gApp.Fail(scope $"Invalid generated file name: {fileName}");
									hadError = true;
									break;
								}
							}

							if (fileName.IsEmpty)
							{
								gApp.Fail("Geneator failed to specify file name");
								hadError = true;
							}
						}

						if ((!hadError) && (genText != default) && (fileName != null))
						{
							if (!mProjectItem.mDetached)
							{
								if (mRegenerating)
								{
									gApp.mProjectPanel.Regenerate(mProjectItem as ProjectSource, genText);
								}
								else
								{
									gApp.mProjectPanel.Generate(mProjectItem as ProjectFolder, fileName, genText);
								}
							}
							Close();
						}

						if (mPendingUIFocus)
						{
							mPendingUIFocus = false;
							if (!mUIEntries.IsEmpty)
								mUIEntries[0].mWidget.SetFocus();
						}

						DeleteAndNullify!(mUIData);
					}

					//

					if (mSubmitting)
					{
						if (!mClosed)
						{
							mSubmitting = false;
							mSubmitQueued = false;
							mDefaultButton.mDisabled = false;
							mEscButton.mDisabled = false;
						}
					}
					else
					{
						for (var uiEntry in entryMap.Values)
							oldEntries.Add(uiEntry);

						for (var uiEntry in oldEntries)
						{
							mTabWidgets.Remove(uiEntry.mWidget);
							uiEntry.mWidget.RemoveSelf();
							DeleteAndNullify!(uiEntry.mWidget);
						}

						ClearAndDeleteItems(oldEntries);
					}
				}
				else
				{
					mKindBar.mMouseVisible = true;
					mKindBar.SetFocus();
					mKindBar.SetLocation("New Class");
				}
				mThreadState = .None;
				MarkDirty();
			}

			bool isWorking = false;
			if (mThreadState == .None)
			{
				if ((mPendingGenList) || (mPendingSelectedEntry != null))
				{
					isWorking = true;
					var bfCompiler = gApp.mBfResolveCompiler;
					if (!bfCompiler.IsPerformingBackgroundOperation())
					{
						bfCompiler.CheckThreadDone();

						mPendingGenList = false;
						if (mPendingSelectedEntry != null)
						{
							if (mSubmitQueued)
								mSubmitting = true;
							mSelectedEntry = mPendingSelectedEntry;
							mPendingSelectedEntry = null;
						}
						mThreadState = .Executing;
						bfCompiler.DoBackgroundHi(new => ThreadProc, new () =>
							{
								mThreadState = .Done;
							}, false);
					}
				}
			}

			gApp.mBfResolveCompiler.CheckThreadDone();

			if ((mThreadState == .Executing) || (isWorking))
			{
				mThreadWaitCount++;
				if (mUpdateCnt % 8 == 0)
					MarkDirty();
			}
			else
				mThreadWaitCount = 0;
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
			//mClassViewPanel.Resize(0, 0, width, height - GS!(34));
		}

		public override void PopupWindow(WidgetWindow parentWindow, float offsetX = 0, float offsetY = 0)
		{
			base.PopupWindow(parentWindow, offsetX, offsetY);
			//mKindBar.SetFocus();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			void DrawLabel(Widget widget, StringView label)
			{
				if (widget == null)
					return;
				if (widget is CheckBox)
					return;
				g.DrawString(label, widget.mX + GS!(6), widget.mY - GS!(20));
			}

			DrawLabel(mKindBar, mRegenerating ? "Regenerating ..." : "Generator");
			for (var uiEntry in mUIEntries)
				DrawLabel(uiEntry.mWidget, uiEntry.mLabel);
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);

			if (mThreadWaitCount > 10)
			{
				using (g.PushColor(0x60505050))
					g.FillRect(0, 0, mWidth, mHeight - GS!(40));
				IDEUtils.DrawWait(g, mWidth/2, mHeight/2, mUpdateCnt);
			}
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			mUIHeight = GS!(32);

			float insetSize = GS!(12);
			
			mKindBar.Resize(insetSize, mUIHeight, mWidth - insetSize - insetSize, GS!(22));
			mUIHeight += GS!(52);

			for (var uiEntry in mUIEntries)
			{
				if (uiEntry.mWidget == null)
					continue;

				float height = GS!(22);
				if (uiEntry.mWidget is ComboBox)
					height = GS!(26);

				if (uiEntry.mWidget is CheckBox)
				{
					mUIHeight -= GS!(20);
					height = GS!(20);
				}

				uiEntry.mWidget.Resize(insetSize, mUIHeight, mWidth - insetSize - insetSize, height);
				mUIHeight += height + GS!(28);
			}

			if (mOutputPanel != null)
			{
				float startY = mKindBar.mVisible ? GS!(60) : GS!(36);
				mOutputPanel.Resize(insetSize, startY, mWidth - insetSize - insetSize, Math.Max(mHeight - startY - GS!(44), GS!(32)));
				mUIHeight = Math.Max(mUIHeight, GS!(160));
			}
		}

		public override void Close()
		{
			if (mThreadState == .Executing)
			{
				var bfCompiler = gApp.mBfResolveCompiler;
				bfCompiler.RequestFastFinish(true);
				bfCompiler.WaitForBackground();
			}
			base.Close();
		}

		public override void Submit()
		{
			mDefaultButton.mDisabled = true;
			mEscButton.mDisabled = true;

			if (mSubmitQueued)
				return;
			mSubmitQueued = true;
			mPendingSelectedEntry = mPendingSelectedEntry ?? mSelectedEntry;
		}
	}
}
