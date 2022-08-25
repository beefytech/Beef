#pragma warning(disable:4996)
#define HEAPHOOK


//#define USE_CHARCONV

#include <stdio.h>

//#include <crtdefs.h>
//#include <malloc.h>
#include <stdlib.h>
#include <string.h>
//#include <intrin.h>

#ifdef USE_CHARCONV
#include <charconv>
#endif

//#define OBJECT_GUARD_END_SIZE 8
#define OBJECT_GUARD_END_SIZE 0

//#define BF_USE_STOMP_ALLOC 1

//extern "C"
//{
//#include "gperftools/stacktrace.h"
//}

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define BF_RETURN_ADDRESS _ReturnAddress()
#else
#define BF_RETURN_ADDRESS __builtin_return_address(0)
#endif

#include "BeefySysLib/Common.h"
#include "BfObjects.h"
//#include "gc.h"
#include "StompAlloc.h"
#include "BeefySysLib/platform/PlatformHelper.h"
#ifndef BF_DISABLE_FFI
#include "ffi.h"
#endif
#include "Thread.h"

#ifdef BF_PLATFORM_WINDOWS
#include <fcntl.h>
#include <io.h>
#endif

USING_NS_BF;

static Beefy::StringT<0> gCmdLineString;
bf::System::Runtime::BfRtCallbacks gBfRtCallbacks;
BfRtFlags gBfRtFlags = (BfRtFlags)0;

#ifdef BF_PLATFORM_WINDOWS
DWORD gBfTLSKey = 0;
#else
pthread_key_t gBfTLSKey = 0;
#endif

static int gTestMethodIdx = -1;
static uint32 gTestStartTick = 0;
static bool gTestBreakOnFailure = false;

static BfpFile* gClientPipe = NULL;
static Beefy::String gTestInBuffer;

namespace bf
{
	namespace System
	{
		class Object;
		class Exception;

		//System::Threading::Thread* gMainThread;

		class Internal
		{
		private:
			BFRT_EXPORT static void __BfStaticCtor();
			BFRT_EXPORT static void __BfStaticDtor();

			BFRT_EXPORT static void BfStaticCtor();
			BFRT_EXPORT static void BfStaticDtor();
			BFRT_EXPORT static void Shutdown();
		public:
			BFRT_EXPORT static Object* UnsafeCastToObject(void* inPtr);
			BFRT_EXPORT static void* UnsafeCastToPtr(Object* obj);
			BFRT_EXPORT static void ObjectDynCheck(Object* object, int typeId, bool allowNull);
			BFRT_EXPORT static void ObjectDynCheckFailed(Object* object, int typeId);
			BFRT_EXPORT static void ThrowIndexOutOfRange(intptr stackOffset);
			BFRT_EXPORT static void ThrowObjectNotInitialized(intptr stackOffset);
			BFRT_EXPORT static void FatalError(String* error, intptr stackOffset = 0);
			BFRT_EXPORT static void MemCpy(void* dest, void* src, intptr length);
			BFRT_EXPORT static void MemMove(void* dest, void* src, intptr length);
			BFRT_EXPORT static void MemSet(void* addr, uint8 val, intptr length);
			BFRT_EXPORT static int CStrLen(char* charPtr);
			BFRT_EXPORT static void* Malloc(intptr length);
			BFRT_EXPORT static void Free(void* ptr);
			BFRT_EXPORT static void* VirtualAlloc(intptr size, bool canExecute, bool canWrite);
			BFRT_EXPORT static int64 GetTickCountMicro();
			BFRT_EXPORT static void BfDelegateTargetCheck(void* target);
			BFRT_EXPORT static void* LoadSharedLibrary(char* filePath);
			BFRT_EXPORT static void LoadSharedLibraryInto(char* filePath, void** libDest);
			BFRT_EXPORT static void* GetSharedProcAddress(void* libHandle, char* procName);
			BFRT_EXPORT static void GetSharedProcAddressInto(void* libHandle, char* procName, void** procDest);
			BFRT_EXPORT static char* GetCommandLineArgs();
			BFRT_EXPORT static void BfLog(char* str);
			BFRT_EXPORT static void ProfilerCmd(char* str);
			BFRT_EXPORT static void ReportMemory();

		private:
			BFRT_EXPORT static void Test_Init(char* testData);
			BFRT_EXPORT static void Test_Error(char* error);
			BFRT_EXPORT static void Test_Write(char* str);
			BFRT_EXPORT static int32 Test_Query();
			BFRT_EXPORT static void Test_Finish();
		};

		namespace IO
		{
			class File
			{
			private:
				BFRT_EXPORT static bool Exists(char* fileName);
			};

			class Directory
			{
			private:
				BFRT_EXPORT static bool Exists(char* fileName);
			};
		}

		namespace Diagnostics
		{
			namespace Contracts
			{
				class Contract
				{
				public:
					enum ContractFailureKind : uint8
					{
						ContractFailureKind_Precondition,
						//[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]
						ContractFailureKind_Postcondition,
						//[SuppressMessage("Microsoft.Naming", "CA1704:IdentifiersShouldBeSpelledCorrectly", MessageId = "Postcondition")]
						ContractFailureKind_PostconditionOnException,
						ContractFailureKind_Invariant,
						ContractFailureKind_Assert,
						ContractFailureKind_Assume,
					};

				private:
					BFRT_EXPORT static void ReportFailure(ContractFailureKind failureKind, char* userMessage, int userMessageLen, char* conditionText, int conditionTextLen);
				};
			}

			class Debug
			{
			private:
				BFRT_EXPORT static void Write(char* str, intptr strLen);
			};
		}

		namespace FFI
		{
			enum FFIABI : int32;
			enum FFIResult : int32;

			struct FFIType;

			struct FFILIB
			{
				struct FFICIF;

				BFRT_EXPORT static void* ClosureAlloc(intptr size, void** outFunc);
				BFRT_EXPORT static FFIResult PrepCif(FFICIF* cif, FFIABI abi, int32 nargs, FFIType* rtype, FFIType** argTypes);
				BFRT_EXPORT static void Call(FFICIF* cif, void* funcPtr, void* rvalue, void** args);
			};
		}

		struct Float
		{
		private:
			BFRT_EXPORT static int ToString(float f, char* outStr, bool roundTrip);
		};

		struct Double
		{
		private:
			BFRT_EXPORT static int ToString(double f, char* outStr, bool roundTrip);
		};
	}
}

//#define BF_TRACK_SIZES 1

#if BF_TRACK_SIZES
static int sAllocSizes[1024*1024];
static int sHighestId = 0;
#endif

using namespace bf::System;

#ifndef BF_PLATFORM_WINDOWS

bool IsDebuggerPresent()
{
    return false;
}


#endif

static void TestString(const StringImpl& str);
static void TestReadCmd(Beefy::String& str);

static void Internal_FatalError(const char* error)
{
	if (gBfRtCallbacks.CheckErrorHandler != NULL)
		gBfRtCallbacks.CheckErrorHandler("FatalError", error, NULL, 0);

	if ((gClientPipe != NULL) && (!gTestBreakOnFailure))
	{
		Beefy::String str = ":TestFatal\t";
		str += error;
		str.Replace('\n', '\r');
		str += "\n";
		TestString(str);

 		Beefy::String result;
 		TestReadCmd(result);
		exit(1);
	}
	else
		BfpSystem_FatalError(error, "BEEF FATAL ERROR");
}

extern "C" BFRT_EXPORT int BF_CALLTYPE ftoa(float val, char* str)
{
	return sprintf(str, "%1.9f", val);
}

/*static void* MallocHook(size_t size, const void *caller)
{
	printf("MallocHook\n");
	return NULL;
}*/


/*static int __cdecl HeapHook(int a, size_t b, void* c, void** d)
{
	printf("Heap Hook\n");
	return 0;
}*/

//////////////////////////////////////////////////////////////////////////

//static Beefy::StringT<0> gErrorString;

static const char* volatile gErrorString = NULL;

void SetErrorString(const char* str)
{
	char* newStr = strdup(str);
	while (true)
	{
		const char* prevStr = gErrorString;
		auto result = (void*)BfpSystem_InterlockedCompareExchangePtr((uintptr*)&gErrorString, (uintptr)prevStr, (uintptr)newStr);
		if (result != prevStr)
			continue;
		if (prevStr != NULL)
			free((void*)prevStr);
		break;
	}
}

#define SETUP_ERROR(str, skip) SetErrorString(str); gBfRtCallbacks.DebugMessageData_SetupError(str, skip)

static void GetCrashInfo()
{
	auto errorString = gErrorString;
	if (errorString != NULL)
	{
		Beefy::String debugStr;
		debugStr += "Beef Error: ";
		debugStr += (const char*)errorString;
		BfpSystem_AddCrashInfo(debugStr.c_str());
	}
}

static void NTAPI TlsFreeFunc(void* ptr)
{
	gBfRtCallbacks.Thread_Exiting();
}

void bf::System::Runtime::Init(int version, int flags, BfRtCallbacks* callbacks)
{
	BfpSystemInitFlags sysInitFlags = BfpSystemInitFlag_InstallCrashCatcher;
	if ((flags & 4) != 0)
		sysInitFlags = (BfpSystemInitFlags)(sysInitFlags | BfpSystemInitFlag_SilentCrash);
	BfpSystem_Init(BFP_VERSION, sysInitFlags);
	BfpSystem_AddCrashInfoFunc(GetCrashInfo);

	if (gBfRtCallbacks.Alloc != NULL)
	{
		Internal_FatalError(StrFormat("BeefRT already initialized. Multiple executable modules in the same process cannot dynamically link to the Beef runtime.").c_str());
	}

	if (version != BFRT_VERSION)
	{
        BfpSystem_FatalError(StrFormat("BeefRT build version '%d' does not match requested version '%d'", BFRT_VERSION, version).c_str(), "BEEF FATAL ERROR");
	}

	gBfRtCallbacks = *callbacks;
	gBfRtFlags = (BfRtFlags)flags;

	Beefy::String cmdLine;

	BfpSystemResult result;
	BFP_GETSTR_HELPER(cmdLine, result, BfpSystem_GetCommandLine(__STR, __STRLEN, &result));

	char* cmdLineStr = (char*)cmdLine.c_str();

	//::MessageBoxA(NULL, cmdLineStr, "BFRT", 0);

	char* useCmdLineStr = cmdLineStr;

	if (cmdLineStr[0] != 0)
	{
		bool nameQuoted = cmdLineStr[0] == '\"';

		Beefy::String passedName;
		int i;
		for (i = (nameQuoted ? 1 : 0); cmdLineStr[i] != 0; i++)
		{
			wchar_t c = cmdLineStr[i];

			if (((nameQuoted) && (c == '"')) ||
				((!nameQuoted) && (c == ' ')))
			{
				i++;
				break;
			}
			passedName += cmdLineStr[i];
		}

		useCmdLineStr += i;
		while (*useCmdLineStr == L' ')
			useCmdLineStr++;
	}
	gCmdLineString = useCmdLineStr;

#ifdef BF_PLATFORM_WINDOWS
	gBfTLSKey = FlsAlloc(TlsFreeFunc);
#else
	pthread_key_create(&gBfTLSKey, TlsFreeFunc);
#endif
}

void bf::System::Runtime::SetErrorString(char* errorStr)
{
	::SetErrorString(errorStr);
}

void bf::System::Runtime::AddCrashInfoFunc(void* func)
{
	BfpSystem_AddCrashInfoFunc(*(BfpCrashInfoFunc*)&func);
}

void bf::System::Runtime::SetCrashReportKind(bf::System::Runtime::RtCrashReportKind crashReportKind)
{
	BfpSystem_SetCrashReportKind((BfpCrashReportKind)crashReportKind);
}

//////////////////////////////////////////////////////////////////////////

void Internal::Shutdown()
{
	BfInternalThread::WaitForAllDone();
	if (gBfRtCallbacks.GC_Shutdown != NULL)
		gBfRtCallbacks.GC_Shutdown();
	BfpSystem_Shutdown();
}

void Internal::BfStaticCtor()
{
	__BfStaticCtor();
}

void Internal::__BfStaticCtor()
{

}

void Internal::BfStaticDtor()
{

}

void Internal::__BfStaticDtor()
{

}

Object* Internal::UnsafeCastToObject(void* inPtr)
{
	return (Object*)inPtr;
}

void* Internal::UnsafeCastToPtr(Object* obj)
{
	return (void*)obj;
}

void Internal::ThrowIndexOutOfRange(intptr stackOffset)
{
	if (gClientPipe != NULL)
	{
		if (gTestBreakOnFailure)
		{
			SETUP_ERROR("Index out of range", (int)(2 + stackOffset));
			BF_DEBUG_BREAK();
		}

		Beefy::String str = ":TestFail\tIndex out of range\n";
		TestString(str);
		exit(1);
	}

	if ((stackOffset != -1) && (::IsDebuggerPresent()))
	{
		SETUP_ERROR("Index out of range", (int)(2 + stackOffset));
		BF_DEBUG_BREAK();
	}

	Internal_FatalError("Index out of range");
}

void Internal::ThrowObjectNotInitialized(intptr stackOffset)
{
	if (gClientPipe != NULL)
	{
		if (gTestBreakOnFailure)
		{
			SETUP_ERROR("Object not initialized", (int)(2 + stackOffset));
			BF_DEBUG_BREAK();
		}

		Beefy::String str = ":TestFail\tObject not initialized\n";
		TestString(str);
		exit(1);
	}

	if ((stackOffset != -1) && (::IsDebuggerPresent()))
	{
		SETUP_ERROR("Object not initialized", (int)(2 + stackOffset));
		BF_DEBUG_BREAK();
	}

	Internal_FatalError("Object not initialized");
}

void Internal::FatalError(bf::System::String* error, intptr stackOffset)
{
	if (gClientPipe != NULL)
	{
		if (gTestBreakOnFailure)
		{
			SETUP_ERROR(error->CStr(), (int)(2 + stackOffset));
			BF_DEBUG_BREAK();
		}

		Beefy::String str = ":TestFail\t";
		str += error->CStr();
		str.Replace('\n', '\r');
		str += "\n";
		TestString(str);
		exit(1);
	}

	if ((stackOffset != -1) && (::IsDebuggerPresent()))
	{
		SETUP_ERROR(error->CStr(), (int)(2 + stackOffset));
		BF_DEBUG_BREAK();
	}

    Internal_FatalError(error->CStr());
}

void Internal::MemCpy(void* dest, void* src, intptr length)
{
	memcpy(dest, src, length);
}

void Internal::MemMove(void* dest, void* src, intptr length)
{
	memmove(dest, src, length);
}

void Internal::MemSet(void* addr, uint8 val, intptr length)
{
	memset(addr, val, length);
}

int Internal::CStrLen(char* charPtr)
{
	return (int)strlen(charPtr);
}

void* Internal::Malloc(intptr length)
{
#if BF_USE_STOMP_ALLOC
	return StompAlloc(length);
#elif BF_TRACK_SIZES
	uint8* allocPtr = (uint8*)malloc(length + 16);
	*((int*)allocPtr) = length;
	sAllocSizes[0] += length;
	return allocPtr + 16;
#else
	return malloc(length);
#endif
}

void* Internal::VirtualAlloc(intptr size, bool canExecute, bool canWrite)
{
#ifdef BF_PLATFORM_WINDOWS
    OutputDebugStrF("Performing VirtualAlloc: %d %d %d\n", size, canExecute, canWrite);
    int prot = PAGE_READWRITE;
    if (canExecute && canWrite)
        prot = PAGE_EXECUTE_READWRITE;
    else if (canExecute)
        prot = PAGE_EXECUTE_READ;
    void* ptr = ::VirtualAlloc(NULL, size, MEM_RESERVE, prot);
    return ptr;
#else
    BF_FATAL("Not supported");
    return NULL;
#endif
}

void Internal::Free(void* ptr)
{
#if BF_USE_STOMP_ALLOC
	StompFree(ptr);
#elif BF_TRACK_SIZES
	uint8* allocPtr = ((uint8*)ptr) - 16;
	sAllocSizes[0] -= *((int*)allocPtr);
	free(allocPtr);
#else
	free(ptr);
#endif
}

BFRT_EXPORT int64 Internal::GetTickCountMicro()
{
	return BFGetTickCountMicro();
}

void Internal::BfDelegateTargetCheck(void* target)
{
	if (target != NULL)
	{
		SETUP_ERROR("Attempting pass non-static method reference to extern method", 2);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
	}
}

void* Internal::LoadSharedLibrary(char* libName)
{
	//::MessageBox(NULL, "Hey", "Dude", 0);
    void* libHandle = BfpDynLib_Load(libName);
	if (libHandle == NULL)
	{
		if (gBfRtCallbacks.CheckErrorHandler != NULL)
		{
			if (gBfRtCallbacks.CheckErrorHandler("LoadSharedLibrary", libName, NULL, 0) == 1)
				return NULL;
		}

		Beefy::String errorStr = StrFormat("Failed to load shared library: %s", libName);
		SETUP_ERROR(errorStr.c_str(), 1);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
	}
	return libHandle;
}

void Internal::LoadSharedLibraryInto(char* libName, void** libDest)
{
	if (*libDest == NULL)
		*libDest = LoadSharedLibrary(libName);
}

void* Internal::GetSharedProcAddress(void* libHandle, char* procName)
{
	if (libHandle == NULL)
		return NULL;

    void* procAddr = BfpDynLib_GetProcAddress((BfpDynLib*)libHandle, procName);
	if (procAddr == NULL)
	{
        char libFileName[4096];
        int libFileNameLen = 4096;
        BfpDynLib_GetFilePath((BfpDynLib*)libHandle, libFileName, &libFileNameLen, NULL);

		if (gBfRtCallbacks.CheckErrorHandler != NULL)
		{
			if (gBfRtCallbacks.CheckErrorHandler("GetSharedProcAddress", libFileName, procName, 0) == 1)
				return NULL;
		}

		Beefy::String errorStr = StrFormat("Failed to load shared procedure '%s' from '%s'", procName, libFileName);
		SETUP_ERROR(errorStr.c_str(), 1);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
	}
	return procAddr;
}

void Internal::GetSharedProcAddressInto(void* libHandle, char* procName, void** procDest)
{
	*procDest = GetSharedProcAddress(libHandle, procName);
}

char* Internal::GetCommandLineArgs()
{
	return (char*)gCmdLineString.c_str();
}

void Internal::BfLog(char* str)
{
// 	static int lineNum = 0;
// 	lineNum++;
//
// 	static FILE* fp = fopen("dbg_internal.txt", "wb");
//
// 	Beefy::String aResult = StrFormat("%d ", lineNum) + str;
// 	fwrite(aResult.c_str(), 1, aResult.length(), fp);
// 	fflush(fp);
}

void Internal::ProfilerCmd(char* str)
{
	if (!::IsDebuggerPresent())
		return;

	gBfRtCallbacks.DebugMessageData_SetupProfilerCmd(str);
	BF_DEBUG_BREAK();
}

void Internal::ReportMemory()
{
	int totalMem = 0;
#if BF_TRACK_SIZES
	for (int i = 0; i <= sHighestId; i++)
		totalMem += sAllocSizes[i];
	OutputDebugStrF("Beef Object Memory: %dk\n", totalMem / 1024);
#endif
}

static void TestString(const StringImpl& str)
{
	BfpFileResult fileResult;
	BfpFile_Write(gClientPipe, str.c_str(), str.length(), -1, &fileResult);
	BF_ASSERT_REL(fileResult == BfpFileResult_Ok);
}

static void TestReadCmd(Beefy::String& str)
{
	while (true)
	{
		int crPos = (int)gTestInBuffer.IndexOf('\n');
		if (crPos != -1)
		{
			str = gTestInBuffer.Substring(0, crPos);
			gTestInBuffer.Remove(0, crPos + 1);
			return;
		}

		char data[1024];
		BfpFileResult fileResult;
		int readSize = (int)BfpFile_Read(gClientPipe, data, 1024, -1, &fileResult);
		if ((fileResult == BfpFileResult_Ok) || (fileResult == BfpFileResult_PartialData))
		{
			gTestInBuffer.Append(data, readSize);
		}
		else
		{
			BF_FATAL("Failed to read pipe to test manager");
		}
	}
}

void Internal::Test_Init(char* testData)
{
	BfpSystem_SetCrashReportKind(BfpCrashReportKind_None);

	Beefy::String args = GetCommandLineArgs();

	BfpFileResult fileResult;
	gClientPipe = BfpFile_Create(args.c_str(), BfpFileCreateKind_OpenExisting, (BfpFileCreateFlags)(BfpFileCreateFlag_Read | BfpFileCreateFlag_Write | BfpFileCreateFlag_Pipe), BfpFileAttribute_None, &fileResult);
	if (fileResult != BfpFileResult_Ok)
		BF_FATAL("Test_Init failed to create pipe to test manager");

	Beefy::String outStr;
	outStr += ":TestInit\n";
	outStr += testData;
	outStr += "\n";
	outStr += ":TestBegin\n";
	TestString(outStr);
}

void Internal::Test_Error(char* error)
{
	if (gTestBreakOnFailure)
	{
		SETUP_ERROR(error, 3);
		BF_DEBUG_BREAK();
	}

	if (gClientPipe != NULL)
	{
		Beefy::String str = ":TestFail\t";
		str += error;
		str.Replace('\n', '\r');
		str += "\n";
		TestString(str);
	}
	else
		Internal_FatalError(error);
}

void Internal::Test_Write(char* strPtr)
{
	if (gClientPipe != NULL)
	{
		Beefy::String str = ":TestWrite\t";
		str += strPtr;
		str.Replace('\n', '\r');
		str += "\n";
		TestString(str);
	}
}

int32 Internal::Test_Query()
{
	if (gTestMethodIdx != -1)
	{
		uint32 tickEnd = BfpSystem_TickCount();
		TestString(StrFormat(":TestResult\t%d\n", tickEnd - gTestStartTick));
	}

	TestString(":TestQuery\n");

	Beefy::String result;
	TestReadCmd(result);

	Beefy::String param;
	int tabPos = (int)result.IndexOf('\t');
	if (tabPos != -1)
	{
		param = result.Substring(tabPos + 1);
		result.RemoveToEnd(tabPos);
	}

	if (result == ":TestRun")
	{
		gTestStartTick = BfpSystem_TickCount();
		Beefy::String options;
		int tabPos = (int)param.IndexOf('\t');
		if (tabPos != -1)
		{
			options = param.Substring(tabPos + 1);
			param.RemoveToEnd(tabPos);
		}

		gTestMethodIdx = atoi(param.c_str());
		gTestBreakOnFailure = options.Contains("FailBreak");
		return gTestMethodIdx;
	}
	else if (result == ":TestFinish")
	{
		return -1;
	}
	else
	{
		printf("Command Str: %s\n", result.c_str());
		BF_FATAL("Invalid test command string from test manager");
	}

	return false;
}

void Internal::Test_Finish()
{
	TestString(":TestFinish\n");

	if (gClientPipe != NULL)
	{
		BfpFile_Release(gClientPipe);
		gClientPipe = NULL;
	}
}

///

int GetStackTrace(void **result, int max_depth, int skip_count);

void BfLog(const char* fmt ...);

static const int cMaxStackTraceCount = 1024;
struct PendingAllocState
{
	bool mHasData;
	void* mStackTrace[cMaxStackTraceCount];
	int mStackTraceCount;
	int mMetadataBytes;

	bool IsSmall(intptr curAllocBytes)
	{
		if ((mStackTraceCount > 255) || (mMetadataBytes > 255))
			return false;

		const intptr maxSmallObjectSize = ((intptr)1 << ((sizeof(intptr) - 2) * 8)) - 1;
		if (curAllocBytes <= maxSmallObjectSize)
			return true;

		intptr objBytes = curAllocBytes - mStackTraceCount*sizeof(intptr) - mMetadataBytes;
		return (objBytes < maxSmallObjectSize);
	}
};

void Internal::ObjectDynCheck(bf::System::Object* object, int typeId, bool allowNull)
{
	return;
	if (object == NULL)
	{
		if (allowNull)
			return;
		SETUP_ERROR("Attempting unboxing on null object", 1);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
		return;
	}

	auto result = gBfRtCallbacks.Object_DynamicCastToTypeId(object, typeId);
	if (result == NULL)
	{
		Beefy::String errorStr = "Attempting invalid cast on object";
		//errorStr += StrFormat("\x1LEAK\t0x%@\n   (%s)0x%@\n", object, object->GetTypeName().c_str(), object);
		errorStr += StrFormat("\x1LEAK\t0x%@\n   (%s)0x%@\n", object, "System.Object", object);
		SETUP_ERROR(errorStr.c_str(), 2);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
	}
}

void Internal::ObjectDynCheckFailed(bf::System::Object* object, int typeId)
{
	if (object == NULL)
	{
		SETUP_ERROR("Attempting unboxing on null object", 1);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
		return;
	}

	Beefy::String errorStr = "Attempting invalid cast on object";

	errorStr += StrFormat("\x1LEAK\t0x%@\n   (%s)0x%@\n", object, "System.Object", object);
	SETUP_ERROR(errorStr.c_str(), 2);
	BF_DEBUG_BREAK();
	gBfRtCallbacks.DebugMessageData_Fatal();
}

extern "C" BFRT_EXPORT int PrintF(const char* fmt, ...)
{
	int ret;
	/* Declare a va_list type variable */
	va_list myargs;
	/* Initialise the va_list variable with the ... after fmt */
	va_start(myargs, fmt);
	/* Forward the '...' to vprintf */
	ret = vprintf(fmt, myargs);
	/* Clean up the va_list */
	va_end(myargs);

	return ret;
}

///

using namespace bf::System::Diagnostics::Contracts;

void Contract::ReportFailure(Contract::ContractFailureKind failureKind, char* userMessage, int userMessageLen, char* conditionText, int conditionTextLen)
{
	Beefy::String userMessageStr;
	if (userMessageLen > 0)
		userMessageStr.Reference(userMessage, userMessageLen);
	Beefy::String conditionTextStr;
	if (conditionTextLen > 0)
		conditionTextStr.Reference(conditionText, conditionTextLen);

    Beefy::String errorMsg = "Contract";
    if (failureKind == Contract::ContractFailureKind_Assert)
    	errorMsg += ": Assert failed";
    if (userMessage != NULL)
        errorMsg += Beefy::String(": ") + userMessageStr;
    if (conditionText != NULL)
        errorMsg += Beefy::String(": ") + conditionTextStr;

	if (::IsDebuggerPresent())
	{
		SETUP_ERROR(errorMsg.c_str(), 3);
		BF_DEBUG_BREAK();
		gBfRtCallbacks.DebugMessageData_Fatal();
	}

	Internal_FatalError(errorMsg.c_str());

	return;
}

void bf::System::Diagnostics::Debug::Write(char* str, intptr strLen)
{
	Beefy::String strVal(str, strLen);
	OutputDebugStr(strVal);
}

//////////////////////////////////////////////////////////////////////////

bool IO::File::Exists(char* fileName)
{
    return BfpFile_Exists(fileName);
}

bool IO::Directory::Exists(char* fileName)
{
    return BfpDirectory_Exists(fileName);
}

//////////////////////////////////////////////////////////////////////////

void* bf::System::FFI::FFILIB::ClosureAlloc(intptr size, void** outFunc)
{
#ifndef BF_DISABLE_FFI
	return ffi_closure_alloc(size, outFunc);
#else
	return NULL;
#endif
}

bf::System::FFI::FFIResult bf::System::FFI::FFILIB::PrepCif(bf::System::FFI::FFILIB::FFICIF* cif, bf::System::FFI::FFIABI abi, int32 nargs, bf::System::FFI::FFIType* rtype, bf::System::FFI::FFIType** argTypes)
{
#ifndef BF_DISABLE_FFI
	return (bf::System::FFI::FFIResult)ffi_prep_cif((ffi_cif*)cif, (ffi_abi)abi, nargs, (ffi_type*)rtype, (ffi_type**)argTypes);
#else
	return (bf::System::FFI::FFIResult)0;
#endif
}

void bf::System::FFI::FFILIB::Call(bf::System::FFI::FFILIB::FFICIF* cif, void* funcPtr, void* rvalue, void** args)
{
#ifndef BF_DISABLE_FFI
	ffi_call((ffi_cif*)cif, (void(*)())funcPtr, rvalue, args);
#endif
}

//////////////////////////////////////////////////////////////////////////

static int ToString(float d, char* outStr, bool roundTrip)
{
	if (!roundTrip)
	{
		int digits;
		if (d > 100000)
			digits = 1;
		else if (d > 10000)
			digits = 2;
		else if (d > 1000)
			digits = 3;
		else if (d > 100)
			digits = 4;
		else if (d > 10)
			digits = 5;
		else
			digits = 6;

		sprintf(outStr, "%1.*f", digits, d);
	}
	else
		sprintf(outStr, "%1.9g", d);

	int len = (int)strlen(outStr);
	for (int i = 0; outStr[i] != 0; i++)
	{
		if (outStr[i] == '.')
		{
			int checkC = len - 1;
			while (true)
			{
				char c = outStr[checkC];
				if (c == '.')
				{
					return checkC;
				}
				else if (c == 'e')
				{
					return len;
				}
				else if (c != '0')
				{
					for (int j = i + 1; j <= checkC; j++)
						if (outStr[j] == 'e')
							return len;
					return checkC + 1;
				}
				checkC--;
			}
		}
	}
	if ((len == 3) && (outStr[0] == 'i'))
	{
		strcpy(outStr, "Infinity");
		return 8;
	}
	if ((len == 4) && (outStr[0] == '-') && (outStr[1] == 'i'))
	{
		strcpy(outStr, "-Infinity");
		return 9;
	}
	if ((len == 9) && (outStr[0] == '-') && (outStr[1] == 'n')) //-nan(xxx)
	{
		strcpy(outStr, "NaN");
		return 3;
	}
	return len;
}

static int ToString(double d, char* outStr, bool roundTrip)
{
	if (!roundTrip)
	{
		int digits;
		if (d < 0)
		{
			if (d < -10000000000)
			{
				sprintf(outStr, "%g", d);
			}
			else
			{
				if (d < -1000000000)
					digits = 1;
				else if (d < -100000000)
					digits = 2;
				else if (d < -10000000)
					digits = 3;
				else if (d < -1000000)
					digits = 4;
				else if (d < -100000)
					digits = 5;
				else if (d < -10000)
					digits = 6;
				else if (d < -1000)
					digits = 7;
				else if (d < -100)
					digits = 8;
				else if (d < -10)
					digits = 9;
				else
					digits = 10;

				sprintf(outStr, "%1.*f", digits, d);
			}
		}
		else
		{
			if (d > 10000000000)
			{
				sprintf(outStr, "%g", d);
			}
			else
			{
				if (d > 1000000000)
					digits = 1;
				else if (d > 100000000)
					digits = 2;
				else if (d > 10000000)
					digits = 3;
				else if (d > 1000000)
					digits = 4;
				else if (d > 100000)
					digits = 5;
				else if (d > 10000)
					digits = 6;
				else if (d > 1000)
					digits = 7;
				else if (d > 100)
					digits = 8;
				else if (d > 10)
					digits = 9;
				else
					digits = 10;

				sprintf(outStr, "%1.*f", digits, d);
			}
		}
	}
	else
		sprintf(outStr, "%1.17g", d);

	int len = (int)strlen(outStr);
	for (int i = 0; outStr[i] != 0; i++)
	{
		if (outStr[i] == '.')
		{
			int checkC = len - 1;
			while (true)
			{
				char c = outStr[checkC];
				if (c == '.')
				{
					return checkC;
				}
				else if (c == 'e')
				{
					return len;
				}
				else if (c != '0')
				{
					for (int j = i + 1; j <= checkC; j++)
						if (outStr[j] == 'e')
							return len;
					return checkC + 1;
				}
				checkC--;
			}
		}
	}
	if ((len == 3) && (outStr[0] == 'i'))
	{
		strcpy(outStr, "Infinity");
		return 8;
	}
	if ((len == 4) && (outStr[0] == '-') && (outStr[1] == 'i'))
	{
		strcpy(outStr, "-Infinity");
		return 9;
	}
	if ((len == 9) && (outStr[0] == '-') && (outStr[1] == 'n')) //-nan(xxx)
	{
		strcpy(outStr, "NaN");
		return 3;
	}
	return len;
}

int Float::ToString(float f, char* outStr, bool roundTrip)
{
#ifdef USE_CHARCONV
	auto result = std::to_chars(outStr, outStr + 256, f);
	return (int)(result.ptr - outStr);
#else
	return ::ToString(f, outStr, roundTrip);
#endif
}

int Double::ToString(double d, char* outStr, bool roundTrip)
{
#ifdef USE_CHARCONV
	auto result = std::to_chars(outStr, outStr + 256, d);
	return (int)(result.ptr - outStr);
#else
	return ::ToString(d, outStr, roundTrip);
#endif
}
