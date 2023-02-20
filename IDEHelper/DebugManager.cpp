#include "DebugManager.h"
#include "BeefySysLib/util/CritSect.h"
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/MemReporter.h"
#include "Compiler/BfIRCodeGen.h"
#include "Debugger.h"
#include "DebugVisualizers.h"
#include "RadixMap.h"
#include "Compiler/BfDemangler.h"
#include "llvm/Support/ErrorHandling.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "NetManager.h"
#include "Compiler/CeDebugger.h"

#ifdef BF_PLATFORM_WINDOWS
#include "DbgMiniDump.h"
#endif

#include <iostream>

#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)
#include "llvm/IR/LLVMContext.h"
#include "llvm/IRReader/IRReader.h"
//#include "llvm/Bitcode/ReaderWriter.h"
#pragma warning(pop)

#ifdef BF_PLATFORM_WINDOWS
#include <psapi.h>
#include <shlobj.h>
#endif

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(disable:4190)

#define ENABLE_DBG_32

//#define BF_DBG_32
//#include "WinDebugger.h"
//#undef BF_DBG_32

/*#define BF_DBG_64
#include "WinDebugger.h"
#undef BF_DBG_64*/

int Beefy::sRadixMapCount = 0;
int Beefy::sRootSize = 0;
int Beefy::sMidSize = 0;
int Beefy::sLeafSize = 0;

USING_NS_BF;

DebugManager* Beefy::gDebugManager = NULL;
Debugger* Beefy::gDebugger = NULL;
PerfManager* Beefy::gDbgPerfManager = NULL;

int64 gBfAllocCount = 0;
int64 gBfFreeCount = 0;

static Dictionary<long, int> gBfAllocMap;
static Dictionary<void*, long> gBfAllocAddrMap;

//////////////////////////////////////////////////////////////////////////

DebugManager::DebugManager()
{
	gDbgPerfManager = new PerfManager();

	mDebugVisualizers = new	DebugVisualizers();
	mStepFilterVersion = 0;
	mStepOverExternalFiles = false;

	mDebugger32 = NULL;
	mDebugger64 = NULL;
	mNetManager = new NetManager();
	mNetManager->mDebugManager = this;

	mSymSrvOptions.mCacheDir = "C:\\SymCache";

	mSymSrvOptions.mSymbolServers.Add("C:\\BeefSyms");
	mSymSrvOptions.mSymbolServers.Add("https://msdl.microsoft.com/download/symbols");
	mSymSrvOptions.mSymbolServers.Add("http://wintest.beefy2d.com/symbols/");
	mSymSrvOptions.mSymbolServers.Add("https://chromium-browser-symsrv.commondatastorage.googleapis.com");
	//TODO: Just for testing
	mSymSrvOptions.mSymbolServers.Add("http://127.0.0.1/symbols");

	SetSourceServerCacheDir();
}

DebugManager::~DebugManager()
{
	if ((gDebugger != NULL) && (gDebugger->IsOnDemandDebugger()))
	{
		delete gDebugger;
		gDebugger = NULL;
	}

	delete mNetManager;
	delete mDebugger64;
	delete mDebugger32;
	/*for (auto stepFilter : mStepFilters)
	{
	}*/
	delete mDebugVisualizers;
}

void DebugManager::OutputMessage(const StringImpl& msg)
{
	AutoCrit autoCrit(mCritSect);
	mOutMessages.push_back("msg " + msg);
}

void DebugManager::OutputRawMessage(const StringImpl& msg)
{
	AutoCrit autoCrit(mCritSect);
	mOutMessages.push_back(msg);
}

void DebugManager::SetSourceServerCacheDir()
{
#ifdef BF_PLATFORM_WINDOWS
	AutoCrit autoCrit(mCritSect);

	WCHAR appDataPath[MAX_PATH] = { 0 };
	SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, appDataPath);
	mSymSrvOptions.mSourceServerCacheDir = UTF8Encode(appDataPath);
	mSymSrvOptions.mSourceServerCacheDir += "\\SourceServer";

	if (mSymSrvOptions.mFlags & BfSymSrvFlag_TempCache)
	{
		mSymSrvOptions.mSourceServerCacheDir += "\\temp";
		RecursiveDeleteDirectory(mSymSrvOptions.mSourceServerCacheDir);
	}
#endif
}

//#define CAPTURE_ALLOC_BACKTRACE
//#define CAPTURE_ALLOC_SOURCES

#ifdef CAPTURE_ALLOC_BACKTRACE
const int sNumAllocAddrs = 0x300000;
const int sCaptureDepth = 14;
const int sCaptureOffset = 4;
static intptr gAllocAddrs[sNumAllocAddrs][sCaptureDepth];
#endif

#ifdef CAPTURE_ALLOC_SOURCES
#include <Dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

struct CaptureAllocLocation
{
public:
	char* mSymName;
	int mTotalSize;
	bool mIsEndpoint;
};

struct CaptureAllocEntry
{
public:
	CaptureAllocLocation* mLoc;
	int mAllocSize;
};

std::map<long, CaptureAllocEntry> gBfCaptureSourceAllocMap;
//std::map<void*, CaptureAllocLocation> gBfCaptureAllocLocation;

#define CAPTURE_ALLOC_POOL_SIZE 0x100000
CaptureAllocLocation* gHashCaptureAllocSize[CAPTURE_ALLOC_POOL_SIZE] = { 0 };

static void ReallocEntry(long oldRequest, long newRequest, int newSize)
{
	auto itr = gBfCaptureSourceAllocMap.find(oldRequest);
	if (itr != gBfCaptureSourceAllocMap.end())
	{
		CaptureAllocEntry* entry = &itr->second;
		entry->mLoc->mTotalSize -= entry->mAllocSize;
		entry->mLoc->mTotalSize += newSize;
		entry->mAllocSize = newSize;
		gBfCaptureSourceAllocMap[newRequest] = *entry;
		gBfCaptureSourceAllocMap.erase(itr);
	}
}

static void RemoveAllocEntry(long lRequest)
{
	auto itr = gBfCaptureSourceAllocMap.find(lRequest);
	if (itr != gBfCaptureSourceAllocMap.end())
	{
		CaptureAllocEntry* entry = &itr->second;

		entry->mLoc->mTotalSize -= entry->mAllocSize;
		gBfCaptureSourceAllocMap.erase(itr);
	}
}

//const LOC_HASHES

#endif

static int gBfNumAllocs = 0;

#ifdef BF_PLATFORM_WINDOWS

static bool gBgTrackingAllocs = false; ///// Leave false most of the time
CritSect gBfCritSect;
static bool gInsideAlloc = false;
static int gLastReqId = 0;
static int BfAllocHook(int nAllocType, void *pvData,
	size_t nSize, int nBlockUse, long lRequest,
	const unsigned char * szFileName, int nLine)
{
#ifdef CAPTURE_ALLOC_SOURCES
	if (gInsideAlloc)
		return TRUE;

	gInsideAlloc = true;

	intptr stackTrace[20];
	int traceCount = (int)RtlCaptureStackBackTrace(1, 20, (void**)&stackTrace, 0);

	/*intptr ebpVal;
	__asm
	{
		mov ebpVal, ebp
	}*/

	//intptr ebp = ebpVal;
	//intptr eip = 0;

	static HANDLE hProcess = 0;
	if (hProcess == NULL)
	{
		hProcess = GetCurrentProcess();
		BOOL worked = SymInitialize(hProcess, NULL, TRUE);
	}

	if (nAllocType == _HOOK_ALLOC)
	{
		for (int i = 0; i < traceCount; i++)
		{
			/*__try
			{
				ebp = *((intptr*)ebp + 0);
				if (ebp < 0x100000)
					break;
				eip = *((intptr*)ebp + 1);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				break;
			}*/

			intptr curAddr = stackTrace[i];

			const char* name = "?";

			int hashVal = (curAddr & 0x7FFFFFFF) % CAPTURE_ALLOC_POOL_SIZE;
			if (gHashCaptureAllocSize[hashVal] == NULL)
			{
				//static HPROCESS hProc = GEtProcessH
				char symData[4096];
				DWORD64 disp = 0;
				SYMBOL_INFO* symInfo = (SYMBOL_INFO*)&symData;
				memset(symInfo, 0, sizeof(SYMBOL_INFO));
				symInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
				symInfo->MaxNameLen = sizeof(symData) - sizeof(SYMBOL_INFO);
				bool foundSym = false;
				if (SymFromAddr(hProcess, (DWORD64)curAddr, &disp, symInfo))
				{
					name = symInfo->Name;
					foundSym = true;
				}

				CaptureAllocLocation* captureAllocLoc = new CaptureAllocLocation();
				captureAllocLoc->mSymName = strdup(name);
				captureAllocLoc->mTotalSize = 0;
				captureAllocLoc->mIsEndpoint = (!foundSym) || (strncmp(name, "Beefy::", 7) == 0) || (strncmp(name, "llvm::", 6) == 0);
				if (strstr(name, "operator new") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				if (strstr(name, "::allocateBuckets") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				if (strstr(name, "::grow") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				if (strstr(name, "::DenseMap") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				/*if (strstr(name, "::Allocate") != NULL)
					captureAllocLoc->mIsEndpoint = false;*/
				if (strstr(name, "::Alloc") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				/*if (strstr(name, "::AllocBytes") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				if (strstr(name, "::AllocMemoryBlock") != NULL)
					captureAllocLoc->mIsEndpoint = false;*/
				if (strstr(name, "::GrowPool") != NULL)
					captureAllocLoc->mIsEndpoint = false;

				// Testing COnstantInt::get
				if (strstr(name, "::CreateConst") != NULL)
					captureAllocLoc->mIsEndpoint = false;
				if (strstr(name, "::get") != NULL)
					captureAllocLoc->mIsEndpoint = false;

				if ((captureAllocLoc->mIsEndpoint) && (foundSym))
				{
				}

				gHashCaptureAllocSize[hashVal] = captureAllocLoc;
			}

			CaptureAllocLocation* captureAllocLoc = gHashCaptureAllocSize[hashVal];

			if ((i < 19) && (!captureAllocLoc->mIsEndpoint))
			{
				continue;
			}

			captureAllocLoc->mTotalSize += (int)nSize;

			CaptureAllocEntry entry;
			entry.mAllocSize = (int)nSize;
			entry.mLoc = captureAllocLoc;
			gBfCaptureSourceAllocMap[lRequest] = entry;
			break;
			//if (i >= sCaptureOffset)
			//gAllocAddrs[lRequest][i - sCaptureOffset] = eip;
		}
	}
	else if (nAllocType == _HOOK_REALLOC)
	{
		long oldRequest = ((int*)pvData)[-2];
		ReallocEntry(oldRequest, lRequest, nSize);
	}
	else if (nAllocType == _HOOK_FREE)
	{
		lRequest = ((int*)pvData)[-2];
		RemoveAllocEntry(lRequest);
	}

	gInsideAlloc = false;
#endif

#ifdef CAPTURE_ALLOC_BACKTRACE
	if (lRequest < sNumAllocAddrs)
	{
		gAllocAddrs[lRequest][0] = 1;

		intptr ebpVal;
		__asm
		{
			mov ebpVal, ebp
		}

		intptr ebp = ebpVal;
		intptr eip = 0;

		for (int i = 0; i < sCaptureDepth + sCaptureOffset; i++)
		{
			__try
			{
				ebp = *((intptr*)ebp + 0);
				if (ebp < 0x100000)
					break;
				eip = *((intptr*)ebp + 1);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				break;
			}

			if (i >= sCaptureOffset)
				gAllocAddrs[lRequest][i - sCaptureOffset] = eip;
		}
	}
#else

	if (!gBgTrackingAllocs)
		return TRUE;

	/*AutoCrit critSect(gBfCritSect);

	if (gLastReqId == lRequest)
	return TRUE;
	if (!gInsideAlloc)
	{
	gInsideAlloc = true;
	if (nAllocType == _HOOK_ALLOC)
	{
	gBfNumAllocs++;
	gBfAllocCount += nSize;
	gBfAllocMap[lRequest] = nSize;
	}
	if (nAllocType == _HOOK_FREE)
	{
	lRequest = ((int*)pvData)[-2];

	auto itr = gBfAllocMap.find(lRequest);
	if (itr != gBfAllocMap.end())
	{
	gBfFreeCount += itr->second;
	gBfAllocMap.erase(itr);
	}
	}
	gInsideAlloc = false;
	}

	gLastReqId = lRequest;
	if (szFileName == NULL)
	return TRUE;	*/

	/*char str[1024];
	sprintf(str, "Alloc: %d File: %s Line: %d\n", lRequest, szFileName, nLine);
	OutputDebugStringA(str);*/
#endif
	return TRUE;
}

#endif //BF_PLATFORM_WINDOWS

void BfReportMemory()
{
	BfLogDbg("Used: %.2fM NumAllocs: %d Allocs: %.2fM\n", (gBfAllocCount - gBfFreeCount) / (1024.0 * 1024.0), gBfNumAllocs, gBfAllocCount / (1024.0 * 1024.0));
}

void BfFullReportMemory()
{
	/*OutputDebugStrF("Testing OOB\n");
	char* str = new char[12];
	delete str;
	char c = str[1];*/

	if (gBfParserCache != NULL)
	{
		MemReporter memReporter;
		memReporter.BeginSection("ParserCache");
		gBfParserCache->ReportMemory(&memReporter);
		memReporter.EndSection();
		memReporter.Report();
	}

	OutputDebugStrF("Used: %.2fM NumAllocs: %d Allocs: %.2fM\n", (gBfAllocCount - gBfFreeCount) / (1024.0 * 1024.0), gBfNumAllocs, gBfAllocCount / (1024.0 * 1024.0));
	OutputDebugStrF("ChunkedDataBuffer allocated blocks: %d\n", ChunkedDataBuffer::sBlocksAllocated);

	if (gDebugManager != NULL)
	{
		MemReporter memReporter;
		if (gDebugManager->mDebugger32 != NULL)
		{
			memReporter.BeginSection("Debugger32");
			gDebugManager->mDebugger32->ReportMemory(&memReporter);
			memReporter.EndSection();
		}
		memReporter.BeginSection("Debugger64");
		gDebugManager->mDebugger64->ReportMemory(&memReporter);
		memReporter.EndSection();
		memReporter.Report();
	}

	BpDump();

#ifdef CAPTURE_ALLOC_SOURCES
	int memTotal = 0;
	std::map<String, int> byNameMap;
	for (int i = 0; i < CAPTURE_ALLOC_POOL_SIZE; i++)
	{
		CaptureAllocLocation* allocLoc = gHashCaptureAllocSize[i];
		if ((allocLoc != NULL) && (allocLoc->mTotalSize > 0))
		{
			auto itr = byNameMap.insert(std::map<String, int>::value_type(allocLoc->mSymName, 0));
			itr.first->second += allocLoc->mTotalSize;
			memTotal += allocLoc->mTotalSize;
		}
	}

	std::multimap<int, String> bySizeMap;

	for (auto kv : byNameMap)
	{
		//OutputDebugStrF("%dk %s\n", (kv.second + 1023) / 1024, kv.first.c_str());
		bySizeMap.insert(std::multimap<int, String>::value_type(-kv.second, kv.first));
	}

	for (auto kv : bySizeMap)
	{
		OutputDebugStrF("%dk %s\n", (-kv.first + 1023) / 1024, kv.second.c_str());
	}

	OutputDebugStrF("Total %dk\n", memTotal / 1024);
#endif
}

struct _CrtMemBlockHeader
{
	_CrtMemBlockHeader* _block_header_next;
	_CrtMemBlockHeader* _block_header_prev;
	char const*         _file_name;
	int                 _line_number;

	int                 _block_use;
	size_t              _data_size;

	long                _request_number;
	//unsigned char       _gap[no_mans_land_size];

	// Followed by:
	// unsigned char    _data[_data_size];
	// unsigned char    _another_gap[no_mans_land_size];
};

//static _CrtMemBlockHeader* __acrt_first_block;
//static _CrtMemBlockHeader* __acrt_last_block;

void ShowMemoryUsage()
{
#ifdef BF_PLATFORM_WINDOWS
	PROCESS_MEMORY_COUNTERS processMemCounters;
	processMemCounters.cb = sizeof(PROCESS_MEMORY_COUNTERS);
	GetProcessMemoryInfo(GetCurrentProcess(), &processMemCounters, sizeof(PROCESS_MEMORY_COUNTERS));
	OutputDebugStrF("WorkingSet : %dk\n", (int)(processMemCounters.WorkingSetSize / 1024));
	OutputDebugStrF("VirtualMem : %dk\n", (int)(processMemCounters.PagefileUsage/1024));

	static bool hasCheckpoint = true;
	_CrtMemState memState;
	_CrtMemCheckpoint(&memState);
	//OutputDebugStrF("Crt Size: %dk\n", (int)(memState.lTotalCount / 1024));

	char* names[6] = { "_FREE_BLOCK", "_NORMAL_BLOCK", "_CRT_BLOCK", "_IGNORE_BLOCK", "_CLIENT_BLOCK", "_MAX_BLOCKS" };
	for (int i = 0; i < 5; i++)
	{
		OutputDebugStrF("%s : %d %dk\n", names[i], memState.lCounts[i], memState.lSizes[i] / 1024);
	}

#ifdef _DEBUG
// 	int64 totalCrtSize = 0;
// 	int64 totalUseCrtSize = 0;
// 	_CrtMemBlockHeader* blockPtr = memState.pBlockHeader;
// 	while (blockPtr != NULL)
// 	{
// 		totalCrtSize += blockPtr->_data_size;
// 		if (blockPtr->_block_use != _FREE_BLOCK)
// 			totalUseCrtSize += blockPtr->_data_size;
// 		blockPtr = blockPtr->_block_header_next;
// 	}
// 	OutputDebugStrF("Crt Size: %dk Used: %dk\n", (int)(totalCrtSize / 1024), (int)(totalUseCrtSize / 1024));
#endif

	_HEAPINFO heapInfo = {0};
	int64 heapSize = 0;

	int heapStatus;
	while ((heapStatus = _heapwalk(&heapInfo)) == _HEAPOK)
	{
		heapSize += (int64)heapInfo._size;
	}
	OutputDebugStrF("WALKED HEAP SIZE: %dk\n", heapSize / 1024);

	//_CrtMemDumpStatistics(&memState);
#endif
}

/*void* TestHeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes)
{
	return HeapAlloc(hHeap, dwFlags, dwBytes);
}*/

static void BfFatalErrorHandler(void *user_data, const std::string& reason, bool gen_crash_diag)
{
	BF_FATAL(reason.c_str());
	OutputDebugStrF("LLVM ERROR: %s\n", reason.c_str());
}

#ifdef BF_PLATFORM_WINDOWS
BOOL WINAPI DllMain(
	HANDLE  hDllHandle,
	DWORD   dwReason,
	LPVOID  lpreserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		BpInit("127.0.0.1", "Beef IDE");
		BpSetThreadName("Main");
		BfpThread_SetName(NULL, "Main", NULL);

		llvm::install_fatal_error_handler(BfFatalErrorHandler, NULL);

		//_CRTDBG_CHECK_EVERY_16_DF
		//_CRTDBG_CHECK_ALWAYS_DF
		//_CRTDBG_DELAY_FREE_MEM_DF

		//_CrtSetDbgFlag (_CRTDBG_ALLOC_MEM_DF /*| _CRTDBG_CHECK_ALWAYS_DF*/);
		_CrtSetAllocHook(BfAllocHook);
	}

	if (dwReason == -123)
	{
		BpDump();
	}

	return TRUE;
}
#endif //BF_PLATFORM_WINDOWS

//////

void SleepTest()
{
    BfpThread_Sleep(3000);
}

void WdAllocTest();

namespace BeefyDbg64
{
	class WinDebugger;
	void TestPDB(const StringImpl& fileName, WinDebugger* debugger);
}

#ifdef BF_PLATFORM_WINDOWS
static _CrtMemState gStartMemCheckpoint;
#endif
BF_EXPORT void BF_CALLTYPE Debugger_Create()
{
	//TODO: Very slow, remove
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF);
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_EVERY_16_DF);
	//TODO: _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF /*| _CRTDBG_CHECK_EVERY_16_DF*/);
	//_CrtSetAllocHook(BfAllocHook);

#ifdef BF_PLATFORM_WINDOWS
	_CrtMemCheckpoint(&gStartMemCheckpoint);
#endif
	//_CrtSetBreakAlloc(621);

	gDebugManager = new DebugManager();
#ifdef ENABLE_DBG_32
	gDebugManager->mDebugger32 = CreateDebugger32(gDebugManager, NULL);
#else
	gDebugManager->mDebugger32 = NULL;
#endif

#ifdef BF32
	gDebugManager->mDebugger64 = NULL;
#else
	gDebugManager->mDebugger64 = CreateDebugger64(gDebugManager, NULL);
#endif

#ifdef BF_PLATFORM_WINDOWS
	::AllowSetForegroundWindow(ASFW_ANY);
#endif

	//BeefyDbg64::TestPDB("C:/Beef/IDE/dist/IDEHelper64_d.pdb", (BeefyDbg64::WinDebugger*)gDebugManager->mDebugger64);
}

BF_EXPORT void BF_CALLTYPE Debugger_SetCallbacks(void* callback)
{
}

BF_EXPORT void BF_CALLTYPE Debugger_FullReportMemory()
{
	//WdAllocTest();
	ShowMemoryUsage();
	BfFullReportMemory();
}

BF_EXPORT void BF_CALLTYPE Debugger_Delete()
{
	delete gDebugManager;
	gDebugManager = NULL;

	delete gPerfManager;
	gPerfManager = NULL;

	//OutputDebugStrF("Deleting Debugger\n");
	//BfReportMemory();

#ifdef BF_PLATFORM_WINDOWS
	gBgTrackingAllocs = false;
#endif
}

BF_EXPORT void BF_CALLTYPE IDEHelper_ProgramStart()
{
	BfIRCodeGen::StaticInit();
}

BF_EXPORT void BF_CALLTYPE IDEHelper_ProgramDone()
{
	//TODO:
	//::MessageBoxA(NULL, "Done", "Done", MB_OK);

	BF_ASSERT(gDebugger == NULL);

	//ShowMemoryUsage();
	//BfFullReportMemory();

#ifdef CAPTURE_ALLOC_SOURCES
	gInsideAlloc = true;
	for (int i = 0; i < CAPTURE_ALLOC_POOL_SIZE; i++)
	{
		if (gHashCaptureAllocSize[i] != NULL)
		{
			free(gHashCaptureAllocSize[i]->mSymName);
			delete gHashCaptureAllocSize[i];
			gHashCaptureAllocSize[i] = NULL;
		}
	}
	gBfCaptureSourceAllocMap.clear();
#endif

#ifdef BF_PLATFORM_WINDOWS
	_CrtMemDumpAllObjectsSince(&gStartMemCheckpoint);

	/*_CrtMemState curMemCheckpoint;
	_CrtMemCheckpoint(&curMemCheckpoint);

	_CrtMemState memDiff;
	if (_CrtMemDifference(&memDiff, &gStartMemCheckpoint, &curMemCheckpoint))
	_CrtMemDumpStatistics(&memDiff);*/

	_CrtMemState curMemCheckpoint = { 0 };
	_CrtMemCheckpoint(&curMemCheckpoint);
	OutputDebugStrF("Heap memory usage: %dk\n", curMemCheckpoint.lTotalCount / 1024);
#endif //BF_PLATFORM_WINDOWS

	BpShutdown();
}

BF_EXPORT int BF_CALLTYPE Debugger_GetAddrSize()
{
	if (gDebugger == NULL)
		return 0;
	return gDebugger->GetAddrSize();
}

BF_EXPORT bool BF_CALLTYPE Debugger_OpenFile(const char* launchPath, const char* targetPath, const char* args, const char* workingDir, void* envBlockPtr, int envBlockSize, bool hotSwapEnabled)
{
	BF_ASSERT(gDebugger == NULL);

	if (!FileExists(launchPath))
	{
		gDebugManager->mOutMessages.push_back(StrFormat("error Unable to locate specified launch target '%s'", launchPath));
		return false;
	}

	DebuggerResult debuggerResult = DebuggerResult_Ok;
	if ((gDebugManager->mDebugger64 != NULL) && (gDebugManager->mDebugger64->CanOpen(launchPath, &debuggerResult)))
		gDebugger = gDebugManager->mDebugger64;
	else
		gDebugger = gDebugManager->mDebugger32;

	if (gDebugger == NULL)
	{
		if (debuggerResult == DebuggerResult_WrongBitSize)
			gDebugManager->mOutMessages.push_back(StrFormat("error The file 32-bit file '%s' cannot be debugged because 32-bit debugger has been disabled", launchPath));
		return false;
	}

	Array<uint8> envBlock;
	if (envBlockPtr != NULL)
	{
		if (envBlockSize != 0)
			envBlock.Insert(0, (uint8*)envBlockPtr, envBlockSize);
	}

	gDebugger->OpenFile(launchPath, targetPath, args, workingDir, envBlock, hotSwapEnabled);
	return true;
}

BF_EXPORT bool BF_CALLTYPE Debugger_ComptimeAttach(void* bfCompiler)
{
	gDebugger = new CeDebugger(gDebugManager, (BfCompiler*)bfCompiler);
	return true;
}

BF_EXPORT void BF_CALLTYPE Debugger_SetSymSrvOptions(const char* symCacheDir, const char* symSrvStr, int flags)
{
	Array<String> symServers;

	const char* startStr = symSrvStr;
	for (const char* cPtr = symSrvStr; true; cPtr++)
	{
		if ((*cPtr == '\n') || (*cPtr == 0))
		{
			String symStr = String(startStr, cPtr - startStr);
			symStr.Trim();
			if (symStr.EndsWith('/'))
				symStr.Remove((int)symStr.length() - 1, 1);
			if (!symStr.IsEmpty())
				symServers.Add(symStr);
			startStr = cPtr;
		}

		if (*cPtr == 0)
			break;
	}

	AutoCrit autoCrit(gDebugManager->mCritSect);

	gDebugManager->mSymSrvOptions.mCacheDir = symCacheDir;
	gDebugManager->mSymSrvOptions.mSymbolServers = symServers;
	gDebugManager->mSymSrvOptions.mFlags = (BfSymSrvFlags)flags;

	gDebugManager->mSymSrvOptions.mCacheDir.Trim();
	if (gDebugManager->mSymSrvOptions.mCacheDir.IsEmpty())
		gDebugManager->mSymSrvOptions.mFlags = BfSymSrvFlag_Disable;

	if (flags & BfSymSrvFlag_TempCache)
	{
		if (!gDebugManager->mSymSrvOptions.mCacheDir.IsEmpty())
		{
			gDebugManager->mSymSrvOptions.mCacheDir.Append("\\temp");
			RecursiveDeleteDirectory(gDebugManager->mSymSrvOptions.mCacheDir);
		}
	}

	gDebugManager->SetSourceServerCacheDir();
}

BF_EXPORT void BF_CALLTYPE Debugger_SetSourcePathRemap(const char* remapStr)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	gDebugManager->mSourcePathRemap.Clear();

	const char* startStr = remapStr;
	for (const char* cPtr = remapStr; true; cPtr++)
	{
		if ((*cPtr == '\n') || (*cPtr == 0))
		{
			String remapStr = String(startStr, cPtr - startStr);
			remapStr.Trim();

			int eqPos = (int)remapStr.IndexOf('=');
			if (eqPos != -1)
			{
				auto keyStr = remapStr.Substring(0, eqPos);
				keyStr.Trim();
				auto valueStr = remapStr.Substring(eqPos + 1);
				valueStr.Trim();
				gDebugManager->mSourcePathRemap[keyStr] = valueStr;
			}

			startStr = cPtr;
		}

		if (*cPtr == 0)
			break;
	}
}

BF_EXPORT bool BF_CALLTYPE Debugger_OpenMiniDump(const char* fileName)
{
#ifdef BF_PLATFORM_WINDOWS
	DbgMiniDump* dbgMiniDump = new DbgMiniDump();
	bool result = dbgMiniDump->StartLoad(fileName);
	if (!result)
	{
		delete dbgMiniDump;
		return false;
	}

	if (dbgMiniDump->GetTargetBitCount() == 32)
		gDebugger = CreateDebugger32(gDebugManager, dbgMiniDump);
	else
		gDebugger = CreateDebugger64(gDebugManager, dbgMiniDump);

	return result;
#else //BF_PLATFORM_WINDOWS
	return false;
#endif
}

BF_EXPORT bool BF_CALLTYPE Debugger_Attach(int processId, BfDbgAttachFlags attachFlags)
{
	BF_ASSERT(gDebugger == NULL);

	if (gDebugManager->mDebugger64->Attach(processId, attachFlags))
	{
		gDebugger = gDebugManager->mDebugger64;
		return true;
	}

	if (gDebugManager->mDebugger32->Attach(processId, attachFlags))
	{
		gDebugger = gDebugManager->mDebugger32;
		return true;
	}

	return false;
}

BF_EXPORT void BF_CALLTYPE Debugger_Run()
{
	gDebugger->Run();
}

BF_EXPORT bool BF_CALLTYPE Debugger_HotLoad(const char* fileNamesStr, int hotIdx)
{
	//DbgModule* dwarf = new DbgModule(gDebugger);
	//dwarf->ReadPE(fileName);

	Array<String> fileNames;
	const char* curPtr = fileNamesStr;
	for (int i = 0; true; i++)
	{
		if ((fileNamesStr[i] == '\0') || (fileNamesStr[i] == '\n'))
		{
			String curFileName = String(curPtr, fileNamesStr + i);

			if ((curFileName.IndexOf("/vdata.") != -1) || (curFileName.IndexOf("\\vdata.") != -1))
			{
				// Do vdata first - so new data and special functions don't have to be deferred resolved
				fileNames.Insert(0, curFileName);
			}
			else
				fileNames.Add(curFileName);
			curPtr = fileNamesStr + i + 1;
		}

		if (fileNamesStr[i] == '\0')
			break;
	}

	gDebugger->HotLoad(fileNames, hotIdx);

	return true;
}

BF_EXPORT bool BF_CALLTYPE Debugger_LoadDebugVisualizers(const char* fileName)
{
	String fn = fileName;
	bool worked = false;
	worked = gDebugManager->mDebugVisualizers->Load(fileName);
	if (!gDebugManager->mDebugVisualizers->mErrorString.empty())
	{
		gDebugManager->mOutMessages.push_back(StrFormat("msg ERROR: %s\n", gDebugManager->mDebugVisualizers->mErrorString.c_str()));
	}

// 	{
// 		BF_FATAL(gDebugManager->mDebugVisualizers->mErrorString.c_str());
// 	}
	return worked;
}

BF_EXPORT void BF_CALLTYPE Debugger_StopDebugging()
{
	gDebugger->StopDebugging();
}

BF_EXPORT void BF_CALLTYPE Debugger_Terminate()
{
	gDebugger->Terminate();
}

BF_EXPORT void BF_CALLTYPE Debugger_Detach()
{
	gDebugManager->mNetManager->CancelAll();
	gDebugger->Detach();
	if (gDebugger->IsOnDemandDebugger())
		delete gDebugger;
	gDebugger = NULL;
	gDebugManager->mNetManager->Clear();
}

BF_EXPORT Breakpoint* BF_CALLTYPE Debugger_CreateBreakpoint(const char* fileName, int lineNum, int wantColumn, int instrOffset)
{
	return gDebugger->CreateBreakpoint(fileName, lineNum, wantColumn, instrOffset);
}

BF_EXPORT Breakpoint* BF_CALLTYPE Debugger_CreateMemoryBreakpoint(intptr address, int byteCount)
{
	return gDebugger->CreateMemoryBreakpoint(address, byteCount);
}

BF_EXPORT Breakpoint* BF_CALLTYPE Debugger_CreateSymbolBreakpoint(const char* symbolName)
{
	return gDebugger->CreateSymbolBreakpoint(symbolName);
}

BF_EXPORT Breakpoint* BF_CALLTYPE Debugger_CreateAddressBreakpoint(intptr address)
{
	return gDebugger->CreateAddressBreakpoint(address);
}

BF_EXPORT Breakpoint* BF_CALLTYPE Debugger_GetActiveBreakpoint()
{
	return gDebugger->GetActiveBreakpoint();
}

BF_EXPORT void BF_CALLTYPE Breakpoint_Delete(Breakpoint* wdBreakpoint)
{
	gDebugger->DeleteBreakpoint(wdBreakpoint);
}

BF_EXPORT void BF_CALLTYPE Breakpoint_Check(Breakpoint* breakpoint)
{
	gDebugger->CheckBreakpoint(breakpoint);
}

BF_EXPORT int BF_CALLTYPE Breakpoint_GetPendingHotBindIdx(Breakpoint* breakpoint)
{
	return breakpoint->mPendingHotBindIdx;
}

BF_EXPORT void BF_CALLTYPE Breakpoint_HotBindBreakpoint(Breakpoint* breakpoint, int lineNum, int hotIdx)
{
	gDebugger->HotBindBreakpoint(breakpoint, lineNum, hotIdx);
}

BF_EXPORT void BF_CALLTYPE Breakpoint_SetThreadId(Breakpoint* breakpoint, intptr threadId)
{
	BfLogDbg("Breakpoint %p set ThreadId=%d\n", breakpoint, threadId);
	breakpoint->mThreadId = threadId;
	gDebugger->CheckBreakpoint(breakpoint);
}

BF_EXPORT int BF_CALLTYPE Breakpoint_GetHitCount(Breakpoint* breapoint)
{
	return breapoint->mHitCount;
}

BF_EXPORT void BF_CALLTYPE Breakpoint_ClearHitCount(Breakpoint* breapoint)
{
	breapoint->mHitCount = 0;
}

BF_EXPORT void BF_CALLTYPE Breakpoint_SetHitCountTarget(Breakpoint* breakpoint, int targetHitCount, DbgHitCountBreakKind breakKind)
{
	breakpoint->mTargetHitCount = targetHitCount;
	breakpoint->mHitCountBreakKind = breakKind;
}

BF_EXPORT void BF_CALLTYPE Breakpoint_SetCondition(Breakpoint* wdBreakpoint, const char* condition)
{
	gDebugger->SetBreakpointCondition(wdBreakpoint, condition);
}

BF_EXPORT void BF_CALLTYPE Breakpoint_SetLogging(Breakpoint* wdBreakpoint, const char* logging, bool breakAfterLogging)
{
	gDebugger->SetBreakpointLogging(wdBreakpoint, logging, breakAfterLogging);
}

BF_EXPORT uintptr BF_CALLTYPE Breakpoint_GetAddress(Breakpoint* wdBreakpoint, Breakpoint** outLinkedSibling)
{
	if (outLinkedSibling != NULL)
		*outLinkedSibling = wdBreakpoint->mLinkedSibling;
	return gDebugger->GetBreakpointAddr(wdBreakpoint);
}

BF_EXPORT bool BF_CALLTYPE Breakpoint_IsMemoryBreakpointBound(Breakpoint* wdBreakpoint)
{
	return wdBreakpoint->IsMemoryBreakpointBound();
}

BF_EXPORT intptr BF_CALLTYPE Breakpoint_GetLineNum(Breakpoint* wdBreakpoint)
{
	return wdBreakpoint->mLineNum;
}

BF_EXPORT void BF_CALLTYPE Breakpoint_Move(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	gDebugger->MoveBreakpoint(breakpoint, lineNum, wantColumn, rebindNow);
}

BF_EXPORT void BF_CALLTYPE Breakpoint_MoveMemoryBreakpoint(Breakpoint* breakpoint, intptr addr, int byteCount)
{
	gDebugger->MoveMemoryBreakpoint(breakpoint, addr, byteCount);
}

BF_EXPORT void BF_CALLTYPE Breakpoint_Disable(Breakpoint* wdBreakpoint)
{
	gDebugger->DisableBreakpoint(wdBreakpoint);
}

BF_EXPORT void BF_CALLTYPE Debugger_CreateStepFilter(const char* filter, bool isGlobal, BfStepFilterKind filterKind)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	StepFilter stepFilter;
	stepFilter.mFilterKind = filterKind;
	gDebugManager->mStepFilters[filter] = stepFilter;
	gDebugManager->mStepFilterVersion++;
}

BF_EXPORT void BF_CALLTYPE StepFilter_Delete(const char* filter)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	bool didRemove = gDebugManager->mStepFilters.Remove(filter);
	BF_ASSERT(didRemove);

	gDebugManager->mStepFilterVersion++;
}

BF_EXPORT int BF_CALLTYPE Debugger_GetRunState()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	if (gDebugger == NULL)
		return RunState_NotStarted;
	return gDebugger->mRunState;
}

BF_EXPORT bool Debugger_HasLoadedTargetBinary()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	if (gDebugger == NULL)
		return false;
	return gDebugger->HasLoadedTargetBinary();
}

BF_EXPORT bool BF_CALLTYPE Debugger_HasPendingDebugLoads()
{
	if (gDebugger == NULL)
		return false;
	return gDebugger->HasPendingDebugLoads();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_PopMessage()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	if (gDebugManager->mOutMessages.size() == 0)
		return NULL;
	String& outString = *gTLStrReturn.Get();
	outString = gDebugManager->mOutMessages.front();
	gDebugManager->mOutMessages.pop_front();
	//gDebugManager->mOutMessages.erase(gDebugManager->mOutMessages.begin());
	return outString.c_str();
}

BF_EXPORT bool BF_CALLTYPE Debugger_HasMessages()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	return gDebugManager->mOutMessages.size() != 0;
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetCurrentException()
{
	/*outString = StrFormat("Exception at 0x%p, exception code 0x%08X",
	gDebugger->mCurException.ExceptionAddress, gDebugger->mCurException.ExceptionCode);*/

	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetCurrentException();
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_BreakAll()
{
	if (gDebugger != NULL)
		gDebugger->BreakAll();
}

BF_EXPORT void BF_CALLTYPE Debugger_Continue()
{
    BfLogDbg("Debugger_Continue\n");
	gDebugger->ContinueDebugEvent();
}

BF_EXPORT void BF_CALLTYPE Debugger_StepInto(bool inAssembly)
{
	gDebugger->StepInto(inAssembly);
}

BF_EXPORT void BF_CALLTYPE Debugger_StepIntoSpecific(intptr addr)
{
	gDebugger->StepIntoSpecific(addr);
}

BF_EXPORT void BF_CALLTYPE Debugger_StepOver(bool inAssembly)
{
	gDebugger->StepOver(inAssembly);
}

BF_EXPORT void BF_CALLTYPE Debugger_StepOut(bool inAssembly)
{
	gDebugger->StepOut(inAssembly);
}

BF_EXPORT void BF_CALLTYPE Debugger_SetNextStatement(bool inAssembly, const char* fileName, int64 wantLineNumOrAsmAddr, int wantColumn)
{
	if (fileName == NULL)
		fileName = "";
	gDebugger->SetNextStatement(inAssembly, fileName, wantLineNumOrAsmAddr, wantColumn);
}

BF_EXPORT void BF_CALLTYPE Debugger_Update()
{
	if (gDebugger != NULL)
		gDebugger->Update();
}

BF_EXPORT void BF_CALLTYPE Debugger_SetDisplayTypes(const char* referenceId, const char* formatStr, int8 intDisplayType, int8 mmDisplayType, int8 floatDisplayType)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	DwDisplayInfo* displayInfo = NULL;
	if (referenceId == NULL)
		displayInfo = &gDebugManager->mDefaultDisplayInfo;
	else
		gDebugManager->mDisplayInfos.TryAdd(referenceId, NULL, &displayInfo);

	if (formatStr != NULL)
		displayInfo->mFormatStr = formatStr;
	displayInfo->mIntDisplayType = (DwIntDisplayType)intDisplayType;
	displayInfo->mMmDisplayType = (DwMmDisplayType)mmDisplayType;
	displayInfo->mFloatDisplayType = (DwFloatDisplayType)floatDisplayType;

	if ((referenceId != NULL) &&
		(displayInfo->mFormatStr.IsEmpty()) &&
		(displayInfo->mIntDisplayType == DwIntDisplayType_Default) &&
		(displayInfo->mMmDisplayType == DwMmDisplayType_Default) &&
		(displayInfo->mFloatDisplayType == DwFloatDisplayType_Default))
	{
		gDebugManager->mDisplayInfos.Remove(referenceId);
	}
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetDisplayTypes(const char* referenceId, int8* intDisplayType, int8* mmDisplayType, int8* floatDisplayType, bool* foundSpecific)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	*foundSpecific = false;
	DwDisplayInfo* displayInfo = &gDebugManager->mDefaultDisplayInfo;
	if (referenceId != NULL)
	{
		/*auto itr = gDebugManager->mDisplayInfos.find(referenceId);
		if (itr != gDebugManager->mDisplayInfos.end())
		{
			displayInfo = &itr->second;
			foundSpecific = true;
		}*/

		if (gDebugManager->mDisplayInfos.TryGetValue(referenceId, &displayInfo))
		{
			*foundSpecific = true;
		}
	}

	*intDisplayType = (int8)displayInfo->mIntDisplayType;
	*mmDisplayType = (int8)displayInfo->mMmDisplayType;
	*floatDisplayType = (int8)displayInfo->mFloatDisplayType;
	return displayInfo->mFormatStr.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetDisplayTypeNames()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	String& outString = *gTLStrReturn.Get();
	outString.clear();
	for (auto& displayInfoEntry : gDebugManager->mDisplayInfos)
	{
		outString += displayInfoEntry.mKey + "\n";
	}
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_EvaluateContinue()
{
	auto debugger = gDebugger;

	String& outString = *gTLStrReturn.Get();
	outString = debugger->EvaluateContinue();
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_EvaluateContinueKeep()
{
	auto debugger = gDebugger;
	debugger->EvaluateContinueKeep();
}

BF_EXPORT StringView BF_CALLTYPE Debugger_Evaluate(const char* expr, int callStackIdx, int cursorPos, int32 language, uint16 expressionFlags)
{
	auto debugger = gDebugger;

	if (debugger == NULL)
		debugger = gDebugManager->mDebugger64;

	String& outString = *gTLStrReturn.Get();
	outString.clear();
	outString = debugger->Evaluate(expr, callStackIdx, cursorPos, language, (DwEvalExpressionFlags)expressionFlags);
#ifdef BF_WANTS_LOG_DBG
	{
		int crPos = (int)outString.IndexOf('\n');
		if (crPos != -1)
			BfLogDbg("Debugger_Evaluate Result=%s\n", outString.Substring(0, crPos).c_str());
		else
			BfLogDbg("Debugger_Evaluate Result=%s\n", outString.c_str());
	}
#endif
	return outString;
}

BF_EXPORT const char* BF_CALLTYPE Debugger_EvaluateToAddress(const char* expr, int callStackIdx, int cursorPos)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	if (gDebugger != NULL)
		outString = gDebugger->EvaluateToAddress(expr, callStackIdx, cursorPos);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_EvaluateAtAddress(const char* expr, intptr atAddr, int cursorPos)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	if (gDebugger != NULL)
		outString = gDebugger->EvaluateAtAddress(expr, atAddr, cursorPos);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetAutoExpressions(callStackIdx, memoryRangeStart, memoryRangeLen);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetAutoLocals(int callStackIdx, bool showRegs)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetAutoLocals(callStackIdx, showRegs);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_CompactChildExpression(const char* expr, const char* parentExpr, int callStackIdx)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->CompactChildExpression(expr, parentExpr, callStackIdx);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetCollectionContinuation(const char* continuationData, int callStackIdx, int count)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetCollectionContinuation(continuationData, callStackIdx, count);
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_ForegroundTarget()
{
	gDebugger->ForegroundTarget();

	//BOOL worked = EnumThreadWindows(gDebugger->mProcessInfo.dwThreadId, WdEnumWindowsProc, 0);
	//BF_ASSERT(worked);
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetProcessInfo()
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetProcessInfo();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetThreadInfo()
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetThreadInfo();
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_SetActiveThread(int threadId)
{
	gDebugger->SetActiveThread(threadId);
}

BF_EXPORT int BF_CALLTYPE Debugger_GetActiveThread()
{
	return gDebugger->GetActiveThread();
}

BF_EXPORT void BF_CALLTYPE Debugger_FreezeThread(int threadId)
{
	gDebugger->FreezeThread(threadId);
}

BF_EXPORT void BF_CALLTYPE Debugger_ThawThread(int threadId)
{
	gDebugger->ThawThread(threadId);
}

BF_EXPORT bool BF_CALLTYPE Debugger_IsActiveThreadWaiting()
{
	return gDebugger->IsActiveThreadWaiting();
}

BF_EXPORT void BF_CALLTYPE CallStack_Update()
{
	gDebugger->UpdateCallStack();
}

BF_EXPORT void BF_CALLTYPE CallStack_Rehup()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	gDebugger->ClearCallStack();
}

BF_EXPORT int BF_CALLTYPE CallStack_GetCount()
{
	return gDebugger->GetCallStackCount();
}

BF_EXPORT int BF_CALLTYPE CallStack_GetRequestedStackFrameIdx()
{
	return gDebugger->GetRequestedStackFrameIdx();
}

BF_EXPORT int BF_CALLTYPE CallStack_GetBreakStackFrameIdx()
{
	return gDebugger->GetBreakStackFrameIdx();
}

BF_EXPORT const char* BF_CALLTYPE CallStack_GetStackFrameInfo(int stackFrameIdx, intptr* addr, const char** outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd,
	int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags)
{
	String& outString = *gTLStrReturn.Get();
	String& outString2 = *gTLStrReturn2.Get();

	outString = gDebugger->GetStackFrameInfo(stackFrameIdx, addr, &outString2, outHotIdx, outDefLineStart, outDefLineEnd, outLine, outColumn, outLanguage, outStackSize, outFlags);
	*outFile = outString2.c_str();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE CallStack_GetStackFrameId(int stackFrameIdx)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetStackFrameId(stackFrameIdx);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->Callstack_GetStackFrameOldFileInfo(stackFrameIdx);
	return outString.c_str();
}

// -1 = no jump, 0 = won't jump, 1 = will jump
BF_EXPORT int BF_CALLTYPE CallStack_GetJmpState(int stackFrameIdx)
{
	return gDebugger->GetJmpState(stackFrameIdx);
}

BF_EXPORT intptr BF_CALLTYPE Debugger_GetStackFrameCalleeAddr(int stackFrameIdx)
{
	return gDebugger->GetStackFrameCalleeAddr(stackFrameIdx);
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
	String& outString = *gTLStrReturn.Get();
	outString.clear();
	gDebugger->GetCodeAddrInfo(addr, inlineCallAddr, &outString, outHotIdx, outDefLineStart, outDefLineEnd, outLine, outColumn);
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
	gDebugger->GetStackAllocInfo(addr, outThreadId, outStackIdx);
}

BF_EXPORT const char* BF_CALLTYPE CallStack_GetStackMethodOwner(int stackFrameIdx, int& language)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetStackMethodOwner(stackFrameIdx, language);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_FindCodeAddresses(const char* fileName, int line, int column, bool allowAutoResolve)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->FindCodeAddresses(fileName, line, column, allowAutoResolve);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetAddressSourceLocation(intptr address)
{
	String& outString = *gTLStrReturn.Get();
	if (gDebugger != NULL)
		outString = gDebugger->GetAddressSourceLocation(address);
	else
		outString = "";
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetAddressSymbolName(intptr address, bool demangle)
{
	String& outString = *gTLStrReturn.Get();
	if (gDebugger != NULL)
		outString = gDebugger->GetAddressSymbolName(address, demangle);
	else
		outString = "";
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_FindLineCallAddresses(intptr address)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->FindLineCallAddresses(address);
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_DisassembleAt(intptr address)
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->DisassembleAt(address);
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_ReadMemory(uintptr address, uintptr size, unsigned char* data)
{
	if (gDebugger == NULL)
		return;
	gDebugger->ReadMemory(address, size, data);
}

BF_EXPORT void BF_CALLTYPE Debugger_WriteMemory(uintptr address, uintptr size, unsigned char* data)
{
	if (gDebugger == NULL)
		return;
	gDebugger->WriteMemory(address, data, size);
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetModulesInfo()
{
	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetModulesInfo();
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Debugger_CancelSymSrv()
{
	gDebugger->CancelSymSrv();
}

BF_EXPORT int BF_CALLTYPE Debugger_LoadImageForModuleWith(const char* moduleName, const char* debugFilePath) // 0 = No Change, 1 = Loaded, 2 = Loading in background
{
	return gDebugger->LoadImageForModule(moduleName, debugFilePath);
}

BF_EXPORT int BF_CALLTYPE Debugger_LoadDebugInfoForModule(const char* moduleName) // 0 = No Change, 1 = Loaded, 2 = Loading in background
{
	return gDebugger->LoadDebugInfoForModule(moduleName);
}

BF_EXPORT int BF_CALLTYPE Debugger_LoadDebugInfoForModuleWith(const char* moduleName, const char* debugFilePath) // 0 = No Change, 1 = Loaded, 2 = Loading in background
{
	return gDebugger->LoadDebugInfoForModule(moduleName, debugFilePath);
}

BF_EXPORT void BF_CALLTYPE Debugger_SetStepOverExternalFiles(bool stepOverExternalFiles)
{
	gDebugManager->mStepOverExternalFiles = stepOverExternalFiles;
	gDebugManager->mStepFilterVersion++;
}

BF_EXPORT void BF_CALLTYPE BfLog_Log(char* str)
{
	BfLog(str);
}

BF_EXPORT void BF_CALLTYPE BfLog_LogDbg(char* str)
{
	BfLogDbg(str);
}

BF_EXPORT Profiler* BF_CALLTYPE Debugger_StartProfiling(intptr threadId, char* desc, int sampleRate)
{
	BF_ASSERT(sampleRate > 0);
	Profiler* profiler = gDebugger->StartProfiling();
	profiler->mTargetThreadId = threadId;
	if (desc != NULL)
		profiler->mDescription = desc;
	profiler->mSamplesPerSecond = sampleRate;
	profiler->Start();
	return profiler;
}

BF_EXPORT Profiler* BF_CALLTYPE Debugger_PopProfiler()
{
	Profiler* profiler = gDebugger->PopProfiler();
	return profiler;
}

BF_EXPORT void BF_CALLTYPE Debugger_InitiateHotResolve(int flags)
{
	if (gDebugger != NULL)
		gDebugger->InitiateHotResolve((DbgHotResolveFlags)flags);
}

BF_EXPORT intptr BF_CALLTYPE Debugger_GetDbgAllocHeapSize()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);
	return gDebugger->GetDbgAllocHeapSize();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetDbgAllocInfo()
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	String& outString = *gTLStrReturn.Get();
	outString = gDebugger->GetDbgAllocInfo();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetHotResolveData(uint8* outTypeData, int* outTypeDataSize)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	if (gDebugger->mHotResolveData == NULL)
	{
		*outTypeDataSize = -1;
		return NULL;
	}

	int dataSize = (int)gDebugger->mHotResolveData->mTypeData.size();
	if (*outTypeDataSize < dataSize)
	{
		*outTypeDataSize = dataSize;
		return NULL;
	}

	*outTypeDataSize = dataSize;
	if (dataSize > 0)
	{
		for (int i = 0; i < dataSize; i++)
			outTypeData[i] = (gDebugger->mHotResolveData->mTypeData[i].mCount > 0) ? 1 : 0;
	}

	String& outString = *gTLStrReturn.Get();
	outString.Clear();

	for (auto& str : gDebugger->mHotResolveData->mBeefCallStackEntries)
	{
		if (!outString.IsEmpty())
			outString += "\n";
		outString += str;
	}

	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Debugger_GetEmitSource(char* inFilePath)
{
	AutoCrit autoCrit(gDebugManager->mCritSect);

	String& outString = *gTLStrReturn.Get();
	outString.Clear();

	if (!gDebugger->GetEmitSource(inFilePath, outString))
		return NULL;

	return outString.c_str();
}

BF_EXPORT NetResult* HTTP_GetFile(char* url, char* destPath)
{
	AutoCrit autoCrit(gDebugManager->mNetManager->mThreadPool.mCritSect);

	auto netResult = gDebugManager->mNetManager->QueueGet(url, destPath, false);
	netResult->mDoneEvent = new SyncEvent();
	return netResult;
}

BF_EXPORT int HTTP_GetResult(NetResult* netResult, int waitMS)
{
	if (netResult->mDoneEvent->WaitFor(waitMS))
	{
		return netResult->mFailed ? 0 : 1;
	}
	else
	{
		return -1;
	}
}

BF_EXPORT void HTTP_GetLastError(NetResult* netResult, const char** error, int* errorLength)
{
	*error = netResult->mError.GetPtr();
	*errorLength = netResult->mError.GetLength();
}

BF_EXPORT void HTTP_Delete(NetResult* netResult)
{
	if (!netResult->mDoneEvent->WaitFor(0))
	{
		///
		{
			AutoCrit autoCrit(gDebugManager->mNetManager->mThreadPool.mCritSect);
			if (netResult->mCurRequest != NULL)
				netResult->mCurRequest->Cancel();
		}
		netResult->mDoneEvent->WaitFor(-1);
	}
	delete netResult;
}

BF_EXPORT void Debugger_SetAliasPath(char* origPath, char* localPath)
{
	gDebugger->SetAliasPath(origPath, localPath);
}

///

BF_EXPORT bool BF_CALLTYPE Profiler_IsRunning(Profiler* profiler)
{
	return profiler->IsSampling();
}

BF_EXPORT void BF_CALLTYPE Profiler_Stop(Profiler* profiler)
{
	profiler->Stop();
}

BF_EXPORT void BF_CALLTYPE Profiler_Clear(Profiler* profiler)
{
	profiler->Clear();
}

BF_EXPORT const char* BF_CALLTYPE Profiler_GetOverview(Profiler* profiler)
{
	String& outString = *gTLStrReturn.Get();
	outString = profiler->GetOverview();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Profiler_GetThreadList(Profiler* profiler)
{
	String& outString = *gTLStrReturn.Get();
	outString = profiler->GetThreadList();
	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE Profiler_GetCallTree(Profiler* profiler, int threadId, bool reverse)
{
	String& outString = *gTLStrReturn.Get();
	outString = profiler->GetCallTree(threadId, reverse);
	return outString.c_str();
}

BF_EXPORT void BF_CALLTYPE Profiler_Delete(Profiler* profiler)
{
	int z = 0;
	delete profiler;
}

BF_TLS_DECLSPEC static int gTLSValue = 3;

BF_EXPORT void BF_CALLTYPE TimeTest(uint32 startTime)
{
	gTLSValue++;

	int elapsedTime = BFTickCount() - startTime;
	OutputDebugStrF("Load Time: %d\n", elapsedTime);
}

BF_EXPORT void BF_CALLTYPE BFTest()
{
	struct DeferredResolveEntry2
	{
		BfFieldDef* mFieldDef;
		int mTypeArrayIdx;
	};

	DeferredResolveEntry2 entry = { NULL, 333 };

	llvm::SmallVector<DeferredResolveEntry2, 8> vec;
	vec.push_back(DeferredResolveEntry2 { NULL, 111 } );
	vec.push_back(DeferredResolveEntry2 { NULL, 222 } );
}

///

// __attribute__((weak))
// Debugger* Beefy::CreateDebugger32(DebugManager* debugManager, DbgMiniDump* miniDump)
// {
// 	return NULL;
// }