#pragma once

#include "../Common.h"

#define BFP_VERSION 2

#ifndef BFP_EXPORT
#define BFP_EXPORT BF_EXPORT
#endif

#ifndef BFP_CALLTYPE
#define BFP_CALLTYPE BF_CALLTYPE
#endif

// Windows file time (the number of 100-nanosecond intervals that have elapsed since 12:00 A.M. January 1, 1601 Coordinated Universal Time (UTC))
typedef uint64 BfpTimeStamp;

typedef intptr BfpThreadId;
struct BfpThread;
struct BfpFile;
struct BfpSpawn;
struct BfpFileWatcher;
struct BfpProcess;

struct BfpGUID
{
	uint32 mData1;
	uint16 mData2;
	uint16 mData3;
	uint8 mData4[8];
};

enum BfpResult
{
	BfpResult_Ok,
	BfpResult_UnknownError,
	BfpResult_InsufficientBuffer,
	BfpResult_NotSupported,
	BfpResult_NoResults,	
	BfpResult_InvalidParameter,
	BfpResult_Locked,
	BfpResult_AlreadyExists,
	BfpResult_NotFound,
	BfpResult_ShareError,
	BfpResult_AccessError,	
	BfpResult_PartialData,	
	BfpResult_TempFileError,	
	BfpResult_Timeout,
	BfpResult_NotEmpty
};

enum BfpSystemResult
{
	BfpSystemResult_Ok = BfpResult_Ok,
	BfpSystemResult_PartialData = BfpResult_PartialData
};

enum BfpFileResult
{
	BfpFileResult_Ok = BfpResult_Ok,
	BfpFileResult_NoResults = BfpResult_NoResults,
	BfpFileResult_UnknownError = BfpResult_UnknownError,
	BfpFileResult_InvalidParameter = BfpResult_InvalidParameter,
	BfpFileResult_Locked = BfpResult_Locked,
	BfpFileResult_AlreadyExists = BfpResult_AlreadyExists,	
	BfpFileResult_NotFound = BfpResult_NotFound,
	BfpFileResult_ShareError = BfpResult_ShareError,
	BfpFileResult_AccessError = BfpResult_AccessError,	
	BfpFileResult_PartialData = BfpResult_PartialData,
	BfpFileResult_InsufficientBuffer = BfpResult_InsufficientBuffer,	
	BfpFileResult_Timeout = BfpResult_Timeout,
	BfpFileResult_NotEmpty = BfpResult_NotEmpty
};

typedef void(*BfpCrashInfoFunc)();

enum BfpSystemInitFlags
{
	BfpSystemInitFlag_None = 0,
	BfpSystemInitFlag_InstallCrashCatcher = 1,
	BfpSystemInitFlag_SilentCrash = 2,
};

enum BfpCrashReportKind
{
	BfpCrashReportKind_Default,
	BfpCrashReportKind_GUI,
	BfpCrashReportKind_Console,
	BfpCrashReportKind_PrintOnly,
	BfpCrashReportKind_None
};

BFP_EXPORT void BFP_CALLTYPE BfpSystem_Init(int version, BfpSystemInitFlags flags);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCommandLine(int argc, char** argv);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_SetCrashReportKind(BfpCrashReportKind crashReportKind);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfoFunc(BfpCrashInfoFunc crashInfoFunc);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_AddCrashInfo(const char* str); // Can do at any time, or during CrashInfoFunc callbacks
BFP_EXPORT void BFP_CALLTYPE BfpSystem_Shutdown();
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_TickCount();
BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpSystem_GetTimeStamp();
BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_EndianSwap16(uint16 val);
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_EndianSwap32(uint32 val);
BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_EndianSwap64(uint64 val);
BFP_EXPORT uint8  BFP_CALLTYPE BfpSystem_InterlockedExchange8(uint8* ptr, uint8 val);
BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedExchange16(uint16* ptr, uint16 val);
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchange32(uint32* ptr, uint32 val); 
BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchange64(uint64* ptr, uint64 val);
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd8(uint8* ptr, uint8 val);
BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd16(uint16* ptr, uint16 val);
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd32(uint32* ptr, uint32 val); // Returns the initial value in 'ptr'
BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedExchangeAdd64(uint64* ptr, uint64 val);
BFP_EXPORT uint8  BFP_CALLTYPE BfpSystem_InterlockedCompareExchange8(uint8* ptr, uint8 oldVal, uint8 newVal);
BFP_EXPORT uint16 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange16(uint16* ptr, uint16 oldVal, uint16 newVal);
BFP_EXPORT uint32 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange32(uint32* ptr, uint32 oldVal, uint32 newVal);
BFP_EXPORT uint64 BFP_CALLTYPE BfpSystem_InterlockedCompareExchange64(uint64* ptr, uint64 oldVal, uint64 newVal);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_FatalError(const char* error, const char* title);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetCommandLine(char* outStr, int* inOutStrSize, BfpSystemResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetExecutablePath(char* outStr, int* inOutStrSize, BfpSystemResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetEnvironmentStrings(char* outStr, int* inOutStrSize, BfpSystemResult* outResult);
BFP_EXPORT int BFP_CALLTYPE BfpSystem_GetNumLogicalCPUs(BfpSystemResult* outResult);
BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTick();
BFP_EXPORT int64 BFP_CALLTYPE BfpSystem_GetCPUTickFreq();
BFP_EXPORT void BFP_CALLTYPE BfpSystem_CreateGUID(BfpGUID* outGuid);
BFP_EXPORT void BFP_CALLTYPE BfpSystem_GetComputerName(char* outStr, int* inOutStrSize, BfpSystemResult* outResult);

#ifdef BFP_INTPTR_UNIQUE

#ifdef BF32
#define BfpSystem_InterlockedExchangePtr(ptr, val) BfpSystem_InterlockedExchange32((uint32*)(ptr), (uint32)(val))
#define BfpSystem_InterlockedExchangeAddPtr(ptr, val) BfpSystem_InterlockedExchangeAdd32((uint32*)(ptr), (uint32)(val))
#define BfpSystem_InterlockedCompareExchangePtr(ptr, oldVal, newVal) BfpSystem_InterlockedCompareExchange32((uint32*)(ptr), (uint32)(oldVal), (uint32)(newVal))
#define BfpSystem_EndianSwapPtr(val) BfpSystem_EndianSwap32((uint32)(val))
#else
#define BfpSystem_InterlockedExchangePtr(ptr, val) BfpSystem_InterlockedExchange64((uint64*)(ptr), (uint64)(val))
#define BfpSystem_InterlockedExchangeAddPtr(ptr, val) BfpSystem_InterlockedExchangeAdd64((uint64*)(ptr), (uint64)(val))
#define BfpSystem_InterlockedCompareExchangePtr(ptr, oldVal, newVal) BfpSystem_InterlockedCompareExchange64((uint64*)(ptr), (uint64)(oldVal), (uint64)(newVal))
#define BfpSystem_EndianSwapPtr(val) BfpSystem_EndianSwap64((uint64)(val))
#endif

#else

#ifdef BF32
#define BfpSystem_InterlockedExchangePtr BfpSystem_InterlockedExchange32
#define BfpSystem_InterlockedExchangeAddPtr BfpSystem_InterlockedExchangeAdd32
#define BfpSystem_InterlockedCompareExchangePtr BfpSystem_InterlockedCompareExchange32
#define BfpSystem_EndianSwapPtr BfpSystem_EndianSwap32
#else
#define BfpSystem_InterlockedExchangePtr BfpSystem_InterlockedExchange64
#define BfpSystem_InterlockedExchangeAddPtr BfpSystem_InterlockedExchangeAdd64
#define BfpSystem_InterlockedCompareExchangePtr BfpSystem_InterlockedCompareExchange64
#define BfpSystem_EndianSwapPtr BfpSystem_EndianSwap64
#endif

#endif

enum BfpProcessResult
{
	BfpProcessResult_Ok = BfpResult_Ok,
	BfpProcessResult_UnknownError = BfpResult_UnknownError,
	BfpProcessResult_InsufficientBuffer = BfpResult_InsufficientBuffer,	
};

BFP_EXPORT intptr BFP_CALLTYPE BfpProcess_GetCurrentId();
BFP_EXPORT bool BFP_CALLTYPE BfpProcess_IsRemoteMachine(const char* machineName);
BFP_EXPORT BfpProcess* BFP_CALLTYPE BfpProcess_GetById(const char* machineName, int processId, BfpProcessResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpProcess_Enumerate(const char* machineName, BfpProcess** outProcesses, int* inOutProcessesSize, BfpProcessResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpProcess_Release(BfpProcess* process);
BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetMainWindowTitle(BfpProcess* process, char* outTitle, int* inOutTitleSize, BfpProcessResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpProcess_GetProcessName(BfpProcess* process, char* outName, int* inOutNameSize, BfpProcessResult* outResult);
BFP_EXPORT int BFP_CALLTYPE BfpProcess_GetProcessId(BfpProcess* process);

enum BfpSpawnFlags
{
	BfpSpawnFlag_None = 0,
	BfpSpawnFlag_ArgsIncludesTarget = 1, // Otherwise most platforms prepend targetPath to the args	
	BfpSpawnFlag_UseArgsFile = 2,
	BfpSpawnFlag_UseArgsFile_Native = 4,
	BfpSpawnFlag_UseArgsFile_UTF8 = 8,
	BfpSpawnFlag_UseArgsFile_BOM = 0x10,
	BfpSpawnFlag_UseShellExecute = 0x20, // Allows opening non-executable files by file association (ie: documents)
	BfpSpawnFlag_RedirectStdInput = 0x40,
	BfpSpawnFlag_RedirectStdOutput = 0x80,
	BfpSpawnFlag_RedirectStdError = 0x100,
	BfpSpawnFlag_NoWindow = 0x200,
	BfpSpawnFlag_ErrorDialog = 0x400,
	BfpSpawnFlag_Window_Hide = 0x800,
	BfpSpawnFlag_Window_Maximized = 0x1000,
};

enum BfpSpawnResult
{
    BfpSpawnResult_Ok = BfpResult_Ok,
	BfpSpawnResult_UnknownError = BfpResult_UnknownError,
	BfpSpawnResult_TempFileError = BfpResult_TempFileError
};

enum BfpKillFlags
{
	BfpKillFlag_None = 0,
	BfpKillFlag_KillChildren = 1
};

BFP_EXPORT BfpSpawn* BFP_CALLTYPE BfpSpawn_Create(const char* targetPath, const char* args, const char* workingDir, const char* env, BfpSpawnFlags flags, BfpSpawnResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpSpawn_Release(BfpSpawn* spawn);
BFP_EXPORT void BFP_CALLTYPE BfpSpawn_Kill(BfpSpawn* spawn, int exitCode, BfpKillFlags killFlags, BfpSpawnResult* outResult);
BFP_EXPORT bool BFP_CALLTYPE BfpSpawn_WaitFor(BfpSpawn* spawn, int waitMS, int* outExitCode, BfpSpawnResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpSpawn_GetStdHandles(BfpSpawn* spawn, BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr); // Caller must release the files

enum BfpThreadCreateFlags
{
	BfpThreadCreateFlag_None = 0,
	BfpThreadCreateFlag_Suspended = 1,
	BfpThreadCreateFlag_StackSizeReserve = 2, // Otherwise is a 'commit'
};

typedef void (BFP_CALLTYPE *BfpThreadStartProc)(void* threadParam);

enum BfpThreadPriority
{
	BfpThreadPriority_VeryLow = -2,
	BfpThreadPriority_Low = -1,
	BfpThreadPriority_Normal = 0,
	BfpThreadPriority_High = 1,
	BfpThreadPriority_VeryHigh =2
};

enum BfpThreadResult
{
	BfpThreadResult_Ok					= BfpResult_Ok,
	BfpThreadResult_UnknownError		= BfpResult_UnknownError,
	BfpThreadResult_InsufficientBuffer	= BfpResult_InsufficientBuffer,
	BfpThreadResult_NotSupported		= BfpResult_NotSupported
};

BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_Create(BfpThreadStartProc startProc, void* threadParam, intptr stackSize = 0, BfpThreadCreateFlags flags = BfpThreadCreateFlag_None, BfpThreadId* outThreadId = NULL);
BFP_EXPORT void BFP_CALLTYPE BfpThread_Release(BfpThread* thread);
BFP_EXPORT void BFP_CALLTYPE BfpThread_SetName(BfpThread* thread, const char* name, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_GetName(BfpThread* thread, char* outName, int* inOutNameSize, BfpThreadResult* outResult);
BFP_EXPORT BfpThread* BFP_CALLTYPE BfpThread_GetCurrent();
BFP_EXPORT BfpThreadId BFP_CALLTYPE BfpThread_GetCurrentId();
BFP_EXPORT bool BFP_CALLTYPE BfpThread_WaitFor(BfpThread* thread, int waitMS);
BFP_EXPORT BfpThreadPriority BFP_CALLTYPE BfpThread_GetPriority(BfpThread* thread, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_SetPriority(BfpThread* thread, BfpThreadPriority threadPriority, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_Suspend(BfpThread* thread, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_Resume(BfpThread* thread, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_GetIntRegisters(BfpThread* thread, intptr* outStackPtr, intptr* outIntRegs, int* inOutIntRegCount, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_GetStackInfo(BfpThread* thread, intptr* outStackBase, int* outStackLimit, BfpThreadResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpThread_Sleep(int sleepMS);
BFP_EXPORT bool BFP_CALLTYPE BfpThread_Yield();

struct BfpCritSect;
BFP_EXPORT BfpCritSect* BFP_CALLTYPE BfpCritSect_Create();
BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Release(BfpCritSect* critSect);
BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Enter(BfpCritSect* critSect);
BFP_EXPORT bool BFP_CALLTYPE BfpCritSect_TryEnter(BfpCritSect* critSect, int waitMS);
BFP_EXPORT void BFP_CALLTYPE BfpCritSect_Leave(BfpCritSect* critSect);

struct BfpTLS;
BFP_EXPORT BfpTLS* BFP_CALLTYPE BfpTLS_Create();
BFP_EXPORT void BFP_CALLTYPE BfpTLS_Release(BfpTLS* tls);
BFP_EXPORT void BFP_CALLTYPE BfpTLS_SetValue(BfpTLS* tls, void* value);
BFP_EXPORT void* BFP_CALLTYPE BfpTLS_GetValue(BfpTLS* tls);


enum BfpEventFlags
{
	BfpEventFlag_None = 0,
	BfpEventFlag_AllowAutoReset = 1,
	BfpEventFlag_AllowManualReset = 2,
	BfpEventFlag_InitiallySet_Auto = 4,
	BfpEventFlag_InitiallySet_Manual = 8
};

enum BfpEventResult
{
	BfpEventResult_Ok				= BfpResult_Ok,
	BfpEventResult_NotSupported		= BfpResult_NotSupported
};

struct BfpEvent;
BFP_EXPORT BfpEvent* BFP_CALLTYPE BfpEvent_Create(BfpEventFlags flags);
BFP_EXPORT void BFP_CALLTYPE BfpEvent_Release(BfpEvent* event);
BFP_EXPORT void BFP_CALLTYPE BfpEvent_Set(BfpEvent* event, bool requireManualReset);
BFP_EXPORT void BFP_CALLTYPE BfpEvent_Reset(BfpEvent* event, BfpEventResult* outResult);
BFP_EXPORT bool BFP_CALLTYPE BfpEvent_WaitFor(BfpEvent* event, int waitMS);

enum BfpLibResult
{
    BfpLibResult_Ok                     = BfpResult_Ok,
	BfpLibResult_UnknownError			= BfpResult_UnknownError,
    BfpLibResult_InsufficientBuffer     = BfpResult_InsufficientBuffer
};

struct BfpDynLib;
BFP_EXPORT BfpDynLib* BFP_CALLTYPE BfpDynLib_Load(const char* fileName);
BFP_EXPORT void BFP_CALLTYPE BfpDynLib_Release(BfpDynLib* lib);
BFP_EXPORT void BFP_CALLTYPE BfpDynLib_GetFilePath(BfpDynLib* lib, char* outPath, int* inOutPathSize, BfpLibResult* outResult);
BFP_EXPORT void* BFP_CALLTYPE BfpDynLib_GetProcAddress(BfpDynLib* lib, const char* name);

enum BfpSysDirectoryKind
{
	BfpSysDirectoryKind_Default, // Home on Linux, Desktop on Windows, etc.
	BfpSysDirectoryKind_Home,
	BfpSysDirectoryKind_System,
	BfpSysDirectoryKind_Desktop,
	BfpSysDirectoryKind_Desktop_Common,
	BfpSysDirectoryKind_AppData_Local,
	BfpSysDirectoryKind_AppData_LocalLow,
	BfpSysDirectoryKind_AppData_Roaming,
	BfpSysDirectoryKind_Programs,
	BfpSysDirectoryKind_Programs_Common	
};

struct BfpFindFileData;
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Create(const char* name, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Rename(const char* oldName, const char* newName, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_Delete(const char* name, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetCurrent(char* outPath, int* inOutPathSize, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_SetCurrent(const char* path, BfpFileResult* outResult);
BFP_EXPORT bool BFP_CALLTYPE BfpDirectory_Exists(const char* path);
BFP_EXPORT void BFP_CALLTYPE BfpDirectory_GetSysDirectory(BfpSysDirectoryKind sysDirKind, char* outPath, int* inOutPathLen, BfpFileResult* outResult);

enum BfpFileCreateKind
{
	BfpFileCreateKind_CreateAlways,
	BfpFileCreateKind_CreateIfNotExists,
	BfpFileCreateKind_OpenExisting,
};

enum BfpFileCreateFlags
{
	BfpFileCreateFlag_Read = 1,
	BfpFileCreateFlag_Write = 2,		

	BfpFileCreateFlag_ShareRead = 4,
	BfpFileCreateFlag_ShareWrite = 8,
	BfpFileCreateFlag_ShareDelete = 0x10,
	
	BfpFileCreateFlag_Append = 0x20,
	BfpFileCreateFlag_Truncate = 0x40,

	BfpFileCreateFlag_WriteThrough = 0x80,
	BfpFileCreateFlag_DeleteOnClose = 0x100,
	BfpFileCreateFlag_NoBuffering = 0x200,

	BfpFileCreateFlag_NonBlocking = 0x400,
	BfpFileCreateFlag_AllowTimeouts = 0x800,
	BfpFileCreateFlag_Pipe = 0x1000,
};

enum BfpFileSeekKind
{
	BfpFileSeekKind_Absolute,
	BfpFileSeekKind_Relative,
	BfpFileSeekKind_FromEnd
};

enum BfpFileAttributes
{
	BfpFileAttribute_None = 0,
	BfpFileAttribute_Normal = 1,
	BfpFileAttribute_Directory = 2,
	BfpFileAttribute_SymLink = 4,
	BfpFileAttribute_Device = 8,
	BfpFileAttribute_ReadOnly = 0x10,
	BfpFileAttribute_Hidden = 0x20,
	BfpFileAttribute_System = 0x40,
	BfpFileAttribute_Temporary = 0x80,
	BfpFileAttribute_Offline = 0x100,
	BfpFileAttribute_Encrypted = 0x200,
	BfpFileAttribute_Archive = 0x400,
};

enum BfpFileCopyKind
{
	BfpFileCopyKind_Always,
	BfpFileCopyKind_IfNotExists,
	BfpFileCopyKind_IfNewer,
};

enum BfpFileWaitFlags
{
	BfpFileWaitFlag_None = 0,
	BfpFileWaitFlag_Read = 1,
	BfpFileWaitFlag_Write = 2,
};

enum BfpFileStdKind
{
	BfpFileStdKind_StdOut,
	BfpFileStdKind_StdError,
	BfpFileStdKind_StdIn
};

BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_Create(const char* name, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags, BfpFileAttributes createdFileAttr, BfpFileResult* outResult);
BFP_EXPORT BfpFile* BFP_CALLTYPE BfpFile_GetStd(BfpFileStdKind kind, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Release(BfpFile* file);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Close(BfpFile* file, BfpFileResult* outResult);
BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Write(BfpFile* file, const void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult);
BFP_EXPORT intptr BFP_CALLTYPE BfpFile_Read(BfpFile* file, void* buffer, intptr size, int timeoutMS, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Flush(BfpFile* file);
BFP_EXPORT int64 BFP_CALLTYPE BfpFile_GetFileSize(BfpFile* file);
BFP_EXPORT int64 BFP_CALLTYPE BfpFile_Seek(BfpFile* file, int64 offset, BfpFileSeekKind seekKind);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Truncate(BfpFile* file);
BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFile_GetTime_LastWrite(const char* path);
BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFile_GetAttributes(const char* path, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_SetAttributes(const char* path, BfpFileAttributes attribs, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Copy(const char* oldPath, const char* newPath, BfpFileCopyKind copyKind, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Rename(const char* oldPath, const char* newPath, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_Delete(const char* path, BfpFileResult* outResult);
BFP_EXPORT bool BFP_CALLTYPE BfpFile_Exists(const char* path);
BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempPath(char* outPath, int* inOutPathSize, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_GetTempFileName(char* outName, int* inOutNameSize, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_GetFullPath(const char* inPath, char* outPath, int* inOutPathSize, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFile_GetActualPath(const char* inPath, char* outPath, int* inOutPathSize, BfpFileResult* outResult);

enum BfpFileChangeKind
{
	BfpFileChangeKind_Added,
	BfpFileChangeKind_Removed,
	BfpFileChangeKind_Modified,
	BfpFileChangeKind_Renamed,
	BfpFileChangeKind_Failed
};

typedef void(*BfpDirectoryChangeFunc)(BfpFileWatcher* watcher, void* userData, BfpFileChangeKind changeKind, const char* directory, const char* fileName, const char* oldName);

enum BfpFileWatcherFlags
{
	BfpFileWatcherFlag_None = 0,
	BfpFileWatcherFlag_IncludeSubdirectories = 1
};

BFP_EXPORT BfpFileWatcher* BFP_CALLTYPE BfpFileWatcher_WatchDirectory(const char* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult);
BFP_EXPORT void BFP_CALLTYPE BfpFileWatcher_Release(BfpFileWatcher* fileWatcher);

enum BfpFindFileFlags
{
	BfpFindFileFlag_None = 0,
	BfpFindFileFlag_Files = 1,
	BfpFindFileFlag_Directories = 2,
};

BFP_EXPORT BfpFindFileData* BFP_CALLTYPE BfpFindFileData_FindFirstFile(const char* path, BfpFindFileFlags flags, BfpFileResult* outResult);
BFP_EXPORT bool BFP_CALLTYPE BfpFindFileData_FindNextFile(BfpFindFileData* findData);
BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_GetFileName(BfpFindFileData* findData, char* outName, int* inOutNameSize, BfpFileResult* outResult);
BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_LastWrite(BfpFindFileData* findData);
BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Created(BfpFindFileData* findData);
BFP_EXPORT BfpTimeStamp BFP_CALLTYPE BfpFindFileData_GetTime_Access(BfpFindFileData* findData);
BFP_EXPORT BfpFileAttributes BFP_CALLTYPE BfpFindFileData_GetFileAttributes(BfpFindFileData* findData);
BFP_EXPORT void BFP_CALLTYPE BfpFindFileData_Release(BfpFindFileData* findData);

BFP_EXPORT int BFP_CALLTYPE BfpStack_CaptureBackTrace(int framesToSkip, intptr* outFrames, int wantFrameCount);

BFP_EXPORT void BFP_CALLTYPE BfpOutput_DebugString(const char* str);
