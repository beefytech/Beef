using System.Diagnostics;
using System.Threading;
using System.Collections;

namespace System.IO
{
	public class FileSystemWatcher
	{
		static Monitor sMonitor = new .() ~ delete _;
		static int32 sCurId;
		static Dictionary<int32, FileSystemWatcher> sWatcherDict = new .() ~ delete _;

		String mDirectory ~ delete _;
		String mFilter ~ delete _;
		Platform.BfpFileWatcher* mFileWatcher;
		public int32 mId;

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
			int32 id = (.)(int)userData;

			FileSystemWatcher fileSysWatcher = null;
			using (sMonitor.Enter())
			{
				sWatcherDict.TryGetValue(id, out fileSysWatcher);
			
				if (fileSysWatcher == null)
					return;
	
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
		}

		public Result<void> StartRaisingEvents()
		{
			using (sMonitor.Enter())
			{
				mId = ++sCurId;
				sWatcherDict[mId] = this;
			}

			Platform.BfpFileWatcherFlags flags = IncludeSubdirectories ? .IncludeSubdirectories : .None;
			mFileWatcher = Platform.BfpFileWatcher_WatchDirectory(mDirectory, => BfpDirectoryChangeFunc, flags, (.)(int)mId, null);
			if (mFileWatcher == null)
				return .Err;
			return .Ok;
		}

		public Result<void> StopRaisingEvents()
		{
			if (mFileWatcher == null)
				return .Ok;

			using (sMonitor.Enter())
			{
				Debug.Assert(sWatcherDict.ContainsKey(mId));
				sWatcherDict.Remove(mId);
			}

			Platform.BfpFileWatcher_Release(mFileWatcher);
			mFileWatcher = null;
			mId = 0;
			return .Ok;
		}
	}
}
