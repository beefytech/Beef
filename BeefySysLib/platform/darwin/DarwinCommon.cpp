#include "Common.h"
#include "BFPlatform.h"
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <wchar.h>
#include <fcntl.h>
#include <time.h>
//#include <link.h>
#include <dlfcn.h>
#include <dirent.h>
#include <syslog.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include <spawn.h>
#include "../PlatformInterface.h"
#include "../PlatformHelper.h"
#include "../util/CritSect.h"
#include "../util/Dictionary.h"
#include "../util/Hash.h"
#include <execinfo.h>
//#include "backtrace.h"
//#include "backtrace-supported.h"
#include "../third_party/stb/stb_sprintf.h"
#include <cxxabi.h>
#include <random>

#include <mach-o/dyld.h>

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
    
    //int64 fileTime = 0;
    //BFSystemTimeToFileTime(statbuf.st_mtime, 0, &fileTime);
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

static CritSect gSysCritSect;
static String gCrashInfo;
static Array<CrashInfoFunc> gCrashInfoFuncs;

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

    dl_info dyldInfo;
    void* addr = (void*)_Unwind_GetIP(context);
    gUnwindExecStr += StrFormat(" %p", addr);
    return _URC_NO_REASON;
}

static bool FancyBacktrace()
{
    gUnwindExecStr += StrFormat("atos -p %d", getpid());    
    _Unwind_Backtrace(&UnwindHandler, NULL);
    return system(gUnwindExecStr.c_str()) == 0;
}

static void Crashed()
{
    //
    {
        AutoCrit autoCrit(gSysCritSect);

        String debugDump;

        debugDump += "**** FATAL APPLICATION ERROR ****\n";

        for (auto func : gCrashInfoFuncs)
            func();

        if (!gCrashInfo.IsEmpty())
        {
            debugDump += gCrashInfo;
            debugDump += "\n";
        }

        fprintf(stderr, "%s", debugDump.c_str());
    }

    if (!FancyBacktrace())
    {
        void* array[64];
        size_t size;
        char** strings;
        size_t i;

        size = backtrace(array, 64);
        strings = backtrace_symbols(array, size);

        for (i = 0; i < size; i++)
            fprintf(stderr, "%s\n", strings[i]);

        free(strings);
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
        gCrashInfo += StrFormat("Signal: %s\n", sigName);
    else
        gCrashInfo += StrFormat("Signal: %d\n", sig);
    Crashed();
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_Init(int version, BfpSystemInitFlags flags)
{
    if (version != BFP_VERSION)
    {
        BfpSystem_FatalError(StrFormat("Bfp build version '%d' does not match requested version '%d'", BFP_VERSION, version).c_str(), "BFP FATAL ERROR");
    }

    signal(SIGSEGV, SigHandler);
    signal(SIGFPE, SigHandler);
    signal(SIGABRT, SigHandler);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCommandLine(int argc, char** argv)
{ 
    char* relPath = argv[0];

    char* cwd = getcwd(NULL, 0);
    gExePath = GetAbsPath(relPath, cwd);
    
    free(cwd);

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
    AutoCrit autoCrit(gSysCritSect);
    gCrashInfoFuncs.Add(crashInfoFunc);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfo(const char* str) // Can do at any time, or during CrashInfoFunc callbacks
{
    AutoCrit autoCrit(gSysCritSect);
    gCrashInfo.Append(str);
}

void BfpSystem_Shutdown()
{

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
    fprintf(stderr, "%s\n", error);
    fflush(stderr);
    Crashed();
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetCommandLine(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
    TryStringOut(gCmdLine, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetExecutablePath(char* outStr, int* inOutStrSize, BfpSystemResult* outResult)
{
    //printf("Setting BfpSystem_GetExecutablePath %s %p\n", gExePath.c_str(), &gExePath);

    if (gExePath.IsEmpty())
    {
        char path[4096];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0)
            gExePath = path;

        // When when running with a './file', we end up with an annoying '/./' in our path
        gExePath.Replace("/./", "/");        
    }

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
        env.Append(envStr, strlen(envStr) + 1);
        ++envPtr;
    }

    TryStringOut(env, outStr, inOutStrSize, (BfpResult*)outResult);
}

BFP_EXPORT int BFP_CALLTYPE BfpSystem_GetNumLogicalCPUs(BfpSystemResult* outResult)
{   
    OUTRESULT(BfpSystemResult_Ok);
    int count = 1;
    size_t count_len = sizeof(count);
    sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);
    return count;
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
//  uuid_t guid;    
//  uuid_generate(guid);    
//  BfpGUID bfpGuid;
//  memcpy(&bfpGuid, guid, 16);
//  return bfpGuid;

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

    //printf("Executing: %s %s %x\n", inTargetPath, args, flags);

    if ((workingDir != NULL) && (workingDir[0] != 0))
    {
        if (chdir(workingDir) != 0)
        {
            //printf("CHDIR failed %s\n", workingDir);
            OUTRESULT(BfpSpawnResult_UnknownError);
            return NULL;
        }
    }

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
                stringViews.Add(Beefy::StringView(args, firstCharIdx, i - firstCharIdx));
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
        stringViews.Add(Beefy::StringView(args, firstCharIdx, i - firstCharIdx));

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
            while ((dup2(stdInFD[0], STDIN_FILENO) == -1) && (errno == EINTR)) {}
        if ((flags & BfpSpawnFlag_RedirectStdOutput) != 0)
            while ((dup2(stdOutFD[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        if ((flags & BfpSpawnFlag_RedirectStdError) != 0)
            while ((dup2(stdErrFD[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}

        // If successful then this shouldn't return at all:
        int result;

        if (env != NULL)
            result = execve(targetPath.c_str(), (char* const*)&argvArr[0], (char* const*)&envArr[0]);
        else
            result = execv(targetPath.c_str(), (char* const*)&argvArr[0]);

        printf("Couldn't execute %s\n", targetPath.c_str());

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
    NOT_IMPL;
    return NULL;
}

BFP_EXPORT void BFP_CALLTYPE BfpFileWatcher_Release(BfpFileWatcher* fileWatcher)
{
    NOT_IMPL;
}

// BfpThread

struct BfpThread
{
    bool mPThreadReleased;
    BfpThreadStartProc mStartProc;
    void* mThreadParam;
    BfpEvent* mDoneEvent;

    pthread_t mPThread;
    int mRefCount;
    int mPriority;   

    BfpThread()
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
};

static __thread BfpThread* gCurrentThread;
static __thread BfpThreadInfo gCurrentThreadInfo;

void* ThreadFunc(void* threadParam)
{
    BfpThread* thread = (BfpThread*)threadParam;
    gCurrentThread = thread;
    thread->mStartProc(thread->mThreadParam);
    BfpEvent_Set(thread->mDoneEvent, true);
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
    thread->mDoneEvent = BfpEvent_Create(BfpEventFlag_None);

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

    BfpEvent_Release(thread->mDoneEvent);
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

    if (thread == NULL)
        BF_FATAL("Invalid thread with non-infinite wait");
    return BfpEvent_WaitFor(thread->mDoneEvent, waitMS);    
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

BFP_EXPORT void BFP_CALLTYPE BfpThread_GetStackInfo(BfpThread* thread, intptr* outStackBase, int* outStackLimit, BfpThreadResult* outResult)
{
    // USE mach_vm_region_info ??
    // https://github.com/dmlloyd/openjdk/blob/2f680fec58a8ffb818a48f542e493306d1113c69/src/hotspot/os_cpu/bsd_x86/os_bsd_x86.cpp#L902-L961

    /*if (gCurrentThreadInfo.mStackBase == 0)
    {
        void* stackBase = 0;
        size_t stackLimit = 0;

        pthread_attr_t attr;
        pthread_getattr_np(pthread_self(), &attr);
        pthread_attr_getstack(&attr, &stackBase, &stackLimit);

        gCurrentThreadInfo.mStackBase = (intptr)stackBase + stackLimit;
        gCurrentThreadInfo.mStackLimit = (int)stackLimit;
        pthread_attr_destroy(&attr);
    }

    *outStackBase = gCurrentThreadInfo.mStackBase;
    *outStackLimit = gCurrentThreadInfo.mStackLimit;

    OUTRESULT(BfpThreadResult_Ok);*/
    OUTRESULT(BfpThreadResult_UnknownError);
}

BFP_EXPORT void BFP_CALLTYPE BfpThread_Sleep(int sleepMS)
{
    usleep(sleepMS * 1000);
}

BFP_EXPORT bool BFP_CALLTYPE BfpThread_Yield()
{
    return sched_yield() == 0;
}

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

BFP_EXPORT BfpTLS* BFP_CALLTYPE BfpTLS_Create()
{
    pthread_key_t key = 0;
    pthread_key_create(&key, NULL);
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

    
    Dl_info info;    
    if (dladdr((void*)lib, &info) == 0)
    {
        OUTRESULT(BfpLibResult_UnknownError);
        return;
    }

    path = info.dli_fname;
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
//  if ((writeCount > 0) && (file->mIsPipe))
//  {
//      ::fsync(file->mHandle);
//  }

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
    int64 oldPos = (int64)lseek(file->mHandle, 0, SEEK_CUR);
    int64 size = (int64)lseek(file->mHandle, 0, SEEK_END);
    lseek(file->mHandle, oldPos, SEEK_SET);
    return (intptr)size;
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
    return lseek(file->mHandle, offset, whence);
}

BFP_EXPORT void BFP_CALLTYPE BfpFile_Truncate(BfpFile* file)
{
    int64 curPos = (int64)lseek(file->mHandle, 0, SEEK_CUR);
    if (ftruncate(file->mHandle, curPos) != 0)
    {
        //TODO: Report error?
    }
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

    //TODO: Check actual wildcards.

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
    return BfpToTimeStamp(findData->mStat.st_mtimespec);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Created(BfpFindFileData* findData)
{
    GetStat(findData);
    return BfpToTimeStamp(findData->mStat.st_ctimespec);
}

BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Access(BfpFindFileData* findData)
{
    GetStat(findData);
    return BfpToTimeStamp(findData->mStat.st_atimespec);
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

BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_Release(BfpFindFileData* findData)
{
    delete findData;
}

BFP_EXPORT int BFP_CALLTYPE BfpStack_CaptureBackTrace(int framesToSkip, intptr* outFrames, int wantFrameCount)
{
    //
    return 0;
}

BFP_EXPORT void BFP_CALLTYPE BfpOutput_DebugString(const char* str)
{
    fputs(str, stdout);
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

#if 0

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

#endif