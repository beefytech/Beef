#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/CritSect.h"
#include "../Compiler/BfUtil.h"
#include <unordered_map>

NS_BF_BEGIN

class BlPdbParser;
class BlTypeInfo;
class BlCompositeType;
class BlCodeView;
class BlType;
class BlStructType;
class BlProcType;

class BlCvTypeInfoExt
{
public:	
	int mMasterTag;	
	int mMemberMasterTag;
};

enum BlTypeMapFlag
{
	BlTypeMapFlag_InfoExt_ProcId_TypeOnly = 0x80000000,
	BlTypeMapFlag_InfoExt_ProcId_Resolved = 0x40000000,
	BlTypeMapFlag_InfoExt_MASK			  = 0x0FFFFFFF
};

class BlCvTypeContainer
{
public:
	int mCvMinTag;
	int mCvMaxTag;	
	// Negative values in mTagMap are contextual:
	//   For LF_FUNC_ID, refers to embedded FunctionType
	std::vector<int> mTagMap;
	std::vector<int> mElementMap; // From FUNC_ID to FuncType, for example
	std::vector<int> mCvTagStartMap;	
	uint8* mSectionData;
	int mSectionSize;
	BlCodeView* mCodeView;
	BlTypeInfo* mOutTypeInfo;
	std::vector<int>* mTPIMap;
	std::vector<int>* mIPIMap;
	int mIdx;
	//std::vector<BlCvTypeInfoExt> mInfoExts;

public:
	void NotImpl();
	void Fail(const StringImpl& err);
	const char* CvParseAndDupString(uint8 *& data, bool hasUniqueName = false);
	int CreateMasterTag(int tagId, BlTypeInfo* outTypeInfo);
	void HashName(const char* namePtr, bool hasMangledName, HashContext& hashContext);
	void HashTPITag(int tagId, HashContext& hashContext);
	void HashIPITag(int tagId, HashContext& hashContext);
	void HashTagContent(int tagId, HashContext& hashContext, bool& isIPI);	
	void ParseTag(int tagId, bool forceFull = false);

public:
	BlCvTypeContainer();
	~BlCvTypeContainer();

	int GetMasterTPITag(int tagId);
	int GetMasterIPITag(int tagId);
	void ScanTypeData();	
	void ParseTypeData();
};

class BlObjectData;

class BlCvTypeSource
{
public:
	BlPdbParser* mTypeServerLib;
	BlObjectData* mObjectData;
	BlCvTypeContainer mTPI;
	BlCvTypeContainer* mIPI;	
	volatile bool mIsDone;	

public:
	BlCvTypeSource();
	~BlCvTypeSource();

	void CreateIPI();
	void Init(BlCodeView* codeView);
};

NS_BF_END
