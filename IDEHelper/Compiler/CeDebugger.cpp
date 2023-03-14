#include "CeDebugger.h"
#include "CeMachine.h"
#include "BfCompiler.h"
#include "../DebugManager.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BfParser.h"
#include "BfReducer.h"
#include "BeefySysLib/util/UTF8.h"
#include "BfUtil.h"
#include "BfExprEvaluator.h"
#include "BeefySysLib/util/BitSet.h"
#include "../DebugVisualizers.h"
#include "BfAutoComplete.h"
#include "BfDemangler.h"

USING_NS_BF;

static addr_ce DecodeTargetDataPtr(const char*& strRef)
{
	addr_ce val = (addr_ce)stouln(strRef, sizeof(addr_ce) * 2);
	strRef += sizeof(addr_ce) * 2;
	return val;
}

//////////////////////////////////////////////////////////////////////////

CeBreakpointCondition::~CeBreakpointCondition()
{
	delete mDbgEvaluationContext;
}

//////////////////////////////////////////////////////////////////////////

CeBreakpoint::~CeBreakpoint()
{
	delete mCondition;
}

//////////////////////////////////////////////////////////////////////////

CePendingExpr::CePendingExpr()
{
	mThreadId = -1;
	mCallStackIdx = -1;
	mPassInstance = NULL;
	mParser = NULL;
	mCursorPos = -1;
	mExprNode = NULL;
	mIdleTicks = 0;
	mExplitType = NULL;
	mExpressionFlags = DwEvalExpressionFlag_None;
	mDone = false;
}

CePendingExpr::~CePendingExpr()
{
	delete mParser;
	delete mPassInstance;
}

//////////////////////////////////////////////////////////////////////////

CeEvaluationContext::CeEvaluationContext(CeDebugger* winDebugger, const StringImpl& expr, CeFormatInfo* formatInfo, BfTypedValue contextValue)
{
	Init(winDebugger, expr, formatInfo, contextValue);
}

void CeEvaluationContext::Init(CeDebugger* ceDebugger, const StringImpl& expr, CeFormatInfo* formatInfo, BfTypedValue contextValue)
{
	mDebugger = ceDebugger;

	mCallStackIdx = 0;
	mParser = NULL;
	mReducer = NULL;
	mPassInstance = NULL;
	mExprEvaluator = NULL;
	mExprNode = NULL;

	if (expr.empty())
		return;

	int atPos = (int)expr.IndexOf('@');
	if ((atPos != -1) && (atPos < expr.mLength - 2) && (expr[atPos + 1] == '0') && (expr[atPos + 2] == 'x'))
	{
		bool isValid = true;
		for (int i = 0; i < atPos; i++)
		{
			char c = expr[i];
			if ((c < '0') || (c > '9'))
			{
				isValid = false;
				break;
			}
		}

		if (isValid)
		{
			int parseLength = expr.mLength;
			for (int i = 0; i < expr.mLength; i++)
			{
				if ((expr[i] == ',') || (::isspace((uint8)expr[i])))
				{
					parseLength = i;
					break;
				}
			}
			mExprString = expr.Substring(0, parseLength);

			String typeIdStr = expr.Substring(0, atPos);
			String addrStr = expr.Substring(atPos + 3, parseLength - atPos - 3);

			int typeId = strtol(typeIdStr.c_str(), NULL, 10);
			int64 addrVal = strtoll(addrStr.c_str(), NULL, 16);

			if ((typeId != 0) && (addrVal != 0))
			{
				auto type = ceDebugger->mCompiler->mContext->FindTypeById(typeId);
				if (type != NULL)
				{
					auto module = ceDebugger->mCeMachine->mCeModule;

					if (type->IsObjectOrInterface())
					{
						mResultOverride = BfTypedValue(module->mBfIRBuilder->CreateIntToPtr(
							module->mBfIRBuilder->CreateConst(BfTypeCode_IntPtr, (uint64)addrVal), module->mBfIRBuilder->MapType(type)), type);
					}
					else
					{
						mResultOverride = BfTypedValue(module->mBfIRBuilder->CreateConstAggCE(module->mBfIRBuilder->MapType(type), (addr_ce)addrVal), type, true);
					}
				}
			}
			return;
		}
	}

	mParser = new BfParser(ceDebugger->mCompiler->mSystem);
	mPassInstance = new BfPassInstance(ceDebugger->mCompiler->mSystem);
	auto terminatedExpr = expr + ";";
	mParser->SetSource(terminatedExpr.c_str(), (int)terminatedExpr.length());
	mParser->Parse(mPassInstance);

	mReducer = new BfReducer();
	mReducer->mAlloc = mParser->mAlloc;
	mReducer->mSystem = ceDebugger->mCompiler->mSystem;
	mReducer->mPassInstance = mPassInstance;
	mReducer->mVisitorPos = BfReducer::BfVisitorPos(mParser->mRootNode);
	mReducer->mVisitorPos.MoveNext();
	mReducer->mSource = mParser;
	mExprNode = mReducer->CreateExpression(mParser->mRootNode->GetFirst());
	mParser->Close();
	mExprEvaluator = new BfExprEvaluator(ceDebugger->mCeMachine->mCeModule);

	if ((formatInfo != NULL) && (mExprNode != NULL) && (mExprNode->GetSrcEnd() < (int)expr.length()))
	{
		String formatFlags = expr.Substring(mExprNode->GetSrcEnd());
		String errorString = "Invalid expression";
		if (!ceDebugger->ParseFormatInfo(formatFlags, formatInfo, mPassInstance, NULL, NULL, &errorString, contextValue))
		{
			mPassInstance->FailAt(errorString, mParser->mSourceData, mExprNode->GetSrcEnd(), (int)expr.length() - mExprNode->GetSrcEnd());
			formatFlags = "";
		}
	}

	if (formatInfo != NULL)
	{
		mExplicitThis = formatInfo->mExplicitThis;
		mCallStackIdx = formatInfo->mCallStackIdx;
	}

	mExprNode->ToString(mExprString);
}

bool CeEvaluationContext::HasExpression()
{
	return !mExprString.IsEmpty();
}

CeEvaluationContext::~CeEvaluationContext()
{
	delete mParser;
	delete mReducer;
	delete mExprEvaluator;
	delete mPassInstance;
}

BfTypedValue CeEvaluationContext::EvaluateInContext(BfTypedValue contextTypedValue, CeDbgState* dbgState)
{
	if (mResultOverride)
		return mResultOverride;

	if (mExprNode == NULL)
		return BfTypedValue();
	mPassInstance->ClearErrors();

	auto ceFrame = mDebugger->GetFrame(mCallStackIdx);

	auto module = mDebugger->mCeMachine->mCeModule;

	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(module->mCurTypeInstance, ceFrame->mFunction->mMethodInstance->GetOwner());
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(module->mCurMethodInstance, ceFrame->mFunction->mMethodInstance);
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(mDebugger->mCompiler->mPassInstance, mPassInstance);
	SetAndRestoreValue<bool> prevIgnoreWrites(module->mBfIRBuilder->mIgnoreWrites, true);

	BfMethodState methodState;
	SetAndRestoreValue<BfMethodState*> prevMethodState(module->mCurMethodState, &methodState);
	methodState.mTempKind = module->mCurMethodInstance->mMethodDef->mIsStatic ? BfMethodState::TempKind_Static : BfMethodState::TempKind_NonStatic;

	CeDbgState localDbgState;
	if (dbgState == NULL)
		dbgState = &localDbgState;

	dbgState->mActiveFrame = ceFrame;
	dbgState->mCeContext = mDebugger->mCeMachine->mCurContext;
	if (contextTypedValue)
		dbgState->mExplicitThis = contextTypedValue;
	else
		dbgState->mExplicitThis = mExplicitThis;
	SetAndRestoreValue<CeDbgState*> prevDbgState(mDebugger->mCurDbgState, dbgState);

	BfTypedValue exprResult;
	mExprEvaluator->VisitChildNoRef(mExprNode);

	auto result = mExprEvaluator->mResult;
	if ((result) && (!result.mType->IsComposite()))
		result = module->LoadValue(result);

	return result;
}

bool CeEvaluationContext::HadError()
{
	return (mPassInstance != NULL) && (mPassInstance->mFailedIdx != 0);
}

String CeEvaluationContext::GetErrorStr()
{
	if (mPassInstance == NULL)
		return "";
	String errorStr = mPassInstance->mErrors[0]->mError;
	if (mExprNode != NULL)
	{
		errorStr += ": ";
		errorStr += mExprNode->ToString();
	}
	return errorStr;
}

//////////////////////////////////////////////////////////////////////////

CeDebugger::CeDebugger(DebugManager* debugManager, BfCompiler* bfCompiler)
{
	mDebugManager = debugManager;
	mCompiler = bfCompiler;
	mCeMachine = bfCompiler->mCeMachine;
	mRunState = RunState_Running;
	mCeMachine->mDebugger = this;
	mCeMachine->mDebugEvent.Reset();
	mDebugPendingExpr = NULL;
	mCurDbgState = NULL;
	mBreakpointVersion = 0;
	mBreakpointCacheDirty = false;
	mBreakpointFramesDirty = false;
	mCurDisasmFuncId = 0;
	mActiveBreakpoint = NULL;
	mCurEvaluationContext = NULL;
	mPendingActiveFrameOffset = 0;
}

String CeDebugger::TypeToString(const BfTypedValue& typedValue)
{
	if (typedValue.mType == NULL)
		return "null";
	if (typedValue.IsReadOnly())
		return String("readonly ") + mCeMachine->mCeModule->TypeToString(typedValue.mType);
	return mCeMachine->mCeModule->TypeToString(typedValue.mType);
}

String CeDebugger::TypeToString(BfType* type, CeTypeModKind typeModKind)
{
	if (type == NULL)
		return "null";
	String str;
	if (typeModKind == CeTypeModKind_ReadOnly)
		str += "readonly ";
	else if (typeModKind == CeTypeModKind_Const)
		str += "const ";
	str += mCeMachine->mCeModule->TypeToString(type);
	return str;
}

CeDebugger::~CeDebugger()
{
	mCeMachine->mDebugEvent.Set(true);
	mCeMachine->mDebugger = NULL;
	delete mDebugPendingExpr;

	for (auto breakpoint : mBreakpoints)
		delete breakpoint;

	for (auto kv : mFileInfo)
		delete kv.mValue;
}

void CeDebugger::OutputMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back("msg " + msg);
}

void CeDebugger::OutputRawMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back(msg);
}

int CeDebugger::GetAddrSize()
{
	return sizeof(addr_ce);
}

bool CeDebugger::CanOpen(const StringImpl& fileName, DebuggerResult* outResult)
{
	return false;
}

void CeDebugger::OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled)
{
}

bool CeDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	return false;
}

void CeDebugger::Run()
{
}

void CeDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
}

void CeDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
}

intptr CeDebugger::GetDbgAllocHeapSize()
{
	return intptr();
}

String CeDebugger::GetDbgAllocInfo()
{
	return String();
}

void CeDebugger::Update()
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	if ((mRunState == RunState_Terminated) || (mRunState == RunState_Terminating))
		return;

	if (mDebugPendingExpr != NULL)
	{
		if (mDebugPendingExpr->mDone)
			mRunState = RunState_DebugEval_Done;
		else
			mRunState = RunState_DebugEval;
	}
	else if (mCeMachine->mDbgPaused)
		mRunState = RunState_Paused;
	else
		mRunState = RunState_Running;
}

void CeDebugger::UpdateBreakpointFrames()
{
	if (mBreakpointCacheDirty)
		UpdateBreakpointCache();
	for (auto& callStack : mCeMachine->mCurContext->mCallStack)
	{
		if (callStack.mFunction->mBreakpointVersion != mBreakpointVersion)
			UpdateBreakpoints(callStack.mFunction);
	}
}

void CeDebugger::ContinueDebugEvent()
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	mRunState = RunState_Running;
	mActiveBreakpoint = NULL;
	mPendingActiveFrameOffset = 0;
	mDbgCallStack.Clear();

	if (mBreakpointFramesDirty)
		UpdateBreakpointFrames();

	mCeMachine->mDebugEvent.Set();
}

void CeDebugger::ForegroundTarget()
{
}

bool CeDebugger::CheckConditionalBreakpoint(CeBreakpoint* breakpoint)
{
	auto _SplitExpr = [&](const StringImpl& expr, StringImpl& outExpr, StringImpl& outSubject)
	{
		int crPos = (int)expr.IndexOf('\n');
		if (crPos != -1)
		{
			outExpr += expr.Substring(0, crPos);
			outSubject += expr.Substring(crPos + 1);
		}
		else
		{
			outExpr += expr;
		}
	};

	if (breakpoint->mCondition != NULL)
	{
		ClearCallStack();

		auto conditional = breakpoint->mCondition;
		if (conditional->mDbgEvaluationContext == NULL)
		{
			StringT<256> expr;
			StringT<256> subjectExpr;
			_SplitExpr(conditional->mExpr, expr, subjectExpr);

			conditional->mDbgEvaluationContext = new CeEvaluationContext(this, expr);
			conditional->mDbgEvaluationContext->mCallStackIdx = -1;
		}

		CeDbgState dbgState;
		BfTypedValue result = conditional->mDbgEvaluationContext->EvaluateInContext(BfTypedValue(), &dbgState);

		if (conditional->mDbgEvaluationContext->mPassInstance->HasFailed())
		{
			String errorStr = "FAILED";
			for (auto error : conditional->mDbgEvaluationContext->mPassInstance->mErrors)
			{
				if (!error->mIsWarning)
					errorStr = error->mError;
			}
			String condError = StrFormat("error Conditional breakpoint expression '%s' failed: %s", conditional->mExpr.c_str(), errorStr.c_str());
			mDebugManager->mOutMessages.push_back(condError);
			return true;
		}
		else if (dbgState.mBlockedSideEffects)
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' contained function calls, which is not allowed", conditional->mExpr.c_str()));
			return true;
		}
		else if ((!result) || (!result.mType->IsBoolean()))
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' must result in a boolean value", conditional->mExpr.c_str()));
			return true;
		}
		else if (ValueToInt(result) <= 0)
			return false;
	}

	breakpoint->mHitCount++;
	switch (breakpoint->mHitCountBreakKind)
	{
	case DbgHitCountBreakKind_Equals:
		if (breakpoint->mHitCount != breakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_GreaterEquals:
		if (breakpoint->mHitCount < breakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_Multiple:
		if ((breakpoint->mHitCount % breakpoint->mTargetHitCount) != 0)
			return false;
		break;
	}

	if (!breakpoint->mLogging.IsEmpty())
	{
		auto ceContext = mCeMachine->mCurContext;

		CeDbgState dbgState;
		dbgState.mActiveFrame = &ceContext->mCallStack[0];
		dbgState.mCeContext = mCeMachine->mCurContext;
		SetAndRestoreValue<CeDbgState*> prevDbgState(mCurDbgState, &dbgState);

		ClearCallStack();

		CeFormatInfo formatInfo;
		formatInfo.mCallStackIdx = -1;

		auto prevRunState = mRunState;
		mRunState = RunState_Paused; // We need to be paused to avoid certain errors in the eval
		String displayString;

		String expr;
		_SplitExpr(breakpoint->mLogging, expr, formatInfo.mSubjectExpr);

		ProcessEvalString(BfTypedValue(), expr, displayString, formatInfo, NULL, false);
		mRunState = prevRunState;

		displayString.Insert(0, "log ");
		displayString.Append("\n");

		mDebugManager->mOutMessages.push_back(displayString);

		if (!breakpoint->mBreakAfterLogging)
			return false;
	}

	return true;
}

Breakpoint* CeDebugger::CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset)
{
	ClearBreakpointCache();

	auto breakpoint = new CeBreakpoint();
	breakpoint->mFilePath = fileName;
	breakpoint->mRequestedLineNum = lineNum;
	breakpoint->mLineNum = lineNum;
	breakpoint->mColumn = wantColumn;
	breakpoint->mInstrOffset = instrOffset;
	mBreakpoints.Add(breakpoint);

	mBreakpointVersion++;

	return breakpoint;
}

Breakpoint* CeDebugger::CreateMemoryBreakpoint(intptr addr, int byteCount)
{
	return nullptr;
}

Breakpoint* CeDebugger::CreateSymbolBreakpoint(const StringImpl& symbolName)
{
	return nullptr;
}

Breakpoint* CeDebugger::CreateAddressBreakpoint(intptr address)
{
	return nullptr;
}

uintptr CeDebugger::GetBreakpointAddr(Breakpoint* breakpoint)
{
	if (mBreakpointCacheDirty)
	{
		UpdateBreakpointCache();
		UpdateBreakpointAddrs();
	}
	return breakpoint->GetAddr();
}

void CeDebugger::CheckBreakpoint(Breakpoint* breakpoint)
{
}

void CeDebugger::HotBindBreakpoint(Breakpoint* breakpoint, int lineNum, int hotIdx)
{
}

int64 CeDebugger::ValueToInt(addr_ce addr, BfType* type)
{
	if ((!type->IsInteger()) && (!type->IsBoolean()))
		return 0;

	auto primType = (BfPrimitiveType*)type;
	auto ceContext = mCeMachine->mCurContext;

	int64 val = 0;
	memcpy(&val, ceContext->mMemory.mVals + addr, type->mSize);

	switch (primType->mTypeDef->mTypeCode)
	{
	case BfTypeCode_Int8:
		val = *(int8*)&val;
		break;
	case BfTypeCode_Int16:
		val = *(int16*)&val;
		break;
	case BfTypeCode_Int32:
		val = *(int32*)&val;
		break;
	}
	return val;
}

int64 CeDebugger::ValueToInt(const BfTypedValue& typedVal)
{
	auto ceModule = mCeMachine->mCeModule;

	auto constant = ceModule->mBfIRBuilder->GetConstant(typedVal.mValue);
	if (constant == NULL)
		return 0;

	if (typedVal.IsAddr())
	{
		BfType* type = typedVal.mType;
		if (type->IsTypedPrimitive())
			type = type->GetUnderlyingType();

		if ((type->IsInteger()) || (type->IsBoolean()))
		{
			auto ceTypedVal = GetAddr(constant);
			if (ceTypedVal)
				return ValueToInt((addr_ce)ceTypedVal.mAddr, type);
		}

		return 0;
	}

	if (constant->mConstType == BfConstType_ExtractValue)
	{
		auto fromConstGEP = (BfConstantExtractValue*)constant;
		auto fromTarget = ceModule->mBfIRBuilder->GetConstantById(fromConstGEP->mTarget);
		if (fromTarget->mConstType == BfConstType_AggCE)
		{
			auto aggCE = (BfConstantAggCE*)fromTarget;

			auto dbgTypeInfo = GetDbgTypeInfo(aggCE->mType);
			if (dbgTypeInfo == NULL)
				return 0;

			auto typeInst = dbgTypeInfo->mType->ToTypeInstance();
			if (typeInst == NULL)
				return 0;

			auto fieldInfo = &dbgTypeInfo->mFieldOffsets[fromConstGEP->mIdx0];
			return ValueToInt(aggCE->mCEAddr + fieldInfo->mDataOffset, fieldInfo->mType);
		}
	}

	if ((BfIRConstHolder::IsInt(constant->mTypeCode)) || (constant->mTypeCode == BfTypeCode_Boolean))
		return constant->mInt64;
	return 0;
}

void CeDebugger::DeleteBreakpoint(Breakpoint* breakpoint)
{
	if (mActiveBreakpoint == breakpoint)
		mActiveBreakpoint = NULL;
	mBreakpoints.Remove((CeBreakpoint*)breakpoint);
	delete breakpoint;
	ClearBreakpointCache();
	mBreakpointVersion++;
}

void CeDebugger::DetachBreakpoint(Breakpoint* breakpoint)
{
}

void CeDebugger::MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	breakpoint->mLineNum = lineNum;
	breakpoint->mColumn = wantColumn;
	ClearBreakpointCache();
	mBreakpointVersion++;
}

void CeDebugger::MoveMemoryBreakpoint(Breakpoint* breakpoint, intptr addr, int byteCount)
{
}

void CeDebugger::DisableBreakpoint(Breakpoint* breakpoint)
{
}

void CeDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& conditionExpr)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	CeBreakpoint* wdBreakpoint = (CeBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);

	if (conditionExpr.empty())
	{
		delete wdBreakpoint->mCondition;
		CeBreakpoint* curBreakpoint = wdBreakpoint;
		wdBreakpoint->mCondition = NULL;
	}
	else
	{
		delete wdBreakpoint->mCondition;
		auto condition = new CeBreakpointCondition();
		condition->mExpr = conditionExpr;
		wdBreakpoint->mCondition = condition;
	}
}

void CeDebugger::SetBreakpointLogging(Breakpoint* breakpoint, const StringImpl& logging, bool breakAfterLogging)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	CeBreakpoint* wdBreakpoint = (CeBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);
	wdBreakpoint->mLogging = logging;
	wdBreakpoint->mBreakAfterLogging = breakAfterLogging;
}

Breakpoint* CeDebugger::FindBreakpointAt(intptr address)
{
	return nullptr;
}

Breakpoint* CeDebugger::GetActiveBreakpoint()
{
	return mActiveBreakpoint;
}

void CeDebugger::BreakAll()
{
	mCeMachine->mSpecialCheck = true;
	mCeMachine->mDbgWantBreak = true;
}

bool CeDebugger::TryRunContinue()
{
	return false;
}

bool CeDebugger::SetupStep(int frameIdx)
{
	auto ceFrame = GetFrame(frameIdx);
	if (ceFrame == NULL)
	{
		ContinueDebugEvent();
		return false;
	}

	int entryIdx = 0;
	auto curEntry = ceFrame->mFunction->FindEmitEntry(ceFrame->GetInstIdx(), &entryIdx);
	if (curEntry == NULL)
	{
		ContinueDebugEvent();
		return false;
	}

	auto ceMachine = mCeMachine;
	auto ceContext = mCeMachine->mCurContext;

	if (entryIdx < ceFrame->mFunction->mEmitTable.mSize - 1)
	{
		int checkIdx = entryIdx + 1;
		while (checkIdx < ceFrame->mFunction->mEmitTable.mSize)
		{
			auto checkEntry = &ceFrame->mFunction->mEmitTable[checkIdx];
			ceMachine->mStepState.mNextInstIdx = checkEntry->mCodePos;
			if ((checkEntry->mScope != curEntry->mScope) || (checkEntry->mLine != curEntry->mLine))
				break;
			++checkIdx;
		}
	}
	else
		ceMachine->mStepState.mNextInstIdx = ceFrame->mFunction->mCode.mSize;
	ceMachine->mStepState.mStartDepth = ceContext->mCallStack.mSize - frameIdx;
	ceMachine->mSpecialCheck = true;

	ContinueDebugEvent();
	return true;
}

void CeDebugger::StepInto(bool inAssembly)
{
	if (!SetupStep())
		return;
	mCeMachine->mStepState.mKind = inAssembly ? CeStepState::Kind_StepInfo_Asm : CeStepState::Kind_StepInfo;
}

void CeDebugger::StepIntoSpecific(intptr addr)
{
}

void CeDebugger::StepOver(bool inAssembly)
{
	if (!SetupStep())
		return;
	mCeMachine->mStepState.mKind = inAssembly ? CeStepState::Kind_StepOver_Asm : CeStepState::Kind_StepOver;
}

void CeDebugger::StepOut(bool inAssembly)
{
	if (!SetupStep(1))
		return;
	mCeMachine->mStepState.mKind = inAssembly ? CeStepState::Kind_StepOut_Asm : CeStepState::Kind_StepOut;
}

void CeDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
	auto ceFrame = GetFrame(0);
	if (ceFrame == NULL)
		return;

	if (inAssembly)
	{
		int32 instIdx = (int32)lineNumOrAsmAddr;
		if (instIdx < ceFrame->mFunction->mCode.mSize)
		{
			mCeMachine->mSpecialCheck = true;
			mCeMachine->mStepState.mKind = CeStepState::Kind_Jmp;
			mCeMachine->mStepState.mNextInstIdx = instIdx;
			ContinueDebugEvent();
		}
	}
	else
	{
		for (auto& emitEntry : ceFrame->mFunction->mEmitTable)
		{
			if (emitEntry.mScope == -1)
				continue;
			auto& scope = ceFrame->mFunction->mDbgScopes[emitEntry.mScope];
			if ((FileNameEquals(fileName, scope.mFilePath) && (emitEntry.mLine == lineNumOrAsmAddr)))
			{
				mCeMachine->mSpecialCheck = true;
				mCeMachine->mStepState.mKind = CeStepState::Kind_Jmp;
				mCeMachine->mStepState.mNextInstIdx = emitEntry.mCodePos;
				ContinueDebugEvent();
				return;
			}
		}
	}
}

CeFrame* CeDebugger::GetFrame(int callStackIdx)
{
	auto ceContext = mCeMachine->mCurContext;
	if (ceContext == NULL)
		return NULL;

	if ((callStackIdx == -1) && (!ceContext->mCallStack.IsEmpty()))
		return &ceContext->mCallStack.back();

	if (callStackIdx < mDbgCallStack.mSize)
	{
		int frameIdx = mDbgCallStack[callStackIdx].mFrameIdx;
		if (frameIdx < 0)
			return NULL;
		auto ceFrame = &ceContext->mCallStack[mDbgCallStack[callStackIdx].mFrameIdx];
		return ceFrame;
	}
	return NULL;
}

String CeDebugger::GetAutocompleteOutput(BfAutoComplete& autoComplete)
{
	String val = "\n:autocomplete\n";

	if (autoComplete.mInsertStartIdx != -1)
	{
		val += StrFormat("insertRange\t%d %d\n", autoComplete.mInsertStartIdx, autoComplete.mInsertEndIdx);
	}

	Array<AutoCompleteEntry*> entries;

	for (auto& entry : autoComplete.mEntriesSet)
	{
		entries.Add(&entry);
	}
	std::sort(entries.begin(), entries.end(), [](AutoCompleteEntry* lhs, AutoCompleteEntry* rhs)
		{
			return stricmp(lhs->mDisplay, rhs->mDisplay) < 0;
		});

	for (auto entry : entries)
	{
		val += String(entry->mEntryType);
		val += "\t";
		val += String(entry->mDisplay);
		val += "\n";
	}

	/*if (autoComplete.mEntries.size() != 0)
	{
		for (auto& entry : autoComplete.mEntries)
		{
			val += String(entry.mEntryType) + "\t" + String(entry.mDisplay) + "\n";
		}
	}*/
	return val;
}

String CeDebugger::DoEvaluate(CePendingExpr* pendingExpr, bool inCompilerThread)
{
	auto ceFrame = GetFrame(pendingExpr->mCallStackIdx);
	if (ceFrame == NULL)
	{
		return "!No comptime stack frame selected";
	}

	if (pendingExpr->mExprNode == NULL)
	{
		return "!No expression";
	}

	auto module = mCeMachine->mCeModule;

	if (!mCeMachine->mDbgPaused)
		pendingExpr->mPassInstance->ClearErrors();

	SetAndRestoreValue<BfTypeState*> prevTypeState(module->mContext->mCurTypeState, NULL);
	SetAndRestoreValue<BfConstraintState*> prevConstraintState(module->mContext->mCurConstraintState, NULL);
	SetAndRestoreValue<BfTypeInstance*> prevTypeInstance(module->mCurTypeInstance, ceFrame->mFunction->mMethodInstance->GetOwner());
	SetAndRestoreValue<BfMethodInstance*> prevMethodInstance(module->mCurMethodInstance, ceFrame->mFunction->mMethodInstance);
	SetAndRestoreValue<BfPassInstance*> prevPassInstance(mCompiler->mPassInstance, pendingExpr->mPassInstance);
	SetAndRestoreValue<bool> prevIgnoreWrites(module->mBfIRBuilder->mIgnoreWrites, true);

	BfMethodState methodState;
	SetAndRestoreValue<BfMethodState*> prevMethodState(module->mCurMethodState, &methodState);
	methodState.mTempKind = module->mCurMethodInstance->mMethodDef->mIsStatic ? BfMethodState::TempKind_Static : BfMethodState::TempKind_NonStatic;

	CeDbgState dbgState;
	dbgState.mActiveFrame = ceFrame;
	dbgState.mCeContext = mCeMachine->mCurContext;
	dbgState.mExplicitThis = pendingExpr->mFormatInfo.mExplicitThis;
	dbgState.mDbgExpressionFlags = pendingExpr->mExpressionFlags;
	if (!inCompilerThread)
		dbgState.mDbgExpressionFlags = (DwEvalExpressionFlags)(dbgState.mDbgExpressionFlags & ~(DwEvalExpressionFlag_AllowCalls | DwEvalExpressionFlag_AllowPropertyEval));
	dbgState.mFormatInfo = &pendingExpr->mFormatInfo;
	SetAndRestoreValue<CeDbgState*> prevDbgState(mCurDbgState, &dbgState);

	BfAutoComplete autoComplete;
	autoComplete.mModule = module;
	autoComplete.mCompiler = module->mCompiler;
	autoComplete.mSystem = module->mSystem;

	BfResolvePassData resolvePass;
	resolvePass.mParsers.Add(pendingExpr->mParser);
	resolvePass.mAutoComplete = &autoComplete;

	SetAndRestoreValue<BfResolvePassData*> prevResolvePass;
	if (pendingExpr->mCursorPos != -1)
	{
		pendingExpr->mParser->mParserFlags = (BfParserFlag)(pendingExpr->mParser->mParserFlags | ParserFlag_Autocomplete);
		pendingExpr->mParser->mCursorIdx = pendingExpr->mCursorPos + 1;
		pendingExpr->mParser->mCursorCheckIdx = pendingExpr->mCursorPos + 1;
		prevResolvePass.Init(module->mCompiler->mResolvePassData, &resolvePass);
	}

	BfTypedValue exprResult;
	BfTypedValue origExprResult;

	if (auto typeRef = BfNodeDynCast<BfTypeReference>(pendingExpr->mExprNode))
	{
		auto resultType = mCeMachine->mCeModule->ResolveTypeRef(typeRef);
		if (resultType != NULL)
			exprResult = BfTypedValue(resultType);
	}
	else
	{
		BfExprEvaluator exprEvaluator(mCeMachine->mCeModule);
		exprEvaluator.mBfEvalExprFlags = (BfEvalExprFlags)(exprEvaluator.mBfEvalExprFlags | BfEvalExprFlags_Comptime);
		exprEvaluator.VisitChildNoRef(pendingExpr->mExprNode);
		exprResult = exprEvaluator.GetResult();
		origExprResult = exprResult;
		if ((exprResult) && (!exprResult.mType->IsComposite()))
			exprResult = module->LoadValue(exprResult);
		module->FixIntUnknown(exprResult);
	}

	if (dbgState.mBlockedSideEffects)
	{
		if ((mCeMachine->mDbgPaused) && ((pendingExpr->mExpressionFlags & DwEvalExpressionFlag_AllowCalls) != 0))
		{
			// Reprocess in compiler thread
			return "!pending";
		}

		return "!sideeffects";
	}

	if (!exprResult)
	{
		auto resultType = mCeMachine->mCeModule->ResolveTypeRef(pendingExpr->mExprNode, {}, BfPopulateType_Data, BfResolveTypeRefFlag_IgnoreLookupError);
		if (resultType != NULL)
		{
			exprResult = BfTypedValue(resultType);
			pendingExpr->mPassInstance->ClearErrors();
		}
	}

	if ((exprResult.mType == NULL) && (dbgState.mReferencedIncompleteTypes))
	{
		if ((mCeMachine->mDbgPaused) && ((pendingExpr->mExpressionFlags & DwEvalExpressionFlag_AllowCalls) != 0))
		{
			// Reprocess in compiler thread
			return "!pending";
		}
		return "!incomplete";
	}

	String val;
	if (pendingExpr->mPassInstance->HasFailed())
	{
		BfLogDbgExpr("Evaluate Failed: %s\n", pendingExpr->mPassInstance->mErrors[0]->mError.c_str());
		String errorStr = pendingExpr->mPassInstance->mErrors[0]->mError;
		for (auto moreInfo : pendingExpr->mPassInstance->mErrors[0]->mMoreInfo)
		{
			errorStr += " ";
			errorStr += moreInfo->mInfo;
		}

		errorStr.Replace('\n', ' ');
		val = StrFormat("!%d\t%d\t%s", pendingExpr->mPassInstance->mErrors[0]->GetSrcStart(), pendingExpr->mPassInstance->mErrors[0]->GetSrcLength(), errorStr.c_str());
	}
	else if ((!exprResult) && (!exprResult.IsNoValueType()))
	{
		return "!Debug evaluate failed";
	}
	else
	{
		CeTypeModKind typeModKind = CeTypeModKind_Normal;
		if ((origExprResult) && (!origExprResult.IsAddr()) && (origExprResult.mType->IsValueType()))
			typeModKind = CeTypeModKind_Const;
		else if (origExprResult.IsReadOnly())
			typeModKind = CeTypeModKind_ReadOnly;
		val = TypedValueToString(exprResult, pendingExpr->mExprNode->ToString(), pendingExpr->mFormatInfo, (pendingExpr->mExpressionFlags & DwEvalExpressionFlag_FullPrecision) != 0, typeModKind);

		if ((!val.empty()) && (val[0] == '!'))
			return val;

		if (pendingExpr->mFormatInfo.mRawString)
			return val;

		if (pendingExpr->mPassInstance->HasMessages())
		{
			for (auto error : pendingExpr->mPassInstance->mErrors)
			{
				if (error->mIsWarning)
				{
					val += "\n:warn\t";
					val += error->mError;
				}
			}
		}

		if (!pendingExpr->mFormatInfo.mReferenceId.empty())
			val += "\n:referenceId\t" + pendingExpr->mFormatInfo.mReferenceId;

		// 	if ((exprResult.mSrcAddress != 0) && (HasMemoryBreakpoint(exprResult.mSrcAddress, exprResult.mType->GetByteCount())))
		// 		val += StrFormat("\n:break\t%@", exprResult.mSrcAddress);

		auto checkType = exprResult.mType;
		if (checkType->IsObject())
			val += "\n:type\tobject";
		else if (checkType->IsPointer())
			val += "\n:type\tpointer";
		else if (checkType->IsInteger())
			val += "\n:type\tint";
		else if (checkType->IsFloat())
			val += "\n:type\tfloat";
	}

	if (dbgState.mHadSideEffects)
		val += "\n:sideeffects";

// 	auto resultConstant = module->mBfIRBuilder->GetConstant(exprResult.mValue);
// 	if (resultConstant != NULL)
// 	{
// 		auto ceResultTyped = GetAddr(resultConstant);
// 		if (ceResultTyped.mAddr != 0)
// 			val += "\n:canEdit";
// 	}

	if ((origExprResult.mType != NULL) && (!origExprResult.mType->IsComposite()) && (!origExprResult.mType->IsRef()) &&
		(origExprResult.IsAddr()) && (!origExprResult.IsReadOnly()))
	{
		val += "\n:canEdit";
	}

	if (pendingExpr->mCursorPos != -1)
		val += GetAutocompleteOutput(autoComplete);

	return val;
}

String CeDebugger::Evaluate(const StringImpl& expr, CeFormatInfo formatInfo, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	BP_ZONE_F("WinDebugger::Evaluate %s", BP_DYN_STR(expr.c_str()));

	AutoCrit autoCrit(mCeMachine->mCritSect);

	if ((expressionFlags & DwEvalExpressionFlag_RawStr) != 0)
	{
		formatInfo.mRawString = true;
	}

	auto ceContext = mCeMachine->mCurContext;
	bool valIsAddr = false;

	BfParser* parser = new BfParser(mCompiler->mSystem);

	BfPassInstance* bfPassInstance = new BfPassInstance(mCompiler->mSystem);

	auto parseAsType = false;

	auto terminatedExpr = expr;
	terminatedExpr.Trim();

	if (terminatedExpr.StartsWith("!"))
	{
		if (terminatedExpr == "!info")
		{
			auto ceFrame = GetFrame(callStackIdx);
			if (ceFrame != NULL)
			{
				if (ceFrame->mFunction->mCeFunctionInfo->mMethodInstance != NULL)
					OutputMessage(StrFormat("Module: %s\n", ceFrame->mFunction->mCeFunctionInfo->mMethodInstance->GetOwner()->mModule->mModuleName.c_str()));
				OutputMessage(StrFormat("Link Name: %s\n", ceFrame->mFunction->mCeFunctionInfo->mName.c_str()));
			}
			return "";
		}
	}

	if (terminatedExpr.EndsWith(">"))
		parseAsType = true;
	else if ((terminatedExpr.StartsWith("comptype(")) && (!terminatedExpr.Contains('.')))
		parseAsType = true;

	if (!parseAsType)
		terminatedExpr += ";";
	if ((terminatedExpr.length() > 2) && (terminatedExpr[0] == '@'))
	{
		if (terminatedExpr[1] == '!') // Return string as error
		{
			int errorEnd = (int)terminatedExpr.IndexOf("@!", 2);
			if (errorEnd != -1)
				return terminatedExpr.Substring(1, errorEnd - 1);
			else
				return terminatedExpr.Substring(1);
		}
		else if (terminatedExpr[1] == '>') // Return string as text
		{
			int errorEnd = (int)terminatedExpr.IndexOf("@>", 2);
			if (errorEnd != -1)
				return terminatedExpr.Substring(2, errorEnd - 1);
			else
				return terminatedExpr.Substring(2);
		}
	}

	parser->SetSource(terminatedExpr.c_str(), (int)terminatedExpr.length());
	parser->Parse(bfPassInstance);

	BfReducer bfReducer;
	bfReducer.mAlloc = parser->mAlloc;
	bfReducer.mSystem = mCompiler->mSystem;
	bfReducer.mPassInstance = bfPassInstance;
	bfReducer.mVisitorPos = BfReducer::BfVisitorPos(parser->mRootNode);
	bfReducer.mVisitorPos.MoveNext();
	bfReducer.mSource = parser;
	BfAstNode* exprNode = NULL;
	if (parseAsType)
		exprNode = bfReducer.CreateTypeRef(parser->mRootNode->mChildArr.GetAs<BfAstNode*>(0));
	else
		exprNode = bfReducer.CreateExpression(parser->mRootNode->mChildArr.GetAs<BfAstNode*>(0));
	parser->Close();

	formatInfo.mCallStackIdx = callStackIdx;

	CePendingExpr* pendingExpr = new CePendingExpr();
	pendingExpr->mPassInstance = bfPassInstance;
	pendingExpr->mParser = parser;
	pendingExpr->mCallStackIdx = callStackIdx;
	pendingExpr->mCursorPos = cursorPos;
	pendingExpr->mExpressionFlags = expressionFlags;
	pendingExpr->mExprNode = exprNode;

	BfType* explicitType = NULL;
	String formatFlags;
	String assignExpr;
	int assignExprOffset = -1;

	if ((exprNode != NULL) && (exprNode->GetSrcEnd() < (int)expr.length()))
	{
		int formatOffset = exprNode->GetSrcEnd();
		while (formatOffset < (int)expr.length())
		{
			char c = expr[formatOffset];
			if (c == ' ')
				formatOffset++;
			else
				break;
		}

		formatFlags = Trim(expr.Substring(formatOffset));
		bool isComplexType = false;
		for (char c : formatFlags)
			if (c == '>')
				isComplexType = true;
		if (isComplexType)
		{
			//explicitType = dbgModule->FindType(expr);
		}

		if ((explicitType == NULL) && (formatFlags.length() > 0))
		{
			String errorString = "Invalid expression";
 			if (!ParseFormatInfo(formatFlags, &formatInfo, bfPassInstance, &assignExprOffset, &assignExpr, &errorString))
 			{
 				if (formatInfo.mRawString)
 					return "";
 				bfPassInstance->FailAt(errorString, parser->mSourceData, exprNode->GetSrcEnd(), (int)expr.length() - exprNode->GetSrcEnd());
 				formatFlags = "";
 			}
			if (assignExprOffset != -1)
				assignExprOffset += formatOffset;
		}
	}

	if (assignExpr.length() > 0)
	{
		String newEvalStr = exprNode->ToString() + " = ";
		int errorOffset = (int)newEvalStr.length();
		newEvalStr += assignExpr;
		String result = Evaluate(newEvalStr, formatInfo, callStackIdx, cursorPos, language, expressionFlags);
		if (result[0] == '!')
		{
			int tabPos = (int)result.IndexOf('\t');
			if (tabPos > 0)
			{
				int errorStart = atoi(result.Substring(1, tabPos - 1).c_str());
				if (errorStart >= errorOffset)
				{
					result = StrFormat("!%d", errorStart - errorOffset + assignExprOffset) + result.Substring(tabPos);
				}
			}
		}
		return result;
	}

	pendingExpr->mExplitType = explicitType;
	pendingExpr->mFormatInfo = formatInfo;

	String result = DoEvaluate(pendingExpr, false);
	if (result == "!pending")
	{
		BF_ASSERT(mDebugPendingExpr == NULL);
		if (mDebugPendingExpr != NULL)
		{
			return "!retry"; // We already have a pending
		}
		mDebugPendingExpr = pendingExpr;
		mCeMachine->mStepState.mKind = CeStepState::Kind_Evaluate;
		mRunState = RunState_DebugEval;
		ContinueDebugEvent();
	}
	else
		delete pendingExpr;
	return result;
}

void CeDebugger::ClearBreakpointCache()
{
	if (!mCeMachine->mDbgPaused)
		mCeMachine->mSpecialCheck = true;
	mBreakpointFramesDirty = true;
	mBreakpointCacheDirty = true;
	for (auto kv : mFileInfo)
		delete kv.mValue;
	mFileInfo.Clear();
}

void CeDebugger::UpdateBreakpointAddrs()
{
	for (auto breakpoint : mBreakpoints)
	{
		breakpoint->mCurBindAddr = 1;
	}

	CeFunction* ceFunction = NULL;
	if (!mCeMachine->mFunctionIdMap.TryGetValue(mCurDisasmFuncId, &ceFunction))
		return;

	if (ceFunction->mBreakpointVersion != mBreakpointVersion)
		UpdateBreakpoints(ceFunction);

	for (auto kv : ceFunction->mBreakpoints)
		kv.mValue.mBreakpoint->mCurBindAddr = ((intptr)ceFunction->mId << 32) | kv.mKey;
}

void CeDebugger::UpdateBreakpointCache()
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	mBreakpointCacheDirty = false;
	if (!mFileInfo.IsEmpty())
		return;
	for (int i = 0; i < (int)mBreakpoints.mSize; i++)
	{
		auto breakpoint = mBreakpoints[i];
		breakpoint->mIdx = i;
		String fileName = breakpoint->mFilePath;
		fileName = FixPathAndCase(fileName);
		CeFileInfo** valuePtr;
		CeFileInfo* fileInfo = NULL;
		if (mFileInfo.TryAdd(fileName, NULL, &valuePtr))
		{
			fileInfo = new CeFileInfo();
			*valuePtr = fileInfo;
		}
		else
			fileInfo = *valuePtr;
		fileInfo->mOrderedBreakpoints.Add(breakpoint);
	}

	for (auto kv : mFileInfo)
	{
		kv.mValue->mOrderedBreakpoints.Sort([](CeBreakpoint* lhs, CeBreakpoint* rhs)
			{
				return lhs->mLineNum < rhs->mLineNum;
			});
	}
}

static int CompareBreakpoint(CeBreakpoint* breakpoint, const int& lineNum)
{
	return breakpoint->mRequestedLineNum - lineNum;
}

void CeDebugger::UpdateBreakpoints(CeFunction* ceFunction)
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	UpdateBreakpointCache();

	ceFunction->UnbindBreakpoints();

	String path;
	int scope = -1;
	CeFileInfo* ceFileInfo = NULL;

	BitSet usedBreakpointSet(mBreakpoints.mSize);

	for (auto& emitEntry : ceFunction->mEmitTable)
	{
		if (emitEntry.mScope != scope)
		{
			if (emitEntry.mScope != -1)
			{
				path = FixPathAndCase(ceFunction->mDbgScopes[emitEntry.mScope].mFilePath);
				if (!mFileInfo.TryGetValue(path, &ceFileInfo))
					ceFileInfo = NULL;
			}
			else
				ceFileInfo = NULL;
			scope = emitEntry.mScope;
		}

		if (ceFileInfo != NULL)
		{
			int idx = ceFileInfo->mOrderedBreakpoints.BinarySearchAlt<int>(emitEntry.mLine, CompareBreakpoint);
			if (idx < 0)
				idx = ~idx - 1;

			while (idx > 0)
			{
				auto breakpoint = ceFileInfo->mOrderedBreakpoints[idx - 1];
				if (breakpoint->mLineNum < emitEntry.mLine)
					break;
				idx--;
			}

			int tryBindCount = 0;
			int bestRequestedBindLine = 0;

			while ((idx >= 0) && (idx < ceFileInfo->mOrderedBreakpoints.mSize))
			{
				auto breakpoint = ceFileInfo->mOrderedBreakpoints[idx];
				if (usedBreakpointSet.IsSet(breakpoint->mIdx))
				{
					idx++;
					continue;
				}
				CeBreakpointBind* breakpointBind = NULL;

				if (tryBindCount > 0)
				{
					if (breakpoint->mRequestedLineNum > bestRequestedBindLine)
						break;
				}
				else
				{
					int lineDiff = emitEntry.mLine - breakpoint->mLineNum;
					if ((lineDiff < 0) || (lineDiff > 4))
						break;

					if ((breakpoint->mHasBound) && (lineDiff != 0))
						break;

					bestRequestedBindLine = breakpoint->mRequestedLineNum;
				}

				tryBindCount++;

				int codePos = emitEntry.mCodePos;
				if (breakpoint->mInstrOffset > 0)
				{
					int instrOffsetLeft = breakpoint->mInstrOffset;
					while (instrOffsetLeft > 0)
					{
						auto& opRef = *(CeOp*)(&ceFunction->mCode[codePos]);
						int instSize = mCeMachine->GetInstSize(ceFunction, codePos);
						codePos += instSize;
						instrOffsetLeft--;
					}
				}

				if (ceFunction->mBreakpoints.TryAdd(codePos, NULL, &breakpointBind))
				{
					usedBreakpointSet.Set(breakpoint->mIdx);
					breakpoint->mLineNum = emitEntry.mLine;
					breakpoint->mHasBound = true;

					auto& opRef = *(CeOp*)(&ceFunction->mCode[codePos]);
					breakpointBind->mPrevOpCode = opRef;
					breakpointBind->mBreakpoint = breakpoint;
					opRef = CeOp_DbgBreak;
				}

				idx++;
			}
		}
	}

	ceFunction->mBreakpointVersion = mBreakpointVersion;
}

CeDbgTypeInfo* CeDebugger::GetDbgTypeInfo(int typeId)
{
	CeDbgTypeInfo* dbgTypeInfo = NULL;
	if (mDbgTypeInfoMap.TryAdd(typeId, NULL, &dbgTypeInfo))
	{
		auto type = mCeMachine->mCeModule->mContext->FindTypeById(typeId);
		if (type == NULL)
		{
			mDbgTypeInfoMap.Remove(typeId);
			return NULL;
		}
		dbgTypeInfo->mType = type;

		auto typeInst = type->ToTypeInstance();
		if (typeInst != NULL)
		{
			for (int fieldIdx = 0; fieldIdx < typeInst->mFieldInstances.mSize; fieldIdx++)
			{
				auto& fieldInst = typeInst->mFieldInstances[fieldIdx];
				if (fieldInst.mDataIdx >= 0)
				{
					while (fieldInst.mDataIdx >= dbgTypeInfo->mFieldOffsets.mSize)
						dbgTypeInfo->mFieldOffsets.Add(CeDbgFieldEntry());
					dbgTypeInfo->mFieldOffsets[fieldInst.mDataIdx].mType = fieldInst.mResolvedType;
					dbgTypeInfo->mFieldOffsets[fieldInst.mDataIdx].mDataOffset = fieldInst.mDataOffset;
				}

				if (fieldInst.mConstIdx != -1)
				{
					auto constant = typeInst->mConstHolder->GetConstantById(fieldInst.mConstIdx);
					if ((constant != NULL) && (BfIRConstHolder::IsInt(constant->mTypeCode)))
					{
						CeDbgTypeInfo::ConstIntEntry constIntEntry;
						constIntEntry.mFieldIdx = fieldIdx;
						constIntEntry.mVal = constant->mInt64;
						dbgTypeInfo->mConstIntEntries.Add(constIntEntry);
					}
				}
			}
		}
	}
	return dbgTypeInfo;
}

CeDbgTypeInfo* CeDebugger::GetDbgTypeInfo(BfIRType irType)
{
	if ((irType.mKind == BfIRTypeData::TypeKind_TypeId) || (irType.mKind == BfIRTypeData::TypeKind_TypeInstId))
		return GetDbgTypeInfo(irType.mId);
	if (irType.mKind == BfIRTypeData::TypeKind_TypeInstPtrId)
	{
		auto type = mCeMachine->mCeModule->mContext->FindTypeById(irType.mId);
		if (type->IsObjectOrInterface())
			return GetDbgTypeInfo(irType.mId);
		else
			return GetDbgTypeInfo(mCeMachine->mCeModule->CreatePointerType(type)->mTypeId);
	}
	return NULL;
}

static bool IsNormalChar(uint32 c)
{
	return (c < 0x80);
}

template <typename T>
static String IntTypeToString(T val, const StringImpl& name, DwDisplayInfo* displayInfo, CeFormatInfo& formatInfo)
{
	auto intDisplayType = displayInfo->mIntDisplayType;
	if (formatInfo.mDisplayType == DwDisplayType_Decimal)
		intDisplayType = DwIntDisplayType_Decimal;
	else if (formatInfo.mDisplayType == DwDisplayType_HexUpper)
		intDisplayType = DwIntDisplayType_HexadecimalUpper;
	else if (formatInfo.mDisplayType == DwDisplayType_HexLower)
		intDisplayType = DwIntDisplayType_HexadecimalLower;

	if (intDisplayType == DwIntDisplayType_Binary)
	{
		String binary;
		for (int i = 0; i < sizeof(T) * 8; i++)
		{
			if ((i != 0) && (i % 4 == 0))
				binary = "'" + binary;

			if ((i != 0) && (i % 16 == 0))
				binary = "'" + binary;

			binary = ((val & ((T)1 << i)) ? "1" : "0") + binary;
		}
		return StrFormat("0b'%s\n%s", binary.c_str(), name.c_str());
	}

	if (intDisplayType == DwIntDisplayType_Octal)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0o%%lo\n%s", name.c_str());
		}
		else
			format = StrFormat("0o%%0%do\n%s", sizeof(val) * 2, name.c_str());
		return StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val));
	}

	if (intDisplayType == DwIntDisplayType_HexadecimalUpper)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0x%%l@\n%s", name.c_str());
		}
		else
			format = StrFormat("0x%%0%dX\n%s", sizeof(val) * 2, name.c_str());
		return StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val));
	}

	//TODO: Implement HexadecimalLower
	if (intDisplayType == DwIntDisplayType_HexadecimalLower)
	{
		String format;
		if (sizeof(T) == 8)
		{
			format = StrFormat("0x%%l@\n%s", name.c_str());
		}
		else
			format = StrFormat("0x%%0%dX\n%s", sizeof(val) * 2, name.c_str());
		return StrFormat(format.c_str(), (typename std::make_unsigned<T>::type)(val));
	}

	if (std::is_unsigned<T>::value)
	{
		if (sizeof(T) == 8)
		{
			if (val > 0x7FFFFFFFF)
				return StrFormat("%llu\n%s\n:editVal\t%lluUL", val, name.c_str(), val);
			else
				return StrFormat("%llu\n%s", val, name.c_str());
		}
		else
			return StrFormat("%u\n%s", val, name.c_str());
	}
	else
	{
		if (sizeof(T) == 8)
		{
			if ((val > 0x7FFFFFFFF) || (val < -0x80000000LL))
				return StrFormat("%lld\n%s\n:editVal\t%lldL", val, name.c_str(), val);
			else
				return StrFormat("%lld\n%s", val, name.c_str(), val);
		}
		else
			return StrFormat("%d\n%s", val, name.c_str());
	}
}

DwDisplayInfo* CeDebugger::GetDisplayInfo(const StringImpl& referenceId)
{
	DwDisplayInfo* displayInfo = &mDebugManager->mDefaultDisplayInfo;
	if (!referenceId.empty())
	{
		mDebugManager->mDisplayInfos.TryGetValue(referenceId, &displayInfo);
	}
	return displayInfo;
}

String CeDebugger::GetMemberList(BfType* type, addr_ce addr, addr_ce addrInst, bool isStatic)
{
	auto typeInst = type->ToTypeInstance();
	if (typeInst == NULL)
		return "";

	auto module = typeInst->mModule;

	String retVal;
	int fieldCount = 0;

	if ((!isStatic) && (typeInst->mBaseType != NULL) && (!typeInst->mBaseType->IsInstanceOf(mCompiler->mValueTypeTypeDef)))
	{
		retVal += StrFormat("[base]\tthis,this=%d@0x%X, nd, na, nv", typeInst->mBaseType->mTypeId, addr);
		fieldCount++;
	}

	auto ceContext = mCompiler->mCeMachine->mCurContext;
	bool didStaticCtor = ceContext->mStaticCtorExecSet.Contains(type->mTypeId);

	bool hasStaticFields = false;
	for (auto& fieldInst : typeInst->mFieldInstances)
	{
		auto fieldDef = fieldInst.GetFieldDef();
		if (fieldDef == NULL)
			continue;

		if (fieldDef->mIsStatic != isStatic)
		{
			if (fieldDef->mIsStatic)
				hasStaticFields = true;
			continue;
		}

		if (fieldCount > 0)
			retVal += "\n";

		retVal += fieldDef->mName;
		if (fieldDef->mIsStatic)
		{
			if (didStaticCtor)
				retVal += StrFormat("\tcomptype(%d).%s", type->mTypeId, fieldDef->mName.c_str());
			else
				retVal += StrFormat("\tcomptype(%d).%s", type->mTypeId, fieldDef->mName.c_str());
		}
		else
		{
			retVal += "\t";
			retVal += fieldDef->mName;
			retVal += StrFormat(",this=%d@0x%X", typeInst->mTypeId, addrInst);
		}

		fieldCount++;
	}

	if (hasStaticFields)
	{
		if (fieldCount > 0)
			retVal += "\n";

		retVal += StrFormat("Static values\tcomptype(%d)", typeInst->mTypeId);
	}

	return retVal;
}

bool CeDebugger::ParseFormatInfo(const StringImpl& formatInfoStr, CeFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExprString, String* errorString, BfTypedValue contextTypedValue)
{
	String formatFlags = formatInfoStr;
	if (assignExprOffset != NULL)
		*assignExprOffset = -1;

	while (formatFlags.length() > 0)
	{
		formatFlags = Trim(formatFlags);
		if (formatFlags.IsEmpty())
			break;
		if (formatFlags[0] != ',')
		{
			return false;
		}
		else
		{
			int nextComma = (int)formatFlags.IndexOf(',', 1);
			int quotePos = (int)formatFlags.IndexOf('"', 1);
			if ((quotePos != -1) && (quotePos < nextComma))
			{
				int nextQuotePos = (int)formatFlags.IndexOf('"', quotePos + 1);
				if (nextQuotePos != -1)
					nextComma = (int)formatFlags.IndexOf(',', nextQuotePos + 1);
			}
			if (nextComma == -1)
				nextComma = (int)formatFlags.length();

			String formatCmd = formatFlags.Substring(1, nextComma - 1);
			formatCmd = Trim(formatCmd);
			bool hadError = false;

			if (strncmp(formatCmd.c_str(), "this=", 5) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String thisExpr = formatCmd.Substring(5);
				if (thisExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, thisExpr, formatInfo);
				formatInfo->mExplicitThis = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = thisExpr.Substring(dbgEvaluationContext.mExprString.GetLength());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "count=", 6) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(6);
				if (countExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, countExpr, formatInfo);
				BfTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mOverrideCount = (intptr)ValueToInt(countValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprString.GetLength());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "maxcount=", 9) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(9);
				if (countExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, countExpr, formatInfo);
				BfTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mMaxCount = (intptr)ValueToInt(countValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprString.GetLength());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "arraysize=", 10) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(10);
				if (countExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, countExpr, formatInfo);
				BfTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)ValueToInt(countValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprString.GetLength());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "assign=", 7) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String assignExpr = formatCmd.Substring(7);
				if (assignExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, assignExpr, formatInfo);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				if (assignExprOffset != NULL)
				{
					//TODO: Keep track of the offset directly, this is a hack
					*assignExprOffset = (int)formatInfoStr.IndexOf("assign=") + 7;
				}
				if (assignExprString != NULL)
					*assignExprString = dbgEvaluationContext.mExprNode->ToString();
				formatFlags = assignExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "refid=", 6) == 0)
			{
				formatInfo->mReferenceId = formatCmd.Substring(6);
				if (formatInfo->mReferenceId[0] == '\"')
					formatInfo->mReferenceId = formatInfo->mReferenceId.Substring(1, formatInfo->mReferenceId.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "_=", 2) == 0)
			{
				formatInfo->mSubjectExpr = formatCmd.Substring(2);
				if (formatInfo->mSubjectExpr[0] == '\"')
					formatInfo->mSubjectExpr = formatInfo->mSubjectExpr.Substring(1, formatInfo->mSubjectExpr.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "expectedType=", 13) == 0)
			{
				formatInfo->mExpectedType = formatCmd.Substring(13);
				if (formatInfo->mExpectedType[0] == '\"')
					formatInfo->mExpectedType = formatInfo->mExpectedType.Substring(1, formatInfo->mExpectedType.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "namespaceSearch=", 16) == 0)
			{
				formatInfo->mNamespaceSearch = formatCmd.Substring(16);
				if (formatInfo->mNamespaceSearch[0] == '\"')
					formatInfo->mNamespaceSearch = formatInfo->mNamespaceSearch.Substring(1, formatInfo->mNamespaceSearch.length() - 2);
			}
			else if (formatCmd == "d")
			{
				formatInfo->mDisplayType = DwDisplayType_Decimal;
			}
			else if (formatCmd == "x")
			{
				formatInfo->mDisplayType = DwDisplayType_HexLower;
			}
			else if (formatCmd == "X")
			{
				formatInfo->mDisplayType = DwDisplayType_HexUpper;
			}
			else if (formatCmd == "s")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Ascii;
			}
			else if (formatCmd == "s8")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf8;
			}
			else if (formatCmd == "s16")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf16;
			}
			else if (formatCmd == "s32")
			{
				formatInfo->mHidePointers = true;
				formatInfo->mDisplayType = DwDisplayType_Utf32;
			}
			else if (formatCmd == "nd")
			{
				formatInfo->mIgnoreDerivedClassInfo = true;
			}
			else if (formatCmd == "na")
			{
				formatInfo->mHidePointers = true;
			}
			else if (formatCmd == "nm")
			{
				formatInfo->mNoMembers = true;
			}
			else if (formatCmd == "ne")
			{
				formatInfo->mNoEdit = true;
			}
			else if (formatCmd == "nv")
			{
				formatInfo->mNoVisualizers = true;
			}
			else if (formatCmd == "rawStr")
			{
				formatInfo->mRawString = true;
			}
			else if (((!formatCmd.IsEmpty()) && ((formatCmd[0] >= '0') && (formatCmd[0] <= '9'))) ||
				(formatCmd.StartsWith("(")))
			{
				String countExpr = formatCmd;
				if (countExpr.empty())
					break;
				CeEvaluationContext dbgEvaluationContext(this, countExpr, formatInfo);
				BfTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)ValueToInt(countValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = dbgEvaluationContext.mExprString;
				continue;
			}
			else
				hadError = true;

			if (hadError)
			{
				if (errorString != NULL)
					*errorString = "Invalid format flags";
				return false;
			}

			formatFlags = formatFlags.Substring(nextComma);
		}
	}
	return true;
}

String CeDebugger::MaybeQuoteFormatInfoParam(const StringImpl& str)
{
	bool needsQuote = false;
	for (int i = 0; i < (int)str.length(); i++)
	{
		char c = str[i];
		if (c == ',')
			needsQuote = true;
	}
	if (!needsQuote)
		return str;

	String qStr = "\"";
	qStr += str;
	qStr += "\"";
	return qStr;
}

BfTypedValue CeDebugger::EvaluateInContext(const BfTypedValue& contextTypedValue, const StringImpl& subExpr, CeFormatInfo* formatInfo, String* outReferenceId, String* outErrors)
{
	CeEvaluationContext dbgEvaluationContext(this, subExpr, formatInfo, contextTypedValue);
// 	if (formatInfo != NULL)
// 	{
// 		dbgEvaluationContext.mDbgExprEvaluator->mSubjectExpr = formatInfo->mSubjectExpr;
// 	}
	//dbgEvaluationContext.mDbgExprEvaluator->mReferenceId = outReferenceId;

	SetAndRestoreValue<CeEvaluationContext*> prevEvalContext(mCurEvaluationContext, &dbgEvaluationContext);

	//mCountResultOverride = -1;
	auto result = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
// 	if ((formatInfo != NULL) && (dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride != -1))
// 		formatInfo->mOverrideCount = dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride;
	if (dbgEvaluationContext.mPassInstance->HasFailed())
	{
		if (outErrors != NULL)
		{
			int errIdx = 0;
			for (auto err : dbgEvaluationContext.mPassInstance->mErrors)
			{
				if (errIdx > 0)
					(*outErrors) += "\n";
				(*outErrors) += err->mError;
				errIdx++;
			}
		}
		return BfTypedValue();
	}
	return result;
}

void CeDebugger::DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors)
{
	bool onlyMemError = errors.StartsWith("Failed to read") && !errors.Contains('\n');
	if ((!debugVis->mShowedError) && (!onlyMemError))
	{
		debugVis->mShowedError = true;
		String errStr = StrFormat("DbgVis '%s' failed while evaluating condition '%s'\n", debugVis->mName.c_str(), evalString.c_str());
		String spacedErrors = errors;
		spacedErrors.Insert(0, " ");
		spacedErrors.Replace("\n", "\n ");
		errStr += spacedErrors;
		OutputMessage(errStr);
	}
}

bool CeDebugger::EvalCondition(DebugVisualizerEntry* debugVis, BfTypedValue typedVal, CeFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr)
{
	auto ceModule = mCeMachine->mCeModule;

	CeFormatInfo displayStrFormatInfo = formatInfo;
	displayStrFormatInfo.mHidePointers = false;
	displayStrFormatInfo.mRawString = false;

	String errors;
	const String conditionStr = mDebugManager->mDebugVisualizers->DoStringReplace(condition, dbgVisWildcardCaptures);
	BfTypedValue evalResult = EvaluateInContext(typedVal, conditionStr, &displayStrFormatInfo, NULL, &errors);
	if ((!evalResult) || (!evalResult.mType->IsBoolean()))
	{
		if (formatInfo.mRawString)
			return false;

		errorStr += "<DbgVis Failed>";
		DbgVisFailed(debugVis, conditionStr, errors);
		return false;
	}

	evalResult = ceModule->LoadValue(evalResult);
	if (auto constant = ceModule->mBfIRBuilder->GetConstant(evalResult.mValue))
	{
		if (constant->mTypeCode == BfTypeCode_Boolean)
			return constant->mBool;
	}

	return false;
}

String CeDebugger::GetArrayItems(DebugVisualizerEntry* debugVis, BfType* valueType, BfTypedValue& curNode, int& count, String* outContinuationData)
{
	CeEvaluationContext conditionEvaluationContext(this, debugVis->mCondition);
	auto ceModule = mCeMachine->mCeModule;

	String addrs;

	bool checkLeft = true;

	addr_ce curNodeAddr = 0;

	int usedCount = 0;
	while (usedCount < count)
	{
		if ((!curNode) || (!curNode.mType->IsPointer()))
			break;
		curNode = ceModule->LoadValue(curNode);

		BfTypedValue condVal = conditionEvaluationContext.EvaluateInContext(curNode);
		if (!condVal)
			break;

		auto ceTypedVal = GetAddr(curNode);
		if (!ceTypedVal)
			break;

		if (ValueToInt(condVal) != 0)
		{
			auto val = curNode;
			if (valueType == NULL)
			{
				//String typeAddr =  val.mType->ToStringRaw();
				String typeAddr = StrFormat("comptype(%d)", val.mType->mTypeId);
				// RPad
				typeAddr.Append(' ', sizeof(addr_ce) * 2 - (int)typeAddr.length());
				addrs += typeAddr;
			}

			String addr = EncodeDataPtr((addr_ce)ceTypedVal.mAddr, false);
			addrs += addr;
			usedCount++;
		}
		auto elemType = curNode.mType->GetUnderlyingType();
		curNodeAddr = (addr_ce)ceTypedVal.mAddr + elemType->GetStride();
		curNode.mValue = ceModule->mBfIRBuilder->CreateIntToPtr(curNodeAddr, ceModule->mBfIRBuilder->MapType(curNode.mType));
	}
	count = usedCount;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNodeAddr, false);
	}

	return addrs;
}

String CeDebugger::GetLinkedListItems(DebugVisualizerEntry* debugVis, addr_ce endNodePtr, BfType* valueType, BfTypedValue& curNode, int& count, String* outContinuationData)
{
	CeEvaluationContext nextEvaluationContext(this, debugVis->mNextPointer);
	CeEvaluationContext valueEvaluationContext(this, debugVis->mValuePointer);
	auto ceModule = mCeMachine->mCeModule;

	String addrs;

	bool checkLeft = true;

	int mapIdx;
	for (mapIdx = 0; mapIdx < count; mapIdx++)
	{
		CeTypedValue ceNodeVal = GetAddr(curNode);
		if (!ceNodeVal)
			break;

		if (ceNodeVal.mAddr == endNodePtr)
			break;
		BfTypedValue val = valueEvaluationContext.EvaluateInContext(curNode);
		if (!val)
			break;

		auto ceVal = GetAddr(val);
		if (!ceVal)
			break;
		if (ceVal.mAddr == 0)
			break;

		if (valueType == NULL)
		{
			String typeAddr = StrFormat("comptype(%d)", val.mType->mTypeId);
			// RPad
			typeAddr.Append(' ', sizeof(addr_ce) * 2 - (int)typeAddr.length());
			addrs += typeAddr;
		}

		String addr = EncodeDataPtr((addr_ce)ceVal.mAddr, false);
		addrs += addr;

		curNode = nextEvaluationContext.EvaluateInContext(curNode);
	}
	count = mapIdx;

	if (outContinuationData != NULL)
	{
		CeTypedValue ceNodeVal = GetAddr(curNode);
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(endNodePtr, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr((addr_ce)ceNodeVal.mAddr, false);
	}

	return addrs;
}

String CeDebugger::GetDictionaryItems(DebugVisualizerEntry* debugVis, BfTypedValue dictValue, int bucketIdx, int nodeIdx, int& count, String* outContinuationData)
{
	CeEvaluationContext nextEvaluationContext(this, debugVis->mNextPointer);
	auto ceModule = mCeMachine->mCeModule;

	BfTypedValue bucketsPtr = EvaluateInContext(dictValue, debugVis->mBuckets);
	BfTypedValue entriesPtr = EvaluateInContext(dictValue, debugVis->mEntries);
	if ((!bucketsPtr) || (!entriesPtr))
	{
		count = -1;
		return "";
	}

	auto ceDictTypedVal = GetAddr(dictValue);
	if (!ceDictTypedVal)
		return "";

	auto ceBucketsTypedVal = GetAddr(bucketsPtr);
	if (!ceBucketsTypedVal)
		return "";

	String addrs;

	if ((!entriesPtr) || (!entriesPtr.mType->IsPointer()))
		return "";
	if ((!bucketsPtr) || (!bucketsPtr.mType->IsPointer()))
		return "";

	auto entryType = entriesPtr.mType->GetUnderlyingType();
	int entrySize = entryType->GetStride();
	int bucketIdxSize = bucketsPtr.mType->GetUnderlyingType()->GetStride();

	auto ceElemTypedVal = GetAddr(entriesPtr);
	if (!ceElemTypedVal)
		return "";

	bool checkLeft = true;

	int encodeCount = 0;
	while (encodeCount < count)
	{
		if (nodeIdx != -1)
		{
			addr_ce entryAddr = (addr_ce)ceElemTypedVal.mAddr + (nodeIdx * entrySize);

			BfTypedValue entryValue = BfTypedValue(ceModule->mBfIRBuilder->CreateConstAggCE(ceModule->mBfIRBuilder->MapType(entryType), entryAddr), entryType);
			addrs += EncodeDataPtr(entryAddr, false);

			BfTypedValue nextValue = nextEvaluationContext.EvaluateInContext(entryValue);
			if ((!nextValue) || (!nextValue.mType->IsInteger()))
			{
				break;
			}

			nodeIdx = (int)ValueToInt(nextValue);
			encodeCount++;
		}
		else
		{
			if (bucketIdxSize == 4)
				nodeIdx = ReadMemory<int>(ceBucketsTypedVal.mAddr + bucketIdx * sizeof(int32));
			else
				nodeIdx = (int)ReadMemory<int64>(ceBucketsTypedVal.mAddr + bucketIdx * sizeof(int64));
			bucketIdx++;
		}
	}

	count = encodeCount;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(dictValue.mType, false) + EncodeDataPtr((addr_ce)ceDictTypedVal.mAddr, false) +
			EncodeDataPtr((addr_ce)bucketIdx, false) + EncodeDataPtr((addr_ce)nodeIdx, false);
	}

	return addrs;
}

String CeDebugger::GetTreeItems(DebugVisualizerEntry* debugVis, Array<addr_ce>& parentList, BfType*& valueType, BfTypedValue& curNode, int count, String* outContinuationData)
{
	CeEvaluationContext leftEvaluationContext(this, debugVis->mLeftPointer);
	CeEvaluationContext rightEvaluationContext(this, debugVis->mRightPointer);
	CeEvaluationContext valueEvaluationContext(this, debugVis->mValuePointer);

	CeEvaluationContext conditionEvaluationContext(this, debugVis->mCondition);

	String addrs;

	//TODO:
// 	bool checkLeft = true;
//
// 	if ((curNode.mPtr & 2) != 0) // Flag from continuation
// 	{
// 		checkLeft = false;
// 		curNode.mPtr &= (addr_ce)~2;
// 	}
//
// 	HashSet<intptr> seenAddrs;
//
// 	for (int mapIdx = 0; mapIdx < count; mapIdx++)
// 	{
// 		BfTypedValue readNode;
// 		while (true)
// 		{
// 			bool checkNode = (curNode.mPtr & 1) == 0;
//
// 			readNode = curNode;
// 			readNode.mPtr &= (addr_ce)~1;
//
// 			if (checkLeft)
// 			{
// 				BfTypedValue leftValue = leftEvaluationContext.EvaluateInContext(readNode);
// 				bool isEmpty = leftValue.mPtr == NULL;
// 				if ((leftValue) && (conditionEvaluationContext.HasExpression()))
// 				{
// 					auto condValue = conditionEvaluationContext.EvaluateInContext(leftValue);
// 					if (condValue)
// 						isEmpty = !condValue.mBool;
// 				}
// 				if (isEmpty)
// 				{
// 					checkLeft = false;
// 					break; // Handle node
// 				}
//
// 				parentList.push_back(curNode.mPtr);
// 				curNode = leftValue;
// 			}
// 			else if (checkNode)
// 			{
// 				break; // Handle node
// 			}
// 			else
// 			{
// 				BfTypedValue rightValue = rightEvaluationContext.EvaluateInContext(readNode);
// 				bool isEmpty = rightValue.mPtr == NULL;
// 				if ((rightValue) && (conditionEvaluationContext.HasExpression()))
// 				{
// 					auto condValue = conditionEvaluationContext.EvaluateInContext(rightValue);
// 					if (condValue)
// 						isEmpty = !condValue.mBool;
// 				}
// 				if (!isEmpty)
// 				{
// 					curNode = rightValue;
// 					checkLeft = true;
// 				}
// 				else
// 				{
// 					if (parentList.size() == 0)
// 					{
// 						// Failed
// 						break;
// 					}
//
// 					curNode.mPtr = parentList.back();
// 					parentList.pop_back();
// 					continue; // Don't check against seenAddrs
// 				}
// 			}
//
// 			if (!seenAddrs.Add(curNode.mPtr))
// 			{
// 				// Failed!
// 				return "";
// 			}
// 		}
//
//
// 		BfTypedValue val = valueEvaluationContext.EvaluateInContext(readNode);
// 		if (valueType == NULL)
// 			valueType = val.mType;
//
// 		String addr = EncodeDataPtr(val.mPtr, false);
// 		addrs += addr;
//
// 		curNode.mPtr |= 1; // Node handled
// 	}
//
// 	if (!checkLeft)
// 		curNode.mPtr |= 2;
//
// 	if (outContinuationData != NULL)
// 	{
// 		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) + EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
// 		for (auto parent : parentList)
// 			*outContinuationData += EncodeDataPtr(parent, false);
// 	}

	return addrs;
}

String CeDebugger::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	if (!mCeMachine->mDbgPaused)
		return "";

	auto ceModule = mCeMachine->mCeModule;
	const char* dataPtr = continuationData.c_str();
	DebugVisualizerEntry* debugVis = (DebugVisualizerEntry*)DecodeLocalDataPtr(dataPtr);

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_TreeItems)
	{
		//TODO:
// 		DbgType* valueType = (DbgType*)DecodeLocalDataPtr(dataPtr);
// 		BfTypedValue curNode;
// 		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
// 		curNode.mPtr = DecodeTargetDataPtr(dataPtr);
//
// 		Array<addr_ce> parentList;
// 		String newContinuationData;
// 		while (*dataPtr != 0)
// 			parentList.push_back(DecodeTargetDataPtr(dataPtr));
//
// 		String retVal = GetTreeItems(dbgCompileUnit, debugVis, parentList, valueType, curNode, count, &newContinuationData);
// 		retVal += "\n" + newContinuationData;
// 		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		addr_ce endNodePtr = DecodeTargetDataPtr(dataPtr);
		BfType* valueType = (BfType*)DecodeLocalDataPtr(dataPtr);
		BfType* nodeType = (BfType*)DecodeLocalDataPtr(dataPtr);

		BfTypedValue curNode = BfTypedValue(
			ceModule->mBfIRBuilder->CreateIntToPtr(DecodeTargetDataPtr(dataPtr), ceModule->mBfIRBuilder->MapType(nodeType)),
			nodeType);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetLinkedListItems(debugVis, endNodePtr, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array)
	{
		BfType* valueType = (BfType*)DecodeLocalDataPtr(dataPtr);

		auto nodeType = (BfType*)DecodeLocalDataPtr(dataPtr);
		BfTypedValue curNode = BfTypedValue(
			ceModule->mBfIRBuilder->CreateIntToPtr(DecodeTargetDataPtr(dataPtr), ceModule->mBfIRBuilder->MapType(nodeType)),
			nodeType);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetArrayItems(debugVis, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Dictionary)
	{
		auto dictValueType = (BfType*)DecodeLocalDataPtr(dataPtr);
		BfTypedValue dictValue = BfTypedValue(
			ceModule->mBfIRBuilder->CreateIntToPtr(DecodeTargetDataPtr(dataPtr), ceModule->mBfIRBuilder->MapType(dictValueType)),
			dictValueType);

		int bucketIdx = (int)DecodeTargetDataPtr(dataPtr);
		int nodeIdx = (int)DecodeTargetDataPtr(dataPtr);

		String newContinuationData;
		String retVal = GetDictionaryItems(debugVis, dictValue, bucketIdx, nodeIdx, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}

	return "";
}

CeTypedValue CeDebugger::GetAddr(BfConstant* constant, BfType* type)
{
	auto module = mCeMachine->mCeModule;
	auto ceContext = mCeMachine->mCurContext;

	if (constant->mConstType == BfConstType_GlobalVar)
	{
		auto globalVar = (BfGlobalVar*)constant;

		String varName(globalVar->mName);

		if (varName.StartsWith("__bfStrObj"))
		{
			int stringId = atoi(varName.c_str() + 10);
			auto addr = ceContext->GetString(stringId);
			return CeTypedValue(addr, globalVar->mType);
		}
		else if (varName.StartsWith("__bfStrData"))
		{
			auto stringType = module->ResolveTypeDef(module->mCompiler->mStringTypeDef)->ToTypeInstance();
			int stringId = atoi(varName.c_str() + 11);
			auto addr = ceContext->GetString(stringId) + stringType->mInstSize;
			return CeTypedValue(addr, globalVar->mType);
		}

		CeStaticFieldInfo* fieldInfo = NULL;
		if (ceContext->mStaticFieldMap.TryAdd(globalVar->mName, NULL, &fieldInfo))
		{
			auto dbgTypeInfo = GetDbgTypeInfo(globalVar->mType);
			if (dbgTypeInfo != NULL)
			{
				uint8* ptr = ceContext->CeMalloc(dbgTypeInfo->mType->mSize);
				if (dbgTypeInfo->mType->mSize > 0)
					memset(ptr, 0, dbgTypeInfo->mType->mSize);
				fieldInfo->mAddr = (addr_ce)(ptr - ceContext->mMemory.mVals);
			}
			else
				fieldInfo->mAddr = 0;
		}
		return CeTypedValue(fieldInfo->mAddr, globalVar->mType);
	}
	else if (constant->mTypeCode == BfTypeCode_StringId)
	{
		auto stringType = module->ResolveTypeDef(module->mCompiler->mStringTypeDef)->ToTypeInstance();
		if ((type != NULL) && (type->IsPointer()))
		{
			BfType* charType = module->GetPrimitiveType(BfTypeCode_Char8);
			BfType* charPtrType = module->CreatePointerType(charType);
			auto addr = ceContext->GetString(constant->mInt32) + stringType->mInstSize;
			return CeTypedValue(addr, module->mBfIRBuilder->MapType(charPtrType));
		}
		else
		{
			auto addr = ceContext->GetString(constant->mInt32);
			return CeTypedValue(addr, module->mBfIRBuilder->MapType(stringType));
		}
	}
	else if (constant->mConstType == BfConstType_AggCE)
	{
		auto aggCE = (BfConstantAggCE*)constant;
		return CeTypedValue(aggCE->mCEAddr, aggCE->mType);
	}
	else if (constant->mConstType == BfConstType_BitCast)
	{
		auto constBitCast = (BfConstantBitCast*)constant;
		auto val = GetAddr(module->mBfIRBuilder->GetConstantById(constBitCast->mTarget));
		if (!val)
			return val;
		return CeTypedValue(val.mAddr, constBitCast->mToType);
	}
	else if (constant->mConstType == BfConstType_IntToPtr)
	{
		auto fromPtrToInt = (BfConstantIntToPtr*)constant;
		auto val = GetAddr(module->mBfIRBuilder->GetConstantById(fromPtrToInt->mTarget));
		if (!val)
			return val;
		return CeTypedValue(val.mAddr, fromPtrToInt->mToType);
	}
	else if (constant->mConstType == BfConstType_GEP32_1)
	{
		auto gepConst = (BfConstantGEP32_1*)constant;

		auto constant = module->mBfIRBuilder->GetConstantById(gepConst->mTarget);
		auto typedVal = GetAddr(constant);
		if (!typedVal)
			return CeTypedValue();

		auto dbgTypeInfo = GetDbgTypeInfo(typedVal.mType);
		if (dbgTypeInfo == NULL)
			return CeTypedValue();

		auto addr = typedVal.mAddr;

		if (gepConst->mIdx0 != 0)
			addr += gepConst->mIdx0 * dbgTypeInfo->mType->GetUnderlyingType()->GetStride();

		return CeTypedValue(addr, module->mBfIRBuilder->MapType(dbgTypeInfo->mType));
	}
	else if (constant->mConstType == BfConstType_GEP32_2)
	{
		auto gepConst = (BfConstantGEP32_2*)constant;

		auto constant = module->mBfIRBuilder->GetConstantById(gepConst->mTarget);
		auto typedVal = GetAddr(constant);
		if (!typedVal.mAddr)
			return CeTypedValue();

		auto dbgTypeInfo = GetDbgTypeInfo(typedVal.mType);

		if ((dbgTypeInfo != NULL) &&
			((dbgTypeInfo->mType->IsPointer()) || ((dbgTypeInfo->mType->IsRef()))))
			dbgTypeInfo = GetDbgTypeInfo(dbgTypeInfo->mType->GetUnderlyingType()->mTypeId);

		if (dbgTypeInfo == NULL)
			return CeTypedValue();

		auto addr = typedVal.mAddr;

		if (gepConst->mIdx0 != 0)
			addr += gepConst->mIdx0 * dbgTypeInfo->mType->GetStride();
		if (gepConst->mIdx1 != 0)
			addr += dbgTypeInfo->mFieldOffsets[gepConst->mIdx1].mDataOffset;
		BfType* ptrType = NULL;
		if (gepConst->mIdx1 == 0)
		{
			auto typeInst = dbgTypeInfo->mType->ToTypeInstance();
			if ((typeInst != NULL) && (typeInst->mBaseType != NULL))
			{
				ptrType = typeInst->mBaseType;
				if (!ptrType->IsValueType())
					ptrType = module->CreatePointerType(ptrType);
			}
		}

		if (ptrType == NULL)
		{
			if (gepConst->mIdx1 > dbgTypeInfo->mFieldOffsets.mSize)
				return CeTypedValue();
			ptrType = module->CreatePointerType(dbgTypeInfo->mFieldOffsets[gepConst->mIdx1].mType);
		}
		return CeTypedValue(addr, module->mBfIRBuilder->MapType(ptrType));
	}
	else if ((constant->mTypeCode == BfTypeCode_Int32) ||
		(constant->mTypeCode == BfTypeCode_Int64) ||
		(constant->mTypeCode == BfTypeCode_IntPtr))
	{
		return CeTypedValue(constant->mInt64, module->mBfIRBuilder->GetPrimitiveType(constant->mTypeCode));
	}

	return CeTypedValue();
}

CeTypedValue CeDebugger::GetAddr(const BfTypedValue typedVal)
{
	auto constant = mCeMachine->mCeModule->mBfIRBuilder->GetConstant(typedVal.mValue);
	if (constant == NULL)
		return CeTypedValue();
	return GetAddr(constant, typedVal.mType);
}

#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

String CeDebugger::ReadString(BfTypeCode charType, intptr addr, intptr maxLength, CeFormatInfo& formatInfo)
{
	int origMaxLength = (int)maxLength;
	if (addr == 0)
		return "";

	BP_ZONE("WinDebugger::ReadString");

	String retVal = "\"";
	bool wasTerminated = false;
	String valString;
	intptr maxShowSize = 255;

	if (maxLength == -1)
		maxLength = formatInfo.mOverrideCount;
	else if (formatInfo.mOverrideCount != -1)
		maxLength = BF_MIN(formatInfo.mOverrideCount, maxLength);
	if (formatInfo.mMaxCount != -1)
		maxLength = BF_MIN(formatInfo.mMaxCount, maxLength);

	if (maxLength == -1)
		maxLength = 8 * 1024 * 1024; // Is 8MB crazy?
	if (!formatInfo.mRawString)
		maxLength = BF_MIN(maxLength, maxShowSize);

	//EnableMemCache();
	bool readFailed = false;
	intptr strPtr = addr;

	int charLen = 1;
	if (charType == BfTypeCode_Char16)
		charLen = 2;
	else if (charType == BfTypeCode_Char32)
		charLen = 4;

	bool isUTF8 = formatInfo.mDisplayType == DwDisplayType_Utf8;

	int readSize = BF_MIN(1024, (int)maxLength * charLen);
	uint8 buf[1024];
	uint8* bufPtr = NULL;
	uint8* bufEnd = NULL;
	bool hasHighAscii = false;

	int i;
	for (i = 0; i < maxLength; i++)
	{
		if (bufPtr >= bufEnd)
		{
			while (true)
			{
				if (readSize < charLen)
				{
					readFailed = true;
					break;
				}

				if (ReadMemory(strPtr, readSize, buf))
					break;

				readSize /= 2;
			}
			if (readFailed)
				break;

			bufPtr = buf;
			bufEnd = buf + readSize;
		}

		switch (charLen)
		{
		case 1:
		{
			char c = GET_FROM(bufPtr, char);
			if ((c != 0) || (formatInfo.mOverrideCount != -1))
			{
				if ((uint8)c >= 0x80)
					hasHighAscii = true;
				valString.Append(c);
			}
			else
				wasTerminated = true;
		}
		break;
		case 2:
		{
			uint16 c16 = GET_FROM(bufPtr, uint16);
			if ((c16 != 0) || (formatInfo.mOverrideCount != -1))
			{
				char str[8];
				u8_toutf8(str, 8, c16);
				valString += str;
			}
			else
				wasTerminated = true;
		}
		break;
		case 4:
		{
			uint32 c32 = GET_FROM(bufPtr, uint32);
			if ((c32 != 0) || (formatInfo.mOverrideCount != -1))
			{
				char str[8];
				u8_toutf8(str, 8, c32);
				valString += str;
			}
			else
				wasTerminated = true;
		}
		break;
		}

		if ((wasTerminated) && (formatInfo.mOverrideCount != -1))
		{
			valString += '\x00';
			wasTerminated = false;
		}

		if ((wasTerminated) || (readFailed))
		{
			break;
		}
		strPtr += charLen;
	}
	//DisableMemCache();

	if (formatInfo.mOverrideCount != -1)
	{
		if (i == formatInfo.mOverrideCount)
			wasTerminated = true;
	}

	if (strPtr == addr + origMaxLength)
		wasTerminated = true;

	if (valString.length() == formatInfo.mOverrideCount)
		wasTerminated = true;

	// 	if (formatInfo.mDisplayType == DwDisplayType_Ascii)
	// 	{
	// 		// Our encoding for retVal is already assumed to be UTF8, so the special case here actually Ascii
	// 		valString = UTF8Encode(ToWString(valString));
	// 	}

	if (formatInfo.mRawString)
	{
		if ((formatInfo.mDisplayType == DwDisplayType_Utf8) || (!hasHighAscii))
			return valString;

		String utf8Str;
		for (int i = 0; i < (int)valString.length(); i++)
		{
			char c = valString[i];
			if ((uint8)c >= 0x80)
			{
				utf8Str += (char)(0xC0 | (((uint8)c & 0xFF) >> 6));
				utf8Str += (char)(0x80 | ((uint8)c & 0x3F));
			}
			else
				utf8Str += c;
		}
		return utf8Str;
	}

	if ((readFailed) && (valString.IsEmpty()))
		return "< Failed to read string >";

	retVal += SlashString(valString, true, true, true);

	// We could go over 'maxShowSize' if we have a lot of slashed chars. An uninitialized string can be filled with '\xcc' chars
	if ((!formatInfo.mRawString) && ((int)retVal.length() > maxShowSize))
	{
		retVal = retVal.Substring(0, maxShowSize);
		wasTerminated = false;
	}

	if (wasTerminated)
		retVal += "\"";
	else
		retVal += "...";

	return retVal;
}

void CeDebugger::ProcessEvalString(BfTypedValue useTypedValue, String& evalStr, String& displayString, CeFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength)
{
	for (int i = 0; i < (int)evalStr.length(); i++)
	{
		char c = evalStr[i];
		char nextC = 0;
		if (i < (int)evalStr.length() - 1)
			nextC = evalStr[i + 1];
		if ((c == '{') && (nextC != '{'))
		{
			// Evaluate

			int endIdx = i;
			for (; endIdx < (int)evalStr.length(); endIdx++)
			{
				//TODO: Do better parsing - this paren could be inside a string, for example
				if (evalStr[endIdx] == '}')
					break;
			}

			CeFormatInfo displayStrFormatInfo = formatInfo;
			displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + (int)displayString.length();
			displayStrFormatInfo.mHidePointers = false;

			if ((limitLength) && (displayStrFormatInfo.mTotalSummaryLength > 255))
			{
				displayString += "...";
			}
			else
			{
				String evalString = evalStr.Substring(i + 1, endIdx - i - 1);
				String errors;
				BfTypedValue evalResult = EvaluateInContext(useTypedValue, evalString, &displayStrFormatInfo, NULL, &errors);
				if (evalResult)
				{
					if (displayStrFormatInfo.mNoEdit)
						formatInfo.mNoEdit = true;

					String result = TypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);

					if ((formatInfo.mRawString) && (limitLength))
					{
						displayString = result;
						return;
					}

					int crPos = (int)result.IndexOf('\n');
					if (crPos != -1)
						displayString += result.Substring(0, crPos);
					else
						displayString += result;
				}
				else if (debugVis != NULL)
				{
					displayString += "<DbgVis Failed>";
					DbgVisFailed(debugVis, evalString, errors);
				}
				else
				{
					displayString += "<Eval Failed>";
				}
			}

			i = endIdx;
			continue;
		}
		else if ((c == '{') && (nextC == '{'))
		{
			// Skip next paren
			i++;
		}
		else if ((c == '}') && (nextC == '}'))
		{
			// Skip next paren
			i++;
		}

		displayString += c;
	}
}

String CeDebugger::TypedValueToString(const BfTypedValue& origTypedValue, const StringImpl& expr, CeFormatInfo& formatInfo, bool fullPrecision, CeTypeModKind typeModKind)
{
	BfTypedValue typedValue = origTypedValue;

	auto module = mCeMachine->mCeModule;

	String retVal;
	if (typedValue.IsNoValueType())
	{
		String typeName = TypeToString(typedValue);

		retVal += typeName;
		retVal += "\n";
		retVal += typeName;
		retVal += "\n";
		retVal += GetMemberList(typedValue.mType, 0, 0, true);
		return retVal;
	}

	auto constant = module->mBfIRBuilder->GetConstant(typedValue.mValue);
	auto ceContext = mCeMachine->mCurContext;
	if (constant == NULL)
	{
		return "!Invalid expression";
	}

	bool didAlloc = false;
	addr_ce addr = 0;

	defer(
		{
			if (didAlloc)
				mCurDbgState->mCeContext->CeFree(addr);
		}
		);

	if (constant->mConstType == BfConstType_AggCE)
	{
		auto aggCE = (BfConstantAggCE*)constant;
		addr = aggCE->mCEAddr;
	}
	else if ((typedValue.IsAddr()) || (typedValue.mType->IsObjectOrInterface()) || (typedValue.mType->IsPointer()))
	{
		CeTypedValue typedVal = GetAddr(constant, typedValue.mType);
		addr = (addr_ce)typedVal.mAddr;
		if (!typedVal)
		{
			return "!Invalid addr type";
		}
	}
	else
	{
		int allocSize = typedValue.mType->mSize;
		auto typeInst = typedValue.mType->ToTypeInstance();
		if (typeInst != NULL)
			allocSize = typeInst->mInstSize;
		if (allocSize < 0)
			return "!Invalid size";

		addr = (addr_ce)(mCurDbgState->mCeContext->CeMalloc(allocSize) - mCurDbgState->mCeContext->mMemory.mVals);
		didAlloc = true;

		if (!mCurDbgState->mCeContext->WriteConstant(mCeMachine->mCeModule, addr, constant, typedValue.mType))
		{
			return StrFormat("!Failed to encode value");
		}
	}

	DwDisplayInfo* displayInfo = GetDisplayInfo(formatInfo.mReferenceId);

	char str[32];
	String result;
	auto memStart = mCurDbgState->mCeContext->mMemory.mVals;

	int checkMemSize = typedValue.mType->mSize;
	if (typedValue.mType->IsPointer())
		checkMemSize = typedValue.mType->GetUnderlyingType()->mSize;

	uint8* data = ceContext->GetMemoryPtr(addr, checkMemSize);
	if ((addr != 0) && (data == NULL))
	{
		if (typedValue.mType->IsPointer())
			return EncodeDataPtr(addr, true) + "\n" + TypeToString(origTypedValue.mType, typeModKind);
		else
			return "!Invalid address";
	}

	addr_ce dataAddr = addr;
	if ((typedValue.IsAddr()) && (typedValue.mType->IsObjectOrInterface()))
		dataAddr = *(addr_ce*)data;

	if (formatInfo.mRawString)
	{
		//if ((dwValueType->mTypeCode != DbgType_Struct) && (dwValueType->mTypeCode != DbgType_Class) && (dwValueType->mTypeCode != DbgType_Ptr) && (dwValueType->mTypeCode != DbgType_SizedArray))
		if ((!typedValue.mType->IsPointer()) && (!typedValue.mType->IsObjectOrStruct()) && (!typedValue.mType->IsStruct()) && (!typedValue.mType->IsSizedArray()))
			return "";
	}

	auto _ShowArraySummary = [&](String& retVal, addr_ce ptrVal, int64 arraySize, BfType* innerType)
	{
		auto ptrType = module->CreatePointerType(innerType);

		String displayString;
		displayString += "{";
		for (int idx = 0; idx < arraySize; idx++)
		{
			if (formatInfo.mTotalSummaryLength + retVal.length() + displayString.length() > 255)
			{
				displayString += "...";
				break;
			}

			if ((idx != 0) && (!displayString.EndsWith('{')))
				displayString += ", ";

			CeFormatInfo displayStrFormatInfo = formatInfo;
			displayStrFormatInfo.mExpandItemDepth = 1;
			displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + (int)retVal.length() + (int)displayString.length();
			displayStrFormatInfo.mHidePointers = false;
			displayStrFormatInfo.mArrayLength = -1;

			// Why did we have this "na" on here? It made "void*[3]" type things show up as "{,,}"
			//String evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + StrFormat(")[%d], na", idx);

			String evalStr = StrFormat("((comptype(%d))(void*)", ptrType->mTypeId) + EncodeDataPtr(ptrVal, true) + StrFormat(")[%lld]", idx);
			BfTypedValue evalResult = EvaluateInContext(typedValue, evalStr, &displayStrFormatInfo);
			String result;
			if (evalResult)
			{
				result = TypedValueToString(evalResult, evalStr, displayStrFormatInfo, NULL);
				int crPos = (int)result.IndexOf('\n');
				if (crPos != -1)
					result.RemoveToEnd(crPos);
			}
			else
				result = "???";

			displayString += result;
		}
		displayString += "}";
		retVal += displayString;
	};

	if (formatInfo.mArrayLength != -1)
	{
		if (formatInfo.mRawString)
			return "";

		if (typedValue.mType->IsPointer())
		{
			auto elementType = typedValue.mType->GetUnderlyingType();

			String retVal;
			addr_ce ptrVal = addr;
			if (!formatInfo.mHidePointers)
			{
				retVal = EncodeDataPtr(ptrVal, true) + " ";
				retVal += module->TypeToString(elementType);
				retVal += StrFormat("[%lld] ", (int64)formatInfo.mArrayLength);
			}

			_ShowArraySummary(retVal, ptrVal, formatInfo.mArrayLength, elementType);

			String idxStr = "[{0}]";

			retVal += "\n" + TypeToString(typedValue);

			String evalStr = StrFormat("((comptype(%d))(void*)", typedValue.mType->mTypeId) + EncodeDataPtr(ptrVal, true) + ")[{0}]";

			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(formatInfo.mArrayLength, 0), 10000) +
				"\t" + idxStr + "\t" + evalStr;
			return retVal;
		}
		else
		{
			CeFormatInfo newFormatInfo = formatInfo;
			newFormatInfo.mArrayLength = -1;

			String retVal = TypedValueToString(typedValue, expr, newFormatInfo);

			int crPos = (int)retVal.IndexOf('\n');
			if (crPos != -1)
				retVal = "!Array length flag not valid with this type" + retVal.Substring(crPos);
			return retVal;
		}
	}

	if (typedValue.mType->IsRef())
	{
		if (dataAddr == 0)
			return "<null>\n" + TypeToString(typedValue);

		addr = *(addr_ce*)data;
		dataAddr = addr;

		data = ceContext->GetMemoryPtr(addr, checkMemSize);
		if ((addr != 0) && (data == NULL))
			return "!Invalid address";

		typedValue = BfTypedValue(typedValue.mValue, typedValue.mType->GetUnderlyingType(), true);
	}

	if (typedValue.mType->IsPointer())
	{
		//ceContext->

		addr_ce ptrVal = addr;

		String retVal;
		BfType* innerType = typedValue.mType->GetUnderlyingType();
		if (innerType == NULL)
			return EncodeDataPtr((uint32)ptrVal, true) + "\nvoid*";

		bool isChar = false;

		if (innerType->IsChar())
			isChar = true;

		if ((isChar) && (formatInfo.mArrayLength == -1))
		{
			auto primType = (BfPrimitiveType*)innerType;

			if (!formatInfo.mHidePointers)
				retVal = EncodeDataPtr(ptrVal, true);

			int strLen = (int)formatInfo.mOverrideCount;
			// 			if (typedValue.mIsLiteral)
			// 			{
			// 				if (strLen == -1)
			// 					strLen = 0x7FFFFFFF;
			// 				if (typedValue.mDataLen > 0)
			// 					strLen = BF_MIN(strLen, typedValue.mDataLen);
			// 				else
			// 					strLen = BF_MIN(strLen, strlen(typedValue.mCharPtr));
			// 			}

			SetAndRestoreValue<intptr> prevOverrideLen(formatInfo.mOverrideCount, strLen);
			String strResult = ReadString(primType->mTypeDef->mTypeCode, ptrVal, strLen, formatInfo);
			if (formatInfo.mRawString)
				return strResult;
			if (!strResult.IsEmpty())
			{
				if (!retVal.IsEmpty())
					retVal += " ";
				retVal += strResult;
			}
			retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);
			return retVal;
		}
		else if ((ptrVal != 0) && (innerType->IsComposite()))
		{
// 			addr = *(addr_ce*)data;
// 			dataAddr = addr;
//
// 			data = ceContext->GetMemoryPtr(addr, checkMemSize);
// 			if ((addr != 0) && (data == NULL))
// 				return "!Invalid address";

			typedValue = BfTypedValue(typedValue.mValue, typedValue.mType->GetUnderlyingType(), true);
		}
		else
		{
			if (formatInfo.mRawString)
				return "";

			String retVal;
			if (!formatInfo.mHidePointers)
				retVal = EncodeDataPtr((uint32)ptrVal, true);

			if (ptrVal != 0)
			{
				BfTypedValue innerTypedVal = BfTypedValue(module->mBfIRBuilder->CreateConstAggCE(module->mBfIRBuilder->MapType(innerType), (addr_ce)ptrVal), innerType, true);

				innerTypedVal.mType = innerType;

				if (innerTypedVal)
				{
					CeFormatInfo defaultFormatInfo;
					defaultFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + 2; // Take into accout the necessary {}'s
					defaultFormatInfo.mExpandItemDepth++;
					defaultFormatInfo.mCallStackIdx = formatInfo.mCallStackIdx;
					String innerStr = TypedValueToString(innerTypedVal, "", defaultFormatInfo);
					int crIdx = (int)innerStr.IndexOf('\n');
					if (crIdx != -1)
					{
						String innerDataStr = innerStr.Substring(0, crIdx);
						if (!innerDataStr.empty())
						{
							if (!retVal.empty())
								retVal += " ";
							retVal += "{" + innerDataStr + "}";
						}
					}
					else
					{
						retVal += " { ??? }";
					}
				}
			}

			retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);
			module->PopulateType(innerType);

			if ((ptrVal != 0) && (!innerType->IsValuelessType()))
			{
				//String ptrDataStr = StrFormat("(%s)", dwValueType->ToStringRaw(language).c_str()) + EncodeDataPtr(typedValue.mPtr, true);
				retVal += "\n*\t";
				// Why did we have this?  It messed up a pointer to sized array
				/*if (language == DbgLanguage_Beef)
					retVal += "this";
				else*/
				retVal += "this";

				if (!formatInfo.mReferenceId.empty())
					retVal += ", refid=" + MaybeQuoteFormatInfoParam(formatInfo.mReferenceId);

				retVal += StrFormat(", this=%d@0x%X", innerType->mTypeId, ptrVal);
			}

			retVal += "\n:canEdit\n:editVal\t" + EncodeDataPtr((uint32)addr, true);

			return retVal;
		}
	}

	if (typedValue.mType->IsPrimitiveType())
	{
		auto primType = (BfPrimitiveType*)typedValue.mType;

		BfTypeCode typeCode = primType->mTypeDef->mTypeCode;
		if (typeCode == BfTypeCode_IntPtr)
			typeCode = (primType->mSize == 8) ? BfTypeCode_Int64 : BfTypeCode_Int32;
		if (typeCode == BfTypeCode_UIntPtr)
			typeCode = (primType->mSize == 8) ? BfTypeCode_UInt64 : BfTypeCode_UInt32;

		switch (typeCode)
		{
		case BfTypeCode_None:
			return "\nvoid";
		case BfTypeCode_Boolean:
			{
				auto val = *(uint8*)(data);
				if (val == 0)
					return "false\n" + TypeToString(origTypedValue.mType, typeModKind);
				else if (val == 1)
					return "true\n" + TypeToString(origTypedValue.mType, typeModKind);
				else
					return StrFormat("true (%d)\n%s", val, TypeToString(origTypedValue.mType, typeModKind).c_str());
			}
			break;
		case BfTypeCode_Char8:
			{
				auto val = *(uint8*)(data);
				if (val != 0)
				{
					char str[2] = { (char)val };
					result = SlashString(str, formatInfo.mDisplayType == DwDisplayType_Utf8, true);

					if (!IsNormalChar(val))
						result = StrFormat("'%s' (0x%02X)\n", result.c_str(), val);
					else
						result = StrFormat("'%s'\n", result.c_str());
				}
				else
					result = "'\\0'\n";
				return result + TypeToString(origTypedValue.mType, typeModKind);
			}
			break;
		case BfTypeCode_Char16:
			{
				auto val = *(uint16*)(data);
				if (val != 0)
				{
					u8_toutf8(str, 8, val);
					result = SlashString(str, true, true);
					if (!IsNormalChar(val))
						result = StrFormat("'%s' (0x%02X)\n", result.c_str(), val);
					else
						result = StrFormat("'%s'\n", result.c_str());
				}
				else
					result = "'\\0'\n";
				return result + TypeToString(origTypedValue.mType, typeModKind);
			}
			break;
		case BfTypeCode_Char32:
			{
				auto val = *(uint32*)(data);
				if (val != 0)
				{
					u8_toutf8(str, 8, val);
					result = SlashString(str, true, true);
					if (!IsNormalChar(val))
						result = StrFormat("'%s' (0x%02X)\n", result.c_str(), val);
					else
						result = StrFormat("'%s'\n", result.c_str());
				}
				else
					result = "'\\0'\n";
				return result + TypeToString(origTypedValue.mType, typeModKind);
			}
			break;
		case BfTypeCode_Int8:
			return IntTypeToString<int8>(*(int8*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_UInt8:
			return IntTypeToString<uint8>(*(uint8*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_Int16:
			return IntTypeToString<int16>(*(int16*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_UInt16:
			return IntTypeToString<uint16>(*(uint16*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_Int32:
			return IntTypeToString<int32>(*(int32*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_UInt32:
			return IntTypeToString<uint32>(*(uint32*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_Int64:
			return IntTypeToString<int64>(*(int64*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_UInt64:
			return IntTypeToString<uint64>(*(uint64*)(data), TypeToString(origTypedValue.mType, typeModKind), displayInfo, formatInfo);
		case BfTypeCode_Float:
			{
				DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
				if (floatDisplayType == DwFloatDisplayType_Default)
					floatDisplayType = DwFloatDisplayType_Minimal;
				if (floatDisplayType == DwFloatDisplayType_Minimal)
					ExactMinimalFloatToStr(*(float*)data, str);
				else if (floatDisplayType == DwFloatDisplayType_Full)
					sprintf(str, "%1.9g", *(float*)data);
				else if (floatDisplayType == DwFloatDisplayType_HexUpper)
					sprintf(str, "0x%04X", *(uint32*)data);
				else //if (floatDisplayType == DwFloatDisplayType_HexLower)
					sprintf(str, "0x%04x", *(uint32*)data);
				return StrFormat("%s\n%s", str, TypeToString(origTypedValue.mType, typeModKind).c_str());
			}
		case BfTypeCode_Double:
			{
				DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
				if (floatDisplayType == DwFloatDisplayType_Default)
					floatDisplayType = DwFloatDisplayType_Minimal;
				if (floatDisplayType == DwFloatDisplayType_Minimal)
					ExactMinimalDoubleToStr(*(double*)data, str);
				else if (floatDisplayType == DwFloatDisplayType_Full)
					sprintf(str, "%1.17g", *(double*)data);
				else if (floatDisplayType == DwFloatDisplayType_HexUpper)
					sprintf(str, "0x%08llX", *(uint64*)data);
				else //if (floatDisplayType == DwFloatDisplayType_HexLower)
					sprintf(str, "0x%08llx", *(uint64*)data);
				return StrFormat("%s\n%s", str, TypeToString(origTypedValue.mType, typeModKind).c_str());
			}
		}
	}

	if (typedValue.mType->IsSizedArray())
	{
		auto arrayType = (BfSizedArrayType*)typedValue.mType;
		auto innerType = arrayType->mElementType;

		String retVal;
		addr_ce ptrVal = addr;

		intptr arraySize = arrayType->mElementCount;
		intptr innerSize = innerType->GetStride();

		String idxStr = "[{0}]";

		if (innerType->IsChar())
		{
			auto primType = (BfPrimitiveType*)innerType;
			String strVal = ReadString(primType->mTypeDef->mTypeCode, ptrVal, arraySize, formatInfo);
			if (formatInfo.mRawString)
				return strVal;
			retVal += strVal;
		}
		else
		{
			if (formatInfo.mRawString)
				return "";

			_ShowArraySummary(retVal, ptrVal, arraySize, innerType);
		}

		retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);

		String referenceId = TypeToString(typedValue.mType);
		String evalStr;

		// Why did we have the "na"? Do we not want to show addresses for all members?

		auto ptrType = module->CreatePointerType(innerType);

		evalStr = StrFormat("((comptype(%d))(void*)", ptrType->mTypeId) + EncodeDataPtr(ptrVal, true) + ")[{0}], refid=" + MaybeQuoteFormatInfoParam(referenceId + ".[]");
		if (typedValue.IsReadOnly())
			evalStr += ", ne";
		retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(arraySize, 0), 10000) +
			"\t" + idxStr + "\t" + evalStr;
		return retVal;
	}

	if (typedValue.mType->IsEnum())
	{
		if (formatInfo.mRawString)
			return "";

		String retVal;
		if (typedValue.mType->IsTypedPrimitive())
		{
			int64 bitsLeft = ValueToInt(typedValue);
			int valueCount = 0;

			String editVal;

			auto typeInst = typedValue.mType->ToTypeInstance();
			auto dbgTypeInfo = GetDbgTypeInfo(typedValue.mType->mTypeId);

			while ((dbgTypeInfo != NULL) && ((bitsLeft != 0) || (valueCount == 0)))
			{
				CeDbgTypeInfo::ConstIntEntry* bestMatch = NULL;

				for (auto& constIntEntry : dbgTypeInfo->mConstIntEntries)
				{
					if (constIntEntry.mVal == bitsLeft)
					{
						bestMatch = &constIntEntry;
						break;
					}
				}

				if (bestMatch == NULL)
				{
					for (auto& constIntEntry : dbgTypeInfo->mConstIntEntries)
					{
						if ((constIntEntry.mVal != 0) &&
							((constIntEntry.mVal & bitsLeft) == constIntEntry.mVal))
						{
							bestMatch = &constIntEntry;
							break;
						}
					}
				}

				if (bestMatch == NULL)
					break;

				if (valueCount > 0)
				{
					retVal += " | ";
				}

				auto bestFieldInstance = &typeInst->mFieldInstances[bestMatch->mFieldIdx];

				retVal += ".";
				retVal += bestFieldInstance->GetFieldDef()->mName;

				valueCount++;
				bitsLeft &= ~bestMatch->mVal;
			}

			if ((valueCount == 0) || (bitsLeft != 0))
			{
				if (valueCount > 0)
					retVal += " | ";
				retVal += StrFormat("%lld", bitsLeft);
			}

			retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);
			retVal += "\n:canEdit";

			return retVal;
		}
		else if (typedValue.mType->mDefineState >= BfTypeDefineState_Defined)
		{
			auto typeInst = typedValue.mType->ToTypeInstance();
			BfType* dscrType = typeInst->GetDiscriminatorType();
			BfType* payloadType = typeInst->GetUnionInnerType();

			int dscrOffset = BF_ALIGN(payloadType->mSize, dscrType->mAlign);

			int dscrVal = 0;
			memcpy(&dscrVal, data + dscrOffset, dscrType->mSize);

			for (auto& fieldInstance : typeInst->mFieldInstances)
			{
				auto fieldDef = fieldInstance.GetFieldDef();
				if (!fieldInstance.mIsEnumPayloadCase)
					continue;

				int tagId = -fieldInstance.mDataIdx - 1;
				if (dscrVal == tagId)
				{
					auto evalResult = BfTypedValue(module->mBfIRBuilder->CreateConstAggCE(
						module->mBfIRBuilder->MapType(fieldInstance.mResolvedType), (addr_ce)dataAddr), fieldInstance.mResolvedType, true);

					String innerResult = TypedValueToString(evalResult, "", formatInfo, NULL);
					int crPos = (int)innerResult.IndexOf('\n');
					if (crPos != -1)
						innerResult.RemoveToEnd(crPos);

					retVal += ".";
					retVal += fieldDef->mName;
					retVal += innerResult;

					retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);
					retVal += "\n:canEdit";

					retVal += "\n" + GetMemberList(fieldInstance.mResolvedType, addr, dataAddr, false);
					return retVal;
				}
			}
		}
	}

	if (typedValue.mType->IsTypedPrimitive())
	{
		auto innerType = typedValue.mType->GetUnderlyingType();

		BfTypedValue innerTypedVal = typedValue;
		innerTypedVal.mType = innerType;

		auto innerReturn = TypedValueToString(innerTypedVal, expr, formatInfo, fullPrecision);
		if (innerReturn.StartsWith("!"))
			return innerReturn;

		int crPos = (int)innerReturn.IndexOf('\n');
		if (crPos == -1)
			return innerReturn;

		retVal += "{ ";
		retVal += innerReturn.Substring(0, crPos);
		retVal += " }";
		retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);
		return retVal;
	}

	bool isCompositeType = typedValue.mType->IsStruct() || typedValue.mType->IsObject();

	if (isCompositeType)
	{
		BfTypeInstance* displayType = typedValue.mType->ToTypeInstance();

		bool isMemoryValid = true;

		if ((dataAddr == 0) && (addr != 0))
			dataAddr = *(addr_ce*)data;

		if ((((origTypedValue.mType->IsObjectOrInterface()) || (origTypedValue.mType->IsPointer())) &&
			(!formatInfo.mHidePointers) || (dataAddr == 0)))
		{
			retVal = EncodeDataPtr((uint32)dataAddr, true);
			retVal += " ";

			if (!ceContext->CheckMemory(dataAddr, displayType->mInstSize))
				isMemoryValid = false;
		}

		bool isBadSrc = false;
		bool isNull = dataAddr == 0;
		bool hadCustomDisplayString = false;

		BfTypeInstance* actualType = displayType;
		bool useActualRawType = false;

		bool isTuple = typedValue.mType->IsTuple();

		String ptrDataStr;

		if (!formatInfo.mIgnoreDerivedClassInfo)
		{
			if (actualType->IsObject())
			{
				if (dataAddr != 0)
				{
					uint8* dataPtr = ceContext->GetMemoryPtr(dataAddr, 4);
					if (dataPtr == 0)
						return "!Invalid object address";
					int actualTypeid = *(int32*)dataPtr;
					auto checkType = mCompiler->mContext->FindTypeById(actualTypeid);
					if (checkType != NULL)
						actualType = checkType->ToTypeInstance();
				}
			}
		}

		BfTypedValue summaryTypedValue = typedValue;

		if ((actualType != NULL) && (actualType != displayType))
		{
			summaryTypedValue = BfTypedValue(module->mBfIRBuilder->CreateIntToPtr(addr, module->mBfIRBuilder->MapType(actualType)), actualType);
		}

		DebugVisualizerEntry* debugVis = NULL;
		Array<String> dbgVisWildcardCaptures;

		if ((!formatInfo.mNoVisualizers) && (!isNull) && (!isBadSrc))
		{
			debugVis = FindVisualizerForType(summaryTypedValue.mType, &dbgVisWildcardCaptures);
		}

		bool wantsCustomExpandedItems = false;
		String displayString;
		if (debugVis != NULL)
		{
			auto& displayStringList = formatInfo.mRawString ? debugVis->mStringViews : debugVis->mDisplayStrings;

			for (auto displayEntry : displayStringList)
			{
				if (!displayEntry->mCondition.empty())
				{
					if (!EvalCondition(debugVis, summaryTypedValue, formatInfo, displayEntry->mCondition, dbgVisWildcardCaptures, displayString))
						continue;
				}

				hadCustomDisplayString = true;
				String displayStr = mDebugManager->mDebugVisualizers->DoStringReplace(displayEntry->mString, dbgVisWildcardCaptures);
				if (displayString.length() > 0)
					displayString += " ";
				ProcessEvalString(summaryTypedValue, displayStr, displayString, formatInfo, debugVis, true);
				if (formatInfo.mRawString)
					return displayString;

				break;
			}

			if ((!debugVis->mExpandItems.empty()) || (debugVis->mCollectionType != DebugVisualizerEntry::CollectionType_None))
			{
				wantsCustomExpandedItems = true;
			}
		}

		if (formatInfo.mRawString)
			return "";

		String reflectedTypeName;
		if (displayType->IsInstanceOf(mCompiler->mTypeTypeDef))
		{
			auto typeInst = displayType->ToTypeInstance();
			auto typeIdField = typeInst->mTypeDef->GetFieldByName("mTypeId");
			if (typeIdField != NULL)
			{
				auto& fieldInstance = typeInst->mFieldInstances[typeIdField->mIdx];
				int typeId = 0;
				memcpy(&typeId, data + fieldInstance.mDataOffset, fieldInstance.mResolvedType->mSize);
				auto typeType = mCompiler->mContext->FindTypeById(typeId);
				if (typeType != NULL)
					reflectedTypeName = module->TypeToString(typeType);
			}
		}

		if (!reflectedTypeName.IsEmpty())
		{
			displayString += "{ ";
			displayString += reflectedTypeName;
			displayString += " }";
		}
		else if ((!isNull) && (!formatInfo.mNoVisualizers) && (!hadCustomDisplayString))
		{
			// Create our own custom display

			String firstRet;
			String bigRet = isTuple ? "(" : "{ ";

			int memberIdx = 0;
			BfType* summaryType = summaryTypedValue.mType;
			bool summaryDone = false;
			bool truncatedMemberList = false;

			String summaryDataStr = ptrDataStr;
			String splatStr;
			if (dataAddr == -1)
				splatStr = expr;

			while ((summaryType != NULL) && (isMemoryValid))
			{
				if ((summaryType->IsTypedPrimitive())
					//&& ((summaryType->mBaseTypes.IsEmpty()) || (!summaryType->mBaseTypes.front()->mBaseType->IsTypedPrimitive())))
					)
				{
					if (formatInfo.mTotalSummaryLength + (int)displayString.length() > 255)
					{
						truncatedMemberList = true;
						summaryDone = true;
						bigRet += "...";
					}
					else
					{
						CeFormatInfo displayStrFormatInfo = formatInfo;
						displayStrFormatInfo.mExpandItemDepth = 1;
						displayStrFormatInfo.mTotalSummaryLength += (int)displayString.length();
						displayStrFormatInfo.mHidePointers = false;

						BfType* primType = summaryType->GetUnderlyingType();
						String result;

						if (primType->IsInteger())
							formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

						if ((dataAddr != 0) && (dataAddr != -1))
						{
							//String evalString = "(" + primType->ToString() + ")" + ptrDataStr;
// 							BfTypedValue evalResult = EvaluateInContext(dbgCompileUnit, origTypedValue, evalString, &displayStrFormatInfo);
// 							if (evalResult)
// 								result = TypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}
						else
						{
// 							BfTypedValue evalResult = origTypedValue;
// 							evalResult.mType = primType;
// 							String evalString = "(" + primType->ToString() + ")" + expr;
// 							result = TypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}

						if (formatInfo.mRawString)
							return result;

						int crPos = (int)result.IndexOf('\n');
						if (crPos != -1)
							result.RemoveToEnd(crPos);

						if (memberIdx == 0)
							firstRet = result;

						bigRet += result;
						memberIdx++;
					}
				}

				auto summaryTypeInst = summaryType->ToTypeInstance();
				if (summaryTypeInst == NULL)
					break;

				module->PopulateType(summaryTypeInst);
				for (auto& fieldInst : summaryTypeInst->mFieldInstances)
				{
					auto fieldDef = fieldInst.GetFieldDef();
					if (fieldDef == NULL)
						continue;
					if (fieldInst.mResolvedType == NULL)
						continue;

					if (!fieldDef->mIsStatic)
					{
						if (formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length() > 255)
						{
							truncatedMemberList = true;
							summaryDone = true;
							bigRet += "...";
							break;
						}

						//if (fieldDef->mName != NULL)
						{
// 							if (member->mName[0] == '$')
// 								continue;

							if (!isdigit(fieldDef->mName[0]))
							{
								if (memberIdx != 0)
									bigRet += isTuple ? ", " : " ";

								if ((!isTuple) || (fieldDef->mName[0] != '_'))
								{
									bigRet += String(fieldDef->mName);
									bigRet += isTuple ? ":" : "=";
								}
							}
							else
							{
								if (memberIdx != 0)
									bigRet += ", ";
							}

							CeFormatInfo displayStrFormatInfo = formatInfo;
							displayStrFormatInfo.mExpandItemDepth = 1;
							displayStrFormatInfo.mHidePointers = false;
							displayStrFormatInfo.mTotalSummaryLength = (int)(formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length());

// 							String evalString;
// 							if (dataPtr != -1)
// 							{
// 								if ((fieldDef->mName[0] >= '0') && (fieldDef->mName[0] <= '9'))
// 									evalString += "this.";
// 								evalString += String(member->mName); // +", this=" + summaryDataStr;
// 							}
// 							else
// 							{
// 								evalString = "(";
// 								evalString += splatStr;
// 								evalString += ").";
// 								evalString += fieldDef->mName;
// 							}
							String referenceId;
							String result;
							if (!fieldInst.mResolvedType->IsValuelessType())
							{
								auto addrVal = dataAddr + fieldInst.mDataOffset;

								BfTypedValue evalResult;
								if (fieldInst.mResolvedType->IsObjectOrInterface())
								{
									auto typeInst = fieldInst.mResolvedType->ToTypeInstance();
									evalResult = BfTypedValue(module->mBfIRBuilder->CreateConstAggCE(module->mBfIRBuilder->MapTypeInst(typeInst), (addr_ce)addrVal), typeInst, true);
								}
								else
								{
									evalResult = BfTypedValue(module->mBfIRBuilder->CreateConstAggCE(module->mBfIRBuilder->MapType(fieldInst.mResolvedType), (addr_ce)addrVal), fieldInst.mResolvedType, true);
								}

								//BfTypedValue evalResult = EvaluateInContext(dbgCompileUnit, summaryTypedValue, evalString, &displayStrFormatInfo, &referenceId);
								if (evalResult)
								{
									displayStrFormatInfo.mReferenceId = referenceId;
									result = TypedValueToString(evalResult, "", displayStrFormatInfo, NULL);
									int crPos = (int)result.IndexOf('\n');
									if (crPos != -1)
										result.RemoveToEnd(crPos);
								}
								else
									result = "???";
							}

							if (fieldInst.mResolvedType->IsInteger())
								formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

							if (formatInfo.mRawString)
								return result;

							if (memberIdx == 0)
								firstRet = result;

							bigRet += result;
							//formatInfo.mEmbeddedDisplayCount = displayStrFormatInfo.mEmbeddedDisplayCount;
							memberIdx++;
						}
					}
				}

				if (truncatedMemberList)
					break;

				// Find first base class with members
				BfType* nextSummaryType = summaryTypeInst->mBaseType;
				summaryType = nextSummaryType;

				if ((summaryType == NULL) || (summaryType == module->mContext->mBfObjectType))
					break;

				// If we don't have many members then find a base class with some members to show
				if ((memberIdx != 0) && (displayString.length() >= 255))
				{
					truncatedMemberList = true;
					bigRet += "...";
					break;
				}
			}

			bigRet += isTuple ? ")" : " }";

			if (displayString.length() > 0)
				displayString += " ";
			if ((memberIdx == 1) && (!truncatedMemberList) && (firstRet.IndexOf('{') == -1) && (!isTuple))
				displayString += "{ " + firstRet + " }";
			else
				displayString += bigRet;
		}

		retVal += displayString;
		retVal += "\n" + TypeToString(origTypedValue.mType, typeModKind);

		BfType* memberListType = displayType;

		if ((actualType != NULL) && (actualType != displayType))
		{
			if (displayType == module->mContext->mBfObjectType)
			{
				memberListType = actualType;
			}
			else
			{
				String actualTypeName = module->TypeToString(actualType);
				retVal += StrFormat(" {%s}\n[%s]\tthis,this=%d@0x%X", actualTypeName.c_str(), actualTypeName.c_str(), actualType->mTypeId, addr);
			}
		}

		if (formatInfo.mNoMembers)
		{
			//
		}
		else if (wantsCustomExpandedItems)
		{
			HandleCustomExpandedItems(retVal, debugVis, summaryTypedValue, addr, dataAddr, dbgVisWildcardCaptures, formatInfo);
		}
		else if ((!isNull) && (!isBadSrc))
		{
			retVal += "\n" + GetMemberList(memberListType, addr, dataAddr, false);
		}

		if ((origTypedValue.mType->IsObjectOrInterface()) || (origTypedValue.mType->IsPointer()))
		{
			retVal += "\n:editVal\t" + EncodeDataPtr((uint32)dataAddr, true);
		}
		return retVal;
	}

	return "!Failed to display value";
}

void CeDebugger::HandleCustomExpandedItems(String& retVal, DebugVisualizerEntry* debugVis, BfTypedValue typedValue, addr_ce addr, addr_ce addrInst, Array<String>& dbgVisWildcardCaptures, CeFormatInfo& formatInfo)
{
	auto debugVisualizers = mDebugManager->mDebugVisualizers;
	auto ceModule = mCeMachine->mCeModule;

	if (formatInfo.mExpandItemDepth > 10) // Avoid crashing on circular ExpandItems
		return;

	bool isReadOnly = false;
// 	if (useTypedValue.mIsReadOnly)
// 		isReadOnly = true;

	String ptrUseDataStr = StrFormat("%d@0x%X", typedValue.mType->mTypeId, addrInst);

	for (auto entry : debugVis->mExpandItems)
	{
		if (!entry->mCondition.empty())
		{
			String error;
			if (!EvalCondition(debugVis, typedValue, formatInfo, entry->mCondition, dbgVisWildcardCaptures, error))
			{
				if (!error.empty())
					retVal += "\n" + entry->mName + "\t@!<DbgVis Failed>@!";
				continue;
			}
		}
		String replacedStr = debugVisualizers->DoStringReplace(entry->mValue, dbgVisWildcardCaptures);
		retVal += "\n" + entry->mName + "\t" + replacedStr + ", this=" + ptrUseDataStr;
	}

	String referenceId = ceModule->TypeToString(typedValue.mType);

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_ExpandedItem)
	{
		BfTypedValue itemValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
		if (itemValue)
		{
			CeFormatInfo itemFormatInfo = formatInfo;
			itemFormatInfo.mExpandItemDepth++;
			String itemRetVal = TypedValueToString(itemValue, "", itemFormatInfo, NULL);

			int crIdx = (int)itemRetVal.IndexOf('\n');
			if (crIdx != -1)
			{
				crIdx = (int)itemRetVal.IndexOf('\n', crIdx + 1);
				if (crIdx != -1)
					retVal += itemRetVal.Substring(crIdx);
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array)
	{
		BfTypedValue sizeValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		Array<int> lowerDimSizes;
		for (auto lowerDim : debugVis->mLowerDimSizes)
		{
			BfTypedValue lowerDimValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(lowerDim, dbgVisWildcardCaptures), &formatInfo);
			int dimSize = 0;
			if ((lowerDimValue) && (lowerDimValue.mType->IsInteger()))
				dimSize = (int)ValueToInt(lowerDimValue);
			dimSize = BF_MAX(dimSize, 1);
			lowerDimSizes.push_back(dimSize);
		}

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (ValueToInt(sizeValue) > 0))
		{
			if (!debugVis->mCondition.IsEmpty())
			{
				int size = (int)ValueToInt(sizeValue);
				BfTypedValue headPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);

				BfTypedValue curNode = headPointer;
				Array<addr_ce> parentList;
				String continuationData;

				int totalSize = 2;
				auto valueType = headPointer.mType;
				String addrs = GetArrayItems(debugVis, valueType, headPointer, totalSize, &continuationData);
				String firstAddr;
				String secondAddr;
				bool hasSecondAddr = valueType == NULL;
				if (addrs.length() > 0)
				{
					const char* addrsPtr = addrs.c_str();
					firstAddr = addrs.Substring(0, sizeof(addr_ce) * 2);
					if (hasSecondAddr)
						secondAddr = addrs.Substring(sizeof(addr_ce) * 2, sizeof(addr_ce) * 2);
				}

				String evalStr;
				if (valueType != NULL)
				{
					auto ptrType = valueType;
					if (!valueType->IsPointer())
						ptrType = ceModule->CreatePointerType(valueType);
					evalStr = StrFormat("(comptype(%d)", ptrType->mTypeId);
					evalStr += ")(void*)0x{1}";
				}
				else
				{
					evalStr += "({1})(void*)0x{2}";
				}
				if (!debugVis->mShowElementAddrs)
					evalStr.Insert(0, "*");

				if (addrs.length() > 0)
				{
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, BF_MAX(size, 0), 10000) +
						"\t[{0}]\t" + evalStr + "\t" + firstAddr;

					if (hasSecondAddr)
						retVal += "\t" + secondAddr;

					if (size != 0)
					{
						retVal += "\n:addrs\t" + addrs;
						if (valueType == NULL)
							retVal += "\n:addrsEntrySize\t2";
						if (continuationData.length() > 0)
							retVal += "\n:continuation\t" + continuationData;
					}
				}
			}
			else if (lowerDimSizes.size() == 1)
			{
				int dimSize1 = lowerDimSizes[0];

				String evalStr = "(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
					StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
				evalStr += ", refid=\"" + referenceId + ".[]\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, ValueToInt(sizeValue) / dimSize1, 50000) +
					"\t[{0}]\t" + evalStr;
			}
			else if (lowerDimSizes.size() == 2)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];

				BfTypedValue headPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d]*)", ceModule->TypeToString(headPointer.mType->GetUnderlyingType()).c_str(), dimSize2) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, ValueToInt(sizeValue) / dimSize1 / dimSize2, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else if (lowerDimSizes.size() == 3)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];
				int dimSize3 = lowerDimSizes[2];

				BfTypedValue headPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d][%d]*)", ceModule->TypeToString(headPointer.mType->GetUnderlyingType()).c_str(), dimSize2, dimSize3) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, ValueToInt(sizeValue) / dimSize1 / dimSize2 / dimSize3, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else
			{
				String evalStr = "*(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) + " + {0}), this=" + ptrUseDataStr;
				evalStr += ", refid=\"" + referenceId + ".[]\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, ValueToInt(sizeValue), 50000) +
					"\t[{0}]\t" + evalStr;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_IndexItems)
	{
		BfTypedValue sizeValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (ValueToInt(sizeValue) > 0))
		{
			String evalStr = debugVis->mValuePointer + ", this=" + ptrUseDataStr;
			evalStr.Replace("$i", "{0}");

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, ValueToInt(sizeValue), 50000) +
				"\t[{0}]\t" + evalStr;
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		BfType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures));
		}

		BfTypedValue headPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);
		if (headPointer)
		{
			BfTypedValue endPointer;
			if (!debugVis->mEndPointer.empty())
				endPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mEndPointer, dbgVisWildcardCaptures), &formatInfo);
			BfTypedValue nextPointer = EvaluateInContext(headPointer, debugVisualizers->DoStringReplace(debugVis->mNextPointer, dbgVisWildcardCaptures), &formatInfo);

			int size = -1;
			if (!debugVis->mSize.empty())
			{
				auto sizeValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
				if (sizeValue)
					size = (int)ValueToInt(sizeValue);
			}

			auto ceEndPointerVal = GetAddr(endPointer);

			BfTypedValue curNode = headPointer;
			Array<addr_ce> parentList;
			String continuationData;

			int totalSize = 2;
			String addrs = GetLinkedListItems(debugVis, (addr_ce)ceEndPointerVal.mAddr, valueType, curNode, totalSize, &continuationData);
			String firstAddr;
			String secondAddr;
			bool hasSecondAddr = valueType == NULL;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = addrs.Substring(0, sizeof(addr_ce) * 2);
				if (hasSecondAddr)
					secondAddr = addrs.Substring(sizeof(addr_ce) * 2, sizeof(addr_ce) * 2);
			}

			String evalStr;
			if (valueType != NULL)
			{
				auto ptrType = valueType;
				if (!valueType->IsPointer())
					ptrType = ceModule->CreatePointerType(valueType);
				evalStr = StrFormat("(comptype(%d)", ptrType->mTypeId);
				evalStr += ")0x{1}";
			}
			else
			{
				evalStr += "({1})0x{2}";
			}
			if (!debugVis->mShowElementAddrs)
				evalStr.Insert(0, "*");

			if (addrs.length() > 0)
			{
				evalStr += ", refid=\"" + referenceId + ".[]\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
					"\t[{0}]\t" + evalStr + "\t" + firstAddr;

				if (hasSecondAddr)
					retVal += "\t" + secondAddr;

				if (size != 0)
				{
					retVal += "\n:addrs\t" + addrs;
					if (valueType == NULL)
						retVal += "\n:addrsEntrySize\t2";
					if (continuationData.length() > 0)
						retVal += "\n:continuation\t" + continuationData;
				}
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_TreeItems)
	{
		BfType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures));
		}

		BfTypedValue sizeValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		BfTypedValue headPointer = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);

		if ((sizeValue) && (headPointer) && (sizeValue.mType->IsInteger()) && (ValueToInt(sizeValue) > 0))
		{
			BfTypedValue curNode = headPointer;
			Array<addr_ce> parentList;
			String continuationData;

			int getItemCount = (int)BF_MIN(ValueToInt(sizeValue), 32LL);

			String addrs = GetTreeItems(debugVis, parentList, valueType, curNode, getItemCount, &continuationData);
			addr_ce firstAddr = 0;
			addr_ce secondAddr = 0;
			bool hasSecondAddr = valueType == NULL;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = DecodeTargetDataPtr(addrsPtr);
				if (hasSecondAddr)
					secondAddr = DecodeTargetDataPtr(addrsPtr);
			}

			String evalStr;
			if (valueType != NULL)
			{
				auto ptrType = valueType;
				if (!valueType->IsPointer())
					ptrType = ceModule->CreatePointerType(valueType);
				evalStr = StrFormat("(comptype(%d)", ptrType->mTypeId);
				evalStr += ")0x{1}";
			}
			else
			{
				evalStr += "*(_T_{1}*)0x{2}";
			}

			int size = (int)ValueToInt(sizeValue);
			if (addrs.length() == 0)
			{
				evalStr = ""; // Failed
			}

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
				"\t[{0}]\t" + evalStr + "\t" + EncodeDataPtr(firstAddr, false);

			if (hasSecondAddr)
				retVal += "\t" + EncodeDataPtr(secondAddr, false);

			if (addrs.length() > 0)
			{
				retVal += "\n:addrs\t" + addrs;
				if (continuationData.length() > 0)
					retVal += "\n:continuation\t" + continuationData;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Dictionary)
	{
		BfTypedValue sizeValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		BfTypedValue entriesPtrValue = EvaluateInContext(typedValue, debugVisualizers->DoStringReplace(debugVis->mEntries, dbgVisWildcardCaptures), &formatInfo);

		if ((sizeValue) && (entriesPtrValue) && (sizeValue.mType->IsInteger()) && (ValueToInt(sizeValue) > 0))
		{
			String continuationData;

			BfType* valueType = entriesPtrValue.mType;

			int getItemCount = (int)BF_MIN(ValueToInt(sizeValue), 2LL);

			BfType* useTypedValType = typedValue.mType;

			String addrs = GetDictionaryItems(debugVis, typedValue, 0, -1, getItemCount, &continuationData);
			addr_ce firstAddr = 0;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = DecodeTargetDataPtr(addrsPtr);
			}

			String evalStr = "((comptype(" + StrFormat("%d", valueType->mTypeId) + "))(void*)0x{1}), na";

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, (int)ValueToInt(sizeValue), 10000) +
				"\t[{0}]\t" + evalStr + "\t" + EncodeDataPtr(firstAddr, false);

			if (addrs.length() > 0)
			{
				retVal += "\n:addrs\t" + addrs;
				if (continuationData.length() > 0)
					retVal += "\n:continuation\t" + continuationData;
			}
		}
	}

	if (formatInfo.mExpandItemDepth == 0)
	{
		retVal += "\n[Raw View]\tthis,this=" + ptrUseDataStr + ", nv";
	}
}

String CeDebugger::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	CeFormatInfo formatInfo;
	return Evaluate(expr, formatInfo, callStackIdx, cursorPos, language, expressionFlags);
}

String CeDebugger::EvaluateContinue()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mDebugPendingExpr == NULL)
		return "";

	if (!mDebugPendingExpr->mDone)
		return "!pending";

	String result = mDebugPendingExpr->mResult;
	delete mDebugPendingExpr;
	mDebugPendingExpr = NULL;
	return result;
}

void CeDebugger::EvaluateContinueKeep()
{
}

String CeDebugger::EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos)
{
	return String();
}

String CeDebugger::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
{
	return String();
}

String CeDebugger::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	return String();
}

String CeDebugger::GetAutoLocals(int callStackIdx, bool showRegs)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mCeMachine->mDbgPaused)
		return "";

	String result;

	auto ceFrame = GetFrame(callStackIdx);
	if (ceFrame == NULL)
		return result;

	int scopeIdx = -1;
	auto ceEntry = ceFrame->mFunction->FindEmitEntry(ceFrame->GetInstIdx());
	if (ceEntry != NULL)
		scopeIdx = ceEntry->mScope;

	int instIdx = ceFrame->GetInstIdx();
	if (ceFrame->mFunction->mDbgInfo != NULL)
	{
		auto ceFunction = ceFrame->mFunction;

		struct CeDbgInfo
		{
			int mShadowCount;
			int mPrevShadowIdx;
			bool mIncluded;

			CeDbgInfo()
			{
				mShadowCount = 0;
				mPrevShadowIdx = -1;
				mIncluded = true;
			}
		};

		Array<CeDbgInfo> dbgInfo;
		Dictionary<String, int> nameIndices;

		dbgInfo.Resize(ceFunction->mDbgInfo->mVariables.mSize);
		for (int i = 0; i < ceFunction->mDbgInfo->mVariables.mSize; i++)
		{
			auto& dbgVar = ceFunction->mDbgInfo->mVariables[i];

			if ((dbgVar.mScope == scopeIdx) && (instIdx >= dbgVar.mStartCodePos) && (instIdx < dbgVar.mEndCodePos))
			{
				int* idxPtr = NULL;
				if (!nameIndices.TryAdd(dbgVar.mName, NULL, &idxPtr))
				{
					int checkIdx = *idxPtr;
					dbgInfo[i].mPrevShadowIdx = checkIdx;

					while (checkIdx != -1)
					{
						dbgInfo[checkIdx].mShadowCount++;
						checkIdx = dbgInfo[checkIdx].mPrevShadowIdx;
					}
				}
				*idxPtr = i;
			}
			else
			{
				dbgInfo[i].mIncluded = false;
			}
		}

		for (int i = 0; i < ceFunction->mDbgInfo->mVariables.mSize; i++)
		{
			auto& dbgVar = ceFunction->mDbgInfo->mVariables[i];
			if (dbgInfo[i].mIncluded)
			{
				for (int shadowIdx = 0; shadowIdx < dbgInfo[i].mShadowCount; shadowIdx++)
					result += "@";
				result += dbgVar.mName;
				result += "\n";
			}
		}
	}

	return result;
}

String CeDebugger::CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx)
{
	return String();
}

String CeDebugger::GetProcessInfo()
{
	return String();
}

DebugVisualizerEntry* CeDebugger::FindVisualizerForType(BfType* dbgType, Array<String>* wildcardCaptures)
{
	auto ceModule = mCeMachine->mCeModule;

	ceModule->PopulateType(dbgType);
	auto entry = mDebugManager->mDebugVisualizers->FindEntryForType(ceModule->TypeToString(dbgType), DbgFlavor_Unknown, wildcardCaptures);

	if (entry == NULL)
	{
		auto typeInst = dbgType->ToTypeInstance();
		if ((typeInst != NULL) && (typeInst->mBaseType != NULL))
			entry = FindVisualizerForType(typeInst->mBaseType, wildcardCaptures);
	}

	return entry;
}

String CeDebugger::GetThreadInfo()
{
	return String();
}

void CeDebugger::SetActiveThread(int threadId)
{
}

int CeDebugger::GetActiveThread()
{
	return 0;
}

void CeDebugger::FreezeThread(int threadId)
{
}

void CeDebugger::ThawThread(int threadId)
{
}

bool CeDebugger::IsActiveThreadWaiting()
{
	return false;
}

void CeDebugger::ClearCallStack()
{
	AutoCrit autoCrit(mCeMachine->mCritSect);
	mDbgCallStack.Clear();
}

void CeDebugger::UpdateCallStack(bool slowEarlyOut)
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	if (!mDbgCallStack.IsEmpty())
		return;

	auto ceContext = mCeMachine->mCurContext;
	for (int frameIdx = ceContext->mCallStack.mSize - 1; frameIdx >= 0; frameIdx--)
	{
		auto ceFrame = &ceContext->mCallStack[frameIdx];

		auto instIdx = ceFrame->GetInstIdx();
		auto emitEntry = ceFrame->mFunction->FindEmitEntry(instIdx);
		if (emitEntry == NULL)
			continue;

		int scopeIdx = emitEntry->mScope;
		int prevInlineIdx = -1;
		while (scopeIdx != -1)
		{
			CeDbgStackInfo ceDbgStackInfo;
			ceDbgStackInfo.mFrameIdx = frameIdx;
			ceDbgStackInfo.mScopeIdx = scopeIdx;
			ceDbgStackInfo.mInlinedFrom = prevInlineIdx;
			mDbgCallStack.Add(ceDbgStackInfo);

			auto ceScope = &ceFrame->mFunction->mDbgScopes[scopeIdx];
			if (ceScope->mInlinedAt == -1)
				break;
			auto inlineInfo = &ceFrame->mFunction->mDbgInlineTable[ceScope->mInlinedAt];
			scopeIdx = inlineInfo->mScope;
			prevInlineIdx = ceScope->mInlinedAt;
		}
	}

	CeDbgStackInfo ceDbgStackInfo;
	ceDbgStackInfo.mFrameIdx = -1;
	ceDbgStackInfo.mScopeIdx = -1;
	ceDbgStackInfo.mInlinedFrom = -1;
	mDbgCallStack.Add(ceDbgStackInfo);
}

int CeDebugger::GetCallStackCount()
{
	AutoCrit autoCrit(mCeMachine->mCritSect);

	if (!mCeMachine->mDbgPaused)
		return 0;

	UpdateCallStack();
	return mDbgCallStack.mSize;
}

int CeDebugger::GetRequestedStackFrameIdx()
{
	return mPendingActiveFrameOffset;
}

int CeDebugger::GetBreakStackFrameIdx()
{
	return 0;
}

bool CeDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	auto ceContext = mCeMachine->mCurContext;
	if (ceContext == NULL)
		return false;
	auto ptr = ceContext->GetMemoryPtr((addr_ce)address, (int32)length);
	if (ptr == NULL)
		return false;
	memcpy(dest, ptr, length);
	return true;
}

bool CeDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	auto ceContext = mCeMachine->mCurContext;
	if (ceContext == NULL)
		return false;
	auto ptr = ceContext->GetMemoryPtr((addr_ce)address, (int32)length);
	if (ptr == NULL)
		return false;
	memcpy(ptr, src, length);
	return true;
}

DbgMemoryFlags CeDebugger::GetMemoryFlags(intptr address)
{
	return (DbgMemoryFlags)(DbgMemoryFlags_Read | DbgMemoryFlags_Write);
}

void CeDebugger::UpdateRegisterUsage(int stackFrameIdx)
{
}

void CeDebugger::UpdateCallStackMethod(int stackFrameIdx)
{
}

void CeDebugger::GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
}

void CeDebugger::GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	*outThreadId = 0;
	if (outStackIdx != NULL)
		*outStackIdx = -1;

	if (!mCeMachine->mDbgPaused)
		return;

	auto ceContext = mCeMachine->mCurContext;
	for (int i = 0; i < (int)ceContext->mCallStack.mSize; i++)
	{
		auto ceFrame = &ceContext->mCallStack[i];
		if ((addr_ce)addr > ceFrame->mFrameAddr)
		{
			if (outStackIdx != NULL)
				*outStackIdx = (int)ceContext->mCallStack.mSize - i - 1;
		}
	}
}

String CeDebugger::GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd, int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mCeMachine->mDbgPaused)
		return "";

	UpdateCallStack();

	enum FrameFlags
	{
		FrameFlags_Optimized = 1,
		FrameFlags_HasPendingDebugInfo = 2,
		FrameFlags_CanGetOldSource = 4,
		FrameFlags_WasHotReplaced = 8,
		FrameFlags_HadError = 0x10
	};

	auto ceContext = mCeMachine->mCurContext;

	*addr = 0;
	*outFile = "";
	*outHotIdx = 0;
	*outDefLineStart = -1;
	*outDefLineEnd = -1;
	*outLine = -1;
	*outColumn = 0;
	*outLanguage = DbgLanguage_Beef;
	*outStackSize = 0;
	*outFlags = 0;

	if (ceContext == NULL)
		return "";

	auto& dbgCallstackInfo = mDbgCallStack[stackFrameIdx];

	if (dbgCallstackInfo.mFrameIdx == -1)
	{
		if (ceContext->mCurCallSource != NULL)
		{
			int line = -1;
			int lineChar = -1;
			auto parserData = ceContext->mCurCallSource->mRefNode->GetParserData();
			if (parserData != NULL)
			{
				parserData->GetLineCharAtIdx(ceContext->mCurCallSource->mRefNode->GetSrcStart(), line, lineChar);
				*outLine = line;
				*outColumn = lineChar;
				*outFile = parserData->mFileName;
			}
		}

		// Entry marker
		String result = "const eval";
		if (ceContext->mCurCallSource->mKind == CeCallSource::Kind_FieldInit)
		{
			return String("field init of : ") + ceContext->mCurModule->TypeToString(ceContext->mCurCallSource->mFieldInstance->mOwner) + "." +
				ceContext->mCurCallSource->mFieldInstance->GetFieldDef()->mName;
		}
		else if (ceContext->mCurCallSource->mKind == CeCallSource::Kind_MethodInit)
		{
			return ("method init of : ") + ceContext->mCurModule->MethodToString(ceContext->mCallerMethodInstance);
		}
		else if (ceContext->mCurCallSource->mKind == CeCallSource::Kind_TypeInit)
			result = "type init";
		else if (ceContext->mCurCallSource->mKind == CeCallSource::Kind_TypeDone)
			result = "type done";

		if (ceContext->mCallerMethodInstance != NULL)
			result += String(" in : ") + ceContext->mCurModule->MethodToString(ceContext->mCallerMethodInstance);
		else if (ceContext->mCallerTypeInstance != NULL)
			result += String(" in : ") + ceContext->mCurModule->TypeToString(ceContext->mCallerTypeInstance);
		return result;
	}

	auto ceFrame = &ceContext->mCallStack[dbgCallstackInfo.mFrameIdx];
	auto ceFunction = ceFrame->mFunction;

	if (ceFunction->mFailed)
		*outFlags |= FrameFlags_HadError;

	int instIdx = (int)(ceFrame->mInstPtr - &ceFunction->mCode[0] - 2);

	BF_ASSERT(ceFunction->mId != -1);
	*addr = ((intptr)ceFunction->mId << 32) | instIdx;

	CeEmitEntry* emitEntry = ceFunction->FindEmitEntry(instIdx);

	*outStackSize = ceContext->mStackSize - ceFrame->mStackAddr;
	if (stackFrameIdx < mDbgCallStack.mSize - 1)
	{
		auto& nextStackInfo = mDbgCallStack[stackFrameIdx + 1];
		if (nextStackInfo.mFrameIdx >= 0)
		{
			auto prevFrame = &ceContext->mCallStack[nextStackInfo.mFrameIdx];
			*outStackSize = prevFrame->mStackAddr - ceFrame->mStackAddr;
		}
	}

	CeDbgScope* ceScope = NULL;
	if (dbgCallstackInfo.mScopeIdx != -1)
		ceScope = &ceFunction->mDbgScopes[dbgCallstackInfo.mScopeIdx];

	if (emitEntry != NULL)
	{
		if (emitEntry->mScope != -1)
			*outFile = ceFunction->mDbgScopes[emitEntry->mScope].mFilePath;
		*outLine = emitEntry->mLine;
		*outColumn = emitEntry->mColumn;
	}

	if (dbgCallstackInfo.mInlinedFrom != -1)
	{
		auto dbgInlineInfo = &ceFunction->mDbgInlineTable[dbgCallstackInfo.mInlinedFrom];
		*outLine = dbgInlineInfo->mLine;
		*outColumn = dbgInlineInfo->mColumn;
	}

	if ((ceScope != NULL) && (ceScope->mMethodVal != -1))
	{
		if ((ceScope->mMethodVal & CeDbgScope::MethodValFlag_MethodRef) != 0)
		{
			auto dbgMethodRef = &ceFunction->mDbgMethodRefTable[ceScope->mMethodVal & CeDbgScope::MethodValFlag_IdxMask];
			return dbgMethodRef->ToString();
		}
		else
		{
			auto callTableEntry = &ceFunction->mCallTable[ceScope->mMethodVal];
			return ceContext->mCurModule->MethodToString(callTableEntry->mFunctionInfo->mMethodInstance);
		}
	}

	return ceContext->mCurModule->MethodToString(ceFrame->mFunction->mMethodInstance);
}

BfType* CeDebugger::FindType(const StringImpl& name)
{
	if (name == "System.Object")
		return mCeMachine->mCeModule->mContext->mBfObjectType;

	BfParser parser(mCompiler->mSystem);
	BfPassInstance passInstance(mCompiler->mSystem);
	parser.SetSource(name.c_str(), (int)name.length());
	parser.Parse(&passInstance);

	BfReducer reducer;
	reducer.mAlloc = parser.mAlloc;
	reducer.mSystem = mCompiler->mSystem;
	reducer.mPassInstance = &passInstance;
	reducer.mVisitorPos = BfReducer::BfVisitorPos(parser.mRootNode);
	reducer.mVisitorPos.MoveNext();
	reducer.mSource = &parser;
	auto typeRef = reducer.CreateTypeRef(parser.mRootNode->GetFirst());
	parser.Close();

	auto ceModule = mCeMachine->mCeModule;
	SetAndRestoreValue<bool> prevIgnoreErrors(ceModule->mIgnoreErrors, true);
	SetAndRestoreValue<bool> prevIgnoreWarning(ceModule->mIgnoreWarnings, true);

	return ceModule->ResolveTypeRef(typeRef, {}, BfPopulateType_Declaration, BfResolveTypeRefFlag_IgnoreLookupError);
}

String CeDebugger::Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	return String();
}

#define CE_GET(T) *((T*)(ptr += sizeof(T)) - 1)

int CeDebugger::GetJmpState(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mCeMachine->mDbgPaused)
		return -1;

	if (stackFrameIdx != 0)
		return -1;

	auto ceFrame = GetFrame(stackFrameIdx);
	if (ceFrame == NULL)
		return -1;

	int instIdx = ceFrame->GetInstIdx();

	auto ptr = &ceFrame->mFunction->mCode[instIdx];
	auto op = CE_GET(CeOp);
	switch (op)
	{
	case CeOp_Jmp:
		return 1;
	case CeOp_JmpIf:
	case CeOp_JmpIfNot:
		{
			CE_GET(int32);
			int frameOfs = CE_GET(int32);
			bool willJump = *(bool*)(mCeMachine->mCurContext->mMemory.mVals + ceFrame->mFrameAddr + frameOfs);
			if (op == CeOp_JmpIfNot)
				willJump = !willJump;
			return willJump ? 1 : 0;
		}
	}

	return -1;
}

intptr CeDebugger::GetStackFrameCalleeAddr(int stackFrameIdx)
{
	return intptr();
}

String CeDebugger::GetStackMethodOwner(int stackFrameIdx, int& language)
{
	return String();
}

String CeDebugger::FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve)
{
	return String();
}

String CeDebugger::GetAddressSourceLocation(intptr address)
{
	return String();
}

String CeDebugger::GetAddressSymbolName(intptr address, bool demangle)
{
	return String();
}

String CeDebugger::DisassembleAtRaw(intptr address)
{
	return String();
}

String CeDebugger::DisassembleAt(intptr address)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mCeMachine->mDbgPaused)
		return "";

	auto ceContext = mCeMachine->mCurContext;

	mCurDisasmFuncId = (int)(address >> 32);
	UpdateBreakpointAddrs();

	CeFunction* ceFunction = NULL;
	if (!mCeMachine->mFunctionIdMap.TryGetValue(mCurDisasmFuncId, &ceFunction))
		return "";

	CeDumpContext dumpCtx;
	dumpCtx.mCeFunction = ceFunction;
	dumpCtx.mStart = &ceFunction->mCode[0];
	dumpCtx.mPtr = dumpCtx.mStart;
	dumpCtx.mEnd = dumpCtx.mPtr + ceFunction->mCode.mSize;

	uint8* start = dumpCtx.mStart;

	dumpCtx.mStr += StrFormat("T Frame Size: %d\n", ceFunction->mFrameSize);
	dumpCtx.mStr += StrFormat("A %llX\n", (intptr)mCurDisasmFuncId << 32);

	int curEmitIdx = 0;
	CeEmitEntry* prevEmitEntry = NULL;
	CeEmitEntry* curEmitEntry = NULL;

	String prevSourcePath;

	while (dumpCtx.mPtr < dumpCtx.mEnd)
	{
		int ofs = (int)(dumpCtx.mPtr - start);

		while ((curEmitIdx < ceFunction->mEmitTable.mSize - 1) && (ofs >= ceFunction->mEmitTable[curEmitIdx + 1].mCodePos))
			curEmitIdx++;
		if (curEmitIdx < ceFunction->mEmitTable.mSize)
			curEmitEntry = &ceFunction->mEmitTable[curEmitIdx];

		if (prevEmitEntry != curEmitEntry)
		{
			if ((curEmitEntry != NULL) && (curEmitEntry->mLine != -1))
			{
				bool pathChanged = false;
				if ((prevEmitEntry == NULL) || (curEmitEntry->mScope != prevEmitEntry->mScope))
				{
					auto ceDbgScope = &ceFunction->mDbgScopes[curEmitEntry->mScope];
					if (ceDbgScope->mFilePath != prevSourcePath)
					{
						pathChanged = true;
						dumpCtx.mStr += StrFormat("S %s\n", ceDbgScope->mFilePath.c_str());
						prevSourcePath = ceDbgScope->mFilePath;
					}
				}

				if ((prevEmitEntry != NULL) && (!pathChanged) && (curEmitEntry->mLine >= prevEmitEntry->mLine))
				{
					dumpCtx.mStr += StrFormat("L %d %d\n", prevEmitEntry->mLine + 1, curEmitEntry->mLine - prevEmitEntry->mLine);
				}
				else
				{
					int startLine = BF_MAX(0, curEmitEntry->mLine - 5);
					dumpCtx.mStr += StrFormat("L %d %d\n", startLine, curEmitEntry->mLine - startLine + 1);
				}

				prevEmitEntry = curEmitEntry;
			}
		}

		dumpCtx.mStr += StrFormat("D %04X: ", ofs);
		dumpCtx.Next();
		dumpCtx.mStr += "\n";

		if (dumpCtx.mJmp != -1)
		{
			dumpCtx.mStr += StrFormat("J %X\n", dumpCtx.mJmp);
			dumpCtx.mJmp = -1;
		}
	}

	return dumpCtx.mStr;
}

String CeDebugger::FindLineCallAddresses(intptr address)
{
	return String();
}

String CeDebugger::GetCurrentException()
{
	return String();
}

String CeDebugger::GetModulesInfo()
{
	return String();
}

void CeDebugger::SetAliasPath(const StringImpl& origPath, const StringImpl& localPath)
{
}

void CeDebugger::CancelSymSrv()
{
}

bool CeDebugger::HasPendingDebugLoads()
{
	return false;
}

int CeDebugger::LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

int CeDebugger::LoadDebugInfoForModule(const StringImpl& moduleName)
{
	return 0;
}

int CeDebugger::LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

void CeDebugger::StopDebugging()
{
	mRunState = RunState_Terminating;
	mCeMachine->mDebugEvent.Set(true);
}

void CeDebugger::Terminate()
{
}

void CeDebugger::Detach()
{
	mRunState = RunState_Terminated;
	mDbgCallStack.Clear();
}

Profiler* CeDebugger::StartProfiling()
{
	return nullptr;
}

Profiler* CeDebugger::PopProfiler()
{
	return nullptr;
}

void CeDebugger::ReportMemory(MemReporter* memReporter)
{
}

bool CeDebugger::IsOnDemandDebugger()
{
	return true;
}

bool CeDebugger::GetEmitSource(const StringImpl& filePath, String& outText)
{
	return false;
}