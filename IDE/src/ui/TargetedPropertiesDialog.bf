using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.events;

namespace IDE.ui
{
	class NewConfigDialog : IDEDialog
	{
		TargetedPropertiesDialog mTargetedProperties;
		EditWidget mConfigNameEdit;
		DarkComboBox mCopiedConfigCombo;
		String mCopyConfigName = new String() ~ delete _;

		public this(TargetedPropertiesDialog targetedProperties) //: base("New Config", "Config Name")
		{
			mTargetedProperties = targetedProperties;
			mCopyConfigName.Set(mTargetedProperties.mActiveConfigName);
			mTitle = new String("New Config");
			mConfigNameEdit = AddEdit(mTargetedProperties.mActiveConfigName);
			mConfigNameEdit.mEditWidgetContent.SelectAll();
			mCopiedConfigCombo = new DarkComboBox();
			mCopiedConfigCombo.mPopulateMenuAction.Add(new => PopulateConfigMenu);
			AddDialogComponent(mCopiedConfigCombo);
			if (!mTargetedProperties.mActiveConfigName.IsEmpty)
				mCopiedConfigCombo.Label = (scope String()..AppendF("Active({0})", mTargetedProperties.mActiveConfigName));

			AddOkCancelButtons(
			    new (evt) => { evt.mCloseDialog = Finish(); },
			    null, 0, 1);
		}

		void SelectConfig(StringView configName)
		{
			mCopyConfigName.Set(configName);
		}

		void PopulateConfigMenu(Menu menu)
		{
			Menu item;

			List<String> sortedConfigNames = scope List<String>(IDEApp.sApp.mWorkspace.mConfigs.Keys);
			sortedConfigNames.Sort(scope (a, b) => String.Compare(a, b, true));

			if (!mTargetedProperties.mActiveConfigName.IsEmpty)
			{
				String dispStr = StackStringFormat!("Active({0})", mTargetedProperties.mActiveConfigName);
				item = menu.AddItem(dispStr);
				item.mOnMenuItemSelected.Add(new (evt) => { SelectConfig(mTargetedProperties.mActiveConfigName); });
			}

			for (var configName in sortedConfigNames)
			{
			    item = menu.AddItem(configName);
			    item.mOnMenuItemSelected.Add(new (evt) => { SelectConfig(configName); });
			}
		}

		bool Finish()
		{
			var text = scope String();
			mDialogEditWidget.GetText(text);
			return mTargetedProperties.CreateNewConfig(text, mCopyConfigName);
		}

		public override void CalcSize()
		{
		    mWidth = GS!(300);
		    mHeight = GS!(140);
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			DrawLabel(g, mConfigNameEdit, "New Config Name");
			DrawLabel(g, mCopiedConfigCombo, "Copy From Config");
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			float curY = GS!(30);
			mConfigNameEdit.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));
			curY += GS!(42);
			mCopiedConfigCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(24));
		}
	}

	class EditTargetDialog : IDEDialog
	{
		public enum Kind
		{
			Config,
			Platform
		}

		TargetedPropertiesDialog mTargetedProperties;
		IDEListView mListView;
		ButtonWidget mDeleteButton;
		ButtonWidget mRenameButton;

		public class Entry
		{
			public String mOrigName ~ delete _;
			public String mNewName ~ delete _;
			public bool mDelete;
		}

		public List<Entry> mEntries = new .() ~ DeleteContainerAndItems!(_);
		Dictionary<ListViewItem, Entry> mEntryMap = new .() ~ delete _;

		public this(TargetedPropertiesDialog targetedProperties, Kind kind) //: base("New Config", "Config Name")
		{
			mTargetedProperties = targetedProperties;
			mTitle = new String((kind == .Config) ? "Configuration Manager" : "Platform Manager");
			mWindowFlags |= .Resizable;

			mListView = new IDEListView();
			mListView.mOnEditDone.Add(new => HandleEditDone);
			mListView.InitScrollbars(false, true);
			mListView.mVertScrollbar.mPageSize = GS!(100);
			mListView.mVertScrollbar.mContentSize = GS!(500);
			mListView.UpdateScrollbars();
			mListView.AddColumn(100, "Name");
			mListView.mLabelX = GS!(8);

			//mListView.mDragEndHandler += HandleDragEnd;
			//mListView.mDragUpdateHandler += HandleDragUpdate;
			AddWidget(mListView);

			mDeleteButton = AddButton("Delete", new (evt) => { evt.mCloseDialog = false; QueryDelete(); });
			mRenameButton = AddButton("Rename", new (evt) => { evt.mCloseDialog = false; QueryRename(); });
		}

		void HandleEditDone(EditWidget editWidget, bool cancelled)
		{
			if (cancelled)
				return;

			let listViewItem = mListView.GetRoot().FindFocusedItem();
			if (listViewItem == null)
				return;

			var entry = mEntryMap[listViewItem];
			if (entry.mNewName == null)
				entry.mNewName = new String();
			else
				entry.mNewName.Clear();
			editWidget.GetText(entry.mNewName);
			entry.mNewName.Trim();

			listViewItem.Label = entry.mNewName;
		}

		void DoDelete()
		{
			mListView.GetRoot().WithSelectedItems(scope [&](listViewItem) =>
				{
					let entry = mEntryMap[listViewItem];
					entry.mDelete = true;
					listViewItem.Remove();
					gApp.DeferDelete(listViewItem);
				});
		}

		void QueryDelete()
		{
			String names = scope String();
			int itemCount = 0;
			mListView.GetRoot().WithSelectedItems(scope [&](listViewItem) =>
				{
					if (itemCount > 0)
						names.Append(", ");
					names.Append(listViewItem.Label);
					++itemCount;
				});

			String queryStr = scope String()..AppendF("Are you sure you want delete '{0}'", names);
			let dialog = ThemeFactory.mDefault.CreateDialog("Delete?", queryStr, DarkTheme.sDarkTheme.mIconWarning);
			dialog.AddYesNoButtons(new (dlg) => { DoDelete(); }, null, -1, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

		void QueryRename()
		{
			let listViewItem = mListView.GetRoot().FindFocusedItem();
			if (listViewItem == null)
				return;

			mListView.EditListViewItem(listViewItem);
		}

		public void Add(StringView name)
		{
			let entry = new Entry();
			entry.mOrigName = new String(name);
			mEntries.Add(entry);
		}

		public void FinishInit()
		{
			mEntries.Sort(scope (lhs, rhs) => String.Compare(lhs.mOrigName, rhs.mOrigName, true));

			for (let entry in mEntries)
			{
				let listViewItem = mListView.GetRoot().CreateChildItem();
				listViewItem.Label = entry.mOrigName;
				listViewItem.mOnMouseDown.Add(new => ValueClicked);
				mEntryMap[listViewItem] = entry;
			}
		}

		public override void CalcSize()
		{
		    mWidth = GS!(320);
		    mHeight = GS!(220);
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			float buttonWidth = GS!(80);
			mDeleteButton.Resize(mWidth - buttonWidth - GS!(6), GS!(6), buttonWidth, GS!(24));
			mRenameButton.Resize(mWidth - buttonWidth - GS!(6), GS!(34), buttonWidth, GS!(24));

			mListView.Resize(GS!(6), GS!(6), mWidth - buttonWidth - GS!(20), mHeight - GS!(48));
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
			IDEUtils.DrawOutline(g, mListView, 0, 1);
		}

		public void ValueClicked(MouseEvent theEvent)
		{
		    let clickedItem = (DarkListViewItem)theEvent.mSender;
		    mListView.SetFocus();

			if (theEvent.mBtn == 1)
			{
				if (!clickedItem.Selected)
					mListView.GetRoot().SelectItem(clickedItem, true);
			}
			else
			{
				mListView.GetRoot().SelectItem(clickedItem, true);
			}
		}
	}

    public class TargetedPropertiesDialog : PropertiesDialog
    {
		public enum TargetedKind
		{
			None,
			Platform,
			Config
		}

        protected const String sConfigLabel = "Configuration:";
        protected const String sPlatformLabel = "Platform:";

        public String mActiveConfigName = new String() ~ delete _;
        public String mActivePlatformName = new String() ~ delete _;

        protected List<String> mConfigNames = new .() ~ DeleteContainerAndItems!(_);
        protected List<String> mPlatformNames = new .() ~ DeleteContainerAndItems!(_);

        protected DarkComboBox mConfigComboBox;
        protected DarkComboBox mPlatformComboBox;

		public struct ConfigPlatformPair : IHashable
		{
			public String mConfig;
			public String mPlatform;
			
			public this(String config, String platform)
			{
				mConfig = config;
				mPlatform = platform;
			}

			public int GetHashCode()
			{
				return mConfig.GetHashCode() ^ mPlatform.GetHashCode();
			}
		}

		protected class ConfigDataGroup
		{
			public bool mIsMultiTargeted;
		    public ConfigPlatformPair mTarget;
		    public PropPage[] mPropPages ~ delete _;

			public this(int categoryCount)
			{
				mPropPages = new PropPage[categoryCount];
			}

			public ~this()
			{
				for (var propPage in mPropPages)
					delete propPage;
			}
		}

		protected Dictionary<ConfigPlatformPair, ConfigDataGroup> mTargetedConfigDatas = new .() ~ delete _;
		protected ConfigDataGroup mMultiTargetConfigData;
		protected List<ConfigDataGroup> mConfigDatas = new List<ConfigDataGroup>() ~ DeleteContainerAndItems!(_);

        public this()
        {
			mActiveConfigName.Set(gApp.mConfigName);
            mConfigNames.Add(new String(mActiveConfigName));

			mActivePlatformName.Set(gApp.mPlatformName);
            mPlatformNames.Add(new String(mActivePlatformName));
			
            mConfigComboBox = new DarkComboBox();
            //mConfigComboBox.Label = String.Format("Active({0})", IDEApp.sApp.mConfigName);
            mConfigComboBox.mPopulateMenuAction.Add(new => PopulateConfigMenu);
            AddWidget(mConfigComboBox);

            mPlatformComboBox = new DarkComboBox();
            //mPlatformComboBox.Label = String.Format("Active({0})", IDEApp.sApp.mPlatformName);
            mPlatformComboBox.mPopulateMenuAction.Add(new => PopulatePlatformMenu);
            AddWidget(mPlatformComboBox);
        }

		public ~this()
		{
			ClearTargetedData();
		}

		public void ClearTargetedData()
		{
			for (var kv in mTargetedConfigDatas)
			{
				delete kv.key.mConfig;
				delete kv.key.mPlatform;
				delete kv.value;
				mConfigDatas.Remove(kv.value);
			}
			mTargetedConfigDatas.Clear();
		}

		public bool IsMultiTargeted()
		{
			return (mConfigNames.Count != 1) || (mPlatformNames.Count != 1);
		}

        protected virtual TargetedKind GetCategoryTargetedKind(int32 categoryTypeInt)
        {
            return .None;
        }

		public virtual void GetConfigList(List<String> configNames)
		{

		}

		public virtual void GetPlatformList(List<String> platformNames)
		{

		}

		protected virtual void CreateNewConfig()
		{
			DarkDialog dialog = new NewConfigDialog(this);
			dialog.PopupWindow(mWidgetWindow);
		}

		protected void ConfigDeleted(String configName)
		{
			bool currentChanged = false;
			int idx = mConfigNames.IndexOf(configName);
			if (idx != -1)
			{
				delete mConfigNames[idx];
				mConfigNames.RemoveAt(idx);

				if (mConfigNames.Count == 0)
				{
					List<String> allConfigs = scope List<String>();
					GetConfigList(allConfigs);
					allConfigs.Sort(scope (lhs, rhs) => lhs <=> rhs);
					mConfigNames.Add(new String(allConfigs[0]));
				}
				currentChanged = true;
			}

			if (currentChanged)
				SelectConfig(mConfigNames);
		}

		protected void ConfigRenamed(String from, String to)
		{
			int idx = mConfigNames.IndexOf(from);
			if (idx != -1)
			{
				mConfigNames[idx].Set(to);
				SelectConfig(mConfigNames);
			}
			MarkDirty();
		}

		protected void PlatformDeleted(String platformName)
		{
			bool currentChanged = false;
			int idx = mPlatformNames.IndexOf(platformName);
			if (idx != -1)
			{
				delete mPlatformNames[idx];
				mPlatformNames.RemoveAt(idx);

				if (mPlatformNames.Count == 0)
				{
					List<String> allPlatforms = scope List<String>();
					GetPlatformList(allPlatforms);
					allPlatforms.Sort(scope (lhs, rhs) => lhs <=> rhs);
					mPlatformNames.Add(new String(allPlatforms[0]));
				}
				currentChanged = true;
			}

			if (currentChanged)
				SelectPlatform(mPlatformNames);
			gApp.mWorkspace.MarkPlatformNamesDirty();
		}

		protected void PlatformRenamed(String from, String to)
		{
			int idx = mPlatformNames.IndexOf(from);
			if (idx != -1)
			{
				mPlatformNames[idx].Set(to);
				SelectPlatform(mPlatformNames);
			}
			MarkDirty();
			gApp.mWorkspace.MarkPlatformNamesDirty();
		}

		public virtual void EditConfigs()
		{

		}

		public virtual void EditPlatforms()
		{

		}

        public virtual void PopulateConfigMenu(Menu menu)
        {
           	Menu item;

			List<String> sortedConfigNames = scope List<String>();
			GetConfigList(sortedConfigNames);
			sortedConfigNames.Sort(scope (a, b) => String.Compare(a, b, true));

			if (!mActiveConfigName.IsEmpty)
			{
				String dispStr = StackStringFormat!("Active({0})", mActiveConfigName);
				item = menu.AddItem(dispStr);
				item.mOnMenuItemSelected.Add(new (evt) => { SelectConfig(mActiveConfigName); });
			}

			for (var configName in sortedConfigNames)
			{
			    item = menu.AddItem(configName);
			    item.mOnMenuItemSelected.Add(new (evt) => { SelectConfig(configName); });
			}

			item = menu.AddItem("<All>");
			item.mOnMenuItemSelected.Add(new (evt) => { SelectAllConfigs(); });
			item = menu.AddItem("<Multiple...>");
			item.mOnMenuItemSelected.Add(new (evt) => { SelectMultipleConfigs(); });
			item = menu.AddItem("<New...>");
			item.mOnMenuItemSelected.Add(new (evt) => { CreateNewConfig(); });
			item = menu.AddItem("<Edit...>");
			item.mOnMenuItemSelected.Add(new (evt) => { EditConfigs(); });
        }

		protected void SelectAllConfigs()
		{
			ClearAndDeleteItems(mConfigNames);
			mConfigComboBox.Label = "<All>";
			List<String> configNames = scope List<String>();
			GetConfigList(configNames);
			for (var configName in configNames)
				mConfigNames.Add(new String(configName));
			ShowPropPage(mPropPage.mCategoryType);
		}

		protected void SelectMultipleConfigs()
		{
			MultiSelectDialog dialog = new MultiSelectDialog();
			List<String> configNames = scope List<String>();
			GetConfigList(configNames);
			for (var configName in configNames)
				dialog.Add(configName, mConfigNames.Contains(configName));
			dialog.FinishInit();
			dialog.AddOkCancelButtons(new (dlg) =>
				{
					ClearAndDeleteItems(mConfigNames);
					for (var entry in dialog.mEntries)
					{
						if (entry.mCheckbox.Checked)
							mConfigNames.Add(new String(entry.mName));
					}
					if (mConfigNames.IsEmpty)
						return;
					SelectConfig(mConfigNames);
				}, null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

		public virtual void PopulatePlatformMenu(Menu menu)
		{
			Menu item;
			var sortedPlatformNames = scope List<String>();
			GetPlatformList(sortedPlatformNames);
			for (var platformName in sortedPlatformNames)
			{
				if (!platformName.IsEmpty)
				{
				    String dispStr = (IDEApp.sApp.mPlatformName == platformName) ? StackStringFormat!("Active({0})", platformName) : platformName;
				    item = menu.AddItem(dispStr);
				    item.mOnMenuItemSelected.Add(new (evt) => { SelectPlatform(platformName); });
				}
			}

			item = menu.AddItem("<All>");
			item.mOnMenuItemSelected.Add(new (evt) => { SelectAllPlatforms(); });
			item = menu.AddItem("<Multiple...>");
			item.mOnMenuItemSelected.Add(new (evt) => { SelectMultiplePlatforms(); });
			item = menu.AddItem("<New...>");
			item.mOnMenuItemSelected.Add(new (evt) => { CreateNewPlatform(); });
			item = menu.AddItem("<Edit...>");
			item.mOnMenuItemSelected.Add(new (evt) => { EditPlatforms(); });
		}

		protected virtual void CreateNewPlatform(String name)
		{
		}

		protected virtual void CreateNewPlatform()
		{
			DarkDialog dialog = new DarkDialog("New Platform", "Platform Name");
			dialog.AddEdit("");
			dialog.AddOkCancelButtons(
			    new (evt) => { var text = scope String(); dialog.mDialogEditWidget.GetText(text); CreateNewPlatform(text); },
			    null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

		protected void SelectAllPlatforms()
		{
			ClearAndDeleteItems(mPlatformNames);
			mPlatformComboBox.Label = "<All>";
			List<String> platformNames = scope .();
			GetPlatformList(platformNames);
			for (var platformName in platformNames)
			{
				mPlatformNames.Add(new String(platformName));
			}
			ShowPropPage(mPropPage.mCategoryType);
		}

		protected void SelectMultiplePlatforms()
		{
			MultiSelectDialog dialog = new MultiSelectDialog();
			List<String> platformNames = scope .();
			GetPlatformList(platformNames);
			for (var platformName in platformNames)
				dialog.Add(platformName, mPlatformNames.Contains(platformName));
			dialog.FinishInit();
			dialog.AddOkCancelButtons(new (dlg) =>
				{
					ClearAndDeleteItems(mPlatformNames);
					for (var entry in dialog.mEntries)
					{
						if (entry.mCheckbox.Checked)
							mPlatformNames.Add(new String(entry.mName));
					}
					if (mPlatformNames.IsEmpty)
						return;
					SelectPlatform(mPlatformNames);
				}, null, 0, 1);
			dialog.PopupWindow(mWidgetWindow);
		}

		public virtual bool CreateNewConfig(String name, String copiedFromConfig)
		{
			return false;
		}

        protected override void ShowPropPage(int32 categoryTypeInt)
        {
            base.ShowPropPage(categoryTypeInt);

            if (GetCategoryTargetedKind(categoryTypeInt) == .Config)
            {
				if (mConfigNames.Count == 1)
				{
	                String dispStr = ((mConfigNames.Count == 1) && (mActiveConfigName == mConfigNames[0])) ? StackStringFormat!("Active({0})", mConfigNames[0]) : mConfigNames[0];
	                mConfigComboBox.Label = dispStr;
				}
				else
				{
					List<String> configNames = scope .();
					GetConfigList(configNames);
					if (mConfigNames.Count == configNames.Count)
						mConfigComboBox.Label = "<All>";
					else
						mConfigComboBox.Label = "<Multiple>";
				}
                mConfigComboBox.mDisabled = false;
			}
			else
			{
				mConfigComboBox.Label = "N/A";
				mConfigComboBox.mDisabled = true;
			}

			if (GetCategoryTargetedKind(categoryTypeInt) != .None)
			{
				if (mPlatformNames.Count == 1)
				{
	                String dispStr = ((mPlatformNames.Count == 1) && (mActivePlatformName == mPlatformNames[0])) ? StackStringFormat!("Active({0})", mPlatformNames[0]) : mPlatformNames[0];
	                mPlatformComboBox.Label = dispStr;
				}
				else
				{
					List<String> platformNames = scope .();
					GetPlatformList(platformNames);
					if (mPlatformNames.Count == platformNames.Count)
						mPlatformComboBox.Label = "<All>";
					else
						mPlatformComboBox.Label = "<Multiple>";
				}
                mPlatformComboBox.mDisabled = false;
            }
            else
            {
                mPlatformComboBox.Label = "N/A";
                mPlatformComboBox.mDisabled = true;
            }
        }

		public void HadExternalChanges(String configName, String platformName)
		{
			int32 categoryType = mPropPage.mCategoryType;
			
			if (configName != null)
			{
				if ((mConfigNames.Count > 1) || (mPlatformNames.Count > 1))
				{
					if ((mConfigNames.Contains(configName)) && (mPlatformNames.Contains(platformName)))
					{
						if (mPropPage == mMultiTargetConfigData.mPropPages[categoryType])
						{
							RemovePropPage();
						}
					}
				}

				if (mTargetedConfigDatas.GetAndRemove(.(configName, platformName)) case .Ok((var key, var configDataGroup)))
				{
					delete key.mConfig;
					delete key.mPlatform;
					if (configDataGroup.mPropPages[categoryType] == mPropPage)
						RemovePropPage();
					mConfigDatas.Remove(configDataGroup);
					delete configDataGroup;
				}
			}
			else
			{
				if (GetCategoryTargetedKind(categoryType) == .None)
					RemovePropPage();
			}

			if (mPropPage == null)
				ShowPropPage(categoryType);
		}

		protected void SelectConfig(List<String> configNames)
		{
			if (configNames != mConfigNames)
			{
				ClearAndDeleteItems(mConfigNames);
				for (var configName in configNames)
					mConfigNames.Add(configName);
			}
			if (mConfigNames.Count == 1)
				SelectConfig(mConfigNames[0]);
			else
			{
				mConfigComboBox.Label = "<Multiple>";
				ShowPropPage(mPropPage.mCategoryType);
			}
		}

		protected void SelectPlatform(List<String> platformNames)
		{
			if (platformNames != mPlatformNames)
			{
				ClearAndDeleteItems(mPlatformNames);
				for (var platformName in platformNames)
					mPlatformNames.Add(platformName);
			}
			if (mPlatformNames.Count == 1)
				SelectPlatform(mPlatformNames[0]);
			else
			{
				mConfigComboBox.Label = "<Multiple>";
				ShowPropPage(mPropPage.mCategoryType);
			}
		}

        protected void SelectConfig(String configName)
        {
			var newConfigName = new String(configName);
			ClearAndDeleteItems(mConfigNames);
            mConfigNames.Add(newConfigName);
            mConfigComboBox.Label = newConfigName;
            ShowPropPage(mPropPage.mCategoryType);
        }

        protected void SelectPlatform(String platformName)
        {
			var newPlatformName = new String(platformName);
			ClearAndDeleteItems(mPlatformNames);
            mPlatformNames.Add(newPlatformName);
            mPlatformComboBox.Label = newPlatformName;
            ShowPropPage(mPropPage.mCategoryType);
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float topY = GS!(32);

            var font = DarkTheme.sDarkTheme.mSmallFont;
            float elementWidth = (mWidth - GS!(6 + 6 + 4)) / 2;

			float propTopY = topY;
			if ((mSearchEdit != null) && (mResetButton != null))
				propTopY += GS!(26);

            var fontWidth = font.GetWidth(sConfigLabel) + GS!(6);
            mConfigComboBox.ResizeClamped(GS!(6) + fontWidth, GS!(6), elementWidth - fontWidth, GS!(24));

            fontWidth = font.GetWidth(sPlatformLabel) + GS!(6);
            mPlatformComboBox.ResizeClamped(mConfigComboBox.mX + mConfigComboBox.mWidth + GS!(6) + fontWidth, GS!(6), elementWidth - fontWidth, GS!(24));

            mCategorySelector.ResizeClamped(GS!(6), topY, mCategorySelector.mWidth, mHeight - topY - GS!(32));
            float catRight = mCategorySelector.mX + mCategorySelector.mWidth;
			if (!mCategorySelector.mVisible)
				catRight = 0;

            mPropPage.mPropertiesListView.ResizeClamped(catRight + GS!(6), propTopY, mWidth - catRight - GS!(12), mHeight - propTopY - GS!(32));
        }

        public override void Draw(Beefy.gfx.Graphics g)
        {
            base.Draw(g);

            var font = DarkTheme.sDarkTheme.mSmallFont;
            g.SetFont(font);
            using (g.PushColor(mConfigComboBox.mDisabled ? ThemeColors.ConfigDataGroup.ConfigDataGroup001.Color : ThemeColors.ConfigDataGroup.ConfigDataGroup002.Color))
                g.DrawString(sConfigLabel, mConfigComboBox.mX - font.GetWidth(sConfigLabel) - GS!(6), mConfigComboBox.mY + GS!(0));
            using (g.PushColor(mPlatformComboBox.mDisabled ? ThemeColors.ConfigDataGroup.ConfigDataGroup001.Color : ThemeColors.ConfigDataGroup.ConfigDataGroup002.Color))
                g.DrawString(sPlatformLabel, mPlatformComboBox.mX - font.GetWidth(sPlatformLabel) - GS!(6), mPlatformComboBox.mY + GS!(0));
        }

		public override void Update()
		{
		    base.Update();

		    CheckForChanges();
		    bool hasChanges = false;

		    for (var targetedConfigData in mConfigDatas)
		    {
		        var targetedConfigName = targetedConfigData.mTarget;

		        bool isCurrent = (targetedConfigName.mConfig == null) ||
		            ((mConfigNames.Contains(targetedConfigName.mConfig)) && (mPlatformNames.Contains(targetedConfigName.mPlatform)));

		        for (int32 pageIdx = 0; pageIdx < targetedConfigData.mPropPages.Count; pageIdx++)
		        {
		            var propPage = targetedConfigData.mPropPages[pageIdx];
		            if (propPage != null)
		            {
		                var categoryItem = mCategoryListViewItems[pageIdx];
		                if (isCurrent)
		                    categoryItem.mIsBold = propPage.mHasChanges;
		                hasChanges |= propPage.mHasChanges;
		            }
		        }
		    }

		    if (hasChanges)
		        mApplyButton.mDisabled = false;
		    else
		        mApplyButton.mDisabled = true;
		}
    }
}
