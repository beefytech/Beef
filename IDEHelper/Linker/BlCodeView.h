#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/MemStream.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/ChunkedDataBuffer.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/WorkThread.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "../Compiler/BfUtil.h"
#include "BlMsf.h"
#include <unordered_map>
#include <unordered_set>
#include "BlPdbParser.h"
//#include "Hash.h"
#include "BlCvTypeSource.h"
#include "BlContext.h"
#include <queue>

//#define BL_USE_DENSEMAP_CV

#ifdef BL_USE_DENSEMAP_CV
#include <sparsehash/dense_hash_map>
#include <sparsehash/dense_hash_set>
#endif

NS_BF_BEGIN

struct PE_SymInfo;
struct COFFRelocation;
class BlContext;
class BlObjectData;
class BlCvTypeSource;

class BlCVStream
{
public:
	String mName;
	Array<int> mBlocks;		
	int mSize;

public:
	BlCVStream()
	{				
		mSize = 0;
	}
};


class BlCvFileInfo
{
public:
	int mStrTableIdx;
	int mChksumOfs;
	int8 mHashType;
	uint8 mHash[16];
};

class BlCvLine
{
public:
	int mOffset;
	int mLineNumStart;	
};

class BlCvLineInfoBlock
{
public:
	int mFileInfoIdx;
	std::vector<BlCvLine> mLines;
	std::vector<int16> mColumns;
};

class BlCvLineInfo
{
public:
	int mStartSegmentIdx;
	int mStartSegmentOffset;
	int mContribBytes;
	std::vector<BlCvLineInfoBlock> mLineInfoBlocks;

	BlCvLineInfo()
	{
		mStartSegmentIdx = -1;
		mStartSegmentOffset = 0;
		mContribBytes = 0;
	}
};

struct BlCvSymDataBlock
{	
	void* mData;
	int mSize;
	int mOutSize;
	int mSourceSectOffset;
	COFFRelocation* mRelocStart;
	COFFRelocation* mRelocEnd;

	FILE* mFP;
	int mFPOfs;
};

enum BlSectInfoState : intptr
{
	BlSectInfoState_QueuedData = -1,
	BlSectInfoState_QueuedDebugT = -2,
	BlSectInfoState_QueuedDebugS = -3
};

struct BlObjectDataSectInfo
{
	int mSegmentIdx;
	int mSegmentOffset;	
	void* mRelocData;
	union
	{
		void* mChunkData;
		BlSectInfoState mState;
	};
	uint16 mSelectNum;
	int8 mSelection;

	bool IsUsed()
	{
		return (intptr)mChunkData >= 0;
	}
};

struct BlObjectDataSectInfo;

class BlCvModuleContrib
{
public:
	int mBlSectionIdx;
	int mBlSectionOfs;
	int mSize;
	int mCharacteristics;

public:
	BlCvModuleContrib()
	{
		mBlSectionIdx = 0;
		mBlSectionOfs = 0;
		mSize = 0;
		mCharacteristics = 0;
	}
};

class BlCvModuleInfo
{
public:
	int mIdx;
	String mName;
	const char* mStrTab;
	BlObjectData* mObjectData;	
	int mSymStreamIdx;
	BlCvTypeSource* mTypeSource;
	PE_SymInfo* mObjSyms;
	BfSizedVector<BlObjectDataSymInfo, 0> mSymInfo;
	BfSizedVector<BlCvSymDataBlock, 1> mSymData;
	int mLineInfoSize;	
	//BfSizedVector<BlCvContrib, 1> mContribs;
	BlCvModuleContrib mContrib;
	std::vector<BlCvFileInfo> mFileInfos;
	OwnedVector<BlCvLineInfo> mLineInfo;
	BfSizedVector<BlCvSymDataBlock, 1> mInlineData;
	BfSizedVector<BlObjectDataSectInfo, 1> mSectInfos;	
	int mDeferredSegOfsStart;
	int mDeferredSegOfsLen;

public:
	BlCvModuleInfo()
	{
		mIdx = -1;		
		mStrTab = NULL;
		//mObjSyms = NULL;
		mObjectData = NULL;		
		mSymStreamIdx = -1;		
		mLineInfoSize = 0;		
		mTypeSource = NULL;
		mDeferredSegOfsStart = 0;
		mDeferredSegOfsLen = 0;
		mObjSyms = NULL;
	}

	~BlCvModuleInfo()
	{
	
	}
};

class BlTypeInfo
{
public:
#ifdef BL_USE_DENSEMAP_CV
	google::dense_hash_map<Val128, int, Val128::Hash, Val128::Equals> mTypeMap;
#else
	std::unordered_map<Val128, int, Val128::Hash, Val128::Equals> mTypeMap;
#endif
	ChunkedDataBuffer mData;
	int mCurTagId;

public:
	BlTypeInfo()
	{
		mCurTagId = 0x1000;

#ifdef BL_USE_DENSEMAP_CV		
		mTypeMap.set_empty_key(Val128(-1));
		mTypeMap.min_load_factor(0);
		mTypeMap.resize(100000);
#endif
	}
};

class BlCvRecordEntry
{
public:
	Val128 mKey;
	int mRecordIndex;
	BlCvRecordEntry* mNext;
};

class BlCvRecordMap
{
public:
#ifdef BL_USE_DENSEMAP_CV
	google::dense_hash_set<Val128, Val128::Hash, Val128::Equals> mKeySet;
#else
	std::unordered_set<Val128, Val128::Hash, Val128::Equals> mKeySet;
#endif
	BlCodeView* mCodeView;
	BlCvRecordEntry* mBuckets[4096];
	BumpAllocator mAlloc;
	int mNumEntries;	

public:
	BlCvRecordMap()
	{
		mCodeView = NULL;
		memset(&mBuckets, 0, sizeof(mBuckets));
		mNumEntries = 0;

#ifdef BL_USE_DENSEMAP_CV
		mKeySet.set_empty_key(Val128(-1));
		mKeySet.min_load_factor(0);
		mKeySet.resize(100000);
#endif
	}

	void Insert(const char* name, int symIdx);
	int TryInsert(const char* name, const Val128& key);
};

class BlCvContrib
{
public:
	int mModuleIdx;
	int mBlSectionOfs;
	int mSize;
	int mCharacteristics;
};

class BlCvContributionSeg
{
public:
	std::vector<BlCvContrib> mContribs;
};

class BlCvContributionMap
{
public:
	OwnedVector<BlCvContributionSeg> mSegments;
};

class BlCvTypeWorkThread : public WorkThread
{
public:	
	std::deque<BlCvTypeSource*> mTypeSourceWorkQueue;
	BlCodeView* mCodeView;
	CritSect mCritSect;
	SyncEvent mWorkEvent;
	volatile bool mTypesDone;
	volatile bool mThreadDone;

public:
	BlCvTypeWorkThread();
	~BlCvTypeWorkThread();

	virtual void Stop() override;
	virtual void Run() override;

	void Add(BlCvTypeSource* typeSource);
};

class BlCvModuleWorkThread : public WorkThread
{
public:	
	std::deque<BlCvModuleInfo*> mModuleWorkQueue;
	BlCodeView* mCodeView;
	CritSect mCritSect;
	SyncEvent mWorkEvent;
	volatile bool mModulesDone;

public:
	BlCvModuleWorkThread();
	~BlCvModuleWorkThread();

	virtual void Stop() override;
	virtual void Run() override;

	void Add(BlCvModuleInfo* module);
};

class BlCvStreamWriter
{
public:
	BlMsf* mMsf;
	BlCVStream* mCurStream;
	uint8* mCurBlockPos;
	uint8* mCurBlockEnd;

public:
	BlCvStreamWriter();

	void Start(BlCVStream* stream);
	void Continue(BlCVStream* stream);
	void End(bool flush = true);

	void Write(const void* data, int size);
	template <typename T>
	void WriteT(const T& val)
	{
		Write(&val, sizeof(T));
	}
	void Write(ChunkedDataBuffer& buffer);
	
	int GetStreamPos();
	void StreamAlign(int align);
	void* GetStreamPtr(int pos);
};

class BlCvStreamReader
{
public:
	BlMsf* mMsf;
	int mBlockIdx;
	BlCVStream* mCurStream;
	uint8* mCurBlockPos;
	uint8* mCurBlockEnd;

public:
	BlCvStreamReader();

	void Open(BlCVStream* stream);

	void* ReadFast(void* data, int size);
	void Seek(int size);
	int GetStreamPos();
};

class BlCodeView
{
public:	
	BlContext* mContext;
	BumpAllocator mAlloc;
	BlMsf mMsf;	

	int mStat_TypeMapInserts;
	int mStat_ParseTagFuncs;

	OwnedVector<BlCVStream> mStreams;

	OwnedVector<BlCvModuleInfo> mModuleInfo;	
	int mSectStartFilePos;
				
	BlTypeInfo mTPI;
	BlTypeInfo mIPI;

	int8 mSignature[16];
	int mAge;

	DynMemStream mSectionHeaders;
	ChunkedVector<int> mSymRecordDeferredPositions;
	int mSymRecordsStream;
	//ChunkedDataBuffer mSymRecords;
	BlCvRecordMap mGlobals;	

	std::unordered_map<String, BlCvTypeSource*> mTypeServerFiles;
	std::unordered_map<String, int> mStrTabMap;
	String mStrTab;

	BlCvContributionMap mContribMap;

	BlCvTypeWorkThread mTypeWorkThread;
	BlCvModuleWorkThread mModuleWorkThread;

	ChunkedVector<int> mDeferedSegOffsets;

	BlCvStreamWriter mWriter;
	BlCvStreamWriter mSymRecordsWriter;

public:	
	void NotImpl();	
	void Fail(const StringImpl& err);	
	char* StrDup(const char* str);
	
	void FixSymAddress(void* oldDataStart, void* oldDataPos, void* outDataPos, BlCvModuleInfo* module, BlCvSymDataBlock* dataBlock, COFFRelocation*& nextReloc);	

	int StartStream(int streamIdx = -1);
	void StartUnnamedStream(BlCVStream& stream);	
	void EndStream(bool flush = true);		
	void FlushStream(BlCVStream* stream);
	void CvEncodeString(const StringImpl& str);
	int AddToStringTable(const StringImpl& str);
	void WriteStringTable(const StringImpl& strTable, std::unordered_map<String, int>& strTabMap);
	void GetOutSectionAddr(int segIdx, int segOfs, uint16& cvSectIdx, long& cvSectOfs);
	bool FixTPITag(BlCvModuleInfo* module, unsigned long& typeId);
	bool FixIPITag(BlCvModuleInfo* module, unsigned long& typeId);
	bool FixIPITag_Member(BlCvModuleInfo* module, unsigned long& typeId);

	bool OnlyHasSimpleRelocs(BlCvModuleInfo * module);

	void StartSection(int sectionNum);
	int EndSection();
	void FlushMemStream();
	
	void CreatePDBInfoStream();
	void WriteTypeData(int streamId, BlTypeInfo& typeInfo);
	void CreateTypeStream();
	void CreateIPIStream();
	void WriteRecordMap(BlCvRecordMap * recordMap);
	int CreateGlobalStream();
	int CreatePublicStream();
	void FinishSymRecords();
	int CreateSymRecordStream();
	int CreateSectionHeaderStream();
	void CreateDBIStream();
	void CreateLinkInfoStream();
	void CreateHeaderBlockStream();
	void CreateNamesStream();	
	void CreateModuleStreamSyms(BlCvModuleInfo* module, BlCvSymDataBlock* dataBlock, int dataOfs);
	void CreateLinkerSymStream();
	void CreateModuleStream(BlCvModuleInfo* module);
	void FinishModuleStream(BlCvModuleInfo* module);
	void CreateLinkerModule();
	void DoFinish();

public:
	BlCodeView();
	~BlCodeView();
		
	bool Create(const StringImpl& fileName);
	void StartWorkThreads();
	void StopWorkThreads();	
	void Finish();
};

NS_BF_END