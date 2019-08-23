#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Hash.h"
#include "../Compiler/BfUtil.h"
#include <unordered_map>

NS_BF_BEGIN

struct BlCvStringTable
{
	int mStream;
	int mStreamOffset;
	const char* mStrTable;

	BlCvStringTable()
	{
		mStream = -1;
		mStreamOffset = 0;
		mStrTable = NULL;
	}
};

class MappedFile;
class BlCvTypeSource;

class BlPdbParser
{
public:
	String mFileName;
	MappedFile* mMappedFile;	
	uint8* mData;
	int mCvPageSize;
	BlCvStringTable mStringTable;

	std::vector<int32> mCvStreamSizes;
	std::vector<int32> mCvStreamPtrStartIdxs;
	std::vector<int32> mCvStreamPtrs;	
	std::vector<int> mCvIPITagStartMap;

	uint8* mCvHeaderData;
	uint8* mCvTypeSectionData;	
	uint8* mCvIPIData;
	BlCvTypeSource* mTypeSource;

public:
	void Fail(const StringImpl& err);
	void NotImpl();

	uint8* CvReadStream(int streamIdx, int* outSize = NULL);
	bool CvParseHeader();	
	void ParseTypeData();	
	void ParseIPIData();

public:
	BlPdbParser();
	~BlPdbParser();

	bool Load(const StringImpl& fileName);
	bool ParseCv(uint8 * rootDirData);	
};

NS_BF_END
