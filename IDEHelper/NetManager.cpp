#include "NetManager.h"
#include "DebugManager.h"
#include "Compiler/BfSystem.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

#ifdef BF_CURL
#define CURL_STATICLIB
#include "curl/curl.h"
#include "curl/multi.h"

static int TransferInfoCallback(void* userp,
	curl_off_t dltotal, curl_off_t dlnow,
	curl_off_t ultotal, curl_off_t ulnow)
{
	NetRequest* netRequest = (NetRequest*)userp;
	if (netRequest->mCancelling)
		return 1;
	return 0;
}

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
	NetRequest* netRequest = (NetRequest*)userp;

	long response_code = 0;
	curl_easy_getinfo(netRequest->mCURL, CURLINFO_RESPONSE_CODE, &response_code);

	if (netRequest->mCancelling)
	{
		netRequest->mFailed = true;
		return 0;
	}

	if (response_code != 200)
		return 0;

	if (netRequest->mFailed)
		return 0;

	if (!netRequest->mOutFile.IsOpen())
	{
		RecursiveCreateDirectory(GetFileDir(netRequest->mOutPath));
		if (!netRequest->mOutFile.Open(netRequest->mOutTempPath, "wb"))
		{
			netRequest->Fail(StrFormat("Failed to create file '%s'", netRequest->mOutTempPath.c_str()));
			return 0;
		}
	}

	uint32 tickNow = BFTickCount();

	if (tickNow - netRequest->mLastUpdateTick >= 500)
	{
		curl_off_t downloadSize = 0;
		curl_easy_getinfo(netRequest->mCURL, CURLINFO_SIZE_DOWNLOAD_T, &downloadSize);
		curl_off_t length = 0;
		curl_easy_getinfo(netRequest->mCURL, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);

		if (netRequest->mShowTracking)
		{
			String msg = StrFormat("symsrv Getting '%s' - %dk", netRequest->mURL.c_str(), (int)(downloadSize / 1024));

			if (length > 0)
			{
				msg += StrFormat(" (%d%%)", (int)(downloadSize * 100 / length));
			}

			netRequest->mNetManager->mDebugManager->OutputRawMessage(msg);
		}

		netRequest->mLastUpdateTick = tickNow;
	}

	size_t realsize = size * nmemb;
	netRequest->mOutFile.Write(contents, (int)realsize);
	return realsize;
}

void NetRequest::Cleanup()
{
	if (mCURLMulti != NULL)
	{
		curl_multi_remove_handle(mCURLMulti, mCURL);		
	}

	if (mCURL != NULL)
		curl_easy_cleanup(mCURL);

	if (mCURLMulti != NULL)
	{
		curl_multi_cleanup(mCURLMulti);
	}
}

void NetRequest::DoTransfer()
{
	if (mCancelling)
		return;

// 	{
// 		mFailed = true;
// 		return;
// 	}

	BfLogDbg("NetManager starting get on %s\n", mURL.c_str());
	mNetManager->mDebugManager->OutputRawMessage(StrFormat("msgLo Getting '%s'\n", mURL.c_str()));

	mOutTempPath = mOutPath + "__partial";	
	
	mCURLMulti = curl_multi_init();

	mCURL = curl_easy_init();	

	if (mShowTracking)
	{		
		mNetManager->mDebugManager->OutputRawMessage(StrFormat("symsrv Getting '%s'", mURL.c_str()));		
	}

	//OutputDebugStrF("Getting '%s'\n", mURL.c_str());

	curl_easy_setopt(mCURL, CURLOPT_URL, mURL.c_str());
	curl_easy_setopt(mCURL, CURLOPT_WRITEDATA, (void*)this);
	curl_easy_setopt(mCURL, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(mCURL, CURLOPT_XFERINFODATA, (void*)this);
	curl_easy_setopt(mCURL, CURLOPT_XFERINFOFUNCTION, TransferInfoCallback);
	curl_easy_setopt(mCURL, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(mCURL, CURLOPT_NOPROGRESS, 0L);
	//auto result = curl_easy_perform(mCURL);

	CURLMcode mcode = curl_multi_add_handle(mCURLMulti, mCURL);
	if (mcode != CURLM_OK)
	{
		mFailed = true;
		return;
	}
	
	while (true)
	{ 
		int activeCount = 0;
		curl_multi_perform(mCURLMulti, &activeCount);
		if (activeCount == 0)
			break;

		int waitRet = 0;
		curl_multi_wait(mCURLMulti, NULL, 0, 20, &waitRet);		

		if (mCancelling)
		{
			mFailed = true;
			return;
		}
	}

// 	if (result != CURLE_OK)
// 	{
// 		mFailed = true;
// 		return;
// 	}

	long response_code = 0;
	curl_easy_getinfo(mCURL, CURLINFO_RESPONSE_CODE, &response_code);
	mNetManager->mDebugManager->OutputRawMessage(StrFormat("msgLo Result for '%s': %d\n", mURL.c_str(), response_code));
	if (response_code != 200)
	{		
		mOutFile.Close();
		// Bad result
		mFailed = true;
		return;
	}

	BfLogDbg("NetManager successfully completed %s\n", mURL.c_str());

	if (mCancelOnSuccess != NULL)
		mNetManager->Cancel(mCancelOnSuccess);

	if (!mOutFile.IsOpen())
	{		
		mFailed = true;
		return; // No data
	}
	mOutFile.Close();

	BfpFile_Delete(mOutPath.c_str(), NULL);
	BfpFileResult renameResult;
	BfpFile_Rename(mOutTempPath.c_str(), mOutPath.c_str(), &renameResult);

	if (renameResult != BfpFileResult_Ok)
	{		
		mFailed = true;		
	}
}

void NetRequest::Perform()
{	
	DoTransfer();	
}

#elif defined BF_PLATFORM_WINDOWS

#include <windows.h>
#include <wininet.h>
#include <stdio.h>

#pragma comment (lib, "wininet.lib")

void NetRequest::Perform()
{
	if (mCancelling)
		return;

	// 	{
	// 		mFailed = true;
	// 		return;
	// 	}

	BfLogDbg("NetManager starting get on %s\n", mURL.c_str());

	String protoName;
	String serverName;
	String objectName;

	int colonPos = (int)mURL.IndexOf("://");
	if (colonPos == -1)
	{
		Fail("Invalid URL");
		return;
	}
	
	protoName = mURL.Substring(0, colonPos);
	serverName = mURL.Substring(colonPos + 3);
		
	int slashPos = (int)serverName.IndexOf('/');
	if (slashPos != -1)
	{
		objectName = serverName.Substring(slashPos);
		serverName.RemoveToEnd(slashPos);
	}	

	mOutTempPath = mOutPath + "__partial";

	HINTERNET hSession = NULL;
	HINTERNET hConnect = NULL;
	HINTERNET hHttpFile = NULL;

	defer
	{
		if (hHttpFile != NULL)
			InternetCloseHandle(hHttpFile);
		if (hConnect != NULL)
			InternetCloseHandle(hConnect);
		if (hSession != NULL)
			InternetCloseHandle(hSession);

		if (mOutFile.IsOpen())
			mOutFile.Close();
	};	
	
	bool isHttp = protoName.Equals("http", StringImpl::CompareKind_OrdinalIgnoreCase);
	bool isHttps = protoName.Equals("https", StringImpl::CompareKind_OrdinalIgnoreCase);
	if ((!isHttp) && (!isHttps))
	{
		Fail("Invalid URL protocol");
		return;
	}

	hSession = InternetOpenW(
		L"Mozilla/5.0",
		INTERNET_OPEN_TYPE_PRECONFIG,
		NULL,
		NULL,
		0);

	hConnect = InternetConnectW(
		hSession,
		UTF8Decode(serverName).c_str(),
		isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT,
		NULL,
		NULL,
		INTERNET_SERVICE_HTTP,
		0,
		0);

	DWORD httpOpenFlags = INTERNET_FLAG_RELOAD;
	if (isHttps)
		httpOpenFlags |= INTERNET_FLAG_SECURE;
	hHttpFile = HttpOpenRequestW(
		hConnect,
		L"GET",
		UTF8Decode(objectName).c_str(),
		NULL,
		NULL,
		NULL,
		httpOpenFlags,
		0);

	if (!HttpSendRequestW(hHttpFile, NULL, 0, 0, 0))
	{
		int err = GetLastError();
		OutputDebugStrF("Failed: %s %X\n", mURL.c_str(), err);;
		Fail("Failed to send request");
		return;
	}

	DWORD statusCode = 0;
	DWORD length = sizeof(DWORD);
	HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &length, NULL);

	if (statusCode != 200)
	{
		Fail("Invalid HTTP response");
		return;
	}

	if (mShowTracking)
	{		
		mNetManager->mDebugManager->OutputRawMessage(StrFormat("symsrv Getting '%s'", mURL.c_str()));
	}

	uint8 buffer[4096];	
	while (true)
	{
		DWORD dwBytesRead = 0;
		BOOL bRead = InternetReadFile(hHttpFile, buffer, 4096, &dwBytesRead);
		if (dwBytesRead == 0) 
			break;
		if (!bRead) 
		{
			//printf("InternetReadFile error : <%lu>\n", GetLastError());
			Fail("Failed to receive");
			return;
		}		

		if (!mOutFile.IsOpen())
		{
			RecursiveCreateDirectory(GetFileDir(mOutPath));
			if (!mOutFile.Open(mOutTempPath, "wb"))
			{
				Fail(StrFormat("Failed to create file '%s'", mOutTempPath.c_str()));
				return;
			}
		}
		mOutFile.Write(buffer, (int)dwBytesRead);
	}	

	BfLogDbg("NetManager successfully completed %s\n", mURL.c_str());

	if (mCancelOnSuccess != NULL)
		mNetManager->Cancel(mCancelOnSuccess);

	if (!mOutFile.IsOpen())
	{
		mFailed = true;
		return; // No data
	}
	mOutFile.Close();

	BfpFile_Delete(mOutPath.c_str(), NULL);
	BfpFileResult renameResult;
	BfpFile_Rename(mOutTempPath.c_str(), mOutPath.c_str(), &renameResult);

	if (renameResult != BfpFileResult_Ok)
		mFailed = true;
}

void NetRequest::Cleanup()
{

}

#else

void NetRequest::Perform()
{
	mFailed = true;
}

void NetRequest::Cleanup()
{

}

#endif

NetRequest::~NetRequest()
{
	Cleanup();
	// This is synchronized
	if (mResult != NULL)
	{
		mResult->mFailed = mFailed;
		mResult->mCurRequest = NULL;
		if (mResult->mDoneEvent != NULL)
		{
			mResult->mDoneEvent->Set(true);
			BF_ASSERT(!mResult->mRemoved);
		}
		if (mResult->mRemoved)
			delete mResult;
	}

	mNetManager->mRequestDoneEvent.Set();	
}

void NetRequest::Fail(const StringImpl& error)
{
	if (mFailed)
		return;

	mError = error;
	mFailed = true;
}

bool NetRequest::Cancel()
{
	mCancelling = true;
	return true;
}

void NetRequest::ShowTracking()
{	
	//mNetManager->mDebugManager->OutputMessage(StrFormat("Getting '%s'\n", mURL.c_str()));
	mNetManager->mDebugManager->OutputRawMessage(StrFormat("symsrv Getting '%s'", mURL.c_str()));
	mShowTracking = true;
}

void NetManagerThread()
{

}

NetManager::NetManager() : mThreadPool(8, 1*1024*1024)
{
	mWaitingResult = NULL;
	mWaitingRequest = NULL;
}

NetManager::~NetManager()
{
	mThreadPool.Shutdown();

	for (auto kv : mCachedResults)
	{
		delete kv.mValue;
	}	
}

NetRequest* NetManager::CreateGetRequest(const StringImpl& url, const StringImpl& destPath)
{
	AutoCrit autoCrit(mThreadPool.mCritSect);

	NetRequest* netRequest = new NetRequest();
	netRequest->mNetManager = this;
	netRequest->mURL = url;
	netRequest->mOutPath = destPath;

	NetResult* netResult = new NetResult();
	netResult->mURL = url;
	netResult->mOutPath = destPath;
	netResult->mFailed = false;
	netResult->mCurRequest = netRequest;

	NetResult** netResultPtr;
	if (mCachedResults.TryAdd(url, NULL, &netResultPtr))
	{
		*netResultPtr = netResult;
	}
	else
	{
		mOldResults.Add(*netResultPtr);
		*netResultPtr = netResult;
	}

	netRequest->mResult = netResult;

	return netRequest;
}

NetResult* NetManager::QueueGet(const StringImpl& url, const StringImpl& destPath)
{
	BfLogDbg("NetManager queueing %s\n", url.c_str());
	
	auto netRequest = CreateGetRequest(url, destPath);
	auto netResult = netRequest->mResult;
	mThreadPool.AddJob(netRequest);
	return netResult;
}

bool NetManager::Get(const StringImpl& url, const StringImpl& destPath)
{	
	NetRequest* netRequest = NULL;
	int waitCount = 0;
	while (true)
	{
		// Check cached
		{
			AutoCrit autoCrit(mThreadPool.mCritSect);

			mWaitingResult = NULL;
			NetResult* netResult;
			if (mCachedResults.TryGetValue(url, &netResult))			
			{
				if (netResult->mCurRequest == NULL)
				{
					BfLogDbg("NetManager::Get using cached result for %s: %d. WaitCount: %d \n", url.c_str(), !netResult->mFailed, waitCount);
					return (!netResult->mFailed) && (FileExists(netResult->mOutPath));
				}
				else if (!netResult->mCurRequest->mShowTracking) // Is done?
				{	
					if (!netResult->mCurRequest->mProcessing)
					{
						BfLogDbg("NetManager::Get pulling queued request into current thread %s\n", url.c_str());
						netRequest = netResult->mCurRequest;
						bool didRemove = mThreadPool.mJobs.Remove(netRequest);
						BF_ASSERT(didRemove);
						break;
					}

					mWaitingResult = netResult;					
					netResult->mCurRequest->ShowTracking();
				}				
			}
			else
			{
				netRequest = CreateGetRequest(url, destPath);
				break;
			}
		}

		waitCount++;
		mRequestDoneEvent.WaitFor();
	}

	// Perform this in the requesting thread
	{
		AutoCrit autoCrit(mThreadPool.mCritSect);
		mWaitingRequest = netRequest;
	}

	netRequest->mShowTracking = true;
	netRequest->Perform();

	AutoCrit autoCrit(mThreadPool.mCritSect);
	mWaitingRequest = NULL;
	auto netResult = netRequest->mResult;
	delete netRequest;

	BfLogDbg("NetManager::Get requested %s: %d\n", url.c_str(), !netResult->mFailed);
	mDebugManager->OutputRawMessage(StrFormat("msgLo Result for '%s': %d\n", url.c_str(), !netResult->mFailed));

	return (!netResult->mFailed) && (FileExists(netResult->mOutPath));
}

void NetManager::CancelAll()
{
	AutoCrit autoCrit(mThreadPool.mCritSect);
	if (mWaitingRequest != NULL)
		mWaitingRequest->Cancel();
	mThreadPool.CancelAll();	
}

void NetManager::Clear()
{
	AutoCrit autoCrit(mThreadPool.mCritSect);		
	BF_ASSERT(mWaitingResult == NULL); // The debugger thread shouldn't be waiting on anything, it should be detached at this point

	for (auto job : mThreadPool.mJobs)
	{
		auto netRequest = (NetRequest*)job;
		netRequest->Cancel();
	}

	for (auto kv : mCachedResults)
	{
		NetResult* netResult = kv.mValue;
		if (netResult->mCurRequest != NULL)
			netResult->mRemoved = true;
		else
			delete netResult;
	}
	mCachedResults.Clear();

	for (auto netResult : mOldResults)
	{
		delete netResult;
	}
	mOldResults.Clear();
}

void NetManager::CancelCurrent()
{
	AutoCrit autoCrit(mThreadPool.mCritSect);
	if (mWaitingRequest != NULL)
		mWaitingRequest->Cancel();
	else if ((mWaitingResult != NULL) && (mWaitingResult->mCurRequest != NULL))
		mWaitingResult->mCurRequest->Cancel();
}

void NetManager::Cancel(NetResult* netResult)
{
	AutoCrit autoCrit(mThreadPool.mCritSect);
	BfLogDbg("NetManager cancel %s\n", netResult->mURL.c_str());
	if (netResult->mCurRequest != NULL)
		netResult->mCurRequest->Cancel();
}

void NetManager::SetCancelOnSuccess(NetResult* dependentResult, NetResult* cancelOnSucess)
{
	AutoCrit autoCrit(mThreadPool.mCritSect);
	if (dependentResult->mCurRequest != NULL)
	{		
		dependentResult->mCurRequest->mCancelOnSuccess = cancelOnSucess;
	}
}
