using System.Diagnostics;
using System.Threading;

namespace System.IO
{
	public class FileSystemWatcher
	{
		String mDirectory ~ delete _;
		String mFilter ~ delete _;
		Platform.BfpFileWatcher* mFileWatcher;

		public delegate void CreatedFunc(String fileName);
		public delegate void DeletedFunc(String fileName);
		public delegate void ChangedFunc(String fileName);
		public delegate void RenameFunc(String newName, String oldName);
		public delegate void ErrorFunc();

		public Event<ChangedFunc> OnChanged ~ _.Dispose();
		public Event<CreatedFunc> OnCreated ~ _.Dispose();
		public Event<DeletedFunc> OnDeleted ~ _.Dispose();
		public Event<RenameFunc> OnRenamed ~ _.Dispose();
		public Event<ErrorFunc> OnError ~ _.Dispose();
		public bool IncludeSubdirectories;

		public this()
		{
			mDirectory = String.Empty;
			mFilter = "*.*";
		}

		public this(StringView path) : this(path, "*.*")
		{
		}

		public this(StringView path, StringView filter)
		{
			this.mDirectory = new String(path);
			this.mFilter = new String(filter);
		}

		public ~this()
		{
			StopRaisingEvents().IgnoreError();
		}

		public String Directory
		{
			get
			{
				return mDirectory;
			}
		}

		static void BfpDirectoryChangeFunc(Platform.BfpFileWatcher* watcher, void* userData, Platform.BfpFileChangeKind changeKind, char8* directory, char8* fileName, char8* newName)
		{
			let fileSysWatcher = (FileSystemWatcher)Internal.UnsafeCastToObject(userData);

			switch (changeKind)
			{
			case .BfpFileChangeKind_Added:
				fileSysWatcher.OnCreated(scope String(fileName));
			case .BfpFileChangeKind_Modified:
				fileSysWatcher.OnChanged(scope String(fileName));
			case .BfpFileChangeKind_Removed:
				fileSysWatcher.OnDeleted(scope String(fileName));
			case .BfpFileChangeKind_Renamed:
				fileSysWatcher.OnRenamed(scope String(fileName), scope String(newName));
			case .BfpFileChangeKind_Failed:
				fileSysWatcher.OnError();
			}
		}

		public Result<void> StartRaisingEvents()
		{
			Platform.BfpFileWatcherFlags flags = IncludeSubdirectories ? .IncludeSubdirectories : .None;
			mFileWatcher = Platform.BfpFileWatcher_WatchDirectory(mDirectory, => BfpDirectoryChangeFunc, flags, Internal.UnsafeCastToPtr(this), null);
			if (mFileWatcher == null)
				return .Err;
			return .Ok;
		}

		public Result<void> StopRaisingEvents()
		{
			if (mFileWatcher == null)
				return .Ok;

			Platform.BfpFileWatcher_Release(mFileWatcher);
			mFileWatcher = null;
			return .Ok;
		}
	}
}
