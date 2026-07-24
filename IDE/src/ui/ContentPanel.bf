using System;
using Beefy.widgets;

namespace IDE.ui
{
    /// Base class for panels that display the contents of a loaded file (or similar content).
    /// This holds the parts of TextPanel that are not specific to text edit widgets, so that
    /// non-text content (such as BinaryDataPanel) can share the same document behavior.
    public abstract class ContentPanel : Panel
    {
		public bool mDisposed;
		public int32 mLastFocusTick;
		public String mFilePath ~ delete _;

        public virtual bool EscapeHandler()
        {
            return false;
        }

		/// True if this panel has edits that have not been saved to disk
		public virtual bool HasUnsavedChanges()
		{
			return false;
		}

		public virtual bool Save()
		{
			return false;
		}

		public virtual void Reload()
		{
		}

		public virtual void RefusedReload()
		{
		}

		// Default closes this panel/tab; override for panels that shouldn't be (eg a persistent
		// singleton panel that should instead notify whatever owns the content).
		public virtual void HandleFileDeleted()
		{
			gApp.CloseDocument(this);
		}

		public virtual void HandleFileRenamed(String newPath)
		{
			if (mFilePath != null)
				IDEApp.sApp.mFileWatcher.RemoveWatch(mFilePath);
			String.NewOrSet!(mFilePath, newPath);
			IDEApp.sApp.mFileWatcher.WatchFile(mFilePath);
		}

        public virtual void Dispose()
        {
			mDisposed = true;
        }

		public override void ParentDeleted()
		{
			if (!mDisposed)
				Dispose();

			base.ParentDeleted();
		}

        public virtual void RecordHistoryLocation(bool ignoreIfClose = false)
        {

        }

		/// Returns true if this panel is displaying the given file
		public virtual bool FileNameMatches(String fileName)
		{
			return false;
		}

		public virtual void Clear()
		{

		}
    }
}
