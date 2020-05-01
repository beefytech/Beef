using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;

namespace IDE.ui
{    
    public class FindAndReplaceDialog : IDEDialog
    {
        EditWidget mEditWidget;
		EditWidget mReplaceWidget;
        DarkComboBox mLocationCombo;
		DarkComboBox mFileTypesCombo;
		bool mIsReplace;
		DarkCheckBox mRecurseDirCheckbox;
		DarkCheckBox mMatchCaseCheckbox;
		DarkCheckBox mMatchWholeWordCheckbox;
		bool mIgnoreContentChanged;
		DarkButton mAbortButton;

        public this(bool isReplace)
        {
			mSettingHistoryManager = gApp.mFindAndReplaceHistoryManager;

			mIsReplace = isReplace;
            mWindowFlags = BFWindow.Flags.ClientSized | BFWindow.Flags.TopMost | BFWindow.Flags.Caption |
                BFWindow.Flags.Border | BFWindow.Flags.SysMenu | BFWindow.Flags.Resizable | .PopupPosition;

            AddOkCancelButtons(new (evt) => { DoFind(); }, null, 0, 1);

            mButtonBottomMargin = GS!(6);
            mButtonRightMargin = GS!(6);

            mEditWidget = AddEdit("");

			if (mIsReplace)
			{
				mReplaceWidget = AddEdit("");
			}

            mLocationCombo = new PathComboBox();
			mLocationCombo.MakeEditable(new PathEditWidget());
            mLocationCombo.Label = FindResultsPanel.sEntireSolution;
            mLocationCombo.mPopulateMenuAction.Add(new => PopulateLocationMenu);
			mLocationCombo.mEditWidget.mOnContentChanged.Add(new (evt) => { UpdateUI(); });
			AddWidget(mLocationCombo);
            AddEdit(mLocationCombo.mEditWidget);

			mFileTypesCombo = new DarkComboBox();
			mFileTypesCombo.MakeEditable();
			mFileTypesCombo.Label = "*";
			mFileTypesCombo.mPopulateMenuAction.Add(new => PopulateFileTypesMenu);
			AddWidget(mFileTypesCombo);
			AddEdit(mFileTypesCombo.mEditWidget);

			mRecurseDirCheckbox = new DarkCheckBox();
			mRecurseDirCheckbox.Label = "&Recurse directories";
			mRecurseDirCheckbox.Checked = true;
			AddDialogComponent(mRecurseDirCheckbox);

			mMatchCaseCheckbox = new DarkCheckBox();
			mMatchCaseCheckbox.Label = "Match &case";
			AddDialogComponent(mMatchCaseCheckbox);

			mMatchWholeWordCheckbox = new DarkCheckBox();
			mMatchWholeWordCheckbox.Label = "Match &whole case";
			AddDialogComponent(mMatchWholeWordCheckbox);

			mAbortButton = new DarkButton();
			mAbortButton.Label = "Abort Current";
			mAbortButton.mOnMouseClick.Add(new (evt) =>
				{
					gApp.mFindResultsPanel.CancelSearch();
				});
			AddDialogComponent(mAbortButton);
			mAbortButton.mVisible = false;

			CreatePrevNextButtons();

			if (var editWidget = gApp.GetActiveWindow().mFocusWidget as EditWidget)
			{
				bool isMultiline = false;

				var content = editWidget.mEditWidgetContent;
				if (content.mSelection.HasValue)
				{
					int selStart = content.mSelection.Value.MinPos;
					int selEnd = content.mSelection.Value.MaxPos;
					for (int i = selStart; i < selEnd; i++)
					{
					    if (content.mData.mText[i].mChar == '\n')
					    {
					        isMultiline = true;
					        break;
					    }
					}
				}

				if (!isMultiline)
				{
					var selText = scope String();
					content.GetSelectionText(selText);
					mEditWidget.SetText(selText);
					mEditWidget.Content.SelectAll();
				}
			}
			

			UpdateUI();
            //mEditWidget.mKeyDownHandler += EditKeyDownHandler;
            //mEditWidget.mContentChangedHandler += (evt) => mFilterChanged = true;
        }

		protected override void UseProps(PropertyBag propBag)
		{
			mIgnoreContentChanged = true;
			mEditWidget.SetText(propBag.Get<String>("Find") ?? "");
			mEditWidget.mEditWidgetContent.SelectAll();
			if (mReplaceWidget != null)
				mReplaceWidget.SetText(propBag.Get<String>("Replace") ?? "");
			mLocationCombo.Label = propBag.Get<String>("Location", FindResultsPanel.sEntireSolution) ?? "";
			mFileTypesCombo.Label = propBag.Get<String>("FileTypes", "*") ?? "";
			mRecurseDirCheckbox.Checked = propBag.Get<bool>("RecurseDir", true);
			mMatchCaseCheckbox.Checked = propBag.Get<bool>("MatchCase");
			mMatchWholeWordCheckbox.Checked = propBag.Get<bool>("MatchWholeWord");
			mIgnoreContentChanged = false;
			UpdateUI();
		}

		void SaveProps()
		{
			PropertyBag propBag = new PropertyBag();
			var str = scope String();
			mEditWidget.GetText(str);
			propBag.Add("Find", str);
			if (mReplaceWidget != null)
			{
				str.Clear();
				mReplaceWidget.GetText(str);
				propBag.Add("Replace", str);
			}
			propBag.Add("Location", mLocationCombo.Label);
			propBag.Add("FileTypes", mFileTypesCombo.Label);
			propBag.Add("RecurseDir", mRecurseDirCheckbox.Checked);
			propBag.Add("MatchCase", mMatchCaseCheckbox.Checked);
			propBag.Add("MatchWholeWord", mMatchWholeWordCheckbox.Checked);

			mSettingHistoryManager.Add(propBag);
		}

		void UpdateUI()
		{
			if (mIgnoreContentChanged)
				return;

			bool foundLocation = false;
			for (var str in FindResultsPanel.sLocationStrings)
			{
				if (str == mLocationCombo.Label)
				{
					foundLocation = true;
					break;
				}
			}

			mRecurseDirCheckbox.mDisabled = foundLocation;
			UpdateProgress();
		}

		void UpdateProgress()
		{
			var title = scope String();
			title.Append(mIsReplace ? "Find and Replace in Files" : "Find in Files");

			bool isSearching = gApp.mFindResultsPanel.IsSearching;
			if (mAbortButton.mVisible != isSearching)
			{
				mAbortButton.mVisible = isSearching;
				mDefaultButton.mDisabled = isSearching;
				MarkDirty();

				if (!isSearching)
					Title = title;
			}

			if ((isSearching) && (mUpdateCnt % 8 == 0))
			{
				title.Append(" - Searching");
				title.Append('.', (mUpdateCnt / 30) % 4);

				MarkDirty();
			}
			Title = title;
		}

        void PopulateLocationMenu(Menu menu)
        {
			for (var str in FindResultsPanel.sLocationStrings)
			{
				var item = menu.AddItem(str);
				item.mOnMenuItemSelected.Add(new (dlg) =>
				{
					mIgnoreContentChanged = true;
					mLocationCombo.Label = str;
					mIgnoreContentChanged = false;
				});
			}
        }

		void PopulateFileTypesMenu(Menu menu)
		{
			var strings = scope List<String>();
			strings.Add("*");
			strings.Add("*.bf");

			for (var str in strings)
			{
				var item = menu.AddItem(str);
				item.mOnMenuItemSelected.Add(new (dlg) => { mFileTypesCombo.Label = str; });
			}
		}

        void DoFind()
        {
            IDEApp.sApp.ShowFindResults(false);
            FindResultsPanel.SearchOptions searchOptions = new FindResultsPanel.SearchOptions();

			searchOptions.mSearchString = new String();
			mEditWidget.GetText(searchOptions.mSearchString);

			if (mReplaceWidget != null)
			{
				searchOptions.mReplaceString = new String();
				mReplaceWidget.GetText(searchOptions.mReplaceString);
			}

			searchOptions.mSearchLocation = new String(mLocationCombo.Label);

			searchOptions.mFileTypes = new List<String>();
			var fileTypes = scope String();
			mFileTypesCombo.GetLabel(fileTypes);
			for (var fileType in fileTypes.Split(';'))
				searchOptions.mFileTypes.Add(new String(fileType));

			searchOptions.mRecurseDirectories = mRecurseDirCheckbox.Checked;
			searchOptions.mMatchCase = mMatchCaseCheckbox.Checked;
			searchOptions.mMatchWholeWord = mMatchWholeWordCheckbox.Checked;

            gApp.mFindResultsPanel.Search(searchOptions);

			SaveProps();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float curY = GS!(30);

			mAbortButton.Resize(GS!(6), mDefaultButton.mY, mFont.GetWidth(mAbortButton.mLabel) + GS!(30), mDefaultButton.mHeight);

            mEditWidget.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			if (mReplaceWidget != null)
			{
				curY += GS!(42);
				mReplaceWidget.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));
			}

            curY += GS!(42);
            mLocationCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			curY += GS!(42);
			mFileTypesCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			curY += GS!(28);
			mRecurseDirCheckbox.Resize(GS!(6), curY, mRecurseDirCheckbox.CalcWidth(), GS!(22));
			curY += GS!(22);
			mMatchCaseCheckbox.Resize(GS!(6), curY, mMatchCaseCheckbox.CalcWidth(), GS!(22));
			curY += GS!(22);
			mMatchWholeWordCheckbox.Resize(GS!(6), curY, mMatchWholeWordCheckbox.CalcWidth(), GS!(22));
        }

        public override void CalcSize()
        {
            mWidth = GS!(400);
            mHeight = GS!(mIsReplace ? 280 : 240);
        }

		public override void AddedToParent()
		{
			base.AddedToParent();
			RehupMinSize();
		}

		void RehupMinSize()
		{
			mWidgetWindow.SetMinimumSize(GS!(300), (.)GS!(mIsReplace ? 264 : 224), true);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			RehupMinSize();
		}

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            g.DrawString("Find what:", 6, mEditWidget.mY - GS!(18));
			if (mReplaceWidget != null)
				g.DrawString("Replace with:", GS!(6), mReplaceWidget.mY - GS!(18));
            g.DrawString("Look in:", GS!(6), mLocationCombo.mY - GS!(18));
			g.DrawString("Look at these file types:", GS!(6), mFileTypesCombo.mY - GS!(18));
        }

		public override void Update()
		{
			base.Update();
			UpdateProgress();
		}
    }
}
