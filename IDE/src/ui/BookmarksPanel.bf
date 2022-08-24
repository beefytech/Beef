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
			mBtnCreateBookmarkFolder.mOnMouseClick.Add(new (args) => gApp.mBookmarkManager.CreateFolder());
			AddWidget(mBtnCreateBookmarkFolder);

			mBtnPrevBookmark = new DarkIconButton();
			mBtnPrevBookmark.mOnMouseClick.Add(new (args) => gApp.Cmd_PrevBookmark());
			AddWidget(mBtnPrevBookmark);

			mBtnNextBookmark = new DarkIconButton();
			mBtnNextBookmark.mOnMouseClick.Add(new (args) => gApp.Cmd_NextBookmark());
			AddWidget(mBtnNextBookmark);

			mBtnPrevBookmarkInFolder = new DarkIconButton();
			mBtnPrevBookmarkInFolder.mOnMouseClick.Add(new (args) => gApp.Cmd_PrevBookmarkInFolder());
			AddWidget(mBtnPrevBookmarkInFolder);

			mBtnNextBookmarkInFolder = new DarkIconButton();
			mBtnNextBookmarkInFolder.mOnMouseClick.Add(new (args) => gApp.Cmd_NextBookmarkInFolder());
			AddWidget(mBtnNextBookmarkInFolder);

			mBookmarksListView = new .();
			mBookmarksListView.mOnEditDone.Add(new => HandleEditDone);

			mBookmarksListView.InitScrollbars(true, true);
			mBookmarksListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);

			mBookmarksListView.AddColumn(200, "Bookmark");
			mBookmarksListView.AddColumn(400, "File");
			mBookmarksListView.AddColumn(120, "Line");

            mBookmarksListView.mOnDragEnd.Add(new => BookmarksLV_OnDragEnd);
            mBookmarksListView.mOnDragUpdate.Add(new => BookmarksLV_OnDragUpdate);

			mBookmarksListView.mOnItemMouseDown.Add(new (item, x, y, btnNum, btnCount) =>
				{
					ListViewItemMouseDown(item, x, y, btnNum, btnCount);
					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (BookmarksListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}
				});
			mBookmarksListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mBookmarksListView.mOnKeyDown.Add(new => BookmarksLV_OnKeyDown);

			AddWidget(mBookmarksListView);

			gApp.mBookmarkManager.BookmarksChanged.Add(new => BookmarksChanged);
			gApp.mBookmarkManager.MovedToBookmark.Add(new => MovedToBookmark);
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

			mBookmarksDirty = true;
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

					target.Open(true, true);

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
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, targetBookmark.mFolder, .After, targetBookmark);
								}
								else if (theEvent.mDragKind == .Before)
								{
									gApp.mBookmarkManager.MoveBookmarkToFolder(sourceBookmark, targetBookmark.mFolder, .Before, targetBookmark);
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
				anItem.mOnMenuItemSelected.Add(new (item) => { DeleteSelectedItems(); });

				anItem = menu.AddItem("Rename");
				anItem.mOnMenuItemSelected.Add(new (item) => { TryRenameItem(); });

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

			mBtnCreateBookmarkFolder.Icon = DarkTheme.sDarkTheme.GetImage(.NewBookmarkFolder);
			mBtnCreateBookmarkFolder.Padding = .(GS!(4), GS!(4), GS!(4), GS!(4));
			float iconButtonWidth = DarkTheme.sUnitSize + GS!(6);

			mBtnPrevBookmark.X = GS!(1) + iconButtonWidth;
			mBtnPrevBookmark.Icon = DarkTheme.sDarkTheme.GetImage(.PrevBookmark);
			mBtnPrevBookmark.Padding = mBtnCreateBookmarkFolder.Padding;

			mBtnNextBookmark.X = (GS!(1) + iconButtonWidth) * 2;
			mBtnNextBookmark.Icon = DarkTheme.sDarkTheme.GetImage(.NextBookmark);
			mBtnNextBookmark.Padding = mBtnCreateBookmarkFolder.Padding;

			mBtnPrevBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 3;
			mBtnPrevBookmarkInFolder.Icon = DarkTheme.sDarkTheme.GetImage(.PrevBookmarkInFolder);
			mBtnPrevBookmarkInFolder.Padding = mBtnCreateBookmarkFolder.Padding;

			mBtnNextBookmarkInFolder.X = (GS!(1) + iconButtonWidth) * 4;
			mBtnNextBookmarkInFolder.Icon = DarkTheme.sDarkTheme.GetImage(.NextBookmarkInFolder);
			mBtnNextBookmarkInFolder.Padding = mBtnCreateBookmarkFolder.Padding;

			mBookmarksListView.mLabelX = GS!(6);

			float buttonHeight = mBtnCreateBookmarkFolder.mHeight;
			mBookmarksListView.Resize(0, buttonHeight, width, Math.Max(mHeight - buttonHeight, 0));
		}

		private bool mBookmarksDirty;

		/// Marks the bookmarks list view as dirty so that it will be rebuild in the next update.
		private void BookmarksChanged()
		{
			mBookmarksDirty = true;
		}

		private void MovedToBookmark(Bookmark bookmark)
		{
			var root = (BookmarksListViewItem)mBookmarksListView.GetRoot();
			root.WithItems(scope (item) =>
				{
					var bmItem = (BookmarksListViewItem)item;

					if (bmItem.RefObject == bookmark)
					{
						bmItem.mIsBold = true;

						ListViewItem parent = item.mParentItem;
						parent.Open(true);
					}
					else
					{
						bmItem.mIsBold = false;
					}
				});
		}

		public override void Update()
		{
			if (mBookmarksDirty)
				UpdateBookmarks();

			ShowTooltip(mBtnCreateBookmarkFolder, "Create new folder");
			ShowTooltip(mBtnPrevBookmark, "Previous bookmark");
			ShowTooltip(mBtnNextBookmark, "Next bookmark");
			ShowTooltip(mBtnPrevBookmarkInFolder, "Previous bookmark in folder");
			ShowTooltip(mBtnNextBookmarkInFolder, "Next bookmark in folder");

			base.Update();
		}

		/// Clears the Panel (does NOT clear the actual bookmarks).
		public void Clear()
		{
			mBookmarksListView.GetRoot().Clear();

			mBookmarksDirty = true;
		}

		/// Shows a tooltip with the given text for the specified widget if the widget is hovered.
		private void ShowTooltip(Widget widget, String text)
		{
		    if (DarkTooltipManager.CheckMouseover(widget, 20, let mousePoint))
		    {
                DarkTooltipManager.ShowTooltip(text, widget, mousePoint.x, mousePoint.y);
			}
		}

		/// Rebuilds the list view.
		private void UpdateBookmarks()
		{
			var root = (BookmarksListViewItem)mBookmarksListView.GetRoot();

			var openFolders = scope List<BookmarkFolder>();

			root.WithItems(scope (item) =>
				{
					if (item.IsOpen && (var bookmarkFolder = ((BookmarksListViewItem)item).RefObject as BookmarkFolder))
					{
						openFolders.Add(bookmarkFolder);
					}
				});

			root.Clear();

			Bookmark currentBookmark = gApp.mBookmarkManager.CurrentBookmark;

			for (BookmarkFolder folder in gApp.mBookmarkManager.mBookmarkFolders)
			{
				bool isRoot = (folder == IDEApp.sApp.mBookmarkManager.mRootFolder);

				BookmarksListViewItem FolderItem = null;

				if (!isRoot)
				{
					FolderItem = AddFolderToListView(root, folder);
				}
				else
				{
					FolderItem = (BookmarksListViewItem)root;
				}

				for (Bookmark bookmark in folder.mBookmarkList)
				{
					BookmarksListViewItem bmItem = AddBookmarkToListView(FolderItem, bookmark);
					bmItem.mIsBold = (bookmark == currentBookmark);
				}

				if (!isRoot)
				{
					// Open folder if it was open before recreating the list view.
					int idx = openFolders.IndexOf(folder);
					if (idx >= 0)
					{
						openFolders.RemoveAtFast(idx);
						FolderItem.Open(true, true);
					}
				}
			}

			mBookmarksDirty = false;
		}

		/// Creates a new ListViewItem for the given folder.
		private BookmarksListViewItem AddFolderToListView(BookmarksListViewItem parent, BookmarkFolder folder)
		{
			var listViewItem = (BookmarksListViewItem)parent.CreateChildItem();
			listViewItem.MakeParent();
			listViewItem.RefObject = folder;
			listViewItem.AllowDragging = true;

			var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(0);

			if (!folder.mBookmarkList.IsEmpty)
			{
				DarkCheckBox cb = new DarkCheckBox();
				cb.State = folder.AreAllDisabled ? .Unchecked : folder.AreAllEnabled ? .Checked : .Indeterminate;
				cb.Resize(GS!(-16), 0, GS!(22), GS!(22));
				cb.mOnValueChanged.Add(new () =>
					{
						folder.AreAllEnabled = cb.State != .Unchecked;
					});
				subViewItem.AddWidget(cb);
			}

			subViewItem.Label = folder.mTitle;
			subViewItem.Resize(GS!(22), 0, 0, 0);

			return listViewItem;
		}

		/// Creates a new ListViewItem for the given bookmark.
		private BookmarksListViewItem AddBookmarkToListView(BookmarksListViewItem parent, Bookmark bookmark)
		{
			var listViewItem = (BookmarksListViewItem)(parent.CreateChildItem());
			
			listViewItem.RefObject = bookmark;
			listViewItem.AllowDragging = true;

			var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(0);

			DarkCheckBox cb = new DarkCheckBox();
			cb.Checked = !bookmark.mIsDisabled;
			cb.Resize(GS!(-16), 0, GS!(22), GS!(22));
			cb.mOnValueChanged.Add(new () =>
				{
					bookmark.mIsDisabled = !cb.Checked;
					mBookmarksDirty = true;
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

			return listViewItem;
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
