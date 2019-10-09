namespace System
{
	public interface INativeWindow
	{
	    int Handle { get; }
	}

	static class Platform
	{
		public const bool IsSingleProcessor = false;
		public const int ProcessorCount = 8;

		public struct BfpTimeStamp : uint64
        {

        }

		public enum Result : int32
		{
			Ok,
			UnknownError,
			InsufficientBuffer,
			NotSupported,
			NoResults,	
			InvalidParameter,
			Locked,
			AlreadyExists,
			NotFound,
			ShareError,
			AccessError,
			PartialData,
			TempFileError,
			Timeout,
			NotEmpty
		};

		public struct BfpCritSect {}
		public struct BfpSpawn {}
		public struct BfpFile {}
		public struct BfpFindFileData {}
		public struct BfpDynLib {}
		public struct BfpEvent {};
		public struct BfpFileWatcher {}
		public struct BfpProcess {}

		public enum BfpSystemResult : int32
		{
			Ok = (int)Result.Ok,
			UnknownError = (int)Result.UnknownError,
			TempFileError = (int)Result.TempFileError
		}

		[StdCall, CLink]
		public static extern uint32 BfpSystem_TickCount();
		[StdCall, CLink]
		public static extern BfpTimeStamp BfpSystem_GetTimeStamp();
		[StdCall, CLink]
		public static extern uint8 BfpSystem_InterlockedExchange8(uint8* ptr, uint8 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint16 BfpSystem_InterlockedExchange16(uint16* ptr, uint16 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint32 BfpSystem_InterlockedExchange32(uint32* ptr, uint32 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint64 BfpSystem_InterlockedExchange64(uint64* ptr, uint64 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint8 BfpSystem_InterlockedExchangeAdd8(uint8* ptr, uint8 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint16 BfpSystem_InterlockedExchangeAdd16(uint16* ptr, uint16 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint32 BfpSystem_InterlockedExchangeAdd32(uint32* ptr, uint32 val); /// Returns the initial value in 'ptr'
		[StdCall, CLink]
		public static extern uint64 BfpSystem_InterlockedExchangeAdd64(uint64* ptr, uint64 val);
		[StdCall, CLink]
		public static extern uint8 BfpSystem_InterlockedCompareExchange8(uint8* ptr, uint8 oldVal, uint8 newVal);
		[StdCall, CLink]
		public static extern uint16 BfpSystem_InterlockedCompareExchange16(uint16* ptr, uint16 oldVal, uint16 newVal);
		[StdCall, CLink]
		public static extern uint32 BfpSystem_InterlockedCompareExchange32(uint32* ptr, uint32 oldVal, uint32 newVal);
		[StdCall, CLink]
		public static extern uint64 BfpSystem_InterlockedCompareExchange64(uint64* ptr, uint64 oldVal, uint64 newVal);
		[StdCall, CLink]
		public static extern void BfpSystem_GetExecutablePath(char8* outStr, int32* inOutStrSize, BfpSystemResult* outResult);
		[StdCall, CLink]
		public static extern void BfpSystem_GetEnvironmentStrings(char8* outStr, int32* inOutStrSize, BfpSystemResult* outResult);
		[StdCall, CLink]
		public static extern int32 BfpSystem_GetNumLogicalCPUs(BfpSystemResult* outResult);
		[StdCall, CLink]
		public static extern int64 BfpSystem_GetCPUTick();
		[StdCall, CLink]
		public static extern int64 BfpSystem_GetCPUTickFreq();
		[StdCall, CLink]
		public static extern void BfpSystem_CreateGUID(Guid* outGuid);
		[StdCall, CLink]
		public static extern void BfpSystem_GetComputerName(char8* outStr, int32* inOutStrSize, BfpSystemResult* outResult);

		public enum BfpFileWatcherFlags : int32
		{
			None = 0,
			IncludeSubdirectories = 1
		};

		public enum BfpFileChangeKind : int32
		{
			BfpFileChangeKind_Added,
			BfpFileChangeKind_Removed,
			BfpFileChangeKind_Modified,
			BfpFileChangeKind_Renamed,	
			BfpFileChangeKind_Failed
		};

		public function void BfpDirectoryChangeFunc(BfpFileWatcher* watcher, void* userData, BfpFileChangeKind changeKind, char8* directory, char8* fileName, char8* oldName);

		[StdCall, CLink]
		public static extern BfpFileWatcher* BfpFileWatcher_WatchDirectory(char8* path, BfpDirectoryChangeFunc callback, BfpFileWatcherFlags flags, void* userData, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFileWatcher_Release(BfpFileWatcher* fileWatcher);

		public enum BfpProcessResult : int32
		{
			Ok = (int)Result.Ok,
			InsufficientBuffer = (int)Result.InsufficientBuffer
		}

		[StdCall, CLink]
		public static extern bool BfpProcess_IsRemoteMachine(char8* machineName);
		[StdCall, CLink]
		public static extern BfpProcess* BfpProcess_GetById(char8* machineName, int32 processId, BfpProcessResult* outResult);
		[StdCall, CLink]
		public static extern void BfpProcess_Enumerate(char8* machineName, BfpProcess** outProcesses, int32* inOutProcessesSize, BfpProcessResult* outResult);
		[StdCall, CLink]
		public static extern void BfpProcess_Release(BfpProcess* process);
		[StdCall, CLink]
		public static extern void BfpProcess_GetMainWindowTitle(BfpProcess* process, char8* outTitle, int32* inOutTitleSize, BfpProcessResult* outResult);
		[StdCall, CLink]
		public static extern void BfpProcess_GetProcessName(BfpProcess* process, char8* outName, int32* inOutNameSize, BfpProcessResult* outResult);
		[StdCall, CLink]
		public static extern int32 BfpProcess_GetProcessId(BfpProcess* process);

		public enum BfpSpawnFlags : int32
		{
			None = 0,
			ArgsIncludesTarget = 1, // Otherwise most platforms prepend targetPath to the args	
			UseArgsFile = 2,
			UseArgsFile_Native = 4,
			UseArgsFile_UTF8 = 8,
			UseArgsFile_BOM = 0x10,
			UseShellExecute = 0x20, // Allows opening non-executable files by file association (ie: documents)
			RedirectStdInput = 0x40,
			RedirectStdOutput = 0x80,
			RedirectStdError = 0x100,
			NoWindow = 0x200,
			ErrorDialog = 0x400,
			Window_Hide = 0x800,
			Window_Maximized = 0x1000,
		};

		public enum BfpKillFlags : int32
		{
			None = 0,
			KillChildren = 1
		}

		public enum BfpSpawnResult : int32
		{
		    Ok = (int)Result.Ok,
			UnknownError = (int)Result.UnknownError
		};

		[StdCall, CLink]
		public static extern BfpSpawn* BfpSpawn_Create(char8* targetPath, char8* args, char8* workingDir, char8* env, BfpSpawnFlags flags, BfpSpawnResult* outResult);
		[StdCall, CLink]
		public static extern void BfpSpawn_Release(BfpSpawn* spawn);
		[StdCall, CLink]
		public static extern void BfpSpawn_Kill(BfpSpawn* spawn, int32 exitCode, BfpKillFlags killFlags, BfpSpawnResult* outResult);
		[StdCall, CLink]
		public static extern bool BfpSpawn_WaitFor(BfpSpawn* spawn, int waitMS, int* outExitCode, BfpSpawnResult* outResult);
		[StdCall, CLink]
		public static extern void BfpSpawn_GetStdHandles(BfpSpawn* spawn, BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr);

		[StdCall, CLink]
		public static extern int BfpProcess_GetCurrentId();

		[StdCall, CLink]
        public static extern BfpCritSect* BfpCritSect_Create();
		[StdCall, CLink]
        public static extern void BfpCritSect_Release(BfpCritSect* critSect);
		[StdCall, CLink]
		public static extern void BfpCritSect_Enter(BfpCritSect* critSect);
		[StdCall, CLink]
		public static extern bool BfpCritSect_TryEnter(BfpCritSect* critSect, int32 waitMS);
		[StdCall, CLink]
		public static extern void BfpCritSect_Leave(BfpCritSect* critSect);

		public enum BfpEventFlags : int32
		{
			None = 0,
			AllowAutoReset = 1,
			AllowManualReset = 2,
			InitiallySet_Auto = 4,
			InitiallySet_Manual = 8
		};

		public enum BfpEventResult : int32
		{
			BfpEventResult_Ok				= (int)Result.Ok,
			BfpEventResult_NotSupported		= (int)Result.NotSupported
		};

		[StdCall, CLink]
		public static extern BfpEvent* BfpEvent_Create(BfpEventFlags flags);
		[StdCall, CLink]
		public static extern void BfpEvent_Release(BfpEvent* event);
		[StdCall, CLink]
		public static extern void BfpEvent_Set(BfpEvent* event, bool requireManualReset);
		[StdCall, CLink]
		public static extern void BfpEvent_Reset(BfpEvent* event, BfpEventResult* outResult);
		[StdCall, CLink]
		public static extern bool BfpEvent_WaitFor(BfpEvent* event, int32 waitMS);

		public enum BfpLibResult : int32
		{
		    Ok                  = (int)Result.Ok,
			UnknownError		= (int)Result.UnknownError,
		    InsufficientBuffer  = (int)Result.InsufficientBuffer
		};

		[StdCall, CLink]
		public static extern BfpDynLib* BfpDynLib_Load(char8* fileName);
		[StdCall, CLink]
		public static extern void BfpDynLib_Release(BfpDynLib* lib);
		[StdCall, CLink]
		public static extern void BfpDynLib_GetFilePath(BfpDynLib* lib, char8* outPath, int32* inOutPathSize, BfpLibResult* outResult);
		[StdCall, CLink]
		public static extern void* BfpDynLib_GetProcAddress(BfpDynLib* lib, char8* name);

		public enum BfpFileResult : int32
		{
			Ok						= (int)Result.Ok,
			NoResults				= (int)Result.NoResults,
			UnknownError			= (int)Result.UnknownError,
			InvalidParameter		= (int)Result.InvalidParameter,
			Locked					= (int)Result.Locked,
			AlreadyExists			= (int)Result.AlreadyExists,
			NotFound				= (int)Result.NotFound,
			ShareError				= (int)Result.ShareError,
			AccessError				= (int)Result.AccessError,
			PartialData				= (int)Result.PartialData,
			InsufficientBuffer		= (int)Result.InsufficientBuffer,
			NotEmpty				= (int)Result.NotEmpty
		};

		[StdCall, CLink]
		public static extern void BfpDirectory_Create(char8* name, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpDirectory_Rename(char8* oldName, char8* newName, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpDirectory_Delete(char8* name, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpDirectory_GetCurrent(char8* outPath, int32* inOutPathSize, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpDirectory_SetCurrent(char8* path, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern bool BfpDirectory_Exists(char8* path);
		[StdCall, CLink]
		public static extern void BfpDirectory_GetSysDirectory(BfpSysDirectoryKind sysDirKind, char8* outPath, int32* inOutPathLen, BfpFileResult* outResult);

		public enum BfpFileCreateKind : int32
		{
			CreateAlways,
			CreateIfNotExists,
			OpenExisting,
		};

		public enum BfpFileCreateFlags : int32
		{
			None = 0,
			Read = 1,
			Write = 2,		

			ShareRead = 4,
			ShareWrite = 8,
			ShareDelete = 0x10,
			
			Append = 0x20,
			Truncate = 0x40,

			WriteThrough = 0x80,
			DeleteOnClose = 0x100,
			NoBuffering = 0x200,

			NonBlocking = 0x400,
			AllowTimeouts = 0x800,
			Pipe = 0x1000,
		};

		public enum BfpFileSeekKind : int32
		{
			Absolute,
			Relative,
			FromEnd
		};

		public enum BfpFileAttributes : int32
		{
			None = 0,
			Normal = 1,
			Directory = 2,
			SymLink = 4,
			Device = 8,
			ReadOnly = 0x10,
			Hidden = 0x20,
			System = 0x40,
			Temporary = 0x80,
			Offline = 0x100,
			Encrypted = 0x200,
			Archive = 0x400,
		};

		public enum BfpFileCopyKind : int32
		{
			Always,
			IfNotExists,
			IfNewer,
		};

		public enum BfpFileStdKind : int32
		{
			Out,
			Error,
			In
		}

		[StdCall, CLink]
		public static extern BfpFile* BfpFile_Create(char8* name, BfpFileCreateKind createKind, BfpFileCreateFlags createFlags, BfpFileAttributes createdFileAttrs, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern BfpFile* BfpFile_GetStd(BfpFileStdKind kind, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_Release(BfpFile* file);
		[StdCall, CLink]
		public static extern int BfpFile_Write(BfpFile* file, void* buffer, int size, int timeoutMS, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern int BfpFile_Read(BfpFile* file, void* buffer, int size, int timeoutMS, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_Flush(BfpFile* file);
		[StdCall, CLink]
		public static extern int64 BfpFile_GetFileSize(BfpFile* file);
		[StdCall, CLink]
		public static extern int64 BfpFile_Seek(BfpFile* file, int64 offset, BfpFileSeekKind seekKind);
		[StdCall, CLink]
		public static extern void BfpFile_Truncate(BfpFile* file);
		[StdCall, CLink]
		public static extern BfpTimeStamp BfpFile_GetTime_LastWrite(char8* path);
		[StdCall, CLink]
		public static extern BfpFileAttributes BfpFile_GetAttributes(char8* path, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_SetAttributes(char8* path, BfpFileAttributes attribs, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_Copy(char8* oldPath, char8* newPath, BfpFileCopyKind copyKind, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_Rename(char8* oldPath, char8* newPath, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_Delete(char8* path, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern bool BfpFile_Exists(char8* path);
		[StdCall, CLink]
		public static extern void BfpFile_GetTempPath(char8* outPath, int32* inOutPathSize, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_GetTempFileName(char8* outName, int32* inOutNameSize, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_GetFullPath(char8* inPath, char8* outPath, int32* inOutPathSize, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern void BfpFile_GetActualPath(char8* inPath, char8* outPath, int32* inOutPathSize, BfpFileResult* outResult);

		public enum BfpFindFileFlags : int32
		{
			None = 0,
			Files = 1,
			Directories = 2,
		};

		[StdCall, CLink]
		public static extern BfpFindFileData* BfpFindFileData_FindFirstFile(char8* path, BfpFindFileFlags flags, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern bool BfpFindFileData_FindNextFile(BfpFindFileData* findData);
		[StdCall, CLink]
		public static extern void BfpFindFileData_GetFileName(BfpFindFileData* findData, char8* outName, int32* inOutNameSize, BfpFileResult* outResult);
		[StdCall, CLink]
		public static extern BfpTimeStamp BfpFindFileData_GetTime_LastWrite(BfpFindFileData* findData);
		[StdCall, CLink]
		public static extern BfpTimeStamp BfpFindFileData_GetTime_Created(BfpFindFileData* findData);
		[StdCall, CLink]
		public static extern BfpTimeStamp BfpFindFileData_GetTime_Access(BfpFindFileData* findData);
		[StdCall, CLink]
		public static extern BfpFileAttributes BfpFindFileData_GetFileAttributes(BfpFindFileData* findData);
		[StdCall, CLink]
		public static extern void BfpFindFileData_Release(BfpFindFileData* findData);

		public enum BfpSysDirectoryKind : int32
		{
			Default, // Home on Linux, Desktop on Windows, etc.
			Home,
			System,
			Desktop,
			Desktop_Common,
			AppData_Local,
			AppData_LocalLow,
			AppData_Roaming,
			Programs,
			Programs_Common
		}

		public static Result<void, Platform.Result> GetStrHelper(String outStr, delegate void (char8* outPtr, int32* outSize, Result* outResult) func)
		{
			let initSize = 4096;
			char8* localBuf = scope char8[initSize]*;

			int32 strSize = initSize;
			Result result = .Ok;
			func(localBuf, &strSize, &result);

			if (result == .Ok)
			{
				outStr.Append(localBuf, strSize - 1);
				return .Ok;
			}
			else if (result == .InsufficientBuffer)
			{
				while (true)
				{
					localBuf = scope char8[strSize]*;
					func(localBuf, &strSize, &result);

					if (result == .InsufficientBuffer)
						continue;
					outStr.Append(localBuf, strSize - 1);
					return .Ok;
				}
			}

			return .Err(result);
		}

		public static Result<Span<T>, Platform.Result> GetSizedHelper<T>(delegate void (T* outPtr, int32* outSize, Result* outResult) func)
		{
			T* vals;
			int32 trySize = 64;
			while (true)
			{
				vals = new T[trySize]*;

				int32 inOutSize = trySize;
				Result result = .Ok;
				func(vals, &inOutSize, &result);

				if ((result != .InsufficientBuffer) && (result != .Ok))
					return .Err(result);
				
				if (result == .Ok)
					return .Ok(Span<T>(vals, inOutSize));

				delete vals;
				trySize = inOutSize;
			}
		}
	}
}
