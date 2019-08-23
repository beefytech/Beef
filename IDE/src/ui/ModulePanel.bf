using Beefy.theme.dark;
using System;
using Beefy.widgets;
using Beefy.theme;
using System.IO;
using Beefy.utils;

namespace IDE.ui
{
	class ModulePanel : Panel
	{
		public IDEListView mListView;
		bool mModulesDirty = true;

		public this()
		{
			mListView = new IDEListView();
			mListView.InitScrollbars(true, true);
			mListView.mHorzScrollbar.mPageSize = GS!(100);
			mListView.mHorzScrollbar.mContentSize = GS!(500);
			mListView.mVertScrollbar.mPageSize = GS!(100);
			mListView.mVertScrollbar.mContentSize = GS!(500);
			mListView.mAutoFocus = true;
			mListView.mOnLostFocus.Add(new (evt) =>
				{
					if (!mShowingRightClickMenu)
						mListView.GetRoot().SelectItemExclusively(null);
				});
			mListView.UpdateScrollbars();

			AddWidget(mListView);

			ListViewColumn column = mListView.AddColumn(GS!(100), "Name");
			column.mMinWidth = GS!(130);
			column = mListView.AddColumn(GS!(200), "Path");
			column = mListView.AddColumn(GS!(200), "Debug Info File");
			column = mListView.AddColumn(GS!(120), "Version");
			column = mListView.AddColumn(GS!(200), "Address");
			column = mListView.AddColumn(GS!(100), "Size");
			column = mListView.AddColumn(GS!(120), "Timestamp");
			mListView.mOnItemMouseDown.Add(new => ListViewItemMouseDown);
			mListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mListView.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);
		    data.Add("Type", "ModulePanel");
		}

		public override void FocusForKeyboard()
		{
			// Update to allow for populating of list if necessary
			Update();
			mListView.SetFocus();
			if ((mListView.GetRoot().FindFocusedItem() == null) && (mListView.GetRoot().GetChildCount() > 0))
			{
				mListView.GetRoot().SelectItemExclusively(mListView.GetRoot().GetChildAtIndex(0));
			}
		}

		public void ModulesChanged()
		{
		    mModulesDirty = true;
		}

		protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
		{
			base.ShowRightClickMenu(relWidget, x, y);

#if !CLI
			var root = relWidget as ListViewItem;
			var listView = root.mListView;
			if (listView.GetRoot().FindFirstSelectedItem() != null)
			{
				Menu menu = new Menu();
			    Menu anItem;
			    anItem = menu.AddItem("Load Symbols...");
			    anItem.mOnMenuItemSelected.Add(new (item) =>
					{
						listView.GetRoot().WithSelectedItems(scope (item) =>
							{
								var pathItem = item.GetSubItem(1);

								String dir = scope String();
								Path.GetDirectoryPath(pathItem.Label, dir);
								IDEUtils.FixFilePath(dir);

								var fileDialog = scope System.IO.OpenFileDialog();
								fileDialog.ShowReadOnly = false;
								fileDialog.Title = "Select Debug Info File";
								fileDialog.Multiselect = false;
								if (!dir.IsEmpty)
									fileDialog.InitialDirectory = dir;
								fileDialog.ValidateNames = true;
								fileDialog.DefaultExt = ".exe";
								fileDialog.SetFilter("PDB Debug Unfo (*.pdb)|*.pdb|All files (*.*)|*.*");
								mWidgetWindow.PreModalChild();
								if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
								{
									var fileNames = fileDialog.FileNames;
									if (gApp.mDebugger.mIsRunning)
									{
										gApp.mDebugger.LoadDebugInfoForModule(scope String(pathItem.Label), fileNames[0]);
									}
								}
							});
					});
				MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
				menuWidget.Init(relWidget, x, y);
			}
#endif
		}

		void UpdateModules()
		{
			int idx = 0;
			if (gApp.mDebugger.mIsRunning)
			{
				var modulesInfoStr = scope String();
				gApp.mDebugger.GetModulesInfo(modulesInfoStr);

				for (let moduleInfoStr in modulesInfoStr.Split('\n'))
				{
					if (moduleInfoStr.IsEmpty)
						continue;

					if (idx < mListView.GetRoot().GetChildCount())
					{
						let lvItem = mListView.GetRoot().GetChildAtIndex(idx);
						int subIdx = 0;
						for (let moduleStr in moduleInfoStr.Split('\t'))
						{
							let subLVItem = (subIdx == 0) ? lvItem : lvItem.GetSubItem(subIdx);
							subLVItem.Label = moduleStr;  
							subIdx++;
						}
					}
					else
					{
						let lvItem = mListView.GetRoot().CreateChildItem();
						int subIdx = 0;
						for (let moduleStr in moduleInfoStr.Split('\t'))
						{
							let subLVItem = (subIdx == 0) ? lvItem : lvItem.CreateSubItem(subIdx);
							subLVItem.Label = moduleStr;
							subIdx++;
						}
					}

					++idx;
				}
			}

			while (idx < mListView.GetRoot().GetChildCount())
				mListView.GetRoot().RemoveChildItemAt(idx);
		}

		public override void Update()
		{
			base.Update();

			if (mModulesDirty)
			{
				UpdateModules();
				mModulesDirty = false;
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
		    base.Resize(x, y, width, height);
		    mListView.Resize(0, 0, width, height);
			mListView.ScrollPositionChanged();
		}
	}
}
