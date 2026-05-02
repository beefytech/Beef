using Beefy.widgets;
using Beefy.theme.dark;
using IDE.Compiler;
using System.Collections;
using System.Threading;
using System;
using Beefy.gfx;
using System.IO;
using Beefy.utils;

namespace IDE.ui
{
	class ErrorsPanel : Panel
	{
        public static String sCurrentDocument = "Current File";
        public static String sOpenDocuments = "Open Files";
		public static String sCurrentProject = "Current Project";
        public static String sEntireSolution = "Entire Solution";
		public static String[] sLocationStrings = new .(sEntireSolution, sCurrentDocument, sOpenDocuments, sCurrentProject) ~ delete _;

		public class ErrorsListView : IDEListView
		{
			protected override ListViewItem CreateListViewItem()
			{
				return new ErrorsListViewItem();
			}
		}

		public class ErrorsListViewItem : IDEListViewItem
		{
			public String mFilePath ~ delete _;
			public int mLine;
			public int mColumn;

			public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
			{
				base.MouseClicked(x, y, origX, origY, btn);

				if (btn == 1)
				{
					Menu menu = new Menu();
					var menuItemCopySingle = menu.AddItem("Copy");
					menuItemCopySingle.mOnMenuItemSelected.Add(new (evt) =>
					{
						String buffer = scope .();
						mListView.GetRoot().WithSelectedItems(scope (item) =>
							{
								var errorItem = (ErrorsListViewItem)item;
								if (!buffer.IsEmpty)
									buffer.Append("\n");
								errorItem.CopyError(buffer);
							});
						if (!buffer.IsEmpty)
							gApp.SetClipboardText(buffer);
					});

					MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
					menuWidget.Init(this, x, y);
				}
			}

			public void CopyError (String buffer)
			{
				var preffix = scope String();
				Font.StrRemoveColors(this.mSubItems[0].mLabel, preffix);
				preffix.ToUpper();
				var description = this.mSubItems[1].mLabel;

				buffer.AppendF("{}: {} at line {}:{} in {}", preffix, description, this.mLine, this.mColumn, this.mFilePath);
			}

			public override void DrawSelect(Graphics g)
			{
				bool hasFocus = mListView.mHasFocus;
				if ((mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(mListView)))
					hasFocus = true;
			    using (g.PushColor(hasFocus ? 0xFFFFFFFF : 0x80FFFFFF))
			        base.DrawSelect(g);
			}
			
			public bool Goto()
			{
				if (mFilePath == null)
					return false;
				gApp.ShowSourceFileLocation(mFilePath, -1, -1, mLine, mColumn, .Always);
				return false;
			}
		}

		public class ErrorSeverityToggleButton : ToggleButton
		{
			private String mNounSingular ~ delete _;
			private String mNounPlural ~ delete _;
			public Image mIcon;
			private int? mFilteredCount;
			private int mTotalCount;
			
			public int? FilteredCount
			{
				get => mFilteredCount;
				set
				{
					if (value != mFilteredCount)
					{
						mFilteredCount = value;
						UpdateLabel();
					}
				}
			}

			public int TotalCount
			{
				get => mTotalCount;
				set
				{
					if (value != mTotalCount)
					{
						mTotalCount = value;
						UpdateLabel();
					}
				}
			}

			public StringView NounSingular
			{
				get => mNounSingular;
				set => String.NewOrSet!(mNounSingular, value);
			}
			public StringView NounPlural
			{
				get => mNounPlural;
				set => String.NewOrSet!(mNounPlural, value);
			}

			public this()
			{
				mLabelAlign = .Left;
			}

			public override void Draw(Graphics g)
			{
				if (mIcon != null)
				{
					mLabelXOfs = GS!(2) + mIcon.mWidth;
					base.Draw(g);
					g.Draw(mIcon, GS!(2), (mHeight - mIcon.mHeight) / 2);
				}
				else
				{
					base.Draw(g);
				}
			}

			public void UpdateLabel()
			{
				var noun = (mTotalCount == 1) ? mNounSingular : mNounPlural;

				if (mFilteredCount.HasValue)
				{
					Label = scope $"{mFilteredCount.Value} of {mTotalCount} {noun}";
				}
				else
				{
					Label = scope $"{mTotalCount} {noun}";
				}

			}

			public float CalcWidth()
			{
				float w = GS!(2);

				if (mIcon != null)
				{
					w += GS!(2) + mIcon.mWidth;
				}

				var font = DarkTheme.sDarkTheme.mSmallFont;
				if (mLabel != null)
					w += font.GetWidth(mLabel);

				w += GS!(6);
				return w;
			}
		}

		public ErrorsListView mErrorLV;

		public bool mNeedsResolveAll;
		public int mDataId;
		public int mErrorListId;
		public int mErrorRefreshId;
		public Monitor mMonitor = new .() ~ delete _;
		public Dictionary<String, List<BfPassInstance.BfError>> mParseErrors = new .() ~ delete _;
		public List<BfPassInstance.BfError> mResolveErrors = new .() ~ DeleteContainerAndItems!(_);
		public int mDirtyTicks;

		public int mErrorCount;
		public int mWarningCount;
		public int? mFilteredErrorCount = null;
		public int? mFilteredWarningCount = null;

		public ErrorSeverityToggleButton mErrorsToggle;
		public ErrorSeverityToggleButton mWarningsToggle;
		public DarkComboBox mScopeFilterCombo;
		public DarkComboBox mSearchTextCombo;
		
		public bool ShowErrors
		{
			get => mErrorsToggle.mToggled;
			set => mErrorsToggle.mToggled = value;
		}
		public bool ShowWarnings
		{
			get => mWarningsToggle.mToggled;
			set => mWarningsToggle.mToggled = value;
		}

		public this()
		{
			mErrorLV = new .();
			//mErrorLV.mPanel = this;
			//mErrorLV.SetShowHeader(false);
			mErrorLV.InitScrollbars(true, true);
			mErrorLV.mLabelX = GS!(6);
			//mErrorLV.mOnItemMouseDown.Add(new => ItemMouseDown);
			mErrorLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mErrorLV.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);
			mErrorLV.AddColumn(100, "Code");
			mErrorLV.AddColumn(400, "Description");
			mErrorLV.AddColumn(100, "Project");
			mErrorLV.AddColumn(120, "File");
			mErrorLV.AddColumn(40, "Line");
			mErrorLV.mOnItemMouseDown.Add(new (item, x, y, btnNum, btnCount) =>
				{
					ListViewItemMouseDown(item, x, y, btnNum, btnCount);

					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (ErrorsListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}

					//mErrorLV.GetRoot().SelectItemExclusively()
				});
			//let newItem = mErrorLV.GetRoot().CreateChildItem();
			//newItem.Label = "Hey";

			AddWidget(mErrorLV);

			mScopeFilterCombo = new DarkComboBox();
			mScopeFilterCombo.Label = sEntireSolution;
			mScopeFilterCombo.mPopulateMenuAction.Add(new => PopulateLocationMenu);
			AddWidget(mScopeFilterCombo);

			mErrorsToggle = new ErrorSeverityToggleButton();
			mErrorsToggle.mIcon = DarkTheme.sDarkTheme.GetImage(.CodeError);
			mErrorsToggle.NounSingular = "Error";
			mErrorsToggle.NounPlural = "Errors";
			mErrorsToggle.mToggled = true;
			mErrorsToggle.HoverText = "Toggle visibility of errors";
			mErrorsToggle.mOnMouseDown.Add(new (evt) => { InvalidateErrorList(); });
			mErrorsToggle.UpdateLabel();
			AddWidget(mErrorsToggle);

			mWarningsToggle = new ErrorSeverityToggleButton();
			mWarningsToggle.mIcon = DarkTheme.sDarkTheme.GetImage(.CodeWarning);
			mWarningsToggle.NounSingular = "Warning";
			mWarningsToggle.NounPlural = "Warnings";
			mWarningsToggle.mToggled = true;
			mWarningsToggle.HoverText = "Toggle visibility of warnings";
			mWarningsToggle.mOnMouseDown.Add(new (evt) => { InvalidateErrorList(); });
			mWarningsToggle.UpdateLabel();
			AddWidget(mWarningsToggle);

			mSearchTextCombo = new DarkComboBox();
			mSearchTextCombo.MakeEditable();
			mSearchTextCombo.mEditWidget.mOnContentChanged.Add(new (evt) =>
				{
					InvalidateErrorList();
				});
			AddWidget(mSearchTextCombo);
		}

		void PopulateLocationMenu(Menu menu)
		{
			for (var str in sLocationStrings)
			{
				var item = menu.AddItem(str);
				item.mOnMenuItemSelected.Add(new (dlg) =>
				{
					mScopeFilterCombo.Label = str;
					InvalidateErrorList();
				});
			}
		}

		public ~this()
		{
			ClearParserErrors(null);
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "ErrorsPanel");

			data.Add("ShowErrors", ShowErrors);
			data.Add("ShowWarnings", ShowWarnings);
			data.Add("Scope", mScopeFilterCombo.Label);
		}

		public override bool Deserialize(StructuredData data)
		{
		    base.Deserialize(data);

			ShowErrors = data.GetBool("ShowErrors", true);
			ShowWarnings = data.GetBool("ShowWarnings", true);

			String scopeLabel = scope .();
			data.GetString("Scope", scopeLabel);
			if (sLocationStrings.Contains(scopeLabel))
				mScopeFilterCombo.Label = scopeLabel;

			return true;
		}

		private float LayoutToolbar()
		{
			float toolbarHeight = GS!(28);
			float btnY = GS!(2);
			float btnH = toolbarHeight - GS!(4);
			float curX = GS!(4);

			float scopeSelectW = GS!(120);
			mScopeFilterCombo.Resize(curX, btnY, scopeSelectW, btnH);
			curX += scopeSelectW + GS!(4);

			float errW = mErrorsToggle.CalcWidth();
			mErrorsToggle.Resize(curX, btnY, errW, btnH);
			curX += errW + GS!(4);

			float warnW = mWarningsToggle.CalcWidth();
			mWarningsToggle.Resize(curX, btnY, warnW, btnH);
			curX += warnW;

			float searchMinWidth = GS!(100);
			float searchWantWidth = GS!(250);
			float searchWidth = Math.Clamp(mWidth - curX - GS!(4), searchMinWidth, searchWantWidth);
			mSearchTextCombo.Resize(mWidth - searchWidth, btnY, searchWidth - GS!(4), btnH);

			return toolbarHeight;
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			float toolbarHeight = LayoutToolbar();
			mErrorLV.Resize(0, toolbarHeight, width, Math.Max(height - toolbarHeight, 0));
		}

		public enum ResolveKind
		{
			None,
			Parse,
			Resolve
		}

		public void ProcessPassInstance(BfPassInstance passInstance, BfPassInstance.PassKind passKind)
		{
			using (mMonitor.Enter())
			{
				int32 errorCount = passInstance.GetErrorCount();
				if (passKind != .Parse)
				{
					if (!mResolveErrors.IsEmpty)
						mDataId++;

					for (let error in mResolveErrors)
					{
						if (error.mIsWarning)
							mWarningCount--;
						else
							mErrorCount--;
					}

					ClearAndDeleteItems(mResolveErrors);
					mResolveErrors.Capacity = mResolveErrors.Count;
				}
				var bfl = scope:: List<BfPassInstance.BfError>();
				for (int32 errorIdx = 0; errorIdx < errorCount; errorIdx++)
				{
				    BfPassInstance.BfError bfError = new BfPassInstance.BfError();
				    passInstance.GetErrorData(errorIdx, bfError, true);
					if (bfError.mFilePath == null)
						bfError.mFilePath = new String(""); //for sort below

					bfl.Add(bfError);
					for (int32 moreInfoIdx < bfError.mMoreInfoCount)
					{
						BfPassInstance.BfError moreInfo = new BfPassInstance.BfError();
						passInstance.GetMoreInfoErrorData(errorIdx, moreInfoIdx, moreInfo, true);
						if (bfError.mMoreInfo == null)
							bfError.mMoreInfo = new List<BfPassInstance.BfError>();
						bfError.mMoreInfo.Add(moreInfo);
					}
				}

				function int(int lhs, int rhs) ascLambda = (lhs, rhs) => lhs <=> rhs;
				bfl.Sort(scope (lhs, rhs) => ascLambda(lhs.mFilePath.GetHashCode()+lhs.mSrcStart, rhs.mFilePath.GetHashCode()+rhs.mSrcStart));
				
				for (int32 errorIdx = 0; errorIdx < bfl.Count; errorIdx++)
				{
					var bfError = bfl[errorIdx];

					if (bfError.mIsWarning)
					{
						mWarningCount++;
					}
					else
						mErrorCount++;

					if (passKind == .Parse)
					{
						if (bfError.mFilePath == null)
							bfError.mFilePath = new String("");
						bool added = mParseErrors.TryAdd(bfError.mFilePath, var keyPtr, var valuePtr);
						if (added)
						{
							*keyPtr = new .(bfError.mFilePath);
							*valuePtr = new .();
						}
						(*valuePtr).Add(bfError);
					}
					else
						mResolveErrors.Add(bfError);

					mDataId++;
				}
			}
		}

		/// Invalidates the error list and forces a rebuild,
		/// so filter changes can take effect immediately in the next update,
		/// even while the compiler is busy (which keeps mDirtyTicks > 0).
		void InvalidateErrorList()
		{
			mErrorListId = -1;
		}

		public void OnActiveSourceViewChanged()
		{
			if (mScopeFilterCombo.Label != sEntireSolution)
				InvalidateErrorList();
		}

		public void OnSourceViewClosed()
		{
			if (mScopeFilterCombo.Label == sOpenDocuments || mScopeFilterCombo.Label == sCurrentDocument)
				InvalidateErrorList();
		}

		public void OnSourceViewOpened()
		{
			if (mScopeFilterCombo.Label == sOpenDocuments || mScopeFilterCombo.Label == sCurrentDocument)
				InvalidateErrorList();
		}

		public void ClearParserErrors(String filePath)
		{
			using (mMonitor.Enter())
			{
				void DeleteErrorList(List<BfPassInstance.BfError> list)
				{
					for (let error in list)
					{
						if (error.mIsWarning)
							mWarningCount--;
						else
							mErrorCount--;
						delete error;
					}
					delete list;
				}

				if (filePath == null)
				{
					for (var kv in mParseErrors)
					{
						delete kv.key;
						DeleteErrorList(kv.value);
						mDataId++;
					}
					mParseErrors.Clear();
				}
				else
				{
					if (mParseErrors.GetAndRemove(filePath) case .Ok((let key, let list)))
					{
						delete key;
						DeleteErrorList(list);
						mDataId++;
					}
				}
			}
		}

		public void Clear()
		{
			using (mMonitor.Enter())
			{
				ClearParserErrors(null);
				ClearAndDeleteItems(mResolveErrors);
				mDataId++;
				mErrorCount = 0;
				mWarningCount = 0;
			}
		}

		void ProcessErrors()
		{
			using (mMonitor.Enter())
			{
				if (mDataId != mErrorListId)
				{
					let root = mErrorLV.GetRoot();

					StringView textFilter = null;
					String activeProjectName = null;
					List<String> openFilePaths = scope .();
					bool hasPathFilter = false;
					bool hasProjectFilter = false;
					bool hasFilter = false;
					bool hasTextFilter = false;

					switch (mScopeFilterCombo.Label)
					{
					case sEntireSolution:
						// Nothing to do.
					case sCurrentDocument:
						hasFilter = true;
						hasPathFilter = true;
						var svp = gApp.GetActiveSourceViewPanel(true);
						if (svp?.mFilePath != null)
						{
							openFilePaths.Add(svp.mFilePath);
						}
					case sCurrentProject:
						hasFilter = true;
						hasProjectFilter = true;
						var svp = gApp.GetActiveSourceViewPanel(true);
						activeProjectName = svp?.mProjectSource?.mProject?.mProjectName;
					case sOpenDocuments:
						hasFilter = true;
						hasPathFilter = true;
						gApp.WithSourceViewPanels(scope (svp) =>
							{
								if (svp.mFilePath != null)
								{
									openFilePaths.Add(svp.mFilePath);
								}
							});
					}

					if (!mSearchTextCombo.Label.IsEmpty)
					{
						hasTextFilter = true;
						hasFilter = true;
						
						textFilter = mSearchTextCombo.Label;
					}

					if (hasFilter)
					{
						mFilteredErrorCount = 0;
						mFilteredWarningCount = 0;
					}
					else
					{
						mFilteredErrorCount = null;
						mFilteredWarningCount = null;
					}

					int idx = 0;
					void HandleError(BfPassInstance.BfError error)
					{
						if (error.mIsWarning ? !ShowWarnings : !ShowErrors)
							return;

						if (hasProjectFilter
							&& ((activeProjectName == null) || (error.mProject != activeProjectName)))
						{
							return;
						}

						if (hasPathFilter)
						{
							if (error.mFilePath == null)
								return;

							bool matched = false;
							for (String openFilePath in openFilePaths)
							{
								if (Path.Equals(error.mFilePath, openFilePath))
								{
									matched = true;
									break;
								}
							}
							if (!matched)
								return;
						}

						int codeDigitStartIdx = int32.MaxValue;
						String codeStr = scope String(32);
						char8[5] codeColorRaw = Font.EncodeColor(error.mIsWarning ? 0xFFFFFF80 : 0xFFFF8080);
						StringView encodedCodeColor = StringView(&codeColorRaw, codeColorRaw.Count);
						codeStr.AppendF(error.mIsWarning ? "{}Warning" : "{}Error", encodedCodeColor);
						if (error.mCode != 0)
						{
							// + 1: we add a space before code
							codeDigitStartIdx = codeStr.Length + 1;
							codeStr.AppendF(" {}", error.mCode);
						}
						codeStr.AppendF("{}", Font.EncodePopColor());

						// Copy project name to allow marking the matched filter.
						let projectName = error.mProject == null ? null : scope String(error.mProject);

						let fileName = scope String(128);
						Path.GetFileName(error.mFilePath, fileName);
						
						char8[5] matchColorRaw = Font.EncodeColor(DarkTheme.COLOR_MENU_FOCUSED);
						StringView encodedMatchColor = StringView(&matchColorRaw, matchColorRaw.Count);

						int errorDescMatchIdx = -1;

						if (hasTextFilter)
						{
							errorDescMatchIdx = error.mError.IndexOf(textFilter, true);
							int fileNameIdx = fileName.IndexOf(textFilter, true);
							int projectNameIdx = projectName?.IndexOf(textFilter, true) ?? -1;
							int codeMatchIdx = codeStr.IndexOf(textFilter, codeDigitStartIdx);

							if (errorDescMatchIdx == -1 &&
								fileNameIdx == -1 &&
								projectNameIdx == -1 &&
								codeMatchIdx == -1)
							{
								return;
							}
							
							// Mark the matched text

							if (fileNameIdx != -1)
							{
								fileName.Insert(fileNameIdx + textFilter.Length, Font.EncodePopColor());
								fileName.Insert(fileNameIdx, encodedMatchColor);
							}
							if (projectNameIdx != -1)
							{
								projectName.Insert(projectNameIdx + textFilter.Length, Font.EncodePopColor());
								projectName.Insert(projectNameIdx, encodedMatchColor);
							}
							if (codeMatchIdx != -1)
							{
								// If we don't match the digits until the end, reapply color for code
								if (codeMatchIdx + textFilter.Length < codeStr.Length - 1)
									codeStr.Insert(codeMatchIdx + textFilter.Length, encodedCodeColor);

								codeStr.Insert(codeMatchIdx, encodedMatchColor);
							}
						}

						ErrorsListViewItem item;

						bool changed = false;
						void SetLabel(ListViewItem item, StringView str)
						{
							if (item.Label == str)
								return;
							changed = true;
							item.Label = str;
						}

						if (idx >= root.GetChildCount())
						{
							item = (.)root.CreateChildItem();
							item.CreateSubItem(1);
							item.CreateSubItem(2);
							item.CreateSubItem(3);
							item.CreateSubItem(4);
						}
						else
							item = (.)root.GetChildAtIndex(idx);

						if (error.mFilePath == null)
							DeleteAndNullify!(error.mFilePath);
						else
							String.NewOrSet!(item.mFilePath, error.mFilePath);
						item.mLine = error.mLine;
						item.mColumn = error.mColumn;

						SetLabel(item, codeStr);

						let descItem = item.GetSubItem(1);
						String errStr = scope String(32);
						int maxLen = 4*1024;
						if (error.mError.Length > maxLen)
						{
							errStr.Append(error.mError.Substring(0, maxLen));
							errStr.Append("...");
						}
						else
							errStr.Append(error.mError);
						errStr.Replace('\n', ' ');

						// Mark the matched text in the error description.
						if (errorDescMatchIdx != -1 && errorDescMatchIdx < errStr.Length)
						{
							errStr.Insert(Math.Min(errorDescMatchIdx + textFilter.Length, errStr.Length), Font.EncodePopColor());
							errStr.Insert(errorDescMatchIdx, encodedMatchColor);
						}

						SetLabel(descItem, errStr);

						let projectItem = item.GetSubItem(2);
						SetLabel(projectItem, projectName);

						let fileNameItem = item.GetSubItem(3);
						SetLabel(fileNameItem, fileName);
						let lineNumberItem = item.GetSubItem(4);
						if (error.mLine != -1)
							SetLabel(lineNumberItem, scope String(16)..AppendF("{}", error.mLine + 1));
						else
							SetLabel(lineNumberItem, "");

						if (changed)
							item.Focused = false;

						idx++;

						if (hasFilter)
						{
							if (error.mIsWarning)
								mFilteredWarningCount += 1;
							else
								mFilteredErrorCount += 1;
						}
					}

					if (!mParseErrors.IsEmpty)
					{
						List<String> paths = scope .();
						for (var path in mParseErrors.Keys)
							paths.Add(path);
						paths.Sort();

						for (var path in paths)
						{
							for (var error in mParseErrors[path])
								HandleError(error);
						}
					}

					for (let error in mResolveErrors)
						HandleError(error);

					while (root.GetChildCount() > idx)
						root.RemoveChildItemAt(root.GetChildCount() - 1);

					mErrorListId = mDataId;
					MarkDirty();
				}
			}
		}

		public void UpdateAlways()
		{
			if (mErrorRefreshId != mDataId)
			{
				gApp.MarkDirty();
				mErrorRefreshId = mDataId;
			}

			let compiler = gApp.mBfResolveCompiler;
			if ((mNeedsResolveAll) && (compiler != null) && (!compiler.IsPerformingBackgroundOperation()))
			{
				if (compiler.mResolveAllWait == 0)
					compiler.QueueDeferredResolveAll();
				mNeedsResolveAll = false;
			}
		}

		public override void Update()
		{
			base.Update();

			if ((mErrorsToggle.TotalCount != mErrorCount) || (mErrorsToggle.FilteredCount != mFilteredErrorCount)
				 || (mWarningsToggle.TotalCount != mWarningCount) || (mWarningsToggle.FilteredCount != mFilteredWarningCount))
			{
				mErrorsToggle.TotalCount = mErrorCount;
				mErrorsToggle.FilteredCount = mFilteredErrorCount;
				mWarningsToggle.TotalCount = mWarningCount;
				mWarningsToggle.FilteredCount = mFilteredWarningCount;
				LayoutToolbar();
				MarkDirty();
			}

			if (!mVisible)
			{
				// Very dirty
				mDirtyTicks = Math.Max(100, mDirtyTicks);
				return;
			}

			let compiler = gApp.mBfResolveCompiler;
			if ((compiler == null) ||
				((!compiler.IsPerformingBackgroundOperation()) && (compiler.mResolveAllWait == 0)))
				mDirtyTicks = 0;
			else
				mDirtyTicks++;

			// mErrorListId = -1 -> force processing
			if (mErrorListId == -1 || mDirtyTicks == 0)
				ProcessErrors();
		}
		
		public void SetNeedsResolveAll()
		{
			mNeedsResolveAll = true;
		}
		
		public void ShowErrorNext()
		{
			if(mDirtyTicks==0)
				ProcessErrors();

			bool foundFocused = false;
			let root = mErrorLV.GetRoot();
			if (root.GetChildCount() == 0)
				return;
			for (let lvItem in root.mChildItems)
			{
				if (lvItem.Focused)
				{
					lvItem.Focused = false;
					foundFocused = true;
				}
				else if (foundFocused)
				{
					lvItem.Focused = true;
					((ErrorsListViewItem)lvItem).Goto();
					return;
				}
			}

			let lvItem = (ErrorsListViewItem)root.GetChildAtIndex(0);
			lvItem.Focused = true;
			lvItem.Goto();
		}
	}
}
