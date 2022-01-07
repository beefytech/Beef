#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/Deque.h"
#include "BeefySysLib/util/ThreadPool.h"
#include "BeefySysLib/util/Dictionary.h"

NS_BF_BEGIN

#ifdef BF_PLATFORM_WINDOWS
#define BF_CURL
#endif

class DebugManager;
class NetManager;
class NetResult;

#ifdef BF_CURL
typedef void CURL;
typedef void CURLM;
#endif

class NetRequest : public ThreadPool::Job
{
public:
	NetManager* mNetManager;

	String mURL;
	String mOutPath;
	String mOutTempPath;
	SysFileStream mOutFile;
#ifdef BF_CURL
	CURL* mCURL;
	CURLM* mCURLMulti;
#else
#endif
	volatile bool mCancelling;
	bool mFailed;
	String mError;
	uint32 mLastUpdateTick;
	bool mShowTracking;
	NetResult* mResult;
	NetResult* mCancelOnSuccess;

	NetRequest()
	{
		mLastUpdateTick = 0;
#ifdef BF_CURL
		mCURL = NULL;
		mCURLMulti = NULL;
#else
#endif
		mCancelling = false;
		mFailed = false;		
		mShowTracking = false;
		mResult = NULL;
		mCancelOnSuccess = NULL;		
	}
	~NetRequest();

	void DoTransfer();

	void Cleanup();
	void Fail(const StringImpl& error);
	bool Cancel() override;	
	void Perform() override;
	void ShowTracking();
};

class NetResult
{
public:
	String mURL;
	String mOutPath;
	bool mFailed;
	String mError;
	NetRequest* mCurRequest;
	bool mRemoved;
	SyncEvent* mDoneEvent;

	NetResult()
	{
		mFailed = false;
		mCurRequest = NULL;
		mRemoved = false;
		mDoneEvent = NULL;
	}

	~NetResult()
	{
		delete mDoneEvent;
	}
};

class NetManager
{
public:
	Dictionary<String, NetResult*> mCachedResults;

	ThreadPool mThreadPool;
	DebugManager* mDebugManager;
	Deque<NetRequest*> mRequests;
	Array<NetResult*> mOldResults;
	SyncEvent mRequestDoneEvent;
	NetResult* mWaitingResult;
	NetRequest* mWaitingRequest;

public:
	NetManager();
	~NetManager();

	NetRequest* CreateGetRequest(const StringImpl& url, const StringImpl& destPath, bool useCache);
	NetResult* QueueGet(const StringImpl& url, const StringImpl& destPath, bool useCache);
	bool Get(const StringImpl& url, const StringImpl& destPath);
	
	void CancelAll();
	void Clear();
	void CancelCurrent();
	void Cancel(NetResult* netResult);
	void SetCancelOnSuccess(NetResult* dependentResult, NetResult* cancelOnSucess);
};

NS_BF_END
