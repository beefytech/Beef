
void OutputDebugStringA(const char* str);
void OutputDebugStringW(const wchar_t* str);

void* BFTlsGetValue(BFTlsKey tlsKey);
bool BFTlsSetValue(BFTlsKey tlsKey, void* tlsValue);
BFTlsKey BFTlsAlloc();
bool BFTlsFree(BFTlsKey tlsKey);

enum GET_FILEEX_INFO_LEVELS
{
    GetFileExInfoStandard,
    GetFileExMaxInfoLevel
};

typedef bool BOOL;

#define MAX_PATH 4096
#define BF_UNUSED __unused
#define BF_ENDIAN_LITTLE
#define INFINITE -1

#define CF_TEXT                 0

#define ERROR_SUCCESS           0
#define ERROR_FILE_NOT_FOUND    2L
#define ERROR_NO_MORE_FILES     18L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_INVALID_NAME      123L
#define ERROR_ALREADY_EXISTS    183L

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr)-1)
#define INVALID_FILE_ATTRIBUTES (uint32)-1
#define INVALID_SET_FILE_POINTER -1
#define INVALID_FILE_SIZE -1

#define NOERROR             0
#define E_NOINTERFACE       (HRESULT)0x80004002L
#define E_FAIL              (HRESULT)0x80004005L

#define FILE_BEGIN           0
#define FILE_CURRENT         1
#define FILE_END             2

#define GENERIC_READ        (0x80000000L)
#define GENERIC_WRITE       (0x40000000L)
#define GENERIC_EXECUTE     (0x20000000L)
#define GENERIC_ALL         (0x10000000L)

#define FILE_SHARE_READ     0x00000001
#define FILE_SHARE_WRITE    0x00000002
#define FILE_SHARE_DELETE   0x00000004

#define CREATE_NEW          1
#define CREATE_ALWAYS       2
#define OPEN_EXISTING       3
#define OPEN_ALWAYS         4
#define TRUNCATE_EXISTING   5

#define STD_INPUT_HANDLE    -10
#define STD_OUTPUT_HANDLE   -11
#define STD_ERROR_HANDLE    -12

#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
#define FILE_ATTRIBUTE_DEVICE               0x00000040
#define FILE_ATTRIBUTE_NORMAL               0x00000080
#define FILE_ATTRIBUTE_TEMPORARY            0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT        0x00000400
#define FILE_ATTRIBUTE_COMPRESSED           0x00000800
#define FILE_ATTRIBUTE_OFFLINE              0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000
#define FILE_ATTRIBUTE_INTEGRITY_STREAM     0x00008000
#define FILE_ATTRIBUTE_VIRTUAL              0x00010000
#define FILE_ATTRIBUTE_NO_SCRUB_DATA        0x00020000

#define FILE_FLAG_WRITE_THROUGH         0x80000000
#define FILE_FLAG_OVERLAPPED            0x40000000
#define FILE_FLAG_NO_BUFFERING          0x20000000
#define FILE_FLAG_RANDOM_ACCESS         0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN       0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE       0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS      0x02000000
#define FILE_FLAG_POSIX_SEMANTICS       0x01000000
#define FILE_FLAG_SESSION_AWARE         0x00800000
#define FILE_FLAG_OPEN_REPARSE_POINT    0x00200000
#define FILE_FLAG_OPEN_NO_RECALL        0x00100000
#define FILE_FLAG_FIRST_PIPE_INSTANCE   0x00080000

#define REPLACEFILE_IGNORE_MERGE_ERRORS 0x00000002

#define FILE_TYPE_UNKNOWN   0x0000
#define FILE_TYPE_DISK      0x0001
#define FILE_TYPE_CHAR      0x0002
#define FILE_TYPE_PIPE      0x0003
#define FILE_TYPE_REMOTE    0x8000

#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE    0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY  0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK  0x000000FF

#define stricmp strcasecmp

#define TIME_ZONE_ID_UNKNOWN 0
#define TIME_ZONE_ID_INVALID -1

struct SYSTEMTIME
{
    uint16 wYear;
    uint16 wMonth;
    uint16 wDayOfWeek;
    uint16 wDay;
    uint16 wHour;
    uint16 wMinute;
    uint16 wSecond;
    uint16 wMilliseconds;
};

struct TIME_ZONE_INFORMATION
{
    int32      Bias;
    wchar_t    StandardName[32];
    SYSTEMTIME StandardDate;
    int32      StandardBias;
    wchar_t    DaylightName[32];
    SYSTEMTIME DaylightDate;
    int32      DaylightBias;
};

extern int gBFPlatformLastError;


typedef intptr (BFSTDCALL *BFLibProcAddr)();

struct FILETIME
{
    uint32 dwLowDateTime;
    uint32 dwHighDateTime;
};

struct WIN32_FIND_DATAW
{
    uint32 dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    uint32 nFileSizeHigh;
    uint32 nFileSizeLow;
    uint32 dwReserved0;
    uint32 dwReserved1;
    wchar_t cFileName[MAX_PATH];
    wchar_t cAlternateFileName[14];
};

struct WIN32_FILE_ATTRIBUTE_DATA
{
    uint32 dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    uint32 nFileSizeHigh;
    uint32 nFileSizeLow;
};

struct SECURITY_ATTRIBUTES
{
    uint32 nLength;
    void* lpSecurityDescriptor;
    bool bInheritHandle;
};

#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 0x102

struct OVERLAPPED
{

};

struct MODULEINFO
{
    void* lpBaseOfDll;
    DWORD SizeOfImage;
    void* EntryPoint;
};

struct ULARGE_INTEGER
{
    uint64 QuadPart;
};

struct LARGE_INTEGER
{
    int64 QuadPart;
};

#define DIR_SEP_CHAR '/'
#define DIR_SEP_CHAR_ALT '\\'

#define PROCESS_ALL_ACCESS 0xFFFF
#define THREAD_ALL_ACCESS 0xFFFF

#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1

HANDLE GetModuleHandle(HANDLE h);
uint32 GetModuleFileNameW(uint32 hModule, wchar_t* lpFilename, uint32 length);
uint32 GetModuleFileNameA(uint32 hModule, char* lpFilename, uint32 length);
HMODULE LoadLibraryA(const char* fileName);
void FreeLibrary(HMODULE lib);
bool CreateDirectoryW(const wchar_t* str, void* securityAttributes);
bool RemoveDirectoryW(const wchar_t* str);
int GetLastError();
BFLibProcAddr GetProcAddress(HMODULE mod, const char* name);
int* GetStdHandle(int32 handleId);
BF_THREADID GetCurrentThreadId();
HANDLE GetCurrentThread();
bool SetThreadPriority(HANDLE hThread, int nPriority);
int GetThreadPriority(HANDLE hThread);
HANDLE OpenThread(int desiredAccess, bool inheritHandle, BF_THREADID threadId);
typedef uint32 (BFSTDCALL *LPTHREAD_START_ROUTINE)(void* param);
void* CreateThread(void* threadAttributes, int32 stackSize, LPTHREAD_START_ROUTINE threadStartProc, void* param, uint32 flags, BF_THREADID* threadId);
uint32 ResumeThread(HANDLE thread);
uint32 SuspendThread(HANDLE thread);
void Sleep(int32 ms);
HANDLE FindFirstFileW(const wchar_t* lpFileName, WIN32_FIND_DATAW* lpFindFileData);
bool FindNextFileW(HANDLE hFindFile, WIN32_FIND_DATAW* lpFindFileData);
bool FindClose(HANDLE hFindFile);
uint32 GetCurrentDirectoryW(uint32 nBufferLength, const wchar_t* lpBuffer);
bool SetCurrentDirectoryW(wchar_t* lpBuffer);
bool CopyFileW(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, bool allowOverwrite);
bool MoveFileW(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName);
bool DeleteFileW(const wchar_t* lpFileName);
bool ReplaceFileW(const wchar_t* lpReplacedFileName, const wchar_t* lpReplacementFileName, const wchar_t* lpBackupFileName, uint32 dwReplaceFlags, void* lpExclude, void* lpReserved);
bool SetFileAttributesW(const wchar_t* lpFileName, uint32 dwFileAttributes);
int32 GetFileType(HANDLE fileHandle);
bool GetFileAttributesExW(const wchar_t* lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, void* lpFileInformation);
HANDLE CreateFileW(const wchar_t* lpFileName, uint32 dwDesiredAccess, uint32 dwShareMode, SECURITY_ATTRIBUTES* lpSecurityAttributes, uint32 dwCreationDisposition, uint32 dwFlagsAndAttributes, HANDLE hTemplateFile);
bool CreatePipe(HANDLE* hReadPipe, HANDLE* hWritePipe, SECURITY_ATTRIBUTES* lpPipeAttributes, uint32 nSize);
bool WriteFile(HANDLE hFile, void* lpBuffer, uint32 nNumberOfBytesToWrite, uint32* lpNumberOfBytesWritten, OVERLAPPED* lpOverlapped);
bool ReadFile(HANDLE hFile, void* lpBuffer, uint32 nNumberOfBytesToRead, uint32* lpNumberOfBytesRead, OVERLAPPED* lpOverlapped);
bool CloseHandle_File(HANDLE handle);
bool SetEvent(HANDLE handle);
bool ResetEvent(HANDLE handle);
int WaitForSingleObject(HANDLE obj, int waitMs);
int WaitForSingleObject_Thread(HANDLE obj, int waitMs);
bool CloseHandle_Event(HANDLE handle);
bool FlushFileBuffers(HANDLE handle);
int32 SetFilePointer(HANDLE handle, int32 distanceToMove, int32* distanceToMoveHigh, uint32 moveMethod);
int32 GetFileSize(HANDLE hFile, uint32* fileSizeHigh);
bool GetFileSizeEx(HANDLE hFile, LARGE_INTEGER* lpFileSize);
bool SetEndOfFile(HANDLE hFile);
bool SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, LARGE_INTEGER* lpNewFilePointer, DWORD dwMoveMethod);
bool SetFileTime(HANDLE hFile, const FILETIME* lpCreationTime, const FILETIME* lpLastAccessTime, const FILETIME* lpLastWriteTime);
bool LockFile(HANDLE hFile, uint32 dwFileOffsetLow, uint32 dwFileOffsetHigh, uint32 nNumberOfBytesToLockLow, uint32 nNumberOfBytesToLockHigh);
bool UnlockFile(HANDLE hFile, uint32 dwFileOffsetLow, uint32 dwFileOffsetHigh, uint32 nNumberOfBytesToUnlockLow, uint32 nNumberOfBytesToUnlockHigh);
bool DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle, HANDLE* lpTargetHandle, uint32 dwDesiredAccess, bool bInheritHandle, uint32 dwOptions);
int32 GetTempPath(int32 bufferLen, wchar_t* str);
HANDLE CreateEventW(SECURITY_ATTRIBUTES* lpEventAttributes, bool bManualReset, bool bInitialState, wchar_t* lpName);
HANDLE OpenEventW(uint32 dwDesiredAccess, bool bInheritHandle, wchar_t* lpName);
HANDLE CreateMutexW(SECURITY_ATTRIBUTES* lpMutexAttributes, bool bInitialOwner, wchar_t* lpName);
HANDLE OpenMutexW(uint32 dwDesiredAccess, bool bInheritHandle, wchar_t* lpName);
bool ReleaseMutex(HANDLE mutex);
uint32 FormatMessageW(uint32 dwFlags, void* lpSource, uint32 dwMessageId, uint32 dwLanguageId, wchar_t* lpBuffer, uint32 nSize, va_list* Arguments);
int32 WSAGetLastError();
bool CloseHandle_Thread(HANDLE handle);
bool CloseHandle_Process(HANDLE handle);
void GetExitCodeProcess(HANDLE handle, DWORD* code);
bool GetProcessTimes(HANDLE handle, FILETIME* createTime, FILETIME* exitTime, FILETIME* kernelTime, FILETIME* userTime);
bool GetProcessWorkingSetSize(HANDLE handle, size_t* curMin, size_t* curMax);
bool SetProcessWorkingSetSize(HANDLE handle, size_t curMin, size_t curMax);
bool EnumProcessModules(HANDLE handle, HMODULE* mods, DWORD cb, DWORD* needed);
bool GetModuleBaseNameW(HANDLE handle, HMODULE mod, wchar_t* modname, int maxLen);
bool GetModuleFileNameExW(HANDLE handle, HMODULE mod, wchar_t* modname, int maxLen);
bool GetModuleInformation(HANDLE handle, HMODULE mod, MODULEINFO* modinfo, int modInfoSize);
int GetPriorityClass(HANDLE handle);
bool SetPriorityClass(HANDLE handle, int priClass);
bool TerminateProcess(HANDLE handle, int termCode);
bool WaitForInputIdle(HANDLE handle, int ms);
int32 GetCurrentProcessId();
HANDLE GetCurrentProcess();
HANDLE OpenProcess(DWORD desiredAccess, bool inheritHandle, DWORD pid);
bool GetDiskFreeSpaceExW(wchar_t* pathNameStr, ULARGE_INTEGER* wapi_free_bytes_avail, ULARGE_INTEGER* wapi_total_number_of_bytes, ULARGE_INTEGER* wapi_total_number_of_free_bytes);
uint32 GetDriveTypeW(wchar_t* driveName);
bool GetVolumeInformationW(wchar_t* lpRootPathName, wchar_t* lpVolumeNameBuffer, uint32 nVolumeNameSize, uint32* lpVolumeSerialNumber, uint32* lpMaximumComponentLength, uint32* lpFileSystemFlags, wchar_t* lpFileSystemNameBuffer, uint32 nFileSystemNameSize);
uint64 BFClockGetTime();
DWORD GetTimeZoneInformation(TIME_ZONE_INFORMATION* lpTimeZoneInformation);
bool SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, FILETIME* lpFileTime);
void BFGetThreadRegisters(HANDLE threadHandle, intptr* stackPtr, intptr* dataPtr);
void mkdir(const char* path);

typedef void (*PFLS_CALLBACK_FUNCTION)(void* lpFlsData);
DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback);
void* FlsGetValue(DWORD dwFlsIndex);
BOOL FlsSetValue(DWORD dwFlsIndex, void* lpFlsData);
BOOL FlsFree(DWORD dwFlsIndex);
