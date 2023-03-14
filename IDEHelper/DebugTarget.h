#pragma once

#include "X86Target.h"
#include "DbgModule.h"

NS_BF_DBG_BEGIN

class HotHeap;

static const int DBG_MAX_LOOKBACK = 64*1024;

class WdStackFrame;

enum DbgOnDemandKind
{
	DbgOnDemandKind_None,
	DbgOnDemandKind_LocalOnly,
	DbgOnDemandKind_AllowRemote
};

class DebugTarget
{
public:
	WinDebugger* mDebugger;

	DbgRadixMap<DbgSymbol*> mSymbolMap;
	DbgAutoStaticEntryBucketMap mAutoStaticEntryBucketMap;
	Array<Array<DbgAutoStaticEntry> > mAutoStaticEntryBuckets;
	DbgRadixMap<DbgSubprogramMapEntry*> mSubprogramMap;
	DbgRadixMap<DbgExceptionDirectoryEntry*> mExceptionDirectoryMap;
	DbgRadixMap<DbgCompileUnitContrib*> mContribMap;

	Array<String>* mCapturedNamesPtr;
	Array<DbgType*>* mCapturedTypesPtr;

	HotHeap* mHotHeap;
	addr_target mHotHeapAddr;
	int64 mHotHeapReserveSize;
	int mLastHotHeapCleanIdx;
	String mTargetPath;
	DbgModule* mLaunchBinary;
	DbgModule* mTargetBinary;
	Array<DbgModule*> mDbgModules;
	Dictionary<int, DbgModule*> mDbgModuleMap;
	Dictionary<addr_target, DbgModule*> mFindDbgModuleCache; // Addresses are all 64k multiples
	HashSet<DbgSrcFile*> mPendingSrcFileRehup; // Waiting to remove old/invalid line info

	BumpAllocator mAlloc;
	bool mWasLocallyBuilt;
	bool mIsEmpty;
	bool mCheckedCompilerSettings;
	bool mBfObjectHasFlags;
	bool mBfObjectHasVDataExtender;
	bool mBfHasLargeStrings;
	bool mBfHasLargeCollections;
	int mBfObjectVDataIntefaceSlotCount;
	int mBfObjectSize;
	int mCurModuleId;

	Array<DwCommonFrameDescriptor*> mCommonFrameDescriptors;
	std::map<addr_target, DwFrameDescriptor> mDwFrameDescriptorMap;
	std::map<addr_target, COFFFrameDescriptorEntry> mCOFFFrameDescriptorMap;

	Dictionary<String, DbgSrcFile*> mSrcFiles;
	Dictionary<String, String> mLocalToOrigSrcMap;

protected:
	bool RollBackStackFrame_ExceptionDirectory(addr_target findPC, CPURegisters* registers, addr_target* outReturnAddressLoc, bool& alreadyRolledBackPC);
	bool RollBackStackFrame_ExceptionDirectory(CPURegisters* registers, addr_target* outReturnAddressLoc, bool& alreadyRolledBackPC);
	bool RollBackStackFrame_DwFrameDescriptor(CPURegisters* registers, addr_target* outReturnAddressLoc);
	bool RollBackStackFrame_COFFFrameDescriptor(CPURegisters* registers, addr_target* outReturnAddressLoc, bool isStackStart);
	bool RollBackStackFrame_SimpleRet(CPURegisters* registers);
	bool PropogateRegisterUpCallStack_ExceptionDirectory(addr_target findPC, CPURegisters* callerRegisters, CPURegisters* calleeRegisters, void* regPtr, bool& wasSaved);
	void RemoveTargetData();
	bool GetValueByNameInBlock_Helper(DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, String& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType);

public:
	DebugTarget(WinDebugger* debugger);
	~DebugTarget();

	void AddDbgModule(DbgModule* dbgModule);
	DbgModule* Init(const StringImpl& launchPath, const StringImpl& targetPath, intptr imageBase = 0);
	void SetupTargetBinary();
	void CheckTargetBinary(DbgModule* module);
	void CreateEmptyTarget();
	DbgModule* HotLoad(const StringImpl& fileName, int hotIdx);
	DbgModule* SetupDyn(const StringImpl& filePath, DataStream* stream, intptr imageBase);
	String UnloadDyn(addr_target imageBase);
	void CleanupHotHeap();
	void RehupSrcFiles();
	DbgSrcFile* AddSrcFile(const String& srcFilePath);
	DbgSrcFile* GetSrcFile(const String& srcFilePath);

	bool FindSymbolAt(addr_target addr, String* outSymbol, addr_target* outOffset = NULL, DbgModule** outDWARF = NULL, bool allowRemote = true);
	addr_target FindSymbolAddr(const StringImpl& name);

	addr_target GetStaticAddress(DbgVariable* dwVariable);
	DbgSubprogram* FindSubProgram(addr_target address, DbgOnDemandKind onDemandKind = DbgOnDemandKind_AllowRemote);
	void GetCompilerSettings();
	void AddAutoStaticEntry(const DbgAutoStaticEntry& entry);
	void IterateAutoStaticEntries(uint64 memoryRangeStart, uint64 memoryRangeLen, std::function<void(DbgAutoStaticEntry const&)>& func);
	void EvaluateAutoStaticVariable(DbgVariable* variable, const StringImpl& fullName);

	bool GetValueByName(DbgSubprogram* dwSubprogram, const StringImpl& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType);
	//bool GetValueByName(const StringImpl& name, CPURegisters* registers, addr_target* outAddr, DbgType** outType, bool* outIsAddr);
	bool GetValueByNameInBlock(DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, String& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType);
	void GetAutoValueNames(DbgAutoValueMapType& outAutos, WdStackFrame* stackFrame, uint64 memoryRangeStart, uint64 memoryRangeLen);
	void GetAutoValueNamesInBlock(DbgAutoValueMapType& outAutos, DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, WdStackFrame* stackFrame, uint64 memoryRangeStart, uint64 memoryRangeLen);
	bool GetAutoLocalsInBlock(Array<String>& outLocals, DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, WdStackFrame* stackFrame, DbgLineData* dwLineData);
	bool RollBackStackFrame(CPURegisters* registers, addr_target* outReturnAddressLoc, bool isStackStart);
	bool PropogateRegisterUpCallStack(CPURegisters* callerRegisters, CPURegisters* calleeRegisters, void* regPtr, bool& wasSaved);
	int GetFrameBaseRegister(DbgSubprogram* dwSubprogram);
	bool GetVariableIndexRegisterAndOffset(DbgVariable* dwVariable, int* outRegister, int* outOffset);

	const DbgMemoryFlags ReadOrigImageData(addr_target address, uint8* data, int size);
	bool DecodeInstruction(addr_target address, CPUInst* inst);
	DbgBreakKind GetDbgBreakKind(addr_target address, CPURegisters* regs, intptr_target* objAddr);
	DbgModule* FindDbgModuleForAddress(addr_target address);
	DbgModule* GetMainDbgModule();
	void ReportMemory(MemReporter* memReporter);
};

NS_BF_DBG_END