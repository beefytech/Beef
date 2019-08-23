using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using Beefy;
using System.Threading;
using System.Diagnostics;

namespace IDE
{
    public class FileWatcher
    {
        class WatcherEntry : RefCounted
        {
			// Either mFileSystemWatcher or mParentDirWatcher is set
            public FileSystemWatcher mFileSystemWatcher ~ delete _;
			public WatcherEntry mParentDirWatcher;

            public String mDirectoryName;
            public bool mIgnoreWrites;
			public bool mHasSubDirDependencies;
			public HashSet<String> mAmbiguousCreatePaths = new .() ~ DeleteContainerAndItems!(_);
        }

        class DepInfo
        {
            public List<Object> mDependentObjects = new List<Object>() ~ delete _;
            public String mContent ~ delete _;
        }

		struct QueuedRefreshEntry
		{
			public WatcherEntry mWatcherEntry;
			public int32 mDelay;
		}

		class QueuedFileChange
		{
			public String mFileName ~ delete _;
			public String mNewFileName ~ delete _;
			public WatcherChangeTypes mChangeType;
		}

		public class ChangeRecord
		{
			public String mPath ~ delete _;
			public WatcherChangeTypes mChangeType;
		}

        // One watcher per directory
		public static int32 sDbgFileCreateDelay;
        Dictionary<String, WatcherEntry> mWatchers = new Dictionary<String, WatcherEntry>();
        Dictionary<String, DepInfo> mWatchedFiles = new Dictionary<String, DepInfo>() ~ DeleteDictionyAndKeysAndItems!(_); // Including ref count
        List<ChangeRecord> mChangeList = new .() ~ DeleteContainerAndItems!(_);
		Dictionary<String, ChangeRecord> mChangeMap = new .() ~ delete _;
        List<Object> mDependencyChangeList = new List<Object>() ~ delete _;
        List<QueuedRefreshEntry> mQueuedRefreshWatcherEntries = new List<QueuedRefreshEntry>() ~ delete _;
		public Monitor mMonitor = new Monitor() ~ delete _;
		List<QueuedFileChange> mQueuedFileChanges = new List<QueuedFileChange>() ~ DeleteContainerAndItems!(_);
		public Monitor mFileChangeMonitor = new Monitor() ~ delete _;
		public int mChangeId;

        public ~this()
        {
			for (var watcher in mWatchers)
			{
				delete watcher.key;
				watcher.value.DeleteUnchecked(); // Allow even when refCount isn't zero
			}
			delete mWatchers;
        }

        void FixFilePath(String filePath)
        {
			String oldPath = scope String(filePath);
			filePath.Clear();
            Path.GetFullPath(oldPath, filePath);
            IDEUtils.FixFilePath(filePath);
            if (!Environment.IsFileSystemCaseSensitive)
                filePath.ToUpper();
        }

        void FileChanged(String filePath, String newPath, WatcherChangeTypes changeType)
        {
			if (changeType == .Renamed)
			{
				// ALWAYS interpret 'file rename' notifications as a delete of filePath and a create of newPath
				// A manual rename in the IDE will have manually processed the rename before we get here, so
				// we expect 'filePath' won't actually match anything and the 'newPath' will already be set up
				// in the mWatchedFile
				FileChanged(filePath, null, .Deleted);

				var dirName = scope String();
				Path.GetDirectoryPath(filePath, dirName);
				dirName.Append(Path.DirectorySeparatorChar);
				FileChanged(dirName, newPath, .FileCreated);
				return;
			}

			if ((changeType == .FileCreated) && (gApp.IsFilteredOut(newPath)))
				return;

			String fixedFilePath = scope String(filePath);
            FixFilePath(fixedFilePath);

            bool wantLoadContent = false;
			if (newPath == null)
			{
	            using (mMonitor.Enter())
	            {
	                DepInfo depInfo;
	                mWatchedFiles.TryGetValue(fixedFilePath, out depInfo);
	                if (depInfo != null)
	                    wantLoadContent = depInfo.mContent != null;
	            }
			}

			//Debug.WriteLine("FileChanged {0} {1} {2}", filePath, newPath, changeType);

            String newContent = scope String();
            if (wantLoadContent)
			{
				//TODO: Make this better
				for (int32 i = 0; i < 25; i++)
				{
					if (gApp.LoadTextFile(fixedFilePath, newContent) case .Ok)
						break;
					Thread.Sleep(10);
				}				
			}

            using (mMonitor.Enter())
            {
                DepInfo depInfo;
                mWatchedFiles.TryGetValue(fixedFilePath, out depInfo);
                if (depInfo == null)
                    return;

                if (depInfo.mContent != null)
                {
                    /*File.WriteAllText(@"c:\\temp\\file1.txt", newContent);
                    File.WriteAllText(@"c:\\temp\\file2.txt", depInfo.mContent);*/

                    if (newContent == null) // No actual content (file renamed - as part of file.new, file -> file.old, file.new -> file
                        return; 
                    if ((depInfo.mContent != null) && (Utils.FileTextEquals(depInfo.mContent, newContent)))
                        return;

                    // We were only saving this file until the content actually changed
					delete depInfo.mContent;
                    depInfo.mContent = null;
                }

				bool added = mChangeMap.TryAdd(fixedFilePath, var keyPtr, var valuePtr);
				if (added)
				{
					ChangeRecord changeRecord = new .();
					changeRecord.mPath = new String(fixedFilePath);
					changeRecord.mChangeType = changeType;
					*keyPtr = changeRecord.mPath;
					*valuePtr = changeRecord;
					mChangeList.Add(changeRecord);
				}
				else
				{
					let changeRecord = *valuePtr;
					changeRecord.mChangeType |= changeType;
				}
				ProjectItem projectItem = null;
                for (var dep in depInfo.mDependentObjects)
				{
					if (var tryProjectItem = dep as ProjectItem)
						projectItem = tryProjectItem;
                    if ((dep != null) && (!mDependencyChangeList.Contains(dep)))
                    {
                        mDependencyChangeList.Add(dep);
					}
				}

				if (projectItem != null)
					gApp.OnWatchedFileChanged(projectItem, changeType, newPath);
            }
        }

        public void OmitFileChange(String filePath, String content = null)
        {
            String fixedFilePath = scope String(filePath);
            FixFilePath(fixedFilePath);

            using (mMonitor.Enter())
            {
                DepInfo depInfo;
                mWatchedFiles.TryGetValue(fixedFilePath, out depInfo);
                if (depInfo != null)
				{					
                    String.NewOrSet!(depInfo.mContent, content);
				}
            }
        }

        public void FileChanged(String filePath)
        {
            FileChanged(filePath, null, WatcherChangeTypes.Changed);
        }

		void TryAddRefreshWatchEntry(WatcherEntry watcherEntry, int32 delay = 0)
		{
			Debug.Assert(watcherEntry != null);
			Debug.Assert(watcherEntry.mParentDirWatcher == null);

			bool found = false;
			for (int32 i = 0; i < mQueuedRefreshWatcherEntries.Count; i++)
			{
				var refreshEntry = ref mQueuedRefreshWatcherEntries[i];
				if (refreshEntry.mWatcherEntry == watcherEntry)
				{
					refreshEntry.mDelay = Math.Min(refreshEntry.mDelay, delay);
					found = true;
				}
			}
			if (!found)
			{
				QueuedRefreshEntry refreshEntry;
				watcherEntry.AddRef();
				refreshEntry.mWatcherEntry = watcherEntry;
				refreshEntry.mDelay = delay;
				mQueuedRefreshWatcherEntries.Add(refreshEntry);
			}
		}

		void QueueFileChanged(FileSystemWatcher fileSystemWatcher, String fileName, String newName, WatcherChangeTypes changeType)
		{
			//Debug.WriteLine("QueueFileChanged {0} {1} {2} {3}", fileSystemWatcher.Directory, fileName, newName, changeType);

			using (mFileChangeMonitor.Enter())
			{
				String fullPath = scope String();
				fullPath.Append(fileSystemWatcher.Directory);
				if (fileName != null)
				{
					fullPath.Append(Path.DirectorySeparatorChar);
					fullPath.Append(fileName);
				}

				var queuedFileChange = new QueuedFileChange();
				if ((changeType == .FileCreated) || (changeType == .DirectoryCreated))
				{
					queuedFileChange.mFileName = new String();
					Path.GetDirectoryPath(fullPath, queuedFileChange.mFileName);
					queuedFileChange.mFileName.Append(Path.DirectorySeparatorChar);
					queuedFileChange.mNewFileName = new String(fullPath);
				}
				else
				{
					queuedFileChange.mFileName = new String(fullPath);
					if (newName != null)
					{
						var newFullPath = new String();
						Path.GetDirectoryPath(fullPath, newFullPath);
						newFullPath.Append(Path.DirectorySeparatorChar);
						newFullPath.Append(newName);
						queuedFileChange.mNewFileName = newFullPath;
					}
				}
				queuedFileChange.mChangeType = changeType;
				mQueuedFileChanges.Add(queuedFileChange);
				mChangeId++;
			}
		}

        void StartDirectoryWatcher(WatcherEntry watcherEntry)
        {
            FileSystemWatcher fileSystemWatcher = null;

			//Debug.WriteLine("StartDirectoryWatcher {0}", watcherEntry.mDirectoryName);

			//Console.WriteLine("StartDirectoryWatcher");
            fileSystemWatcher = new FileSystemWatcher(watcherEntry.mDirectoryName);
			fileSystemWatcher.IncludeSubdirectories = watcherEntry.mHasSubDirDependencies;
			delete watcherEntry.mFileSystemWatcher;
			watcherEntry.mFileSystemWatcher = null;

			void GetPath(String fileName, String outPath)
			{
				outPath.Append(watcherEntry.mDirectoryName);
				outPath.Append(Path.DirectorySeparatorChar);
				outPath.Append(fileName);
			}

			void CheckFileCreated(String fileName)
			{
				if (sDbgFileCreateDelay > 0)
					Thread.Sleep(sDbgFileCreateDelay);

				var filePath = scope String();
				GetPath(fileName, filePath);
				if (File.Exists(filePath))
				{
					QueueFileChanged(fileSystemWatcher, fileName, null, .FileCreated);
				}
				else if (Directory.Exists(filePath))
					QueueFileChanged(fileSystemWatcher, fileName, null, .DirectoryCreated);
				else
				{
					using (mFileChangeMonitor.Enter())
					{
						if (watcherEntry.mAmbiguousCreatePaths.TryAdd(fileName, var entryPtr))
							*entryPtr = new String(fileName);
					}
				}	
			}

			bool HasAmbiguousFileName(String fileName)
			{
				switch (watcherEntry.mAmbiguousCreatePaths.GetAndRemove(fileName))
				{
				case .Ok(let str):
					delete str;
					return true;
				case .Err:
					return false;
				}
			}

            if (!watcherEntry.mIgnoreWrites)
                fileSystemWatcher.OnChanged.Add(new (fileName) =>
					{
						using (mFileChangeMonitor.Enter())
						{
							if (HasAmbiguousFileName(fileName))
							{
								CheckFileCreated(fileName);
							}
							
							QueueFileChanged(fileSystemWatcher, fileName, null, .Changed);
						}
					});
            fileSystemWatcher.OnCreated.Add(new (fileName) =>
				{
					CheckFileCreated(fileName);
				});
            fileSystemWatcher.OnDeleted.Add(new (fileName) =>
				{
					using (mFileChangeMonitor.Enter())
					{
						if (HasAmbiguousFileName(fileName))
						{
							// We didn't process the CREATE so just ignore the DELETE
						}
						else
							QueueFileChanged(fileSystemWatcher, fileName, null, .Deleted);
					}
				});
            fileSystemWatcher.OnRenamed.Add(new (oldName, newName) =>
				{
					using (mFileChangeMonitor.Enter())
					{
						if (HasAmbiguousFileName(oldName))
						{
							// We didn't process the CREATE so treat this as a new CREATE
							CheckFileCreated(newName);
						}
						else
							QueueFileChanged(fileSystemWatcher, oldName, newName, .Renamed);
					}
				});
			fileSystemWatcher.OnError.Add(new () => QueueFileChanged(fileSystemWatcher, null, null, .Failed));
            if (fileSystemWatcher.StartRaisingEvents() case .Err)
			{
				delete fileSystemWatcher;
				//TryAddRefreshWatchEntry(watcherEntry, 30); // Try again later?
			}
			else
			{
		        fileSystemWatcher.OnError.Add(new () =>
		            {
		                if (watcherEntry.mFileSystemWatcher == fileSystemWatcher)
		                {							                
		                    TryAddRefreshWatchEntry(watcherEntry);
		                }
		            });				
	            watcherEntry.mFileSystemWatcher = fileSystemWatcher;
			}
        }

        // This corrects an error where we attempted to watch a directory that wasn't valid but now it is
        public void FileIsValid(String filePath)
        {
			String fixedFilePath = scope String(filePath);
			FixFilePath(fixedFilePath);
            using (mMonitor.Enter())
            {
                String directoryName = scope String();
                Path.GetDirectoryPath(fixedFilePath, directoryName);
                WatcherEntry watcherEntry;
                mWatchers.TryGetValue(directoryName, out watcherEntry);
                if (watcherEntry != null)
                {
                    if ((watcherEntry.mFileSystemWatcher == null) && (watcherEntry.mParentDirWatcher == null))
                    {
						TryAddRefreshWatchEntry(watcherEntry);                        
                    }                        
                }
            }
        }

        public void WatchFile(String filePath, Object dependentObject = null, bool ignoreWrites = false)
        {
			//Debug.WriteLine("WatchFile {0}", filePath);

#if !CLI
            String fixedFilePath = scope String(filePath);
			FixFilePath(fixedFilePath);
            using (mMonitor.Enter())
            {
                DepInfo depInfo;
                mWatchedFiles.TryGetValue(fixedFilePath, out depInfo);
                if (depInfo != null)
                {
                    depInfo.mDependentObjects.Add(dependentObject);
                    return;
                }
                depInfo = new DepInfo();
                depInfo.mDependentObjects.Add(dependentObject);
                mWatchedFiles[new String(fixedFilePath)] = depInfo;
                            
                String directoryName = scope String();
                Path.GetDirectoryPath(fixedFilePath, directoryName);

                mWatchers.TryGetValue(directoryName, var watcherEntry);
                if (watcherEntry != null)
                {
					watcherEntry.AddRef();
                }
                else
                {
					directoryName = new String(directoryName);

                    watcherEntry = new WatcherEntry();
                    watcherEntry.mDirectoryName = directoryName;
                    watcherEntry.mIgnoreWrites = ignoreWrites;

					String checkDir = scope String()..Append(directoryName);
					while (true)
					{
						String parentDirName = scope String();
						if (Path.GetDirectoryPath(checkDir, parentDirName) case .Err)
							break;
						if (mWatchers.TryGetValue(parentDirName, var parentWatcherEntry))
						{
							while (parentWatcherEntry.mParentDirWatcher != null)
								parentWatcherEntry = parentWatcherEntry.mParentDirWatcher;

							parentWatcherEntry.AddRef();
							watcherEntry.mParentDirWatcher = parentWatcherEntry;
							if (!parentWatcherEntry.mHasSubDirDependencies)
							{
								// Restart the watcher with the IncludeSubdirectories flag
								parentWatcherEntry.mHasSubDirDependencies = true;
								StartDirectoryWatcher(parentWatcherEntry);
							}
							break;
						}

						checkDir.Set(parentDirName);
					}

					if (watcherEntry.mParentDirWatcher == null)
                    	StartDirectoryWatcher(watcherEntry);
                    mWatchers[directoryName] = watcherEntry;
                }
            }            
#endif
        }

		public void DerefWatcherEntry(WatcherEntry watchEntry)
		{
			if (watchEntry.ReleaseRefNoDelete() == 0)
			{
				if (mWatchers.GetAndRemove(watchEntry.mDirectoryName) case .Ok((var key, var value)))
				{
					if (watchEntry.mParentDirWatcher != null)
						DerefWatcherEntry(watchEntry.mParentDirWatcher);

					Debug.Assert(watchEntry == value);
					delete key;
					delete watchEntry;
				}
			}
		}

        public void RemoveWatch(String filePath, Object dependentObject = null)
        {
#if !CLI
            String fixedFilePath = scope String(filePath);
			FixFilePath(fixedFilePath);
            using (mMonitor.Enter())
            {
                DepInfo depInfo;
				String outKey;
                if (!mWatchedFiles.TryGetValue(fixedFilePath, out outKey, out depInfo))
					return;
                depInfo.mDependentObjects.Remove(dependentObject);

                if (depInfo.mDependentObjects.Count == 0)
                {
                    mWatchedFiles.Remove(fixedFilePath);					

                    String directoryName = scope String();
                    Path.GetDirectoryPath(fixedFilePath, directoryName);
                    WatcherEntry watcherEntry = null;
					String key;
                    mWatchers.TryGetValue(directoryName, out key, out watcherEntry);

					DerefWatcherEntry(watcherEntry);
                    
					delete outKey;
					delete depInfo;
                }

				mDependencyChangeList.Remove(dependentObject);
            }
#endif
        }

        public void Update(Action<String, String, WatcherChangeTypes> fileChangeHandler = null)
        {
			while (true)
			{
				QueuedFileChange queuedFileChange;
				using (mFileChangeMonitor.Enter())
				{
					if (mQueuedFileChanges.Count == 0)
						break;
					queuedFileChange = mQueuedFileChanges.PopFront();
				}

				if (fileChangeHandler != null)
					fileChangeHandler(queuedFileChange.mFileName, queuedFileChange.mNewFileName, queuedFileChange.mChangeType);

				FileChanged(queuedFileChange.mFileName, queuedFileChange.mNewFileName, queuedFileChange.mChangeType);
				delete queuedFileChange;
			}

            using (mMonitor.Enter())
            {
                if (mQueuedRefreshWatcherEntries.Count == 0)
                    return;
                ref QueuedRefreshEntry refreshEntry = ref mQueuedRefreshWatcherEntries[0];
				Debug.Assert(refreshEntry.mWatcherEntry != null);
				if (refreshEntry.mDelay > 0)
				{
					refreshEntry.mDelay--;
				}
                else
                {                    
                	StartDirectoryWatcher(refreshEntry.mWatcherEntry);
					DerefWatcherEntry(refreshEntry.mWatcherEntry);
					mQueuedRefreshWatcherEntries.RemoveAt(0);
				}
            }
        }        

        public ChangeRecord PopChangedFile()
        {
            using (mMonitor.Enter())
            {
                if (mChangeList.Count == 0)
                    return null;
                let changeRecord = mChangeList[0];
				bool removed = mChangeMap.Remove(changeRecord.mPath);
				Debug.Assert(removed);
                mChangeList.RemoveAt(0);
                return changeRecord;
            }
        }

		public void AddChangedFile(ChangeRecord changeRecord)
		{
			using (mMonitor.Enter())
			{
				mChangeList.Add(changeRecord);
				bool added = mChangeMap.TryAdd(changeRecord.mPath, changeRecord);
				Debug.Assert(added);
			}
		}

        public Object PopChangedDependency()
        {
            using (mMonitor.Enter())
            {
                if (mDependencyChangeList.Count == 0)
                    return null;
                Object dep = mDependencyChangeList[0];
                mDependencyChangeList.RemoveAt(0);
                return dep;
            }
        }

		public void AddChangedDependency(Object obj)
		{
			using (mMonitor.Enter())
			{
				mDependencyChangeList.Add(obj);
			}
		}
    }
}
