#pragma once

#ifndef BP_NOINC
#include "../Common.h"
#include "CritSect.h"
#endif

#include "Dictionary.h"
#include "TLSingleton.h"

#ifdef BF_PLATFORM_WINDOWS
#include <winsock.h>
#else
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
typedef int SOCKET;
#endif

enum BpConnectState
{
	BpConnectState_NotConnected,
	BpConnectState_Connecting,
	BpConnectState_Connected,
	BpConnectState_Failed
};

#ifndef BP_DISABLED

NS_BF_BEGIN

class Buffer
{
public:
	uint8* mPtr;
	int mBufSize;
	int mDataSize;

public:	
	Buffer();
	~Buffer();
	uint8* Alloc(int size);	
	void Free();
	void Clear();
};

class CircularBuffer
{
public:
	// Either points to an internal segment of the CircularBuffer, or if the range is discontinuous then 
	//  it presents a temporary pointer that is later committed
	class View
	{
	public:
		uint8* mPtr;			
		int mSrcIdx;
		int mSrcSize;
		uint8* mTempBuf;
		int mTempBufSize;

		CircularBuffer* mCircularBuffer;

	public:
		View();
		~View();

		void Commit(int size = -1);
	};

public:
	uint8* mBuffer;	
	int mTail;
	int mDataSize;
	int mBufSize;

public:
	CircularBuffer();
	~CircularBuffer();

	void Clear();
	void Resize(int newSize);
	void GrowReserve(int addSize);
	void Grow(int addSize);
	void GrowFront(int addSize);
	int GetSize();
	void MapView(int idx, int len, View& view);
	void Read(void* ptr, int idx, int len);
	void Write(void* ptr, int idx, int len);	

	void RemoveFront(int len);	
};

class BpContext
{

};

enum BpResult
{
	BpResult_Ok = 0,
	BpResult_InternalError,
	BpResult_AlreadyInitialized
};

#define BP_CHUNKSIZE 4096
#define BP_NUMCHUNKS 8

#define BP_CLIENT_VERSION 2

enum BpCmd
{
	BpCmd_Init,
	BpCmd_Enter,	
	BpCmd_EnterDyn,
	BpCmd_Leave,
	BpCmd_LODSmallEntry,
	BpCmd_StrEntry,
	BpCmd_ThreadAdd,
	BpCmd_ThreadRemove,
	BpCmd_SetThread,	
	BpCmd_ThreadName,	
	BpCmd_Tick,
	BpCmd_PrevTick,
	BpCmd_KeepAlive,
	BpCmd_ClockInfo,
	BpCmd_StreamSplitInfo,
	BpCmd_Event,
	BpCmd_LODEvent,
	BpCmd_Cmd
};

class BpCmdTarget
{
public:	
	CritSect mCritSect;			
	Buffer mOutBuffer;	
	char* mThreadName;

	int mCurDynStrIdx;
	const char* mDynStrs[64];

	int mCurDepth;

public:
	BpCmdTarget();
	void Disable();
	// Note: returned "const char*" is actually an index into a temporary string table
	const char* DynamicString(const char* str);
	const char* ToStrPtr(const char* str);

	void Enter(const char* name);
	void Enter(const char* name, va_list vargs);
	void EnterF(const char* name, ...);	
	void Leave();	

	void Event(const char* name, const char* details);
};

class BpRootCmdTarget : public BpCmdTarget
{
public:
	void Init();
	void Tick();
	void KeepAlive();
	void AddThread(int threadId, BfpThreadId nativeThreadId);
};

class BpThreadInfo : public BpCmdTarget
{
public:	
    BfpThreadId mNativeThreadId;
	int mThreadId;	
	bool mReadyToSend;
	bool mHasTerminated;

public:
	BpThreadInfo();
	~BpThreadInfo();
	void SetThreadName(const char* name);
	void RemoveThread();
};

struct BpZoneName
{
public:
	int mIdx;
	int mSize;
};

class BpManager
{
public:	
#ifdef BF_PLATFORM_WINDOWS
	HANDLE mMutex;
	HANDLE mSharedMemoryFile;
#endif
	SOCKET mSocket;
	BfpThread* mThread;
	BfpThreadId mThreadId;
	String mServerName;
	String mSessionName;
	String mSessionID;
	BpRootCmdTarget mRootCmdTarget;
	BpConnectState mConnectState;
	SyncEvent mShutdownEvent;
	CritSect mCritSect;
	Array<BpThreadInfo*> mThreadInfos;
	String mClientName;
	TLSDtor mTLSDtor;
	bool mCollectData;
	bool mThreadRunning;	
	int64 mInitTimeStamp;
	uint mInitTickCount;
	int mPauseCount;
	int mInitCount;

	CircularBuffer mOutBuffer;
	int mOutBlockSizeLeft;
	int mCurThreadId;	

	Dictionary<const char*, BpZoneName> mZoneNameMap;

	BF_TLS_DECLSPEC static BpThreadInfo* sBpThreadInfo;
	static BpManager* sBpManager;

	int64 mCurTick;

protected:	
	bool Connect();	
	void FinishWorkThread();
	void ThreadProc();	
	static void BFP_CALLTYPE ThreadProcThunk(void* ptr);
	uint8* StartCmd(uint8 cmd, CircularBuffer::View& view, int maxLen);
	void EndCmd(CircularBuffer::View& view, uint8* ptr);
	void TrySendData();
	void LostConnection();
	virtual BpThreadInfo* SlowGetCurThreadInfo();

public:
	virtual void* AllocBytes(int size);
	virtual void FreeBytes(void*);

public:
	BpManager();
	~BpManager();

	void Clear();

	void SetClientName(const StringImpl& clientName);
	BpResult Init(const char* serverName, const char* sessionName);
	void RetryConnect();
	void Pause();
	void Unpause();
	void Shutdown();
	bool IsDisconnected();

	BpContext* CreateContext(const char* name);
	void CloseContext();
	void Tick();	

	static BpThreadInfo* GetCurThreadInfo();	
	static BpManager* Get();
};

NS_BF_END

class BpAutoZone
{
public:
	BpAutoZone(const char* name)
	{
		Beefy::BpManager::GetCurThreadInfo()->Enter(name);
	}

	~BpAutoZone()
	{
		Beefy::BpManager::sBpThreadInfo->Leave();
	}
};

class BpAutoZoneF
{
public:
	BpAutoZoneF(const char* name, ...)
	{
		va_list args;
		va_start(args, name);
		Beefy::BpManager::GetCurThreadInfo()->Enter(name, args);
	}

	~BpAutoZoneF()
	{
		Beefy::BpManager::sBpThreadInfo->Leave();
	}
};

#define BP_ENTER(zoneName) BpEnter(name)

#define BP_DYN_STR(str) BpDynStr(str)

// We purposely 'incorrectly' define BP_ZONE as a variadic macro so compilation will fail rather than warn if we attempt to add params
#define BP_TOKENPASTE(x, y) x ## y
#define BP_TOKENPASTE2(x, y) BP_TOKENPASTE(x, y)

#define BP_ZONE(zoneName, ...) BpAutoZone BP_TOKENPASTE2(bpZone_, __COUNTER__)(zoneName, ##__VA_ARGS__)
#define BP_ZONE_F(zoneName, ...) BpAutoZoneF BP_TOKENPASTE2(bpZone_, __COUNTER__)(zoneName, ##__VA_ARGS__)
#define BP_SHUTDOWN() BpShutdown()

#else

#define BP_ENTER(zoneName) do { } while (0)
#define BP_DYN_STR(str) str

#define BP_ZONE(zoneName, ...) do { } while (0)
#define BP_ZONE_F(zoneName, ...) do { } while (0)
#define BP_SHUTDOWN() do { } while (0)

#endif

#if (defined BP_DYNAMIC) || (!defined BF_PLATFORM_WINDOWS)
#define BP_EXPORT BF_EXPORT
#define BP_CALLTYPE BF_CALLTYPE
#else
#define BP_EXPORT
#define BP_CALLTYPE
#endif

BP_EXPORT void BP_CALLTYPE BpInit(const char* serverName, const char* sessionName);
BP_EXPORT void BP_CALLTYPE BpShutdown();
BP_EXPORT BpConnectState BP_CALLTYPE BpGetConnectState();
BP_EXPORT void BP_CALLTYPE BpRetryConnect();
BP_EXPORT void BP_CALLTYPE BpPause();
BP_EXPORT void BP_CALLTYPE BpUnpause();
BP_EXPORT void BP_CALLTYPE BpSetClientName(const char* clientName);
BP_EXPORT void BP_CALLTYPE BpSetThreadName(const char* threadName);
BP_EXPORT void BP_CALLTYPE BpEnter(const char* zoneName);
BP_EXPORT void BP_CALLTYPE BpEnterF(const char* zoneName, ...);
BP_EXPORT void BP_CALLTYPE BpLeave();
BP_EXPORT void BP_CALLTYPE BpFrameTick();
BP_EXPORT void BP_CALLTYPE BpEvent(const char* name, const char* details);
BP_EXPORT const char* BP_CALLTYPE BpDynStr(const char* str);
