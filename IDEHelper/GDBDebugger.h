#pragma once

#if !defined BF_DBG_64 && !defined BF_DBG_32
#define BF_DBG_64
#endif

#include "BFPlatform.h"

#include "Debugger.h"
#include <deque>
#include <mutex>

NS_BF_BEGIN

//----------------------------------------------------------------------------
// GDB/MI value tree
//----------------------------------------------------------------------------

// A recursive value produced by the GDB/MI parser.  A value is one of:
//   - a C-string (leaf)
//   - a tuple: { key=val, key=val, ... }
//   - a list:  [ val, val, ... ]  or  [ key=val, ... ]
class GDBMIValue
{
public:
	enum class Kind { String, Tuple, List };

	Kind mKind;
	String mString;                   // valid when Kind::String
	Array<String> mKeys;              // parallel to mChildren (may be empty for list)
	Array<GDBMIValue*> mChildren;

public:
	GDBMIValue();
	~GDBMIValue();

	// Return the child whose key matches `key`, or NULL if not found.
	GDBMIValue* Get(const char* key) const;

	// Return the string value of a child with the given key, or "" if missing.
	String GetStr(const char* key) const;
	String* GetStrPtr(const char* key) const;

	// Return the hex-decoded integer value of a child, or 0 if missing/unparseable.
	uint64 GetHex(const char* key) const;

	// Return the decimal integer value of a child, or 0 if missing/unparseable.
	int GetInt(const char* key) const;
};

//----------------------------------------------------------------------------
// GDB/MI record
//----------------------------------------------------------------------------

enum class GDBMIRecordType
{
	Result,      // ^done / ^error / ^running / ^exit
	ExecAsync,   // *stopped / *running
	StatusAsync, // +...
	NotifyAsync, // =...
	ConsoleStream, // ~"..."
	TargetStream,  // @"..."
	LogStream,     // &"..."
	Prompt,        // (gdb)
};

struct GDBMIRecord
{
	GDBMIRecordType mType;
	int mToken;          // numeric token prefix, or -1
	String mClass;       // e.g. "done", "stopped", "error"
	GDBMIValue* mValue;  // may be NULL

	GDBMIRecord();
	~GDBMIRecord();
};

//----------------------------------------------------------------------------
// Frame snapshot (used for call stack)
//----------------------------------------------------------------------------

struct GDBFrame
{
	intptr mAddr;
	String mFile;
	String mFullFile;
	int mLine;           // 0-based (IDE convention)
	String mFunction;
	String mModule;
	bool mIsBeef;

	GDBFrame()
	{
		mAddr = 0;
		mLine = -1;
		mIsBeef = false;
	}
};

//----------------------------------------------------------------------------
// Breakpoint
//----------------------------------------------------------------------------

class GDBBreakpoint : public Breakpoint
{
public:
	int mGDBBpNum;        // GDB breakpoint number from -break-insert response
	uintptr mResolvedAddr;

	GDBBreakpoint()
	{
		mGDBBpNum = -1;
		mResolvedAddr = 0;
	}

	virtual uintptr GetAddr() override { return mResolvedAddr; }
	virtual bool IsMemoryBreakpointBound() override { return false; }
};

//----------------------------------------------------------------------------
// Launch mode
//----------------------------------------------------------------------------

enum GDBLaunchMode
{
	GDBLaunchMode_Local,      // gdb directly on the local machine (default)
	GDBLaunchMode_WSL,        // "wsl gdb" — GDB runs inside WSL; paths are converted
	GDBLaunchMode_SSH,        // "ssh <server> gdb" — GDB runs on a remote host over SSH
	GDBLaunchMode_GDBServer,  // local GDB + "-target-select extended-remote <host:port>"
};

//----------------------------------------------------------------------------
// Debugger
//----------------------------------------------------------------------------

class GDBDebugger : public Debugger
{
protected:
	// GDB process handles
	BfpSpawn* mGDBSpawn;
	BfpFile* mGDBStdin;
	BfpFile* mGDBStdout;
	BfpFile* mGDBStderr;

	// Background threads
	BfpThread* mLaunchThread;
	BfpThread* mStdoutThread;

	// MI record queue (written by stdout thread, drained by Update())
	CritSect mRecordCritSect;
	std::deque<GDBMIRecord*> mIncomingRecords;

	// Token counter for MI commands
	int mNextToken;

	// Call stack (populated while stopped)
	Array<GDBFrame> mCallStack;
	bool mCallStackDirty;

	// Breakpoints
	Array<GDBBreakpoint*> mBreakpoints;
	Dictionary<int, GDBBreakpoint*> mBreakpointNumMap;    // GDB bp# → our bp
	Dictionary<uintptr, GDBBreakpoint*> mBreakpointAddrMap; // load addr → our bp
	Breakpoint* mActiveBreakpoint;

	// Stack frame indices
	int mRequestedStackFrameIdx;
	int mBreakStackFrameIdx;
	int mEvalStackFrameIdx;

	int mProcessId;
	bool mDidAttach;
	bool mNeedBreakpointRebind;
	bool mNeedsExecRun;
	int mAutoStepRemaining;

	// Exception info
	uint64 mExceptionAddress;
	uint32 mExceptionCode;
	String mExceptionDescription;

	// Launch parameters (set by OpenFile, consumed by launch thread)
	String mLaunchPath;
	String mLaunchArgs;
	String mWorkingDir;
	Array<uint8> mEnvBlock;
	DbgOpenFileFlags mOpenFileFlags;
	bool mHotSwapEnabled;

	// Launch mode — determined by parsing the "path@location" launchPath syntax
	GDBLaunchMode mLaunchMode;
	// Remote host for SSH and gdbserver modes (e.g. "user@host" or "host:1234")
	String mGDBServerHost;

	// Whether GDB has been launched and is ready
	bool mGDBReady;
	// Whether the debuggee is currently running
	bool mRunning;
	Array<String> mGDBRetainedVariables;

protected:
	void OutputMessageLine(const StringImpl& msg);

	// MI I/O
	void SendRaw(const char* line);
	int SendCommand(const char* cmd);    // returns token
	// Wait for a result record with the given token (blocking, with timeout)
	GDBMIRecord* WaitForToken(int token, int timeoutMS = 5000);
	// Send a command and wait for its result synchronously
	GDBMIRecord* SendSync(const char* cmd, int timeoutMS = 5000);
	bool SendSyncNoResult(const char* cmd, int timeoutMS = 5000);

	// MI parsing helpers (static)
	static String ParseCString(const char*& p);
	static GDBMIValue* ParseValue(const char*& p);
	static GDBMIRecord* ParseMILine(const char* line);

	// Record processing
	void ProcessRecord(GDBMIRecord* rec);
	void HandleStoppedRecord(GDBMIRecord* rec);

	// Thread procs
	static void BFP_CALLTYPE LaunchThreadProc(void* param);
	static void BFP_CALLTYPE StdoutThreadProc(void* param);
	void DoLaunch();
	void ReadStdout();

	// Helper: translate a GDB name to IDE display form
	static String FixBeefName(const char* name);

	// Breakpoint helpers
	GDBMIRecord* InsertBreakpointByLocation(const char* file, int lineNum1Based);
	GDBMIRecord* InsertBreakpointByName(const char* sym, bool mainModuleOnly);
	void BindBreakpointFromResult(GDBBreakpoint* bp, GDBMIRecord* rec);
	void DeleteGDBBreakpoint(GDBBreakpoint* bp);

	void WaitForLaunchThread();
	void WaitForStdoutThread();
	void DoCheckBreakpoint(Breakpoint* breakpoint, bool force);
	void CheckBreakpoints(bool force);

public:
	GDBDebugger(DebugManager* debugManager);
	~GDBDebugger();

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
