#define BFP_HAS_EXECINFO
#define BFP_HAS_PTHREAD_TIMEDJOIN_NP
#define BFP_HAS_PTHREAD_GETATTR_NP
#define BFP_HAS_DLINFO
#define BFP_HAS_FILEWATCHER

#include "../posix/PosixCommon.cpp"

#ifdef BFP_HAS_FILEWATCHER

#include <sys/inotify.h>

struct BfpFileWatcher
{
    String mPath;
    BfpDirectoryChangeFunc mDirectoryChangeFunc;
    int mHandle;
    BfpFileWatcherFlags mFlags;
    void* mUserData;
};

class InotifyFileWatchManager : public FileWatchManager
{
    struct SubdirInfo
    {
        BfpFileWatcher* mWatcher;
        int mHandle;
        String mRelativePath;
    };

    static constexpr size_t MAX_NOTIFY_EVENTS = 1024;
    static constexpr size_t NOTIFY_BUFFER_SIZE = (sizeof(inotify_event) * (MAX_NOTIFY_EVENTS + PATH_MAX));

    bool mIsClosing;
    int mInotifyHandle;
    pthread_t mWorkerThread;
    Dictionary<int, BfpFileWatcher*> mWatchers;
    Dictionary<int, SubdirInfo> mSubdirs;
    CritSect mCritSect;

private:

    void WorkerProc()
    {
        char buffer[NOTIFY_BUFFER_SIZE];

        char pathBuffer[PATH_MAX];

        Array<inotify_event*> unhandledEvents;
        while (!mIsClosing)
        {
            int length = read(mInotifyHandle, &buffer, NOTIFY_BUFFER_SIZE);
            if (mIsClosing) 
                break;
            if (length < 0)
            {
                BFP_ERRPRINTF("Failed to read inotify event data!\n");
                return;
            }
            int i = 0;
            while(i < length)
            {
                inotify_event* event = (inotify_event*) &buffer[i];
                if(event->len != 0)
                {
                    BfpFileWatcher* w;
                    SubdirInfo* subdir;
                    {
                        AutoCrit autoCrit(mCritSect);
                        if (!mWatchers.TryGetValue(event->wd, &w))
                            continue;
                        if (!mSubdirs.TryGetValue(event->wd, &subdir))
                            subdir = NULL;
                    }

                    bool handleDir = (event->mask & IN_ISDIR) && (w->mFlags & BfpFileWatcherFlag_IncludeSubdirectories);

                    if (GetRelativePath(pathBuffer, sizeof(pathBuffer), event->name, event->len, w, subdir) == 0)
                    {
                        // our buffer was too small, we can't handle this event
                        i += sizeof(inotify_event) + event->len;
                        continue;
                    }

                    if (event->mask & IN_MOVED_FROM)
                    {
                        unhandledEvents.Add(event);
                    }
                    if ((event->mask & IN_MOVED_TO))
                    {
                        bool handled = false;
                        for (int i = 0; i < unhandledEvents.size(); i++)
                        {
                            // Only handle as rename if src and dst directory is the same
                            if ((event->cookie == unhandledEvents[i]->cookie) && (event->wd == unhandledEvents[i]->wd))
                            {
                                char renameBuffer[PATH_MAX];
                                if (GetRelativePath(renameBuffer, sizeof(renameBuffer), unhandledEvents[i]->name, unhandledEvents[i]->len, w, subdir) == 0)
                                {
                                   break;
                                }
                                w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Renamed, w->mPath.c_str(), renameBuffer, pathBuffer);
                                unhandledEvents.RemoveAtFast(i);
                                handled = true;
                                break;
                            }
                        }

                        if (!handled)
                        {
                            unhandledEvents.Add(event);
                        }
                    }
                    if (event->mask & IN_CREATE)
                    {
                        w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Added, w->mPath.c_str(), pathBuffer, NULL);
                        HandleDirAdd(event, w, subdir);
                    }
                    if (event->mask & IN_DELETE)
                    {
                        w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Removed, w->mPath.c_str(), pathBuffer, NULL);
                        HandleDirRemove(event, w, subdir);
                    }
                    if (event->mask & IN_CLOSE_WRITE)
                    {
                        w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Modified, w->mPath.c_str(), pathBuffer, NULL);
                    }

                }
                i += sizeof(inotify_event) + event->len;
            }
            for (auto event : unhandledEvents)
            {
                BfpFileWatcher* w;
                SubdirInfo* subdir;
                {
                    AutoCrit autoCrit(mCritSect);
                    if (!mWatchers.TryGetValue(event->wd, &w))
                        continue;
                    if (!mSubdirs.TryGetValue(event->wd, &subdir))
                        subdir = NULL;
                }

                if (GetRelativePath(pathBuffer, sizeof(pathBuffer), event->name, event->len, w, subdir) == 0)
                {
                    continue;
                }

                if (event->mask & IN_MOVED_FROM)
                {
                    w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Removed, w->mPath.c_str(), pathBuffer, NULL);
                    HandleDirRemove(event, w, subdir);
                }
                if (event->mask & IN_MOVED_TO)
                {
                    w->mDirectoryChangeFunc(w, w->mUserData, BfpFileChangeKind_Added, w->mPath.c_str(), pathBuffer, NULL);
                    HandleDirAdd(event, w, subdir);
                }

            }
            unhandledEvents.Clear();
        }
    }

    static void* WorkerProcThunk(void* _this)
    {
        BfpThread_SetName(NULL, "InotifyFileWatcher", NULL);
        ((InotifyFileWatchManager*)_this)->WorkerProc();
        return NULL;
    }
   
    void HandleDirRemove(const inotify_event* event, const BfpFileWatcher* fileWatch, const SubdirInfo* subdir)
    {
        const bool shouldHandle = (event->mask & IN_ISDIR) && (fileWatch->mFlags & BfpFileWatcherFlag_IncludeSubdirectories);
        if (!shouldHandle)
            return;
        
        AutoCrit autoCrit(mCritSect);
        Array<int> toRemove;
        String removedDir;
        if (subdir != NULL)
        {
            removedDir = subdir->mRelativePath;
            removedDir.Append('/');
            removedDir.Append(event->name);
        }

        for (const auto& kv : mSubdirs)
        {
            if (subdir == NULL)
            {
                if (kv.mValue.mWatcher == fileWatch)
                {
                    toRemove.Add(kv.mKey);
                }
            }
            else
            {
                if (kv.mValue.mRelativePath.StartsWith(removedDir))
                {
                    //BFP_ERRPRINTF("REMOVING: %s %s\n", kv.mValue.mRelativePath.c_str(), subdir->mRelativePath.c_str());
                    toRemove.Add(kv.mKey);
                }
            }
        }
        for (const auto val : toRemove)
        {
            mSubdirs.Remove(val);
            if (mWatchers.Remove(val))
                InotifyRemoveWatch(val);
        }
    }

    void HandleDirAdd(const inotify_event* event, BfpFileWatcher* fileWatch, const SubdirInfo* subdir)
    {
        const bool shouldHandle = (event->mask & IN_ISDIR) && (fileWatch->mFlags & BfpFileWatcherFlag_IncludeSubdirectories);
        if (!shouldHandle)
            return;

        String dirPath = fileWatch->mPath;

        if (subdir != NULL)
        {
            dirPath.Append('/');
            dirPath.Append(subdir->mRelativePath);    
        }
        dirPath.Append('/');
        dirPath.Append(event->name);

        int watchHandle = InotifyWatchPath(dirPath.c_str());
        if (watchHandle == -1)
        {
            BFP_ERRPRINTF("Failed to add watch for subdirectory '%s' (%d)\n", dirPath.c_str(), errno);
            return;
        }
        AddWatchEntry(watchHandle, fileWatch);
        AddSubdirEntry(watchHandle, dirPath, fileWatch);
        WatchSubdirectories(dirPath.c_str(), fileWatch);
    }

    void AddWatchEntry(int handle, BfpFileWatcher* fileWatcher)
    {
        AutoCrit autoCrit(mCritSect);
        #if _DEBUG
        BfpFileWatcher* prevWatcher;
        if (mWatchers.TryGetValue(handle, &prevWatcher))
            BF_ASSERT(prevWatcher == fileWatcher);
        #endif
        mWatchers[handle] = fileWatcher;
    }

    void AddSubdirEntry(int handle, const String& currentPath, BfpFileWatcher* fileWatcher)
    {
        AutoCrit autoCrit(mCritSect);
        #if _DEBUG
        SubdirInfo* subdir;
        if (mSubdirs.TryGetValue(handle, &subdir))
            BF_ASSERT(subdir->mWatcher == fileWatcher);
        #endif

        SubdirInfo info;
        info.mHandle = handle;
        info.mWatcher = fileWatcher;
        auto substringLength = std::min(currentPath.length(), fileWatcher->mPath.length() + 1);
        info.mRelativePath = currentPath.Substring(substringLength);
        mSubdirs[handle] = info;
    }

    int InotifyWatchPath(const char* path)
    {
        return inotify_add_watch(mInotifyHandle, path, IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVE);
    }

    void InotifyRemoveWatch(int handle)
    {
        if (inotify_rm_watch(mInotifyHandle, handle) == -1)
        {
            BFP_ERRPRINTF("Failed to remove watch (%d)", errno);
        }
    }

    void HandleDirectory(DIR* dirp, String& o_path, Array<DIR*>& o_workList, BfpFileWatcher* fileWatcher)
    {
        struct dirent* dp;
        while ((dp = readdir(dirp)) != NULL)
        {
            if (dp->d_type != DT_DIR)
                continue;

            if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
                continue;

            auto length = o_path.length();
            o_path.Append('/');
            o_path.Append(dp->d_name);
            int watchHandle = InotifyWatchPath(o_path.c_str());
            if (watchHandle == -1)
            {
                o_path.RemoveToEnd(length);
                BFP_ERRPRINTF("Failed to add watch for subdirectory '%s' (%d)\n", o_path.c_str(), errno);
                continue;
            }
            AddWatchEntry(watchHandle, fileWatcher);
            AddSubdirEntry(watchHandle, o_path, fileWatcher);
            DIR* todo = opendir(o_path.c_str());
            if (todo == NULL)
            {
                 o_path.RemoveToEnd(length);
                 continue;
            }
            o_workList.Add(dirp);
            o_workList.Add(todo);
            return;
        }
        o_workList.Add(NULL);
        closedir(dirp);
    }

    void WatchSubdirectories(const char* path, BfpFileWatcher* fileWatcher)
    {
        DIR* dirp = opendir(path);
        if (dirp == NULL)
            return;

        Array<DIR*> workList;
        String currentPath(path);

        HandleDirectory(dirp, currentPath, workList, fileWatcher);
        while (workList.size() > 0)
        {
            dirp = workList.back();
            workList.pop_back();
            if (dirp == NULL)
            {
                auto dirSeparator = currentPath.LastIndexOf('/');
                if (dirSeparator == -1)
                {
                    BF_ASSERT(workList.IsEmpty());
                    break;
                }
                currentPath.RemoveToEnd(dirSeparator);
                continue;
            }
            HandleDirectory(dirp, currentPath, workList, fileWatcher);
        }
    }

    int GetRelativePath(char* buffer, int bufferSize, const char* fileName, int fileNameLength, const BfpFileWatcher* fileWatcher, const SubdirInfo* subdir)
    {
        if (subdir == NULL)
        {
            memcpy(buffer, fileName, fileNameLength);
            return fileNameLength;
        }

        const auto subdirLength = subdir->mRelativePath.length();
        if (bufferSize < (subdirLength + fileNameLength + 2))
            return 0;
        memcpy(buffer, subdir->mRelativePath.GetPtr(), subdirLength);
        buffer[subdirLength] = '/';
        buffer += subdirLength + 1;
        memcpy(buffer, fileName, fileNameLength);
        buffer[fileNameLength] = '\0';
        return subdirLength + fileNameLength + 1;
    }

public:

    virtual bool Init() override
    {
        mIsClosing = false;
        mInotifyHandle = inotify_init();
        if (mInotifyHandle == -1)
        {
            BFP_ERRPRINTF("Failed to initialize inotify (%d)\n", errno);
            return false;
        }

        int err = pthread_create(&mWorkerThread, NULL, &WorkerProcThunk, this);
        if (err != 0)
        {
            BFP_ERRPRINTF("Failed to create worker thread for inotify FileWatcher!\n");
            return false;
        }

        return true;
    }

    virtual void Shutdown() override
    {
        mIsClosing = true;
        close(mInotifyHandle);
    }

    virtual BfpFileWatcher* WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult) override
    {
        int watchHandle = InotifyWatchPath(path);
        if (watchHandle == -1)
        {
            BFP_ERRPRINTF("Failed to add watch for directory '%s' (%d)\n", path, errno);

            OUTRESULT(BfpFileResult_UnknownError);
            return NULL;
        }
        BfpFileWatcher* fileWatcher = new BfpFileWatcher();
        fileWatcher->mPath = path;
        fileWatcher->mDirectoryChangeFunc = callback;
        fileWatcher->mHandle = watchHandle;
        fileWatcher->mFlags = flags;
        fileWatcher->mUserData = userData;
        AddWatchEntry(watchHandle, fileWatcher);

        if (flags & BfpFileWatcherFlag_IncludeSubdirectories)
        {
            WatchSubdirectories(path, fileWatcher);
        }

        return fileWatcher;
    }

    virtual void Remove(BfpFileWatcher* watcher) override
    {
        AutoCrit autoCrit(mCritSect);

        if ((watcher->mFlags & BfpFileWatcherFlag_IncludeSubdirectories))
        {
            Array<int> toRemove;
            for (const auto& subdir : mSubdirs)
            {
                if (subdir.mValue.mWatcher == watcher)
                {
                    toRemove.Add(subdir.mValue.mHandle);    
                }
            }
            
            for (auto handle : toRemove)
            {
                mSubdirs.Remove(handle);
                mWatchers.Remove(handle);
                InotifyRemoveWatch(handle);
            }
        }

        if (mWatchers.Remove(watcher->mHandle))
        {
            InotifyRemoveWatch(watcher->mHandle);
        }

        delete watcher;
    }

};


FileWatchManager* FileWatchManager::Get()
{
    if (gFileWatchManager == NULL)
    {
        gFileWatchManager = new InotifyFileWatchManager();
        gFileWatchManager->Init();
    }
    return gFileWatchManager;
}
#endif // BFP_HAS_FILEWATCHER
