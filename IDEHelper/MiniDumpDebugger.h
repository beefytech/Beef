#pragma once

#include "WinDebugger.h"
#include "DbgMiniDump.h"

NS_BF_DBG_BEGIN

class MiniDumpMemoryRegion
{
public:
	addr_target mAddress;
	intptr_target mAddressLength;
	void* mData;
	MiniDumpMemoryRegion* mNext;
};

class MiniDumpDebugger : public WinDebugger
{
public:
	DbgMiniDump* mMiniDump;
	Array<MappedFile*> mMappedFiles;
	DbgRadixMap<MiniDumpMemoryRegion*> mMemMap;
	BumpAllocator mAlloc;

	WdThreadInfo* mExceptionThread;
	uintptr mExceptionContextRVA;

public:

	MiniDumpDebugger(DebugManager* debugManager, DbgMiniDump* miniDump);
	~MiniDumpDebugger();

	void MapMemory(addr_target addr, void* data, intptr_target size);
	MappedFile* MapModule(COFF* dbgModule, const StringImpl& fileName);

	virtual bool PopulateRegisters(CPURegisters* registers) override;
	using WinDebugger::PopulateRegisters;

	virtual bool ReadMemory(intptr address, uint64 length, void* dest, bool isLocal) override;
	virtual bool WriteMemory(intptr address, void* src, uint64 length) override;

	virtual bool IsOnDemandDebugger() override { return true; }
	virtual bool IsMiniDumpDebugger() override { return true; }

	virtual void ContinueDebugEvent() { }
	virtual Breakpoint* CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset) { return NULL; }
	virtual Breakpoint* CreateMemoryBreakpoint(intptr addr, int byteCount, const StringImpl& addrType) { return NULL; }
	virtual Breakpoint* CreateSymbolBreakpoint(const StringImpl& symbolName) { return NULL; }
	virtual Breakpoint* CreateAddressBreakpoint(intptr address) { return NULL; }
	virtual void BreakAll() override {};
	virtual bool TryRunContinue() override { return false; }
	virtual void StepInto(bool inAssembly) override { }
	virtual void StepIntoSpecific(intptr addr) override { }
	virtual void StepOver(bool inAssembly) override { }
	virtual void StepOut(bool inAssembly) override { }
	virtual void SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn) { }

	virtual Profiler* StartProfiling() { return NULL; }
	virtual Profiler* PopProfiler() { return NULL; }
};

NS_BF_DBG_END
