using Beefy.theme.dark;
using System;
using Beefy.widgets;
using Beefy.theme;
using System.IO;
using Beefy.utils;
using Beefy.gfx;

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

		public void GetFileNameFrom(ListViewItem item, String filePath)
		{
			var pathItem = item.GetSubItem(1);
			StringView label = pathItem.Label;
			int parenPos = label.IndexOf('(');
			if (parenPos != -1)
				label.RemoveToEnd(parenPos);
			label.Trim();
			filePath.Append(label);
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
				anItem = menu.AddItem("Load Image...");
				anItem.mOnMenuItemSelected.Add(new (item) =>
					{
						listView.GetRoot().WithSelectedItems(scope (item) =>
							{
								String filePath = scope .();
								GetFileNameFrom(item, filePath);

								String dir = scope String();
								Path.GetDirectoryPath(filePath, dir);
								IDEUtils.FixFilePath(dir);

								String fileName = scope String();
								Path.GetFileName(filePath, fileName);

								String extName = scope String();
								Path.GetExtension(filePath, extName);
								extName.ToLower();

								var fileDialog = scope System.IO.OpenFileDialog();
								fileDialog.ShowReadOnly = false;
								fileDialog.Title = "Select Image File";
								fileDialog.Multiselect = false;
								if (!dir.IsEmpty)
									fileDialog.InitialDirectory = dir;
								fileDialog.ValidateNames = true;
								fileDialog.DefaultExt = ".exe";
								fileDialog.FileName = fileName;
								fileDialog.SetFilter(scope String()..AppendF("{0}|{0}|File (*{1})|*{1}|All files (*.*)|*.*", fileName, extName));
								mWidgetWindow.PreModalChild();
								if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
								{
									var fileNames = fileDialog.FileNames;
									gApp.mDebugger.LoadImageForModule(filePath, fileNames[0]);
								}
							});
					});

			    anItem = menu.AddItem("Load Debug Info...");
			    anItem.mOnMenuItemSelected.Add(new (item) =>
					{
						listView.GetRoot().WithSelectedItems(scope (item) =>
							{
								String filePath = scope .();
								GetFileNameFrom(item, filePath);

								String dir = scope String();
								Path.GetDirectoryPath(filePath, dir);
								IDEUtils.FixFilePath(dir);

								var fileDialog = scope System.IO.OpenFileDialog();
								fileDialog.ShowReadOnly = false;
								fileDialog.Title = "Select Debug Info File";
								fileDialog.Multiselect = false;
								if (!dir.IsEmpty)
									fileDialog.InitialDirectory = dir;
								fileDialog.ValidateNames = true;
								fileDialog.DefaultExt = ".exe";
								fileDialog.SetFilter("PDB Debug Info (*.pdb)|*.pdb|All files (*.*)|*.*");
								mWidgetWindow.PreModalChild();
								if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
								{
									var fileNames = fileDialog.FileNames;
									if (gApp.mDebugger.mIsRunning)
									{
										gApp.mDebugger.LoadDebugInfoForModule(filePath, fileNames[0]);
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

					ListViewItem lvItem;

					if (idx < mListView.GetRoot().GetChildCount())
					{
						lvItem = mListView.GetRoot().GetChildAtIndex(idx);
						int subIdx = 0;
						for (let moduleStr in moduleInfoStr.Split('\t'))
						{
							let subLVItem = (DarkListViewItem)((subIdx == 0) ? lvItem : lvItem.GetSubItem(subIdx));
							subLVItem.Label = moduleStr;
							subIdx++;
						}
					}
					else
					{
						lvItem = mListView.GetRoot().CreateChildItem();
						int subIdx = 0;
						for (let moduleStr in moduleInfoStr.Split('\t'))
						{
							let subLVItem = (subIdx == 0) ? lvItem : lvItem.CreateSubItem(subIdx);
							subLVItem.Label = moduleStr;
							subIdx++;
						}
					}

					DarkListViewItem subLVItem = (DarkListViewItem)lvItem.GetSubItem(1);
					if (subLVItem.mLabel.StartsWith("!"))
					{
						subLVItem.mTextColor = ThemeColors.Panel.TypeWildcardEditWidget008.Color;
						subLVItem.mLabel.Remove(0, 1);
					}
					else
					{
						subLVItem.mTextColor = ThemeColors.Panel.WorkspaceProperties002.Color;
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
