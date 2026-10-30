#pragma once

#include "Debugger.h"
#include "DbgExprEvaluator.h"

namespace Beefy
{
	class BfSystem;
	class BfReducer;
	class BfPassInstance;
	class DebugVisualizerEntry;
};

NS_BF_DBG_BEGIN

class DbgTargetHost;

class DbgEvaluationContext
{
public:
	BfParser* mParser;
	BfReducer* mReducer;
	BfPassInstance* mPassInstance;
	DbgExprEvaluator* mDbgExprEvaluator;
	BfExpression* mExprNode;

public:
	DbgEvaluationContext(DbgTargetHost* debugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	DbgEvaluationContext(DbgTargetHost* debugger, DbgCompileUnit* dbgCompileUnit, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	void Init(DbgTargetHost* debugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo = NULL, DbgTypedValue contextValue = DbgTypedValue());
	bool HasExpression();
	~DbgEvaluationContext();
	DbgTypedValue EvaluateInContext(DbgTypedValue contextTypedValue);
	String GetErrorStr();
	bool HadError();
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

class DbgTargetHost : public Debugger
{
public:
	BfSystem* mBfSystem;
	volatile bool mIsRunning;
	int mPageSize;
	Array<WdStackFrame*> mCallStack;
	int mRequestedStackFrameIdx; // -1 means to show mShowPCOverride, -2 means "auto" stop, -3 means breakpoint - normally 0 but during inlining the address can be ambiguous
	CPURegisters mDebugEvalSetRegisters;
	Dictionary<DbgModule*, DbgPendingDebugInfoLoad> mPendingDebugInfoLoad;
	HashSet<String> mLiteralSet;

public:
	virtual void Fail(const StringImpl& error) = 0;
	virtual addr_target GetTLSOffset(int tlsIndex) = 0;
	virtual int EnableWriting(intptr address, int size) = 0;
	virtual int SetProtection(intptr address, int size, int prot) = 0;
	virtual bool IsMiniDumpDebugger() = 0;
	virtual void MapMemory(addr_target addr, void* data, intptr_target size) {}

	virtual int GetActiveThreadId() = 0;
	virtual String GetActiveThreadName() = 0;
	virtual void ClearExplicitStopThread() = 0;
	virtual int GetActiveBreakpointHitCount() = 0;
	virtual String GetActiveBreakpointCondition() = 0;
	virtual int GetTargetProcessId() = 0;

	virtual bool PopulateRegisters(CPURegisters* registers) = 0;
	virtual DbgTypedValue GetRegister(const StringImpl& regName, DbgLanguage language, CPURegisters* registers, Array<RegForm>* regForms = NULL) = 0;
	virtual bool AssignToReg(int callStackIdx, DbgTypedValue reg, DbgTypedValue value, String& outError) = 0;
	virtual bool SetRegisters(CPURegisters* registers) = 0;
	virtual void SaveAllRegisters() = 0;
	virtual void RestoreAllRegisters() = 0;

	virtual void PushValue(CPURegisters* registers, int64 val) = 0;
	virtual void PushValue(CPURegisters* registers, const DbgTypedValue& typedValue) = 0;
	virtual void SetThisRegister(CPURegisters* registers, addr_target val) = 0;
	virtual void AddParamValue(int paramIdx, bool hadThis, CPURegisters* registers, const DbgTypedValue& typedValue) = 0;
	virtual bool CheckNeedsSRetArgument(DbgType* retType) = 0;
	virtual DbgTypedValue ReadReturnValue(CPURegisters* registers, DbgType* type) = 0;

	virtual bool ProcessEvalString(DbgCompileUnit* dbgCompileUnit, DbgTypedValue useTypedValue, String& evalStr, String& displayString, DwFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength) = 0;

	virtual bool WantsHotHeap() = 0;
	virtual addr_target ReserveHotHeapMemory(addr_target address, int size) = 0;
	virtual void ReleaseHotHeapMemory(addr_target address) = 0;
	virtual void ReserveHotTargetMemory(int size) = 0;
	virtual addr_target AllocHotTargetMemory(int size, bool canExecute, bool canWrite, int* outAllocSize) = 0;
	virtual void ReleaseHotTargetMemory(addr_target addr, int size) = 0;
	virtual bool WriteInstructions(intptr address, void* src, uint64 length) = 0;
	virtual bool SetHotJump(DbgSubprogram* oldSubprogram, addr_target newTarget, int newTargetSize) = 0;
	virtual void PhysSetBreakpoint(addr_target address) = 0;

	virtual addr_target DecodeThunk(addr_target address, DbgModuleMemoryCache* memoryCache) = 0;
	virtual bool DecodeInstruction(addr_target address, DbgModuleMemoryCache* memoryCache, CPUInst* inst) = 0;
	virtual bool IsReturnInstruction(addr_target address, DbgModuleMemoryCache* memoryCache) = 0;
	virtual DbgBreakKind GetDbgBreakKind(addr_target address, DbgModuleMemoryCache* memoryCache, intptr_target* regs, intptr_target* outObjectPtr) = 0;

	template<typename T> T ReadMemory(intptr addr, bool local = false, bool* failed = NULL);
	using Debugger::ReadMemory;

	bool ParseFormatInfo(DbgModule* dbgModule, const StringImpl& formatInfoStr, DwFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExpr = NULL, String* errorString = NULL, DbgTypedValue contextTypedValue = DbgTypedValue());
	String MaybeQuoteFormatInfoParam(const StringImpl& str);
	void DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors);
	DbgTypedValue EvaluateInContext(DbgCompileUnit* dbgCompileUnit, const DbgTypedValue& contextTypedValue, const StringImpl& subExpr, DwFormatInfo* formatInfo = NULL, String* outReferenceId = NULL, String* outErrors = NULL);
	bool EvalCondition(DebugVisualizerEntry* debugVis, DbgCompileUnit* dbgCompileUnit, DbgTypedValue typedVal, DwFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr);
	DebugVisualizerEntry* FindVisualizerForType(DbgType* dbgType, Array<String>* wildcardCaptures);
};

template<typename T> T DbgTargetHost::ReadMemory(intptr addr, bool local, bool* failed)
{
	bool success = true;
	T val;
	memset(&val, 0, sizeof(T));
	if (local)
	{
		if (addr != 0)
			memcpy(&val, (void*)(intptr)addr, sizeof(T));
		else
			success = false;
	}
	else
	{
		success = ReadMemory(addr, (int)sizeof(T), &val);
	}

	if (failed != NULL)
		*failed = !success;
	return val;
}

NS_BF_DBG_END
