#pragma warning(disable:4996)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#include "DbgTargetHost.h"
#include "DbgExprEvaluator.h"
#include "DebugManager.h"
#include "DebugVisualizers.h"
#include "CPU.h"
#include "X86.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/UTF8.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/FileEnumerator.h"
#include <inttypes.h>
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"
#include "Compiler/BfDemangler.h"
#include "Compiler/BfPrinter.h"
#include <float.h>
#ifdef BF_PLATFORM_WINDOWS
#include "COFF.h"
#endif
#include "BeefySysLib/util/AllocDebug.h"
#include <limits>

USING_NS_BF_DBG;

DbgTargetHost::DbgTargetHost()
{
	mEmptyDebugTarget = NULL;
	mDbgBreak = false;
	mDebugPendingExpr = NULL;
}

DbgPendingExpr::DbgPendingExpr()
{
	mThreadId = -1;
	mCallStackIdx = -1;
	mParser = NULL;
	mCursorPos = -1;
	mExprNode = NULL;
	mIdleTicks = 0;
	mExplitType = NULL;
	mExpressionFlags = DwEvalExpressionFlag_None;
	mUsedSpecifiedLock = false;
	mStackIdxOverride = -1;
}

DbgPendingExpr::~DbgPendingExpr()
{
	delete mParser;
}

bool DbgTargetHost::FixCallStackIdx(int& callStackIdx)
{
	callStackIdx = BF_MAX(callStackIdx, 0);
	if (mCallStack.IsEmpty())
		UpdateCallStack();

	int stackSize = (int)mCallStack.size();
	while (callStackIdx >= mCallStack.size())
	{
		UpdateCallStack();
		if (stackSize == (int)mCallStack.size())
			break; // Didn't change
		stackSize = (int)mCallStack.size();
	}

	if (callStackIdx >= stackSize)
	{
		callStackIdx = 0;
		return false;
	}
	return true;
}

DbgTypedValue DbgTargetHost::GetRegister(const StringImpl& regName, DbgLanguage language, CPURegisters* registers, Array<RegForm>* regForms)
{
	int regNum = -1;

	String lwrRegName = ToLower(regName);

	// int regs

#ifdef BF_DBG_32
	DbgTypeCode regType = DbgType_i32;
	if (lwrRegName == "eax")
		regNum = X86Reg_EAX;
	else if (lwrRegName == "ecx")
		regNum = X86Reg_ECX;
	else if (lwrRegName == "edx")
		regNum = X86Reg_EDX;
	else if (lwrRegName == "ebx")
		regNum = X86Reg_EBX;
	else if (lwrRegName == "esp")
		regNum = X86Reg_ESP;
	else if (lwrRegName == "ebp")
		regNum = X86Reg_EBP;
	else if (lwrRegName == "esi")
		regNum = X86Reg_ESI;
	else if (lwrRegName == "edi")
		regNum = X86Reg_EDI;
	else if (lwrRegName == "eip")
		regNum = X86Reg_EIP;
	else if (lwrRegName == "efl")
		regNum = X86Reg_EFL;
#else
	DbgTypeCode regType = DbgType_i64;

	if (lwrRegName == "rax")
		regNum = X64Reg_RAX;
	else if (lwrRegName == "rcx")
		regNum = X64Reg_RCX;
	else if (lwrRegName == "rdx")
		regNum = X64Reg_RDX;
	else if (lwrRegName == "rbx")
		regNum = X64Reg_RBX;
	else if (lwrRegName == "rsp")
		regNum = X64Reg_RSP;
	else if (lwrRegName == "rbp")
		regNum = X64Reg_RBP;
	else if (lwrRegName == "rsi")
		regNum = X64Reg_RSI;
	else if (lwrRegName == "rdi")
		regNum = X64Reg_RDI;
	else if (lwrRegName == "rip")
		regNum = X64Reg_RIP;
	else if (lwrRegName == "r8")
		regNum = X64Reg_R8;
	else if (lwrRegName == "r9")
		regNum = X64Reg_R9;
	else if (lwrRegName == "r10")
		regNum = X64Reg_R10;
	else if (lwrRegName == "r11")
		regNum = X64Reg_R11;
	else if (lwrRegName == "r12")
		regNum = X64Reg_R12;
	else if (lwrRegName == "r13")
		regNum = X64Reg_R13;
	else if (lwrRegName == "r14")
		regNum = X64Reg_R14;
	else if (lwrRegName == "r15")
		regNum = X64Reg_R15;
	else
	{
		regType = DbgType_i32;
		if (lwrRegName == "eax")
			regNum = X64Reg_RAX;
		else if (lwrRegName == "ecx")
			regNum = X64Reg_RCX;
		else if (lwrRegName == "edx")
			regNum = X64Reg_RDX;
		else if (lwrRegName == "ebx")
			regNum = X64Reg_RBX;
		else if (lwrRegName == "efl")
			regNum = X64Reg_EFL;
		else if (lwrRegName == "esi")
			regNum = X64Reg_RSI;
		else if (lwrRegName == "edi")
			regNum = X64Reg_RDI;
		else if (lwrRegName == "r8d")
			regNum = X64Reg_R8;
		else if (lwrRegName == "r9d")
			regNum = X64Reg_R9;
		else if (lwrRegName == "r10d")
			regNum = X64Reg_R10;
		else if (lwrRegName == "r11d")
			regNum = X64Reg_R11;
		else if (lwrRegName == "r12d")
			regNum = X64Reg_R12;
		else if (lwrRegName == "r13d")
			regNum = X64Reg_R13;
		else if (lwrRegName == "r14d")
			regNum = X64Reg_R14;
		else if (lwrRegName == "r15d")
			regNum = X64Reg_R15;
		else
		{
			regType = DbgType_i16;
			if (lwrRegName == "ax")
				regNum = X64Reg_RAX;
			else if (lwrRegName == "cx")
				regNum = X64Reg_RCX;
			else if (lwrRegName == "dx")
				regNum = X64Reg_RDX;
			else if (lwrRegName == "bx")
				regNum = X64Reg_RBX;
			else if (lwrRegName == "si")
				regNum = X64Reg_RSI;
			else if (lwrRegName == "di")
				regNum = X64Reg_RDI;
			else if (lwrRegName == "r8w")
				regNum = X64Reg_R8;
			else if (lwrRegName == "r9w")
				regNum = X64Reg_R9;
			else if (lwrRegName == "r10w")
				regNum = X64Reg_R10;
			else if (lwrRegName == "r11w")
				regNum = X64Reg_R11;
			else if (lwrRegName == "r12w")
				regNum = X64Reg_R12;
			else if (lwrRegName == "r13w")
				regNum = X64Reg_R13;
			else if (lwrRegName == "r14w")
				regNum = X64Reg_R14;
			else if (lwrRegName == "r15w")
				regNum = X64Reg_R15;
			else
			{
				regType = DbgType_i8;
				if (lwrRegName == "al")
					regNum = X64Reg_RAX;
				else if (lwrRegName == "cl")
					regNum = X64Reg_RCX;
				else if (lwrRegName == "dl")
					regNum = X64Reg_RDX;
				else if (lwrRegName == "bl")
					regNum = X64Reg_RBX;
				else if (lwrRegName == "sil")
					regNum = X64Reg_RSI;
				else if (lwrRegName == "dil")
					regNum = X64Reg_RDI;
				else if (lwrRegName == "r8b")
					regNum = X64Reg_R8;
				else if (lwrRegName == "r9b")
					regNum = X64Reg_R9;
				else if (lwrRegName == "r10b")
					regNum = X64Reg_R10;
				else if (lwrRegName == "r11b")
					regNum = X64Reg_R11;
				else if (lwrRegName == "r12b")
					regNum = X64Reg_R12;
				else if (lwrRegName == "r13b")
					regNum = X64Reg_R13;
				else if (lwrRegName == "r14b")
					regNum = X64Reg_R14;
				else if (lwrRegName == "r15b")
					regNum = X64Reg_R15;
			}
		}
	}
#endif

	auto dbgModule = mEmptyDebugTarget->GetMainDbgModule();

	if (regNum != -1)
	{
		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(regType, language);
		typedVal.mInt64 = registers->mIntRegsArray[regNum];
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	// st regs

	if ((lwrRegName.length() == 3) && (lwrRegName[0] == 's') && (lwrRegName[1] == 't') && (lwrRegName[2] >= '0') && (lwrRegName[2] <= '7'))
	{
		regNum = CPUReg_FPSTREG_FIRST + (lwrRegName[2] - '0');
	}
	if (regNum != -1)
	{
		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_Double, language);
		typedVal.mDouble = ConvertFloat80ToDouble(registers->mFpMmRegsArray[regNum - CPUReg_FPSTREG_FIRST].fp.fp80);
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	// mm regs

	if ((lwrRegName.length() == 3) && (lwrRegName[0] == 'm') && (lwrRegName[1] == 'm') && (lwrRegName[2] >= '0') && (lwrRegName[2] <= '7'))
	{
		regNum = CPUReg_MMREG_FIRST + (lwrRegName[2] - '0');
	}
	if (regNum != -1)
	{
		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_i64, language);
		typedVal.mInt64 = registers->mFpMmRegsArray[regNum - CPUReg_MMREG_FIRST].mm;
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	// xmm regs

#ifdef BF_DBG_32
	if ((lwrRegName.length() == 6) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] >= '0') && (lwrRegName[3] <= '7') &&
		(lwrRegName[4] == '_') && (lwrRegName[5] >= '0') && (lwrRegName[5] <= '3'))
	{
		regNum = CPUReg_XMMREG_FIRST + ((lwrRegName[3] - '0') * 4) + (lwrRegName[5] - '0');
	}
#else
	if ((lwrRegName.length() == 6) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] >= '0') && (lwrRegName[3] <= '9') &&
		(lwrRegName[4] == '_') && (lwrRegName[5] >= '0') && (lwrRegName[5] <= '3'))
	{
		regNum = CPUReg_XMMREG_FIRST + ((lwrRegName[3] - '0') * 4) + (lwrRegName[5] - '0');
	}
	if ((lwrRegName.length() == 7) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] == '1') && (lwrRegName[4] >= '0') && (lwrRegName[4] <= '9') &&
		(lwrRegName[5] == '_') && (lwrRegName[6] >= '0') && (lwrRegName[6] <= '3'))
	{
		regNum = CPUReg_XMMREG_FIRST + ((10 + (lwrRegName[4] - '0')) * 4) + (lwrRegName[6] - '0');
	}
#endif

	if (regNum != -1)
	{
		int xmmMajor = (regNum - CPUReg_XMMREG_FIRST) >> 2;
		int xmmMinor = (regNum - CPUReg_XMMREG_FIRST) & 3;

		DwMmDisplayType mmDisplayType = GetDisplayInfo(StrFormat("$XMM%d", xmmMajor))->mMmDisplayType;

		RegForm regForm = RegForm_Unknown;
		if (regForms != NULL)
		{
			int regFormIdx = CPUReg_M128_XMMREG_FIRST + xmmMajor;
			if (regFormIdx < (int)regForms->size())
				regForm = (*regForms)[regFormIdx];
		}
		if (mmDisplayType == DwMmDisplayType_Default)
		{
			if ((regForm == RegForm_Double) || (regForm == RegForm_Double2))
				mmDisplayType = DwMmDisplayType_Double;
			else if (regForm == RegForm_Int4)
				mmDisplayType = DwMmDisplayType_Int32;
		}

		//TODO: Add int types
		if (mmDisplayType == DwMmDisplayType_Double)
		{
			DbgTypedValue typedVal;
			typedVal.mType = dbgModule->GetPrimitiveType(DbgType_Double, language);
			typedVal.mDouble = registers->mXmmDRegsArray[xmmMajor].d[xmmMinor];
			typedVal.mRegNum = regNum;
			return typedVal;
		}
		else if ((mmDisplayType == DwMmDisplayType_UInt8) || (mmDisplayType == DwMmDisplayType_Int16) || (mmDisplayType == DwMmDisplayType_Int32))
		{
			DbgTypedValue typedVal;
			typedVal.mType = dbgModule->GetPrimitiveType(DbgType_i32, language);
			typedVal.mInt32 = registers->mXmmI32RegsARray[xmmMajor].i[xmmMinor];
			typedVal.mRegNum = regNum;
			return typedVal;
		}
		else if (mmDisplayType == DwMmDisplayType_Int64)
		{
			DbgTypedValue typedVal;
			typedVal.mType = dbgModule->GetPrimitiveType(DbgType_i64, language);
			typedVal.mInt64 = registers->mXmmI64RegsARray[xmmMajor].i[xmmMinor];
			typedVal.mRegNum = regNum;
			return typedVal;
		}

		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_Single, language);
		typedVal.mSingle = registers->mXmmRegsArray[xmmMajor].f[xmmMinor];
		typedVal.mRegNum = regNum;
		return typedVal;
	}
#ifdef BF_DBG_32
	if ((lwrRegName.length() == 4) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] >= '0') && (lwrRegName[3] <= '7'))
	{
		regNum = CPUReg_M128_XMMREG_FIRST + (lwrRegName[3] - '0');
	}
#else
	if ((lwrRegName.length() == 4) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] >= '0') && (lwrRegName[3] <= '9'))
	{
		regNum = CPUReg_M128_XMMREG_FIRST + (lwrRegName[3] - '0');
	}
	if ((lwrRegName.length() == 5) && (lwrRegName[0] == 'x') && (lwrRegName[1] == 'm') && (lwrRegName[2] == 'm') && (lwrRegName[3] == '1') && (lwrRegName[4] >= '0') && (lwrRegName[4] <= '5'))
	{
		regNum = CPUReg_M128_XMMREG_FIRST + 10 + (lwrRegName[4] - '0');
	}
#endif
	if (regNum != -1)
	{
		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_RegGroup, language);
		typedVal.mSingle = 0.0f; // ignored at a higher level (but if it's used as an rvalue in the meantime, it'll resolve to zero)
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	// flags

	if ((lwrRegName.length() == 6) && (lwrRegName[0] == 'f') && (lwrRegName[1] == 'l') && (lwrRegName[2] == 'a') && (lwrRegName[3] == 'g') && (lwrRegName[5] == 'f'))
	{
		switch(lwrRegName[4])
		{
		case 'c': regNum = CPUReg_FLAG_CF_CARRY; break;
		case 'p': regNum = CPUReg_FLAG_PF_PARITY; break;
		case 'a': regNum = CPUReg_FLAG_AF_ADJUST; break;
		case 'z': regNum = CPUReg_FLAG_ZF_ZERO; break;
		case 's': regNum = CPUReg_FLAG_SF_SIGN; break;
		case 'i': regNum = CPUReg_FLAG_IF_INTERRUPT; break;
		case 'd': regNum = CPUReg_FLAG_DF_DIRECTION; break;
		case 'o': regNum = CPUReg_FLAG_OF_OVERFLOW; break;
		default: break;
		}
	}
	if (regNum != -1)
	{
		int flagBit = CPURegisters::GetFlagBitForRegister(regNum);
		BF_ASSERT(flagBit >= 0);

		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_Bool, language);
		typedVal.mBool = (registers->mIntRegs.efl & ((uint64)1 << flagBit)) != 0;
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	// categories

	if (lwrRegName == "allregs")
		regNum = CPUReg_CAT_ALLREGS;
	else if (lwrRegName == "iregs")
		regNum = CPUReg_CAT_IREGS;
	else if (lwrRegName == "fpregs")
		regNum = CPUReg_CAT_FPREGS;
	else if (lwrRegName == "mmregs")
		regNum = CPUReg_CAT_MMREGS;
	else if (lwrRegName == "xmmregs")
		regNum = CPUReg_CAT_XMMREGS;
	else if (lwrRegName == "flags")
		regNum = CPUReg_CAT_FLAGS;
	if (regNum != -1)
	{
		DbgTypedValue typedVal;
		typedVal.mType = dbgModule->GetPrimitiveType(DbgType_RegGroup, language);
		typedVal.mSingle = 0.0f; // ignored at a higher level (but if it's used as an rvalue in the meantime, it'll resolve to zero)
		typedVal.mRegNum = regNum;
		return typedVal;
	}

	return DbgTypedValue();
}

DbgModule* DbgTargetHost::GetCallStackDbgModule(int callStackIdx)
{
	if ((mRunState == RunState_NotStarted) || (!IsPaused()))
		return mEmptyDebugTarget->GetMainDbgModule();
	if (callStackIdx == -1)
		return mDebugTarget->GetMainDbgModule();
	FixCallStackIdx(callStackIdx);
	if (callStackIdx >= mCallStack.size())
		return mDebugTarget->GetMainDbgModule();
	UpdateCallStackMethod(callStackIdx);
	auto subProgram = mCallStack[callStackIdx]->mSubProgram;
	if (subProgram != NULL)
		return subProgram->mCompileUnit->mDbgModule;

	auto dbgModule = mDebugTarget->FindDbgModuleForAddress(mCallStack[callStackIdx]->mRegisters.GetPC());
	if (dbgModule != NULL)
		return dbgModule;
	return mDebugTarget->GetMainDbgModule();
}

DbgSubprogram* DbgTargetHost::GetCallStackSubprogram(int callStackIdx)
{
	if ((IsInRunState()) || (mRunState == RunState_NotStarted) || (callStackIdx == -1))
		return NULL;
	if (callStackIdx >= (int)mCallStack.size())
		UpdateCallStack();
	if (mCallStack.IsEmpty())
		return NULL;
	if (callStackIdx >= (int)mCallStack.size())
		callStackIdx = 0;
	UpdateCallStackMethod(callStackIdx);
	auto subProgram = mCallStack[callStackIdx]->mSubProgram;
	return subProgram;
}

DbgCompileUnit* DbgTargetHost::GetCallStackCompileUnit(int callStackIdx)
{
	if ((IsInRunState()) || (mRunState == RunState_NotStarted) || (callStackIdx == -1))
		return NULL;
	if (callStackIdx >= (int)mCallStack.size())
		UpdateCallStack();
	if (mCallStack.IsEmpty())
		return NULL;
	if (callStackIdx >= (int)mCallStack.size())
		callStackIdx = 0;
	UpdateCallStackMethod(callStackIdx);
	auto subProgram = mCallStack[callStackIdx]->mSubProgram;
	if (subProgram == NULL)
		return NULL;
	return subProgram->mCompileUnit;
}

String DbgTargetHost::EvaluateContinue(DbgPendingExpr* pendingExpr, BfPassInstance& bfPassInstance)
{
	DbgModule* dbgModule = NULL;
	DbgCompileUnit* dbgCompileUnit = NULL;

	if (pendingExpr->mThreadId == -1)
	{
		if ((pendingExpr->mFormatInfo.mLanguage == DbgLanguage_Beef) && (mDebugTarget != NULL) && (mDebugTarget->mTargetBinary != NULL))
			dbgModule = mDebugTarget->mTargetBinary;
		else
			dbgModule = mEmptyDebugTarget->GetMainDbgModule();
	}
	else
	{
		dbgModule = GetCallStackDbgModule(pendingExpr->mCallStackIdx);
		if ((dbgModule != NULL) &&(!dbgModule->mDebugTarget->mIsEmpty))
			dbgCompileUnit = GetCallStackCompileUnit(pendingExpr->mCallStackIdx);
	}

	if (dbgModule == NULL)
		dbgModule = mEmptyDebugTarget->GetMainDbgModule();

	if (!pendingExpr->mException.empty())
	{
		RestoreAllRegisters();
		return "!" + pendingExpr->mException;
	}

	DwAutoComplete autoComplete;

	if (bfPassInstance.HasFailed())
	{
		// Don't allow pending calls if we've already failed in the calling Evaluate()
		pendingExpr->mExpressionFlags = (DwEvalExpressionFlags)(pendingExpr->mExpressionFlags & ~DwEvalExpressionFlag_AllowCalls);
	}

	DbgExprEvaluator dbgExprEvaluator(this, dbgModule, &bfPassInstance, pendingExpr->mCallStackIdx, pendingExpr->mCursorPos);
	if (!pendingExpr->mFormatInfo.mStackSearchStr.IsEmpty())
	{
		dbgExprEvaluator.mStackSearch = new DbgStackSearch();
		dbgExprEvaluator.mStackSearch->mSearchStr = pendingExpr->mFormatInfo.mStackSearchStr;
	}
	dbgExprEvaluator.mLanguage = pendingExpr->mFormatInfo.mLanguage;
	dbgExprEvaluator.mReferenceId = &pendingExpr->mReferenceId;
	dbgExprEvaluator.mExpressionFlags = pendingExpr->mExpressionFlags;
	dbgExprEvaluator.mExplicitThis = pendingExpr->mFormatInfo.mExplicitThis;
	dbgExprEvaluator.mSubjectExpr = pendingExpr->mFormatInfo.mSubjectExpr;
	dbgExprEvaluator.mNamespaceSearchStr = pendingExpr->mFormatInfo.mNamespaceSearch;
	dbgExprEvaluator.mExpectingTypeName = pendingExpr->mFormatInfo.mExpectedType;
	dbgExprEvaluator.mCallResults = &pendingExpr->mCallResults;
	if ((pendingExpr->mExpressionFlags & DwEvalExpressionFlag_ValidateOnly) != 0)
	{
		 dbgExprEvaluator.mValidateOnly = true;
	}

	if (pendingExpr->mCursorPos != -1)
	{
		dbgExprEvaluator.mAutoComplete = &autoComplete;
	}
	dbgExprEvaluator.mDbgCompileUnit = dbgCompileUnit;

	DbgTypedValue exprResult;
	if (pendingExpr->mExplitType != NULL)
	{
		exprResult.mHasNoValue = true;
		exprResult.mType = pendingExpr->mExplitType;
	}
	else if (pendingExpr->mExprNode != NULL)
	{
		exprResult = dbgExprEvaluator.Resolve(pendingExpr->mExprNode);
	}

	if (dbgExprEvaluator.mCreatedPendingCall)
	{
		BF_ASSERT(mRunState == RunState_DebugEval);
		//ContinueDebugEvent();
		return "!pending";
	}

	if (dbgExprEvaluator.mCountResultOverride != -1)
		pendingExpr->mFormatInfo.mOverrideCount = dbgExprEvaluator.mCountResultOverride;

	String val;
	if (bfPassInstance.HasFailed())
	{
		BfLogDbgExpr("Evaluate Failed: %s\n", bfPassInstance.mErrors[0]->mError.c_str());
		val = StrFormat("!%d\t%d\t%s", bfPassInstance.mErrors[0]->GetSrcStart(), bfPassInstance.mErrors[0]->GetSrcLength(), bfPassInstance.mErrors[0]->mError.c_str());
	}
	else if (dbgExprEvaluator.mBlockedSideEffects)
	{
		BfLogDbgExpr("Evaluate blocked side effects\n");
		val = "!sideeffects";
	}
	else if (!exprResult)
	{
		if (exprResult.mType != NULL)
		{
			BfLogDbgExpr("Evaluate success\n");

			String typeName = exprResult.mType->ToString();
			DbgType* rawType = exprResult.mType;
			if (rawType->IsBfObjectPtr())
				rawType = rawType->mTypeParam;
			String typeNameRaw = rawType->ToStringRaw();
			val = typeName + "\n" + typeName;
			val += "\n" + GetMemberList(exprResult.mType, typeNameRaw, false, true, false, false, exprResult.mIsReadOnly);
			if (exprResult.mType->mTypeCode == DbgType_Namespace)
			{
				val += "\n:type\tnamespace";
			}
			else
			{
				auto type = exprResult.mType;
				if (type->IsPointer())
					type = type->mTypeParam;
				if (type->IsBfObject())
					val += "\n:type\tclass";
				else
					val += "\n:type\tvaluetype";
			}

			if (!pendingExpr->mReferenceId.empty())
				val += "\n:referenceId\t" + pendingExpr->mReferenceId;
		}
		else
			val = "!";
	}
	else if ((pendingExpr->mExpressionFlags & (DwEvalExpressionFlag_MemoryAddress)) != 0)
	{
		DbgType* resultType = exprResult.mType->RemoveModifiers();
		if ((resultType->IsInteger()) || (resultType->IsPointerOrRef()))
		{
			val = EncodeDataPtr(exprResult.mPtr, false) + "\n" + StrFormat("%d", 0);
		}
		else
		{
			if (exprResult.mSrcAddress != 0)
				val = StrFormat("!Type '%s' is invalid. A pointer or address value is expected. Try using the '&' address-of operator.", exprResult.mType->ToString().c_str());
			else
				val = StrFormat("!Type '%s' is invalid. A pointer or address value is expected. An explicit cast may be required.", exprResult.mType->ToString().c_str());
		}
	}
	else if ((pendingExpr->mExpressionFlags & (DwEvalExpressionFlag_MemoryWatch)) != 0)
	{
		DbgType* resultType = exprResult.mType->RemoveModifiers();

		bool isMemoryWatch = (pendingExpr->mExpressionFlags & DwEvalExpressionFlag_MemoryWatch) != 0;

		if (!resultType->IsPointerOrRef())
		{
			if (exprResult.mSrcAddress != 0)
				val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected. Try using the '&' address-of operator.", exprResult.mType->ToString().c_str());
			else
				val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected. An explicit cast may be required.", exprResult.mType->ToString().c_str());
		}
		else
		{
			auto innerType = resultType->mTypeParam;
			int byteCount = innerType->GetByteCount();

			if (pendingExpr->mFormatInfo.mArrayLength != -1)
				byteCount *= pendingExpr->mFormatInfo.mArrayLength;

			if (byteCount == 0)
			{
				val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected, try casting to a non-void pointer type.", exprResult.mType->ToString().c_str());
			}
	#ifdef BF_DBG_32
			else if ((isMemoryWatch) && (!IsMemoryBreakpointSizeValid(exprResult.mPtr, byteCount)))
			{
				if (innerType->mSize > 16)
					val = StrFormat("!Element size is %d bytes. A maximum of 16 bytes can be watched. Try casting to an appropriately-sized pointer or watching an individual member.", byteCount);
				else if (!IsMemoryBreakpointSizeValid(0, byteCount))
					val = StrFormat("!Element size is %d bytes, which is not a supported size for a memory watch. Try casting to an appropriately-sized pointer.", byteCount);
				else
					val = StrFormat("!Element size is %d bytes, which is not a supported size for a memory watch at non-aligned address %@. Try casting to an appropriately-sized pointer.", byteCount, exprResult.mPtr);
			}
	#else
			else if ((isMemoryWatch) && (!IsMemoryBreakpointSizeValid(exprResult.mPtr, byteCount)))
			{
				if (innerType->mSize > 32)
					val = StrFormat("!Element size is %d bytes. A maximum of 32 bytes can be watched. Try casting to an appropriately-sized pointer or watching an individual member.", byteCount);
				else if (!IsMemoryBreakpointSizeValid(0, byteCount))
					val = StrFormat("!Element size is %d bytes, which is not a supported size for a memory watch. Try casting to an appropriately-sized pointer.", byteCount);
				else
					val = StrFormat("!Element size is %d bytes, which is not a supported size for a memory watch at non-aligned address %@. Try casting to an appropriately-sized pointer.", byteCount, exprResult.mPtr);
			}
	#endif
			else
			{
				auto language = dbgExprEvaluator.GetLanguage();
				val = EncodeDataPtr(exprResult.mPtr, false) + "\n" + StrFormat("%d", byteCount) + "\n" + StrFormat("%d\t", language) + innerType->ToStringRaw(language);
			}
		}
	}
	else
	{
		if (pendingExpr->mFormatInfo.mNoEdit)
			exprResult.mIsReadOnly = true;
		if (!pendingExpr->mReferenceId.empty())
			pendingExpr->mFormatInfo.mReferenceId = pendingExpr->mReferenceId;
		val = DbgTypedValueToString(exprResult, pendingExpr->mExprNode->ToString(), pendingExpr->mFormatInfo, &dbgExprEvaluator, (pendingExpr->mExpressionFlags & DwEvalExpressionFlag_FullPrecision) != 0);

		if ((!val.empty()) && (val[0] == '!'))
			return val;

		if (pendingExpr->mFormatInfo.mRawString)
			return val;

		if (exprResult.mIsLiteral)
			val += "\n:literal";

		if (bfPassInstance.HasMessages())
		{
			for (auto error : bfPassInstance.mErrors)
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

		auto breakAddress = exprResult.mSrcAddress;
		int breakSize = exprResult.mType->GetByteCount();
		if (exprResult.mType->IsRef())
			breakSize = exprResult.mType->mTypeParam->GetByteCount();
		if ((breakAddress != 0) && (HasMemoryBreakpoint(breakAddress, breakSize)))
			val += StrFormat("\n:break\t%@", breakAddress);

		auto checkType = exprResult.mType->RemoveModifiers();
		if (checkType->IsBfObjectPtr())
			val += "\n:type\tobject";
		else if ((checkType->IsPointer()) || (checkType->mTypeCode == DbgType_Subroutine))
			val += "\n:type\tpointer";
		else if (checkType->IsInteger())
			val += "\n:type\tint";
		else if (checkType->IsFloat())
			val += "\n:type\tfloat";
		else if ((exprResult.mRegNum >= X64Reg_M128_XMM0) && (exprResult.mRegNum <= X64Reg_M128_XMM15))
			val += "\n:type\tmm128";
		else
			val += "\n:type\tvaluetype";

		if ((pendingExpr->mFormatInfo.mTypeKindFlags & DbgTypeKindFlag_Int) != 0)
			val += "\n:type\tint";

		if (dbgExprEvaluator.mHadSideEffects)
			val += "\n:sideeffects";
		if ((dbgExprEvaluator.mStackSearch != NULL) && (dbgExprEvaluator.mStackSearch->mStartingStackIdx != dbgExprEvaluator.mCallStackIdx))
			val += StrFormat("\n:stackIdx\t%d", dbgExprEvaluator.mCallStackIdx);

		auto underlyingType = exprResult.mType->RemoveModifiers();
		bool canEdit = true;
		if (pendingExpr->mFormatInfo.mLanguage == DbgLanguage_Beef)
		{
			if (exprResult.mType->IsConst())
				canEdit = false;
		}
		if (pendingExpr->mFormatInfo.mNoEdit)
			canEdit = false;
		if (exprResult.mIsReadOnly)
			canEdit = false;

		const char* langStr = (pendingExpr->mFormatInfo.mLanguage == DbgLanguage_Beef) ? "@Beef:" : "@C:";

		if (exprResult.mSrcAddress != 0)
		{
			val += StrFormat("\n:addrValueExpr\t%s(%s*)", langStr, exprResult.mType->ToString(pendingExpr->mFormatInfo.mLanguage).c_str());
			val += EncodeDataPtr(exprResult.mSrcAddress, true);
		}

		if (exprResult.mType->IsPointerOrRef())
		{
			auto underlyingType = exprResult.mType->mTypeParam;
			if (underlyingType != NULL)
			{
				val += StrFormat("\n:pointeeExpr\t%s(%s%s)", langStr, underlyingType->ToString(pendingExpr->mFormatInfo.mLanguage).c_str(),
					underlyingType->IsBfObject() ? "" : "*");
				val += EncodeDataPtr(exprResult.mPtr, true);
			}

			val += "\n:pointer\t" + EncodeDataPtr(exprResult.mPtr, true);
		}

		if (val[0] == '!')
		{
			// Already has an error embedded, can't edit
		}
		else if ((exprResult.mSrcAddress != 0) && (underlyingType->mTypeCode >= DbgType_i8) && (underlyingType->mTypeCode <= DbgType_Ptr) &&
            (underlyingType->mTypeCode != DbgType_Class) && (underlyingType->mTypeCode != DbgType_Struct))
		{
			if (canEdit)
				val += "\n:canEdit";
			if (exprResult.mType->mTypeCode == DbgType_Ptr)
			{
				val += "\n:editVal\t" + EncodeDataPtr(exprResult.mPtr, true);
			}
		}
		else if ((underlyingType->IsStruct()) && (exprResult.mSrcAddress != 0) && (underlyingType->IsTypedPrimitive()))
		{
			auto primType = underlyingType->GetRootBaseType();
			DbgTypedValue primVal = dbgExprEvaluator.ReadTypedValue(NULL, primType, exprResult.mSrcAddress, DbgAddrType_Target);

			String primResult = DbgTypedValueToString(primVal, "", pendingExpr->mFormatInfo, NULL);
			int crPos = (int)primResult.IndexOf('\n');
			if (crPos != -1)
				primResult.RemoveToEnd(crPos);

			if (canEdit)
				val += "\n:canEdit";
			val += "\n:editVal\t" + primResult;
		}
		else if (exprResult.mRegNum >= 0)
		{
			bool isPseudoReg = ( ((exprResult.mRegNum >= X86Reg_M128_XMMREG_FIRST) && (exprResult.mRegNum <= X86Reg_M128_XMMREG_LAST))
				|| ((exprResult.mRegNum >= X86Reg_CAT_FIRST) && (exprResult.mRegNum <= X86Reg_CAT_LAST)) );
			if (!isPseudoReg)
			{
				if (canEdit)
					val += "\n:canEdit";
				if (exprResult.mType->mTypeCode == DbgType_Ptr)
				{
					val += "\n:editVal\t" + EncodeDataPtr(exprResult.mPtr, true);
				}
			}
		}
	}

	if (pendingExpr->mFormatInfo.mRawString)
		return "";

	if (val[0] != '!')
	{
		if (pendingExpr->mUsedSpecifiedLock)
			val += "\n:usedLock";

		if (pendingExpr->mStackIdxOverride != -1)
			val += StrFormat("\n:stackIdx\t%d", pendingExpr->mStackIdxOverride);
	}

	if (pendingExpr->mCursorPos != -1)
		val += GetAutocompleteOutput(autoComplete);

	return val;
}

String DbgTargetHost::EvaluateContinue()
{
	BP_ZONE("WinDebugger::EvaluateContinue");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mDebugPendingExpr == NULL)
		return "!Evaluation canceled";

	if (!IsPaused())
		return "!Not paused";

	if (mRunState == RunState_DebugEval_Done)
		mRunState = RunState_Paused;

	BfPassInstance bfPassInstance(mBfSystem);
	String result = EvaluateContinue(mDebugPendingExpr, bfPassInstance);
	if (result != "!pending")
	{
		FinishPendingEval();
	}

	return result;
}

void DbgTargetHost::EvaluateContinueKeep()
{
	if (mDebugPendingExpr != NULL)
		mDebugPendingExpr->mIdleTicks = 0;
}

#ifdef BF_PLATFORM_WINDOWS
static void PdbTestFile(DbgTargetHost* debugger, const StringImpl& path)
{
	if (!path.EndsWith(".PDB", StringImpl::CompareKind_OrdinalIgnoreCase))
		return;

	OutputDebugStrF("Testing %s\n", path.c_str());
	COFF coffFile(debugger->mDebugTarget);
	uint8 wantGuid[16] = { 0 };
	if (!coffFile.TryLoadPDB(path, wantGuid, -1))
		return;
	if (!coffFile.mIs64Bit)
		return;
	coffFile.ParseTypeData();
	coffFile.ParseSymbolData();
	coffFile.ParseGlobalsData();

	for (int i = 0; i < coffFile.mTypes.mSize; i++)
		coffFile.mTypes[i]->PopulateType();

	for (int i = 0; i < coffFile.mCvModuleInfo.mSize; i++)
		coffFile.ParseCompileUnit(i);
}

static void PdbTest(DbgTargetHost* debugger, const StringImpl& path)
{
	for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Files))
	{
		String filePath = fileEntry.GetFilePath();

		PdbTestFile(debugger, filePath);
	}

	for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Directories))
	{
		String childPath = fileEntry.GetFilePath();
		String dirName;
		dirName = GetFileName(childPath);

		PdbTest(debugger, childPath);
	}
}
#endif

String DbgTargetHost::Evaluate(const StringImpl& expr, DwFormatInfo formatInfo, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	BP_ZONE_F("WinDebugger::Evaluate %s", BP_DYN_STR(expr.c_str()));

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((expressionFlags & DwEvalExpressionFlag_Symbol) != 0)
	{
		DwAutoComplete autoComplete;

		String retVal;

		retVal += GetAutocompleteOutput(autoComplete);
		return retVal;
	}

	UpdateCallStackMethod(callStackIdx);

	BfLogDbgExpr("Evaluate %s in thread %d\n", expr.c_str(), GetEvalThreadId());

	if (language != -1)
		formatInfo.mLanguage = (DbgLanguage)language;

	int activeThreadId = GetEvalThreadId();
	if ((!IsPaused()) && (mRunState != RunState_NotStarted) && (mRunState != RunState_DebugEval))
	{
		activeThreadId = 0;
		callStackIdx = -1;
	}

	if (mDebugPendingExpr != NULL)
	{
		// We already have a pending call
		expressionFlags = (DwEvalExpressionFlags)(expressionFlags & ~DwEvalExpressionFlag_AllowCalls);
	}

	if ((expressionFlags & DwEvalExpressionFlag_RawStr) != 0)
	{
		formatInfo.mRawString = true;
	}

	if ((expressionFlags & DwEvalExpressionFlag_AllowStringView) != 0)
	{
		formatInfo.mAllowStringView = true;
	}

	auto terminatedExpr = expr + ";";

	int prevActiveThreadId = GetEvalThreadId();
	bool restoreActiveThread = false;
	defer(
		{
			if (restoreActiveThread)
				SetActiveThread(prevActiveThreadId);
		});

	bool usedSpecifiedLock = false;
	int stackIdxOverride = -1;

	if (terminatedExpr.StartsWith('{'))
	{
		String locString;
		int closeIdx = terminatedExpr.IndexOf('}');
		if (closeIdx != -1)
			locString = terminatedExpr.Substring(1, closeIdx - 1);

		for (int i = 0; i <= closeIdx; i++)
			terminatedExpr[i] = ' ';

		locString.Trim();
		if (locString.StartsWith("Thread:", StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			bool foundLockMatch = true;

			locString.Remove(0, 7);
			char* endPtr = NULL;
			int64 threadId = (int64)strtoll(locString.c_str(), &endPtr, 10);

			if (endPtr != NULL)
			{
				locString.Remove(0, endPtr - locString.c_str());
				locString.Trim();

				if (locString.StartsWith("SP:", StringImpl::CompareKind_OrdinalIgnoreCase))
				{
					locString.Remove(0, 3);
					char* endPtr = NULL;
					uint64 sp = (uint64)strtoll(locString.c_str(), &endPtr, 16);

					if (endPtr != NULL)
					{
						locString.Remove(0, endPtr - locString.c_str());
						locString.Trim();

						if (locString.StartsWith("Func:", StringImpl::CompareKind_OrdinalIgnoreCase))
						{
							locString.Remove(0, 5);
							char* endPtr = NULL;
							int64 funcAddr = (int64)strtoll(locString.c_str(), &endPtr, 16);

							if (endPtr != NULL)
							{
								// Actually do it

								if ((GetEvalThreadId() != 0) && (GetEvalThreadId() != threadId))
									restoreActiveThread = true;
								if ((GetEvalThreadId() == 0) || (GetEvalThreadId() != threadId))
									SetActiveThread(threadId);
								if ((GetEvalThreadId() != 0) && (GetEvalThreadId() == threadId))
								{
									int foundStackIdx = -1;

									int checkStackIdx = 0;
									while (true)
									{
										if (checkStackIdx >= mCallStack.mSize)
											UpdateCallStack();
										if (checkStackIdx >= mCallStack.mSize)
											break;

										auto stackFrame = mCallStack[checkStackIdx];
										if (stackFrame->mRegisters.GetSP() == sp)
										{
											foundStackIdx = checkStackIdx;
											break;
										}

										if (stackFrame->mRegisters.GetSP() > sp)
										{
											foundStackIdx = checkStackIdx - 1;
											break;
										}

										checkStackIdx++;
									}

									if (foundStackIdx != -1)
									{
										UpdateCallStackMethod(foundStackIdx);
										auto stackFrame = mCallStack[foundStackIdx];

										if ((stackFrame->mSubProgram != NULL) && ((int64)stackFrame->mSubProgram->mBlock.mLowPC == funcAddr))
										{
											if ((callStackIdx != foundStackIdx) || (GetEvalThreadId() != prevActiveThreadId))
												usedSpecifiedLock = true;
											callStackIdx = foundStackIdx;
											foundLockMatch = true;
										}
									}
								}
							}
						}
					}
				}
			}

			if (!foundLockMatch)
				return "!Locked stack frame not found";

			bool doClear = false;
			for (int i = closeIdx; i < terminatedExpr.mLength; i++)
			{
				char c = terminatedExpr[i];
				if (doClear)
				{
					terminatedExpr[i] = ' ';
					if (c == '}')
						break;
				}
				else
				{
					if (c == '{')
					{
						int endIdx = terminatedExpr.IndexOf('}');
						if (endIdx == -1)
							break;
						terminatedExpr[i] = ' ';
						doClear = true;
					}
					else if (!::isspace((uint8)c))
						break;
				}
			}
		}
		else if (!locString.IsEmpty())
		{
			const char* checkPtr = locString.c_str();
			if ((*checkPtr == '^') || (*checkPtr == '@'))
				checkPtr++;

			char* endPtr = NULL;
			int useCallStackIdx = strtol(checkPtr, &endPtr, 10);
			if (endPtr == locString.c_str() + locString.length())
			{
				if (locString[0] == '@')
					callStackIdx = useCallStackIdx;
				else
					callStackIdx += useCallStackIdx;
				stackIdxOverride = callStackIdx;
			}
			else
			{
				formatInfo.mStackSearchStr = locString;
			}
		}
	}

	auto dbgModule = GetCallStackDbgModule(callStackIdx);
	auto dbgSubprogram = GetCallStackSubprogram(callStackIdx);
	DbgCompileUnit* dbgCompileUnit = NULL;
	if (dbgSubprogram != NULL)
		dbgCompileUnit = dbgSubprogram->mCompileUnit;

	if ((expr.length() > 0) && (expr[0] == '!'))
	{
		if (expr.StartsWith("!step "))
		{
			expressionFlags = (DwEvalExpressionFlags)(expressionFlags | DwEvalExpressionFlag_StepIntoCalls);
			for (int i = 0; i < 5; i++)
				terminatedExpr[i] = ' ';
		}
		else
		{
			String cmd = expr;
			int commaPos = (int)cmd.IndexOf(',');
			if (commaPos != -1)
				cmd.RemoveToEnd(commaPos);

			if (cmd == "!info")
			{
				OutputMessage(StrFormat("Module: %s\n", dbgModule->mDisplayName.c_str()));
				if (dbgSubprogram == NULL)
				{
					//
				}
				else if (dbgSubprogram->mLinkName != NULL)
				{
					OutputMessage(StrFormat("Link Name: %s\n", dbgSubprogram->mLinkName));
				}
				else
				{
					String outSymbol;
					if (mDebugTarget->FindSymbolAt(dbgSubprogram->mBlock.mLowPC, &outSymbol))
					{
						OutputMessage(StrFormat("Link Name: %s\n", outSymbol.c_str()));
					}
				}
				return "";
			}
			else if (cmd == "!dbg")
			{
				mDbgBreak = true;
				return "";
			}
#ifdef BF_PLATFORM_WINDOWS
			else if (cmd == "!pdbtest")
			{
				PdbTest(this, "c:\\");
			}
			else if (cmd.StartsWith("!pdbtest "))
				PdbTestFile(this, cmd.Substring(9));
#endif
		}
	}

	bool valIsAddr = false;

	BfParser* parser = new BfParser(mBfSystem);
	parser->mCompatMode = true;

	BfPassInstance bfPassInstance(mBfSystem);

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
		else // Look for "@:" or "@Beef:" style
		{
			int colonIdx = terminatedExpr.IndexOf(':');
			if (colonIdx > 0)
			{
				bool isValid = true;
				DbgLanguage language = DbgLanguage_Unknown;
				String lang = terminatedExpr.Substring(1, colonIdx - 1);
				lang = ToUpper(lang);
				if ((lang == "") || (lang == "BEEF"))
				{
					language = DbgLanguage_Beef;
				}
				else if (lang == "C")
				{
					language = DbgLanguage_C;
				}
				if (language != DbgLanguage_Unknown)
				{
					for (int i = 0; i < colonIdx + 1; i++)
						terminatedExpr[i] = ' ';

					DbgLanguage curLanguage = DbgLanguage_Unknown;
					if (dbgSubprogram != NULL)
						curLanguage = dbgSubprogram->GetLanguage();
					if (language != curLanguage)
					{
						dbgModule = mDebugTarget->mTargetBinary;
						dbgSubprogram = NULL;
						formatInfo.mLanguage = language;
						callStackIdx = -1;
					}
				}
			}
		}
	}

	parser->SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	parser->Parse(&bfPassInstance);

	BfReducer bfReducer;
	bfReducer.mAlloc = parser->mAlloc;
	bfReducer.mSystem = mBfSystem;
	bfReducer.mPassInstance = &bfPassInstance;
	bfReducer.mVisitorPos = BfReducer::BfVisitorPos(parser->mRootNode);
	bfReducer.mVisitorPos.MoveNext();
	bfReducer.mCompatMode = parser->mCompatMode;
	bfReducer.mSource = parser;
	auto exprNode = bfReducer.CreateExpression(parser->mRootNode->mChildArr.GetAs<BfAstNode*>(0));
	parser->Close();

	formatInfo.mCallStackIdx = callStackIdx;
	if ((formatInfo.mLanguage == DbgLanguage_Unknown) && (dbgSubprogram != NULL))
		formatInfo.mLanguage = dbgSubprogram->GetLanguage();

	DbgPendingExpr* pendingExpr = new DbgPendingExpr();
	if (activeThreadId != 0)
		pendingExpr->mThreadId = activeThreadId;
	pendingExpr->mParser = parser;
	pendingExpr->mCallStackIdx = callStackIdx;
	pendingExpr->mCursorPos = cursorPos;
	pendingExpr->mExpressionFlags = expressionFlags;
	pendingExpr->mExprNode = exprNode;

	DbgType* explicitType = NULL;
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
			explicitType = dbgModule->FindType(expr);
		}

		if ((explicitType == NULL) && (formatFlags.length() > 0))
		{
			String errorString = "Invalid expression";
			if (!ParseFormatInfo(dbgModule, formatFlags, &formatInfo, &bfPassInstance, &assignExprOffset, &assignExpr, &errorString))
			{
				if (formatInfo.mRawString)
					return "";
				bfPassInstance.FailAt(errorString, parser->mSourceData, exprNode->GetSrcEnd(), (int) expr.length() - exprNode->GetSrcEnd());
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

	pendingExpr->mUsedSpecifiedLock = usedSpecifiedLock;
	pendingExpr->mStackIdxOverride = stackIdxOverride;
	pendingExpr->mExplitType = explicitType;
	pendingExpr->mFormatInfo = formatInfo;
	String result = EvaluateContinue(pendingExpr, bfPassInstance);
	if (result == "!pending")
	{
		BF_ASSERT(mDebugPendingExpr == NULL);
		if (mDebugPendingExpr != NULL)
		{
			return "!retry"; // We already have a pending
		}
		mDebugPendingExpr = pendingExpr;
		OnPendingEvalStarted();
	}
	else
		delete pendingExpr;

	return result;
}

String DbgTargetHost::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	DwFormatInfo formatInfo;
	return Evaluate(expr, formatInfo, callStackIdx, cursorPos, language, expressionFlags);
}

static void ConvertDoubleToFloat80(double d, uint8 fp80[10])
{
	uint64 di = *reinterpret_cast<uint64*>(&d);

	uint64 m = di & (((uint64)1 << 52) - 1);
	uint64 e = (di >> 52) & 0x7ff;

	memset(fp80, 0, 10);

	// sign bit is directly transferred
	if (di & ((uint64)1 << 63))
		fp80[9] |= 0x80;

	if (!e && !m)
		return; // zero

	fp80[7] |= 0x80; // leading integer bit in mantissa (always 1 in normalized numbers)

	if (e == 0x7ff)
	{
		fp80[9] |= 0x7f;
		fp80[8] = 0xff;

		if (m == 0)
			return; // inf

		fp80[7] |= 0x3f; // any nonzero value will be a NaN (SNaN or QNaN)
		if (m & ((uint64)1 << 51))
			fp80[7] |= 0x40; // QNaN

		return;
	}

	int useExponent = (int)e - 1023;

	if (!e)
	{
		// denormal; can renormalize though since fp80 supports lower exponents
		BF_ASSERT(m != 0); // we should have trapped zero above
		while (!(m & ((uint64)1 << 51)))
		{
			m <<= 1;
			--useExponent;
		}
		// finally we have our leading 1 bit; strip that off and we have a normalized number again
		m <<= 1;
		--useExponent;

		m &= (((uint64)1 << 52) - 1);
	}

	useExponent += 16383;
	BF_ASSERT((useExponent > 0) && (useExponent < 0x7fff));
	*reinterpret_cast<uint16*>(&fp80[8]) |= (uint16)useExponent;
	*reinterpret_cast<uint64*>(&fp80[0]) |= (m << 11);
}

bool DbgTargetHost::AssignToReg(int callStackIdx, DbgTypedValue regVal, DbgTypedValue value, String& outError)
{
	BF_ASSERT(regVal.mRegNum >= 0);

	if (mCallStack.size() == 0)
	{
		outError = "No call stack";
		return false;
	}

	if (callStackIdx >= (int)mCallStack.size())
	{
		outError = "Invalid call stack index";
		return false;
	}

	auto registers = &mCallStack[callStackIdx]->mRegisters;
	void* regPtr = NULL;

#ifdef BF_DBG_32
	if ((regVal.mRegNum >= X86Reg_INTREG_FIRST) && (regVal.mRegNum <= X86Reg_INTREG_LAST))
	{
		BF_ASSERT(regVal.mType->mSize == sizeof(int32));
		registers->mIntRegsArray[regVal.mRegNum - X86Reg_INTREG_FIRST] = (uint64)value.mUInt32; // don't sign-extend
	}
	else if ((regVal.mRegNum >= X86Reg_FPSTREG_FIRST) && (regVal.mRegNum <= X86Reg_FPSTREG_LAST))
	{
		BF_ASSERT(regVal.mType->mSize == sizeof(float) || regVal.mType->mSize == sizeof(double));
		CPURegisters::FpMmReg* reg = &registers->mFpMmRegsArray[regVal.mRegNum - X86Reg_FPSTREG_FIRST];
		double d;
		if (regVal.mType->mSize == sizeof(float))
			d = (double)value.mSingle;
		else
			d = value.mDouble;
		ConvertDoubleToFloat80(d, reg->fp.fp80);
	}
	else if ((regVal.mRegNum >= X86Reg_MMREG_FIRST) && (regVal.mRegNum <= X86Reg_MMREG_LAST))
	{
		BF_ASSERT(regVal.mType->mSize == sizeof(int32) || regVal.mType->mSize == sizeof(int64));
		CPURegisters::FpMmReg* reg = &registers->mFpMmRegsArray[regVal.mRegNum - X86Reg_MMREG_FIRST];
		if (regVal.mType->mSize == sizeof(int32))
			reg->mm = (uint64)value.mUInt32; // don't sign-extend
		else if (regVal.mType->mSize == sizeof(int64))
			reg->mm = value.mInt64;
		// whenever we use the low 64 bits of the reg as mm, the upper 16 bits of the 80-bit float must be set to all-1s to indicate NaN
		reg->fp.fp80[8] = reg->fp.fp80[9] = 0xFF;
	}
	else if ((regVal.mRegNum >= X86Reg_XMMREG_FIRST) && (regVal.mRegNum <= X86Reg_XMMREG_LAST))
	{
		int xmmMajor = (regVal.mRegNum - X86Reg_XMMREG_FIRST) >> 2;
		int xmmMinor = (regVal.mRegNum - X86Reg_XMMREG_FIRST) & 3;

		registers->mXmmRegsArray[xmmMajor].f[xmmMinor] = value.mSingle;
	}
	else if ((regVal.mRegNum >= X86Reg_M128_XMMREG_FIRST) && (regVal.mRegNum <= X86Reg_M128_XMMREG_LAST))
	{
		outError = "Cannot write directly to 128-bit XMM register, please use inner float components";
		return false;
	}
	else if ((regVal.mRegNum >= X86Reg_FLAG_FIRST) && (regVal.mRegNum <= X86Reg_FLAG_LAST))
	{
		int flagBit = CPURegisters::GetFlagBitForRegister(regVal.mRegNum);
		if (flagBit >= 0)
		{
			if (value.mBool)
				registers->mIntRegs.efl |= ((uint64)1 << flagBit);
			else
				registers->mIntRegs.efl &= ~((uint64)1 << flagBit);
		}
		else
		{
			outError = "Unrecognized flag";
			return false;
		}
	}
	else if ((regVal.mRegNum >= X86Reg_CAT_FIRST) && (regVal.mRegNum <= X86Reg_CAT_LAST))
	{
		outError = "Cannot write directly to register categories, please use inner float components";
		return false;
	}
#else

	if ((regVal.mRegNum >= X64Reg_INTREG_FIRST) && (regVal.mRegNum <= X64Reg_INTREG_LAST))
	{
		//BF_ASSERT(regVal.mType->mSize == sizeof(addr_target));
		registers->mIntRegsArray[regVal.mRegNum - X64Reg_INTREG_FIRST] = value.GetInt64(); // don't sign-extend
		regPtr = &registers->mIntRegsArray[regVal.mRegNum - X64Reg_INTREG_FIRST];
	}
	else if ((regVal.mRegNum >= X64Reg_FPSTREG_FIRST) && (regVal.mRegNum <= X64Reg_FPSTREG_LAST))
	{
		BF_ASSERT(regVal.mType->mSize == sizeof(float) || regVal.mType->mSize == sizeof(double));
		CPURegisters::FpMmReg* reg = &registers->mFpMmRegsArray[regVal.mRegNum - X64Reg_FPSTREG_FIRST];
		double d;
		if (regVal.mType->mSize == sizeof(float))
			d = (double)value.mSingle;
		else
			d = value.mDouble;
		ConvertDoubleToFloat80(d, reg->fp.fp80);
		regPtr = reg;
	}
	else if ((regVal.mRegNum >= X64Reg_MMREG_FIRST) && (regVal.mRegNum <= X64Reg_MMREG_LAST))
	{
		BF_ASSERT(regVal.mType->mSize == sizeof(int32) || regVal.mType->mSize == sizeof(int64));
		CPURegisters::FpMmReg* reg = &registers->mFpMmRegsArray[regVal.mRegNum - X64Reg_MMREG_FIRST];
		if (regVal.mType->mSize == sizeof(int32))
			reg->mm = (uint64)value.mUInt32; // don't sign-extend
		else if (regVal.mType->mSize == sizeof(int64))
			reg->mm = value.mInt64;
		// whenever we use the low 64 bits of the reg as mm, the upper 16 bits of the 80-bit float must be set to all-1s to indicate NaN
		reg->fp.fp80[8] = reg->fp.fp80[9] = 0xFF;
		regPtr = reg;
	}
	else if ((regVal.mRegNum >= X64Reg_XMMREG_FIRST) && (regVal.mRegNum <= X64Reg_XMMREG_LAST))
	{
		int xmmMajor = (regVal.mRegNum - X64Reg_XMMREG_FIRST) >> 2;
		int xmmMinor = (regVal.mRegNum - X64Reg_XMMREG_FIRST) & 3;

		if (value.mType->GetByteCount() == 4)
			registers->mXmmRegsArray[xmmMajor].f[xmmMinor] = value.mSingle;
		else if (value.mType->GetByteCount() == 8)
			registers->mXmmDRegsArray[xmmMajor].d[xmmMinor] = value.mDouble;
		else
			BF_FATAL("Invalid XMM set value type");
		regPtr = &registers->mXmmRegsArray[xmmMajor];
	}
	else if ((regVal.mRegNum >= X64Reg_M128_XMMREG_FIRST) && (regVal.mRegNum <= X64Reg_M128_XMMREG_LAST))
	{
		outError = "Cannot write directly to 128-bit XMM register, please use inner float components";
		return false;
	}
	else if ((regVal.mRegNum >= X64Reg_FLAG_FIRST) && (regVal.mRegNum <= X64Reg_FLAG_LAST))
	{
		int flagBit = CPURegisters::GetFlagBitForRegister(regVal.mRegNum);
		if (flagBit >= 0)
		{
			if (value.mBool)
				registers->mIntRegs.efl |= ((uint64)1 << flagBit);
			else
				registers->mIntRegs.efl &= ~((uint64)1 << flagBit);
			regPtr = &registers->mIntRegs.efl;
		}
		else
		{
			outError = "Unrecognized flag";
			return false;
		}
	}
	else if ((regVal.mRegNum >= X64Reg_CAT_FIRST) && (regVal.mRegNum <= X64Reg_CAT_LAST))
	{
		outError = "Cannot write directly to register categories, please use inner float components";
		return false;
	}
	else
		BF_FATAL("Not implemented");
#endif

	if (callStackIdx == 0)
	{
		SetRegisters(&mCallStack[0]->mRegisters);
		return true;
	}
	else
	{
		bool wasSaved = false;
		for (int calleeStackIdx = callStackIdx - 1; calleeStackIdx >= 0; calleeStackIdx--)
		{
			auto calleeRegisters = &mCallStack[calleeStackIdx]->mRegisters;
			if (!mDebugTarget->PropogateRegisterUpCallStack(registers, calleeRegisters, regPtr, wasSaved))
			{
				outError = "Failed to set register";
				return false;
			}
			if (wasSaved)
				return true;
		}

		// This register wasn't saved, so commit it to the callstack top
		return AssignToReg(0, regVal, value, outError);
	}
}

String DbgTargetHost::GetAutocompleteOutput(DwAutoComplete& autoComplete)
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

String DbgTargetHost::EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (IsInRunState())
		return "!Target not paused";

	auto dbgModule = GetCallStackDbgModule(callStackIdx);
	auto dbgCompileUnit = GetCallStackCompileUnit(callStackIdx);

	BfParser parser(mBfSystem);
	parser.mCompatMode = true;

	BfPassInstance bfPassInstance(mBfSystem);
	auto terminatedExpr = expr + ";";
	parser.SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	parser.Parse(&bfPassInstance);

	BfReducer bfReducer;
	bfReducer.mAlloc = parser.mAlloc;
	bfReducer.mSystem = mBfSystem;
	bfReducer.mPassInstance = &bfPassInstance;
	bfReducer.mVisitorPos = BfReducer::BfVisitorPos(parser.mRootNode);
	bfReducer.mVisitorPos.MoveNext();
	bfReducer.mSource = &parser;
	auto exprNode = bfReducer.CreateExpression(parser.mRootNode->GetFirst());
	parser.Close();

	DwAutoComplete autoComplete;
	DbgExprEvaluator dbgExprEvaluator(this, dbgModule, &bfPassInstance, callStackIdx, cursorPos);
	if (cursorPos != -1)
		dbgExprEvaluator.mAutoComplete = &autoComplete;
	dbgExprEvaluator.mDbgCompileUnit = dbgCompileUnit;

	DwFormatInfo formatInfo;
	formatInfo.mCallStackIdx = callStackIdx;

	DbgTypedValue exprResult;
	if (exprNode != NULL)
		exprResult = dbgExprEvaluator.Resolve(exprNode);

	DbgType* resultType = exprResult.mType->RemoveModifiers();

	String val;
	if (bfPassInstance.HasFailed())
	{
		val = StrFormat("!%d\t%d\t%s", bfPassInstance.mErrors[0]->mSrcStart, bfPassInstance.mErrors[0]->GetSrcLength(), bfPassInstance.mErrors[0]->mError.c_str());
	}
	else if (exprResult.mType == NULL)
	{
		val = "!Invalid expression";
	}
	else if (!resultType->IsPointerOrRef())
	{
		if (exprResult.mSrcAddress != 0)
			val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected. Try using the '&' address-of operator.", exprResult.mType->ToString().c_str());
		else
			val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected. An explicit cast may be required.", exprResult.mType->ToString().c_str());
	}
	else
	{
		auto innerType = resultType->mTypeParam;
		int byteCount = innerType->GetByteCount();
		if (byteCount == 0)
		{
			val = StrFormat("!Type '%s' is invalid. A sized pointer type is expected, try casting to a non-void pointer type.", exprResult.mType->ToString().c_str());
		}
#ifdef BF_DBG_32
		else if ((byteCount != 1) && (byteCount != 2) && (byteCount != 4))
		{
			val = StrFormat("!Element size is %d bytes. Only 1, 2, or 4 byte elements can be tracked. Try casting to an appropriately-sized pointer.", innerType->mSize);
		}
#else
		else if ((byteCount != 1) && (byteCount != 2) && (byteCount != 4) && (byteCount != 8))
		{
			val = StrFormat("!Element size is %d bytes. Only 1, 2, 4, or 8 byte elements can be tracked. Try casting to an appropriately-sized pointer.", innerType->mSize);
		}
#endif
		else
		{
			val = EncodeDataPtr(exprResult.mPtr, false) + "\n" + StrFormat("%d", byteCount);
		}
	}

	if (cursorPos != -1)
		val += GetAutocompleteOutput(autoComplete);

	return val;
}

// This is currently only used for autocomplete during conditional breakpoint expression entry.
//  If we want to use it for more than that then remove DwEvalExpressionFlags_ValidateOnly
String DbgTargetHost::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (IsInRunState())
		return "!Target not paused";

	if (!IsPaused())
		return "!Target not running";

	WdStackFrame stackFrame;
	memset(&stackFrame.mRegisters, 0, sizeof(stackFrame.mRegisters));
	stackFrame.mHasGottenSubProgram = true;
	*stackFrame.mRegisters.GetPCRegisterRef() = (intptr_target)atAddr;
	stackFrame.mSubProgram = mDebugTarget->FindSubProgram((addr_target)atAddr);
	if (stackFrame.mSubProgram == NULL)
		return "!Invalid address";
	mCallStack.push_back(&stackFrame);
	int callStackIdx = (int)mCallStack.size() - 1;
	String val = Evaluate(expr, callStackIdx, cursorPos, -1, DwEvalExpressionFlag_ValidateOnly);
	mCallStack.pop_back();

	return val;
}

String DbgTargetHost::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	BP_ZONE("WinDebugger::GetAutoExpressions");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (IsInRunState())
		return "!Not paused";

	if (!IsPaused())
		return "!Not running";

	if (!FixCallStackIdx(callStackIdx))
		return "";

	CPUStackFrame* stackFrame = (callStackIdx >= 0) ? mCallStack[callStackIdx] : mCallStack.front();

	String result;

	DbgAutoValueMapType dwarfAutos;
	mDebugTarget->GetAutoValueNames(dwarfAutos, stackFrame, memoryRangeStart, memoryRangeLen);

	for (auto const &a : dwarfAutos)
	{
		std::pair<uint64, uint64> varRange = a.mValue;
		if (varRange.first != 0)
			result += StrFormat("&%s\t%llu\t%llu\n", a.mKey.c_str(), varRange.second, varRange.first);
		else
			result += StrFormat("?%s\t%llu\n", a.mKey.c_str(), varRange.second);
	}

#ifdef BF_DBG_64
	// add int regs
	const char* regStrs[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "rip", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", 0 };
#else
	// add int regs
	const char* regStrs[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "eip", 0 };
#endif
	for (const char** p = regStrs; *p; ++p)
		result += StrFormat("$%s\t%d\n", *p, sizeof(addr_target));

	if (callStackIdx < (int)mCallStack.size() - 2)
	{
		WdStackFrame* prevStackFrame = mCallStack[callStackIdx + 1];
		// Inlined methods have no stack frame
		int stackSize = prevStackFrame->mRegisters.GetSP() - stackFrame->mRegisters.GetSP();
		result += StrFormat("&$StackFrame\t%llu\t%llu\n", stackSize, stackFrame->mRegisters.GetSP());
	}

	return result;
}

String DbgTargetHost::CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx)
{
	DbgCompileUnit* compileUnit = GetCallStackCompileUnit(callStackIdx);
	DbgModule* dbgModule = GetCallStackDbgModule(callStackIdx);
	if (dbgModule == NULL)
		return "!failed";

	DbgLanguage language = DbgLanguage_Unknown;
	if (compileUnit != NULL)
		language = compileUnit->mLanguage;

	auto terminatedParentExpr = parentExpr + ";";

	String parentPrefix;

	int colonIdx = terminatedParentExpr.IndexOf(':');
	if (colonIdx > 0)
	{
		bool isValid = true;
		String lang = terminatedParentExpr.Substring(1, colonIdx - 1);
		lang = ToUpper(lang);
		if ((lang == "") || (lang == "BEEF"))
		{
			language = DbgLanguage_Beef;
		}
		else if (lang == "C")
		{
			language = DbgLanguage_C;
		}
		if (language != DbgLanguage_Unknown)
		{
			parentPrefix += terminatedParentExpr.Substring(0, colonIdx + 1);
			terminatedParentExpr.Remove(0, colonIdx + 1);
		}
	}

	if (terminatedParentExpr.StartsWith('{'))
	{
		int prefixEnd = terminatedParentExpr.IndexOf('}');
		parentPrefix += terminatedParentExpr.Substring(0, prefixEnd + 1);
		terminatedParentExpr.Remove(0, prefixEnd + 1);
	}

	BfPassInstance bfPassInstance(mBfSystem);

	BfParser parser(mBfSystem);
	parser.mCompatMode = language != DbgLanguage_Beef;
	auto terminatedExpr = expr + ";";
	parser.SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	parser.Parse(&bfPassInstance);

	BfParser parentParser(mBfSystem);
	parentParser.mCompatMode = language != DbgLanguage_Beef;
	parentParser.SetSource(terminatedParentExpr.c_str(), terminatedParentExpr.length());
	parentParser.Parse(&bfPassInstance);

	BfReducer bfReducer;
	bfReducer.mCompatMode = true;
	bfReducer.mAlloc = parser.mAlloc;
	bfReducer.mSystem = mBfSystem;
	bfReducer.mPassInstance = &bfPassInstance;
	bfReducer.mVisitorPos = BfReducer::BfVisitorPos(parser.mRootNode);
	bfReducer.mVisitorPos.MoveNext();
	bfReducer.mSource = &parser;
	auto exprNode = bfReducer.CreateExpression(parser.mRootNode->GetFirst());
	bfReducer.mAlloc = parentParser.mAlloc;
	bfReducer.mVisitorPos = BfReducer::BfVisitorPos(parentParser.mRootNode);
	bfReducer.mVisitorPos.MoveNext();
	auto parentExprNode = bfReducer.CreateExpression(parentParser.mRootNode->GetFirst());
	parser.Close();

	if ((exprNode == NULL) || (parentExprNode == NULL))
		return "!failed";

	DbgExprEvaluator dbgExprEvaluator(this, dbgModule, &bfPassInstance, callStackIdx, -1);

	DwFormatInfo formatInfo;
	formatInfo.mCallStackIdx = callStackIdx;
	formatInfo.mLanguage = language;

	String formatFlags;
	String assignExpr;
	if ((exprNode != NULL) && (exprNode->GetSrcEnd() < (int) expr.length()))
	{
		formatFlags = Trim(expr.Substring(exprNode->GetSrcEnd()));
		if (formatFlags.length() > 0)
		{
			String errorString = "Invalid expression";
			if (!ParseFormatInfo(dbgModule, formatFlags, &formatInfo, &bfPassInstance, NULL, &assignExpr, &errorString))
			{
				bfPassInstance.FailAt(errorString, parser.mSourceData, exprNode->GetSrcEnd(), (int) expr.length() - exprNode->GetSrcEnd());
				formatFlags = "";
			}
		}
	}
	dbgExprEvaluator.mExplicitThis = formatInfo.mExplicitThis;
	dbgExprEvaluator.mExplicitThisExpr = parentExprNode;

	DbgTypedValue exprResult = dbgExprEvaluator.Resolve(exprNode);
	BfAstNode* headNode = dbgExprEvaluator.FinalizeExplicitThisReferences(exprNode);

	BfPrinter printer(parser.mRootNode, NULL, NULL);
	printer.mIgnoreTrivia = true;
	printer.mReformatting = true;
	printer.VisitChild(headNode);
	String result;
	result += parentPrefix;
	result += printer.mOutString;
	if (formatInfo.mNoVisualizers)
		result += ", nv";
	if (formatInfo.mNoMembers)
		result += ", nm";
	if (formatInfo.mNoEdit)
		result += ", ne";
	if (formatInfo.mIgnoreDerivedClassInfo)
		result += ", nd";
	if (formatInfo.mDisplayType == DwDisplayType_Ascii)
		result += ", s";
	if (formatInfo.mDisplayType == DwDisplayType_Utf8)
		result += ", s8";
	if (formatInfo.mDisplayType == DwDisplayType_Utf16)
		result += ", s16";
	if (formatInfo.mDisplayType == DwDisplayType_Utf32)
		result += ", s32";
	return result;
}
