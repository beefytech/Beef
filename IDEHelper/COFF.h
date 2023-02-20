#pragma once

#include "DbgModule.h"
#include "StrBloomMap.h"
#include "DbgSymSrv.h"
#include "Compiler/BfUtil.h"
#include "Backend/BeLibManger.h"

namespace Beefy
{
	class FileStream;
	class DataStream;
	class MemStream;
	class SafeMemStream;
	class ZipFile;
}

struct CV_LVAR_ADDR_RANGE;
struct CV_LVAR_ADDR_GAP;

NS_BF_DBG_BEGIN

struct CvSectionContrib
{
	int16 mSection;
	int16 mPad1;
	int32 mOffset;
	int32 mSize;
	uint32 mFlags;
	int16 mModule;
	int16 mPad2;
	uint32 mDataCrc;
	uint32 mRelocCrc;
};

struct CvModuleInfoBase
{
	uint32 mOpened;
	CvSectionContrib mSectionContrib;

	uint16 mFlags;
	int16 mStream;
	int32 mSymbolBytes;
	int32 mOldLinesBytes;
	int32 mLinesBytes;
	int16 mNumFiles;
	uint16 mPadding;
	uint32 mOffsets;
	int32 mSourceFileCount;
	int32 mCompilerNameCount;
};

class CvCompileUnit;

struct CvCrossScopeImport
{
	const char* mScopeName;
	CvCompileUnit* mScopeCompileUnit;
	Array<uint32> mImports;
};

struct CvCrossScopeExportEntry
{
	uint32 mLocalId;
	uint32 mGlobalId;
};

class CvCompileUnit : public DbgCompileUnit
{
public:
	Array<CvCrossScopeExportEntry> mExports;
	Array<CvCrossScopeImport> mImports;
	Dictionary<uint32, uint32> mExportMap;
	int mModuleIdx;

	CvCompileUnit(DbgModule* dbgModule) : DbgCompileUnit(dbgModule)
	{
	}
};

struct CvModuleInfo : public CvModuleInfoBase
{
	const char* mModuleName;
	const char* mObjectName;
	CvCompileUnit* mCompileUnit;
	int mIdx;
	bool mHasMappedMethods;
};

struct CvStringTable
{
	int mStream;
	int mStreamOffset;
	const char* mStrTable;

	CvStringTable()
	{
		mStream = -1;
		mStreamOffset = 0;
		mStrTable = NULL;
	}
};

enum CvSymStreamType
{
	CvSymStreamType_Globals_Scan,
	CvSymStreamType_Globals_Targeted,
	CvSymStreamType_Symbols
};

struct CvModuleRef
{
	CvModuleRef* mNext;
	int mModule;
};

struct CvInlineInfo
{
	CvInlineInfo* mNext;
	CvInlineInfo* mTail;
	DbgSubprogram* mSubprogram;
	uint8* mData;
	int mDataLen;
	int mInlinee;
	bool mDebugDump;
};
typedef Array<CvInlineInfo> CvInlineInfoVec;

class CvModuleInfoNameEntry
{
public:
	CvModuleInfo* mModuleInfo;

	bool operator==(const CvModuleInfoNameEntry& other) const
	{
		return mModuleInfo == other.mModuleInfo;
	}

	static size_t GetHashCode(const char* str, int len)
	{
		int curHash = 0;
		const char* strP = str;
		for (int i = 0; i < len; i++)
		{
			char c = *(strP++);
			if (c == 0)
				break;
			c = tolower(c);
			curHash = ((curHash ^ c) << 5) - curHash;
		}
		return curHash;
	}

	static size_t GetHashCode(const StringImpl& str)
	{
		for (int i = str.mLength - 1; i >= 0; i--)
		{
			char c = str[i];
			if ((c == '\\') || (c == '/'))
			{
				return GetHashCode((const char*)&str[i], str.mLength - i);
			}
		}
		if (str.mLength == 0)
			return 0;
		return GetHashCode((const char*)&str[0], str.mLength);
	}
};

class COFF;

class CvStreamReader
{
public:
	COFF* mCOFF;
	int mPageBits;
	Array<uint8*> mStreamPtrs;
	Array<uint8> mTempData;
	int mSize;

public:
	CvStreamReader()
	{
		mCOFF = NULL;
		mPageBits = 0;
		mSize = 0;
	}

	bool IsSetup()
	{
		return mCOFF != NULL;
	}
	uint8* GetTempPtr(int offset, int size, bool mayRecurse = false, bool* madeCopy = NULL);
	uint8* GetPermanentPtr(int offset, int size, bool* madeCopy = NULL);
	//char* GetPermanentCharPtr(int offset, int size);
};

class CvLibInfo
{
public:
	BeLibFile mLibFile;
	Dictionary<String, BeLibEntry*> mSymDict;
};

class COFF : public DbgModule
{
public:
	enum ParseKind
	{
		ParseKind_Header,
		ParseKind_Info,
		ParseKind_Full
	};

public:
	ZipFile* mEmitSourceFile;
	uint8 mWantPDBGuid[16];
	int mWantAge;

	uint8 mPDBGuid[16];
	int mFileAge;
	int mDebugAge;
	ParseKind mParseKind;
	bool mPDBLoaded;
	bool mIs64Bit;

	int mCvPageSize;
	int mCvPageBits;
	int mCvMinTag;
	int mCvMaxTag;
	int mCvIPIMinTag;
	int mCvIPIMaxTag;
	//Array<void*> mCvTagData; // a DbgType* for type info, or a ptr to the data stream for child info
	Array<DbgType*> mCvTypeMap;
	Array<int> mCvTagStartMap;
	Array<int> mCvIPITagStartMap;

	Array<Array<uint8>> mTempBufs;
	int mTempBufIdx;

	Array<DbgType*> mCvSystemTypes;

	Array<int32> mCvStreamSizes;
	Array<int32> mCvStreamPtrStartIdxs;
	Array<int32> mCvStreamPtrs;

	CvStreamReader mCvTypeSectionReader;
	CvStreamReader mCvIPIReader;
	CvStreamReader mCvSymbolRecordReader;

	StringT<128> mPDBPath;
	StringT<128> mOrigPDBPath;

	SafeMemStream* mCvDataStream;
	CvStringTable mStringTable;
	uint8* mCvHeaderData;
	//uint8* mCvTypeSectionData
	uint8* mCvStrTableData;
	uint8* mCvPublicSymbolData;
	uint8* mCvGlobalSymbolData;
	uint8* mNewFPOData;
	int mCvGlobalSymbolInfoStream;
	int mCvPublicSymbolInfoStream;
	int mCvSymbolRecordStream;
	int mCvSrcSrvStream;
	int mCvEmitStream;
	int mCvNewFPOStream;
	Array<CvModuleInfo*> mCvModuleInfo;
	Dictionary<int, DbgSrcFile*> mCVSrcFileRefCache;
	Dictionary<CaseInsensitiveString, CvModuleInfo*> mModuleNameMap;
	HashSet<CvModuleInfoNameEntry> mModuleNameSet;
	Dictionary<String, CvLibInfo*> mHotLibMap;
	Dictionary<String, BeLibEntry*> mHotLibSymMap;
	addr_target mHotThunkCurAddr;
	int mHotThunkDataLeft;
	DbgType* mGlobalsTargetType;
	const char* mPrevScanName;

	// For hot data
	Array<DbgSectionData> mCvTypeSectionData;
	Array<DbgSectionData> mCvCompileUnitData;
	//int mCvTypeSectionDataSize;
	//uint8* mCvCompileUnitData;
	//int mCvCompileUnitDataSize;

	HANDLE mCvMappedFile;
	void* mCvMappedViewOfFile;
	size_t mCvMappedFileSize;
	HANDLE mCvMappedFileMapping;
	bool mIsFastLink;
	bool mTriedSymSrv;
	DbgSymRequest* mDbgSymRequest;
	bool mWantsAutoLoadDebugInfo;

	int mProcSymCount;

public:
	virtual void Fail(const StringImpl& error) override;
	virtual void SoftFail(const StringImpl& error);
	virtual void HardFail(const StringImpl& error) override;

	virtual void ParseGlobalsData() override;
	virtual void ParseSymbolData() override;
	virtual void ParseTypeData(CvStreamReader& reader, int dataOffset);
	void ParseTypeData(int sectionNum, CvStreamReader& reader, int& sectionSize, int& dataOfs, int& hashStream, int& hashAdjOffset, int& hashAdjSize, int& minVal, int& maxVal);
	virtual void ParseTypeData() override;
	void ParseCompileUnit_Symbols(DbgCompileUnit* compileUnit, uint8* sectionData, uint8* data, uint8* dataEnd, CvInlineInfoVec& inlineDataVec, bool deferInternals, DbgSubprogram* useSubprogram);
	CvCompileUnit* ParseCompileUnit(CvModuleInfo* moduleInfo, CvCompileUnit* compileUnit, uint8* sectionData, int sectionSize);
	virtual CvCompileUnit* ParseCompileUnit(int compileUnitId) override;
	virtual void ParseCompileUnits() override;
	virtual void MapCompileUnitMethods(DbgCompileUnit* compileUnit) override;
	virtual void MapCompileUnitMethods(int compileUnitId) override;
	virtual void PopulateType(DbgType* dbgType) override;
	virtual void PopulateTypeGlobals(DbgType* dbgType) override;
	virtual void PopulateSubprogram(DbgSubprogram* dbgSubprogram) override;
	void FixSubprogramName(DbgSubprogram * dbgSubprogram);
	int MapImport(CvCompileUnit* compileUnit, int Id);
	void FixupInlinee(DbgSubprogram* dbgSubprogram, uint32 ipiTag);
	virtual void FixupInlinee(DbgSubprogram* dbgSubprogram) override;
	virtual void PopulateStaticVariableMap() override;
	virtual void ProcessDebugInfo() override;
	virtual void FinishHotSwap() override;
	virtual intptr EvaluateLocation(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, DbgAddrType* outAddrType, DbgEvalLocFlags flags = DbgEvalLocFlag_None) override;
	virtual bool CanGetOldSource() override;
	virtual String GetOldSourceCommand(const StringImpl& path) override;
	virtual bool GetEmitSource(const StringImpl& filePath, String& outText) override;
	virtual bool HasPendingDebugInfo() override;
	virtual void PreCacheImage() override;
	virtual void PreCacheDebugInfo() override;
	virtual bool RequestImage() override;
	virtual bool RequestDebugInfo(bool allowRemote) override;
	virtual bool WantsAutoLoadDebugInfo() override;
	virtual bool DbgIsStrMutable(const char* str) override;
	virtual addr_target LocateSymbol(const StringImpl& name) override;
	virtual void ParseFrameDescriptors() override;

	bool LoadModuleImage(const StringImpl& imagePath);
	void FixConstant(DbgVariable* variable);
	void MapRanges(DbgVariable* variable, CV_LVAR_ADDR_RANGE* range, CV_LVAR_ADDR_GAP* gaps);
	void MakeThis(DbgSubprogram* curSubprogram, DbgVariable*& curParam);
	const char* CvCheckTargetMatch(const char* name, bool& wasBeef);
	int CvGetStringHash(const char* str);
	void CvFixupName(char* name);
	DbgType* CvGetTypeOrNamespace(char* name, DbgLanguage language = DbgLanguage_Unknown);
	void ParseSymbolStream(CvSymStreamType symStreamType);
	void ScanCompileUnit(int compileUnitId);
	void ParseFrameDescriptors(uint8* data, int size, addr_target baseAddr);

	const char* CvParseSymbol(int offset, CvSymStreamType symStreamType, addr_target& outAddr);
	uint8* HandleSymStreamEntries(CvSymStreamType symStreamType, uint8* data, uint8* addrMap);
	const char* CvParseString(uint8*& data);
	const char* CvParseAndDupString(uint8*& data);
	const char* CvDupString(const char* str, int strLen);

	void CvReadStream(int sectionIdx, CvStreamReader& streamReader);
	void CvInitStreamRaw(CvStreamReader& streamReader, uint8* data, int size);
	uint8* CvReadStream(int sectionIdx, int* outSize = NULL);
	uint8* CvReadStreamSegment(int sectionIdx, int offset, int size);
	void ReleaseTempBuf(uint8* buf);

	void InitCvTypes();
	DbgType* CvCreateType();
	int CvConvRegNum(int regNum, int* outBits = NULL);
	addr_target GetSectionAddr(uint16 section, uint32 offset);
	int64 CvParseConstant(uint16 constVal, uint8*& data);
	int64 CvParseConstant(uint8*& data);
	DbgType* CvGetType(int typeId);
	DbgType* CvGetTypeSafe(int typeId);
	DbgType* CvGetType(int typeId, CvCompileUnit* compileUnit);
	int CvGetTagStart(int tagIdx, bool ipi);
	int CvGetTagSize(int tagIdx, bool ipi);
	uint8* CvGetTagData(int tagIdx, bool ipi, int* outDataSize = NULL);
	void CvParseArgList(DbgSubprogram* subprogram, int tagIdx, bool ipi);
	DbgSubprogram* CvParseMethod(DbgType* parentType, const char* methodName, int tagIdx, bool ipi, DbgSubprogram* subprogram = NULL);
	void CvParseMethodList(DbgType* parentType, const char* methodName, int tagIdx, bool ipi);
	void CvParseMembers(DbgType* parentType, int tagIdx, bool ipi);
	DbgType* CvParseType(int tagIdx, bool ipi = false);
	bool CvParseDBI(int wantAge);
	void ParseSectionHeader(int sectionIdx);
	void CvParseIPI();
	bool CvParseHeader(uint8 wantGuid[16], int32 wantAge);
	void FixTypes(int startingIdx);
	bool ParseCv(DataStream& CvFS, uint8* rootDirData, int pageSize, uint8 wantGuid[16], int32 wantAge);
	bool TryLoadPDB(const String& CvPath, uint8 wantGuid[16], int32 wantAge);
	void ClosePDB();
	virtual void ReportMemory(MemReporter* memReporter) override;

public:
	COFF(DebugTarget* debugTarget);
	~COFF();

	virtual bool LoadPDB(const String& CvPath, uint8 wantGuid[16], int32 wantAge) override;
	virtual bool CheckSection(const char* name, uint8* sectionData,
		int sectionSize) override;
};

class CvAutoReleaseTempData
{
public:
	COFF * mCOFF;
	uint8* mData;

public:
	CvAutoReleaseTempData(COFF* coff, uint8* data)
	{
		mCOFF = coff;
		mData = data;
	}

	~CvAutoReleaseTempData()
	{
		mCOFF->ReleaseTempBuf(mData);
	}
};

NS_BF_DBG_END

namespace std
{
	template<>
	struct hash<NS_BF_DBG::CvModuleInfoNameEntry>
	{
		size_t operator()(const NS_BF_DBG::CvModuleInfoNameEntry& val) const
		{
			return NS_BF_DBG::CvModuleInfoNameEntry::GetHashCode(Beefy::StringImpl::MakeRef(val.mModuleInfo->mModuleName));
		}
	};
}
