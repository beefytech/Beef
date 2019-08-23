using System;
using System.Collections.Generic;
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
        public String mNotes ~ delete _;
    }

    public class BookmarkManager
    {
        public List<Bookmark> mBookmarkList = new List<Bookmark>() ~
            {
				for (var bookmark in mBookmarkList)
					bookmark.Kill();
				delete _;
            };
        public int32 mBookmarkIdx;

        public Bookmark CreateBookmark(String fileName, int wantLineNum, int wantColumn)
        {
            mBookmarkIdx = (int32)mBookmarkList.Count;

            Bookmark bookmark = new Bookmark();
            bookmark.mFileName = new String(fileName);
            bookmark.mLineNum = (int32)wantLineNum;
            bookmark.mColumn = (int32)wantColumn;
            mBookmarkList.Add(bookmark);            

            gApp.mDebugger.mBreakpointsChangedDelegate();
            return bookmark;
        }

        public void DeleteBookmark(Bookmark bookmark)
        {
			int idx = mBookmarkList.IndexOf(bookmark);
            mBookmarkList.RemoveAt(idx);
			if (mBookmarkIdx == idx)
				mBookmarkIdx--;
            if (mBookmarkIdx >= mBookmarkList.Count)
                mBookmarkIdx = (int32)mBookmarkList.Count - 1;
            gApp.mDebugger.mBreakpointsChangedDelegate();
			bookmark.Kill();
        }

		public void Clear()
		{
			for (var bookmark in mBookmarkList)
				bookmark.Kill();
			mBookmarkList.Clear();
			mBookmarkIdx = 0;
			gApp.mDebugger.mBreakpointsChangedDelegate();
		}

        public void PrevBookmark()
        {
            if (mBookmarkList.Count == 0)
                return;

            mBookmarkIdx--;
            if (mBookmarkIdx < 0)
                mBookmarkIdx = (int32)mBookmarkList.Count - 1;

            var bookmark = mBookmarkList[mBookmarkIdx];
            gApp.ShowSourceFileLocation(bookmark.mFileName, -1, -1, bookmark.mLineNum, bookmark.mColumn, LocatorType.Smart);
        }

        public void NextBookmark()
        {
            if (mBookmarkList.Count == 0)
                return;

            mBookmarkIdx++;
            if (mBookmarkIdx >= mBookmarkList.Count)
                mBookmarkIdx = 0;

            var bookmark = mBookmarkList[mBookmarkIdx];
            gApp.ShowSourceFileLocation(bookmark.mFileName, -1, -1, bookmark.mLineNum, bookmark.mColumn, LocatorType.Smart);
        }
    }
}
