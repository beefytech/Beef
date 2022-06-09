using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using IDE.ui;
using System.Diagnostics;

namespace IDE
{
    public class TrackedTextElement
    {
		public int mRefCount = 1;
		public bool mIsDead;

        public String mFileName ~ delete _;
        public int32 mLineNum;
        public int32 mColumn;        
        public bool mSnapToLineStart = true;
        public int32 mMoveIdx; // Increments when we manually move an element (ie: a history location updating because the user types near the previous history location)

        public virtual void Move(int wantLineNum, int wantColumn)
        {
            mLineNum = (int32)wantLineNum;
            mColumn = (int32)wantColumn;
        }

		public virtual void Kill()
		{
			mIsDead = true;
			Deref();
		}

		public void AddRef()
		{
			mRefCount++;
		}

		public void Deref()
		{
			if (--mRefCount == 0)
				delete this;
		}

		public ~this()
		{
			Debug.Assert(mRefCount == 0);
		}
    }

    public class Bookmark : TrackedTextElement
    {
		public String mTitle ~ delete _;
        public String mNotes ~ delete _;
		public bool mIsDisabled;
		public BookmarkFolder mFolder;
    }

	public class BookmarkFolder
	{
		/// The title of the bookmark-folder that will be visible in the bookmark-panel
		public String mTitle ~ delete _;

	    public List<Bookmark> mBookmarkList = new List<Bookmark>() ~
	        {
				for (var bookmark in mBookmarkList)
					bookmark.Kill();
				
				gApp.mDebugger.mBreakpointsChangedDelegate();

				delete _;
	        };

		/// Gets or Sets whether every bookmark in this folder is disabled or not.
		public bool IsDisabled
		{
			get
			{
				for (var bookmark in mBookmarkList)
					if (!bookmark.mIsDisabled)
						return false;

				return true;
			}
			set
			{
				for (var bookmark in mBookmarkList)
					bookmark.mIsDisabled = value;

				gApp.mBookmarksPanel.mBookmarksDirty = true;
			}
		}

		/// Adds the given bookmark to this folder. If needed, removes it from its old folder.
		public void AddBookmark(Bookmark bookmark)
		{
			if (bookmark.mFolder != null)
			{
				bookmark.mFolder.mBookmarkList.Remove(bookmark);
			}
			mBookmarkList.Add(bookmark);
			bookmark.mFolder = this;

			//gApp.mBookmarksPanel.mBookmarksDirty = true;
		}
	}

    public class BookmarkManager
    {
		public BookmarkFolder mRootFolder = new .();
		public List<BookmarkFolder> mBookmarkFolders = new .() {mRootFolder} ~ DeleteContainerAndItems!(_);

		private int mBookmarkCount;

		/// Index of the folder that was navigated to last
		public int32 mFolderIdx;
		/// Index of the bookmark (inside of its folder) that was navigated to last
        public int32 mBookmarkIdx;
		
		/// Number of bookmarks created, used to generate the names.
		private int32 _createdBookmarks;
		/// Number of folders created, used to generate the names.
		private int32 _createdFolders;
		
		/// Gets or sets whether all bookmarks are disabled or not.
		public bool AllBookmarksDisabled
		{
			get
			{
				for (var folder in mBookmarkFolders)
				{
					if (!folder.IsDisabled)
						return false;
				}

				return true;
			}
			set
			{
				for (var folder in mBookmarkFolders)
				{
					folder.IsDisabled = value;
				}
				
				gApp.mBookmarksPanel.mBookmarksDirty = true;
			}
		}
		
		/**
		 * Creates a new bookmark folder
		 * @param title The title of the bookmark
		 * @returns the newly created BookmarkFolder
		 */
		public BookmarkFolder CreateFolder(String title = null)
		{
			mBookmarkIdx = -1;

			BookmarkFolder folder = new .();

			if (title == null)
				folder.mTitle = new $"Folder {_createdFolders++}";
			else
				folder.mTitle = new String(title);

			mBookmarkFolders.Add(folder);            

			gApp.mBookmarksPanel.mBookmarksDirty = true;

			return folder;
		}

		/// Deletes the given bookmark folder and all bookmarks inside it.
		public void DeleteFolder(BookmarkFolder folder)
		{
			int folderIdx = mBookmarkFolders.IndexOf(folder);
			
			mBookmarkFolders.Remove(folder);

			delete folder;

			// Select the previous folder
			if (mFolderIdx == folderIdx)
				folderIdx--;
			if (mFolderIdx >= mBookmarkFolders.Count)
			    mFolderIdx = (int32)mBookmarkFolders.Count - 1;

			// Select last bookmark inside the newly selected folder
			if (mBookmarkIdx >= mBookmarkFolders[mFolderIdx].mBookmarkList.Count)
			    mBookmarkIdx = (int32)mBookmarkFolders[mFolderIdx].mBookmarkList.Count - 1;
			
			gApp.mBookmarksPanel.mBookmarksDirty = true;
		}

        public Bookmark CreateBookmark(String fileName, int wantLineNum, int wantColumn, bool isDisabled = false, String title = null, BookmarkFolder folder = null)
        {
			var folder;

			folder = folder ?? mRootFolder;

			mFolderIdx = (int32)mBookmarkFolders.IndexOf(folder);

			mBookmarkIdx = (int32)folder.mBookmarkList.Count;

            Bookmark bookmark = new Bookmark();
            bookmark.mFileName = new String(fileName);
            bookmark.mLineNum = (int32)wantLineNum;
            bookmark.mColumn = (int32)wantColumn;

			if (title == null)
				bookmark.mTitle = new $"Bookmark {_createdBookmarks++}";
			else
				bookmark.mTitle = new String(title);

			bookmark.mIsDisabled = isDisabled;
   			
			folder.AddBookmark(bookmark);

            gApp.mDebugger.mBreakpointsChangedDelegate();
			
			gApp.mBookmarksPanel.mBookmarksDirty = true;

			mBookmarkCount++;

            return bookmark;
        }

		/** Moves the bookmark to the specified folder.
		 * @param bookmark The bookmark to move.
		 * @param folder The folder to which the bookmark will be moved.
		 * @param insertBefore If null the bookmark will be added at the end of the folder. Otherwise it will be inserted before the specified bookmark.
		 */
		public void MoveBookmarkToFolder(Bookmark bookmark, BookmarkFolder folder, Bookmark insertBefore = null)
		{
			if (bookmark.mFolder != null)
			{
				bookmark.mFolder.mBookmarkList.Remove(bookmark);
			}

			if (insertBefore == null)
				folder.mBookmarkList.Add(bookmark);
			else
			{
				Debug.Assert(folder == insertBefore.mFolder, "Insert before must be in folder.");

				int index = folder.mBookmarkList.IndexOf(insertBefore);

				folder.mBookmarkList.Insert(index, bookmark);
			}

			bookmark.mFolder = folder;
			
			FixupIndices();

			gApp.mBookmarksPanel.mBookmarksDirty = true;
		}

		enum Placement
		{
			Before,
			After
		}

		public void MoveFolder(BookmarkFolder folder, Placement place = .After, BookmarkFolder target = null)
		{
			if (folder == target)
				return;

			if (mBookmarkFolders.Contains(folder))
				mBookmarkFolders.Remove(folder);

			if (target == null)
			{
				mBookmarkFolders.Add(folder);
			}
			else
			{
				int index = mBookmarkFolders.IndexOf(target);

				if (place == .After)
					index++;

				mBookmarkFolders.Insert(index, folder);
			}
			
			FixupIndices();

			gApp.mBookmarksPanel.mBookmarksDirty = true;
		}

		/// Make sure that the bookmark and folder indices are valid.
		private void FixupIndices()
		{
			if (mBookmarkIdx <= 0 || mBookmarkIdx >= mBookmarkFolders[mFolderIdx].mBookmarkList.Count)
			{
				mBookmarkIdx = 0;

				// Don't have an empty folder selected
				if (mBookmarkFolders[mFolderIdx].mBookmarkList.Count == 0)
					mFolderIdx = 0;
			}
		}

        public void DeleteBookmark(Bookmark bookmark)
        {
			BookmarkFolder folder = bookmark.mFolder;

			folder.mBookmarkList.Remove(bookmark);

			FixupIndices();

			gApp.mDebugger.mBreakpointsChangedDelegate();
			bookmark.Kill();

			gApp.mBookmarksPanel.mBookmarksDirty = true;

			mBookmarkCount--;
        }

		public void Clear()
		{
			for (var folder in mBookmarkFolders)
			{
				for (var bookmark in folder.mBookmarkList)
					bookmark.Kill();
				folder.mBookmarkList.Clear();
			}
			ClearAndDeleteItems!(mBookmarkFolders);

			mRootFolder = new BookmarkFolder();
			mBookmarkFolders.Add(mRootFolder);

			mFolderIdx = 0;
			mBookmarkIdx = 0;
			gApp.mDebugger.mBreakpointsChangedDelegate();
			
			gApp.mBookmarksPanel.mBookmarksDirty = true;
		}

        public void PrevBookmark(bool currentFolderOnly = false)
        {
			if (mBookmarkCount == 0)
				return;

			int32 currentFolderIdx = mFolderIdx;
			int32 currentBookmarkIdx = mBookmarkIdx;

			Bookmark prevBookmark = null;
			
			repeat
			{
			    mBookmarkIdx--;

			    if (mBookmarkIdx < 0)
				{
					if (!currentFolderOnly)
					{
						mFolderIdx--;

						if (mFolderIdx < 0)
						{
							// wrap to last folder
							mFolderIdx = (int32)mBookmarkFolders.Count - 1;
						}
					}

					// Select last bookmark in current folder
					mBookmarkIdx = (int32)mBookmarkFolders[mFolderIdx].mBookmarkList.Count - 1;
				}

				if (mBookmarkIdx >= 0)
			    	prevBookmark = mBookmarkFolders[mFolderIdx].mBookmarkList[mBookmarkIdx];
				else
					prevBookmark = null;
			}
			// skip disabled bookmarks, stop when we reach starting point
			while ((prevBookmark == null || prevBookmark.mIsDisabled) && ((currentFolderIdx != mFolderIdx) || (currentBookmarkIdx != mBookmarkIdx) && mBookmarkIdx != -1));

			// If prevBookmark is disabled no bookmark is enabled.
			if (prevBookmark != null && !prevBookmark.mIsDisabled)
				GotoBookmark(prevBookmark);
        }

        public void NextBookmark(bool currentFolderOnly = false)
        {
			if (mBookmarkCount == 0)
				return;

			int32 currentFolderIdx = mFolderIdx;
			int32 currentBookmarkIdx = mBookmarkIdx;

			Bookmark nextBookmark = null;
			
			repeat
			{
			    mBookmarkIdx++;

			    if (mBookmarkIdx >= mBookmarkFolders[mFolderIdx].mBookmarkList.Count)
				{
					if (!currentFolderOnly)
					{
						mFolderIdx++;
						
						if (mFolderIdx >= mBookmarkFolders.Count)
						{
							// wrap to first folder
							mFolderIdx = 0;
						}
					}

					// Select first bookmark in current folder (or -1 if there is no bookmark)
					mBookmarkIdx = mBookmarkFolders[mFolderIdx].mBookmarkList.IsEmpty ? -1 : 0;
				}

				if (mBookmarkIdx >= 0)
			    	nextBookmark = mBookmarkFolders[mFolderIdx].mBookmarkList[mBookmarkIdx];
				else
					nextBookmark = null;
			}
			// skip disabled bookmarks, stop when we reach starting point
			while ((nextBookmark == null || nextBookmark.mIsDisabled) && ((currentFolderIdx != mFolderIdx) || (currentBookmarkIdx != mBookmarkIdx) && mBookmarkIdx != -1));

			// If nextBookmark is disabled no bookmark is enabled.
			if (nextBookmark != null && !nextBookmark.mIsDisabled)
				GotoBookmark(nextBookmark);
        }

		public void GotoBookmark(Bookmark bookmark)
		{
			mFolderIdx = (int32)mBookmarkFolders.IndexOf(bookmark.mFolder);
			mBookmarkIdx = (int32)bookmark.mFolder.mBookmarkList.IndexOf(bookmark);

			gApp.ShowSourceFileLocation(bookmark.mFileName, -1, -1, bookmark.mLineNum, bookmark.mColumn, LocatorType.Smart);
		}
    }
}
