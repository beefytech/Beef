#include "DebugTarget.h"
#include "WinDebugger.h"
#include "DWARFInfo.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/MemStream.h"
#include "BeefySysLib/Util/BeefPerf.h"
#include "DebugManager.h"
#include "HotHeap.h"
//#include <Winternl.h>

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

DebugTarget::DebugTarget(WinDebugger* debugger)
{
	mCheckedCompilerSettings = false;
	mBfObjectHasFlags = false;
	mBfObjectHasVDataExtender = false;
	mBfHasLargeStrings = false;
	mBfHasLargeCollections = false;
	mBfObjectVDataIntefaceSlotCount = -1;
	mBfObjectSize = -1;
	mDebugger = debugger;
	mLaunchBinary = NULL;
	mTargetBinary = NULL;
	mCapturedNamesPtr = NULL;
	mCapturedTypesPtr = NULL;
	mHotHeap = NULL;
	mHotHeapAddr = 0;
	mHotHeapReserveSize = 0;
	mLastHotHeapCleanIdx = 0;
	mIsEmpty = false;
	mWasLocallyBuilt = false;
	mCurModuleId = 0;

	/*dbgType = new DbgType();
	dbgType->mName = "int";
	dbgType->mTypeCode = DbgType_i32;
	dbgType->mSize = 4;
	mPrimitiveTypes[DbgType_i32] = dbgType;*/
}

DebugTarget::~DebugTarget()
{
	for (auto dwarf : mDbgModules)
		delete dwarf;

	for (auto& dwSrcFilePair : mSrcFiles)
		delete dwSrcFilePair.mValue;

	delete mHotHeap;
	mHotHeap = NULL;
}

static bool PathEquals(const String& pathA, String& pathB)
{
	const char* ptrA = pathA.c_str();
	const char* ptrB = pathB.c_str();

	while (true)
	{
		char cA = *(ptrA++);
		char cB = *(ptrB++);

		if ((cA == 0) || (cB == 0))
		{
			return (cA == 0) && (cB == 0);
		}

		cA = toupper((uint8)cA);
		cB = toupper((uint8)cB);
		if (cA == '\\')
			cA = '/';
		if (cB == '\\')
			cB = '/';
		if (cA != cB)
			return false;
	}
	return true;
}

void DebugTarget::SetupTargetBinary()
{
	bool wantsHotHeap = (mDebugger->mHotSwapEnabled == true && mDebugger->mDbgProcessId == 0);

#ifdef BF_DBG_32
	if (wantsHotHeap)
		mHotHeap = new HotHeap();
#else
	if (wantsHotHeap)
	{
		// 64-bit hot loaded code needs to be placed close to the original EXE so 32-bit relative
		//  offsets within the hot code can still reach the old code
		addr_target checkHotReserveAddr = (addr_target)mTargetBinary->mImageBase + mTargetBinary->mImageSize;
		int mb = 1024 * 1024;
		int reserveSize = 512 * mb;

		// Round up to MB boundary + 64MB, to help keep other DLLs at their preferred base addresses
		checkHotReserveAddr = ((checkHotReserveAddr + 64 * mb) & ~(mb - 1));

		checkHotReserveAddr = (addr_target)mTargetBinary->mImageBase;

		addr_target reservedPtr = NULL;
		while ((addr_target)checkHotReserveAddr < (addr_target)mTargetBinary->mImageBase + 0x30000000)
		{
			reservedPtr = (addr_target)VirtualAllocEx(mDebugger->mProcessInfo.hProcess, (void*)(intptr)checkHotReserveAddr, reserveSize, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (reservedPtr != NULL)
			{
				mHotHeapAddr = reservedPtr;
				mHotHeapReserveSize = reserveSize;
				BfLogDbg("VirtualAllocEx %p %d. ImageBase: %p\n", mHotHeapAddr, mHotHeapReserveSize, mTargetBinary->mImageBase);
				break;
			}
			checkHotReserveAddr += 4 * mb;
		}

		if (reservedPtr == 0)
		{
			BfLogDbg("VirtualAllocEx failed. ImageBase: %p\n", mTargetBinary->mImageBase);
			mDebugger->Fail("Failed to reserve memory for hot swapping");
		}
		else
		{
			BF_ASSERT(mHotHeap == NULL);
			mHotHeap = new HotHeap(reservedPtr, reserveSize);
		}
	}
#endif
}

void DebugTarget::CheckTargetBinary(DbgModule* module)
{
	if (mTargetBinary != NULL)
		return;
	if (!PathEquals(module->mFilePath, mTargetPath))
		return;
	mTargetBinary = module;
	if (mTargetBinary != mLaunchBinary)
		SetupTargetBinary();
}

DbgModule* DebugTarget::Init(const StringImpl& launchPath, const StringImpl& targetPath, intptr imageBase)
{
	BP_ZONE("DebugTarget::Init");

	AutoDbgTime dbgTime("DebugTarget::Init Launch:" + launchPath + " Target:" + targetPath);

	mTargetPath = targetPath;
	FileStream fileStream;
	fileStream.mFP = _wfopen(UTF8Decode(launchPath).c_str(), L"rb");
	if (fileStream.mFP == NULL)
	{
		mDebugger->OutputMessage(StrFormat("Debugger failed to open binary: %s\n", launchPath.c_str()));
		return NULL;
	}

	DbgModule* dwarf = new COFF(this);
	mLaunchBinary = dwarf;
	dwarf->mDisplayName = GetFileName(launchPath);
	dwarf->mFilePath = launchPath;
	dwarf->mImageBase = (intptr)imageBase;
	if (!dwarf->ReadCOFF(&fileStream, DbgModuleKind_Module))
	{
		mDebugger->OutputMessage(StrFormat("Debugger failed to read binary: %s\n", launchPath.c_str()));
		delete dwarf;
		return NULL;
	}
	CheckTargetBinary(dwarf);

	AddDbgModule(dwarf);
	return dwarf;
}

void DebugTarget::CreateEmptyTarget()
{
	auto emptyTarget = new DbgModule(this);
	AddDbgModule(emptyTarget);
	mTargetBinary = emptyTarget;
	mLaunchBinary = emptyTarget;
}

DbgModule* DebugTarget::HotLoad(const StringImpl& fileName, int hotIdx)
{
	BP_ZONE("DebugTarget::HotLoad");

	AutoDbgTime dbgTime("DebugTarget::HotLoad " + fileName);

	FileStream fileStream;
	fileStream.mFP = _wfopen(UTF8Decode(fileName).c_str(), L"rb");
	if (fileStream.mFP == NULL)
	{
		mDebugger->OutputMessage(StrFormat("Debugger failed to open binary: %s\n", fileName.c_str()));
		return NULL;
	}

	DbgModule* dwarf = new COFF(this);
	dwarf->mHotIdx = hotIdx;
	dwarf->mDisplayName = GetFileName(fileName);
	dwarf->mFilePath = fileName;
	if (!dwarf->ReadCOFF(&fileStream, DbgModuleKind_HotObject))
	{
		mDebugger->OutputMessage(StrFormat("Debugger failed to read binary: %s\n", fileName.c_str()));
		delete dwarf;
		return NULL;
	}
	AddDbgModule(dwarf);
	return dwarf;
}

DbgModule* DebugTarget::SetupDyn(const StringImpl& filePath, DataStream* stream, intptr imageBase)
{
	BP_ZONE("DebugTarget::SetupDyn");

	//AutoDbgTime dbgTime("DebugTarget::SetupDyn " + filePath);

	DbgModule* dwarf = new COFF(this);
	dwarf->mFilePath = filePath;
	dwarf->mImageBase = (intptr)imageBase;
	if (!dwarf->ReadCOFF(stream, DbgModuleKind_Module))
	{
		//mDebugger->OutputMessage(StrFormat("Debugger failed to read binary: %s", fileName.c_str()));
		delete dwarf;
		return NULL;
	}
	AddDbgModule(dwarf);

	dwarf->mDisplayName = GetFileName(filePath);
	dwarf->mOrigImageData = new DbgModuleMemoryCache(dwarf->mImageBase, dwarf->mImageSize);
	CheckTargetBinary(dwarf);

	/*dbgModule->mOrigImageData = new uint8[dbgModule->mImageSize];
	memset(dbgModule->mOrigImageData, 0xCC, dbgModule->mImageSize);
	for (auto& section : dbgModule->mSections)
	{
		bool couldRead = mDebugger->ReadMemory((intptr)imageBase + section.mAddrStart, section.mAddrLength, dbgModule->mOrigImageData + section.mAddrStart);
		BF_ASSERT(couldRead);
	}
	dbgModule->GotImageData();*/
	return dwarf;
}

String DebugTarget::UnloadDyn(addr_target imageBase)
{
	String filePath;

	//AutoDbgTime dbgTime("DebugTarget::UnloadDyn");

	for (int i = 0; i < (int)mDbgModules.size(); i++)
	{
		DbgModule* dwarf = mDbgModules[i];
		if (dwarf->mImageBase == imageBase)
		{
			dwarf->mDeleting = true;
			dwarf->RemoveTargetData();
			RemoveTargetData();
			filePath = dwarf->mFilePath;

			if (mTargetBinary == dwarf)
			{
				mTargetBinary = NULL;
				delete mHotHeap;
				mHotHeap = NULL;

				if (mHotHeapAddr != 0)
				{
					BfLogDbg("VirtualFreeEx %p %d\n", mHotHeapAddr, mHotHeapReserveSize);
					::VirtualFreeEx(mDebugger->mProcessInfo.hProcess, (void*)(intptr)mHotHeapAddr, 0, MEM_RELEASE);
					mHotHeapAddr = 0;
				}
			}

			mFindDbgModuleCache.Clear();
			mDbgModules.RemoveAt(i);
			bool success = mDbgModuleMap.Remove(dwarf->mId);
			BF_ASSERT_REL(success);

			delete dwarf;
			return filePath;
		}
	}

	return "";
}

void DebugTarget::CleanupHotHeap()
{
	//TODO: We have more work to do before this catches all the required cases
	return;

	bool hadRemovals = false;
	for (int dwarfIdx = 0; dwarfIdx < (int)mDbgModules.size(); dwarfIdx++)
	{
		DbgModule* dbgModule = mDbgModules[dwarfIdx];
		if (dbgModule->IsObjectFile())
		{
			if (!mHotHeap->IsReferenced(dbgModule->mImageBase, dbgModule->mImageSize))
			{
				Beefy::OutputDebugStrF("Clearing hot module %p data from %@ to %@\n", dbgModule, dbgModule->mImageBase, dbgModule->mImageBase + dbgModule->mImageSize);

				BfLogDbg("Unloading hot idx: %s@%d\n", dbgModule->mFilePath.c_str(), dbgModule->mHotIdx);
				dbgModule->mDeleting = true;
				dbgModule->RemoveTargetData();
				hadRemovals = true;
			}
		}
	}

	if (hadRemovals)
	{
		RemoveTargetData();
		for (int dwarfIdx = 0; dwarfIdx < (int)mDbgModules.size(); dwarfIdx++)
		{
			DbgModule* dbgModule = mDbgModules[dwarfIdx];
			if (dbgModule->mDeleting)
			{
				mFindDbgModuleCache.Clear();
				mDbgModules.RemoveAt(dwarfIdx);
				bool success = mDbgModuleMap.Remove(dbgModule->mId);
				BF_ASSERT_REL(success);
				delete dbgModule;
				dwarfIdx--;
			}
		}
	}
}

void DebugTarget::RehupSrcFiles()
{
	for (auto srcFile : mPendingSrcFileRehup)
		srcFile->RehupLineData();
	mPendingSrcFileRehup.Clear();
}

DbgSrcFile* DebugTarget::AddSrcFile(const String& srcFilePath)
{
	String filePath = FixPathAndCase(srcFilePath);
	DbgSrcFile* dwSrcFile = NULL;
	DbgSrcFile** dwSrcFilePtr = NULL;
	if (mSrcFiles.TryAdd(filePath, NULL, &dwSrcFilePtr))
	{
		dwSrcFile = new DbgSrcFile();
		dwSrcFile->mFilePath = FixPath(srcFilePath);
		*dwSrcFilePtr = dwSrcFile;
	}
	else
	{
		dwSrcFile = *dwSrcFilePtr;
	}
	return dwSrcFile;
}

DbgSrcFile* DebugTarget::GetSrcFile(const String& srcFilePath)
{
	String filePath = FixPathAndCase(srcFilePath);

	DbgSrcFile* srcFile = NULL;
	if (!mSrcFiles.TryGetValue(filePath, &srcFile))
	{
		String* origSrcPath;
		if (mLocalToOrigSrcMap.TryGetValue(filePath, &origSrcPath))
		{
			mSrcFiles.TryGetValue(*origSrcPath, &srcFile);
		}
	}

	return srcFile;
}

bool DebugTarget::FindSymbolAt(addr_target addr, String* outSymbol, addr_target* outOffset, DbgModule** outDWARF, bool allowRemote)
{
	//TODO: First search for symbol, then determine if the addr is within the defining DbgModule

	DbgModule* insideDWARF = NULL;

	auto dbgModule = FindDbgModuleForAddress(addr);
	if (dbgModule != NULL)
		dbgModule->ParseSymbolData();

	auto dwSymbol = mSymbolMap.Get(addr, 16*1024*1024);
	if (dwSymbol != NULL)
	{
		dbgModule = dwSymbol->mDbgModule;
	}

	if (dbgModule == NULL)
		return false;

	if (outDWARF != NULL)
		*outDWARF = dbgModule;

	bool isExecutable = false;
	bool isInsideSomeSegment = false;
	for (int i = 0; i < (int)dbgModule->mSections.size(); i++)
	{
		auto section = &dbgModule->mSections[i];

		if ((addr >= section->mAddrStart + dbgModule->mImageBase) && (addr < section->mAddrStart + dbgModule->mImageBase + section->mAddrLength))
		{
			if (dbgModule->HasPendingDebugInfo())
			{
				if (dbgModule->WantsAutoLoadDebugInfo())
				{
					DbgPendingDebugInfoLoad* dbgPendingDebugInfoLoad = NULL;
					mDebugger->mPendingDebugInfoLoad.TryAdd(dbgModule, NULL, &dbgPendingDebugInfoLoad);
					dbgPendingDebugInfoLoad->mModule = dbgModule;
					dbgPendingDebugInfoLoad->mAllowRemote |= allowRemote;
				}
			}

			isInsideSomeSegment = true;
			if (section->mIsExecutable)
				isExecutable = true;
		}
	}

	if (!isInsideSomeSegment)
		return false;

	if (dwSymbol == NULL)
		return false;

	if (isExecutable)
	{
		addr_target thunkAddr = mDebugger->mCPU->DecodeThunk(addr, dwSymbol->mDbgModule->mOrigImageData);
		if (thunkAddr != 0)
		{
			if (FindSymbolAt(thunkAddr, outSymbol, outOffset, outDWARF))
				return true;
		}
	}

	if (outSymbol != NULL)
	{
		*outSymbol = dwSymbol->mName;
	}
	if (outOffset != NULL)
		*outOffset = addr - dwSymbol->mAddress;
	else if (addr != dwSymbol->mAddress)
		return false;
	return true;
}

addr_target DebugTarget::FindSymbolAddr(const StringImpl& name)
{
	//TODO: Search through all modules?

	auto dbgModule = GetMainDbgModule();
	if (dbgModule != NULL)
	{
		auto entry = dbgModule->mSymbolNameMap.Find(name.c_str());
		if (entry!= NULL)
		{
			DbgSymbol* dwSymbol = entry->mValue;
			return dwSymbol->mAddress;
		}
	}
	return (addr_target)-1;
}

bool DebugTarget::GetValueByName(DbgSubprogram* subProgram, const StringImpl& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType)
{
	BP_ZONE("DebugTarget::GetValueByName");

	//BF_ASSERT(*outAddrType == DbgAddrType_None);

	String checkName = name;

	if (subProgram != NULL)
	{
		subProgram->PopulateSubprogram();
		if (GetValueByNameInBlock(subProgram, &subProgram->mBlock, checkName, stackFrame, outAddr, outType, outAddrType))
			return true;
	}

	for (int dwarfIdx = mDbgModules.size() - 1; dwarfIdx >= 0; dwarfIdx--)
	{
		DbgModule* dwarf = mDbgModules[dwarfIdx];
		for (auto compileUnit : dwarf->mCompileUnits)
		{
			if (GetValueByNameInBlock(NULL, compileUnit->mGlobalBlock, checkName, stackFrame, outAddr, outType, outAddrType))
				return true;
		}
	}

	return false;
}

static const uint64 kAutoStaticBucketPageSize = 1024ull;

void DebugTarget::AddAutoStaticEntry(const DbgAutoStaticEntry& entry)
{
	uint64 remainingSize = entry.mAddrLen;
	uint64 offset = entry.mAddrStart % kAutoStaticBucketPageSize;
	uint64 curPage = entry.mAddrStart / kAutoStaticBucketPageSize;
	while (remainingSize > 0)
	{
		int bucketIndex = -1;

		/*auto map_iter = mAutoStaticEntryBucketMap.find((addr_target)curPage);
		if (map_iter != mAutoStaticEntryBucketMap.end())*/

		int* bucketIndexPtr = NULL;
		if (!mAutoStaticEntryBucketMap.TryAddRaw((addr_target)curPage, NULL, &bucketIndexPtr))
		{
			bucketIndex = *bucketIndexPtr;
		}
		else
		{
			bucketIndex = (int)mAutoStaticEntryBuckets.size();
			*bucketIndexPtr = bucketIndex;
			mAutoStaticEntryBuckets.push_back(Array<DbgAutoStaticEntry>());
			//mAutoStaticEntryBucketMap[(addr_target)curPage] = bucketIndex;
		}

		mAutoStaticEntryBuckets[bucketIndex].push_back(entry);

		uint64 adjSize = BF_MIN(kAutoStaticBucketPageSize - offset, remainingSize);
		remainingSize -= adjSize;
		offset = 0;
		++curPage;
	}
}
void DebugTarget::IterateAutoStaticEntries(uint64 memoryRangeStart, uint64 memoryRangeLen, std::function<void(DbgAutoStaticEntry const&)>& func)
{
	uint64 remainingSize = memoryRangeLen;
	uint64 offset = memoryRangeStart % kAutoStaticBucketPageSize;
	uint64 curPage = memoryRangeStart / kAutoStaticBucketPageSize;
	HashSet<DbgVariable*> foundVariables;
	while (remainingSize > 0)
	{
		//int bucketIndex = -1;
		/*auto map_iter = mAutoStaticEntryBucketMap.find((addr_target)curPage);
		if (map_iter != mAutoStaticEntryBucketMap.end())*/

		int* bucketIndexPtr = NULL;
		if (mAutoStaticEntryBucketMap.TryGetValue((addr_target)curPage, &bucketIndexPtr))
		{
			auto const & bucket = mAutoStaticEntryBuckets[*bucketIndexPtr];
			for (auto const & entry : bucket)
			{
				uint64 rangeMin = BF_MAX((uint64)entry.mAddrStart, memoryRangeStart);
				uint64 rangeMax = BF_MIN((uint64)entry.mAddrStart + entry.mAddrLen, memoryRangeStart + memoryRangeLen);

				if (rangeMin > rangeMax)
					continue;

				if (foundVariables.Contains(entry.mVariable))
					continue;

				func(entry);

				foundVariables.Add(entry.mVariable);
			}
		}

		uint64 adjSize = std::min(kAutoStaticBucketPageSize - offset, remainingSize);
		remainingSize -= adjSize;
		offset = 0;
		++curPage;
	}
}

void DebugTarget::EvaluateAutoStaticVariable(DbgVariable* variable, const StringImpl& fullName)
{
	BF_ASSERT(variable->mIsStatic && !variable->mIsConst); // why are you calling this if you haven't checked these yet
	BF_ASSERT(!variable->mInAutoStaticMap);

	if ((variable->mLocationData != NULL) && (variable->mLocationLen > 0))
	{
		DbgAddrType addrType = DbgAddrType_Local;
		intptr variableAddr = variable->mStaticCachedAddr;
		if (variableAddr == 0)
		{
			addrType = DbgAddrType_Target;
			variableAddr = variable->mCompileUnit->mDbgModule->EvaluateLocation(NULL, variable->mLocationData, variable->mLocationLen, NULL/*registers*/, &addrType);
		}

		//BF_ASSERT((addrType == DbgAddrType_Value) || (variableAddr > 0));
		if ((addrType == DbgAddrType_Local) && (variableAddr != 0))
		{
			DbgAutoStaticEntry entry;

			entry.mFullName = fullName;
			entry.mVariable = variable;
			entry.mAddrStart = variableAddr;
			entry.mAddrLen = variable->mType->mSize;

			AddAutoStaticEntry(entry);

			variable->mInAutoStaticMap = true;
		}
	}
}

void DebugTarget::GetAutoValueNames(DbgAutoValueMapType& outAutos, WdStackFrame* stackFrame, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	BP_ZONE("DebugTarget::GetAutoValueNames");

	for (auto dbgModule : mDbgModules)
	{
		// one-time iteration of mTypeMap
		if (mAutoStaticEntryBuckets.empty())
		{
			if (dbgModule->mTypeMap.IsEmpty())
				continue;

			for (auto typeEntry : dbgModule->mTypeMap)
			{
				for (auto variable : typeEntry.mValue->mMemberList)
				{
					if (variable->mIsConst)
						continue;

					if (variable->mIsStatic)
					{
						String fullName = variable->mName;
						for (DbgType* scope = typeEntry.mValue; scope && scope->mTypeName; scope = scope->mParent)
							fullName = String(scope->mTypeName) + "." + fullName;

						EvaluateAutoStaticVariable(variable, fullName);
					}
				}
			}
		}

		for (int subProgramIdx = (int)dbgModule->mSubprograms.size() - 1; subProgramIdx >= 0; subProgramIdx--)
		{
			auto subProgram = dbgModule->mSubprograms[subProgramIdx];
			GetAutoValueNamesInBlock(outAutos, subProgram, &subProgram->mBlock, stackFrame, memoryRangeStart, memoryRangeLen);
		}

		for (auto compileUnit : dbgModule->mCompileUnits)
		{
			GetAutoValueNamesInBlock(outAutos, NULL, compileUnit->mGlobalBlock, stackFrame, memoryRangeStart, memoryRangeLen);
		}
	}

	std::function<void(const DbgAutoStaticEntry&)> iterFunc = [this, &outAutos](const DbgAutoStaticEntry& entry)
	{
		//outAutos.insert(DbgAutoValueMapType::value_type(entry.mFullName, DbgAutoValueMapType::value_type::second_type(entry.mAddrStart, entry.mAddrLen)));
		outAutos.TryAdd(entry.mFullName, std::make_pair(entry.mAddrStart, entry.mAddrLen));
	};
	IterateAutoStaticEntries(memoryRangeStart, memoryRangeLen, iterFunc);
}

void DebugTarget::GetAutoValueNamesInBlock(DbgAutoValueMapType& outAutos, DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, WdStackFrame* stackFrame, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	addr_target pc = stackFrame->GetSourcePC();
	if (dwSubprogram != NULL)
	{
		auto dwarf = dwSubprogram->mCompileUnit->mDbgModule;
		if (dwBlock->mLowPC == -1)
		{
			//Debug ranges
			addr_target* rangeData = (addr_target*)(dwarf->mDebugRangesData + dwBlock->mHighPC);
			while (true)
			{
				addr_target lowPC = *(rangeData++);
				if (lowPC == 0)
					return;
				addr_target highPC = *(rangeData++);
				// Matches?
				if ((pc >= lowPC) && (pc < highPC))
					break;
			}
		}
		else if ((pc < dwBlock->mLowPC) || (pc >= dwBlock->mHighPC))
			return;
	}

	for (auto subBlocks : dwBlock->mSubBlocks)
	{
		GetAutoValueNamesInBlock(outAutos, dwSubprogram, subBlocks, stackFrame, memoryRangeStart, memoryRangeLen);
	}

	auto evalFunc = [this, &outAutos, stackFrame, dwSubprogram, memoryRangeStart, memoryRangeLen, dwBlock](Beefy::SLIList<DbgVariable*>& varList)
	{
		for (auto variable : varList)
		{
			if (variable->mIsConst)
				continue;

			if (variable->mRangeStart != 0)
			{
				auto curPC = stackFrame->mRegisters.GetPC();
				if ((curPC < variable->mRangeStart) || (curPC >= variable->mRangeStart + variable->mRangeLen))
					continue;
			}

			if (variable->mIsStatic)
			{
				if (!variable->mInAutoStaticMap && !dwBlock->mAutoStaticVariablesProcessed)
					EvaluateAutoStaticVariable(variable, variable->mName);

				if (variable->mInAutoStaticMap)
					continue; // we'll pick it up in IterateAutoStaticEntries if it's in range
			}

			if (variable->mName == NULL)
				continue;
			if (variable->mType == NULL)
				continue;

			uint64 variableAddr = 0;
			outAutos.TryAdd(variable->mName, std::make_pair(variableAddr, variable->mType->mSize));
		}
	};

	if (dwBlock == &dwSubprogram->mBlock)
	{
		evalFunc(dwSubprogram->mParams);
	}

	evalFunc(dwBlock->mVariables);

	dwBlock->mAutoStaticVariablesProcessed = true;
}

bool DebugTarget::GetAutoLocalsInBlock(Array<String>& outLocals, DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, WdStackFrame* stackFrame, DbgLineData* dwLineData)
{
	std::function<void(const StringImpl& localName)> _AddLocal = [&](const StringImpl& localName)
	{
		if (!outLocals.Contains(localName))
			outLocals.Add(localName);
		else
			_AddLocal(String("@") + localName);
	};

	addr_target pc = stackFrame->GetSourcePC();
	if (dwSubprogram != NULL)
	{
		auto dwarf = dwSubprogram->mCompileUnit->mDbgModule;
		if (dwBlock->mLowPC == -1)
		{
			//Debug ranges
			addr_target* rangeData = (addr_target*)(dwarf->mDebugRangesData + dwBlock->mHighPC);
			while (true)
			{
				addr_target lowPC = *(rangeData++);
				if (lowPC == 0)
					return false;
				addr_target highPC = *(rangeData++);
				// Matches?
				if ((pc >= lowPC) && (pc < highPC))
					break;
			}
		}
		else if ((pc < dwBlock->mLowPC) || (pc >= dwBlock->mHighPC))
			return false;
	}

	for (auto subBlock : dwBlock->mSubBlocks)
	{
		GetAutoLocalsInBlock(outLocals, dwSubprogram, subBlock, stackFrame, dwLineData);
	}

	if (dwBlock == &dwSubprogram->mBlock)
	{
		for (auto variable : dwSubprogram->mParams)
		{
			if ((variable->mName != NULL) &&
				(variable->mName[0] != '$') &&
                ((variable->mLocationData != NULL) || (variable->mIsConst)))
				_AddLocal(variable->mName);
		}
	}

	for (auto variable : dwBlock->mVariables)
	{
		if (variable->mRangeStart != 0)
		{
			auto curPC = stackFrame->GetSourcePC();
			if ((curPC < variable->mRangeStart) || (curPC >= variable->mRangeStart + variable->mRangeLen))
				continue;
		}

		if ((variable->mName[0] != '$'))
			_AddLocal(variable->mName);
	}

	return true;
}

DbgSubprogram* DebugTarget::FindSubProgram(addr_target pc, DbgOnDemandKind onDemandKind)
{
	BP_ZONE("WinDebugger::FindSubProgram");

	for (int pass = 0; pass < 2; pass++)
	{
		int mapBlockSize = 1 << DbgRadixMap<DbgSubprogramMapEntry*>::BLOCK_SHIFT;

		DbgSubprogram* foundSubprogram = NULL;

		// Even though the radix map is LIFO, we might find an inline parent in an earlier block
		// so we can't terminate the search until we exhaust the lookup
		addr_target bestStart = 0;
		for (int offset = 0; offset < DBG_MAX_LOOKBACK; offset += mapBlockSize)
		{
			DbgSubprogramMapEntry* subprogramMapEntry = mSubprogramMap.FindFirstLeafAt(pc - offset);
			while (subprogramMapEntry != NULL)
			{
				auto dwSubprogram = subprogramMapEntry->mEntry;
				if ((pc >= dwSubprogram->mBlock.mLowPC) && (pc < dwSubprogram->mBlock.mHighPC) && (dwSubprogram->mBlock.mLowPC > bestStart))
				{
					if ((dwSubprogram->mLineInfo != NULL) && (dwSubprogram->mLineInfo->mHasInlinees))
					{
						if (dwSubprogram->mNeedLineDataFixup)
							mDebugger->FixupLineDataForSubprogram(dwSubprogram);

						DbgSubprogram* inlinedSubprogram = NULL;
						if (dwSubprogram->FindClosestLine(pc, &inlinedSubprogram) != NULL)
						{
							return inlinedSubprogram;
						}
					}

					/*bool inGap = false;
					if (dwSubprogram->mHasLineGaps)
					{
						mDebugger->FixupLineDataForSubprogram(dwSubprogram);
						auto lineData = dwSubprogram->FindClosestLine(pc, 0, true);
						inGap == (lineData == NULL) || (lineData->mLine == -1);
					}

					if (!inGap)
					{
						foundSubprogram = dwSubprogram;
						bestStart = dwSubprogram->mBlock.mLowPC;
					}
					else
					{
					}*/

					return dwSubprogram;
					//foundSubprogram = dwSubprogram;
					//bestStart = dwSubprogram->mBlock.mLowPC;
				}
				subprogramMapEntry = subprogramMapEntry->mNext;
			}
		}
		if (foundSubprogram != NULL)
			return foundSubprogram;

		if (pass != 0)
			return NULL;

		DbgCompileUnitContrib* contrib = mContribMap.Get(pc, DBG_MAX_LOOKBACK);
		if ((contrib != NULL) && (pc >= contrib->mAddress) && (pc < contrib->mAddress + contrib->mLength))
		{
			contrib->mDbgModule->ParseCompileUnit(contrib->mCompileUnitId);
		}
		else if (onDemandKind != DbgOnDemandKind_None)
		{
			bool found = false;

			for (auto module : mDbgModules)
			{
				if ((pc >= module->mImageBase) && (pc < module->mImageBase + module->mImageSize))
				{
					if ((module->mFilePath.EndsWith("ntdll.dll", String::CompareKind_OrdinalIgnoreCase)) ||
						(module->mFilePath.EndsWith("kernel32.dll", String::CompareKind_OrdinalIgnoreCase)) ||
						(module->mFilePath.EndsWith("kernelbase.dll", String::CompareKind_OrdinalIgnoreCase)))
					{
						String symName;
						addr_target addrOfs = 0;
						if (FindSymbolAt(pc, &symName, &addrOfs))
						{
							// We don't need any symbols for the loader
							if ((symName == "LdrInitShimEngineDynamic") ||
								(symName == "DebugBreak"))
								continue;
						}
					}

					if (module->RequestDebugInfo(onDemandKind == DbgOnDemandKind_AllowRemote))
					{
						// Give another chance to ParseCompileUnit and then match again
						found = true;
						pass = -1;
					}
				}
			}

			// No contribution found
			if (!found)
				break;
		}
	}

	//BF_ASSERT(slowFoundSubprogram == foundSubprogram);

	return NULL;
}

void DebugTarget::GetCompilerSettings()
{
	if (!mCheckedCompilerSettings)
	{
		auto dbgModule = GetMainDbgModule();
		if (dbgModule == NULL)
			return;
		DbgType* bfObjectType = dbgModule->FindType("System.CompilerSettings", NULL, DbgLanguage_Beef);
		if (bfObjectType != NULL)
		{
			bfObjectType->PopulateType();
			if (bfObjectType->IsBfObjectPtr())
				bfObjectType = bfObjectType->mTypeParam;
			for (auto member : bfObjectType->mMemberList)
			{
				if (strcmp(member->mName, "cHasDebugFlags") == 0)
					mBfObjectHasFlags = member->mConstValue != 0;
				if (strcmp(member->mName, "cHasVDataExtender") == 0)
					mBfObjectHasVDataExtender = member->mConstValue != 0;
				if (strcmp(member->mName, "cHasLargeStrings") == 0)
					mBfHasLargeStrings = member->mConstValue != 0;
				if (strcmp(member->mName, "cHasLargeCollections") == 0)
					mBfHasLargeCollections = member->mConstValue != 0;
				if (strcmp(member->mName, "cVDataIntefaceSlotCount") == 0)
					mBfObjectVDataIntefaceSlotCount = (int)member->mConstValue;
			}

			auto objectRoot = bfObjectType->GetBaseType();
			BF_ASSERT(strcmp(objectRoot->mTypeName, "Object") == 0);
			mBfObjectSize = objectRoot->GetByteCount();
		}
		mCheckedCompilerSettings = true;
	}
}

void DebugTarget::AddDbgModule(DbgModule* dbgModule)
{
	dbgModule->mId = ++mCurModuleId;
	mFindDbgModuleCache.Clear();
	mDbgModules.Add(dbgModule);
	bool success = mDbgModuleMap.TryAdd(dbgModule->mId, dbgModule);
	BF_ASSERT_REL(success);
}

#if 1
bool DebugTarget::RollBackStackFrame_ExceptionDirectory(addr_target findPC, CPURegisters* registers, addr_target* outReturnAddressLoc, bool& alreadyRolledBackPC)
{
	addr_target pcAddress = registers->GetPC();

	auto exceptionDirectoryEntry = mExceptionDirectoryMap.Get(findPC, DBG_MAX_LOOKBACK);
	if ((exceptionDirectoryEntry != NULL) && (findPC >= exceptionDirectoryEntry->mAddress) &&
		(findPC < exceptionDirectoryEntry->mAddress + exceptionDirectoryEntry->mAddressLength))
	{
		auto dbgModule = exceptionDirectoryEntry->mDbgModule;
		//const uint8* data = dbgModule->mExceptionData + exceptionDirectoryEntry->mExceptionPos;

		uint32 exceptionPos = exceptionDirectoryEntry->mExceptionPos;
		if (dbgModule->IsObjectFile())
		{
			exceptionPos -= dbgModule->mImageBase - dbgModule->GetLinkedModule()->mImageBase;
		}
		while ((exceptionPos & 1) != 0)
		{
			// It's a reference to another entry -- directly reference the 'exceptionPos' from that other entry
			dbgModule->mOrigImageData->Read(dbgModule->mImageBase + (exceptionPos & ~1) + 8, (uint8*)&exceptionPos, 4);
		}

		struct EntryHeader
		{
			uint8 mVersion;
			uint8 mPrologSize;
			uint8 mNumUnwindCodes;
			uint8 mFrameRegister;
		};

		EntryHeader entryHeader;

		dbgModule->mOrigImageData->Read(dbgModule->mImageBase + exceptionPos, (uint8*)&entryHeader, sizeof(EntryHeader));

		uint8 version = entryHeader.mVersion;
		uint8 flags = (version >> 3);
		version &= 7;

		//TODO: Sometimes we get a 'version 2'.  Not sure how to parse that yet.
		//BF_ASSERT(version == 1);

		//uint8 prologSize = GET(uint8);
		//uint8 numUnwindCodes = GET(uint8);

		int dataSize = entryHeader.mNumUnwindCodes * 2;

		if (flags & 4) // UNW_FLAG_CHAININFO
		{
			if ((entryHeader.mNumUnwindCodes % 2) == 1)
				dataSize += 2; // Align
			dataSize += sizeof(uint32)*3;
		}

		uint8 dataBuf[512];
		dbgModule->mOrigImageData->Read(dbgModule->mImageBase + exceptionPos + sizeof(EntryHeader), dataBuf, dataSize);
		const uint8* data = dataBuf;

		if (flags & 1) // UNW_FLAG_EHANDLER
		{
		}
		else if (flags & 4) // UNW_FLAG_CHAININFO
		{
		}

		/*if (pcAddress < exceptionDirectoryEntry->mAddress + prologSize)
		{
		// We're in the prolog so just do a standard leaf 'ret'
		return false;
		}*/

		addr_target* regSP = registers->GetSPRegisterRef();
		int regFPOffset = 0;

		uint8 frameRegister = entryHeader.mFrameRegister;
		uint8 frameRegisterOffset = frameRegister >> 4;
		frameRegister &= 15;

		intptr_target newSP = 0;
		const uint8* paramAreaStart = data;
		const uint8* dataEnd = data + entryHeader.mNumUnwindCodes * 2;

		while (data < dataEnd)
		{
			uint8 offsetInProlog = GET(uint8);
			uint8 unwindOpCode = GET(uint8);
			uint8 opInfo = unwindOpCode >> 4;
			unwindOpCode &= 15;

			bool executeOp = pcAddress >= exceptionDirectoryEntry->mAddress - exceptionDirectoryEntry->mOrigAddressOffset + offsetInProlog;

			if ((unwindOpCode == 0) && (executeOp))
			{
				// UWOP_PUSH_NONVOL
				intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
				if (!gDebugger->ReadMemory(*regSP, sizeof(intptr_target), regRef))
					return false;
				*regSP += sizeof(intptr_target);
			}
			else if (unwindOpCode == 1)
			{
				// UWOP_ALLOC_LARGE
				int allocSize = 0;
				if (opInfo == 0)
					allocSize = (int)GET(uint16) * 8;
				else if (opInfo == 1)
					allocSize = (int)GET(int32);
				if (executeOp)
					*regSP += allocSize;
			}
			else if ((unwindOpCode == 2) && (executeOp))
			{
				// UWOP_ALLOC_SMALL
				int allocSize = (int)opInfo * 8 + 8;
				*regSP += allocSize;
			}
			else if ((unwindOpCode == 3) && (executeOp))
			{
				// UWOP_SET_FPREG
				intptr_target* regRef = registers->GetExceptionRegisterRef(frameRegister);
				*regSP = *regRef - (16 * frameRegisterOffset);
			}
			else if (unwindOpCode == 4)
			{
				// UWOP_SAVE_NONVOL
				int offset = (int)GET(uint16) * 8;
				if (executeOp)
				{
					intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
					if (!gDebugger->ReadMemory(*regSP + offset, sizeof(intptr_target), regRef))
						return false;
				}
			}
			else if (unwindOpCode == 5)
			{
				// UWOP_SAVE_NONVOL_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
					if (!gDebugger->ReadMemory(*regSP + offset, sizeof(intptr_target), regRef))
						return false;
				}
			}
			else if (unwindOpCode == 8)
			{
				// UWOP_SAVE_XMM128
				int offset = (int)GET(uint16) * 16;
				if (executeOp)
				{
					auto* regRef = registers->GetXMMRegRef(opInfo);
					if (!gDebugger->ReadMemory(*regSP + offset, sizeof(*regRef), regRef))
						return false;
				}
			}
			else if (unwindOpCode == 9)
			{
				// UWOP_SAVE_XMM128_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					auto* regRef = registers->GetXMMRegRef(opInfo);
					if (!gDebugger->ReadMemory(*regSP + offset, sizeof(*regRef), regRef))
						return false;
				}
			}
			else if (unwindOpCode == 10)
			{
				// UWOP_PUSH_MACHFRAME

				if (executeOp)
				{
					alreadyRolledBackPC = true;

					if (opInfo == 0)
					{
						addr_target regRIP;
						gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &regRIP); // RIP (correct back trace)
						*regSP += sizeof(intptr_target);

						addr_target reg;
						gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); // CS
						*regSP += sizeof(intptr_target);

						gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); // EFLAGS
						*regSP += sizeof(intptr_target);

						addr_target oldRSP;
						gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &oldRSP); // Old RSP
						*regSP += sizeof(intptr_target);

						gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); //SS
						*regSP += sizeof(intptr_target);

						addr_target* regPC = registers->GetPCRegisterRef();
						*regSP = oldRSP;
						*regPC = regRIP;
					}
					else
					{
						*regSP += 6 * sizeof(intptr_target);
					}
				}
			}

			// Note: RCX/RDX are reversed
		}

		if (flags & 4) // UNW_FLAG_CHAININFO
		{
			if (((intptr)data & 3) != 0)
			{
				// Align
				data = (const uint8*)(((intptr)data + 3) & ~3);
			}

			uint32 chainedRVAStart = GET(uint32);
			uint32 chainedRVAEnd = GET(uint32);
			uint32 chainedUnwindRVA = GET(uint32);

			return RollBackStackFrame_ExceptionDirectory(dbgModule->mImageBase + chainedRVAStart, registers, outReturnAddressLoc, alreadyRolledBackPC);
		}

		return true;
	}

	return false;
}

// We write the modified callerRegisters into the saved memory locations in the callee's stack frame --
//  this is needed for modifying variables tied to non-volatile registers on non-topmost stack frames
bool DebugTarget::PropogateRegisterUpCallStack_ExceptionDirectory(addr_target findPC, CPURegisters* callerRegisters, CPURegisters* calleeRegisters, void* regPtr, bool& wasSaved)
{
	addr_target pcAddress = calleeRegisters->GetPC();

	auto exceptionDirectoryEntry = mExceptionDirectoryMap.Get(findPC, DBG_MAX_LOOKBACK);
	if ((exceptionDirectoryEntry != NULL) && (findPC >= exceptionDirectoryEntry->mAddress) &&
		(findPC < exceptionDirectoryEntry->mAddress + exceptionDirectoryEntry->mAddressLength))
	{
		auto dbgModule = exceptionDirectoryEntry->mDbgModule;
		//const uint8* data = dbgModule->mExceptionData + exceptionDirectoryEntry->mExceptionPos;

		uint32 exceptionPos = exceptionDirectoryEntry->mExceptionPos;
		while ((exceptionPos & 1) != 0)
		{
			// It's a reference to another entry -- directly reference the 'exceptionPos' from that other entry
			dbgModule->mOrigImageData->Read(dbgModule->mImageBase + (exceptionPos & ~1) + 8, (uint8*)&exceptionPos, 4);
		}

		struct EntryHeader
		{
			uint8 mVersion;
			uint8 mPrologSize;
			uint8 mNumUnwindCodes;
			uint8 mFrameRegister;
		};

		EntryHeader entryHeader;

		dbgModule->mOrigImageData->Read(dbgModule->mImageBase + exceptionPos, (uint8*)&entryHeader, sizeof(EntryHeader));

		uint8 version = entryHeader.mVersion;
		uint8 flags = (version >> 3);
		version &= 7;

		int dataSize = entryHeader.mNumUnwindCodes * 2;

		if (flags & 4) // UNW_FLAG_CHAININFO
		{
			if ((entryHeader.mNumUnwindCodes % 2) == 1)
				dataSize += 2; // Align
			dataSize += sizeof(uint32)*3;
		}

		uint8 dataBuf[512];
		dbgModule->mOrigImageData->Read(dbgModule->mImageBase + exceptionPos + sizeof(EntryHeader), dataBuf, dataSize);
		const uint8* data = dataBuf;

		if (flags & 1) // UNW_FLAG_EHANDLER
		{
		}
		else if (flags & 4) // UNW_FLAG_CHAININFO
		{
		}

		/*if (pcAddress < exceptionDirectoryEntry->mAddress + prologSize)
		{
		// We're in the prolog so just do a standard leaf 'ret'
		return false;
		}*/

		addr_target regSP = calleeRegisters->GetSP();
		int regFPOffset = 0;

		uint8 frameRegister = entryHeader.mFrameRegister;
		uint8 frameRegisterOffset = frameRegister >> 4;
		frameRegister &= 15;

		intptr_target newSP = 0;
		const uint8* paramAreaStart = data;
		const uint8* dataEnd = data + entryHeader.mNumUnwindCodes * 2;

		while (data < dataEnd)
		{
			uint8 offsetInProlog = GET(uint8);
			uint8 unwindOpCode = GET(uint8);
			uint8 opInfo = unwindOpCode >> 4;
			unwindOpCode &= 15;

			bool executeOp = pcAddress >= exceptionDirectoryEntry->mAddress - exceptionDirectoryEntry->mOrigAddressOffset + offsetInProlog;

			if ((unwindOpCode == 0) && (executeOp))
			{
				// UWOP_PUSH_NONVOL
				intptr_target* regRef = callerRegisters->GetExceptionRegisterRef(opInfo);
				if (regRef == regPtr)
				{
					mDebugger->WriteMemory(regSP, regRef, sizeof(intptr_target));
					wasSaved = true;
				}
				regSP += sizeof(intptr_target);
			}
			else if (unwindOpCode == 1)
			{
				// UWOP_ALLOC_LARGE
				int allocSize = 0;
				if (opInfo == 0)
					allocSize = (int)GET(uint16) * 8;
				else if (opInfo == 1)
					allocSize = (int)GET(int32);
				if (executeOp)
					regSP += allocSize;
			}
			else if ((unwindOpCode == 2) && (executeOp))
			{
				// UWOP_ALLOC_SMALL
				int allocSize = (int)opInfo * 8 + 8;
				regSP += allocSize;
			}
			else if ((unwindOpCode == 3) && (executeOp))
			{
				// UWOP_SET_FPREG
				intptr_target* regRef = callerRegisters->GetExceptionRegisterRef(frameRegister);
				regSP = *regRef - (16 * frameRegisterOffset);
			}
			else if (unwindOpCode == 4)
			{
				// UWOP_SAVE_NONVOL
				int offset = (int)GET(uint16) * 8;
				if (executeOp)
				{
					intptr_target* regRef = callerRegisters->GetExceptionRegisterRef(opInfo);
					if (regRef == regPtr)
					{
						if (!gDebugger->WriteMemory(regSP + offset, regRef, sizeof(intptr_target)))
							return false;
						wasSaved = true;
					}
				}
			}
			else if (unwindOpCode == 5)
			{
				// UWOP_SAVE_NONVOL_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					intptr_target* regRef = callerRegisters->GetExceptionRegisterRef(opInfo);
					if (regRef == regPtr)
					{
						if (!gDebugger->WriteMemory(regSP + offset, regRef, sizeof(intptr_target)))
							return false;
						wasSaved = true;
					}
				}
			}
			else if (unwindOpCode == 8)
			{
				// UWOP_SAVE_XMM128
				int offset = (int)GET(uint16) * 16;
				if (executeOp)
				{
					auto* regRef = callerRegisters->GetXMMRegRef(opInfo);
					if (regRef == regPtr)
					{
						if (!gDebugger->WriteMemory(regSP + offset, regRef, sizeof(*regRef)))
							return false;
						wasSaved = true;
					}
				}
			}
			else if (unwindOpCode == 9)
			{
				// UWOP_SAVE_XMM128_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					auto* regRef = callerRegisters->GetXMMRegRef(opInfo);
					if (regRef == regPtr)
					{
						if (!gDebugger->WriteMemory(regSP + offset, regRef, sizeof(*regRef)))
							return false;
						wasSaved = true;
					}
				}
			}
			else if (unwindOpCode == 10)
			{
				// UWOP_PUSH_MACHFRAME

				if (executeOp)
				{
					if (opInfo == 0)
					{
						//addr_target regRIP;
						//gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &regRIP); // RIP (correct back trace)
						regSP += sizeof(intptr_target);

						//addr_target reg;
						//gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); // CS
						regSP += sizeof(intptr_target);

						//gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); // EFLAGS
						regSP += sizeof(intptr_target);

						//addr_target oldRSP;
						//gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &oldRSP); // Old RSP
						regSP += sizeof(intptr_target);

						//gDebugger->ReadMemory(*regSP, sizeof(intptr_target), &reg); //SS
						regSP += sizeof(intptr_target);

						addr_target* regPC = calleeRegisters->GetPCRegisterRef();
						//*regSP = oldRSP;
						//*regPC = regRIP;
					}
					else
					{
						regSP += 6 * sizeof(intptr_target);
					}
				}
			}

			// Note: RCX/RDX are reversed
		}

		if (flags & 4) // UNW_FLAG_CHAININFO
		{
			if (((intptr)data & 3) != 0)
			{
				// Align
				data = (const uint8*)(((intptr)data + 3) & ~3);
			}

			uint32 chainedRVAStart = GET(uint32);
			uint32 chainedRVAEnd = GET(uint32);
			uint32 chainedUnwindRVA = GET(uint32);

			return PropogateRegisterUpCallStack_ExceptionDirectory(dbgModule->mImageBase + chainedRVAStart, callerRegisters, calleeRegisters, regPtr, wasSaved);
		}

		return true;
	}

	return false;
}

#endif

#if 0
bool DebugTarget::RollBackStackFrame_ExceptionDirectory(addr_target findPC, CPURegisters* registers, addr_target* outReturnAddressLoc)
{
	addr_target pcAddress = registers->GetPC();

	auto exceptionDirectoryEntry = mExceptionDirectoryMap.Get(findPC, DBG_MAX_LOOKBACK);
	if ((exceptionDirectoryEntry != NULL) && (findPC >= exceptionDirectoryEntry->mAddress) &&
		(findPC < exceptionDirectoryEntry->mAddress + exceptionDirectoryEntry->mAddressLength))
	{
		auto dbgModule = exceptionDirectoryEntry->mDbgModule;
		const uint8* data = dbgModule->mExceptionData + (exceptionDirectoryEntry->mExceptionPos - dbgModule->mExceptionDataRVA);
		uint8 version = GET(uint8);
		uint8 flags = (version >> 3);
		version &= 7;

		uint8 prologSize = GET(uint8);
		uint8 numUnwindCodes = GET(uint8);

		if (exceptionDirectoryEntry->mAddress - dbgModule->mImageBase == 0x0000000000048efc)
		{
		}

		if (flags & 1) // UNW_FLAG_EHANDLER
		{
		}
		else if (flags & 4) // UNW_FLAG_CHAININFO
		{
		}

		/*if (pcAddress < exceptionDirectoryEntry->mAddress + prologSize)
		{
		// We're in the prolog so just do a standard leaf 'ret'
		return false;
		}*/

		addr_target* regSP = registers->GetSPRegisterRef();
		int regFPOffset = 0;

		uint8 frameRegister = GET(uint8);
		uint8 frameRegisterOffset = frameRegister >> 4;
		frameRegister &= 15;

		intptr_target newSP = 0;
		const uint8* paramAreaStart = data;
		const uint8* dataEnd = data + numUnwindCodes * 2;

		while (data < dataEnd)
		{
			uint8 offsetInProlog = GET(uint8);
			uint8 unwindOpCode = GET(uint8);
			uint8 opInfo = unwindOpCode >> 4;
			unwindOpCode &= 15;

			bool executeOp = pcAddress >= exceptionDirectoryEntry->mAddress - exceptionDirectoryEntry->mOrigAddressOffset + offsetInProlog;

			if ((unwindOpCode == 0) && (executeOp))
			{
				// UWOP_PUSH_NONVOL
				intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
				gDebugger->ReadMemory(*regSP, sizeof(intptr_target), regRef);
				*regSP += sizeof(intptr_target);
			}
			else if (unwindOpCode == 1)
			{
				// UWOP_ALLOC_LARGE
				int allocSize = 0;
				if (opInfo == 0)
					allocSize = (int)GET(uint16) * 8;
				else if (opInfo == 1)
					allocSize = (int)GET(int32);
				if (executeOp)
					*regSP += allocSize;
			}
			else if ((unwindOpCode == 2) && (executeOp))
			{
				// UWOP_ALLOC_SMALL
				int allocSize = (int)opInfo * 8 + 8;
				*regSP += allocSize;
			}
			else if ((unwindOpCode == 3) && (executeOp))
			{
				// UWOP_SET_FPREG
				intptr_target* regRef = registers->GetExceptionRegisterRef(frameRegister);
				*regSP = *regRef - (16 * frameRegisterOffset);
			}
			else if (unwindOpCode == 4)
			{
				// UWOP_SAVE_NONVOL
				int offset = (int)GET(uint16) * 8;
				if (executeOp)
				{
					intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
					gDebugger->ReadMemory(*regSP - offset, sizeof(intptr_target), regRef);
				}
			}
			else if (unwindOpCode == 5)
			{
				// UWOP_SAVE_NONVOL_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					intptr_target* regRef = registers->GetExceptionRegisterRef(opInfo);
					gDebugger->ReadMemory(*regSP - offset, sizeof(intptr_target), regRef);
				}
			}
			else if (unwindOpCode == 8)
			{
				// UWOP_SAVE_XMM128
				int offset = (int)GET(uint16) * 16;
				if (executeOp)
				{
					auto* regRef = registers->GetXMMRegRef(opInfo);
					gDebugger->ReadMemory(*regSP - offset, sizeof(*regRef), regRef);
				}
			}
			else if (unwindOpCode == 9)
			{
				// UWOP_SAVE_XMM128_FAR
				int offset = GET(int32);
				if (executeOp)
				{
					auto* regRef = registers->GetXMMRegRef(opInfo);
					gDebugger->ReadMemory(*regSP - offset, sizeof(*regRef), regRef);
				}
			}
			else if (unwindOpCode == 10)
			{
				// UWOP_PUSH_MACHFRAME
				BF_ASSERT(0 == "Not supported");
			}

			// Note: RCX/RDX are reversed
		}

		if (flags & 4) // UNW_FLAG_CHAININFO
		{
			if (((intptr)data & 3) != 0)
			{
				// Align
				data = (const uint8*)(((intptr)data + 3) & ~3);
			}

			uint32 chainedRVAStart = GET(uint32);
			uint32 chainedRVAEnd = GET(uint32);
			uint32 chainedUnwindRVA = GET(uint32);

			return RollBackStackFrame_ExceptionDirectory(dbgModule->mImageBase + chainedRVAStart, registers, outReturnAddressLoc);
		}

		return true;
	}

	return false;
}
#endif

bool DebugTarget::RollBackStackFrame_ExceptionDirectory(CPURegisters* registers, addr_target* outReturnAddressLoc, bool& alreadyRolledBackPC)
{
	addr_target pcAddress = (addr_target)registers->GetPC();
	return RollBackStackFrame_ExceptionDirectory(registers->GetPC(), registers, outReturnAddressLoc, alreadyRolledBackPC);
}

bool DebugTarget::RollBackStackFrame_DwFrameDescriptor(CPURegisters* registers, addr_target* outReturnAddressLoc)
{
	addr_target pcAddress = (addr_target)registers->GetPC();

	//TODO: Verify we replaced this correctly
	/*auto stackFrameItr = mFrameDescriptorMap.upper_bound(pcAddress);
	if (stackFrameItr != mFrameDescriptorMap.end())
		stackFrameItr--;
	if (stackFrameItr == mFrameDescriptorMap.end())
		return false;*/

	if (mDwFrameDescriptorMap.empty())
		return false;
	auto stackFrameItr = mDwFrameDescriptorMap.upper_bound(pcAddress);
	if (stackFrameItr == mDwFrameDescriptorMap.begin())
		return false;
	stackFrameItr--;

	auto dwFrameDescriptor = &stackFrameItr->second;
	if (pcAddress > dwFrameDescriptor->mHighPC)
		return false;

	struct RegisterRuleData
	{
	public:
		int mRegisterRule;
		int mParamOffset;
		const uint8* mParamData;

	public:
		RegisterRuleData()
		{
			mRegisterRule = DW_CFA_undefined;
			mParamOffset = 0;
			mParamData = NULL;
		}
	};

	struct State
	{
		State* mPrevState;
		RegisterRuleData mRegisterRuleDataArray[CPURegisters::kNumIntRegs];
		int mRegisterRuleDataIdx;
		addr_target mCFA = 0;
		int mCFAOffset = 0;

		State()
		{
			mPrevState = NULL;
			mCFA = 0;
			mCFAOffset = 0;
			mRegisterRuleDataIdx = -1;
		}
	};

	State initialState;
	State rootState;
	rootState.mRegisterRuleDataIdx = DwarfReg_SP;

	State* state = &rootState;

	// Set up default rule to restore stack pointer
	state->mRegisterRuleDataArray[state->mRegisterRuleDataIdx].mRegisterRule = DW_CFA_val_offset;
	state->mRegisterRuleDataArray[state->mRegisterRuleDataIdx].mParamOffset = 0;

	for (int pass = 0; pass < 2; pass++)
	{
		/*if ((pass == 0) && (dwFrameDescriptor->mCommonFrameDescriptor == NULL))
			continue;*/
		addr_target curLoc = dwFrameDescriptor->mLowPC;

		const uint8* data = (pass == 0) ? dwFrameDescriptor->mCommonFrameDescriptor->mInstData : dwFrameDescriptor->mInstData;
		const uint8* dataEnd = data + ((pass == 0) ? dwFrameDescriptor->mCommonFrameDescriptor->mInstLen : dwFrameDescriptor->mInstLen);

		if (pass == 1)
			initialState = rootState;

		while ((data < dataEnd) && (pcAddress >= curLoc))
		{
			uint8 opCode = GET(uint8);

			if ((opCode >= DW_CFA_advance_loc) && (opCode < DW_CFA_offset0))
			{
				int advance = opCode - DW_CFA_advance_loc;
				curLoc += advance * dwFrameDescriptor->mCommonFrameDescriptor->mCodeAlignmentFactor;
			}
			else if (opCode >= DW_CFA_restore)
			{
				int regNum = opCode - DW_CFA_restore;
				state->mRegisterRuleDataArray[regNum] = initialState.mRegisterRuleDataArray[regNum];
			}
			else
			{
				switch (opCode)
				{
				case DW_CFA_advance_loc1:
					curLoc += GET(uint8) * dwFrameDescriptor->mCommonFrameDescriptor->mCodeAlignmentFactor;
					break;
				case DW_CFA_advance_loc2:
					curLoc += GET(uint16) * dwFrameDescriptor->mCommonFrameDescriptor->mCodeAlignmentFactor;
					break;
				case DW_CFA_advance_loc4:
					curLoc += GET(uint32) * dwFrameDescriptor->mCommonFrameDescriptor->mCodeAlignmentFactor;
					break;
				case DW_CFA_def_cfa:
					{
						int regNum = (int)DecodeULEB128(data);
						state->mCFAOffset = (int)DecodeULEB128(data);
						BF_ASSERT(regNum < CPURegisters::kNumIntRegs);
						state->mCFA = registers->mIntRegsArray[regNum] + state->mCFAOffset;
					}
					break;
				case DW_CFA_def_cfa_offset:
					state->mCFA -= state->mCFAOffset;
					state->mCFAOffset = DecodeULEB128(data);
					state->mCFA += state->mCFAOffset;
				case DW_CFA_nop:
					break;
				case DW_CFA_offset0:
				case DW_CFA_offset1:
				case DW_CFA_offset2:
				case DW_CFA_offset3:
				case DW_CFA_offset4:
				case DW_CFA_offset5:
				case DW_CFA_offset6:
				case DW_CFA_offset7:
				case DW_CFA_offset8:
					{
						int regNum = opCode - DW_CFA_offset0;
						BF_ASSERT(regNum < CPURegisters::kNumIntRegs);
						int offset = (int)DecodeULEB128(data) * dwFrameDescriptor->mCommonFrameDescriptor->mDataAlignmentFactor;
						auto registerRuleData = &state->mRegisterRuleDataArray[regNum];
						registerRuleData->mRegisterRule = DW_CFA_offset;
						registerRuleData->mParamOffset = offset;

						//int registerSize = 4;//TODO
						//gDebugger->ReadMemory(state_mCFA + offset, registerSize, &registers->mIntRegsArray[regNum]);
					}
					break;
				case DW_CFA_def_cfa_register:
					{
						int regNum = (int)DecodeULEB128(data);
						BF_ASSERT(regNum < CPURegisters::kNumIntRegs);
						state->mCFA = registers->mIntRegsArray[regNum] + state->mCFAOffset;
					}
					break;
				case DW_CFA_def_cfa_expression:
					{
						int blockLen = (int)DecodeULEB128(data);
						DbgAddrType addrType;
						state->mCFA = dwFrameDescriptor->mCommonFrameDescriptor->mDbgModule->ExecuteOps(NULL, data, blockLen , NULL, registers, &addrType, DbgEvalLocFlag_None, &state->mCFA);
					}
					break;
				case DW_CFA_expression:
					{
						int regNum = (int)DecodeULEB128(data);
						BF_ASSERT(regNum < CPURegisters::kNumIntRegs);
						int blockLen = (int)DecodeULEB128(data);
						auto registerRuleData = &state->mRegisterRuleDataArray[regNum];
						registerRuleData->mRegisterRule = DW_CFA_expression;
						registerRuleData->mParamOffset = blockLen;
						registerRuleData->mParamData = data;
						data += blockLen;
					}
					break;
				case DW_CFA_remember_state:
					{
						auto nextState = new State();
						*nextState = *state;
						nextState->mPrevState = state;
						state = nextState;
					}
					break;
				case DW_CFA_restore_state:
					{
						auto prevState = state->mPrevState;
						BF_ASSERT(prevState != NULL);
						delete state;
						state = prevState;
					}
					break;
				case DW_CFA_restore_extended:
					{
						int regNum = (int)DecodeULEB128(data);
						state->mRegisterRuleDataArray[regNum] = initialState.mRegisterRuleDataArray[regNum];
					}
					break;
				default:
					BF_FATAL("Unknown DW_CFA");
					break;
				}
			}
		}
	}

	for (int registerNum = 0; registerNum < CPURegisters::kNumIntRegs; registerNum++)
	{
		auto registerRuleData = &state->mRegisterRuleDataArray[registerNum];
		switch (registerRuleData->mRegisterRule)
		{
		case DW_CFA_offset:
		{
			if (outReturnAddressLoc != 0)
				*outReturnAddressLoc = state->mCFA + registerRuleData->mParamOffset;
			int registerSize = sizeof(addr_target);
			gDebugger->ReadMemory(state->mCFA + registerRuleData->mParamOffset, registerSize, &registers->mIntRegsArray[registerNum]);
		}
		break;
		case DW_CFA_val_offset:
			registers->mIntRegsArray[registerNum] = state->mCFA + registerRuleData->mParamOffset;
			break;
		case DW_CFA_remember_state:
		case DW_CFA_undefined:
			break;
		case DW_CFA_expression:
			{
				DbgAddrType addrType;
				registers->mIntRegsArray[registerNum] = dwFrameDescriptor->mCommonFrameDescriptor->mDbgModule->ExecuteOps(NULL, registerRuleData->mParamData, registerRuleData->mParamOffset, NULL, registers, &addrType, DbgEvalLocFlag_None, &state->mCFA);
			}
			break;
		default:
			BF_FATAL("Unknown DW_CFA");
			break;
		}
	}

	while (state->mPrevState != NULL)
	{
		auto prev = state->mPrevState;
		delete state;
		state = prev;
	}

	return true;
}

bool DebugTarget::RollBackStackFrame_COFFFrameDescriptor(CPURegisters* registers, addr_target* outReturnAddressLoc, bool isStackStart)
{
#ifdef BF_DBG_32
	addr_target pcAddress = (addr_target)registers->GetPC();

	if (mCOFFFrameDescriptorMap.empty())
		return false;
	auto stackFrameItr = mCOFFFrameDescriptorMap.upper_bound(pcAddress);
	if (stackFrameItr == mCOFFFrameDescriptorMap.begin())
		return false;
	stackFrameItr--;

	auto frameEntry = &stackFrameItr->second;
	addr_target startAddress = stackFrameItr->first;
	if (pcAddress > startAddress + frameEntry->mFrameDescriptor->mCodeSize)
		return false;

	struct _StackEntry
	{
		COFFFrameProgram::Command mCmd;
		addr_target mValue;
	};

	//SizedArray<_StackEntry, 8> stack;

	int stackPos = 0;
	COFFFrameProgram::Command stackCmds[8];
	addr_target stackValues[8];
	addr_target temps[4];

	auto _GetValue = [&](int stackPos)
	{
		addr_target val = 0;
		switch (stackCmds[stackPos])
		{
		case COFFFrameProgram::Command_EIP:
			return (addr_target)registers->mIntRegs.eip;
		case COFFFrameProgram::Command_ESP:
			return (addr_target)registers->mIntRegs.esp;
		case COFFFrameProgram::Command_EBP:
			return (addr_target)registers->mIntRegs.ebp;
		case COFFFrameProgram::Command_EAX:
			return (addr_target)registers->mIntRegs.eax;
		case COFFFrameProgram::Command_EBX:
			return (addr_target)registers->mIntRegs.ebx;
		case COFFFrameProgram::Command_ECX:
			return (addr_target)registers->mIntRegs.ecx;
		case COFFFrameProgram::Command_EDX:
			return (addr_target)registers->mIntRegs.edx;
		case COFFFrameProgram::Command_ESI:
			return (addr_target)registers->mIntRegs.esi;
		case COFFFrameProgram::Command_EDI:
			return (addr_target)registers->mIntRegs.edi;
		case COFFFrameProgram::Command_T0:
			return (addr_target)temps[0];
		case COFFFrameProgram::Command_T1:
			return (addr_target)temps[1];
		case COFFFrameProgram::Command_T2:
			return (addr_target)temps[2];
		case COFFFrameProgram::Command_T3:
			return (addr_target)temps[3];
		case COFFFrameProgram::Command_RASearch:
			{
				addr_target raSearch = (addr_target)(registers->GetSP() + frameEntry->mFrameDescriptor->mLocalSize + frameEntry->mFrameDescriptor->mSavedRegsSize);

				// If we are directly on a "sub esp, <X>", this can occur when stepping out of a function, where the caller needs to adjust the stack after the call.
				//  There is generally not a unique frame descriptor to handle this condition, so we need to manually handle it here
				if ((isStackStart) && (pcAddress != startAddress))
				{
					uint8 ops[2];
					if (ReadOrigImageData(pcAddress, ops, 2) != DbgMemoryFlags_None)
					{
						if ((ops[0] == 0x83) && (ops[1] == 0xEC))
						{
							// SUB ESP, <UINT8>
							uint8 rhs = 0;
							ReadOrigImageData(pcAddress + 2, &rhs, 1);
							raSearch -= rhs;
						}
						else if ((ops[0] == 0x81) && (ops[1] == 0xEC))
						{
							// SUB ESP, <UINT32>
							uint32 rhs = 0;
							ReadOrigImageData(pcAddress + 2, (uint8*)&rhs, 4);
							raSearch -= rhs;
						}
					}
				}
				return raSearch;
			}
		case COFFFrameProgram::Command_Value:
			return stackValues[stackPos];
		}
		return val;
	};

	COFFFrameProgram::Command* cmdPtr = frameEntry->mProgram.mCommands;
	while (true)
	{
		COFFFrameProgram::Command cmd = *(cmdPtr++);
		if (cmd == COFFFrameProgram::Command_None)
			break;
		switch (cmd)
		{
		case COFFFrameProgram::Command_EIP:
		case COFFFrameProgram::Command_ESP:
		case COFFFrameProgram::Command_EBP:
		case COFFFrameProgram::Command_EAX:
		case COFFFrameProgram::Command_EBX:
		case COFFFrameProgram::Command_ECX:
		case COFFFrameProgram::Command_EDX:
		case COFFFrameProgram::Command_ESI:
		case COFFFrameProgram::Command_EDI:
		case COFFFrameProgram::Command_T0:
		case COFFFrameProgram::Command_T1:
		case COFFFrameProgram::Command_RASearch:
			if (stackPos >= 8)
				return false;
			stackCmds[stackPos++] = cmd;
			break;
		case COFFFrameProgram::Command_Add:
			{
				if (stackPos < 2)
					return false;
				addr_target lhs = _GetValue(stackPos - 2);
				addr_target rhs = _GetValue(stackPos - 1);
				stackPos -= 2;
				stackValues[stackPos] = lhs + rhs;
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		case COFFFrameProgram::Command_Subtract:
			{
				if (stackPos < 2)
					return false;
				addr_target lhs = _GetValue(stackPos - 2);
				addr_target rhs = _GetValue(stackPos - 1);
				stackPos -= 2;
				stackValues[stackPos] = lhs - rhs;
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		case COFFFrameProgram::Command_Align:
			{
				if (stackPos < 2)
					return false;
				addr_target lhs = _GetValue(stackPos - 2);
				addr_target rhs = _GetValue(stackPos - 1);
				stackPos -= 2;
				stackValues[stackPos] = BF_ALIGN(lhs, rhs);
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		case COFFFrameProgram::Command_Set:
			{
				if (stackPos < 2)
					return false;
				addr_target rhs = _GetValue(stackPos - 1);
				switch (stackCmds[stackPos - 2])
				{
				case COFFFrameProgram::Command_EIP:
					registers->mIntRegs.eip = rhs;
					break;
				case COFFFrameProgram::Command_ESP:
					registers->mIntRegs.esp = rhs;
					break;
				case COFFFrameProgram::Command_EBP:
					registers->mIntRegs.ebp = rhs;
					break;
				case COFFFrameProgram::Command_EAX:
					registers->mIntRegs.eax = rhs;
					break;
				case COFFFrameProgram::Command_EBX:
					registers->mIntRegs.ebx = rhs;
					break;
				case COFFFrameProgram::Command_ECX:
					registers->mIntRegs.ecx = rhs;
					break;
				case COFFFrameProgram::Command_EDX:
					registers->mIntRegs.edx = rhs;
					break;
				case COFFFrameProgram::Command_ESI:
					registers->mIntRegs.esi = rhs;
					break;
				case COFFFrameProgram::Command_EDI:
					registers->mIntRegs.edi = rhs;
					break;
				case COFFFrameProgram::Command_T0:
					temps[0] = rhs;
					break;
				case COFFFrameProgram::Command_T1:
					temps[1] = rhs;
					break;
				case COFFFrameProgram::Command_T2:
					temps[2] = rhs;
					break;
				case COFFFrameProgram::Command_T3:
					temps[3] = rhs;
					break;
				}
				stackPos -= 2;
			}
			break;
		case COFFFrameProgram::Command_Deref:
			{
				if (stackPos < 1)
					return false;
				addr_target addr = _GetValue(stackPos - 1);
				stackPos--;
				stackValues[stackPos] = mDebugger->ReadMemory<addr_target>(addr);
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		case COFFFrameProgram::Command_Value:
			{
				if (stackPos >= 8)
					return false;
				addr_target val = *(addr_target*)cmdPtr;
				cmdPtr += 4;
				stackValues[stackPos] = val;
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		case COFFFrameProgram::Command_Value8:
			{
				if (stackPos >= 8)
					return false;
				addr_target val = (uint8)*(cmdPtr++);
				stackValues[stackPos] = val;
				stackCmds[stackPos++] = COFFFrameProgram::Command_Value;
			}
			break;
		}
	}
	return true;
#endif
	return false;
}

bool DebugTarget::RollBackStackFrame_SimpleRet(CPURegisters* registers)
{
	int regSize = sizeof(addr_target);
	addr_target* regPC = registers->GetPCRegisterRef();
	addr_target* regSP = registers->GetSPRegisterRef();

	addr_target newPC = 0;
	gDebugger->ReadMemory(*regSP, sizeof(addr_target), &newPC);
	*regSP += regSize;
	*regPC = newPC;
	return true;
}

bool DebugTarget::RollBackStackFrame(CPURegisters* registers, addr_target* outReturnAddressLoc, bool isStackStart)
{
	if (outReturnAddressLoc != NULL)
		*outReturnAddressLoc = 0;

	CPUInst inst;
	if (DecodeInstruction(registers->GetPC(), &inst))
	{
		if (inst.IsReturn())
		{
			// If we are literally just a return then often the frame descriptor is wrong,
			//  but we know this is ACTUALLY just a simple rollback
			return RollBackStackFrame_SimpleRet(registers);
		}
	}

#ifdef BF_DBG_32
	if (RollBackStackFrame_DwFrameDescriptor(registers, outReturnAddressLoc))
		return true;
	if (RollBackStackFrame_COFFFrameDescriptor(registers, outReturnAddressLoc, isStackStart))
		return true;
	auto pc = registers->GetPC();
	DbgSubprogram* dbgSubprogram = FindSubProgram(pc);
	if (dbgSubprogram != NULL)
	{
		if (pc == dbgSubprogram->mBlock.mLowPC)
			return RollBackStackFrame_SimpleRet(registers);

		auto dbgModule = dbgSubprogram->mCompileUnit->mDbgModule;
		if ((dbgModule != NULL) && (!dbgModule->mParsedFrameDescriptors))
		{
			dbgModule->ParseFrameDescriptors();
			if (RollBackStackFrame_COFFFrameDescriptor(registers, outReturnAddressLoc, isStackStart))
				return true;
		}
	}
	else
	{
		return RollBackStackFrame_SimpleRet(registers);
	}
#endif

	// Fall through after this, we need to process a 'return'
	bool alreadyRolledBackPC = false;
	bool success = RollBackStackFrame_ExceptionDirectory(registers, outReturnAddressLoc, alreadyRolledBackPC);

	///TODO: Why did we break when there was a minidump? This breaks default-rollback of just a 'ret'
// 	if (!success)
// 	{
// 		if (mDebugger->IsMiniDumpDebugger())
// 		{
// 			return false;
// 		}
// 	}
	if (alreadyRolledBackPC)
		return true;

#ifdef BF_DBG_32
	// Try rollback assuming a frame pointer
	addr_target newPC = 0;
	addr_target stackFrame = registers->GetBP();
	int regSize = sizeof(addr_target);

	addr_target* regPC = registers->GetPCRegisterRef();
	addr_target* regSP = registers->GetSPRegisterRef();
	addr_target* regBP = registers->GetBPRegisterRef();

	// Using stack frame
	*regSP = *regBP;
	//*regBP = gDebugger->ReadMemory<addr_target>(*regSP);
	gDebugger->ReadMemory(*regSP, sizeof(addr_target), regBP);
	*regSP += regSize;
	//newPC = gDebugger->ReadMemory<addr_target>(*regSP);
	gDebugger->ReadMemory(*regSP, sizeof(addr_target), &newPC);
	*regSP += regSize;

	*regPC = newPC;
#else
	// Do a 'leaf' rollback - assume no frame pointer
	addr_target newPC = 0;
	addr_target stackFrame = registers->GetBP();
	int regSize = sizeof(addr_target);

	addr_target* regPC = registers->GetPCRegisterRef();
	addr_target* regSP = registers->GetSPRegisterRef();

	if (!gDebugger->ReadMemory(*regSP, sizeof(addr_target), &newPC))
		return false;
	*regSP += regSize;
	*regPC = newPC;
#endif

	/*MEMORY_BASIC_INFORMATION memInfo;
	if (VirtualQueryEx(mProcessInfo.hProcess, (void*)(intptr)(*regPC), &memInfo, sizeof(memInfo)) == 0)
		return false;

	if ((memInfo.Protect != PAGE_EXECUTE) && (memInfo.Protect != PAGE_EXECUTE_READ) &&
		(memInfo.Protect != PAGE_EXECUTE_READWRITE) && (memInfo.Protect != PAGE_EXECUTE_WRITECOPY))
		return false;*/

	return true;
}

bool DebugTarget::PropogateRegisterUpCallStack(CPURegisters* callerRegisters, CPURegisters* calleeRegisters, void* regPtr, bool& wasSaved)
{
	//TODO: Implement for X86
	return PropogateRegisterUpCallStack_ExceptionDirectory(calleeRegisters->GetPC(), callerRegisters, calleeRegisters, regPtr, wasSaved);
}

int DebugTarget::GetFrameBaseRegister(DbgSubprogram* dwSubprogram)
{
	if (dwSubprogram->mFrameBaseLen == 0)
		return -1;

	auto locData = dwSubprogram->mFrameBaseData;
	if (locData != NULL)
	{
		uint8 opCode = GET_FROM(locData, uint8);
		if ((opCode >= DW_OP_reg0) && (opCode <= DW_OP_reg8))
		{
			return opCode - DW_OP_reg0;
		}
	}

#ifdef BF_DBG_32
	if (dwSubprogram->mLocalBaseReg == DbgSubprogram::LocalBaseRegKind_VFRAME)
		return X86Reg_EBP;
	else if (dwSubprogram->mLocalBaseReg == DbgSubprogram::LocalBaseRegKind_EBX)
		return X86Reg_EBX;
	return X86Reg_EBP;
#else
	if (dwSubprogram->mLocalBaseReg == DbgSubprogram::LocalBaseRegKind_RSP)
		return X64Reg_RSP;
	else if (dwSubprogram->mLocalBaseReg == DbgSubprogram::LocalBaseRegKind_R13)
		return X64Reg_R13;
	return X64Reg_RBP;
#endif

	return -1;
}

bool DebugTarget::GetVariableIndexRegisterAndOffset(DbgVariable* dwVariable, int* outRegister, int* outOffset)
{
	if (dwVariable->mLocationLen == 0)
		return false;

	auto locData = dwVariable->mLocationData;
	uint8 opCode = GET_FROM(locData, uint8);
	if (opCode == DW_OP_fbreg)
	{
		*outRegister = -1;
		int64 offset = DecodeSLEB128(locData);
		*outOffset = offset;
		return true;
	}
	else if ((opCode >= DW_OP_breg0) && (opCode <= DW_OP_breg8))
	{
		*outRegister = opCode - DW_OP_breg0;
		*outOffset = DecodeSLEB128(locData);
		return true;
	}

	return false;
}

//int64 BfDebuggerReadMemory(int64 addr);

addr_target DebugTarget::GetStaticAddress(DbgVariable* dwVariable)
{
	DbgAddrType addrType;
	return (addr_target)dwVariable->mCompileUnit->mDbgModule->EvaluateLocation(NULL, dwVariable->mLocationData, dwVariable->mLocationLen, NULL, &addrType);
}

bool DebugTarget::GetValueByNameInBlock_Helper(DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, String& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType)
{
	int nameLen = (int)name.length();

	// Checks for previous version of a local name by stripping off the '@' each time we find a match, until we don't have any @'s left
	auto _CheckName = [&](const char* localName)
	{
		const char* namePtr = name.c_str();
		if (*namePtr != '@')
			return;
		while (*namePtr == '@')
			namePtr++;
		if (strcmp(localName, namePtr) == 0)
		{
			nameLen--;
			name.Remove(0, 1);
		}
	};

	SizedArray<DbgVariable*, 32> checkVars;
	for (auto variable : dwBlock->mVariables)
	{
		checkVars.push_back(variable);
	}

	auto _FixParam = [&](DbgVariable* variable)
	{
		if (dwSubprogram->GetLanguage() != DbgLanguage_Beef)
		{
			if ((*outAddrType == DbgAddrType_Target) /*&& (variable->mIsParam)*/ && (dwSubprogram->mCompileUnit->mDbgModule->mDbgFlavor == DbgFlavor_MS))
			{
				auto dbgType = *outType;

				int size = dbgType->GetByteCount();
				if (dbgType->IsTypedPrimitive())
				{
					// Already correct
				}
				else if ((dbgType->IsCompositeType()) && (variable->mIsParam))
				{
					int size = (*outType)->mSize;
					if ((size != 1) && (size != 2) && (size != 4) && (size != sizeof(intptr_target)))
					{
						auto actualAddr = mDebugger->ReadMemory<intptr_target>(*outAddr);
						*outAddr = actualAddr;
					}
				}
			}
		}

		if (variable->mSigNoPointer)
		{
			if (*outAddrType == DbgAddrType_Target)
				*outAddrType = DbgAddrType_TargetDeref;
		}

		if ((variable->mIsParam) && (dwSubprogram->GetLanguage() == DbgLanguage_Beef))
		{
			if ((!(*outType)->IsRef()) && (!(*outType)->IsConst()))
				*outType = dwSubprogram->mCompileUnit->mDbgModule->GetConstType(*outType);
		}
	};

	for (int varIdx = (int)checkVars.size() - 1; varIdx >= 0; varIdx--)
	{
		auto variable = checkVars[varIdx];

		if (variable->mRangeStart != 0)
		{
			auto curPC = stackFrame->GetSourcePC();
			if ((curPC < variable->mRangeStart) || (curPC >= variable->mRangeStart + variable->mRangeLen))
				continue;
		}

		if (mCapturedNamesPtr != NULL)
		{
			mCapturedNamesPtr->push_back(variable->mName);
			if (mCapturedTypesPtr != NULL)
				mCapturedTypesPtr->push_back(variable->mType);
		}
		else if ((strncmp(name.c_str(), variable->mName, nameLen) == 0))
		{
			if ((variable->mName[nameLen] == '$') && (variable->mName[nameLen + 1] == 'a')) // Alias
			{
				const char* aliasName = variable->mName + nameLen + 3;
				return GetValueByName(dwSubprogram, aliasName, stackFrame, outAddr, outType, outAddrType);
			}
			else if (variable->mName[nameLen] != 0)
				continue;

			*outType = variable->mType;

			if (((variable->mType != NULL) && (variable->mType->mTypeCode == DbgType_Const)) || (variable->mIsConst))
			{
				if ((variable->mLocationData == NULL) || (variable->mIsConst))
				{
					if (variable->mType->IsCompositeType())
					{
						*outAddr = (intptr)variable;
						*outAddrType = DbgAddrType_LocalSplat;
						return true;
					}

					*outAddrType = DbgAddrType_Local;
					if (variable->mIsConst)
						*outAddr = (uint64)&variable->mConstValue;
					else
						*outAddr = NULL; // Optimized out?
					return true;
				}
				else
				{
					// Strip off const part, for proper evaluation of value for receiver
					//TODO: Why did we do this?  This screwed up our 'abuse' of the const
					//  for 'let' statements.
					// *outType = variable->mType->mTypeParam;
				}
			}

			if (variable->mLocationData == NULL)
				return false;

			*outAddr = variable->mCompileUnit->mDbgModule->EvaluateLocation(dwSubprogram, variable->mLocationData, variable->mLocationLen, stackFrame, outAddrType);

			_FixParam(variable);

			return true;
		}
		else
			_CheckName(variable->mName);
	}

	if (dwBlock == &dwSubprogram->mBlock)
	{
		for (auto variable : dwSubprogram->mParams)
		{
			if (variable->mName == NULL)
				continue;

			if (mCapturedNamesPtr != NULL)
			{
				mCapturedNamesPtr->push_back(variable->mName);
				if (mCapturedTypesPtr != NULL)
					mCapturedTypesPtr->push_back(variable->mType);
			}
			else if (strcmp(name.c_str(), variable->mName) == 0)
			{
				*outType = variable->mType;

				if (variable->mLocationData == NULL)
				{
					if (variable->mIsConst)
					{
						if (variable->mType->IsCompositeType())
						{
							*outType = dwSubprogram->mCompileUnit->mDbgModule->GetConstType(*outType);
							*outAddr = (intptr)variable;
							*outAddrType = DbgAddrType_LocalSplat;
							return true;
						}
					}

					return false;
				}

				*outAddr = variable->mCompileUnit->mDbgModule->EvaluateLocation(dwSubprogram, variable->mLocationData, variable->mLocationLen, stackFrame, outAddrType, variable->mIsParam ? DbgEvalLocFlag_IsParam : DbgEvalLocFlag_None);

				_FixParam(variable);

				return true;
			}
			else
				_CheckName(variable->mName);
		}
	}

	return false;
}

bool DebugTarget::GetValueByNameInBlock(DbgSubprogram* dwSubprogram, DbgBlock* dwBlock, String& name, WdStackFrame* stackFrame, intptr* outAddr, DbgType** outType, DbgAddrType* outAddrType)
{
	addr_target pc = stackFrame->GetSourcePC();
	if (dwSubprogram != NULL)
	{
		auto dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		if (dwBlock->mLowPC == -1)
		{
			//Debug ranges
			addr_target* rangeData = (addr_target*)(dbgModule->mDebugRangesData + dwBlock->mHighPC);
			while (true)
			{
				addr_target lowPC = *(rangeData++);
				if (lowPC == 0)
					return false;
				addr_target highPC = *(rangeData++);
				// Matches?
				if ((pc >= lowPC) && (pc < highPC))
					break;
			}
		}
		else if ((pc < dwBlock->mLowPC) || (pc >= dwBlock->mHighPC))
			return false;
	}

	for (auto subBlock : dwBlock->mSubBlocks)
	{
		if (GetValueByNameInBlock(dwSubprogram, subBlock, name, stackFrame, outAddr, outType, outAddrType))
			return true;
	}

	if (GetValueByNameInBlock_Helper(dwSubprogram, dwBlock, name, stackFrame, outAddr, outType, outAddrType))
		return true;

	return false;
}

const DbgMemoryFlags DebugTarget::ReadOrigImageData(addr_target address, uint8* data, int size)
{
	auto dwarf = FindDbgModuleForAddress(address);
	if ((dwarf != NULL) && (dwarf->mOrigImageData != NULL))
	{
		return dwarf->mOrigImageData->Read(address, data, size);
	}
	return DbgMemoryFlags_None;
}

bool DebugTarget::DecodeInstruction(addr_target address, CPUInst* inst)
{
	auto dwarf = FindDbgModuleForAddress(address);
	if ((dwarf != NULL) && (dwarf->mOrigImageData != NULL))
	{
		return mDebugger->mCPU->Decode(address, dwarf->mOrigImageData, inst);
	}
	return false;
}

DbgBreakKind DebugTarget::GetDbgBreakKind(addr_target address, CPURegisters* registers, intptr_target* objAddr)
{
	auto dwarf = FindDbgModuleForAddress(address);
	if ((dwarf != NULL) && (dwarf->mOrigImageData != NULL))
	{
		auto result = mDebugger->mCPU->GetDbgBreakKind(address, dwarf->mOrigImageData, registers->mIntRegsArray, objAddr);
		return result;
	}
	return DbgBreakKind_None;
}

DbgModule* DebugTarget::FindDbgModuleForAddress(addr_target address)
{
	addr_target checkAddr = address & ~0xFFFF;
	DbgModule** valuePtr = NULL;
	if (mFindDbgModuleCache.TryAdd(checkAddr, NULL, &valuePtr))
	{
		for (auto dwarf : mDbgModules)
		{
			if ((address >= dwarf->mImageBase) && (address < dwarf->mImageBase + dwarf->mImageSize))
				*valuePtr = dwarf;
		}
	}

	return *valuePtr;
}

DbgModule* DebugTarget::GetMainDbgModule()
{
	return mTargetBinary;
}

#ifdef BF_DBG_32
template <typename T>
void ReportRadixMap(MemReporter* memReporter, RadixMap32<T>& radixMap)
{
	memReporter->Add(sizeof(void*) * RadixMap32<T>::ROOT_LENGTH);
	for (int rootIdx = 0; rootIdx < RadixMap32<T>::ROOT_LENGTH; rootIdx++)
	{
		auto root = radixMap.mRoot[rootIdx];
		if (root != NULL)
		{
			memReporter->Add(sizeof(RadixMap32<T>::Leaf));
		}
	}
}
#else
template <typename T>
void ReportRadixMap(MemReporter* memReporter, RadixMap64<T>& radixMap)
{
	memReporter->Add(sizeof(void*) * RadixMap64<T>::ROOT_LENGTH);
	for (int rootIdx = 0; rootIdx < RadixMap64<T>::ROOT_LENGTH; rootIdx++)
	{
		auto mid = radixMap.mRoot[rootIdx];
		if (mid != NULL)
		{
			memReporter->Add(sizeof(RadixMap64<T>::Mid));
			for (int midIdx = 0; midIdx < RadixMap64<T>::MID_LENGTH; midIdx++)
			{
				auto leaf = mid->mLeafs[midIdx];
				if (leaf != NULL)
				{
					memReporter->Add(sizeof(RadixMap64<T>::Leaf));
				}
			}
		}
	}
}
#endif

void DebugTarget::ReportMemory(MemReporter* memReporter)
{
	memReporter->BeginSection("SymbolMap");
	ReportRadixMap(memReporter, mSymbolMap);
	memReporter->EndSection();

	memReporter->BeginSection("SubprogramMap");
	ReportRadixMap(memReporter, mSubprogramMap);
	memReporter->EndSection();

	memReporter->BeginSection("ExceptionDirectoryMap");
	ReportRadixMap(memReporter, mExceptionDirectoryMap);
	memReporter->EndSection();

	memReporter->BeginSection("ContribMap");
	ReportRadixMap(memReporter, mContribMap);
	memReporter->EndSection();

	memReporter->BeginSection("CommonFrameDescriptors");
	memReporter->AddVec(mCommonFrameDescriptors);
	memReporter->EndSection();

	for (auto dbgModule : mDbgModules)
	{
		memReporter->BeginSection("DbgModules");
		dbgModule->ReportMemory(memReporter);
		memReporter->EndSection();
	}
}

template <typename T>
struct VectorRemoveCtx
{
	T* mVec;
	int mMatchStartIdx;
	bool mFinished;

	VectorRemoveCtx(T& vec)
	{
		mVec = &vec;
		mMatchStartIdx = -1;
		mFinished = false;
	}

	~VectorRemoveCtx()
	{
		BF_ASSERT(mFinished);
	}

	void RemoveCond(int& idx, bool doRemove)
	{
		if (doRemove)
		{
			if (mMatchStartIdx == -1)
				mMatchStartIdx = idx;
		}
		else
		{
			if (mMatchStartIdx != -1)
			{
				mVec->RemoveRange(mMatchStartIdx, idx - mMatchStartIdx);
				idx = mMatchStartIdx;
				mMatchStartIdx = -1;
			}
		}
	}

	void Finish()
	{
		if (mMatchStartIdx != -1)
			mVec->RemoveRange(mMatchStartIdx, (int)mVec->size() - mMatchStartIdx);
		mFinished = true;
	}
};

void DebugTarget::RemoveTargetData()
{
	BP_ZONE("DebugTarget::RemoveTargetData");

#ifdef _DEBUG
	DbgModule* problemModule = NULL;

	int checkIdx = 0;
	for (DbgSymbol* headNode : mSymbolMap)
	{
		auto node = headNode;
		while (node != NULL)
		{
			checkIdx++;

			auto next = node->mNext;
			if (node->mDbgModule->mDeleting)
			{
				problemModule = node->mDbgModule;
				//OutputDebugStrF("Should have been removed by DbgModule::RemoveTargetData %@ %s\n", node->mAddress, node->mName);
				BF_DBG_FATAL("Should have been removed by DbgModule::RemoveTargetData");
				mSymbolMap.Remove(node);
			}
			node = next;
		}
	}

	if (problemModule != NULL)
	{
		problemModule->RemoveTargetData();
	}

	for (DbgSubprogramMapEntry* headNode : mSubprogramMap)
	{
		auto node = headNode;
		while (node != NULL)
		{
			auto next = node->mNext;
			if (node->mEntry->mCompileUnit->mDbgModule->mDeleting)
			{
				//OutputDebugStrF("Should have been removed by DbgModule::RemoveTargetData %s\n", node->mEntry->mName);
				BF_DBG_FATAL("Should have been removed by DbgModule::RemoveTargetData");
				mSubprogramMap.Remove(node);
			}
			node = next;
		}
	}

	for (DbgExceptionDirectoryEntry* exDirEntry : mExceptionDirectoryMap)
	{
		auto node = exDirEntry;
		while (node != NULL)
		{
			auto next = node->mNext;
			if (node->mDbgModule->mDeleting)
			{
				//BF_DBG_FATAL("Should have been removed by DbgModule::RemoveTargetData");
				mExceptionDirectoryMap.Remove(node);
			}
			node = next;
		}
	}

#endif
	VectorRemoveCtx<Array<DwCommonFrameDescriptor*>> cieRemoveCtx(mCommonFrameDescriptors);
	for (int dieIdx = 0; dieIdx < (int)mCommonFrameDescriptors.size(); dieIdx++)
	{
		DwCommonFrameDescriptor* commonFrameDescriptor = mCommonFrameDescriptors[dieIdx];
		cieRemoveCtx.RemoveCond(dieIdx, commonFrameDescriptor->mDbgModule->mDeleting);
	}
	cieRemoveCtx.Finish();

	auto frameDescItr = mDwFrameDescriptorMap.begin();
	while (frameDescItr != mDwFrameDescriptorMap.end())
	{
		DwFrameDescriptor* frameDesc = &frameDescItr->second;
		if (frameDesc->mCommonFrameDescriptor->mDbgModule->mDeleting)
			frameDescItr = mDwFrameDescriptorMap.erase(frameDescItr);
		else
			++frameDescItr;
	}
}