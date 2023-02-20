#pragma once

#include "DebugCommon.h"
#include "Compiler/MemReporter.h"
#include "BeefySysLib/util/HashSet.h"
#include <vector>

NS_BF_BEGIN

class DebugManager;
class BfAutoComplete;

enum DbgHitCountBreakKind : int8
{
	DbgHitCountBreakKind_None,
	DbgHitCountBreakKind_Equals,
	DbgHitCountBreakKind_GreaterEquals,
	DbgHitCountBreakKind_Multiple
};

class Breakpoint
{
public:
	String mFilePath;
	int mRequestedLineNum;
	int mLineNum;
	int mColumn;
	int mInstrOffset;
	int mPendingHotBindIdx;
	int mHitCount;
	int mTargetHitCount;
	DbgHitCountBreakKind mHitCountBreakKind;
	String mLogging;
	bool mBreakAfterLogging;

	intptr mThreadId;
	String mSymbolName;

	Breakpoint* mHead;
	Breakpoint* mLinkedSibling; // For things like templates with multiple imps on same source line
	bool mIsLinkedSibling; // Not in breakpoint list

public:
	Breakpoint()
	{
		mRequestedLineNum = -1;
		mLineNum = -1;
		mColumn = -1;
		mInstrOffset = 0;
		mPendingHotBindIdx = -1;
		mHitCount = 0;
		mTargetHitCount = 0;
		mHitCountBreakKind = DbgHitCountBreakKind_None;
		mThreadId = -1;
		mHead = NULL;
		mLinkedSibling = NULL;
		mIsLinkedSibling = false;
		mBreakAfterLogging = false;
	}

	virtual uintptr GetAddr() = 0;
	virtual bool IsMemoryBreakpointBound() = 0;
};

enum DbgTypeKindFlags
{
	DbgTypeKindFlag_None = 0,
	DbgTypeKindFlag_Int = 1
};

enum DwDisplayType : int8
{
	DwDisplayType_NotSpecified,
	DwDisplayType_Decimal,
	DwDisplayType_HexLower,
	DwDisplayType_HexUpper,
	DwDisplayType_Char,
	DwDisplayType_Ascii,
	DwDisplayType_Utf8,
	DwDisplayType_Utf16,
	DwDisplayType_Utf32,
};

enum DwIntDisplayType : int8
{
	DwIntDisplayType_Default,
	DwIntDisplayType_Decimal,
	DwIntDisplayType_HexadecimalUpper,
	DwIntDisplayType_Binary,
	DwIntDisplayType_Octal,

	DwIntDisplayType_HexadecimalLower,
};

enum DwFloatDisplayType : int8
{
	DwFloatDisplayType_Default,
	DwFloatDisplayType_Minimal,
	DwFloatDisplayType_Full,
	DwFloatDisplayType_HexUpper,

	DwFloatDisplayType_HexLower,
};

enum DwMmDisplayType : int8
{
	DwMmDisplayType_Default,
	DwMmDisplayType_UInt8,
	DwMmDisplayType_Int16,
	DwMmDisplayType_Int32,
	DwMmDisplayType_Int64,
	DwMmDisplayType_Float,
	DwMmDisplayType_Double
};

enum DwEvalExpressionFlags : int16
{
	DwEvalExpressionFlag_None = 0,
	DwEvalExpressionFlag_FullPrecision = 0x01,
	DwEvalExpressionFlag_ValidateOnly = 0x02,
	DwEvalExpressionFlag_DeselectCallStackIdx = 0x04,
	DwEvalExpressionFlag_AllowSideEffects = 0x08,
	DwEvalExpressionFlag_AllowCalls = 0x10,
	DwEvalExpressionFlag_AllowPropertyEval = 0x20,
	DwEvalExpressionFlag_MemoryAddress = 0x40,
	DwEvalExpressionFlag_MemoryWatch = 0x80,
	DwEvalExpressionFlag_Symbol = 0x100,
	DwEvalExpressionFlag_StepIntoCalls = 0x200,
	DwEvalExpressionFlag_RawStr = 0x400,
	DwEvalExpressionFlag_AllowStringView = 0x800
};

struct DwDisplayInfo
{
	String mFormatStr;
	DwIntDisplayType mIntDisplayType;
	DwMmDisplayType mMmDisplayType;
	DwFloatDisplayType mFloatDisplayType;

	DwDisplayInfo()
	{
		mIntDisplayType = DwIntDisplayType_Default;
		mMmDisplayType = DwMmDisplayType_Default;
		mFloatDisplayType = DwFloatDisplayType_Default;
	}
};

enum RunState
{
	RunState_NotStarted,
	RunState_Running,
	RunState_Running_ToTempBreakpoint,
	RunState_Paused,
	RunState_Breakpoint,
	RunState_DebugEval,
	RunState_DebugEval_Done,
	RunState_HotStep,
	RunState_Exception,
	RunState_Terminating,
	RunState_Terminated,
	RunState_SearchingSymSrv,
	RunState_HotResolve,
	RunState_TargetUnloaded
};

enum DebuggerResult
{
	DebuggerResult_Ok,
	DebuggerResult_UnknownError,
	DebuggerResult_CannotOpen,
	DebuggerResult_WrongBitSize,
};

enum BfDbgAttachFlags : uint8
{
	BfDbgAttachFlag_None = 0,
	BfDbgAttachFlag_ShutdownOnExit = 1
};

enum BfSymSrvFlags : uint8
{
	BfSymSrvFlag_None = 0,
	BfSymSrvFlag_Disable = 1,
	BfSymSrvFlag_TempCache = 2, // For testing - set up clean temporary cache
};

enum DbgHotResolveFlags : uint8
{
	DbgHotResolveFlag_None = 0,
	DbgHotResolveFlag_ActiveMethods = 1,
	DbgHotResolveFlag_Allocations = 2,
	DbgHotResolveFlag_KeepThreadState = 4
};

enum DbgMemoryFlags : uint8
{
	DbgMemoryFlags_None = 0,
	DbgMemoryFlags_Read = 1,
	DbgMemoryFlags_Write = 2,
	DbgMemoryFlags_Execute = 4
};

class DbgModuleMemoryCache
{
public:

public:
	uintptr mAddr;
	int mSize;
	int mBlockSize;
	uint8** mBlocks;
	DbgMemoryFlags* mFlags;
	int mBlockCount;
	bool mOwns;

public:
	DbgModuleMemoryCache(uintptr addr, int size);
	//DbgModuleMemoryCache(uintptr addr, uint8* data, int size, bool makeCopy);
	~DbgModuleMemoryCache();

	DbgMemoryFlags Read(uintptr addr, uint8* data, int size);
	void ReportMemory(MemReporter* memReporter);
};

class DbgHotResolveData
{
public:
	struct TypeData
	{
		intptr mCount;
		intptr mSize;

		TypeData()
		{
			mCount = 0;
			mSize = 0;
		}
	};

public:
	Array<TypeData> mTypeData;
	Beefy::HashSet<String> mBeefCallStackEntries;
};

class Profiler;

class Debugger
{
public:
	DebugManager* mDebugManager;
	RunState mRunState;
	DbgHotResolveData* mHotResolveData;
	bool mHadImageFindError;

public:
	Debugger()
	{
		mDebugManager = NULL;
		mRunState = RunState_NotStarted;
		mHotResolveData = NULL;
		mHadImageFindError = false;
	}
	virtual ~Debugger() { delete mHotResolveData; }

	virtual void OutputMessage(const StringImpl& msg) = 0;
	virtual void OutputRawMessage(const StringImpl& msg) = 0;
	virtual int GetAddrSize() = 0;
	virtual bool CanOpen(const StringImpl& fileName, DebuggerResult* outResult) = 0;
	virtual void OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled) = 0;
	virtual bool Attach(int processId, BfDbgAttachFlags attachFlags) = 0;
	virtual void Run() = 0;
	virtual bool HasLoadedTargetBinary() { return true; }
	virtual void HotLoad(const Array<String>& objectFiles, int hotIdx) = 0;
	virtual void InitiateHotResolve(DbgHotResolveFlags flags) = 0;
	virtual intptr GetDbgAllocHeapSize() = 0;
	virtual String GetDbgAllocInfo() = 0;
	virtual void Update() = 0;
	virtual void ContinueDebugEvent() = 0;
	virtual void ForegroundTarget() = 0;
	virtual Breakpoint* CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset) = 0;
	virtual Breakpoint* CreateMemoryBreakpoint(intptr addr, int byteCount) = 0;
	virtual Breakpoint* CreateSymbolBreakpoint(const StringImpl& symbolName) = 0;
	virtual Breakpoint* CreateAddressBreakpoint(intptr address) = 0;
	virtual void CheckBreakpoint(Breakpoint* breakpoint) = 0;
	virtual void HotBindBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int hotIdx) = 0;
	virtual void DeleteBreakpoint(Breakpoint* wdBreakpoint) = 0;
	virtual void DetachBreakpoint(Breakpoint* wdBreakpoint) = 0;
	virtual void MoveBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int wantColumn, bool rebindNow) = 0;
	virtual void MoveMemoryBreakpoint(Breakpoint* wdBreakpoint, intptr addr, int byteCount) = 0;
	virtual void DisableBreakpoint(Breakpoint* wdBreakpoint) = 0;
	virtual void SetBreakpointCondition(Breakpoint* wdBreakpoint, const StringImpl& condition) = 0;
	virtual void SetBreakpointLogging(Breakpoint* wdBreakpoint, const StringImpl& logging, bool breakAfterLogging) = 0;
	virtual Breakpoint* FindBreakpointAt(intptr address) = 0;
	virtual Breakpoint* GetActiveBreakpoint() = 0;
	virtual uintptr GetBreakpointAddr(Breakpoint* breakpoint) { return breakpoint->GetAddr(); }
	virtual void BreakAll() = 0;
	virtual bool TryRunContinue() = 0;
	virtual void StepInto(bool inAssembly) = 0;
	virtual void StepIntoSpecific(intptr addr) = 0;
	virtual void StepOver(bool inAssembly) = 0;
	virtual void StepOut(bool inAssembly) = 0;
	virtual void SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn) = 0;
	//virtual DbgTypedValue GetRegister(const StringImpl& regName, CPURegisters* registers, Array<RegForm>* regForms = NULL) = 0;
	virtual String Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags) = 0;
	virtual String EvaluateContinue() = 0;
	virtual void EvaluateContinueKeep() = 0;
	virtual String EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos) = 0;
	virtual String EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos) = 0;
	virtual String GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count) = 0;
	virtual String GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen) = 0;
	virtual String GetAutoLocals(int callStackIdx, bool showRegs) = 0;
	virtual String CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx) = 0;
	virtual String GetProcessInfo() = 0;
	virtual String GetThreadInfo() = 0;
	virtual void SetActiveThread(int threadId) = 0;
	virtual int GetActiveThread() = 0;
	virtual void FreezeThread(int threadId) = 0;
	virtual void ThawThread(int threadId) = 0;
	virtual bool IsActiveThreadWaiting() = 0;
	virtual void ClearCallStack() = 0;
	virtual void UpdateCallStack(bool slowEarlyOut = true) = 0;
	virtual int GetCallStackCount() = 0;
	virtual int GetRequestedStackFrameIdx() = 0;
	virtual int GetBreakStackFrameIdx() = 0;
	virtual bool ReadMemory(intptr address, uint64 length, void* dest, bool local = false) = 0;
	virtual bool WriteMemory(intptr address, void* src, uint64 length) = 0;
	virtual DbgMemoryFlags GetMemoryFlags(intptr address) = 0;
	virtual void UpdateRegisterUsage(int stackFrameIdx) = 0;
	virtual void UpdateCallStackMethod(int stackFrameIdx) = 0;
	virtual void GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn) = 0;
	virtual void GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx) = 0;
	virtual String GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd, int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags) = 0;
	virtual String GetStackFrameId(int stackFrameIdx) { return ""; }
	virtual String Callstack_GetStackFrameOldFileInfo(int stackFrameIdx) = 0;
	virtual int GetJmpState(int stackFrameIdx) = 0;
	virtual intptr GetStackFrameCalleeAddr(int stackFrameIdx) = 0;
	virtual String GetStackMethodOwner(int stackFrameIdx, int& language) = 0;
	virtual String FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve) = 0;
	virtual String GetAddressSourceLocation(intptr address) = 0;
	virtual String GetAddressSymbolName(intptr address, bool demangle) = 0;
	virtual String DisassembleAtRaw(intptr address) = 0;
	virtual String DisassembleAt(intptr address) = 0;
	virtual String FindLineCallAddresses(intptr address) = 0;
	virtual String GetCurrentException() = 0;
	virtual String GetModulesInfo() = 0;
	virtual void SetAliasPath(const StringImpl& origPath, const StringImpl& localPath) = 0;
	virtual void CancelSymSrv() = 0;
	virtual bool HasPendingDebugLoads() = 0;
	virtual int LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName) = 0;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName) = 0;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName) = 0;
	virtual void StopDebugging() = 0;
	virtual void Terminate() = 0;
	virtual void Detach() = 0;
	virtual Profiler* StartProfiling() = 0;
	virtual Profiler* PopProfiler() = 0; // Profiler requested by target program
	virtual void ReportMemory(MemReporter* memReporter) = 0;
	virtual bool IsOnDemandDebugger() = 0;
	virtual bool GetEmitSource(const StringImpl& filePath, String& outText) = 0;
};

class Profiler
{
public:
	String mDescription;
	int mSamplesPerSecond;
	intptr mTargetThreadId;

public:
	Profiler()
	{
		mSamplesPerSecond = -1;
		mTargetThreadId = 0;
	}
	virtual ~Profiler() {}

	virtual void Start() = 0;
	virtual void Stop() = 0;
	virtual void Clear() = 0;
	virtual bool IsSampling() = 0;
	virtual String GetOverview() = 0;
	virtual String GetThreadList() = 0;
	virtual String GetCallTree(int threadId, bool reverse) = 0;
};

NS_BF_END
