#pragma once

#include "BfSystem.h"
#include "BfModule.h"
#include "BeefySysLib/util/Heap.h"
#include "BeefySysLib/util/AllocDebug.h"
#include "../Debugger.h"

NS_BF_BEGIN

class BfCompiler;
class CeFrame;
class CeExprEvaluator;
class CeContext;
class CeMachine;
class CeFunction;
class BfReducer;
class CeDebugger;
class DebugVisualizerEntry;

class CeBreakpoint : public Breakpoint
{
public:		
	uintptr mCurBindAddr;
	bool mHasBound;
	int mIdx;

public:
	CeBreakpoint()
	{				
		mCurBindAddr = 1;
		mHasBound = false;
		mIdx = -1;
	}

	virtual uintptr GetAddr() { return mCurBindAddr; }
	virtual bool IsMemoryBreakpointBound() { return false; }
};

struct CeFormatInfo
{
	int mCallStackIdx;
	bool mHidePointers;
	bool mIgnoreDerivedClassInfo;
	bool mNoVisualizers;
	bool mNoMembers;
	bool mRawString;
	bool mNoEdit;
	DbgTypeKindFlags mTypeKindFlags;
	intptr mArrayLength;
	intptr mOverrideCount;
	intptr mMaxCount;
	DwDisplayType mDisplayType;	
	int mTotalSummaryLength;
	String mReferenceId;
	String mSubjectExpr;
	String mExpectedType;
	String mNamespaceSearch;
	int mExpandItemDepth;
	BfTypedValue mExplicitThis;

	CeFormatInfo()
	{
		mCallStackIdx = -1;
		mHidePointers = false;
		mIgnoreDerivedClassInfo = false;
		mRawString = false;
		mNoVisualizers = false;
		mNoMembers = false;
		mNoEdit = false;
		mTypeKindFlags = DbgTypeKindFlag_None;
		mArrayLength = -1;
		mOverrideCount = -1;
		mMaxCount = -1;
		mTotalSummaryLength = 0;
		mDisplayType = DwDisplayType_NotSpecified;
		mExpandItemDepth = 0;		
	}
};

class CeEvaluationContext
{
public:
	CeDebugger* mDebugger;
	BfParser* mParser;
	BfReducer* mReducer;
	BfPassInstance* mPassInstance;
	BfExprEvaluator* mExprEvaluator;
	BfExpression* mExprNode;
	BfTypedValue mResultOverride;	
	String mExprString;

	BfTypedValue mExplicitThis;
	int mCallStackIdx;

public:
	CeEvaluationContext(CeDebugger* winDebugger, const StringImpl& expr, CeFormatInfo* formatInfo = NULL, BfTypedValue contextValue = BfTypedValue());
	void Init(CeDebugger* winDebugger, const StringImpl& expr, CeFormatInfo* formatInfo = NULL, BfTypedValue contextValue = BfTypedValue());
	bool HasExpression();
	~CeEvaluationContext();
	BfTypedValue EvaluateInContext(BfTypedValue contextTypedValue);
	String GetErrorStr();
	bool HadError();
};

class CeDbgState
{
public:
	CeFrame* mActiveFrame;
	CeContext* mCeContext;
	BfTypedValue mExplicitThis;
	DwEvalExpressionFlags mDbgExpressionFlags;
	bool mHadSideEffects;
	bool mBlockedSideEffects;

public:
	CeDbgState()
	{
		mActiveFrame = NULL;
		mCeContext = NULL;
		mDbgExpressionFlags = DwEvalExpressionFlag_None;
		mHadSideEffects = false;
		mBlockedSideEffects = false;
	}
};

class CePendingExpr
{
public:
	int mThreadId;
	BfParser* mParser;
	BfType* mExplitType;
	CeFormatInfo mFormatInfo;
	DwEvalExpressionFlags mExpressionFlags;
	int mCursorPos;
	BfAstNode* mExprNode;
	String mReferenceId;
	int mCallStackIdx;
	String mResult;	
	int mIdleTicks;
	String mException;

	CePendingExpr();
	~CePendingExpr();
};

class CeFileInfo
{
public:
	Array<CeBreakpoint*> mOrderedBreakpoints;
};

class CeDbgFieldEntry
{
public:
	BfType* mType;
	int mDataOffset;

public:
	CeDbgFieldEntry()
	{
		mType = NULL;
		mDataOffset = 0;
	}
};

class CeDbgTypeInfo
{
public:
	struct ConstIntEntry
	{
	public:
		int mFieldIdx;
		int64 mVal;		
	};

public:
	BfType* mType;
	Array<CeDbgFieldEntry> mFieldOffsets;
	Array<ConstIntEntry> mConstIntEntries;
};

struct CeTypedValue
{
	addr_ce mAddr;
	BfIRType mType;

	CeTypedValue()
	{
		mAddr = 0;
		mType = BfIRType();
	}

	CeTypedValue(addr_ce addr, BfIRType type)
	{
		mAddr = addr;
		mType = type;
	}

	operator bool() const
	{
		return mType.mKind != BfIRTypeData::TypeKind_None;
	}
};

class CeDebugger : public Debugger
{
public:
	BfCompiler* mCompiler;
	CeMachine* mCeMachine;
	DebugManager* mDebugManager;
	CePendingExpr* mDebugPendingExpr;
	CeDbgState* mCurDbgState;	
	Array<CeBreakpoint*> mBreakpoints;
	Dictionary<String, CeFileInfo*> mFileInfo;
	Dictionary<int, CeDbgTypeInfo> mDbgTypeInfoMap;

	CeEvaluationContext* mCurEvaluationContext;
	CeBreakpoint* mActiveBreakpoint;
	int mBreakpointVersion;
	bool mBreakpointCacheDirty;	
	bool mBreakpointFramesDirty;
	int mCurDisasmFuncId;

public:
	bool SetupStep(int frameIdx = 0);
	CeFrame* GetFrame(int callStackIdx);
	String EvaluateContinue(CePendingExpr* pendingExpr, BfPassInstance& bfPassInstance);
	String Evaluate(const StringImpl& expr, CeFormatInfo formatInfo, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags);
	DwDisplayInfo* GetDisplayInfo(const StringImpl& referenceId);
	String GetMemberList(BfType* type, addr_ce addr, addr_ce addrInst, bool isStatic);
	DebugVisualizerEntry* FindVisualizerForType(BfType* dbgType, Array<String>* wildcardCaptures);
	bool ParseFormatInfo(const StringImpl& formatInfoStr, CeFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExprString, String* errorString, BfTypedValue contextTypedValue = BfTypedValue());
	String MaybeQuoteFormatInfoParam(const StringImpl& str);
	BfTypedValue EvaluateInContext(const BfTypedValue& contextTypedValue, const StringImpl& subExpr, CeFormatInfo* formatInfo = NULL, String* outReferenceId = NULL, String* outErrors = NULL);
	void DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors);
	String GetArrayItems(DebugVisualizerEntry* debugVis, BfType* valueType, BfTypedValue& curNode, int& count, String* outContinuationData);
	String GetLinkedListItems(DebugVisualizerEntry* debugVis, addr_ce endNodePtr, BfType* valueType, BfTypedValue& curNode, int& count, String* outContinuationData);
	String GetDictionaryItems(DebugVisualizerEntry* debugVis, BfTypedValue dictValue, int bucketIdx, int nodeIdx, int& count, String* outContinuationData);
	String GetTreeItems(DebugVisualizerEntry* debugVis, Array<addr_ce>& parentList, BfType*& valueType, BfTypedValue& curNode, int count, String* outContinuationData);
	bool EvalCondition(DebugVisualizerEntry* debugVis, BfTypedValue typedVal, CeFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr);
	CeTypedValue GetAddr(BfConstant* constant);
	CeTypedValue GetAddr(const BfTypedValue typeVal);
	String ReadString(BfTypeCode charType, intptr addr, intptr maxLength, CeFormatInfo& formatInfo);
	void ProcessEvalString(BfTypedValue useTypedValue, String& evalStr, String& displayString, CeFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength);
	String TypedValueToString(const BfTypedValue& typedValue, const StringImpl& expr, CeFormatInfo& formatFlags, bool fullPrecision = false);
	void HandleCustomExpandedItems(String& retVal, DebugVisualizerEntry* debugVis, BfTypedValue typedValue, addr_ce addr, addr_ce addrInst, Array<String>& dbgVisWildcardCaptures, CeFormatInfo& formatInfo);
	void ClearBreakpointCache();
	void UpdateBreakpointCache();
	void UpdateBreakpointFrames();
	void UpdateBreakpointAddrs();
	void UpdateBreakpoints(CeFunction* ceFunction);	
	void Continue();
	CeDbgTypeInfo* GetDbgTypeInfo(int typeId);
	CeDbgTypeInfo* GetDbgTypeInfo(BfIRType irType);
	int64 ValueToInt(const BfTypedValue& typedVal);
	BfType* FindType(const StringImpl& name);

public:
	CeDebugger(DebugManager* debugManager, BfCompiler* bfCompiler);
	~CeDebugger();

	virtual void OutputMessage(const StringImpl& msg) override;
	virtual void OutputRawMessage(const StringImpl& msg) override;
	virtual int GetAddrSize() override;
	virtual bool CanOpen(const StringImpl& fileName, DebuggerResult* outResult) override;
	virtual void OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled) override;
	virtual bool Attach(int processId, BfDbgAttachFlags attachFlags) override;
	virtual void Run() override;
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
	virtual uintptr GetBreakpointAddr(Breakpoint* breakpoint) override;
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
	//virtual DbgTypedValue GetRegister(const StringImpl& regName, CPURegisters* registers, Array<RegForm>* regForms = NULL) override;
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
	virtual void GetCodeAddrInfo(intptr addr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn) override;
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
	virtual Profiler* PopProfiler() override; // Profiler requested by target program
	virtual void ReportMemory(MemReporter* memReporter) override;
	virtual bool IsOnDemandDebugger() override;
};

NS_BF_END