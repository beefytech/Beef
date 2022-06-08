using Beefy.theme.dark;
using Beefy.utils;
using Beefy.widgets;
using System;
using System.Collections;
using Beefy.theme;
using Beefy.events;

namespace IDE.ui
{
	class BookmarksPanel : Panel
	{
		public class BookmarksListView : IDEListView
		{
			protected override ListViewItem CreateListViewItem()
			{
				return new BookmarksListViewItem();
			}
		}

		public class BookmarksListViewItem : IDEListViewItem
		{
			public Bookmark Bookmark;
			public String BookmarkLine ~ delete _;

			public void Goto()
			{
				gApp.mBookmarkManager.GotoBookmark(Bookmark);
			}
		}

		public BookmarksListView mBookmarksLV;

		public this()
		{
			mBookmarksLV = new .();
			mBookmarksLV.mOnEditDone.Add(new => HandleEditDone);

			mBookmarksLV.InitScrollbars(true, true);
			mBookmarksLV.mLabelX = GS!(6);
			mBookmarksLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);

			mBookmarksLV.AddColumn(200, "Bookmark");
			mBookmarksLV.AddColumn(400, "File");
			mBookmarksLV.AddColumn(120, "Line");

			mBookmarksLV.mOnItemMouseDown.Add(new (item, x, y, btnNum, btnCount) =>
				{
					if ((btnNum == 0) && (btnCount == 2))
					{
						let mainItem = (BookmarksListViewItem)item.GetSubItem(0);
						mainItem.Goto();
					}

					ListViewItemMouseDown(item, x, y, btnNum, btnCount);
				});
			mBookmarksLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mBookmarksLV.mOnKeyDown.Add(new => BookmarksLV_OnKeyDown);

			AddWidget(mBookmarksLV);

		}

		private void BookmarksLV_OnKeyDown(KeyDownEvent event)
		{
            if (event.mKeyCode == KeyCode.Delete)
			{
				DeleteSelectedItem();
			}

			ListViewKeyDown_ShowMenu(event);
		}

		/// Tries to rename the currently selected bookmark
		public void TryRenameItem()
		{
			ListViewItem selectedItem = mBookmarksLV.GetRoot().FindFirstSelectedItem();
			RenameItem(selectedItem);
		}

		public void DeleteSelectedItem()
		{
			ListViewItem selectedItem = mBookmarksLV.GetRoot().FindFirstSelectedItem();
			if (var bookmarkItem = selectedItem as BookmarksListViewItem)
			{
				gApp.mBookmarkManager.DeleteBookmark(bookmarkItem.Bookmark);
			}
		}

        private void HandleEditDone(EditWidget editWidget, bool cancelled)
        {
            String newValue = scope String();
            editWidget.GetText(newValue);
            newValue.Trim();
			
			ListViewItem listViewItem = mBookmarksLV.mEditingItem;

			if (var item = listViewItem as BookmarksListViewItem)
			{
				item.Bookmark.mTitle.Clear();
				item.Bookmark.mTitle.Append(newValue);
				listViewItem.Label = item.Bookmark.mTitle;
			}

		}

		public ~this()
		{

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
									gApp.mBookmarkManager.DeleteBookmark(bookmarkItem.Bookmark);
								}
							});
						
					});

			    anItem = menu.AddItem("Rename");
				anItem.mOnMenuItemSelected.Add(new (item) =>
				    {
						var selectedItem = mBookmarksLV.GetRoot().FindFirstSelectedItem();
						if (selectedItem != null)
				        	RenameItem(selectedItem);
				    });

				menu.AddItem();

				if (gApp.mBookmarkManager.AllBookmarksDisabled)
				{
					anItem = menu.AddItem("Enable all Bookmarks");
					anItem.mOnMenuItemSelected.Add(new (item) =>
					    {
							for (Bookmark b in gApp.mBookmarkManager.mBookmarkList)
							{
								b.mIsDisabled = false;
							}
							mBookmarksDirty = true;
					    });
				}
				else
				{
					anItem = menu.AddItem("Disable all Bookmarks");
					anItem.mOnMenuItemSelected.Add(new (item) =>
					    {
							for (Bookmark b in gApp.mBookmarkManager.mBookmarkList)
							{
								b.mIsDisabled = true;
							}
							mBookmarksDirty = true;
					    });
				}

				MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
				menuWidget.Init(relWidget, x, y);
			}
		}
		
		void EditListViewItem(ListViewItem listViewItem)
		{
			mBookmarksLV.EditListViewItem(listViewItem);
		}

        void RenameItem(ListViewItem listViewItem)
        {            
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
			mBookmarksLV.Resize(0, 0, width, height);
		}

		public void Clear()
		{

		}

		public bool mBookmarksDirty;


		public override void Update()
		{
			if (mBookmarksDirty)
				UpdateBookmarks();

			base.Update();
		}

		private void UpdateBookmarks()
		{
			var root = mBookmarksLV.GetRoot();

			root.Clear();

			for (Bookmark bookmark in gApp.mBookmarkManager.mBookmarkList)
			{
				BookmarksListViewItem listViewItem = (.)root.CreateChildItem();
            	SetupListViewItem(listViewItem, bookmark);
			}

			mBookmarksDirty = false;
		}

		private void SetupListViewItem(BookmarksListViewItem listViewItem, Bookmark bookmark)
		{
			listViewItem.Bookmark = bookmark;

			var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(0);

			DarkCheckBox cb = new DarkCheckBox();
			cb.Checked = !bookmark.mIsDisabled;
			cb.Resize(GS!(-16), 0, GS!(22), GS!(22));
			cb.mOnValueChanged.Add(new () => {
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
			mBookmarksLV.KeyDown(keyCode, isRepeat);
			
			base.KeyDown(keyCode, isRepeat);
		}

		private void DeleteSelectedItems()
		{
			var root = mBookmarksLV.GetRoot();
			List<ListViewItem> selectedItems = scope List<ListViewItem>();                    
			root.WithSelectedItems(scope (listViewItem) =>
			    {
			        selectedItems.Add(listViewItem);
			    });

			// Go through in reverse, to process children before their parents
			for (int itemIdx = selectedItems.Count - 1; itemIdx >= 0; itemIdx--)
			{
				BookmarksListViewItem item = (.)selectedItems[itemIdx];

				if (item.Bookmark != null)
					gApp.mBookmarkManager.DeleteBookmark(item.Bookmark);
			}
		}
	}
}