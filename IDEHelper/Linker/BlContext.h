#pragma once

#include "../Beef/BfCommon.h"
#include "../Compiler/BfUtil.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/ChunkedDataBuffer.h"
#include "BlSymTable.h"
#include "BeefySysLib/MemStream.h"
#include "BeefySysLib/util/ChunkedVector.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/MappedFile.h"
#include <map>
#include <set>

NS_BF_BEGIN

//#define BL_PERFTIME


#ifdef BL_PERFTIME
#define BL_AUTOPERF(name) AutoPerf autoPerf##__LINE__(name);
#else
#define BL_AUTOPERF(name)
#endif

struct BlSymHash
{
public:
	const char* mName;
};

enum BlSymKind
{
	BlSymKind_Undefined = -1,
	BlSymKind_LibSymbolRef = -2,
	BlSymKind_LibSymbolGroupRef = -3,
	BlSymKind_WeakSymbol = -4,
	BlSymKind_ImageBaseRel = -5,
	BlSymKind_Import = -6,
	BlSymKind_ImportImp = -7,
	BlSymKind_Absolute = -8,	
};

class BlCodeView;

class BlSymbol
{
public:
	const char* mName;
	union
	{
		// If mSectionIdx is negative, then we're not a resolved symbol and the mKind member is used instead
		int mSegmentIdx;
		BlSymKind mKind;
	};
	union
	{
		int mSegmentOffset;
		int mParam;
	};	

	int mObjectDataIdx;
};

class BlLib
{
public:
	String mFileName;
	char* mStrTable;

public:
	BlLib()
	{
		mStrTable = NULL;
	}
};

// From lowest priority to highest
enum BlObjectDataKind
{		
	BlObjectDataKind_DirectiveLib,
	BlObjectDataKind_DefaultLib,
	BlObjectDataKind_SpecifiedLib,
	BlObjectDataKind_SpecifiedObj,
};

struct BlObjectDataSymInfo
{
	BlSymbol* mSym;

	enum Kind
	{
		Kind_None = -1,
		Kind_Section = -2
	};

	union
	{
		int mSegmentIdx;
		Kind mKind;
	};
	union 
	{
		int mSegmentOffset;	
		int mSectionNum;
	};

	BlObjectDataSymInfo()
	{
		mSym = NULL;
		mSegmentIdx = Kind_None;
		mSegmentOffset = 0;
	}
};

enum BlObjectDataLoadState
{
	BlObjectDataLoadState_UnloadedLib = -1,
	BlObjectDataLoadState_UnloadedThinLib = -2,
	BlObjectDataLoadState_ImportObject = -3,	
};

class BlObjectData
{
public:
	BlObjectDataKind mKind;
	int mIdx;
	BlLib* mLib;
	char* mName;
	union
	{
		void* mData;
		BlSymbol* mImportSym;
	};	
	union
	{
		int mSize;
		BlObjectDataLoadState mLoadState;
	};

	FILE* mFP;
	int mFPOfs;

public:
	BlObjectData()
	{
		mKind = BlObjectDataKind_DefaultLib;
		mIdx = -1;
		mLib = NULL;
		mName = NULL;
		mData = NULL;
		mSize = 0;

		mFP = NULL;
		mFPOfs = 0;
	}
};

struct BlLibMemberHeader
{
	char mName[16];
	char mDate[12];
	char mUserId[6];
	char mGroupId[6];
	char mMode[8];
	char mSize[10];
	char mEnd[2];

	void Init()
	{
		char* spaces = "                ";
		memcpy(mName, spaces, 16);
		memcpy(mDate, spaces, 12);
		memcpy(mUserId, spaces, 6);
		memcpy(mGroupId, spaces, 6);
		memcpy(mMode, spaces, 8);
		memcpy(mSize, spaces, 10);
		mEnd[0] = '`';
		mEnd[1] = '\n';
	}
};

class BlChunk
{
public:
	int mOffset;
	int mAlignPad; // Before data
	void* mData;
	int mSize;
	int mObjectDataIdx;
};

enum BlRelocKind : uint8
{
	BlRelocKind_ADDR32NB,
	BlRelocKind_ADDR64,
	BlRelocKind_REL32,
	BlRelocKind_REL32_1,
	BlRelocKind_REL32_4,
	BlRelocKind_SECREL,
	BlRelocKind_SECTION,
};

enum BeRelocFlags : uint8
{
	//BeRelocFlags_SymName = 1,
	BeRelocFlags_Sym = 2,
	BeRelocFlags_SymIsNew = 4,
	BeRelocFlags_Loc = 8,
	BeRelocFlags_Func = 0x10
};

class BlReloc
{
public:
	BlRelocKind mKind;	
	BeRelocFlags mFlags;	
	int32 mOutOffset;
	union
	{
		struct
		{
			int32 mSegmentIdx;
			int32 mSegmentOffset;
		};
		const char* mSymName;
		BlSymbol* mSym;
	};
};

class BlContext;

class BlSegmentInfo
{
public:
	String mName;	
};

// Segments are almost like sections, but segments like ".text" and ".text$mn" get 
//  combined together into BlOutSection objects so there's not a one-to-one correspondence.
class BlSegment : public BlSegmentInfo
{
public:
	int mOutSectionIdx;
	int mCharacteristics;
	BumpAllocator mAlloc;

	BlContext* mContext;
	std::vector<BlChunk> mChunks;
	std::vector<BlReloc> mRelocs;
	int mResolvedRelocIdx;
	int mCurSize;
	int mSegmentIdx;
	int mAlign;

	int mRVA;	

public:
	BlSegment()
	{
		mOutSectionIdx = -1;
		mCharacteristics = 0;
		mContext = NULL;
		mCurSize = 0;
		mSegmentIdx = -1;
		mAlign = 1;
		mRVA = 0;
		mResolvedRelocIdx = 0;
	}

	BlChunk* AddChunk(void* data, int size, int characteristics, int& offset);
};

class BlOutSection
{
public:
	int mIdx;
	String mName;
	std::vector<BlSegment*> mSegments;
	int mCharacteristics;
	int mAlign;
	int mRVA;
	int mRawDataPos;
	int mRawSize;
	int mVirtualSize;

public:
	BlOutSection()
	{
		mIdx = 0;
		mCharacteristics = 0;
		mAlign = 1;
		mRawDataPos = 0;
		mRVA = 0;
		mRawSize = 0;
		mVirtualSize = 0;
	}
};

class BlPendingComdat
{
public:

};

class BlImportLookup
{
public:
	String mName;	
	int mHint;
	int mThunkIdx;
	int mIDataIdx;	

public:
	BlImportLookup()
	{		
		mHint = 0;
		mThunkIdx = -1;
		mIDataIdx = -1;
	}
};

class BlImportFile
{
public:
	OwnedVector<BlImportLookup> mLookups;
};

struct BlResIdLess
{
	bool operator()(const char16_t* lhs, const char16_t* rhs) const
	{
		bool lhsIsID = ((uintptr)lhs < 0x10000);
		bool rhsIsID = ((uintptr)rhs < 0x10000);

		// Strings before IDs
		if (lhsIsID && !rhsIsID)
			return false;
		if (!lhsIsID && rhsIsID)
			return true;
		if (lhsIsID && rhsIsID)
			return (uintptr)lhs < (uintptr)rhs;
		return wcscmp((wchar_t*)lhs, (wchar_t*)rhs) < 0;
	}
};

class BlResEntry
{
public:
	bool mIsDir;

	virtual ~BlResEntry()
	{

	}
};

class BlResData : public BlResEntry
{
public:
	BlResData()
	{
		mIsDir = false;
	}
	void* mData;
	int mSize;
};

class BlResDirectory : public BlResEntry
{
public:
	BlResDirectory()
	{
		mIsDir = true;
	}
	std::map<const char16_t*, BlResEntry*, BlResIdLess> mEntries;
};

class BlPeRelocs
{
public:
	ChunkedDataBuffer mData;
	uint32 mBlockAddr;
	int32* mLenPtr;

public:
	BlPeRelocs()
	{
		mBlockAddr = 0;
		mLenPtr = NULL;
	}

	void Add(uint32 addr, int fixupType);
};

class BlExport
{
public:	
	String mSrcName;
	int mOrdinal;
	bool mNoName;
	bool mIsPrivate;
	bool mIsData;
	bool mOrdinalSpecified;

public:
	BlExport()
	{
		mOrdinal = -1;
		mNoName = false;
		mIsPrivate = false;
		mIsData = false;
		mOrdinalSpecified = false;
	}
};

class BlImportGroup
{
public:
	std::vector<int> mObjectDatas;
};

class BlSymNotFound
{
public:
	BlSymbol* mSym;
	int mSegmentIdx;
	int mOutOffset;
};

class BlContext
{
public:
	CritSect mCritSect;
	String mOutName;
	String mPDBPath;
	String mImpLibName;
	int mDebugKind;
	bool mVerbose;
	bool mNoDefaultLib;
	std::set<String> mNoDefaultLibs;
	std::vector<String> mDefaultLibs;
	bool mHasFixedBase;
	bool mHasDynamicBase;
	bool mHighEntropyVA;
	bool mIsNXCompat;
	bool mIsTerminalServerAware;
	int mPeSubsystem;
	int mHeapReserve;
	int mHeapCommit;
	int mStackReserve;
	int mStackCommit;
	int mPeVersionMajor;
	int mPeVersionMinor;
	int mProcessingSegIdx;
	int mDbgSymSectsFound;
	int mDbgSymSectsUsed;
	bool mIsDLL;
	String mEntryPoint;
	uint32 mTimestamp;
	int mNumObjFiles;
	int mNumLibs;
	int mNumImportedObjs;
	int mNumWeakSymbols;

	BumpAllocator mAlloc;
	bool mFailed;
	int mErrorCount;

	std::vector<String> mSearchPaths;
	OwnedVector<MappedFile> mMappedFiles;
	OwnedVector<BlObjectData> mObjectDatas;
	std::vector<BlObjectData*> mObjectDataWorkQueue;
	//std::vector<BlChunk*> mChunks;
	std::vector<BlSegment*> mSegments;
	std::vector<BlSegment*> mOrderedSegments;
	std::map<String, String> mSectionMerges;
	OwnedVector<BlSegment> mDynSegments;
	OwnedVector<BlLib> mLibs;
	BlSymTable mSymTable;
	std::vector<String> mRemapStrs;
	OwnedVector<BlOutSection> mOutSections;
	std::map<String, BlImportFile> mImportFiles;
	std::vector<BlImportLookup*> mImportLookups;
	std::map<String, BlExport> mExports;
	std::vector<String> mForcedSyms;
	OwnedVector<BlImportGroup> mImportGroups;
	std::vector<BlSymNotFound> mSymNotFounds;

	BlResDirectory mRootResDirectory;
	OwnedVector<BlResEntry> mResEntries;
	DynMemStream mImportStream;
	DynMemStream mThunkStream;
	DynMemStream mDebugDirStream;
	DynMemStream mCvInfoStream;
	DynMemStream mResDataStream;
	DynMemStream mLoadConfigStream;
	DynMemStream mExportStream;
	BlCodeView* mCodeView;	
	BlSegment* mIDataSeg;	
	BlSegment* mTextSeg;
	BlSegment* mRDataSeg;
	BlSegment* mDataSeg;
	BlSegment* mBSSSeg;
	int mNumFixedSegs;
	String mManifestUAC;
	String mManifestData;	
	BlPeRelocs mPeRelocs;

	int64 mImageBase;
	/*BlSection mTextSect;
	BlSection mRDataSect;
	BlSection mBSSSect;	*/	
	
	std::set<String> mSeenLibs;	

public:
	BlSymHash Hash(const char* symName);
	String ObjectDataIdxToString(int objectDataIdx);
	String GetSymDisp(const StringImpl& name);
	String FixFilePath(const StringImpl& path, const StringImpl& actualFileName);
	void Fail(const StringImpl& error);
	void Warn(const StringImpl& error);
	void Fail(BlObjectData* objectData, const StringImpl& error);
	void AssertFailed();
	void NotImpl();
	void PrintStats();
	void AddSegment(BlSegment* section);
	void LoadFiles();	
	bool HandleLibSymbolRef(BlSymbol* sym, int objectDataIds);
	BlSymbol* ProcessSymbol(BlSymbol* sym);
	BlSymbol* ResolveSym(const char* name, bool throwError = true);
	uint64 GetSymAddr(BlSymbol* sym);
	void AddAbsoluteSym(const char* name, int val);
	BlSegment* CreateSegment(const StringImpl& name, int characteristics, int align);
	void PlaceSections();
	void ResolveRelocs();
	void LoadResourceData(BlObjectData* objectData);
	void LoadDefData(BlObjectData* objectData);
	void WriteDLLLib(const StringImpl& libName);
	
	void PopulateIData_LookupTable(BlSegment* iDataSect, int& hintNameTableSize);
	void PopulateIData(BlSegment* iDataSeg);
	BlSegment* CreateIData();
	void PopulateThunkData(BlSegment* thunkSeg, BlSegment* iDataSeg);
	BlSegment* CreateThunkData();
	void PopulateExportData(BlSegment * exportSeg);	
	BlSegment * CreateExportData();
	void PopulateCvInfoData(BlSegment* cvInfoSeg);
	BlSegment* CreateCvInfoData();
	void PopulateDebugDirData(BlSegment* debugDirSeg, BlSegment* cvInfoSeg);
	BlSegment* CreateDebugDirData();
	void PopulateResData(BlSegment* resSeg);	
	void GetResDataStats(BlResDirectory* resDir, int& dirCount, int& dataCount, int& symsStrsSize, int& dataSize);
	void CreateResData(BlSegment* resSeg, BlResDirectory* resDir, int resDirSize, int symsStrSize, DynMemStream& symStrsStream, DynMemStream& dataEntryStream);
	BlSegment* CreateResData();	
	void WriteOutSection(BlOutSection * outSection, DataStream * st);	

public:
	BlContext();
	~BlContext();

	void Init(bool is64Bit, bool isDebug);
	void AddSearchPath(const StringImpl& directory);
	bool DoAddFile(const StringImpl& path, BlObjectDataKind objectDataKind);
	bool AddFile(const StringImpl& fileName, BlObjectDataKind objectDataKind);			

	void Link();
};

NS_BF_END
