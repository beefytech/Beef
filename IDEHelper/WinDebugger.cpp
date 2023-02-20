#pragma warning(disable:4996)
// TODO: Remove for 64-bit
#pragma warning(disable:4244)
#pragma warning(disable:4267)

#define NTDDI_VERSION 0x06020000

#include "WinDebugger.h"
#include "CPU.h"
#include "DbgModule.h"
#include "DebugVisualizers.h"
#include "MiniDumpDebugger.h"
#include "X86.h"
#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/UTF8.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/FileHandleStream.h"
#include "BeefySysLib/util/FileEnumerator.h"
#include <inttypes.h>
#include <windows.h>
#include "DbgExprEvaluator.h"
#include "Compiler/BfSystem.h"
#include "Compiler/BfParser.h"
#include "Compiler/BfReducer.h"
#include "Compiler/BfDemangler.h"
#include "Compiler/BfPrinter.h"
#include <Shlobj.h>
#include "NetManager.h"
#include "DebugManager.h"
#include "X86Target.h"
#include "HotHeap.h"
#include "HotScanner.h"
#include "Profiler.h"
#include <float.h>

#include <psapi.h>

#if !defined BF32 || !defined BF_DBG_64

#define STATUS_WX86_CONTINUE             0x4000001DL
#define STATUS_WX86_SINGLE_STEP          0x4000001EL
#define STATUS_WX86_BREAKPOINT           0x4000001FL
#define STATUS_WX86_EXCEPTION_CONTINUE   0x40000020L

#pragma pack(push, 1)
struct HotJumpOp
{
	uint8 mOpCode;
	int32 mRelTarget;
};
#pragma pack(pop)

#include "BeefySysLib/util/AllocDebug.h"

#include <limits>

USING_NS_BF_DBG;

static void FilterThreadName(String& name)
{
	for (int i = 0; i < (int)name.length(); i++)
	{
		uint8 c = name[i];
		if (c == 0)
		{
			name.RemoveToEnd(i);
			return;
		}

		if (c < 32)
		{
			name.Remove(i);
			i--;
			continue;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

WdBreakpointCondition::~WdBreakpointCondition()
{
	delete mDbgEvaluationContext;
}

//////////////////////////////////////////////////////////////////////////

DbgEvaluationContext::DbgEvaluationContext(WinDebugger* winDebugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	Init(winDebugger, dbgModule, expr, formatInfo, contextValue);
}

DbgEvaluationContext::DbgEvaluationContext(WinDebugger* winDebugger, DbgCompileUnit* dbgCompileUnit, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	DbgModule* dbgModule = NULL;
	if (dbgCompileUnit != NULL)
		dbgModule = dbgCompileUnit->mDbgModule;
	Init(winDebugger, dbgModule, expr, formatInfo, contextValue);
}

void DbgEvaluationContext::Init(WinDebugger* winDebugger, DbgModule* dbgModule, const StringImpl& expr, DwFormatInfo* formatInfo, DbgTypedValue contextValue)
{
	if (expr.empty())
	{
		mParser = NULL;
		mReducer = NULL;
		mPassInstance = NULL;
		mDbgExprEvaluator = NULL;
		mExprNode = NULL;
		return;
	}

	mParser = new BfParser(winDebugger->mBfSystem);
	mParser->mCompatMode = true;
	mPassInstance = new BfPassInstance(winDebugger->mBfSystem);
	auto terminatedExpr = expr + ";";
	mParser->SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	mParser->Parse(mPassInstance);

	mReducer = new BfReducer();
	mReducer->mAlloc = mParser->mAlloc;
	mReducer->mSystem = winDebugger->mBfSystem;
	mReducer->mPassInstance = mPassInstance;
	mReducer->mVisitorPos = BfReducer::BfVisitorPos(mParser->mRootNode);
	mReducer->mVisitorPos.MoveNext();
	mReducer->mCompatMode = mParser->mCompatMode;
	mReducer->mSource = mParser;
	mExprNode = mReducer->CreateExpression(mParser->mRootNode->GetFirst());
	mParser->Close();
	mDbgExprEvaluator = new DbgExprEvaluator(winDebugger, dbgModule, mPassInstance, -1, -1);

	if ((formatInfo != NULL) && (mExprNode != NULL) && (mExprNode->GetSrcEnd() < (int) expr.length()))
	{
		String formatFlags = expr.Substring(mExprNode->GetSrcEnd());
		String errorString = "Invalid expression";
		if (!winDebugger->ParseFormatInfo(dbgModule, formatFlags, formatInfo, mPassInstance, NULL, NULL, &errorString, contextValue))
		{
			mPassInstance->FailAt(errorString, mParser->mSourceData, mExprNode->GetSrcEnd(), (int)expr.length() - mExprNode->GetSrcEnd());
			formatFlags = "";
		}
	}

	if (formatInfo != NULL)
	{
		mDbgExprEvaluator->mExplicitThis = formatInfo->mExplicitThis;
		mDbgExprEvaluator->mCallStackIdx = formatInfo->mCallStackIdx;
		mDbgExprEvaluator->mLanguage = formatInfo->mLanguage;
	}
}

bool DbgEvaluationContext::HasExpression()
{
	return mExprNode != NULL;
}

DbgEvaluationContext::~DbgEvaluationContext()
{
	delete mParser;
	delete mReducer;
	delete mDbgExprEvaluator;
	delete mPassInstance;
}

DbgTypedValue DbgEvaluationContext::EvaluateInContext(DbgTypedValue contextTypedValue)
{
	if (mExprNode == NULL)
		return DbgTypedValue();
	mPassInstance->ClearErrors();
	if (contextTypedValue)
	{
		mDbgExprEvaluator->mExplicitThis = contextTypedValue;
		if ((mDbgExprEvaluator->mExplicitThis.mType->IsPointer()) && (mDbgExprEvaluator->mExplicitThis.mType->mTypeParam->WantsRefThis()))
		{
			mDbgExprEvaluator->mExplicitThis.mType = mDbgExprEvaluator->mExplicitThis.mType->mTypeParam;
			mDbgExprEvaluator->mExplicitThis.mSrcAddress = mDbgExprEvaluator->mExplicitThis.mPtr;
			mDbgExprEvaluator->mExplicitThis.mPtr = 0;
		}

		if ((mDbgExprEvaluator->mExplicitThis.mType->IsCompositeType()) && (!mDbgExprEvaluator->mExplicitThis.mType->WantsRefThis()))
		{
			if (mDbgExprEvaluator->mExplicitThis.mSrcAddress != 0)
			{
				mDbgExprEvaluator->mExplicitThis.mType = mDbgExprEvaluator->mDbgModule->GetPointerType(mDbgExprEvaluator->mExplicitThis.mType);
				mDbgExprEvaluator->mExplicitThis.mPtr = mDbgExprEvaluator->mExplicitThis.mSrcAddress;
				mDbgExprEvaluator->mExplicitThis.mSrcAddress = 0;
			}
		}
	}
	if (contextTypedValue.mType != NULL)
		mDbgExprEvaluator->mDbgCompileUnit = contextTypedValue.mType->mCompileUnit;
	DbgTypedValue exprResult;
	auto result = mDbgExprEvaluator->Resolve(mExprNode);
	return result;
}

bool DbgEvaluationContext::HadError()
{
	return mPassInstance->mFailedIdx != 0;
}

String DbgEvaluationContext::GetErrorStr()
{
	String errorStr = mPassInstance->mErrors[0]->mError;
	if (mExprNode != NULL)
	{
		errorStr += ": ";
		errorStr += mExprNode->ToString();
	}
	return errorStr;
}

//////////////////////////////////////////////////////////////////////////

typedef HRESULT(WINAPI* SetThreadDescription_t)(HANDLE hThread, PCWSTR lpThreadDescription);
typedef HRESULT(WINAPI* GetThreadDescription_t)(HANDLE hThread, PWSTR* lpThreadDescription);
static SetThreadDescription_t gSetThreadDescription = NULL;
static GetThreadDescription_t gGetThreadDescription = NULL;
static HMODULE gKernelDll = NULL;

static void ImportKernel()
{
	if (gKernelDll != NULL)
		return;

	WCHAR path[MAX_PATH];
	GetSystemDirectory(path, MAX_PATH);
	wcscat(path, L"\\kernel32.dll");
	gKernelDll = GetModuleHandle(path);
	if (gKernelDll == NULL)
	{
		return;
	}

	gSetThreadDescription = (SetThreadDescription_t)GetProcAddress(gKernelDll, "SetThreadDescription");
	gGetThreadDescription = (GetThreadDescription_t)GetProcAddress(gKernelDll, "GetThreadDescription");
}

void WinDebugger::TryGetThreadName(WdThreadInfo* threadInfo)
{
	if (threadInfo->mHThread == NULL)
		return;

	ImportKernel();

	PWSTR wStr = NULL;
	if (gGetThreadDescription != NULL)
	{
		gGetThreadDescription(threadInfo->mHThread, &wStr);
		if (wStr == NULL)
			return;

		threadInfo->mName = UTF8Encode(wStr);
		FilterThreadName(threadInfo->mName);
		LocalFree(wStr);
	}
}

static void CreateFilterName(String& name, DbgType* type)
{
	CreateFilterName(name, type->mParent);

	switch (type->mTypeCode)
	{
	case DbgType_Namespace:
	case DbgType_Struct:
	case DbgType_Class:
		name += type->mName;
		break;
	}
}

static void CreateFilterName(String& name, const char* srcStr, DbgLanguage language)
{
	int chevronDepth = 0;
	const char* cPtr = srcStr;
	for (; true; cPtr++)
	{
		char c = *cPtr;
		if (c == 0)
			break;
		if (c == '>')
			chevronDepth--;
		bool inGeneric = chevronDepth > 0;
		if (c == '<')
			chevronDepth++;
		if (inGeneric) // Bundle all generic instances together
			continue;
		if (c == '[') // Bundle all arrays together
			name.clear();
		if (c == '(')
			return; // Start of params
		if ((c == ':') && (cPtr[1] == ':') && (language == DbgLanguage_Beef))
		{
			name.Append('.');
			cPtr++;
		}
		else
			name.Append(c);
	}
}

static void CreateFilterName(String& name, DbgSubprogram* subprogram)
{
	auto language = subprogram->GetLanguage();
	if (subprogram->mName == NULL)
	{
		if (subprogram->mLinkName[0] == '<')
		{
			name += subprogram->mLinkName;
			return;
		}
		name = BfDemangler::Demangle(subprogram->mLinkName, language);
		// Strip off the params since we need to generate those ourselves
		int parenPos = (int)name.IndexOf('(');
		if (parenPos != -1)
			name.RemoveToEnd(parenPos);
		return;
	}
	else if (subprogram->mHasQualifiedName)
	{
		const char* cPtr = subprogram->mName;
		if (strncmp(cPtr, "_bf::", 5) == 0)
		{
			CreateFilterName(name, cPtr + 5, DbgLanguage_Beef);
			name.Replace(".__BfStaticCtor", ".this$static");
			name.Replace(".__BfCtorClear", ".this$clear");
			name.Replace(".__BfCtor", ".this");
		}
		else
			CreateFilterName(name, subprogram->mName, language);

		return;
	}
	else
	{
		if (subprogram->mParentType != NULL)
		{
			String parentName = subprogram->mParentType->ToString();
			CreateFilterName(name, parentName.c_str(), language);
			if (!name.empty())
			{
				if (language == DbgLanguage_Beef)
					name += ".";
				else
					name += "::";
			}
		}

		if ((language == DbgLanguage_Beef) && (subprogram->mParentType != NULL) && (subprogram->mParentType->mTypeName != NULL) &&
			(strcmp(subprogram->mName, subprogram->mParentType->mTypeName) == 0))
			name += "this";
		else if ((language == DbgLanguage_Beef) && (subprogram->mName[0] == '~'))
			name += "~this";
		else if (strncmp(subprogram->mName, "_bf::", 5) == 0)
		{
			CreateFilterName(name, subprogram->mName + 5, DbgLanguage_Beef);
		}
		else
		{
			CreateFilterName(name, subprogram->mName, language);
		}
	}

	if (name.empty())
		name += "`anon";
	if ((name[name.length() - 1] == '!') || (name[0] == '<'))
	{
		if (language == DbgLanguage_Beef)
		{
			// It's a mixin - assert that there's no params
			//BF_ASSERT(subprogram->mParams.Size() == 0);
		}
		return;
	}
}

//////////////////////////////////////////////////////////////////////////

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

// conversion logic based on table at http://en.wikipedia.org/wiki/Extended_precision
//CDH TODO put this somewhere more general
static double ConvertFloat80ToDouble(const byte fp80[10])
{
	uint16 e = *((uint16*)&fp80[8]);
	uint64 m = *((uint64*)&fp80[0]);
	uint64 bit63 = (uint64)1 << 63;
	uint64 bit62 = (uint64)1 << 62;

	bool isNegative = (e & 0x8000) != 0;
	double s = isNegative ? -1.0 : 1.0;
	e &= 0x7fff;

	if (!e)
	{
		// the high bit and mantissa content will determine whether it's an actual zero, or a denormal or
		// pseudo-denormal number with an effective exponent of -16382.  But since that exponent is so far
		// below anything we can handle in double-precision (even accounting for denormal bit shifts), we're
		// effectively still dealing with zero.
		return s * 0.0;
	}
	else if (e == 0x7fff)
	{
		if (m & bit63)
		{
			if (m & bit62)
			{
				return std::numeric_limits<double>::quiet_NaN();
			}
			else
			{
				if (m == bit63)
					return s * std::numeric_limits<double>::infinity();
				else
					return std::numeric_limits<double>::signaling_NaN();
			}
		}
		else
		{
			return std::numeric_limits<double>::quiet_NaN();
		}
	}
	else
	{
		if (!(m & bit63))
			return std::numeric_limits<double>::quiet_NaN(); // unnormal (we don't handle these since 80387 and later treat them as invalid operands anyway)

		// else is a normalized value
	}

	int useExponent = (int)e - 16383;
	if (useExponent < -1022)
		return s * 0.0; // we could technically support e from -1023 to -1074 as denormals, but don't bother with that for now.
	else if (useExponent > 1023)
		return s * HUGE_VAL;

	useExponent += 1023;

	BF_ASSERT((useExponent > 0) && (useExponent < 0x7ff)); // assume we've filtered for valid exponent range
	BF_ASSERT(m & bit63); // assume we've filtered out values that aren't normalized by now

	uint64 result = 0;
	if (isNegative)
		result |= bit63;
	result |= (uint64)useExponent << 52;
	result |= (m & ~bit63) >> 11;

	return *reinterpret_cast<double*>(&result);
}

addr_target NS_BF_DBG::DecodeTargetDataPtr(const char*& strRef)
{
	addr_target val = (addr_target)stouln(strRef, sizeof(intptr_target) * 2);
	strRef += sizeof(intptr_target) * 2;
	return val;
}

WinDebugger::WinDebugger(DebugManager* debugManager) : mDbgSymSrv(this)
{
	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));

	mActiveHotIdx = -1;
	mGotStartupEvent = false;
	mIsContinuingFromException = false;
	mDestroying = false;
	mDebugManager = debugManager;
	mNeedsRehupBreakpoints = false;
	mStepInAssembly = false;
	mStepSP = 0;
	mStepIsRecursing = false;
	mStepStopOnNextInstruction = false;
	mDebugTarget = NULL;
	mShuttingDown = false;
	mBfSystem = new BfSystem();
	mAtBreakThread = NULL;
	mActiveThread = NULL;
	mActiveBreakpoint = NULL;
	mSteppingThread = NULL;
	mExplicitStopThread = NULL;
	mStepSwitchedThreads = false;
	mIsDebuggerWaiting = false;
	mWantsDebugContinue = false;
	mContinueFromBreakpointFailed = false;
	mIsStepIntoSpecific = false;
	mDbgBreak = false;
	mDebuggerWaitingThread = NULL;
	mStepType = StepType_None;
	mOrigStepType = StepType_None;
	mLastValidStepIntoPC = 0;
	mActiveSymSrvRequest = NULL;

	mStoredReturnValueAddr = 0;
#ifdef BF_DBG_32
	mCPU = gX86Target->mX86CPU;
#else
	mCPU = gX86Target->mX64CPU;
#endif
	mRunState = RunState_NotStarted;
	mIsRunning = false;
	mSavedAtBreakpointAddress = 0;
	mSavedBreakpointAddressContinuing = 0;
	mRequestedStackFrameIdx = 0;
	mShowPCOverride = 0;
	mCurNoInfoStepTries = 0;
	mDbgAttachFlags = BfDbgAttachFlag_None;
	mDbgProcessHandle = 0;
	mDbgThreadHandle = 0;
	mDbgProcessId = 0;
	mDbgHeapData = NULL;
	mIsPartialCallStack = true;

	for (int i = 0; i < 4; i++)
	{
		mFreeMemoryBreakIndices.push_back(i);
	}
	mMemoryBreakpointVersion = 0;

	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	mPageSize = systemInfo.dwPageSize;

	mEmptyDebugTarget = new DebugTarget(this);
	mEmptyDebugTarget->CreateEmptyTarget();
	mEmptyDebugTarget->mIsEmpty = true;
	mDebugTarget = mEmptyDebugTarget;
	mDebugPendingExpr = NULL;
	mDebugEvalThreadInfo = WdThreadInfo();

	mMemCacheAddr = 0;
	mDebuggerThreadId = 0;
}

WinDebugger::~WinDebugger()
{
	mDestroying = true;

	delete gDbgPerfManager;
	gDbgPerfManager = NULL;

	if ((mDebugTarget != NULL) && (mDebugTarget != mEmptyDebugTarget))
		Detach();

	for (auto breakpoint : mBreakpoints)
	{
		auto checkBreakpoint = breakpoint->mLinkedSibling;
		while (checkBreakpoint != NULL)
		{
			auto nextBreakpoint = checkBreakpoint->mLinkedSibling;
			delete checkBreakpoint;
			checkBreakpoint = nextBreakpoint;
		}

		delete breakpoint;
	}

	delete mEmptyDebugTarget;

	delete mBfSystem;
	for (auto kv : mPendingProfilerMap)
		delete kv.mValue;
	for (auto profiler : mNewProfilerList)
		delete profiler;
	delete mDebugPendingExpr;
}

void WinDebugger::Fail(const StringImpl& error)
{
	if (mIsRunning)
		mDebugManager->mOutMessages.push_back(StrFormat("error %s", error.c_str()));
}

// Leave active thread unpaused
void WinDebugger::ThreadRestorePause(WdThreadInfo* onlyPauseThread, WdThreadInfo* dontPauseThread)
{
	BfLogDbg("ThreadRestorePause %d %d\n", (onlyPauseThread != NULL) ? onlyPauseThread->mThreadId : 0, (dontPauseThread != NULL) ? dontPauseThread->mThreadId : 0);
	for (auto threadInfo : mThreadList)
	{
		if (((threadInfo != dontPauseThread) && (!threadInfo->mIsBreakRestorePaused)) &&
			((onlyPauseThread == NULL) || (threadInfo == onlyPauseThread)))
		{
			BF_ASSERT(!threadInfo->mIsBreakRestorePaused);

			BfLogDbg("SuspendThread %d\n", threadInfo->mThreadId);
			::SuspendThread(threadInfo->mHThread);

			threadInfo->mIsBreakRestorePaused = true;
		}
	}
}

void WinDebugger::ThreadRestoreUnpause()
{
	BfLogDbg("ThreadRestoreUnpause\n");
	for (auto threadInfo : mThreadList)
	{
		if (threadInfo->mIsBreakRestorePaused)
		{
			BfLogDbg("ResumeThread %d\n", threadInfo->mThreadId);
			::ResumeThread(threadInfo->mHThread);
			threadInfo->mIsBreakRestorePaused = false;
		}
	}
}

void WinDebugger::UpdateThreadDebugRegisters(WdThreadInfo* threadInfo)
{
	if (threadInfo->mMemoryBreakpointVersion == mMemoryBreakpointVersion)
		return;

	auto threadId = threadInfo->mHThread;

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_DEBUG_REGISTERS;
	BF_GetThreadContext(threadId, &lcContext);
	for (int memoryBreakIdx = 0; memoryBreakIdx < 4; memoryBreakIdx++)
	{
		WdMemoryBreakpointBind memoryBreakpointBind = mMemoryBreakpoints[memoryBreakIdx];

		WdBreakpoint* wdBreakpoint = memoryBreakpointBind.mBreakpoint;
		if (wdBreakpoint == NULL)
		{
			*(&lcContext.Dr0 + memoryBreakIdx) = 0;
			lcContext.Dr7 &= ~((1 << (memoryBreakIdx * 2)) | (1 << (16 + memoryBreakIdx * 4)) | (3 << (18 + memoryBreakIdx * 4)));
		}
		else
		{
			int sizeCode = 0;
			if (memoryBreakpointBind.mByteCount == 2)
				sizeCode = 1;
			else if (memoryBreakpointBind.mByteCount == 4)
				sizeCode = 3;
			else if (memoryBreakpointBind.mByteCount == 8)
				sizeCode = 2;

			addr_target calcAddr = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress + memoryBreakpointBind.mOfs;
			BF_ASSERT(calcAddr == memoryBreakpointBind.mAddress);

			*(&lcContext.Dr0 + memoryBreakIdx) = calcAddr;
			lcContext.Dr7 |= (1 << (memoryBreakIdx * 2)) | (1 << (16 + memoryBreakIdx * 4)) | (sizeCode << (18 + memoryBreakIdx * 4));
		}
	}
	bool worked = BF_SetThreadContext(threadId, &lcContext) != 0;
	BF_ASSERT(worked || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated));
	threadInfo->mMemoryBreakpointVersion = mMemoryBreakpointVersion;
}

void WinDebugger::UpdateThreadDebugRegisters()
{
	for (auto threadInfo : mThreadList)
	{
		::SuspendThread(threadInfo->mHThread);
		UpdateThreadDebugRegisters(threadInfo);
		::ResumeThread(threadInfo->mHThread);
	}
}

void WinDebugger::PhysSetBreakpoint(addr_target address)
{
	BfLogDbg("PhysSetBreakpoint %p\n", address);

	uint8 newData = 0xCC;

	// This ensure that we have the orig image data cached
	DbgMemoryFlags flags = mDebugTarget->ReadOrigImageData(address, NULL, 1);
	if ((flags & DbgMemoryFlags_Execute) == 0)
	{
		BfLogDbg("Breakpoint ignored - execute flag NOT set in breakpoint address\n", address);

		BfLogDbg("Memory Flags = %d\n", gDebugger->GetMemoryFlags(address));

		return;
	}

	// Replace it with Breakpoint
	SIZE_T dwReadBytes;
	BOOL worked = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, &newData, 1, &dwReadBytes);
	if (!worked)
	{
		int err = GetLastError();
		BfLogDbg("SetBreakpoint FAILED %p\n", address);
	}
	FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, 1);

	{
		uint8 mem = ReadMemory<uint8>(address);
		BfLogDbg("Breakpoint byte %X\n", mem);
	}
}

void WinDebugger::SetBreakpoint(addr_target address, bool fromRehup)
{
	int* countPtr = NULL;
	if (mPhysBreakpointAddrMap.TryAdd(address, NULL, &countPtr))
	{
		BfLogDbg("SetBreakpoint %p\n", address);
		*countPtr = 1;
	}
	else
	{
		if (fromRehup)
		{
			BfLogDbg("SetBreakpoint %p Count: %d. Rehup (ignored).\n", address, *countPtr);
			return;
		}

		(*countPtr)++;
		BfLogDbg("SetBreakpoint %p Count: %d\n", address, *countPtr);
		return;
	}

	PhysSetBreakpoint(address);
}

void WinDebugger::SetTempBreakpoint(addr_target address)
{
	BfLogDbg("SetTempBreakpoint %p\n", address);
	mTempBreakpoint.push_back(address);
	SetBreakpoint(address);
}

void WinDebugger::PhysRemoveBreakpoint(addr_target address)
{
	BfLogDbg("PhysRemoveBreakpoint %p\n", address);

	uint8 origData;
	DbgMemoryFlags flags = mDebugTarget->ReadOrigImageData(address, &origData, 1);
	if ((flags & DbgMemoryFlags_Execute) == 0)
	{
		//BF_ASSERT("Failed" == 0);
		return;
	}

	SIZE_T dwReadBytes;
	if (!WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, &origData, 1, &dwReadBytes))
	{
		int err = GetLastError();
		BfLogDbg("RemoveBreakpoint FAILED %p\n", address);
	}
	FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, 1);
}

void WinDebugger::RemoveBreakpoint(addr_target address)
{
	int* countPtr = NULL;
	mPhysBreakpointAddrMap.TryGetValue(address, &countPtr);

	// This can happen when we shutdown and we're continuing from a breakpoint
	//BF_ASSERT(*countPtr != NULL);
	if (countPtr == NULL)
	{
		BfLogDbg("RemoveBreakpoint %p FAILED\n", address);
		return;
	}

	BfLogDbg("RemoveBreakpoint %p count: %d\n", address, *countPtr);
	if (*countPtr > 1)
	{
		(*countPtr)--;
		return;
	}
	mPhysBreakpointAddrMap.Remove(address);
	PhysRemoveBreakpoint(address);
}

void WinDebugger::SingleStepX86()
{
	// In what cases did this catch bugs?
	// This caused other failures (caught in tests)
// 	if (mActiveThread->mIsAtBreakpointAddress != 0)
// 	{
// 		ContinueFromBreakpoint();
// 		return;
// 	}

	BfLogDbg("Setup SingleStepX86 ActiveThread: %d\n", (mActiveThread != NULL) ? mActiveThread->mThreadId : -1);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
	lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);
}

bool WinDebugger::IsInRunState()
{
	return (mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint);
}

bool WinDebugger::ContinueFromBreakpoint()
{
	if (mDebuggerWaitingThread->mFrozen)
	{
		BfLogDbg("ContinueFromBreakpoint bailout on frozen thread\n");
		mDebuggerWaitingThread->mStoppedAtAddress = 0;
		mDebuggerWaitingThread->mIsAtBreakpointAddress = 0;
		return true;
	}

	mActiveThread = mDebuggerWaitingThread;
	mActiveBreakpoint = NULL;

	BfLogDbg("ContinueFromBreakpoint. ActiveThread: %d\n", (mActiveThread != NULL) ? mActiveThread->mThreadId : -1);

	BfLogDbg("ResumeThread %d\n", mActiveThread->mThreadId);
	BOOL success = ::ResumeThread(mActiveThread->mHThread);
	if (success)
	{
		// It's possible the active thread is suspended - possibly by the GC, so we would deadlock if we
		//  attempted to pause the other threads
		BfLogDbg("SuspendThread %d\n", mActiveThread->mThreadId);
		BfLogDbg("Thread already paused!\n");
		::SuspendThread(mActiveThread->mHThread);
		return false;
	}

	ThreadRestorePause(NULL, mActiveThread);

	PhysRemoveBreakpoint(mActiveThread->mIsAtBreakpointAddress);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
	lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

	mActiveThread->mStoppedAtAddress = 0;
	mActiveThread->mBreakpointAddressContinuing = mActiveThread->mIsAtBreakpointAddress;
	mActiveThread->mIsAtBreakpointAddress = 0;
	BfLogDbg("ContinueFromBreakpoint set mIsAtBreakpointAddress = 0\n");
	return true;
}

void WinDebugger::ValidateBreakpoints()
{
	HashSet<addr_target> usedBreakpoints;

	std::function<void(WdBreakpoint*)> _AddBreakpoint = [&](WdBreakpoint* breakpoint)
	{
		if (breakpoint->mAddr != 0)
		{
			usedBreakpoints.Add(breakpoint->mAddr);

			WdBreakpoint* foundBreakpoint = NULL;
			auto itr = mBreakpointAddrMap.Find(breakpoint->mAddr);
			bool found = false;
			while (itr != mBreakpointAddrMap.end())
			{
				WdBreakpoint* foundBreakpoint = itr->mValue;
				found |= foundBreakpoint == breakpoint;
				itr.NextWithSameKey(breakpoint->mAddr);
			}

			BF_ASSERT(found);
		}

		auto checkSibling = (WdBreakpoint*)breakpoint->mLinkedSibling;
		while (checkSibling != NULL)
		{
			_AddBreakpoint(checkSibling);
			checkSibling = (WdBreakpoint*)checkSibling->mLinkedSibling;
		}
	};

	for (auto breakpoint : mBreakpoints)
		_AddBreakpoint(breakpoint);

	for (auto& entry : mBreakpointAddrMap)
	{
		BF_ASSERT(usedBreakpoints.Contains(entry.mKey));
	}
}

Breakpoint* WinDebugger::FindBreakpointAt(intptr address)
{
#ifdef _DEBUG
	//ValidateBreakpoints();
#endif

	WdBreakpoint* breakpoint = NULL;
	mBreakpointAddrMap.TryGetValue(address, &breakpoint);
	return breakpoint;
}

Breakpoint* WinDebugger::GetActiveBreakpoint()
{
	if ((mActiveBreakpoint != NULL) && (mActiveBreakpoint->mHead != NULL))
		return mActiveBreakpoint->mHead;
	return mActiveBreakpoint;
}

void WinDebugger::DebugThreadProc()
{
	BpSetThreadName("DebugThread");
	BfpThread_SetName(NULL, "DebugThread", NULL);

	mDebuggerThreadId = GetCurrentThreadId();

	if (!IsMiniDumpDebugger())
	{
		if (!DoOpenFile(mLaunchPath, mArgs, mWorkingDir, mEnvBlock))
		{
			if (mDbgProcessId != 0)
				OutputRawMessage("error Unable to attach to process");
			else
				OutputRawMessage(StrFormat("error Failed to launch: %s", mLaunchPath.c_str()));
			mShuttingDown = true;
			mRunState = RunState_Terminated;
		}
	}

	while (!mShuttingDown)
	{
		DoUpdate();
	}

	mIsRunning = false;

	for (int i = 0; i < (int) mBreakpoints.size(); i++)
	{
		WdBreakpoint* wdBreakpoint = mBreakpoints[i];

		if (wdBreakpoint->mAddr != 0)
			mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);

		wdBreakpoint->mAddr = 0;
		wdBreakpoint->mLineData = DbgLineDataEx();
		wdBreakpoint->mSrcFile = NULL;
		if (wdBreakpoint->mLinkedSibling != NULL)
		{
			DeleteBreakpoint(wdBreakpoint->mLinkedSibling);
			wdBreakpoint->mLinkedSibling = NULL;
		}
	}

	if (!IsMiniDumpDebugger())
	{
		while (true)
		{
			if (!mIsDebuggerWaiting)
			{
				if (!WaitForDebugEvent(&mDebugEvent, 0))
					break;
			}
			if (mDebuggerWaitingThread != NULL)
			{
				BF_ASSERT_REL((mDebuggerWaitingThread->mIsAtBreakpointAddress == 0) || (mShuttingDown));
				::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_CONTINUE);
				BfLogDbg("::ContinueDebugEvent startup ThreadId:%d\n", mDebuggerWaitingThread->mThreadId);
			}
			mIsDebuggerWaiting = false;
			mDebuggerWaitingThread = NULL;
		}
	}

	mDebuggerThreadId = 0;
}

static void DebugThreadProcThunk(void* winDebugger)
{
	((WinDebugger*) winDebugger)->DebugThreadProc();
}

int WinDebugger::GetAddrSize()
{
	return sizeof(addr_target);
}

bool WinDebugger::CanOpen(const StringImpl& fileName, DebuggerResult* outResult)
{
	FILE* fp = fopen(fileName.c_str(), "rb");
	if (fp == NULL)
	{
		*outResult = DebuggerResult_CannotOpen;
		return false;
	}

	FileStream fs;
	fs.mFP = fp;

	*outResult = DebuggerResult_Ok;
	bool canRead = DbgModule::CanRead(&fs, outResult);
	fclose(fp);
	return canRead;
}

void WinDebugger::OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled)
{
	BF_ASSERT(!mIsRunning);
	mLaunchPath = launchPath;
	mTargetPath = targetPath;
	mArgs = args;
	mWorkingDir = workingDir;
	mEnvBlock = envBlock;
	mHotSwapEnabled = hotSwapEnabled;
	mDebugTarget = new DebugTarget(this);
}

bool WinDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	BF_ASSERT(!mIsRunning);

	mDbgAttachFlags = attachFlags;
	mDbgProcessHandle = ::OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)processId);
	if (mDbgProcessHandle == 0)
		return false;
	BOOL is32Bit = false;
	if (!IsWow64Process(mDbgProcessHandle, &is32Bit))
	{
		mDbgProcessHandle = 0;
		::CloseHandle(mDbgProcessHandle);
		return false;
	}

	bool want32Bit = sizeof(intptr_target) == 4;
	if (want32Bit != (is32Bit != 0))
	{
		mDbgProcessHandle = 0;
		::CloseHandle(mDbgProcessHandle);
		return false;
	}

	HMODULE mainModule = 0;
	DWORD memNeeded = 0;
	::EnumProcessModules(mDbgProcessHandle, &mainModule, sizeof(HMODULE), &memNeeded);

	WCHAR fileName[MAX_PATH] = {0};
	GetModuleFileNameExW(mDbgProcessHandle, mainModule, fileName, MAX_PATH);
	mLaunchPath = UTF8Encode(fileName);
	mTargetPath = mLaunchPath;

	mDbgProcessId = processId;
	mDbgProcessHandle = 0;
	::CloseHandle(mDbgProcessHandle);

	mDebugTarget = new DebugTarget(this);

	return true;
}

void WinDebugger::Run()
{
	mIsRunning = true;
	DWORD localThreadId;
	HANDLE hThread = ::CreateThread(NULL, 64 * 1024, (LPTHREAD_START_ROUTINE) &DebugThreadProcThunk, (void*)this, 0, &localThreadId);
	CloseHandle(hThread);
}

bool WinDebugger::HasLoadedTargetBinary()
{
	if (mDebugTarget == NULL)
		return false;
	return mDebugTarget->mTargetBinary != NULL;
}

void WinDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mDebugTarget->mTargetBinary == NULL)
	{
		Fail("Hot swapping failed because the hot target binary has not yet been loaded.");
		return;
	}

	if (mDebugTarget->mHotHeap == NULL)
	{
		Fail("There is no hot heap space available for hot swapping.");
		return;
	}

	BfLogDbg("WinDebugger::HotLoad Start %d\n", hotIdx);

	SetAndRestoreValue<int> prevHotIdx(mActiveHotIdx, hotIdx);

	BF_ASSERT(mHotThreadStates.empty());
	mHotThreadStates.Resize(mThreadList.size());
	for (int threadIdx = 0; threadIdx < (int)mThreadList.size(); threadIdx++)
	{
		WdThreadInfo* threadInfo = mThreadList[threadIdx];
		SetAndRestoreValue<WdThreadInfo*> prevActiveThread(mActiveThread, threadInfo);
		BfLogDbg("SuspendThread %d\n", threadInfo->mThreadId);
		::SuspendThread(threadInfo->mHThread);
		mHotThreadStates[threadIdx].mThreadId = threadInfo->mThreadId;
		PopulateRegisters(&mHotThreadStates[threadIdx].mRegisters);
	}

	for (auto address : mTempBreakpoint)
		RemoveBreakpoint(address);
	mTempBreakpoint.Clear();
	mStepBreakpointAddrs.Clear();
	for (auto breakpoint : mBreakpoints)
	{
		DetachBreakpoint(breakpoint);
	}

	int startingModuleIdx = (int)mDebugTarget->mDbgModules.size();

	bool failed = false;
	for (auto fileName : objectFiles)
	{
		BfLogDbg("WinDebugger::HotLoad: %s\n", fileName.c_str());
		DbgModule* newBinary = mDebugTarget->HotLoad(fileName, hotIdx);
		if ((newBinary != NULL) && (newBinary->mFailed))
			failed = true;
	}

	for (int moduleIdx = startingModuleIdx; moduleIdx < (int)mDebugTarget->mDbgModules.size(); moduleIdx++)
	{
		auto dbgModule = mDebugTarget->mDbgModules[moduleIdx];
		BF_ASSERT(dbgModule->IsObjectFile());
		BF_ASSERT(dbgModule->mHotIdx == hotIdx);
		dbgModule->FinishHotSwap();
	}

	for (auto dwarf : mDebugTarget->mDbgModules)
		dwarf->RevertWritingEnable();

	int blockAllocSinceClean = mDebugTarget->mHotHeap->mBlockAllocIdx - mDebugTarget->mLastHotHeapCleanIdx;
	// Clean up the hot heap every 64MB
	int blocksBetweenCleans = (64 * 1024 * 1024) / HotHeap::BLOCK_SIZE;

#ifdef _DEBUG
	//TODO: This is just for testing
	blocksBetweenCleans = 1;
#endif

	//TODO: Put this back after we fix the cleanup
	if (blockAllocSinceClean >= blocksBetweenCleans)
		CleanupHotHeap();

	mDebugTarget->RehupSrcFiles();

	for (int breakIdx = 0; breakIdx < (int)mBreakpoints.size(); breakIdx++)
	{
		auto breakpoint = mBreakpoints[breakIdx];
		CheckBreakpoint(breakpoint);
	}

	for (int hotThreadIdx = 0; hotThreadIdx < (int)mHotThreadStates.size(); hotThreadIdx++)
	{
		auto& hotThreadState = mHotThreadStates[hotThreadIdx];
		WdThreadInfo* threadInfo = NULL;
		if (!mThreadMap.TryGetValue((uint32)hotThreadState.mThreadId, &threadInfo))
			continue;

		BfLogDbg("ResumeThread %d\n", threadInfo->mThreadId);
		::ResumeThread(threadInfo->mHThread);
	}

	mHotThreadStates.Clear();

	if (IsPaused())
	{
		ClearCallStack();
		UpdateCallStack();
	}
}

void WinDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	delete mHotResolveData;
	mHotResolveData = NULL;

	mHotResolveData = new DbgHotResolveData();
	DbgHotScanner* hotScanner = new DbgHotScanner(this);
	hotScanner->Scan(flags);
	delete hotScanner;
}

intptr WinDebugger::GetDbgAllocHeapSize()
{
	if (mDbgHeapData == NULL)
	{
		Beefy::String memName = StrFormat("BFGC_stats_%d", mProcessInfo.dwProcessId);

		mDbgHeapData = new WinDbgHeapData();
		mDbgHeapData->mFileMapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, memName.c_str());
		if (mDbgHeapData->mFileMapping == 0)
		{
			delete mDbgHeapData;
			mDbgHeapData = NULL;
			return 0;
		}

		mDbgHeapData->mStats = (WinDbgHeapData::Stats*)MapViewOfFile(mDbgHeapData->mFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(WinDbgHeapData::Stats));
	}

	if (mDbgHeapData->mStats == NULL)
		return 0;

	return mDbgHeapData->mStats->mHeapSize;
}

String WinDebugger::GetDbgAllocInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto threadInfo : mThreadList)
		::SuspendThread(threadInfo->mHThread);

	delete mHotResolveData;
	mHotResolveData = NULL;

	mHotResolveData = new DbgHotResolveData();
	DbgHotScanner* hotScanner = new DbgHotScanner(this);
	hotScanner->Scan((DbgHotResolveFlags)(DbgHotResolveFlag_Allocations | DbgHotResolveFlag_KeepThreadState));
	delete hotScanner;

	String result;

	if (mHotResolveData != NULL)
	{
		DbgExprEvaluator exprEvaluator(this, NULL, NULL, -1, -1);
		exprEvaluator.mDebugTarget = mDebugTarget;

		String typeName;

		result += ":types\n";

		for (int typeId = 0; typeId < mHotResolveData->mTypeData.size(); typeId++)
		{
			auto& typeData = mHotResolveData->mTypeData[typeId];
			if (typeData.mCount > 0)
			{
				auto type = exprEvaluator.GetBeefTypeById(typeId);
				typeName.Clear();
				exprEvaluator.BeefTypeToString(type, typeName);
				if (typeName.IsEmpty())
					typeName = StrFormat("Type #%d", typeId);
				result += StrFormat("type\t%d\t%s\t%lld\t%lld\n", typeId, typeName.c_str(), typeData.mCount, typeData.mSize);
			}
		}
	}

	for (auto threadInfo : mThreadList)
		::ResumeThread(threadInfo->mHThread);

	return result;
}

bool WinDebugger::DoOpenFile(const StringImpl& fileName, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock)
{
	BP_ZONE("WinDebugger::DoOpenFile");

	AutoCrit autoCrit(mDebugManager->mCritSect);
	//gDbgPerfManager->StartRecording();

	STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));

	if (mDbgProcessId != 0)
	{
		BOOL success = ::DebugActiveProcess(mDbgProcessId);
		if (!success)
			return false;

		mProcessInfo.dwProcessId = mDbgProcessId;
	}
	else
	{
		BP_ZONE("DoOpenFile_CreateProcessW");

		UTF16String envW;

		DWORD flags = DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_DEFAULT_ERROR_MODE;
		void* envPtr = NULL;
		if (!envBlock.IsEmpty())
		{
			//UTF16?
			if (envBlock[1] == 0)
			{
				envPtr = (void*)&envBlock[0];
				flags |= CREATE_UNICODE_ENVIRONMENT;
			}
			else
			{
				String str8((char*)&envBlock[0], (int)envBlock.size());
				envW = UTF8Decode(str8);

				envPtr = (void*)envW.c_str();
				flags |= CREATE_UNICODE_ENVIRONMENT;
			}
		}

		String cmdLine = "\"";
		cmdLine += fileName;
		cmdLine += "\"";

		if (!args.IsEmpty())
		{
			cmdLine += " ";
			cmdLine += args;
		}

		BOOL worked = CreateProcessW(NULL, (WCHAR*)UTF8Decode(cmdLine).c_str(), NULL, NULL, FALSE,
			flags, envPtr, (WCHAR*)UTF8Decode(workingDir).c_str(), &si, &mProcessInfo);

		if (!worked)
		{
			auto lastError = ::GetLastError();
			if (lastError == ERROR_DIRECTORY)
			{
				mDebugManager->mOutMessages.push_back(StrFormat("error Unable to locate specified working directory '%s'", SlashString(workingDir, false, false).c_str()));
			}

			return false;
		}

		WdThreadInfo* threadInfo = new WdThreadInfo();
		threadInfo->mProcessId = mProcessInfo.dwProcessId;
		threadInfo->mThreadId = mProcessInfo.dwThreadId;
		threadInfo->mHThread = mProcessInfo.hThread;
		threadInfo->mThreadLocalBase = NULL;
		threadInfo->mStartAddress = NULL;
		mThreadMap[mProcessInfo.dwThreadId] = threadInfo;
		mThreadList.push_back(threadInfo);
	}

	mRunState = RunState_Running;

	while (true)
	{
		BP_ZONE("DoOpenFile_WaitForImageBase");
		autoCrit.mCritSect->Unlock();
		DoUpdate();
		autoCrit.mCritSect->Lock();

		ContinueDebugEvent();

		if ((mDebugTarget->mLaunchBinary != NULL) && (mDebugTarget->mLaunchBinary->mOrigImageData != NULL))
			break;
	}

	RehupBreakpoints(true);

	//gDbgPerfManager->StopRecording();
	//gDbgPerfManager->DbgPrint();

	return true;
}

void WinDebugger::StopDebugging()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	BfLogDbg("WinDebugger::Terminate\n");
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminated) || (mRunState == RunState_Terminating))
		return;

	if ((mDbgProcessId != 0) && ((mDbgAttachFlags & BfDbgAttachFlag_ShutdownOnExit) == 0))
	{
		for (auto address : mTempBreakpoint)
			RemoveBreakpoint(address);
		for (auto breakpoint : mBreakpoints)
			DetachBreakpoint(breakpoint);

		BfLogDbg("StopDebugging\n");
		::DebugActiveProcessStop(mDbgProcessId);
		mRunState = RunState_Terminated;
		BfLogDbg("mRunState = RunState_Terminated\n");
	}
	else
	{
		TerminateProcess(mProcessInfo.hProcess, 0);
		mRunState = RunState_Terminating;
		BfLogDbg("mRunState = RunState_Terminating\n");
	}
}

void WinDebugger::Terminate()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	BfLogDbg("WinDebugger::Terminate\n");
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminated) || (mRunState == RunState_Terminating))
		return;
	TerminateProcess(mProcessInfo.hProcess, 0);
	mRunState = RunState_Terminating;
	BfLogDbg("mRunState = RunState_Terminating\n");
}

static int gDebugUpdateCnt = 0;

void WinDebugger::Detach()
{
	BfLogDbg("Debugger Detach\n");

	mDebugManager->mNetManager->CancelAll();
	while ((mIsRunning) || (mDebuggerThreadId != 0))
	{
		mShuttingDown = true;
		Sleep(1);
	}

	for (auto profiler : mProfilerSet)
		profiler->Stop();

	BfLogDbg("Debugger Detach - thread finished\n");

	mPendingProfilerMap.Clear();
	for (auto profiler : mNewProfilerList)
		delete profiler;
	mNewProfilerList.Clear();

	mPendingImageLoad.Clear();
	mPendingDebugInfoLoad.Clear();

	RemoveTempBreakpoints();
	mContinueEvent.Reset();
	if (mDebugTarget != mEmptyDebugTarget)
		delete mDebugTarget;
	mDebugTarget = mEmptyDebugTarget;

	mShuttingDown = false;
	mStepSP = 0;
	ClearCallStack();
	mRunState = RunState_NotStarted;
	mStepType = StepType_None;
	mHadImageFindError = false;
	mIsPartialCallStack = true;

	delete mDebugPendingExpr;
	mDebugPendingExpr = NULL;

	for (auto threadPair : mThreadMap)
	{
		auto threadInfo = threadPair.mValue;
		delete threadInfo;
	}
	mThreadMap.Clear();
	mThreadList.Clear();
	mHotTargetMemory.Clear();

	// We don't need to close the hThread when we have attached to a process
	if (mDbgProcessId == 0)
	{
		CloseHandle(mProcessInfo.hThread);
		CloseHandle(mProcessInfo.hProcess);
	}

	for (auto breakpoint : mBreakpoints)
	{
		if (!mDestroying)
		{
			BF_FATAL("Breakpoints should be deleted already");
		}

		if (breakpoint->mMemoryBreakpointInfo != NULL)
		{
			DetachBreakpoint(breakpoint);
		}
	}

	ZeroMemory(&mProcessInfo, sizeof(mProcessInfo));
	mStepBreakpointAddrs.Clear();
	mIsRunning = false;

	mDbgAttachFlags = BfDbgAttachFlag_None;
	mDbgProcessId = 0;
	delete mDbgHeapData;
	mDbgHeapData = NULL;
	mDbgProcessHandle = 0;
	ClearCallStack();
	mWantsDebugContinue = false;
	mAtBreakThread = NULL;
	mActiveThread = NULL;
	mActiveBreakpoint = NULL;
	mSteppingThread = NULL;
	mExplicitStopThread = NULL;
	mIsContinuingFromException = false;
	mGotStartupEvent = false;

	mIsDebuggerWaiting = false;
	mPhysBreakpointAddrMap.Clear();
	mBreakpointAddrMap.Clear();

	gDebugUpdateCnt = 0;
}

Profiler* WinDebugger::StartProfiling()
{
	return new DbgProfiler(this);
}

Profiler* WinDebugger::PopProfiler()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	if (mNewProfilerList.IsEmpty())
		return NULL;
	auto profiler = (DbgProfiler*)mNewProfilerList[0];
	mNewProfilerList.erase(mNewProfilerList.begin());
	return profiler;
}

void WinDebugger::AddProfiler(DbgProfiler * profiler)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mProfilerSet.Add(profiler);
}

void WinDebugger::RemoveProfiler(DbgProfiler * profiler)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mProfilerSet.Remove(profiler);
}

void WinDebugger::ReportMemory(MemReporter* memReporter)
{
	mEmptyDebugTarget->ReportMemory(memReporter);
	if (mDebugTarget != mEmptyDebugTarget)
		mDebugTarget->ReportMemory(memReporter);
}

bool WinDebugger::GetEmitSource(const StringImpl& filePath, String& outText)
{
	if (!filePath.StartsWith("$Emit"))
		return false;

	int dollarPos = filePath.IndexOf('$', 1);
	String numStr = filePath.Substring(5, dollarPos - 5);
	int id = atoi(numStr.c_str());

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (dbgModule->mId == id)
			return dbgModule->GetEmitSource(filePath, outText);
	}

	return false;
}

void WinDebugger::ModuleChanged(DbgModule* dbgModule)
{
	mDebugManager->mOutMessages.push_back(String("dbgInfoLoaded ") + dbgModule->mFilePath);
}

bool WinDebugger::DoUpdate()
{
	if ((mDbgProcessId != 0) && ((mDbgAttachFlags & BfDbgAttachFlag_ShutdownOnExit) == 0))
		::DebugSetProcessKillOnExit(FALSE);
	else
		::DebugSetProcessKillOnExit(TRUE);

	//
	{
		AutoCrit autoCrit(mDebugManager->mCritSect);
		auto _ModuleChanged = [&](DbgModule* dbgModule)
		{
			ModuleChanged(dbgModule);
			ClearCallStack(); // We may have actual dbgSubprograms and stuff now...
		};

		for (auto dbgModule : mPendingImageLoad)
		{
			dbgModule->PreCacheImage();
		}

		for (auto kv : mPendingDebugInfoLoad)
		{
			kv.mKey->PreCacheDebugInfo();
		}

		while (!mPendingImageLoad.IsEmpty())
		{
			auto dbgModule = mPendingImageLoad.back();
			mPendingImageLoad.pop_back();
			dbgModule->RequestImage();
			_ModuleChanged(dbgModule);
		}

		if (!mPendingDebugInfoLoad.IsEmpty())
		{
			Array<DbgPendingDebugInfoLoad> pendingList;
			for (auto kv : mPendingDebugInfoLoad)
				pendingList.Add(kv.mValue);
			mPendingDebugInfoLoad.Clear();

			for (auto& entry : pendingList)
			{
				auto dbgModule = entry.mModule;
				entry.mModule->RequestDebugInfo(entry.mAllowRemote);
				// We do a "_ModuleChanged" even if the load failed, so we rehup the callstack and stop
				//  saying "<Loading...>"
				_ModuleChanged(entry.mModule);
			}
		}
	}

	if (IsMiniDumpDebugger())
	{
		//
		{
			AutoCrit autoCrit(mDebugManager->mCritSect);
			if (mRunState == RunState_Terminating)
			{
				mRunState = RunState_Terminated;
				return false;
			}
		}

		Sleep(20);
		return false;
	}

	if (mIsDebuggerWaiting)
	{
		if ((IsInRunState()) || (mRunState == RunState_Terminating) || (mRunState == RunState_DebugEval))
			ContinueDebugEvent();
		if (mContinueEvent.WaitFor(8))
		{
			BF_ASSERT(!mWantsDebugContinue); // mWantsDebugContinue should already been reset
			BfLogDbg("::ContinueDebugEvent 1 ThreadId:%d\n", mDebuggerWaitingThread->mThreadId);
			BF_ASSERT_REL(mDebuggerWaitingThread->mIsAtBreakpointAddress == 0);
			::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, mIsContinuingFromException ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE);
			mIsContinuingFromException = false;
			mIsDebuggerWaiting = false;
			mDebuggerWaitingThread = NULL;
		}
		else
			return false;
	}

	if (!WaitForDebugEvent(&mDebugEvent, 8))
		return false;

	gDebugUpdateCnt++;

	static const char* eventNames[] = { "DBG_EVENT ?",
		"EXCEPTION_DEBUG_EVENT",
		"CREATE_THREAD_DEBUG_EVENT",
		"CREATE_PROCESS_DEBUG_EVENT",
		"EXIT_THREAD_DEBUG_EVENT",
		"EXIT_PROCESS_DEBUG_EVENT",
		"LOAD_DLL_DEBUG_EVENT",
		"UNLOAD_DLL_DEBUG_EVENT",
		"OUTPUT_DEBUG_STRING_EVENT",
		"RIP_EVENT"};

	BfLogDbg("WaitForDebugEvent %s ThreadId:%d\n", eventNames[mDebugEvent.dwDebugEventCode], mDebugEvent.dwThreadId);

	BP_ZONE(eventNames[mDebugEvent.dwDebugEventCode]);

	AutoCrit autoCrit(mDebugManager->mCritSect);

	mActiveBreakpoint = NULL;
	mIsDebuggerWaiting = true;
	mWantsDebugContinue = true;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mShowPCOverride = 0;

	WdThreadInfo* threadInfo = NULL;

	mThreadMap.TryGetValue(mDebugEvent.dwThreadId, &threadInfo);

	mDebuggerWaitingThread = threadInfo;
	mExplicitStopThread = mDebuggerWaitingThread;

	switch (mDebugEvent.dwDebugEventCode)
	{
	case CREATE_PROCESS_DEBUG_EVENT:
		{
			if (threadInfo == NULL)
			{
				BF_ASSERT(mThreadMap.size() == 0);

				WdThreadInfo* newThreadInfo = new WdThreadInfo();
				newThreadInfo->mProcessId = mDebugEvent.dwProcessId;
				newThreadInfo->mThreadId = mDebugEvent.dwThreadId;
				newThreadInfo->mHThread = mDebugEvent.u.CreateProcessInfo.hThread;
				newThreadInfo->mThreadLocalBase = mDebugEvent.u.CreateProcessInfo.lpThreadLocalBase;
				newThreadInfo->mStartAddress = (void*)mDebugEvent.u.CreateProcessInfo.lpStartAddress;

				BF_CONTEXT lcContext;
				lcContext.ContextFlags = BF_CONTEXT_CONTROL;
				BF_GetThreadContext(newThreadInfo->mHThread, &lcContext);
				newThreadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

				mThreadMap[mDebugEvent.dwThreadId] = newThreadInfo;
				mDebuggerWaitingThread = newThreadInfo;
				mThreadList.push_back(mDebuggerWaitingThread);
				UpdateThreadDebugRegisters();
				OutputMessage(StrFormat("Creating thread from CREATE_PROCESS_DEBUG_EVENT %d\n", mDebugEvent.dwThreadId));

				threadInfo = mDebuggerWaitingThread;
				mProcessInfo.dwThreadId = threadInfo->mThreadId;
				mProcessInfo.hThread = threadInfo->mHThread;
				mProcessInfo.hProcess = mDebugEvent.u.CreateProcessInfo.hProcess;
			}
			else
			{
				threadInfo->mThreadLocalBase = mDebugEvent.u.CreateProcessInfo.lpThreadLocalBase;
				threadInfo->mStartAddress = (void*)mDebugEvent.u.CreateProcessInfo.lpStartAddress;
			}

			BF_CONTEXT lcContext;
			lcContext.ContextFlags = BF_CONTEXT_CONTROL;
			BF_GetThreadContext(threadInfo->mHThread, &lcContext);
			threadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

			DbgModule* launchBinary = mDebugTarget->Init(mLaunchPath, mTargetPath, (addr_target)(intptr)mDebugEvent.u.CreateProcessInfo.lpBaseOfImage);
			addr_target gotImageBase = (addr_target)(intptr)mDebugEvent.u.CreateProcessInfo.lpBaseOfImage;
			if (launchBinary->mImageBase != gotImageBase)
			{
				BF_FATAL("Image base didn't match");
			}

			launchBinary->mImageBase = gotImageBase;
			launchBinary->mImageSize = (int)launchBinary->GetImageSize();
			launchBinary->mOrigImageData = new DbgModuleMemoryCache(launchBinary->mImageBase, launchBinary->mImageSize);

			if (launchBinary == mDebugTarget->mTargetBinary)
				mDebugTarget->SetupTargetBinary();

			if (mDebugEvent.u.CreateProcessInfo.hFile != NULL)
				CloseHandle(mDebugEvent.u.CreateProcessInfo.hFile);

			mDbgProcessHandle = mDebugEvent.u.CreateProcessInfo.hProcess;
			mDbgThreadHandle = mDebugEvent.u.CreateProcessInfo.hThread;

			mGotStartupEvent = true;
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case EXIT_PROCESS_DEBUG_EVENT:
		{
			BfLogDbg("EXIT_PROCESS_DEBUG_EVENT\n");

			DWORD exitCode = mDebugEvent.u.ExitProcess.dwExitCode;

			String exitMessage;
			switch (exitCode)
			{
			case STATUS_DLL_NOT_FOUND:
				exitMessage = "STATUS_DLL_NOT_FOUND";
				break;
			case STATUS_DLL_INIT_FAILED:
				exitMessage = "STATUS_DLL_INIT_FAILED";
				break;
			case STATUS_ENTRYPOINT_NOT_FOUND:
				exitMessage = "STATUS_ENTRYPOINT_NOT_FOUND";
				break;
			}

			String exitCodeStr;
			if ((exitCode >= 0x10000000) && (exitCode <= 0xF7000000))
				exitCodeStr = StrFormat("0x%X", exitCode);
			else
				exitCodeStr = StrFormat("%d", exitCode);

			if (!exitMessage.IsEmpty())
				OutputMessage(StrFormat("Process terminated. ExitCode: %s (%s).\n", exitCodeStr.c_str(), exitMessage.c_str()));
			else
				OutputMessage(StrFormat("Process terminated. ExitCode: %s.\n", exitCodeStr.c_str()));
			mRunState = RunState_Terminated;
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case LOAD_DLL_DEBUG_EVENT:
		{
			WCHAR moduleNameStr[MAX_PATH] = { 0 };
			GetFinalPathNameByHandleW(mDebugEvent.u.LoadDll.hFile, moduleNameStr, MAX_PATH, FILE_NAME_NORMALIZED);

			std::wstring wow64Dir;
			std::wstring systemDir;

			PWSTR wow64DirPtr = NULL;
			SHGetKnownFolderPath(FOLDERID_SystemX86, KF_FLAG_NO_ALIAS, NULL, &wow64DirPtr);
			if (wow64DirPtr != NULL)
			{
				wow64Dir = wow64DirPtr;
				CoTaskMemFree(wow64DirPtr);
			}

			PWSTR systemDirPtr = NULL;
			SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_NO_ALIAS, NULL, &systemDirPtr);
			if (systemDirPtr != NULL)
			{
				systemDir = systemDirPtr;
				CoTaskMemFree(systemDirPtr);
			}

			if ((mDebugEvent.u.LoadDll.lpImageName != 0) && (mDebugEvent.u.LoadDll.fUnicode))
			{
				addr_target strAddr = ReadMemory<addr_target>((addr_target)(intptr)mDebugEvent.u.LoadDll.lpImageName);

				for (int i = 0; i < MAX_PATH - 1; i++)
				{
					WCHAR c = ReadMemory<WCHAR>(strAddr + i*2);
					moduleNameStr[i] = (WCHAR)c;
					if (c == 0)
						break;
				}
			}

			String origModuleName = UTF8Encode(moduleNameStr);
			String moduleName = origModuleName;

			String loadMsg;
			HANDLE altFileHandle = INVALID_HANDLE_VALUE;
			if (moduleName != origModuleName)
			{
				loadMsg = StrFormat("Loading DLL: %s(%s) @ %s", origModuleName.c_str(), moduleName.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.LoadDll.lpBaseOfDll, true).c_str());
				altFileHandle = ::CreateFileW(UTF8Decode(moduleName).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			}
			else
			{
				loadMsg = StrFormat("Loading DLL: %s @ %s", moduleName.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.LoadDll.lpBaseOfDll, true).c_str());
			}

			BfLogDbg("LOAD_DLL_DEBUG_EVENT %s\n", moduleName.c_str());

			bool skipLoad = false;
#ifdef BF_DBG_32
			if (((uintptr)mDebugEvent.u.LoadDll.lpBaseOfDll & 0xFFFFFFFF00000000LL) != 0)
			{
				skipLoad = true;
				loadMsg += " - Skipped";
			}
#endif

			if (!skipLoad)
			{
				FileHandleStream stream;
				stream.mFileHandle = mDebugEvent.u.LoadDll.hFile;
				if (altFileHandle != INVALID_HANDLE_VALUE)
					stream.mFileHandle = altFileHandle;
				if (mDebugTarget->SetupDyn(moduleName, &stream, (intptr)mDebugEvent.u.LoadDll.lpBaseOfDll) == NULL)
					loadMsg += " - Failed to load";
				stream.mFileHandle = 0;
			}

			OutputMessage(loadMsg + "\n");

			if (altFileHandle != INVALID_HANDLE_VALUE)
				::CloseHandle(altFileHandle);
			::CloseHandle(mDebugEvent.u.LoadDll.hFile);

			// Try to bind any breakpoints tied to this DLL
			RehupBreakpoints(true);
			mDebugManager->mOutMessages.push_back("modulesChanged");
		}
		break;
	case UNLOAD_DLL_DEBUG_EVENT:
		{
			bool needsBreakpointRehup = false;

			String name = "???";
			DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress((addr_target)(intptr)mDebugEvent.u.UnloadDll.lpBaseOfDll);
			if (dbgModule != NULL)
			{
				name = dbgModule->mFilePath;

				for (int i = 0; i < (int)mBreakpoints.size(); i++)
				{
					auto breakpoint = mBreakpoints[i];
					auto checkBreakpoint = breakpoint;
					bool hasAddr = false;
					while (checkBreakpoint != NULL)
					{
						if ((checkBreakpoint->mAddr >= dbgModule->mImageBase) && (checkBreakpoint->mAddr < dbgModule->mImageBase + dbgModule->mImageSize))
							hasAddr = true;
						checkBreakpoint = (WdBreakpoint*)checkBreakpoint->mLinkedSibling;
					}

					if (hasAddr)
					{
						DetachBreakpoint(breakpoint);
						needsBreakpointRehup = true;
					}
				}

				bool hadTarget = mDebugTarget->mTargetBinary != NULL;
				mDebugTarget->UnloadDyn(dbgModule->mImageBase);
				if (needsBreakpointRehup)
					RehupBreakpoints(true);

				if ((mDebugTarget->mTargetBinary == NULL) && (hadTarget))
				{
					mRunState = RunState_TargetUnloaded;
				}

				mPendingDebugInfoLoad.Remove(dbgModule);
				mPendingDebugInfoRequests.Remove(dbgModule);
				mDebugManager->mOutMessages.push_back("modulesChanged");
			}

			if (!name.empty())
				OutputMessage(StrFormat("Unloading DLL: %s @ %0s\n", name.c_str(), EncodeDataPtr((addr_target)(intptr)mDebugEvent.u.UnloadDll.lpBaseOfDll, true).c_str()));

			BfLogDbg("UNLOAD_DLL_DEBUG_EVENT %s\n", name.c_str());
		}
		break;
	case OUTPUT_DEBUG_STRING_EVENT:
		{
			const int maxChars = 1024 * 1024;
			int len = BF_MIN(maxChars, (int)mDebugEvent.u.DebugString.nDebugStringLength); // 1MB max
			char* message = new char[len + 1];

			message[0] = 0;
			message[len] = 0;
			ReadMemory((addr_target)(intptr)mDebugEvent.u.DebugString.lpDebugStringData, len, message);

			if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId == mDebugEvalThreadInfo.mThreadId))
				mDebugManager->mOutMessages.push_back(String("dbgEvalMsg ") + message);
			else
				mDebugManager->mOutMessages.push_back(String("msg ") + message);

			BfLogDbg("OUTPUT_DEBUG_STRING_EVENT (BreakAddr:%@): %s\n", threadInfo->mIsAtBreakpointAddress, message);

			BF_ASSERT_REL(threadInfo->mIsAtBreakpointAddress == 0);

			delete [] message;
		}
		break;
	case CREATE_THREAD_DEBUG_EVENT:
		{
			WdThreadInfo* threadInfo = new WdThreadInfo();
			threadInfo->mProcessId = mDebugEvent.dwProcessId;
			threadInfo->mThreadId = mDebugEvent.dwThreadId;
			threadInfo->mHThread = mDebugEvent.u.CreateThread.hThread;
			threadInfo->mThreadLocalBase = mDebugEvent.u.CreateThread.lpThreadLocalBase;
			threadInfo->mStartAddress = (void*)mDebugEvent.u.CreateThread.lpStartAddress;

			BF_CONTEXT lcContext;
			lcContext.ContextFlags = BF_CONTEXT_CONTROL;
			BF_GetThreadContext(threadInfo->mHThread, &lcContext);
			threadInfo->mStartSP = BF_CONTEXT_SP(lcContext);

			mThreadMap[mDebugEvent.dwThreadId] = threadInfo;
			mDebuggerWaitingThread = threadInfo;
			mThreadList.push_back(mDebuggerWaitingThread);
			UpdateThreadDebugRegisters();
			OutputMessage(StrFormat("Creating thread %d\n", mDebugEvent.dwThreadId));
		}
		break;
	case EXIT_THREAD_DEBUG_EVENT:
		{
			OutputMessage(StrFormat("Exiting thread %d\n", mDebugEvent.dwThreadId));
			if (mSteppingThread == threadInfo)
			{
				// We were attempting stepping on this thread, but not anymore!
				ClearStep();
			}
			::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_CONTINUE);
			mIsDebuggerWaiting = false;
			mWantsDebugContinue = false;

			if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId == mDebugEvalThreadInfo.mThreadId))
			{
				// Thread terminated while evaluating! Is there a more graceful way of handling this?
				CleanupDebugEval(false);
				mRunState = RunState_Running;
			}

			mThreadList.Remove(mDebuggerWaitingThread);

			delete mDebuggerWaitingThread;
			mDebuggerWaitingThread = NULL;
			mThreadMap.Remove(mDebugEvent.dwThreadId);
			return true;
		}
		break;
	case RIP_EVENT:
		OutputMessage("RIP Event\n");
		break;
	case EXCEPTION_DEBUG_EVENT:
		{
			auto exceptionRecord = &mDebugEvent.u.Exception.ExceptionRecord;

			switch (exceptionRecord->ExceptionCode)
			{
			case STATUS_WX86_BREAKPOINT:
			case EXCEPTION_BREAKPOINT:
				{
					if (mRunState == RunState_Terminating)
					{
						BfLogDbg("Ignoring event because of RunState_Terminating\n");
						break;
					}

					mAtBreakThread = threadInfo;
					mActiveThread = mAtBreakThread;

					bool isHighAddr = false;
#ifdef BF_DBG_32
					if (((uintptr)exceptionRecord->ExceptionAddress & 0xFFFFFFFF00000000) != 0)
					{
						if (mActiveThread == mThreadList.front())
						{
							// Skip the initial Wow64 ntdll.dll!LdrpDoDebuggerBreak
							mRunState = RunState_Running;
							break;
						}
						isHighAddr = true;
					}
#endif

					addr_target pcAddress = (addr_target)(intptr)exceptionRecord->ExceptionAddress;
					if (isHighAddr)
						pcAddress = (addr_target)-1;
					//mStoppedAtAddress = pcAddress;

					bool isStepOut = false;

					if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto))
					{
						isStepOut = mStepBreakpointAddrs.Contains(pcAddress);
					}

					BF_CONTEXT lcContext;
					lcContext.ContextFlags = BF_CONTEXT_ALL;
					BF_GetThreadContext(threadInfo->mHThread, &lcContext);

					BfLogDbg("EXCEPTION_BREAKPOINT Thread:%d %p SP:%p\n", mActiveThread->mThreadId, pcAddress, BF_CONTEXT_SP(lcContext));

					uint8 origImageData = 0xCC;
					mDebugTarget->ReadOrigImageData(pcAddress, &origImageData, 1);
					bool wasDebugBreakpoint = origImageData != 0xCC;

					DbgSubprogram* dwSubprogram = NULL;
					DbgLineData* dwLineData = NULL;
					if (!isStepOut)
					{
						dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
						if (dwSubprogram == NULL)
							dwSubprogram = mDebugTarget->FindSubProgram(pcAddress, DbgOnDemandKind_LocalOnly);
					}

					bool isLineStart = (dwLineData != NULL) && (dwSubprogram->GetLineAddr(*dwLineData) == pcAddress);
					bool isNonDebuggerBreak = false;

					if (wasDebugBreakpoint)
					{
						// Go ahead and set EIP back one instruction
						BF_CONTEXT_IP(lcContext)--;
						BF_SetThreadContext(threadInfo->mHThread, &lcContext);

						if ((dwSubprogram != NULL) && (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid) &&
							(pcAddress == dwSubprogram->mBlock.mLowPC))
						{
							BfLogDbg("Hit HotReplaceKind_Invalid breakpoint\n");
							mRunState = RunState_Paused;
							mDebugManager->mOutMessages.push_back("error This lambda was replaced by a new version that has incompatible captures. A program restart is required.");
							PhysRemoveBreakpoint(pcAddress);
							break;
						}
					}
					else
					{
						// This was an actual "break" instruction
						BfLogDbg("Non-debugger break\n");
						isNonDebuggerBreak = true;

						auto prevState = mRunState;

						// Make it an "auto" stop, so for example when we have an assert/retry we won't stop inside assembly
						mRequestedStackFrameIdx = -2;

						mRunState = RunState_Paused;
						CheckNonDebuggerBreak();
						if (IsInRunState())
						{
							BF_ASSERT((prevState == RunState_Running) || (prevState == RunState_DebugEval));
							mRunState = prevState;
							break; // Continue as if nothing happened
						}

						if (prevState == RunState_DebugEval)
							mRequestedStackFrameIdx = -1; // Don't show a rolled back stack idx if a debug eval fails

						ClearStep();
					}

					if (threadInfo->mIsBreakRestorePaused)
					{
						// The thread is supposed to be paused, but the IP has been reset
						//  so just break here so we'll hit that breakpoint again once we're
						//  actually unpaused properly
						BfLogDbg("Ignoring EXCEPTION_BREAKPOINT\n", threadInfo->mThreadId);
						break;
					}

					if ((mRunState == RunState_DebugEval) || (mRunState == RunState_HotStep))
					{
						// If we hit a breakpoint while doing a debug eval, we just remove the breakpoint
						//  and expect to reinstate it during a rehup after the evaluation has completed
						WdBreakpoint* breakpoint = (WdBreakpoint*)FindBreakpointAt((uintptr_t) exceptionRecord->ExceptionAddress);
						if (breakpoint != NULL)
						{
							mNeedsRehupBreakpoints = true;
							RemoveBreakpoint(breakpoint->mLineData.GetAddress());
						}
						break;
					}

					bool isDeeper = false;

					int stepBreakAddrIdx = (int)mStepBreakpointAddrs.IndexOf(pcAddress);

					WdBreakpoint* breakpoint = NULL;
					bool ignoreBreakpoint = false;

					if ((mStepType != StepType_None) && (mSteppingThread == mAtBreakThread))
					{
						if (mStepType == StepType_ToTempBreakpoint)
						{
							RemoveTempBreakpoints();
							mRunState = RunState_Paused;
							break;
						}

						if (mContinueFromBreakpointFailed)
						{
							BfLogDbg("Continuing from ContinueFromBreakpointFailed\n");
							SetupStep(mStepType);
							mRunState = RunState_Running;
							break;
						}

						if (!isStepOut)
							breakpoint = (WdBreakpoint*)FindBreakpointAt(pcAddress);

						// Ignore breakpoint if it's on the line we're stepping off of
						if ((breakpoint != NULL) && (breakpoint->mAddr == mStepPC) &&
							(mStepSP == BF_CONTEXT_SP(lcContext)))
						{
							ignoreBreakpoint = true;
						}
						else if ((breakpoint != NULL) && (stepBreakAddrIdx == -1) && (!CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress)))
						{
							ignoreBreakpoint = true;
						}

						if ((stepBreakAddrIdx == -1) && (breakpoint == NULL) && (!isNonDebuggerBreak))
						{
							// If a breakpoint is removed in a prior thread
							BfLogDbg("Ignoring step break (old breakpoint)\n");

							if ((mSteppingThread == mAtBreakThread) && (mStepSwitchedThreads))
							{
								SetupStep(mStepType);
							}
							break;
						}

						if ((stepBreakAddrIdx != -1) && (breakpoint == NULL) && (mSteppingThread != mActiveThread))
						{
							BfLogDbg("Ignoring break (wrong thread)\n");
							ThreadRestorePause(mSteppingThread, mActiveThread);
							threadInfo->mIsAtBreakpointAddress = pcAddress;
							break;
						}

						isDeeper = mStepSP > BF_CONTEXT_SP(lcContext);
						if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto))
						{
							isDeeper = mStepSP >= BF_CONTEXT_SP(lcContext);
							BfLogDbg("StepOut Iteration SP:%p StartSP:%p IsDeeper:%d\n", BF_CONTEXT_SP(lcContext), mStepSP, isDeeper);
						}

						if (((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_ThenInto)) && (breakpoint == NULL) && (isDeeper))
						{
							// We're encountered recursion

							// Make sure we don't already have one of these stored
							BF_ASSERT(mStoredReturnValueAddr == 0);
							threadInfo->mIsAtBreakpointAddress = pcAddress;
							break; // Don't fall through, we don't want to set mIsAtBreakpointAddress
						}

						if (isStepOut)
						{
							threadInfo->mIsAtBreakpointAddress = pcAddress;

							if (mStepType == StepType_StepOut_ThenInto)
							{
								dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
								if ((dwLineData != NULL) && (pcAddress == dwSubprogram->GetLineAddr(*dwLineData)))
								{
									// Our step out from a filtered function put us at the start of a new line. Stop here
									// <do nothing>
								}
								else
								{
									// .. otherwise keep going until we get to the start of a new line
									SetupStep(StepType_StepInto);
									mRunState = RunState_Running;
									break;
								}
							}

							if (!mStepInAssembly)
							{
								// Keep stepping out until we find a frame that we have source for
								DbgSubprogram* dwSubprogram = NULL;
								DbgLineData* dwLineData = FindLineDataAtAddress(BF_CONTEXT_IP(lcContext), &dwSubprogram);
								if (dwLineData == NULL)
								{
									SetupStep(StepType_StepOut);
									break;
								}

								if ((dwLineData->mColumn == -1) && (!dwSubprogram->HasValidLines()))
								{
									// This is a method we don't actually want to be in, it has no valid lines!
									SetupStep(StepType_StepOut);
									break;
								}

								if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
								{
									// We've stepped out, but right into the start of an inlined method, so step out of this inlined method now...
									SetupStep(StepType_StepOut);
									break;
								}
							}

							ClearStep();
							mRunState = RunState_Paused;
							threadInfo->mStoppedAtAddress = pcAddress;

							break;
						}

						mRunState = RunState_Paused;
						if (breakpoint != NULL)
						{
							// While stepping we hit a legit breakpoint
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;

							// Ignore breakpoint on return statement if we're return-stepping
							mRunState = RunState_Breakpoint;
						}

						if ((mStepType == StepType_StepInto) && (dwSubprogram != NULL))
						{
							// Don't filter out the current subprogram (would break cases where we explicitly stepped into or hit breakpoint in a filtered subprogram)
							bool isInStartSubprogram = (mStepStartPC >= dwSubprogram->mBlock.mLowPC) && (mStepStartPC < dwSubprogram->mBlock.mHighPC);
							if ((!isInStartSubprogram) && (IsStepFiltered(dwSubprogram, dwLineData)))
							{
								BfLogDbg("Hit step filter\n");
								mRunState = RunState_Running;
								SetupStep(StepType_StepOut_ThenInto);
								break;
							}
						}

						if ((mStepType == StepType_StepOver) && (stepBreakAddrIdx == 0) && (mStepBreakpointAddrs[0] != 0) && (mStepBreakpointAddrs.size() > 1))
						{
							// Break was on the 'call' instruction, not the instruction after it -- means recursion
							BfLogDbg("StepOver detected recursing\n");
							mStepIsRecursing = true;
							if (mTempBreakpoint.Remove(mStepBreakpointAddrs[0]))
							{
								RemoveBreakpoint(mStepBreakpointAddrs[0]);
							}
							mStepBreakpointAddrs[0] = 0;
							mRunState = RunState_Running;
							break;
						}

						if ((mStepType == StepType_StepOver) && (stepBreakAddrIdx > 0) && (mStepBreakpointAddrs[0] != 0) && (isDeeper))
						{
							// This is the first time we've hit the target breakpoint.
							if (HasSteppedIntoCall())
							{
								mStepIsRecursing = true;
								RemoveBreakpoint(mStepBreakpointAddrs[0]);
								mStepBreakpointAddrs[0] = 0;
								//mStepBreakpointAddrs.erase(mStepBreakpointAddrs.begin());
							}
						}

						if ((mStepType == StepType_StepOver) && (mStepIsRecursing) && (stepBreakAddrIdx != -1) && (isDeeper))
						{
							// Decrement so the equality test on "step out" marks us as not being deeper when we
							//  hit the expected SP
							BfLogDbg("Converting StepOver to StepOut\n");
							mStepSP--;
							mStepType = StepType_StepOut_ThenInto;
							//SetupStep(StepType_StepOut);
							mRunState = RunState_Running;
							break;
						}

						if ((mStepType == StepType_StepOver) && (!ignoreBreakpoint) && (breakpoint == NULL) && (!mStepInAssembly))
						{
							// Test for stepping over inline method
							DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);

							// mTempBreakpoints will have 2 entries if we are on a 'call' line.  If we have an inlined call immediately following a call, then we
							//  assume we're hitting a return break
							/*if ((dwSubprogram != NULL) && (dwSubprogram->mInlineParent != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC) && (mTempBreakpoint.size() < 2))
							{
								BfLogDbg("Attempting StepOver of inlined method\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							} */

							//TODO: The previous logic with the "(mTempBreakpoint.size() < 2)" was causing Try!(Method()); stepovers to enter into Try!.  What did we mean by
							//  "assume we're hitting a return break"?
							if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
							{
								RemoveTempBreakpoints();
								BfLogDbg("Attempting StepOver of inlined method\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							}
						}

						if (mStepType == StepType_StepOut_Inline)
						{
							if (mOrigStepType == StepType_StepOver)
							{
								// For the step over, if we are still inside the source line after an inline then step over again...
								DbgSubprogram* origSubprogram = NULL;
								auto origLineData = FindLineDataAtAddress(mStepStartPC, &origSubprogram);
								DbgSubprogram* curSubprogram = NULL;
								auto curLineData = FindLineDataAtAddress(pcAddress, &curSubprogram);
								if ((origLineData != NULL) &&
									((origLineData == curLineData) ||
									 ((origSubprogram == curSubprogram) && (origLineData->mLine == curLineData->mLine))))
								{
									mRunState = RunState_Running;
									SetupStep(StepType_StepOver);
									break;
								}
							}

							ClearStep();
							break;
						}

						if ((mStepType != StepType_None) && (ignoreBreakpoint) && (!mStepInAssembly) && (stepBreakAddrIdx == -1))
						{
							// Ignore breakpoint by just continuing...
							mRunState = RunState_Running;
							break;
						}

						RemoveTempBreakpoints();

						if ((mStepType != StepType_None) && (!mStepInAssembly)  && (!isLineStart) && (stepBreakAddrIdx != -1))
						{
							SetupStep(mStepType);
							mRunState = RunState_Running;
						}
						else
						{
							//if (mStepType != StepType_Return)
							if (stepBreakAddrIdx != -1)
							{
								// Even if we've detected we're at a breakpoint, we mark ourselves as just stepping if we also
								//  have a step breakpoint here
								StepLineTryPause(pcAddress, true);
							}

							if (mRunState == RunState_Paused)
								ClearStep();
						}

						if (ignoreBreakpoint)
						{
							SetupStep(mStepType);
							mRunState = RunState_Running;
						}

						if ((mRunState == RunState_Paused) && (breakpoint != NULL))
						{
							// Just do the 'check' here so we can do the logging/condition stuff
							CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress);
						}
					}
					else
					{
						breakpoint = (WdBreakpoint*)FindBreakpointAt((uintptr_t)exceptionRecord->ExceptionAddress);
						if ((breakpoint != NULL) && (!CheckConditionalBreakpoint(breakpoint, dwSubprogram, pcAddress)))
						{
							ClearCallStack();
							BfLogDbg("Skipping conditional breakpoint. Setting mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;
							mRunState = RunState_Running;
							break;
						}
						if (breakpoint != NULL)
						{
							BfLogDbg("Breakpoint hit. mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
							threadInfo->mIsAtBreakpointAddress = breakpoint->mAddr;
							mRunState = RunState_Breakpoint;
						}
						else if ((stepBreakAddrIdx != -1) || (isNonDebuggerBreak))
						{
							if (mRunState != RunState_DebugEval)
							{
								// Was in mStepBreakpointAddrs list
								if ((isNonDebuggerBreak) || (mStepType == StepType_None) || (mSteppingThread == mAtBreakThread))
								{
									BfLogDbg("Hit mStepBreakpointAddrs breakpoint\n");
									mRunState = RunState_Paused;
								}
								else
								{
									BfLogDbg("Ignored mStepBreakpointAddrs breakpoint (wrong thread)\n");
									mRunState = RunState_Running;
								}
							}
						}
						else
						{
							BfLogDbg("Ignoring break (old or ignored breakpoint)\n");
							mRunState = RunState_Running;
						}
					}

					if ((breakpoint != NULL) && (!ignoreBreakpoint))
					{
						mActiveBreakpoint = breakpoint;
						mBreakStackFrameIdx = -1;
					}

					if ((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint))
						threadInfo->mStoppedAtAddress = pcAddress;
				}
				break;
			case STATUS_WX86_SINGLE_STEP:
			case EXCEPTION_SINGLE_STEP:
				{
					if (mRunState == RunState_Terminating)
					{
						BfLogDbg("Ignoring event because of RunState_Terminating\n");
						break;
					}

					if ((mStepSwitchedThreads) && (mActiveThread == mSteppingThread) && (mActiveThread->mIsAtBreakpointAddress != NULL))
					{
						ContinueFromBreakpoint();
						break;
					}

					if (mRunState == RunState_HotStep)
					{
						BF_ASSERT(mActiveThread == mDebuggerWaitingThread);
						mRunState = RunState_Paused;
						break;
					}

					mActiveThread = mDebuggerWaitingThread;

					BF_CONTEXT lcContext;
					lcContext.ContextFlags = BF_CONTEXT_ALL;
					BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
					addr_target pcAddress = BF_CONTEXT_IP(lcContext);

					bool wasUnfilteredStep = mStepType == StepType_StepInto_Unfiltered;
					if (mStepType == StepType_StepInto_UnfilteredSingle)
					{
						wasUnfilteredStep = true;
						mStepType = StepType_StepInto;
						mStepStartPC = pcAddress;
					}

					BfLogDbg("EXCEPTION_SINGLE_STEP Thread:%d PC:%p\n", mActiveThread->mThreadId, exceptionRecord->ExceptionAddress);

					if (lcContext.Dr6 & 0x0F) // Memory breakpoint hit
					{
						WdBreakpoint* foundBreakpoint = NULL;
						for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
						{
							if ((lcContext.Dr6 & ((intptr_target)1 << memoryWatchSlot)) != 0)
							{
								foundBreakpoint = mMemoryBreakpoints[memoryWatchSlot].mBreakpoint;
								break;
							}
						}

						BF_ASSERT(foundBreakpoint != NULL);

						DbgSubprogram* subprogram = mDebugTarget->FindSubProgram(pcAddress);
						if (CheckConditionalBreakpoint(foundBreakpoint, subprogram, pcAddress))
						{
							if (foundBreakpoint != NULL)
							{
								mDebugManager->mOutMessages.push_back(StrFormat("memoryBreak %s", EncodeDataPtr(foundBreakpoint->mMemoryBreakpointInfo->mMemoryAddress, false).c_str()));
								mRunState = RunState_Paused;
							}

							mActiveBreakpoint = foundBreakpoint;
							mBreakStackFrameIdx = -1;
							RemoveTempBreakpoints();
							BfLogDbg("Memory breakpoint hit: %p\n", foundBreakpoint);
						}
						else
							ClearCallStack();
						break;
					}

					if ((mRunState == RunState_DebugEval) && (mDebugEvalThreadInfo.mThreadId == mDebuggerWaitingThread->mThreadId))
					{
						if ((addr_target)(intptr)exceptionRecord->ExceptionAddress == mDebugEvalSetRegisters.GetPC())
						{
							// This indicates we are returning from kernel mode and our registers are clobbered
							SetRegisters(&mDebugEvalSetRegisters);
						}
						break;
					}

					bool hadBreakpointContinue = true;
					if (threadInfo->mBreakpointAddressContinuing != 0)
					{
						bool wantsBreakpoint = WantsBreakpointAt(threadInfo->mBreakpointAddressContinuing);
						BfLogDbg("Continuing breakpoint at %p WantsReset:%d\n", threadInfo->mBreakpointAddressContinuing, wantsBreakpoint);

						if (wantsBreakpoint)
						{
							PhysSetBreakpoint(threadInfo->mBreakpointAddressContinuing);
						}
						threadInfo->mBreakpointAddressContinuing = NULL;
						hadBreakpointContinue = true;

						ThreadRestoreUnpause();
					}

					if ((mSteppingThread != NULL) && (mSteppingThread != mActiveThread))
					{
						// This SINGLE_STEP happened in the wrong thread - we need the stepping thread to do the stepping!
						//  Try again.
						mActiveThread = mSteppingThread;
						SingleStepX86();
						break;
					}

					bool isDeeper = mStepSP > BF_CONTEXT_SP(lcContext);
					if ((mStepSwitchedThreads) && (mStepType == StepType_StepOver) && (isDeeper))
					{
						if (HasSteppedIntoCall())
						{
							// Since we switched threads, we needed to do a hardware step which has placed us inside a
							//  call, so we need to step out of that now...
							SetupStep(StepType_StepOut_NoFrame);
							break;
						}
					}

					// If we don't have a mStepBreakpointAddrs set, that means we're stepping through individual instructions --
					//  so process the new location here
					if (((mStepType == StepType_StepInto) || (mStepType == StepType_StepInto_Unfiltered) || (mStepType == StepType_StepOver)) && (mStepBreakpointAddrs.size() == 0))
					{
						DbgSubprogram* dwSubprogram = NULL;
						DbgLineData* dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);

						if ((dwSubprogram != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC) && (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced))
						{
							BfLogDbg("Stepping through hot thunk\n");
							mRunState = RunState_Running;
							SingleStepX86();
							break;
						}

						if ((mStepType == StepType_StepOver) && (!mStepInAssembly))
						{
							if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress == dwSubprogram->mBlock.mLowPC))
							{
								BfLogDbg("Attempting StepOver of inlined method - SingleStep\n");
								SetupStep(StepType_StepOut);
								mRunState = RunState_Running;
								break;
							}
						}

						// Column of -1 means "Illegal", keep stepping!
						if ((mStepInAssembly) ||
							((dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dwLineData->mColumn >= 0) &&
								((dwSubprogram->GetLineAddr(*dwLineData) == pcAddress) || (mStepStopOnNextInstruction))))
						{
							// Hit a line while stepping, we're done!
							mRunState = RunState_Paused;
							StepLineTryPause(pcAddress, false);
							if (mRunState == RunState_Paused)
							{
								if ((mStepType == StepType_StepInto) && (!wasUnfilteredStep) && (!mStepInAssembly) && (dwSubprogram != NULL))
								{
									// Don't filter out the current subprogram (would break cases where we explicitly stepped into or hit breakpoint in a filtered subprogram)
									bool isInStartSubprogram = (mStepStartPC >= dwSubprogram->mBlock.mLowPC) && (mStepStartPC < dwSubprogram->mBlock.mHighPC);
									if ((!isInStartSubprogram) && (IsStepFiltered(dwSubprogram, dwLineData)))
									{
										BfLogDbg("Hit step filter (2)\n");
										mRunState = RunState_Running;
										SetupStep(StepType_StepOut_ThenInto);
										break;
									}
								}

								ClearStep();
								mCurNoInfoStepTries = 0; // Reset
							}
							else
								SetupStep(mStepType);
						}
						else if (dwSubprogram != NULL)
						{
							if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && ((mStepType == StepType_StepInto) || (mStepType == StepType_StepInto_Unfiltered)))
							{
								SingleStepX86();
							}
							else
							{
								// Inside a line's instruction, keep going
								SetupStep(mStepType);
								mCurNoInfoStepTries = 0; // Reset
							}
						}
						else if (mStepType == StepType_StepInto_Unfiltered)
						{
							CPUInst inst;
							if (mDebugTarget->DecodeInstruction(pcAddress, &inst))
							{
								if (inst.IsBranch())
								{
									auto target = inst.GetTarget();
									if (target != 0)
									{
										DbgSubprogram* destSubprogram = mDebugTarget->FindSubProgram(target);
										if ((destSubprogram != NULL) && (target == destSubprogram->mBlock.mLowPC))
										{
											// We're jumping to an actual subprogram, so continue stepping here
											mStepType = StepType_StepInto_UnfilteredSingle;
											SingleStepX86();
											break;
										}
									}
								}
							}

							// We requested to step into this method so stop here even if we don't have source
							mRunState = RunState_Paused;
						}
						else
						{
							// No debug info!
							bool doStepOut = false;
							if (mCurNoInfoStepTries < 16)
							{
								mCurNoInfoStepTries++;
								BfLogDbg("NoInfoStepTries: %d\n", mCurNoInfoStepTries);
								if (!SetupStep(mStepType))
									doStepOut = true;
							}
							else
								doStepOut = true;
							if (doStepOut)
							{
								// Step out of current call.
								mStepSP = 0;
								SetupStep(StepType_StepOut_NoFrame);
								// Aggressive stepout - don't monitor BP
								mStepSP = 0;
							}
						}
					}
					else if (!hadBreakpointContinue)
					{
						BF_DBG_FATAL("EXCEPTION_SINGLE_STEP bad debugger state");
					}

					if (mRunState == RunState_Paused)
						threadInfo->mStoppedAtAddress = pcAddress;
				}
				break;
			default:
				{
					bool isSystemException =
						(exceptionRecord->ExceptionCode >= STATUS_ACCESS_VIOLATION) &&
						(exceptionRecord->ExceptionCode <= STATUS_ASSERTION_FAILURE);

					bool isFirstChance = mDebugEvent.u.Exception.dwFirstChance != 0;
					bool handled = false;

					//TODO: Use a user-defined filter here to determine whether to stop or continue
					if ((!isSystemException) && (isFirstChance))
					{
						if (exceptionRecord->ExceptionCode == 0x406D1388) // Visual C
						{
							if ((int32)exceptionRecord->ExceptionInformation[0] == 0x1000)
							{
								struct THREADNAME_INFO
								{
									DWORD dwType; // Must be 0x1000.
									LPCSTR szName; // Pointer to name (in user addr space).
									DWORD dwThreadID; // Thread ID (-1=caller thread).
									DWORD dwFlags; // Reserved for future use, must be zero.
								};

								THREADNAME_INFO* threadNameInfo = (THREADNAME_INFO*)exceptionRecord->ExceptionInformation;

								DwFormatInfo formatInfo;
								formatInfo.mRawString = true;
								String nameStr = ReadString(DbgType_SChar, (intptr)threadNameInfo->szName, false, 1024, formatInfo, false);

								WdThreadInfo* namingThreadInfo = threadInfo;
								if (threadNameInfo->dwThreadID != (DWORD)-1)
								{
									namingThreadInfo = NULL;
									mThreadMap.TryGetValue(threadNameInfo->dwThreadID, &namingThreadInfo);
								}

								if (namingThreadInfo != NULL)
								{
									namingThreadInfo->mName = nameStr;
									FilterThreadName(namingThreadInfo->mName);
								}
							}
							else if (((int32)exceptionRecord->ExceptionInformation[0] == 0x1001) && ((int32)exceptionRecord->ExceptionInformation[1] == 0x1002))
							{
								struct FailMessage
								{
									addr_target mPtr0; // Unknown
									addr_target mPtr1; // 0
									addr_target mPtr2; // 0
									addr_target mPtr3; // Unknown
									addr_target mErrorStr;
								};

								FailMessage failMessage = ReadMemory<FailMessage>(exceptionRecord->ExceptionInformation[2]);
								DwFormatInfo formatInfo;
								String failStr = ReadString(DbgType_SChar16, failMessage.mErrorStr, false, 8192, formatInfo, false);

								mDebugManager->mOutMessages.push_back(StrFormat("error Run-Time Check Failure %d - %s", exceptionRecord->ExceptionInformation[6], failStr.c_str()));
								mRunState = RunState_Paused;
								mRequestedStackFrameIdx = -2; // -2 = "auto"
								handled = true;
							}
						}

						if (!handled)
						{
							OutputMessage(StrFormat("Skipping first chance exception %08X at address %@ in thread %d\n", exceptionRecord->ExceptionCode, exceptionRecord->ExceptionAddress, threadInfo->mThreadId));
							::ContinueDebugEvent(mDebuggerWaitingThread->mProcessId, mDebuggerWaitingThread->mThreadId, DBG_EXCEPTION_NOT_HANDLED);
							mIsDebuggerWaiting = false;
						}
					}
					else
					{
						BfLogDbg("EXCEPTION in thread %d at %p\n", threadInfo->mThreadId, exceptionRecord->ExceptionAddress);
						OutputDebugStrF("EXCEPTION\n");
						mActiveThread = threadInfo;
						memcpy(&mCurException, exceptionRecord, sizeof(EXCEPTION_RECORD));

						if (mRunState == RunState_DebugEval)
						{
							if ((intptr)mCurException.ExceptionAddress == 42)
							{
								BfLogDbg("RunState_DebugEval_Done\n");
								OutputDebugStrF(" RunState_DebugEval_Done\n");
							}
							else
							{
								BfLogDbg("Exception at 0x%@ in thread %d, exception code 0x%08X",
									mCurException.ExceptionAddress, mActiveThread->mThreadId, mCurException.ExceptionCode);
								mDebugPendingExpr->mException = StrFormat("Exception at 0x%@ in thread %d, exception code 0x%08X",
									mCurException.ExceptionAddress, mActiveThread->mThreadId, mCurException.ExceptionCode);
							}
							mRunState = RunState_DebugEval_Done;

							mExplicitStopThread = mActiveThread;
							mRequestedStackFrameIdx = mDebugPendingExpr->mCallStackIdx;
						}
						else
						{
							mRunState = RunState_Exception;
						}
					}
				}
				break;
			}
		}
		break;
	}

	if ((mDebugEvalThreadInfo.mThreadId != 0) && (mRunState != RunState_DebugEval) && (mRunState != RunState_DebugEval_Done))
	{
		CleanupDebugEval();
	}

	// Stepping done?
	if (mStepType == StepType_None)
	{
		mLastValidStepIntoPC = 0;
	}

	BF_ASSERT(mDebuggerWaitingThread != NULL);
	return true;
}

void WinDebugger::Update()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

// 	if (mRunState == RunState_DebugEval)
// 		ContinueDebugEvent();
	if (mRunState == RunState_DebugEval_Done)
	{
		if (mDebugPendingExpr != NULL)
		{
			mDebugPendingExpr->mIdleTicks++;
			if (mDebugPendingExpr->mIdleTicks >= 2)
			{
				BfLogDbg("Finishing pending expr in thread %d\n", mDebugEvalThreadInfo.mThreadId);

				mRunState = RunState_Paused;
				CleanupDebugEval();
			}
		}
	}
	else if (mDebugPendingExpr != NULL)
	{
		mDebugPendingExpr->mIdleTicks = 0;
	}
}

void WinDebugger::ContinueDebugEvent()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(mRunState != RunState_DebugEval_Done);

	if (!mWantsDebugContinue)
		return;

	if (!TryRunContinue())
		return;

// 	if ((mRunState == RunState_DebugEval) && (mDebuggerWaitingThread->mThreadId != mDebugEvalThreadInfo.mThreadId))
// 	{
// 		// Don't process the 'mIsAtBreakpointAddress' stuff
// 		mWantsDebugContinue = false;
// 		mContinueEvent.Set();
// 		return;
// 	}

	if ((mDebuggerWaitingThread->mIsAtBreakpointAddress == 0) && (mDebuggerWaitingThread->mStoppedAtAddress != 0))
	{
		auto breakpoint = FindBreakpointAt(mDebuggerWaitingThread->mStoppedAtAddress);
		if (breakpoint != NULL)
		{
			mDebuggerWaitingThread->mIsAtBreakpointAddress = mDebuggerWaitingThread->mStoppedAtAddress;
		}
	}

	if (mDebuggerWaitingThread->mIsAtBreakpointAddress != 0)
	{
		if (!ContinueFromBreakpoint())
		{
			BfLogDbg("ContinueFromBreakpoint failed\n");

			ClearCallStack();
			mDebuggerWaitingThread->mStoppedAtAddress = 0;
			mDebuggerWaitingThread->mIsAtBreakpointAddress = 0;
			mWantsDebugContinue = false;
			mContinueFromBreakpointFailed = true;
			mContinueEvent.Set();
			return;
		}
	}

	if ((mRunState == RunState_Breakpoint) || (mRunState == RunState_Paused) || (mRunState == RunState_TargetUnloaded))
	{
		ClearCallStack();
		mRunState = RunState_Running;
	}

	mDebuggerWaitingThread->mStoppedAtAddress = 0;
	mWantsDebugContinue = false;
	BF_ASSERT_REL(mDebuggerWaitingThread->mIsAtBreakpointAddress == 0);
	mContinueEvent.Set();
}

static BOOL CALLBACK WdEnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	HWND owner = GetWindow(hwnd, GW_OWNER);
	if (!IsWindowVisible(hwnd))
		return TRUE;

	DWORD processId = 0;
	DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);

	if (processId != ((WinDebugger*)gDebugger)->mProcessInfo.dwProcessId)
		return TRUE;

	SetForegroundWindow(hwnd);
	return TRUE;
}

void WinDebugger::ForegroundTarget()
{
	EnumWindows(WdEnumWindowsProc, 0);
}

static int gFindLineDataAt = 0;

DbgLineData* WinDebugger::FindLineDataAtAddress(addr_target address, DbgSubprogram** outSubProgram, DbgSrcFile** outSrcFile, int* outLineIdx, DbgOnDemandKind onDemandKind)
{
	gFindLineDataAt++;

	BP_ZONE("WinDebugger::FindLineDataAtAddress");

	auto dwSubprogram = mDebugTarget->FindSubProgram((addr_target)address, onDemandKind);
	if (dwSubprogram == NULL)
		return NULL;

	FixupLineDataForSubprogram(dwSubprogram);
	auto lineData = dwSubprogram->FindClosestLine(address, outSubProgram, outSrcFile, outLineIdx);

	return lineData;
}

DbgLineData* WinDebugger::FindLineDataInSubprogram(addr_target address, DbgSubprogram* dwSubprogram)
{
	auto dwCompileUnit = dwSubprogram->mCompileUnit;

	FixupLineDataForSubprogram(dwSubprogram);
	auto lineData = dwSubprogram->FindClosestLine(address);
	return lineData;
}

bool WinDebugger::IsStepFiltered(DbgSubprogram* dbgSubprogram, DbgLineData* dbgLineData)
{
	if (mIsStepIntoSpecific)
		return false;

	if (dbgSubprogram->mStepFilterVersion != mDebugManager->mStepFilterVersion)
	{
		String filterName;
		CreateFilterName(filterName, dbgSubprogram);
		dbgSubprogram->PopulateSubprogram();

		bool doDefault = false;

		StepFilter* stepFilterPtr;
		if (mDebugManager->mStepFilters.TryGetValue(filterName, &stepFilterPtr))
		{
			switch (stepFilterPtr->mFilterKind)
			{
			case BfStepFilterKind_Default:
				doDefault = true;
				break;
			case BfStepFilterKind_Filtered:
				dbgSubprogram->mIsStepFiltered = true;
				break;
			case BfStepFilterKind_NotFiltered:
				dbgSubprogram->mIsStepFiltered = false;
				break;
			}
		}
		else
		{
			doDefault = true;
		}

		if (doDefault)
		{
			dbgSubprogram->mIsStepFiltered = dbgSubprogram->mIsStepFilteredDefault;
		}

		dbgSubprogram->mStepFilterVersion = mDebugManager->mStepFilterVersion;
	}

	if (!dbgSubprogram->mIsStepFiltered)
	{
		if (dbgLineData != NULL)
		{
			auto dbgSrcFile = dbgSubprogram->GetLineSrcFile(*dbgLineData);
			if (dbgSrcFile->mStepFilterVersion != mDebugManager->mStepFilterVersion)
			{
				dbgSrcFile->mFileExistKind = dbgSubprogram->mCompileUnit->mDbgModule->CheckSourceFileExist(dbgSrcFile->GetLocalPath());
				dbgSrcFile->mStepFilterVersion = mDebugManager->mStepFilterVersion;
			}

			switch (dbgSrcFile->mFileExistKind)
			{
			case DbgFileExistKind_NotFound:
				return true;
			case DbgFileExistKind_HasOldSourceCommand:
				if (mDebugManager->mStepOverExternalFiles)
					return true;
			}
		}
	}

	return dbgSubprogram->mIsStepFiltered;
}

void WinDebugger::RemoveTempBreakpoints()
{
	BfLogDbg("RemoveTempBreakpoints\n");

	for (auto address : mTempBreakpoint)
	{
		RemoveBreakpoint(address);
// 		if (FindBreakpointAt(address) == NULL)
// 		{
// 			RemoveBreakpoint(address);
// 		}
// 		else
// 		{
// 			BfLogDbg("Ignoring remove on temp breakpoint %p\n", address);
// 		}
	}
	mTempBreakpoint.Clear();
	mStepBreakpointAddrs.Clear();
}

void WinDebugger::RehupBreakpoints(bool doFlush)
{
	BfLogDbg("RehupBreakpoints\n");

	// First pass- detach breakpoints that need to be rebound
	for (int i = 0; i < (int)mBreakpoints.size(); i++)
	{
		auto breakpoint = mBreakpoints[i];
		while (breakpoint != NULL)
		{
			if (((breakpoint->mSrcFile != NULL) && (breakpoint->mSrcFile->mDeferredRefs.size() > 0)) ||
				(!breakpoint->mSymbolName.IsEmpty()))
			{
				// This breakpoint was already bound, but we loaded a debug module that also had this file so rebind it
				DetachBreakpoint(breakpoint);
			}

			breakpoint = (WdBreakpoint*)breakpoint->mLinkedSibling;
		}
	}

	// Second pass- actually set breakpoints
	for (int i = 0; i < (int)mBreakpoints.size(); i++)
	{
		auto breakpoint = mBreakpoints[i];
		while (breakpoint != NULL)
		{
			CheckBreakpoint(breakpoint);
 			if (breakpoint->mAddr != 0)
 				SetBreakpoint(breakpoint->mAddr, true);
			breakpoint = (WdBreakpoint*)breakpoint->mLinkedSibling;
		}
	}

	mNeedsRehupBreakpoints = false;
}

bool WinDebugger::WantsBreakpointAt(addr_target address)
{
	if (mTempBreakpoint.Contains(address))
		return true;
	for (auto breakpoint : mBreakpoints)
	{
		WdBreakpoint* checkBreakpoint = breakpoint;
		while (checkBreakpoint != NULL)
		{
			if (address == checkBreakpoint->mAddr)
				return true;
			checkBreakpoint = (WdBreakpoint*)checkBreakpoint->mLinkedSibling;
		}
	}
	return false;
}

void WinDebugger::CheckBreakpoint(WdBreakpoint* wdBreakpoint, DbgSrcFile* srcFile, int lineNum, int hotIdx)
{
	BP_ZONE("WinDebugger::CheckBreakpoint:atLoc");

	if (hotIdx == -1)
	{
		BF_ASSERT(wdBreakpoint->mPendingHotBindIdx == -1);
	}
	WdBreakpoint* headBreakpoint = wdBreakpoint;
	headBreakpoint->mPendingHotBindIdx = -1;

	bool foundInSequence = false;
	DbgSubprogram* lastFoundSubprogram = NULL;

	int highestHotIdx = -1;
	bool foundLine = false;

	int bestLineNum = -1;
	int bestLineOffset = 0x7FFFFFFF;

	auto _CheckLineInfo = [&](DbgSubprogram* dbgSubprogram, DbgLineInfo* dbgLineInfo)
	{
		// Scan first so we can determine if we want to do fix up line data or not.
		bool hasNear = false;
		int maxLineDist = 6;

		for (int lineIdx = 0; lineIdx < dbgLineInfo->mLines.mSize; lineIdx++)
		{
			auto lineData = &dbgLineInfo->mLines[lineIdx];
			auto& ctx = dbgLineInfo->mContexts[lineData->mCtxIdx];
			if (ctx.mSrcFile != srcFile)
				continue;

			int lineOffset = lineData->mLine - lineNum;
			if ((lineOffset >= 0) && (lineOffset <= maxLineDist))
				hasNear = true;
		}

		if (!hasNear)
			return;

		FixupLineDataForSubprogram(dbgSubprogram);

		for (int lineIdx = 0; lineIdx < dbgLineInfo->mLines.mSize; lineIdx++)
		{
			//TODO: Do fixup lineData... ?
			auto lineData = &dbgLineInfo->mLines[lineIdx];
			auto& ctx = dbgLineInfo->mContexts[lineData->mCtxIdx];
			if (ctx.mSrcFile != srcFile)
				continue;

// 			if (ctx.mInlinee != NULL)
// 			{
// 				if (lineIdx + 1 < dbgLineInfo->mLines.mSize)
// 				{
// 					auto nextLineData = &dbgLineInfo->mLines[lineIdx + 1];
// 					if (nextLineData->mRelAddress == lineData->mRelAddress)
// 					{
// 						// Use the later entry (same logic from DisassembleAt)
// 						continue;
// 					}
// 				}
// 			}

			if ((lineData->mColumn == -1) && (wdBreakpoint->mInstrOffset == -1))
				continue;

			int lineOffset = lineData->mLine - lineNum;

			if (lineOffset == 0)
			{
				foundLine = true;

				auto address = dbgSubprogram->GetLineAddr(*lineData);
				auto subProgram = mDebugTarget->FindSubProgram(address);
				if (subProgram->mNeedLineDataFixup)
					FixupLineDataForSubprogram(subProgram);

				if (subProgram != NULL)
					highestHotIdx = BF_MAX(highestHotIdx, subProgram->mCompileUnit->mDbgModule->mHotIdx);

				if ((foundInSequence) && (subProgram != lastFoundSubprogram))
					foundInSequence = false;

				if ((subProgram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && (address < subProgram->mBlock.mLowPC + sizeof(HotJumpOp)))
				{
					// If this breakpoint ends up on the hot jmp instruction
					continue;
				}

				if (!foundInSequence)
				{
					lastFoundSubprogram = subProgram;

					if ((subProgram != NULL) && (subProgram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) && (address == subProgram->mBlock.mLowPC))
					{
						// This instruction is actually the hot jump, we don't need a breakpoint here
						foundInSequence = true;
						continue;
					}

					if (wdBreakpoint->mSrcFile != NULL)
					{
						wdBreakpoint = new WdBreakpoint();
						// Insert at head
						wdBreakpoint->mLinkedSibling = headBreakpoint->mLinkedSibling;
						headBreakpoint->mLinkedSibling = wdBreakpoint;
						wdBreakpoint->mRequestedLineNum = headBreakpoint->mRequestedLineNum;
						wdBreakpoint->mLineNum = headBreakpoint->mLineNum;
						wdBreakpoint->mColumn = headBreakpoint->mColumn;
						wdBreakpoint->mInstrOffset = headBreakpoint->mInstrOffset;
						wdBreakpoint->mIsLinkedSibling = true;
						wdBreakpoint->mHead = headBreakpoint;
					}

					if (wdBreakpoint->mInstrOffset > 0)
					{
						for (int instIdx = 0; instIdx < wdBreakpoint->mInstrOffset; instIdx++)
						{
							CPUInst inst;
							if (!mDebugTarget->DecodeInstruction(address, &inst))
								break;
							address += inst.mSize;
						}
					}

					wdBreakpoint->mSrcFile = ctx.mSrcFile;
					wdBreakpoint->mLineData = DbgLineDataEx(lineData, subProgram);
					wdBreakpoint->mBreakpointType = BreakpointType_User;
					wdBreakpoint->mAddr = address;

					if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mStoppedAtAddress == address))
					{
						BfLogDbg("CheckBreakpoint setting mIsAtBreakpointAddress = %p\n", address);
						mDebuggerWaitingThread->mIsAtBreakpointAddress = address;
					}

					BfLogDbg("Breakpoint %p found at %s in %s\n", wdBreakpoint, subProgram->mName, GetFileName(subProgram->mCompileUnit->mDbgModule->mFilePath).c_str());

					mBreakpointAddrMap.ForceAdd(address, wdBreakpoint);
					SetBreakpoint(address);

					foundInSequence = true;
				}
			}
			else
			{
				//TODO: We didn't have this here, but if we don't have this then there are some cases where the method-closing brace generates code in
				// multiple places so we need to ensure this will break on them all
				foundInSequence = false;
			}

			if ((lineOffset >= 0) && (lineOffset <= maxLineDist) && (lineOffset <= bestLineOffset))
			{
				if (lineOffset < bestLineOffset)
				{
					bestLineNum = lineData->mLine;
					bestLineOffset = lineOffset;
				}
			}
		}
	};

	for (int pass = 0; pass < 2; pass++)
	{
		if (lineNum == -1)
			break;

		bestLineNum = -1;
		bestLineOffset = 0x7FFFFFFF;

		if (hotIdx >= 0)
		{
			if (hotIdx >= srcFile->mHotReplacedDbgLineInfo.size())
				return;

			auto hotReplacedLineInfo = srcFile->mHotReplacedDbgLineInfo[hotIdx];
			for (auto& hotReplacedEntry : hotReplacedLineInfo->mEntries)
			{
				_CheckLineInfo(hotReplacedEntry.mSubprogram, hotReplacedEntry.mLineInfo);
			}
		}
		else
		{
			for (auto subprogram : srcFile->mLineDataRefs)
				_CheckLineInfo(subprogram, subprogram->mLineInfo);
		}

		if (foundLine)
			break;

		// Don't allow the breakpoint to be inexactly bound -- only match on pass 0
		if (hotIdx != -1)
			break;

		if (bestLineNum == -1)
			break;

		lineNum = bestLineNum;
		wdBreakpoint->mLineNum = bestLineNum;
	}

	int highestCheckHotIdx = highestHotIdx - 1;
	if (hotIdx != -1)
		highestCheckHotIdx = hotIdx - 1;

	for (int hotFileIdx = highestCheckHotIdx; hotFileIdx >= 0; hotFileIdx--)
	{
		auto& hotReplacedDbgLineData = wdBreakpoint->mSrcFile->mHotReplacedDbgLineInfo;
		// Only try to bind to an old hot version if we haven't unloaded the hot module
		if ((hotFileIdx < (int)hotReplacedDbgLineData.size()) && (hotReplacedDbgLineData[hotFileIdx]->mEntries.size() > 0))
		{
			headBreakpoint->mPendingHotBindIdx = hotFileIdx;
			break;
		}
	}
}

void WinDebugger::HotBindBreakpoint(Breakpoint* breakpoint, int lineNum, int hotIdx)
{
	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	CheckBreakpoint(wdBreakpoint, wdBreakpoint->mSrcFile, lineNum, hotIdx);
}

void WinDebugger::CheckBreakpoint(WdBreakpoint* wdBreakpoint)
{
	if (!mGotStartupEvent)
		return;

	if (wdBreakpoint->mThreadId == 0) // Not bound to threadId yet...
	{
		return;
	}

	if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
	{
		if (wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap != 0)
			return;

		if (mFreeMemoryBreakIndices.size() == 0)
			return;

		if ((IsInRunState()) || (mActiveThread == NULL))
			return;

		int wantBytes[4];
		int wantBindCount = 0;
		int bytesLeft = wdBreakpoint->mMemoryBreakpointInfo->mByteCount;
		addr_target curAddr = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress;
		while (bytesLeft > 0)
		{
			if (wantBindCount >= mFreeMemoryBreakIndices.size())
				return;

			int curByteCount = 1;
#ifdef BF_DBG_64
			if ((bytesLeft >= 8) && ((curAddr & 7) == 0))
				curByteCount = 8;
			else
#endif
			if ((bytesLeft >= 4) && ((curAddr & 3) == 0))
				curByteCount = 4;
			else if ((bytesLeft >= 2) && ((curAddr & 1) == 0))
				curByteCount = 2;

			wantBytes[wantBindCount++] = curByteCount;
			bytesLeft -= curByteCount;
			curAddr += curByteCount;
		}

		addr_target curOfs = 0;
		for (int i = 0; i < wantBindCount; i++)
		{
			int memoryBreakIdx = mFreeMemoryBreakIndices.back();
			mFreeMemoryBreakIndices.pop_back();

			mMemoryBreakpoints[memoryBreakIdx].mBreakpoint = wdBreakpoint;
			mMemoryBreakpoints[memoryBreakIdx].mAddress = wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress + curOfs;
			mMemoryBreakpoints[memoryBreakIdx].mByteCount = wantBytes[i];
			mMemoryBreakpoints[memoryBreakIdx].mOfs = curOfs;
			curOfs += wantBytes[i];
			mMemoryBreakpointVersion++;

			wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap |= 1<<memoryBreakIdx;
		}

		UpdateThreadDebugRegisters();
	}

	if (wdBreakpoint->mAddr != 0)
		return;

	if (!wdBreakpoint->mSymbolName.IsEmpty())
	{
		auto headBreakpoint = wdBreakpoint->GetHeadBreakpoint();

		String symbolName = wdBreakpoint->mSymbolName;
		bool onlyBindFirst = false;
		if (symbolName.StartsWith("-"))
		{
			symbolName.Remove(0);
			onlyBindFirst = true;
		}

		for (auto dbgModule : mDebugTarget->mDbgModules)
		{
			dbgModule->ParseSymbolData();

			addr_target targetAddr = -1;
			auto entry = dbgModule->mSymbolNameMap.Find(symbolName.c_str());
			if (entry != NULL)
			{
				DbgSymbol* dwSymbol = entry->mValue;
				targetAddr = dwSymbol->mAddress;
			}

			if (targetAddr == -1)
			{
				if (symbolName == ".")
				{
					targetAddr = mDebugTarget->mLaunchBinary->mImageBase + mDebugTarget->mLaunchBinary->mEntryPoint;
					onlyBindFirst = true;
				}
			}

			if (targetAddr != -1)
			{
				if (wdBreakpoint->mAddr == 0)
				{
					wdBreakpoint->mAddr = targetAddr;
					wdBreakpoint->mBreakpointType = BreakpointType_User;
					mBreakpointAddrMap.ForceAdd(wdBreakpoint->mAddr, wdBreakpoint);
					SetBreakpoint(wdBreakpoint->mAddr);
				}
				else
				{
					wdBreakpoint = new WdBreakpoint();
					// Insert at head
					wdBreakpoint->mLinkedSibling = headBreakpoint->mLinkedSibling;
					headBreakpoint->mLinkedSibling = wdBreakpoint;
					wdBreakpoint->mSymbolName = headBreakpoint->mSymbolName;
					wdBreakpoint->mIsLinkedSibling = true;
					wdBreakpoint->mHead = headBreakpoint;
				}

				if (onlyBindFirst)
					break;
			}
		}
		return;
	}

	BP_ZONE("WinDebugger::CheckBreakpoint");

	// Rehup if we load a DLL that also uses this file we bound to (thus the mDeferredRefs check)
	if (wdBreakpoint->mSrcFile == NULL)
	{
		DbgSrcFile* srcFile = mDebugTarget->GetSrcFile(wdBreakpoint->mFilePath);
		if (srcFile == NULL)
			return;

		for (auto& deferredSrcFileRef : srcFile->mDeferredRefs)
		{
			deferredSrcFileRef.mDbgModule->ParseCompileUnit(deferredSrcFileRef.mCompileUnitId);
		}
		srcFile->mDeferredRefs.Clear();

		CheckBreakpoint(wdBreakpoint, srcFile, wdBreakpoint->mRequestedLineNum, -1);
	}
}

bool WinDebugger::IsMemoryBreakpointSizeValid(addr_target addr, int size)
{
	int wantBindCount = 0;
	int bytesLeft = size;
	addr_target curAddr = addr;

	for (int i = 0; i < 4; i++)
	{
		int curByteCount = 1;
#ifdef BF_DBG_64
		if ((bytesLeft >= 8) && ((curAddr & 7) == 0))
			curByteCount = 8;
		else
#endif
		if ((bytesLeft >= 4) && ((curAddr & 3) == 0))
			curByteCount = 4;
		else if ((bytesLeft >= 2) && ((curAddr & 1) == 0))
			curByteCount = 2;
		bytesLeft -= curByteCount;
		curAddr += curByteCount;

		if (bytesLeft == 0)
			return true;
	}

	return false;
}

bool WinDebugger::HasMemoryBreakpoint(addr_target addr, int size)
{
	for (int i = 0; i < 4; i++)
	{
		if ((mMemoryBreakpoints[i].mAddress == addr) &&
			(mMemoryBreakpoints[i].mOfs == 0) &&
			(mMemoryBreakpoints[i].mBreakpoint->mMemoryBreakpointInfo->mByteCount == size))
			return true;
	}

	return false;
}

Breakpoint* WinDebugger::CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateBreakpoint %s %d %d\n", fileName.c_str(), lineNum, wantColumn);

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mFilePath = FixPathAndCase(fileName);
	wdBreakpoint->mRequestedLineNum = lineNum;
	wdBreakpoint->mLineNum = lineNum;
	wdBreakpoint->mColumn = wantColumn;
	wdBreakpoint->mInstrOffset = instrOffset;
	mBreakpoints.push_back(wdBreakpoint);

	BfLogDbg("CreateBreakpoint Created %p\n", wdBreakpoint);

	return wdBreakpoint;
}

void WinDebugger::CheckBreakpoint(Breakpoint* checkBreakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	CheckBreakpoint((WdBreakpoint*)checkBreakpoint);
}

Breakpoint* WinDebugger::CreateMemoryBreakpoint(intptr addr, int byteCount)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateMemoryBreakpoint %p %d\n", addr, byteCount);

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();

	WdMemoryBreakpointInfo* memoryBreakInfo = new WdMemoryBreakpointInfo();
	memoryBreakInfo->mMemoryAddress = addr;
	memoryBreakInfo->mByteCount = byteCount;
	wdBreakpoint->mMemoryBreakpointInfo = memoryBreakInfo;
	mBreakpoints.push_back(wdBreakpoint);
	CheckBreakpoint(wdBreakpoint);
	return wdBreakpoint;
}

Breakpoint* WinDebugger::CreateSymbolBreakpoint(const StringImpl& symbolName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateSymbolBreakpoint %s\n", symbolName.c_str());

	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mSymbolName = symbolName;
	mBreakpoints.push_back(wdBreakpoint);
	CheckBreakpoint(wdBreakpoint);
	return wdBreakpoint;
}

Breakpoint* WinDebugger::CreateAddressBreakpoint(intptr inAddress)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("CreateAddressBreakpoint %p\n", inAddress);

	addr_target address = (addr_target)inAddress;
	WdBreakpoint* wdBreakpoint = new WdBreakpoint();
	wdBreakpoint->mAddr = address;
	mBreakpointAddrMap.ForceAdd(wdBreakpoint->mAddr, wdBreakpoint);
	SetBreakpoint(address);

	if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mStoppedAtAddress == address))
	{
		BfLogDbg("CreateAddressBreakpoint setting mIsAtBreakpointAddress = %p\n", address);
		mDebuggerWaitingThread->mIsAtBreakpointAddress = address;
	}

	mBreakpoints.push_back(wdBreakpoint);
	return wdBreakpoint;
}

void WinDebugger::DeleteBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	while (wdBreakpoint != NULL)
	{
		BfLogDbg("WinDebugger::DeleteBreakpoint %p Count:%d\n", wdBreakpoint, mBreakpoints.size());

		if (wdBreakpoint == mActiveBreakpoint)
			mActiveBreakpoint = NULL;

		if (wdBreakpoint->mCondition != NULL)
		{
			if (!wdBreakpoint->mIsLinkedSibling)
				delete wdBreakpoint->mCondition;
		}

		if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
		{
			for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
			{
				if (mMemoryBreakpoints[memoryWatchSlot].mBreakpoint == wdBreakpoint)
				{
					mFreeMemoryBreakIndices.push_back(memoryWatchSlot);
					mMemoryBreakpoints[memoryWatchSlot] = WdMemoryBreakpointBind();
					mMemoryBreakpointVersion++;
					UpdateThreadDebugRegisters();
				}
			}

			wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap = 0;
		}

		if (wdBreakpoint->mAddr != 0)
		{
			mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);
			RemoveBreakpoint(wdBreakpoint->mAddr);

			for (auto thread : mThreadList)
			{
				if (thread->mIsAtBreakpointAddress == wdBreakpoint->mAddr)
					thread->mIsAtBreakpointAddress = NULL;
				if (thread->mBreakpointAddressContinuing == wdBreakpoint->mAddr)
					thread->mBreakpointAddressContinuing = NULL;
			}
		}

		if (!wdBreakpoint->mIsLinkedSibling)
		{
			mBreakpoints.Remove(wdBreakpoint);
		}

		auto nextBreakpoint = (WdBreakpoint*)wdBreakpoint->mLinkedSibling;
		delete wdBreakpoint;

		wdBreakpoint = nextBreakpoint;
	}
}

void WinDebugger::DetachBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("WinDebugger::DetachBreakpoint %p\n", breakpoint);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	if (wdBreakpoint->mAddr != 0)
	{
		mBreakpointAddrMap.Remove(wdBreakpoint->mAddr, wdBreakpoint);
		RemoveBreakpoint(wdBreakpoint->mAddr);
		if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mIsAtBreakpointAddress == wdBreakpoint->mAddr))
			mDebuggerWaitingThread->mIsAtBreakpointAddress = NULL;
		if ((mDebuggerWaitingThread != NULL) && (mDebuggerWaitingThread->mBreakpointAddressContinuing == wdBreakpoint->mAddr))
			mDebuggerWaitingThread->mBreakpointAddressContinuing = NULL;
		wdBreakpoint->mLineData = DbgLineDataEx();
		wdBreakpoint->mAddr = 0;
	}

	if (wdBreakpoint->mCondition != NULL)
	{
		delete wdBreakpoint->mCondition->mDbgEvaluationContext;
		wdBreakpoint->mCondition->mDbgEvaluationContext = NULL;
	}

	if (wdBreakpoint->mMemoryBreakpointInfo != NULL)
	{
		for (int memoryWatchSlot = 0; memoryWatchSlot < 4; memoryWatchSlot++)
		{
			if (mMemoryBreakpoints[memoryWatchSlot].mBreakpoint == wdBreakpoint)
			{
				mFreeMemoryBreakIndices.push_back(memoryWatchSlot);
				mMemoryBreakpoints[memoryWatchSlot] = WdMemoryBreakpointBind();
				mMemoryBreakpointVersion++;
				UpdateThreadDebugRegisters();
			}
		}

		wdBreakpoint->mMemoryBreakpointInfo->mMemoryWatchSlotBitmap = 0;
	}

	if (wdBreakpoint->mLinkedSibling != NULL)
	{
		DeleteBreakpoint(wdBreakpoint->mLinkedSibling);
		wdBreakpoint->mLinkedSibling = NULL;
	}

	wdBreakpoint->mSrcFile = NULL;
	wdBreakpoint->mPendingHotBindIdx = -1;
}

void WinDebugger::MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;

	AutoCrit autoCrit(mDebugManager->mCritSect);

	DetachBreakpoint(wdBreakpoint);

	//TODO: This doesn't actually rebind correctly while the app is running
	if ((lineNum != -1) && (wantColumn != -1))
	{
		wdBreakpoint->mRequestedLineNum = lineNum;
		wdBreakpoint->mLineNum = lineNum;
		wdBreakpoint->mColumn = wantColumn;
	}
	if (rebindNow)
		CheckBreakpoint(wdBreakpoint);
}

void WinDebugger::MoveMemoryBreakpoint(Breakpoint* breakpoint, intptr addr, int byteCount)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	DetachBreakpoint(wdBreakpoint);
	wdBreakpoint->mMemoryBreakpointInfo->mMemoryAddress = addr;
	wdBreakpoint->mMemoryBreakpointInfo->mByteCount = byteCount;
	CheckBreakpoint(wdBreakpoint);
}

void WinDebugger::DisableBreakpoint(Breakpoint* breakpoint)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	DetachBreakpoint(wdBreakpoint);

	delete wdBreakpoint->mMemoryBreakpointInfo;
	wdBreakpoint->mMemoryBreakpointInfo = NULL;
}

void WinDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& conditionExpr)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);

	if (conditionExpr.empty())
	{
		delete wdBreakpoint->mCondition;
		WdBreakpoint* curBreakpoint = wdBreakpoint;
		wdBreakpoint->mCondition = NULL;
	}
	else
	{
		delete wdBreakpoint->mCondition;
		auto condition = new WdBreakpointCondition();
		condition->mExpr = conditionExpr;
		wdBreakpoint->mCondition = condition;
	}
}

void WinDebugger::SetBreakpointLogging(Breakpoint* breakpoint, const StringImpl& logging, bool breakAfterLogging)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	WdBreakpoint* wdBreakpoint = (WdBreakpoint*)breakpoint;
	BF_ASSERT(!wdBreakpoint->mIsLinkedSibling);
	wdBreakpoint->mLogging = logging;
	wdBreakpoint->mBreakAfterLogging = breakAfterLogging;
}

bool WinDebugger::CheckConditionalBreakpoint(WdBreakpoint* breakpoint, DbgSubprogram* dbgSubprogram, addr_target pcAddress)
{
	// What was this assertion for?
	//BF_ASSERT(mCallStack.size() == 0);

	auto headBreakpoint = breakpoint->GetHeadBreakpoint();

	if (headBreakpoint->mThreadId != -1)
	{
		if ((mActiveThread != NULL) && (mActiveThread->mThreadId != headBreakpoint->mThreadId))
			return false;
	}

	auto _SplitExpr = [&](const StringImpl& expr, StringImpl& outExpr, StringImpl& outSubject)
	{
		int crPos = expr.IndexOf('\n');
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

	if (headBreakpoint->mCondition != NULL)
	{
		ClearCallStack();

		auto conditional = headBreakpoint->mCondition;
		if (conditional->mDbgEvaluationContext == NULL)
		{
			CPURegisters registers;
			PopulateRegisters(&registers);
			auto pcAddress = registers.GetPC();
			DbgSubprogram* subprogram = mDebugTarget->FindSubProgram(pcAddress);
			if (subprogram == NULL)
			{
				return false;
			}

			StringT<256> expr;
			StringT<256> subjectExpr;
			if (breakpoint->mMemoryBreakpointInfo != NULL)
			{
				subjectExpr += "*";
			}

			_SplitExpr(conditional->mExpr, expr, subjectExpr);
			DbgLanguage language = DbgLanguage_Unknown;
			if (expr.StartsWith("@Beef:"))
			{
				expr.Remove(0, 6);
				language = DbgLanguage_Beef;
			}
			else if (expr.StartsWith("@C:"))
			{
				expr.Remove(0, 3);
				language = DbgLanguage_C;
			}

			conditional->mDbgEvaluationContext = new DbgEvaluationContext(this, subprogram->mCompileUnit->mDbgModule, expr);
			if (language != DbgLanguage_Unknown)
				conditional->mDbgEvaluationContext->mDbgExprEvaluator->mLanguage = language;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mSubjectExpr = subjectExpr;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mDbgCompileUnit = subprogram->mCompileUnit;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mCallStackIdx = 0;
			conditional->mDbgEvaluationContext->mDbgExprEvaluator->mExpressionFlags = (DwEvalExpressionFlags)(DwEvalExpressionFlag_AllowSideEffects);
		}

		WdStackFrame* wdStackFrame = new WdStackFrame();
		PopulateRegisters(&wdStackFrame->mRegisters);
		mCallStack.Add(wdStackFrame);
		DbgTypedValue result = conditional->mDbgEvaluationContext->EvaluateInContext(DbgTypedValue());
		ClearCallStack();

		if ((result.mType != NULL) && (result.mType->mTypeCode == DbgType_Bitfield))
			result.mType = result.mType->mTypeParam;

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
		else if (conditional->mDbgEvaluationContext->mDbgExprEvaluator->mBlockedSideEffects)
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' contained function calls, which is not allowed", conditional->mExpr.c_str()));
			return true;
		}
		else if ((!result) || (!result.mType->IsBoolean()))
		{
			mDebugManager->mOutMessages.push_back(StrFormat("error Conditional breakpoint expression '%s' must result in a boolean value", conditional->mExpr.c_str()));
			return true;
		}
		else if (!result.mBool)
			return false;
	}

	headBreakpoint->mHitCount++;
	switch (headBreakpoint->mHitCountBreakKind)
	{
	case DbgHitCountBreakKind_Equals:
		if (headBreakpoint->mHitCount != headBreakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_GreaterEquals:
		if (headBreakpoint->mHitCount < headBreakpoint->mTargetHitCount)
			return false;
		break;
	case DbgHitCountBreakKind_Multiple:
		if ((headBreakpoint->mHitCount % headBreakpoint->mTargetHitCount) != 0)
			return false;
		break;
	}

	mActiveBreakpoint = breakpoint;
	mBreakStackFrameIdx = -1;
	if (!headBreakpoint->mLogging.IsEmpty())
	{
		ClearCallStack();

		DwFormatInfo formatInfo;
		formatInfo.mCallStackIdx = 0;

		DbgCompileUnit* dbgCompileUnit = NULL;
		if (dbgSubprogram == NULL)
			dbgSubprogram = mDebugTarget->FindSubProgram(pcAddress);
		if (dbgSubprogram != NULL)
		{
			dbgCompileUnit = dbgSubprogram->mCompileUnit;
			formatInfo.mLanguage = dbgSubprogram->GetLanguage();
		}

		auto prevRunState = mRunState;
		mRunState = RunState_Paused; // We need to be paused to avoid certain errors in the eval
		String displayString;

		String expr;
		_SplitExpr(headBreakpoint->mLogging, expr, formatInfo.mSubjectExpr);
		if (expr.StartsWith("@Beef:"))
		{
			expr.Remove(0, 6);
			formatInfo.mLanguage = DbgLanguage_Beef;
		}
		else if (expr.StartsWith("@C:"))
		{
			expr.Remove(0, 3);
			formatInfo.mLanguage = DbgLanguage_C;
		}

		ProcessEvalString(dbgCompileUnit, DbgTypedValue(), expr, displayString, formatInfo, NULL, false);
		mRunState = prevRunState;

		displayString.Insert(0, "log ");
		displayString.Append("\n");

		mDebugManager->mOutMessages.push_back(displayString);

		if (!headBreakpoint->mBreakAfterLogging)
			return false;
	}

	return true;
}

void WinDebugger::CleanupDebugEval(bool restoreRegisters)
{
	BfLogDbg("CleanupDebugEval ThreadId=%d\n", mDebugEvalThreadInfo.mThreadId);

	WdThreadInfo* evalThreadInfo = NULL;
	if (mThreadMap.TryGetValue(mDebugEvalThreadInfo.mThreadId, &evalThreadInfo))
	{
		if ((restoreRegisters) && (!mDbgBreak))
		{
			SetAndRestoreValue<WdThreadInfo*> activeThread(mActiveThread, evalThreadInfo);
			RestoreAllRegisters();
// 			if (mRunState == RunState_Running_ToTempBreakpoint)
// 				mRunState = RunState_Paused;
		}

		evalThreadInfo->mStartSP = mDebugEvalThreadInfo.mStartSP;
		evalThreadInfo->mStoppedAtAddress = mDebugEvalThreadInfo.mStoppedAtAddress;
		evalThreadInfo->mIsAtBreakpointAddress = mDebugEvalThreadInfo.mIsAtBreakpointAddress;
		evalThreadInfo->mBreakpointAddressContinuing = mDebugEvalThreadInfo.mBreakpointAddressContinuing;
	}

	delete mDebugPendingExpr;
	mDebugPendingExpr = NULL;
	mDebugEvalThreadInfo = WdThreadInfo();

	OutputRawMessage("rehupLoc");
}

bool WinDebugger::FixCallStackIdx(int& callStackIdx)
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

bool WinDebugger::HasLineInfoAt(addr_target address)
{
	BP_ZONE("WinDebugger::HasLineInfoAt");

	DbgSubprogram* dbgSubprogram = NULL;
	auto dwLineData = FindLineDataAtAddress(address, &dbgSubprogram);
	return (dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dbgSubprogram->GetLineAddr(*dwLineData) == address);
}

void WinDebugger::StepLineTryPause(addr_target address, bool requireExactMatch)
{
	if (mStepInAssembly)
		return;

	if (mStepLineData.mLineData != NULL)
	{
		DbgSubprogram* dbgSubprogram = NULL;
		DbgSrcFile* dbgSrcFile = NULL;
		auto dwLineData = FindLineDataAtAddress(address, &dbgSubprogram, &dbgSrcFile);
		if ((dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && ((!requireExactMatch) || (dbgSubprogram->GetLineAddr(*dwLineData) == address)))
		{
			// "Invalid" line
			if (dwLineData->mColumn == -1)
			{
				SetupStep(mStepType);
				mRunState = RunState_Running;
				return;
			}

			// If we're on the same line but a different column or a <= address then keep it keep looking
			if ((dbgSrcFile == mStepLineData.GetSrcFile()) &&
				((!requireExactMatch) || (dwLineData != mStepLineData.mLineData) || (address <= mStepStartPC)) &&
				(dwLineData->mLine == mStepLineData.mLineData->mLine))
			{
				SetupStep(mStepType);
				mRunState = RunState_Running;
				return;
			}
		}
	}

	mRunState = RunState_Paused;
}

void WinDebugger::BreakAll()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	::DebugBreakProcess(mProcessInfo.hProcess);
}

void WinDebugger::StepInto(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!TryRunContinue())
		return;

	BfLogDbg("StepInto\n");

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepInto);
	ContinueDebugEvent();
}

void WinDebugger::StepIntoSpecific(intptr inAddr)
{
	addr_target addr = (addr_target)inAddr;

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!TryRunContinue())
		return;

	BfLogDbg("StepIntoSpecific %p\n", addr);

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = false;

	SetupStep(StepType_StepInto);
	mIsStepIntoSpecific = true;
	mStepType = StepType_StepInto_Unfiltered;
	if (mStepStartPC != addr)
	{
		RemoveTempBreakpoints();
		SetTempBreakpoint(addr);
		mStepBreakpointAddrs.push_back(addr);
	}

	ContinueDebugEvent();
}

void WinDebugger::PushValue(CPURegisters* registers, int64 val)
{
	addr_target* regSP = registers->GetSPRegisterRef();
	*regSP -= sizeof(addr_target);
	WriteMemory<addr_target>(*regSP, (addr_target)val);
}

void WinDebugger::PushValue(CPURegisters* registers, const DbgTypedValue& typedValue)
{
	addr_target* regSP = registers->GetSPRegisterRef();

	int byteCount = typedValue.mType->GetByteCount();
	if ((byteCount == 8) || (sizeof(addr_target) == 8))
	{
		*regSP -= sizeof(int64);

		addr_target val = typedValue.mInt64;
		if (typedValue.mType->IsCompositeType())
			val = typedValue.mSrcAddress;
		WriteMemory<int64>(*regSP, val);
	}
	else
	{
		*regSP -= sizeof(int32);
		addr_target val = typedValue.mInt32;
		if (typedValue.mType->IsCompositeType())
			val = typedValue.mSrcAddress;
		WriteMemory<int32>(*regSP, val);
	}
}

void WinDebugger::SetThisRegister(CPURegisters* registers, addr_target val)
{
#if BF_DBG_32
	registers->mIntRegs.ecx = val;
#else
	registers->mIntRegs.rcx = val;
#endif
}

void WinDebugger::AddParamValue(int paramIdx, bool hadThis, CPURegisters* registers, const DbgTypedValue& typedValue)
{
#if BF_DBG_32
	PushValue(registers, typedValue);
#else
	int regIdx = paramIdx + (hadThis ? 1 : 0);
	if (typedValue.mType->IsFloat())
	{
		PushValue(registers, typedValue);
		if (regIdx < 4)
		{
			if (typedValue.mType->mTypeCode == DbgType_Single)
			{
				registers->mXmmRegsArray[regIdx].f[0] = typedValue.mSingle;
			}
			else
			{
				registers->mXmmDRegsArray[regIdx].d[0] = typedValue.mDouble;
			}
		}
	}
	else
	{
		PushValue(registers, typedValue);
		if (regIdx < 4)
		{
			int64 val;
			if (typedValue.mType->IsCompositeType())
            	val = typedValue.mSrcAddress;
			else
				val = typedValue.mPtr;
			if (regIdx == 0)
				registers->mIntRegs.rcx = val;
			else if (regIdx == 1)
				registers->mIntRegs.rdx = val;
			else if (regIdx == 2)
				registers->mIntRegs.r8 = val;
			else if (regIdx == 3)
				registers->mIntRegs.r9 = val;
		}
	}
#endif
}

bool WinDebugger::CheckNeedsSRetArgument(DbgType* retType)
{
	if (!retType->IsCompositeType())
		return false;

	if (retType->GetByteCount() == 0)
		return false;

	//TODO: Change when we change the calling convention
	if (retType->GetLanguage() == DbgLanguage_Beef)
		return true;

	int retSize = retType->GetByteCount();
	//TODO: Check for 'POD' type?
	if ((retSize == 1) || (retSize == 2) || (retSize == 4) || (retSize == sizeof(addr_target)))
		return false;
	return true;
}

DbgTypedValue WinDebugger::ReadReturnValue(CPURegisters* registers, DbgType* type)
{
	DbgTypedValue retValue;

	if (type->IsFloat())
	{
		retValue.mType = type;
#if BF_DBG_32
		retValue.mDouble = ConvertFloat80ToDouble(registers->mFpMmRegsArray[0].fp.fp80);
		if (type->mSize == 4)
			retValue.mSingle = (float)retValue.mDouble;
#else
		if (retValue.mType->mTypeCode == DbgType_Single)
			retValue.mSingle = registers->mXmmRegsArray[0].f[0];
		else
			retValue.mDouble = registers->mXmmDRegsArray[0].d[0];
#endif
	}
	else if (type->IsCompositeType())
	{
		retValue.mType = type;
		if (CheckNeedsSRetArgument(type))
		{
#ifdef BF_DBG_32
			retValue.mSrcAddress = mSavedContext.Esp - BF_ALIGN(type->GetByteCount(), 16);
#else
			retValue.mSrcAddress = mSavedContext.Rsp - BF_ALIGN(type->GetByteCount(), 16);
#endif
		}
		else
		{
#ifdef BF_DBG_32
			retValue.mInt32 = mSavedContext.Eax;
#else
			retValue.mInt64 = mSavedContext.Rax;
#endif
		}
	}
	else
	{
#ifdef BF_DBG_32
		retValue.mType = type;
		retValue.mInt32 = registers->mIntRegs.eax;
		if (type->mSize == 8)
			(&retValue.mInt32)[1] = registers->mIntRegs.edx;
#else
		retValue.mType = type;
		retValue.mInt64 = registers->mIntRegs.rax;
#endif
		return retValue;
	}

	return retValue;
}

bool WinDebugger::SetRegisters(CPURegisters* registers)
{
	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_CONTROL | BF_CONTEXT_INTEGER | BF_CONTEXT_FLOATING_POINT | BF_CONTEXT_EXTENDED_REGISTERS | BF_CONTEXT_SEGMENTS;
	lcContext.ContextFlags |= BF_CONTEXT_EXCEPTION_REQUEST;

	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

#ifdef BF_DBG_32
	lcContext.Eax = registers->mIntRegs.eax;
	lcContext.Ecx = registers->mIntRegs.ecx;
	lcContext.Edx = registers->mIntRegs.edx;
	lcContext.Ebx = registers->mIntRegs.ebx;
	lcContext.Esp = registers->mIntRegs.esp;
	lcContext.Ebp = registers->mIntRegs.ebp;
	lcContext.Esi = registers->mIntRegs.esi;
	lcContext.Edi = registers->mIntRegs.edi;
	lcContext.Eip = registers->mIntRegs.eip;
	lcContext.EFlags = registers->mIntRegs.efl;

	BF_ASSERT(sizeof(lcContext.FloatSave.RegisterArea) == sizeof(registers->mFpMmRegsArray));
	memcpy(lcContext.FloatSave.RegisterArea, registers->mFpMmRegsArray, sizeof(lcContext.FloatSave.RegisterArea));

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 32*sizeof(float));
	memcpy(&lcContext.ExtendedRegisters[160], registers->mXmmRegsArray, sizeof(registers->mXmmRegsArray));
#else
	lcContext.Rax = registers->mIntRegs.rax;
	lcContext.Rcx = registers->mIntRegs.rcx;
	lcContext.Rdx = registers->mIntRegs.rdx;
	lcContext.Rbx = registers->mIntRegs.rbx;
	lcContext.Rsp = registers->mIntRegs.rsp;
	lcContext.Rbp = registers->mIntRegs.rbp;
	lcContext.Rsi = registers->mIntRegs.rsi;
	lcContext.Rdi = registers->mIntRegs.rdi;
	lcContext.Rip = registers->mIntRegs.rip;
	lcContext.EFlags = (DWORD)registers->mIntRegs.efl;

	lcContext.R8 = registers->mIntRegs.r8;
	lcContext.R9 = registers->mIntRegs.r9;
	lcContext.R10 = registers->mIntRegs.r10;
	lcContext.R11 = registers->mIntRegs.r11;
	lcContext.R12 = registers->mIntRegs.r12;
	lcContext.R13 = registers->mIntRegs.r13;
	lcContext.R14 = registers->mIntRegs.r14;
	lcContext.R15 = registers->mIntRegs.r15;

	for (int i = 0; i < 8; i++)
	{
		memcpy(&lcContext.FltSave.FloatRegisters[i], &registers->mFpMmRegsArray[i], 10);
	}

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 64 * sizeof(float));
	memcpy(BF_CONTEXT_XMMDATA(lcContext), registers->mXmmRegsArray, sizeof(registers->mXmmRegsArray));
#endif

	//lcContext.ContextFlags |= BF_CONTEXT_EXCEPTION_REQUEST;

	BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

	return (lcContext.ContextFlags & (BF_CONTEXT_EXCEPTION_ACTIVE | BF_CONTEXT_SERVICE_ACTIVE)) == 0;
}

void WinDebugger::SaveAllRegisters()
{
	BfLogDbg("SaveAllRegisters setting mSavedAtBreakpointAddress = %p\n", mActiveThread->mIsAtBreakpointAddress);
	mSavedAtBreakpointAddress = mActiveThread->mIsAtBreakpointAddress;
	mSavedBreakpointAddressContinuing = mActiveThread->mBreakpointAddressContinuing;
	mSavedContext.ContextFlags = BF_CONTEXT_ALL;
	BF_GetThreadContext(mActiveThread->mHThread, &mSavedContext);
}

void WinDebugger::RestoreAllRegisters()
{
	BfLogDbg("RestoreAllRegisters setting mIsAtBreakpointAddress = %p\n", mSavedAtBreakpointAddress);
	mActiveThread->mIsAtBreakpointAddress = mSavedAtBreakpointAddress;
	mActiveThread->mBreakpointAddressContinuing = mSavedBreakpointAddressContinuing;
	BF_SetThreadContext(mActiveThread->mHThread, &mSavedContext);

#ifdef BF_DBG_32
	//TODO: Find the test that this was required for...
// 	if (mActiveThread->mIsAtBreakpointAddress == mSavedContext.Eip)
// 	{
// 		if (mRunState == RunState_Running_ToTempBreakpoint)
// 			mRunState = RunState_Paused;
// 	}
// 	else
// 	{
// 		SetTempBreakpoint(mSavedContext.Eip);
// 		mRunState = RunState_Running_ToTempBreakpoint;
// 		mStepType = StepType_ToTempBreakpoint;
// 		mSteppingThread = mActiveThread;
// 	}
#endif
}

void WinDebugger::OutputMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back("msg " + msg);
}

void WinDebugger::OutputRawMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back(msg);
}

void WinDebugger::SetRunState(RunState runState)
{
	mRunState = runState;
}

bool WinDebugger::TryRunContinue()
{
	if (mRunState == RunState_Exception)
	{
		mIsContinuingFromException = true;
		mRunState = RunState_Paused;
	}

	if (((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint)) && (mNeedsRehupBreakpoints))
		RehupBreakpoints(true);

	return true;
}

void WinDebugger::ClearStep()
{
	BfLogDbg("ClearStep\n");

	RemoveTempBreakpoints();
	mOrigStepType = StepType_None;
	mStepType = StepType_None;
	mStepStartPC = 0;
	mStepSP = 0;
	mStepPC = 0;
	mIsStepIntoSpecific = false;
	mStepIsRecursing = false;
	mStepStopOnNextInstruction = false;
	mStepLineData = DbgLineDataEx();
}

bool WinDebugger::SetupStep(StepType stepType)
{
	BP_ZONE("SetupStep");

	RemoveTempBreakpoints();
	if (mNeedsRehupBreakpoints)
		RehupBreakpoints(true);

	if (mOrigStepType == StepType_None)
		mOrigStepType = stepType;
	mStepType = stepType;
	mSteppingThread = mActiveThread;
	mStepSwitchedThreads = false;
	mContinueFromBreakpointFailed = false;

	CPURegisters registers;
	PopulateRegisters(&registers);
	addr_target pcAddress = registers.GetPC();

	if (mStepLineData.IsNull())
	{
		DbgSubprogram* dbgSubprogram = NULL;
		auto dbgLineData = FindLineDataAtAddress(pcAddress, &dbgSubprogram);
		mStepLineData = DbgLineDataEx(dbgLineData, dbgSubprogram);
		mStepStartPC = registers.GetPC();
	}

	bool isDeeper = mStepSP > registers.GetSP();

	BfLogDbg("SetupStep %d PC:%p SP:%p StepStartSP:%p Thread:%d\n", stepType, (addr_target)registers.GetPC(), (addr_target)registers.GetSP(), (addr_target)mStepSP, mSteppingThread->mThreadId);

	mStepSP = registers.GetSP();
	mStepPC = registers.GetPC();

	if ((mStepType == StepType_StepOut) || (mStepType == StepType_StepOut_NoFrame) || (mStepType == StepType_StepOut_ThenInto))
	{
		if (mStepType != StepType_StepOut_NoFrame)
		{
			// Test for stepping out of an inline method
			DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);
			if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL))
			{
				DbgSubprogram* topSubprogram = dwSubprogram->GetRootInlineParent();

				if ((mOrigStepType == StepType_StepInto) || (mOrigStepType == StepType_StepInto_Unfiltered))
				{
					mStepType = mOrigStepType;
				}
				else
				{
					mStepType = StepType_StepOut_Inline;
					// Set up pcAddress to detect recursion
					//TODO: We can't set a physical breakpoint here because we will immediately hit it when attempting to step over an inlined method.
					//  An inlined method can't recurse anyway, but store the pcAddress in mTempBreakpoints because we still check that for recursion
					// SetTempBreakpoint(pcAddress);
					//mTempBreakpoint.push_back(pcAddress);
					mStepBreakpointAddrs.push_back(pcAddress);
				}

				addr_target endAddress = dwSubprogram->mBlock.mHighPC;
				if (dwSubprogram->mHasLineAddrGaps)
				{
					// Keep bumping out the address as long as we can find lines that contain the nextPC
					addr_target nextAddr = pcAddress;
					for (auto& lineInfo : topSubprogram->mLineInfo->mLines)
					{
						auto lineAddr = topSubprogram->GetLineAddr(lineInfo);
						if ((nextAddr >= lineAddr) && (nextAddr < lineAddr + lineInfo.mContribSize))
						{
							auto ctx = topSubprogram->mLineInfo->mContexts[lineInfo.mCtxIdx];
							if (ctx.mInlinee == dwSubprogram)
							{
								nextAddr = lineAddr + lineInfo.mContribSize;
							}
						}
					}
					if (nextAddr != pcAddress)
						endAddress = nextAddr;
				}

				BfLogDbg("Stepping out of inlined method, end address: %p\n", endAddress);
				SetTempBreakpoint(endAddress);
				mStepBreakpointAddrs.push_back(endAddress);

				addr_target decodeAddress = dwSubprogram->mBlock.mLowPC;
				while (decodeAddress < endAddress)
				{
					CPUInst inst;
					if (!mDebugTarget->DecodeInstruction(decodeAddress, &inst))
						break;

					addr_target targetAddress = inst.GetTarget();
					// We need to find a targetAddress
					if ((targetAddress != 0) &&
						!((targetAddress >= dwSubprogram->mBlock.mLowPC) && (targetAddress < dwSubprogram->mBlock.mHighPC)) &&
						((targetAddress >= topSubprogram->mBlock.mLowPC) && (targetAddress < topSubprogram->mBlock.mHighPC)))
					{
						BfLogDbg("Stepping out of inlined method, branch address: %p\n", targetAddress);
						SetTempBreakpoint(targetAddress);
						mStepBreakpointAddrs.push_back(targetAddress);
					}

					decodeAddress += inst.GetLength();
				}

				return true;
			}
		}

		if ((mStepType != StepType_StepOut_NoFrame) && (RollBackStackFrame(&registers, true)))
		{
			bool isStackAdjust = false;
			DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress);
			if (dwSubprogram != NULL)
			{
				if ((strcmp(dwSubprogram->mName, "_chkstk") == 0) ||
					(strcmp(dwSubprogram->mName, "__chkstk") == 0) ||
					(strcmp(dwSubprogram->mName, "_alloca_probe") == 0))
					isStackAdjust = true;
			}

			pcAddress = registers.GetPC();
			if (isStackAdjust)
			{
				// We set it to zero so we never detect an "isDeeper" condition which would skip over the return-location breakpoint
				mStepSP = 0;
			}
			else
			{
				addr_target oldAddress = pcAddress;

				CPUInst inst;
				while (true)
				{
					if (mStepInAssembly)
						break;
					if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
						break;
					if ((inst.IsBranch()) || (inst.IsCall()) || (inst.IsReturn()))
						break;
#ifdef BF_DBG_32
					if (!inst.StackAdjust(mStepSP))
						break;
#endif
					DbgSubprogram* checkSubprogram = NULL;
					auto checkLineData = FindLineDataAtAddress(pcAddress, &checkSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
					if (checkLineData == NULL)
						break;
					if (checkSubprogram->GetLineAddr(*checkLineData) == pcAddress)
						break;
					pcAddress += inst.GetLength();
				}

				if (pcAddress != oldAddress)
				{
					BfLogDbg("Adjusting stepout address from %p to %p\n", oldAddress, pcAddress);
				}
			}

			BfLogDbg("SetupStep Stepout SetTempBreakpoint %p\n", pcAddress);
			SetTempBreakpoint(pcAddress);
			mStepBreakpointAddrs.push_back(pcAddress);
			if (mStepType != StepType_StepOut_ThenInto)
				mStepType = StepType_StepOut;
		}
		else
		{
			// Try to handle the case where we just entered this call so the return address is the first entry on the stack
			addr_target* regSP = registers.GetSPRegisterRef();
			pcAddress = ReadMemory<addr_target>(*regSP);
			*regSP += sizeof(addr_target);
			if (mDebugTarget->FindSubProgram(pcAddress) != NULL)
			{
				BfLogDbg("SetupStep Stepout SetTempBreakpoint (2) %p\n", pcAddress);

				SetTempBreakpoint(pcAddress);
				mStepBreakpointAddrs.push_back(pcAddress);
				if (mOrigStepType == StepType_StepInto)
					mStepType = StepType_StepInto;
				else
					mStepType = StepType_StepOver;
				return true;
			}
			else
			{
				// Just do stepovers until we eventually step out
				//BF_DBG_FATAL("StepOut Failed");
				BfLogDbg("StepOut Failed\n");

				if (mLastValidStepIntoPC != 0)
				{
					BfLogDbg("Using mLastValidStepIntoPC: %p\n", mLastValidStepIntoPC);
					if (mOrigStepType == StepType_StepInto)
						mStepType = StepType_StepInto;
					else
						mStepType = StepType_StepOver;
					SetTempBreakpoint(mLastValidStepIntoPC);
					mStepBreakpointAddrs.push_back(0);
					mStepBreakpointAddrs.push_back(mLastValidStepIntoPC);
					mLastValidStepIntoPC = 0;
					return true;
				}
				else
				{
					BfLogDbg("Stopping");
					mStepType = StepType_None;
					mRunState = RunState_Paused;
					return true;
				}
			}
		}
	}

	if ((mStepType != StepType_StepOut) && (mStepType != StepType_StepOut_ThenInto))
	{
		if (mDebuggerWaitingThread != mSteppingThread)
		{
			// We've switched threads, so there's a possible race condition:
			//  This new thread may already have an EXCEPTION_BREAKPOINT queued up so the PC is actually
			//  located one byte past the BREAK instruction, which is one byte into whatever instruction
			//  was previously there.  We can't insert normal BREAK instructions because we don't know
			//  if the current PC is actually at an instruction start, so we do a single step with a
			//  slower stack call check to see if we need to step out after a "step over"
			BfLogDbg("Step - switched threads mIsAtBreakpointAddress:%p\n", mSteppingThread->mIsAtBreakpointAddress);
			mStepSwitchedThreads = true;
			SingleStepX86();
			return true;
		}

		bool breakOnNext = false;

		int instIdx = 0;
		for (instIdx = 0; true; instIdx++)
		{
			bool isAtLine = false;
			DbgSubprogram* dwSubprogram = NULL;

			auto dwLineData = FindLineDataAtAddress(pcAddress, &dwSubprogram, NULL, NULL, DbgOnDemandKind_LocalOnly);
			isAtLine = (instIdx > 0) && (dwLineData != NULL) && (dwLineData->IsStackFrameSetup()) && (dwSubprogram->GetLineAddr(*dwLineData) == pcAddress);

			// "Never step into" line
			if ((dwLineData != NULL) && (dwLineData->mColumn == -2) && (stepType == StepType_StepInto))
				stepType = StepType_StepOver;

			CPUInst inst;
			if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
			{
				BfLogDbg("Decode failed, set up SingleStepX86 %p\n", pcAddress);
				SingleStepX86();
				mStepStopOnNextInstruction = true;
				return true;
			}

			if (instIdx > 256)
			{
				BfLogDbg("Too many SetupStep iterations");
				breakOnNext = true;
			}

			if ((inst.IsReturn()) && (instIdx == 0) && (!mStepInAssembly))
			{
				// Do actual STEP OUT so we set up proper "stepping over unimportant post-return instructions"
				if (stepType == StepType_StepInto)
					return SetupStep(StepType_StepOut_ThenInto);
				else
					return SetupStep(StepType_StepOut);
			}

			if ((breakOnNext) || (mStepInAssembly) || (isAtLine) || (inst.IsBranch()) || (inst.IsCall()) || (inst.IsReturn()))
			{
				if (((instIdx == 0) || (mStepInAssembly)) && (!breakOnNext))
				{
					if ((stepType == StepType_StepOver) && (inst.IsCall()))
					{
						// Continue - sets a breakpoint on the call line to detect recursion.
						//  The next loop through will set a breakpoint on the line after the return
						BfLogDbg("StepHadCall\n");
						breakOnNext = true;

						BfLogDbg("StepHadCall setting mIsAtBreakpointAddress = %p\n", pcAddress);
						mSteppingThread->mIsAtBreakpointAddress = pcAddress;

						SetTempBreakpoint(pcAddress);
						mStepBreakpointAddrs.push_back(pcAddress);
					}
					else
					{
						if (inst.IsCall())
						{
							if ((mLastValidStepIntoPC == 0) || (dwSubprogram != NULL))
								mLastValidStepIntoPC = pcAddress + inst.mSize;
						}

						if ((dwLineData != NULL) && (inst.IsBranch()))
						{
							addr_target targetAddr = inst.GetTarget();
							if (targetAddr < dwSubprogram->GetLineAddr(*dwLineData))
							{
								// Jumping backwards, stop at next instruction
								mStepStopOnNextInstruction = true;
							}
						}

						bool isPrefixOnly = false;
						if ((mStepInAssembly) && (stepType == StepType_StepOver) && (inst.IsRep(isPrefixOnly)))
						{
							if (isPrefixOnly)
							{
								CPUInst nextInst;
								if (mDebugTarget->DecodeInstruction(pcAddress + inst.GetLength(), &nextInst))
								{
									if (nextInst.IsBranch())
									{
										// repne jmp - this appears in __chkstk (for example)
										// We don't have a good way to "step over" this one, so just do a single step
									}
									else
									{
										// Step over the rep + target instruction
										auto doneAddr = pcAddress + inst.GetLength() + nextInst.GetLength();
										BfLogDbg("SetupStep SetTempBreakpoint %p\n", doneAddr);
										SetTempBreakpoint(doneAddr);
										mStepBreakpointAddrs.push_back(doneAddr);
										break;
									}
								}
							}
							else
							{
								// Step over the instruction
								auto doneAddr = pcAddress + inst.GetLength();
								BfLogDbg("SetupStep SetTempBreakpoint %p\n", doneAddr);
								SetTempBreakpoint(doneAddr);
								mStepBreakpointAddrs.push_back(doneAddr);
								break;
							}
						}

						// Just step a single instruction
						BfLogDbg("SetupStep SingleStepX86 %p\n", pcAddress);
						SingleStepX86();
						if (inst.IsReturn())
							mStepStopOnNextInstruction = true;
						break;
					}
				}
				else
				{
					// Move us to this instruction so we can hardware single-step into it
					BfLogDbg("SetupStep SetTempBreakpoint %p\n", pcAddress);
					SetTempBreakpoint(pcAddress);
					mStepBreakpointAddrs.push_back(pcAddress);
					break;
				}
			}

			// Not an interesting instruction - move to next
			pcAddress += inst.mSize;

			if ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL) && (pcAddress >= dwSubprogram->mBlock.mHighPC))
			{
				auto endAddress = dwSubprogram->mBlock.mHighPC;
				BfLogDbg("Stepping past end of inlined method, end address: %p\n", endAddress);

				mStepType = StepType_StepOut_Inline;
				SetTempBreakpoint(endAddress);
				mStepBreakpointAddrs.push_back(endAddress);
				return true;
			}
		}

		if (instIdx > 1)
			BfLogDbg("SetupStep instIdx: %d\n", instIdx);
	}

	return true;
}

void WinDebugger::CheckNonDebuggerBreak()
{
	enum MessageType
	{
		MessageType_None = 0,
		MessageType_Error = 1,
		MessageType_ProfilerCmd = 2
	};

	CPURegisters registers;
	PopulateRegisters(&registers);
	addr_target pcAddress = registers.GetPC();

	addr_target debugMessageDataAddr = (addr_target)-1;
	if (mDebugTarget->mTargetBinary != NULL)
	{
		mDebugTarget->mTargetBinary->ParseSymbolData();
		debugMessageDataAddr = mDebugTarget->FindSymbolAddr("gBfDebugMessageData");
	}
	if (debugMessageDataAddr != (addr_target)-1)
	{
		struct BfDebugMessageData
		{
			int mMessageType; // 0 = none, 1 = error
			int mStackWindbackCount;
			int mBufParamLen;
			addr_target mBufParam;
			addr_target mPCOverride;
		};
		BfDebugMessageData messageData = ReadMemory<BfDebugMessageData>(debugMessageDataAddr);
		WriteMemory<int>(debugMessageDataAddr, 0); // Zero out type so we won't trigger again
		if (messageData.mMessageType != 0)
		{
			llvm::SmallVector<char, 4096> strBuf;
			int strLen = messageData.mBufParamLen;
			strBuf.resize(strLen + 1);

			char* str = &strBuf[0];
			str[strLen] = 0;
			if (ReadMemory(messageData.mBufParam, strLen, str))
			{
				if (messageData.mMessageType == MessageType_Error)
				{
					mRequestedStackFrameIdx = messageData.mStackWindbackCount;
					if (messageData.mPCOverride != 0)
					{
						mShowPCOverride = messageData.mPCOverride;
						mRequestedStackFrameIdx = -2;
					}
					mDebugManager->mOutMessages.push_back(StrFormat("error %s", str));
				}
				else if (messageData.mMessageType == MessageType_ProfilerCmd)
				{
					// It's important to set this here, because we unlock the critSect during StopSampling and we can't have the
					//  IDE thinking that we're actually paused when it checks the mRunState
					mRunState = RunState_Running;

					char* cmd = strtok(str, "\t");
					if (strcmp(cmd, "StartSampling") == 0)
					{
						char* sessionIdStr = strtok(NULL, "\t");
						char* threadIdStr = strtok(NULL, "\t");
						char* sampleRateStr = strtok(NULL, "\t");
						char* descStr = strtok(NULL, "\t");

						if (threadIdStr != NULL)
						{
							int threadId = atoi(threadIdStr);
							int sampleRate = atoi(sampleRateStr);
							int sessionId = atoi(sessionIdStr);

							Profiler** profilerPtr;
							if (mPendingProfilerMap.TryAdd(sessionId, NULL, &profilerPtr))
							{
								DbgProfiler* profiler = new DbgProfiler(this);
								if (descStr != NULL)
									profiler->mDescription = descStr;
								if (sampleRate > 0)
									profiler->mSamplesPerSecond = sampleRate;
								profiler->Start();
								*profilerPtr = profiler;

								mDebugManager->mOutMessages.push_back("newProfiler");
								mNewProfilerList.push_back(profiler);
							}
						}
					}
					else if (strcmp(cmd, "StopSampling") == 0)
					{
						char* sessionIdStr = strtok(NULL, "\t");
						if (sessionIdStr != NULL)
						{
							int sessionId = atoi(sessionIdStr);

							Profiler* profiler;
							if (mPendingProfilerMap.Remove(sessionId, &profiler))
							{
								if (profiler->IsSampling())
								{
									// Need to unlock so we don't deadlock
									mDebugManager->mCritSect.Unlock();
									profiler->Stop();
									mDebugManager->mCritSect.Lock();
								}
							}
						}
					}
					else if (strcmp(cmd, "ClearSampling") == 0)
					{
						for (auto& kv : mPendingProfilerMap)
						{
							auto profiler = kv.mValue;
							profiler->Clear();
						}
					}
					else if (strcmp(cmd, "ClearOutput") == 0)
					{
						mDebugManager->mOutMessages.push_back("clearOutput");
					}
				}
				return;
			}
		}
	}

	intptr_target objAddr;
	auto dbgBreakKind = mDebugTarget->GetDbgBreakKind(pcAddress, &registers, &objAddr);
	if (dbgBreakKind == DbgBreakKind_ObjectAccess)
	{
		String errorStr = "error Attempted to access deleted object";
		String objectAddr = EncodeDataPtr((addr_target)objAddr, true);
		errorStr += StrFormat("\x1LEAK\t(System.Object)%s\n   (%s)%s\n", objectAddr.c_str(), "System.Object", objectAddr.c_str());
		mDebugManager->mOutMessages.push_back(errorStr);
		return;
	}
	else if (dbgBreakKind == DbgBreakKind_ArithmeticOverflow)
	{
		String errorStr = "error Arithmetic overflow detected";
		mDebugManager->mOutMessages.push_back(errorStr);
		return;
	}

	bool showMainThread = false;

	String symbol;
	addr_target offset;
	DbgModule* dbgModule;
	if (mDebugTarget->FindSymbolAt(pcAddress, &symbol, &offset, &dbgModule))
	{
		if ((symbol == "DbgBreakPoint") || (symbol == "RtlUserThreadStart") || (symbol == "RtlUserThreadStart@8"))
		{
			showMainThread = true;
		}
	}
#ifdef BF_DBG_32
	else if ((dbgModule != NULL) && (dbgModule->mDisplayName.Equals("kernel32.dll", StringImpl::CompareKind_OrdinalIgnoreCase)))
	{
		showMainThread = true;
	}
#endif

	if (showMainThread)
	{
		// This is a manual break, show the main thread
		mActiveThread = mThreadList.front();
		if (mDebugPendingExpr != NULL)
		{
			for (auto thread : mThreadList)
			{
				if (thread->mThreadId == mDebugEvalThreadInfo.mThreadId)
				{
					mActiveThread = thread;
					break;
				}
			}
		}
	}
}

bool WinDebugger::HasSteppedIntoCall()
{
	//  Some calls (like __chkstk) actually push results to the stack, so we need to check
	//  if we're REALLY deeper or not, by rolling back the callstack once

	CPURegisters registers;
	PopulateRegisters(&registers);
	if (RollBackStackFrame(&registers, true))
	{
		// If the previous frames SP is equal or deeper than our step start then we are indeed inside a call!
		if (mStepSP >= registers.GetSP())
			return true;
	}
	return false;
}

void WinDebugger::StepOver(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("StepOver\n");

	if (!TryRunContinue())
		return;

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepOver);
	ContinueDebugEvent();
}

void WinDebugger::StepOut(bool inAssembly)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("StepOut\n");

	if (!TryRunContinue())
		return;

	mCurNoInfoStepTries = 0; // Reset
	mStepInAssembly = inAssembly;

	SetupStep(StepType_StepOut);

	ContinueDebugEvent();
}

void WinDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	DbgSubprogram* subProgram = NULL;
	if (!inAssembly)
	{
		if (mCallStack.size() == 0)
			UpdateCallStack();
		if (mCallStack.size() > 0)
		{
			UpdateCallStackMethod(0);
			subProgram = mCallStack[0]->mSubProgram;
		}
		if (subProgram == NULL)
			return;
	}

	DbgSubprogram* rootInlineParent = NULL;
	if (subProgram != NULL)
		rootInlineParent = subProgram->GetRootInlineParent();

	String result;
	if (mDebugTarget == NULL)
		return;

	DbgSrcFile* srcFile = NULL;
	if (!fileName.IsEmpty())
	{
		srcFile = mDebugTarget->GetSrcFile(fileName);
		if (srcFile == NULL)
			return;
	}

	addr_target pcAddress = 0;
	if (inAssembly)
	{
		pcAddress = lineNumOrAsmAddr;
	}
	else
	{
		int lineNum = (int)lineNumOrAsmAddr;

		addr_target bestAddr[2] = { 0, 0 };
		int checkLineNum[2] = { lineNum - 1, lineNum };

		auto _CheckLineInfo = [&](DbgSubprogram* dbgSubprogram, DbgLineInfo* dbgLineInfo)
		{
			for (int iPass = 0; iPass < 2; ++iPass)
			{
				int bestLineOffset = 0x7FFFFFFF;

				for (auto& lineData : dbgLineInfo->mLines)
				{
					auto addr = dbgSubprogram->GetLineAddr(lineData);
					if ((addr < subProgram->mBlock.mLowPC) || (addr >= subProgram->mBlock.mHighPC))
						continue;

					int lineOffset = lineData.mLine - checkLineNum[iPass];

					if ((lineOffset >= 0) && (lineOffset <= 6) && (lineOffset <= bestLineOffset))
					{
						if (lineOffset < bestLineOffset)
						{
							bestLineOffset = lineOffset;
							bestAddr[iPass] = addr;
						}
					}
				}
			}
		};

		for (int checkHotIdx = -1; checkHotIdx < (int)srcFile->mHotReplacedDbgLineInfo.size(); checkHotIdx++)
		{
			if (checkHotIdx >= 0)
			{
				auto hotReplacedLineInfo = srcFile->mHotReplacedDbgLineInfo[checkHotIdx];
				for (auto& hotReplacedEntry : hotReplacedLineInfo->mEntries)
				{
					_CheckLineInfo(hotReplacedEntry.mSubprogram, hotReplacedEntry.mLineInfo);
				}
			}
			else
			{
				for (auto subprogram : srcFile->mLineDataRefs)
					_CheckLineInfo(subprogram, subprogram->mLineInfo);
			}

			if (bestAddr[1] != 0)
				break;
		}

		if (bestAddr[1] != 0)
		{
			const int kMaxAddrDist = 64; // within reasonable range
			if ((bestAddr[0] != 0) && (bestAddr[1] - bestAddr[0] <= kMaxAddrDist))
			{
				addr_target addrStart = bestAddr[0];
				addr_target addrEnd = bestAddr[1];
				addr_target addr = addrStart;

				BF_ASSERT(addrEnd - addr <= kMaxAddrDist);

				addr_target lastOp = 0;
				while (addr < addrEnd)
				{
					CPUInst inst;
					if (!mDebugTarget->DecodeInstruction(addr, &inst))
						break;
					lastOp = addr;
					addr += inst.GetLength();
				}
			}

			pcAddress = (uint64)bestAddr[1];
		}
	}

	if (pcAddress)
	{
		BF_ASSERT(mActiveThread->mBreakpointAddressContinuing == 0);
		mActiveThread->mIsAtBreakpointAddress = 0;
		mActiveThread->mStoppedAtAddress = pcAddress;

		if (mCallStack.size() == 0)
			UpdateCallStack();

		CPURegisters* regs = &mCallStack.front()->mRegisters;
		*regs->GetPCRegisterRef() = pcAddress;

		SetRegisters(regs);

		WdBreakpoint* breakpoint = (WdBreakpoint*)FindBreakpointAt(pcAddress);
		if (breakpoint != NULL)
		{
			BfLogDbg("SetNextStatement setting mIsAtBreakpointAddress = %p\n", breakpoint->mAddr);
			mActiveThread->mIsAtBreakpointAddress = breakpoint->mAddr;
		}
	}
}

bool WinDebugger::PopulateRegisters(CPURegisters* registers, BF_CONTEXT& lcContext)
{
#ifdef BF_DBG_32
	registers->mIntRegs.eax = lcContext.Eax;
	registers->mIntRegs.ecx = lcContext.Ecx;
	registers->mIntRegs.edx = lcContext.Edx;
	registers->mIntRegs.ebx = lcContext.Ebx;
	registers->mIntRegs.esp = lcContext.Esp;
	registers->mIntRegs.ebp = lcContext.Ebp;
	registers->mIntRegs.esi = lcContext.Esi;
	registers->mIntRegs.edi = lcContext.Edi;
	registers->mIntRegs.eip = lcContext.Eip;
	registers->mIntRegs.efl = lcContext.EFlags;

	BF_ASSERT(sizeof(lcContext.FloatSave.RegisterArea) == sizeof(registers->mFpMmRegsArray));
	memcpy(registers->mFpMmRegsArray, lcContext.FloatSave.RegisterArea, sizeof(lcContext.FloatSave.RegisterArea));

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 32 * sizeof(float));
	memcpy(registers->mXmmRegsArray, &lcContext.ExtendedRegisters[160], sizeof(registers->mXmmRegsArray));
#else
	registers->mIntRegs.rax = lcContext.Rax;
	registers->mIntRegs.rcx = lcContext.Rcx;
	registers->mIntRegs.rdx = lcContext.Rdx;
	registers->mIntRegs.rbx = lcContext.Rbx;
	registers->mIntRegs.rsp = lcContext.Rsp;
	registers->mIntRegs.rbp = lcContext.Rbp;
	registers->mIntRegs.rsi = lcContext.Rsi;
	registers->mIntRegs.rdi = lcContext.Rdi;
	registers->mIntRegs.rip = lcContext.Rip;
	registers->mIntRegs.efl = lcContext.EFlags;

	registers->mIntRegs.r8 = lcContext.R8;
	registers->mIntRegs.r9 = lcContext.R9;
	registers->mIntRegs.r10 = lcContext.R10;
	registers->mIntRegs.r11 = lcContext.R11;
	registers->mIntRegs.r12 = lcContext.R12;
	registers->mIntRegs.r13 = lcContext.R13;
	registers->mIntRegs.r14 = lcContext.R14;
	registers->mIntRegs.r15 = lcContext.R15;

	registers->mIntRegs.gs = lcContext.SegGs;

	for (int i = 0; i < 8; i++)
	{
		memcpy(&registers->mFpMmRegsArray[i], &lcContext.FltSave.FloatRegisters[i], 10);
	}

	BF_ASSERT(sizeof(registers->mXmmRegsArray) == 64 * sizeof(float));
	memcpy(registers->mXmmRegsArray, BF_CONTEXT_XMMDATA(lcContext), sizeof(registers->mXmmRegsArray));
#endif

	return (lcContext.ContextFlags & (BF_CONTEXT_EXCEPTION_ACTIVE | BF_CONTEXT_SERVICE_ACTIVE)) == 0;
}

bool WinDebugger::PopulateRegisters(CPURegisters* registers)
{
	/*static bool sCheckedProcessorFeatures = false;
	static bool sMmxAvailable = false;
	static bool sXmmAvailable = false;
	if (!sCheckedProcessorFeatures)
	{
		//CDH we don't do anything with these yet since we grab BF_CONTEXT_ALL anyway, but could be useful
		sMmxAvailable = ::IsProcessorFeaturePresent(PF_MMX_INSTRUCTIONS_AVAILABLE) != 0;
		sXmmAvailable = ::IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE) != 0;
		sCheckedProcessorFeatures = true;
	}*/

	BF_ASSERT(registers != nullptr);

	BF_CONTEXT lcContext;
	lcContext.ContextFlags = BF_CONTEXT_ALL | BF_CONTEXT_EXCEPTION_REQUEST;
	BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

	return PopulateRegisters(registers, lcContext);
}

bool WinDebugger::RollBackStackFrame(CPURegisters* registers, bool isStackStart)
{
	BF_ASSERT(registers != nullptr);

	return mDebugTarget->RollBackStackFrame(registers, NULL, isStackStart);
}

bool WinDebugger::SetHotJump(DbgSubprogram* oldSubprogram, addr_target newTarget, int newTargetSize)
{
	BfLogDbg("SetHotJump %s %p->%p\n", oldSubprogram->mName, oldSubprogram->mBlock.mLowPC, newTarget);

	//AutoCrit autoCrit(mDebugManager->mCritSect);
	BF_ASSERT(mDebugManager->mCritSect.mLockCount == 1);

	addr_target jmpInstStart = oldSubprogram->mBlock.mLowPC;
	addr_target jmpInstEnd = jmpInstStart + sizeof(HotJumpOp);

	if (jmpInstEnd > oldSubprogram->mBlock.mHighPC)
	{
		if ((oldSubprogram->mBlock.mHighPC - oldSubprogram->mBlock.mLowPC == 1) &&
			(newTargetSize == 1))
			return true; // Special case for just stub 'ret' methods
		String err = StrFormat("Failed to hot replace method, method '%s' too small to insert hot thunk", oldSubprogram->ToString().c_str());
		Fail(err);
		return false;
	}

	if (oldSubprogram->mHotReplaceKind != DbgSubprogram::HotReplaceKind_Replaced)
	{
		for (int hotThreadIdx = 0; hotThreadIdx < (int)mHotThreadStates.size(); hotThreadIdx++)
		{
			auto& hotThreadState = mHotThreadStates[hotThreadIdx];
			WdThreadInfo* threadInfo = NULL;
			if (!mThreadMap.TryGetValue((uint32)hotThreadState.mThreadId, &threadInfo))
				continue;

			int tryStart = GetTickCount();

			while ((hotThreadState.mRegisters.GetPC() >= jmpInstStart) && (hotThreadState.mRegisters.GetPC() < jmpInstEnd))
			{
				if (GetTickCount() - tryStart >= 8000)
				{
					Fail("Failed to hot replace method, can't move past prelude");
					return false;
				}

				BfLogDbg("SetHotJump skipping through %p\n", hotThreadState.mRegisters.GetPC());

				bool removedBreakpoint = false;

				mActiveThread = threadInfo;
				if ((mActiveThread->mStoppedAtAddress >= jmpInstStart) && (mActiveThread->mStoppedAtAddress < jmpInstEnd))
				{
					for (addr_target addr = jmpInstStart; addr < jmpInstEnd; addr++)
					{
						if (mPhysBreakpointAddrMap.ContainsKey(addr))
						{
							removedBreakpoint = true;
							RemoveBreakpoint(addr);
						}
					}
				}

				RunState oldRunState = mRunState;
				mRunState = RunState_HotStep;

				if (mWantsDebugContinue)
				{
					mWantsDebugContinue = false;
					BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
					mContinueEvent.Set();
				}

				BF_CONTEXT lcContext;
				lcContext.ContextFlags = BF_CONTEXT_ALL;
				BF_GetThreadContext(mActiveThread->mHThread, &lcContext);
				lcContext.EFlags |= 0x100; // Set trap flag, which raises "single-step" exception
				BF_SetThreadContext(mActiveThread->mHThread, &lcContext);

				::ResumeThread(mActiveThread->mHThread);
				BfLogDbg("ResumeThread %d\n", mActiveThread->mThreadId);

				while (mRunState != RunState_Terminated)
				{
					mDebugManager->mCritSect.Unlock();
					Sleep(0);
					mDebugManager->mCritSect.Lock();
					if (IsPaused())
						break;
					if (mWantsDebugContinue)
					{
						BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

						mWantsDebugContinue = false;
						BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
						mContinueEvent.Set();
					}
				}

				BF_GetThreadContext(mActiveThread->mHThread, &lcContext);

				::SuspendThread(mActiveThread->mHThread);
				BfLogDbg("SuspendThread %d\n", mActiveThread->mThreadId);
				mRunState = oldRunState;

				if ((mRunState != RunState_Terminated) && (mRunState != RunState_Terminating))
				{
					if (!IsPaused())
					{
						BF_ASSERT(mWantsDebugContinue);
						mWantsDebugContinue = false;
						BF_ASSERT_REL(mActiveThread->mIsAtBreakpointAddress == 0);
						mContinueEvent.Set();
					}
				}

				PopulateRegisters(&hotThreadState.mRegisters);
			}
		}
	}

	HotJumpOp jumpOp;
	jumpOp.mOpCode = 0xE9;
	jumpOp.mRelTarget = newTarget - oldSubprogram->mBlock.mLowPC - sizeof(HotJumpOp);
	WriteMemory(oldSubprogram->mBlock.mLowPC, jumpOp);
	::FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)oldSubprogram->mBlock.mLowPC, sizeof(HotJumpOp));
	return true;
}

DbgSubprogram* WinDebugger::TryFollowHotJump(DbgSubprogram* subprogram, addr_target addr)
{
	if (subprogram->mHotReplaceKind != DbgSubprogram::HotReplaceKind_Replaced)
		return subprogram;

	if (addr != subprogram->mBlock.mLowPC)
		return subprogram;

	auto dbgModule = subprogram->mCompileUnit->mDbgModule;

	HotJumpOp jumpOp = ReadMemory<HotJumpOp>(addr);
	if (jumpOp.mOpCode != 0xE9)
		return subprogram;
	addr_target jumpAddr = addr + jumpOp.mRelTarget + sizeof(HotJumpOp);

	auto jumpSubprogram = mDebugTarget->FindSubProgram(jumpAddr);
	if (jumpSubprogram == NULL)
		return subprogram;

	return jumpSubprogram;
}

bool WinDebugger::ShouldShowStaticMember(DbgType* dbgType, DbgVariable* member)
{
	// If locationData is non-null, that means it was added in addition to the static declaration in the CV type info,
	//  so only add the names from the type definition
	auto flavor = dbgType->mCompileUnit->mDbgModule->mDbgFlavor;
	return ((((dbgType->IsNamespace()) || (flavor != DbgFlavor_MS)) && ((member->mLocationData != NULL) || member->mIsConst)) ||
		((flavor == DbgFlavor_MS) && (member->mLocationData == NULL)));
}

String WinDebugger::GetMemberList(DbgType* dbgType, const StringImpl& expr, bool isPtr, bool isStatic, bool forceCast, bool isSplat, bool isReadOnly)
{
	auto dbgModule = dbgType->GetDbgModule();
	dbgType->PopulateType();
	auto language = dbgType->GetLanguage();

	if (!isStatic)
	{
		String retVal;
		bool needsNewline = false;
		bool isBfObject = false;

		if (dbgType->IsBfObjectPtr())
		{
			isBfObject = true;
			dbgType = dbgType->mTypeParam;
		}

		int baseIdx = 0;
		for (auto baseTypeEntry : dbgType->mBaseTypes)
		{
			auto baseType = baseTypeEntry->mBaseType;
			if ((baseType->mSize > 0) || (baseType->mTypeCode != DbgType_Struct) || (strcmp(baseType->mTypeName, "ValueType") != 0))
			{
				String baseTypeStr = baseType->ToStringRaw(language);
				if (baseIdx > 0)
					retVal += "\n";
				if (isSplat)
					retVal += "[base]\t((" + baseTypeStr + ")" + expr + "), nv";
				else if (dbgType->WantsRefThis())
					retVal += "[base]\t((" + baseTypeStr + ")this), nd, na, nv, this=" + expr;
				else
					retVal += "[base]\t((" + baseTypeStr + "*)this), nd, na, nv, this=" + expr;
				if (isReadOnly)
					retVal += ", ne";
			}

			needsNewline = true;

			baseIdx++;
		}

		String thisExpr = expr;
		String castString;
		if (dbgType->IsBfObject())
		{
			auto ptrType = dbgType->GetDbgModule()->GetPointerType(dbgType);
			castString = ptrType->ToStringRaw(language);
		}
		else
			castString = dbgType->ToStringRaw(language);

		bool hadStatics = false;
		for (auto member : dbgType->mMemberList)
		{
			if (member->mMemberOffset < 0)
				continue;

			if (member->mIsStatic)
			{
				if (ShouldShowStaticMember(dbgType, member))
					hadStatics = true;
			}
			else
			{
				bool ignoreMember = false;
				if (member->mName != NULL)
				{
					if ((member->mName[0] == '?') ||
						(member->mName[0] == '$') ||
						(strncmp(member->mName, "_vptr$", 6) == 0))
						ignoreMember = true;
				}

				if (!ignoreMember)
				{
					if (needsNewline)
						retVal += "\n";

					if (member->mName == NULL)
					{
						retVal += GetMemberList(member->mType, expr, isPtr, isStatic, forceCast, isSplat, isReadOnly);
					}
					else
					{
						retVal += String(member->mName);
						if (isSplat)
						{
							retVal += "\t(" + thisExpr + ")." + String(member->mName);
							// We don't want to rely on this being enforced here.  For one, ref types shouldn't get ", ne" added,
							//  and this doesn't solve the issue of attempting to assign via the Immediate window
							/*if (isReadOnly)
								retVal += ", ne";*/
						}
						else
						{
							if (forceCast)
								retVal += "\t((" + castString + ")this)." + String(member->mName);
							else if ((member->mName[0] >= '0') && (member->mName[0] <= '9')) // Numbered tuple member?
								retVal += "\tthis." + String(member->mName);
							else
								retVal += "\t" + String(member->mName);

							retVal += ", this=" + thisExpr;
// 							if (isReadOnly)
// 								retVal += ", ne";
						}
					}
					needsNewline = true;
				}
			}
		}
		if (hadStatics)
		{
			if (needsNewline)
				retVal += "\n";
			retVal += "Static values\t" + castString;
		}
		return retVal;
	}
	else
	{
		if (dbgType->IsBfObjectPtr())
			dbgType = dbgType->mTypeParam;

		String retVal;
		String memberPrefix = expr;

		bool needsNewline = false;
		bool hadStatics = false;
		for (auto member : dbgType->mMemberList)
		{
			if (member->mIsStatic)
			{
				if (ShouldShowStaticMember(dbgType, member))
				{
					if (needsNewline)
						retVal += "\n";
					retVal += String(member->mName) + "\t" + memberPrefix + "." + String(member->mName);
					needsNewline = true;
				}
			}
		}
		return retVal;
	}
	return "";
}

bool WinDebugger::ParseFormatInfo(DbgModule* dbgModule, const StringImpl& formatInfoStr, DwFormatInfo* formatInfo, BfPassInstance* bfPassInstance, int* assignExprOffset, String* assignExprString, String* errorString, DbgTypedValue contextTypedValue)
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
			int nextComma = formatFlags.IndexOf(',', 1);
			int quotePos = formatFlags.IndexOf('"', 1);
			if ((quotePos != -1) && (quotePos < nextComma))
			{
				int nextQuotePos = formatFlags.IndexOf('"', quotePos + 1);
				if (nextQuotePos != -1)
					nextComma = formatFlags.IndexOf(',', nextQuotePos + 1);
			}
			if (nextComma == -1)
				nextComma = formatFlags.length();

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
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, thisExpr, formatInfo);
				formatInfo->mExplicitThis = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = thisExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "count=", 6) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(6);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mOverrideCount = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "maxcount=", 9) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(9);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mMaxCount = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "arraysize=", 10) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String countExpr = formatCmd.Substring(10);
				if (countExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
				continue;
			}
			else if (strncmp(formatCmd.c_str(), "assign=", 7) == 0)
			{
				formatCmd = formatFlags.Substring(1);
				formatCmd = Trim(formatCmd);
				String assignExpr = formatCmd.Substring(7);
				if (assignExpr.empty())
					break;
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, assignExpr, formatInfo);
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
				if ((formatInfo->mReferenceId.mLength >= 2) && (formatInfo->mReferenceId[0] == '\"'))
					formatInfo->mReferenceId = formatInfo->mReferenceId.Substring(1, formatInfo->mReferenceId.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "action=", 7) == 0)
			{
				formatInfo->mAction = formatCmd.Substring(7);
				if ((formatInfo->mAction.mLength >= 2) && (formatInfo->mAction[0] == '\"'))
					formatInfo->mAction = formatInfo->mReferenceId.Substring(1, formatInfo->mReferenceId.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "_=", 2) == 0)
			{
				formatInfo->mSubjectExpr = formatCmd.Substring(2);
				if ((formatInfo->mSubjectExpr.mLength >= 2) && (formatInfo->mSubjectExpr[0] == '\"'))
					formatInfo->mSubjectExpr = formatInfo->mSubjectExpr.Substring(1, formatInfo->mSubjectExpr.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "expectedType=", 13) == 0)
			{
				formatInfo->mExpectedType = formatCmd.Substring(13);
				if ((formatInfo->mExpectedType.mLength >= 2) && (formatInfo->mExpectedType[0] == '\"'))
					formatInfo->mExpectedType = formatInfo->mExpectedType.Substring(1, formatInfo->mExpectedType.length() - 2);
			}
			else if (strncmp(formatCmd.c_str(), "namespaceSearch=", 16) == 0)
			{
				formatInfo->mNamespaceSearch = formatCmd.Substring(16);
				if ((formatInfo->mNamespaceSearch.mLength >= 2) && (formatInfo->mNamespaceSearch[0] == '\"'))
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
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, countExpr, formatInfo);
				DbgTypedValue countValue = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
				if ((countValue) && (countValue.mType->IsInteger()))
					formatInfo->mArrayLength = (intptr)countValue.GetInt64();
				if (dbgEvaluationContext.HadError())
				{
					if (errorString != NULL)
						*errorString = dbgEvaluationContext.GetErrorStr();
					return false;
				}
				formatFlags = countExpr.Substring(dbgEvaluationContext.mExprNode->GetSrcEnd());
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

String WinDebugger::MaybeQuoteFormatInfoParam(const StringImpl& str)
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

DbgTypedValue WinDebugger::EvaluateInContext(DbgCompileUnit* dbgCompileUnit, const DbgTypedValue& contextTypedValue, const StringImpl& subExpr, DwFormatInfo* formatInfo, String* outReferenceId, String* outErrors)
{
	DbgEvaluationContext dbgEvaluationContext(this, dbgCompileUnit->mDbgModule, subExpr, formatInfo, contextTypedValue);
	dbgEvaluationContext.mDbgExprEvaluator->mDbgCompileUnit = dbgCompileUnit;
	if (formatInfo != NULL)
	{
		dbgEvaluationContext.mDbgExprEvaluator->mLanguage = formatInfo->mLanguage;
		dbgEvaluationContext.mDbgExprEvaluator->mSubjectExpr = formatInfo->mSubjectExpr;
	}
	dbgEvaluationContext.mDbgExprEvaluator->mReferenceId = outReferenceId;
	auto result = dbgEvaluationContext.EvaluateInContext(contextTypedValue);
	if ((formatInfo != NULL) && (dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride != -1))
		formatInfo->mOverrideCount = dbgEvaluationContext.mDbgExprEvaluator->mCountResultOverride;
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
		return DbgTypedValue();
	}
	return result;
}

void WinDebugger::DbgVisFailed(DebugVisualizerEntry* debugVis, const StringImpl& evalString, const StringImpl& errors)
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

bool WinDebugger::EvalCondition(DebugVisualizerEntry* debugVis, DbgCompileUnit* dbgCompileUnit, DbgTypedValue typedVal, DwFormatInfo& formatInfo, const StringImpl& condition, const Array<String>& dbgVisWildcardCaptures, String& errorStr)
{
	DwFormatInfo displayStrFormatInfo = formatInfo;
	displayStrFormatInfo.mHidePointers = false;
	displayStrFormatInfo.mRawString = false;

	String errors;
	const String conditionStr = mDebugManager->mDebugVisualizers->DoStringReplace(condition, dbgVisWildcardCaptures);
	DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, typedVal, conditionStr, &displayStrFormatInfo, NULL, &errors);
	if ((!evalResult) || (!evalResult.mType->IsBoolean()))
	{
		if (formatInfo.mRawString)
			return false;

		errorStr += "<DbgVis Failed>";
		DbgVisFailed(debugVis, conditionStr, errors);
		return false;
	}

	return evalResult.mBool;
}

String WinDebugger::GetArrayItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData)
{
	DbgEvaluationContext conditionEvaluationContext(this, dbgCompileUnit, debugVis->mCondition);

	String addrs;

	bool checkLeft = true;

	int usedCount = 0;
	while (usedCount < count)
	{
		DbgTypedValue condVal = conditionEvaluationContext.EvaluateInContext(curNode);
		if (!condVal)
			break;
		if (condVal.mBool)
		{
			auto val = curNode;
			if (valueType == NULL)
			{
				String typeAddr = val.mType->ToStringRaw();
				// RPad
				typeAddr.Append(' ', sizeof(addr_target) * 2 - typeAddr.length());
				addrs += typeAddr;
			}

			String addr = EncodeDataPtr(val.mPtr, false);
			addrs += addr;
			usedCount++;
		}

		curNode.mPtr += curNode.mType->mTypeParam->GetStride();
	}
	count = usedCount;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
	}

	return addrs;
}

String WinDebugger::GetLinkedListItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, addr_target endNodePtr, DbgType* valueType, DbgTypedValue& curNode, int& count, String* outContinuationData)
{
	DbgEvaluationContext nextEvaluationContext(this, dbgCompileUnit, debugVis->mNextPointer);
	DbgEvaluationContext valueEvaluationContext(this, dbgCompileUnit, debugVis->mValuePointer);

	String addrs;

	bool checkLeft = true;

	int mapIdx;
	for (mapIdx = 0; mapIdx < count; mapIdx++)
	{
		if (curNode.mPtr == endNodePtr)
			break;
		DbgTypedValue val = valueEvaluationContext.EvaluateInContext(curNode);
		if (!val)
			break;
		if (val.mPtr == 0)
			break;

		if (valueType == NULL)
		{
			String typeAddr = val.mType->ToStringRaw();
			// RPad
			typeAddr.Append(' ', sizeof(addr_target)*2 - typeAddr.length());
			addrs += typeAddr;
		}

		String addr = EncodeDataPtr(val.mPtr, false);
		addrs += addr;

		curNode = nextEvaluationContext.EvaluateInContext(curNode);
	}
	count = mapIdx;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(endNodePtr, false) + EncodeDataPtr(valueType, false) +
			EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
	}

	return addrs;
}

String WinDebugger::GetDictionaryItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgTypedValue dictValue, int bucketIdx, int nodeIdx, int& count, String* outContinuationData)
{
	//DbgEvaluationContext bucketsEvaluationContext(this, dbgModule, debugVis->mBuckets);
	DbgEvaluationContext nextEvaluationContext(this, dbgCompileUnit->mDbgModule, debugVis->mNextPointer);

	DbgTypedValue bucketsPtr = EvaluateInContext(dbgCompileUnit, dictValue, debugVis->mBuckets);
	DbgTypedValue entriesPtr = EvaluateInContext(dbgCompileUnit, dictValue, debugVis->mEntries);
	if ((!bucketsPtr) || (!entriesPtr))
	{
		count = -1;
		return "";
	}
	int entrySize = entriesPtr.mType->mTypeParam->GetStride();
	int bucketIdxSize = bucketsPtr.mType->mTypeParam->GetStride();

	String addrs;

	bool checkLeft = true;

	int encodeCount = 0;
	while (encodeCount < count)
	{
		if (nodeIdx != -1)
		{
			DbgTypedValue entryValue;
			entryValue.mSrcAddress = entriesPtr.mPtr + (nodeIdx * entrySize);
			entryValue.mType = entriesPtr.mType->mTypeParam;

			addrs += EncodeDataPtr(entryValue.mSrcAddress, false);

			DbgTypedValue nextValue = nextEvaluationContext.EvaluateInContext(entryValue);
			if ((!nextValue) || (!nextValue.mType->IsInteger()))
			{
				break;
			}

			nodeIdx = (int)nextValue.GetInt64();
			encodeCount++;
		}
		else
		{
			if (bucketIdxSize == 4)
				nodeIdx = ReadMemory<int>(bucketsPtr.mPtr + bucketIdx * sizeof(int32));
			else
				nodeIdx = (int)ReadMemory<int64>(bucketsPtr.mPtr + bucketIdx * sizeof(int64));
			bucketIdx++;
		}
	}

	count = encodeCount;
	//count = mapIdx;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(dictValue.mType, false) + EncodeDataPtr(dictValue.mSrcAddress, false) +
			EncodeDataPtr((addr_target)bucketIdx, false) + EncodeDataPtr((addr_target)nodeIdx, false);
	}

	return addrs;
}

String WinDebugger::GetTreeItems(DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, Array<addr_target>& parentList, DbgType*& valueType, DbgTypedValue& curNode, int count, String* outContinuationData)
{
	DbgEvaluationContext leftEvaluationContext(this, dbgCompileUnit, debugVis->mLeftPointer);
	DbgEvaluationContext rightEvaluationContext(this, dbgCompileUnit, debugVis->mRightPointer);
	DbgEvaluationContext valueEvaluationContext(this, dbgCompileUnit, debugVis->mValuePointer);

	DbgEvaluationContext conditionEvaluationContext(this, dbgCompileUnit, debugVis->mCondition);

	String addrs;

	bool checkLeft = true;

	if ((curNode.mPtr & 2) != 0) // Flag from continuation
	{
		checkLeft = false;
		curNode.mPtr &= (addr_target)~2;
	}

	HashSet<intptr> seenAddrs;

	for (int mapIdx = 0; mapIdx < count; mapIdx++)
	{
		DbgTypedValue readNode;
		while (true)
		{
			bool checkNode = (curNode.mPtr & 1) == 0;

			readNode = curNode;
			readNode.mPtr &= (addr_target)~1;

			if (checkLeft)
			{
				DbgTypedValue leftValue = leftEvaluationContext.EvaluateInContext(readNode);
				bool isEmpty = leftValue.mPtr == NULL;
				if ((leftValue) && (conditionEvaluationContext.HasExpression()))
				{
					auto condValue = conditionEvaluationContext.EvaluateInContext(leftValue);
					if (condValue)
						isEmpty = !condValue.mBool;
				}
				if (isEmpty)
				{
					checkLeft = false;
					break; // Handle node
				}

				parentList.push_back(curNode.mPtr);
				curNode = leftValue;
			}
			else if (checkNode)
			{
				break; // Handle node
			}
			else
			{
				DbgTypedValue rightValue = rightEvaluationContext.EvaluateInContext(readNode);
				bool isEmpty = rightValue.mPtr == NULL;
				if ((rightValue) && (conditionEvaluationContext.HasExpression()))
				{
					auto condValue = conditionEvaluationContext.EvaluateInContext(rightValue);
					if (condValue)
						isEmpty = !condValue.mBool;
				}
				if (!isEmpty)
				{
					curNode = rightValue;
					checkLeft = true;
				}
				else
				{
					if (parentList.size() == 0)
					{
						// Failed
						break;
					}

					curNode.mPtr = parentList.back();
					parentList.pop_back();
					continue; // Don't check against seenAddrs
				}
			}

			if (!seenAddrs.Add(curNode.mPtr))
			{
				// Failed!
				return "";
			}
		}

		DbgTypedValue val = valueEvaluationContext.EvaluateInContext(readNode);
		if (valueType == NULL)
			valueType = val.mType;

		String addr = EncodeDataPtr(val.mPtr, false);
		addrs += addr;

		curNode.mPtr |= 1; // Node handled
	}

	if (!checkLeft)
		curNode.mPtr |= 2;

	if (outContinuationData != NULL)
	{
		*outContinuationData += EncodeDataPtr(debugVis, false) + EncodeDataPtr(valueType, false) +  EncodeDataPtr(curNode.mType, false) + EncodeDataPtr(curNode.mPtr, false);
		for (auto parent : parentList)
			*outContinuationData += EncodeDataPtr(parent, false);
	}

	return addrs;
}

String WinDebugger::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	DbgCompileUnit* dbgCompileUnit = GetCallStackCompileUnit(callStackIdx);;

	if (!IsPaused())
		return "";

	const char* dataPtr = continuationData.c_str();
	DebugVisualizerEntry* debugVis = (DebugVisualizerEntry*)DecodeLocalDataPtr(dataPtr);

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_TreeItems)
	{
		DbgType* valueType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		Array<addr_target> parentList;
		String newContinuationData;
		while (*dataPtr != 0)
			parentList.push_back(DecodeTargetDataPtr(dataPtr));

		String retVal = GetTreeItems(dbgCompileUnit, debugVis, parentList, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		addr_target endNodePtr = DecodeTargetDataPtr(dataPtr);
		DbgType* valueType = (DbgType*) DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetLinkedListItems(dbgCompileUnit, debugVis, endNodePtr, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Array)
	{
		DbgType* valueType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		DbgTypedValue curNode;
		curNode.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		curNode.mPtr = DecodeTargetDataPtr(dataPtr);

		String newContinuationData;

		if (count < 0)
			count = 3;

		String retVal = GetArrayItems(dbgCompileUnit, debugVis, valueType, curNode, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_Dictionary)
	{
		DbgTypedValue dictValue;
		dictValue.mType = (DbgType*)DecodeLocalDataPtr(dataPtr);
		dictValue.mSrcAddress = DecodeTargetDataPtr(dataPtr);

		int bucketIdx = (int)DecodeTargetDataPtr(dataPtr);
		int nodeIdx = (int)DecodeTargetDataPtr(dataPtr);

		String newContinuationData;
		String retVal = GetDictionaryItems(dbgCompileUnit, debugVis, dictValue, bucketIdx, nodeIdx, count, &newContinuationData);
		retVal += "\n" + newContinuationData;
		return retVal;
	}

	return "";
}

template <typename T>
static String IntTypeToString(T val, const StringImpl& name, DwDisplayInfo* displayInfo, DwFormatInfo& formatInfo)
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
		return StrFormat(format.c_str(), (std::make_unsigned<T>::type)(val));
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
		return StrFormat(format.c_str(), (std::make_unsigned<T>::type)(val));
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
		return StrFormat(format.c_str(), (std::make_unsigned<T>::type)(val));
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

DwDisplayInfo* WinDebugger::GetDisplayInfo(const StringImpl& referenceId)
{
	DwDisplayInfo* displayInfo = &mDebugManager->mDefaultDisplayInfo;
	if (!referenceId.empty())
	{
		if (!mDebugManager->mDisplayInfos.TryGetValue(referenceId, &displayInfo))
		{
			int dollarIdx = referenceId.LastIndexOf('$');
			if ((dollarIdx > 0) && (referenceId[dollarIdx - 1] == ']'))
			{
				// Try getting series displayinfo
				mDebugManager->mDisplayInfos.TryGetValueWith(StringView(referenceId, 0, dollarIdx), &displayInfo);
			}
		}
	}
	return displayInfo;
}

static String WrapWithModifiers(const StringImpl& origName, DbgType* dbgType, DbgLanguage language)
{
	if (language == DbgLanguage_Unknown)
		language = dbgType->GetLanguage();

	String name = origName;
	while (true)
	{
		if (dbgType->mTypeCode == DbgType_Const)
		{
			if (language == DbgLanguage_Beef)
				name = "readonly " + name;
			else
				name = "const " + name;
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Volatile)
		{
			name = "volatile " + name;
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_TypeDef)
		{
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Ref)
		{
			if (language == DbgLanguage_Beef)
				name = "ref " + name;
			else
				name = name + "&";
			dbgType = dbgType->mTypeParam;
		}
		else if (dbgType->mTypeCode == DbgType_Bitfield)
		{
			return dbgType->ToString(language);
		}
		else
			return name;
	}
}

DebugVisualizerEntry* WinDebugger::FindVisualizerForType(DbgType* dbgType, Array<String>* wildcardCaptures)
{
	auto entry = mDebugManager->mDebugVisualizers->FindEntryForType(dbgType->ToString(DbgLanguage_Unknown, true), dbgType->mCompileUnit->mDbgModule->mDbgFlavor, wildcardCaptures);

	if (entry == NULL)
	{
		dbgType = dbgType->GetPrimaryType();
		dbgType->PopulateType();
		for (auto baseTypeEntry : dbgType->mBaseTypes)
		{
			entry = FindVisualizerForType(baseTypeEntry->mBaseType, wildcardCaptures);
			if (entry != NULL)
				break;
		}
	}

	return entry;
}

#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

String WinDebugger::ReadString(DbgTypeCode charType, intptr addr, bool isLocalAddr, intptr maxLength, DwFormatInfo& formatInfo, bool wantStringView)
{
	int origMaxLength = maxLength;
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
	if ((!formatInfo.mRawString) && (!wantStringView))
		maxLength = BF_MIN(maxLength, maxShowSize);

	if (wantStringView)
	{
		// Limit the original string view to 1MB, reevaluate on "More"
		maxLength = BF_MIN(maxLength, 1024 * 1024);
	}

	//EnableMemCache();
	bool readFailed = false;
	intptr strPtr = addr;

	int charLen = 1;
	if ((charType == DbgType_SChar16) || (charType == DbgType_UChar16))
		charLen = 2;
	else if ((charType == DbgType_SChar32) || (charType == DbgType_UChar32))
		charLen = 4;

	bool isUTF8 = formatInfo.mDisplayType == DwDisplayType_Utf8;

	int readSize = BF_MIN(1024, maxLength * charLen);
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

				if (ReadMemory(strPtr, readSize, buf, isLocalAddr))
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

	if ((formatInfo.mRawString) || (wantStringView))
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

	retVal += SlashString(valString, true, true, formatInfo.mLanguage == DbgLanguage_Beef);

	// We could go over 'maxShowSize' if we have a lot of slashed chars. An uninitialized string can be filled with '\xcc' chars
	if ((!formatInfo.mRawString) && (!wantStringView) && ((int)retVal.length() > maxShowSize))
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

void WinDebugger::ProcessEvalString(DbgCompileUnit* dbgCompileUnit, DbgTypedValue useTypedValue, String& evalStr, String& displayString, DwFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength)
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

			DwFormatInfo displayStrFormatInfo = formatInfo;
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
				DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, useTypedValue, evalString, &displayStrFormatInfo, NULL, &errors);
				if (evalResult)
				{
					if (displayStrFormatInfo.mNoEdit)
						formatInfo.mNoEdit = true;

					String result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);

					if ((formatInfo.mRawString) && (limitLength))
					{
						displayString = result;
						return;
					}

					int crPos = result.IndexOf('\n');
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

static bool IsNormalChar(uint32 c)
{
	return (c < 0x80);
}

String WinDebugger::DbgTypedValueToString(const DbgTypedValue& origTypedValue, const StringImpl& expr, DwFormatInfo& formatInfo, DbgExprEvaluator* optEvaluator, bool fullPrecision)
{
	BP_ZONE("WinDebugger::DbgTypedValueToString");

	DbgTypedValue typedValue = origTypedValue;
	auto dbgCompileUnit = typedValue.mType->mCompileUnit;
	auto dbgModule = typedValue.mType->GetDbgModule();
	auto language = origTypedValue.mType->GetLanguage();
	if (language == DbgLanguage_Unknown)
		language = formatInfo.mLanguage;
	formatInfo.mLanguage = language;
	bool isBeef = language == DbgLanguage_Beef;

	char str[32];
	bool readFailed = false;
	bool isCompositeType = false;
	bool isSizedArray = false;
	bool isEnum = false;
	int64 enumVal = 0;
	String result;
	String stringViewData;

	DwDisplayInfo* displayInfo = GetDisplayInfo(formatInfo.mReferenceId);
	bool wantStringView = (displayInfo->mFormatStr == "str") && (formatInfo.mAllowStringView);

	DbgType* origValueType = typedValue.mType;
	bool origHadRef = false;
	DbgType* dwValueType = typedValue.mType->RemoveModifiers(&origHadRef);

	if (dwValueType == NULL)
		dwValueType = dbgModule->GetPrimitiveType(DbgType_Void, language);
	else
		dwValueType = dwValueType->GetPrimaryType();

	if (dwValueType->mTypeCode == DbgType_TypeDef)
	{
		DbgTypedValue realTypedVal = typedValue;
		realTypedVal.mType = dwValueType->mTypeParam;
		return DbgTypedValueToString(realTypedVal, expr, formatInfo, optEvaluator);
	}

	if (formatInfo.mRawString)
	{
		if ((dwValueType->mTypeCode != DbgType_Struct) && (dwValueType->mTypeCode != DbgType_Class) && (dwValueType->mTypeCode != DbgType_Ptr) && (dwValueType->mTypeCode != DbgType_SizedArray))
			return "";
	}

	auto _ShowArraySummary = [&](String& retVal, addr_target ptrVal, int64 arraySize, DbgType* innerType)
	{
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

			DwFormatInfo displayStrFormatInfo = formatInfo;
			displayStrFormatInfo.mExpandItemDepth = 1;
			displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + retVal.length() + displayString.length();
			displayStrFormatInfo.mHidePointers = false;
			displayStrFormatInfo.mArrayLength = -1;

			// Why did we have this "na" on here? It made "void*[3]" type things show up as "{,,}"
			//String evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + StrFormat(")[%d], na", idx);
			String evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + StrFormat(")[%lld]", idx);
			DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, typedValue, evalStr, &displayStrFormatInfo);
			String result;
			if (evalResult)
			{
				result = DbgTypedValueToString(evalResult, evalStr, displayStrFormatInfo, NULL);
				int crPos = result.IndexOf('\n');
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

		if (dwValueType->IsPointer())
		{
			String retVal;
			addr_target ptrVal = (addr_target)typedValue.mPtr;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
			{
				retVal = EncodeDataPtr(ptrVal, true) + " ";
				retVal += dwValueType->mTypeParam->ToString(language);
				retVal += StrFormat("[%lld] ", (int64)formatInfo.mArrayLength);
			}

			_ShowArraySummary(retVal, ptrVal, formatInfo.mArrayLength, dwValueType->mTypeParam);

			String idxStr = "[{0}]";

			DbgType* innerType = dwValueType->mTypeParam;

			retVal += "\n" + dwValueType->ToString(language);

			String evalStr = "*((" + typedValue.mType->ToStringRaw(language) + ")" + EncodeDataPtr(ptrVal, true) + " + {0})";

			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(formatInfo.mArrayLength, 0), 10000) +
				"\t" + idxStr + "\t" + evalStr;
			return retVal;
		}
		else
		{
			DwFormatInfo newFormatInfo = formatInfo;
			newFormatInfo.mArrayLength = -1;

			String retVal = DbgTypedValueToString(typedValue, expr, newFormatInfo, optEvaluator);

			int crPos = (int)retVal.IndexOf('\n');
			if (crPos != -1)
				retVal = "!Array length flag not valid with this type" + retVal.Substring(crPos);
			return retVal;
		}
	}

	switch (dwValueType->mTypeCode)
	{
	case DbgType_Void:
		return "\nvoid";
	case DbgType_Bool:
		{
			if (typedValue.mUInt8 == 0)
				return "false\n" + WrapWithModifiers("bool", origValueType, language);
			else if (typedValue.mUInt8 == 1)
				return "true\n" + WrapWithModifiers("bool", origValueType, language);
			else
				return StrFormat("true (%d)\n", typedValue.mUInt8) + WrapWithModifiers("bool", origValueType, language);
		}
		break;
	case DbgType_UChar:
		if (language != DbgLanguage_Beef)
			return IntTypeToString<uint8>(typedValue.mUInt8, WrapWithModifiers("uint8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_SChar:
		{
			if (typedValue.mInt8 != 0)
			{
				char str[2] = {(char)typedValue.mInt8};
				result = SlashString(str, formatInfo.mDisplayType == DwDisplayType_Utf8, true);

				if (!IsNormalChar(typedValue.mUInt8))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt8);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers("char", origValueType, language);
		}
		break;
	case DbgType_UChar16:
		if (language != DbgLanguage_Beef)
			return IntTypeToString<int16>(typedValue.mUInt8, WrapWithModifiers("uint16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_SChar16:
		{
			if (typedValue.mInt16 != 0)
			{
				u8_toutf8(str, 8, typedValue.mUInt32);
				result = SlashString(str, true, true);
				if (!IsNormalChar(typedValue.mUInt32))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt16);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers(isBeef ? "char16" : "int16_t", origValueType, language);
		}
		break;
	case DbgType_UChar32:
	case DbgType_SChar32:
		{
			if (typedValue.mInt32 != 0)
			{
				u8_toutf8(str, 8, typedValue.mUInt32);
				result = SlashString(str, true, true);
				if (!IsNormalChar(typedValue.mUInt32))
					result = StrFormat("'%s' (0x%02X)\n", result.c_str(), typedValue.mUInt32);
				else
					result = StrFormat("'%s'\n", result.c_str());
			}
			else
				result = "'\\0'\n";
			return result + WrapWithModifiers(isBeef ? "char32" : "int32_t", origValueType, language);
		}
		break;
	case DbgType_i8:
		return IntTypeToString<int8>(typedValue.mInt8, WrapWithModifiers(isBeef ? "int8" : "int8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u8:
		return IntTypeToString<uint8>(typedValue.mUInt8, WrapWithModifiers(isBeef ? "uint8" : "uint8_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i16:
		return IntTypeToString<int16>(typedValue.mInt16, WrapWithModifiers(isBeef ? "int16" : "int16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u16:
		return IntTypeToString<uint16>(typedValue.mUInt16, WrapWithModifiers(isBeef ? "uint16" : "uint16_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i32:
		return IntTypeToString<int32>(typedValue.mInt32, WrapWithModifiers(isBeef ? "int32" : "int32_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u32:
		return IntTypeToString<uint32>(typedValue.mUInt32, WrapWithModifiers(isBeef ? "uint32" : "uint32_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_i64:
		return IntTypeToString<int64>(typedValue.mInt64, WrapWithModifiers(isBeef ? "int64" : "int64_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_u64:
		return IntTypeToString<uint64>(typedValue.mUInt64, WrapWithModifiers(isBeef ? "uint64" : "uint64_t", origValueType, language), displayInfo, formatInfo);
	case DbgType_RegGroup:
		{
			if ((typedValue.mRegNum >= CPUReg_M128_XMMREG_FIRST) && (typedValue.mRegNum <= CPUReg_M128_XMMREG_LAST))
			{
				int callStackIdx = formatInfo.mCallStackIdx;
				FixCallStackIdx(callStackIdx);

				UpdateRegisterUsage(callStackIdx);
				WdStackFrame* wdStackFrame = mCallStack[callStackIdx];
				RegForm regForm = RegForm_Unknown;
				if (typedValue.mRegNum < (int)wdStackFrame->mRegForms.size())
					regForm = wdStackFrame->mRegForms[typedValue.mRegNum];

				int xmmMajor = typedValue.mRegNum - CPUReg_M128_XMMREG_FIRST;
				String headerStr;

				String xmmType = "__m128";

				int xmmCount = 4;
				if ((regForm == RegForm_Double) || (regForm == RegForm_Double2) ||
					(regForm == RegForm_Long) || (regForm == RegForm_Long2) ||
					(regForm == RegForm_ULong) || (regForm == RegForm_ULong2))
					xmmCount = 2;
				//TODO: add byte, short, int, etc...

				if (optEvaluator)
				{
					DwMmDisplayType mmDwMmDisplayType = displayInfo->mMmDisplayType;
					if (mmDwMmDisplayType == DwMmDisplayType_Default)
					{
						if ((regForm == RegForm_Double) || (regForm == RegForm_Double2))
							mmDwMmDisplayType = DwMmDisplayType_Double;
						else if (regForm == RegForm_Int4)
							mmDwMmDisplayType = DwMmDisplayType_Int32;
					}

					if (mmDwMmDisplayType == DwMmDisplayType_Double)
					{
						xmmType = "__m128d";
						xmmCount = 2;
						double xmmRegVals[2];
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_Double);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mDouble;
						}
						headerStr = StrFormat("(%f, %f)", xmmRegVals[0], xmmRegVals[1]);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_UInt8)
					{
						int xmmRegVals[4];
						xmmCount = 16;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < BF_ARRAY_COUNT(xmmRegVals); ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
							xmmRegVals[0] & 0xFF, (xmmRegVals[0] >> 8) & 0xFF, (xmmRegVals[0] >> 16) & 0xFF, (xmmRegVals[0] >> 24) & 0xFF,
							xmmRegVals[1] & 0xFF, (xmmRegVals[1] >> 8) & 0xFF, (xmmRegVals[1] >> 16) & 0xFF, (xmmRegVals[1] >> 24) & 0xFF,
							xmmRegVals[2] & 0xFF, (xmmRegVals[2] >> 8) & 0xFF, (xmmRegVals[2] >> 16) & 0xFF, (xmmRegVals[2] >> 24) & 0xFF,
							xmmRegVals[3] & 0xFF, (xmmRegVals[3] >> 8) & 0xFF, (xmmRegVals[3] >> 16) & 0xFF, (xmmRegVals[3] >> 24) & 0xFF);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int16)
					{
						int xmmRegVals[4];
						xmmCount = 8;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < BF_ARRAY_COUNT(xmmRegVals); ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d, %d, %d, %d, %d)",
							xmmRegVals[0] & 0xFFFF, (xmmRegVals[0] >> 16) & 0xFFFF,
							xmmRegVals[1] & 0xFFFF, (xmmRegVals[1] >> 16) & 0xFFFF,
							xmmRegVals[2] & 0xFFFF, (xmmRegVals[2] >> 16) & 0xFFFF,
							xmmRegVals[3] & 0xFFFF, (xmmRegVals[3] >> 16) & 0xFFFF);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int32)
					{
						int xmmRegVals[4];
						xmmCount = 4;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i32);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt32;
						}
						headerStr = StrFormat("(%d, %d, %d, %d)", xmmRegVals[0], xmmRegVals[1], xmmRegVals[2], xmmRegVals[3]);
					}
					else if (mmDwMmDisplayType == DwMmDisplayType_Int64)
					{
						int64 xmmRegVals[2];
						xmmCount = 2;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_i64);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mInt64;
						}
						headerStr = StrFormat("(%lld, %lld)", xmmRegVals[0], xmmRegVals[1]);
					}
					else // float
					{
						float xmmRegVals[4];
						xmmCount = 4;
						CPURegisters* regs = optEvaluator->GetRegisters();
						for (int xmmMinor = 0; xmmMinor < xmmCount; ++xmmMinor)
						{
							DbgTypedValue xmmReg = GetRegister(StrFormat("xmm%d_%d", xmmMajor, xmmMinor), language, regs, &wdStackFrame->mRegForms);
							BF_ASSERT(xmmReg.mType->mTypeCode == DbgType_Single);
							BF_ASSERT(xmmReg.mRegNum == CPUReg_XMMREG_FIRST + (xmmMajor * 4) + xmmMinor);
							xmmRegVals[xmmMinor] = xmmReg.mSingle;
						}
						headerStr = StrFormat("(%f, %f, %f, %f)", xmmRegVals[0], xmmRegVals[1], xmmRegVals[2], xmmRegVals[3]);
					}
				}
				else
				{
					headerStr = StrFormat("XMM%d", xmmMajor);
				}

				result = headerStr + "\n" + xmmType;
				for (int i = 0; i < xmmCount; i++)
				{
					if (xmmCount == 16)
						result += WrapWithModifiers(StrFormat("\n[%d]\t(uint8)($xmm%d_%d >> %d)", i, xmmMajor, i / 4, (i % 4)*8), origValueType, language);
					else if (xmmCount == 8)
						result += WrapWithModifiers(StrFormat("\n[%d]\t(int16)($xmm%d_%d >> %d)", i, xmmMajor, i / 2, (i % 2)*8), origValueType, language);
					else
						result += WrapWithModifiers(StrFormat("\n[%d]\t$xmm%d_%d", i, xmmMajor, i), origValueType, language);
				}

				return result;
			}
			else
			{
				switch (typedValue.mRegNum)
				{
				case CPUReg_CAT_ALLREGS:
				{
					return "ALLREGS\n__allregs\niregs\t$iregs\nflags\t$flags\nfpregs\t$fpregs\nmmregs\t$mmregs\nxmmregs\t$xmmregs";
				}
				break;
				case CPUReg_CAT_IREGS:
				{
#ifdef BF_DBG_32
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = StrFormat("(eax=0x%08x, ebx=0x%08x, ecx=0x%08x, edx=0x%08x, esi=0x%08x, edi=0x%08x, esp=0x%08x, ebp=0x%08x, eip=0x%08x, efl=0x%08x)",
							(uint32)regs->mIntRegs.eax, (uint32)regs->mIntRegs.ebx, (uint32)regs->mIntRegs.ecx, (uint32)regs->mIntRegs.edx,
							(uint32)regs->mIntRegs.esi, (uint32)regs->mIntRegs.edi, (uint32)regs->mIntRegs.esp, (uint32)regs->mIntRegs.ebp,
							(uint32)regs->mIntRegs.eip, (uint32)regs->mIntRegs.efl);
					}
					else
					{
						headerStr = "IREGS";
					}

					return StrFormat("%s\n__iregs\neax\t$eax\nebx\t$ebx\necx\t$ecx\nedx\t$edx\nesi\t$esi\nedi\t$edi\nesp\t$esp\nebp\t$ebp\neip\t$eip", headerStr.c_str());
#else
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = StrFormat("(rax=0x%@, rbx=0x%@, rcx=0x%@, rdx=0x%@, rsi=0x%@, rdi=0x%@, rsp=0x%@, rbp=0x%@, eip=0x%@, r8=0x%@, r9=0x%@, r10=0x%@, r11=0x%@, r12=0x%@, r13=0x%@, r14=0x%@, r15=0x%@, efl=0x%08x)",
							(uint64)regs->mIntRegs.rax, (uint64)regs->mIntRegs.rbx, (uint64)regs->mIntRegs.rcx, (uint64)regs->mIntRegs.rdx,
							(uint64)regs->mIntRegs.rsi, (uint64)regs->mIntRegs.rdi, (uint64)regs->mIntRegs.rsp, (uint64)regs->mIntRegs.rbp,
							(uint64)regs->mIntRegs.rip,
							(uint64)regs->mIntRegs.r8, (uint64)regs->mIntRegs.r9, (uint64)regs->mIntRegs.r10, (uint64)regs->mIntRegs.r11,
							(uint64)regs->mIntRegs.r12, (uint64)regs->mIntRegs.r13, (uint64)regs->mIntRegs.r14, (uint64)regs->mIntRegs.r15,
							(uint32)regs->mIntRegs.efl);
					}
					else
					{
						headerStr = "IREGS";
					}

					return StrFormat("%s\n__iregs\neax\t$eax\nebx\t$ebx\necx\t$ecx\nedx\t$edx\nesi\t$esi\nedi\t$edi\nesp\t$esp\nebp\t$ebp\neip\t$eip\nr8\t$r8\nr9\t$r9\nr10\t$r10\nr11\t$r11\nr12\t$r12\nr13\t$r13\nr14\t$r14\nr15\t$r15", headerStr.c_str());
#endif
				}
				break;
				case CPUReg_CAT_FPREGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = "(";
						for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						{
							if (i)
								headerStr += ", ";
							double val = ConvertFloat80ToDouble(regs->mFpMmRegsArray[i].fp.fp80);
							headerStr += StrFormat("%f", val);
						}
						headerStr += ")";
					}
					else
					{
						headerStr = "FPREGS";
					}

					result = StrFormat("%s\n__fpregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						result += StrFormat("\n[%d]\t$st%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_MMREGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
						headerStr = "(";
						for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						{
							if (i)
								headerStr += ", ";
							uint64 val = regs->mFpMmRegsArray[i].mm;
							headerStr += StrFormat("0x%016llx", val);
						}
						headerStr += ")";
					}
					else
					{
						headerStr = "MMREGS";
					}

					result = StrFormat("%s\n__mmregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumFpMmRegs; ++i)
						result += StrFormat("\n[%d]\t$mm%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_XMMREGS:
				{
					String headerStr = StrFormat("XMMREGS[%d]", CPURegisters::kNumXmmRegs); // these are too big to put a useful header for the entire category

					result = StrFormat("%s\n__xmmregs", headerStr.c_str());
					for (int i = 0; i < CPURegisters::kNumXmmRegs; ++i)
						result += StrFormat("\n[%d]\t$xmm%d", i, i);

					return result;
				}
				break;
				case CPUReg_CAT_FLAGS:
				{
					String headerStr;
					if (optEvaluator)
					{
						CPURegisters* regs = optEvaluator->GetRegisters();
#ifdef BF_DBG_32
#define FLAGVAR(abbr, name) int flag##abbr = ((regs->mIntRegs.efl & ((uint64)1 << CPURegisters::GetFlagBitForRegister(X86Reg_FLAG_##abbr##_##name))) != 0) ? 1 : 0
						FLAGVAR(CF, CARRY);
						FLAGVAR(PF, PARITY);
						FLAGVAR(AF, ADJUST);
						FLAGVAR(ZF, ZERO);
						FLAGVAR(SF, SIGN);
						FLAGVAR(IF, INTERRUPT);
						FLAGVAR(DF, DIRECTION);
						FLAGVAR(OF, OVERFLOW);
#undef FLAGVAR
#else
#define FLAGVAR(abbr, name) int flag##abbr = ((regs->mIntRegs.efl & ((uint64)1 << CPURegisters::GetFlagBitForRegister(X64Reg_FLAG_##abbr##_##name))) != 0) ? 1 : 0
						FLAGVAR(CF, CARRY);
						FLAGVAR(PF, PARITY);
						FLAGVAR(AF, ADJUST);
						FLAGVAR(ZF, ZERO);
						FLAGVAR(SF, SIGN);
						FLAGVAR(IF, INTERRUPT);
						FLAGVAR(DF, DIRECTION);
						FLAGVAR(OF, OVERFLOW);
#undef FLAGVAR
#endif
						headerStr = StrFormat("(CF=%d, PF=%d, AF=%d, ZF=%d, SF=%d, IF=%d, DF=%d, OF=%d)",
							flagCF, flagPF, flagAF, flagZF, flagSF, flagIF, flagDF, flagOF);
					}
					else
					{
						headerStr = "FLAGS";
					}

					return StrFormat("%s\n__flags\nCarry (CF)\t$flagcf\nParity (PF)\t$flagpf\nAdjust (AF)\t$flagaf\nZero (ZF)\t$flagzf\nSign (SF)\t$flagsf\nInterrupt (IF)\t$flagif\nDirection (DF)\t$flagdf\nOverflow (OF)\t$flagof",
						headerStr.c_str());
				}
				break;
				default:
					BF_ASSERT(false && "unknown category register");
					return "UNKNOWNCATEGORY\n__unknown\n";
				}
			}
		}
		break;
	case DbgType_Single:
		{
			DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
			if (floatDisplayType == DwFloatDisplayType_Default)
				floatDisplayType = DwFloatDisplayType_Minimal;
			if (floatDisplayType == DwFloatDisplayType_Minimal)
				ExactMinimalFloatToStr(typedValue.mSingle, str);
			else if (floatDisplayType == DwFloatDisplayType_Full)
				sprintf(str, "%1.9g", typedValue.mSingle);
			else if (floatDisplayType == DwFloatDisplayType_HexUpper)
				sprintf(str, "0x%04X", typedValue.mUInt32);
			else //if (floatDisplayType == DwFloatDisplayType_HexLower)
				sprintf(str, "0x%04x", typedValue.mUInt32);
			return StrFormat("%s\n%s", str, WrapWithModifiers("float", origValueType, language).c_str());
		}
	case DbgType_Double:
		{
			DwFloatDisplayType floatDisplayType = displayInfo->mFloatDisplayType;
			if (floatDisplayType == DwFloatDisplayType_Default)
				floatDisplayType = DwFloatDisplayType_Minimal;
			if (floatDisplayType == DwFloatDisplayType_Minimal)
				ExactMinimalDoubleToStr(typedValue.mDouble, str);
			else if (floatDisplayType == DwFloatDisplayType_Full)
				sprintf(str, "%1.17g", typedValue.mDouble);
			else if (floatDisplayType == DwFloatDisplayType_HexUpper)
				sprintf(str, "0x%08llX", typedValue.mUInt64);
			else //if (floatDisplayType == DwFloatDisplayType_HexLower)
				sprintf(str, "0x%08llx", typedValue.mUInt64);
			return StrFormat("%s\n%s", str, WrapWithModifiers("double", origValueType, language).c_str());
		}
	case DbgType_Subroutine:
		if (typedValue.mCharPtr != NULL)
			return StrFormat("%s\nfunc", typedValue.mCharPtr);
		else
			return "\nfunc";
	case DbgType_RawText:
		return StrFormat("%s\nrawtext", typedValue.mCharPtr);
	case DbgType_Ptr:
	{
		addr_target ptrVal = (addr_target)typedValue.mPtr;
		String retVal;
		DbgType* innerType = dwValueType->mTypeParam;
		if (innerType == NULL)
			return EncodeDataPtr(ptrVal, true) + "\nvoid*";

		bool isChar = false;

		DbgType* unmodInnerType = innerType->RemoveModifiers();
		if (unmodInnerType != NULL)
		{
			if (language == DbgLanguage_Beef)
			{
				if ((unmodInnerType->mTypeCode == DbgType_UChar) ||
					(unmodInnerType->mTypeCode == DbgType_UChar16) ||
					(unmodInnerType->mTypeCode == DbgType_UChar32))
					isChar = true;
			}
			else
			{
				if ((unmodInnerType->mTypeCode == DbgType_SChar) ||
					(unmodInnerType->mTypeCode == DbgType_SChar16) ||
					(unmodInnerType->mTypeCode == DbgType_SChar32))
					isChar = true;
			}
		}

		if ((isChar) && (formatInfo.mArrayLength == -1))
		{
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true);

			int strLen = formatInfo.mOverrideCount;
			if (typedValue.mIsLiteral)
			{
				if (strLen == -1)
					strLen = 0x7FFFFFFF;
				if (typedValue.mDataLen > 0)
					strLen = BF_MIN(strLen, typedValue.mDataLen);
				else
					strLen = BF_MIN(strLen, strlen(typedValue.mCharPtr));
			}

			SetAndRestoreValue<intptr> prevOverrideLen(formatInfo.mOverrideCount, strLen);
			String strResult = ReadString(unmodInnerType->mTypeCode, typedValue.mLocalIntPtr, typedValue.mIsLiteral, strLen, formatInfo, wantStringView);
			if (formatInfo.mRawString)
				return strResult;
			if (!strResult.IsEmpty())
			{
				if (!retVal.IsEmpty())
					retVal += " ";
				if (!wantStringView)
					retVal += strResult;
			}
			retVal += "\n" + origValueType->ToString(language);
			retVal += "\n:stringView";
			if (wantStringView)
			{
				retVal += "\t";
				retVal += SlashString(strResult, false, false, true);
			}
			return retVal;
		}
		else if ((unmodInnerType != NULL) &&
			((unmodInnerType->mTypeCode == DbgType_Class) || (unmodInnerType->mTypeCode == DbgType_Struct) || (unmodInnerType->mTypeCode == DbgType_Union)))
		{
			isCompositeType = true;
		}
		else if ((unmodInnerType != NULL) && (unmodInnerType->mTypeCode == DbgType_SizedArray))
		{
			isSizedArray = true;
		}
		else if (unmodInnerType->mTypeCode == DbgType_Subroutine)
		{
			if (formatInfo.mRawString)
				return "";
			addr_target funcPtr = (addr_target)typedValue.mPtr;
			String retVal;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(funcPtr, true);

			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			static String demangledName;
			auto subProgram = mDebugTarget->FindSubProgram(funcPtr);
			if (subProgram != NULL)
			{
				demangledName = subProgram->ToString();
			}
			else if (mDebugTarget->FindSymbolAt(funcPtr, &symbolName, &offset, &dwarf))
			{
				demangledName = BfDemangler::Demangle(symbolName, language);

				if (offset != 0)
					demangledName += StrFormat("+%d", offset);
			}
			else
			{
				auto dbgModule = mDebugTarget->FindDbgModuleForAddress(funcPtr);
				if (dbgModule != NULL)
					demangledName += dbgModule->GetLinkedModule()->mDisplayName + "!";
				demangledName += StrFormat("0x%@", funcPtr);
			}

			retVal += " {";
			retVal += demangledName;
			retVal += "}";
			retVal += "\n" + origValueType->ToString(language);

			return retVal;
		}
		else if (unmodInnerType->mTypeCode == DbgType_Void)
		{
			if (formatInfo.mRawString)
				return "";
			addr_target ptr = (addr_target)typedValue.mPtr;
			String symbolName;
			addr_target offset;
			DbgModule* dwarf;
			String demangledName;

			retVal += demangledName = StrFormat("0x%@", ptr);

			if (mDebugTarget->FindSymbolAt(ptr, &symbolName, &offset, &dwarf))
			{
				if (offset == 0)
				{
					retVal += " {";
					retVal += BfDemangler::Demangle(symbolName, language);
					retVal += "}";
				}
			}

			retVal += "\n" + origValueType->ToString(language);

			return retVal;
		}
		else
		{
			if (formatInfo.mRawString)
				return "";
			addr_target ptrVal = (addr_target)typedValue.mPtr;
			String retVal;
			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true);

			if (ptrVal != 0)
			{
				DbgExprEvaluator dbgExprEvaluator(this, dbgModule, NULL, -1, -1);
				DbgTypedValue innerTypedVal = dbgExprEvaluator.ReadTypedValue(NULL, innerType, typedValue.mPtr, DbgAddrType_Target);
				if (innerTypedVal)
				{
					DwFormatInfo defaultFormatInfo;
					defaultFormatInfo.mLanguage = formatInfo.mLanguage;
					defaultFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + 2; // Take into accout the necessary {}'s
					defaultFormatInfo.mExpandItemDepth++;
					String innerStr = DbgTypedValueToString(innerTypedVal, "", defaultFormatInfo, &dbgExprEvaluator);
					int crIdx = innerStr.IndexOf('\n');
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
						retVal += "{ ??? }";
					}
				}
			}

			retVal += "\n" + origValueType->ToString(language);
			innerType->PopulateType();

			if ((ptrVal != 0) &&
				((!innerType->mMemberList.IsEmpty()) || (innerType->mSize > 0) || (innerType->mTypeParam != NULL)))
			{
				String ptrDataStr = StrFormat("(%s)", dwValueType->ToStringRaw(language).c_str()) + EncodeDataPtr(typedValue.mPtr, true);
				retVal += "\n*\t";
				// Why did we have this?  It messed up a pointer to sized array
				/*if (language == DbgLanguage_Beef)
					retVal += "this";
				else*/
					retVal += "*this";

				if (!formatInfo.mReferenceId.empty())
					retVal += ", refid=" + MaybeQuoteFormatInfoParam(formatInfo.mReferenceId);

				retVal += ", this=" + ptrDataStr;
			}
			return retVal;
		}

		break;
	}
	case DbgType_Union:
	case DbgType_Class:
	case DbgType_Struct:
		isCompositeType = true;
		break;
	case DbgType_Enum:
		enumVal = typedValue.GetInt64();
		isEnum = true;
		break;
	case DbgType_SizedArray:
		{
			isSizedArray = true;
		}
		break;
	default:
		break;
	}

	if (isSizedArray)
	{
		String retVal;
		addr_target ptrVal = 0;

		DbgType* arrayType = dwValueType;
		DbgType* innerType = dwValueType->mTypeParam;
		if (dwValueType->mTypeCode == DbgType_SizedArray)
		{
			ptrVal = (addr_target)typedValue.mSrcAddress;
		}
		else
		{
			BF_ASSERT(dwValueType->mTypeCode == DbgType_Ptr);
			arrayType = innerType;
			innerType = arrayType->mTypeParam;
			ptrVal = typedValue.mPtr;

			if ((!typedValue.mIsLiteral) && (!formatInfo.mHidePointers))
				retVal = EncodeDataPtr(ptrVal, true) + " ";
		}
		if (ptrVal == 0)
			ptrVal = typedValue.mPtr;

		intptr arraySize = 0;
		intptr innerSize = innerType->GetStride();
		if (innerSize > 0)
			arraySize = arrayType->GetStride() / innerSize;
		else
		{
			// Failure!
		}

		String idxStr = "[{0}]";

		if (innerType->IsChar(language))
		{
			String strVal = ReadString(innerType->mTypeCode, typedValue.mSrcAddress, false, arraySize, formatInfo, false);
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

		retVal += "\n" + origValueType->ToString(language);

		String referenceId = dwValueType->ToString(language);
		String evalStr;

		// Why did we have the "na"? Do we not want to show addresses for all members?
		evalStr = "((" + innerType->ToStringRaw(language) + "*)" + EncodeDataPtr(ptrVal, true) + ")[{0}], refid=" + MaybeQuoteFormatInfoParam(referenceId + ".[]");
		if (typedValue.mIsReadOnly)
			evalStr += ", ne";
		retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, (int)BF_MAX(arraySize, 0), 10000) +
			"\t" + idxStr + "\t" + evalStr;
		return retVal;
	}

	dwValueType->PopulateType();

	if (isEnum)
	{
		String retVal;
		int64 bitsLeft = enumVal;
		int valueCount = 0;

		String editVal;

		dwValueType = dwValueType->GetPrimaryType();

		dwValueType->PopulateType();

		while ((bitsLeft != 0) || (valueCount == 0))
		{
			DbgVariable* bestMatch = NULL;

			for (auto member : dwValueType->mMemberList)
			{
				if (member->mConstValue == bitsLeft)
				{
					bestMatch = member;
					break;
				}
			}

			if (bestMatch == NULL)
			{
				for (auto member : dwValueType->mMemberList)
				{
					if ((member->mConstValue != 0) &&
						((member->mConstValue & bitsLeft) == member->mConstValue))
					{
						bestMatch = member;
						break;
					}
				}
			}

			if (bestMatch == NULL)
				break;

			if (valueCount > 0)
			{
				retVal += " | ";
				if (language == DbgLanguage_C)
					editVal += " | ";
			}

			if (language == DbgLanguage_Beef)
				retVal += ".";
			retVal += bestMatch->mName;

			if (language == DbgLanguage_C)
			{
				if (dwValueType->mParent != NULL)
				{
					editVal += dwValueType->mParent->ToString(language);
					editVal += "::";
				}
				editVal += bestMatch->mName;
			}

			valueCount++;
			bitsLeft &= ~bestMatch->mConstValue;
		}

		if ((valueCount == 0) || (bitsLeft != 0))
		{
			if (valueCount > 0)
				retVal += " | ";
			retVal += StrFormat("%lld", bitsLeft);

			if (language == DbgLanguage_C)
			{
				if (valueCount > 0)
					editVal += " | ";
				editVal += StrFormat("%lld", bitsLeft);
			}
		}

		retVal += "\n" + origValueType->ToString();
		if (language == DbgLanguage_C)
		{
			retVal += "\n:editVal\t";
			retVal += editVal;
		}
		retVal += "\n:canEdit";

		return retVal;
	}
	else if (isCompositeType)
	{
		addr_target ptrVal;
		if (dwValueType->IsPointer())
			ptrVal = (addr_target)typedValue.mPtr;
		else
			ptrVal = (addr_target)typedValue.mSrcAddress;
		String retVal;
		if ((!typedValue.mIsLiteral) && (dwValueType->IsPointer()) &&
			((!formatInfo.mHidePointers) || (ptrVal == 0)))
			retVal = EncodeDataPtr(ptrVal, true);

		DbgType* innerType = dwValueType;
		bool wasPtr = false;
		if (innerType->mTypeCode == DbgType_Ptr)
		{
			wasPtr = true;
			innerType = dwValueType->mTypeParam;
			innerType = innerType->RemoveModifiers();
		}

		innerType = innerType->GetPrimaryType();
		addr_target dataPtr = wasPtr ? typedValue.mPtr : typedValue.mSrcAddress;
		DbgType* actualType = NULL;
		bool useActualRawType = false;

		bool isBfObject = innerType->IsBfObject();
		bool hasCPPVTable = false;
		if (!isBfObject)
			hasCPPVTable = innerType->HasCPPVTable();

		int bfObjectFlags = 0;
		addr_target classVDataPtr = 0;
		bool isAppendBfObject = false;
		bool isStackBfObject = false;
		bool isDeletedBfObject = false;
		bool isCompositeWithoutAddress = false;

		if (innerType->IsBfPayloadEnum())
		{
			if (formatInfo.mRawString)
				return "";

			auto tagMember = innerType->mMemberList.mTail;
			int tagIdx = 0;

			if (dataPtr == -1)
			{
				DbgEvaluationContext dbgEvaluationContext(this, dbgModule, "(int)" + expr, &formatInfo);
				auto dscValue = dbgEvaluationContext.EvaluateInContext(DbgTypedValue());
				tagIdx = dscValue.mInt32;
			}
			else if (!ReadMemory((intptr)ptrVal + tagMember->mMemberOffset, tagMember->mType->mSize, (void*)&tagIdx))
			{
				return StrFormat("!Failed to read from 0x%@", ptrVal);
			}

			char findStr[16];
			findStr[0] = '_';
			itoa(tagIdx, findStr + 1, 10);
			int len = strlen(findStr);
			findStr[len] = '_';
			len++;

			if (!retVal.empty())
				retVal += " ";

			int startIdx = 0;
			for (auto member : innerType->mMemberList)
			{
				if (strncmp(member->mName, findStr, len) == 0)
				{
					retVal += ".";
					retVal += member->mName + len;

					String tupleExpr;

					DbgTypedValue tupleVal;
					if (dataPtr == -1)
					{
						tupleVal.mSrcAddress = -1;
						tupleVal.mType = member->mType;
						//tupleExpr = "$" + expr + "$u";
						tupleVal.mVariable = typedValue.mVariable;
						tupleExpr = "(" + member->mType->ToStringRaw() + ")(" + expr + ")";
					}
					else
					{
						tupleVal.mType = member->mType;
						tupleVal.mSrcAddress = ptrVal;
					}

					DwFormatInfo displayStrFormatInfo = formatInfo;
					displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + (int)retVal.length();
					displayStrFormatInfo.mExpandItemDepth++;
					displayStrFormatInfo.mHidePointers = false;
					retVal += DbgTypedValueToString(tupleVal, tupleExpr, displayStrFormatInfo, NULL);
					int idx = (int)retVal.IndexOf('\n');
					if (idx != -1)
					{
						if ((idx > 2) && (strncmp(retVal.c_str() + idx - 2, "()", 2) == 0))
						{
							// Take off a terminating "()" on the value, if there is one
							retVal.Remove(idx - 2, 2);
						}

						String typeName = innerType->ToString(DbgLanguage_Unknown, true);
						typeName += " ";
						retVal.Insert(idx + 1, typeName);
					}

					return retVal;
				}
			}
		}

		if (isBfObject)
		{
			classVDataPtr = ReadMemory<addr_target>(ptrVal);
			mDebugTarget->GetCompilerSettings();
			if (mDebugTarget->mBfObjectHasFlags)
			{
				bfObjectFlags = ((int)classVDataPtr) & 0xFF;

				if ((bfObjectFlags & BfObjectFlag_Deleted) != 0)
					isDeletedBfObject = true;
				if ((bfObjectFlags & BfObjectFlag_AppendAlloc) != 0)
					isAppendBfObject = true;
				if ((bfObjectFlags & (BfObjectFlag_StackAlloc | BfObjectFlag_Allocated)) == BfObjectFlag_StackAlloc)
					isStackBfObject = true;

				classVDataPtr &= ~0xFF;
			}
		}

		if (!formatInfo.mIgnoreDerivedClassInfo)
		{
			if (isBfObject)
			{
				dbgModule->ParseSymbolData();

				String symbolName;
				addr_target symOffset;
				if ((mDebugTarget->FindSymbolAt(classVDataPtr, &symbolName, &symOffset)) && (symOffset < 0x100))
				{
					String mangledClassName;

					const char* symEnd = "sBfClassVData";
					int symEndLen = strlen(symEnd);
					if (((int)symbolName.length() > symEndLen) && (strstr(symbolName.c_str(), symEnd) != NULL))
						mangledClassName = symbolName;

					// If we have flags then we may be pointing past the _typeData, actually.  We could fix this by masking out
					//  the flags area, but we need to be sure we are running a build that supports flags
					symEnd = "sBfTypeData";
					symEndLen = strlen(symEnd);
					if (((int) symbolName.length() > symEndLen) && (strstr(symbolName.c_str(), symEnd) != NULL))
						mangledClassName = symbolName;

					if (mangledClassName.length() > 0)
					{
						String className = BfDemangler::Demangle(mangledClassName, innerType->GetLanguage(), BfDemangler::Flag_RawDemangle);

						for (int i = 0; i < className.length() - 3; i++)
						{
							if ((className[i] == 'b') &&
								(className[i + 1] == 'f') &&
								(className[i + 2] == '.'))
							{
								bool matches;
								if (i == 0)
									matches = true;
								else
								{
									char prevC = className[i - 1];
									if ((prevC == ' ') ||
										(prevC == ',') ||
										(prevC == '<'))
									{
										matches = true;
									}
								}
								if (matches)
									className.Remove(i, 3);
							}
						}

						int lastDot = (int)className.LastIndexOf('.');
						if (lastDot > 0)
							className = className.Substring(0, lastDot);

						const char* arrPrefix = "System.Array1<";
						if (strncmp(className.c_str(), arrPrefix, strlen(arrPrefix)) == 0)
						{
							className = className.Substring(strlen(arrPrefix), className.length() - strlen(arrPrefix) - 1);
							className += "[]";
						}

						auto typeEntry = dbgModule->GetLinkedModule()->mTypeMap.Find(className.c_str(), DbgLanguage_BeefUnfixed);
						if (typeEntry != NULL)
						{
							actualType = typeEntry->mValue;
							if (!actualType->IsBfObject())
							{
								if (actualType->mTypeCode == DbgType_Ptr)
								{
									actualType = actualType->mTypeParam;
								}
							}
						}
					}
				}
			}
			else if (hasCPPVTable)
			{
				dbgModule->ParseSymbolData();

				addr_target classVDataPtr = ReadMemory<addr_target>(ptrVal);
				String symbolName;

				addr_target offset = 0;
				if (mDebugTarget->FindSymbolAt(classVDataPtr, &symbolName, &offset, NULL))
				{
					// On GNU, vtable indices can "go negative" for things like RTTI and virtual inheritance, so
					//  we can't rely on an exact vtable address lookup
					if (offset < 0x200)
					{
						DbgLanguage lang = innerType->GetLanguage();
						const char* symStart = (innerType->mCompileUnit->mDbgModule->mDbgFlavor == DbgFlavor_GNU) ? "_ZTV" : "??_7";
						if (strncmp(symbolName.c_str(), symStart, strlen(symStart)) == 0)
						{
							//String mangledClassName = symbolName.Substring(1);
							String className = BfDemangler::Demangle(symbolName, lang);
							int vtableNameIdx = (int)className.IndexOf("::`vftable'");
							if (vtableNameIdx != -1)
								className = className.Substring(0, vtableNameIdx);

							auto typeEntry = dbgModule->mTypeMap.Find(className.c_str(), DbgLanguage_C);
							if (typeEntry != NULL)
							{
								actualType = typeEntry->mValue;

								if ((int)className.IndexOf('<') != -1)
									useActualRawType = true;

								int thisOffset = 0;
								if (!DbgExprEvaluator::TypeIsSubTypeOf(actualType, innerType, &thisOffset))
								{
									// This catches virtual inheritance cases where we can't downcast
									actualType = NULL;
								}
							}
						}
					}
				}
			}
		}

		DbgType* displayType = origValueType;

		String displayString;
		bool wantsCustomExpandedItems = false;
		DebugVisualizerEntry* debugVis = NULL;
		Array<String> dbgVisWildcardCaptures;
		DbgType* dwUseType = (actualType != NULL) ? actualType : innerType;
		//auto ptrDataType = dwValueType;
		//TODO: Changed this from the above to account for COFF types where 'this' is always a fwd reference, does this cause any issues?
		auto ptrDataType = innerType;
		String ptrDataStr;
		if (/*(!innerType->IsBfObject()) &&*/ (!ptrDataType->IsPointer()))
		{
			if ((dataPtr != 0) || (ptrDataType->GetByteCount() > sizeof(addr_target)))
			{
				bool wantsRefThis = ptrDataType->WantsRefThis();
				ptrDataType = ptrDataType->GetDbgModule()->GetPointerType(ptrDataType);
				if (wantsRefThis)
					ptrDataStr += "*";
			}
			else
			{
				// Data is inline - must be int-sized or less
				isCompositeWithoutAddress = true;
				dataPtr = typedValue.mPtr;
			}
		}
		String ptrDataTypeStr = ptrDataType->ToStringRaw();
		ptrDataStr += StrFormat("(%s)", ptrDataTypeStr.c_str()) + EncodeDataPtr(dataPtr, true);

		DbgType* dwUsePtrType = dwUseType;
		String ptrUseDataStr;
		if (!dwUsePtrType->IsPointer())
		{
			bool wantsRefThis = dwUsePtrType->WantsRefThis();
			dwUsePtrType = dwUsePtrType->GetDbgModule()->GetPointerType(dwUsePtrType);
			if (wantsRefThis)
				ptrUseDataStr += "*";
		}
		String ptrUseDataTypeStr = dwUsePtrType->ToStringRaw();
		ptrUseDataStr += StrFormat("(%s)", ptrUseDataTypeStr.c_str()) + EncodeDataPtr(dataPtr, true);

		if ((origTypedValue.mSrcAddress == -1) && (origTypedValue.mVariable != NULL))
		{
			ptrDataStr = origTypedValue.mVariable->mName;
			if (!origTypedValue.mType->RemoveModifiers()->Equals(origTypedValue.mVariable->mType->RemoveModifiers()))
			{
				//ptrDataStr = StrFormat("(%s)%s", origTypedValue.mType->ToString().c_str(), origTypedValue.mVariable->mName);
				ptrDataStr = expr;
			}
			ptrUseDataStr = ptrDataStr;
		}

		bool isNull = wasPtr && (dataPtr == 0);
		bool isBadSrc = !wasPtr && (dataPtr == 0) && (!dwValueType->IsValuelessType());

		if ((ptrVal == 0) && (dwValueType->IsTypedPrimitive()))
		{
			DbgTypedValue rawVal;
			rawVal.mInt64 = origTypedValue.mInt64;
			rawVal.mType = dwValueType->GetRootBaseType();

			ptrDataStr = "(" + dwUseType->ToStringRaw() + ")";
			ptrDataStr += DbgTypedValueToString(rawVal, expr, formatInfo, optEvaluator, fullPrecision);

			int editValIdx = ptrDataStr.IndexOf(":editVal");
			if (editValIdx != -1)
				ptrDataStr.Remove(0, editValIdx + 9);
			int crPos = (int)ptrDataStr.IndexOf('\n');
			if (crPos != -1)
				ptrDataStr.RemoveToEnd(crPos);
			ptrUseDataStr = ptrDataStr;

			if ((origTypedValue.mRegNum != -1) && (!expr.IsEmpty()) && (!formatInfo.mExplicitThis))
			{
				// There's no address, use direct local identifier
				ptrDataStr = expr;
				ptrUseDataStr = expr;
			}

			// This keeps 'function' types from showing null as "<null parent>"
			isBadSrc = false;
		}
		else if ((ptrVal == 0) && (dwValueType->IsCompositeType()))
		{
		}

		DbgTypedValue useTypedValue = typedValue;
		if ((origHadRef) || ((typedValue.mType->HasPointer()) && (!dwUseType->HasPointer())))
		{
			useTypedValue.mSrcAddress = useTypedValue.mPtr;
			useTypedValue.mPtr = 0;

			if (dwUseType->IsTypedPrimitive())
			{
				int byteCount = dwUseType->GetByteCount();
				if (byteCount <= sizeof(intptr))
				{
					ReadMemory(useTypedValue.mSrcAddress, byteCount, &useTypedValue.mPtr);
				}
			}
		}
		useTypedValue.mType = dwUseType;
		if ((!formatInfo.mNoVisualizers) && (!isNull) && (!isBadSrc))
		{
			if (language == DbgLanguage_Beef)
				dwUseType->FixName();
			debugVis = FindVisualizerForType(dwUseType, &dbgVisWildcardCaptures);
		}

		bool hadCustomDisplayString = false;
		if (debugVis != NULL)
		{
			auto& displayStringList = (formatInfo.mRawString || wantStringView) ? debugVis->mStringViews : debugVis->mDisplayStrings;

			for (auto displayEntry : displayStringList)
			{
				if (!displayEntry->mCondition.empty())
				{
					if (!EvalCondition(debugVis, dbgCompileUnit, useTypedValue, formatInfo, displayEntry->mCondition, dbgVisWildcardCaptures, displayString))
						continue;
				}

				hadCustomDisplayString = true;
				String displayStr = mDebugManager->mDebugVisualizers->DoStringReplace(displayEntry->mString, dbgVisWildcardCaptures);
				if (displayString.length() > 0)
					displayString += " ";

				if (wantStringView)
				{
					DwFormatInfo strFormatInfo = formatInfo;
					strFormatInfo.mRawString = true;
					ProcessEvalString(dbgCompileUnit, useTypedValue, displayStr, stringViewData, strFormatInfo, debugVis, true);
				}
				else
					ProcessEvalString(dbgCompileUnit, useTypedValue, displayStr, displayString, formatInfo, debugVis, true);
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

		bool isTuple = (dwUseType->mName != NULL) && (dwUseType->mName[0] == '(') && (language == DbgLanguage_Beef);

		if (isBadSrc)
		{
			displayString += "<null parent>";
		}
		else if ((!isNull) && (!formatInfo.mNoVisualizers) && (!hadCustomDisplayString))
		{
			// Create our own custom display

			String firstRet;
			String bigRet = isTuple ? "(" : "{ ";

			int memberIdx = 0;
			DbgType* summaryType = dwUseType;
			bool summaryDone = false;
			bool truncatedMemberList = false;

			DbgTypedValue summaryTypedValue = useTypedValue;
			String summaryDataStr = ptrDataStr;
			String splatStr;
			if (dataPtr == -1)
				splatStr = expr;

			while (summaryType != NULL)
			{
				summaryType->PopulateType();

				if ((summaryType->IsTypedPrimitive()) &&
					((summaryType->mBaseTypes.IsEmpty()) || (!summaryType->mBaseTypes.front()->mBaseType->IsTypedPrimitive())))
				{
					if (formatInfo.mTotalSummaryLength + (int)displayString.length() > 255)
					{
						truncatedMemberList = true;
						summaryDone = true;
						bigRet += "...";
					}
					else
					{
						DwFormatInfo displayStrFormatInfo = formatInfo;
						displayStrFormatInfo.mExpandItemDepth = 1;
						displayStrFormatInfo.mTotalSummaryLength += (int)displayString.length();
						displayStrFormatInfo.mHidePointers = false;

						DbgType* primType = summaryType->mTypeParam;
						String result;

						if (primType->IsInteger())
							formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

						if ((dataPtr != 0) && (dataPtr != -1))
						{
							String evalString = "(" + primType->ToString() +  ")" + ptrDataStr;
							DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, origTypedValue, evalString, &displayStrFormatInfo);
							if (evalResult)
								result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}
						else
						{
							DbgTypedValue evalResult = origTypedValue;
							evalResult.mType = primType;
							String evalString = "(" + primType->ToString() + ")" + expr;
							result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
						}

						if (formatInfo.mRawString)
							return result;

						int crPos = result.IndexOf('\n');
						if (crPos != -1)
							result.RemoveToEnd(crPos);

						if (memberIdx == 0)
							firstRet = result;

						bigRet += result;
						memberIdx++;
					}
				}

				for (auto member : summaryType->mMemberList)
				{
					if (!member->mIsStatic)
					{
						if (formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length() > 255)
						{
							truncatedMemberList = true;
							summaryDone = true;
							bigRet += "...";
							break;
						}

						if (member->mName != NULL)
						{
							if (member->mName[0] == '$')
								continue;

							if (!isdigit(*member->mName))
							{
								if (memberIdx != 0)
									bigRet += isTuple ? ", " : " ";

								if ((!isTuple) || (member->mName[0] != '_'))
								{
									bigRet += String(member->mName);
									bigRet += isTuple ? ":" : "=";
								}
							}
							else
							{
								if (memberIdx != 0)
									bigRet += ", ";
							}

							DwFormatInfo displayStrFormatInfo = formatInfo;
							displayStrFormatInfo.mExpandItemDepth = 1;
							displayStrFormatInfo.mHidePointers = false;
							displayStrFormatInfo.mTotalSummaryLength = formatInfo.mTotalSummaryLength + retVal.length() + bigRet.length();

							String evalString;
							if (dataPtr != -1)
							{
								if ((member->mName[0] >= '0') && (member->mName[0] <= '9'))
									evalString += "this.";
								evalString += String(member->mName); // +", this=" + summaryDataStr;
							}
							else
							{
								evalString = "(";
								evalString += splatStr;
								evalString += ").";
								evalString += member->mName;
							}
							String referenceId;
							String result;
							if (!member->mType->IsValuelessType())
							{
								DbgTypedValue evalResult = EvaluateInContext(dbgCompileUnit, summaryTypedValue, evalString, &displayStrFormatInfo, &referenceId);
								if (evalResult)
								{
									displayStrFormatInfo.mReferenceId = referenceId;
									result = DbgTypedValueToString(evalResult, evalString, displayStrFormatInfo, NULL);
									int crPos = result.IndexOf('\n');
									if (crPos != -1)
										result.RemoveToEnd(crPos);
								}
								else
									result = "???";
							}

							if (member->mType->IsInteger())
								formatInfo.mTypeKindFlags = (DbgTypeKindFlags)(formatInfo.mTypeKindFlags | DbgTypeKindFlag_Int);

							if (formatInfo.mRawString)
								return result;

							if (memberIdx == 0)
								firstRet = result;

							bigRet += result;
							//formatInfo.mEmbeddedDisplayCount = displayStrFormatInfo.mEmbeddedDisplayCount;
							memberIdx++;
						}
						else
						{
							//TODO: Handle C++ unions?
						}
					}
				}

				if (truncatedMemberList)
					break;

				// Find first base class with members
				DbgType* nextSummaryType = NULL;
				for (auto checkBase : summaryType->mBaseTypes)
				{
					auto checkBaseType = checkBase->mBaseType;
					checkBaseType = checkBaseType->GetPrimaryType();
					checkBaseType->PopulateType();
					if ((checkBaseType->GetByteCount() > 0) || (checkBaseType->IsPrimitiveType()))
					{
						if (!splatStr.empty())
						{
							splatStr = "(" + checkBaseType->ToString() + ")" + splatStr;
						}
						else
						{
							summaryTypedValue.mType = checkBaseType;
						}
						nextSummaryType = checkBaseType;
						break;
					}
				}
				summaryType = nextSummaryType;

				if (summaryType == NULL)
					break;

				// Don't add the Object members
				if ((summaryType->GetBaseType() == NULL) && (summaryType->IsBfObject()))
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

		DbgType* memberListType = actualType;
		bool memberListForceCast = false;
		if (actualType != NULL)
		{
			String valTypeName = displayType->ToString();
			String actualTypeName = actualType->ToString(DbgLanguage_Unknown, true);
			String actualUseTypeName = actualTypeName;

			if ((int)actualTypeName.IndexOf('^') != -1)
				useActualRawType = true;

			if (useActualRawType)
				actualUseTypeName = actualType->ToStringRaw();

			if (displayString.empty())
			{
				// Nothing to display
			}
			else
			{
				if (!retVal.empty())
					retVal += " ";
				retVal += displayString;
			}

			retVal += "\n" + valTypeName;

			if ((innerType->IsBaseBfObject()) || (innerType->IsInterface()))
			{
				if (actualType != innerType)
				{
					retVal += " {" + actualTypeName + "}";
					memberListForceCast = true;
				}
			}
			else
			{
				if (actualType != innerType)
				{
					retVal += " {" + actualTypeName + "}";
					retVal += "\n";
					if (!wantsCustomExpandedItems)
					{
						retVal += "[" + actualTypeName + "]\t((" + actualUseTypeName;
						if (!actualType->IsBfObject())
							retVal += "*";
						retVal += ")this), nd, na, nv, this=" + ptrDataStr;
						memberListType = innerType;
					}
				}
			}
		}
		else
		{
			if ((formatInfo.mHidePointers) && (formatInfo.mIgnoreDerivedClassInfo))
			{
				displayType = innerType;
				if (displayString.empty())
					retVal += displayType->ToString(DbgLanguage_Unknown, true);
			}

			if (!displayString.empty())
			{
				if (!retVal.empty())
					retVal += " ";
				retVal += displayString;
			}
			else
			{
				if (formatInfo.mRawString)
					return "";
			}

			retVal += "\n" + displayType->ToString(DbgLanguage_Unknown, true);
			memberListType = innerType;
		}

		if ((isBfObject) && (mDebugTarget->mBfObjectHasFlags) && (!formatInfo.mNoVisualizers) && (!formatInfo.mRawString))
		{
			int stackTraceLen = 1;
			addr_target stackTraceAddr = ptrVal + sizeof(addr_target);
			if ((bfObjectFlags & BfObjectFlag_AllocInfo) != 0)
			{
				addr_target objectSize = ReadMemory<addr_target>(ptrVal + sizeof(addr_target));
				addr_target largeAllocInfo = ReadMemory<addr_target>(ptrVal + objectSize);
				stackTraceLen = largeAllocInfo & 0xFFFF;
				stackTraceAddr = ptrVal + objectSize + sizeof(addr_target);
			}
			else if ((bfObjectFlags & BfObjectFlag_AllocInfo_Short) != 0)
			{
				addr_target dbgAllocInfo = ReadMemory<addr_target>(ptrVal + sizeof(addr_target));
				stackTraceLen = dbgAllocInfo & 0xFF;
				stackTraceAddr = ptrVal + (dbgAllocInfo >> 16);
			}

			retVal += StrFormat("\n[AllocStackTrace]\t(System.CallStackList)%s, count=%d, na", EncodeDataPtr(stackTraceAddr, true).c_str(), stackTraceLen);
		}

		retVal += StrFormat("\n:language\t%d", language);

		if (formatInfo.mNoMembers)
		{
			//
		}
		else if (wantsCustomExpandedItems)
		{
			HandleCustomExpandedItems(retVal, dbgCompileUnit, debugVis, dwUseType, dwValueType, ptrUseDataStr, ptrDataStr, useTypedValue, dbgVisWildcardCaptures, formatInfo);
		}
		else if ((!isNull) && (!isBadSrc))
		{
			if (dataPtr == -1)
			{
				//String splatName = ((origTypedValue.mSrcAddress == -1) && (origTypedValue.mVariable != NULL)) ? origTypedValue.mVariable->mName : expr;
				String splatName = expr;
				retVal += "\n" + GetMemberList(memberListType, splatName, wasPtr, false, false, true, origTypedValue.mIsReadOnly);
			}
			else
			{
                retVal += "\n" + GetMemberList(memberListType, ptrDataStr, wasPtr, false, memberListForceCast, isCompositeWithoutAddress, origTypedValue.mIsReadOnly);
			}
		}

		if (formatInfo.mExpandItemDepth > 0)
			return retVal;
		if (isAppendBfObject)
			retVal += "\n:appendAlloc";
		if (isStackBfObject)
			retVal += "\n:stack";
		if (isDeletedBfObject)
			retVal += "\n:deleted";

		if (!formatInfo.mAction.IsEmpty())
		{
			retVal += "\n:action\t";
			retVal += formatInfo.mAction;
		}
		else if ((debugVis != NULL) && (!debugVis->mAction.empty()))
		{
			String rawActionStr = mDebugManager->mDebugVisualizers->DoStringReplace(debugVis->mAction, dbgVisWildcardCaptures);
			String actionStr;
			ProcessEvalString(dbgCompileUnit, useTypedValue, rawActionStr, actionStr, formatInfo, debugVis, true);
			retVal += "\n:action\t" + actionStr;
		}

		if ((!typedValue.mIsLiteral) && (dwValueType->IsPointer()))
		{
			retVal += "\n:editVal\t" + EncodeDataPtr(ptrVal, true);
		}

		if (((debugVis != NULL) && (!debugVis->mStringViews.IsEmpty())) || (wantStringView))
			retVal += "\n:stringView";

		if (wantStringView)
		{
			retVal += "\t";
			retVal += SlashString(stringViewData, false, false, true);
		}

		return retVal;
	}

	return "Unknown Type\n" + origValueType->ToString();
}

void WinDebugger::HandleCustomExpandedItems(String& retVal, DbgCompileUnit* dbgCompileUnit, DebugVisualizerEntry* debugVis, DbgType* dwUseType, DbgType* dwValueType, String& ptrUseDataStr, String& ptrDataStr, DbgTypedValue useTypedValue, Array<String>& dbgVisWildcardCaptures, DwFormatInfo& formatInfo)
{
	auto debugVisualizers = mDebugManager->mDebugVisualizers;
	auto dbgModule = dbgCompileUnit->mDbgModule;

	if (formatInfo.mExpandItemDepth > 10) // Avoid crashing on circular ExpandItems
		return;

	auto language = formatInfo.mLanguage;

	bool isReadOnly = false;
	if (useTypedValue.mIsReadOnly)
		isReadOnly = true;

	for (auto entry : debugVis->mExpandItems)
	{
		if (!entry->mCondition.empty())
		{
			String error;
			if (!EvalCondition(debugVis, dbgCompileUnit, useTypedValue, formatInfo, entry->mCondition, dbgVisWildcardCaptures, error))
			{
				if (!error.empty())
					retVal += "\n" + entry->mName + "\t@!<DbgVis Failed>@!";
				continue;
			}
		}
		String replacedStr = debugVisualizers->DoStringReplace(entry->mValue, dbgVisWildcardCaptures);
		retVal += "\n" + entry->mName + "\t" + replacedStr + ", this=(" + ptrUseDataStr + ")";
	}

	String referenceId = dwUseType->ToString();

	if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_ExpandedItem)
	{
		DbgTypedValue itemValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
		if (itemValue)
		{
			DwFormatInfo itemFormatInfo = formatInfo;
			itemFormatInfo.mExpandItemDepth++;
			String itemRetVal = DbgTypedValueToString(itemValue, "", itemFormatInfo, NULL);

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
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		Array<int> lowerDimSizes;
		for (auto lowerDim : debugVis->mLowerDimSizes)
		{
			DbgTypedValue lowerDimValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(lowerDim, dbgVisWildcardCaptures), &formatInfo);
			int dimSize = 0;
			if ((lowerDimValue) && (lowerDimValue.mType->IsInteger()))
				dimSize = (int)lowerDimValue.GetInt64();
			dimSize = BF_MAX(dimSize, 1);
			lowerDimSizes.push_back(dimSize);
		}

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			if (!debugVis->mCondition.IsEmpty())
			{
				int size = (int)sizeValue.GetInt64();
				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);

				DbgTypedValue curNode = headPointer;
				Array<addr_target> parentList;
				String continuationData;

				int totalSize = 2;
				auto valueType = headPointer.mType;
				String addrs = GetArrayItems(dbgCompileUnit, debugVis, valueType, headPointer, totalSize, &continuationData);
				String firstAddr;
				String secondAddr;
				bool hasSecondAddr = valueType == NULL;
				if (addrs.length() > 0)
				{
					const char* addrsPtr = addrs.c_str();
					firstAddr = addrs.Substring(0, sizeof(addr_target) * 2);
					if (hasSecondAddr)
						secondAddr = addrs.Substring(sizeof(addr_target) * 2, sizeof(addr_target) * 2);
				}

				String evalStr;
				if (valueType != NULL)
				{
					evalStr = "(" + valueType->ToStringRaw();
					if (!valueType->IsPointer())
						evalStr += "*";
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
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1, 50000) +
					"\t[{0}]\t" + evalStr;
			}
			else if (lowerDimSizes.size() == 2)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];

				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d]*)", headPointer.mType->mTypeParam->ToStringRaw(language).c_str(), dimSize2) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1 / dimSize2, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else if (lowerDimSizes.size() == 3)
			{
				int dimSize1 = lowerDimSizes[0];
				int dimSize2 = lowerDimSizes[1];
				int dimSize3 = lowerDimSizes[2];

				DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures), &formatInfo);
				if ((headPointer.mType != NULL) && (headPointer.mType->IsPointer()))
				{
					String evalStr = StrFormat("((%s[%d][%d]*)", headPointer.mType->mTypeParam->ToStringRaw(language).c_str(), dimSize2, dimSize3) + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) +
						StrFormat(" + {0} * %d), arraysize=%d, na, this=", dimSize1, dimSize1) + ptrUseDataStr;
					evalStr += ", refid=\"" + referenceId + ".[]\"";
					if (isReadOnly)
						evalStr += ", ne";
					retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64() / dimSize1 / dimSize2 / dimSize3, 50000) +
						"\t[{0}]\t" + evalStr;
				}
			}
			else
			{
				String evalStr = "*(" + debugVisualizers->DoStringReplace(debugVis->mValuePointer, dbgVisWildcardCaptures) + " + {0}), this=" + ptrUseDataStr;
				evalStr += ", refid=\"" + referenceId + ".[]${0}\"";
				if (isReadOnly)
					evalStr += ", ne";
				retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64(), 50000) +
					"\t[{0}]\t" + evalStr;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_IndexItems)
	{
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);

		if ((sizeValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			String evalStr = debugVis->mValuePointer + ", this=" + ptrUseDataStr;
			evalStr.Replace("$i", "{0}");

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%lld\t%d", 0, sizeValue.GetInt64(), 50000) +
				"\t[{0}]\t" + evalStr;
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_LinkedList)
	{
		DbgType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = dbgModule->FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures), dwValueType);
			if (valueType != NULL)
				valueType = valueType->ResolveTypeDef();
		}

		DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);
		if (headPointer)
		{
			DbgTypedValue endPointer;
			if (!debugVis->mEndPointer.empty())
				endPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mEndPointer, dbgVisWildcardCaptures), &formatInfo);
			DbgTypedValue nextPointer = EvaluateInContext(dbgCompileUnit, headPointer, debugVisualizers->DoStringReplace(debugVis->mNextPointer, dbgVisWildcardCaptures), &formatInfo);

			int size = -1;
			if (!debugVis->mSize.empty())
			{
				auto sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
				if (sizeValue)
					size = (int)sizeValue.GetInt64();
			}

			DbgTypedValue curNode = headPointer;
			Array<addr_target> parentList;
			String continuationData;

			int totalSize = 2;
			String addrs = GetLinkedListItems(dbgCompileUnit, debugVis, endPointer.mPtr, valueType, curNode, totalSize, &continuationData);
			String firstAddr;
			String secondAddr;
			bool hasSecondAddr = valueType == NULL;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = addrs.Substring(0, sizeof(addr_target)*2);
				if (hasSecondAddr)
					secondAddr = addrs.Substring(sizeof(addr_target)*2, sizeof(addr_target)*2);
			}

			String evalStr;
			if (valueType != NULL)
			{
				evalStr = "(" + valueType->ToStringRaw();
				if (!valueType->IsPointer())
					evalStr += "*";
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
		DbgType* valueType = NULL;
		if (!debugVis->mValueType.empty())
		{
			valueType = dbgModule->FindType(debugVisualizers->DoStringReplace(debugVis->mValueType, dbgVisWildcardCaptures), dwValueType);
			if (valueType != NULL)
				valueType = valueType->ResolveTypeDef();
		}

		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		DbgTypedValue headPointer = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mHeadPointer, dbgVisWildcardCaptures), &formatInfo);

		if (sizeValue)
			sizeValue.mType = sizeValue.mType->RemoveModifiers();
		if ((sizeValue) && (headPointer) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			DbgTypedValue curNode = headPointer;
			Array<addr_target> parentList;
			String continuationData;

			int getItemCount = (int)BF_MIN(sizeValue.GetInt64(), 32LL);

			String addrs = GetTreeItems(dbgCompileUnit, debugVis, parentList, valueType, curNode, getItemCount, &continuationData);
			addr_target firstAddr = 0;
			addr_target secondAddr = 0;
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
				evalStr = "*(" + valueType->ToStringRaw();
				if (!valueType->IsPointer())
					evalStr += "*";
				evalStr += ")0x{1}";
			}
			else
			{
				evalStr += "*(_T_{1}*)0x{2}";
			}

			int size = (int)sizeValue.GetInt64();
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
		DbgTypedValue sizeValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mSize, dbgVisWildcardCaptures), &formatInfo);
		DbgTypedValue entriesPtrValue = EvaluateInContext(dbgCompileUnit, useTypedValue, debugVisualizers->DoStringReplace(debugVis->mEntries, dbgVisWildcardCaptures), &formatInfo);

		if (sizeValue)
			sizeValue.mType = sizeValue.mType->RemoveModifiers();
		if ((sizeValue) && (entriesPtrValue) && (sizeValue.mType->IsInteger()) && (sizeValue.GetInt64() > 0))
		{
			String continuationData;

			DbgType* valueType = entriesPtrValue.mType;

			int getItemCount = (int)std::min(sizeValue.GetInt64(), 2LL);

			DbgType* useTypedValType = useTypedValue.mType;
			addr_target useTypedValPtr = useTypedValue.mPtr;
			addr_target useTypedValAddr = useTypedValue.mSrcAddress;

			String addrs = GetDictionaryItems(dbgCompileUnit, debugVis, useTypedValue, 0, -1, getItemCount, &continuationData);
			addr_target firstAddr = 0;
			if (addrs.length() > 0)
			{
				const char* addrsPtr = addrs.c_str();
				firstAddr = DecodeTargetDataPtr(addrsPtr);
			}

			String evalStr = "((" + valueType->ToStringRaw() + ")0x{1}), na";

			evalStr += ", refid=\"" + referenceId + ".[]\"";
			if (isReadOnly)
				evalStr += ", ne";
			retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, (int)sizeValue.GetInt64(), 10000) +
				"\t[{0}]\t" + evalStr + "\t" + EncodeDataPtr(firstAddr, false);

			if (addrs.length() > 0)
			{
				retVal += "\n:addrs\t" + addrs;
				if (continuationData.length() > 0)
					retVal += "\n:continuation\t" + continuationData;
			}
		}
	}
	else if (debugVis->mCollectionType == DebugVisualizerEntry::CollectionType_CallStackList)
	{
		int size = 0;

		String addrs;
		String firstVal;
		auto ptr = useTypedValue.mPtr;

		for (int i = 0; i < formatInfo.mOverrideCount; i++)
		{
			auto funcAddr = ReadMemory<addr_target>(ptr + i * sizeof(addr_target));
			auto srcFuncAddr = funcAddr;

			addrs += EncodeDataPtr(funcAddr - 1, false);
			if (i == 0)
				firstVal = addrs;
			addrs += EncodeDataPtr((addr_target)0, false);
			size++;

			int inlineIdx = 0;

			auto subProgram = mDebugTarget->FindSubProgram(funcAddr - 1, DbgOnDemandKind_LocalOnly);
			while (subProgram != NULL)
			{
				if (subProgram->mInlineeInfo == NULL)
					break;

				auto prevFuncAddr = subProgram->mBlock.mLowPC;

				subProgram = subProgram->mInlineeInfo->mInlineParent;
				addrs += EncodeDataPtr(subProgram->mBlock.mLowPC + 1, false);
				addrs += EncodeDataPtr(prevFuncAddr, false);
				size++;

				inlineIdx++;
			}
		}

		String evalStr = "(System.CallStackAddr)0x{1}";
		evalStr += ", refid=\"" + referenceId + ".[]\"";
		evalStr += ", ne";

		retVal += "\n:repeat" + StrFormat("\t%d\t%d\t%d", 0, size, 10000) +
			"\t[{0}]\t" + evalStr + ", action=ShowCodeAddr {1} {2}\t" + firstVal + "\t" + EncodeDataPtr((addr_target)0, false);

		retVal += "\n:addrs\t" + addrs;
		retVal += "\n:addrsEntrySize\t2";
		return;
	}

	if (formatInfo.mExpandItemDepth == 0)
	{
		//retVal += "\n[Raw View]\tthis, this=" + ptrDataStr + ", nv";
		retVal += "\n[Raw View]\t" + ptrDataStr + ", nv";
	}
}

bool WinDebugger::IsPaused()
{
	return (mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint) || (mRunState == RunState_Exception) || (mRunState == RunState_DebugEval_Done);
}

DbgTypedValue WinDebugger::GetRegister(const StringImpl& regName, DbgLanguage language, CPURegisters* registers, Array<RegForm>* regForms)
{
	int regNum = -1;

	String lwrRegName(regName);
	_strlwr((char*)lwrRegName.c_str());

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

DbgModule* WinDebugger::GetCallStackDbgModule(int callStackIdx)
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

DbgSubprogram* WinDebugger::GetCallStackSubprogram(int callStackIdx)
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

DbgCompileUnit* WinDebugger::GetCallStackCompileUnit(int callStackIdx)
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

String WinDebugger::EvaluateContinue(DbgPendingExpr* pendingExpr, BfPassInstance& bfPassInstance)
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

String WinDebugger::EvaluateContinue()
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
		BfLogDbg("EvaluateContinue finishing pending expr in thread %d\n", mDebugEvalThreadInfo.mThreadId);
		CleanupDebugEval();
	}

	return result;
}

void WinDebugger::EvaluateContinueKeep()
{
	if (mDebugPendingExpr != NULL)
		mDebugPendingExpr->mIdleTicks = 0;
}

static void PdbTestFile(WinDebugger* debugger, const StringImpl& path)
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

static void PdbTest(WinDebugger* debugger, const StringImpl& path)
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

String WinDebugger::Evaluate(const StringImpl& expr, DwFormatInfo formatInfo, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
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

	BfLogDbgExpr("Evaluate %s in thread %d\n", expr.c_str(), (mActiveThread != NULL) ? mActiveThread->mThreadId : 0);

	if (language != -1)
		formatInfo.mLanguage = (DbgLanguage)language;

	auto activeThread = mActiveThread;
	if ((!IsPaused()) && (mRunState != RunState_NotStarted) && (mRunState != RunState_DebugEval))
	{
		activeThread = NULL;
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

	auto prevActiveThread = mActiveThread;
	bool restoreActiveThread = false;
	defer(
		{
			if (restoreActiveThread)
				SetActiveThread(prevActiveThread->mThreadId);
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

								if ((mActiveThread != NULL) && (mActiveThread->mThreadId != threadId))
									restoreActiveThread = true;
								if ((mActiveThread == NULL) || (mActiveThread->mThreadId != threadId))
									SetActiveThread(threadId);
								if ((mActiveThread != NULL) && (mActiveThread->mThreadId == threadId))
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
											if ((callStackIdx != foundStackIdx) || (mActiveThread != prevActiveThread))
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
			else if (cmd == "!pdbtest")
			{
				PdbTest(this, "c:\\");
			}
			else if (cmd.StartsWith("!pdbtest "))
				PdbTestFile(this, cmd.Substring(9));
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
	if (activeThread != NULL)
		pendingExpr->mThreadId = activeThread->mThreadId;
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
		mDebugEvalThreadInfo = *mActiveThread;
		mActiveThread->mIsAtBreakpointAddress = 0;
		mActiveThread->mStoppedAtAddress = 0;
		mActiveThread->mBreakpointAddressContinuing = 0;
	}
	else
		delete pendingExpr;

	return result;
}

String WinDebugger::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	DwFormatInfo formatInfo;
	return Evaluate(expr, formatInfo, callStackIdx, cursorPos, language, expressionFlags);
}

static void ConvertDoubleToFloat80(double d, byte fp80[10])
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

bool WinDebugger::AssignToReg(int callStackIdx, DbgTypedValue regVal, DbgTypedValue value, String& outError)
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

String WinDebugger::GetAutocompleteOutput(DwAutoComplete& autoComplete)
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

String WinDebugger::EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos)
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
String WinDebugger::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
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

String WinDebugger::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
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

String WinDebugger::GetAutoLocals(int stackFrameIdx, bool showRegs)
{
	BP_ZONE("WinDebugger::GetAutoExpressions");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (IsInRunState())
		return "";

	if (!IsPaused())
		return "";

	if (mCallStack.size() == 0)
		UpdateCallStack();
	String result;

	Array<String> localList;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);

	if (actualStackFrameIdx >= mCallStack.size())
		return "";
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram == NULL)
		return "";

	auto langage = dwSubprogram->GetLanguage();

	DbgLineData* dwLineData = FindLineDataInSubprogram(wdStackFrame->GetSourcePC(), dwSubprogram);
	if (dwLineData == NULL)
		return "";

	dwSubprogram->PopulateSubprogram();
	mDebugTarget->GetAutoLocalsInBlock(localList, dwSubprogram, &dwSubprogram->mBlock, wdStackFrame, dwLineData);

	String lastLocal;
	for (auto local : localList)
	{
		if (langage == DbgLanguage_C)
		{
			if ((local == "this") && (strncmp(dwSubprogram->mName, "<lambda_", 8) == 0))
			{
				// Use explicit "$this" so we can see the actual capture
				result += "$this\n";
				continue;
			}
		}

		bool wasAlias = false;
		for (int i = 0; i < (int)local.length() - 1; i++)
		{
			if ((local[i] == '$') && (local[i + 1] == 'a'))
			{
				// Alias
				wasAlias = true;
				String localName = local.Substring(0, i) + "\n";
				if (localName != lastLocal)
				{
					result += localName;
					lastLocal = localName;
				}
				break;
			}
		}
		if (!wasAlias)
			result += local + "\n";
	}

	if (showRegs)
	{
		result += "$FLAGS\n";

		UpdateRegisterUsage(stackFrameIdx);
		for (int regIdx = 0; regIdx < (int)wdStackFrame->mRegForms.size(); regIdx++)
		{
			if (wdStackFrame->mRegForms[regIdx] != RegForm_Invalid)
				result += "$" + String(CPURegisters::GetRegisterName(regIdx)) + "\n";
		}
	}

	return result;
}

String WinDebugger::CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx)
{
	DbgCompileUnit* compileUnit = GetCallStackCompileUnit(callStackIdx);
	DbgModule* dbgModule = GetCallStackDbgModule(callStackIdx);
	if (dbgModule == NULL)
		return "!failed";

	DbgLanguage language = DbgLanguage_Unknown;
	if (compileUnit != NULL)
		language = compileUnit->mLanguage;

	BfPassInstance bfPassInstance(mBfSystem);

	BfParser parser(mBfSystem);
	parser.mCompatMode = language != DbgLanguage_Beef;
	auto terminatedExpr = expr + ";";
	parser.SetSource(terminatedExpr.c_str(), terminatedExpr.length());
	parser.Parse(&bfPassInstance);

	auto terminatedParentExpr = parentExpr + ";";

	String parentPrefix;
	if (terminatedParentExpr.StartsWith('{'))
	{
		int prefixEnd = terminatedParentExpr.IndexOf('}');
		parentPrefix = terminatedParentExpr.Substring(0, prefixEnd + 1);
		terminatedParentExpr.Remove(0, prefixEnd + 1);
	}

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

String WinDebugger::GetProcessInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread == NULL) && (!mIsRunning))
		return "";

	SYSTEM_INFO sysinfo = { 0 };
	GetSystemInfo(&sysinfo);

	FILETIME creationTime = { 0 };
	FILETIME exitTime = { 0 };
	FILETIME kernelTime = { 0 };
	FILETIME userTime = { 0 };
	::GetProcessTimes(mProcessInfo.hProcess, &creationTime, &exitTime, &kernelTime, &userTime);

	String retStr;
	PROCESS_MEMORY_COUNTERS memInfo = { 0 };
	::GetProcessMemoryInfo(mProcessInfo.hProcess, &memInfo, sizeof(PROCESS_MEMORY_COUNTERS));

	FILETIME currentTime = { 0 };
	::GetSystemTimeAsFileTime(&currentTime);

	retStr += StrFormat("VirtualMemory\t%lld\n", memInfo.PagefileUsage);
	retStr += StrFormat("WorkingMemory\t%lld\n", memInfo.WorkingSetSize);
	retStr += StrFormat("RunningTime\t%lld\n", *(int64*)&currentTime - *(int64*)&creationTime);
	retStr += StrFormat("KernelTime\t%lld\n", *(int64*)&kernelTime / sysinfo.dwNumberOfProcessors);
	retStr += StrFormat("UserTime\t%lld\n", *(int64*)&userTime / sysinfo.dwNumberOfProcessors);

	return retStr;
}

String WinDebugger::GetThreadInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String retStr;
	if ((mActiveThread == NULL) && (!mIsRunning))
	{
		retStr = "";
	}
	else
	{
		if (mActiveThread != NULL)
			retStr = StrFormat("%d", mActiveThread->mThreadId);

		for (auto threadInfo : mThreadList)
		{
			SetAndRestoreValue<WdThreadInfo*> prevThread(mActiveThread, threadInfo);

			retStr += "\n";

			for (int pass = 0; pass < 2; pass++)
			{
				CPURegisters registers;
				PopulateRegisters(&registers);

				String locString = EncodeDataPtr((addr_target)registers.GetPC(), true);

				TryGetThreadName(threadInfo);

				bool hadThreadName = true;
				String threadName = threadInfo->mName;
				if (threadName.IsEmpty())
				{
					hadThreadName = false;
					if (threadInfo->mThreadId == mProcessInfo.dwThreadId)
						threadName = "Main Thread";
					else
						threadName = "Worker Thread";
				}

				bool isInvalid = false;
				addr_target appendAddr = 0;

				for (int stackIdx = 0; true; stackIdx++)
				{
					auto subProgram = mDebugTarget->FindSubProgram(registers.GetPC(), DbgOnDemandKind_LocalOnly);
					if (subProgram != NULL)
					{
						if (subProgram->mLineInfo != NULL)
						{
							DbgModule* module = subProgram->mCompileUnit->mDbgModule;
							DbgModule* linkedModule = module->GetLinkedModule();
							if (linkedModule->mDisplayName.length() > 0)
							{
								locString = linkedModule->mDisplayName + "!" + subProgram->ToString();
								if (!hadThreadName)
									threadName = module->mDisplayName + " thread";
							}
							else
							{
								locString = subProgram->ToString();
							}
							appendAddr = 0;
							break;
						}
					}

					DbgModule* module = mDebugTarget->FindDbgModuleForAddress(registers.GetPC());
					if (module == NULL)
					{
						isInvalid = true;
						break;
					}

					DbgModule* linkedModule = module->GetLinkedModule();
					appendAddr = (addr_target)registers.GetPC();
					locString = linkedModule->mDisplayName + "!" + EncodeDataPtr((addr_target)registers.GetPC(), true);
					if (!hadThreadName)
						threadName = linkedModule->mDisplayName + " thread";

					if ((mActiveThread == mExplicitStopThread) && (mActiveBreakpoint != NULL))
					{
						if ((subProgram == NULL) ||
							(mActiveBreakpoint->mAddr < subProgram->mBlock.mLowPC) ||
							(mActiveBreakpoint->mAddr >= subProgram->mBlock.mHighPC))
							break;
					}

					if (pass == 1) // Just take the first item
						break;

					if (stackIdx == 128)
						break; // Too many!

					addr_target returnAddr;
					if (!mDebugTarget->RollBackStackFrame(&registers, &returnAddr, true))
					{
						isInvalid = true;
						break;
					}
				}

				if ((isInvalid) && (pass == 0))
					continue;

				if (appendAddr != 0)
				{
					String symbolName;
					addr_target offset;
					DbgModule* dwarf;
					if (mDebugTarget->FindSymbolAt(appendAddr, &symbolName, &offset, &dwarf))
					{
						DbgModule* linkedModule = dwarf->GetLinkedModule();
						String demangledName = BfDemangler::Demangle(symbolName, DbgLanguage_Unknown);
						if (!linkedModule->mDisplayName.empty())
						{
							demangledName = linkedModule->mDisplayName + "!" + demangledName;
						}
						locString = demangledName + StrFormat("+0x%X", offset);
					}
				}

				retStr += StrFormat("%d\t%s\t%s", threadInfo->mThreadId, threadName.c_str(), locString.c_str());

				String attrs;
				if (threadInfo->mFrozen)
				{
					attrs += "Fr";
				}
				if (!attrs.IsEmpty())
				{
					retStr += "\t";
					retStr += attrs;
				}

				break;
			}
		}
	}
	return retStr;
}

void WinDebugger::SetActiveThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread != NULL) && (mActiveThread->mThreadId == threadId))
		return;

	auto prevThread = mActiveThread;

	if (mThreadMap.TryGetValue(threadId, &mActiveThread))
	{
		BfLogDbg("SetActiveThread %d\n", threadId);

		if (prevThread != NULL)
		{
			Array<WdStackFrame*>* prevFrameArray = NULL;
			mSavedCallStacks.TryAdd(prevThread, NULL, &prevFrameArray);
			for (auto frameInfo : *prevFrameArray)
				delete frameInfo;
			*prevFrameArray = mCallStack;
			mCallStack.Clear();
		}

		DoClearCallStack(false);

		Array<WdStackFrame*>* newFrameArray = NULL;
		if (mSavedCallStacks.TryGetValue(mActiveThread, &newFrameArray))
		{
			mCallStack = *newFrameArray;
			newFrameArray->Clear();
		}
	}
	else
	{
		BfLogDbg("SetActiveThread %d FAILED\n", threadId);
	}
}

int WinDebugger::GetActiveThread()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mActiveThread == NULL)
		return -1;
	return mActiveThread->mThreadId;
}

void WinDebugger::FreezeThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(!IsInRunState());
	auto thread = mThreadMap[threadId];
	if (!thread->mFrozen)
	{
		thread->mFrozen = true;
		::SuspendThread(thread->mHThread);
		BfLogDbg("SuspendThread %d from FreezeThread\n", thread->mThreadId);
	}
}

void WinDebugger::ThawThread(int threadId)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BF_ASSERT(!IsInRunState());
	auto thread = mThreadMap[threadId];
	if (thread->mFrozen)
	{
		thread->mFrozen = false;
		::ResumeThread(thread->mHThread);
		BfLogDbg("ResumeThread %d from ThawThread\n", thread->mThreadId);
	}
}

bool WinDebugger::IsActiveThreadWaiting()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	return mActiveThread == mDebuggerWaitingThread;
}

void WinDebugger::DoClearCallStack(bool clearSavedStacks)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	BfLogDbg("ClearCallstack\n");
	BF_ASSERT(mRunState != RunState_DebugEval);

	for (auto wdStackFrame : mCallStack)
		delete wdStackFrame;

	if (clearSavedStacks)
	{
		for (auto& kv : mSavedCallStacks)
		{
			for (auto wdStackFrame : kv.mValue)
				delete wdStackFrame;
		}
		mSavedCallStacks.Clear();
	}

	mCallStack.Clear();
	mIsPartialCallStack = true;
}

void WinDebugger::ClearCallStack()
{
	DoClearCallStack(true);
}

void WinDebugger::UpdateCallStack(bool slowEarlyOut)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!mIsPartialCallStack)
		return;

	if (mActiveThread == NULL)
		return;

	if (IsInRunState())
		return;

	uint32 tickStart = BFTickCount();

	CPURegisters registers;

	if (mCallStack.size() > 0)
	{
		WdStackFrame* wdStackFrame = mCallStack.back();
		if (wdStackFrame->mIsEnd)
		{
			return;
		}
		memcpy(&registers, &wdStackFrame->mRegisters, sizeof(registers));
		bool regsRolledBack = RollBackStackFrame(&registers, mCallStack.size() == 1);
		// If we can't roll them back then mIsEnd should have been set for the previous frame
		BF_ASSERT(regsRolledBack);
	}
	else
	{
		BF_ASSERT(mIsPartialCallStack);
		mCallStack.Reserve(1024);
		PopulateRegisters(&registers);
		BfLogDbg("UpdateCallStack starting. Thread=%d PC=0x%p\n", mActiveThread->mThreadId, registers.GetPC());
	}

	bool isPartial = false;

	// Incrementally fill callstack structure to avoid stepping slowdown during deep nesting
	for (int fillIdx = 0; fillIdx < (slowEarlyOut ? 10000 : 100000); fillIdx++)
	{
		WdStackFrame* wdStackFrame = new WdStackFrame();
		memcpy(&wdStackFrame->mRegisters, &registers, sizeof(registers));
		wdStackFrame->mIsStart = mCallStack.size() == 0;
		wdStackFrame->mIsEnd = false;

		bool rollbackSuccess = false;
		for (int tryCount = 0; tryCount < 16; tryCount++)
		{
			if (!RollBackStackFrame(&registers, wdStackFrame->mIsStart))
			{
				break;
			}
			if (registers.GetPC() > 0xFFFF)
			{
				rollbackSuccess = true;
				break;
			}

			if (mCallStack.size() > 0)
				break; // Only retry for the first frame
		}

		if (!rollbackSuccess)
			wdStackFrame->mIsEnd = true;

		if (registers.GetSP() <= wdStackFrame->mRegisters.GetSP())
		{
			// SP went the wrong direction, stop rolling back
			wdStackFrame->mIsEnd = true;
		}

		mCallStack.push_back(wdStackFrame);

		if (IsMiniDumpDebugger())
		{
			// Make sure to queue up any debug stuff we need
			UpdateCallStackMethod((int)mCallStack.size() - 1);
		}

		if (wdStackFrame->mIsEnd)
			break;

		// Time-limit callstack generation.  Most useful for debug mode.
		if ((slowEarlyOut) && ((fillIdx % 100) == 0))
		{
			uint32 tickEnd = BFTickCount();
			if (tickEnd - tickStart >= 10)
			{
				isPartial = true;
				break;
			}
		}
	}

	if (!isPartial)
		mIsPartialCallStack = false;
}

int WinDebugger::GetCallStackCount()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	return (int)mCallStack.size();
}

int WinDebugger::GetRequestedStackFrameIdx()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveThread == mExplicitStopThread) && (mRequestedStackFrameIdx >= -1))
	{
		if (mActiveBreakpoint != NULL)
			mRequestedStackFrameIdx = GetBreakStackFrameIdx();
		if (mRequestedStackFrameIdx == -1)
			mRequestedStackFrameIdx = 0;
		return mRequestedStackFrameIdx;
	}

	int newCallStackIdx = 0;
	while (true)
	{
		if (newCallStackIdx >= (int)mCallStack.size() - 1)
			UpdateCallStack();
		if (newCallStackIdx >= (int)mCallStack.size() - 1)
			break;

		intptr addr;
		String file;
		int hotIdx;
		int defLineStart;
		int defLineEnd;
		int line;
		int column;
		int language;
		int stackSize;
		int8 flags;
		GetStackFrameInfo(newCallStackIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
		if (!file.empty())
			return newCallStackIdx;
		newCallStackIdx++;
	}

	return 0;
}

int WinDebugger::GetBreakStackFrameIdx()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if ((mActiveBreakpoint == NULL) || (mRunState != RunState_Breakpoint))
		return -1;
	if ((mBreakStackFrameIdx != -1) || (mActiveThread != mExplicitStopThread))
		return mBreakStackFrameIdx;

	mBreakStackFrameIdx = 0;
	BF_ASSERT(mActiveBreakpoint != NULL);
	if (mCallStack.IsEmpty())
		UpdateCallStack();
	if (!mCallStack.IsEmpty())
	{
		UpdateCallStackMethod(0);

		for (int stackIdx = 0; stackIdx < (int)mCallStack.size(); stackIdx++)
		{
			auto callStackEntry = mCallStack[stackIdx];
			if (callStackEntry->mSubProgram == NULL)
				break;
			if ((mActiveBreakpoint->mAddr < callStackEntry->mSubProgram->mBlock.mLowPC) ||
				(mActiveBreakpoint->mAddr >= callStackEntry->mSubProgram->mBlock.mHighPC))
				break;

			DbgSubprogram* specificSubprogram = callStackEntry->mSubProgram;
			auto dwLineData = callStackEntry->mSubProgram->FindClosestLine(mActiveBreakpoint->mAddr, &specificSubprogram);
			if (dwLineData == NULL)
				break;

			if (mActiveBreakpoint->mLineData == dwLineData)
			{
				mBreakStackFrameIdx = stackIdx;
				break;
			}
		}
	}
	return mBreakStackFrameIdx;
}

static const char* SafeString(const char* str)
{
	if (str == NULL)
		return "???";
	return str;
}

void WinDebugger::UpdateRegisterUsage(int stackFrameIdx)
{
	WdStackFrame* wdStackFrame = mCallStack[stackFrameIdx];
	if (wdStackFrame->mRegForms.size() != 0)
		return;

	auto dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram == NULL)
		return;

	addr_target addr = dwSubprogram->mBlock.mLowPC;

	const uint8* baseOp = nullptr;
	while (addr < dwSubprogram->mBlock.mHighPC)
	{
		CPUInst inst;
		if (!mDebugTarget->DecodeInstruction(addr, &inst))
			break;

		bool overrideForm = inst.mAddress <= (addr_target)wdStackFrame->mRegisters.GetPC();
		inst.MarkRegsUsed(wdStackFrame->mRegForms, overrideForm);

		addr += inst.GetLength();
	}
}

// It's safe to pass an invalid idx in here
void WinDebugger::UpdateCallStackMethod(int stackFrameIdx)
{
	if (mCallStack.empty())
		return;

	int startIdx = std::min(stackFrameIdx, (int)mCallStack.size() - 1);
	while (startIdx >= 0)
	{
		WdStackFrame* wdStackFrame = mCallStack[startIdx];
		if (wdStackFrame->mHasGottenSubProgram)
			break;
		startIdx--;
	}
	startIdx++;

	for (int checkFrameIdx = startIdx; checkFrameIdx <= stackFrameIdx; checkFrameIdx++)
	{
		//BF_ASSERT(checkFrameIdx < mCallStack.size());
		if (checkFrameIdx >= mCallStack.size())
			break;

		WdStackFrame* wdStackFrame = mCallStack[checkFrameIdx];

		wdStackFrame->mHasGottenSubProgram = true;
		addr_target pcAddress = (addr_target)wdStackFrame->GetSourcePC();
		DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress, DbgOnDemandKind_LocalOnly);
		wdStackFrame->mHasGottenSubProgram = true;
		wdStackFrame->mSubProgram = dwSubprogram;

		if ((dwSubprogram == NULL) && (IsMiniDumpDebugger()))
		{
			// FindSymbolAt will queue up debug info if necessary...
			String symbolName;
			addr_target offset;
			DbgModule* dbgModule;
			mDebugTarget->FindSymbolAt(pcAddress, &symbolName, &offset, &dbgModule);
		}

		auto prevStackFrame = wdStackFrame;

		// Insert inlines
		int insertIdx = checkFrameIdx + 1;
		while ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL))
		{
			WdStackFrame* inlineStackFrame = new WdStackFrame();
			*inlineStackFrame = *wdStackFrame;
			inlineStackFrame->mInInlineMethod = true;
			wdStackFrame->mInInlineCall = true;
			inlineStackFrame->mSubProgram = dwSubprogram->mInlineeInfo->mInlineParent;
			mCallStack.Insert(insertIdx, inlineStackFrame);
			dwSubprogram = dwSubprogram->mInlineeInfo->mInlineParent;
			insertIdx++;
			checkFrameIdx++;

			prevStackFrame = inlineStackFrame;
		}
	}
}

void WinDebugger::GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	DbgSubprogram* subProgram = NULL;
	DbgLineData* callingLineData = FindLineDataAtAddress((addr_target)addr, &subProgram);

	if (inlineCallAddr != 0)
	{
		auto inlinedSubProgram = mDebugTarget->FindSubProgram(inlineCallAddr);
		if (inlinedSubProgram != 0)
		{
			FixupLineDataForSubprogram(inlinedSubProgram->mInlineeInfo->mRootInliner);
			DbgSubprogram* parentSubprogram = inlinedSubProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
			auto foundLine = parentSubprogram->FindClosestLine(inlinedSubProgram->mBlock.mLowPC, &parentSubprogram);
			if (foundLine != NULL)
			{
				auto srcFile = parentSubprogram->GetLineSrcFile(*foundLine);
				*outFile = srcFile->GetLocalPath();
				*outLine = foundLine->mLine;
			}

			*outHotIdx = inlinedSubProgram->mCompileUnit->mDbgModule->mHotIdx;
			*outColumn = -1;

			DbgSubprogram* callingSubProgram = NULL;
			DbgLineData* callingLineData = FindLineDataAtAddress(inlinedSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
			if ((callingLineData != NULL) && (callingSubProgram == subProgram))
			{
				auto callingSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
				auto srcFile = callingSrcFile;
				*outFile = srcFile->GetLocalPath();

				if (*outLine == callingLineData->mLine)
					*outColumn = callingLineData->mColumn;
			}
			return;
		}
	}

	if (subProgram != NULL)
	{
		if ((subProgram->mInlineeInfo != NULL) && ((addr_target)addr >= subProgram->mBlock.mHighPC))
			callingLineData = &subProgram->mInlineeInfo->mLastLineData;

		*outHotIdx = subProgram->mCompileUnit->mDbgModule->mHotIdx;
		*outFile = subProgram->GetLineSrcFile(*callingLineData)->GetLocalPath();
		*outLine = callingLineData->mLine;
		*outColumn = callingLineData->mColumn;

		FixupLineDataForSubprogram(subProgram);
		DbgLineData* dwStartLineData = NULL;
		DbgLineData* dwEndLineData = NULL;
		if (subProgram->mLineInfo != NULL)
		{
			if (subProgram->mLineInfo->mLines.size() > 0)
			{
				dwStartLineData = &subProgram->mLineInfo->mLines[0];
				dwEndLineData = &subProgram->mLineInfo->mLines.back();
			}
		}
		else
		{
			if (subProgram->mInlineeInfo != NULL)
			{
				dwStartLineData = &subProgram->mInlineeInfo->mFirstLineData;
				dwEndLineData = &subProgram->mInlineeInfo->mLastLineData;
			}
		}

		if (dwEndLineData != NULL)
		{
			*outDefLineStart = dwStartLineData->mLine;
			*outDefLineEnd = dwEndLineData->mLine;
		}
	}
}

void WinDebugger::GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	*outThreadId = 0;
	if (outStackIdx != NULL)
		*outStackIdx = -1;

	if (!IsPaused())
		return;

	for (auto thread : mThreadList)
	{
		NT_TIB64 tib = { 0 };
		if (!ReadMemory((intptr)thread->mThreadLocalBase, sizeof(tib), &tib))
			continue;

		MEMORY_BASIC_INFORMATION stackInfo = { 0 };
		if (VirtualQueryEx(mProcessInfo.hProcess, (void*)(tib.StackBase - 1), &stackInfo, sizeof(MEMORY_BASIC_INFORMATION)) == 0)
			continue;

		if ((addr >= (intptr)stackInfo.AllocationBase) && (addr < (intptr)tib.StackBase))
		{
			*outThreadId = thread->mThreadId;

			if (outStackIdx == NULL)
				return;

			if (mActiveThread == thread)
			{
				UpdateCallStack(false);
				for (int callStackIdx = 0; callStackIdx < (int)mCallStack.size(); callStackIdx++)
				{
					UpdateCallStackMethod(callStackIdx);

					auto stackFrame = mCallStack[callStackIdx];
					if (addr >= (intptr)stackFrame->mRegisters.GetSP())
					{
						*outStackIdx = callStackIdx;
					}
				}
			}
			return;
		}
	}
}

String WinDebugger::GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd,
	int* outLine, int* outColumn, int* outLanguage, int* outStackSize, int8* outFlags)
{
	enum FrameFlags
	{
		FrameFlags_Optimized = 1,
		FrameFlags_HasPendingDebugInfo = 2,
		FrameFlags_CanGetOldSource = 4,
		FrameFlags_WasHotReplaced = 8,
		FrameFlags_HadError = 0x10
	};

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mCallStack.size() == 0)
		UpdateCallStack();

	*addr = 0;
	*outFile = "";
	*outHotIdx = 0;
	*outDefLineStart = -1;
	*outDefLineEnd = -1;
	*outLine = -1;
	*outColumn = 0;
	*outLanguage = 0;
	*outStackSize = 0;
	*outFlags = 0;

	UpdateCallStackMethod(stackFrameIdx);

	if (stackFrameIdx >= mCallStack.size())
	{
		return "";
	}

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);

	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();
	if (stackFrameIdx == -1)
		pcAddress = mShowPCOverride;

	*addr = pcAddress;

	if (actualStackFrameIdx < (int)mCallStack.size() - 2)
	{
		WdStackFrame* prevStackFrame = mCallStack[actualStackFrameIdx + 1];
		// Inlined methods have no stack frame
		*outStackSize = prevStackFrame->mRegisters.GetSP() - wdStackFrame->mRegisters.GetSP();
	}

	const auto& _CheckHashSrcFile = [&](String& outStr, DbgModule* dbgModule, DbgSrcFile* srcFile)
	{
		if (srcFile->mHashKind != DbgHashKind_None)
		{
			outStr += "#";
			srcFile->GetHash(outStr);
		}
	};

	auto _SetFlags = [&](DbgSubprogram* dwSubprogram)
	{
		DbgModule* dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		if (dwSubprogram->mIsOptimized)
			*outFlags |= FrameFlags_Optimized;
		if (dbgModule->HasPendingDebugInfo())
			*outFlags |= FrameFlags_HasPendingDebugInfo;
		if (dbgModule->CanGetOldSource())
			*outFlags |= FrameFlags_CanGetOldSource;
		if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) || (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid))
			*outFlags |= FrameFlags_WasHotReplaced;
	};

	auto _FixFilePath = [&](DbgModule* dbgModule)
	{
		if (outFile == NULL)
			return;

		if (outFile->StartsWith("$Emit"))
		{
			int dollarPos = outFile->IndexOf('$', 1);
			if (dollarPos == -1)
				return;

			outFile->Insert(dollarPos, StrFormat("%d", dbgModule->mId));
		}
	};

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* nextStackFrame = mCallStack[actualStackFrameIdx - 1];
		auto subProgram = nextStackFrame->mSubProgram;

		_SetFlags(subProgram);

		FixupLineDataForSubprogram(subProgram->mInlineeInfo->mRootInliner);
		DbgSubprogram* parentSubprogram = subProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
		auto foundLine = parentSubprogram->FindClosestLine(subProgram->mBlock.mLowPC, &parentSubprogram);
		if (foundLine != NULL)
		{
			auto srcFile = parentSubprogram->GetLineSrcFile(*foundLine);
			*outFile = srcFile->GetLocalPath();
			_CheckHashSrcFile(*outFile, subProgram->mCompileUnit->mDbgModule, srcFile);
			*outLine = foundLine->mLine;
		}

		*outLanguage = subProgram->GetLanguage();
		*outHotIdx = subProgram->mCompileUnit->mDbgModule->mHotIdx;
		*outColumn = -1;

		DbgSubprogram* callingSubProgram = NULL;
		DbgLineData* callingLineData = FindLineDataAtAddress(nextStackFrame->mSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
		if ((callingLineData != NULL) && (callingSubProgram == wdStackFrame->mSubProgram))
		{
			auto callingSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
			*outLanguage = callingSubProgram->mCompileUnit->mLanguage;
			auto srcFile = callingSrcFile;
			*outFile = srcFile->GetLocalPath();

			_CheckHashSrcFile(*outFile, subProgram->mCompileUnit->mDbgModule, srcFile);
			if (*outLine == callingLineData->mLine)
				*outColumn = callingLineData->mColumn;
		}

		String name = wdStackFrame->mSubProgram->ToString();
		DbgModule* dbgModule = wdStackFrame->mSubProgram->mCompileUnit->mDbgModule;
		DbgModule* linkedModule = dbgModule->GetLinkedModule();
		if (!linkedModule->mDisplayName.empty())
			name = linkedModule->mDisplayName + "!" + name;
		_FixFilePath(dbgModule);
		return name;
	}

	DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram != NULL)
	{
		String demangledName;
		if ((dwSubprogram->mName != NULL) && (strncmp(dwSubprogram->mName, ":Sep@", 5) == 0))
		{
			char* p;
			auto addr = strtoll(dwSubprogram->mName + 5, &p, 16);
			if (addr != 0)
			{
				auto parentSubprogram = mDebugTarget->FindSubProgram(addr);
				if (parentSubprogram != NULL)
					demangledName = parentSubprogram->ToString();
			}
		}
		if (demangledName.IsEmpty())
		{
			dwSubprogram->ToString(demangledName, true);
		}

		DbgSrcFile* dwSrcFile = NULL;
		DbgLineData* dwLineData = NULL;

		FixupLineDataForSubprogram(dwSubprogram);
		addr_target findAddress = wdStackFrame->GetSourcePC();
		DbgSubprogram* specificSubprogram = dwSubprogram;
		dwLineData = dwSubprogram->FindClosestLine(findAddress, &specificSubprogram);

		if ((dwLineData == NULL) && (dwSubprogram->mInlineeInfo != NULL) && (findAddress >= dwSubprogram->mBlock.mHighPC))
			dwLineData = &dwSubprogram->mInlineeInfo->mLastLineData;

		if (dwLineData != NULL)
			dwSrcFile = dwSubprogram->GetLineSrcFile(*dwLineData);

		DbgLineData* dwStartLineData = NULL;
		DbgLineData* dwEndLineData = NULL;
 		if (dwSubprogram->mLineInfo != NULL)
 		{
			if (dwSubprogram->mLineInfo->mLines.size() > 0)
			{
				dwStartLineData = &dwSubprogram->mLineInfo->mLines[0];
				dwEndLineData = &dwSubprogram->mLineInfo->mLines.back();
			}
 		}
		else
		{
			if (dwSubprogram->mInlineeInfo != NULL)
			{
				dwStartLineData = &dwSubprogram->mInlineeInfo->mFirstLineData;
				dwEndLineData = &dwSubprogram->mInlineeInfo->mLastLineData;
			}
		}

		DbgModule* dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		DbgModule* linkedModule = dbgModule->GetLinkedModule();
		if (!linkedModule->mDisplayName.empty())
			demangledName = linkedModule->mDisplayName + "!" + demangledName;

		if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) || (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid))
			demangledName = "#" + demangledName;

		_SetFlags(dwSubprogram);

		if ((dwLineData != NULL) && (dwSrcFile != NULL))
		{
			*outFile = dwSrcFile->GetLocalPath();
			_CheckHashSrcFile(*outFile, dbgModule, dwSrcFile);

			*outHotIdx = dbgModule->mHotIdx;
			*outLine = dwLineData->mLine;
			*outColumn = dwLineData->mColumn;
			*outLanguage = (int)dwSubprogram->mCompileUnit->mLanguage;

			if (dwEndLineData != NULL)
			{
				*outDefLineStart = dwStartLineData->mLine;
				*outDefLineEnd = dwEndLineData->mLine;
			}

			_FixFilePath(dbgModule);
			return demangledName;
		}
		else
		{
			_FixFilePath(dbgModule);
			return demangledName + StrFormat("+0x%X", pcAddress - dwSubprogram->mBlock.mLowPC);
		}
	}
	else
	{
		String symbolName;
		addr_target offset;
		DbgModule* dbgModule = NULL;
		if (mDebugTarget->FindSymbolAt(pcAddress, &symbolName, &offset, &dbgModule))
		{
			if (dbgModule->HasPendingDebugInfo())
			{
				*outFlags |= FrameFlags_HasPendingDebugInfo;

				if (mPendingDebugInfoLoad.ContainsKey(dbgModule))
				{
					String outName = EncodeDataPtr(pcAddress, true);
					if ((dbgModule != NULL) && (!dbgModule->mDisplayName.empty()))
						outName = dbgModule->mDisplayName + "!<Loading...>" + outName;
					return outName;
				}
			}

			DbgModule* linkedModule = dbgModule->GetLinkedModule();
			String demangledName = BfDemangler::Demangle(symbolName, DbgLanguage_Unknown);
			if (!linkedModule->mDisplayName.empty())
				demangledName = linkedModule->mDisplayName + "!" + demangledName;
			_FixFilePath(dbgModule);
			return demangledName + StrFormat("+0x%X", offset);
		}
	}

	DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress(pcAddress);
	DbgModule* linkedModule = NULL;
	if (dbgModule != NULL)
	{
		linkedModule = dbgModule->GetLinkedModule();
		if (dbgModule->HasPendingDebugInfo())
			*outFlags |= FrameFlags_HasPendingDebugInfo;
	}
	String outName = EncodeDataPtr(pcAddress, true);
	if ((linkedModule != NULL) && (!linkedModule->mDisplayName.empty()))
		outName = linkedModule->mDisplayName + "!" + outName;
	_FixFilePath(dbgModule);
	return outName;
}

String WinDebugger::GetStackFrameId(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	intptr addr = 0;
	if (wdStackFrame->mSubProgram != NULL)
		addr = wdStackFrame->mSubProgram->mBlock.mLowPC;
	else
		addr = wdStackFrame->mRegisters.GetPC();

	String str = StrFormat("Thread:%d SP:%llX Func:%llX", mActiveThread->mThreadId, wdStackFrame->mRegisters.GetSP(), addr);
	return str;
}

String WinDebugger::Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	DbgModule* dbgModule = NULL;
	DbgSrcFile* dbgSrcFile = NULL;

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* nextStackFrame = mCallStack[actualStackFrameIdx - 1];
		auto subProgram = nextStackFrame->mSubProgram;
		dbgModule = subProgram->mCompileUnit->mDbgModule;

		FixupLineDataForSubprogram(subProgram->mInlineeInfo->mRootInliner);
		DbgSubprogram* parentSubprogram = subProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
		auto foundLine = parentSubprogram->FindClosestLine(subProgram->mBlock.mLowPC, &parentSubprogram);
		if (foundLine != NULL)
			dbgSrcFile = parentSubprogram->GetLineSrcFile(*foundLine);

		DbgSubprogram* callingSubProgram = NULL;
		DbgLineData* callingLineData = FindLineDataAtAddress(nextStackFrame->mSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
		if ((callingLineData != NULL) && (callingSubProgram == wdStackFrame->mSubProgram))
			dbgSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
	}
	else
	{
		DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
		if (dwSubprogram != NULL)
		{
			FixupLineDataForSubprogram(dwSubprogram);
			addr_target findAddress = wdStackFrame->GetSourcePC();

			DbgSubprogram* dbgSubprogram = NULL;
			DbgLineData* dwLineData = dwSubprogram->FindClosestLine(findAddress, &dbgSubprogram, &dbgSrcFile);
			dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		}
	}

	if (dbgSrcFile != NULL)
	{
		// Note: we must use mFilePath here, make sure we don't use GetLocalPath()
		return dbgModule->GetOldSourceCommand(dbgSrcFile->mFilePath);
	}
	return "";
}

int WinDebugger::GetJmpState(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return -1;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();

	CPUInst inst;
	if (!mDebugTarget->DecodeInstruction(pcAddress, &inst))
		return -1;

	return inst.GetJmpState(wdStackFrame->mRegisters.mIntRegs.efl);
}

intptr WinDebugger::GetStackFrameCalleeAddr(int stackFrameIdx)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return -1;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();
	if (stackFrameIdx == -1)
		pcAddress = mShowPCOverride;

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* inlineStackFrame = mCallStack[actualStackFrameIdx - 1];
		return inlineStackFrame->mSubProgram->mBlock.mLowPC - 1;
	}

	return pcAddress - 1;
}

String WinDebugger::GetStackMethodOwner(int stackFrameIdx, int& language)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (!FixCallStackIdx(stackFrameIdx))
		return "";

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	if (actualStackFrameIdx >= (int)mCallStack.size())
		actualStackFrameIdx = 0;
	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	if (wdStackFrame->mSubProgram == NULL)
		return "";
	auto parentType = wdStackFrame->mSubProgram->GetParent();
	if (parentType == NULL)
		return "";
	parentType = parentType->GetPrimaryType();
	language = (int)parentType->GetLanguage();
	return parentType->ToString();
}

String WinDebugger::FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve)
{
	String result;
	if (mDebugTarget == NULL)
		return "";

	DbgSrcFile* srcFile = mDebugTarget->GetSrcFile(fileName);
	if (srcFile == NULL)
		return result;

	bool foundInSequence = false;
	WdBreakpoint* prevBreakpoint = NULL;

	int bestLineOffset = 0x7FFFFFFF;

	for (auto dbgSubprogram : srcFile->mLineDataRefs)
	{
		for (auto& lineData : dbgSubprogram->mLineInfo->mLines)
		{
			auto lineSrcFile = dbgSubprogram->GetLineSrcFile(lineData);
			if (lineSrcFile != srcFile)
				continue;
			int lineOffset = lineData.mLine - line;

			if ((lineOffset >= 0) && (lineOffset <= 12) && (lineOffset <= bestLineOffset))
			{
				if (lineOffset < bestLineOffset)
				{
					bestLineOffset = lineOffset;
					result = "";
				}

				if (!foundInSequence)
				{
					auto addr = dbgSubprogram->GetLineAddr(lineData);
					result += EncodeDataPtr(addr, false) + "\t" + dbgSubprogram->ToString() + "\n";
				}
			}

			// New sequence?
			if (!lineData.IsStackFrameSetup())
				foundInSequence = false;
		}
	}

	return result;
}

String WinDebugger::GetAddressSourceLocation(intptr address)
{
	DbgSubprogram* subProgram = NULL;
	DbgLineData* lineData = FindLineDataAtAddress(address, &subProgram);
	if (lineData != NULL)
		return StrFormat("%s:%d:%d", subProgram->GetLineSrcFile(*lineData)->GetLocalPath().c_str(), lineData->mLine + 1, lineData->mColumn + 1);

	String outSymbol;
	addr_target offset = 0;
	DbgModule* dbgModule;
	if (mDebugTarget->FindSymbolAt(address, &outSymbol, &offset, &dbgModule))
	{
		if (offset < 0x10000)
		{
			outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);
			if (offset > 0)
				outSymbol += StrFormat("+%x", offset);
			return outSymbol;
		}
	}

	return StrFormat("0x%@", address);
}

String WinDebugger::GetAddressSymbolName(intptr address, bool demangle)
{
	auto subProgram = mDebugTarget->FindSubProgram(address);
	if (subProgram != NULL)
		return subProgram->ToString();

	String outSymbol;
	addr_target offset = 0;
	DbgModule* dbgModule;
	if (mDebugTarget->FindSymbolAt(address, &outSymbol, &offset, &dbgModule))
	{
		if (offset < 0x10000)
		{
			if (demangle)
				outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);
			if (offset > 0)
				outSymbol += StrFormat("+%x", offset);
			return outSymbol;
		}
	}

	return StrFormat("0x%@", address);
}

String WinDebugger::DisassembleAtRaw(intptr inAddress)
{
	addr_target address = (addr_target)inAddress;

	const int addrBorder = 1024;

	for (int offset = 0; offset < 8; offset++)
	{
		String result;
		bool addOffset = true;
		bool hadAddr = false;

		DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress(address);
		DbgModuleMemoryCache* memCache = NULL;
		defer
		(
			if (dbgModule == NULL)
				delete memCache;
		);

		if ((dbgModule != NULL) && (dbgModule->mOrigImageData == NULL))
			dbgModule = NULL;

		result += "R\n"; // Raw

		addr_target addrStart = address;
		if (dbgModule != NULL)
		{
			dbgModule->ParseSymbolData();
			memCache = dbgModule->mOrigImageData;
			addrStart = BF_MAX((addr_target)dbgModule->mImageBase, address - addrBorder - offset);
		}
		else
		{
			memCache = new DbgModuleMemoryCache(addrStart & (4096 - 1), 4096 * 2);
		}

		if (memCache->mAddr == 0)
			return "";

		//addr_target imageBase = dbgModule->mImageBase;
		//int imageSize = dbgModule->mImageSize;

		addr_target dataAddr = addrStart;
		addr_target addrEnd = addrStart + addrBorder * 2 + 16;

		while (dataAddr < addrEnd)
		{
			if (dataAddr == address)
				hadAddr = true;
			if (dataAddr > address)
			{
				if (!hadAddr)
				{
					if (offset == 7)
					{
						dataAddr = address;
					}
					break;
				}
			}

			String outSymbol;
			addr_target symOffset = 0;
			DbgModule* symDWARF;

			if (mDebugTarget->FindSymbolAt(dataAddr, &outSymbol, &symOffset, &symDWARF))
			{
				if (symOffset == 0)
				{
					outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);
					if ((symDWARF != NULL) && (!symDWARF->mDisplayName.empty()))
						outSymbol = symDWARF->GetLinkedModule()->mDisplayName + "!" + outSymbol;
					result += "T " + outSymbol + ":\n";
				}
			}

			CPUInst inst;
			if (!mCPU->Decode(dataAddr, memCache, &inst))
			{
				if ((offset == 7) && (!hadAddr))
				{
					uint8 instData[1];
					memCache->Read(dataAddr, instData, 1);
					int instLen = 1;

#ifdef BF_DBG_32
					result += StrFormat("D %08X:        ", dataAddr);
#else
					result += StrFormat("D %@:        ", dataAddr);
#endif
					for (int i = 0; i < instLen; i++)
						result += StrFormat("%02X ", instData[i]);
					for (int i = instLen; i < 8; i++)
						result += "   ";
					result += "\n";
					dataAddr++;

					continue;
				}
				break;
			}

			int instLen = inst.GetLength();

#ifdef BF_DBG_32
			result += StrFormat("D %08X:        ", dataAddr);
#else
			result += StrFormat("D %@:        ", dataAddr);
#endif

			uint8 instData[32];
			int showInstLen = BF_MIN(32, instLen);
			memCache->Read(dataAddr, instData, showInstLen);

			for (int i = 0; i < showInstLen; i++)
				result += StrFormat("%02X ", instData[i]);
			for (int i = instLen; i < 8; i++)
				result += "   ";

			result += mCPU->InstructionToString(&inst, dataAddr);

			if ((inst.IsCall()) || (inst.IsBranch()))
			{
				addr_target targetAddr = inst.GetTarget();
				if (targetAddr != 0)
				{
					if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &symOffset))
					{
						if (symOffset < 0x10000)
						{
							outSymbol = BfDemangler::Demangle(outSymbol, DbgLanguage_Unknown);

							result += " ; " + outSymbol;
							if (symOffset > 0)
								result += StrFormat("+%x", symOffset);
							//result += ">";
						}
					}
				}
			}

			result += "\n";
			dataAddr += instLen;
		}

		if (!hadAddr)
			continue;

		return result;
	}

	return "";
}

String WinDebugger::DisassembleAt(intptr inAddress)
{
	BP_ZONE("WinDebugger::DisassembleAt");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	addr_target address = (addr_target)inAddress;

	if (mDebugTarget == NULL)
		return "";

	String result;
	auto dwSubProgram = mDebugTarget->FindSubProgram(address);
	if (dwSubProgram == NULL)
		return DisassembleAtRaw(address);
	dwSubProgram = dwSubProgram->GetRootInlineParent();
	DbgModule* dwarf = dwSubProgram->mCompileUnit->mDbgModule;

	int frameBaseRegister = mDebugTarget->GetFrameBaseRegister(dwSubProgram);

	addr_target addrStart = dwSubProgram->mBlock.mLowPC;
	addr_target addrEnd = dwSubProgram->mBlock.mHighPC;

	auto dwCompileUnit = dwSubProgram->mCompileUnit;
	{
		FixupLineData(dwCompileUnit);
	}

	DbgSrcFile* dwSrcFile = NULL;

	FixupLineDataForSubprogram(dwSubProgram);

	DbgLineData* dwLineData = NULL;
	if (dwSubProgram->mLineInfo != NULL)
    	dwLineData = &dwSubProgram->mLineInfo->mLines[0];
	int nextLineDataIdx = 1;

	if (dwSubProgram->mIsOptimized)
		result += "O\n";

	DbgSrcFile* srcFile = NULL;
	int firstLine = 0;
	int curLine = 0;
	if (dwLineData != NULL)
	{
		srcFile = dwSubProgram->GetLineSrcFile(*dwLineData);
		result += "S " + srcFile->GetLocalPath() + "\n";
		if (srcFile->mHashKind != DbgHashKind_None)
		{
			result += "H ";
			srcFile->GetHash(result);
			result += "\n";
		}

		curLine = BF_MAX(0, dwLineData->mLine - 5);
		//for (; curLine <= dwLineData->mLine; curLine++)
		result += StrFormat("L %d %d\n", curLine, dwLineData->mLine - curLine + 1);
		curLine = dwLineData->mLine + 1;
		firstLine = dwLineData->mLine;
	}

	Array<DbgSubprogram*> inlineStack;

	Array<DbgBlock*> blockList;
	blockList.push_back(&dwSubProgram->mBlock);

	addr_target dataAddr = addrStart;
	int decodeFailureCount = 0;

	auto& _PopInlineStack = [&]()
	{
		int depth = inlineStack.size();
		auto curStackEntry = inlineStack.back();
		if (depth > 1)
			result += StrFormat("T <<<%d Inline End ", depth);
		else
			result += "T <<< Inline End ";
		result += curStackEntry->ToString();
		result += "\n";
		inlineStack.pop_back();
	};

	std::function<void(DbgSubprogram* subprogram, int depth)> _UpdateInlineStackHelper = [&](DbgSubprogram* subprogram, int depth)
	{
		int stackIdx = depth - 1;
		if (stackIdx < inlineStack.size())
		{
			auto curStackEntry = inlineStack[stackIdx];
			if (curStackEntry != subprogram)
				_PopInlineStack();
		}

		if (depth > 1)
		{
			_UpdateInlineStackHelper(subprogram->mInlineeInfo->mInlineParent, depth - 1);
		}

		if (stackIdx >= inlineStack.size())
		{
			if (depth > 1)
				result += StrFormat("T >>>%d Inline ", depth);
			else
				result += "T >>> Inline ";
			result += subprogram->ToString();
			result += "\n";
			inlineStack.push_back(subprogram);
		}
	};

	auto _UpdateInlineStack = [&](DbgSubprogram* subprogram)
	{
		if (subprogram == NULL)
		{
			while (!inlineStack.IsEmpty())
				_PopInlineStack();
			return;
		}

		int inlineDepth = subprogram->GetInlineDepth();
		while (inlineDepth < inlineStack.size())
			_PopInlineStack();
		if (inlineDepth > 0)
			_UpdateInlineStackHelper(subprogram, inlineDepth);
	};

	while (dataAddr < addrEnd)
	{
		// Pop off old scopes
		while (blockList.size() > 0)
		{
			auto lastBlock = blockList.back();
			if (dataAddr < lastBlock->mHighPC)
				break;
			blockList.pop_back();
		}

		// Check entry into new child scopes
		auto lastBlock = blockList.back();
		for (auto checkBlock : lastBlock->mSubBlocks)
		{
			if ((dataAddr >= checkBlock->mLowPC) && (dataAddr < checkBlock->mHighPC))
			{
				blockList.push_back(checkBlock);
				break;
			}
		}

		bool allowSourceJump = false;

		if ((dwLineData != NULL) && (dwLineData->mContribSize != 0) && (dataAddr >= dwSubProgram->GetLineAddr(*dwLineData) + dwLineData->mContribSize))
		{
			DbgSubprogram* inlinedSubprogram = NULL;
			auto inlinedLine = dwSubProgram->FindClosestLine(dataAddr, &inlinedSubprogram);

			_UpdateInlineStack(dwSubProgram);
		}

		// Update line data
		while ((dwLineData != NULL) && (dwSubProgram->GetLineAddr(*dwLineData) <= dataAddr))
		{
			_UpdateInlineStack(dwSubProgram->GetLineInlinee(*dwLineData));

			const int lineLimit = 5; // 15

			if (allowSourceJump)
				curLine = dwLineData->mLine;

			auto lineSrcFile = dwSubProgram->GetLineSrcFile(*dwLineData);

			if (lineSrcFile != srcFile)
			{
				srcFile = lineSrcFile;
				result += "S ";
				result += srcFile->GetLocalPath();
				result += "\n";
				// Just show the one line from the new file
				curLine = dwLineData->mLine;
			}

			if (dwLineData->mLine < curLine - 1)
			{
				// Jumping backwards - possibly into inlined method, or possibly in current method.
				//  Show previous 6 lines, for context
				curLine = BF_MAX(0, dwLineData->mLine - lineLimit);
			}

			if ((curLine <= firstLine) && (dwLineData->mLine >= firstLine))
			{
				// Jumping from inlined method (declared above) back into main method
				curLine = dwLineData->mLine;
			}

			if (curLine < dwLineData->mLine - lineLimit)
			{
				// Don't show huge span of source - only show the last 6 lines at maximum
				curLine = dwLineData->mLine - lineLimit;
			}

			//for ( ; curLine <= dwLineData->mLine; curLine++)
			result += StrFormat("L %d %d\n", curLine, dwLineData->mLine - curLine + 1);
			curLine = dwLineData->mLine + 1;

			DbgLineData* nextLineData = NULL;
			while (nextLineDataIdx < dwSubProgram->mLineInfo->mLines.mSize)
			{
				nextLineData = &dwSubProgram->mLineInfo->mLines[nextLineDataIdx];

				//TODO:
				/*{
					result += StrFormat("T LineIdx: %d (%@ to %@)", nextLineDataIdx, dwSubProgram->GetLineAddr(*nextLineData), dwSubProgram->GetLineAddr(*nextLineData) + nextLineData->mContribSize);
					auto inlinee = dwSubProgram->GetLineInlinee(*nextLineData);
					if (inlinee != NULL)
					{
						result += StrFormat(" Inlinee: %s Depth: %d", inlinee->mName, inlinee->GetInlineDepth());
					}
					result += "\n";
				}*/

				auto nextLineAddr = dwSubProgram->GetLineAddr(*nextLineData);
				if (nextLineAddr > dataAddr)
				{
					if (nextLineDataIdx + 1 < dwSubProgram->mLineInfo->mLines.mSize)
					{
						auto peekLineData = &dwSubProgram->mLineInfo->mLines[nextLineDataIdx + 1];
						if (peekLineData->mRelAddress == nextLineData->mRelAddress)
						{
							// Use the later entry
							++nextLineDataIdx;
							continue;
						}
					}

					break;
				}
				// If we go back to an older entry beacuse of a gap then we need to catch back up...
				++nextLineDataIdx;
				nextLineData = NULL; // Keep searching...
			}

			dwLineData = nextLineData;
			nextLineDataIdx++;
		}

		// Have we gone off the end of the inline function?
		//  We may not have an explicit non-inlined line data at the transition point...
		while (!inlineStack.IsEmpty())
		{
			auto subProgram = inlineStack.back();
			if (dataAddr < subProgram->mBlock.mHighPC)
				break;
			_PopInlineStack();
		}

		bool hadDecodeFailure = false;
		CPUInst inst;
		if (!mCPU->Decode(dataAddr, dwarf->mOrigImageData, &inst))
			hadDecodeFailure = true;

		if ((decodeFailureCount == 8) || ((decodeFailureCount > 0) && (!hadDecodeFailure)))
		{
			for (int i = decodeFailureCount; i < 4 + sizeof(addr_target); i++)
				result += "   ";
			result += " ???\n";
			decodeFailureCount = 0;
		}

		if (decodeFailureCount == 0)
		{
#ifdef BF_DBG_32
			result += StrFormat("D %08X:  ", dataAddr);
#else
			result += StrFormat("D %@:  ", dataAddr);
#endif
		}
		if (hadDecodeFailure)
		{
			uint8 byte = 0;
			dwarf->mOrigImageData->Read(dataAddr, &byte, 1);
			result += StrFormat("%02X ", byte);
			dataAddr++;
			decodeFailureCount++;
			continue;
		}

		int instLen = inst.GetLength();
		uint8 instData[32];
		int showInstLen = BF_MIN(32, instLen);
		dwarf->mOrigImageData->Read(dataAddr, instData, showInstLen);

		for (int i = 0; i < showInstLen; i++)
			result += StrFormat("%02X ", instData[i]);

		for (int i = instLen; i < 4 + sizeof(addr_target); i++)
			result += "   ";

		result += " ";

		result += mCPU->InstructionToString(&inst, dataAddr);

		int reg;
		int offset;
		if (inst.GetIndexRegisterAndOffset(&reg, &offset))
		{
			for (int blockIdx = (int)blockList.size() - 1; blockIdx >= 0; blockIdx--)
			{
				auto dwBlock = blockList[blockIdx];
				for (auto variable : dwBlock->mVariables)
				{
					int varRegister;
					int varOffset;
					if (mDebugTarget->GetVariableIndexRegisterAndOffset(variable, &varRegister, &varOffset))
					{
						if (varRegister == -1)
							varRegister = frameBaseRegister;
						if ((reg == varRegister) && (offset == varOffset))
						{
							result += " ; ";
							result += variable->mName;
							break;
						}
					}
				}
			}
		}
		else if ((inst.IsCall()) || (inst.IsBranch()) || (inst.IsLoadAddress()))
		{
			addr_target targetAddr = inst.GetTarget();
			if (targetAddr != 0)
			{
				if ((targetAddr >= addrStart) && (targetAddr < addrEnd))
				{
					result += StrFormat("\nJ %s", EncodeDataPtr(targetAddr, false).c_str());
				}
				else
				{
					String outSymbol;
					addr_target offset = 0;
					if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &offset))
					{
						if (offset < 0x10000)
						{
							outSymbol = BfDemangler::Demangle(outSymbol, dwSubProgram->GetLanguage());

							result += " ; " + outSymbol;
							if (offset > 0)
								result += StrFormat("+%x", offset);
						}
					}
				}
			}
		}

		result += "\n";
		dataAddr += instLen;
	}

	// Why did we want to "show lines at end"??
	// Show lines at end
	/*if (curLine > 0)
	{
		for (int i = 0; i < 6; i++, curLine++)
			result += StrFormat("L %d\n", curLine);
	}*/
	return result;
}

String WinDebugger::FindLineCallAddresses(intptr inAddress)
{
	String callAddresses;

	addr_target address = (addr_target)inAddress;

	DbgSubprogram* dwSubprogram = NULL;

	DbgLineData* startLineData = FindLineDataAtAddress(address, &dwSubprogram, NULL);

	if (dwSubprogram == NULL)
		return "";

	CPURegisters registers;
	PopulateRegisters(&registers);

	auto inlinerSubprogram = dwSubprogram->GetRootInlineParent();
	FixupLineDataForSubprogram(inlinerSubprogram);

	if (inlinerSubprogram->mLineInfo->mLines.mSize == 0)
		return "";
	auto lineData = &inlinerSubprogram->mLineInfo->mLines[0];

	addr_target addr = dwSubprogram->mBlock.mLowPC;
	addr_target endAddr = dwSubprogram->mBlock.mHighPC;

	DbgSubprogram* checkSubprogram = dwSubprogram;
	DbgLineData* checkLineData = lineData;
	addr_target checkLineAddr = 0;

	int lineIdx = 0;
	while (checkLineData != NULL)
	{
		//auto nextLineData = dwSubprogram->mCompileUnit->mLineDataMap.GetNext(checkLineData);
		++lineIdx;

		DbgLineData* nextLineData = NULL;
		addr_target nextLineAddr;
		if (lineIdx < inlinerSubprogram->mLineInfo->mLines.size())
		{
			nextLineData = &inlinerSubprogram->mLineInfo->mLines[lineIdx];
			nextLineAddr = dwSubprogram->GetLineAddr(*nextLineData);
		}
		else
			nextLineAddr = inlinerSubprogram->mBlock.mHighPC;

		// This stuff doesn't make sense...
		DbgSubprogram* nextSubProgram;
		if (nextLineData != NULL)
		{
			if (nextLineAddr > dwSubprogram->mBlock.mHighPC)
				break;

			endAddr = nextLineAddr;
			nextSubProgram = mDebugTarget->FindSubProgram(endAddr);
			if (nextSubProgram != NULL)
			{
				auto dbgModule = nextSubProgram->mCompileUnit->mDbgModule;
				dbgModule->ParseSymbolData();
			}
		}
		else
		{
			nextSubProgram = dwSubprogram;
			endAddr = dwSubprogram->mBlock.mHighPC;
		}

		auto _HandleSection = [&]()
		{
			while (addr < endAddr)
			{
				CPUInst inst;
				if (!mDebugTarget->DecodeInstruction(addr, &inst))
					break;

				*registers.GetPCRegisterRef() = addr;
				if (inst.IsCall())
				{
					bool addSymbol = true;

					if (addr < (addr_target)inAddress)
						callAddresses += "-";
					callAddresses += EncodeDataPtr(addr, false);

					addr_target targetAddr = inst.GetTarget(this, &registers);
					if (targetAddr != 0)
					{
						String outSymbol;
						auto subprogram = mDebugTarget->FindSubProgram(targetAddr);
						if (subprogram != NULL)
						{
							CreateFilterName(outSymbol, subprogram);
							addSymbol = true;
						}
						else
						{
							addr_target offset = 0;
							String fullSymbolName;
							if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &offset))
							{
								if (offset < 0x200)
								{
									//outSymbol = BfDemangler::Demangle(outSymbol, dwSubprogram->GetLanguage());
									if (outSymbol == "___chkstk_ms")
										addSymbol = false;
									else
									{
										String demangledName = BfDemangler::Demangle(outSymbol, DbgLanguage_C);
										outSymbol.clear();
										CreateFilterName(outSymbol, demangledName.c_str(), DbgLanguage_C);
									}
								}
								else
									outSymbol.clear();
							}
						}

						if (addSymbol)
						{
							if (outSymbol.empty())
								callAddresses += "\tFunc@" + EncodeDataPtr(targetAddr, false);
							else
								callAddresses += "\t" + outSymbol;

							String attrs;
							bool isFiltered = false;
							if (subprogram != NULL)
							{
								subprogram->PopulateSubprogram();
								isFiltered = subprogram->mIsStepFilteredDefault;
								if (isFiltered)
									attrs += "d"; // 'd' for default filtered
							}

							StepFilter* stepFilterPtr = NULL;
							if (mDebugManager->mStepFilters.TryGetValue(outSymbol, &stepFilterPtr))
								isFiltered = stepFilterPtr->IsFiltered(isFiltered);

							if (isFiltered)
								attrs += "f";  // 'f' for filter

							if (!attrs.IsEmpty())
								callAddresses += "\t" + attrs;
						}
					}

					if (addSymbol)
						callAddresses += "\n";
				}

				inst.PartialSimulate(this, &registers);
				addr += inst.GetLength();
			}
		};

		// For inlining - only add calls that are found either directly in our main block (not an inlined block)
		//  But add inlined methods when their parent is our current block
		if ((checkSubprogram == dwSubprogram) && (checkLineData->mLine == startLineData->mLine))
		{
			_HandleSection();
		}
		else if ((checkSubprogram->mInlineeInfo != NULL) && (checkSubprogram->mInlineeInfo->mInlineParent == dwSubprogram))
		{
			if (checkLineAddr == checkSubprogram->mBlock.mLowPC)
			{
				addr_target inlineStartAddr = checkSubprogram->mBlock.mLowPC;

				// Find the calling line
				DbgSubprogram* callingSubprogram = dwSubprogram;
				auto checkLineData = dwSubprogram->FindClosestLine(inlineStartAddr, &callingSubprogram);
				if ((checkLineData != NULL) && (checkLineData->mCtxIdx == startLineData->mCtxIdx) && (checkLineData->mLine == startLineData->mLine))
				{
					if (inlineStartAddr <= (addr_target)inAddress)
						callAddresses += "-";
					callAddresses += EncodeDataPtr(inlineStartAddr, false);
					String outSymbol;
					CreateFilterName(outSymbol, checkSubprogram);
					callAddresses += "\t" + outSymbol;

					bool isFiltered = dwSubprogram->mIsStepFilteredDefault;
					StepFilter* stepFilterPtr;
					if (mDebugManager->mStepFilters.TryGetValue(outSymbol, &stepFilterPtr))
						isFiltered = stepFilterPtr->IsFiltered(isFiltered);

					if (isFiltered)
						callAddresses += "\tf"; // 'f' for filter

					callAddresses += "\n";
				}

// 				if (checkSubprogram->mBlock.mHighPC < endAddr)
// 				{
// 					addr = checkSubprogram->mBlock.mHighPC;
// 					_HandleSection();
// 				}
			}

			// If we have unattributed data after the end of an inlined method, add that
			if ((endAddr > checkSubprogram->mBlock.mHighPC) && (nextSubProgram == dwSubprogram))
			{
				addr = checkSubprogram->mBlock.mHighPC;
				_HandleSection();
			}
		}

		checkLineData = nextLineData;
		checkSubprogram = nextSubProgram;
		checkLineAddr = nextLineAddr;
		addr = endAddr;
	}

	return callAddresses;
}

String WinDebugger::GetCurrentException()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String result = StrFormat("%s\n%08X",
		EncodeDataPtr((addr_target)(intptr)mCurException.ExceptionAddress, true).c_str(),
		mCurException.ExceptionCode);

	String exStr;
	switch (mCurException.ExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	{
		String accessType;
		if (mCurException.ExceptionInformation[0] == 0)
			accessType = "reading from";
		else if (mCurException.ExceptionInformation[0] == 8)
			accessType = "executing";
		else
			accessType = "writing to";
		exStr = StrFormat("EXCEPTION_ACCESS_VIOLATION %s %s", accessType.c_str(), EncodeDataPtr((addr_target)mCurException.ExceptionInformation[1], true).c_str());
	}
	break;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		exStr = "EXCEPTION_DATATYPE_MISALIGNMENT";
	case EXCEPTION_SINGLE_STEP:
		exStr = "EXCEPTION_SINGLE_STEP";
		break;
	case EXCEPTION_BREAKPOINT:
		exStr = "EXCEPTION_BREAKPOINT";
		break;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		exStr = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		break;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		exStr = "EXCEPTION_FLT_DENORMAL_OPERAND";
		break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		exStr = "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_FLT_INEXACT_RESULT:
		exStr = "EXCEPTION_FLT_INEXACT_RESULT";
		break;
	case EXCEPTION_FLT_INVALID_OPERATION:
		exStr = "EXCEPTION_FLT_INVALID_OPERATIO";
		break;
	case EXCEPTION_FLT_OVERFLOW:
		exStr = "EXCEPTION_FLT_OVERFLOW";
		break;
	case EXCEPTION_FLT_STACK_CHECK:
		exStr = "EXCEPTION_FLT_STACK_CHECK";
		break;
	case EXCEPTION_FLT_UNDERFLOW:
		exStr = "EXCEPTION_FLT_UNDERFLOW";
		break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		exStr = "EXCEPTION_INT_DIVIDE_BY_ZERO";
		break;
	case EXCEPTION_INT_OVERFLOW:
		exStr = "EXCEPTION_INT_OVERFLOW";
		break;
	case EXCEPTION_PRIV_INSTRUCTION:
		exStr = "EXCEPTION_PRIV_INSTRUCTION";
		break;
	case EXCEPTION_IN_PAGE_ERROR:
		exStr = "EXCEPTION_IN_PAGE_ERROR";
		break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		exStr = "EXCEPTION_ILLEGAL_INSTRUCTION";
		break;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		exStr = "EXCEPTION_NONCONTINUABLE_EXCEPTION";
		break;
	case EXCEPTION_STACK_OVERFLOW:
		exStr = "EXCEPTION_STACK_OVERFLOW";
		break;
	case EXCEPTION_INVALID_DISPOSITION:
		exStr = "EXCEPTION_INVALID_DISPOSITION";
		break;
	case EXCEPTION_GUARD_PAGE:
		exStr = "EXCEPTION_GUARD_PAGE";
		break;
	case EXCEPTION_INVALID_HANDLE:
		exStr = "EXCEPTION_INVALID_HANDLE";
		break;
	case CONTROL_C_EXIT:
		exStr = "CONTROL_C_EXIT";
		break;
	default:
		exStr += StrFormat("EXCEPTION %08X", mCurException.ExceptionCode);
	}
	if (mActiveThread != NULL)
		exStr += StrFormat(" in thread %d", mActiveThread->mThreadId);

	if (!exStr.empty())
		result += "\n" + exStr;

	// After we retrieve the exception then we can go back to just being normal 'paused'
	//  This allows us to evaluate stuff, Set Next Statement, etc.
	mRunState = RunState_Paused;

	return result.c_str();
}

void WinDebugger::SetAliasPath(const StringImpl& origPath, const StringImpl& localPath)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String fixedOrigPath = FixPathAndCase(origPath);
	String fixedLocalPath = FixPathAndCase(localPath);

	auto origFile = mDebugTarget->AddSrcFile(origPath);
	origFile->mLocalPath = FixPath(localPath);

	mDebugTarget->mLocalToOrigSrcMap[fixedLocalPath] = fixedOrigPath;
	// We invalidate the step filters, because previously-failing 'CheckSourceFileExist' checks may now succeed
	mDebugManager->mStepFilterVersion++;
}

String WinDebugger::GetModulesInfo()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	String str;

	for (auto module : mDebugTarget->mDbgModules)
	{
		COFF* coff = (COFF*)module;
		if (module->mHotIdx > 0)
			continue;

		str += module->mDisplayName;
		str += "\t";
		if (module->mLoadState == DbgModuleLoadState_Loaded)
		{
			str += module->mFilePath;
		}
		else if (module->mLoadState == DbgModuleLoadState_NotLoaded)
		{
			str += module->mFilePath;
			str += " (Loading...)";
		}
		else if (module->mLoadState == DbgModuleLoadState_Failed)
		{
			str += "!";
			str += module->mFilePath;
		}

		if (module->mMappedImageFile != NULL)
		{
			str += " (";
			str += module->mMappedImageFile->mFileName;
			str += ")";
		}

		str += "\t";
		str += coff->mPDBPath;
		str += "\t";
		str += module->mVersion;
		str += StrFormat("\t%@-%@\t%dk\t", module->mImageBase, module->mImageBase + module->mImageSize, module->mImageSize / 1024);

		time_t timestamp = coff->mTimeStamp;
		if (timestamp == 0)
			timestamp = GetFileTimeWrite(coff->mFilePath);
		if (timestamp != 0)
		{
			char timeString[256];
			auto time_info = localtime(&timestamp);
			strftime(timeString, sizeof(timeString), "%D %T", time_info);
			str += timeString;
		}
		str += "\n";
	}

	return str;
}

void WinDebugger::CancelSymSrv()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);
	if (mActiveSymSrvRequest != NULL)
		mActiveSymSrvRequest->Cancel();
}

bool WinDebugger::HasPendingDebugLoads()
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	return (!mPendingImageLoad.IsEmpty()) || (!mPendingDebugInfoLoad.IsEmpty());
}

int WinDebugger::LoadImageForModule(const StringImpl &modulePath, const StringImpl& imagePath)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (modulePath.Equals(dbgModule->mFilePath, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			auto coff = (COFF*)dbgModule;

			if (!coff->LoadModuleImage(imagePath))
			{
				mDebugManager->mOutMessages.push_back("error Failed to load image " + imagePath);
			}
			ModuleChanged(dbgModule);

			return 0;
		}
	}

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(DbgModule* dbgModule)
{
	if (!dbgModule->HasPendingDebugInfo())
		return 0;

	if (dbgModule->RequestDebugInfo())
	{
		ClearCallStack(); // Make this re-resolve with debug info
		return 1;
	}

	DbgPendingDebugInfoLoad* dbgPendingDebugInfoLoad = NULL;
	if (mPendingDebugInfoLoad.TryAdd(dbgModule, NULL, &dbgPendingDebugInfoLoad))
	{
		dbgPendingDebugInfoLoad->mModule = dbgModule;
		dbgPendingDebugInfoLoad->mAllowRemote = true;
		return 2;
	}
	dbgPendingDebugInfoLoad->mAllowRemote = true;

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(const StringImpl& moduleName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		String checkModuleName = GetFileName(dbgModule->mFilePath);
		if (moduleName.Equals(checkModuleName, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			return LoadDebugInfoForModule(dbgModule);
		}
	}

	return 0;
}

int WinDebugger::LoadDebugInfoForModule(const StringImpl& modulePath, const StringImpl& debugFileName)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	for (auto dbgModule : mDebugTarget->mDbgModules)
	{
		if (modulePath.Equals(dbgModule->mFilePath, StringImpl::CompareKind_OrdinalIgnoreCase))
		{
			auto coff = (COFF*)dbgModule;

			String err;
			if (!coff->mPDBLoaded)
			{
				dbgModule->mFailMsgPtr = &err;
				if (coff->TryLoadPDB(debugFileName, coff->mWantPDBGuid, coff->mWantAge))
				{
					ModuleChanged(dbgModule);
				}
				dbgModule->mFailMsgPtr = NULL;
			}
			else
			{
				err = StrFormat("Module '%s' already has debug information loaded", GetFileName(modulePath).c_str());
			}

			if (!err.IsEmpty())
			{
				mDebugManager->mOutMessages.push_back("error " + err);
			}

			return 0;
		}
	}

	return 0;
}

void WinDebugger::FixupLineData(DbgCompileUnit* compileUnit)
{
	if (!compileUnit || !compileUnit->mNeedsLineDataFixup)
		return;
	compileUnit->mNeedsLineDataFixup = false;
}

static int CompareLineData(const void* lineDataP1, const void* lineDataP2)
{
	int cmpResult = (int)(((DbgLineData*)lineDataP1)->mRelAddress - ((DbgLineData*)lineDataP2)->mRelAddress);
	if (cmpResult != 0)
		return cmpResult;

	// A larger contrib size means it's the 'outer' inlinee
	cmpResult = -(((DbgLineData*)lineDataP1)->mContribSize - ((DbgLineData*)lineDataP2)->mContribSize);
	if (cmpResult != 0)
		return cmpResult;

	return -(((DbgLineData*)lineDataP1)->mCtxIdx - ((DbgLineData*)lineDataP2)->mCtxIdx);
}

void WinDebugger::FixupLineDataForSubprogram(DbgSubprogram* subProgram)
{
	if ((subProgram == NULL) || (!subProgram->mNeedLineDataFixup))
		return;

	BP_ZONE("FixupLineDataForSubprogram");

	subProgram->mNeedLineDataFixup = false;
	if (subProgram->mInlineeInfo != NULL)
		FixupLineDataForSubprogram(subProgram->mInlineeInfo->mRootInliner);

	if ((subProgram->mLineInfo == NULL) || (subProgram->mLineInfo->mLines.mSize == 0))
		return;

	//TODO: I think this was covering up a bug in DWARF line encoding? Figure this out
// 	if (subProgram->mLineInfo->mLines.mSize >= 2)
// 	{
// 		DbgLineData* line0 = &subProgram->mLineInfo->mLines[0];
// 		DbgLineData* line1 = &subProgram->mLineInfo->mLines[1];
//
//
// 		if ((line0->mRelAddress == line1->mRelAddress) && (!line0->IsStackFrameSetup()) && (line1->IsStackFrameSetup()))
// 		{
// 			CPUInst inst;
// 			if (mCPU->Decode(line0->mAddress, subProgram->mCompileUnit->mDbgModule->mOrigImageData, &inst))
// 				line1->mAddress += inst.GetLength();
// 		}
// 	}

	qsort(subProgram->mLineInfo->mLines.mVals, subProgram->mLineInfo->mLines.mSize, sizeof(DbgLineData), CompareLineData);

	// If we have multiple lines with the same line/column/context, merge them
	if (!subProgram->mLineInfo->mLines.IsEmpty())
	{
		auto prevLine = &subProgram->mLineInfo->mLines[0];
		for (int i = 1; i < subProgram->mLineInfo->mLines.mSize; i++)
		{
			auto nextLine = &subProgram->mLineInfo->mLines[i];

			if ((nextLine->mLine == prevLine->mLine) && (nextLine->mColumn == prevLine->mColumn) && (nextLine->mCtxIdx == prevLine->mCtxIdx) &&
				(nextLine->mRelAddress == prevLine->mRelAddress + prevLine->mContribSize))
			{
				prevLine->mContribSize += nextLine->mContribSize;
				// This messed up inline cases because mContribSize actually INCLUDES inlined lines so it caused the address to skip too far
				//nextLine->mRelAddress += nextLine->mContribSize;
				//nextLine->mContribSize = 0;
			}
			else
			{
				prevLine = nextLine;
			}
		}
	}
}

void WinDebugger::ReserveHotTargetMemory(int size)
{
	HotTargetMemory hotTargetMemory;
	hotTargetMemory.mOffset = 0;
	hotTargetMemory.mSize = 0;
	hotTargetMemory.mPtr = NULL;

	if (size > 0)
	{
		// In 64-bit mode we have a reserved region on program load that we commit here because the offsets
		//  must be within 32-bits of the original EXE image, but in 32-bit mode we don't reserve anything
		//  until here
#ifdef BF_DBG_32
	//hotTargetMemory.mSize = std::max(1024 * 1024, size);
		BF_ASSERT((size & (mPageSize - 1)) == 0);
		hotTargetMemory.mSize = size;
		hotTargetMemory.mPtr = (addr_target)(intptr)VirtualAllocEx(mProcessInfo.hProcess, NULL, hotTargetMemory.mSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		mDebugTarget->mHotHeap->AddTrackedRegion(hotTargetMemory.mPtr, hotTargetMemory.mSize);
#else
		hotTargetMemory.mSize = size;
		hotTargetMemory.mPtr = mDebugTarget->mHotHeap->Alloc(size);
		BF_ASSERT(hotTargetMemory.mPtr != 0);
		auto ptr = ::VirtualAllocEx(mProcessInfo.hProcess, (void*)(intptr)hotTargetMemory.mPtr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		BF_ASSERT(ptr == (void*)(intptr)hotTargetMemory.mPtr);
#endif
	}

	BfLogDbg("ReserveHotTargetMemory %p %d\n", hotTargetMemory.mPtr, hotTargetMemory.mSize);
	int err = GetLastError();
	mHotTargetMemory.push_back(hotTargetMemory);
}

addr_target WinDebugger::AllocHotTargetMemory(int size, bool canExecute, bool canWrite, int* outAllocSize)
{
	int prot = PAGE_READWRITE;
	if (canExecute && canWrite)
		prot = PAGE_EXECUTE_READWRITE;
	else if (canExecute)
		prot = PAGE_EXECUTE_READ;

	size = (size + (mPageSize - 1)) & ~(mPageSize - 1);
	*outAllocSize = size;

	HotTargetMemory* hotTargetMemory = NULL;
	bool foundHotTargetMemory = false;
	for (int i = mHotTargetMemory.mSize - 1; i >= BF_MAX(mHotTargetMemory.mSize - 32, 0); i--)
	{
		hotTargetMemory = &mHotTargetMemory[i];
		if (hotTargetMemory->mPtr == 0)
		{
			Fail("Failed to allocate memory for hot loading");
			return 0;
		}

		if (hotTargetMemory->GetSizeLeft() >= size)
		{
			foundHotTargetMemory = true;
			break;
		}
	}

	if (!foundHotTargetMemory)
	{
		ReserveHotTargetMemory(size);
		foundHotTargetMemory = true;
		hotTargetMemory = &mHotTargetMemory.back();
	}

	BF_ASSERT(hotTargetMemory->mOffset + size <= hotTargetMemory->mSize);
	addr_target result = hotTargetMemory->mPtr + hotTargetMemory->mOffset;

	::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)result, size, prot, NULL);

	BfLogDbg("AllocHotTargetMemory: %p %d %d %d\n", result, size, canExecute, canWrite);

	hotTargetMemory->mOffset += size;
	return result;
}

void WinDebugger::ReleaseHotTargetMemory(addr_target addr, int size)
{
#ifdef BF_DBG_32
	::VirtualFreeEx(mProcessInfo.hProcess, (void*)(intptr)addr, 0, MEM_RELEASE);
#else
	mDebugTarget->mHotHeap->Release(addr, size);
	::VirtualFreeEx(mProcessInfo.hProcess, (void*)(intptr)addr, size, MEM_DECOMMIT);
#endif
}

void WinDebugger::CleanupHotHeap()
{
	mDebugTarget->mLastHotHeapCleanIdx = mDebugTarget->mHotHeap->mBlockAllocIdx;

	// Our criteria for determining whether a hot loaded file is still being used:
	//  1) If we are currently executing a method from that object file.
	//  2) If the symbol map has a symbol with that address.
	//  3) If the static variable map contains a reference - including a conservative scan of the data
	//        This handles vdata references
	// This is a conservative check which won't purge hot reloads that contain deleted
	//  methods (for example), but it will purge hot reloads where all the changed
	//  data has been overwritten.
	// For delegate bindings, the original module declaring the bind creates a "preserve"
	//  global such as "bf_hs_preserve@_ZN5TestO4TestEv", whose preserved symbol ensures it
	//  doesn't get unloaded.  The current version of that method resides in "_ZN5TestO4TestEv",
	//  ensuring that the method pointed to by the global variable is valid
	mDebugTarget->mHotHeap->ClearReferencedFlags();

	addr_target lowAddr = mDebugTarget->mHotHeap->mHotAreaStart;
	addr_target highAddr = lowAddr + mDebugTarget->mHotHeap->mHotAreaSize;

	// Do conservative scan through all thread stacks.  Stack traces aren't 100% reliable, so we
	//  need to do a full conservative scan of any addresses stored in the stack
	//  to ensure we don't miss any return addresses
	for (int threadIdx = 0; threadIdx < (int)mThreadList.size(); threadIdx++)
	{
		WdThreadInfo* threadInfo = mThreadList[threadIdx];

		BF_CONTEXT lcContext;
		lcContext.ContextFlags = BF_CONTEXT_CONTROL;
		BF_GetThreadContext(threadInfo->mHThread, &lcContext);

		addr_target checkStackAddr = BF_CONTEXT_SP(lcContext);
		checkStackAddr &= ~(sizeof(addr_target) - 1);

		// Conservative check on registers
		for (int regNum = 0; regNum < sizeof(BF_CONTEXT)/sizeof(addr_target); regNum++)
		{
			addr_target checkAddr = ((addr_target*)&lcContext)[regNum];
			if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
				mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
		}

		// Conservative check on all stack data
		while (checkStackAddr < threadInfo->mStartSP)
		{
			addr_target checkAddrArr[1024];
			int numAddrsChecking = BF_MIN(1024, (int)((threadInfo->mStartSP - checkStackAddr) / sizeof(addr_target)));
			ReadMemory(checkStackAddr, numAddrsChecking * sizeof(addr_target), checkAddrArr);
			checkStackAddr += numAddrsChecking * sizeof(addr_target);

			for (int addrIdx = 0; addrIdx < numAddrsChecking; addrIdx++)
			{
				addr_target checkAddr = checkAddrArr[addrIdx];
				if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
					mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
			}
		}
	}

	auto mainModule = mDebugTarget->mTargetBinary;
	for (auto entry : mainModule->mSymbolNameMap)
	{
		auto dwSymbol = entry->mValue;
		addr_target checkAddr = dwSymbol->mAddress;
		if ((checkAddr >= lowAddr) && (checkAddr < highAddr))
			mDebugTarget->mHotHeap->MarkBlockReferenced(checkAddr);
	}

	mDebugTarget->CleanupHotHeap();

	BfLogDbg("Hot load memory used: %dk\n", (int)mDebugTarget->mHotHeap->GetUsedSize() / 1024);
}

int WinDebugger::EnableWriting(intptr address, int size)
{
	DWORD oldProt;
	bool success = ::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)address, size, PAGE_READWRITE, &oldProt);
	if (!success)
	{
		int err = GetLastError();
	}
	return (int)oldProt;
}

int WinDebugger::SetProtection(intptr address, int size, int prot)
{
	DWORD oldProt;
	::VirtualProtectEx(mProcessInfo.hProcess, (void*)(intptr)address, size, prot, &oldProt);
	return (int)oldProt;
}

void WinDebugger::EnableMemCache()
{
	mMemCacheAddr = 1;
}

void WinDebugger::DisableMemCache()
{
	mMemCacheAddr = 0;
}

bool WinDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	if (local)
	{
		__try
		{
			memcpy(dest, (void*)address, length);
			return true;
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	if (mMemCacheAddr != 0)
	{
		addr_target targetAddr = (addr_target)address;
		if ((targetAddr >= mMemCacheAddr) && (targetAddr + length <= mMemCacheAddr + WD_MEMCACHE_SIZE) && (mMemCacheAddr > 1))
		{
			memcpy(dest, mMemCacheData + (targetAddr - mMemCacheAddr), length);
			return true;
		}

		// We need a new block
		SIZE_T dwReadBytes;
		if (::ReadProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, mMemCacheData, (SIZE_T)WD_MEMCACHE_SIZE, &dwReadBytes) != 0)
		{
			mMemCacheAddr = targetAddr;
			memcpy(dest, mMemCacheData, length);
			return true;
		}

		// Failed, turn off caching
		mMemCacheAddr = 0;
	}

	SIZE_T dwReadBytes;
	if (::ReadProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, dest, (SIZE_T)length, &dwReadBytes) != 0)
		return true;
	int lastErr = ::GetLastError();
	memset(dest, 0, length);
	return false;
}

bool WinDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	SIZE_T dwBytesWritten = 0;
	int result = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, src, (SIZE_T)length, &dwBytesWritten);
	return result != 0;
}

addr_target WinDebugger::GetTLSOffset(int tlsIndex)
{
	typedef LONG NTSTATUS;
	typedef DWORD KPRIORITY;
	typedef WORD UWORD;

	enum THREADINFOCLASS
	{
		ThreadBasicInformation,
	};

	struct CLIENT_ID
	{
		HANDLE UniqueProcess;
		HANDLE UniqueThread;
	};

	struct
	{
		NTSTATUS mExitStatus;
		void* mTebBaseAddress;
		CLIENT_ID mClientId;
		KAFFINITY mAffinityMask;
		KPRIORITY mPriority;
		KPRIORITY mBasePriority;
	} threadInfo = { 0 };

	ULONG len = 0;
	bool loadedManually = false;
	static HMODULE module = NULL;
	static NTSTATUS(__stdcall *NtQueryInformationThread)(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength, PULONG ReturnLength);

	if (module == NULL)
	{
		module = GetModuleHandleA("ntdll.dll");
		NtQueryInformationThread = reinterpret_cast<decltype(NtQueryInformationThread)>(GetProcAddress(module, "NtQueryInformationThread"));
	}

	if (NtQueryInformationThread == NULL)
		return 0;
	NTSTATUS status = NtQueryInformationThread(mActiveThread->mHThread, (THREADINFOCLASS)0, &threadInfo, sizeof(threadInfo), nullptr);
	if (status < 0)
		return 0;

#ifdef BF_DBG_32
	addr_target tibAddr = ReadMemory<addr_target>((intptr)threadInfo.mTebBaseAddress + 0x0);
	addr_target tlsTable = ReadMemory<addr_target>((intptr)tibAddr + 0x2C);
#else
	addr_target tlsTable = ReadMemory<addr_target>((intptr)threadInfo.mTebBaseAddress + 0x58);
#endif
	return ReadMemory<addr_target>(tlsTable + tlsIndex * sizeof(addr_target));
}

bool WinDebugger::WriteInstructions(intptr address, void* src, uint64 length)
{
	SIZE_T dwBytesWritten = 0;
	bool result = ::WriteProcessMemory(mProcessInfo.hProcess, (void*)(intptr)address, src, (SIZE_T)length, &dwBytesWritten) != 0;
	result |= ::FlushInstructionCache(mProcessInfo.hProcess, (void*)(intptr)address, (SIZE_T)length) != 0;
	BF_ASSERT(result);
	BfLogDbg("WriteInstructions: %p %d\n", address, length);
	return result;
}

DbgMemoryFlags WinDebugger::GetMemoryFlags(intptr address)
{
	MEMORY_BASIC_INFORMATION memBasicInfo;
	if (::VirtualQueryEx(mProcessInfo.hProcess, (void*)address, &memBasicInfo, sizeof(MEMORY_BASIC_INFORMATION)) == 0)
	{
		//BfLogDbg("VirtualQueryEx failed with %d\n", GetLastError());
		return DbgMemoryFlags_None;
	}

	DbgMemoryFlags flags = DbgMemoryFlags_None;
	if (memBasicInfo.AllocationProtect & PAGE_READWRITE)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	if (memBasicInfo.AllocationProtect & PAGE_READONLY)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
	if (memBasicInfo.AllocationProtect & PAGE_WRITECOPY)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE)
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_READ)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
	}
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_READWRITE)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Read);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	if (memBasicInfo.AllocationProtect & PAGE_EXECUTE_WRITECOPY)
	{
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Execute);
		flags = (DbgMemoryFlags)(flags | DbgMemoryFlags_Write);
	}
	return flags;
}

#ifdef BF_DBG_32
Debugger* Beefy::CreateDebugger32(DebugManager* debugManager, DbgMiniDump* miniDump)
#else
Debugger* Beefy::CreateDebugger64(DebugManager* debugManager, DbgMiniDump* miniDump)
#endif
{
	if (miniDump != NULL)
	{
		auto debugger = new MiniDumpDebugger(debugManager, miniDump);

		return debugger;
	}
	return new WinDebugger(debugManager);
}

#ifdef BF_DBG_32

void WdAllocTest()
{
	Array<BeefyDbg32::WdStackFrame*> stackFrameList;

	for (int i = 0; true; i++)
	{
		WdStackFrame* stackFrame = new WdStackFrame();
		stackFrameList.push_back(stackFrame);
	}
}
#endif

#endif //!defined BF32 || !defined BF_DBG_64