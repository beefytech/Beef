#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/Deque.h"
#include "BeefySysLib/util/ThreadPool.h"
#include "BeefySysLib/util/Dictionary.h"

#ifdef BF_PLATFORM_WINDOWS

NS_BF_BEGIN
#define BF_CURL

class DebugManager;
class NetManager;
class NetResult;

#ifdef BF_CURL
typedef void CURL;
#endif

class NetRequest : public ThreadPool::Job
{
public:
	NetManager* mNetManager;

	String mURL;
	String mOutPath;
	String mOutTempPath;
	FileStream mOutFile;
#ifdef BF_CURL
	CURL* mCURL;
#else
#endif
	bool mCancelling;
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
#else
#endif
		mCancelling = false;
		mFailed = false;		
		mShowTracking = false;
		mResult = NULL;
		mCancelOnSuccess = NULL;
	}
	~NetRequest();

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
	NetRequest* mCurRequest;	
	bool mRemoved;	

	NetResult()
	{
		mFailed = false;
		mCurRequest = NULL;
		mRemoved = false;
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

public:
	NetManager();
	~NetManager();

	NetRequest* CreateGetRequest(const StringImpl& url, const StringImpl& destPath);
	NetResult* QueueGet(const StringImpl& url, const StringImpl& destPath);
	bool Get(const StringImpl& url, const StringImpl& destPath);
	
	void CancelAll();
	void Clear();
	void CancelCurrent();
	void Cancel(NetResult* netResult);
	void SetCancelOnSuccess(NetResult* dependentResult, NetResult* cancelOnSucess);
};

NS_BF_END

#else

NS_BF_BEGIN

class NetManager
{
public:
	DebugManager* mDebugManager;

public:
	void CancelAll() {};
	void Clear() {};
};

NS_BF_END

#endif