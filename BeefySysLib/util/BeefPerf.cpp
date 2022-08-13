#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "BeefPerf.h"
#include "platform/PlatformHelper.h"

#ifndef BP_DISABLED

#ifndef BF_PLATFORM_WINDOWS
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netdb.h>
typedef fd_set FD_SET;
#define closesocket close
#endif

#ifdef BF_PLATFORM_MACOS
#include <sys/socket.h>
#include <mach/error.h>
#include <mach/mach.h>
#endif

#ifdef BF_PLATFORM_POSIX
#include <cerrno>
#endif

#pragma comment(lib,"wsock32.lib")

#pragma warning(disable:4996)

USING_NS_BF;

// All DLLs must use the same ABI version
#define BP_ABI_VERSION 3

BpManager* BpManager::sBpManager = NULL;
static bool gOwnsBpManager = false;
static BpThreadInfo sFakeThreadInfo;

struct BpManagerOwner
{
	BpManager* mOwnedManager;
	bool mDidShutdown;
	BpManagerOwner()
	{
		mOwnedManager = NULL;
		mDidShutdown = false;
	}
	~BpManagerOwner()
	{
		mDidShutdown = true;
		delete mOwnedManager;
	}
};
static BpManagerOwner gBpManagerOwner;

BF_TLS_DECLSPEC BpThreadInfo* Beefy::BpManager::sBpThreadInfo;

inline void EncodeSLEB128(uint8*& buf, int value)
{
	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);    
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		*(buf++) = curByte;                
	}
	while (hasMore);        
}

inline void EncodeSLEB128(uint8*& buf, int64_t value)
{
	bool hasMore;
	do
	{
		uint8 curByte = (uint8)(value & 0x7f);    
		value >>= 7;
		hasMore = !((((value == 0) && ((curByte & 0x40) == 0)) ||
			((value == -1) && ((curByte & 0x40) != 0))));
		if (hasMore)
			curByte |= 0x80;
		*(buf++) = curByte;
	}
	while (hasMore);        
}

inline void EncodeULEB32(uint8*& buf, uint64 value)
{
	do 
	{
		uint8 byteVal = value & 0x1f;
		value >>= 5;
		if (value != 0)
			byteVal |= 0x20; // Mark this byte to show that more bytes will follow
		*(buf++) = byteVal;
	} 
	while (value != 0);
}

//////////////////////////////////////////////////////////////////////////

Buffer::Buffer()
{
	mPtr = NULL;
	mBufSize = 0;
	mDataSize = 0;
}

Buffer::~Buffer()
{
	delete mPtr;
}

uint8* Buffer::Alloc(int size)
{
	if (mDataSize + size > mBufSize)
	{
		// Grow factor 50%
		int wantSize = max(mDataSize + size, mBufSize + mBufSize / 2);
		auto bpManager = BpManager::Get();
		//uint8* newPtr = new uint8[wantSize];
		uint8* newPtr = (uint8*)bpManager->AllocBytes(wantSize);
		memcpy(newPtr, mPtr, mDataSize);
		//delete mPtr;
		bpManager->FreeBytes(mPtr);
		mPtr = newPtr;
		mBufSize = wantSize;
	}
	uint8* ptr = mPtr + mDataSize;
	mDataSize += size;
	return ptr;
}

void Buffer::Free()
{
	//delete mPtr;
	BpManager::Get()->FreeBytes(mPtr);
	mPtr = NULL;
	mDataSize = 0;
	mBufSize = 0;
}

void Buffer::Clear()
{
	mDataSize = 0;
}

//////////////////////////////////////////////////////////////////////////

CircularBuffer::View::View()
{
	mPtr = NULL;
	mCircularBuffer = NULL;	
	mTempBuf = NULL;
	mTempBufSize = 0;
	mSrcIdx = 0;
	mSrcSize = 0;
}

CircularBuffer::View::~View()
{
	delete mTempBuf;
}

void CircularBuffer::View::Commit(int size)
{
	if (mPtr == mTempBuf)
	{
		if (size == -1)
			size = mSrcSize;
		else
			BF_ASSERT(size <= mSrcSize);
		mCircularBuffer->Write(mTempBuf, mSrcIdx, size);
	} 
	else if (size != -1)
		BF_ASSERT(size <= mSrcSize);
}

CircularBuffer::CircularBuffer()
{	
	mTail = 0;
	mBufSize = 0;
	mDataSize = 0;
	mBuffer = NULL;
}

CircularBuffer::~CircularBuffer()
{
	delete mBuffer;
}

void CircularBuffer::Clear()
{
	mTail = 0;
	mDataSize = 0;
}

void CircularBuffer::Resize(int newSize)
{	
	uint8* newBuffer = new uint8[newSize];
	Read(newBuffer, 0, mDataSize);

	delete mBuffer;
	mBuffer = newBuffer;
	mBufSize = newSize;
	mTail = 0;		
}

void CircularBuffer::GrowReserve(int addSize)
{	
	if (mDataSize + addSize <= mBufSize)
		return;
	Resize(max(mDataSize + addSize, mDataSize + mDataSize/2));
}

void CircularBuffer::Grow(int addSize)
{	
	GrowReserve(addSize);
	mDataSize += addSize;
}

void CircularBuffer::GrowFront(int addSize)
{
	if (mDataSize + addSize > mBufSize)
	{
		Resize(mDataSize + addSize);
	}
	mDataSize += addSize;
	mTail = (mTail + mBufSize - addSize) % mBufSize;
}

int CircularBuffer::GetSize()
{
	return mDataSize;
}

void CircularBuffer::MapView(int idx, int len, CircularBuffer::View& view)
{
	view.mCircularBuffer = this;
	view.mSrcIdx = idx;
	view.mSrcSize = len;
	if (mTail + idx + len <= mBufSize)
	{
		view.mPtr = mBuffer + mTail + idx;
	}
	else
	{
		if (view.mTempBufSize < len)
		{
			delete view.mTempBuf;
			view.mTempBuf = new uint8[len];
			view.mTempBufSize = len;
		}
		view.mPtr = view.mTempBuf;		
		Read(view.mTempBuf, idx, len);
	}
}

void CircularBuffer::Read(void* ptr, int idx, int len)
{
	BF_ASSERT(len <= mBufSize);

	if (len == 0)
		return;

	int absIdx = (mTail + idx) % mBufSize;
	if (absIdx + len > mBufSize)
	{		
		int lowSize = mBufSize - absIdx;
		memcpy(ptr, mBuffer + absIdx, lowSize);
		memcpy((uint8*)ptr + lowSize, mBuffer, len - lowSize);
	}
	else
	{
		memcpy(ptr, mBuffer + absIdx, len);
	}
}

void CircularBuffer::Write(void* ptr, int idx, int len)
{
	BF_ASSERT(len <= mBufSize);

	if (len == 0)
		return;

	int absIdx = (mTail + idx) % mBufSize;
	if (absIdx + len > mBufSize)
	{		
		int lowSize = mBufSize - absIdx;
		memcpy(mBuffer + absIdx, ptr, lowSize);
		memcpy(mBuffer, (uint8*)ptr + lowSize, len - lowSize);
	}
	else
	{
		memcpy(mBuffer + absIdx, ptr, len);
	}
}

void CircularBuffer::RemoveFront(int len)
{
	mTail = (mTail + len) % mBufSize;
	mDataSize -= len;
}

//////////////////////////////////////////////////////////////////////////

BpCmdTarget::BpCmdTarget()
{		
	mCurDynStrIdx = 0;
	mCurDepth = 0;	
	mThreadName = NULL;
}

void BpCmdTarget::Disable()
{
	AutoCrit autoCrit(mCritSect);
	mOutBuffer.Free();
}

const char* BpCmdTarget::DynamicString(const char* str)
{
	int usedIdx = mCurDynStrIdx;
	mDynStrs[usedIdx] = str;
	mCurDynStrIdx = (mCurDynStrIdx + 1) % BF_ARRAY_COUNT(mDynStrs);
	return (const char*)(intptr)usedIdx;
}

const char* BpCmdTarget::ToStrPtr(const char* str)
{
	if ((intptr)str < BF_ARRAY_COUNT(mDynStrs))
		return mDynStrs[(intptr)str];
	return str;
}

#define BPCMD_PREPARE if ((gBpManagerOwner.mDidShutdown) || (!BpManager::Get()->mCollectData)) return; AutoCrit autoCrit(mCritSect)

#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

//#define BPCMD_RESERVE(addSize) mOutBuffer.resize(mOutBuffer.size() + (addSize)); uint8* data = &mOutBuffer[mOutBuffer.size() - (addSize)];
#define BPCMD_RESERVE(addSize) uint8* data = mOutBuffer.Alloc(addSize)
#define BPCMD_RESERVE_UNDECL(addSize) data = mOutBuffer.Alloc(addSize)
#define BPCMD_MEMBER(T) *((T*)(data += sizeof(T)) - 1)
#define BPCMD_MEMCPY(ptr, size) memcpy(data, ptr, size); data += size
#define BPCMD_END() BF_ASSERT(data == mOutBuffer.mPtr + mOutBuffer.mDataSize)

static int64 GetTimestamp()
{
#ifdef BF_PLATFORM_WINDOWS
	return __rdtsc() / 100;
#else
	return BfpSystem_GetCPUTick() / 100;
#endif
}

#define MAX_DEPTH 8192

void BpCmdTarget::Enter(const char* name)
{
	BPCMD_PREPARE;

	// Failure here could be from unbalanced enter/leave calls
	BF_ASSERT((uint32)mCurDepth <= MAX_DEPTH);

	if ((intptr)name < BF_ARRAY_COUNT(mDynStrs))
	{
		const char* dynStr = mDynStrs[(intptr)name];
		int len = (int)strlen(dynStr);
		BPCMD_RESERVE(1 + 8 + len + 1);
		BPCMD_MEMBER(uint8) = BpCmd_EnterDyn;
		BPCMD_MEMBER(int64) = GetTimestamp();
		BPCMD_MEMCPY(dynStr, len + 1);
		BPCMD_END();
	}
	else
	{		
		BPCMD_RESERVE(1 + 8 + sizeof(const char*));
		BPCMD_MEMBER(uint8) = BpCmd_Enter;
		BPCMD_MEMBER(int64) = GetTimestamp();
		BPCMD_MEMBER(const char*) = name;
		BPCMD_END();
	}

	mCurDepth++;
}

void BpCmdTarget::Enter(const char* name, va_list args)
{
	BPCMD_PREPARE;

	// Failure here could be from unbalanced enter/leave calls
	BF_ASSERT((uint32)mCurDepth <= MAX_DEPTH);

	int len = 0;

	int paramSize = 0;

	//va_list origArgs = args;

	va_list origArgs;
	va_copy(origArgs, args);

	//va_start(args, name);		
	const char* cPtr = ToStrPtr(name);
	while (true)
	{
		char c = *(cPtr++);
		if (c == 0)
			break;
		len++;
		if (c == '%')
		{
			len++;
			char nextC = *(cPtr++);
			if (nextC != '%')
			{
				if (nextC == 'f')
				{
					va_arg(args, double);
					paramSize += 4; // float
				}
				else if (nextC == 'd')
				{
					intptr val = va_arg(args, intptr);
					paramSize += 4; // int32
				}
				else if (nextC == 's')
				{
					const char* str = ToStrPtr(va_arg(args, char*));
					paramSize += (int)strlen(str) + 1;
				}
				else
				{
					BF_FATAL("Invalid format flag");
				}
			}
		}
	}
	//va_end(args);

	uint8* data;
	if ((intptr)name < BF_ARRAY_COUNT(mDynStrs))
	{
		const char* dynStr = mDynStrs[(intptr)name];
		int len = (int)strlen(dynStr);
		BPCMD_RESERVE_UNDECL(1 + 8 + len + 1 + paramSize);
		BPCMD_MEMBER(uint8) = BpCmd_EnterDyn;
		BPCMD_MEMBER(int64) = GetTimestamp();
		BPCMD_MEMCPY(dynStr, len + 1);		
	}
	else
	{		
		BPCMD_RESERVE_UNDECL(1 + 8 + sizeof(const char*) + paramSize);
		BPCMD_MEMBER(uint8) = BpCmd_Enter;
		BPCMD_MEMBER(int64) = GetTimestamp();
		BPCMD_MEMBER(const char*) = name;		
	}

	/*BPCMD_RESERVE(1 + 8 + len + 1 + paramSize);	
	BPCMD_MEMBER(uint8) = BpCmd_Enter;	
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_MEMCPY(name, len + 1);*/

	args = origArgs;
	//va_start(args, name);		
	cPtr = ToStrPtr(name);
	while (true)
	{
		char c = *(cPtr++);
		if (c == 0)
			break;
		if (c == '%')
		{
			char nextC = *(cPtr++);
			if (nextC != '%')
			{
				if (nextC == 'f')
				{
					BPCMD_MEMBER(float) = (float)va_arg(args, double);					
				}
				else if (nextC == 'd')
				{
					BPCMD_MEMBER(int32) = (int32)va_arg(args, intptr);
				}
				else if (nextC == 's')
				{
					const char* str = ToStrPtr(va_arg(args, char*));
					BPCMD_MEMCPY(str, (int)strlen(str) + 1);
				}
				else
				{
					BF_FATAL("Invalid format flag");
				}
			}
		}
	}
	va_end(args);

	BPCMD_END();

	mCurDepth++;
}

void BpCmdTarget::EnterF(const char* name, ...)
{
	va_list args;
	va_start(args, name);
	Enter(name, args);
	va_end(args);
}

void BpCmdTarget::Leave()
{
	BPCMD_PREPARE;

	if (mCurDepth <= 0)
	{
		// This is either due to improperly balanced Enter/Leaves or from a Reconnnect
		BF_ASSERT(BpManager::Get()->mInitCount > 1);
		return;
	}

	BPCMD_RESERVE(1 + 8);
	BPCMD_MEMBER(uint8) = BpCmd_Leave;
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_END();

	mCurDepth--;
}

void BpCmdTarget::Event(const char* name, const char* details)
{
	name = ToStrPtr(name);
	details = ToStrPtr(details);

	BPCMD_PREPARE;

	int nameLen = (int)strlen(name);
	int detailsLen = (int)strlen(details);
	BPCMD_RESERVE(1 + 8 + nameLen+1 + detailsLen+1);
	BPCMD_MEMBER(uint8) = BpCmd_Event;	
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_MEMCPY(name, nameLen + 1);
	BPCMD_MEMCPY(details, detailsLen + 1);
	BPCMD_END();	
}

void BpRootCmdTarget::Init()
{
	BPCMD_PREPARE;

	BPCMD_RESERVE(1);
	BPCMD_MEMBER(uint8) = BpCmd_Init;
	BPCMD_END();
}

void BpRootCmdTarget::Tick()
{
	BPCMD_PREPARE;

	BPCMD_RESERVE(1 + 8);
	BPCMD_MEMBER(uint8) = BpCmd_Tick;
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_END();
}

void BpRootCmdTarget::KeepAlive()
{
	BPCMD_PREPARE;

	BPCMD_RESERVE(1 + 8);
	BPCMD_MEMBER(uint8) = BpCmd_KeepAlive;
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_END();
}

void BpRootCmdTarget::AddThread(int threadId, BfpThreadId nativeThreadId)
{
	BPCMD_PREPARE;

	BPCMD_RESERVE(1 + 8 + 4 + 4);
	BPCMD_MEMBER(uint8) = BpCmd_ThreadAdd;	
	BPCMD_MEMBER(int64) = GetTimestamp();
	BPCMD_MEMBER(int32) = threadId;
	BPCMD_MEMBER(int32) = (int32)nativeThreadId;
	BPCMD_END();
}

BpThreadInfo::BpThreadInfo()
{
	mNativeThreadId = -1;
	mThreadId = 0;
	mReadyToSend = false;
	mHasTerminated = false;
}

BpThreadInfo::~BpThreadInfo()
{
	if (mThreadName != NULL)
		BpManager::Get()->FreeBytes(mThreadName);
}

void BpThreadInfo::SetThreadName(const char* name)
{
	int len = (int)strlen(name);
	if (mThreadName == NULL)
	{		
		mThreadName = (char*)BpManager::Get()->AllocBytes(len + 1);
		memcpy(mThreadName, name, len + 1);
	}	

	BPCMD_PREPARE;
	
	BPCMD_RESERVE(1 + len + 1);
	BPCMD_MEMBER(uint8) = BpCmd_ThreadName;	
	BPCMD_MEMCPY(name, len + 1);
	BPCMD_END();	
}

void BpThreadInfo::RemoveThread()
{	
	BPCMD_PREPARE;

	BPCMD_RESERVE(1 + 8);
	BPCMD_MEMBER(uint8) = BpCmd_ThreadRemove;
	BPCMD_MEMBER(int64) = GetTimestamp();	
	BPCMD_END();	
}

//////////////////////////////////////////////////////////////////////////

static void NTAPI FlsFreeFunc(void* ptr)
{
	BpThreadInfo* threadInfo = (BpThreadInfo*)ptr;

	auto bpManager = BpManager::Get();
	AutoCrit autoCrit(bpManager->mCritSect);

	threadInfo->mHasTerminated = true;
	threadInfo->RemoveThread();

	if (!bpManager->mThreadRunning)
	{
		bpManager->mThreadInfos.Remove(threadInfo);		
		delete threadInfo;

		// It's possible that other thread-specific destructors will occur after this call, which may use BeefPerf,
		//  and in that case we will re-add this thread info
		BpManager::sBpThreadInfo = NULL;
	}	
}

BpManager::BpManager() : mShutdownEvent(true), mTLSDtor(&FlsFreeFunc)
{	
#ifdef BF_PLATFORM_WINDOWS
	mMutex = NULL;
	mSharedMemoryFile = NULL;
#endif
	mSocket = INVALID_SOCKET;
	mConnectState = BpConnectState_NotConnected;
	mThread = NULL;
	mThreadId = 0;
	mCurTick = 0;
	mCurThreadId = 0;
	mOutBlockSizeLeft = 0;
	mPauseCount = 0;
	mInitCount = 0;
	
	mCollectData = false;
	mThreadRunning = false;

	mInitTimeStamp = GetTimestamp();
	mInitTickCount = BFTickCount();
}

BpManager::~BpManager()
{
	Shutdown();	
	
#ifdef BF_PLATFORM_WINDOWS
	if (mMutex != NULL)
		::CloseHandle(mMutex);
	if (mSharedMemoryFile != NULL)
		::CloseHandle(mSharedMemoryFile);
#endif
}

bool BpManager::Connect()
{
	struct sockaddr_in server;
	struct hostent * hp;

	server.sin_family = PF_INET;
	hp = gethostbyname(mServerName.c_str());
	if (hp == NULL)
		return false;

	memcpy(&server.sin_addr, hp->h_addr_list[0], sizeof(server.sin_addr));
	server.sin_port = htons(4208);
#ifdef BF_PLATFORM_WINDOWS	
	bool isLocalhost = server.sin_addr.S_un.S_addr == 0x0100007f;
#else
	bool isLocalhost = server.sin_addr.s_addr == 0x0100007f;
#endif

	int result = ::connect(mSocket, (sockaddr*)&server, sizeof(server));
	if (result != 0)
	{
#ifdef BF_PLATFORM_WINDOWS
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK)
			return false;
#else		
		if (errno != EINPROGRESS)
			return false;
#endif
	}
				
	int totalWaitedMS = 0;

	// Wait for connection - normally we wait for either the connection to occur or an error to occur,
	// but if we shutdown the app then we need to ensure we waited "long enough", but we don't want
	// to keep very short programs from exiting in a timely manner so we have a short localhost timeout
	// since Windows will delay for 2s before failing
	while (true)
	{
		int selectTimeoutMS = 20;

		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = selectTimeoutMS * 1000;

		FD_SET socketWriteSet;
		FD_ZERO(&socketWriteSet);
		FD_SET(mSocket, &socketWriteSet);

		FD_SET socketErrorSet;
		FD_ZERO(&socketErrorSet);
		FD_SET(mSocket, &socketErrorSet);

		int result = select((int)mSocket + 1, NULL, &socketWriteSet, &socketErrorSet, &timeout);
		if (result == -1)
			return false;

		if (FD_ISSET(mSocket, &socketWriteSet))
			break;
		if (FD_ISSET(mSocket, &socketErrorSet))
			return false;

		totalWaitedMS += selectTimeoutMS;
		if (mShutdownEvent.WaitFor(0))
		{
			// We are shutting down - have we waited enough?
			int minWaitMS = isLocalhost ? 50 : 5*1000;
			if (totalWaitedMS >= minWaitMS)
				return false;
		}
		
		// We don't want to wait too long, otherwise we will buffer up too much
		int maxWaitMS = isLocalhost ? 5*1000 : 15*1000;
		if (totalWaitedMS >= maxWaitMS)
			return false;
	}
		
	return true;
}

uint8* BpManager::StartCmd(uint8 cmd, CircularBuffer::View& view, int maxLen)
{	
	mOutBuffer.GrowReserve(maxLen);
	mOutBuffer.MapView(mOutBuffer.GetSize(), maxLen, view);
	uint8* dataOut = view.mPtr;
	GET_FROM(dataOut, uint8) = cmd;
	return dataOut;
}

void BpManager::EndCmd(CircularBuffer::View& view, uint8* ptr)
{
	int actualSize = (int)(ptr - view.mPtr);
	view.Commit(actualSize);
	mOutBuffer.Grow(actualSize);
}

void BpManager::TrySendData()
{
	CircularBuffer::View outView;

	while (true)
	{
		int sizeLeft = mOutBuffer.GetSize();
		if (sizeLeft == 0)
			return;

		int trySend = std::min(sizeLeft, 8192);
		mOutBuffer.MapView(0, trySend, outView);

		int result = send(mSocket, (const char*)outView.mPtr, trySend, 0);
		
		if (result < 0)
		{
#ifdef BF_PLATFORM_WINDOWS
			int err = WSAGetLastError();
			switch (err)
			{
			case WSAECONNABORTED:
			case WSAECONNRESET:
				mConnectState = BpConnectState_NotConnected;
			}
#else
			switch (errno)
			{				
			case ECONNRESET:
				mConnectState = BpConnectState_NotConnected;
			}
#endif
			return;
		}

		mOutBuffer.RemoveFront(result);		
	}
}

void BpManager::LostConnection()
{	
	mCollectData = false;
	AutoCrit autoCrit(mCritSect);
	mRootCmdTarget.Disable();
	for (auto threadInfo : mThreadInfos)
		threadInfo->Disable();
#ifdef BF_PLATFORM_WINDOWS
	closesocket(mSocket);
#else
	close(mSocket);
#endif
	mSocket = INVALID_SOCKET;	
}

BpThreadInfo* BpManager::SlowGetCurThreadInfo()
{
    BfpThreadId curThreadId = BfpThread_GetCurrentId();
	// Try to find an existing one
	{
		AutoCrit autoCrit(mCritSect);
		for (auto threadInfo : mThreadInfos)
			if (threadInfo->mNativeThreadId == curThreadId)
				return threadInfo;
	}

	auto threadInfo = new BpThreadInfo();	
	threadInfo->mThreadId = mCurThreadId++;	
	threadInfo->mNativeThreadId = curThreadId;	
	sBpThreadInfo = threadInfo;	
	
	AutoCrit autoCrit(mCritSect);
	mThreadInfos.push_back(threadInfo);

	if (mThreadRunning)
		mRootCmdTarget.AddThread(threadInfo->mThreadId, threadInfo->mNativeThreadId);

	mTLSDtor.Add((void*)threadInfo);	

	return threadInfo;
}

void* BpManager::AllocBytes(int size)
{
	return new uint8[size];
}

void BpManager::FreeBytes(void* ptr)
{
	delete [] (uint8*)ptr;
}

void BpManager::FinishWorkThread()
{
	AutoCrit autoCrit(mCritSect);
	for (int i = 0; i < (int)mThreadInfos.size(); i++)
	{
		auto threadInfo = mThreadInfos[i];
		if (threadInfo->mHasTerminated)
		{
			delete threadInfo;
			mThreadInfos.erase(mThreadInfos.begin() + i);
			i--;
		}
	}	
	mThreadRunning = false;
}

void BpManager::ThreadProc()
{
	BfpThread_SetName(NULL, "BeefPerf", NULL);

	if (!Connect())
	{				
		mConnectState = BpConnectState_Failed;
		LostConnection();		
		FinishWorkThread();
		return;
	}	

	Buffer threadBuffer;
	String tempStr;
	CircularBuffer::View outView;

	timeval timeout;
	timeout.tv_sec  = 0;
	timeout.tv_usec = 20 * 1000; // 20ms

	uint32 gLastMsgTick = BFTickCount();

	DWORD lastTimeTick = 0;

	while (mConnectState != BpConnectState_NotConnected)
	{
		bool wantsExit = mShutdownEvent.WaitFor(0);

		FD_SET socketReadSet;
		FD_ZERO(&socketReadSet);
		FD_SET(mSocket, &socketReadSet);

		FD_SET socketWriteSet;
		FD_ZERO(&socketWriteSet);
		if (mOutBuffer.GetSize() > 0)			
			FD_SET(mSocket, &socketWriteSet);

		FD_SET socketErrorSet;
		FD_ZERO(&socketErrorSet);
		FD_SET(mSocket, &socketErrorSet);

		int selResult = select((int)mSocket + 1, &socketReadSet, &socketWriteSet, &socketErrorSet, &timeout);
		if (FD_ISSET(mSocket, &socketWriteSet))
		{
			TrySendData();
			//continue;
		}
		if (FD_ISSET(mSocket, &socketReadSet))
		{
			// Just eat the data
			uint8 data[4096];
			int len = recv(mSocket, (char*)data, 4096, 0);
			int b = 0;
		}
		if (FD_ISSET(mSocket, &socketErrorSet))
		{
#ifdef BF_PLATFORM_WINDOWS
			int err = WSAGetLastError();
#endif
			mConnectState = BpConnectState_NotConnected;
			LostConnection();			
			FinishWorkThread();
			return;
		}

		// Alloc space for size
		mOutBuffer.Grow(4);
		int startBufferSize = mOutBuffer.GetSize();

		int threadIdx = -1;
		int threadId = -1;		

		//
		{
			// We need to send the BpCmd_ThreadAdd before sending any data from the thread,			
			AutoCrit autoCrit(mCritSect);

			for (int threadIdx = 0; threadIdx < (int)mThreadInfos.size(); threadIdx++)
			{
				auto threadInfo = mThreadInfos[threadIdx];
				if ((threadInfo->mHasTerminated) && (threadInfo->mOutBuffer.mDataSize == 0))
				{
					mThreadInfos.erase(mThreadInfos.begin() + threadIdx);
					delete threadInfo;
					threadIdx--;
				}
				else
				{
					threadInfo->mReadyToSend = true;
				}
			}
		}

		while (true)
		{
			BpCmdTarget* cmdTarget = NULL;

			if (threadIdx == -1)
			{
				cmdTarget = &mRootCmdTarget;
				threadIdx++;
			}
			else
			{
				AutoCrit autoCrit(mCritSect);				
				if (threadIdx >= (int)mThreadInfos.size())
					break;
				auto threadInfo = mThreadInfos[threadIdx++];
				threadId = threadInfo->mThreadId;
				cmdTarget = threadInfo;

				if (!threadInfo->mReadyToSend)
					continue;
			}

			//
			{
				AutoCrit autoCrit(cmdTarget->mCritSect);				
				BF_ASSERT(threadBuffer.mDataSize == 0);
				memcpy(threadBuffer.Alloc(cmdTarget->mOutBuffer.mDataSize), cmdTarget->mOutBuffer.mPtr, cmdTarget->mOutBuffer.mDataSize);
				cmdTarget->mOutBuffer.Clear();
			}

			if (threadBuffer.mDataSize == 0)
				continue;			

			uint8* dataOut = StartCmd(BpCmd_SetThread, outView, 5);
			EncodeSLEB128(dataOut, threadId);
			EndCmd(outView, dataOut);

			uint8* dataIn = threadBuffer.mPtr;
			uint8* dataInEnd = dataIn + threadBuffer.mDataSize;

			static int sIdx = 0;			

			while (dataIn < dataInEnd)
			{				
				BpCmd cmd = (BpCmd)*(dataIn++);

				int nameMsgLen;
				if (cmd == BpCmd_Init)
				{
					AutoCrit autoCrit(mRootCmdTarget.mCritSect);

					String env;
					env += "SessionID\t";
					env += mSessionID;
					env += "\n";

					if (!mSessionName.IsEmpty())
					{
						env += "SessionName\t";
						env += mSessionName;
						env += "\n";
					}

					if (!mClientName.IsEmpty())
					{
						env += "ClientName\t";
						env += mClientName;
						env += "\n";
					}					
					
					int32 nameLen = (int32)env.length();
					uint8* dataOut = StartCmd(BpCmd_Init, outView, 1 + 4 + nameLen + 1);
					EncodeSLEB128(dataOut, BP_CLIENT_VERSION);					
					memcpy(dataOut, env.c_str(), nameLen + 1);
					dataOut += nameLen + 1;
					EndCmd(outView, dataOut);

					dataOut = StartCmd(BpCmd_ClockInfo, outView, 1 + 8 + 8 + 8 + 1);
					EncodeSLEB128(dataOut, mCurTick);
					EncodeSLEB128(dataOut, GetTimestamp());
					EncodeSLEB128(dataOut, (int)BFTickCount());
					EncodeSLEB128(dataOut, BfpSystem_GetCPUTickFreq());
					EndCmd(outView, dataOut);
				}
				else if ((cmd == BpCmd_Enter) || (cmd == BpCmd_EnterDyn))
				{
					int64 tick = GET_FROM(dataIn, int64);
					const char* name;

					int strIdx;
					int paramsSize = 0;
					if (cmd == BpCmd_EnterDyn)
					{
						name = (const char*)dataIn;
						int nameLen = (int)strlen(name);
						dataIn += nameLen + 1;
						paramsSize = -1;
						strIdx = -nameLen;

						nameMsgLen = 4+1 + nameLen;
					}
					else
					{
						name = GET_FROM(dataIn, const char*);

						BpZoneName* zoneName;
						if (mZoneNameMap.TryAdd(name, NULL, &zoneName))						
						{
							int nameLen = (int)strlen(name);
							strIdx = (int)mZoneNameMap.size() - 1;							
							zoneName->mIdx = strIdx;
							zoneName->mSize = 0;
							bool isDyn = false;

							const char* cPtr = name;
							while (true)
							{
								char c = *(cPtr++);
								if (c == 0)
									break;
								if (c == '%')
								{
									char nextC = *(cPtr++);
									if (nextC != '%')
									{
										if (nextC == 'f')
										{
											zoneName->mSize += 4;
										}
										else if (nextC == 'd')
										{
											zoneName->mSize += 4;
										}
										else if (nextC == 's')
										{
											isDyn = true;
										}
									}
								}
							}
							if (isDyn)
								zoneName->mSize = -1;
							
							uint8* dataOut = StartCmd(BpCmd_StrEntry, outView, 1 + nameLen + 1);
							memcpy(dataOut, name, nameLen + 1);
							dataOut += nameLen + 1;
							EndCmd(outView, dataOut);

							paramsSize = zoneName->mSize;
						}
						else
						{
							strIdx = zoneName->mIdx;
							paramsSize = zoneName->mSize;
						}

						nameMsgLen = 4+1;
					}

					bool isDynSize = false;
					int addParamSize = 0;
					if (paramsSize == -1)
					{
						isDynSize = true;
						addParamSize = 4 + 1; // For size param

											  // Calc size
						auto checkDataIn = dataIn;

						const char* cPtr = name;
						while (true)
						{
							char c = *(cPtr++);
							if (c == 0)
								break;
							if (c == '%')
							{
								char nextC = *(cPtr++);
								if (nextC != '%')
								{
									if (nextC == 'f')
									{										
										checkDataIn += 4;
									}
									else if (nextC == 'd')
									{										
										checkDataIn += 4;
									}
									else if (nextC == 's')
									{
										int len = (int)strlen((const char*)checkDataIn);
										checkDataIn += len + 1;										
									}							
								}
							}
						}

						paramsSize = (int)(checkDataIn - dataIn);
					}

					uint8* dataOut = StartCmd(BpCmd_Enter, outView, 1 + 8+1 + nameMsgLen + paramsSize + addParamSize);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;
					EncodeSLEB128(dataOut, tickDelta);
					if (strIdx < 0)
					{
						EncodeSLEB128(dataOut, strIdx);
						memcpy(dataOut, name, -strIdx);
						dataOut += -strIdx;
					}
					else
						EncodeSLEB128(dataOut, strIdx);

					if (isDynSize)
						EncodeSLEB128(dataOut, paramsSize);

					if (paramsSize != 0)
					{
						memcpy(dataOut, dataIn, paramsSize);
						dataOut += paramsSize;
						dataIn += paramsSize;
					}

					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_Leave)
				{
					uint8* dataOut = StartCmd(BpCmd_Leave, outView, 1 + 8+1);

					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;
					EncodeSLEB128(dataOut, tickDelta);
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_ThreadName)
				{					
					const char* name = (const char*)dataIn;
					int nameLen = (int)strlen(name);
					uint8* dataOut = StartCmd(BpCmd_ThreadName, outView, 1 + nameLen + 1);					
					memcpy(dataOut, dataIn, nameLen + 1);
					dataIn += nameLen + 1;
					dataOut += nameLen + 1;
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_Tick)
				{
					uint8* dataOut = StartCmd(BpCmd_Tick, outView, 1 + 8+1);

					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;
					EncodeSLEB128(dataOut, tickDelta);
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_KeepAlive)
				{
					uint8* dataOut = StartCmd(BpCmd_KeepAlive, outView, 1 + 8+1);

					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;
					EncodeSLEB128(dataOut, tickDelta);
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_ThreadAdd)
				{
					uint8* dataOut = StartCmd(BpCmd_ThreadAdd, outView, 1 + 8+1 + 4+1 + 4+1);

					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;
					int32 threadId = GET_FROM(dataIn, int32);
					int32 nativeThreadId = GET_FROM(dataIn, int32);
					EncodeSLEB128(dataOut, tickDelta);
					EncodeSLEB128(dataOut, threadId);
					EncodeSLEB128(dataOut, nativeThreadId);
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_ThreadRemove)
				{
					uint8* dataOut = StartCmd(BpCmd_ThreadRemove, outView, 1 + 8+1);

					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;					
					EncodeSLEB128(dataOut, tickDelta);					
					EndCmd(outView, dataOut);
				}
				else if (cmd == BpCmd_Event)
				{
					int64 tick = GET_FROM(dataIn, int64);
					int64 tickDelta = tick - mCurTick;
					mCurTick = tick;

					const char* name = (const char*)dataIn;
					int nameLen = (int)strlen(name);
					const char* details = (const char*)(dataIn + nameLen + 1);
					int detailsLen = (int)strlen(details);

					uint8* dataOut = StartCmd(BpCmd_Event, outView, 1 + 8+1 + nameLen + 1 + detailsLen + 1);
					EncodeSLEB128(dataOut, tickDelta);
					memcpy(dataOut, dataIn, nameLen + 1);
					dataIn += nameLen + 1;
					dataOut += nameLen + 1;
					memcpy(dataOut, dataIn, detailsLen + 1);
					dataIn += detailsLen + 1;
					dataOut += detailsLen + 1;
					EndCmd(outView, dataOut);
				}
				else
					BF_FATAL("Not handled");
				
				// If we get a large backlog then try to break it up into smaller chunks and send out periodically
				int bufSizeAdded = mOutBuffer.GetSize() - startBufferSize;
				BF_ASSERT(bufSizeAdded >= 0);
				if (bufSizeAdded >= 64*1024)				
				{					
					// Set chunk size
					CircularBuffer::View view;
					mOutBuffer.MapView(startBufferSize - 4, 4, view);
					uint8* data = view.mPtr;
					GET_FROM(data, int32) = bufSizeAdded;
					view.Commit();

					// We can attempt some more sending now...
					TrySendData();

					// Start next header
					mOutBuffer.Grow(4);
					startBufferSize = mOutBuffer.GetSize();
				}
			}

			threadBuffer.Clear();
		}

		int bufSizeAdded = mOutBuffer.GetSize() - startBufferSize;

		DWORD curTick = BFTickCount();
		
		// Encode clock info
		if ((lastTimeTick == 0) || ((bufSizeAdded > 0) && (curTick - lastTimeTick >= 1000)))
		{			
			uint8* dataOut = StartCmd(BpCmd_ClockInfo, outView, 1 + 8 + 8 + 8 + 1);
			EncodeSLEB128(dataOut, mCurTick);
			EncodeSLEB128(dataOut, GetTimestamp());
			EncodeSLEB128(dataOut, (int)BFTickCount());
			EncodeSLEB128(dataOut, BfpSystem_GetCPUTickFreq());
			EndCmd(outView, dataOut);

			lastTimeTick = curTick;
		}

		bufSizeAdded = mOutBuffer.GetSize() - startBufferSize;
		//BF_ASSERT((bufSizeAdded <= 64*1024) && (bufSizeAdded >= 0));
		if (bufSizeAdded != 0)
		{
			// Set chunk size
			CircularBuffer::View view;
			mOutBuffer.MapView(startBufferSize - 4, 4, view);
			uint8* data = view.mPtr;
			GET_FROM(data, int32) = bufSizeAdded;
			view.Commit();
		}
		else
			mOutBuffer.Grow(-4); // No data added, pop chunk size off

		if (mOutBuffer.GetSize() == 0)
		{			
			if (wantsExit)
			{
				break;
			}

			uint32 tickNow = BFTickCount();
			if ((tickNow - gLastMsgTick >= 1000) /*&& (!wantsExit)*/)
			{				
				mRootCmdTarget.KeepAlive();
			}
		}
		else
		{			
			gLastMsgTick = BFTickCount();			
		}
	}

	mOutBuffer.Clear();
	mConnectState = BpConnectState_NotConnected;
	closesocket(mSocket);
	mSocket = INVALID_SOCKET;
	FinishWorkThread();
}

void BFP_CALLTYPE BpManager::ThreadProcThunk(void* ptr)
{
	((BpManager*)ptr)->ThreadProc();
}

void BpManager::Clear()
{
	int threadIdx = 0;

	while (true)
	{
		BpThreadInfo* threadInfo = NULL;
		//
		{
			AutoCrit autoCrit(mCritSect);
			if (threadIdx >= (int)mThreadInfos.size())
				break;
			threadInfo = mThreadInfos[threadIdx++];
		}

		AutoCrit autoCrit(threadInfo->mCritSect);
		threadInfo->mOutBuffer.Clear();
	}
}

void BpManager::SetClientName(const StringImpl& clientName)
{
	AutoCrit autoCrit(mRootCmdTarget.mCritSect);
	mClientName = clientName;
}

BpResult BpManager::Init(const char* serverName, const char* sessionName)
{	
	if (serverName == NULL)
	{		
		FinishWorkThread();
		return BpResult_Ok;
	}

	if (mSocket != INVALID_SOCKET)
		return BpResult_AlreadyInitialized;

	BfpGUID guid;
	BfpSystem_CreateGUID(&guid);
	mSessionID = StrFormat("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid.mData1, guid.mData2, guid.mData3,
		guid.mData4[0], guid.mData4[1], guid.mData4[2], guid.mData4[3],
		guid.mData4[4], guid.mData4[5], guid.mData4[6], guid.mData4[7]);

	if (mClientName.IsEmpty())
	{		
		BfpSystemResult result;
		BFP_GETSTR_HELPER(mClientName, result, BfpSystem_GetComputerName(__STR, __STRLEN, &result));
	}

#ifdef BF_PLATFORM_WINDOWS
	WSADATA wsa;	
	int result = WSAStartup(MAKEWORD(2, 0), &wsa);
	if (result != 0)
	{
		return BpResult_InternalError;
	}
#endif

	mSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (mSocket == INVALID_SOCKET)
		return BpResult_InternalError;

	u_long iMode = 1;
#ifdef BF_PLATFORM_WINDOWS
	result = ioctlsocket(mSocket, FIONBIO, &iMode);
#else
	int result = ioctl(mSocket, FIONBIO, &iMode);
#endif

	AutoCrit autoCrit(mCritSect);
	AutoCrit autoCrit2(mRootCmdTarget.mCritSect);
	
	mInitCount++;
	mThreadRunning = true;
	mCollectData = true;
	mServerName = serverName;
	mSessionName = sessionName;
	mConnectState = BpConnectState_Connecting;
	mCurTick = 0;
	mZoneNameMap.Clear();

	bool isReinit = mInitCount > 1;
	if (isReinit)
	{
		// We may have old data in the command targets, so clear all that out
		mRootCmdTarget.Disable();
		for (auto threadInfo : mThreadInfos)
		{
			AutoCrit autoCrit(mCritSect);
			threadInfo->Disable();
			if (threadInfo->mThreadName != NULL)
				threadInfo->SetThreadName(threadInfo->mThreadName);
			threadInfo->mCurDepth = 0;
		}
	}

	for (auto threadInfo : mThreadInfos)
	{		
		mRootCmdTarget.AddThread(threadInfo->mThreadId, threadInfo->mNativeThreadId);
	}

	mRootCmdTarget.Init();
	
	mThread = BfpThread_Create(ThreadProcThunk, (void*)this, 512 * 1024, BfpThreadCreateFlag_StackSizeReserve, &mThreadId);
	return BpResult_Ok;
}

void BpManager::RetryConnect()
{
	{
		AutoCrit autoCrit(mCritSect);		
		if ((mConnectState == BpConnectState_Connecting) ||
			(mConnectState == BpConnectState_Connected))
			return;
	}
	Shutdown();
	Init(mServerName.c_str(), mSessionName.c_str());
}

void BpManager::Pause()
{
	BfpSystem_InterlockedExchangeAdd32((uint32*)&mPauseCount, 1);
}

void BpManager::Unpause()
{
	BfpSystem_InterlockedExchangeAdd32((uint32*)&mPauseCount, -1);
}

void BpManager::Shutdown()
{
	BfpThread* workerThread = NULL;
	{
		AutoCrit autoCrit(mCritSect);
		mCollectData = false;
		workerThread = mThread;
		if (workerThread != NULL)
			mShutdownEvent.Set(true);
	}
	
	if (workerThread != NULL)
	{
		BfpThread_WaitFor(mThread, -1);
		mShutdownEvent.Reset();
	}
}

bool BpManager::IsDisconnected()
{
	return mConnectState == BpConnectState_NotConnected;
}

BpContext* Beefy::BpManager::CreateContext(const char* name)
{
	return nullptr;
}

void Beefy::BpManager::CloseContext()
{

}

void BpManager::Tick()
{
	mRootCmdTarget.Tick();
}

BpThreadInfo* BpManager::GetCurThreadInfo()
{
	BpThreadInfo* threadInfo = sBpThreadInfo;
	if (threadInfo == NULL)
	{
		if (gBpManagerOwner.mDidShutdown)
			return &sFakeThreadInfo;
		threadInfo = Get()->SlowGetCurThreadInfo();	
		sBpThreadInfo = threadInfo;
	}
	
	return threadInfo;
}

struct BpSharedMemory
{
public:
	BpManager* mBpManager;
	int mABIVersion;
};

BpManager* BpManager::Get()
{
	if (sBpManager != NULL)
		return sBpManager;

#ifdef BF_PLATFORM_WINDOWS
	char mutexName[128];
	sprintf(mutexName, "BeefPerf_mutex_%d", GetCurrentProcessId());
	char memName[128];
	sprintf(memName, "BeefPerf_mem_%d", GetCurrentProcessId());

	auto mutex = ::CreateMutexA(NULL, TRUE, mutexName);
	if (mutex != NULL)
	{
		HANDLE fileMapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, memName);
		if (fileMapping != NULL)						
		{
			BpSharedMemory* sharedMem = (BpSharedMemory*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BpSharedMemory));
			if (sharedMem != NULL)
			{
				if (sharedMem->mABIVersion == BP_ABI_VERSION)
				{
					sBpManager = sharedMem->mBpManager;
				}
				else
					OutputDebugStringA("*** BeefPerf ABI mismatch! ***\r\n");
				::UnmapViewOfFile(sharedMem);
			}
			else
				BF_FATAL("BpManager::Get MapViewOfFile error");
			::CloseHandle(fileMapping);
		}
		else
		{
			fileMapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BpSharedMemory), memName);
			if (fileMapping != NULL)
			{
				BpSharedMemory* sharedMem = (BpSharedMemory*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BpSharedMemory));
				if (sharedMem != NULL)
				{
					sBpManager = new BpManager();					
					sBpManager->mMutex = mutex;
					sBpManager->mSharedMemoryFile = fileMapping;
					sharedMem->mBpManager = sBpManager;
					sharedMem->mABIVersion = BP_ABI_VERSION;
					::UnmapViewOfFile(sharedMem);
					::ReleaseMutex(mutex);
					gBpManagerOwner.mOwnedManager = sBpManager;
				}
				else
				{
					BF_FATAL("BpManager::Get MapViewOfFile error");
					::CloseHandle(fileMapping);
					::CloseHandle(mutex);
				}
			}
			else
				BF_FATAL("BpManager::Get CreateFileMapping error");
		}
	}
	else
	{
		BF_FATAL("BpManager::Get CreateMutex error");
	}
#endif //BF_PLATFORM_WINDOWS
	
	if (sBpManager == NULL)
	{
		sBpManager = new BpManager();		
		gBpManagerOwner.mOwnedManager = sBpManager;
	}
	return sBpManager;
}

//////////////////////////////////////////////////////////////////////////

BP_EXPORT void BP_CALLTYPE BpShutdown()
{
	BpManager::Get()->Shutdown();
}

BP_EXPORT void BP_CALLTYPE BpSetClientName(const char* clientName)
{
	BpManager::Get()->SetClientName(clientName);
}

BP_EXPORT void BP_CALLTYPE BpInit(const char* serverName, const char* sessionName)
{
	BpManager::Get()->Init(serverName, sessionName);
}

BP_EXPORT BpConnectState BP_CALLTYPE BpGetConnectState()
{
	return BpManager::Get()->mConnectState;
}

BP_EXPORT void BP_CALLTYPE BpRetryConnect()
{
	return BpManager::Get()->RetryConnect();
}

BP_EXPORT void BP_CALLTYPE BpPause()
{
	BpManager::Get()->Pause();
}

BP_EXPORT void BP_CALLTYPE BpUnpause()
{
	BpManager::Get()->Unpause();
}

BP_EXPORT void BP_CALLTYPE BpSetThreadName(const char* threadName)
{
	BpManager::GetCurThreadInfo()->SetThreadName(threadName);
}

BP_EXPORT void BP_CALLTYPE BpEnter(const char* zoneName)
{
	BpManager::GetCurThreadInfo()->Enter(zoneName);
}

BP_EXPORT void BP_CALLTYPE BpEnterF(const char* zoneName, ...)
{
	va_list args;
	va_start(args, zoneName);
	BpManager::GetCurThreadInfo()->Enter(zoneName, args);
}

BP_EXPORT void BP_CALLTYPE BpLeave()
{
	BpManager::GetCurThreadInfo()->Leave();
}

BP_EXPORT void BP_CALLTYPE BpFrameTick()
{
	BpManager::Get()->Tick();
}

BP_EXPORT void BP_CALLTYPE BpEvent(const char* name, const char* details)
{
	BpManager::GetCurThreadInfo()->Event(name, details);
}

BP_EXPORT const char* BP_CALLTYPE BpDynStr(const char* str)
{
	return BpManager::GetCurThreadInfo()->DynamicString(str);
}

#else

BP_EXPORT void BP_CALLTYPE BpShutdown()
{
	
}

BP_EXPORT BpConnectState BP_CALLTYPE BpGetConnectState()
{
	return BpConnectState_NotConnected;
}

BP_EXPORT void BP_CALLTYPE BpRetryConnect()
{

}

BP_EXPORT void BP_CALLTYPE BpPause()
{

}

BP_EXPORT void BP_CALLTYPE BpUnpause()
{

}

BP_EXPORT void BP_CALLTYPE BpSetClientName(const char* clientName)
{
	
}

BP_EXPORT void BP_CALLTYPE BpInit(const char* serverName, const char* sessionName)
{
	
}

BP_EXPORT void BP_CALLTYPE BpSetThreadName(const char* threadName)
{
	
}

BP_EXPORT void BP_CALLTYPE BpEnter(const char* zoneName)
{
	
}

BP_EXPORT void BP_CALLTYPE BpEnterF(const char* zoneName, ...)
{
	
}

BP_EXPORT void BP_CALLTYPE BpLeave()
{
	
}

BP_EXPORT void BP_CALLTYPE BpFrameTick()
{
	
}

BP_EXPORT void BP_CALLTYPE BpEvent(const char* name, const char* details)
{
	
}

BP_EXPORT const char* BP_CALLTYPE BpDynStr(const char* str)
{
	return str;
}

#endif
