#pragma once

#if !defined BF_DBG_64 && !defined BF_DBG_32
#define BF_DBG_64
#endif

#if defined BF_DBG_64 && defined BF_PLATFORM_WINDOWS
// Set LLDB_ENABLED below to test LLDB debugger on Windows
//#define LLDB_ENABLED
#endif

#include "Debugger.h"

#ifdef LLDB_ENABLED
#pragma warning(disable:4251)
#include "lldb/API/LLDB.h"

NS_BF_BEGIN

class LLDBBreakpoint : public Breakpoint
{
public:
	lldb::SBBreakpoint mLLDBBreakpoint;
	uintptr mResolvedAddr;

	LLDBBreakpoint() : mResolvedAddr(0) {}

	virtual uintptr GetAddr() override { return mResolvedAddr; }
	virtual bool IsMemoryBreakpointBound() override { return false; }
};

class LLDBDebugger : public Debugger
{
public:
	lldb::SBDebugger mLLDBDebugger;
	lldb::SBTarget mLLDBTarget;
	lldb::SBProcess mLLDBProcess;

	// Call stack (populated while stopped)
	Array<lldb::SBFrame> mCallStack;
	bool mCallStackDirty;

	// Breakpoints
	Array<LLDBBreakpoint*> mBreakpoints;
	Dictionary<int, LLDBBreakpoint*> mBreakpointIdMap;    // LLDB break_id → our bp
	Dictionary<uintptr, LLDBBreakpoint*> mBreakpointAddrMap; // load addr → our bp
	Breakpoint* mActiveBreakpoint;                         // bp we stopped at

	// Stack frame indices
	int mRequestedStackFrameIdx;
	int mBreakStackFrameIdx;

	int mProcessId;
	bool mDidAttach;
	bool mNeedBreakpointRebind;  // true after launch until first stop event
	int mAutoStepRemaining;      // >0 while auto-stepping through BeefStartProgram (2=StepInto, 1=StepOver)

	// Exception info (populated when RunState_Exception is set)
	uint64 mExceptionAddress;
	uint32 mExceptionCode;
	String mExceptionDescription;

	// Stored launch parameters — set by OpenFile, consumed by the launch thread
	String mLaunchPath;
	String mLaunchArgs;
	String mWorkingDir;
	Array<uint8> mEnvBlock;
	DbgOpenFileFlags mOpenFileFlags;
	bool mHotSwapEnabled;

	// Background launch thread
	BfpThread* mLaunchThread;

protected:
	void DumpSymbolAddrs(const StringImpl& sym);
	void DoCreateBreakpointByName(LLDBBreakpoint* bp);

public:
	LLDBDebugger(DebugManager* debugManager);
	~LLDBDebugger();

	virtual void OutputMessage(const StringImpl& msg) override;
	virtual void OutputRawMessage(const StringImpl& msg) override;
	virtual int GetAddrSize() override;
	virtual bool CanOpen(const StringImpl& fileName, DebuggerResult* outResult) override;
	virtual void OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled, DbgOpenFileFlags openFileFlags) override;
	virtual bool Attach(int processId, BfDbgAttachFlags attachFlags) override;
	virtual void GetStdHandles(BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr) override;
	virtual void Run() override;
	virtual void HotLoad(const Array<String>& objectFiles, int hotIdx) override;
	virtual void InitiateHotResolve(DbgHotResolveFlags flags) override;
	virtual intptr GetDbgAllocHeapSize() override;
	virtual String GetDbgAllocInfo() override;
	void DoLaunch();
	static void BFP_CALLTYPE LaunchThreadProc(void* param);
	void WaitForLaunchThread();
	void HandleProcessEvent(lldb::StateType state);
	virtual void Update() override;
	virtual void ContinueDebugEvent() override;
	virtual void ForegroundTarget(int altProcessId) override;
	virtual Breakpoint* CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset) override;
	virtual Breakpoint* CreateMemoryBreakpoint(intptr addr, int byteCount) override;
	virtual Breakpoint* CreateSymbolBreakpoint(const StringImpl& symbolName) override;
	virtual Breakpoint* CreateAddressBreakpoint(intptr address) override;
	virtual void CheckBreakpoint(Breakpoint* breakpoint) override;
	virtual void HotBindBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int hotIdx) override;
	virtual void DeleteBreakpoint(Breakpoint* wdBreakpoint) override;
	virtual void DetachBreakpoint(Breakpoint* wdBreakpoint) override;
	virtual void MoveBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int wantColumn, bool rebindNow) override;
	virtual void MoveMemoryBreakpoint(Breakpoint* wdBreakpoint, intptr addr, int byteCount) override;
	virtual void DisableBreakpoint(Breakpoint* wdBreakpoint) override;
	virtual void SetBreakpointCondition(Breakpoint* wdBreakpoint, const StringImpl& condition) override;
	virtual void SetBreakpointLogging(Breakpoint* wdBreakpoint, const StringImpl& logging, bool breakAfterLogging) override;
	virtual Breakpoint* FindBreakpointAt(intptr address) override;
	virtual Breakpoint* GetActiveBreakpoint() override;
	virtual void BreakAll() override;
	virtual bool TryRunContinue() override;
	virtual void StepInto(bool inAssembly) override;
	virtual void StepIntoSpecific(intptr addr) override;
	virtual void StepOver(bool inAssembly) override;
	virtual void StepOut(bool inAssembly) override;
	virtual void SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn) override;
	virtual String Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags) override;
	virtual String EvaluateContinue() override;
	virtual void EvaluateContinueKeep() override;
	virtual String EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos) override;
	virtual String EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos) override;
	virtual String GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count) override;
	virtual String GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen) override;
	virtual String GetAutoLocals(int callStackIdx, bool showRegs) override;
	virtual String CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx) override;
	virtual String GetProcessInfo() override;
	virtual int GetProcessId() override;
	virtual String GetThreadInfo() override;
	virtual void SetActiveThread(int threadId) override;
	virtual int GetActiveThread() override;
	virtual void FreezeThread(int threadId) override;
	virtual void ThawThread(int threadId) override;
	virtual bool IsActiveThreadWaiting() override;
	virtual void ClearCallStack() override;
	virtual void UpdateCallStack(bool slowEarlyOut = true) override;
	virtual int GetCallStackCount() override;
	virtual int GetRequestedStackFrameIdx() override;
	virtual int GetBreakStackFrameIdx() override;
	virtual bool ReadMemory(intptr address, uint64 length, void* dest, bool local = false) override;
	virtual bool WriteMemory(intptr address, void* src, uint64 length) override;
	virtual DbgMemoryFlags GetMemoryFlags(intptr address) override;
	virtual void UpdateRegisterUsage(int stackFrameIdx) override;
	virtual void UpdateCallStackMethod(int stackFrameIdx) override;
	virtual void GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn) override;
	virtual void GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx) override;
	virtual String GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd, int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags) override;
	virtual String Callstack_GetStackFrameOldFileInfo(int stackFrameIdx) override;
	virtual int GetJmpState(int stackFrameIdx) override;
	virtual intptr GetStackFrameCalleeAddr(int stackFrameIdx) override;
	virtual String GetStackMethodOwner(int stackFrameIdx, int& language) override;
	virtual String FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve) override;
	virtual String GetAddressSourceLocation(intptr address) override;
	virtual String GetAddressSymbolName(intptr address, bool demangle) override;
	virtual String DisassembleAtRaw(intptr address) override;
	virtual String DisassembleAt(intptr address) override;
	virtual String FindLineCallAddresses(intptr address) override;
	virtual String GetCurrentException() override;
	virtual String GetModulesInfo() override;
	virtual void SetAliasPath(const StringImpl& origPath, const StringImpl& localPath) override;
	virtual void CancelSymSrv() override;
	virtual bool HasPendingDebugLoads() override;
	virtual int LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName) override;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName) override;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName) override;
	virtual void StopDebugging() override;
	virtual void Terminate() override;
	virtual void Detach() override;
	virtual Profiler* StartProfiling() override;
	virtual Profiler* PopProfiler() override;
	virtual void ReportMemory(MemReporter* memReporter) override;
	virtual bool IsOnDemandDebugger() override;
	virtual bool GetEmitSource(const StringImpl& filePath, String& outText) override;
};

NS_BF_END

#endif
