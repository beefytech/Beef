#pragma once

#if !defined BF_DBG_64 && !defined BF_DBG_32
#define BF_DBG_64
#endif

#include "BFPlatform.h"

#if defined BF_DBG_64 && defined BF_PLATFORM_WINDOWS
// Set LLDB_ENABLED below to test LLDB debugger on Windows
//#define LLDB_ENABLED
#endif

#include "Debugger.h"

#ifdef LLDB_ENABLED
#pragma warning(disable:4251)
#include "lldb/API/LLDB.h"

NS_BF_BEGIN

enum LLDBLaunchMode
{
	LLDBLaunchMode_Local,
	LLDBLaunchMode_Remote   // GDB RSP over TCP (compatible with OpenOCD, lldb-server --gdbserver)
};

class LLDBBreakpoint : public Breakpoint
{
public:
	lldb::SBBreakpoint mLLDBBreakpoint;
	Array<lldb::SBBreakpoint> mVersionBreakpoints;   // bindings in older hot compiles (HotBindBreakpoint)
	uintptr mResolvedAddr;

	LLDBBreakpoint() : mResolvedAddr(0) {}

	virtual uintptr GetAddr() override { return mResolvedAddr; }
	virtual bool IsMemoryBreakpointBound() override { return false; }
};

struct LLDBHotObject;

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

	// The user step in progress, for step filtering (see ContinueStep)
	enum StepKind
	{
		StepKind_None,
		StepKind_Into,
		StepKind_Over,
		StepKind_Out
	};
	StepKind mStepKind;
	bool mStepOutThenInto;
	bool mStepOutFinishedLine;
	int mStepContinueCount;
	uint64 mStepStartFunctionAddr;
	Dictionary<String, bool> mHasStatementLinesCache;

	// The evaluation in progress (see Evaluate)
	lldb::SBExpressionOptions mEvalOptions;
	bool mEvalAllowCalls;
	bool mEvalAllowProperties;
	String mEvalError;               // why a path failed (a property getter or indexer)

	// Exception info (populated when RunState_Exception is set)
	uint64 mExceptionAddress;
	uint32 mExceptionCode;
	String mExceptionDescription;

	// Stored launch parameters — set by OpenFile, consumed by the launch thread
	String mLaunchPath;
	String mTargetPath;                              // the program the IDE built - an executable, or a shared library a host loads
	String mLaunchArgs;
	String mWorkingDir;
	Array<uint8> mEnvBlock;
	DbgOpenFileFlags mOpenFileFlags;
	bool mHotSwapEnabled;

	// Remote debugging
	LLDBLaunchMode mLaunchMode;
	String mRemoteHost;            // "host:port" for LLDBLaunchMode_Remote
	bool mUseHardwareBreakpoints;  // when true, require-hardware-breakpoint setting is applied

	// Background launch thread
	BfpThread* mLaunchThread;

	// Target stdout/stderr, which LLDB captures through a pty. With DbgOpenFileFlag_RedirectStd*
	// it's forwarded to FIFOs whose read ends are handed to the IDE via GetStdHandles; otherwise
	// it's echoed to our own stdout/stderr.
	int mStdOutPipeWrite;
	int mStdErrPipeWrite;
	BfpFile* mStdOutPipeRead;    // Not yet claimed by GetStdHandles
	BfpFile* mStdErrPipeRead;
	String mStdOutPending;       // Data the pipe wasn't ready to accept yet
	String mStdErrPending;
	int mTerminalFd;              // the IDE's terminal, while the target is its foreground process group
	int mTerminalPrevForeground;

	// Hot swap
	struct HotSymbol
	{
		uint64 mAddr;      // For a thread-local variable, its offset in the executable's TLS block
		uint64 mSize;
		bool mIsCode;
		bool mIsTLS;
	};

	enum HotDataFixupKind
	{
		HotDataFixupKind_None,
		HotDataFixupKind_MergeVData,          // sBfClassVData: merge new vtable entries into the original
		HotDataFixupKind_MergeVExt,           // sBfClassVData .vext: merge, then use the new table
		HotDataFixupKind_CopyTypeData,        // sBfTypeData: copy new reflection data over the original
		HotDataFixupKind_LinkStringLiterals   // sStringLiterals: chain the new table onto the original
	};

	struct HotDataFixup
	{
		HotDataFixupKind mKind;
		String mName;
		uint64 mOldAddr;
		uint64 mOldSize;
		uint64 mNewAddr;
		uint64 mNewSize;
	};

	struct HotImage
	{
		uint64 mAddr;
		uint64 mSize;
		int mHotIdx;
		lldb::SBModule mModule;
		String mModulePath;
	};

	struct HotRange
	{
		uint64 mAddr;
		uint64 mSize;
	};

	struct HotVersion
	{
		int mHotIdx;
		Array<lldb::SBModule> mModules;
	};

	struct HotPatchedEntry
	{
		uint64 mNewAddr;
		uint64 mJmpAddr;   // Where the jump to mNewAddr is - the entry, or the end of the prologue
		uint64 mEndAddr;   // End of the bytes we wrote
		bool mNeedsStepTrap;
	};

	struct HotPatch
	{
		String mName;
		uint64 mOldAddr;
		uint64 mOldSize;
		uint64 mNewAddr;
		bool mIncompatibleLambda;   // captures changed - keep the old version, error if it's called
	};

	uint64 mHotHeapStart;
	uint64 mHotHeapSize;
	uint64 mHotHeapUsed;
	uint64 mHotHeapNextHint;
	bool mHotHeapGrowDown;                           // reserving below a shared library, rather than past the executable
	lldb::SBModule mHotBaseModule;                   // the executable or shared library the hot compiled code belongs to
	Dictionary<String, HotSymbol> mHotSymbols;       // global symbols first defined by a hot load → that definition
	Dictionary<String, HotSymbol> mHotPendingSymbols; // definitions from the batch currently being loaded
	Array<HotDataFixup> mHotPendingDataFixups;
	Dictionary<uint64, HotPatchedEntry> mHotPatchedEntries; // entry of each hot-replaced method → its jump
	Array<int> mHotInvalidLambdaTrapIds;            // breakpoints on old lambdas with incompatible captures
	Array<int> mHotStepTrapIds;                      // temporary breakpoints for a step-in in progress
	bool mHotExeTlsLoaded;
	lldb::SBModule mHotTlsInfoModule;               // the module the TLS info below was read from
	Dictionary<String, uint64> mHotExeTlsOffsets;    // the executable's thread-local variables → offset in its TLS block
	uint64 mHotTlsBlockSize;                         // the base module's TLS block, rounded to its alignment
	uint64 mHotTlsModuleId;                          // the base module's TLS module id (1 for the executable)
	uint64 mHotTlsExtraOffset;                       // __BFTLS_EXTRA, reserved for new thread-local variables
	uint64 mHotTlsExtraSize;
	uint64 mHotTlsExtraUsed;
	Dictionary<String, uint64> mHotTlsDemangled;        // demangled thread-local names → TLS offset (for evaluation)
	bool mHotTlsDemangledValid;
	Array<HotImage> mHotImages;                      // each hot-loaded object's memory, until freed
	Array<HotRange> mHotFreeRanges;                  // freed hot memory, sorted by address
	Array<HotVersion> mHotVersions;                  // modules added by each hot load, oldest first
	Array<String> mHotModulePaths;                   // copies of hot-loaded objects registered with LLDB
	Dictionary<String, uint64> mHotExternalAddrs;    // cache of symbols resolved through dlsym in the target

protected:
	void DumpSymbolAddrs(const StringImpl& sym);
	void DoCreateBreakpointByName(LLDBBreakpoint* bp);
	void BeginStep(lldb::SBThread& thread, StepKind stepKind);
	bool FunctionHasStatementLines(lldb::SBFunction& function);
	bool IsStepFiltered(lldb::SBFunction& function);
	bool ContinueStep(lldb::SBThread& thread);
	lldb::SBValue EvaluateBeefPath(lldb::SBFrame& frame, const StringImpl& expr);
	lldb::SBType HotFindNewestType(const char* typeName);
	lldb::SBValue HotFindStaticVariable(lldb::SBFrame& frame, const StringImpl& qualifier, const StringImpl& name);
	bool HotFindThreadLocalOffset(const char* qualifiedName, uint64& outOffset);
	lldb::SBValue HotFindMemberInNewestTypes(lldb::SBValue value, const StringImpl& name, int depth);
	String RewriteBeefMemberAccess(lldb::SBFrame& frame, const StringImpl& expr);
	String RewriteBeefMemberAccessInSpan(lldb::SBFrame& frame, const StringImpl& expr);
	lldb::SBValue EvaluateBeefCall(lldb::SBFrame& frame, const StringImpl& expr, String& outError);
	lldb::SBValue EvaluateBeefOperand(lldb::SBFrame& frame, const StringImpl& expr, String& outError);
	void SplitBeefArgs(const StringImpl& argsText, Array<String>& outArgs);
	bool FlattenBeefStruct(lldb::SBValue value, Array<String>& outTypes, Array<String>& outTexts);
	lldb::SBValue CallBeefMethod(lldb::SBFrame& frame, lldb::SBValue thisValue, lldb::SBType staticType, const StringImpl& methodName,
		const Array<lldb::SBValue>& args, bool allowCall, String& outError);
	lldb::SBType GetBeefDynamicType(lldb::SBValue objectRef);
	void CreateOutputPipes();
	void GiveTerminalToTarget(const StringImpl& ttyPath, int pid);
	void RestoreTerminal();
	void CloseOutputPipes();
	void PumpTargetOutput();
	void HotResetState();
	bool HotWaitForStop(String& outError);
	bool HotEvaluate(const StringImpl& expr, uint64& outValue, String& outError);
	bool HotReserveHeap(uint64 minSize, String& outError);
	uint64 HotAlloc(uint64 size, uint64 align, String& outError);
	void HotFree(uint64 addr, uint64 size);
	void HotCleanupImages(int currentHotIdx);
	bool HotFindExeSymbol(const StringImpl& name, HotSymbol& outSymbol);
	bool HotFindCanonicalSymbol(const StringImpl& name, HotSymbol& outSymbol);
	bool HotResolveExternal(const StringImpl& name, uint64& outAddr, String& outError);
	bool HotResolveObjectSymbol(LLDBHotObject* obj, int symIdx, Array<HotPatch>& patches, uint64& outAddr, String& outError);
	bool HotParseObject(LLDBHotObject* obj, String& outError);
	bool HotPrepareObject(LLDBHotObject* obj, Array<HotPatch>& patches, String& outError);
	void HotChooseBaseModule(const Array<LLDBHotObject*>& objects);
	lldb::SBModule HotGetBaseModule();
	bool HotIsBaseModuleExecutable();
	bool HotGetTlsBlockAddr(lldb::SBFrame& frame, uint64& outAddr);
	bool HotLinkObject(LLDBHotObject* obj, Array<HotPatch>& patches, String& outError);
	bool HotApplyDataFixups(String& outError);
	void HotRegisterDebugInfo(LLDBHotObject* obj, int hotIdx);
	void HotRemoveDebugInfo();
	bool HotIsInPatchedEntry(uint64 addr, uint64* outEntryAddr, HotPatchedEntry* outEntry);
	bool HotGetPatchLayout(const HotPatch& patch, uint64& outJmpAddr, int& outJmpSize, uint64* outPrologueSize);
	void HotSetStepTraps();
	bool HotLoadExeTlsInfo(String& outError);
	bool HotResolveTlsSymbol(LLDBHotObject* obj, int symIdx, uint64& outOffset, String& outError);
	void HotCheckLambdaCaptures(Array<HotPatch>& patches);
	lldb::SBBreakpoint CreateLineBreakpoint(LLDBBreakpoint* bp, int lineNum);
	void HotDeleteVersionBreakpoints(LLDBBreakpoint* bp);
	void HotGetVersionModules(int hotIdx, lldb::SBFileSpecList& outModules);
	int HotFindVersionWithFile(const lldb::SBFileSpec& fileSpec, int belowHotIdx);
	int HotGetModuleVersion(lldb::SBModule module);
	void HotClearStepTraps();
	void HotFilterBreakpointLocations(LLDBBreakpoint* bp);
	bool HotStepThreadsPastPatches(const Array<HotPatch>& patches, String& outError);
	bool HotApplyPatches(const Array<HotPatch>& patches, int& outNumPatched, String& outError);

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
