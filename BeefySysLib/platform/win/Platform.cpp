#define INITKNOWNFOLDERS
#include <guiddef.h>
#include <KnownFolders.h>
#undef INITKNOWNFOLDERS

#pragma warning(disable:4065)
#pragma warning(disable:4996)

#include "Common.h"

#ifndef BF_NO_BFAPP
#include "BFApp.h"
#endif

#include <mmsystem.h>
#include <shellapi.h>
#include <Objbase.h>
#include <signal.h>
#include <ntsecapi.h>   // UNICODE_STRING
#include <psapi.h>
#include <shlobj.h>
#include "../PlatformInterface.h"
#include "../PlatformHelper.h"
#include "CrashCatcher.h"
#include "../util/CritSect.h"
#include "../util/Dictionary.h"
#include "../util/HashSet.h"
#include "../../third_party/putty/wildcard.h"

#include "util/AllocDebug.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "rpcrt4.lib")

#define NOT_IMPL BF_FATAL("Not implemented")

USING_NS_BF;

static bool gTimerInitialized = false;
static int gTimerDivisor = 0;
CritSect gBfpCritSect;

struct WindowsSharedInfo
{
	int mThreadAcc;
	uint32 mTickStart;
};

static WindowsSharedInfo* gGlobalPlatformInfo = NULL;
static HANDLE gGlobalMutex = 0;

#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004
typedef LONG KPRIORITY;

enum SYSTEM_INFORMATION_CLASS
{
	SystemProcessInformation = 5
}; // SYSTEM_INFORMATION_CLASS

struct CLIENT_ID
{
	HANDLE UniqueProcess;
	HANDLE UniqueThread;
}; // struct CLIENT_ID

enum THREAD_STATE
{
	StateInitialized,
	StateReady,
	StateRunning,
	StateStandby,
	StateTerminated,
	StateWait,
	StateTransition,
	StateUnknown
};

struct VM_COUNTERS
{
	SIZE_T        PeakVirtualSize;
	SIZE_T        VirtualSize;
	ULONG         PageFaultCount;
	SIZE_T        PeakWorkingSetSize;
	SIZE_T        WorkingSetSize;
	SIZE_T        QuotaPeakPagedPoolUsage;
	SIZE_T        QuotaPagedPoolUsage;
	SIZE_T        QuotaPeakNonPagedPoolUsage;
	SIZE_T        QuotaNonPagedPoolUsage;
	SIZE_T        PagefileUsage;
	SIZE_T        PeakPagefileUsage;
	SIZE_T        PrivatePageCount;
};

struct SYSTEM_THREAD {
	LARGE_INTEGER   KernelTime;
	LARGE_INTEGER   UserTime;
	LARGE_INTEGER   CreateTime;
	ULONG           WaitTime;
	LPVOID          StartAddress;
	CLIENT_ID       ClientId;
	DWORD           Priority;
	LONG            BasePriority;
	ULONG           ContextSwitchCount;
	THREAD_STATE    State;
	ULONG           WaitReason;
};

struct SYSTEM_PROCESS_INFORMATION
{
	ULONG                   NextEntryOffset;
	ULONG                   NumberOfThreads;
	LARGE_INTEGER           Reserved[3];
	LARGE_INTEGER           CreateTime;
	LARGE_INTEGER           UserTime;
	LARGE_INTEGER           KernelTime;
	UNICODE_STRING          ImageName;
	KPRIORITY               BasePriority;
	HANDLE                  ProcessId;
	HANDLE                  InheritedFromProcessId;
	ULONG                   HandleCount;
	ULONG                   Reserved2[2];
	ULONG                   PrivatePageCount;
	VM_COUNTERS             VirtualMemoryCounters;
	IO_COUNTERS             IoCounters;
	SYSTEM_THREAD           Threads[1];
};

class BfpManager
{
public:
	BfpManager* mNext;

	BfpManager()
	{
		mNext = NULL;
	}

	virtual ~BfpManager()
	{
	}
};
BfpManager* gManagerTail;

static void BfpRecordManager(BfpManager* manager)
{
	AutoCrit autoCrit(gBfpCritSect);
	manager->mNext = gManagerTail;
	gManagerTail = manager;
}

typedef NTSTATUS(NTAPI* NtQuerySystemInformation_t)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
static NtQuerySystemInformation_t gNtQuerySystemInformation = NULL;
static HMODULE gNTDll = NULL;

static void ImportNTDll()
{
	if (gNTDll != NULL)
		return;

	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, MAX_PATH);
	wcscat(path, L"\\ntdll.dll");
	gNTDll = GetModuleHandle(path);
	if (gNTDll == NULL)
	{
		return;
	}
	gNtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(gNTDll, "NtQuerySystemInformation");
}

typedef HRESULT(WINAPI* SetThreadDescription_t)(HANDLE hThread, PCWSTR lpThreadDescription);
typedef HRESULT(WINAPI* GetThreadDescription_t)(HANDLE hThread, PWSTR* lpThreadDescription);
static SetThreadDescription_t gSetThreadDescription = NULL;
static GetThreadDescription_t gGetThreadDescription = NULL;
static HMODULE gKernelDll = NULL;

static void ImportKernel()
{
	if (gKernelDll != NULL)
		return;

	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, MAX_PATH);
	wcscat(path, L"\\kernel32.dll");
	gKernelDll = GetModuleHandle(path);
	if (gKernelDll == NULL)
	{
		return;
	}

	gSetThreadDescription = (SetThreadDescription_t)GetProcAddress(gKernelDll, "SetThreadDescription");
	gGetThreadDescription = (GetThreadDescription_t)GetProcAddress(gKernelDll, "GetThreadDescription");
}

#ifdef BF_MINGW
#define timeGetTime GetTickCount
#endif

static bool IsHandleValid(HANDLE handle)
{
	return (handle != NULL) && (handle != INVALID_HANDLE_VALUE);
}

WindowsSharedInfo* GetSharedInfo()
{
	if (gGlobalPlatformInfo != NULL)
		return gGlobalPlatformInfo;

	String sharedName = StrFormat("BfpSharedInfo_%d", GetCurrentProcessId());

	bool created = false;
	HANDLE sharedFileMapping = ::OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, sharedName.c_str());

	if (sharedFileMapping == NULL)
	{
		sharedFileMapping = ::CreateFileMappingA(
			INVALID_HANDLE_VALUE,
			NULL,
			PAGE_READWRITE,
			0,
			sizeof(WindowsSharedInfo),
			sharedName.c_str());
		created = true;
	}

	BF_ASSERT(sharedFileMapping != NULL);

	gGlobalPlatformInfo = (WindowsSharedInfo*)MapViewOfFile(sharedFileMapping,
		FILE_MAP_READ | FILE_MAP_WRITE,
		0,
		0,
		sizeof(WindowsSharedInfo));

	if (created)
	{
		memset(gGlobalPlatformInfo, 0, sizeof(WindowsSharedInfo));
		gGlobalPlatformInfo->mTickStart = timeGetTime();
	}

	timeBeginPeriod(1);

	return gGlobalPlatformInfo;
}

uint32 Beefy::BFTickCount()
{
	return timeGetTime() - GetSharedInfo()->mTickStart;
}

/*#ifdef BF_MINGW
uint64 __rdtsc( )
{
#if defined i386
   long long a;
   asm volatile("rdtsc":"=A" (a));
   return a;
#elif defined __x86_64
   unsigned int _hi,_lo;
   asm volatile("rdtsc":"=a"(_lo),"=d"(_hi));
   return ((unsigned long long int)_hi << 32) | _lo;
#endif
}
#endif*/

#ifdef BF_MINGW
#define ALL_PROCESSOR_GROUPS 0xffff
WINBASEAPI DWORD WINAPI GetActiveProcessorCount(WORD GroupNumber);
#endif

static int IntCompare(const void* a, const void* b)
{
	return *(int*)a - *(int*)b;
}

uint64 Beefy::BFGetTickCountMicroFast()
{
	if (!gTimerInitialized)
	{
		WindowsSharedInfo* windowsSharedInfo = GetSharedInfo();
		windowsSharedInfo->mThreadAcc++;

#ifdef BF_MINGW
		//TODO: Fix (needs new libkernel32.a)
		int processorCount = 4;
#else
		int processorCount = (int) ::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#endif

		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		::SetThreadAffinityMask(::GetCurrentThread(), (int64)1 << (windowsSharedInfo->mThreadAcc % processorCount));

		uint64 deltaMicro = 0;

		uint64 startMicroA = __rdtsc();
		uint32 outStartMS = timeGetTime();

		int timingSet[30];

		uint32 prevQPFMicro = 0;

		LARGE_INTEGER frequency = { 0, 1 };
		QueryPerformanceFrequency(&frequency);
		uint64 startMicro = __rdtsc();

		for (int i = 0; i < BF_ARRAY_COUNT(timingSet); i++)
		{
			uint32 qPFMicro;
			do
			{
				LARGE_INTEGER timeNow;
				QueryPerformanceCounter(&timeNow);

				qPFMicro = (uint32)((timeNow.QuadPart * 100000000) / frequency.QuadPart);
			} while (qPFMicro - prevQPFMicro < 100000);
			prevQPFMicro = qPFMicro;

			int64 curMicro = __rdtsc();
			int aDivisor = (int)(curMicro - startMicro);
			startMicro = curMicro;
			timingSet[i] = aDivisor;
		}

		qsort(timingSet, BF_ARRAY_COUNT(timingSet), sizeof(timingSet[0]), IntCompare);
		gTimerDivisor = timingSet[BF_ARRAY_COUNT(timingSet) / 3];

		//gTimerDivisor = *gTimingSet.rbegin();
		OutputDebugStrF("BFGetTickCountMicro divisor: %d\n", gTimerDivisor);

		::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_NORMAL);

		uint32 outEndMS = timeGetTime();
		uint64 endMicroA = __rdtsc();

		// It's possible we can run this test in multiple threads at once, we could wrap a CritSect around it but
		//  at least this fence will avoid the case where we have a zero gTimerDivisor
		BF_FULL_MEMORY_FENCE();
		gTimerInitialized = true;
	}

	uint64 clock = __rdtsc();
	return (clock * 1000) / gTimerDivisor;
}

uint64 Beefy::BFGetTickCountMicro()
{
	static LARGE_INTEGER freq;
	static UINT64 startTime;
	UINT64 curTime;
	LARGE_INTEGER value;

	if (!freq.QuadPart)
	{
		if (!QueryPerformanceFrequency(&freq))
		{
			ULARGE_INTEGER fileTime;
			GetSystemTimeAsFileTime((FILETIME*)&fileTime);
			return fileTime.QuadPart / 10;
		}
		QueryPerformanceCounter(&value);
		startTime = value.QuadPart;
	}
	QueryPerformanceCounter(&value);
	curTime = value.QuadPart;

	return (int64)((curTime - startTime) * (double)1000000 / freq.QuadPart);
}

static uint64 WinConvertFILETIME(const FILETIME& ft)
{
	LONGLONG ll = (int64)ft.dwHighDateTime << 32;
	ll = ll + ft.dwLowDateTime - 116444736000000000;
	return (uint64)(ll / 10000000);
}

int64 Beefy::GetFileTimeWrite(const StringImpl& path)
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
		return (int64)WinConvertFILETIME(data.ftLastWriteTime);
	else
		return 0;
}

bool Beefy::FileExists(const StringImpl& path, String* outActualName)
{
	WIN32_FIND_DATAW findData;
	HANDLE handleVal = FindFirstFileW(UTF8Decode(path).c_str(), &findData);
	if (handleVal == INVALID_HANDLE_VALUE)
		return false;
	FindClose(handleVal);
	if (outActualName != NULL)
		*outActualName = UTF8Encode(findData.cFileName);
	return (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool Beefy::DirectoryExists(const StringImpl& path, String* outActualName)
{
	WIN32_FIND_DATAW findData;
	HANDLE handleVal = FindFirstFileW(UTF8Decode(path).c_str(), &findData);
	if (handleVal == INVALID_HANDLE_VALUE)
		return false;
	FindClose(handleVal);
	if (outActualName != NULL)
		*outActualName = UTF8Encode(findData.cFileName);
	return (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void Beefy::BFFatalError(const StringImpl& message, const StringImpl& file, int line)
{
#ifndef BF_NO_BFAPP
	if (gBFApp != NULL)
		gBFApp->mSysDialogCnt++;
#endif

	String failMsg = StrFormat("%s in %s:%d", message.c_str(), file.c_str(), line);
	BfpSystem_FatalError(failMsg.c_str(), "FATAL ERROR");

#ifndef BF_NO_BFAPP
	if (gBFApp != NULL)
		gBFApp->mSysDialogCnt--;
#endif
}

//////////////////////////////////////////////////

static bool GetFileInfo(const char* path, BfpTimeStamp* lastWriteTime, uint32* fileAttributes)
{
	if (lastWriteTime != NULL)
		*lastWriteTime = 0;
	if (fileAttributes != NULL)
		*fileAttributes = 0;

	// Fast Path
	WIN32_FILE_ATTRIBUTE_DATA data = { 0 };
	if (::GetFileAttributesExW(UTF8Decode(path).c_str(), GetFileExInfoStandard, &data))
	{
		if (lastWriteTime != NULL)
			*lastWriteTime = *(BfpTimeStamp*)&data.ftLastWriteTime;
		if (fileAttributes != NULL)
			*fileAttributes = data.dwFileAttributes;

		return true;
	}

	int error = ::GetLastError();
	if ((error == ERROR_FILE_NOT_FOUND) ||
		(error == ERROR_PATH_NOT_FOUND) ||
		(error == ERROR_NOT_READY))
	{
		return false;
	}

	// Slow Path- This case is in case someone latched onto the file.  In this case, GetFileAttributes will fail but
	//  FindFirstFile will not (though it is slower)
	WIN32_FIND_DATAW findData = { 0 };
	HANDLE findHandleFrom = ::FindFirstFileW(UTF8Decode(path).c_str(), &findData);

	if (!IsHandleValid(findHandleFrom))
		return false;

	if (lastWriteTime != NULL)
		*lastWriteTime = *(BfpTimeStamp*)&findData.ftLastWriteTime;
	if (fileAttributes != NULL)
		*fileAttributes = findData.dwFileAttributes;

	::FindClose(findHandleFrom);
	return true;
}

static BfpFileAttributes FileAttributes_WinToBFP(uint32 fileAttributes)
{
	BfpFileAttributes attrs = BfpFileAttribute_None;
	if ((fileAttributes & FILE_ATTRIBUTE_NORMAL) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Normal);
	if ((fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Directory);
	if ((fileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Hidden);
	if ((fileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_System);
	if ((fileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Temporary);
	if ((fileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Offline);
	if ((fileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Encrypted);
	if ((fileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_Archive);
	if ((fileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
		attrs = (BfpFileAttributes)(attrs | BfpFileAttribute_ReadOnly);
	return attrs;
}

static uint32 FileAttributes_BFPToWin(BfpFileAttributes fileAttributes)
{
	uint32 attributes = 0;
	if ((fileAttributes & BfpFileAttribute_ReadOnly) != 0)
		attributes |= FILE_ATTRIBUTE_READONLY;
	if ((fileAttributes & BfpFileAttribute_Hidden) != 0)
		attributes |= FILE_ATTRIBUTE_HIDDEN;
	if ((fileAttributes & BfpFileAttribute_System) != 0)
		attributes |= FILE_ATTRIBUTE_SYSTEM;
	if ((fileAttributes & BfpFileAttribute_Temporary) != 0)
		attributes |= FILE_ATTRIBUTE_TEMPORARY;
	if ((fileAttributes & BfpFileAttribute_Offline) != 0)
		attributes |= FILE_ATTRIBUTE_OFFLINE;
	if ((fileAttributes & BfpFileAttribute_Encrypted) != 0)
		attributes |= FILE_ATTRIBUTE_ENCRYPTED;
	if ((fileAttributes & BfpFileAttribute_Archive) != 0)
		attributes |= FILE_ATTRIBUTE_ARCHIVE;
	if ((fileAttributes & BfpFileAttribute_Normal) != 0)
		attributes |= FILE_ATTRIBUTE_NORMAL;
	return attributes;
}

enum BfpOverlappedKind
{
	BfpOverlappedKind_FileWatcher,
	BfpOverlappedKind_File
};

struct BfpOverlapped
{
	BfpOverlappedKind mKind;
	volatile HANDLE mHandle;
	OVERLAPPED mOverlapped;

	virtual ~BfpOverlapped()
	{
	}

	virtual void Completed(int errorCode, int numBytes, OVERLAPPED* overlapped)
	{
	}
};

struct BfpOverlappedFile : BfpOverlapped
{
	Array<uint8> mRecvBuffer;

	BfpOverlappedFile()
	{
		mKind = BfpOverlappedKind_File;
	}
};

struct BfpAsyncData
{
	HANDLE mEvent;

	BfpAsyncData()
	{
		mEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	}

	~BfpAsyncData()
	{
		::CloseHandle(mEvent);
	}

	void SetEvent()
	{
		::SetEvent(mEvent);
	}

	bool WaitAndResetEvent(int timeoutMS)
	{
		while (true)
		{
			auto result = ::WaitForSingleObjectEx(mEvent, (DWORD)timeoutMS, TRUE);
			if (result == STATUS_USER_APC)
				continue; // Retry
			if (result == WAIT_OBJECT_0)
			{
				::ResetEvent(mEvent);
				return true;
			}
			return false;
		}
	}
};

struct BfpFile
{
	HANDLE mHandle;
	BfpAsyncData* mAsyncData;
	bool mIsPipe;
	bool mIsStd;

	BfpFile(HANDLE handle)
	{
		mHandle = handle;
		mAsyncData = NULL;
		mIsPipe = false;
		mIsStd = false;
	}

	BfpFile()
	{
		mHandle = 0;
		mAsyncData = NULL;
		mIsPipe = false;
		mIsStd = false;
	}

	~BfpFile()
	{
		delete mAsyncData;
	}
};

struct BfpFileWatcher : public BfpOverlapped
{
	String mPath;
	BfpDirectoryChangeFunc mDirectoryChangeFunc;
	BfpFileWatcherFlags mFlags;
	void* mUserData;
	char mBuffer[0x10000];

	void Monitor()
	{
		mOverlapped = { 0 };

		DWORD filter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SECURITY;

		DWORD size;
		DWORD success = ::ReadDirectoryChangesW(mHandle,
			mBuffer, 0x10000,
			(mFlags & BfpFileWatcherFlag_IncludeSubdirectories) != 0,
			filter,
			&size,
			&mOverlapped,
			NULL);
		if (!success)
		{
			mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Failed, mPath.c_str(), NULL, NULL);
		}
	}

	void Restart()
	{
		mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Failed, mPath.c_str(), NULL, NULL);
		OutputDebugStrF("Restarting directory monitor for %s\n", mPath.c_str());
		Monitor();
	}

	virtual void Completed(int errorCode, int numBytes, OVERLAPPED* overlapped) override
	{
		if (errorCode != 0)
		{
			if (errorCode == 995 /* ERROR_OPERATION_ABORTED */)
			{
				// Win2000 inside a service the first completion status is false?
				//  Restart file monitoring?
				return;
			}
			else
			{
				mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Failed, mPath.c_str(), NULL, NULL);
				return;
			}
		}

		if (numBytes == 0)
		{
			mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Failed, mPath.c_str(), NULL, NULL);
			return;
		}

		FILE_NOTIFY_INFORMATION* fileNotifyInformation = (FILE_NOTIFY_INFORMATION*)mBuffer;
		String oldName;
		while (true)
		{
			String fileName = UTF8Encode(UTF16String(fileNotifyInformation->FileName, fileNotifyInformation->FileNameLength / 2));
			if (fileNotifyInformation->Action == FILE_ACTION_RENAMED_OLD_NAME)
				oldName = fileName;
			else if (fileNotifyInformation->Action == FILE_ACTION_RENAMED_NEW_NAME)
				mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Renamed, mPath.c_str(), oldName.c_str(), fileName.c_str());
			else if (fileNotifyInformation->Action == FILE_ACTION_ADDED)
				mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Added, mPath.c_str(), fileName.c_str(), NULL);
			else if (fileNotifyInformation->Action == FILE_ACTION_REMOVED)
				mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Removed, mPath.c_str(), fileName.c_str(), NULL);
			else if (fileNotifyInformation->Action == FILE_ACTION_MODIFIED)
				mDirectoryChangeFunc(this, mUserData, BfpFileChangeKind_Modified, mPath.c_str(), fileName.c_str(), NULL);

			if (fileNotifyInformation->NextEntryOffset == 0)
				break;
			fileNotifyInformation = (FILE_NOTIFY_INFORMATION*)((uint8*)fileNotifyInformation + fileNotifyInformation->NextEntryOffset);
		}

		Monitor();
	}
};

class IOCPManager;
static IOCPManager* gIOCPManager;
class IOCPManager : public BfpManager
{
public:
	HANDLE mIOCompletionPort;

	Array<HANDLE> mWorkerThreads;
	CritSect mCritSect;
	bool mShuttingDown;

public:
	IOCPManager()
	{
		mIOCompletionPort = NULL;
		mShuttingDown = false;
	}

	~IOCPManager()
	{
		Shutdown();
	}

	void Shutdown()
	{
		OutputDebugStrF("IOCP.Shutdown Start\n");

		//
		{
			AutoCrit autoCrit(mCritSect);

			if (mShuttingDown)
				return;

			mShuttingDown = true;

			if (!IsHandleValid(mIOCompletionPort))
				return;
		}

		for (int i = 0; i < (int)mWorkerThreads.size(); i++)
			::PostQueuedCompletionStatus(mIOCompletionPort, 0, (ULONG_PTR)1, NULL);

		for (int i = 0; i < (int)mWorkerThreads.size(); i++)
		{
			::WaitForSingleObject(mWorkerThreads[i], INFINITE);
			::CloseHandle(mWorkerThreads[i]);
		}

		::CloseHandle(mIOCompletionPort);
		mIOCompletionPort = 0;

		OutputDebugStrF("IOCP.Shutdown Done\n");
	}

	void AddWorkerThread()
	{
		AutoCrit autoCrit(mCritSect);

		if (mShuttingDown)
			return;

		DWORD threadId;
		HANDLE handle = ::CreateThread(NULL, 0, WorkerProcThunk, (void*)this, 0, &threadId);
		mWorkerThreads.Add(handle);
	}

	void EnsureInitialized()
	{
		AutoCrit autoCrit(mCritSect);

		if ((mShuttingDown) || (mIOCompletionPort != NULL))
			return;

		mIOCompletionPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

		int workerThreads = 1;

		for (int i = 0; i < workerThreads; i++)
			AddWorkerThread();
	}

	void Add(BfpFileWatcher* fileWatcher)
	{
		//OutputDebugStrF("IOCP.Add %@\n", fileWatcher);

		EnsureInitialized();
		auto handle = ::CreateIoCompletionPort(fileWatcher->mHandle, mIOCompletionPort, (ULONG_PTR)fileWatcher, 0);
		BF_ASSERT(handle == mIOCompletionPort);
	}

	void Kill(BfpFileWatcher* fileWatcher)
	{
		//OutputDebugStrF("IOCP.Kill %@\n", fileWatcher);

		BF_ASSERT(fileWatcher->mHandle == NULL);
		delete fileWatcher;
	}

	void Remove(BfpOverlapped* bfpOverlapped)
	{
		/*if (mShuttingDown)
		{
			Kill(fileWatcher);
			return;
		}		*/

		//QueueRemove(fileWatcher);

		//OutputDebugStrF("IOCP.Remove %@\n", fileWatcher);

		AutoCrit autoCrit(mCritSect);
		auto handle = bfpOverlapped->mHandle;
		bfpOverlapped->mHandle = NULL;
		BF_FULL_MEMORY_FENCE();
		::CloseHandle(handle);
		::PostQueuedCompletionStatus(mIOCompletionPort, 0, (ULONG_PTR)bfpOverlapped | 1, NULL);
	}

	void WorkerProc()
	{
		while (true)
		{
			DWORD numBytes;
			ULONG_PTR completionKey;
			OVERLAPPED* overlapped;
			int error = 0;
			if (!::GetQueuedCompletionStatus(mIOCompletionPort, &numBytes, &completionKey, &overlapped, INFINITE))
			{
				error = GetLastError();
				if ((error == ERROR_INVALID_HANDLE) || (error == ERROR_ABANDONED_WAIT_0))
					break;
				continue;
			}

			int32 keyFlags = (int32)completionKey & 3;
			BfpOverlapped* bfpOverlapped = (BfpOverlapped*)(completionKey & ~3);

			if (keyFlags == 0) // Normal
			{
				// Check to see if we have released this
				{
					AutoCrit autoCrit(mCritSect);
					if (bfpOverlapped->mHandle == NULL)
						continue;
				}

				if (numBytes == 0)
				{
					if (bfpOverlapped->mKind == BfpOverlappedKind_FileWatcher)
					{
						auto fileWatcher = (BfpFileWatcher*)bfpOverlapped;
						if (fileWatcher->mHandle != NULL)
							fileWatcher->Restart();
						else
							Kill(fileWatcher);
					}
					continue;
				}

				bfpOverlapped->Completed(error, numBytes, overlapped);
			}
			else if (keyFlags == 1)
			{
				if (bfpOverlapped == NULL)
					break; // Normal shutdown

				if (bfpOverlapped->mKind == BfpOverlappedKind_FileWatcher)
					Kill((BfpFileWatcher*)bfpOverlapped);
			}
		}
	}

	static DWORD __stdcall WorkerProcThunk(void* _this)
	{
		BfpThread_SetName(NULL, "IOCPManager", NULL);
		((IOCPManager*)_this)->WorkerProc();
		return 0;
	}

	static IOCPManager* Get()
	{
		AutoCrit autoCrit(gBfpCritSect);
		if (gIOCPManager == NULL)
		{
			gIOCPManager = new IOCPManager();
			BfpRecordManager(gIOCPManager);
		}
		return gIOCPManager;
	}
};


static void __cdecl HandlePureVirtualFunctionCall()
{
	BfpSystem_FatalError("Pure virtual function call", NULL);
}

static void __cdecl HandleInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
	BfpSystem_FatalError("Invalid parameter", NULL);
}

static void __cdecl AbortHandler(int)
{
	BfpSystem_FatalError("Abort handler", NULL);
}

static int64 gCPUFreq = -1;
static int64 gStartCPUTick = -1;
static int64 gStartQPF = -1;

static void InitCPUFreq()
{
	if (gStartCPUTick == -1)
	{
		gStartCPUTick = __rdtsc();
		LARGE_INTEGER largeVal = { 0 };
		QueryPerformanceCounter(&largeVal);
		gStartQPF = largeVal.QuadPart;
	}
}

static void(*sOldSIGABRTHandler)(int signal) = nullptr;

BFP_EXPORT void BFP_CALLTYPE BfpSystem_Init(int version, BfpSystemInitFlags flags)
{
	InitCPUFreq();

	::_set_error_mode(_OUT_TO_STDERR);

#ifdef _DEBUG
	::_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	::SetErrorMode(SEM_FAILCRITICALERRORS);

	if (version != BFP_VERSION)
	{
		BfpSystem_FatalError(StrFormat("Bfp build version '%d' does not match requested version '%d'", BFP_VERSION, version).c_str(), "BFP FATAL ERROR");
	}

	if ((flags & BfpSystemInitFlag_InstallCrashCatcher) != 0)
	{
		// Set up a handler for pure virtual function calls
		_set_purecall_handler(HandlePureVirtualFunctionCall);
		// Set up a handler for invalid parameters such as printf(NULL);
		_set_invalid_parameter_handler(HandleInvalidParameter);

		// Set up a handler for abort(). This is done in two steps.
		// First we ask the CRT to call an abort handler. This is the
		// default, but explicitly setting it seems like a good idea.
		_set_abort_behavior(_CALL_REPORTFAULT, _CALL_REPORTFAULT);
		// Then we install our abort handler.
		sOldSIGABRTHandler = signal(SIGABRT, &AbortHandler);
		if (sOldSIGABRTHandler == SIG_ERR)
			sOldSIGABRTHandler = nullptr;

		CrashCatcher::Get()->Init();
		if ((flags & BfpSystemInitFlag_SilentCrash) != 0)
			CrashCatcher::Get()->SetCrashReportKind(BfpCrashReportKind_None);
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCommandLine(int argc, char** argv)
{
	// This isn't required on Windows, but it is on Linux
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCrashReportKind(BfpCrashReportKind crashReportKind)
{
	CrashCatcher::Get()->SetCrashReportKind(crashReportKind);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfoFunc(BfpCrashInfoFunc crashInfoFunc)
{
	CrashCatcher::Get()->AddCrashInfoFunc(crashInfoFunc);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfo(const char* str)
{
	CrashCatcher::Get()->AddInfo(str);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCrashRelaunchCmd(const char* str)
{
	CrashCatcher::Get()->SetRelaunchCmd(str);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_Shutdown()
{
	while (gManagerTail != NULL)
	{
		auto next = gManagerTail->mNext;
		delete gManagerTail;
		gManagerTail = next;
	}

	if (CrashCatcher::Shutdown())
	{
		_set_purecall_handler(nullptr);
		_set_invalid_parameter_handler(nullptr);
		signal(SIGABRT, sOldSIGABRTHandler);
	}
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_TickCount()
{
	return BFTickCount();
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpSystem_GetTimeStamp()
{
	BfpTimeStamp timeStamp = 0;
	GetSystemTimeAsFileTime((FILETIME*)&timeStamp);
	return timeStamp;
}

BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_EndianSwap16(uint16 val)
{
	return _byteswap_ushort(val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_EndianSwap32(uint32 val)
{
	return _byteswap_ulong(val);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_EndianSwap64(uint64 val)
{
	return _byteswap_uint64(val);
}

BFP_EXPORT uint8 BFP_CALLTYPE BfpSystem_InterlockedExchange8(uint8* ptr, uint8 val) // Returns the initial value in 'ptr'
{
	return ::InterlockedExchange8((volatile char*)ptr, val);
}

BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedExchange16(uint16* ptr, uint16 val) // Returns the initial value in 'ptr'
{
	return ::InterlockedExchange16((volatile short*)ptr, val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchange32(uint32* ptr, uint32 val) // Returns the initial value in 'ptr'
{
	return ::InterlockedExchange((volatile uint32*)ptr, val);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchange64(uint64* ptr, uint64 val)
{
	return ::InterlockedExchange((volatile uint64*)ptr, val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd8(uint8* ptr, uint8 val) // Returns the initial value in 'ptr'
{
	return ::InterlockedExchangeAdd8((volatile char*)ptr, val);
}

BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd16(uint16* ptr, uint16 val) // Returns the initial value in 'ptr'
{
	return ::_InterlockedExchangeAdd16((volatile short*)ptr, val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd32(uint32* ptr, uint32 val) // Returns the initial value in 'ptr'
{
	return ::InterlockedExchangeAdd((volatile uint32*)ptr, val);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd64(uint64* ptr, uint64 val)
{
	return ::InterlockedExchangeAdd((volatile uint64*)ptr, val);
}

BFP_EXPORT uint8 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange8(uint8* ptr, uint8 oldVal, uint8 newVal)
{
	return ::_InterlockedCompareExchange8((volatile char*)ptr, newVal, oldVal);
}

BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange16(uint16* ptr, uint16 oldVal, uint16 newVal)
{
	return ::_InterlockedCompareExchange16((volatile short*)ptr, newVal, oldVal);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange32(uint32* ptr, uint32 oldVal, uint32 newVal)
{
	return ::InterlockedCompareExchange((volatile uint32*)ptr, newVal, oldVal);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange64(uint64* ptr, uint64 oldVal, uint64 newVal)
{
	return ::InterlockedCompareExchange64((volatile int64*)ptr, newVal, oldVal);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_FatalError(const char* error, const char* title)
{
	if (title != NULL)
		CrashCatcher::Get()->Crash(String(title) + "\n" + String(error));
	else
		CrashCatcher::Get()->Crash(error);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetCommandLine(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
	WCHAR* ptr = ::GetCommandLineW();

	UTF16String wString(ptr);
	String env = UTF8Encode(wString);

	TryStringOut(env, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetExecutablePath(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
	WCHAR path[MAX_PATH];
	GetModuleFileNameW(NULL, path, MAX_PATH);

	String env = UTF8Encode(path);
	TryStringOut(env, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetEnvironmentStrings(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
	WCHAR* ptr = ::GetEnvironmentStringsW();
	WCHAR* endPtr = ptr;

	while (true)
	{
		if ((endPtr[0] == 0) && (endPtr[1] == 0))
			break;
		endPtr++;
	}

	UTF16String wString(ptr, (int)(endPtr - ptr + 2));
	String env = UTF8Encode(wString);
	::FreeEnvironmentStringsW(ptr);

	TryStringOut(env, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT int BFP_CALLTYPE BfpSystem_GetNumLogicalCPUs(BfpSystemResult* outResult)
{
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	OUTRESULT(BfpSystemResult_Ok);
	return sysInfo.dwNumberOfProcessors;
}

BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTick()
{
	return __rdtsc();
}

BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTickFreq()
{
	LARGE_INTEGER largeVal = { 0 };
	QueryPerformanceFrequency(&largeVal);
	int64 qpfFreq = largeVal.QuadPart;

	if (gStartCPUTick == -1)
	{
		InitCPUFreq();
		Sleep(10);
	}

	int64 cpuTick1 = __rdtsc();
	QueryPerformanceCounter(&largeVal);
	int64 slowTick1 = largeVal.QuadPart;

	int64 cpuElapsed = cpuTick1 - gStartCPUTick;
	int64 slowElapsed = slowTick1 - gStartQPF;
	double elapsedSeconds = slowElapsed / (double)qpfFreq;
	int64 freq = (int64)(cpuElapsed / elapsedSeconds);
	gCPUFreq = freq;

	return gCPUFreq;
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_CreateGUID(BfpGUID* outGuid)
{
	memset(outGuid, 0, sizeof(BfpGUID));
	UuidCreate((UUID*)outGuid);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetComputerName(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
	char computerName[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
	DWORD computerNameSize = MAX_COMPUTERNAME_LENGTH;
	GetComputerNameA(computerName, &computerNameSize);
	TryStringOut(computerName, outStr, inOutStrSize, (BfpResult*)outResult);
}

/// BfpFileWatcher

BFP_EXPORT BfpFileWatcher* BFP_CALLTYPE BfpFileWatcher_WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult)
{
	HANDLE directoryHandle = CreateFileW(
		UTF8Decode(path).c_str(), // Directory name
		FILE_LIST_DIRECTORY, // access (read-write) mode
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, // security descriptor
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,  // file attributes
		NULL // file with attributes to copy
	);

	if (!IsHandleValid(directoryHandle))
	{
		OUTRESULT(BfpFileResult_UnknownError);
		return NULL;
	}

	BfpFileWatcher* fileWatcher = new BfpFileWatcher();
	fileWatcher->mKind = BfpOverlappedKind_FileWatcher;
	fileWatcher->mPath = path;
	fileWatcher->mDirectoryChangeFunc = callback;
	fileWatcher->mHandle = directoryHandle;
	fileWatcher->mFlags = flags;
	fileWatcher->mUserData = userData;
	IOCPManager::Get()->Add(fileWatcher);
	fileWatcher->Monitor();

	return fileWatcher;
}

BFP_EXPORT void BFP_CALLTYPE BfpFileWatcher_Release(BfpFileWatcher* fileWatcher)
{
	IOCPManager::Get()->Remove(fileWatcher);
	//::CloseHandle(fileWatcher->mFileHandle);
	//fileWatcher->mFileHandle = NULL;

	//delete fileWatcher;
}

/// BfpProcess

BFP_EXPORT intptr BFP_CALLTYPE BfpProcess_GetCurrentId()
{
	return ::GetCurrentProcessId();
}

struct BfpProcess
{
	int mProcessId;
	SYSTEM_PROCESS_INFORMATION* mInfo;
	String mImageName;
};

BFP_EXPORT bool BFP_CALLTYPE BfpProcess_IsRemoteMachine(const char* machineName)
{
	return false;
}

BFP_EXPORT BfpProcess* BFP_CALLTYPE BfpProcess_GetById(const char* machineName, int processId, BfpProcessResult* outResult)
{
	BfpProcess* process = new BfpProcess();
	process->mProcessId = processId;
	process->mInfo = NULL;
	return process;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_Enumerate(const char* machineName, BfpProcess** outProcesses, int* inOutProcessesSize, BfpProcessResult* outResult)
{
	ImportNTDll();
	if (gNtQuerySystemInformation == NULL)
	{
		*inOutProcessesSize = 0;
		return;
	}

	uint allocSize = 1024;
	uint8* data = NULL;

	while (true)
	{
		data = new uint8[allocSize];

		ULONG wantSize = 0;
		NTSTATUS status = gNtQuerySystemInformation(SystemProcessInformation, data, allocSize, &wantSize);

		if (status != STATUS_INFO_LENGTH_MISMATCH)
		{
			auto processInfo = (SYSTEM_PROCESS_INFORMATION*)data;

			auto curProcessInfo = processInfo;
			int count = 0;
			while (true)
			{
				if (curProcessInfo == NULL)
					break;

				count++;
				if (curProcessInfo->NextEntryOffset == 0)
					break;
				curProcessInfo = (SYSTEM_PROCESS_INFORMATION*)((intptr)curProcessInfo + curProcessInfo->NextEntryOffset);
			}

			if (count > *inOutProcessesSize)
			{
				*inOutProcessesSize = count;
				OUTRESULT(BfpProcessResult_InsufficientBuffer);
				delete data;
				return;
			}

			curProcessInfo = processInfo;
			count = 0;
			while (true)
			{
				if (curProcessInfo == NULL)
					break;

				if (curProcessInfo->ProcessId != 0)
				{
					BfpProcess* process = new BfpProcess();
					int dataSize = sizeof(SYSTEM_PROCESS_INFORMATION) + sizeof(SYSTEM_THREAD) * (curProcessInfo->NumberOfThreads - 1);
					process->mProcessId = (int)(intptr)curProcessInfo->ProcessId;
					process->mInfo = (SYSTEM_PROCESS_INFORMATION*)malloc(dataSize);
					memcpy(process->mInfo, curProcessInfo, dataSize);

					UTF16String utf16;
					utf16.Set(curProcessInfo->ImageName.Buffer, curProcessInfo->ImageName.Length / 2);
					process->mImageName = UTF8Encode(utf16);

					outProcesses[count++] = process;
				}

				if (curProcessInfo->NextEntryOffset == 0)
					break;
				curProcessInfo = (SYSTEM_PROCESS_INFORMATION*)((intptr)curProcessInfo + curProcessInfo->NextEntryOffset);
			}
			*inOutProcessesSize = count;

			OUTRESULT(BfpProcessResult_Ok);
			delete data;
			return;
		}

		allocSize = wantSize + 4096;
		delete data;
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_Release(BfpProcess* process)
{
	if (process->mInfo != NULL)
		free(process->mInfo);
	delete process;
}

//struct EnumWndData
//{
//	HWND mBestHandle;
//	uint32 mProcessId;
//};

struct MainWindowCache
{
	Dictionary<uint32, HWND> mMap;
	uint32 mLastUpdate;

	MainWindowCache()
	{
		mLastUpdate = 0;
	}
};

static MainWindowCache gMainWindowCache;

static BOOL CALLBACK EnumWndProc(HWND hWnd, LPARAM param)
{
	//EnumWndData* enumWndData = (EnumWndData*)param;

	DWORD processId = 0;

	::GetWindowThreadProcessId(hWnd, &processId);

	if (gMainWindowCache.mMap.ContainsKey(processId))
	{
		//if (processId == 6720)
			//OutputDebugStrF("Bailed\n");
		return TRUE;
	}

	if ((::IsWindowVisible(hWnd)) && (::GetWindow(hWnd, GW_OWNER) == NULL))
	{
		//if (processId == 6720)
			//OutputDebugStrF("Set\n");

		gMainWindowCache.mMap[processId] = hWnd;
	}

	return TRUE;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetMainWindowTitle(BfpProcess* process, char* outTitle, int* inOutTitleSize, BfpProcessResult* outResult)
{
	HWND mainWindow = NULL;

	//
	{
		AutoCrit autoCrit(gBfpCritSect);

		if ((gMainWindowCache.mLastUpdate == 0) || (BFTickCount() - gMainWindowCache.mLastUpdate > 100))
		{
			gMainWindowCache.mMap.Clear();
			::EnumWindows(EnumWndProc, 0);
			gMainWindowCache.mLastUpdate = BFTickCount();
		}

		gMainWindowCache.mMap.TryGetValue(process->mProcessId, &mainWindow);
	}

	/*EnumWndData enumWndData;
	enumWndData.mBestHandle = NULL;
	enumWndData.mProcessId = process->mProcessId;*/

	String title;

	if (mainWindow != NULL)
	{
		int wantSize = 128;

		while (true)
		{
			WCHAR* str = new WCHAR[wantSize];

			int size = ::GetWindowTextW(mainWindow, str, wantSize) + 1;

			if (size <= 0)
			{
				delete str;
				break;
			}

			if (size <= wantSize)
			{
				title = UTF8Encode(str);
				delete str;
				break;
			}

			delete str;
			wantSize = size;
		}
	}

	TryStringOut(title, outTitle, inOutTitleSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetProcessName(BfpProcess* process, char* outName, int* inOutNameSize, BfpProcessResult* outResult)
{
	if (process->mImageName.IsEmpty())
	{
		HANDLE hProc = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process->mProcessId);
		if (hProc == INVALID_HANDLE_VALUE)
		{
			OUTRESULT(BfpProcessResult_UnknownError);
			return;
		}
		WCHAR wName[MAX_PATH];
		::GetModuleFileNameExW(hProc, NULL, wName, MAX_PATH);
		::CloseHandle(hProc);
		String name = UTF8Encode(wName);
		process->mImageName = name;
	}

	TryStringOut(process->mImageName, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT int BFP_CALLTYPE BfpProcess_GetProcessId(BfpProcess* process)
{
	return process->mProcessId;
}

BFP_EXPORT int BFP_CALLTYPE BfpProcess_GetRunningTime(BfpProcess* process)
{
	if (process->mInfo == NULL)
		return -1;
	return (int)(process->mInfo->CreateTime.QuadPart / 10);
}

/// BfpSpawn

struct BfpSpawn
{
public:
	String mArgsParamFilePath;
	HANDLE mHProcess;
	DWORD mProcessId;
	HANDLE mStandardInputWritePipeHandle;
	HANDLE mStandardOutputReadPipeHandle;
	HANDLE mStandardErrorReadPipeHandle;
	bool mIsDone;

public:
	BfpSpawn()
	{
		mHProcess = 0;
		mProcessId = 0;
		mStandardInputWritePipeHandle = 0;
		mStandardOutputReadPipeHandle = 0;
		mStandardErrorReadPipeHandle = 0;
		mIsDone = false;
	}

	~BfpSpawn()
	{
		if (IsHandleValid(mHProcess))
			::CloseHandle(mHProcess);

		if (IsHandleValid(mStandardInputWritePipeHandle))
			::CloseHandle(mStandardInputWritePipeHandle);
		if (IsHandleValid(mStandardOutputReadPipeHandle))
			::CloseHandle(mStandardOutputReadPipeHandle);
		if (IsHandleValid(mStandardErrorReadPipeHandle))
			::CloseHandle(mStandardErrorReadPipeHandle);
	}

	static bool CreatePipeWithSecurityAttributes(HANDLE& hReadPipe, HANDLE& hWritePipe, SECURITY_ATTRIBUTES* lpPipeAttributes, int32 nSize)
	{
		hReadPipe = 0;
		hWritePipe = 0;
		bool ret = ::CreatePipe(&hReadPipe, &hWritePipe, lpPipeAttributes, nSize);
		if (!ret || (hReadPipe == INVALID_HANDLE_VALUE) || (hWritePipe == INVALID_HANDLE_VALUE))
			return false;
		return true;
	}

	// Using synchronous Anonymous pipes for process input/output redirection means we would end up
	// wasting a worker threadpool thread per pipe instance. Overlapped pipe IO is desirable, since
	// it will take advantage of the NT IO completion port infrastructure. But we can't really use
	// Overlapped I/O for process input/output as it would break Console apps (managed Console class
	// methods such as WriteLine as well as native CRT functions like printf) which are making an
	// assumption that the console standard handles (obtained via GetStdHandle()) are opened
	// for synchronous I/O and hence they can work fine with ReadFile/WriteFile synchrnously!
	bool CreatePipe(HANDLE& parentHandle, HANDLE& childHandle, bool parentInputs)
	{
		SECURITY_ATTRIBUTES securityAttributesParent = { 0 };
		securityAttributesParent.bInheritHandle = 1;

		HANDLE hTmp = INVALID_HANDLE_VALUE;
		if (parentInputs)
			CreatePipeWithSecurityAttributes(childHandle, hTmp, &securityAttributesParent, 0);
		else
			CreatePipeWithSecurityAttributes(hTmp, childHandle, &securityAttributesParent, 0);

		HANDLE dupHandle = 0;

		// Duplicate the parent handle to be non-inheritable so that the child process
		// doesn't have access. This is done for correctness sake, exact reason is unclear.
		// One potential theory is that child process can do something brain dead like
		// closing the parent end of the pipe and there by getting into a blocking situation
		// as parent will not be draining the pipe at the other end anymore.
		if (!::DuplicateHandle(GetCurrentProcess(), hTmp,
			GetCurrentProcess(), &dupHandle,
			0, false, DUPLICATE_SAME_ACCESS))
		{
			return false;
		}

		parentHandle = dupHandle;

		if (hTmp != INVALID_HANDLE_VALUE)
			::CloseHandle(hTmp);

		return true;
	}

	bool StartWithCreateProcess(const char* targetPath, const char* args, const char* workingDir, const char* env, BfpSpawnFlags flags, BfpSpawnResult* outResult)
	{
		String fileName = targetPath;

		String commandLine;

		if ((flags & BfpSpawnFlag_ArgsIncludesTarget) == 0)
		{
			if ((!fileName.StartsWith("\"")) && (!fileName.EndsWith("\"")))
			{
				bool needsQuoting = false;
				for (int i = 0; i < (int)fileName.length(); i++)
				{
					char c = fileName[i];
					if (c == ' ')
						needsQuoting = true;
				}

				if (needsQuoting)
				{
					commandLine.Append('\"');
					commandLine.Append(fileName);
					commandLine.Append('\"');
				}
				else
				{
					commandLine.Append(fileName);
				}
			}
			else
			{
				commandLine.Append(fileName);
			}
		}

		if ((args != NULL) && (args[0] != '0'))
		{
			if (!commandLine.IsEmpty())
				commandLine.Append(' ');
			commandLine.Append(args);
		}

		STARTUPINFOW startupInfo = { 0 };
		PROCESS_INFORMATION processInfo = { 0 };

		bool retVal;
		int32 errorCode = 0;

		//
		{
			// set up the streams
			if ((flags & (BfpSpawnFlag_RedirectStdInput | BfpSpawnFlag_RedirectStdOutput | BfpSpawnFlag_RedirectStdError)) != 0)
			{
				if ((flags & BfpSpawnFlag_RedirectStdInput) != 0)
					CreatePipe(mStandardInputWritePipeHandle, startupInfo.hStdInput, true);
				else if (::GetConsoleWindow() != NULL)
					startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
				else
					startupInfo.hStdInput = INVALID_HANDLE_VALUE;

				if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
					CreatePipe(mStandardOutputReadPipeHandle, startupInfo.hStdOutput, false);
				else
					startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

				if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
					CreatePipe(mStandardErrorReadPipeHandle, startupInfo.hStdError, false);
				else
					startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

				startupInfo.dwFlags = STARTF_USESTDHANDLES;
			}

			// set up the creation flags paramater
			int32 creationFlags = 0;

			if ((flags & BfpSpawnFlag_NoWindow) != 0)
				creationFlags |= CREATE_NO_WINDOW;
			// set up the environment block parameter

			WCHAR* targetStrPtr = NULL;
			UTF16String targetStrW;
			if ((flags & BfpSpawnFlag_ArgsIncludesTarget) != 0)
			{
				// If we already included the target in the args then the actual target may be different, so we need to explicitly pass that
				targetStrW = UTF8Decode(targetPath);
				targetStrPtr = (WCHAR*)targetStrW.c_str();
			}

			WCHAR* dirStrPtr = NULL;
			UTF16String dirStrW;
			if ((workingDir != NULL) && (workingDir[0] != 0))
			{
				dirStrW = UTF8Decode(workingDir);
				dirStrPtr = (WCHAR*)dirStrW.c_str();
			}

			UTF16String envW;
			void* envVoidPtr = NULL;

			if ((env != NULL) && (env[0] != 0))
			{
				bool useUnicodeEnv = false;
				if (useUnicodeEnv)
				{
					const char* envPtr = env;
					while (true)
					{
						if ((envPtr[0] == 0) && (envPtr[1] == 0))
							break;
						envPtr++;
					}

					int envSize = (int)(envPtr - env) + 2;
					String str8(env, envSize);
					envW = UTF8Decode(str8);
					envVoidPtr = (void*)envW.c_str();
					startupInfo.dwFlags |= CREATE_UNICODE_ENVIRONMENT;
				}
				else
				{
					envVoidPtr = (void*)env;
				}
			}

			retVal = ::CreateProcessW(
				targetStrPtr,
				(WCHAR*)UTF8Decode(commandLine).c_str(), // pointer to the command line string
				NULL,               // pointer to process security attributes, we don't need to inherit the handle
				NULL,               // pointer to thread security attributes
				true,               // handle inheritance flag
				creationFlags,      // creation flags
				envVoidPtr,     // pointer to new environment block
				dirStrPtr,   // pointer to current directory name
				&startupInfo,        // pointer to STARTUPINFO
				&processInfo         // pointer to PROCESS_INFORMATION
			);

			if (!retVal)
				errorCode = ::GetLastError();

			if (processInfo.hThread != INVALID_HANDLE_VALUE)
				::CloseHandle(processInfo.hThread);

			if ((flags & BfpSpawnFlag_RedirectStdInput) != 0)
				::CloseHandle(startupInfo.hStdInput);
			if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
				::CloseHandle(startupInfo.hStdOutput);
			if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
				::CloseHandle(startupInfo.hStdError);

			if (!retVal)
			{
				if (IsHandleValid(mStandardInputWritePipeHandle))
				{
					::CloseHandle(mStandardInputWritePipeHandle);
					mStandardInputWritePipeHandle = 0;
				}
				if (IsHandleValid(mStandardOutputReadPipeHandle))
				{
					::CloseHandle(mStandardOutputReadPipeHandle);
					mStandardOutputReadPipeHandle = 0;
				}
				if (IsHandleValid(mStandardErrorReadPipeHandle))
				{
					::CloseHandle(mStandardErrorReadPipeHandle);
					mStandardErrorReadPipeHandle = 0;
				}

				OUTRESULT(BfpSpawnResult_UnknownError);
				if (errorCode == ERROR_BAD_EXE_FORMAT || errorCode == ERROR_EXE_MACHINE_TYPE_MISMATCH)
				{
					return false;
				}
				return false;
			}
		}

		bool ret = false;
		if (IsHandleValid(processInfo.hProcess))
		{
			mHProcess = processInfo.hProcess;
			mProcessId = processInfo.dwProcessId;
			ret = true;
		}

		if (!ret)
		{
			OUTRESULT(BfpSpawnResult_UnknownError);
			return false;
		}
		return true;
	}

	bool StartWithShellExecute(const char* targetPath, const char* args, const char* workingDir, const char* env, BfpSpawnFlags flags, BfpSpawnResult* outResult)
	{
		SHELLEXECUTEINFOW shellExecuteInfo = { 0 };
		shellExecuteInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
		shellExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
		if ((flags & BfpSpawnFlag_ErrorDialog) != 0)
		{
			//shellExecuteInfo.hwnd = startInfo.ErrorDialogParentHandle;
		}
		else
		{
			shellExecuteInfo.fMask |= SEE_MASK_FLAG_NO_UI;
		}

		if ((flags & BfpSpawnFlag_NoWindow) != 0)
			shellExecuteInfo.nShow = SW_HIDE;
		else
			shellExecuteInfo.nShow = SW_SHOWNORMAL;

		UTF16String fileW;
		UTF16String verbW;
		UTF16String argsW;
		UTF16String dirW;

		String target = targetPath;
		int barPos = (int)target.IndexOf('|');
		if (barPos != -1)
		{
			fileW = UTF8Decode(target.Substring(0, barPos));
			shellExecuteInfo.lpFile = fileW.c_str();

			verbW = UTF8Decode(target.Substring(barPos + 1));
			shellExecuteInfo.lpVerb = verbW.c_str();
		}
		else
		{
			fileW = UTF8Decode(target);
			shellExecuteInfo.lpFile = fileW.c_str();
		}

		if ((args != NULL) && (args[0] != 0))
		{
			argsW = UTF8Decode(args);
			shellExecuteInfo.lpParameters = argsW.c_str();
		}

		if ((workingDir != NULL) && (workingDir[0] != 0))
		{
			dirW = UTF8Decode(workingDir);
			shellExecuteInfo.lpDirectory = dirW.c_str();
		}

		shellExecuteInfo.fMask |= SEE_MASK_FLAG_DDEWAIT;



		BOOL success = ::ShellExecuteExW(&shellExecuteInfo);
		if (!success)
		{
			int lastError = ::GetLastError();
			OUTRESULT(BfpSpawnResult_UnknownError);
			return false;
		}

		mHProcess = shellExecuteInfo.hProcess;
		return true;
	}
};

BFP_EXPORT BfpSpawn* BFP_CALLTYPE BfpSpawn_Create(const char* targetPath, const char* args, const char* workingDir, const char* env, BfpSpawnFlags flags, BfpSpawnResult* outResult)
{
	String newArgs;
	String tempFileName;

	if ((flags & BfpSpawnFlag_UseArgsFile) != 0)
	{
		char tempPathStr[MAX_PATH];
		::GetTempPathA(MAX_PATH, tempPathStr);

		char tempFileNameStr[MAX_PATH];
		::GetTempFileNameA(tempPathStr, "BFP", 0, tempFileNameStr);

		tempFileName = tempFileNameStr;

		BfpFileResult fileResult;
		BfpFile* file = BfpFile_Create(tempFileNameStr, BfpFileCreateKind_CreateAlways, BfpFileCreateFlag_Write, BfpFileAttribute_Normal, &fileResult);
		if (file == NULL)
		{
			OUTRESULT(BfpSpawnResult_TempFileError);
			return NULL;
		}

		if ((flags & BfpSpawnFlag_UseArgsFile_Native) != 0)
		{
			UTF16String wStr = UTF8Decode(args);

			if ((flags & BfpSpawnFlag_UseArgsFile_BOM) != 0)
			{
				uint8 bom[2] = { 0xFF, 0xFE };
				BfpFile_Write(file, bom, 2, -1, NULL);
			}

			BfpFile_Write(file, wStr.c_str(), wStr.length() * 2, -1, NULL);
		}
		else
			BfpFile_Write(file, args, strlen(args), -1, NULL);
		BfpFile_Release(file);

		newArgs.Append("@");
		newArgs.Append(tempFileName);
		if (newArgs.Contains(' '))
		{
			newArgs.Insert(0, '\"');
			newArgs.Append('\"');
		}

		args = newArgs.c_str();
	}

	BfpSpawn* spawn = new BfpSpawn();
	bool success = false;
	if ((flags & BfpSpawnFlag_UseShellExecute) != 0)
		success = spawn->StartWithShellExecute(targetPath, args, workingDir, env, flags, outResult);
	else
		success = spawn->StartWithCreateProcess(targetPath, args, workingDir, env, flags, outResult);
	if (!success)
	{
		delete spawn;
		return NULL;
	}
	spawn->mArgsParamFilePath = tempFileName;
	return spawn;
}

BFP_EXPORT void BFP_CALLTYPE BfpSpawn_Release(BfpSpawn* spawn)
{
	if ((!spawn->mArgsParamFilePath.IsEmpty()) && (spawn->mIsDone))
		BfpFile_Delete(spawn->mArgsParamFilePath.c_str(), NULL);

	delete spawn;
}

// static BOOL CALLBACK TerminateAppEnum(HWND hwnd, LPARAM lParam)
// {
// 	DWORD dwID;
// 	GetWindowThreadProcessId(hwnd, &dwID);
//
// 	if (dwID == (DWORD)lParam)
// 	{
// 		PostMessage(hwnd, WM_CLOSE, 0, 0);
// 	}
//
// 	return TRUE;
// }

BFP_EXPORT void BFP_CALLTYPE BfpSpawn_Kill(BfpSpawn* spawn, int exitCode, BfpKillFlags killFlags, BfpSpawnResult* outResult)
{
	//::EnumWindows((WNDENUMPROC)TerminateAppEnum, (LPARAM)spawn->mProcessId);

	if ((killFlags & BfpKillFlag_KillChildren) != 0)
	{
		ImportNTDll();

		HashSet<int> killSet;
		killSet.Add(spawn->mProcessId);
		HashSet<int> killedSet;
		killedSet.Add(spawn->mProcessId);

		// New child processes can launch between the NtQuerySystemInformation call and the actual process termination,
		//  so we need to run multiple loops until we stop finding child processes
		while (true)
		{
			uint allocSize = 8192;
			uint8* data;
			while (true)
			{
				data = new uint8[allocSize];

				ULONG wantSize = 0;
				NTSTATUS status = gNtQuerySystemInformation(SystemProcessInformation, data, allocSize, &wantSize);

				if (status != STATUS_INFO_LENGTH_MISMATCH)
				{
					bool foundNew;
					do
					{
						foundNew = false;
						auto processInfo = (SYSTEM_PROCESS_INFORMATION*)data;

						auto curProcessInfo = processInfo;
						while (true)
						{
							if (curProcessInfo == NULL)
								break;

							if (killSet.Contains((int)(intptr)curProcessInfo->InheritedFromProcessId))
							{
								if (killSet.Add((int)(intptr)curProcessInfo->ProcessId))
									foundNew = true;
							}

							if (curProcessInfo->NextEntryOffset == 0)
								break;
							curProcessInfo = (SYSTEM_PROCESS_INFORMATION*)((intptr)curProcessInfo + curProcessInfo->NextEntryOffset);
						}
					} while (foundNew);

					delete data;
					break;
				}

				allocSize = wantSize + 4096;
				delete data;
			}

			if (killedSet.size() == killSet.size())
				break;

			for (auto pid : killSet)
			{
				if (!killedSet.Add(pid))
					continue;

				HANDLE procHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)pid);
				if (procHandle != NULL)
				{
					::TerminateProcess(procHandle, exitCode);
					// 			DWORD threadId = 0;
					// 			::CreateRemoteThread(procHandle, NULL, 0, (LPTHREAD_START_ROUTINE)ExitProcess, (void*)(intptr)exitCode, 0, &threadId);
					// 			if (pid != spawn->mProcessId)
					// 				::CloseHandle(procHandle);
				}
			}
		}
	}

	if (!::TerminateProcess(spawn->mHProcess, (UINT)exitCode))
	{
		int lastError = ::GetLastError();
		OUTRESULT(BfpSpawnResult_UnknownError);
		return;
	}

	//  	BOOL hadConsole = ::FreeConsole();
	//  	::AttachConsole(spawn->mProcessId);
	//  	::SetConsoleCtrlHandler(NULL, true);
	//  	::GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
	//  	//::Sleep(2000);
	//  	::FreeConsole();
	// 	::SetConsoleCtrlHandler(NULL, false);

	// 	if (!::TerminateProcess(spawn->mHProcess, (UINT)exitCode))
	// 	{
	// 		int lastError = ::GetLastError();
	// 		OUTRESULT(BfpSpawnResult_UnknownError);
	// 		return;
	// 	}
	OUTRESULT(BfpSpawnResult_Ok);
}

BFP_EXPORT bool BFP_CALLTYPE BfpSpawn_WaitFor(BfpSpawn* spawn, int waitMS, int* outExitCode, BfpSpawnResult* outResult)
{
	if (::WaitForSingleObject(spawn->mHProcess, waitMS) != WAIT_OBJECT_0)
	{
		OUTRESULT(BfpSpawnResult_UnknownError);
		return false;
	}

	spawn->mIsDone = true;
	::GetExitCodeProcess(spawn->mHProcess, (DWORD*)outExitCode);
	OUTRESULT(BfpSpawnResult_Ok);
	return true;
}

BFP_EXPORT void BFP_CALLTYPE BfpSpawn_GetStdHandles(BfpSpawn* spawn, BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
	if (outStdIn != NULL)
	{
		*outStdIn = new BfpFile(spawn->mStandardInputWritePipeHandle);
		spawn->mStandardInputWritePipeHandle = 0;
	}

	if (outStdOut != NULL)
	{
		*outStdOut = new BfpFile(spawn->mStandardOutputReadPipeHandle);
		spawn->mStandardOutputReadPipeHandle = 0;
	}

	if (outStdErr != NULL)
	{
		*outStdErr = new BfpFile(spawn->mStandardErrorReadPipeHandle);
		spawn->mStandardErrorReadPipeHandle = 0;
	}
}

/// BfpThread

BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_Create(BfpThreadStartProc startProc, void* threadParam, intptr stackSize, BfpThreadCreateFlags flags, BfpThreadId* outThreadId)
{
	DWORD creationFlags = 0;
	if ((flags & BfpThreadCreateFlag_Suspended) != 0)
		creationFlags |= CREATE_SUSPENDED;
	if (((flags & BfpThreadCreateFlag_StackSizeReserve) != 0) && (stackSize != 0))
		creationFlags |= STACK_SIZE_PARAM_IS_A_RESERVATION;

	DWORD threadId;
	// We have an explicit LPTHREAD_START_ROUTINE cast because BfpThreadStartProc returns 'void'.  This doesn't break the calling convention, it just means
	//  the return register value is garbage
	HANDLE handle = ::CreateThread(NULL, (SIZE_T)stackSize, (LPTHREAD_START_ROUTINE)startProc, threadParam, creationFlags, &threadId);
	if (outThreadId != NULL)
		*outThreadId = threadId;

	return (BfpThread*)handle;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Release(BfpThread* thread)
{
	::CloseHandle((HANDLE)thread);
}

static void SetThreadName(DWORD threadId, const char* name)
{
	const DWORD MS_VC_EXCEPTION = 0x406D1388;

	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;

	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = name;
	info.dwThreadID = threadId;
	info.dwFlags = 0;

	__try
	{
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_SetName(BfpThread* thread, const char* name, BfpThreadResult* outResult)
{
	ImportKernel();

	HANDLE hThread = (HANDLE)thread;
	if (hThread == NULL)
		hThread = ::GetCurrentThread();

	if (gSetThreadDescription != NULL)
	{
		gSetThreadDescription(hThread, UTF8Decode(name).c_str());

		OUTRESULT(BfpThreadResult_Ok);
		return;
	}

	SetThreadName(::GetThreadId(hThread), name);

	OUTRESULT(BfpThreadResult_UnknownError);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_GetName(BfpThread* thread, char* outName, int* inOutNameSize, BfpThreadResult* outResult)
{
	ImportKernel();

	PWSTR wStr = NULL;
	if (gGetThreadDescription != NULL)
	{
		gGetThreadDescription((HANDLE)thread, &wStr);
		if (wStr == NULL)
		{
			OUTRESULT(BfpThreadResult_UnknownError);
			return;
		}

		String str = UTF8Encode(wStr);
		TryStringOut(str, outName, inOutNameSize, (BfpResult*)outResult);

		LocalFree(wStr);
		return;
	}

	OUTRESULT(BfpThreadResult_UnknownError);
}

BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_GetCurrent()
{
	return (BfpThread*)::OpenThread(THREAD_ALL_ACCESS, TRUE, ::GetCurrentThreadId());
}

BFP_EXPORT BfpThreadId BFP_CALLTYPE BfpThread_GetCurrentId()
{
	return (BfpThreadId)::GetCurrentThreadId();
}

BFP_EXPORT bool BFP_CALLTYPE BfpThread_WaitFor(BfpThread* thread, int waitMS)
{
	return ::WaitForSingleObject((HANDLE)thread, waitMS) == WAIT_OBJECT_0;
}

BFP_EXPORT BfpThreadPriority BFP_CALLTYPE BfpThread_GetPriority(BfpThread* thread, BfpThreadResult* outResult)
{
	return (BfpThreadPriority)::GetThreadPriority((HANDLE)thread);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_SetPriority(BfpThread* thread, BfpThreadPriority threadPriority, BfpThreadResult* outResult)
{
	// Coincidentally, our priority values map to (THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST)
	if (::SetThreadPriority((HANDLE)thread, (int)threadPriority))
		OUTRESULT(BfpThreadResult_Ok);
	else
		OUTRESULT(BfpThreadResult_UnknownError);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Suspend(BfpThread* thread, BfpThreadResult* outResult)
{
	DWORD suspendCount = ::SuspendThread((HANDLE)thread);
	if (suspendCount == (DWORD)-1)
	{
		int error = GetLastError();
		BF_DBG_FATAL("Failed BfpThread_Suspend");
		OUTRESULT(BfpThreadResult_UnknownError);
		return;
	}
	OUTRESULT(BfpThreadResult_Ok);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Resume(BfpThread* thread, BfpThreadResult* outResult)
{
	DWORD suspendCount = ::ResumeThread((HANDLE)thread);
	if (suspendCount == (DWORD)-1)
	{
		int error = GetLastError();
		BF_DBG_FATAL("Failed BfpThread_Resume");
		OUTRESULT(BfpThreadResult_UnknownError);
		return;
	}
	OUTRESULT(BfpThreadResult_Ok);
}


// Windows 7 SP1 is the first version of Windows to support the AVX API.

// The value for CONTEXT_XSTATE has changed between Windows 7 and
// Windows 7 SP1 and greater.
// While the value will be correct for future SDK headers, we need to set
// this value manually when building with a Windows 7 SDK for running on
// Windows 7 SPI OS bits.

#undef CONTEXT_XSTATE

#if defined(_M_X64)
#define CONTEXT_XSTATE                      (0x00100040)
#else
#define CONTEXT_XSTATE                      (0x00010040)
#endif

// Since the AVX API is not declared in the Windows 7 SDK headers and
// since we don't have the proper libs to work with, we will declare
// the API as function pointers and get them with GetProcAddress calls
// from kernel32.dll.  We also need to set some #defines.

#define XSTATE_AVX                          (XSTATE_GSSE)
#define XSTATE_MASK_AVX                     (XSTATE_MASK_GSSE)

typedef DWORD64(WINAPI* PGETENABLEDXSTATEFEATURES)();
static PGETENABLEDXSTATEFEATURES pfnGetEnabledXStateFeatures = NULL;

typedef BOOL(WINAPI* PINITIALIZECONTEXT)(PVOID Buffer, DWORD ContextFlags, PCONTEXT* Context, PDWORD ContextLength);
static PINITIALIZECONTEXT pfnInitializeContext = NULL;

typedef BOOL(WINAPI* PGETXSTATEFEATURESMASK)(PCONTEXT Context, PDWORD64 FeatureMask);
static PGETXSTATEFEATURESMASK pfnGetXStateFeaturesMask = NULL;

typedef PVOID(WINAPI* LOCATEXSTATEFEATURE)(PCONTEXT Context, DWORD FeatureId, PDWORD Length);
static LOCATEXSTATEFEATURE pfnLocateXStateFeature = NULL;

typedef BOOL(WINAPI* SETXSTATEFEATURESMASK)(PCONTEXT Context, DWORD64 FeatureMask);
static SETXSTATEFEATURESMASK pfnSetXStateFeaturesMask = NULL;

static uint8 ContextBuffer[4096];
static CONTEXT* CaptureRegistersEx(HANDLE hThread, intptr*& curPtr)
{
	PCONTEXT Context;
	DWORD ContextSize;
	DWORD64 FeatureMask;
	DWORD FeatureLength;
	BOOL Success;
	PM128A Xmm;
	PM128A Ymm;

	if (pfnGetEnabledXStateFeatures == (PGETENABLEDXSTATEFEATURES)-1)
		return NULL;

	if (pfnGetEnabledXStateFeatures == NULL)
	{
		HMODULE hm = GetModuleHandleA("kernel32.dll");
		if (hm == NULL)
		{
			pfnGetEnabledXStateFeatures = (PGETENABLEDXSTATEFEATURES)-1;
			return NULL;
		}

		pfnGetEnabledXStateFeatures = (PGETENABLEDXSTATEFEATURES)GetProcAddress(hm, "GetEnabledXStateFeatures");
		pfnInitializeContext = (PINITIALIZECONTEXT)GetProcAddress(hm, "InitializeContext");
		pfnGetXStateFeaturesMask = (PGETXSTATEFEATURESMASK)GetProcAddress(hm, "GetXStateFeaturesMask");
		pfnLocateXStateFeature = (LOCATEXSTATEFEATURE)GetProcAddress(hm, "LocateXStateFeature");
		pfnSetXStateFeaturesMask = (SETXSTATEFEATURESMASK)GetProcAddress(hm, "SetXStateFeaturesMask");

		if (pfnGetEnabledXStateFeatures == NULL
			|| pfnInitializeContext == NULL
			|| pfnGetXStateFeaturesMask == NULL
			|| pfnLocateXStateFeature == NULL
			|| pfnSetXStateFeaturesMask == NULL)
		{
			pfnGetEnabledXStateFeatures = (PGETENABLEDXSTATEFEATURES)-1;
			return NULL;
		}
	}

	FeatureMask = pfnGetEnabledXStateFeatures();
	if ((FeatureMask & XSTATE_MASK_AVX) == 0)
		return NULL;

	ContextSize = 0;
	Success = pfnInitializeContext(NULL,
		CONTEXT_ALL | CONTEXT_XSTATE | CONTEXT_EXCEPTION_REQUEST,
		NULL,
		&ContextSize);

	if (ContextSize > sizeof(ContextBuffer))
		return NULL;

	Success = pfnInitializeContext(ContextBuffer,
		CONTEXT_ALL | CONTEXT_XSTATE | CONTEXT_EXCEPTION_REQUEST,
		&Context,
		&ContextSize);

	if (Success == FALSE)
		return NULL;

	Success = pfnSetXStateFeaturesMask(Context, XSTATE_MASK_AVX);
	if (Success == FALSE)
		return Context;

	Success = GetThreadContext(hThread, Context);
	if (Success == FALSE)
		return Context;

	Success = pfnGetXStateFeaturesMask(Context, &FeatureMask);
	if (Success == FALSE)
		return Context;

	if ((FeatureMask & XSTATE_MASK_AVX) == 0)
		return Context;

	Xmm = (PM128A)pfnLocateXStateFeature(Context, XSTATE_LEGACY_SSE, &FeatureLength);
	Ymm = (PM128A)pfnLocateXStateFeature(Context, XSTATE_AVX, NULL);
	memcpy(curPtr, Ymm, FeatureLength);
	curPtr += FeatureLength / sizeof(intptr);
	return Context;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_GetIntRegisters(BfpThread* thread, intptr* outStackPtr, intptr* outIntRegs, int* inOutIntRegCount, BfpThreadResult* outResult)
{
	CONTEXT ctx;
	intptr* curPtr = outIntRegs;
	CONTEXT* ctxPtr = NULL;

	if (*inOutIntRegCount > 48)
		ctxPtr = CaptureRegistersEx((HANDLE)thread, curPtr);

	if (ctxPtr == NULL)
	{
		memset(&ctx, 0, sizeof(CONTEXT));
		ctx.ContextFlags = CONTEXT_ALL;
		BOOL success = ::GetThreadContext((HANDLE)thread, (CONTEXT*)&ctx);
		if (!success)
		{
			int error = GetLastError();
			OUTRESULT(BfpThreadResult_UnknownError);
			return;
		}
		ctxPtr = &ctx;

		DWORD lastError = GetLastError();
		BF_ASSERT(success);
	}

#ifdef BF32
	* outStackPtr = (intptr)ctxPtr->Esp;
	if (*inOutIntRegCount < (int)(curPtr - outIntRegs) + 7)
	{
		OUTRESULT(BfpThreadResult_InsufficientBuffer);
		return;
	}
#else
	* outStackPtr = (intptr)ctxPtr->Rsp;
	if (*inOutIntRegCount < (int)(curPtr - outIntRegs) + 48)
	{
		OUTRESULT(BfpThreadResult_InsufficientBuffer);
		return;
	}
#endif

	OUTRESULT(BfpThreadResult_Ok);

	if (outIntRegs == NULL)
		return;

#ifdef BF32
	* (curPtr++) = (intptr)ctxPtr->Eax;
	*(curPtr++) = (intptr)ctxPtr->Ebx;
	*(curPtr++) = (intptr)ctxPtr->Ecx;
	*(curPtr++) = (intptr)ctxPtr->Edx;
	*(curPtr++) = (intptr)ctxPtr->Esi;
	*(curPtr++) = (intptr)ctxPtr->Edi;
	*(curPtr++) = (intptr)ctxPtr->Ebp;
#else
	* (curPtr++) = (intptr)ctxPtr->SegFs; // Testing
	*(curPtr++) = (intptr)ctxPtr->Rax;
	*(curPtr++) = (intptr)ctxPtr->Rbx;
	*(curPtr++) = (intptr)ctxPtr->Rcx;
	*(curPtr++) = (intptr)ctxPtr->Rdx;
	*(curPtr++) = (intptr)ctxPtr->Rsi;
	*(curPtr++) = (intptr)ctxPtr->Rdi;
	*(curPtr++) = (intptr)ctxPtr->Rbp;
	*(curPtr++) = (intptr)ctxPtr->R8;
	*(curPtr++) = (intptr)ctxPtr->R9;
	*(curPtr++) = (intptr)ctxPtr->R10;
	*(curPtr++) = (intptr)ctxPtr->R11;
	*(curPtr++) = (intptr)ctxPtr->R12;
	*(curPtr++) = (intptr)ctxPtr->R13;
	*(curPtr++) = (intptr)ctxPtr->R14;
	*(curPtr++) = (intptr)ctxPtr->R15;
	memcpy(curPtr, &ctxPtr->Xmm0, 16 * 16);
	curPtr += (16 * 16) / sizeof(intptr);
#endif

	* inOutIntRegCount = (int)(curPtr - outIntRegs);
}

struct BfpCritSect
{
	CRITICAL_SECTION mCritSect;
};

BFP_EXPORT void BFP_CALLTYPE BfpThread_Sleep(int sleepMS)
{
	::Sleep(sleepMS);
}

BFP_EXPORT bool BFP_CALLTYPE BfpThread_Yield()
{
	return ::SwitchToThread();
}

///

struct BfpThreadInfo
{
	intptr mStackBase;
	intptr mStackLimit;
	NT_TIB* mTeb;
};

BFP_EXPORT BfpThreadInfo* BFP_CALLTYPE BfpThreadInfo_Create()
{
	BfpThreadInfo* threadInfo = new BfpThreadInfo();
	threadInfo->mStackBase = 0;
	threadInfo->mStackLimit = 0;
	threadInfo->mTeb = (NT_TIB*)NtCurrentTeb();
	return threadInfo;
}

BFP_EXPORT void BFP_CALLTYPE BfpThreadInfo_Release(BfpThreadInfo* threadInfo)
{
	delete threadInfo;
}

static __declspec(thread) BfpThreadInfo gThreadStackInfo;

BFP_EXPORT void BFP_CALLTYPE BfpThreadInfo_GetStackInfo(BfpThreadInfo* threadInfo, intptr* outStackBase, int* outStackLimit, BfpThreadInfoFlags flags, BfpThreadResult* outResult)
{
	if (threadInfo == NULL)
	{
		threadInfo = &gThreadStackInfo;
		if (threadInfo->mTeb == NULL)
			threadInfo->mTeb = (NT_TIB*)NtCurrentTeb();
	}

	if ((threadInfo->mStackBase == 0) || ((flags & BfpThreadInfoFlags_NoCache) != 0))
	{
		MEMORY_BASIC_INFORMATION stackInfo = { 0 };
		// We subtract one page for our request. VirtualQuery rounds UP to the next page.
		// Unfortunately, the stack grows down. If we're on the first page (last page in the
		// VirtualAlloc), we'll be moved to the next page, which is off the stack!  Note this
		// doesn't work right for IA64 due to bigger pages.
		void* currentAddr = (void*)((intptr_t)&stackInfo - 4096);

		// Query for the current stack allocation information.
		VirtualQuery(currentAddr, &stackInfo, sizeof(MEMORY_BASIC_INFORMATION));

		threadInfo->mStackBase = (uintptr_t)threadInfo->mTeb->StackBase;
		threadInfo->mStackLimit = (uintptr_t)stackInfo.AllocationBase;
	}

	*outStackBase = (intptr)threadInfo->mStackBase;
	*outStackLimit = (int)(threadInfo->mStackBase - threadInfo->mStackLimit);
	OUTRESULT(BfpThreadResult_Ok);
	return;
}

///

BFP_EXPORT BfpCritSect* BFP_CALLTYPE BfpCritSect_Create()
{
	BfpCritSect* critSect = new BfpCritSect();
	::InitializeCriticalSection(&critSect->mCritSect);

	return critSect;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Release(BfpCritSect* critSect)
{
	::DeleteCriticalSection(&critSect->mCritSect);
	delete critSect;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Enter(BfpCritSect* critSect)
{
	::EnterCriticalSection(&critSect->mCritSect);
}

BFP_EXPORT bool BFP_CALLTYPE BfpCritSect_TryEnter(BfpCritSect* critSect, int waitMS)
{
	if (waitMS == 0)
	{
		return ::TryEnterCriticalSection(&critSect->mCritSect);
	}
	else if (waitMS == -1)
	{
		BfpCritSect_Enter(critSect);
		return true;
	}

	// This is a poor implementation.  We should use a mutex if this is required
	uint32 start = BFTickCount();
	while ((int)(BFTickCount() - start) < waitMS)
	{
		if (::TryEnterCriticalSection(&critSect->mCritSect))
			return true;
		BfpThread_Yield();
	}
	return false;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Leave(BfpCritSect* critSect)
{
	::LeaveCriticalSection(&critSect->mCritSect);
}

#define DWORD_TO_BFPTLS(val) ((BfpTLS*)(intptr)(val))
#define BFPTLS_TO_DWORD(val) ((DWORD)(intptr)(val))

struct BfpTLS;
BFP_EXPORT BfpTLS* BFP_CALLTYPE BfpTLS_Create(BfpTLSProc exitProc)
{
	return DWORD_TO_BFPTLS(::FlsAlloc(exitProc));
}

BFP_EXPORT void BFP_CALLTYPE BfpTLS_Release(BfpTLS* tls)
{
	::FlsFree(BFPTLS_TO_DWORD(tls));
}

BFP_EXPORT void BFP_CALLTYPE BfpTLS_SetValue(BfpTLS* tls, void* value)
{
	::FlsSetValue(BFPTLS_TO_DWORD(tls), value);
}

BFP_EXPORT void* BFP_CALLTYPE BfpTLS_GetValue(BfpTLS* tls)
{
	return ::FlsGetValue(BFPTLS_TO_DWORD(tls));
}

BFP_EXPORT BfpEvent* BFP_CALLTYPE BfpEvent_Create(BfpEventFlags flags)
{
	BfpEvent* event = new BfpEvent();
	::InitializeCriticalSection(&event->mCritSect);
	::InitializeConditionVariable(&event->mCondVariable);
	event->mSet = (flags & (BfpEventFlag_InitiallySet_Auto | BfpEventFlag_InitiallySet_Manual)) != 0;
	event->mManualReset = (flags & BfpEventFlag_InitiallySet_Manual) != 0;
	return event;
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Release(BfpEvent* event)
{
	::DeleteCriticalSection(&event->mCritSect);
	delete event;
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Set(BfpEvent* event, bool requireManualReset)
{
	::EnterCriticalSection(&event->mCritSect);
	event->mSet = true;
	if (requireManualReset)
		event->mManualReset = true;
	if (event->mManualReset)
		::WakeAllConditionVariable(&event->mCondVariable);
	else
		::WakeConditionVariable(&event->mCondVariable);
	::LeaveCriticalSection(&event->mCritSect);
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Reset(BfpEvent* event, BfpEventResult* outResult)
{
	event->mSet = false;
	event->mManualReset = false;
}

BFP_EXPORT bool BFP_CALLTYPE BfpEvent_WaitFor(BfpEvent* event, int waitMS)
{
	::EnterCriticalSection(&event->mCritSect);
	while (!event->mSet)
	{
		if (!SleepConditionVariableCS(&event->mCondVariable, &event->mCritSect, waitMS))
		{
			if (GetLastError() == ERROR_TIMEOUT)
			{
				// Timeout
				LeaveCriticalSection(&event->mCritSect);
				return false;
			}
		}
	}
	if (!event->mManualReset)
		event->mSet = false;
	::LeaveCriticalSection(&event->mCritSect);
	return true;
}

struct BfpDynLib;
BFP_EXPORT BfpDynLib* BFP_CALLTYPE BfpDynLib_Load(const char* fileName)
{
	UTF16String wPath = UTF8Decode(fileName);
	return (BfpDynLib*)::LoadLibraryW(wPath.c_str());
}

BFP_EXPORT void BFP_CALLTYPE BfpDynLib_Release(BfpDynLib* lib)
{
	::FreeLibrary((HMODULE)lib);
}

BFP_EXPORT void BFP_CALLTYPE BfpDynLib_GetFilePath(BfpDynLib* lib, char* outPath, int* inOutPathSize, BfpLibResult* outResult)
{
	Beefy::String path;

	WCHAR cPath[4096];
	::GetModuleFileNameW((HMODULE)lib, cPath, 4096);
	path = UTF8Encode(cPath);

	TryStringOut(path, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void* BFP_CALLTYPE BfpDynLib_GetProcAddress(BfpDynLib* lib, const char* name)
{
	return ::GetProcAddress((HMODULE)lib, name);
}

struct BfpFindFileData;
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Create(const char* path, BfpFileResult* outResult)
{
	UTF16String wPath = UTF8Decode(path);
	if (!::CreateDirectoryW(wPath.c_str(), NULL))
	{
		int lastError = ::GetLastError();
		switch (lastError)
		{
		case ERROR_ALREADY_EXISTS:
			OUTRESULT(BfpFileResult_AlreadyExists);
			break;
		case ERROR_PATH_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
	{
		OUTRESULT(BfpFileResult_Ok);
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Rename(const char* oldName, const char* newName, BfpFileResult* outResult)
{
	UTF16String wOldPath = UTF8Decode(oldName);
	UTF16String wNewPath = UTF8Decode(newName);
	if (!::MoveFileW(wOldPath.c_str(), wNewPath.c_str()))
	{
		int err = ::GetLastError();
		switch (err)
		{
		case ERROR_ALREADY_EXISTS:
			OUTRESULT(BfpFileResult_AlreadyExists);
			break;
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
	{
		OUTRESULT(BfpFileResult_Ok);
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Delete(const char* path, BfpFileResult* outResult)
{
	UTF16String wPath = UTF8Decode(path);
	if (!::RemoveDirectoryW(wPath.c_str()))
	{
		switch (GetLastError())
		{
		case ERROR_FILE_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		case ERROR_DIR_NOT_EMPTY:
			OUTRESULT(BfpFileResult_NotEmpty);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
	{
		OUTRESULT(BfpFileResult_Ok);
	}
}

BFP_EXPORT bool BFP_CALLTYPE BfpDirectory_Exists(const char* path)
{
	WIN32_FIND_DATAW findData;

	UTF16String wpath = UTF8Decode(path);
	if (wpath.length() > 0)
	{
		uint16& endC = wpath[wpath.length() - 1];
		if ((endC == '\\') || (endC == '/'))
		{
			wpath.pop_back();
		}
	}
	HANDLE handleVal = FindFirstFileW(wpath.c_str(), &findData);
	if (handleVal == INVALID_HANDLE_VALUE)
		return false;
	FindClose(handleVal);
	return (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetSysDirectory(BfpSysDirectoryKind sysDirKind, char* outPath, int* inOutPathLen, BfpFileResult* outResult)
{
	String path;

	auto _GetKnownFolder = [&](REFKNOWNFOLDERID rfid)
	{
		PWSTR pStrPtr;
		int result = SHGetKnownFolderPath(rfid, KF_FLAG_CREATE | KF_FLAG_SIMPLE_IDLIST, NULL, &pStrPtr);
		if (result != 0)
		{
			OUTRESULT(BfpFileResult_UnknownError);
			return;
		}
		path = UTF8Encode(pStrPtr);
		CoTaskMemFree(pStrPtr);
		TryStringOut(path, outPath, inOutPathLen, (BfpResult*)outResult);
	};

	switch (sysDirKind)
	{
	case BfpSysDirectoryKind_Default:
		_GetKnownFolder(FOLDERID_Desktop);
		break;
	case BfpSysDirectoryKind_Home:
		_GetKnownFolder(FOLDERID_Profile);
		break;
	case BfpSysDirectoryKind_System:
		_GetKnownFolder(FOLDERID_System);
		break;
	case BfpSysDirectoryKind_Desktop:
		_GetKnownFolder(FOLDERID_Desktop);
		return;
	case BfpSysDirectoryKind_Desktop_Common:
		_GetKnownFolder(FOLDERID_PublicDesktop);
		return;
	case BfpSysDirectoryKind_AppData_Local:
		_GetKnownFolder(FOLDERID_LocalAppData);
		return;
	case BfpSysDirectoryKind_AppData_LocalLow:
		_GetKnownFolder(FOLDERID_LocalAppDataLow);
		return;
	case BfpSysDirectoryKind_AppData_Roaming:
		_GetKnownFolder(FOLDERID_RoamingAppData);
		return;
	case BfpSysDirectoryKind_Programs:
		_GetKnownFolder(FOLDERID_Programs);
		return;
	case BfpSysDirectoryKind_Programs_Common:
		_GetKnownFolder(FOLDERID_CommonPrograms);
		return;
	case BfpSysDirectoryKind_Documents:
		_GetKnownFolder(FOLDERID_Documents);
		return;
	}

	TryStringOut(path, outPath, inOutPathLen, (BfpResult*)outResult);
}

//////////////////////////////////////////////////////////////////////////

BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_Create(const char* path, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags, BfpFileAttributes createdFileAttrs, BfpFileResult* outResult)
{
	UTF16String wPath = UTF8Decode(path);

	//OVERLAPPED

	if ((createFlags & BfpFileCreateFlag_Pipe) != 0)
	{
		String pipeName = StrFormat("\\\\%s\\pipe\\%s", ".", path);
		wPath = UTF8Decode(pipeName);

		if ((createKind == BfpFileCreateKind_CreateIfNotExists) ||
			(createKind == BfpFileCreateKind_CreateAlways))
		{
			bool isOverlapped = false;

			DWORD openMode = 0;
			DWORD desiredAccess = 0;
			if ((createFlags & BfpFileCreateFlag_Read) != 0)
				openMode |= PIPE_ACCESS_INBOUND;
			if ((createFlags & BfpFileCreateFlag_Write) != 0)
				openMode |= PIPE_ACCESS_OUTBOUND;

			if (createKind == BfpFileCreateKind_CreateIfNotExists)
				openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

			int pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE;
			if ((createFlags & BfpFileCreateFlag_AllowTimeouts) != 0)
			{
				openMode |= FILE_FLAG_OVERLAPPED;
				isOverlapped = true;
			}
			else if ((createFlags & BfpFileCreateFlag_NonBlocking) != 0)
				pipeMode |= PIPE_NOWAIT;

			HANDLE handle = ::CreateNamedPipeW(wPath.c_str(), openMode, pipeMode, PIPE_UNLIMITED_INSTANCES, 8192, 8192, 0, NULL);
			if (handle == INVALID_HANDLE_VALUE)
			{
				if (outResult != NULL)
				{
					int lastError = GetLastError();
					switch (lastError)
					{
					default:
						*outResult = BfpFileResult_UnknownError;
						break;
					}
				}
				return NULL;
			}

			OUTRESULT(BfpFileResult_Ok);

			BfpFile* bfpFile = new BfpFile();
			bfpFile->mHandle = handle;
			bfpFile->mIsPipe = true;

			if (isOverlapped)
			{
				bfpFile->mAsyncData = new BfpAsyncData();
			}

			return bfpFile;
		}
	}

	DWORD desiredAccess = 0;
	if ((createFlags & BfpFileCreateFlag_Append) != 0)
		desiredAccess |= FILE_APPEND_DATA;
	else
	{
		if ((createFlags & BfpFileCreateFlag_Read) != 0)
			desiredAccess |= GENERIC_READ;
		if ((createFlags & BfpFileCreateFlag_Write) != 0)
			desiredAccess |= GENERIC_WRITE;
	}

	DWORD shareMode = 0;
	if ((createFlags & BfpFileCreateFlag_ShareRead) != 0)
		shareMode |= FILE_SHARE_READ;
	if ((createFlags & BfpFileCreateFlag_ShareWrite) != 0)
		shareMode |= FILE_SHARE_WRITE;
	if ((createFlags & BfpFileCreateFlag_ShareDelete) != 0)
		shareMode |= FILE_SHARE_DELETE;

	DWORD creationDisposition = 0;
	if (createKind == BfpFileCreateKind_CreateAlways)
	{
		if ((createFlags & BfpFileCreateFlag_Append) != 0)
			creationDisposition = OPEN_ALWAYS;
		else
			creationDisposition = CREATE_ALWAYS;
	}
	else if (createKind == BfpFileCreateKind_CreateIfNotExists)
	{
		creationDisposition = CREATE_NEW;
	}
	else if (createKind == BfpFileCreateKind_OpenAlways)
	{
		creationDisposition = OPEN_ALWAYS;
	}
	else
	{
		creationDisposition = OPEN_EXISTING;
	}

	DWORD attributes = 0;
	if ((createdFileAttrs & (BfpFileAttribute_Directory | BfpFileAttribute_SymLink | BfpFileAttribute_Device)) != 0)
	{
		if (outResult != NULL)
			*outResult = BfpFileResult_InvalidParameter;
		return NULL;
	}

	attributes = FileAttributes_WinToBFP(createdFileAttrs);

	if ((createFlags & BfpFileCreateFlag_WriteThrough) != 0)
		desiredAccess |= FILE_FLAG_WRITE_THROUGH;
	if ((createFlags & BfpFileCreateFlag_DeleteOnClose) != 0)
		desiredAccess |= FILE_FLAG_DELETE_ON_CLOSE;
	if ((createFlags & BfpFileCreateFlag_NoBuffering) != 0)
		desiredAccess |= FILE_FLAG_NO_BUFFERING;

	HANDLE handle = ::CreateFileW(wPath.c_str(), desiredAccess, shareMode, NULL, creationDisposition, attributes, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		if (outResult != NULL)
		{
			int lastError = GetLastError();

			switch (lastError)
			{
			case ERROR_SHARING_VIOLATION:
				*outResult = BfpFileResult_ShareError;
				break;
			case ERROR_FILE_NOT_FOUND:
				*outResult = BfpFileResult_NotFound;
				break;
			default:
				*outResult = BfpFileResult_UnknownError;
				break;
			}
		}
		return NULL;
	}

	OUTRESULT(BfpFileResult_Ok);

	BfpFile* bfpFile = new BfpFile();
	bfpFile->mHandle = handle;
	return bfpFile;
}

BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_GetStd(BfpFileStdKind kind, BfpFileResult* outResult)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	switch (kind)
	{
	case BfpFileStdKind_StdOut:
		h = ::GetStdHandle(STD_OUTPUT_HANDLE);
		break;
	case BfpFileStdKind_StdError:
		h = ::GetStdHandle(STD_ERROR_HANDLE);
		break;
	case BfpFileStdKind_StdIn:
		h = ::GetStdHandle(STD_INPUT_HANDLE);
		break;
	}
	if ((h == INVALID_HANDLE_VALUE) || (h == 0))
	{
		OUTRESULT(BfpFileResult_NotFound);
		return NULL;
	}

	BfpFile* bfpFile = new BfpFile();
	bfpFile->mHandle = h;
	bfpFile->mIsStd = true;

	return bfpFile;
}

BFP_EXPORT intptr BFP_CALLTYPE BfpFile_GetSystemHandle(BfpFile* file)
{
	return (intptr)file->mHandle;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Release(BfpFile* file)
{
	if ((file->mHandle != INVALID_HANDLE_VALUE) && (!file->mIsStd))
		::CloseHandle(file->mHandle);

	delete file;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Close(BfpFile* file, BfpFileResult* outResult)
{
	if (file->mHandle != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(file->mHandle);
		file->mHandle = INVALID_HANDLE_VALUE;
		OUTRESULT(BfpFileResult_Ok);
	}
	else
		OUTRESULT(BfpFileResult_UnknownError);
}

BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Write(BfpFile* file, const void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult)
{
	DWORD bytesWritten = 0;
	if (::WriteFile(file->mHandle, buffer, (uint32)size, &bytesWritten, NULL))
	{
		if (outResult != NULL)
		{
			if (bytesWritten != size)
				*outResult = BfpFileResult_PartialData;
			else
				*outResult = BfpFileResult_Ok;
		}
		return bytesWritten;
	}

	if (outResult != NULL)
	{
		switch (GetLastError())
		{
		default:
			*outResult = BfpFileResult_UnknownError;
			break;
		}
	}

	return bytesWritten;
}

struct OverlappedReadResult : OVERLAPPED
{
	BfpFile* mFile;
	intptr mBytesRead;
	DWORD mErrorCode;
};

static void WINAPI OverlappedReadComplete(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	OverlappedReadResult* readResult = (OverlappedReadResult*)lpOverlapped;
	readResult->mErrorCode = dwErrorCode;
	readResult->mBytesRead = dwNumberOfBytesTransfered;
	readResult->mFile->mAsyncData->SetEvent();
}

BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Read(BfpFile* file, void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult)
{
	bool forceNormalRead = false;

	if ((file->mIsStd) && (file->mHandle == GetStdHandle(STD_INPUT_HANDLE)))
	{
		INPUT_RECORD record;
		DWORD numRead;
	 	while (true)
		{
			if (timeoutMS != -1)
			{
				if (!GetNumberOfConsoleInputEvents(file->mHandle, &numRead))
				{
					forceNormalRead = true;
					break;
				}

				if (numRead == 0)
				{
					OUTRESULT(BfpFileResult_Timeout);
					return 0;
				}
			}

			if (!ReadConsoleInput(file->mHandle, &record, 1, &numRead))
			{
				forceNormalRead = true;
				break;
			}
			if (numRead > 0)
			{
				if ((record.Event.KeyEvent.bKeyDown) && (record.Event.KeyEvent.uChar.AsciiChar != 0))
				{
					memset(buffer, record.Event.KeyEvent.uChar.AsciiChar, 1);
					OUTRESULT(BfpFileResult_Ok);
					return 1;
				}
			}
		}

	}

	if ((timeoutMS != -1) && (!forceNormalRead))
	{
		if (file->mAsyncData == NULL)
		{
			OUTRESULT(BfpFileResult_InvalidParameter);
			return 0;
		}

		while (true)
		{
			OverlappedReadResult overlapped;
			memset(&overlapped, 0, sizeof(OverlappedReadResult));
			overlapped.mFile = file;

			//TODO: this doesn't set file stream location.  It only works for streams like pipes, sockets, etc
			if (::ReadFileEx(file->mHandle, buffer, (uint32)size, &overlapped, OverlappedReadComplete))
			{
				if (!file->mAsyncData->WaitAndResetEvent(timeoutMS))
				{
					::CancelIoEx(file->mHandle, &overlapped);
					// There's a chance we completed before we were cancelled -- check on that
					if (!file->mAsyncData->WaitAndResetEvent(0))
					{
						OUTRESULT(BfpFileResult_Timeout);
						return 0;
					}
				}

				if (overlapped.mErrorCode == 0)
				{

				}
				else if (overlapped.mErrorCode == ERROR_OPERATION_ABORTED)
				{
					OUTRESULT(BfpFileResult_Timeout);
					return 0;
				}
			}
			else
			{
				int lastError = ::GetLastError();
				if (lastError == ERROR_PIPE_LISTENING)
				{
					overlapped.hEvent = file->mAsyncData->mEvent;
					if (!::ConnectNamedPipe(file->mHandle, &overlapped))
					{
						int lastError = ::GetLastError();
						if (lastError == ERROR_IO_PENDING)
						{
							if (!file->mAsyncData->WaitAndResetEvent(timeoutMS))
							{
								::CancelIoEx(file->mHandle, &overlapped);
								// Clear event set by CancelIoEx
								file->mAsyncData->WaitAndResetEvent(0);

								OUTRESULT(BfpFileResult_Timeout);
								return 0;
							}
						}
					}

					// Now we have a connection, so retry the read...
					continue;
				}
				else
				{
					OUTRESULT(BfpFileResult_UnknownError);
					return 0;
				}
			}

			OUTRESULT(BfpFileResult_Ok);
			return overlapped.mBytesRead;
		}
	}

	DWORD bytesRead = 0;
	if (::ReadFile(file->mHandle, buffer, (uint32)size, &bytesRead, NULL))
	{

		if (bytesRead != size)
			OUTRESULT(BfpFileResult_PartialData);
		else
			OUTRESULT(BfpFileResult_Ok);
		return bytesRead;
	}

	int lastError = ::GetLastError();
	switch (lastError)
	{
	case ERROR_BROKEN_PIPE: // Just an EOF
		OUTRESULT(BfpFileResult_Ok);
		break;
	default:
		OUTRESULT(BfpFileResult_UnknownError);
		break;
	}

	return bytesRead;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Flush(BfpFile* file)
{
	::FlushFileBuffers(file->mHandle);
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFile_GetFileSize(BfpFile* file)
{
	LARGE_INTEGER largeInteger;
	largeInteger.QuadPart = 0;
	::GetFileSizeEx(file->mHandle, &largeInteger);
	return largeInteger.QuadPart;
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFile_Seek(BfpFile* file, int64 offset, BfpFileSeekKind seekKind)
{
	DWORD moveMethod;
	if (seekKind == BfpFileSeekKind_Absolute)
		moveMethod = FILE_BEGIN;
	else if (seekKind == BfpFileSeekKind_Relative)
		moveMethod = FILE_CURRENT;
	else
		moveMethod = FILE_END;

	LARGE_INTEGER liOffset;
	liOffset.QuadPart = offset;
	LARGE_INTEGER newPos;
	newPos.QuadPart = 0;
	::SetFilePointerEx(file->mHandle, liOffset, &newPos, moveMethod);
	return newPos.QuadPart;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Truncate(BfpFile* file, BfpFileResult* outResult)
{
	if (!SetEndOfFile(file->mHandle))
	{
		OUTRESULT(BfpFileResult_UnknownError);
		return;
	}
	OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFile_GetTime_LastWrite(const char* path)
{
	UTF16String wPath = UTF8Decode(path);

	BfpTimeStamp lastWriteTime = 0;
	GetFileInfo(path, &lastWriteTime, NULL);

	return lastWriteTime;
}

BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFile_GetAttributes(const char* path, BfpFileResult* outResult)
{
	uint32 fileAttributes;
	GetFileInfo(path, NULL, &fileAttributes);
	return FileAttributes_WinToBFP(fileAttributes);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_SetAttributes(const char* path, BfpFileAttributes attribs, BfpFileResult* outResult)
{
	if (!::SetFileAttributesW(UTF8Decode(path).c_str(), FileAttributes_BFPToWin(attribs)))
	{
		OUTRESULT(BfpFileResult_UnknownError);
		return;
	}
	OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Copy(const char* oldName, const char* newName, BfpFileCopyKind copyKind, BfpFileResult* outResult)
{
	if (copyKind == BfpFileCopyKind_IfNewer)
	{
		BfpTimeStamp fromTime = 0;
		GetFileInfo(oldName, &fromTime, NULL);
		BfpTimeStamp toTime = 0;
		GetFileInfo(newName, &toTime, NULL);

		if ((toTime != 0) && (toTime >= fromTime))
		{
			OUTRESULT(BfpFileResult_Ok);
			return;
		}
	}

	UTF16String wOldPath = UTF8Decode(oldName);
	UTF16String wNewPath = UTF8Decode(newName);
	if (!::CopyFileW(wOldPath.c_str(), wNewPath.c_str(), copyKind == BfpFileCopyKind_IfNotExists))
	{
		switch (::GetLastError())
		{
		case ERROR_ALREADY_EXISTS:
			OUTRESULT(BfpFileResult_AlreadyExists);
			break;
		case ERROR_PATH_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
	{
		OUTRESULT(BfpFileResult_Ok);
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Rename(const char* oldName, const char* newName, BfpFileResult* outResult)
{
	UTF16String wOldPath = UTF8Decode(oldName);
	UTF16String wNewPath = UTF8Decode(newName);
	if (!::MoveFileW(wOldPath.c_str(), wNewPath.c_str()))
	{
		switch (::GetLastError())
		{
		case ERROR_ALREADY_EXISTS:
			OUTRESULT(BfpFileResult_AlreadyExists);
			break;
		case ERROR_PATH_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
	{
		OUTRESULT(BfpFileResult_Ok);
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Delete(const char* path, BfpFileResult* outResult)
{
	if (!::DeleteFileW(UTF8Decode(path).c_str()))
	{
		int lastError = GetLastError();
		switch (lastError)
		{
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			OUTRESULT(BfpFileResult_NotFound);
			break;
		default:
			OUTRESULT(BfpFileResult_UnknownError);
			break;
		}
	}
	else
		OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT bool BFP_CALLTYPE BfpFile_Exists(const char* path)
{
	WIN32_FIND_DATAW findData;
	HANDLE handleVal = FindFirstFileW(UTF8Decode(path).c_str(), &findData);
	if (handleVal == INVALID_HANDLE_VALUE)
		return false;
	FindClose(handleVal);
	return (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempPath(char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
	WCHAR wStr[4096];
	::GetTempPathW(4096, wStr);

	String str = UTF8Encode(wStr);
	TryStringOut(str, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempFileName(char* outName, int* inOutNameSize, BfpFileResult* outResult)
{
	WCHAR wPath[4096];
	wPath[0] = 0;
	::GetTempPathW(4096, wPath);

	WCHAR wFileName[4096];
	wFileName[0] = 0;
	GetTempFileNameW(wPath, L"tmp", 0, wFileName);

	String str = UTF8Encode(wFileName);
	TryStringOut(str, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetFullPath(const char* inPath, char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
	WCHAR wPath[4096];
	wPath[0] = 0;
	GetFullPathNameW(UTF8Decode(inPath).c_str(), 4096, wPath, NULL);

	String str = UTF8Encode(wPath);
	TryStringOut(str, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetActualPath(const char* inPathC, char* outPathC, int* inOutPathSize, BfpFileResult* outResult)
{
	String inPath = inPathC;
	String outPath;

	// Check for '/../' backtracking - handle those first
	{
		int i = 0;
		int32 lastComponentStart = -1;
		String subName;

		while (i < inPath.mLength)
		{
			// skip until path separator
			while ((i < inPath.mLength) && (inPath[i] != DIR_SEP_CHAR) && (inPath[i] != DIR_SEP_CHAR_ALT))
				++i;

			if (lastComponentStart != -1)
			{
				if ((i - lastComponentStart == 2) && (inPath[lastComponentStart] == '.') && (inPath[lastComponentStart + 1] == '.'))
				{
					// Backtrack
					while ((lastComponentStart > 0) &&
						((inPath[lastComponentStart - 1] == DIR_SEP_CHAR) || (inPath[lastComponentStart - 1] == DIR_SEP_CHAR_ALT)))
						lastComponentStart--;
					while ((lastComponentStart > 0) && (inPath[lastComponentStart - 1] != DIR_SEP_CHAR) && (inPath[lastComponentStart - 1] != DIR_SEP_CHAR_ALT))
						lastComponentStart--;
					inPath.Remove(lastComponentStart, i - lastComponentStart + 1);
					i = lastComponentStart;
					continue;
				}
				else if ((i - lastComponentStart == 1) && (inPath[lastComponentStart] == '.'))
				{
					inPath.Remove(lastComponentStart, i - lastComponentStart + 1);
					i = lastComponentStart;
					continue;
				}
			}

			++i;
			// Ignore multiple slashes in a row
			while ((i < inPath.mLength) && ((inPath[i] == DIR_SEP_CHAR) || (inPath[i] == DIR_SEP_CHAR_ALT)))
				++i;

			lastComponentStart = i;
		}
	}

	int32 i = 0;

	// for network paths (\\server\share\RestOfPath), getting the display
	// name mangles it into unusable form (e.g. "\\server\share" turns
	// into "share on server (server)"). So detect this case and just skip
	// up to two path components
	int length = (int)inPath.length();
	if (length >= 2 && inPath[0] == DIR_SEP_CHAR && inPath[1] == DIR_SEP_CHAR)
	{
		int skippedCount = 0;
		i = 2; // start after '\\'
		while (i < length && skippedCount < 2)
		{
			if (inPath[i] == DIR_SEP_CHAR)
				++skippedCount;
			++i;
		}

		outPath.Append(inPath.c_str(), i);
	}
	// for drive names, just add it uppercased
	else if (length >= 2 && inPath[1] == ':')
	{
		outPath.Append(toupper(inPath[0]));
		outPath.Append(':');
		if ((length >= 3) &&
			((inPath[2] == DIR_SEP_CHAR) || (inPath[2] == DIR_SEP_CHAR_ALT)))
		{
			outPath.Append(DIR_SEP_CHAR);
			i = 3; // start after drive, colon and separator
		}
		else
		{
			i = 2; // start after drive and colon
		}
	}

	if ((i == 0) && (length >= 1) &&
		((inPath[0] == DIR_SEP_CHAR) || (inPath[1] == DIR_SEP_CHAR_ALT)))
	{
		i++; // start after initial slash
		outPath.Append(DIR_SEP_CHAR);
	}

	int32 lastComponentStart = i;
	bool addSeparator = false;
	String subName;

	while (i < length)
	{
		// skip until path separator
		while ((i < length) && (inPath[i] != DIR_SEP_CHAR) && (inPath[i] != DIR_SEP_CHAR_ALT))
			++i;

		if (addSeparator)
			outPath.Append(DIR_SEP_CHAR);

		SHFILEINFOW info = { 0 };

		subName.Clear();
		subName = inPath.Substring(0, i);
		for (int j = 0; j < (int)subName.length(); j++)
			if (subName[j] == DIR_SEP_CHAR_ALT)
				subName[j] = DIR_SEP_CHAR;
		info.szDisplayName[0] = 0;
		int32 size = (int32)sizeof(SHFILEINFOW);

		WIN32_FIND_DATAW findData;
		HANDLE handleVal = FindFirstFileW(UTF8Decode(subName).c_str(), &findData);
		if (handleVal != INVALID_HANDLE_VALUE)
		{
			outPath.Append(UTF8Encode(findData.cFileName));
			FindClose(handleVal);
		}
		else
		{
			// most likely file does not exist.
			// So just append original path name component.
			outPath.Append(inPath.Substring(lastComponentStart, i - lastComponentStart));
		}

		++i;
		// Ignore multiple slashes in a row
		while ((i < length) && ((inPath[i] == DIR_SEP_CHAR) || (inPath[i] == DIR_SEP_CHAR_ALT)))
			++i;

		lastComponentStart = i;
		addSeparator = true;
	}

	TryStringOut(outPath, outPathC, inOutPathSize, (BfpResult*)outResult);
}

// BfpFindFileData

struct BfpFindFileData
{
	BfpFindFileFlags mFlags;
	WIN32_FIND_DATA mFindData;
    Beefy::String mWildcard;
	HANDLE mHandle;
};

#define HANDLE_TO_BFPFINDFILEDATA(handle) ((BfpFindFileData*)(handle))
#define BFPFINDFILEDATA_TO_HANDLE(findData) ((HANDLE)(findData))

static bool BfpFindFileData_CheckFilter(BfpFindFileData* findData)
{
	bool isDir = (findData->mFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	if (isDir)
	{
		if ((findData->mFlags & BfpFindFileFlag_Directories) == 0)
			return false;

		if ((wcscmp(findData->mFindData.cFileName, L".") == 0) || (wcscmp(findData->mFindData.cFileName, L"..") == 0))
			return false;
	}
	else
	{
		if ((findData->mFlags & BfpFindFileFlag_Files) == 0)
			return false;
	}

	Beefy::String fileName = UTF8Encode(findData->mFindData.cFileName);
	Beefy::MakeUpper(fileName);
    if (!wc_match(findData->mWildcard.c_str(), fileName.c_str()))
        return false;

	return true;
}

BFP_EXPORT BfpFindFileData* BFP_CALLTYPE BfpFindFileData_FindFirstFile(const char* path, BfpFindFileFlags flags, BfpFileResult* outResult)
{
	Beefy::String findStr = path;
	Beefy::String wildcard;

    int lastSlashPos = std::max((int)findStr.LastIndexOf('/'), (int)findStr.LastIndexOf('\\'));
    if (lastSlashPos != -1)
    {
        wildcard = findStr.Substring(lastSlashPos + 1);
        findStr = findStr.Substring(0, lastSlashPos + 1);
		findStr.Append("*");
    }
    if (wildcard == "*.*")
        wildcard = "*";

	BfpFindFileData* findData = new BfpFindFileData();
	findData->mFlags = flags;
	findData->mWildcard = wildcard;
	Beefy::MakeUpper(findData->mWildcard);

	FINDEX_SEARCH_OPS searchOps;
	if ((flags & BfpFindFileFlag_Files) == 0)
		searchOps = FindExSearchLimitToDirectories;
	else
		searchOps = FindExSearchNameMatch;

	UTF16String wPath = UTF8Decode(findStr);
	findData->mHandle = ::FindFirstFileExW(wPath.c_str(), FindExInfoBasic, &findData->mFindData, searchOps, NULL, 0);
	if (findData->mHandle == INVALID_HANDLE_VALUE)
	{
		if (outResult != NULL)
		{
			switch (GetLastError())
			{
			default:
				*outResult = BfpFileResult_UnknownError;
				break;
			}
		}

		delete findData;
		return NULL;
	}

	if (!BfpFindFileData_CheckFilter(findData))
	{
		if (!BfpFindFileData_FindNextFile(findData))
		{
			::FindClose(findData->mHandle);
			if (outResult != NULL)
				*outResult = BfpFileResult_NoResults;
			delete findData;
			return NULL;
		}
	}

	OUTRESULT(BfpFileResult_Ok);
	return findData;
}

BFP_EXPORT bool BFP_CALLTYPE BfpFindFileData_FindNextFile(BfpFindFileData* findData)
{
	while (true)
	{
		if (!::FindNextFileW(findData->mHandle, &findData->mFindData))
			return false;
		if (BfpFindFileData_CheckFilter(findData))
			return true;
	}
}

BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_GetFileName(BfpFindFileData* findData, char* outName, int* inOutNameSize, BfpFileResult* outResult)
{
	String name = UTF8Encode(findData->mFindData.cFileName);
	TryStringOut(name, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_LastWrite(BfpFindFileData* findData)
{
	return *(BfpTimeStamp*)&findData->mFindData.ftLastWriteTime;
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Created(BfpFindFileData* findData)
{
	return *(BfpTimeStamp*)&findData->mFindData.ftCreationTime;
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Access(BfpFindFileData* findData)
{
	return *(BfpTimeStamp*)&findData->mFindData.ftLastAccessTime;
}

BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFindFileData_GetFileAttributes(BfpFindFileData* findData)
{
	return FileAttributes_WinToBFP(findData->mFindData.dwFileAttributes);
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFindFileData_GetFileSize(BfpFindFileData* findData)
{
	return ((int64)findData->mFindData.nFileSizeHigh << 32) | (int64)findData->mFindData.nFileSizeLow;
}

BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_Release(BfpFindFileData* findData)
{
	::FindClose(findData->mHandle);
	delete findData;
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetCurrent(char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
	wchar_t* wCwdStr = _wgetcwd(NULL, 0);
	String str = UTF8Encode(wCwdStr);
	free(wCwdStr);
	TryStringOut(str, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_SetCurrent(const char* path, BfpFileResult* outResult)
{
	UTF16String wPath = UTF8Decode(path);
	if (_wchdir(wPath.c_str()) == -1)
		OUTRESULT(BfpFileResult_UnknownError);
	else
		OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT int BFP_CALLTYPE BfpStack_CaptureBackTrace(int framesToSkip, intptr* outFrames, int wantFrameCount)
{
	return (int)RtlCaptureStackBackTrace(framesToSkip + 1, wantFrameCount, (void**)outFrames, NULL);
}

BFP_EXPORT void BFP_CALLTYPE BfpOutput_DebugString(const char* str)
{
	OutputDebugStringA(str);
}
