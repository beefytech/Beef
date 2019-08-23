#include "BlCvParser.h"
#include "BlContext.h"
#include "BlCodeView.h"
#include "codeview/cvinfo.h"
#include "../COFFData.h"
#include "BeefySysLib/util/PerfTimer.h"

USING_NS_BF;

#define CV_BLOCK_SIZE 0x1000
#define MSF_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\032DS\0\0"
#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

BlCvParser::BlCvParser(BlContext* context)
{
	mContext = context;
	mCodeView = context->mCodeView;
	mCurModule = NULL;	
	mSyms = NULL;
}

void BlCvParser::NotImpl()
{
}

void BlCvParser::Fail(const StringImpl& err)
{
	mContext->Fail(StrFormat("%s in %s", err.c_str(), mContext->ObjectDataIdxToString(mCurModule->mObjectData->mIdx).c_str()));
}

int64 BlCvParser::CvParseConstant(uint16 constVal, uint8*& data)
{
	if (constVal < LF_NUMERIC) // 0x8000
		return constVal;
	switch (constVal)
	{
	case LF_CHAR:
		return GET(int8);
	case LF_SHORT:
		return GET(int16);
	case LF_USHORT:
		return GET(uint16);
	case LF_LONG:
		return GET(int32);
	case LF_ULONG:
		return GET(uint32);
	case LF_QUADWORD:
		return GET(int64);
	case LF_UQUADWORD:
		return (int64)GET(uint64);
	default:
		BF_FATAL("Not handled");
	}
	return 0;
}

int64 BlCvParser::CvParseConstant(uint8*& data)
{
	uint16 val = GET(uint16);
	return CvParseConstant(val, data);
}

const char* BlCvParser::CvParseString(uint8*& data)
{
	const char* strStart = (const char*)data;
	int strLen = (int)strlen((const char*)data);	
	data += strLen + 1;
	if (strLen == 0)
		return NULL;
	return strStart;
}

bool BlCvParser::MapAddress(void* symbolData, void* cvLoc, BlReloc& outReloc, COFFRelocation*& nextReloc)
{	
	int posOfs = (int)((uint8*)cvLoc - (uint8*)symbolData);
	int posSect = (int)((uint8*)cvLoc + 4 - (uint8*)symbolData);

	if (nextReloc->mVirtualAddress != posOfs)
	{
		outReloc.mFlags = (BeRelocFlags)0;
		outReloc.mSymName = NULL;
		return false;
	}

	auto sym = &(*mSyms)[nextReloc->mSymbolTableIndex];
	if (sym->mSym != NULL)
	{
		outReloc.mFlags = BeRelocFlags_Sym;
		outReloc.mSym = sym->mSym;
	}
	else
	{		
		outReloc.mFlags = BeRelocFlags_Loc;
		outReloc.mSegmentIdx = sym->mSegmentIdx;
		outReloc.mSegmentOffset = sym->mSegmentOffset;
	}

	nextReloc++;
	if (nextReloc->mVirtualAddress != posSect)
		return false;
	// It MUST be the same sym	
	nextReloc++;

	return true;
}

bool BlCvParser::TryReloc(void* symbolData, void* cvLoc, int32* outVal, COFFRelocation*& nextReloc)
{
	int posOfs = (int)((uint8*)cvLoc - (uint8*)symbolData);
	if (nextReloc->mVirtualAddress != posOfs)
		return true;

	return false;
}

void BlCvParser::AddModule(BlObjectData* objectData, const char* strTab)
{
	//const char* invalidStrs[] = { "delete_scalar", "new_scalar", "throw_bad_alloc", "std_type_info_static", "delete_scalar_size", "main2" };
	//const char* invalidStrs[] = { "delete_scalar", "new_scalar" };
	/*const char* invalidStrs[] = { "delete_scalar" };
	for (const char* invalidStr : invalidStrs)
	{
		//TODO:
		if (strstr(objectData->mName, invalidStr) != NULL)
		{
			return;
		}
	}
	if (strstr(objectData->mName, "new_scalar") != NULL)
	{
		mTEMP_Testing = true;
	}*/
	
	mCurModule = mCodeView->mModuleInfo.Alloc();
	mCurModule->mObjectData = objectData;
	mCurModule->mIdx = (int)mCodeView->mModuleInfo.size() - 1;
	mCurModule->mStrTab = strTab;	
}

void BlCvParser::AddTypeData(PESectionHeader* sect)
{
	mTypeSects.push_back(sect);
}

void BlCvParser::AddSymbolData(PESectionHeader* sect)
{
	mSymSects.push_back(sect);
}

void BlCvParser::ParseTypeData(void* typeData, int size)
{
	uint8* data = (uint8*)typeData;
	uint8* dataEnd = data + size;

	int sig = GET(int32);
	if (sig != CV_SIGNATURE_C13)
	{
		Fail("Invalid debug signature");
		return;
	}

	bool useWorkList = true;

	if (*(int16*)(data + 2) == LF_TYPESERVER2)
	{
		lfTypeServer2& typeServer = *(lfTypeServer2*)(data + 2);

		String filePath = (char*)typeServer.name;
		String fixedFilePath = FixPathAndCase(filePath);
		auto itr = mCodeView->mTypeServerFiles.find(fixedFilePath);
		if (itr == mCodeView->mTypeServerFiles.end())
		{
			auto itrPair = mCodeView->mTypeServerFiles.insert(std::make_pair(fixedFilePath, (BlCvTypeSource*)NULL));
			BlPdbParser* pdbParser = new BlPdbParser();
			pdbParser->mTypeSource = new BlCvTypeSource;
			itrPair.first->second = pdbParser->mTypeSource;
			pdbParser->mTypeSource->mTypeServerLib = pdbParser;
			pdbParser->mTypeSource->Init(mCodeView);			
			
			if (FileExists(filePath))
			{
				pdbParser->mFileName = filePath;
			}
			else
			{
				String fileName = GetFileName(filePath);
				String checkFilePath;
				if (mCurModule->mObjectData->mLib != NULL)
					checkFilePath = GetFileDir(mCurModule->mObjectData->mLib->mFileName);
				else
					checkFilePath = GetFileDir(mCurModule->mObjectData->mName);
				checkFilePath += fileName;
				pdbParser->mFileName = checkFilePath;
			}			

			if (useWorkList)
			{				
				mCodeView->mTypeWorkThread.Add(pdbParser->mTypeSource);				
			}
			else
			{				
				pdbParser->Load(pdbParser->mFileName);				
			}
			
			mCurModule->mTypeSource = pdbParser->mTypeSource;
		}
		else
		{
			mCurModule->mTypeSource = itr->second;
		}

		return;
	}

	auto typeSource = new BlCvTypeSource();
	typeSource->Init(mCodeView);
	mCurModule->mTypeSource = typeSource;
	typeSource->mTPI.mSectionData = data;
	typeSource->mTPI.mSectionSize = size - 4;
	typeSource->mObjectData = mCurModule->mObjectData;
		
	if (useWorkList)
	{				
		mCodeView->mTypeWorkThread.Add(typeSource);		
	}
	else
	{		
		typeSource->mTPI.ScanTypeData();
		typeSource->mTPI.ParseTypeData();
	}
}

void BlCvParser::ParseSymbolData(void* symbolData, int size, void* relocData, int numRelocs)
{	
// 	static int itrCount = 0;
// 	itrCount++;
// 
// 	if (itrCount == 0x32)
// 	{
// 		NOP;
// 	}

	uint8* data = (uint8*)symbolData;
	uint8* symDataEnd = data + size;

	int sig = GET(int32);
	if (sig != CV_SIGNATURE_C13)
	{
		Fail("Invalid debug signature");
		return;
	}

	bool relocFailed = false;
	uint8* fileChecksumStart = NULL;
	uint8* fileChecksumEnd = NULL;	

	COFFRelocation* nextReloc = (COFFRelocation*)relocData;
	COFFRelocation* relocEnd = nextReloc + numRelocs;
	const char* strTable = NULL;

	while (data < symDataEnd)
	{
		int sectionNum = GET(int32);
		int sectionLen = GET(int32);
		uint8* sectionStart = data;
		uint8* sectionEnd = data + sectionLen;
		if (sectionNum == DEBUG_S_STRINGTABLE)
		{			
			//BF_ASSERT(mCurModule->mStrTab.size() == 0);
			//mCurModule->mStrTab.insert(mCurModule->mStrTab.begin(), (char*)sectionStart, (char*)sectionEnd);
			strTable = (char*)sectionStart;
		}
		else if (sectionNum == DEBUG_S_FILECHKSMS)
		{
			fileChecksumStart = sectionStart;
			fileChecksumEnd = sectionEnd;
		}
		data = sectionEnd;
		PTR_ALIGN(data, (uint8*)symbolData, 4);
	}
	
	//int bytesPerFile = 24;
	
	// Handle DEBUG_S_FILECHKSMS
	if (fileChecksumStart != NULL)
	{
		data = fileChecksumStart;
		uint8* sectionStart = data;
		uint8* sectionEnd = fileChecksumEnd;

		// Always at least 8 bytes per file info
		mChksumOfsToFileIdx.resize((sectionEnd - data) / 8);
		mCurModule->mFileInfos.reserve((sectionEnd - data) / 24);

		while (data < sectionEnd)
		{
			int dataOfs = (int)(data - sectionStart);

			uint32 fileTableOfs = GET(uint32);

			const char* fileName = strTable + fileTableOfs;

			uint8 hashLen = GET(uint8);
			uint8 hashType = GET(uint8);

			BlCvFileInfo fileInfo;
			fileInfo.mChksumOfs = dataOfs;
			fileInfo.mStrTableIdx = mCodeView->AddToStringTable(fileName);

			if (hashLen > 0)
			{
				BF_ASSERT(hashType == 1);
				fileInfo.mHashType = 1;
				memcpy(fileInfo.mHash, data, 16);
				data += hashLen;
			}
			else
			{
				fileInfo.mHashType = 0;
			}
			
			mChksumOfsToFileIdx[dataOfs / 8] = (int)mCurModule->mFileInfos.size();
			mCurModule->mFileInfos.push_back(fileInfo);
			PTR_ALIGN(data, sectionStart, 4);
		}
	}
	
	data = (uint8*)symbolData + 4;
	while (data < symDataEnd)
	{
		int sectionNum = GET(int32);
		int sectionLen = GET(int32);
		uint8* sectionStart = data;
		uint8* sectionEnd = data + sectionLen;

		if ((sectionNum == DEBUG_S_SYMBOLS) || (sectionNum == DEBUG_S_INLINEELINES))
		{			
			//BL_AUTOPERF("DEBUG_S_SYMBOLS");

			BlCvSymDataBlock dataBlock;
			dataBlock.mSize = (int)(sectionEnd - sectionStart);
			dataBlock.mData = sectionStart;
			dataBlock.mSourceSectOffset = (int)((uint8*)sectionStart - (uint8*)symbolData);
			dataBlock.mRelocStart = nextReloc;
			dataBlock.mOutSize = -1;

			WIN32_MEMORY_RANGE_ENTRY vAddrs = { dataBlock.mData, (SIZE_T)dataBlock.mSize };
			PrefetchVirtualMemory(GetCurrentProcess(), 1, &vAddrs, 0);

			data = sectionEnd;
			while ((nextReloc < relocEnd) && ((int)nextReloc->mVirtualAddress < (int)(sectionEnd - (uint8*)symbolData)))
				nextReloc++;

			dataBlock.mRelocEnd = nextReloc;
			if (sectionNum == DEBUG_S_SYMBOLS)
				mCurModule->mSymData.push_back(dataBlock);
			else
				mCurModule->mInlineData.push_back(dataBlock);
			PTR_ALIGN(data, (uint8*)symbolData, 4);
			continue;
		}

		if ((sectionNum & DEBUG_S_IGNORE) != 0)
		{
			data = sectionEnd;
			while ((nextReloc < relocEnd) && ((int)nextReloc->mVirtualAddress < (int)(sectionEnd - (uint8*)symbolData)))
				nextReloc++;
			PTR_ALIGN(data, (uint8*)symbolData, 4);
			continue;
		}

		switch (sectionNum)
		{	
		case DEBUG_S_LINES:
			{			
				//BL_AUTOPERF("DEBUG_S_LINES");

				CV_DebugSLinesHeader_t& lineSec = GET(CV_DebugSLinesHeader_t);

				BlReloc reloc;				
				MapAddress(symbolData, &lineSec.offCon, reloc, nextReloc);

				auto lineInfo = mCurModule->mLineInfo.Alloc();

				/*if ((reloc.mFlags & BeRelocFlags_Sym) != 0)
				{
					auto sym = mContext->ProcessSymbol(reloc.mSym);
					if (sym->mSegmentIdx >= 0)
					{
						lineInfo->mStartSegmentIdx = sym->mSegmentIdx;
						lineInfo->mStartSegmentOffset = sym->mSegmentOffset;
					}
					else
						relocFailed = true;
				}
				else*/ if ((reloc.mFlags & BeRelocFlags_Loc) != 0)
				{
					lineInfo->mStartSegmentIdx = reloc.mSegmentIdx;
					lineInfo->mStartSegmentOffset = reloc.mSegmentOffset;
				}
				else
				{
					relocFailed = true;
				}

				lineInfo->mContribBytes = lineSec.cbCon;
				

				while (data < sectionEnd)
				{
					CV_DebugSLinesFileBlockHeader_t& linesFileHeader = GET(CV_DebugSLinesFileBlockHeader_t);

					lineInfo->mLineInfoBlocks.emplace_back(BlCvLineInfoBlock());
					auto& lineInfoBlock = lineInfo->mLineInfoBlocks.back();
					lineInfoBlock.mLines.reserve(linesFileHeader.nLines);
					if ((lineSec.flags & CV_LINES_HAVE_COLUMNS) != 0)
						lineInfoBlock.mColumns.reserve(linesFileHeader.nLines);
					//BF_ASSERT(linesFileHeader.offFile % bytesPerFile == 0);
					//lineInfoBlock.mFileInfoIdx = linesFileHeader.offFile / bytesPerFile;
					lineInfoBlock.mFileInfoIdx = mChksumOfsToFileIdx[linesFileHeader.offFile / 8];

					for (int lineIdx = 0; lineIdx < linesFileHeader.nLines; lineIdx++)
					{
						CV_Line_t& srcLineData = GET(CV_Line_t);

						BlCvLine cvLine;
						cvLine.mLineNumStart = srcLineData.linenumStart;
						cvLine.mOffset = srcLineData.offset;
						lineInfoBlock.mLines.push_back(cvLine);
					}

					if ((lineSec.flags & CV_LINES_HAVE_COLUMNS) != 0)
					{
						for (int lineIdx = 0; lineIdx < linesFileHeader.nLines; lineIdx++)
						{
							CV_Column_t& srcColumnData = GET(CV_Column_t);
							lineInfoBlock.mColumns.push_back(srcColumnData.offColumnStart);
						}
					}
				}
			}
			break;
		case DEBUG_S_INLINEELINES:
			{
				// Already handled
			}
			break;
		case DEBUG_S_STRINGTABLE:
			{
				// Already handled
			}
			break;
		case DEBUG_S_FILECHKSMS:
			{
				// Already handled
			}
			break;
		default:
			NotImpl();
		}

		data = sectionEnd;
		PTR_ALIGN(data, (uint8*)symbolData, 4);
	}

	if (nextReloc != (COFFRelocation*)relocData + numRelocs)
		relocFailed = true;
	if (relocFailed)
		Fail("Failed to apply relocations to debug symbol data");
}

void BlCvParser::AddContribution(int blSectionIdx, int blSectionOfs, int size, int characteristics)
{
	if (mCurModule == NULL)
		return;

	if ((characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
	{
		if (mCurModule->mContrib.mCharacteristics == 0)
		{
			mCurModule->mContrib.mBlSectionIdx = blSectionIdx;
			mCurModule->mContrib.mBlSectionOfs = blSectionOfs;
			mCurModule->mContrib.mSize = size;
			mCurModule->mContrib.mCharacteristics = characteristics;
		}
	}

	BlCvContrib contrib;
	contrib.mModuleIdx = mCurModule->mIdx;
	contrib.mBlSectionOfs = blSectionOfs;
	contrib.mSize = size;
	contrib.mCharacteristics = characteristics;

	while (blSectionIdx >= (int)mCodeView->mContribMap.mSegments.size())
		mCodeView->mContribMap.mSegments.Alloc();
	auto contribMapSeg = mCodeView->mContribMap.mSegments[blSectionIdx];
	contribMapSeg->mContribs.push_back(contrib);
}

void BlCvParser::FinishModule(PESectionHeader* sectHeaderArr, const BfSizedVectorRef<BlObjectDataSectInfo>& sectInfos, PE_SymInfo* objSyms, const BfSizedVectorRef<BlObjectDataSymInfo>& syms)
{
	if (mCurModule == NULL)
		return;
		
	mCurModule->mObjSyms = objSyms;
	if (!syms.empty())
		mCurModule->mSymInfo.insert(mCurModule->mSymInfo.begin(), syms.begin(), syms.end());
	for (auto sectInfo : sectInfos)
		mCurModule->mSectInfos.push_back(sectInfo);
	mSyms = &syms;

	// 
	{
		BL_AUTOPERF("BlCvParser::FinishModule ParseTypeData");
		for (auto sect : mTypeSects)
		{
			ParseTypeData((uint8*)mCurModule->mObjectData->mData + sect->mPointerToRawData, sect->mSizeOfRawData);
		}
	}
	
	//
	{
		BL_AUTOPERF("BlCvParser::FinishModule ParseSymbolData");
		for (auto sect : mSymSects)
		{
			ParseSymbolData(
				(uint8*)mCurModule->mObjectData->mData + sect->mPointerToRawData, sect->mSizeOfRawData,
				(uint8*)mCurModule->mObjectData->mData + sect->mPointerToRelocations, sect->mNumberOfRelocations);
		}
	}

	mCodeView->mModuleWorkThread.Add(mCurModule);	
}
