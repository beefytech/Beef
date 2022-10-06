#include "Common.h"
#include "BFPlatform.h"
#include <sys/stat.h>
#ifndef BF_PLATFORM_DARWIN
#include <sys/sysinfo.h>
#endif
#include <sys/wait.h>
#include <wchar.h>
#include <fcntl.h>
#include <time.h>
#ifdef BFP_HAS_DLINFO
#include <link.h>
#endif
#include <dirent.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <dlfcn.h>
#include "../PlatformInterface.h"
#include "../PlatformHelper.h"
#include "../../util/CritSect.h"
#include "../../util/Dictionary.h"
#include "../../util/Hash.h"
#include "../../third_party/putty/wildcard.h"
#ifdef BFP_HAS_EXECINFO
#include <execinfo.h>
#endif

#ifdef BFP_HAS_BACKTRACE

#ifdef BFP_BACKTRACE_PATH
#include BFP_BACKTRACE_PATH
#elif BFP_HAS_BACKTRACE
#include "backtrace.h"
#include "backtrace-supported.h"
#endif

#endif
#define STB_SPRINTF_DECORATE(name) BF_stbsp_##name
#include "../../third_party/stb/stb_sprintf.h"
#include <cxxabi.h>
#include <random>

#ifndef BFP_PRINTF
#define BFP_PRINTF(...) printf (__VA_ARGS__)
#define BFP_ERRPRINTF(...) fprintf (stderr, __VA_ARGS__)
#endif

//#include <cxxabi.h>
//using __cxxabiv1::__cxa_demangle;

USING_NS_BF;

struct BfpPipeInfo
{
	String mPipePath;
	int mWriteHandle;
};

struct BfpFile
{	
	BfpPipeInfo* mPipeInfo;
	int mHandle;	
	bool mNonBlocking;
	bool mAllowTimeout;	
	bool mIsStd;

	BfpFile()
	{
		mPipeInfo = NULL;
		mHandle = -1;		
		mNonBlocking = false;
		mAllowTimeout = false;		
		mIsStd = false;
	}

	BfpFile(int handle)
	{	
		mPipeInfo = NULL;
		mHandle = handle;		
		mNonBlocking = false;
		mAllowTimeout = false;		
		mIsStd = false;
	}

	~BfpFile()
	{
		delete mPipeInfo;
	}
};

class FileWatchManager;
static FileWatchManager* gFileWatchManager = NULL;
class FileWatchManager
{
public:
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual BfpFileWatcher* WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult) = 0;
    virtual void Remove(BfpFileWatcher* watcher) = 0;
    static FileWatchManager* Get();
};

class NullFilewatchManager : public FileWatchManager
{
    virtual bool Init() { return false; }
    virtual void Shutdown() {}
    virtual BfpFileWatcher* WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult) { NOT_IMPL; return NULL; }
    virtual void Remove(BfpFileWatcher* watcher) { NOT_IMPL; }
};

#ifndef BFP_HAS_FILEWATCHER
FileWatchManager* FileWatchManager::Get()
{
    if (gFileWatchManager == NULL)
    {
        gFileWatchManager = new NullFilewatchManager();
        gFileWatchManager->Init();
    }
    return gFileWatchManager;
}
#endif

BfpTimeStamp BfpToTimeStamp(const timespec& ts)
{
    return (int64)(ts.tv_sec * 10000000) + (int64)(ts.tv_nsec / 100) + 116444736000000000;
}

int gBFPlatformLastError = 0;

uint32 Beefy::BFTickCount()
{    
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint32)((uint64)now.tv_sec * 1000.0 + (uint64)now.tv_nsec / 1000000);
}

int64 Beefy::EndianSwap(int64 val)
{
	return __builtin_bswap64(val);
}

/*int* GetStdHandle(int32 handleId)
{
    if (handleId == STD_INPUT_HANDLE)
        return (int*)STDIN_FILENO;
    if (handleId == STD_OUTPUT_HANDLE)
        return (int*)STDOUT_FILENO;
    return (int*)STDERR_FILENO;
}*/

/*int32 GetFileType(HANDLE fileHandle)
{
    if (isatty(file->mHandleHandle))
        return FILE_TYPE_CHAR;
    return FILE_TYPE_DISK;
}*/

/*bool WriteFile(HANDLE hFile, void* lpBuffer, uint32 nNumberOfBytesToWrite, uint32* lpNumberOfBytesWritten, OVERLAPPED* lpOverlapped)
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
}*/

int64 Beefy::GetFileTimeWrite(const StringImpl& path)
{
    struct stat statbuf = {0};
    int result = stat(path.c_str(), &statbuf);
    if (result == -1)
        return 0;
        
    return statbuf.st_mtime;
}

/*DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION* lpTimeZoneInformation)
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
}*/

bool Beefy::FileExists(const StringImpl& path, String* outActualName)
{
    struct stat statbuf = {0};
    int result = stat(path.c_str(), &statbuf);
    if (result != 0)
        return false;
    return !S_ISDIR(statbuf.st_mode);
}

bool Beefy::DirectoryExists(const StringImpl& path, String* outActualName)
{
    struct stat statbuf = {0};
    int result = stat(path.c_str(), &statbuf);
    if (result != 0)
        return false;
    return S_ISDIR(statbuf.st_mode);
}

uint64 Beefy::BFGetTickCountMicro()
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return ((uint64)now.tv_sec * 1000000.0 + (uint64)now.tv_nsec / 1000);
}

uint64 Beefy::BFGetTickCountMicroFast()
{
    return BFGetTickCountMicro();
}

/*
int64 abs(int64 val)
{
    return llabs(val);
}
*/
void mkdir(const char* path)
{
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

typedef void(*CrashInfoFunc)();

struct BfpGlobalData
{
	CritSect mSysCritSect;
	String mCrashInfo;
	Array<CrashInfoFunc> mCrashInfoFuncs;
};

static BfpGlobalData* gBfpGlobal;

static BfpGlobalData* BfpGetGlobalData()
{
	if (gBfpGlobal == NULL)
		gBfpGlobal = new BfpGlobalData();
	return gBfpGlobal;
}

#ifdef BFP_HAS_BACKTRACE

struct bt_ctx {
    struct backtrace_state *state;
    int error;
};

static void error_callback(void *data, const char *msg, int errnum)
{
    struct bt_ctx *ctx = (bt_ctx*)data;
    BFP_ERRPRINTF("ERROR: %s (%d)", msg, errnum);
    ctx->error = 1;
}

static void syminfo_callback (void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize)
{
    char str[4096];
    if (symname)
        BF_stbsp_snprintf(str, 4096, "%@ %s\n", pc, symname);
    else
        BF_stbsp_snprintf(str, 4096, "%@\n", pc);
    BFP_ERRPRINTF("%s", str);
}

static int full_callback(void *data, uintptr_t pc, const char* filename, int lineno, const char* function)
{
    struct bt_ctx *ctx = (bt_ctx*)data;
    if (function)
    {
        int status = -1;
        char* demangledName = abi::__cxa_demangle(function, NULL, NULL, &status );
        const char* showName = (demangledName != NULL) ? demangledName : function;

        char str[4096];
        BF_stbsp_snprintf(str, 4096, "%@ %s %s:%d\n", pc, showName, filename?filename:"??", lineno);
        BFP_ERRPRINTF("%s", str);

        if (demangledName != NULL)
            free(demangledName);
    }
    else
        backtrace_syminfo (ctx->state, pc, syminfo_callback, error_callback, data);
    return 0;
}

static int simple_callback(void *data, uintptr_t pc)
{
    struct bt_ctx *ctx = (bt_ctx*)data;
    backtrace_pcinfo(ctx->state, pc, full_callback, error_callback, data);
    return 0;
}

static inline void bt(struct backtrace_state *state)
{
    struct bt_ctx ctx = {state, 0};
    //backtrace_print(state, 0, stdout);
    backtrace_simple(state, 2, simple_callback, error_callback, &ctx);
}

#endif

typedef void(*CrashInfoFunc)();

static String gCmdLine;
static String gExePath;

typedef struct _Unwind_Context _Unwind_Context;   // opaque

typedef enum {
  _URC_NO_REASON = 0,
  _URC_OK = 0,
  _URC_FOREIGN_EXCEPTION_CAUGHT = 1,
  _URC_FATAL_PHASE2_ERROR = 2,
  _URC_FATAL_PHASE1_ERROR = 3,
  _URC_NORMAL_STOP = 4,
  _URC_END_OF_STACK = 5,
  _URC_HANDLER_FOUND = 6,
  _URC_INSTALL_CONTEXT = 7,
  _URC_CONTINUE_UNWIND = 8,
  _URC_FAILURE = 9
} _Unwind_Reason_Code;

typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn)(struct _Unwind_Context *, void *);
extern "C" _Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Trace_Fn, void *);
extern "C" uintptr_t _Unwind_GetIP(struct _Unwind_Context *context);

static String gUnwindExecStr;
static int gUnwindIdx = 0;

static _Unwind_Reason_Code UnwindHandler(struct _Unwind_Context* context, void* ref)
{       
    gUnwindIdx++;
    if (gUnwindIdx < 2)
        return _URC_NO_REASON;
    
    void* addr = (void*)_Unwind_GetIP(context);

#if BFP_HAS_ATOS    
    gUnwindExecStr += StrFormat(" %p", addr);
#else
    Dl_info info;
    if (dladdr(addr, &info))
    {
        if (info.dli_sname)
            BFP_ERRPRINTF("0x%p %s\n", addr, info.dli_sname);
        else if (info.dli_fname)
            BFP_ERRPRINTF("0x%p %s\n", addr, info.dli_fname);
        else
            BFP_ERRPRINTF("0x%p\n", addr);
    }
#endif
    return _URC_NO_REASON;
}

static bool FancyBacktrace()
{
    gUnwindExecStr += StrFormat("atos -p %d", getpid());    
    _Unwind_Backtrace(&UnwindHandler, NULL);
#if BFP_HAS_ATOS
    return system(gUnwindExecStr.c_str()) == 0;
#else    
    return true;
#endif
}

static void Crashed()
{
    //
    {
        AutoCrit autoCrit(BfpGetGlobalData()->mSysCritSect);

        String debugDump;

        debugDump += "**** FATAL APPLICATION ERROR ****\n";

        for (auto func : BfpGetGlobalData()->mCrashInfoFuncs)
            func();

        if (!BfpGetGlobalData()->mCrashInfo.IsEmpty())
        {
            debugDump += BfpGetGlobalData()->mCrashInfo;
            debugDump += "\n";
        }

        BFP_ERRPRINTF("%s", debugDump.c_str());
    }

    if (!FancyBacktrace())
    {
#ifdef BFP_HAS_EXECINFO        
        void* array[64];
        size_t size;
        char** strings;
        size_t i;

        size = backtrace(array, 64);
        strings = backtrace_symbols(array, size);
      
        for (i = 0; i < size; i++)
            BFP_ERRPRINTF("%s\n", strings[i]);

        free(strings);
#endif
    }

    exit(1);
}

static void SigHandler(int sig)
{
	//printf("SigHandler paused...\n");	

    const char* sigName = NULL;
    switch (sig)
    {
    case SIGFPE:
        sigName = "SIGFPE";
        break;
    case SIGSEGV:
        sigName = "SIGSEGV";
        break;
    case SIGABRT:
        sigName = "SIGABRT";
        break;
    case SIGILL:
        sigName = "SIGILL";
        break;
    }

    if (sigName != NULL)
		BfpGetGlobalData()->mCrashInfo += StrFormat("Signal: %s\n", sigName);
    else
		BfpGetGlobalData()->mCrashInfo += StrFormat("Signal: %d\n", sig);
    Crashed();
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_Init(int version, BfpSystemInitFlags flags)
{
	BfpGetGlobalData();

    if (version != BFP_VERSION)
    {
        BfpSystem_FatalError(StrFormat("Bfp build version '%d' does not match requested version '%d'", BFP_VERSION, version).c_str(), "BFP FATAL ERROR");
    }

    //if (ptrace(PTRACE_TRACEME, 0, 1, 0) != -1)
    {
        //ptrace(PTRACE_DETACH, 0, 1, 0);
        //signal(SIGSEGV, SigHandler);
        //signal(SIGFPE, SigHandler);
        //signal(SIGABRT, SigHandler);

        /*struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = signal_segv;
        action.sa_flags = SA_SIGINFO;
        if(sigaction(SIGSEGV, &action, NULL) < 0)
            perror("sigaction");*/
    }
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCommandLine(int argc, char** argv)
{	
    char exePath[PATH_MAX] = { 0 };
    int nchar = readlink("/proc/self/exe", exePath, PATH_MAX);
    if (nchar > 0)
    {
        gExePath = exePath;
    }
    else
    {
        char* relPath = argv[0];
        char* cwd = getcwd(NULL, 0);
        gExePath = GetAbsPath(relPath, cwd);
        free(cwd);
    }    
	
	for (int i = 0; i < argc; i++)
	{
		if (i != 0)
			gCmdLine.Append(' ');

		String arg = argv[i];
		if ((arg.Contains(' ')) || (arg.Contains('\"')))
		{
			arg.Replace("\"", "\\\"");
			gCmdLine.Append("\"");
			gCmdLine.Append(arg);
			gCmdLine.Append("\"");
		}
		else
			gCmdLine.Append(arg);
	}
}


BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCrashReportKind(BfpCrashReportKind crashReportKind)
{

}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfoFunc(BfpCrashInfoFunc crashInfoFunc)
{
    AutoCrit autoCrit(BfpGetGlobalData()->mSysCritSect);
	BfpGetGlobalData()->mCrashInfoFuncs.Add(crashInfoFunc);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfo(const char* str) // Can do at any time, or during CrashInfoFunc callbacks
{
    AutoCrit autoCrit(BfpGetGlobalData()->mSysCritSect);
	BfpGetGlobalData()->mCrashInfo.Append(str);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCrashRelaunchCmd(const char* str)
{    
}

void BfpSystem_Shutdown()
{
    if (gFileWatchManager != NULL)
    {
        gFileWatchManager->Shutdown();
        gFileWatchManager = NULL;
    }
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_TickCount()
{
    return Beefy::BFTickCount();
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpSystem_GetTimeStamp()
{
	struct timeval tv;
	BfpTimeStamp result = 11644473600LL;
	gettimeofday(&tv, NULL);
	result += tv.tv_sec;
	result *= 10000000LL;
	result += tv.tv_usec * 10;
	return result;
}

BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_EndianSwap16(uint16 val)
{
    return __builtin_bswap16(val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_EndianSwap32(uint32 val)
{
    return __builtin_bswap32(val);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_EndianSwap64(uint64 val)
{
    return __builtin_bswap64(val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchange32(uint32* ptr, uint32 val)
{
	// __sync_lock_test_and_set only has Acquire semantics, so we need a __sync_synchronize to enforce a full barrier
	uint32 prevVal = __sync_lock_test_and_set(ptr, val);
	__sync_synchronize();
	return prevVal;	
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchange64(uint64* ptr, uint64 val)
{
	// __sync_lock_test_and_set only has Acquire semantics, so we need a __sync_synchronize to enforce a full barrier
	uint64 prevVal = __sync_lock_test_and_set(ptr, val);
	__sync_synchronize();
	return prevVal;	
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd32(uint32* ptr, uint32 val)
{
	return __sync_fetch_and_add(ptr, val);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd64(uint64* ptr, uint64 val)
{
	return __sync_fetch_and_add(ptr, val);
}

BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange32(uint32* ptr, uint32 oldVal, uint32 newVal)
{
    return __sync_val_compare_and_swap(ptr, oldVal, newVal);
}

BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange64(uint64* ptr, uint64 oldVal, uint64 newVal)
{
    return __sync_val_compare_and_swap(ptr, oldVal, newVal);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_FatalError(const char* error, const char* title)
{
    BFP_ERRPRINTF("%s\n", error);
    fflush(stderr);
    Crashed();
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetCommandLine(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
    TryStringOut(gCmdLine, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetExecutablePath(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
#ifdef BF_PLATFORM_DARWIN
    if (gExePath.IsEmpty())
    {
        char path[4096];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0)
            gExePath = path;

        // When when running with a './file', we end up with an annoying '/./' in our path
        gExePath.Replace("/./", "/");        
    }
#endif

	TryStringOut(gExePath, outStr, inOutStrSize, (BfpResult*)outResult);
}

extern char **environ;

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetEnvironmentStrings(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
    String env;

    char** envPtr = environ;
    while (true)
    {
        char* envStr = *envPtr;
        if (envStr == NULL)
            break;

        env.Append(envStr, strlen(envStr) + 1);
        ++envPtr;
    }

    TryStringOut(env, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT int BFP_CALLTYPE BfpSystem_GetNumLogicalCPUs(BfpSystemResult* outResult)
{	
#ifdef BF_PLATFORM_ANDROID
    //TODO: Handle this
    OUTRESULT(BfpSystemResult_Ok);
	return 1;
#elif defined BF_PLATFORM_DARWIN
    OUTRESULT(BfpSystemResult_Ok);
    int count = 1;
    size_t count_len = sizeof(count);
    sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);
    return count;
#else
    OUTRESULT(BfpSystemResult_Ok);
    return get_nprocs_conf();
#endif
}

BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTick()
{
	return 10000000;
}

BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTickFreq()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec * 10000000LL) + now.tv_nsec / 100;
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_CreateGUID(BfpGUID* outGuid)
{
// 	uuid_t guid;	
// 	uuid_generate(guid);	
// 	BfpGUID bfpGuid;
// 	memcpy(&bfpGuid, guid, 16);
// 	return bfpGuid;

	uint8* ptr = (uint8*)outGuid;

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint8> dis(0, 255);
	for (int i = 0; i < 16; i++)
		ptr[i] = dis(gen);

	// variant must be 10xxxxxx
	ptr[8] &= 0xBF;
	ptr[8] |= 0x80;

	// version must be 0100xxxx
	ptr[6] &= 0x4F;
	ptr[6] |= 0x40;
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetComputerName(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
	char hostName[1024];
	gethostname(hostName, 1024);
	TryStringOut(hostName, outStr, inOutStrSize, (BfpResult*)outResult);
}

// BfpProcess

BFP_EXPORT intptr BFP_CALLTYPE BfpProcess_GetCurrentId()
{
    return getpid();
}

BFP_EXPORT bool BFP_CALLTYPE BfpProcess_IsRemoteMachine(const char* machineName)
{
    return false;
}

BFP_EXPORT BfpProcess* BFP_CALLTYPE BfpProcess_GetById(const char* machineName, int processId, BfpProcessResult* outResult)
{
    NOT_IMPL;
    return NULL;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_Enumerate(const char* machineName, BfpProcess** outProcesses, int* inOutProcessesSize, BfpProcessResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_Release(BfpProcess* process)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetMainWindowTitle(BfpProcess* process, char* outTitle, int* inOutTitleSize, BfpProcessResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetProcessName(BfpProcess* process, char* outName, int* inOutNameSize, BfpProcessResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT int BFP_CALLTYPE BfpProcess_GetProcessId(BfpProcess* process)
{
    NOT_IMPL;
    return 0;
}

// BfpSpawn

struct BfpSpawn
{
    int mPid;
    bool mExited;
    int mStatus;
    int mStdInFD;
    int mStdOutFD;
    int mStdErrFD;
};

BFP_EXPORT BfpSpawn* BFP_CALLTYPE BfpSpawn_Create(const char* inTargetPath, const char* args, const char* workingDir, const char* env, BfpSpawnFlags flags, BfpSpawnResult* outResult)
{
    Beefy::Array<Beefy::StringView> stringViews;

    //printf("BfpSpawn_Create: %s %s %x\n", inTargetPath, args, flags);    

    char* prevWorkingDir = NULL;

	if ((workingDir != NULL) && (workingDir[0] != 0))
	{
		if (chdir(workingDir) != 0)
		{
			//printf("CHDIR failed %s\n", workingDir);
			OUTRESULT(BfpSpawnResult_UnknownError);
			return NULL;
		}

        prevWorkingDir = getcwd(NULL, 0);
	}

    defer(
        {
            if (prevWorkingDir != NULL)
            {
                chdir(prevWorkingDir);
                free(prevWorkingDir);
            }
        });

	String newArgs;
	String tempFileName;

	if ((flags & BfpSpawnFlag_UseArgsFile) != 0)
	{
		char tempFileNameStr[256];
		int size = 256;
		BfpFileResult fileResult;
		BfpFile_GetTempFileName(tempFileNameStr, &size, &fileResult);
		if (fileResult == BfpFileResult_Ok)
		{
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
	}

    int32 firstCharIdx = -1;
    bool inQuote = false;

	String targetPath = inTargetPath;
	String verb;
	if ((flags & BfpSpawnFlag_UseShellExecute) != 0)
	{
		String target = targetPath;
		int barPos = (int)target.IndexOf('|');
		if (barPos != -1)
		{
			verb = targetPath.Substring(barPos + 1);
			targetPath.RemoveToEnd(barPos);
		}				
	}

    int32 i = 0;
    for ( ; true; i++)
    {
        char c = args[i];
        if (c == '\0')
            break;
        if ((c == ' ') && (!inQuote))
        {
            if (firstCharIdx != -1)
            {
                stringViews.Add(Beefy::StringView(args + firstCharIdx, i - firstCharIdx));
                firstCharIdx = -1;
            }
        }
        else
        {
            if (firstCharIdx == -1)
                firstCharIdx = i;
            if (c == '"')
                inQuote = !inQuote;
            else if ((inQuote) && (c == '\\'))
            {
                c = args[i + 1];
                if (c == '"')
                    i++;
            }
        }
    }
    if (firstCharIdx != -1)
        stringViews.Add(Beefy::StringView(args + firstCharIdx, i - firstCharIdx));

    Beefy::Array<char*> argvArr;

    if ((flags & BfpSpawnFlag_ArgsIncludesTarget) == 0)
        argvArr.Add(strdup(targetPath.c_str()));

    for (int32 i = 0; i < (int32)stringViews.size(); i++)
    {
        Beefy::StringView stringView = stringViews[i];
        char* str = NULL;
        for (int32 pass = 0; pass < 2; pass++)
        {
            char* strPtr = str;

            int32 strPos = 0;
            for (int32 char8Idx = 0; char8Idx < stringView.mLength; char8Idx++)
            {
                char c = stringView.mPtr[char8Idx];
                if (c == '"')
                    inQuote = !inQuote;
                else
                {
                    if ((inQuote) && (c == '\\') && (char8Idx < stringView.mLength - 1))
                    {
                        char nextC = stringView.mPtr[char8Idx + 1];
                        if (nextC == '"')
                        {
                            c = nextC;
                            char8Idx++;
                        }
                    }
                    if (strPtr != NULL)
                        *(strPtr++) = c;
                    strPos++;
                }
            }
            if (pass == 0)
                str = (char*)malloc(strPos + 1);
            else
                *(strPtr++) = 0;
        }

        argvArr.Add(str);
    }
    argvArr.Add(NULL);

    char** argv = NULL;

    //pid_t pid = 0;
    //int status = posix_spawn(&pid, targetPath, NULL, NULL, &argvArr[0], environ);

    Beefy::Array<char*> envArr;
    if (env != NULL)
    {
        char* envPtr = (char*)env;
        while (true)
        {
            if (*envPtr == 0)
                break;

            envArr.Add(envPtr);
            envPtr += strlen(envPtr) + 1;
        }
    }
    envArr.Add(NULL);

    int stdInFD[2];
    int stdOutFD[2];
    int stdErrFD[2];

	bool failed = false;
	if ((flags & BfpSpawnFlag_RedirectStdInput) != 0)
		if (pipe(stdInFD) != 0)
			failed = true;
	if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
		if (pipe(stdOutFD) != 0)
			failed = true;
	if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
		if (pipe(stdErrFD) != 0)
			failed = true;
	if (failed)
	{
		//printf("Pipe failed\n");
		OUTRESULT(BfpSpawnResult_UnknownError);
		return NULL;
	}

    BfpSpawn* spawn;
    pid_t pid = fork();
    if (pid == -1) // Error
    {
        OUTRESULT(BfpSpawnResult_UnknownError);
        return NULL;
    }
    else if (pid == 0) // Child
    {
        if ((flags & BfpSpawnFlag_RedirectStdInput) != 0)
        {
            close(stdInFD[1]);            
            while ((dup2(stdInFD[0], STDIN_FILENO) == -1) && (errno == EINTR)) {}
            close(stdInFD[0]);            
        }

        if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
        {
            close(stdOutFD[0]);
            while ((dup2(stdOutFD[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
            close(stdOutFD[1]);
        }
        if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
        {
            close(stdErrFD[0]);
            while ((dup2(stdErrFD[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}
            close(stdErrFD[0]);
        }
        
        // If successful then this shouldn't return at all:
        int result;

        if (env != NULL)
            result = execve(targetPath.c_str(), (char* const*)&argvArr[0], (char* const*)&envArr[0]);
        else
            result = execv(targetPath.c_str(), (char* const*)&argvArr[0]);

        BFP_ERRPRINTF("Couldn't execute %s\n", targetPath.c_str());

        exit(-1);
    }
    else // Parent
    {
        spawn = new BfpSpawn();

        if ((flags & BfpSpawnFlag_RedirectStdInput) != 0)
        {
            spawn->mStdInFD = stdInFD[1];
            close(stdInFD[0]);
        }
        else
            spawn->mStdInFD = 0;

        if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
        {
            spawn->mStdOutFD = stdOutFD[0];
            close(stdOutFD[1]);
        }
        else
            spawn->mStdOutFD = 0;

        if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
        {
            spawn->mStdErrFD = stdErrFD[0];
            close(stdErrFD[1]);
        }
        else
            spawn->mStdErrFD = 0;
    }

    for (auto val : argvArr)
        free(val);

    //printf("Spawn pid:%d status:%d\n", pid, status);
    spawn->mPid = pid;
    spawn->mExited = false;
    spawn->mStatus = 0;

    return spawn;
}

void BfpSpawn_Release(BfpSpawn* spawn)
{
    // We don't support 'detaching' currently- this can create zombie processes since we
    //  don't have a reaper strategy
    BfpSpawn_WaitFor(spawn, -1, NULL, NULL);

    delete spawn;
}

BFP_EXPORT void BFP_CALLTYPE BfpSpawn_GetStdHandles(BfpSpawn* spawn, BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
    if (outStdIn != NULL)
    {
        *outStdIn = new BfpFile(spawn->mStdInFD);
        spawn->mStdInFD = 0;
    }

    if (outStdOut != NULL)
    {
        *outStdOut = new BfpFile(spawn->mStdOutFD);
        spawn->mStdOutFD = 0;
    }

    if (outStdErr != NULL)
    {
        *outStdErr = new BfpFile(spawn->mStdErrFD);
        spawn->mStdErrFD = 0;
    }
}

bool BfpSpawn_WaitFor(BfpSpawn* spawn, int waitMS, int* outExitCode, BfpSpawnResult* outResult)
{
    OUTRESULT(BfpSpawnResult_Ok);
    if (!spawn->mExited)
    {
        int flags = 0;
        if (waitMS != -1)
        {
            flags = WNOHANG;
        }
        //TODO: Implement values other than 0 or -1 for waitMS?

        pid_t result = waitpid(spawn->mPid, &spawn->mStatus, flags);
        if (result != spawn->mPid)
            return false;

        spawn->mExited = true;
    }

    if (!WIFEXITED(spawn->mStatus) && !WIFSIGNALED(spawn->mStatus))
        return false;

    if (outExitCode != NULL)
        *outExitCode = WEXITSTATUS(spawn->mStatus);
    return true;
}

// BfpFileWatcher

BFP_EXPORT BfpFileWatcher* BFP_CALLTYPE BfpFileWatcher_WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult)
{
    return FileWatchManager::Get()->WatchDirectory(path, callback, flags, userData, outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFileWatcher_Release(BfpFileWatcher* fileWatcher)
{
    FileWatchManager::Get()->Remove(fileWatcher);
}

// BfpThread

struct BfpThread
{
    bool mPThreadReleased;
    BfpThreadStartProc mStartProc;
    void* mThreadParam;
#ifndef BFP_HAS_PTHREAD_TIMEDJOIN_NP
    BfpEvent* mDoneEvent;
#endif    

    pthread_t mPThread;
    int mRefCount;
    int mPriority;   

    BfpThread()
    {
    }

    ~BfpThread()
    {     
    }

    void Release()
    {
        int refCount = __sync_fetch_and_sub(&mRefCount, 1) - 1;
        if (refCount == 0)
            delete this;
    }
};

struct BfpThreadInfo
{
    intptr mStackBase;
    int mStackLimit;
	pthread_t mPThread;
};

static __thread BfpThread* gCurrentThread;
static __thread BfpThreadInfo gCurrentThreadInfo;

void* ThreadFunc(void* threadParam)
{
    BfpThread* thread = (BfpThread*)threadParam;
    
    gCurrentThread = thread;
    thread->mStartProc(thread->mThreadParam);
#ifndef BFP_HAS_PTHREAD_TIMEDJOIN_NP
    BfpEvent_Set(thread->mDoneEvent, true);
#endif    
    
    thread->Release();
    return NULL;
}

BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_Create(BfpThreadStartProc startProc, void* threadParam, intptr stackSize, BfpThreadCreateFlags flags, BfpThreadId* outThreadId)
{
    BfpThread* thread = new BfpThread();
    thread->mPThreadReleased = false;
    thread->mStartProc = startProc;
    thread->mThreadParam = threadParam;
    thread->mRefCount = 2;
    thread->mPriority = 0;
#ifndef BFP_HAS_PTHREAD_TIMEDJOIN_NP
    thread->mDoneEvent = BfpEvent_Create(BfpEventFlag_None);
#endif    

    BF_ASSERT(sizeof(pthread_t) <= sizeof(void*));    
    pthread_attr_t params;
    pthread_attr_init(&params);
    pthread_attr_setstacksize(&params, stackSize);
    //pthread_attr_setdetachstate(&params,PTHREAD_CREATE_DETACHED);

    pthread_create(&thread->mPThread, &params, ThreadFunc, (void*)thread);

    pthread_attr_destroy(&params);
    
    if (outThreadId != NULL)
        *outThreadId = (BfpThreadId)thread->mPThread;

    return thread;
}

#define FIXTHREAD() \
    pthread_t pt; \
    if (((intptr)thread & 1) != 0) \
    { \
        pt = (pthread_t)((intptr)thread & ~3); \
        thread = NULL; \
    } else \
    pt = thread->mPThread

BFP_EXPORT void BFP_CALLTYPE BfpThread_Release(BfpThread* thread)
{
    FIXTHREAD();
    if (thread == NULL)
        return;

#ifndef BFP_HAS_PTHREAD_TIMEDJOIN_NP
    BfpEvent_Release(thread->mDoneEvent);
#endif
    if (!thread->mPThreadReleased)
    {
        pthread_detach(thread->mPThread);
        thread->mPThreadReleased = true;
    }
    
    thread->Release();
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_SetName(BfpThread* thread, const char* name, BfpThreadResult* outResult)
{
	OUTRESULT(BfpThreadResult_Ok);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_GetName(BfpThread* thread, char* outName, int* inOutNameSize, BfpThreadResult* outResult)
{
	String str = "";
	TryStringOut(str, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_GetCurrent()
{
    if (gCurrentThread == NULL)
    {
        // Not a "true" BfpThread, this is either the main thread or a thread we didn't create
        return (BfpThread*)((intptr)pthread_self() | 1);
    }
    return gCurrentThread;
}

BFP_EXPORT BfpThreadId BFP_CALLTYPE BfpThread_GetCurrentId()
{
    if (gCurrentThread == NULL)
    {
        return (BfpThreadId)((intptr)pthread_self());
    }
    return (BfpThreadId)gCurrentThread->mPThread;
}

BFP_EXPORT bool BFP_CALLTYPE BfpThread_WaitFor(BfpThread* thread, int waitMS)
{
    FIXTHREAD();

    if (waitMS == -1)
    {
        pthread_join(pt, NULL);
        thread->mPThreadReleased = true;
        return true;
    }

#ifdef BFP_HAS_PTHREAD_TIMEDJOIN_NP
    struct timespec waitTime;
    waitTime.tv_sec = waitMS / 1000;
    waitTime.tv_nsec = (waitMS % 1000) * 1000000;
    int result = pthread_timedjoin_np(pt, NULL, &waitTime);
    if (result == 0)
    {
        if (thread != NULL)
            thread->mPThreadReleased = true;
        return true;
    }
    return false;
#else
    if (thread == NULL)
        BF_FATAL("Invalid thread with non-infinite wait");
    return BfpEvent_WaitFor(thread->mDoneEvent, waitMS);    
#endif
}

BFP_EXPORT void BFP_CALLTYPE BfpSpawn_Kill(BfpSpawn* spawn, int exitCode, BfpKillFlags killFlags, BfpSpawnResult* outResult)
{
	//TODO: Implement
	OUTRESULT(BfpSpawnResult_UnknownError);
}

BFP_EXPORT BfpThreadPriority BFP_CALLTYPE BfpThread_GetPriority(BfpThread* thread, BfpThreadResult* outResult)
{
    FIXTHREAD();

    OUTRESULT(BfpThreadResult_Ok);
    if (thread == NULL)
        return (BfpThreadPriority)0;

    return (BfpThreadPriority)thread->mPriority;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_SetPriority(BfpThread* thread, BfpThreadPriority threadPriority, BfpThreadResult* outResult)
{
    // In effect, we have two 'nice' settings: 0 (normal) or 10 (low)
    //  High-priority settings just don't do anything
    //pid_t tid = syscall(SYS_gettid);
    //int ret = setpriority(PRIO_PROCESS, tid, -std::min(nPriority, 0) * 10);
    OUTRESULT(BfpThreadResult_Ok);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Suspend(BfpThread* thread, BfpThreadResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Resume(BfpThread* thread, BfpThreadResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_GetIntRegisters(BfpThread* thread, intptr* outStackPtr, intptr* outIntRegs, int* inOutIntRegCount, BfpThreadResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Sleep(int sleepMS)
{
    usleep(sleepMS * 1000);
}

BFP_EXPORT bool BFP_CALLTYPE BfpThread_Yield()
{
    return sched_yield() == 0;
}

///

BFP_EXPORT BfpThreadInfo* BFP_CALLTYPE BfpThreadInfo_Create()
{
	BfpThreadInfo* threadInfo = new BfpThreadInfo();
	threadInfo->mStackBase = 0;
	threadInfo->mStackLimit = 0;	
	threadInfo->mPThread = pthread_self();
    return threadInfo;
}

BFP_EXPORT void BFP_CALLTYPE BfpThreadInfo_Release(BfpThreadInfo* threadInfo)
{
	delete threadInfo;
}

BFP_EXPORT void BFP_CALLTYPE BfpThreadInfo_GetStackInfo(BfpThreadInfo* threadInfo, intptr* outStackBase, int* outStackLimit, BfpThreadInfoFlags flags, BfpThreadResult* outResult)
{
#ifdef BFP_HAS_PTHREAD_GETATTR_NP
	if (threadInfo == NULL)
	{
		threadInfo = &gCurrentThreadInfo;
		threadInfo->mPThread = pthread_self();
	}

	if (threadInfo->mStackBase == 0)
	{
		void* stackBase = 0;
		size_t stackLimit = 0;

		pthread_attr_t attr;
		pthread_getattr_np(threadInfo->mPThread, &attr);
		pthread_attr_getstack(&attr, &stackBase, &stackLimit);

		threadInfo->mStackBase = (intptr)stackBase + stackLimit;
		threadInfo->mStackLimit = (int)stackLimit;
		pthread_attr_destroy(&attr);
	}

	*outStackBase = threadInfo->mStackBase;
	*outStackLimit = threadInfo->mStackLimit;

	OUTRESULT(BfpThreadResult_Ok);
#else
	OUTRESULT(BfpThreadResult_UnknownError);
#endif
}

///

struct BfpCritSect
{
    pthread_mutex_t mPMutex;
};

BFP_EXPORT BfpCritSect* BFP_CALLTYPE BfpCritSect_Create()
{
    BfpCritSect* critSect = new BfpCritSect();

    pthread_mutexattr_t     attributes;        
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&critSect->mPMutex, &attributes);
    pthread_mutexattr_destroy(&attributes);

    return critSect;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Release(BfpCritSect* critSect)
{
    pthread_mutex_destroy(&critSect->mPMutex);
    delete critSect;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Enter(BfpCritSect* critSect)
{
    pthread_mutex_lock(&critSect->mPMutex);
}

BFP_EXPORT bool BFP_CALLTYPE BfpCritSect_TryEnter(BfpCritSect* critSect, int waitMS)
{
    if (waitMS == -1)
    {
        BfpCritSect_Enter(critSect);
        return true;
    }
    else if (waitMS == 0)
    {
        return pthread_mutex_trylock(&critSect->mPMutex) == 0;
    }
    
    uint32 start = Beefy::BFTickCount();
    while ((int)(Beefy::BFTickCount() - start) < waitMS)
    {
        if (pthread_mutex_trylock(&critSect->mPMutex) == 0)
        {                
            return true;
        }
    }
    return false;
}

BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Leave(BfpCritSect* critSect)
{
    pthread_mutex_unlock(&critSect->mPMutex);
}

BFP_EXPORT BfpTLS* BFP_CALLTYPE BfpTLS_Create(BfpTLSProc exitProc)
{
    pthread_key_t key = 0;
    pthread_key_create(&key, exitProc);
    return (BfpTLS*)(intptr)key;
}

BFP_EXPORT void BFP_CALLTYPE BfpTLS_Release(BfpTLS* tls)
{
    pthread_key_delete((pthread_key_t)(intptr)tls);
}

BFP_EXPORT void BFP_CALLTYPE BfpTLS_SetValue(BfpTLS* tls, void* value)
{
    pthread_setspecific((pthread_key_t)(intptr)tls, value);
}

BFP_EXPORT void* BFP_CALLTYPE BfpTLS_GetValue(BfpTLS* tls)
{
    return pthread_getspecific((pthread_key_t)(intptr)tls);
}

struct BfpEvent
{
    pthread_mutex_t mMutex;
    pthread_cond_t mCondVariable;
    bool mSet;
    bool mManualReset;
};

BFP_EXPORT BfpEvent* BFP_CALLTYPE BfpEvent_Create(BfpEventFlags flags)
{
    BfpEvent* event = new BfpEvent();
    pthread_mutex_init(&event->mMutex, NULL);
    pthread_cond_init(&event->mCondVariable, NULL);
    event->mSet = (flags & (BfpEventFlag_InitiallySet_Auto | BfpEventFlag_InitiallySet_Manual)) != 0;
    event->mManualReset = (flags & BfpEventFlag_InitiallySet_Manual) != 0;
    return event;
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Release(BfpEvent* event)
{
    pthread_cond_destroy(&event->mCondVariable);
    pthread_mutex_destroy(&event->mMutex);
    delete event;
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Set(BfpEvent* event, bool requireManualReset)
{
    pthread_mutex_lock(&event->mMutex);
    event->mSet = true;
    if (requireManualReset)
        event->mManualReset = true;
    if (event->mManualReset)
        pthread_cond_broadcast(&event->mCondVariable);
    else
        pthread_cond_signal(&event->mCondVariable);
    pthread_mutex_unlock(&event->mMutex);
}

BFP_EXPORT void BFP_CALLTYPE BfpEvent_Reset(BfpEvent* event, BfpEventResult* outResult)
{
    event->mSet = false;
    event->mManualReset = false;
}

BFP_EXPORT bool BFP_CALLTYPE BfpEvent_WaitFor(BfpEvent* event, int waitMS)
{
    int result = pthread_mutex_lock(&event->mMutex);
    BF_ASSERT(result == 0);
    while (!event->mSet)
    {
        if (waitMS == -1)
        {
            pthread_cond_wait(&event->mCondVariable, &event->mMutex);
        }
        else
        {
            timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += waitMS / 1000;
            ts.tv_nsec += (waitMS % 1000) * 1000000;
            
            result = pthread_cond_timedwait(&event->mCondVariable, &event->mMutex, &ts);
            
            if (waitMS == (uint32)-1)
                BF_ASSERT(result == 0);
            
            if (result != 0)
            {
                // Timeout                
                pthread_mutex_unlock(&event->mMutex);
                return false;
            }
        }
    }
    if (!event->mManualReset)
        event->mSet = false;        
    pthread_mutex_unlock(&event->mMutex);
    return true;
}

BFP_EXPORT BfpDynLib* BFP_CALLTYPE BfpDynLib_Load(const char* fileName)
{
    BfpDynLib* mod = NULL;
    
    static const char* prefixes[] = {NULL, "lib"};
    static const char* suffixes[] = {NULL, ".so", ".dylib"};
    
    for (int prefixIdx = 0; prefixIdx < sizeof(prefixes)/sizeof(prefixes[0]); prefixIdx++)
    {
        for (int suffixIdx = 0; suffixIdx < sizeof(suffixes)/sizeof(suffixes[0]); suffixIdx++)
        {
            const char* prefix = prefixes[prefixIdx];
            const char* suffix = suffixes[suffixIdx];
            
            Beefy::String checkName = fileName;
            if (prefix != NULL)
                checkName = Beefy::String(prefix) + checkName;
            if (suffix != NULL)
            {
                int dotPos = checkName.LastIndexOf('.');
                if (dotPos != -1)
                    checkName.RemoveToEnd(dotPos);
                checkName += suffix;
            }
            
            mod = (BfpDynLib*)dlopen(checkName.c_str(), RTLD_LAZY);
            if (mod != NULL)
                return mod;
        }
    }
    
     /*mod = (BfpDynLib*)dlopen("/var/Beef/qt-build/Debug/bin/libIDEHelper.so", RTLD_LAZY);;
     if (mod == NULL)
     {
         printf("Err: %s\n", dlerror());
         fflush(stdout);
     }*/

    return NULL;
}

BFP_EXPORT void BFP_CALLTYPE BfpDynLib_Release(BfpDynLib* lib)
{
    dlclose((void*)lib);
}

BFP_EXPORT void BFP_CALLTYPE BfpDynLib_GetFilePath(BfpDynLib* lib, char* outPath, int* inOutPathSize, BfpLibResult* outResult)
{
    Beefy::String path;

#ifdef BFP_HAS_DLINFO
    link_map* linkMap = NULL;
    dlinfo((void*)lib, RTLD_DI_LINKMAP, &linkMap);
    if (linkMap == NULL)
    {
        OUTRESULT(BfpLibResult_UnknownError);
        return;
    }

    path = linkMap->l_name;    
#else
    Dl_info info;    
    if (dladdr((void*)lib, &info) == 0)
    {
        OUTRESULT(BfpLibResult_UnknownError);
        return;
    }

    path = info.dli_fname;    
#endif

    TryStringOut(path, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void* BFP_CALLTYPE BfpDynLib_GetProcAddress(BfpDynLib* lib, const char* name)
{
    return dlsym((void*)lib, name);
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Create(const char* path, BfpFileResult* outResult)
{
    if (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
    {
        switch (errno)
        {
        case EEXIST:
            OUTRESULT(BfpFileResult_AlreadyExists);
            break;
        case ENOENT:
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

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Rename(const char* oldName, const char* newName, BfpFileResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Delete(const char* path, BfpFileResult* outResult)
{
    if (rmdir(path) != 0)
    {
        switch (errno)
        {
        case ENOENT:
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

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetCurrent(char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
    char* str = getcwd(NULL, 0);
    Beefy::String path = str;
    free(str);
    TryStringOut(path, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_SetCurrent(const char* path, BfpFileResult* outResult)
{    
    if (chdir(path) != 0)
        OUTRESULT(BfpFileResult_NotFound);  
    else    
        OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT bool BFP_CALLTYPE BfpDirectory_Exists(const char* path)
{
    struct stat statbuf = {0};
    int result = stat(path, &statbuf);
    if (result != 0)
        return false;
    return S_ISDIR(statbuf.st_mode);   
}

BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetSysDirectory(BfpSysDirectoryKind sysDirKind, char* outPath, int* inOutPathLen, BfpFileResult* outResult)
{
	String path = "~";
	TryStringOut(path, outPath, inOutPathLen, (BfpResult*)outResult);
}

BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_Create(const char* inName, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags, BfpFileAttributes createdFileAttrs, BfpFileResult* outResult)
{    
	auto _DoCreate = [&](String& name)
	{
		int flags = 0;
		int mode = 0;
		int pipePairHandle = -1;

		if ((createFlags & (BfpFileCreateFlag_Read | BfpFileCreateFlag_Write)) == (BfpFileCreateFlag_Read | BfpFileCreateFlag_Write))
			flags |= O_RDWR;
		else if ((createFlags & BfpFileCreateFlag_Read) != 0)
			flags |= O_RDONLY;
		else if ((createFlags & BfpFileCreateFlag_Write) != 0)
			flags |= O_WRONLY;

		if ((createFlags & BfpFileCreateFlag_Append) != 0)
			flags |= O_APPEND;
		if ((createFlags & BfpFileCreateFlag_Truncate) != 0)
			flags |= O_TRUNC;
		if ((createFlags & (BfpFileCreateFlag_NonBlocking | BfpFileCreateFlag_AllowTimeouts)) != 0)
			flags |= O_NONBLOCK;

		if ((createFlags & BfpFileCreateFlag_Pipe) != 0)
		{
			name = "/tmp/" + name;
			if ((createKind == BfpFileCreateKind_CreateAlways) ||
				(createKind == BfpFileCreateKind_CreateIfNotExists))
			{
				for (int pass = 0; pass < 2; pass++)
				{
					int result = mknod(name.c_str(), S_IFIFO | 0666, 0);
					if (result == 0)
						break;

					int err = errno;
					if (err == EEXIST)
					{
						err = remove(name.c_str());
						if (err == 0)
							continue;
						OUTRESULT(BfpFileResult_AlreadyExists);
						return -1;
					}

					OUTRESULT(BfpFileResult_UnknownError);
					return -1;
				}
			}
		}
		else
		{
			if (createKind == BfpFileCreateKind_CreateAlways)
				flags |= O_CREAT;
			else if (createKind == BfpFileCreateKind_CreateIfNotExists)
				flags |= O_CREAT | O_EXCL;
		}

		mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

		int result = open(name.c_str(), flags, mode);
        //printf("BfpFile_Create %s %d %d %d\n", name.c_str(), result, flags, mode);

		if (result <= 0)
		{
			switch (errno)
			{
			case EEXIST:
				OUTRESULT(BfpFileResult_AlreadyExists);
				break;
			case ENOENT:
				OUTRESULT(BfpFileResult_NotFound);
				break;
			case EACCES:
				OUTRESULT(BfpFileResult_AccessError);
				break;
			default:
				OUTRESULT(BfpFileResult_UnknownError);
				break;
			}
			return -1;
		}
        return result;
	};
    
    // POSIX doesn't need the OpenAlways kind.
    if (createKind == BfpFileCreateKind_OpenAlways)
        createKind = BfpFileCreateKind_CreateAlways;

	BfpFile* bfpFile = NULL;

	int result;	
	if ((createFlags & BfpFileCreateFlag_Pipe) != 0)
	{
		int readHandle;
		int writeHandle;

		String name = inName;
		String altName = name + "__";

		bool isCreating = false;
		if ((createKind == BfpFileCreateKind_CreateAlways) ||
			(createKind == BfpFileCreateKind_CreateIfNotExists))
		{
			readHandle = _DoCreate(name);
			writeHandle = _DoCreate(altName);
			isCreating = true;
		}
		else
		{
			readHandle = _DoCreate(altName);
			writeHandle = _DoCreate(name);
		}

		if ((readHandle != -1) && (writeHandle != -1))
		{
			OUTRESULT(BfpFileResult_Ok);

			BfpPipeInfo* pipeInfo = new BfpPipeInfo();
			pipeInfo->mWriteHandle = writeHandle;		
			if (isCreating)
				pipeInfo->mPipePath = name;
			bfpFile = new BfpFile();
			bfpFile->mHandle = readHandle;			
			bfpFile->mPipeInfo = pipeInfo;
		}
		else
		{
			if (readHandle != -1)
				close(readHandle);
			if (writeHandle != -1)
				close(writeHandle);

			return NULL;
		}
	}
	else
	{
		String name = inName;
		int handle = _DoCreate(name);
		if (handle == -1)
			return NULL;

		OUTRESULT(BfpFileResult_Ok);
		bfpFile = new BfpFile();
		bfpFile->mHandle = handle;
	}

	OUTRESULT(BfpFileResult_Ok);	
	if ((createFlags & (BfpFileCreateFlag_NonBlocking | BfpFileCreateFlag_AllowTimeouts)) != 0)
        bfpFile->mNonBlocking = true;
	if ((createFlags & BfpFileCreateFlag_AllowTimeouts) != 0)       
        bfpFile->mAllowTimeout = true;	
    return bfpFile;
}

BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_GetStd(BfpFileStdKind kind, BfpFileResult* outResult)
{
	int h = -1;
	switch (kind)
	{
	case BfpFileStdKind_StdOut:
		h = STDOUT_FILENO;
		break;
	case BfpFileStdKind_StdError:
		h = STDERR_FILENO;
		break;
	case BfpFileStdKind_StdIn:
		h = STDIN_FILENO;
		break;
	}
	if (h == -1)
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
	if ((file->mHandle != -1) && (!file->mIsStd))
		close(file->mHandle);
	if (file->mPipeInfo != NULL)
	{
		if (file->mPipeInfo->mWriteHandle != -1)
			close(file->mPipeInfo->mWriteHandle);
		
		if (!file->mPipeInfo->mPipePath.IsEmpty())
		{
			int worked = remove(file->mPipeInfo->mPipePath.c_str());
			remove((file->mPipeInfo->mPipePath + "__").c_str());
			//printf("Removing %s %d\n", file->mPipeInfo->mPipePath.c_str(), worked);
		}
	}	

	delete file;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Close(BfpFile* file, BfpFileResult* outResult)
{
	if (file->mHandle != -1)
	{
		close(file->mHandle);
		file->mHandle = -1;
		if (file->mPipeInfo != NULL)
		{
			close(file->mPipeInfo->mWriteHandle);
			file->mPipeInfo->mWriteHandle = -1;
		}

		OUTRESULT(BfpFileResult_Ok);
	}
	else
		OUTRESULT(BfpFileResult_UnknownError);
}

BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Write(BfpFile* file, const void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult)
{   
	int writeHandle = file->mHandle;
	if (file->mPipeInfo != NULL)
		writeHandle = file->mPipeInfo->mWriteHandle;

    intptr writeCount = ::write(writeHandle, buffer, size);
// 	if ((writeCount > 0) && (file->mIsPipe))
// 	{
// 		::fsync(file->mHandle);
// 	}

    if (writeCount < 0)
        OUTRESULT(BfpFileResult_UnknownError);
    else if (writeCount != size)
        OUTRESULT(BfpFileResult_PartialData);
    else
        OUTRESULT(BfpFileResult_Ok);
    return writeCount;
}

BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Read(BfpFile* file, void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult)
{
	if (file->mNonBlocking)
	{
		if (!file->mAllowTimeout)		
			timeoutMS = -1;		
		
		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = timeoutMS * 1000;

		fd_set readFDSet;
		FD_ZERO(&readFDSet);
		FD_SET(file->mHandle, &readFDSet);

		fd_set errorFDSet;
		FD_ZERO(&errorFDSet);
		FD_SET(file->mHandle, &errorFDSet);

		if (select(file->mHandle + 1, &readFDSet, NULL, &errorFDSet, (timeoutMS == -1) ? NULL : &timeout) < 0)
		{
			OUTRESULT(BfpFileResult_Timeout);
			return 0;
		}
	}

    intptr readCount = ::read(file->mHandle, buffer, size);
    if (readCount < 0)
        OUTRESULT(BfpFileResult_UnknownError);
    else if (readCount != size)
        OUTRESULT(BfpFileResult_PartialData);
    else
        OUTRESULT(BfpFileResult_Ok);
    return readCount;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Flush(BfpFile* file)
{
    ::fsync(file->mHandle);
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFile_GetFileSize(BfpFile* file)
{
    int64 oldPos = (int64)lseek64(file->mHandle, 0, SEEK_CUR);
    int64 size = (int64)lseek64(file->mHandle, 0, SEEK_END);
    lseek64(file->mHandle, oldPos, SEEK_SET);
    return (int64)size;
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFile_Seek(BfpFile* file, int64 offset, BfpFileSeekKind seekKind)
{
    int whence;
    if (seekKind == BfpFileSeekKind_Absolute)        
        whence = SEEK_SET;
    else if (seekKind == BfpFileSeekKind_Relative)
        whence = SEEK_CUR;
    else
        whence = SEEK_END;
    return lseek64(file->mHandle, offset, whence);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Truncate(BfpFile* file, BfpFileResult* outResult)
{
    int64 curPos = (int64)lseek64(file->mHandle, 0, SEEK_CUR);
	if (ftruncate64(file->mHandle, curPos) != 0)
    {
        OUTRESULT(BfpFileResult_UnknownError);
        return;
    }
    OUTRESULT(BfpFileResult_Ok);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFile_GetTime_LastWrite(const char* path)
{
    struct stat statbuf = {0};
    int result = stat(path, &statbuf);
    if (result != 0)
        return 0;    
    return statbuf.st_mtime;
}

BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFile_GetAttributes(const char* path, BfpFileResult* outResult)
{
    NOT_IMPL;
    return (BfpFileAttributes)0;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_SetAttributes(const char* path, BfpFileAttributes attribs, BfpFileResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Copy(const char* oldPath, const char* newPath, BfpFileCopyKind copyKind, BfpFileResult* outResult)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;  

    fd_from = open(oldPath, O_RDONLY);
    if (fd_from < 0)
    {
        OUTRESULT(BfpFileResult_NotFound);
        return;
    }

    int flags = O_WRONLY | O_CREAT;
    if (copyKind == BfpFileCopyKind_IfNotExists)
        flags |= O_EXCL;

    fd_to = open(newPath, flags, 0666);
    if (fd_to < 0)
    {
        if (errno == EEXIST)
        {
            OUTRESULT(BfpFileResult_AlreadyExists);
            goto out_error;
        }

        OUTRESULT(BfpFileResult_UnknownError);
        goto out_error;
    }

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                OUTRESULT(BfpFileResult_UnknownError);
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            OUTRESULT(BfpFileResult_UnknownError);
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        OUTRESULT(BfpFileResult_Ok);
        return;
    }

out_error:
    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);   
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Rename(const char* oldPath, const char* newPath, BfpFileResult* outResult)
{
    NOT_IMPL;
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Delete(const char* path, BfpFileResult* outResult)
{
    if (remove(path) != 0)
    {
        switch (errno)
        {
        case ENOENT:
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
    struct stat statbuf = {0};
    int result = stat(path, &statbuf);
    if (result != 0)
        return false;
    return !S_ISDIR(statbuf.st_mode);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempPath(char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
    NOT_IMPL;
}

static const char cHash64bToChar[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_' };
static void HashEncode64(StringImpl& outStr, uint64 val)
{
    for (int i = 0; i < 10; i++)
    {
        int charIdx = (int)((val >> (i * 6)) & 0x3F) - 1;
        if (charIdx != -1)
            outStr.Append(cHash64bToChar[charIdx]);
    }
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempFileName(char* outName, int* inOutNameSize, BfpFileResult* outResult)
{
    static uint32 uniqueIdx = 0;
    BfpSystem_InterlockedExchangeAdd32(&uniqueIdx, 1);

    Beefy::HashContext ctx;
    ctx.Mixin(uniqueIdx);
    ctx.Mixin(getpid());
    ctx.Mixin(Beefy::BFGetTickCountMicro());

    uint64 hash = ctx.Finish64();

    String str = "/tmp/bftmp_";
    HashEncode64(str, hash);

    TryStringOut(str, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetFullPath(const char* inPath, char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
    String str;

    if (inPath[0] == '/')
    {
        str = inPath;
    }
    else
    {
        char* cwdPtr = getcwd(NULL, 0);
    Beefy::String cwdPath = cwdPtr;
        free(cwdPtr);
        str = GetAbsPath(inPath, cwdPath);
    }
    TryStringOut(str, outPath, inOutPathSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_GetActualPath(const char* inPath, char* outPath, int* inOutPathSize, BfpFileResult* outResult)
{
    NOT_IMPL;
}

// BfpFindFileData

struct BfpFindFileData
{
    BfpFindFileFlags mFlags;    
    DIR* mDirStruct;
    Beefy::String mWildcard;
    Beefy::String mDirPath;

    dirent* mDirEnt;
    bool mHasStat;
    struct stat mStat;
};

BFP_EXPORT BfpFindFileData* BFP_CALLTYPE BfpFindFileData_FindFirstFile(const char* path, BfpFindFileFlags flags, BfpFileResult* outResult)
{
    Beefy::String findStr = path;
    Beefy::String wildcard;
    
    int lastSlashPos = std::max((int)findStr.LastIndexOf('/'), (int)findStr.LastIndexOf('\\'));
    if (lastSlashPos != -1)
    {
        wildcard = findStr.Substring(lastSlashPos + 1);
        findStr = findStr.Substring(0, lastSlashPos);
    }
    if (wildcard == "*.*")
        wildcard = "*";
    
    DIR* dir = opendir(findStr.c_str());
    if (dir == NULL)
    {
        OUTRESULT(BfpFileResult_NotFound);
        return NULL;
    }

    BfpFindFileData* findData = new BfpFindFileData();
    findData->mFlags = flags;
    findData->mDirPath = findStr;
    findData->mDirStruct = dir;    
    findData->mWildcard = wildcard;
    findData->mHasStat = false;
    findData->mDirEnt = NULL;
        
    if (!BfpFindFileData_FindNextFile(findData))
    {            
        OUTRESULT(BfpFileResult_NoResults);
        closedir(findData->mDirStruct);
        delete findData;
        return NULL;
    }    

    OUTRESULT(BfpFileResult_Ok);
    return findData;
}

static void GetStat(BfpFindFileData* findData)
{
    if (findData->mHasStat)
        return;

    Beefy::String filePath = findData->mDirPath + "/" + findData->mDirEnt->d_name;
    
    findData->mStat = { 0 };
    int result = stat(filePath.c_str(), &findData->mStat);

    findData->mHasStat = true;
}

static bool BfpFindFileData_CheckFilter(BfpFindFileData* findData)
{
    bool isDir = false;
    if (findData->mDirEnt->d_type == DT_DIR)
        isDir = true;
    if (findData->mDirEnt->d_type == DT_LNK)
    {
        GetStat(findData);
        isDir = S_ISDIR(findData->mStat.st_mode);
    }

    if (isDir)
    {
        if ((findData->mFlags & BfpFindFileFlag_Directories) == 0)          
            return false;
        
        if ((strcmp(findData->mDirEnt->d_name, ".") == 0) || (strcmp(findData->mDirEnt->d_name, "..") == 0))        
            return false;        
    }
    else
    {
        if ((findData->mFlags & BfpFindFileFlag_Files) == 0)
            return false;
    }

    if (!wc_match(findData->mWildcard.c_str(), findData->mDirEnt->d_name))
        return false;

    return true;
}

BFP_EXPORT bool BFP_CALLTYPE BfpFindFileData_FindNextFile(BfpFindFileData* findData)
{    
    while (true)
    {
        findData->mHasStat = false;        
        findData->mDirEnt = readdir(findData->mDirStruct);
        if (findData->mDirEnt == NULL)
            return false;

        if (BfpFindFileData_CheckFilter(findData))
            break;
    }

    return true;
}

BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_GetFileName(BfpFindFileData* findData, char* outName, int* inOutNameSize, BfpFileResult* outResult)
{
    Beefy::String name = findData->mDirEnt->d_name;
    TryStringOut(name, outName, inOutNameSize, (BfpResult*)outResult);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_LastWrite(BfpFindFileData* findData)
{
    GetStat(findData);    
#ifdef BF_PLATFORM_DARWIN    
    return BfpToTimeStamp(findData->mStat.st_mtimespec);
#else
    return BfpToTimeStamp(findData->mStat.st_mtim);
#endif
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Created(BfpFindFileData* findData)
{
    GetStat(findData);
#ifdef BF_PLATFORM_DARWIN        
    return BfpToTimeStamp(findData->mStat.st_ctimespec);
#else
    return BfpToTimeStamp(findData->mStat.st_ctim);
#endif
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Access(BfpFindFileData* findData)
{
    GetStat(findData);
#ifdef BF_PLATFORM_DARWIN
    return BfpToTimeStamp(findData->mStat.st_atimespec);
#else
    return BfpToTimeStamp(findData->mStat.st_atim);
#endif
}

BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFindFileData_GetFileAttributes(BfpFindFileData* findData)
{
    BfpFileAttributes flags = BfpFileAttribute_None;
    if (S_ISDIR(findData->mStat.st_mode))
        flags = (BfpFileAttributes)(flags | BfpFileAttribute_Directory);
    if (S_ISREG(findData->mStat.st_mode))
        flags = (BfpFileAttributes)(flags | BfpFileAttribute_Normal);
    else if (!S_ISLNK(findData->mStat.st_mode))
        flags = (BfpFileAttributes)(flags | BfpFileAttribute_Device);
    if ((findData->mStat.st_mode & S_IRUSR) == 0)
        flags = (BfpFileAttributes)(flags | BfpFileAttribute_ReadOnly);
    return flags;
}

BFP_EXPORT int64 BFP_CALLTYPE BfpFindFileData_GetFileSize(BfpFindFileData* findData)
{
    return (int64)findData->mStat.st_size;
}

BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_Release(BfpFindFileData* findData)
{
    closedir(findData->mDirStruct);
    delete findData;
}

BFP_EXPORT int BFP_CALLTYPE BfpStack_CaptureBackTrace(int framesToSkip, intptr* outFrames, int wantFrameCount)
{
    //
    return 0;
}

BFP_EXPORT void BFP_CALLTYPE BfpOutput_DebugString(const char* str)
{
    BFP_PRINTF("%s", str);
    fflush(stdout);
}

//////////////////////////////////////////////////////////////////////////

void Beefy::BFFatalError(const StringImpl& message, const StringImpl& file, int line)
{
	String error;
    error += "ERROR: ";
	error += message;
	error += " in ";
	error += file;
	error += StrFormat(" line %d", line);
	BfpSystem_FatalError(error.c_str(), "FATAL ERROR");
}
