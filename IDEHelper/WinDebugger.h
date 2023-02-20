#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/Util/PerfTimer.h"
#include "DbgSymSrv.h"
#include "DebugTarget.h"
#include "DbgExprEvaluator.h"
#include "Debugger.h"
#include "HotHeap.h"
#include "COFF.h"

namespace Beefy
{
	class BfSystem;
	class BfReducer;
	class BfPassInstance;
	class DebugManager;
	class DebugVisualizers;
	class DebugVisualizerEntry;
};

NS_BF_DBG_BEGIN

class DbgProfiler;
class DbgModule;
class DbgSrcFile;
class DbgLineData;
class X86;
class HotHeap;

#ifdef BF_DBG_32

#ifdef BF32
#define BF_CONTEXT CONTEXT
#define BF_GetThreadContext GetThreadContext
#define BF_SetThreadContext SetThreadContext
#define BF_CONTEXT_CONTROL CONTEXT_CONTROL
#define BF_CONTEXT_INTEGER CONTEXT_INTEGER
#define BF_CONTEXT_SEGMENTS CONTEXT_SEGMENTS
#define BF_CONTEXT_FLOATING_POINT CONTEXT_FLOATING_POINT
#define BF_CONTEXT_DEBUG_REGISTERS CONTEXT_DEBUG_REGISTERS
#define BF_CONTEXT_EXTENDED_REGISTERS CONTEXT_EXTENDED_REGISTERS
#define BF_CONTEXT_EXCEPTION_REQUEST CONTEXT_EXCEPTION_REQUEST
#define BF_CONTEXT_EXCEPTION_ACTIVE CONTEXT_EXCEPTION_ACTIVE
#define BF_CONTEXT_SERVICE_ACTIVE CONTEXT_SERVICE_ACTIVE
#define BF_CONTEXT_ALL CONTEXT_ALL
#define BF_CONTEXT_SP(ctx) ctx.Esp
#define BF_CONTEXT_BP(ctx) ctx.Ebp
#define BF_CONTEXT_IP(ctx) ctx.Eip
#define BF_CONTEXT_FLTDATA(ctx) ctx.FloatSave.RegisterArea
#define BF_CONTEXT_XMMDATA(ctx) &ctx.ExtendedRegisters[160]
#else
#define BF_CONTEXT WOW64_CONTEXT
#define BF_GetThreadContext Wow64GetThreadContext
#define BF_SetThreadContext Wow64SetThreadContext
#define BF_CONTEXT_CONTROL WOW64_CONTEXT_CONTROL
#define BF_CONTEXT_INTEGER WOW64_CONTEXT_INTEGER
#define BF_CONTEXT_SEGMENTS WOW64_CONTEXT_SEGMENTS
#define BF_CONTEXT_FLOATING_POINT WOW64_CONTEXT_FLOATING_POINT
#define BF_CONTEXT_DEBUG_REGISTERS WOW64_CONTEXT_DEBUG_REGISTERS
#define BF_CONTEXT_EXTENDED_REGISTERS WOW64_CONTEXT_EXTENDED_REGISTERS
#define BF_CONTEXT_EXCEPTION_REQUEST WOW64_CONTEXT_EXCEPTION_REQUEST
#define BF_CONTEXT_EXCEPTION_ACTIVE WOW64_CONTEXT_EXCEPTION_ACTIVE
#define BF_CONTEXT_SERVICE_ACTIVE WOW64_CONTEXT_SERVICE_ACTIVE
#define BF_CONTEXT_ALL WOW64_CONTEXT_ALL
#define BF_CONTEXT_SP(ctx) ctx.Esp
#define BF_CONTEXT_BP(ctx) ctx.Ebp
#define BF_CONTEXT_IP(ctx) ctx.Eip
#define BF_CONTEXT_FLTDATA(ctx) ctx.FloatSave.RegisterArea
#define BF_CONTEXT_XMMDATA(ctx) &ctx.ExtendedRegisters[160]
#endif

#else // BG_DBG_64

#define BF_CONTEXT CONTEXT
#define BF_GetThreadContext GetThreadContext
#define BF_SetThreadContext SetThreadContext
#define BF_CONTEXT_CONTROL CONTEXT_CONTROL
#define BF_CONTEXT_INTEGER CONTEXT_INTEGER
#define BF_CONTEXT_SEGMENTS CONTEXT_SEGMENTS
#define BF_CONTEXT_FLOATING_POINT CONTEXT_FLOATING_POINT
#define BF_CONTEXT_DEBUG_REGISTERS CONTEXT_DEBUG_REGISTERS
#define BF_CONTEXT_EXTENDED_REGISTERS 0
#define BF_CONTEXT_EXCEPTION_REQUEST CONTEXT_EXCEPTION_REQUEST
#define BF_CONTEXT_EXCEPTION_ACTIVE CONTEXT_EXCEPTION_ACTIVE
#define BF_CONTEXT_SERVICE_ACTIVE CONTEXT_SERVICE_ACTIVE
#define BF_CONTEXT_ALL CONTEXT_ALL
#define BF_CONTEXT_SP(ctx) ctx.Rsp
#define BF_CONTEXT_BP(ctx) ctx.Rbp
#define BF_CONTEXT_IP(ctx) ctx.Rip
#define BF_CONTEXT_FLTDATA(ctx) ctx.FltSave.FloatRegisters
#define BF_CONTEXT_XMMDATA(ctx) ctx.FltSave.XmmRegisters

#endif

enum BreakpointType
{
	BreakpointType_User,
	BreakpointType_Stepping
};

struct DwFormatInfo;

class DbgEvaluationContext
{
public:
	BfParser* mParser;
	BfReducer* mReducer;
	BfPassInstance* mPassInstance;
	DbgExprEvaluator* mDbgExprEvaluator;
	BfExpression* mExprNode;

public:
	DbgEvaluationContext(WinDebugger* winDebugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	DbgEvaluationContext(WinDebugger* winDebugger, DbgCompileUnit* dbgCompileUnit, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	void Init(WinDebugger* winDebugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	bool HasExpression();
	~DbgEvaluationContext();
	DbgTypedValue EvaluateInContext(DbgTypedValue contextTypedValue);
	String GetErrorStr();
	bool HadError();
};

class WdBreakpointCondition
{
public:
	DbgEvaluationContext* mDbgEvaluationContext;
	String mExpr;
	~WdBreakpointCondition();
};

class WdMemoryBreakpointInfo
{
public:
	addr_target mMemoryAddress;
	int mByteCount;
	String mReferenceName;
	int8 mMemoryWatchSlotBitmap;

	WdMemoryBreakpointInfo()
	{
		mMemoryAddress = 0;
		mByteCount = 0;
		mMemoryWatchSlotBitmap = 0;
	}
};

class WdBreakpoint : public Breakpoint
{
public:
	WdMemoryBreakpointInfo* mMemoryBreakpointInfo;

	addr_target mAddr;
	DbgSrcFile* mSrcFile;
	DbgLineDataEx mLineData;
	WdBreakpointCondition* mCondition;
	BreakpointType mBreakpointType;

public:
	WdBreakpoint()
	{
		mMemoryBreakpointInfo = NULL;
		mAddr = 0;
		mSrcFile = NULL;
		mLineData = DbgLineDataEx();
		mCondition = NULL;
		mBreakpointType = BreakpointType_User;
	}

	virtual uintptr GetAddr() override
	{
		return (uintptr)mAddr;
	}

	virtual bool IsMemoryBreakpointBound() override
	{
		return (mMemoryBreakpointInfo != NULL) && (mMemoryBreakpointInfo->mMemoryWatchSlotBitmap != 0);
	}

	WdBreakpoint* GetHeadBreakpoint()
	{
		if (mHead != NULL)
			return (WdBreakpoint*)mHead;
		return this;
	}
};

enum StepType
{
	StepType_None,
	StepType_StepInto,
	StepType_StepInto_Unfiltered,
	StepType_StepInto_UnfilteredSingle,
	StepType_StepOver,
	StepType_StepOut,
	StepType_StepOut_ThenInto, // For supporting step filters
	StepType_StepOut_NoFrame,
	StepType_StepOut_Inline,
	StepType_ToTempBreakpoint
};

class WdStackFrame
{
public:
	CPURegisters mRegisters;
	bool mIsStart;
	bool mIsEnd;
	bool mInInlineMethod;
	bool mInInlineCall; // Means the frame above us is inlined

	DbgSubprogram* mSubProgram;
	bool mHasGottenSubProgram;
	Array<RegForm> mRegForms;

public:
	WdStackFrame()
	{
		mSubProgram = NULL;
		mHasGottenSubProgram = false;
		mIsStart = true;
		mIsEnd = false;
		mInInlineMethod = false;
		mInInlineCall = false;
	}

	addr_target GetSourcePC()
	{
		// 'PC' is return address of call in these cases, so subtract 1 for cases where
		//  we want to bring it back into the calling block range (note that this doesn't
		//  properly determine the actual starting address of the call instruction, though)
		if ((!mIsStart) && (!mInInlineCall))
			return mRegisters.GetPC() - 1;
		return mRegisters.GetPC();
	}
};

typedef WdStackFrame CPUStackFrame;

struct WdThreadInfo
{
public:
	uint mProcessId;
	uint mThreadId;
	HANDLE mHThread;
	void* mThreadLocalBase;
	void* mStartAddress;
	bool mIsBreakRestorePaused;
	bool mFrozen;
	addr_target mStartSP;
	String mName;
	addr_target mStoppedAtAddress;
	addr_target mIsAtBreakpointAddress;
	addr_target mBreakpointAddressContinuing;
	int mMemoryBreakpointVersion;

public:
	WdThreadInfo()
	{
		mProcessId = 0;
		mThreadId = 0;
		mHThread = 0;
		mStartSP = 0;
		mThreadLocalBase = NULL;
		mStartAddress = NULL;
		mIsBreakRestorePaused = false;
		mFrozen = false;

		mIsAtBreakpointAddress = 0;
		mStoppedAtAddress = 0;
		mBreakpointAddressContinuing = 0;
		mMemoryBreakpointVersion = 0;
	}
};

class DbgPendingExpr
{
public:
	int mThreadId;
	BfParser* mParser;
	DbgType* mExplitType;
	DwFormatInfo mFormatInfo;
	DwEvalExpressionFlags mExpressionFlags;
	int mCursorPos;
	BfExpression* mExprNode;
	String mReferenceId;
	int mCallStackIdx;
	String mResult;
	Array<DbgCallResult> mCallResults;
	int mIdleTicks;
	String mException;
	bool mUsedSpecifiedLock;
	int mStackIdxOverride;

	DbgPendingExpr();
	~DbgPendingExpr();
};

class HotTargetMemory
{
public:
	addr_target mPtr;
	int mOffset;
	int mSize;

public:
	int GetSizeLeft()
	{
		return mSize - mOffset;
	}
};

#define WD_MEMCACHE_SIZE 8*1024

struct WdMemoryBreakpointBind
{
	WdBreakpoint* mBreakpoint;
	addr_target mAddress;
	int mOfs;
	int mByteCount;

	WdMemoryBreakpointBind()
	{
		mAddress = 0;
		mBreakpoint = NULL;
		mOfs = 0;
		mByteCount = 0;
	}
};

struct DbgPendingDebugInfoLoad
{
	DbgModule* mModule;
	bool mAllowRemote;

	DbgPendingDebugInfoLoad()
	{
		mModule = NULL;
		mAllowRemote = false;
	}
};

struct WinHotThreadState
{
	CPURegisters mRegisters;
	int mThreadId;
};

class WinDbgHeapData
{
public:
	struct Stats
	{
		intptr mHeapSize;
	};

public:
	HANDLE mFileMapping;
	Stats* mStats;

	WinDbgHeapData()
	{
		mFileMapping = 0;
		mStats = NULL;
	}

	~WinDbgHeapData()
	{
		if (mFileMapping != 0)
			::CloseHandle(mFileMapping);
	}
};

struct DwFormatInfo;

class WinDebugger : public Debugger
{
public:
	SyncEvent mContinueEvent;

	Array<HotTargetMemory> mHotTargetMemory;
	Array<WinHotThreadState> mHotThreadStates;
	int mActiveHotIdx;

	volatile bool mShuttingDown;
	volatile bool mIsRunning;
	bool mDestroying;
	String mLaunchPath;
	String mTargetPath;
	String mArgs;
	String mWorkingDir;
	bool mHotSwapEnabled;
	Array<uint8> mEnvBlock;
	DebugTarget* mEmptyDebugTarget;
	DebugTarget* mDebugTarget;
	BfSystem* mBfSystem;
	CPU* mCPU;
	PROCESS_INFORMATION mProcessInfo;
	BfDbgAttachFlags mDbgAttachFlags;
	WinDbgHeapData* mDbgHeapData;
	DWORD mDbgProcessId;
	HANDLE mDbgProcessHandle;
	HANDLE mDbgThreadHandle;
	bool mIsDebuggerWaiting;
	bool mWantsDebugContinue;
	bool mNeedsRehupBreakpoints;
	bool mContinueFromBreakpointFailed;
	WdThreadInfo* mDebuggerWaitingThread;
	WdThreadInfo* mAtBreakThread;
	WdThreadInfo* mActiveThread;
	WdBreakpoint* mActiveBreakpoint;
	WdThreadInfo* mSteppingThread;
	WdThreadInfo* mExplicitStopThread; // Don't try to show first frame-with-source for this thread (when we hit breakpoint in asm, encounter exception, etc)
	DEBUG_EVENT mDebugEvent;
	bool mGotStartupEvent;
	int mPageSize;
	DbgSymSrv mDbgSymSrv;
	DbgSymRequest* mActiveSymSrvRequest;
	DWORD mDebuggerThreadId;

	WdMemoryBreakpointBind mMemoryBreakpoints[4];
	int mMemoryBreakpointVersion;
	Dictionary<addr_target, int> mPhysBreakpointAddrMap; // To make sure we don't create multiple physical breakpoints at the same addr
	Array<WdBreakpoint*> mBreakpoints;
	Dictionary<addr_target, WdBreakpoint*> mBreakpointAddrMap;
	Array<int> mFreeMemoryBreakIndices;
	Array<WdStackFrame*> mCallStack;
	Dictionary<WdThreadInfo*, Array<WdStackFrame*>> mSavedCallStacks;
	bool mIsPartialCallStack;
	int mRequestedStackFrameIdx; // -1 means to show mShowPCOverride, -2 means "auto" stop, -3 means breakpoint - normally 0 but during inlining the address can be ambiguous
	int mBreakStackFrameIdx;
	addr_target mShowPCOverride;
	Dictionary<uint32, WdThreadInfo*> mThreadMap;
	Array<WdThreadInfo*> mThreadList;
	StepType mOrigStepType;
	StepType mStepType;
	int mCurNoInfoStepTries;
	DbgLineDataEx mStepLineData;
	bool mStepInAssembly;
	bool mStepIsRecursing;
	bool mStepSwitchedThreads;
	bool mStepStopOnNextInstruction;
	bool mDbgBreak;

	addr_target mStepStartPC;
	addr_target mStepPC;
	addr_target mStepSP;
	addr_target mStoredReturnValueAddr;
	addr_target mLastValidStepIntoPC;
	bool mIsStepIntoSpecific;
	CPURegisters mDebugEvalSetRegisters;
	DbgPendingExpr* mDebugPendingExpr;
	WdThreadInfo mDebugEvalThreadInfo; // Copy of thread info when eval started
	Array<DbgModule*> mPendingImageLoad;
	Dictionary<DbgModule*, DbgPendingDebugInfoLoad> mPendingDebugInfoLoad;
	Array<DbgModule*> mPendingDebugInfoRequests;
	HashSet<String> mLiteralSet;

	EXCEPTION_RECORD mCurException;
	bool mIsContinuingFromException;
	Array<int64> mTempBreakpoint;
	Array<int64> mStepBreakpointAddrs;

	addr_target mSavedBreakpointAddressContinuing;
	addr_target mSavedAtBreakpointAddress;
	BF_CONTEXT mSavedContext;

	Dictionary<int, Profiler*> mPendingProfilerMap;
	Array<Profiler*> mNewProfilerList;
	HashSet<Profiler*> mProfilerSet;

	addr_target mMemCacheAddr;
	uint8 mMemCacheData[WD_MEMCACHE_SIZE];

public:
	void Fail(const StringImpl& error);
	void TryGetThreadName(WdThreadInfo* threadInfo);
	void ThreadRestorePause(WdThreadInfo* onlyPauseThread, WdThreadInfo* dontPauseThread);
	void ThreadRestoreUnpause();
	void UpdateThreadDebugRegisters(WdThreadInfo* threadInfo);
	void UpdateThreadDebugRegisters();
	void ValidateBreakpoints();
	void PhysSetBreakpoint(addr_target address);
	void SetBreakpoint(addr_target address, bool fromRehup = false);
	void SetTempBreakpoint(addr_target address);
	void PhysRemoveBreakpoint(addr_target address);
	void RemoveBreakpoint(addr_target address);
	void SingleStepX86();
	bool IsInRunState();
	bool ContinueFromBreakpoint();
	bool HasLineInfoAt(addr_target address);
	DbgLineData* FindLineDataAtAddress(addr_target address, DbgSubprogram** outSubProgram = NULL, DbgSrcFile** outSrcFile = NULL, int* outLineIdx = NULL, DbgOnDemandKind onDemandKind = DbgOnDemandKind_AllowRemote);
	DbgLineData* FindLineDataInSubprogram(addr_target address, DbgSubprogram* dwSubprogram);
	bool IsStepFiltered(DbgSubprogram* dbgSubprogram, DbgLineData* dbgLineData);
	void RemoveTempBreakpoints();
	void RehupBreakpoints(bool doFlush);
	bool WantsBreakpointAt(addr_target address);
	void CheckBreakpoint(WdBreakpoint* wdBreakpoint, DbgSrcFile* srcFile, int lineNum, int hotIdx);
	void CheckBreakpoint(WdBreakpoint* wdBreakpoint);
	bool IsMemoryBreakpointSizeValid(addr_target addr, int size);
	bool HasMemoryBreakpoint(addr_target addr, int size);
	bool PopulateRegisters(CPURegisters* registers, BF_CONTEXT& lcContext);
	virtual bool PopulateRegisters(CPURegisters* registers);
	bool RollBackStackFrame(CPURegisters* registers, bool isStackStart);
	bool SetHotJump(DbgSubprogram* oldSubprogram, addr_target newTarget, int newTargetSize);
	DbgSubprogram* TryFollowHotJump(DbgSubprogram* subprogram, addr_target addr);

	bool ParseFormatInfo(DbgModule* dbgModule, const StringImpl& formatInfoStr, DwFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExpr = NULL, String* errorString = NULL, DbgTypedValue contextTypedValue = DbgTypedValue());
	String MaybeQuoteFormatInfoParam(const StringImpl& str);
	void DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors);
	DbgTypedValue EvaluateInContext(DbgCompileUnit* dbgCompileUnit, const DbgTypedValue& contextTypedValue, const StringImpl& subExpr, DwFormatInfo* formatInfo = NULL, String* outReferenceId = NULL, String* outErrors = NULL);
	bool EvalCondition(DebugVisualizerEntry* debugVis, DbgCompileUnit* dbgCompileUnit, DbgTypedValue typedVal, DwFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr);
	DwDisplayInfo* GetDisplayInfo(const StringImpl& referenceId);
	void ProcessEvalString(DbgCompileUnit* dbgCompileUnit, DbgTypedValue useTypedValue, String& evalStr, String& displayString, DwFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength);
	String ReadString(DbgTypeCode charType, intptr addr, bool isLocalAddr, intptr maxLength, DwFormatInfo& formatInfo, bool wantStringView);
	String DbgTypedValueToString(const DbgTypedValue& typedValue, const StringImpl& expr, DwFormatInfo& formatFlags, DbgExprEvaluator* optEvaluator, bool fullPrecision = false);
	bool ShouldShowStaticMember(DbgType* dbgType, DbgVariable* member);
	String GetMemberList(DbgType* dbgType, const StringImpl& expr, bool isPtr, bool isStatic, bool forceCast = false, bool isSplat = false, bool isReadOnly = false);
	DebugVisualizerEntry* FindVisualizerForType(DbgType* dbgType, Array<String>* wildcardCaptures);
	void ReserveHotTargetMemory(int size);
	addr_target AllocHotTargetMemory(int size, bool canExecute, bool canWrite, int* outAllocSize);
	void ReleaseHotTargetMemory(addr_target addr, int size);
	void CleanupHotHeap();
	int EnableWriting(intptr address, int size);
	int SetProtection(intptr address, int size, int prot);
	void EnableMemCache();
	void DisableMemCache();
	template<typename T> T ReadMemory(intptr addr, bool local = false, bool* failed = NULL);
	bool WriteInstructions(intptr address, void* src, uint64 length);
	template<typename T> bool WriteMemory(intptr addr, T val);
	virtual DbgMemoryFlags GetMemoryFlags(intptr address) override;

	void SetRunState(RunState runState);
	bool IsPaused();
	void ClearStep();
	bool SetupStep(StepType stepType);
	void CheckNonDebuggerBreak();
	bool HasSteppedIntoCall();
	void StepLineTryPause(addr_target address, bool requireExactMatch);
	void PushValue(CPURegisters* registers, int64 val);
	void PushValue(CPURegisters* registers, const DbgTypedValue& typedValue);
	void SetThisRegister(CPURegisters* registers, addr_target val);
	void AddParamValue(int paramIdx, bool hadThis, CPURegisters* registers, const DbgTypedValue& typedValue);
	bool CheckNeedsSRetArgument(DbgType* retType);
	DbgTypedValue ReadReturnValue(CPURegisters* registers, DbgType* type);
	bool SetRegisters(CPURegisters* registers);
	void SaveAllRegisters();
	void RestoreAllRegisters();
	String GetArrayItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData);
	String GetLinkedListItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, addr_target endNodePtr, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData);
	String GetDictionaryItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgTypedValue dictValue, int bucketIdx, int nodeIdx, int& count, String* outContinuationData);
	String GetTreeItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, Array<addr_target>& parentList, DbgType*& valueType, DbgTypedValue& curNode, int count, String* outContinuationData);
	void HandleCustomExpandedItems(String& retVal, DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* dwUseType, DbgType* dwValueType, String& ptrUseDataStr, String& ptrDataStr, DbgTypedValue useTypedValue, Array<String>& dbgVisWildcardCaptures, DwFormatInfo& formatInfo);
	void ModuleChanged(DbgModule* dbgModule);
	bool DoUpdate();
	void DebugThreadProc();
	bool DoOpenFile(const StringImpl& fileName, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock);

	DbgTypedValue GetRegister(const StringImpl& regName, DbgLanguage language, CPURegisters* registers, Array<RegForm>* regForms = NULL);
	void FixupLineData(DbgCompileUnit* compileUnit);
	void FixupLineDataForSubprogram(DbgSubprogram* subProgram);
	DbgModule* GetCallStackDbgModule(int callStackIdx);
	DbgSubprogram* GetCallStackSubprogram(int callStackIdx);
	DbgCompileUnit* GetCallStackCompileUnit(int callStackIdx);
	String Evaluate(const StringImpl& expr, DwFormatInfo formatInfo, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags);
	String EvaluateContinue() override;
	void EvaluateContinueKeep() override;
	String EvaluateContinue(DbgPendingExpr* pendingExpr, BfPassInstance& bfPassInstance);
	String GetAutocompleteOutput(DwAutoComplete& autoComplete);
	bool CheckConditionalBreakpoint(WdBreakpoint* breakpoint, DbgSubprogram* dbgSubprogram, addr_target pcAddress);
	void CleanupDebugEval(bool restoreRegisters = true);
	bool FixCallStackIdx(int& callStackIdx);
	void DoClearCallStack(bool clearSavedStacks);

	int LoadDebugInfoForModule(DbgModule* dbgModule);

public:
	WinDebugger(DebugManager* debugManager);
	virtual ~WinDebugger();

	virtual void OutputMessage(const StringImpl& msg) override;
	virtual void OutputRawMessage(const StringImpl& msg) override;
	virtual int GetAddrSize() override;
	virtual bool CanOpen(const StringImpl& fileName, DebuggerResult* outResult) override;
	virtual void OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled) override;
	virtual bool Attach(int processId, BfDbgAttachFlags attachFlags) override;
	virtual void Run() override;
	virtual bool HasLoadedTargetBinary() override;
	virtual void HotLoad(const Array<String>& objectFiles, int hotIdx) override;
	virtual void InitiateHotResolve(DbgHotResolveFlags flags) override;
	virtual intptr GetDbgAllocHeapSize() override;
	virtual String GetDbgAllocInfo() override;
	virtual void Update() override;
	virtual void ContinueDebugEvent() override;
	virtual void ForegroundTarget() override;
	virtual Breakpoint* CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset) override;
	virtual Breakpoint* CreateMemoryBreakpoint(intptr addr, int byteCount) override;
	virtual Breakpoint* CreateSymbolBreakpoint(const StringImpl& symbolName) override;
	virtual Breakpoint* CreateAddressBreakpoint(intptr address) override;
	virtual void CheckBreakpoint(Breakpoint* breakpoint) override;
	virtual void HotBindBreakpoint(Breakpoint* breakpoint, int lineNum, int hotIdx) override;
	virtual void DeleteBreakpoint(Breakpoint* breakpoint) override;
	virtual void DetachBreakpoint(Breakpoint* breakpoint) override;
	virtual void MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow) override;
	virtual void MoveMemoryBreakpoint(Breakpoint* breakpoint, intptr addr, int byteCount) override;
	virtual void DisableBreakpoint(Breakpoint* breakpoint) override;
	virtual void SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& condition) override;
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
	virtual String EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos) override;
	virtual String EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos) override;
	virtual bool AssignToReg(int callStackIdx, DbgTypedValue reg, DbgTypedValue value, String& outError);
	virtual String GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count) override;
	virtual String GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen) override;
	virtual String GetAutoLocals(int callStackIdx, bool showRegs) override;
	virtual String CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx) override;
	virtual String GetProcessInfo() override;
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
	addr_target GetTLSOffset(int tlsIndex);
	virtual void UpdateRegisterUsage(int stackFrameIdx) override;
	virtual void UpdateCallStackMethod(int stackFrameIdx) override;
	virtual void GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn) override;
	virtual void GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx) override;
	virtual String GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn, int* outLanguage, int* outStackSize, int8* outFlags) override;
	virtual String GetStackFrameId(int stackFrameIdx) override;
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
	virtual void SetAliasPath(const StringImpl& origPath, const StringImpl& localPath) override;
	virtual String GetModulesInfo() override;
	virtual void CancelSymSrv() override;
	virtual bool HasPendingDebugLoads() override;
	virtual int LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName) override;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName) override;
	virtual int LoadDebugInfoForModule(const StringImpl& moduleName) override;
	virtual void StopDebugging() override;
	virtual void Terminate() override;
	virtual void Detach() override;
	virtual Profiler* StartProfiling() override;
	virtual Profiler* PopProfiler() override;
	void AddProfiler(DbgProfiler* profiler);
	void RemoveProfiler(DbgProfiler* profiler);

	virtual void ReportMemory(MemReporter* memReporter) override;

	virtual bool IsOnDemandDebugger() override { return false; }
	virtual bool IsMiniDumpDebugger() { return false; }
	virtual bool GetEmitSource(const StringImpl& filePath, String& outText);
};

template<typename T> bool WinDebugger::WriteMemory(intptr addr, T val)
{
	SIZE_T dwWriteBytes;
	return WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)addr, &val, (SIZE_T)sizeof(T), &dwWriteBytes) != 0;
}

template<typename T> T WinDebugger::ReadMemory(intptr addr, bool local, bool* failed)
{
	bool success = true;
	T val;
	memset(&val, 0, sizeof(T));
	if (local)
	{
		if (addr != 0)
		{
			memcpy(&val, (void*)(intptr)addr, sizeof(T));
			/*__try
			{
				memcpy(&val, (void*)(intptr)addr, sizeof(T));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				success = false;
			}*/
		}
		else
			success = false;
	}
	else
	{
		//SIZE_T dwReadBytes;
		memset(&val, 0, sizeof(T));
		//success = ReadProcessMemory(mProcessInfo.hProcess, (void*)(intptr)addr, &val, (SIZE_T)sizeof(T), &dwReadBytes) != 0;
		success = ReadMemory(addr, (int)sizeof(T), &val);
	}

	if (failed != NULL)
		*failed = !success;
	return val;
}

addr_target DecodeTargetDataPtr(const char*& strRef);

NS_BF_DBG_END
