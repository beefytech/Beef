#include <execinfo.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#define lseek64 lseek
#define ftruncate64 ftruncate

#define BFP_HAS_FILEWATCHER

#include "../posix/PosixCommon.cpp"

char* itoa(int value, char* str, int base)
{
    if (base == 16)
        sprintf(str, "%X", value);
    else
        sprintf(str, "%d", value);
    return str;
}

// Needs at least macos 10.13, will crash in callback on earlier version because of kFSEventStreamCreateFlagUseExtendedData
#ifdef BFP_HAS_FILEWATCHER

#include <CoreServices/CoreServices.h>
#include <atomic>

static void* gCoreFoundationLib = NULL;
static void* gCoreServicesLib = NULL;

// libdispatch Function Pointers

static dispatch_queue_t (*bf_dispatch_queue_create)(const char* label, void* attr) = NULL;
static void (*bf_dispatch_async_f)(dispatch_queue_t queue, void* context, dispatch_function_t work) = NULL;
static void (*bf_dispatch_release)(void* object) = NULL;

// FSEvents Function Pointers
static FSEventStreamRef (*bf_FSEventStreamCreate)(
	CFAllocatorRef allocator,
	FSEventStreamCallback callback,
	FSEventStreamContext *context,
	CFArrayRef pathsToWatch,
	FSEventStreamEventId sinceWhen,
	CFTimeInterval latency,
	FSEventStreamCreateFlags flags
) = NULL;
static void (*bf_FSEventStreamSetDispatchQueue)(FSEventStreamRef streamRef, dispatch_queue_t q) = NULL;

static Boolean (*bf_FSEventStreamStart)(FSEventStreamRef streamRef) = NULL;
static void (*bf_FSEventStreamStop)(FSEventStreamRef streamRef) = NULL;
static void (*bf_FSEventStreamInvalidate)(FSEventStreamRef streamRef) = NULL;
static void (*bf_FSEventStreamRelease)(FSEventStreamRef streamRef) = NULL;

// FSEvents Constant
static CFStringRef bf_kFSEventStreamEventExtendedDataPathKey = NULL;
static CFStringRef bf_kFSEventStreamEventExtendedFileIDKey = NULL;

// CoreFoundation Function Pointers
static const void* (*bf_CFArrayGetValueAtIndex)(CFArrayRef theArray, CFIndex idx) = NULL;
static const void* (*bf_CFDictionaryGetValue)(CFDictionaryRef theDict, const void *key) = NULL;
static const char* (*bf_CFStringGetCStringPtr)(CFStringRef theString, CFStringEncoding encoding) = NULL;
static Boolean (*bf_CFStringGetCString)(CFStringRef theString, char *buffer, CFIndex bufferSize, CFStringEncoding encoding) = NULL;
static Boolean (*bf_CFNumberGetValue)(CFNumberRef number, CFNumberType theType, void *valuePtr) = NULL;
static CFStringRef (*bf_CFStringCreateWithCString)(CFAllocatorRef alloc, const char *cStr, CFStringEncoding encoding) = NULL;
static CFArrayRef (*bf_CFArrayCreate)(CFAllocatorRef allocator, const void **values, CFIndex numValues, const CFArrayCallBacks *callBacks) = NULL;
static void (*bf_CFRelease)(CFTypeRef cf) = NULL;

struct BfpFileWatcher
{
	String					mWatchPath;
	String					mAbsPath;
	BfpDirectoryChangeFunc	mDirectoryChangeFunc;
	void*					mUserData;

	FSEventStreamRef		mStreamRef;
	dispatch_queue_t		mDispatchQueue;

	CritSect				mCritSect;
	std::atomic<bool>		mShuttingDown;
	bool					mStarted;
	bool					mIncludeSubdirs;
	// Make type non-copyable
	BfpFileWatcher(const BfpFileWatcher&) = delete;
	BfpFileWatcher& operator=(const BfpFileWatcher&) = delete;

	BfpFileWatcher() :
		mDirectoryChangeFunc(NULL),
		mUserData(NULL),
		mStreamRef(NULL),
		mDispatchQueue(NULL),
		mShuttingDown(false),
		mStarted(false),
		mIncludeSubdirs(false)
	{
	}

	~BfpFileWatcher()
	{
		ReleaseStream();
		ReleaseQueue();
	}

	void ReleaseStream()
	{
		if (mStreamRef == NULL)
			return;

		if (mStarted)
		{
			bf_FSEventStreamStop(mStreamRef);
			mStarted = false;
		}

		bf_FSEventStreamInvalidate(mStreamRef);
		bf_FSEventStreamRelease(mStreamRef);
		mStreamRef = NULL;
	}

	void ReleaseQueue()
	{
		if (mDispatchQueue == NULL)
			return;

		bf_dispatch_release(mDispatchQueue);
		mDispatchQueue = NULL;
	}
};

static bool CheckPathExists(const char* path)
{
	struct stat sb;
	return (lstat(path, &sb) == 0);
}

// Callback for watcher
static void FSEventsCallback(
    ConstFSEventStreamRef streamRef,
    void* clientCallBackInfo,
    size_t numEvents,
    void* eventData,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
	BfpFileWatcher* watcher = (BfpFileWatcher*)clientCallBackInfo;
	if (watcher->mShuttingDown)
		return;

	AutoCrit autoCrit(watcher->mCritSect);

	CFArrayRef dictArray = (CFArrayRef)(eventData);

	// We do not handle renames across batches as that would needlessly complicate things
	// if cross batch rename occurs then remove and add is emitted
	Dictionary<uint64_t, String> renamedItems;

	char pathBuffer[PATH_MAX];

	for (size_t i = 0; i < numEvents; i++)
	{
		if (watcher->mShuttingDown)
			return;

		FSEventStreamEventFlags flags = eventFlags[i];

		if (flags & (kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagUnmount | kFSEventStreamEventFlagMount))
		{
			watcher->mAbsPath.clear();
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Failed, watcher->mWatchPath.c_str(),NULL, NULL);

			char* newAbsPath = realpath(watcher->mWatchPath.c_str(), NULL);
			if (newAbsPath == NULL)
				continue;

			watcher->mAbsPath = newAbsPath;
			free(newAbsPath);
			continue;
		}

		if (watcher->mAbsPath.empty())
			continue;

		CFDictionaryRef dict = (CFDictionaryRef)(bf_CFArrayGetValueAtIndex(dictArray, (CFIndex)i));

		CFStringRef pathCF = (CFStringRef)bf_CFDictionaryGetValue(dict, bf_kFSEventStreamEventExtendedDataPathKey);
		if (pathCF == NULL)
			continue;

		const char* path = bf_CFStringGetCStringPtr(pathCF, kCFStringEncodingUTF8);
		if (path == NULL)
		{
			if (!bf_CFStringGetCString(pathCF, pathBuffer, sizeof(pathBuffer), kCFStringEncodingUTF8))
				continue;

			path = pathBuffer;
		}

		// Make event path relative to watched path
		String absFilePath = StringImpl::MakeRef(path);
		String relativeFilePath = GetRelativePath(absFilePath, watcher->mAbsPath);
		String relativeDirectoryPath = GetFileDir(relativeFilePath);

		// Check if event happened inside subdirectory and should be ignored
		// - when registered without BfpFileWatcherFlag_IncludeSubdirectories
		if ((!watcher->mIncludeSubdirs) && (!relativeDirectoryPath.empty()))
		{
			// Trigger modified/error for containing directory

			if (flags & (kFSEventStreamEventFlagKernelDropped | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagMustScanSubDirs))
			{
				if (relativeDirectoryPath.IndexOf('/') == -1)
				{
					watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Failed, watcher->mWatchPath.c_str(),NULL, NULL);
				}
			}
			else if (flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemCloned | kFSEventStreamEventFlagItemRemoved))
			{
				if (relativeDirectoryPath.IndexOf('/') == -1)
				{
					watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),relativeDirectoryPath.c_str(), NULL);
				}
			}

			continue;
		}

		if (flags & (kFSEventStreamEventFlagKernelDropped | kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagMustScanSubDirs))
		{
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Failed, watcher->mWatchPath.c_str(),NULL, NULL);
			continue;
		}

		if (flags & kFSEventStreamEventFlagItemRenamed)
		{
			CFNumberRef fileIDCF = (CFNumberRef)bf_CFDictionaryGetValue(dict, bf_kFSEventStreamEventExtendedFileIDKey);

			if (fileIDCF == NULL)
				continue;

			uint64_t fileID = 0;
			bf_CFNumberGetValue(fileIDCF, kCFNumberLongLongType, &fileID);

			uint64_t* keyPtr;
			String* valPtr;

			if (renamedItems.TryAdd(fileID, &keyPtr, &valPtr))
			{
				*valPtr = absFilePath;
			}
			else
			{
				String* newPath;
				String* oldPath;
				if (CheckPathExists(absFilePath.c_str()))
				{
					newPath = &relativeFilePath;
					oldPath = valPtr;
				}
				else if (CheckPathExists(valPtr->c_str()))
				{
					oldPath = &relativeFilePath;
					newPath = valPtr;
				}
				else
				{
					// Neither path does exist, might be A -> B -> C rename or removed just skip
					continue;
				}

				// Make the stored path relative
				*valPtr =  GetRelativePath(*valPtr, watcher->mAbsPath);

				const auto newDirPathIdx = newPath->LastIndexOf('/');
				const auto oldDirPathIdx = oldPath->LastIndexOf('/');

				// Only handle as rename if it is within the same directory
				bool isRename = false;
				if (newDirPathIdx == oldDirPathIdx)
				{
					if (newDirPathIdx == -1)
					{
						isRename = true;
					}
					else
					{
						isRename = (String::Compare(*newPath, 0, *oldPath, 0, newDirPathIdx, false) == 0);
					}
				}

				if (isRename)
				{
					watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Renamed, watcher->mWatchPath.c_str(), oldPath->c_str(), newPath->c_str());
				}
				else
				{
					watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Removed, watcher->mWatchPath.c_str(),oldPath->c_str(), NULL);
					watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Added, watcher->mWatchPath.c_str(),newPath->c_str(), NULL);

					// trigger modified on directories where the changes happened
					String dir = GetFileDir(*oldPath);
					if (!dir.empty())
						watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),dir.c_str(), NULL);
					dir = GetFileDir(*newPath);
					if (!dir.empty())
						watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),dir.c_str(), NULL);
				}

				renamedItems.Remove(fileID);
			}

			continue;
		}

		// Since events are coalesced into one, we can have created and removed set at the same time
		// Check if the file exists and remove the invalid flag
		if ((flags & kFSEventStreamEventFlagItemRemoved) && (flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemCloned)))
		{
			if (CheckPathExists(absFilePath.c_str()))
				flags &= ~kFSEventStreamEventFlagItemRemoved;
			else
				flags &= ~(kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemCloned);
		}

		if (flags & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemCloned))
		{
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Added, watcher->mWatchPath.c_str(),relativeFilePath.c_str(), NULL);
			if (!relativeDirectoryPath.empty())
				watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),relativeDirectoryPath.c_str(), NULL);
		}
		else if (flags & kFSEventStreamEventFlagItemRemoved)
		{
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Removed, watcher->mWatchPath.c_str(),relativeFilePath.c_str(), NULL);
			if (!relativeDirectoryPath.empty())
				watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),relativeDirectoryPath.c_str(), NULL);
		}

		if (flags & (
			kFSEventStreamEventFlagItemInodeMetaMod |
			kFSEventStreamEventFlagItemModified |
			kFSEventStreamEventFlagItemXattrMod |
			kFSEventStreamEventFlagItemFinderInfoMod |
			kFSEventStreamEventFlagItemChangeOwner))
		{
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),relativeFilePath.c_str(), NULL);
		}
	}

	// Moved to/from outside
	for (const auto& kv : renamedItems)
	{
		bool exists = CheckPathExists(kv.mValue.c_str());
		String relativeFilePath = GetRelativePath(kv.mValue, watcher->mAbsPath);
		String relativeDirectoryPath = GetFileDir(relativeFilePath);
		watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, (exists ? BfpFileChangeKind_Added : BfpFileChangeKind_Removed), watcher->mWatchPath.c_str(),relativeFilePath.c_str(), NULL);
		if (!relativeDirectoryPath.empty())
			watcher->mDirectoryChangeFunc(watcher, watcher->mUserData, BfpFileChangeKind_Modified, watcher->mWatchPath.c_str(),relativeDirectoryPath.c_str(), NULL);
	}

}

class FsEventFileWatchManager : public FileWatchManager
{
public:
	CritSect				mCritSect;
	bool					mInitialized;

public:
	FsEventFileWatchManager()
	{
		mInitialized = false;
	}

    bool Init() override;

    void Shutdown() override;

    BfpFileWatcher* WatchDirectory(const char *path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void *userData, BfpFileResult *outResult) override;

    void Remove(BfpFileWatcher *watcher) override;

};

bool FsEventFileWatchManager::Init()
{
	AutoCrit autoCrit(mCritSect);

	gCoreFoundationLib = dlopen("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", RTLD_LAZY);
	gCoreServicesLib = dlopen("/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices", RTLD_LAZY);
	if ((gCoreFoundationLib == NULL) || (gCoreServicesLib == NULL) )
	{
		return false;
	}

	bool symbolsLoaded = true;

#define BF_DP_GET_SYM(name) (symbolsLoaded &= (bf_##name = (decltype(bf_##name))dlsym(RTLD_DEFAULT, #name)) != NULL)
	BF_DP_GET_SYM(dispatch_queue_create);
	BF_DP_GET_SYM(dispatch_async_f);
	BF_DP_GET_SYM(dispatch_release);
#undef BF_DP_GET_SYM

#define BF_CF_GET_SYM(name) (symbolsLoaded &= (bf_##name = (decltype(bf_##name))dlsym(gCoreFoundationLib, #name)) != NULL)

	// Resolve CoreFoundation functions
	BF_CF_GET_SYM(CFArrayGetValueAtIndex);
	BF_CF_GET_SYM(CFDictionaryGetValue);
	BF_CF_GET_SYM(CFStringGetCStringPtr);
	BF_CF_GET_SYM(CFStringGetCString);
	BF_CF_GET_SYM(CFNumberGetValue);
	BF_CF_GET_SYM(CFStringCreateWithCString);
	BF_CF_GET_SYM(CFArrayCreate);
	BF_CF_GET_SYM(CFRelease);

#undef BF_CF_GET_SYM


#define BF_CS_GET_SYM(name) (symbolsLoaded &= (bf_##name = (decltype(bf_##name))dlsym(gCoreServicesLib, #name)) != NULL)

	// Resolve CoreServices functions
	BF_CS_GET_SYM(FSEventStreamCreate);
	BF_CS_GET_SYM(FSEventStreamSetDispatchQueue);
	BF_CS_GET_SYM(FSEventStreamStart);
	BF_CS_GET_SYM(FSEventStreamStop);
	BF_CS_GET_SYM(FSEventStreamInvalidate);
	BF_CS_GET_SYM(FSEventStreamRelease);

#undef BF_CS_GET_SYM

	if (!symbolsLoaded)
	{
		return false;
	}

	// Create constants, these are not exported so cannot resolve from library

#define BF_CREATE_CONST_STR(name, value) (symbolsLoaded &= (bf_##name = bf_CFStringCreateWithCString(NULL, value, kCFStringEncodingUTF8)) != NULL)

	BF_CREATE_CONST_STR(kFSEventStreamEventExtendedDataPathKey, "path");
	BF_CREATE_CONST_STR(kFSEventStreamEventExtendedFileIDKey, "fileID");

#undef BF_CREATE_CONST_STR

	if (!symbolsLoaded)
		return false;

	mInitialized = true;
	return true;
}

void FsEventFileWatchManager::Shutdown()
{
	AutoCrit autoCrit(mCritSect);
	mInitialized = false;

	// Leaking of constants / dlopen symbols is intentional
	// because we don't wait on removed watchers
}

BfpFileWatcher* FsEventFileWatchManager::WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult)
{
	{
		AutoCrit autoCrit(mCritSect);

		if (!mInitialized)
		{
			OUTRESULT(BfpFileResult_UnknownError);
			return NULL;
		}
	}

	String watchPath;
	// Make watch path lexically absolute, so it won't change when working directory changes
	if (path[0] != '/')
	{
		char* cwdPtr = getcwd(NULL, 0);
		if (cwdPtr)
		{
			String cwdPath = String::MakeRef(cwdPtr);
			watchPath = GetAbsPath(path, cwdPath);
			free(cwdPtr);
		}
		else
		{
			OUTRESULT(BfpFileResult_NotFound);
			return NULL;
		}
	}
	else
	{
		watchPath = GetAbsPath(path, "/");
	}

	char* absPath = realpath(watchPath.c_str(), NULL);
	if (absPath == NULL)
	{
		OUTRESULT(BfpFileResult_NotFound);
		return NULL;
	}
	defer ( free(absPath) );

	CFStringRef pathString = bf_CFStringCreateWithCString(NULL, absPath, kCFStringEncodingUTF8);
	if (pathString == NULL)
	{
		OUTRESULT(BfpFileResult_UnknownError);
		return NULL;
	}
	defer( bf_CFRelease(pathString) );

    CFArrayRef pathsToWatch = bf_CFArrayCreate(NULL, (const void **)&pathString, 1, NULL);
	if (pathsToWatch == NULL)
	{
		OUTRESULT(BfpFileResult_UnknownError);
		return NULL;
	}
	defer( bf_CFRelease(pathsToWatch) );

	BfpFileWatcher* pWatcher = new BfpFileWatcher();
	pWatcher->mWatchPath = std::move(watchPath);
	pWatcher->mAbsPath = absPath;
	pWatcher->mDirectoryChangeFunc = callback;
	pWatcher->mUserData = userData;
	pWatcher->mIncludeSubdirs = (flags & BfpFileWatcherFlag_IncludeSubdirectories) != 0;
	pWatcher->mDispatchQueue = bf_dispatch_queue_create("com.beef.filewatcher", NULL);
	if (pWatcher->mDispatchQueue == NULL)
	{
		delete pWatcher;
		OUTRESULT(BfpFileResult_UnknownError);
		return NULL;
	}

    FSEventStreamContext context = { };
	context.info = pWatcher;
    pWatcher->mStreamRef = bf_FSEventStreamCreate(
        NULL,
        &FSEventsCallback,
        &context,
        pathsToWatch,
        kFSEventStreamEventIdSinceNow,
        0.1,
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot | kFSEventStreamCreateFlagNoDefer |
        kFSEventStreamCreateFlagUseExtendedData | kFSEventStreamCreateFlagUseCFTypes
    );

	if (pWatcher->mStreamRef == NULL)
	{
		delete pWatcher;
		OUTRESULT(BfpFileResult_UnknownError);
		return NULL;
	}

	bf_FSEventStreamSetDispatchQueue(pWatcher->mStreamRef, pWatcher->mDispatchQueue);
    if (!bf_FSEventStreamStart(pWatcher->mStreamRef))
    {
	    delete pWatcher;
    	OUTRESULT(BfpFileResult_UnknownError);
    	return NULL;
    }

	pWatcher->mStarted = true;

	OUTRESULT(BfpFileResult_Ok);
	return pWatcher;
}


void FsEventFileWatchManager::Remove(BfpFileWatcher* watcher)
{
	if (watcher->mShuttingDown.exchange(true))
	{
		return;
	}

	bf_dispatch_async_f(watcher->mDispatchQueue, watcher, [](void* param) {
		BfpFileWatcher* watcher = (BfpFileWatcher*)param;
		watcher->ReleaseStream();
	});

	// Wait for callbacks to exit
	{
		AutoCrit autoCrit(watcher->mCritSect);
	}

	bf_dispatch_async_f(watcher->mDispatchQueue, watcher, [](void* param) {
		BfpFileWatcher* watcher = (BfpFileWatcher*)param;
		delete watcher;
	});
}

FileWatchManager* FileWatchManager::Allocate()
{
    return new FsEventFileWatchManager();
}

#endif // BFP_HAS_FILEWATCHER
