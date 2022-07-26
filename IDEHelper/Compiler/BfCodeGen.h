#pragma once

#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BfAst.h"
#include "BfSystem.h"

namespace llvm
{
	class Module;
	class LLVMContext;
	class TargetMachine;
}

NS_BF_BEGIN

class BfModule;

enum BfCodeGenResultType
{
	BfCodeGenResult_NotDone,
	BfCodeGenResult_Done,
	BfCodeGenResult_DoneCached,
	BfCodeGenResult_Failed,
	BfCodeGenResult_Aborted,
};
struct BfCodeGenResult
{
	static const int kErrorMsgBufSize = 1024;

	BfCodeGenResultType mType;
	char mErrorMsgBuf[kErrorMsgBufSize]; // not using String etc. since can be modified cross-process
	int mErrorMsgBufLen;

	BfCodeGenResult() { mType = BfCodeGenResult_NotDone; mErrorMsgBufLen = 0; }
};

class BfCodeGenErrorEntry
{
public:
	BfModule* mSrcModule;
	String mOutFileName;
	Array<String> mErrorMessages;
};

class BfCodeGenRequest
{
public:
	BfModule* mSrcModule;
	BfCodeGenOptions mOptions;
	BfCodeGenResult mResult;
	Array<uint8> mOutBuffer;
	BfSizedArray<uint8> mData;
	String mOutFileName;

	BfCodeGenResult* mExternResultPtr;

public:
	BfCodeGenRequest()
	{
		mSrcModule = NULL;
		mResult.mType = BfCodeGenResult_NotDone;
		mResult.mErrorMsgBufLen = 0;
		mExternResultPtr = NULL;
	}

	~BfCodeGenRequest()
	{
	}

	void DbgSaveData();
};

class BfCodeGen;

class BfCodeGenThread
{
public:
	BfCodeGen* mCodeGen;
	int mThreadIdx;

	//std::vector<BfCodeGenRequest*> mRequests;
	volatile bool mShuttingDown;
	volatile bool mRunning;

public:
	bool RawWriteObjectFile(llvm::Module* module, const StringImpl& outFileName, const BfCodeGenOptions& codeGenOptions);

public:
	BfCodeGenThread();
	~BfCodeGenThread();

	void RunLoop();
	void Shutdown();

	void Start();
};

class BfCodeGenFileData
{
public:
	Val128 mIRHash; // Equal when exact IR bits are equal
	Val128 mIROrderedHash; // Equal when isomorphic (when alphabetically reordered functions hash equally)
	bool mLastWasObjectWrite;
};

class BfCodeGenDirectoryData
{
public:
	BfCodeGen* mCodeGen;
	Dictionary<String, BfCodeGenFileData> mFileMap;
	Dictionary<String, String> mBuildSettings;
	String mDirectoryName;
	bool mDirty;
	bool mVerified;
	int64 mFileTime;
	String mError;
	bool mFileFailed;

public:
	BfCodeGenDirectoryData()
	{
		mCodeGen = NULL;
		mDirty = true;
		mVerified = true;
		mFileTime = 0;
		mFileFailed = false;
	}

	String GetDataFileName();
	void Read();
	void Write();
	void Verify();
	void Clear();
	bool CheckCache(const StringImpl& fileName, Val128 hash, Val128* outOrderedHash, bool disallowObjectWrite);
	void SetHash(const StringImpl& fileName, Val128 hash, Val128 orderedHash, bool isObjectWrite);
	void ClearHash(const StringImpl& fileName);
	void FileFailed();
	String GetValue(const StringImpl& key);
	void SetValue(const StringImpl& key, const StringImpl& value);
};

class BfCodeGenFileEntry
{
public:
	String mFileName;
	BfModule* mModule;
	BfProject* mProject;
	bool mWasCached;
	bool mModuleHotReferenced; // Only applicable for hot loading
};

class BfCodeGen
{
public:
	typedef int (BF_CALLTYPE* ClearCacheFunc)();
	typedef int (BF_CALLTYPE* GetVersionFunc)();
	typedef void (BF_CALLTYPE* KillFunc)();
	typedef void (BF_CALLTYPE* CancelFunc)();
	typedef void (BF_CALLTYPE* FinishFunc)();
	typedef void (BF_CALLTYPE* GenerateObjFunc)(const void* ptr, int size, const char* outFileName, BfCodeGenResult* resultPtr, const BfCodeGenOptions& options);

public:
    BfpDynLib* mReleaseModule;
	bool mAttemptedReleaseThunkLoad;
	bool mIsUsingReleaseThunk;
	ClearCacheFunc mClearCacheFunc;
	GetVersionFunc mGetVersionFunc;
	KillFunc mKillFunc;
	CancelFunc mCancelFunc;
	FinishFunc mFinishFunc;
	GenerateObjFunc mGenerateObjFunc;

	Val128 mBackendHash;
	int mMaxThreadCount;
	CritSect mThreadsCritSect;
	Array<BfCodeGenThread*> mThreads;
	Array<BfCodeGenThread*> mOldThreads;
	Deque<BfCodeGenRequest*> mRequests;
	CritSect mPendingRequestCritSect;
	Deque<BfCodeGenRequest*> mPendingRequests;
	SyncEvent mRequestEvent;
	int mRequestIdx;
	SyncEvent mDoneEvent;

	int mQueuedCount;
	int mCompletionCount;

	Array<BfCodeGenErrorEntry> mFailedRequests;
	Array<BfCodeGenFileEntry> mCodeGenFiles;

	CritSect mCacheCritSect;
	bool mDisableCacheReads;
	Dictionary<String, BfCodeGenDirectoryData*> mDirectoryCache;

public:
	void SetMaxThreads(int maxThreads);
	void BindReleaseThunks();
	void ClearResults();
	void DoWriteObjectFile(BfCodeGenRequest* codeGenRequest, const void* ptr, int size, const StringImpl& outFileName, BfCodeGenResult* externResultPtr);
	bool ExternWriteObjectFile(BfCodeGenRequest* codeGenRequest);
	void ClearOldThreads(bool waitForThread);
	void ClearBuildCache();
	void RequestComplete(BfCodeGenRequest* request);
	void ProcessErrors(BfPassInstance* passInstance, bool canceled);
	BfCodeGenDirectoryData* GetDirCache(const StringImpl& cacheDir);

public:
	BfCodeGen();
	~BfCodeGen();

	void ResetStats();
	void UpdateStats();
	void WriteObjectFile(BfModule* module, const StringImpl& outFileName, const BfCodeGenOptions& options);
	String GetBuildValue(const StringImpl& buildDir, const StringImpl& key);
	void SetBuildValue(const StringImpl& buildDir, const StringImpl& key, const StringImpl& value);
	void WriteBuildCache(const StringImpl& buildDir);
	void Cancel();
	bool Finish();
};

NS_BF_END
