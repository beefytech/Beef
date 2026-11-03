#include "LLDBDebugger.h"

#ifdef LLDB_ENABLED

#include "DebugManager.h"
#include "DebugTarget.h"
#include "DbgModule.h"
#include "BeefySysLib/FileStream.h"
#include <limits.h>

USING_NS_BF;
USING_NS_BF_DBG;

void LLDBLog(const char* fmt ...);

// The order of X64CPURegisters::IntRegs; fs_base goes in the slot reserved for the thread pointer
static const char* sIntRegNames[] = { "rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip", "rflags", "fs_base" };

void LLDBDebugger::HostUnsupported(const char* name)
{
	mHostUnsupportedCount++;
	LLDBLog("HostUnsupported: %s\n", name);
	if (mHostUnsupportedNames.Add(name))
		OutputMessage(StrFormat("Linux debugger: %s is not supported yet\n", name));
}

void LLDBDebugger::HostSyncModules()
{
	if ((!mLLDBTarget.IsValid()) || (!mLLDBProcess.IsValid()))
		return;
	if (mDebugTarget == NULL)
		mDebugTarget = new DebugTarget(this);

	char exePath[PATH_MAX] = { 0 };
	mLLDBTarget.GetExecutable().GetPath(exePath, sizeof(exePath));

	HashSet<String> seenPaths;
	for (uint32 moduleIdx = 0; moduleIdx < mLLDBTarget.GetNumModules(); moduleIdx++)
	{
		lldb::SBModule module = mLLDBTarget.GetModuleAtIndex(moduleIdx);
		if (HotGetModuleVersion(module) > 0)
			continue;
		char modulePath[PATH_MAX] = { 0 };
		module.GetFileSpec().GetPath(modulePath, sizeof(modulePath));
		lldb::addr_t loadBase = module.GetObjectFileHeaderAddress().GetLoadAddress(mLLDBTarget);
		if ((modulePath[0] == 0) || (loadBase == LLDB_INVALID_ADDRESS))
			continue;
		String path = modulePath;
		if (!FileExists(path))
			continue;
		seenPaths.Add(path);

		addr_target prevBase = 0;
		if (mHostModuleBases.TryGetValue(path, &prevBase))
		{
			if (prevBase == (addr_target)loadBase)
				continue;
			mDebugTarget->UnloadDyn(prevBase);
			mHostModuleBases.Remove(path);
		}

		DbgModule* dbgModule = NULL;
		if ((strcmp(modulePath, exePath) == 0) && (mDebugTarget->mLaunchBinary == NULL))
		{
			dbgModule = mDebugTarget->Init(path, mTargetPath, (intptr)loadBase);
			// SetupDyn gives the other modules theirs
			if ((dbgModule != NULL) && (dbgModule->mOrigImageData == NULL))
				dbgModule->mOrigImageData = new DbgModuleMemoryCache(dbgModule->mImageBase, dbgModule->mImageSize);
		}
		else
		{
			FileStream fileStream;
			if (fileStream.Open(path, "rb"))
				dbgModule = mDebugTarget->SetupDyn(path, &fileStream, (intptr)loadBase);
		}
		LLDBLog("HostSyncModules: %s at 0x%llx %s\n", modulePath, (unsigned long long)loadBase, (dbgModule != NULL) ? "loaded" : "failed");
		if (dbgModule != NULL)
			mHostModuleBases[path] = (addr_target)loadBase;
	}

	Array<String> removedPaths;
	for (auto& kv : mHostModuleBases)
	{
		if (!seenPaths.Contains(kv.mKey))
			removedPaths.Add(kv.mKey);
	}
	for (auto& path : removedPaths)
	{
		mDebugTarget->UnloadDyn(mHostModuleBases[path]);
		mHostModuleBases.Remove(path);
	}
}

void LLDBDebugger::HostClearCallStack()
{
	for (auto wdStackFrame : mCallStack)
		delete wdStackFrame;
	mCallStack.Clear();
	mHostFrameGroupStart.Clear();
}

void LLDBDebugger::HostReadFrameRegisters(lldb::SBFrame& frame, CPURegisters* registers)
{
	for (int regIdx = 0; regIdx < BF_ARRAY_COUNT(sIntRegNames); regIdx++)
	{
		lldb::SBValue value = frame.FindRegister(sIntRegNames[regIdx]);
		if (!value.IsValid())
			continue;
		lldb::SBError error;
		uint64 regValue = value.GetValueAsUnsigned(error, 0);
		if (error.Success())
			registers->mIntRegsArray[regIdx] = (int64)regValue;
	}

	for (int regIdx = 0; regIdx < CPURegisters::kNumXmmRegs; regIdx++)
	{
		lldb::SBValue value = frame.FindRegister(StrFormat("xmm%d", regIdx).c_str());
		if (!value.IsValid())
			continue;
		lldb::SBData data = value.GetData();
		lldb::SBError error;
		if (data.GetByteSize() >= sizeof(registers->mXmmRegsArray[regIdx]))
			data.ReadRawData(error, 0, &registers->mXmmRegsArray[regIdx], sizeof(registers->mXmmRegsArray[regIdx]));
	}
}

void LLDBDebugger::HostBuildCallStack()
{
	HostClearCallStack();
	if (mLLDBFrames.IsEmpty())
		return;
	HostSyncModules();

	int groupStart = 0;
	for (int frameIdx = 0; frameIdx < (int)mLLDBFrames.size(); frameIdx++)
	{
		lldb::SBFrame& frame = mLLDBFrames[frameIdx];
		if (frame.IsInlined())
			continue;
		WdStackFrame* wdStackFrame = new WdStackFrame();
		HostReadFrameRegisters(frame, &wdStackFrame->mRegisters);
		wdStackFrame->mCFA = (addr_target)frame.GetCFA();
		wdStackFrame->mIsStart = mCallStack.IsEmpty();
		mCallStack.Add(wdStackFrame);
		mHostFrameGroupStart.Add(groupStart);
		groupStart = frameIdx + 1;
	}
	if (!mCallStack.IsEmpty())
		mCallStack.back()->mIsEnd = true;
}

void LLDBDebugger::Fail(const StringImpl& error)
{
	if (mIsRunning)
		mDebugManager->mOutMessages.push_back(StrFormat("error %s", error.c_str()));
}

bool LLDBDebugger::IsMiniDumpDebugger()
{
	return false;
}

int LLDBDebugger::GetActiveThreadId()
{
	return GetActiveThread();
}

String LLDBDebugger::GetActiveThreadName()
{
	if ((IsTargetBusy()) || (!mLLDBProcess.IsValid()))
		return String();
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if ((!thread.IsValid()) || (thread.GetName() == NULL))
		return String();
	return thread.GetName();
}

void LLDBDebugger::ClearExplicitStopThread()
{
}

int LLDBDebugger::GetActiveBreakpointHitCount()
{
	if (mActiveBreakpoint == NULL)
		return 0;
	Breakpoint* headBreakpoint = (mActiveBreakpoint->mHead != NULL) ? mActiveBreakpoint->mHead : mActiveBreakpoint;
	return headBreakpoint->mHitCount;
}

String LLDBDebugger::GetActiveBreakpointCondition()
{
	if (mActiveBreakpoint == NULL)
		return String();
	Breakpoint* headBreakpoint = (mActiveBreakpoint->mHead != NULL) ? mActiveBreakpoint->mHead : mActiveBreakpoint;
	return ((LLDBBreakpoint*)headBreakpoint)->mBeefCondition;
}

int LLDBDebugger::GetTargetProcessId()
{
	return GetProcessId();
}

bool LLDBDebugger::PopulateRegisters(CPURegisters* registers)
{
	if ((IsTargetBusy()) || (!mLLDBProcess.IsValid()))
		return false;
	lldb::SBThread thread = mLLDBProcess.GetSelectedThread();
	if (!thread.IsValid())
		return false;
	lldb::SBFrame frame = thread.GetFrameAtIndex(0);
	if (!frame.IsValid())
		return false;
	HostReadFrameRegisters(frame, registers);
	return true;
}

bool LLDBDebugger::WantsHotHeap()
{
	return false;
}

bool LLDBDebugger::WriteInstructions(intptr address, void* src, uint64 length)
{
	return WriteMemory(address, src, length);
}

// FindSymbolAt treats 0 as no thunk
addr_target LLDBDebugger::DecodeThunk(addr_target address, DbgModuleMemoryCache* memoryCache)
{
	return 0;
}

bool LLDBDebugger::DecodeInstInfo(addr_target address, DbgModuleMemoryCache* memoryCache, CPURegisters* registers, DbgInstInfo* outInfo)
{
	if (!mLLDBTarget.IsValid())
		return false;
	lldb::SBAddress addr;
	addr.SetLoadAddress((lldb::addr_t)address, mLLDBTarget);
	lldb::SBInstructionList instructions = mLLDBTarget.ReadInstructions(addr, 1);
	if (instructions.GetSize() == 0)
		return false;
	lldb::SBInstruction instruction = instructions.GetInstructionAtIndex(0);
	lldb::InstructionControlFlowKind controlFlowKind = instruction.GetControlFlowKind(mLLDBTarget);
	outInfo->mSize = (int)instruction.GetByteSize();
	outInfo->mIsCall = (controlFlowKind == lldb::eInstructionControlFlowKindCall) || (controlFlowKind == lldb::eInstructionControlFlowKindFarCall);
	outInfo->mIsReturn = (controlFlowKind == lldb::eInstructionControlFlowKindReturn) || (controlFlowKind == lldb::eInstructionControlFlowKindFarReturn);
	outInfo->mTarget = 0;
	if (outInfo->mIsCall)
	{
		// Only a direct call's operand is a bare address; register and memory operands need the simulation Windows does
		const char* operands = instruction.GetOperands(mLLDBTarget);
		if ((operands != NULL) && (strncmp(operands, "0x", 2) == 0))
		{
			char* operandEnd = NULL;
			uint64 target = strtoull(operands + 2, &operandEnd, 16);
			if ((operandEnd != NULL) && (*operandEnd == 0))
				outInfo->mTarget = (addr_target)target;
		}
	}
	if (registers != NULL)
		*registers->GetPCRegisterRef() = address;
	return true;
}

// HotGetTlsBlockAddr knows only the hot base module's TLS module id, and evaluates in the stopped thread's innermost frame
bool LLDBDebugger::GetTLSBlockAddr(DbgModule* dbgModule, CPURegisters* registers, addr_target& outAddr)
{
	if ((registers == NULL) || (mHotTlsModuleId == 1) || (mLLDBFrames.IsEmpty()))
		return false;
	lldb::SBModule baseModule = HotGetBaseModule();
	if (!baseModule.IsValid())
		return false;
	char modulePath[PATH_MAX] = { 0 };
	char moduleRealPath[PATH_MAX];
	char dbgModuleRealPath[PATH_MAX];
	baseModule.GetFileSpec().GetPath(modulePath, sizeof(modulePath));
	if ((realpath(modulePath, moduleRealPath) == NULL) || (realpath(dbgModule->mFilePath.c_str(), dbgModuleRealPath) == NULL) ||
		(strcmp(moduleRealPath, dbgModuleRealPath) != 0))
		return false;

	lldb::SBFrame& frame = mLLDBFrames[0];
	if ((addr_target)frame.FindRegister("fs_base").GetValueAsUnsigned(0) != (addr_target)registers->mIntRegs.gs)
		return false;
	uint64 blockAddr = 0;
	if (!HotGetTlsBlockAddr(frame, blockAddr))
		return false;
	outAddr = (addr_target)blockAddr;
	return true;
}

addr_target LLDBDebugger::GetTLSOffset(int tlsIndex)
{
	HostUnsupported("GetTLSOffset");
	return 0;
}

int LLDBDebugger::EnableWriting(intptr address, int size)
{
	HostUnsupported("EnableWriting");
	return 0;
}

int LLDBDebugger::SetProtection(intptr address, int size, int prot)
{
	HostUnsupported("SetProtection");
	return 0;
}

DbgTypedValue LLDBDebugger::GetRegister(const StringImpl& regName, DbgLanguage language, CPURegisters* registers, Array<RegForm>* regForms)
{
	HostUnsupported("GetRegister");
	return DbgTypedValue();
}

bool LLDBDebugger::AssignToReg(int callStackIdx, DbgTypedValue reg, DbgTypedValue value, String& outError)
{
	HostUnsupported("AssignToReg");
	outError = "Not supported";
	return false;
}

bool LLDBDebugger::SetRegisters(CPURegisters* registers)
{
	HostUnsupported("SetRegisters");
	return false;
}

void LLDBDebugger::SaveAllRegisters()
{
	HostUnsupported("SaveAllRegisters");
}

void LLDBDebugger::RestoreAllRegisters()
{
	HostUnsupported("RestoreAllRegisters");
}

void LLDBDebugger::PushValue(CPURegisters* registers, int64 val)
{
	HostUnsupported("PushValue");
}

void LLDBDebugger::PushValue(CPURegisters* registers, const DbgTypedValue& typedValue)
{
	HostUnsupported("PushValue");
}

void LLDBDebugger::SetThisRegister(CPURegisters* registers, addr_target val)
{
	HostUnsupported("SetThisRegister");
}

void LLDBDebugger::AddParamValue(int paramIdx, bool hadThis, CPURegisters* registers, const DbgTypedValue& typedValue)
{
	HostUnsupported("AddParamValue");
}

bool LLDBDebugger::CheckNeedsSRetArgument(DbgType* retType)
{
	HostUnsupported("CheckNeedsSRetArgument");
	return false;
}

DbgTypedValue LLDBDebugger::ReadReturnValue(CPURegisters* registers, DbgType* type)
{
	HostUnsupported("ReadReturnValue");
	return DbgTypedValue();
}

bool LLDBDebugger::ProcessEvalString(DbgCompileUnit* dbgCompileUnit, DbgTypedValue useTypedValue, String& evalStr, String& displayString, DwFormatInfo& formatInfo, DebugVisualizerEntry* debugVis, bool limitLength)
{
	HostUnsupported("ProcessEvalString");
	return false;
}

addr_target LLDBDebugger::ReserveHotHeapMemory(addr_target address, int size)
{
	HostUnsupported("ReserveHotHeapMemory");
	return 0;
}

void LLDBDebugger::ReleaseHotHeapMemory(addr_target address)
{
	HostUnsupported("ReleaseHotHeapMemory");
}

void LLDBDebugger::ReserveHotTargetMemory(int size)
{
	HostUnsupported("ReserveHotTargetMemory");
}

addr_target LLDBDebugger::AllocHotTargetMemory(int size, bool canExecute, bool canWrite, int* outAllocSize)
{
	HostUnsupported("AllocHotTargetMemory");
	*outAllocSize = 0;
	return 0;
}

void LLDBDebugger::ReleaseHotTargetMemory(addr_target addr, int size)
{
	HostUnsupported("ReleaseHotTargetMemory");
}

bool LLDBDebugger::SetHotJump(DbgSubprogram* oldSubprogram, addr_target newTarget, int newTargetSize)
{
	HostUnsupported("SetHotJump");
	return false;
}

void LLDBDebugger::PhysSetBreakpoint(addr_target address)
{
	HostUnsupported("PhysSetBreakpoint");
}

bool LLDBDebugger::DecodeInstruction(addr_target address, DbgModuleMemoryCache* memoryCache, CPUInst* inst)
{
	HostUnsupported("DecodeInstruction");
	return false;
}

DbgBreakKind LLDBDebugger::GetDbgBreakKind(addr_target address, DbgModuleMemoryCache* memoryCache, intptr_target* regs, intptr_target* outObjectPtr)
{
	HostUnsupported("GetDbgBreakKind");
	return DbgBreakKind_None;
}

#endif
