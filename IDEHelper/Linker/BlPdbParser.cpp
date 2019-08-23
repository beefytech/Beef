#include "BlPdbParser.h"
#include "BlContext.h"
#include "codeview/cvinfo.h"
#include "BlCvParser.h"
#include "BlCvTypeSource.h"

USING_NS_BF;

#define MSF_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\032DS\0\0"
#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define GET_INTO(T, name) T name = GET(T)
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

void BlPdbParser::Fail(const StringImpl& err)
{
	BF_FATAL("Err");
}

void BlPdbParser::NotImpl()
{
	BF_FATAL("NotImpl");
}

BlPdbParser::BlPdbParser()
{
	mCvHeaderData = NULL;
	mCvTypeSectionData = NULL;
	mCvIPIData = NULL;
	mMappedFile = NULL;
}

BlPdbParser::~BlPdbParser()
{
	delete mCvHeaderData;
	delete mCvTypeSectionData;
	delete mCvIPIData;
	delete mMappedFile;
}

bool BlPdbParser::Load(const StringImpl& fileName)
{
	mMappedFile = new MappedFile();
	if (!mMappedFile->Open(fileName))
		return false;
	mData = (uint8*)mMappedFile->mData;

	uint8* data = (uint8*)mMappedFile->mData;
	if (memcmp(data, MSF_SIGNATURE_700, 32) != 0)
	{
		Fail("PDB signature error");
		return false;
	}
	data += 32;

	int pageSize = GET(int32);
	int fpmPageNum = GET(int32);
	int totalPageCount = GET(int32);
	int rootDirSize = GET(int32);
	int unknown = GET(int32);
	int rootDirPtr = GET(int32);

	bool failed = false;

	mCvPageSize = pageSize;

	int rootPageCount = (rootDirSize + pageSize - 1) / pageSize;
	int rootPointersPages = (rootPageCount * sizeof(int32) + pageSize - 1) / pageSize;
	int rootPageIdx = 0;

	std::vector<uint8> rootDirData;
	rootDirData.resize(rootPageCount * pageSize);
	data = mData + (rootDirPtr * pageSize);

	int32* rootPages = (int32*)data;
	for (int subRootPageIdx = 0; subRootPageIdx < pageSize / 4; subRootPageIdx++, rootPageIdx++)
	{
		if (rootPageIdx >= rootPageCount)
			break;

		int rootPagePtr = rootPages[subRootPageIdx];
		if (rootPagePtr == 0)
			break;
		data = mData + (rootPagePtr * pageSize);
		memcpy(&rootDirData[rootPageIdx * pageSize], data, pageSize);
		data += pageSize;
	}

	if (!ParseCv(&rootDirData[0]))
	{
		Fail("Failed to parse PDB");
		return false;
	}

	return true;
}

bool BlPdbParser::CvParseHeader()
{
	uint8* data = CvReadStream(1);
	mCvHeaderData = data;

	int32 pdbVersion = GET(int32);
	int32 timestamp = GET(int32);
	int32 pdbAge = GET(int32);
	int8 pdbGuid[16];
	for (int i = 0; i < 16; i++)
		pdbGuid[i] = GET(int8);

	/*if ((wantAge != -1) &&
	((pdbAge != wantAge) || (memcmp(pdbGuid, wantGuid, 16) != 0)))
	{
	String msg = "PDB version did not match requested version\n";

	msg += StrFormat(" Age: %d Module GUID: ", wantAge);
	for (int i = 0; i < 16; i++)
	msg += StrFormat("%02X", (uint8)wantGuid[i]);
	msg += "\n";

	msg += StrFormat(" Age: %d PDB GUID   : ", pdbAge);
	for (int i = 0; i < 16; i++)
	msg += StrFormat("%02X", (uint8)pdbGuid[i]);
	msg += "\n";

	mDebugger->OutputMessage(msg);
	return false;
	}*/

	int nameTableIdx = -1;

	GET_INTO(int32, strTabLen);
	const char* strTab = (const char*)data;
	data += strTabLen;

	GET_INTO(int32, numStrItems);
	GET_INTO(int32, strItemMax);

	GET_INTO(int32, usedLen);
	data += usedLen * sizeof(int32);
	GET_INTO(int32, deletedLen);
	data += deletedLen * sizeof(int32);

	for (int tableIdx = 0; tableIdx < numStrItems; tableIdx++)
	{
		GET_INTO(int32, strOfs);
		GET_INTO(int32, streamNum);

		const char* tableName = strTab + strOfs;
		if (strcmp(tableName, "/names") == 0)
			mStringTable.mStream = streamNum;
	}

	return true;
}

uint8* BlPdbParser::CvReadStream(int streamIdx, int* outSize)
{
	int streamSize = mCvStreamSizes[streamIdx];
	if (outSize != NULL)
		*outSize = streamSize;
	if (streamSize <= 0)
		return NULL;
	int streamPageCount = (streamSize + mCvPageSize - 1) / mCvPageSize;

	uint8* sectionData = new uint8[streamSize];
	bool deferDeleteSectionData = false;

	int streamPtrIdx = mCvStreamPtrStartIdxs[streamIdx];
	for (int streamPageIdx = 0; streamPageIdx < streamPageCount; streamPageIdx++)
	{
		uint8* data = mData + (mCvStreamPtrs[streamPtrIdx] * mCvPageSize);
		memcpy(sectionData + streamPageIdx * mCvPageSize, data, std::min(streamSize - (streamPageIdx * mCvPageSize), mCvPageSize));
		streamPtrIdx++;
	}

	return sectionData;
}

bool BlPdbParser::ParseCv(uint8* rootDirData)
{
	uint8* data = rootDirData;

	bool failed = false;
	int numStreams = GET(int32);
	if (numStreams == 0)
		return true;

	mCvStreamSizes.resize(numStreams);
	mCvStreamPtrStartIdxs.resize(numStreams);

	int streamPages = 0;
	for (int i = 0; i < (int)mCvStreamSizes.size(); i++)
		mCvStreamSizes[i] = GET(int32);
	for (int streamIdx = 0; streamIdx < numStreams; streamIdx++)
	{
		mCvStreamPtrStartIdxs[streamIdx] = streamPages;
		if (mCvStreamSizes[streamIdx] > 0)
			streamPages += (mCvStreamSizes[streamIdx] + mCvPageSize - 1) / mCvPageSize;
	}
	mCvStreamPtrs.resize(streamPages);
	for (int i = 0; i < (int)mCvStreamPtrs.size(); i++)
		mCvStreamPtrs[i] = GET(int32);


	//////////////////////////////////////////////////////////////////////////

	if (!CvParseHeader())
		return false;

	ParseTypeData();
	ParseIPIData();

	return true;
}

void BlPdbParser::ParseTypeData()
{			
	int sectionSize = 0;
	mCvTypeSectionData = CvReadStream(2, &sectionSize);
	uint8* data = mCvTypeSectionData;
	uint8* sectionData = mCvTypeSectionData;
	
	int32 ver = GET(int32);
	int32 headerSize = GET(int32);
	int32 minVal = GET(int32);
	int32 maxVal = GET(int32);
	int32 followSize = GET(int32);

	int16 hashStream = GET(int16);
	int16 hashStreamPadding = GET(int16);
	int32 hashKey = GET(int32);
	int32 hashBucketsSize = GET(int32);
	int32 hashValsOffset = GET(int32);
	int32 hashValsSize = GET(int32);
	int32 hashTypeInfoOffset = GET(int32);
	int32 hashTypeInfoSize = GET(int32);
	int32 hashAdjOffset = GET(int32);
	int32 hashAdjSize = GET(int32);

	mTypeSource->mTPI.mCvMinTag = minVal;
	mTypeSource->mTPI.mCvMaxTag = maxVal;
	//mCvTypeMap.clear();
	//mTypeSource->mTPI.mCvTagStartMap.clear();
	//mCvTypeMap.resize(maxVal - minVal);
	mTypeSource->mTPI.mCvTagStartMap.resize(maxVal - minVal);

	//DbgDataMap dataMap(minVal, maxVal);

	mTypeSource->mTPI.mSectionData = data;
	mTypeSource->mTPI.mSectionSize = sectionSize - (int)(data - sectionData);
	mTypeSource->mTPI.ScanTypeData();
	mTypeSource->mTPI.ParseTypeData();

	if (hashAdjSize > 0)
	{
		int sectionSize = 0;
		uint8* data = CvReadStream(hashStream, &sectionSize);
		uint8* sectionData = data;

		data = sectionData + hashAdjOffset;

		GET_INTO(int32, adjustCount);
		GET_INTO(int32, unk0);
		GET_INTO(int32, unkCount);
		for (int i = 0; i < unkCount; i++)
		{
			GET_INTO(int32, unk2);
		}
		GET_INTO(int32, unkCount2);
		for (int i = 0; i < unkCount2; i++)
		{
			GET_INTO(int32, unk3);
		}

		// Types listed in the adjustment table are always primary types, 
		//  they should override any "old types" with the same name
		for (int adjIdx = 0; adjIdx < adjustCount; adjIdx++)
		{
			GET_INTO(int32, adjVal);
			GET_INTO(CV_typ_t, typeId);			
		}

		delete[] sectionData;
	}
}

void BlPdbParser::ParseIPIData()
{
	int sectionSize = 0;
	mCvIPIData = CvReadStream(4, &sectionSize);
	uint8* data = mCvIPIData;
	uint8* sectionData = data;

	int32 ver = GET(int32);
	int32 headerSize = GET(int32);
	int32 minVal = GET(int32);
	int32 maxVal = GET(int32);
	int32 followSize = GET(int32);

	int16 hashStream = GET(int16);
	int16 hashStreamPadding = GET(int16);
	int32 hashKey = GET(int32);
	int32 hashBucketsSize = GET(int32);
	int32 hashValsOffset = GET(int32);
	int32 hashValsSize = GET(int32);
	int32 hashTypeInfoOffset = GET(int32);
	int32 hashTypeInfoSize = GET(int32);
	int32 hashAdjOffset = GET(int32);
	int32 hashAdjSize = GET(int32);

	mTypeSource->CreateIPI();
	mTypeSource->mIPI->mCvMinTag = minVal;
	mTypeSource->mIPI->mCvMaxTag = maxVal;
	int recordCount = maxVal - minVal;
	mTypeSource->mIPI->mCvTagStartMap.resize(recordCount);

	int typeDataSize = sectionSize - (int)(data - sectionData);
	mTypeSource->mIPI->mSectionData = data;
	mTypeSource->mIPI->mSectionSize = typeDataSize;
	mTypeSource->mIPI->ScanTypeData();
	mTypeSource->mIPI->ParseTypeData();
}

