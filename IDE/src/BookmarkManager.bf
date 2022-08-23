using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using IDE.ui;
using System.Diagnostics;
using System.IO;

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

		internal int IndexInFolder => mFolder.mBookmarkList.IndexOf(this);
    }

	public class BookmarkFolder
	{
		/// The title of the bookmark-folder that will be visible in the bookmark-panel
		public String mTitle ~ delete _;

	    public List<Bookmark> mBookmarkList = new List<Bookmark>() ~ delete _;

		/// Gets or Sets whether every bookmark in this folder is disabled or not.
		public bool AreAllDisabled
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

				gApp.mBookmarkManager.BookmarksChanged();
			}
		}

		public bool AreAllEnabled
		{
			get
			{
				for (var bookmark in mBookmarkList)
					if (bookmark.mIsDisabled)
						return false;

				return true;
			}
			set
			{
				for (var bookmark in mBookmarkList)
					bookmark.mIsDisabled = !value;

				gApp.mBookmarkManager.BookmarksChanged();
			}
		}

		/// Adds the given bookmark to this folder. If needed, removes it from its old folder.
		internal void AddBookmark(Bookmark bookmark)
		{
			if (bookmark.mFolder != null)
			{
				bookmark.mFolder.mBookmarkList.Remove(bookmark);
			}
			mBookmarkList.Add(bookmark);
			bookmark.mFolder = this;
		}

		internal int Index => gApp.mBookmarkManager.mBookmarkFolders.IndexOf(this);
	}

	using internal Bookmark;
	using internal BookmarkFolder;

    public class BookmarkManager
    {
		public BookmarkFolder mRootFolder = new .();
		public List<BookmarkFolder> mBookmarkFolders = new .() {mRootFolder} ~
			{
				while (!_.IsEmpty)
				{
					DeleteFolder(_.Back);
				}

				delete _;
			};

		/// Occurs when a bookmark/folder is added, removed or moved.
		public Event<Action> BookmarksChanged ~ _.Dispose();

		public delegate void MovedToBookmarkEventHandler(Bookmark bookmark);

		// Occurs when the user jumped to a bookmark.
		public Event<MovedToBookmarkEventHandler> MovedToBookmark ~ _.Dispose();

		private int mBookmarkCount;

		/// Number of bookmarks created, used to generate the names.
		private int32 sBookmarkId;
		/// Number of folders created, used to generate the names.
		private int32 sFolderId;
		
		/// Gets or sets whether all bookmarks are disabled or not.
		public bool AllBookmarksDisabled
		{
			get
			{
				for (var folder in mBookmarkFolders)
				{
					if (!folder.AreAllDisabled)
						return false;
				}

				return true;
			}
			set
			{
				for (var folder in mBookmarkFolders)
				{
					folder.AreAllDisabled = value;
				}

				BookmarksChanged();
			}
		}

		/// Gets the currently selected bookmark or null, if no bookmark is selected.
		public Bookmark CurrentBookmark
		{
			get;
			private set;
		}

		public BookmarkFolder CurrentFolder => CurrentBookmark?.mFolder;

		/**
		 * Creates a new bookmark folder
		 * @param title The title of the bookmark
		 * @returns the newly created BookmarkFolder
		 */
		public BookmarkFolder CreateFolder(String title = null)
		{
			BookmarkFolder folder = new .();

			if (title == null)
				folder.mTitle = new $"Folder {++sFolderId}";
			else
				folder.mTitle = new String(title);

			mBookmarkFolders.Add(folder);

			BookmarksChanged();

			return folder;
		}

		/// Deletes the given bookmark folder and all bookmarks inside it.
		public void DeleteFolder(BookmarkFolder folder)
		{
			if (folder == CurrentFolder)
			{
				// the current bookmark will be deleted, so we have to find another one
				int folderIdx = mBookmarkFolders.IndexOf(folder);
				int newFolderIdx = folderIdx;

				BookmarkFolder newFolder = null;

				repeat
				{
					newFolderIdx--;

					if (newFolderIdx < 0)
					{
						newFolderIdx = mBookmarkFolders.Count - 1;
					}

					if (mBookmarkFolders[newFolderIdx].mBookmarkList.Count != 0)
					{
						newFolder = mBookmarkFolders[newFolderIdx];
						break;
					}
				}
				// Break when we reach start
				while ((folderIdx != newFolderIdx));

				if (newFolder != null)
				{
					Debug.Assert(newFolder.mBookmarkList.Count != 0);

					CurrentBookmark = newFolder.mBookmarkList[0];
				}
			}

			while (!folder.mBookmarkList.IsEmpty)
			{
				gApp.mBookmarkManager.DeleteBookmark(folder.mBookmarkList.Back);
			}

			mBookmarkFolders.Remove(folder);

			delete folder;

			if (gApp.mDebugger?.mBreakpointsChangedDelegate.HasListeners == true)
				gApp.mDebugger.mBreakpointsChangedDelegate();

			BookmarksChanged();
		}

        public Bookmark CreateBookmark(String fileName, int wantLineNum, int wantColumn, bool isDisabled = false, String title = null, BookmarkFolder folder = null)
        {
			var folder;

			folder = folder ?? mRootFolder;

            Bookmark bookmark = new Bookmark();
            bookmark.mFileName = new String(fileName);
            bookmark.mLineNum = (int32)wantLineNum;
            bookmark.mColumn = (int32)wantColumn;

			if (title == null)
			{
				String baseName = Path.GetFileNameWithoutExtension(fileName, .. scope .());

				if (IDEApp.IsSourceCode(fileName))
				{
					var bfSystem = IDEApp.sApp.mBfResolveSystem;
					if (bfSystem != null)
					{
						String content = scope .();
						gApp.LoadTextFile(fileName, content).IgnoreError();

						var parser = bfSystem.CreateEmptyParser(null);
						defer delete parser;
						parser.SetSource(content, fileName, -1);
						var passInstance = bfSystem.CreatePassInstance();
						defer delete passInstance;
						parser.Parse(passInstance, !IDEApp.IsBeefFile(fileName));
						parser.Reduce(passInstance);

						String name = parser.GetLocationName(wantLineNum, wantColumn, .. scope .());
						if (!name.IsEmpty)
							bookmark.mTitle = new $"#{++sBookmarkId} {name}";
					}
				}

				if (bookmark.mTitle == null)
				{
					bookmark.mTitle = new $"#{++sBookmarkId} {baseName}";
				}
			}
			else
				bookmark.mTitle = new String(title);

			bookmark.mIsDisabled = isDisabled;
   			
			folder.[Friend]AddBookmark(bookmark);
			
			if (gApp.mDebugger.mBreakpointsChangedDelegate.HasListeners)
				gApp.mDebugger.mBreakpointsChangedDelegate();

			CurrentBookmark = bookmark;
			BookmarksChanged();

			mBookmarkCount++;

            return bookmark;
        }

        public void DeleteBookmark(Bookmark bookmark)
        {
			bool deletedCurrentBookmark = false;
			Bookmark newCurrentBookmark = null;

			if (bookmark == CurrentBookmark)
			{
				deletedCurrentBookmark = true;

				// try to select a bookmark from the current folder
				newCurrentBookmark = FindPrevBookmark(true);

				if (newCurrentBookmark == null || newCurrentBookmark == CurrentBookmark)
				{
					// Current folder is empty, select from previous folder
					newCurrentBookmark = FindPrevBookmark(false);

					if (newCurrentBookmark == CurrentBookmark)
					{
						// We have to make sure, that we don't select the current bookmark
						newCurrentBookmark = null;
					}
				}
			}

			BookmarkFolder folder = bookmark.mFolder;

			folder.mBookmarkList.Remove(bookmark);

			bookmark.Kill();

			if (deletedCurrentBookmark)
				CurrentBookmark = newCurrentBookmark;

			if (gApp.mDebugger.mBreakpointsChangedDelegate.HasListeners)
				gApp.mDebugger.mBreakpointsChangedDelegate();

			BookmarksChanged();

			mBookmarkCount--;
        }

		enum Placement
		{
			Before,
			After
		}

		/** Moves the bookmark to the specified folder.
		 * @param bookmark The bookmark to move.
		 * @param folder The folder to which the bookmark will be moved.
		 * @param insertBefore If null the bookmark will be added at the end of the folder. Otherwise it will be inserted before the specified bookmark.
		 */
		public void MoveBookmarkToFolder(Bookmark bookmark, BookmarkFolder targetFolder, Placement place = .Before, Bookmark targetBookmark = null)
		{
			if (bookmark.mFolder != null)
			{
				bookmark.mFolder.mBookmarkList.Remove(bookmark);
			}

			if (targetBookmark == null)
				targetFolder.mBookmarkList.Add(bookmark);
			else
			{
				Debug.Assert(targetFolder == targetBookmark.mFolder, "Insert before must be in folder.");

				int index = targetFolder.mBookmarkList.IndexOf(targetBookmark);
				
				if (place == .After)
					index++;

				targetFolder.mBookmarkList.Insert(index, bookmark);
			}

			bookmark.mFolder = targetFolder;

			BookmarksChanged();
		}

		/** Moves the given folder in front of or behind the given target-folder.
		 * @param folder The folder to move.
		 * @param place Specifies whether folder will be placed in front of or behind target.
		 * @param target The folder relative to which folder will be placed.
		 */
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

			BookmarksChanged();
		}

		/// Deletes all bookmarks and bookmark-folders.
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
			
			if (gApp.mDebugger.mBreakpointsChangedDelegate.HasListeners)
				gApp.mDebugger.mBreakpointsChangedDelegate();

			mBookmarkCount = 0;

			BookmarksChanged();
		}

		/** Finds and returns the previous bookmark relative to CurrentBookmark.
		 * @param currentFolderOnly If set to true, only the current folder will be searched. Otherwise all folders will be searched.
		 * @returns The previous bookmark. If CurrentBookmark is the only bookmark, CurrentBookmark will be returned. Null, if there are no bookmarks.
		 */
		private Bookmark FindPrevBookmark(bool currentFolderOnly)
		{
			if (mBookmarkCount == 0)
				return null;

			int currentFolderIdx = CurrentFolder.Index;
			int currentBookmarkIdx = CurrentBookmark.IndexInFolder;

			Bookmark prevBookmark = null;

			int newFolderIndex = currentFolderIdx;
			int newBmIndex = currentBookmarkIdx;

			repeat
			{
			    newBmIndex--;

			    if (newBmIndex < 0)
				{
					if (!currentFolderOnly)
					{
						newFolderIndex--;

						if (newFolderIndex < 0)
						{
							// wrap to last folder
							newFolderIndex = (int32)mBookmarkFolders.Count - 1;
						}
					}

					// Select last bookmark in current folder
					newBmIndex = (int32)mBookmarkFolders[newFolderIndex].mBookmarkList.Count - 1;
				}

				if (newBmIndex >= 0)
					prevBookmark = mBookmarkFolders[newFolderIndex].mBookmarkList[newBmIndex];
				else
					prevBookmark = null;
			}
			// skip disabled bookmarks, stop when we reach starting point
			while ((prevBookmark == null || prevBookmark.mIsDisabled) && ((currentFolderIdx != newFolderIndex) || (currentBookmarkIdx != newBmIndex) && newBmIndex != -1));

			return prevBookmark;
		}

		/** Jumps to the previous bookmark.
		 * @param currentFolderOnly If true, only the current folder will be searched. Otherwise all folders will be searched.
		 */
        public void PrevBookmark(bool currentFolderOnly = false)
        {
			Bookmark prevBookmark = FindPrevBookmark(currentFolderOnly);

			// If prevBookmark is disabled no bookmark is enabled.
			if (prevBookmark != null && !prevBookmark.mIsDisabled)
				GotoBookmark(prevBookmark);
        }
		
		/* Jumps to the next bookmark.
		 * @param currentFolderOnly If true, only the current folder will be searched. Otherwise all folders will be searched.
		 */
        public void NextBookmark(bool currentFolderOnly = false)
        {
			if (mBookmarkCount == 0)
				return;

			int currentFolderIdx = CurrentFolder.Index;
			int currentBookmarkIdx = CurrentBookmark.IndexInFolder;

			Bookmark nextBookmark = null;

			int newFolderIndex = currentFolderIdx;
			int newBmIndex = currentBookmarkIdx;

			repeat
			{
			    newBmIndex++;

			    if (newBmIndex >= mBookmarkFolders[newFolderIndex].mBookmarkList.Count)
				{
					if (!currentFolderOnly)
					{
						newFolderIndex++;

						if (newFolderIndex >= mBookmarkFolders.Count)
						{
							// wrap to first folder
							newFolderIndex = 0;
						}
					}

					// Select first bookmark in current folder (or -1 if there is no bookmark)
					newBmIndex = mBookmarkFolders[newFolderIndex].mBookmarkList.IsEmpty ? -1 : 0;
				}

				if (newBmIndex >= 0)
				nextBookmark = mBookmarkFolders[newFolderIndex].mBookmarkList[newBmIndex];
				else
					nextBookmark = null;
			}
			// skip disabled bookmarks, stop when we reach starting point
			while ((nextBookmark == null || nextBookmark.mIsDisabled) && ((currentFolderIdx != newFolderIndex) || (currentBookmarkIdx != newBmIndex) && newBmIndex != -1));

			// If nextBookmark is disabled no bookmark is enabled.
			if (nextBookmark != null && !nextBookmark.mIsDisabled)
				GotoBookmark(nextBookmark);
        }

		/// Moves the cursor to the given bookmark.
		public void GotoBookmark(Bookmark bookmark)
		{
			CurrentBookmark = bookmark;

			gApp.ShowSourceFileLocation(bookmark.mFileName, -1, -1, bookmark.mLineNum, bookmark.mColumn, LocatorType.Smart);

			MovedToBookmark(bookmark);
		}

		public void RecalcCurId()
		{
			sBookmarkId = 0;
			sFolderId = 0;

			for (var folder in mBookmarkFolders)
			{
				if (folder.mTitle?.StartsWith("Folder ") == true)
				{
					if (int32 curId = int32.Parse(folder.mTitle.Substring("Folder ".Length)))
						sFolderId = Math.Max(sFolderId, curId);
				}

				for (var bookmark in folder.mBookmarkList)
				{
					if (bookmark.mTitle.StartsWith("#"))
					{
						int spacePos = bookmark.mTitle.IndexOf(' ');
						if (spacePos != -1)
						{
							if (int32 curId = int32.Parse(bookmark.mTitle.Substring(1, spacePos - 1)))
								sBookmarkId = Math.Max(sBookmarkId, curId);
						}
					}
				}
			}
		}
    }
}
