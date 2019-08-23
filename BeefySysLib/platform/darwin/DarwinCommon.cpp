#include "Common.h"
#include "BFPlatform.h"
#include <CoreFoundation/CFByteOrder.h>
#include <mach/mach_time.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <wchar.h>
#include <fcntl.h>
#include <mach/clock.h>
#include <mach/mach.h>
#include <time.h>
#include <dirent.h>
#include <syslog.h>
#include "SDL_timer.h"

int gBFPlatformLastError = 0;

#define NOT_IMPL throw "Unimplemented";

inline uint64 get_mach_frequency()
{
    uint64 freq = 0;
    if (freq == 0)
    {
        static mach_timebase_info_data_t sTimebaseInfo;
        if ( sTimebaseInfo.denom == 0)
            (void) mach_timebase_info(&sTimebaseInfo);
        
        freq = (uint64_t)1000000 * (uint64)sTimebaseInfo.denom / (uint64)sTimebaseInfo.numer;
    }
    return freq;
}

uint32 Beefy::BFTickCount()
{
    /*uint64 t;
    t = mach_absolute_time();
    t /= get_mach_frequency();
    return (uint32)t;*/
    return SDL_GetTicks();
}

int64 Beefy::EndianSwap(int64 val)
{
	return CFSwapInt64(val);
}

bool IsDebuggerPresent()
{
    /*   int mib[4];
     struct kinfo_proc info;
     size_t size;
     
     info.kp_proc.p_flag = 0;
     mib[0] = CTL_KERN;
     mib[1] = KERN_PROC;
     mib[2] = KERN_PROC_PID;
     mib[3] = getpid();
     
     size = sizeof(info);
     sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
     
     return ((info.kp_proc.p_flag & P_TRACED) != 0);*/
    return true;
}

void OutputDebugStringA(const char* str)
{
    if (IsDebuggerPresent())
        fputs(str, stdout);
}

void OutputDebugStringW(const wchar_t* str)
{
    if (IsDebuggerPresent())
    {
        fputws(str, stdout);
    }
}

void* BFTlsGetValue(BFTlsKey tlsKey)
{
    return pthread_getspecific(tlsKey);
}

bool BFTlsSetValue(BFTlsKey tlsKey, void* tlsValue)
{
    return pthread_setspecific(tlsKey, tlsValue) == 0;
}

BFTlsKey BFTlsAlloc()
{
    pthread_key_t key = NULL;
    pthread_key_create(&key, NULL);
    return key;
}

bool BFTlsFree(BFTlsKey tlsKey)
{
    return pthread_key_delete(tlsKey) == 0;
}

HANDLE GetModuleHandle(HANDLE h)
{
    NOT_IMPL
    return 0;
}

uint32 GetModuleFileNameW(uint32 hModule, wchar_t* lpFilename, uint32 length)
{
    return (uint32)mbstowcs(lpFilename, gBFArgV[0], length);
}

uint32 GetModuleFileNameA(uint32 hModule, char* lpFilename, uint32 length)
{
    strncpy(lpFilename, gBFArgV[0], length);
    return (uint32)strlen(lpFilename);
}

HMODULE LoadLibraryA(const char* fileName)
{
    HMODULE mod = NULL;
    /*(HMODULE)dlopen(fileName, RTLD_LAZY);
     if (mod == NULL)
     mod = (HMODULE)dlopen((std::string(fileName) + ".dylib").c_str(), RTLD_LAZY);
     if (mod == NULL)
     mod = (HMODULE)dlopen((std::string(fileName) + ".so").c_str(), RTLD_LAZY);*/
    
    static const char* prefixes[] = {NULL, "lib"};
    static const char* suffixes[] = {NULL, ".so", ".dylib"};
    
    for (int prefixIdx = 0; prefixIdx < sizeof(prefixes)/sizeof(prefixes[0]); prefixIdx++)
    {
        for (int suffixIdx = 0; suffixIdx < sizeof(suffixes)/sizeof(suffixes[0]); suffixIdx++)
        {
            const char* prefix = prefixes[prefixIdx];
            const char* suffix = suffixes[suffixIdx];
            
            std::string checkName = fileName;
            if (prefix != NULL)
                checkName = std::string(prefix) + checkName;
            if (suffix != NULL)
                checkName += suffix;
            
            mod = (HMODULE)dlopen(checkName.c_str(), RTLD_LAZY);
            if (mod != NULL)
                return mod;
        }
    }
    
    return NULL;
}

BFLibProcAddr GetProcAddress(HMODULE mod, const char* name)
{
    return (BFLibProcAddr)dlsym(mod, name);
}

bool CreateDirectoryW(const wchar_t* str, void* securityAttributes)
{NOT_IMPL
    return false;
}

bool RemoveDirectoryW(const wchar_t* str)
{NOT_IMPL
    return false;
}

int GetLastError()
{
    return gBFPlatformLastError;
}

void BFSetLastError(int error)
{
    gBFPlatformLastError = error;
}

int* GetStdHandle(int32 handleId)
{
    if (handleId == STD_INPUT_HANDLE)
        return (int*)STDIN_FILENO;
    if (handleId == STD_OUTPUT_HANDLE)
        return (int*)STDOUT_FILENO;
    return (int*)STDERR_FILENO;
}

#ifdef USING_PTHREAD
intptr GetCurrentThreadId()
{
    return (intptr)pthread_self();
}

HANDLE GetCurrentThread()
{
    return pthread_self();
}

bool SetThreadPriority(HANDLE hThread, int nPriority)
{
    return false;
}

void* CreateThread(void* threadAttributes, int32 stackSize, BFThreadStartProc threadStartProc, void* param, uint32 flags, intptr* threadId)
{
    BF_ASSERT(sizeof(pthread_t) <= sizeof(void*));
    pthread_t thread;
    pthread_attr_t params;
    pthread_attr_init(&params);
    pthread_attr_setstacksize(&params, stackSize);
    //pthread_attr_setdetachstate(&params,PTHREAD_CREATE_DETACHED);
    pthread_create(threadId, &params, (void*(*)(void *))threadStartProc, param);
    thread = (pthread_t)*threadId;
    pthread_attr_destroy(&params);
    
    //printf("CreateThread: %08X\n", (intptr)thread);
    
    return (void*)thread;
}

void ResumeThread(HANDLE thread)
{NOT_IMPL
    thread_suspend(thread);
}

uint32 SuspendThread(HANDLE thread)
{NOT_IMPL
}
#else
BF_THREADID GetCurrentThreadId()
{
    return pthread_self();
}

HANDLE GetCurrentThread()
{
    return (HANDLE)(intptr)mach_thread_self();
}

static int gThreadPriorityDefaultValue = -1;

int GetThreadPriority(HANDLE hThread)
{
    mach_msg_type_number_t count = 1;
    boolean_t getDefault = false;
    thread_precedence_policy_data_t precedence;
    precedence.importance = -1;
    kern_return_t result = thread_policy_get((thread_act_t)(intptr)hThread,
                                             THREAD_PRECEDENCE_POLICY,
                                             (thread_policy_t)&precedence,
                                             &count,
                                             &getDefault);
    return precedence.importance - gThreadPriorityDefaultValue;
}

bool SetThreadPriority(HANDLE hThread, int nPriority)
{
    /*thread_precedence_policy_data_t precedence;
    precedence.importance = 0;
    
    if (gThreadPriorityDefaultValue == -1)
    {
        mach_msg_type_number_t count = 1;
        boolean_t getDefault = false;
        kern_return_t result = thread_policy_get((thread_act_t)(intptr)hThread,
                                                 THREAD_PRECEDENCE_POLICY,
                                                 (thread_policy_t)&precedence,
                                                 &count,
                                                 &getDefault);
        gThreadPriorityDefaultValue = precedence.importance;
    }
    precedence.importance = gThreadPriorityDefaultValue + nPriority * 16;
    kern_return_t result = thread_policy_set((thread_act_t)(intptr)hThread,
                               THREAD_PRECEDENCE_POLICY,
                               (thread_policy_t)&precedence,
                               THREAD_PRECEDENCE_POLICY_COUNT);
    return result == 0;*/
    
    int policy = 0;
    struct sched_param sched;
    sched.sched_priority = 0;
    pthread_getschedparam(pthread_self(), &policy, &sched);
    sched.sched_priority = std::min(nPriority * 16 + 47, 62);
    //int retVal = pthread_setschedparam(pthread_self(), policy, &sched);
    
    return true;
}

HANDLE OpenThread(int desiredAccess, bool inheritHandle, BF_THREADID threadId)
{
    return (HANDLE)(intptr)mach_thread_self();
}

void* CreateThread(void* threadAttributes, int32 stackSize, BFThreadStartProc threadStartProc, void* param, uint32 flags, BF_THREADID* threadId)
{
    BF_ASSERT(sizeof(pthread_t) <= sizeof(void*));
    pthread_t thread;
    pthread_attr_t params;
    pthread_attr_init(&params);
    pthread_attr_setstacksize(&params, stackSize);
    //pthread_attr_setdetachstate(&params,PTHREAD_CREATE_DETACHED);
    pthread_create(&thread, &params, (void*(*)(void *))threadStartProc, param);
    pthread_attr_destroy(&params);
    *threadId = thread;
    
    //printf("CreateThread: %08X\n", (int32)(intptr)thread);
    
    return (void*)thread;
}

uint32 SuspendThread(HANDLE thread)
{
    kern_return_t kernResult = thread_suspend((thread_act_t)(intptr)thread);
    BF_ASSERT(kernResult == KERN_SUCCESS);
    return 0;
}

uint32 ResumeThread(HANDLE thread)
{
    kern_return_t kernResult = thread_resume((thread_act_t)(intptr)thread);
    BF_ASSERT(kernResult == KERN_SUCCESS);
    return 0;
}


#endif

void Sleep(int32 ms)
{
    usleep(ms * 1000);
}

struct BFFindFileData
{
    DIR* mDirStruct;
    std::string mWildcard;
};

HANDLE FindFirstFileW(wchar_t* lpFileName, WIN32_FIND_DATAW* lpFindFileData)
{
    std::string findStr = Beefy::UTF8Encode(lpFileName);
    std::string wildcard;
    
    int lastSlashPos = std::max((int)findStr.rfind('/'), (int)findStr.rfind('\\'));
    if (lastSlashPos != -1)
    {
        wildcard = findStr.substr(lastSlashPos + 1);
        findStr = findStr.substr(0, lastSlashPos);
    }
    
    DIR* dir = opendir(findStr.c_str());
    if (dir == NULL)
    {
        BFSetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    
    BFFindFileData* bfFindFileData = new BFFindFileData();
    bfFindFileData->mDirStruct = dir;
    bfFindFileData->mWildcard = wildcard;
    
    if (!FindNextFileW((HANDLE)bfFindFileData, lpFindFileData))
    {
        FindClose((HANDLE)bfFindFileData);
        BFSetLastError(ERROR_NO_MORE_FILES);
        return INVALID_HANDLE_VALUE;
    }
    
    return (HANDLE)bfFindFileData;
}

void BFSystemTimeToFileTime(time_t timeT, long nsec, FILETIME* lpFileTime)
{
    uint64 ft64 = (timeT * 10000000ULL) + (nsec / 100);
    lpFileTime->dwHighDateTime = (uint32)(ft64 >> 32);
    lpFileTime->dwLowDateTime = (uint32)ft64;
}

void BFSystemTimeToFileTime(timespec& timeSpec, FILETIME* lpFileTime)
{
    uint64 ft64 = (timeSpec.tv_nsec * 10000000ULL) + (timeSpec.tv_nsec / 100);
    lpFileTime->dwHighDateTime = (uint32)(ft64 >> 32);
    lpFileTime->dwLowDateTime = (uint32)ft64;
}

bool FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW* lpFindFileData)
{
    memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAW));
    
    BFFindFileData* bfFindFileData = (BFFindFileData*)hFindFile;
    dirent* ent = NULL;
    while (true)
    {
        ent = readdir(bfFindFileData->mDirStruct);
        if (ent == NULL)
            return false;
        
        if (bfFindFileData->mWildcard == "*")
            break;
        std::string fileName = ent->d_name;
        if (bfFindFileData->mWildcard[0] == '*')
        {
            if (fileName.length() < bfFindFileData->mWildcard.length() - 1)
                continue;
            
            std::string checkFileName = fileName.substr(fileName.length() - bfFindFileData->mWildcard.length() + 1);
            if (checkFileName == bfFindFileData->mWildcard.substr(1))
                break;
        }
    }
    mbstowcs(lpFindFileData->cFileName, ent->d_name, 1024);
    
    struct stat statbuf = {0};
    int result = stat(ent->d_name, &statbuf);
    if (result == -1)
        return -1;
    if (S_ISDIR(statbuf.st_mode))
        lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
    if (S_ISREG(statbuf.st_mode))
        lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_NORMAL;
    else if (!S_ISLNK(statbuf.st_mode))
        lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DEVICE;
    if ((statbuf.st_mode & S_IRUSR) == 0)
        lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
    
    lpFindFileData->nFileSizeLow = (int32)statbuf.st_size;
    lpFindFileData->nFileSizeHigh = (int32)(statbuf.st_size >> 32);
    BFSystemTimeToFileTime(statbuf.st_ctimespec, &lpFindFileData->ftCreationTime);
    BFSystemTimeToFileTime(statbuf.st_atimespec, &lpFindFileData->ftLastAccessTime);
    BFSystemTimeToFileTime(statbuf.st_mtimespec, &lpFindFileData->ftLastWriteTime);
    
    return true;
}

bool FindClose(HANDLE hFindFile)
{
    BFFindFileData* bfFindFileData = (BFFindFileData*)hFindFile;
    closedir(bfFindFileData->mDirStruct);
    delete bfFindFileData;
    return true;
}

uint32 GetCurrentDirectoryW(uint32 nBufferLength, wchar_t* lpBuffer)
{
    char str[2048];
    getcwd(str, 2048);
    return (uint32)mbstowcs(lpBuffer, str, nBufferLength);
}

bool SetCurrentDirectoryW(wchar_t* lpBuffer)
{
    if (chdir(Beefy::UTF8Encode(lpBuffer).c_str()) == 0)
        return true;
    BFSetLastError(ERROR_FILE_NOT_FOUND);
    return false;
}

bool CopyFileW(wchar_t* lpExistingFileName, wchar_t* lpNewFileName, bool allowOverwrite)
{NOT_IMPL
    return true;
}

bool MoveFileW(wchar_t* lpExistingFileName, wchar_t* lpNewFileName)
{NOT_IMPL
    return true;
}

bool DeleteFileW(wchar_t* lpFileName)
{NOT_IMPL
    return true;
}

bool ReplaceFileW(wchar_t* lpReplacedFileName, wchar_t* lpReplacementFileName, wchar_t* lpBackupFileName,
                  uint32 dwReplaceFlags, void* lpExclude, void* lpReserved)
{NOT_IMPL
    return true;
}

bool SetFileAttributesW(wchar_t* lpFileName, uint32 dwFileAttributes)
{NOT_IMPL
    return true;
}

int32 GetFileType(HANDLE fileHandle)
{
    if (isatty((int)(intptr)fileHandle))
        return FILE_TYPE_CHAR;
    return FILE_TYPE_DISK;
}

bool GetFileAttributesExW(wchar_t* lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, void* lpFileInformation)
{NOT_IMPL
    return 0;
}

HANDLE CreateFileW(wchar_t* lpFileName, uint32 dwDesiredAccess, uint32 dwShareMode, SECURITY_ATTRIBUTES* lpSecurityAttributes,
                   uint32 dwCreationDisposition, uint32 dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    std::string fileName = Beefy::UTF8Encode(lpFileName);
    int flags = 0;
    int mode = 0;
    
    if ((dwDesiredAccess & (GENERIC_READ | GENERIC_WRITE)) == (GENERIC_READ | GENERIC_WRITE))
        flags |= O_RDWR;
    else if (dwDesiredAccess & GENERIC_READ)
        flags |= O_RDONLY;
    else if (dwDesiredAccess & GENERIC_WRITE)
        flags |= O_WRONLY;
    
    if (dwCreationDisposition == OPEN_ALWAYS)
        flags |= O_CREAT;
    else if (dwCreationDisposition == CREATE_ALWAYS)
        flags |= O_CREAT | O_TRUNC;
    else if (dwCreationDisposition == CREATE_NEW)
        flags |= O_CREAT | O_EXCL;
    else if (dwCreationDisposition == TRUNCATE_EXISTING)
        flags |= O_TRUNC;
    
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    
    return (HANDLE)(intptr)open(fileName.c_str(), flags, mode);
}

bool CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, SECURITY_ATTRIBUTES* lpPipeAttributes, uint32 nSize)
{NOT_IMPL
    return true;
}

bool WriteFile(HANDLE hFile, void* lpBuffer, uint32 nNumberOfBytesToWrite, uint32* lpNumberOfBytesWritten, OVERLAPPED* lpOverlapped)
{
#ifdef BF_PLATFORM_IOS
    int logType = -1;
    if (hFile == (int*)STDOUT_FILENO)
        logType = LOG_WARNING;
    else if (hFile == (int*)STDERR_FILENO)
        logType = LOG_ERR;
    
    if (logType != -1)
    {
        static std::string strOut;
        strOut.resize(nNumberOfBytesToWrite);
        memcpy(&strOut[0], lpBuffer, nNumberOfBytesToWrite);
        if ((strOut[0] != '\r') && (strOut[0] != '\n'))
            syslog(LOG_WARNING, "%s", strOut.c_str());
    }
#endif
    
    int writeCount = (int)::write((int)(intptr)hFile, lpBuffer, nNumberOfBytesToWrite);
    if (writeCount == -1)
    {
        //TODO: set gBFPlatformLastError
        lpNumberOfBytesWritten = 0;
        return false;
    }
    
    *lpNumberOfBytesWritten = (uint32)writeCount;
    return true;
}

static bool doSleep = false;

bool ReadFile(HANDLE hFile, void* lpBuffer, uint32 nNumberOfBytesToRead, uint32* lpNumberOfBytesRead, OVERLAPPED* lpOverlapped)
{
    int readCount = (int)::read((int)(intptr)hFile, lpBuffer, nNumberOfBytesToRead);
    
    if (doSleep)
        Sleep(1000);
    
    if (readCount == -1)
    {
        //TODO: set gBFPlatformLastError
        lpNumberOfBytesRead = 0;
        return false;
    }
    
    *lpNumberOfBytesRead = (uint32)readCount;
    return true;
}

bool CloseHandle_File(HANDLE handle)
{
    return close((int)(intptr)handle) == 0;
}

bool FlushFileBuffers(HANDLE handle)
{
    return fsync((int)(intptr)handle) == 0;
}

int32 SetFilePointer(HANDLE handle, int32 distanceToMove, int32* distanceToMoveHigh, uint32 moveMethod)
{
    return (int32)lseek((int)(intptr)handle, distanceToMove, (moveMethod == FILE_BEGIN) ? SEEK_SET : (moveMethod == FILE_CURRENT) ? SEEK_CUR : SEEK_END);
}

int32 GetFileSize(HANDLE hFile, uint32* fileSizeHigh)
{
    *fileSizeHigh = 0;
    int32 oldPos = (int32)lseek((int)(intptr)hFile, 0, SEEK_CUR);
    int32 size = (int32)lseek((int)(intptr)hFile, 0, SEEK_END);
    lseek((int)(intptr)hFile, oldPos, SEEK_SET);
    return size;
}

bool SetEndOfFile(HANDLE hFile)
{
    int32 curPos = (int32)lseek((int)(intptr)hFile, 0, SEEK_CUR);
    ftruncate((int)(intptr)hFile, curPos);
    return true;
}

bool SetFileTime(HANDLE hFile, const FILETIME* lpCreationTime, const FILETIME* lpLastAccessTime, const FILETIME* lpLastWriteTime)
{NOT_IMPL
    return true;
}

bool LockFile(HANDLE hFile, uint32 dwFileOffsetLow, uint32 dwFileOffsetHigh, uint32 nNumberOfBytesToLockLow, uint32 nNumberOfBytesToLockHigh)
{NOT_IMPL
    return true;
}

bool UnlockFile(HANDLE hFile, uint32 dwFileOffsetLow, uint32 dwFileOffsetHigh, uint32 nNumberOfBytesToUnlockLow, uint32 nNumberOfBytesToUnlockHigh)
{NOT_IMPL
    return true;
}

bool DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle, HANDLE* lpTargetHandle,
                     uint32 dwDesiredAccess, bool bInheritHandle, uint32 dwOptions)
{NOT_IMPL
    return true;
}

int32 GetTempPath(int32 bufferLen, wchar_t* str)
{NOT_IMPL
    return 0;
}

HANDLE CreateEventW(SECURITY_ATTRIBUTES* lpEventAttributes, bool bManualReset, bool bInitialState, wchar_t* lpName)
{
    BF_ASSERT(lpName == NULL);
    Beefy::SyncEvent* syncEvent = new Beefy::SyncEvent(bManualReset, bInitialState);
    return syncEvent;
}

HANDLE OpenEventW(uint32 dwDesiredAccess, bool bInheritHandle, wchar_t* lpName)
{NOT_IMPL
    return 0;
}

bool SetEvent(HANDLE handle)
{
    ((Beefy::SyncEvent*)handle)->Set();
    return true;
}

bool ResetEvent(HANDLE handle)
{
    ((Beefy::SyncEvent*)handle)->Reset();
    return true;
}

bool CloseHandle_Event(HANDLE handle)
{
    delete (Beefy::SyncEvent*)handle;
    return true;
}


int WaitForSingleObject(HANDLE obj, int waitMs)
{
    Beefy::SyncEvent* syncEvent = (Beefy::SyncEvent*)obj;
    bool worked = syncEvent->WaitFor(waitMs);
    return worked ? WAIT_OBJECT_0 : -1;
}

int WaitForSingleObject_Thread(HANDLE obj, int waitMs)
{
    BF_ASSERT(waitMs == -1);
    //printf("WaitForSingleObject_Thread: %08X\n", (int32)(intptr)obj);
    int result = pthread_join((pthread_t)obj, NULL);
    BF_ASSERT(result == 0);
    return WAIT_OBJECT_0;
}

HANDLE CreateMutexW(SECURITY_ATTRIBUTES* lpMutexAttributes, bool bInitialOwner, wchar_t* lpName)
{NOT_IMPL
    return 0;
}

HANDLE OpenMutexW(uint32 dwDesiredAccess, bool bInheritHandle, wchar_t* lpName)
{NOT_IMPL
    return 0;
}

bool ReleaseMutex(HANDLE mutex)
{NOT_IMPL
    return true;
}

uint32 FormatMessageW(uint32 dwFlags, void* lpSource, uint32 dwMessageId, uint32 dwLanguageId, wchar_t* lpBuffer, uint32 nSize, va_list* Arguments)
{NOT_IMPL
    return 0;
}

int32 WSAGetLastError()
{NOT_IMPL
    return 0;
}

void GetExitCodeProcess(HANDLE handle, DWORD* code)
{NOT_IMPL
}

bool CloseHandle_Thread(HANDLE handle)
{NOT_IMPL
    return true;
}

bool CloseHandle_Process(HANDLE handle)
{NOT_IMPL
    return true;
}

bool GetProcessTimes(HANDLE handle, FILETIME* createTime, FILETIME* exitTime, FILETIME* kernelTime, FILETIME* userTime)
{NOT_IMPL
    return true;
}

bool GetProcessWorkingSetSize(HANDLE handle, size_t* curMin, size_t* curMax)
{NOT_IMPL
    return true;
}

bool SetProcessWorkingSetSize(HANDLE handle, size_t curMin, size_t curMax)
{NOT_IMPL
    return true;
}

bool EnumProcessModules(HANDLE handle, HMODULE* mods, DWORD cb, DWORD* needed)
{NOT_IMPL
    return true;
}

HANDLE OpenProcess(DWORD desiredAccess, bool inheritHandle, DWORD pid)
{NOT_IMPL
    return 0;
}

bool GetModuleBaseNameW(HANDLE handle, HMODULE mod, wchar_t* modname, int maxLen)
{NOT_IMPL
    return true;
}

bool GetModuleFileNameExW(HANDLE handle, HMODULE mod, wchar_t* modname, int maxLen)
{NOT_IMPL
    return true;
}

bool GetModuleInformation(HANDLE handle, HMODULE mod, MODULEINFO* modinfo, int modInfoSize)
{NOT_IMPL
    return true;
}

int GetPriorityClass(HANDLE handle)
{NOT_IMPL
    return 0;
}

bool SetPriorityClass(HANDLE handle, int priClass)
{NOT_IMPL
    return true;
}

bool TerminateProcess(HANDLE handle, int termCode)
{NOT_IMPL
    return true;
}

bool WaitForInputIdle(HANDLE handle, int ms)
{NOT_IMPL
    return true;
}

int32 GetCurrentProcessId()
{NOT_IMPL
    return 0;
}

HANDLE GetCurrentProcess()
{NOT_IMPL
    return 0;
}

bool GetDiskFreeSpaceExW(wchar_t* pathNameStr, ULARGE_INTEGER* wapi_free_bytes_avail, ULARGE_INTEGER*wapi_total_number_of_bytes, ULARGE_INTEGER* wapi_total_number_of_free_bytes)
{NOT_IMPL
    return true;
}

uint32 GetDriveTypeW(wchar_t* driveName)
{NOT_IMPL
    return 0;
}

bool GetVolumeInformationW(wchar_t* lpRootPathName, wchar_t* lpVolumeNameBuffer, uint32 nVolumeNameSize, uint32* lpVolumeSerialNumber, uint32* lpMaximumComponentLength, uint32* lpFileSystemFlags, wchar_t* lpFileSystemNameBuffer, uint32 nFileSystemNameSize)
{NOT_IMPL
    return true;
}

uint64 BFClockGetTime()
{
    static bool initialized = false;
    static clock_serv_t cclock;
    if (!initialized)
    {
        host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
        initialized = true;
    }
    
    time_t rawtime;
    struct tm timeinfo;
    time(&rawtime);
    timeinfo = *localtime(&rawtime);
    
    mach_timespec_t mts;
    clock_get_time(cclock, &mts);
    //mach_port_deallocate(mach_task_self(), cclock);
    //if (timeinfo.tm_isdst)
        //mts.tv_sec += 60 * 60;
    return ((mts.tv_sec /*+ timeinfo.tm_gmtoff*/) * 10000000ULL) + (mts.tv_nsec / 100);
}

DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION* lpTimeZoneInformation)
{
    std::wstring tzName0 = Beefy::UTF8Decode(tzname[0]);
    std::wstring tzName1 = Beefy::UTF8Decode(tzname[1]);
    
    bool isDST = false;
    
    time_t timeNow;
    time(&timeNow);
    tm tmNow = *gmtime(&timeNow);
    isDST = tmNow.tm_isdst;
    
    struct tm checkTM;
    memset(&checkTM, 0, sizeof(tm));
    checkTM.tm_mday = 1;
    checkTM.tm_year = tmNow.tm_year;
    time_t checkTime = mktime(&checkTM);
    
    time_t lastOffset = 0;
    time_t minOffset = 0;
    time_t maxOffset = 0;
    
    for (int pass = 0; pass < 2; pass++)
    {
        int searchDir = 60*60*24;
        int thresholdCount = 0;
        
        while (true)
        {
            checkTime += searchDir;
            
            tm checkTM = *gmtime(&checkTime);
            
            if (checkTM.tm_year != tmNow.tm_year)
                break; // No DST
            
            mktime(&checkTM);
            
            time_t offset = checkTM.tm_gmtoff;
            if (lastOffset != offset)
            {
                if (thresholdCount == 0)
                {
                    minOffset = offset;
                    maxOffset = offset;
                }
                else if (thresholdCount == 3)
                {
                    SYSTEMTIME* sysTimeP = (offset == minOffset) ?
                    &lpTimeZoneInformation->StandardDate :
                    &lpTimeZoneInformation->DaylightDate;
                    
                    if (offset == minOffset)
                        tzName0 = Beefy::UTF8Decode(checkTM.tm_zone);
                    else
                        tzName1 = Beefy::UTF8Decode(checkTM.tm_zone);
                    
                    sysTimeP->wDay = 0;
                    sysTimeP->wDayOfWeek = 0;
                    sysTimeP->wYear = checkTM.tm_year + 1900;
                    sysTimeP->wMonth = checkTM.tm_mon;
                    sysTimeP->wDay = checkTM.tm_mday + 1;
                    sysTimeP->wHour = checkTM.tm_hour;
                    sysTimeP->wMinute = checkTM.tm_min;
                    sysTimeP->wSecond = checkTM.tm_sec;
                    sysTimeP->wMilliseconds = 0;
                    
                    break;
                }
                else
                {
                    if (thresholdCount == 1)
                        searchDir /= -24;
                    else
                        searchDir /= -60;
                    minOffset = std::min(minOffset, offset);
                    maxOffset = std::max(maxOffset, offset);
                }
                thresholdCount++;
                lastOffset = offset;
            }
        }
    }
	
    wcsncpy(lpTimeZoneInformation->StandardName, tzName0.c_str(), 32);
    wcsncpy(lpTimeZoneInformation->DaylightName, tzName1.c_str(), 32);
    
    lpTimeZoneInformation->DaylightBias = (int32)maxOffset;
    lpTimeZoneInformation->StandardBias = (int32)minOffset;
    
    if (minOffset == maxOffset)
        return 0;
    return isDST ? 2 : 1;
}

bool SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, FILETIME* lpFileTime)
{
    tm checkTM;
    checkTM.tm_year = lpSystemTime->wYear - 1900;
    checkTM.tm_mon = lpSystemTime->wMonth;
    checkTM.tm_mday = lpSystemTime->wDay - 1;
    checkTM.tm_hour = lpSystemTime->wHour;
    checkTM.tm_min = lpSystemTime->wMinute;
    checkTM.tm_sec = lpSystemTime->wSecond;
    
    time_t timeT = mktime(&checkTM);
    
    uint64 ft64 = timeT * 10000000ULL;
    
    lpFileTime->dwHighDateTime = (uint32)(ft64 >> 32);
    lpFileTime->dwLowDateTime = (uint32)ft64;
    return true;
}

uint64 Beefy::BFGetTickCountMicro()
{
    static bool initialized = false;
    static clock_serv_t cclock;
    if (!initialized)
    {
        host_get_clock_service(mach_host_self(), REALTIME_CLOCK, &cclock);
        initialized = true;
    }
    
    mach_timespec_t mts;
    clock_get_time(cclock, &mts);
    return (mts.tv_sec * 1000000ULL) + (mts.tv_nsec / 1000);
}

uint64 Beefy::BFGetTickCountMicroFast()
{
    return BFGetTickCountMicro();
}

#if (defined __IPHONEOS__) && !TARGET_IPHONE_SIMULATOR

void BFGetThreadRegisters(HANDLE threadHandle, intptr* stackPtr, intptr* dataPtr)
{
    mach_msg_type_number_t thread_state_count = ARM_UNIFIED_THREAD_STATE_COUNT;
    arm_unified_thread_state_t unifiedState;
    kern_return_t kernResult = thread_get_state((thread_act_t)(intptr)threadHandle, ARM_UNIFIED_THREAD_STATE, (natural_t*)&unifiedState.ts_32, &thread_state_count);

#ifdef BF32
    arm_thread_state32_t state = unifiedState.ts_32;
#else
    zzz SHOULDN'T GET HERE
    arm_thread_state64_t state = unifiedState.ts_64;
#endif
      
    BF_ASSERT(kernResult == KERN_SUCCESS);
    
    *stackPtr = (intptr)state.__sp;
    
    if (dataPtr != NULL)
	{
#ifdef BF32
		intptr* curPtr = dataPtr;
		*(curPtr++) = (intptr)state.__r[0];
   		*(curPtr++) = (intptr)state.__r[1];
		*(curPtr++) = (intptr)state.__r[2];
   		*(curPtr++) = (intptr)state.__r[3];
		*(curPtr++) = (intptr)state.__r[4];
   		*(curPtr++) = (intptr)state.__r[5];
		*(curPtr++) = (intptr)state.__r[6];
   		*(curPtr++) = (intptr)state.__r[7];
		*(curPtr++) = (intptr)state.__r[8];
   		*(curPtr++) = (intptr)state.__r[9];
		*(curPtr++) = (intptr)state.__r[10];
   		*(curPtr++) = (intptr)state.__r[11];
		*(curPtr++) = (intptr)state.__r[12];
   		*(curPtr++) = (intptr)state.__lr;
      	*(curPtr++) = (intptr)state.__cpsr;
#else
		intptr* curPtr = dataPtr;
#endif
        int count = (int)(curPtr - dataPtr);
		BF_ASSERT(count == BF_REGISTER_COUNT);
	}
}

#else

void BFGetThreadRegisters(HANDLE threadHandle, intptr* stackPtr, intptr* dataPtr)
{
#ifdef BF32
    mach_msg_type_number_t thread_state_count = x86_THREAD_STATE32_COUNT;
    x86_thread_state32_t state;
    kern_return_t kernResult = thread_get_state((thread_act_t)threadHandle, x86_THREAD_STATE32, (natural_t*)&state, &thread_state_count);
#else
    mach_msg_type_number_t thread_state_count = x86_THREAD_STATE64_COUNT;
    x86_thread_state64_t state;
    kern_return_t kernResult = thread_get_state((thread_act_t)threadHandle, x86_THREAD_STATE64, (natural_t*)&state, &thread_state_count);
#endif
    BF_ASSERT(kernResult == KERN_SUCCESS);
    
    *stackPtr = (intptr)state.__esp;
    
    if (dataPtr != NULL)
	{
#ifdef BF32
		intptr* curPtr = dataPtr;
		*(curPtr++) = (intptr)state.__eax;
		*(curPtr++) = (intptr)state.__ebx;
		*(curPtr++) = (intptr)state.__ecx;
		*(curPtr++) = (intptr)state.__edx;
		*(curPtr++) = (intptr)state.__esi;
		*(curPtr++) = (intptr)state.__edi;
        *(curPtr++) = (intptr)state.__ebp;
#else
		intptr* curPtr = dataPtr;
		*(curPtr++) = (intptr)ctx.Rax;
		*(curPtr++) = (intptr)ctx.Rbx;
		*(curPtr++) = (intptr)ctx.Rcx;
		*(curPtr++) = (intptr)ctx.Rdx;
		*(curPtr++) = (intptr)ctx.Rsi;
		*(curPtr++) = (intptr)ctx.Rdi;
        *(curPtr++) = (intptr)ctx.Rbp;
		*(curPtr++) = (intptr)ctx.R8;
		*(curPtr++) = (intptr)ctx.R9;
		*(curPtr++) = (intptr)ctx.R10;
		*(curPtr++) = (intptr)ctx.R11;
		*(curPtr++) = (intptr)ctx.R12;
		*(curPtr++) = (intptr)ctx.R13;
		*(curPtr++) = (intptr)ctx.R14;
		*(curPtr++) = (intptr)ctx.R15;
#endif
        int count = curPtr - dataPtr;
		BF_ASSERT(count == BF_REGISTER_COUNT);
	}
}

#endif

int64 abs(int64 val)
{
    return llabs(val);
}

void mkdir(const char* path)
{
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}