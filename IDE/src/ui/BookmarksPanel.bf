using Beefy.theme.dark;
using Beefy.utils;
using Beefy.widgets;
using System;
using System.Collections;
using Beefy.theme;
using Beefy.events;
using System.Diagnostics;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.geom;

namespace IDE.ui
{
	public class BookmarksListView : IDEListView
	{
		protected override ListViewItem CreateListViewItem()
		{
			return new BookmarksListViewItem();
		}
		protected override void SetScaleData()
		{
			base.SetScaleData();
			mIconX = GS!(200);
			mOpenButtonX = GS!(0);
			mLabelX = GS!(0);
			//mChildIndent = GS!(16);
			mHiliteOffset = GS!(-2);
		}
	}

	public class BookmarksListViewItem : IDEListViewItem
	{
		public Object RefObject;
		public String BookmarkLine ~ delete _;
		
		public float mLabelOffsetFolder = GS!(16);
		public float mLabelOffsetBookmark = GS!(0);

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);

			mLabelOffsetFolder = GS!(16);
			mLabelOffsetBookmark = GS!(0);
		}

        protected override float GetLabelOffset()
        {
			if (RefObject is BookmarkFolder)
			{
				return mLabelOffsetFolder;
			}

            return mLabelOffsetBookmark;
        }

		public void Goto()
		{
			if (Bookmark bookmark = RefObject as Bookmark)
			{
				gApp.mBookmarkManager.GotoBookmark(bookmark);
			}
		}
	}

	class BookmarksPanel : Panel
	{
		public DarkIconButton mBtnCreateBookmarkFolder;
		public DarkIconButton mBtnPrevBookmark;
		public DarkIconButton mBtnNextBookmark;
		public DarkIconButton mBtnPrevBookmarkInFolder;
		public DarkIconButton mBtnNextBookmarkInFolder;

		public BookmarksListView mBookmarksListView;

		public this()
		{
			mBtnCreateBookmarkFolder = new DarkIconButton();
			mBtnCreateBookmarkFolder.Icon = DarkTheme.sDarkTheme.GetImage(.NewBookmarkFolder);
			mBtnCreateBookmarkFolder.mOnMouseClick.Add(new (args) => gApp.mBookmarkManager.CreateFolder());
			AddWidget(mBtnCreateBookmarkFolder);

			float iconButtonWidth = mBtnCreateBookmarkFolder.Width;

			mBtnPrevBookmark = new DarkIconButton();
			mBtnPrevBookmark.Icon = DarkTheme.sDarkTheme.GetImage(.PrevBookmark);
			mBtnPrevBookmark.mOnMouseClick.Add(new (args) => gApp.Cmd_PrevBookmark());
			mBtnPrevBookmark.X = GS!(1) + iconButtonWidth;
			AddWidget(mBtnPrevBookmark);

			mBtnNextBookmark = new DarkIconButton();
			mBtnNextBookmark.Icon = DarkTheme.sDarkTheme.GetImage(.NextBookmark);
			mBtnNextBookmark.mOnMouseClick.Add(new (args) => gApp.Cmd_NextBookmark());
			mBtnNextBookmark.X = (GS!(1) + iconButtonWidth) * 2;
			AddWidget(mBtnNextBookmark);

			mBtnPrevBookmarkInFolder = new DarkIconButton();
			mBtnPrevBookmarkInFolder.Icon = DarkTheme.sDarkTheme.GetImage(.PrevBookmarkInFolder);
			mBtnPrevBookmarkInFolder.mOnMouseClick.Add(new (args) => gApp.Cmd_PrevBookmarkInFolder());
			mBtnPrevBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 3;
			AddWidget(mBtnPrevBookmarkInFolder);

			mBtnNextBookmarkInFolder = new DarkIconButton();
			mBtnNextBookmarkInFolder.Icon = DarkTheme.sDarkTheme.GetImage(.NextBookmarkInFolder);
			mBtnNextBookmarkInFolder.mOnMouseClick.Add(new (args) => gApp.Cmd_NextBookmarkInFolder());
			mBtnNextBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 4;
			AddWidget(mBtnNextBookmarkInFolder);


			mBookmarksListView = new .();
			mBookmarksListView.mOnEditDone.Add(new => HandleEditDone);

			mBookmarksListView.InitScrollbars(true, true);
			mBookmarksListView.mLabelX = GS!(6);
			mBookmarksListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);

			mBookmarksListView.AddColumn(200, "Bookmark");
			mBookmarksListView.AddColumn(400, "File");
			mBookmarksListView.AddColumn(120, "Line");

            mBookmarksListView.mOnDragEnd.Add(new => BookmarksLV_OnDragEnd);
            mBookmarksListView.mOnDragUpdate.Add(new => BookmarksLV_OnDragUpdate);

			mBookmarksListView.mOnItemMouseDown.Add(new (item, x, y, btnNum, btnCount) =>
				{
					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (BookmarksListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}

					ListViewItemMouseDown(item, x, y, btnNum, btnCount);
				});
			mBookmarksListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mBookmarksListView.mOnKeyDown.Add(new => BookmarksLV_OnKeyDown);

			AddWidget(mBookmarksListView);
		}

		private void BookmarksLV_OnKeyDown(KeyDownEvent event)
		{
			if (event.mKeyCode == KeyCode.Delete)
			{
				DeleteSelectedItems();
			}

			ListViewKeyDown_ShowMenu(event);
		}
		
		public override void RehupScale(float oldScale, float newScale)
		{
			mBookmarksListView.mOpenButtonX = GS!(4);

			float iconButtonWidth = mBtnCreateBookmarkFolder.Width;
			
			mBtnPrevBookmark.X = GS!(1) + iconButtonWidth;
			mBtnNextBookmark.X = (GS!(1) + iconButtonWidth) * 2;
			mBtnPrevBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 3;
			mBtnNextBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 4;

			base.RehupScale(oldScale, newScale);
		}
		
		private void BookmarksLV_OnDragUpdate(DragEvent evt)
		{
			var dragKind = evt.mDragKind;
			evt.mDragKind = .None;
			
			var dragSource = evt.mSender as BookmarksListViewItem;
			var dragTarget = evt.mDragTarget as BookmarksListViewItem;

			// Folders can only be dragged onto other folders
			if (dragSource.RefObject is BookmarkFolder && (!dragTarget.RefObject is BookmarkFolder))
				return;

			if (dragSource == null)
			    return;
			if (dragTarget == null)  
			    return;

			evt.mDragKind = .After;

			if ((dragTarget.mLabel == "") && (dragKind == .After))
			    dragKind = .Before;

			if (dragKind == .None)
			    return;
			evt.mDragKind = dragKind;
		}

		private void BookmarksLV_OnDragEnd(DragEvent theEvent)
		{
			if (theEvent.mDragKind == .None)
			    return;

			if (theEvent.mDragTarget is BookmarksListViewItem)
			{
			    var source = (BookmarksListViewItem)theEvent.mSender;
			    var target = (BookmarksListViewItem)theEvent.mDragTarget;

			    if (source.mListView == target.mListView)
			    {                    
			        if (source == target)
			            return;

					List<BookmarksListViewItem> selectedItems = scope .();
					mBookmarksListView.GetRoot().WithSelectedItems(scope [&] (selectedItem) =>
					    {
					        selectedItems.Add((BookmarksListViewItem)selectedItem);
					    });
					
					for (BookmarksListViewItem item in selectedItems)
					{
						if (var sourceBookmark = item.RefObject as Bookmark)
						{
							if (var targetBookmark = target.RefObject as Bookmark)
							{
								if (theEvent.mDragKind == .After)
								{
									int index = targetBookmark.mFolder.mBookmarkList.IndexOf(targetBookmark);
									index++;

									Bookmark prevBookmark = null;

									if (index < targetBookmark.mFolder.mBookmarkList.Count)
									{
										prevBookmark = targetBookmark.mFolder.mBookmarkList[index];
									}
									
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, targetBookmark.mFolder, prevBookmark);
								}
								else if (theEvent.mDragKind == .Before)
								{
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, targetBookmark.mFolder, targetBookmark);
								}
							}
							else if (var targetFolder = target.RefObject as BookmarkFolder)
							{
								if (theEvent.mDragKind == .Before)
								{
									// Drop before folder -> Drop to root
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, gApp.mBookmarkManager.mRootFolder);
								}
								else if (theEvent.mDragKind == .After || theEvent.mDragKind == .Inside)
								{
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, targetFolder);
								}
							}
						}
						else if (var sourceFolder = item.RefObject as BookmarkFolder)
						{
							if (var targetFolder = target.RefObject as BookmarkFolder)
							{
								if (theEvent.mDragKind == .Before)
								{
									gApp.mBookmarkManager.MoveFolder(sourceFolder, .Before, targetFolder);
								}
								else if (theEvent.mDragKind == .After)
								{
									gApp.mBookmarkManager.MoveFolder(sourceFolder, .After, targetFolder);
								}
							}
						}
					}
			    }
			}
		}

		/// Tries to rename the currently selected bookmark
		public void TryRenameItem()
		{
			ListViewItem selectedItem = mBookmarksListView.GetRoot().FindFirstSelectedItem();

			RenameItem(selectedItem);
		}

		private void HandleEditDone(EditWidget editWidget, bool cancelled)
		{
			String newValue = scope String();
			editWidget.GetText(newValue);
			newValue.Trim();

			ListViewItem listViewItem = mBookmarksListView.mEditingItem;

			if (var item = listViewItem as BookmarksListViewItem)
			{
				if (var bookmark = item.RefObject as Bookmark)
				{
					bookmark.mTitle.Clear();
					bookmark.mTitle.Append(newValue);
					listViewItem.Label = bookmark.mTitle;
				}
				else if (var folder = item.RefObject as BookmarkFolder)
				{
					folder.mTitle.Clear();
					folder.mTitle.Append(newValue);
					listViewItem.Label = folder.mTitle;
				}
			}
		}

		protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
		{
			base.ShowRightClickMenu(relWidget, x, y);

			var root = relWidget as ListViewItem;
			var listView = root.mListView;
			if (listView.GetRoot().FindFirstSelectedItem() != null)
			{
				Menu menu = new Menu();
				Menu anItem;
				anItem = menu.AddItem("Delete");
				anItem.mOnMenuItemSelected.Add(new (item) =>
					{
						listView.GetRoot().WithSelectedItems(scope (item) =>
							{
								if (var bookmarkItem = item as BookmarksListViewItem)
								{
									if (var bookmark = bookmarkItem.RefObject as Bookmark)
										gApp.mBookmarkManager.DeleteBookmark(bookmark);
									else if (var folder = bookmarkItem.RefObject as BookmarkFolder)
										gApp.mBookmarkManager.DeleteFolder(folder);
								}
							});
					});

				anItem = menu.AddItem("Rename");
				anItem.mOnMenuItemSelected.Add(new (item) =>
					{
						var selectedItem = mBookmarksListView.GetRoot().FindFirstSelectedItem();
						if (selectedItem != null)
							RenameItem(selectedItem);
					});

				menu.AddItem();

				if (gApp.mBookmarkManager.AllBookmarksDisabled)
				{
					anItem = menu.AddItem("Enable all Bookmarks");
					anItem.mOnMenuItemSelected.Add(new (item) =>
						{
							gApp.mBookmarkManager.AllBookmarksDisabled = false;
						});
				}
				else
				{
					anItem = menu.AddItem("Disable all Bookmarks");
					anItem.mOnMenuItemSelected.Add(new (item) =>
						{
							gApp.mBookmarkManager.AllBookmarksDisabled = true;
						});
				}

				MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
				menuWidget.Init(relWidget, x, y);
			}
		}

		void EditListViewItem(ListViewItem listViewItem)
		{
			mBookmarksListView.EditListViewItem(listViewItem);
		}

		void RenameItem(ListViewItem listViewItem)
		{
			if (listViewItem != null)
				EditListViewItem(listViewItem);
		}

		public override void Serialize(StructuredData data)
		{
			base.Serialize(data);

			data.Add("Type", "BookmarksPanel");
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);

			float buttonHeight = mBtnCreateBookmarkFolder.mHeight;

			mBookmarksListView.Resize(0, buttonHeight, width, Math.Max(mHeight - buttonHeight, 0));
		}

		public bool mBookmarksDirty;

		public override void Update()
		{
			if (mBookmarksDirty)
				UpdateBookmarks();

			ShowTooltip(mBtnCreateBookmarkFolder, "Create a new folder.");
			ShowTooltip(mBtnPrevBookmark, "Move the cursor to previous bookmark.");
			ShowTooltip(mBtnNextBookmark, "Move the cursor to next bookmark.");
			ShowTooltip(mBtnPrevBookmarkInFolder, "Move the cursor to previous bookmark in the current folder.");
			ShowTooltip(mBtnNextBookmarkInFolder, "Move the cursor to next bookmark in the current folder.");

			base.Update();
		}

		public void Clear()
		{
			var root = mBookmarksListView.GetRoot();

			root.Clear();

			mBookmarksDirty = true;
		}

		private void ShowTooltip(Widget widget, String text)
		{
		    Point mousePoint;
		    if (DarkTooltipManager.CheckMouseover(widget, 20, out mousePoint))
		    {
                DarkTooltipManager.ShowTooltip(text, widget, mousePoint.x, mousePoint.y);
			}
		}

		private void UpdateBookmarks()
		{
			var root = mBookmarksListView.GetRoot();

			root.Clear();

			for (BookmarkFolder folder in gApp.mBookmarkManager.mBookmarkFolders)
			{
				bool isRoot = (folder == IDEApp.sApp.mBookmarkManager.mRootFolder);

				BookmarksListViewItem FolderItem = null;

				if (!isRoot)
				{
					FolderItem = (BookmarksListViewItem)root.CreateChildItem();

					SetupListViewItemFolder(FolderItem, folder);
				}
				else
				{
					FolderItem = (BookmarksListViewItem)root;
				}

				for (Bookmark bookmark in folder.mBookmarkList)
				{
					var listViewItem = (BookmarksListViewItem)(FolderItem.CreateChildItem());
					SetupListViewItem(listViewItem, bookmark);
				}
			}

			mBookmarksDirty = false;
		}

		private void SetupListViewItemFolder(BookmarksListViewItem listViewItem, BookmarkFolder folder)
		{
			listViewItem.AllowDragging = true;

			listViewItem.RefObject = folder;

			var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(0);

			DarkCheckBox cb = new DarkCheckBox();
			cb.Checked = !folder.IsDisabled;
			cb.Resize(GS!(-16), 0, GS!(22), GS!(22));
			cb.mOnValueChanged.Add(new () =>
				{
					folder.IsDisabled = !cb.Checked;
				});
			subViewItem.AddWidget(cb);

			subViewItem.Label = folder.mTitle;
			subViewItem.Resize(GS!(22), 0, 0, 0);
		}

		private void SetupListViewItem(BookmarksListViewItem listViewItem, Bookmark bookmark)
		{
			listViewItem.AllowDragging = true;

			listViewItem.RefObject = bookmark;

			var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(0);

			DarkCheckBox cb = new DarkCheckBox();
			cb.Checked = !bookmark.mIsDisabled;
			cb.Resize(GS!(-16), 0, GS!(22), GS!(22));
			cb.mOnValueChanged.Add(new () =>
				{
					bookmark.mIsDisabled = !cb.Checked;
				});
			subViewItem.AddWidget(cb);

			subViewItem.Label = bookmark.mTitle;
			subViewItem.Resize(GS!(22), 0, 0, 0);

			subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(1);
			subViewItem.Label = bookmark.mFileName;

			// Internally lines are 0-based -> add one for display
			listViewItem.BookmarkLine = new $"{bookmark.mLineNum + 1}";

			subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(2);
			subViewItem.Label = listViewItem.BookmarkLine;
		}

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			mBookmarksListView.KeyDown(keyCode, isRepeat);

			base.KeyDown(keyCode, isRepeat);
		}

		private void DeleteSelectedItems()
		{
			var root = mBookmarksListView.GetRoot();
			List<ListViewItem> selectedItems = scope List<ListViewItem>();
			root.WithSelectedItems(scope (listViewItem) =>
				{
					selectedItems.Add(listViewItem);
				});

			// Go through in reverse, to process children before their parents
			for (int itemIdx = selectedItems.Count - 1; itemIdx >= 0; itemIdx--)
			{
				BookmarksListViewItem item = (.)selectedItems[itemIdx];

				if (var bookmark = item.RefObject as Bookmark)
					gApp.mBookmarkManager.DeleteBookmark(bookmark);
				else if (var folder = item.RefObject as BookmarkFolder)
					gApp.mBookmarkManager.DeleteFolder(folder);
			}
		}
	}
}
