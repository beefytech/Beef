#include "BlCodeView.h"
#include "codeview/cvinfo.h"
#include "BlContext.h"
#include "BlHash.h"
#include "../COFFData.h"
#include "../Compiler/BfAstAllocator.h"
#include "BeefySysLib/util/BeefPerf.h"
#include <direct.h>
#include <time.h>

//#define BL_AUTOPERF_CV(name) AutoPerf autoPerf##__LINE__(name);
#define BL_AUTOPERF_CV(name)

USING_NS_BF;

#define CV_BLOCK_SIZE 0x1000
#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - (origPtr)) + (alignSize - 1)) & ~(alignSize - 1) ) )

//////////////////////////////////////////////////////////////////////////

void BlCvRecordMap::Insert(const char* name, int symIdx)
{
	int hashKey = BlHash::HashStr_PdbV1(name) % 4096;
	auto entry = mAlloc.Alloc<BlCvRecordEntry>();
	entry->mRecordIndex = symIdx;
	entry->mNext = mBuckets[hashKey];
	mBuckets[hashKey] = entry;
	mNumEntries++;
}

int BlCvRecordMap::TryInsert(const char* name, const Val128& key)
{
	auto pair = mKeySet.insert(key);
	if (!pair.second)
		return -1;

	//BL_AUTOPERF_CV("BlCvRecordMap::TryInsert insert");

	int hashKey = BlHash::HashStr_PdbV1(name) % 4096;
	/*auto checkEntry = mBuckets[hashKey];
	while (checkEntry != NULL)
	{
		if (checkEntry->mKey == key)
			return -1;
		checkEntry = checkEntry->mNext;
	}*/

	auto entry = mAlloc.Alloc<BlCvRecordEntry>();
	entry->mKey = key;
	entry->mNext = mBuckets[hashKey];
	mBuckets[hashKey] = entry;
	mNumEntries++;
	entry->mRecordIndex = mCodeView->mSymRecordsWriter.GetStreamPos();
	return entry->mRecordIndex;
}


//////////////////////////////////////////////////////////////////////////

BlCvTypeWorkThread::BlCvTypeWorkThread()
{
	mTypesDone = false;
	mThreadDone = false;
}

BlCvTypeWorkThread::~BlCvTypeWorkThread()
{
	Stop();
}

void BlCvTypeWorkThread::Stop()
{
	mTypesDone = true;
	mWorkEvent.Set();
	WorkThread::Stop();
}

void BlCvTypeWorkThread::Run()
{
	// Any failure is instant-abort
	while (!mCodeView->mContext->mFailed)
	{		
		BlCvTypeSource* typeSource = NULL;

		mCritSect.Lock();
		if (!mTypeSourceWorkQueue.empty())
		{
			typeSource = mTypeSourceWorkQueue.front();
			mTypeSourceWorkQueue.pop_front();
		}
		mCritSect.Unlock();

		if (typeSource == NULL)
		{
			if (mTypesDone)
				break;

			BP_ZONE("Waiting");
			mWorkEvent.WaitFor();
			continue;
		}

		BF_ASSERT(!typeSource->mIsDone);

		if (typeSource->mTypeServerLib != NULL)
		{
			BP_ZONE("Load TypeServerLib");
			if (!typeSource->mTypeServerLib->Load(typeSource->mTypeServerLib->mFileName))
			{
				mCodeView->mContext->Fail(StrFormat("Failed to load: %s", typeSource->mTypeServerLib->mFileName.c_str()));
			}
		}
		else
		{
			BP_ZONE("Load Obj");
			typeSource->mTPI.ScanTypeData();
			typeSource->mTPI.ParseTypeData();			
		}
		typeSource->mIsDone = true;
		//typeSource->mDoneSignal.Set();
		
		// The module work thread may be waiting for this type source
		mCodeView->mModuleWorkThread.mWorkEvent.Set();
	}

	// Wake up module work thread
	mThreadDone = true;
	mCodeView->mModuleWorkThread.mWorkEvent.Set();	
}

void BlCvTypeWorkThread::Add(BlCvTypeSource* typeSource)
{
	AutoCrit autoCrit(mCritSect);
	mTypeSourceWorkQueue.push_back(typeSource);
	mWorkEvent.Set();
}

//////////////////////////////////////////////////////////////////////////

BlCvModuleWorkThread::BlCvModuleWorkThread()
{
	mModulesDone = false;
}

BlCvModuleWorkThread::~BlCvModuleWorkThread()
{
	Stop();
}

void BlCvModuleWorkThread::Stop()
{
	mModulesDone = true;
	mWorkEvent.Set();
	WorkThread::Stop();
}

void BlCvModuleWorkThread::Run()
{
	{
		BP_ZONE("Initial Waiting");
		//mCodeView->mTypeWorkThread.WaitForFinish();
	}

	// Any failure is instant-abort
	while (!mCodeView->mContext->mFailed)
	{
		BlCvModuleInfo* module = NULL;
		
		mCritSect.Lock();
		if (!mModuleWorkQueue.empty())
		{
			module = mModuleWorkQueue.front();
			if ((module->mTypeSource == NULL) || (module->mTypeSource->mIsDone))
			{
				mModuleWorkQueue.pop_front();
			}
			else
			{
				module = NULL;
			}
		}
		mCritSect.Unlock();

		if (module == NULL)
		{
			if ((mModulesDone) && (mCodeView->mTypeWorkThread.mThreadDone))
				break;

			BP_ZONE("Waiting");
			mWorkEvent.WaitFor();
			continue;
		}

		BF_ASSERT(module->mSymStreamIdx == -1);		
		mCodeView->CreateModuleStream(module);
	}

	//
	{
		BP_ZONE("Waiting for TypeThread");
		mCodeView->mTypeWorkThread.WaitForFinish();
	}

	if (!mCodeView->mContext->mFailed)
	{
		BP_ZONE("DoFinish");
		mCodeView->DoFinish();
	}
}

void BlCvModuleWorkThread::Add(BlCvModuleInfo* module)
{
	AutoCrit autoCrit(mCritSect);
	mModuleWorkQueue.push_back(module);
	mWorkEvent.Set();
}

//////////////////////////////////////////////////////////////////////////

BlCvStreamWriter::BlCvStreamWriter()
{
	mCurStream = NULL;
	mCurStream = NULL;
	mCurBlockPos = NULL;
	mCurBlockEnd = NULL;
}

void BlCvStreamWriter::Start(BlCVStream * stream)
{
	mCurStream = stream;
	BF_ASSERT(mCurStream->mBlocks.empty());
}

void BlCvStreamWriter::Continue(BlCVStream * stream)
{
	mCurStream = stream;
	if (!stream->mBlocks.empty())
	{
		mCurBlockPos = (uint8*)mMsf->mBlocks[mCurStream->mBlocks.back()]->mData;
		mCurBlockEnd = mCurBlockPos + CV_BLOCK_SIZE;
		mCurBlockPos += stream->mSize % CV_BLOCK_SIZE;
	}
}

void BlCvStreamWriter::End(bool flush)
{
	if (flush)
	{
		for (auto block : mCurStream->mBlocks)
			mMsf->FlushBlock(block);
	}

	mCurStream->mSize = GetStreamPos();
	mCurBlockPos = NULL;
	mCurBlockEnd = NULL;
	mCurStream = NULL;
}

void BlCvStreamWriter::Write(const void* data, int size)
{
	while (mCurBlockPos + size > mCurBlockEnd)
	{
		int writeBytes = (int)(mCurBlockEnd - mCurBlockPos);
		if (writeBytes > 0)
		{
			memcpy(mCurBlockPos, data, writeBytes);
			data = (uint8*)data + writeBytes;
			size -= writeBytes;
		}

		int newBlock = mMsf->Alloc();
		mCurStream->mBlocks.Add(newBlock);
		mCurBlockPos = (uint8*)mMsf->mBlocks[newBlock]->mData;
		mCurBlockEnd = mCurBlockPos + CV_BLOCK_SIZE;
	}

	if (size > 0)
	{
		memcpy(mCurBlockPos, data, size);
		mCurBlockPos += size;
	}
}


void BlCvStreamWriter::Write(ChunkedDataBuffer& buffer)
{
	int size = buffer.GetSize();
	buffer.SetReadPos(0);

	while (buffer.mReadCurPtr + size > buffer.mReadNextAlloc)
	{
		int curSize = (int)(buffer.mReadNextAlloc - buffer.mReadCurPtr);
		if (curSize > 0)
			Write(buffer.mReadCurPtr, curSize);

		buffer.NextReadPool();
		size -= curSize;
	}

	Write(buffer.mReadCurPtr, size);
	buffer.mReadCurPtr += size;
}

int BlCvStreamWriter::GetStreamPos()
{
	return (int)((mCurStream->mBlocks.size() - 1)*CV_BLOCK_SIZE + (mCurBlockPos - (mCurBlockEnd - CV_BLOCK_SIZE)));
}

void BlCvStreamWriter::StreamAlign(int align)
{
	PTR_ALIGN(mCurBlockPos, mCurBlockEnd - CV_BLOCK_SIZE, align);
}

void* BlCvStreamWriter::GetStreamPtr(int pos)
{
	int blockNum = mCurStream->mBlocks[pos / CV_BLOCK_SIZE];
	auto data = (uint8*)mMsf->mBlocks[blockNum]->mData;
	return (uint8*)data + (pos % CV_BLOCK_SIZE);
}

//////////////////////////////////////////////////////////////////////////

BlCvStreamReader::BlCvStreamReader()
{
	mMsf = NULL;
	mBlockIdx = -1;
	mCurBlockPos = NULL;
	mCurBlockEnd = NULL;
}

void BlCvStreamReader::Open(BlCVStream* stream)
{
	mCurStream = stream;
	if (!mCurStream->mBlocks.empty())
	{
		mBlockIdx = 0;
		mCurBlockPos = (uint8*)mMsf->mBlocks[stream->mBlocks[mBlockIdx]]->mData;
		mCurBlockEnd = mCurBlockPos + CV_BLOCK_SIZE;
	}
	else
	{
		mBlockIdx = -1;
		mCurBlockPos = NULL;
		mCurBlockEnd = NULL;
	}
}

// Pass NULL to data to make sure we have a mutable pointer into the stream.  This can
//  only work if we know the alignment of a stream and we are reading an aligned member
//  with a size less than or equal to the stream's alignment
void* BlCvStreamReader::ReadFast(void* data, int size)
{
	if (mCurBlockPos + size <= mCurBlockEnd)
	{
		// The FAST case, just return a pointer
		void* ptr = mCurBlockPos;
		mCurBlockPos += size;
		return ptr;
	}

	void* ptr = data;
	while (mCurBlockPos + size > mCurBlockEnd)
	{
		int readBytes = (int)(mCurBlockEnd - mCurBlockPos);
		if (readBytes > 0)
		{
			memcpy(data, mCurBlockPos, readBytes);
			data = (uint8*)data + readBytes;
			size -= readBytes;
		}

		mBlockIdx++;
		mCurBlockPos = (uint8*)mMsf->mBlocks[mCurStream->mBlocks[mBlockIdx]]->mData;
		mCurBlockEnd = mCurBlockPos + CV_BLOCK_SIZE;
	}

	// We had to load new data but nothing got read in yet
	if (ptr == data)
	{
		void* ptr = mCurBlockPos;
		mCurBlockPos += size;
		return ptr;
	}

	if (size > 0)
	{
		memcpy(data, mCurBlockPos, size);
		mCurBlockPos += size;
	}

	return ptr;
}

void BlCvStreamReader::Seek(int size)
{	
	while (mCurBlockPos + size > mCurBlockEnd)
	{
		int readBytes = (int)(mCurBlockEnd - mCurBlockPos);
		size -= readBytes;
		mBlockIdx++;
		mCurBlockPos = (uint8*)mMsf->mBlocks[mCurStream->mBlocks[mBlockIdx]]->mData;
		mCurBlockEnd = mCurBlockPos + CV_BLOCK_SIZE;
	}	
	mCurBlockPos += size;	
}

int BlCvStreamReader::GetStreamPos()
{
	return (int)(mBlockIdx*CV_BLOCK_SIZE + (mCurBlockPos - (mCurBlockEnd - CV_BLOCK_SIZE)));
}

//////////////////////////////////////////////////////////////////////////

#pragma comment(lib, "rpcrt4.lib")

BlCodeView::BlCodeView()
{
	//mTagBufPtr = NULL;

	mSymRecordsStream = -1;
	mStat_TypeMapInserts = 0;
	mStat_ParseTagFuncs = 0;
	
	mSectStartFilePos = -1;	
	for (int i = 0; i < 5; i++)
		mStreams.Alloc();
	

	memset(mSignature, 0, sizeof(mSignature));
	//uint32 sig[] = { 0x8BB4B1E6, 0x32C63441, 0xB5BD0FDC, 0x8206881A };
	//uint32 sig[] = { 0x00000000, 0x00000001, 0x00000000, 0x00000000 };
	//memcpy(mSignature, sig, 16);			
	UuidCreate((UUID*)&mSignature);
	
	mAge = 1;

	mGlobals.mCodeView = this;
	
	mTypeWorkThread.mCodeView = this;
	mModuleWorkThread.mCodeView = this;
	
	mWriter.mMsf = &mMsf;
	mSymRecordsWriter.mMsf = &mMsf;

	AddToStringTable("");
}

BlCodeView::~BlCodeView()
{
	for (auto pair : mTypeServerFiles)
		delete pair.second;
}

void BlCodeView::Fail(const StringImpl& err)
{	
	mContext->Fail(err);
}

char* BlCodeView::StrDup(const char* str)
{
	int len = (int)strlen(str);
	char* newStr = (char*)mAlloc.AllocBytes(len + 1);
	memcpy(newStr, str, len + 1);
	return newStr;
}

void BlCodeView::NotImpl()
{
	BF_FATAL("NotImpl");
}

void BlCodeView::FixSymAddress(void* oldDataStart, void* oldDataPos, void* outDataPos, BlCvModuleInfo* module, BlCvSymDataBlock* dataBlock, COFFRelocation*& nextReloc)
{
	//BL_AUTOPERF_CV("BlCodeView::FixSymAddress");

	int posOfs = (int)((uint8*)oldDataPos - (uint8*)oldDataStart) + dataBlock->mSourceSectOffset;
	int posSect = posOfs + 4;

	if ((nextReloc->mVirtualAddress != posOfs) || (nextReloc == dataBlock->mRelocEnd))
		return;

	auto objSym = &module->mSymInfo[nextReloc->mSymbolTableIndex];

	int segmentIdx = -1;
	int segmentOffset = -1;

	if (objSym->mSym == NULL)
	{
		segmentIdx = objSym->mSegmentIdx;
		segmentOffset = objSym->mSegmentOffset;
	}
	else
	{		
		auto sym = objSym->mSym;
		BF_ASSERT(sym != NULL);
		if (sym != NULL)
		{	
			if (sym->mKind == BlSymKind_WeakSymbol)			
				sym = mContext->ProcessSymbol(sym);			

			if (sym->mSegmentIdx >= 0)
			{
				segmentIdx = sym->mSegmentIdx;
				segmentOffset = sym->mSegmentOffset;				
			}
			else if (sym->mKind == BlSymKind_ImageBaseRel)
			{
				// Leave zeros
			}
			else if (sym->mKind == BlSymKind_Absolute)
			{
				*(int32*)(outDataPos) += sym->mParam;
				*((int16*)outDataPos + 2) += (int)mContext->mOutSections.size(); // One past end is 'Abs' section
			}
			else if (sym->mKind == BlSymKind_ImportImp)
			{
				int segmentIdx = mContext->mIDataSeg->mSegmentIdx;
				auto lookup = mContext->mImportLookups[sym->mParam];
				int segmentOffset = lookup->mIDataIdx * 8;

				/*auto segment = mContext->mSegments[segmentIdx];				
				auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
				*(int32*)(outDataPos) += (segment->mRVA - outSection->mRVA) + segmentOffset;
				*((int16*)outDataPos + 2) += outSection->mIdx + 1;*/
			}
			else
			{
				// Invalid address
				BF_FATAL("Invalid address");
			}
		}
	}

	if (segmentIdx != -1)
	{
		auto segment = mContext->mSegments[segmentIdx];
		auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
		*(int32*)(outDataPos) += (segment->mRVA - outSection->mRVA) + segmentOffset;
		*((int16*)outDataPos + 2) += outSection->mIdx + 1;
	}

	nextReloc++;
	if (nextReloc->mVirtualAddress != posSect)
		Fail("Symbol remap failure");
	// It MUST be the same sym	
	nextReloc++;
}


int BlCodeView::StartStream(int streamIdx)
{
	BF_ASSERT(mWriter.mCurStream == NULL);

	BF_ASSERT(streamIdx <= 5);
	if (streamIdx == -1)
	{
		streamIdx = (int)mStreams.size();
		mStreams.Alloc();
	}

	mWriter.Start(mStreams[streamIdx]);	
	return streamIdx;
}

void BlCodeView::StartUnnamedStream(BlCVStream& stream)
{
	mWriter.Start(&stream);	
}

void BlCodeView::EndStream(bool flush)
{	
	mWriter.End(flush);
}

void BlCodeView::FlushStream(BlCVStream* stream)
{
	for (auto block : stream->mBlocks)
		mMsf.FlushBlock(block);
}

void BlCodeView::CvEncodeString(const StringImpl& str)
{
	mWriter.Write((void*)str.c_str(), (int)str.length() + 1);
}

int BlCodeView::AddToStringTable(const StringImpl&str)
{
	auto pairVal = mStrTabMap.insert(std::make_pair(str, -1));
	if (pairVal.second)
	{
		pairVal.first->second = (int)mStrTab.length();		
		mStrTab.Append(str.c_str(), str.length());
	}
	return pairVal.first->second;
}

void BlCodeView::WriteStringTable(const StringImpl&strTab, std::unordered_map<String, int>& strTabMap)
{
	mWriter.WriteT((int32)0xEFFEEFFE);
	mWriter.WriteT(1); // Version
	mWriter.WriteT((int32)strTab.length() + 1);
	mWriter.Write((char*)strTab.c_str(), (int)strTab.length() + 1);

	int bucketCount = (int)(strTabMap.size() * 1.25) + 1;
	std::vector<uint32> buckets;
	buckets.resize(bucketCount);

	mWriter.WriteT(bucketCount);
	for (auto pair : mStrTabMap)
	{
		int idx = pair.second;
		uint32 hash = BlHash::HashStr_PdbV1(pair.first.c_str());
		for (int ofs = 0; ofs < bucketCount; ofs++)
		{
			int slot = (hash + ofs) % bucketCount;
			if (slot == 0)
				continue; // 0 is reserved
			if (buckets[slot] != 0)
				continue;
			buckets[slot] = idx;
			break;
		}
	}
	mWriter.Write((void*)&buckets[0], (int)buckets.size() * 4);
	mWriter.WriteT((int)strTabMap.size());
}

void BlCodeView::GetOutSectionAddr(int segIdx, int segOfs, uint16& cvSectIdx, long& cvSectOfs)
{
	auto segment = mContext->mSegments[segIdx];
	auto outSect = mContext->mOutSections[segment->mOutSectionIdx];
	cvSectIdx = outSect->mIdx + 1;
	cvSectOfs = (segment->mRVA - outSect->mRVA) + segOfs;
}

void BlCodeView::StartSection(int sectionNum)
{
	mSectStartFilePos = mWriter.GetStreamPos();	
	BF_ASSERT((mSectStartFilePos % 4) == 0);
	mWriter.WriteT((int32)sectionNum);
	mWriter.WriteT((int32)0); // Temporary - size
}

int BlCodeView::EndSection()
{	
	int totalLen = mWriter.GetStreamPos() - mSectStartFilePos - 8;
	*((int32*)mWriter.GetStreamPtr(mSectStartFilePos + 4)) = totalLen;
	return totalLen + 8;
}

bool BlCodeView::Create(const StringImpl& fileName)
{
	if (!mMsf.Create(fileName))
		return false;

	mSymRecordsStream = StartStream();
	mSymRecordsWriter.Start(mWriter.mCurStream);
	EndStream(false);

	CreateLinkerModule();

	return true;
}

void BlCodeView::StartWorkThreads()
{
	mTypeWorkThread.Start();
	mModuleWorkThread.Start();
}

void BlCodeView::StopWorkThreads()
{
	BL_AUTOPERF_CV("BlCodeView::StopWorkThread");

	mTypeWorkThread.Stop();
	mModuleWorkThread.Stop();
}

void BlCodeView::CreatePDBInfoStream()
{
	//PDB Info Stream
	StartStream(1);
	mWriter.WriteT((int32)20000404); //VC70
	mWriter.WriteT((int32)mContext->mTimestamp);
	mWriter.WriteT((int32)mAge);
	mWriter.WriteT(mSignature);

	int strCount = 0;
	String namesStr;
	for (auto& stream : mStreams)
	{
		if (!stream->mName.empty())
		{
			namesStr += stream->mName;
			namesStr.Append((char)0);
			strCount++;
		}
	}

	// Named string table
	mWriter.WriteT((int32)namesStr.length());
	mWriter.Write(namesStr.c_str(), (int)namesStr.length());
	mWriter.WriteT((int32)strCount); // actual item count
	mWriter.WriteT((int32)strCount * 2); // max item count.  What should this be?
	mWriter.WriteT((int32)1); // usedLength
	int32 usedBits = 0;
	// This is supposed to be hashed but we just fill in the correct number of bits.	
	for (int i = 0; i < strCount; i++)
		usedBits |= 1 << i;
	mWriter.WriteT((int32)usedBits);
	mWriter.WriteT((int32)0); // deletedLength

	int strTabIdx = 0;	
	for (int streamIdx = 0; streamIdx < (int)mStreams.size(); streamIdx++)
	{
		auto& stream = mStreams[streamIdx];
		if (!stream->mName.empty())
		{
			mWriter.WriteT((int32)strTabIdx);
			mWriter.WriteT((int32)streamIdx);
			strTabIdx += (int)stream->mName.length() + 1;			
		}
	}

	// Features info
	mWriter.WriteT((int32)0);
	mWriter.WriteT((int32)20140508); // PdbImplVC140

	EndStream();
}

void BlCodeView::WriteTypeData(int streamId, BlTypeInfo& typeInfo)
{
	int hashStream = StartStream();
	int pos = 0;
	uint8 tempData[0x10002];

	int hashBucketsSize = 0x3FFFF;
	int numTags = typeInfo.mCurTagId - 0x1000;

	struct _JumpEntry
	{
		int mTypeIdx;
		int mOfs;
	};

	Array<_JumpEntry> indexEntries;	
	uint8* curChunk = NULL;

	typeInfo.mData.SetReadPos(0);
	for (int tagId = 0x1000; tagId < typeInfo.mCurTagId; tagId++)
	{		
		// Align
		typeInfo.mData.mReadCurPtr = (uint8*)(((intptr)typeInfo.mData.mReadCurPtr + 3) & ~3);

		// Chunks are every 8k, which is perfect for the jump table
		if (curChunk != typeInfo.mData.mReadCurAlloc)
		{
			indexEntries.Add({ tagId, typeInfo.mData.GetReadPos() });
			curChunk = typeInfo.mData.mReadCurAlloc;
		}

		uint8* dataHead = (uint8*)typeInfo.mData.FastRead(tempData, 4);
		// Pointer not valid
		uint8* data = dataHead;
		int trLength = GET(uint16);
		if (dataHead == tempData)
		{			
			typeInfo.mData.Read(tempData + 4, trLength - 2);
		}
		else
		{
			uint8* leafData = (uint8*)typeInfo.mData.FastRead(tempData + 4, trLength - 2);
			if (leafData != dataHead + 4)
			{
				memcpy(tempData, dataHead, 4);
				dataHead = tempData;
			}
		}

		data = dataHead;
		int32 hashVal = BlHash::GetTypeHash(data) % hashBucketsSize;
		mWriter.WriteT(hashVal);
		
		//typeInfo.mData.TryGetPtr
	}

	mWriter.Write(&indexEntries[0], (int)indexEntries.size() * 8);

	// Adjuster
	/*mWriter.WriteT(0); // Adjust count
	mWriter.WriteT(0); // Adjust capacity
	mWriter.WriteT(0); // Used len
	mWriter.WriteT(0); // Deleted len*/

	EndStream();

	int hashValsSize = numTags * 4;
	int hashTypeInfoSize = (int)indexEntries.size() * 8;
	int adjusterSize = 0;

	StartStream(streamId);
	mWriter.WriteT(0x0131ca0b); // ver
	mWriter.WriteT(56); // headerSize
	mWriter.WriteT(0x1000); // minVal
	mWriter.WriteT(typeInfo.mCurTagId); // maxVal
	mWriter.WriteT(typeInfo.mData.GetSize()); // followSize - should be total section size - headerSize(56)
	mWriter.WriteT((int16)hashStream); // hashStream
	mWriter.WriteT((int16)-1); // hashStreamPadding
	mWriter.WriteT(4); // hashKeySize
	mWriter.WriteT(hashBucketsSize); // hashBucketsSize
	mWriter.WriteT(0); // hashValsOffset
	mWriter.WriteT(hashValsSize); // hashValsSize
	mWriter.WriteT(hashValsSize); // hashTypeInfoOffset
	mWriter.WriteT(hashTypeInfoSize); // hashTypeInfoSize
	mWriter.WriteT(hashValsSize + hashTypeInfoSize); // hashAdjOffset
	mWriter.WriteT(adjusterSize); // hashAdjSize
	typeInfo.mData.SetReadPos(0);	
	mWriter.Write(typeInfo.mData);
	EndStream();
}

void BlCodeView::CreateTypeStream()
{	
	//BL_AUTOPERF_CV("BlCodeView::CreateTypeStream");
	BP_ZONE("BlCodeView::CreateTypeStream");
	WriteTypeData(2, mTPI);	
}

void BlCodeView::CreateIPIStream()
{	
	//BL_AUTOPERF_CV("BlCodeView::CreateIPIStream");
	BP_ZONE("BlCodeView::CreateIPIStream");
	WriteTypeData(4, mIPI);	
}

void BlCodeView::WriteRecordMap(BlCvRecordMap* recordMap)
{
	mWriter.WriteT((int32)-1); // verSig
	mWriter.WriteT((int32)0xf12f091a); // verHdr

	mWriter.WriteT((int32)(recordMap->mNumEntries * 4 * 2)); // sizeHr

	int bucketsCount = 0;
	for (int hashKey = 0; hashKey < 4096; hashKey++)
		if (recordMap->mBuckets[hashKey] != NULL)
			bucketsCount++;
	mWriter.WriteT((int32)(0x81 * 4 + bucketsCount * 4)); // sizeBuckets

	uint32 bucketBits[0x80] = { 0 };
	int32 buckets[4096];
	int bucketIdx = 0;

	// HR
	int hrIdx = 0;
	for (int hashKey = 0; hashKey < 4096; hashKey++)
	{
		auto checkEntry = recordMap->mBuckets[hashKey];
		if (checkEntry == NULL)
			continue;
		bucketBits[hashKey / 32] |= 1 << (hashKey % 32);
		buckets[bucketIdx++] = hrIdx;
		while (checkEntry != NULL)
		{
			mWriter.WriteT(checkEntry->mRecordIndex + 1);
			mWriter.WriteT(1);
			checkEntry = checkEntry->mNext;
			hrIdx++;
		}
	}

	for (int i = 0; i < 0x80; i++)
		mWriter.WriteT((int32)bucketBits[i]);
	mWriter.WriteT(0);

	for (int i = 0; i < bucketIdx; i++)
		mWriter.WriteT(buckets[i] * 12);
}

int BlCodeView::CreateGlobalStream()
{
	BP_ZONE("BlCodeView::CreateGlobalStream");

	int globalStreamIdx = StartStream();

	WriteRecordMap(&mGlobals);
	
	EndStream();
	return globalStreamIdx;
}

int BlCodeView::CreatePublicStream()
{
	BP_ZONE("BlCodeView::CreatePublicStream");

	int publicStreamIdx = StartStream();
	
	BlCvRecordMap publics;
	
	struct _SortEntry
	{
		uint64 mAddr;
		int mRecordIdx;
	};

	Array<_SortEntry> addrMap;
	addrMap.Reserve(mContext->mSymTable.mMap.size());

	for (auto symPair : mContext->mSymTable.mMap)
	{
		auto sym = symPair.second;
		if ((sym->mSegmentIdx >= 0) || (sym->mKind == BlSymKind_Absolute))
		{	
			bool isAbs = sym->mKind == BlSymKind_Absolute;

#define ADDR_FLAG_ABS 0x8000000000000000L

			auto segment = (isAbs) ? (BlSegment*)NULL : mContext->mSegments[sym->mSegmentIdx];
			

			int pubSymLen = (int)offsetof(PUBSYM32, name);
			int nameSize = (int)strlen(sym->mName) + 1;

			PUBSYM32 pubSym = { 0 };
			pubSym.reclen = pubSymLen + nameSize - 2;
			pubSym.rectyp = S_PUB32;

			if (isAbs)
			{
				pubSym.seg = (int)mContext->mOutSections.size(); // One past end is 'Abs' section
				pubSym.off = sym->mSegmentOffset;
			}
			else
			{
				auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
				if ((outSection->mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
				{
					pubSym.pubsymflags.fFunction = 1;
					pubSym.pubsymflags.fCode = 1;
				}

				pubSym.off = (segment->mRVA - outSection->mRVA) + sym->mSegmentOffset;
				pubSym.seg = outSection->mIdx + 1;
			}
						
			int addBytes = ((nameSize + 2 + 3) & ~3) - (nameSize + 2);
			pubSym.reclen += addBytes;

			int idx = mSymRecordsWriter.GetStreamPos();
			if (isAbs)
				addrMap.Add({ ADDR_FLAG_ABS + sym->mSegmentOffset, idx});
			else
				addrMap.Add({ (uint64)(segment->mRVA + sym->mSegmentOffset), idx });

			mSymRecordsWriter.Write(&pubSym, pubSymLen);
			mSymRecordsWriter.Write(sym->mName, nameSize);
			if (addBytes > 0)
			{
				int zero = 0;
				mSymRecordsWriter.Write(&zero, addBytes);
			}

			publics.Insert(sym->mName, idx);			
		}
	}

	std::sort(addrMap.begin(), addrMap.end(), [](const _SortEntry& lhs, const _SortEntry& rhs) { return lhs.mAddr < rhs.mAddr; });

	int bucketsCount = 0;
	for (int hashKey = 0; hashKey < 4096; hashKey++)
		if (publics.mBuckets[hashKey] != NULL)
			bucketsCount++;
	int symHashSize = 16 + (int32)(publics.mNumEntries * 4 * 2) + (int32)(0x81 * 4 + bucketsCount * 4);
	int addrHashSize = (int)addrMap.size() * 4;

	mWriter.WriteT(symHashSize); // symHashSize
	mWriter.WriteT(addrHashSize); // addrMapSize
	mWriter.WriteT(0); // thunkCount
	mWriter.WriteT(0); // thunkSize
	mWriter.WriteT(0); // thunkTableStream
	mWriter.WriteT(0); // thunkTableOfs
	mWriter.WriteT(0); // thunkSectCount

	int symHashPos = mWriter.GetStreamPos();
	WriteRecordMap(&publics);
	BF_ASSERT(mWriter.GetStreamPos() - symHashPos == symHashSize);

	for (auto& addrMapEntry : addrMap)
	{
		mWriter.WriteT(addrMapEntry.mRecordIdx);
	}

	EndStream();
	return publicStreamIdx;
}

void BlCodeView::FinishSymRecords()
{
	BP_ZONE("BlCodeView::FinishSymRecords");

	BlCvStreamReader reader;
	reader.mMsf = &mMsf;
	reader.Open(mStreams[mSymRecordsStream]);
	
	auto itr = mSymRecordDeferredPositions.begin();
	auto endItr = mSymRecordDeferredPositions.end();

	int wantPos = mSymRecordsWriter.GetStreamPos();
	int curPos = 0;
	while (itr != endItr)
	{
		int wantOfs = *itr;
		reader.Seek(wantOfs - curPos);
		curPos = wantOfs;

		int16 hdrBuf[2];
		int16* hdr = (int16*)reader.ReadFast(&hdrBuf, 4);
		int16 symLen = hdr[0];
		int16 symType = hdr[1];

		int wantSeekBytes = symLen - 2;

		int addrOfs;
		switch (symType)
		{		
		case S_LPROC32:
		case S_GPROC32:
			addrOfs = offsetof(PROCSYM32, off);
			break;
		case S_LTHREAD32:
		case S_GTHREAD32:
			addrOfs = offsetof(THREADSYM32, off);
			break;
		case S_LDATA32:
		case S_GDATA32:
			addrOfs = offsetof(DATASYM32, off);
			break;
		default:
			NotImpl();
			break;
		}
		
		reader.Seek(addrOfs - 4);
		int32* ofsPtr = (int32*)reader.ReadFast(NULL, 4);
		int16* segPtr = (int16*)reader.ReadFast(NULL, 2);

		auto segment = mContext->mSegments[*segPtr];
		auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
		*ofsPtr += segment->mRVA - outSection->mRVA;
		*segPtr = outSection->mIdx + 1;

		reader.Seek(wantSeekBytes - addrOfs - 2);
		curPos += 2 + symLen;

		BF_ASSERT(curPos == reader.GetStreamPos());
		++itr;
	}

	mSymRecordsWriter.End();
}

/*int BlCodeView::CreateSymRecordStream()
{
	int symRecordStreamIdx = StartStream();
	//mSymRecords.Read(mFS, mSymRecords.GetSize());
	//mWriter.Write(mSymRecords);
	
	EndStream();
	return symRecordStreamIdx;
}*/

int BlCodeView::CreateSectionHeaderStream()
{
	int symRecordStreamIdx = StartStream();
	mWriter.Write(mSectionHeaders.GetPtr(), mSectionHeaders.GetSize());
	EndStream();
	return symRecordStreamIdx;
}

void BlCodeView::CreateDBIStream()
{
	int globalStreamIdx = CreateGlobalStream();
	int publicStreamIdx = CreatePublicStream();
	//int symRecordStreamIdx = CreateSymRecordStream();
	int symRecordStreamIdx = mSymRecordsStream;
	int sectionHeaderStreamIdx = CreateSectionHeaderStream();

	StartStream(3);	

	mWriter.WriteT((int32)-1); // VersionSignature
	mWriter.WriteT((int32)19990903); // VersionHeader V70
	mWriter.WriteT((int32)mAge);
	mWriter.WriteT((int16)globalStreamIdx);
	//int buildNum = (1 << 15) | (12 << 8) | (0); // Fake as 'NewVersionFormat' 12.0 (MSVC 2013)
	mWriter.WriteT((int16)0x8e00);
	mWriter.WriteT((int16)publicStreamIdx);
	mWriter.WriteT((int16)0x5e92); // PdbDllVersion
	mWriter.WriteT((int16)symRecordStreamIdx);
	mWriter.WriteT((int16)0); // PdbDllRbld
	
	int substreamSizesPos = mWriter.GetStreamPos();
	int32* sizePtrs = (int32*)mWriter.mCurBlockPos;
	mWriter.WriteT((int32)0); // ModInfoSize	
	mWriter.WriteT((int32)0); // SectionContributionSize
	mWriter.WriteT((int32)0); // SectionMapSize
	mWriter.WriteT((int32)0); // SourceInfoSize
	mWriter.WriteT((int32)0); // TypeServerSize
	mWriter.WriteT((int32)0); // MFCTypeServerIndex
	mWriter.WriteT((int32)0); // OptionalDbgHeaderSize
	mWriter.WriteT((int32)0); // ECSubstreamSize
		
	int16 flags = 0; // WasIncrementallyLinked, ArePrivateSymbolsStripped, HasConfictingTypes (?)
	mWriter.WriteT((int16)flags);
	mWriter.WriteT((int16)0x8664); // Machine type
	mWriter.WriteT((int32)0); // Padding
	
	struct _SectionContribEntry
	{
		uint32 mSectionIdx;		
		int32 mOffset;
		int32 mSize;
		uint32 mCharacteristics;
		uint32 mModuleIdx;		
		uint32 mDataCrc;
		uint32 mRelocCrc;
	};

	//////////////////////////////////////////////////////////////////////////
	// ModuleInfo
	int curPos = mWriter.GetStreamPos();
	for (auto moduleInfo : mModuleInfo)
	{
		mWriter.WriteT((int32)0); // Unused
				
		_SectionContribEntry contribEntry = { 0 };
		if (!moduleInfo->mContrib.mCharacteristics != 0)
		{
			auto& contrib = moduleInfo->mContrib;
			auto blSection = mContext->mSegments[contrib.mBlSectionIdx];
			auto outSection = mContext->mOutSections[blSection->mOutSectionIdx];
			
			contribEntry.mCharacteristics = contrib.mCharacteristics;
			contribEntry.mSectionIdx = outSection->mIdx + 1;
			contribEntry.mOffset = (blSection->mRVA - outSection->mRVA) + contrib.mBlSectionOfs;
			contribEntry.mSize = contrib.mSize;
			contribEntry.mModuleIdx = moduleInfo->mIdx;
			//TODO: DataCRC, RelocCRC			
		}
		mWriter.WriteT(contribEntry);

		int16 flags = 0; // 
		mWriter.WriteT(flags);
		mWriter.WriteT((int16)moduleInfo->mSymStreamIdx);		
		int symSize = 4;
		for (auto& block : moduleInfo->mSymData)
		{
			BF_ASSERT(block.mOutSize >= 0);
			symSize += block.mOutSize;
		}
		BF_ASSERT(symSize % 4 == 0);
		mWriter.WriteT(symSize);
		mWriter.WriteT(0); // C11-style LineInfo size
		mWriter.WriteT(moduleInfo->mLineInfoSize); //C13-style LineInfo size
		mWriter.WriteT((int32)moduleInfo->mFileInfos.size()); // Source file count
		mWriter.WriteT((int32)0); // Unusued
		mWriter.WriteT((int32)0); // SourceFileNameIndex

		// What about special "* Linker *" module? The only case this should be non-zero?
		mWriter.WriteT((int32)0); // PdbFilePathNameIndex

		if (!moduleInfo->mName.empty())
			CvEncodeString(moduleInfo->mName);
		else
			CvEncodeString(moduleInfo->mObjectData->mName);
		if (moduleInfo->mObjectData != NULL)
			CvEncodeString(moduleInfo->mObjectData->mName);
		else
			mWriter.WriteT((uint8)0);
		mWriter.StreamAlign(4);
	}
	int32 modInfoSize = mWriter.GetStreamPos() - curPos;

	//////////////////////////////////////////////////////////////////////////
	// SectionContributionSize
	curPos = mWriter.GetStreamPos();
	mWriter.WriteT((int32)(0xeffe0000 + 19970605)); // Ver60	
	for (auto& outSection : mContext->mOutSections)
	{		
		for (auto& segment : outSection->mSegments)		
		{
			if (segment->mSegmentIdx >= mContribMap.mSegments.size())
				continue;

			for (auto& contrib : mContribMap.mSegments[segment->mSegmentIdx]->mContribs)
			{
				_SectionContribEntry contribEntry = { 0 };				
				contribEntry.mCharacteristics = contrib.mCharacteristics;
				contribEntry.mSectionIdx = outSection->mIdx + 1;
				contribEntry.mOffset = (segment->mRVA - outSection->mRVA) + contrib.mBlSectionOfs;
				contribEntry.mSize = contrib.mSize;
				contribEntry.mModuleIdx = contrib.mModuleIdx;

				mWriter.WriteT(contribEntry);
			}
		}
	}
	int32 sectionContributionSize = mWriter.GetStreamPos() - curPos;

	//////////////////////////////////////////////////////////////////////////
	// SectionMap
	curPos = mWriter.GetStreamPos();
	mWriter.WriteT((int16)mContext->mOutSections.size()); // Section count
	mWriter.WriteT((int16)mContext->mOutSections.size()); // Log section count
	for (auto outSection : mContext->mOutSections)
	{
		int16 flags = (1 << 3) | (1 << 8); // AddressIs32Bit, IsSelector
		if ((outSection->mCharacteristics & IMAGE_SCN_MEM_READ) != 0)
			flags |= (1 << 0);
		if ((outSection->mCharacteristics & IMAGE_SCN_MEM_WRITE) != 0)
			flags |= (1 << 1);
		if ((outSection->mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
			flags |= (1 << 2);
		mWriter.WriteT(flags);
		mWriter.WriteT((int16)0); // Logical overlay number - ?
		mWriter.WriteT((int16)0); // Group
		mWriter.WriteT((int16)(outSection->mIdx + 1)); // Frame
		mWriter.WriteT((int16)-1); // Section name string idx
		mWriter.WriteT((int16)-1); // Class name string idx
		mWriter.WriteT((int32)0); // Offset
		mWriter.WriteT((int32)outSection->mVirtualSize); // SectionLength
	}
	int32 sectionMapSize = mWriter.GetStreamPos() - curPos;

	//////////////////////////////////////////////////////////////////////////
	// FileInfo
	curPos = mWriter.GetStreamPos();
	mWriter.WriteT((int16)mModuleInfo.size());
	int numSourceFiles = 0;
	for (auto module : mModuleInfo)
		numSourceFiles += (int)module->mFileInfos.size();
	mWriter.WriteT((int16)numSourceFiles); // NumSourceFiles - this is ignored now since it would only allow 64k files
	// ModIndices
	int curFileIdx = 0;
	for (auto module : mModuleInfo)
	{
		mWriter.WriteT((int16)curFileIdx);
		curFileIdx += (int)module->mFileInfos.size();
	}
	// ModFileCounts
	for (auto module : mModuleInfo)	
		mWriter.WriteT((int16)module->mFileInfos.size());
	
	int strIdx = 0;
	for (auto module : mModuleInfo)
	{
		for (auto& cvFileInfo : module->mFileInfos)
		{
			char* str = (char*)mStrTab.c_str() + cvFileInfo.mStrTableIdx;			
			mWriter.WriteT((int32)strIdx);
			strIdx += (int)strlen(str) + 1;
		}
	}
	
	for (auto module : mModuleInfo)
	{
		for (auto& cvFileInfo : module->mFileInfos)
		{
			char* str = (char*)mStrTab.c_str() + cvFileInfo.mStrTableIdx;
			mWriter.Write(str, (int)strlen(str) + 1);
		}		
	}
	mWriter.StreamAlign(4);
	int32 fileInfoSize = mWriter.GetStreamPos() - curPos;
	
	//////////////////////////////////////////////////////////////////////////
	// Type Server
	curPos = mWriter.GetStreamPos();
	int typeServerSize = mWriter.GetStreamPos() - curPos;
	
	//////////////////////////////////////////////////////////////////////////
	// EC
	curPos = mWriter.GetStreamPos();
	String ecStrTab;
	std::unordered_map<String, int> ecStrTabMap;
	WriteStringTable(ecStrTab, ecStrTabMap);
	int ecSize = mWriter.GetStreamPos() - curPos;

	//////////////////////////////////////////////////////////////////////////
	// Optional debug header	
	curPos = mWriter.GetStreamPos();
	mWriter.WriteT((int16)-1); // fpo
	mWriter.WriteT((int16)-1); // exception
	mWriter.WriteT((int16)-1); // fixup
	mWriter.WriteT((int16)-1); // omap_to_src
	mWriter.WriteT((int16)-1); // omap_from_src
	mWriter.WriteT((int16)sectionHeaderStreamIdx); // section_header 
	mWriter.WriteT((int16)-1); // token_rid_map
	mWriter.WriteT((int16)-1); // x_data
	mWriter.WriteT((int16)-1); // p_data
	mWriter.WriteT((int16)-1); // new_fpo
	mWriter.WriteT((int16)-1); // section_header_origin
	int optionalDbgHeaderSize = mWriter.GetStreamPos() - curPos;
	
	// Go back and fill in substream sizes	
	sizePtrs[0] = modInfoSize;
	sizePtrs[1] = sectionContributionSize;
	sizePtrs[2] = sectionMapSize;
	sizePtrs[3] = fileInfoSize;
	sizePtrs[4] = typeServerSize;
	sizePtrs[5] = 0; // MFCTypeServerIndex
	// Note: the position of the optionalDbgHeader and ecSize seem reversed, but this is correct
	sizePtrs[6] = optionalDbgHeaderSize;
	sizePtrs[7] = ecSize;

	EndStream();
}

void BlCodeView::CreateNamesStream()
{
	StartStream();
	mWriter.mCurStream->mName = "/names";
	WriteStringTable(mStrTab, mStrTabMap);
	EndStream();
}

void BlCodeView::CreateLinkInfoStream()
{
	StartStream();
	mWriter.mCurStream->mName = "/LinkInfo";
	EndStream();
}

void BlCodeView::CreateHeaderBlockStream()
{
	StartStream();
	mWriter.mCurStream->mName = "/src/headerblock";
	EndStream();
}

bool BlCodeView::FixTPITag(BlCvModuleInfo* module, unsigned long& typeId)
{	
	if (typeId < 0x1000)
		return true;	

	//BL_AUTOPERF_CV("BlCodeView::FixTPITag");

	auto& tpi = module->mTypeSource->mTPI;
	if (tpi.mCvMinTag == -1)
	{
		typeId = 0;
		return false;
	}
	typeId = tpi.GetMasterTPITag(typeId);
	return true;
}

bool BlCodeView::FixIPITag(BlCvModuleInfo* module, unsigned long& typeId)
{
	if (typeId < 0x1000)
		return true;

	//BL_AUTOPERF_CV("BlCodeView::FixTPITag");

	auto ipi = module->mTypeSource->mIPI;	
	int masterTag = ipi->GetMasterIPITag(typeId);
	/*if ((masterTag & BlTypeMapFlag_InfoExt_ProcId_TypeOnly) != 0)
	{		
		// Force loading of whole type now
		module->mTypeSource->mIPI->ParseTag(typeId, true);
		masterTag = ipi->GetMasterIPITag(typeId);
	}
	if ((masterTag & BlTypeMapFlag_InfoExt_ProcId_Resolved) != 0)
	{
		int extId = masterTag & BlTypeMapFlag_InfoExt_MASK;
		masterTag = module->mTypeSource->mIPI->mInfoExts[extId].mMasterTag;
	}*/	
	typeId = masterTag;
	return true;
}

bool BlCodeView::FixIPITag_Member(BlCvModuleInfo* module, unsigned long& typeId)
{
	if (typeId == 0)
		return true;

	//BL_AUTOPERF_CV("BlCodeView::FixIPITag");

	auto ipi = module->mTypeSource->mIPI;
	int memberTag = ipi->mElementMap[typeId - ipi->mCvMinTag];
	BF_ASSERT(memberTag != -1);
	typeId = memberTag;

	/*typeId = ipi->GetMasterIPITag(typeId);
	if ((typeId & BlTypeMapFlag_InfoExt_ProcId_TypeOnly) != 0)
		typeId = typeId & BlTypeMapFlag_InfoExt_MASK;
	else if ((typeId & BlTypeMapFlag_InfoExt_ProcId_Resolved) != 0)
	{
		int extId = typeId & BlTypeMapFlag_InfoExt_MASK;
		typeId = module->mTypeSource->mIPI->mInfoExts[extId].mMemberMasterTag;
	}
	else
	{
		BF_FATAL("No member found");
	}*/
	return true;
}

void BlCodeView::CreateModuleStreamSyms(BlCvModuleInfo* module, BlCvSymDataBlock* dataBlock, int dataOfs)
{
	//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms");

	int streamStartPos = mWriter.GetStreamPos();

#define DECL_SYM(symType, symName) \
		symType& old_##symName = *(symType*)dataStart; \
		symType& symName = *((symType*)(dataOut += sizeof(symType)) - 1); \
		symName = old_##symName;

#define _FIX_SYM_ADDRESS(ofsName) \
		FixSymAddress(sectionStart, &old_##ofsName, &ofsName, module, dataBlock, nextReloc)

#define FIX_SYM_ADDRESS(ofsName) \
		_FixSymAddress(&old_##ofsName, &ofsName)

	#define FIXTPI(typeVal) \
		FixTPITag(module, typeVal)
	#define FIXIPI(typeVal) \
		FixIPITag(module, typeVal)

	
	// For FIXTPI, if we fail to lookup type then we just don't include this symbol.
	//  This can happen when we have a TYPESERVER but don't have the referenced PDB
/*#define FIXTPI(typeVal) \
		if (!FixType(module, typeVal)) \
		{ \
			dataOut = dataOutStart; \
			data = dataEnd; \
			continue; \
		}*/	

	COFFRelocation* nextReloc = dataBlock->mRelocStart;
	int dataOutMaxSize = dataBlock->mSize * 2; // Alignment may add size
	//uint8* dataOut = new uint8[dataOutMaxSize];	
	//uint8* dataOutHead = dataOut;
	//uint8* dataOutMax = dataOut + dataOutMaxSize;

	const static int MAX_BLOCK_DEPTH = 256;

	int blockIdx = 0;
	//int blockPositions[MAX_BLOCK_DEPTH];
	int blockPtrs[MAX_BLOCK_DEPTH];

	const char* lastProcName = NULL;

	int curStreamPos = streamStartPos;
	int streamDataOfs = -streamStartPos + dataOfs;

	static int procId = 0;

	uint8* data = (uint8*)dataBlock->mData;
	uint8* sectionStart = data;
	uint8* sectionEnd = data + dataBlock->mSize;	
	uint8 dataChunk[0x10000];

	auto _SkipSymAddress = [&](void* oldDataPos, void* outDataPos)
	{
		int posOfs = (int)((uint8*)oldDataPos - (uint8*)sectionStart) + dataBlock->mSourceSectOffset;
		int posSect = posOfs + 4;

		if ((nextReloc->mVirtualAddress != posOfs) || (nextReloc == dataBlock->mRelocEnd))
			return;

		nextReloc++;
		if (nextReloc->mVirtualAddress != posSect)
			Fail("Symbol remap failure");
		// It MUST be the same sym	
		nextReloc++;
	};

	int deferredOutIdx = -1;
	bool addrIsInvalid = false;

	auto _FixSymAddress = [&](void* oldDataPos, void* outDataPos)
	{
		int posOfs = (int)((uint8*)oldDataPos - (uint8*)sectionStart) + dataBlock->mSourceSectOffset;
		int posSect = posOfs + 4;

		if ((nextReloc->mVirtualAddress != posOfs) || (nextReloc == dataBlock->mRelocEnd))
			return;

		auto objSym = &module->mSymInfo[nextReloc->mSymbolTableIndex];

		int segmentIdx = -1;
		int segmentOffset = -1;

		if (objSym->mKind == BlObjectDataSymInfo::Kind_Section)
		{
			auto& sectInfo = module->mSectInfos[objSym->mSectionNum - 1];
			segmentIdx = sectInfo.mSegmentIdx;
			segmentOffset = sectInfo.mSegmentOffset;
		}
		else
		{
			segmentIdx = objSym->mSegmentIdx;
			segmentOffset = objSym->mSegmentOffset;
		}

		if (segmentIdx != -1)
		{
			
			auto segment = mContext->mSegments[segmentIdx];

			bool deferAddr = true;
			if (deferAddr)
			{				
				int outOfs = (int)((uint8*)outDataPos - dataChunk);
				deferredOutIdx = curStreamPos + outOfs;

				*(int32*)(outDataPos) += segmentOffset;
				*((int16*)outDataPos + 2) += segmentIdx;
				
			}
			else
			{
				auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
				*(int32*)(outDataPos) += (segment->mRVA - outSection->mRVA) + segmentOffset;
				*((int16*)outDataPos + 2) += outSection->mIdx + 1;
			}
		}
		else
		{
			addrIsInvalid = true;
		}

		nextReloc++;
		if (nextReloc->mVirtualAddress != posSect)
			Fail("Symbol remap failure");
		// It MUST be the same sym	
		nextReloc++;
	};

	while (data < sectionEnd)
	{
		int symLen;
		int symType;
		
		uint8* dataOut = dataChunk;

		uint8* dataStart = data;
		uint8* dataOutStart = dataChunk;
		uint8* dataEnd;
		uint8* dataOutEnd;
		
		symLen = GET(uint16);
		dataEnd = data + symLen;
		dataOutEnd = dataChunk + symLen + 2;
		symType = GET(uint16);		

		bool skipEntry = false;
		const char* globalsName = NULL;
		bool isProcRef = false;

		deferredOutIdx = -1;

		addrIsInvalid = false;

		switch (symType)
		{
		case S_OBJNAME:
			{
				DECL_SYM(OBJNAMESYM, objNameSym);				
			}
			break;
		case S_COMPILE3:
			{
				DECL_SYM(COMPILESYM3, compileSym);				
			}
			break;
		case S_ENVBLOCK:
			{
				DECL_SYM(ENVBLOCKSYM, envBlock);
			}
			break;
		case S_BUILDINFO:
			{
				BUILDINFOSYM& buildInfoSym = *(BUILDINFOSYM*)dataStart;
				skipEntry = true;
			}
			break;
		case S_CONSTANT:
			{
				struct _ConstSymShort
				{
					unsigned short  reclen;     // Record length
					unsigned short  rectyp;     // S_CONSTANT or S_MANCONSTANT
					CV_typ_t        typind;     // Type index (containing enum if enumerate) or metadata token
				};

				CONSTSYM& oldConstSym = *(CONSTSYM*)dataStart;
				globalsName = (char*)oldConstSym.name;

				DECL_SYM(_ConstSymShort, constSym);
				FIXTPI(constSym.typind);				
				skipEntry = true;
			}
			break;
		case S_UDT:
			{
				DECL_SYM(UDTSYM, udtSym);
				FIXTPI(udtSym.typind);
				//TODO: JUST TESTING!
				//udtSym.typind = 0x0074;
				globalsName = (const char*)udtSym.name;
				skipEntry = true;
			}
			break;
		case S_LDATA32:
		case S_GDATA32:
			{
				
				DECL_SYM(DATASYM32, dataSym);
				FIX_SYM_ADDRESS(dataSym.off);
				FIXTPI(dataSym.typind);
				globalsName = (char*)old_dataSym.name;				
				skipEntry = true;
			}
			break;
		case S_GTHREAD32:
		case S_LTHREAD32:
			{
				DECL_SYM(THREADSYM32, threadSym);
				FIX_SYM_ADDRESS(threadSym.off);
				FIXTPI(threadSym.typind);
				globalsName = (char*)old_threadSym.name;				
				skipEntry = true;
			}
			break;
		case S_EXPORT:
			{
				EXPORTSYM& exportSym = *(EXPORTSYM*)dataStart;				
			}
		break;
		case S_LPROC32:
		case S_GPROC32:		
			{				
				DECL_SYM(PROCSYM32, procSym);
				FIX_SYM_ADDRESS(procSym.off);
				FIXTPI(procSym.typind);				
				
				blockPtrs[blockIdx++] = curStreamPos;
				globalsName = (const char*)old_procSym.name;
				isProcRef = true;
			}
			break;
		case S_LPROC32_ID:
		case S_GPROC32_ID:			
			{
				DECL_SYM(PROCSYM32, procSym);
				FIX_SYM_ADDRESS(procSym.off);

				lastProcName = (char*)old_procSym.name;
				
				FixIPITag_Member(module, procSym.typind);				

				if (!addrIsInvalid)
					blockPtrs[blockIdx++] = curStreamPos;
				globalsName = (const char*)old_procSym.name;
				isProcRef = true;
				
				if (symType == S_LPROC32_ID)
					procSym.rectyp = S_LPROC32;
				else
					procSym.rectyp = S_GPROC32;				
			}
			break;
		case S_FRAMEPROC:
			{
				//DECL_SYM(FRAMEPROCSYM, frameProc);
				//FixSymAddress(&frameProc.);
			}
			break;
		case S_THUNK32:
			{
				DECL_SYM(THUNKSYM32, thunkSym);
				FIX_SYM_ADDRESS(thunkSym.off);

				lastProcName = (char*)old_thunkSym.name;
				blockPtrs[blockIdx++] = curStreamPos;
			}
			break;
		case S_BLOCK32:
			{
				DECL_SYM(BLOCKSYM32, blockSym);
				FIX_SYM_ADDRESS(blockSym.off);
				
				if (blockIdx > 0)
					blockSym.pParent = blockPtrs[blockIdx - 1] + streamDataOfs;
				blockPtrs[blockIdx++] = curStreamPos;
			}
			break;
		case S_LOCAL:
			{
				DECL_SYM(LOCALSYM, localSym);
				FIXTPI(localSym.typind);
			}
			break;
		case S_BPREL32:
			{
				DECL_SYM(BPRELSYM32, bpRel32);
				FIXTPI(bpRel32.typind);
			}
			break;
		case S_REGISTER:
			{
				DECL_SYM(REGSYM, regSym);
				FIXTPI(regSym.typind);
			}
			break;
		case S_REGREL32:
			{
				DECL_SYM(REGREL32, regRel32);
				FIXTPI(regRel32.typind);
			}
			break;
		case S_DEFRANGE_REGISTER:
			{
				DECL_SYM(DEFRANGESYMREGISTER, defRangeReg);				
				FIX_SYM_ADDRESS(defRangeReg.range.offStart);				
			}
			break;
		case S_DEFRANGE_FRAMEPOINTER_REL:
			{
				DECL_SYM(DEFRANGESYMFRAMEPOINTERREL, defRangeFPRel);
				FIX_SYM_ADDRESS(defRangeFPRel.range.offStart);
			}
			break;
		case S_DEFRANGE_SUBFIELD_REGISTER:
			{
				DECL_SYM(DEFRANGESYMSUBFIELDREGISTER, defRangeSubFieldReg);
				FIX_SYM_ADDRESS(defRangeSubFieldReg.range.offStart);
			}
		break;
		case S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
			{
				DECL_SYM(DEFRANGESYMFRAMEPOINTERREL_FULL_SCOPE, defRangeFPRel);
			}
			break;
		case S_DEFRANGE_REGISTER_REL:
			{
				DECL_SYM(DEFRANGESYMREGISTERREL, defRangeRegRel);
				FIX_SYM_ADDRESS(defRangeRegRel.range.offStart);
			}
			break;
		case S_ENDARG:
			//BF_ASSERT(curParam == NULL);
			break;				
		case S_PROC_ID_END:
			{
				struct _ENDSYM
				{
					unsigned short  reclen;
					unsigned short  rectyp;
				};
				DECL_SYM(_ENDSYM, endSym);
				endSym.rectyp = S_END;
			}
			// Fall through
		case S_INLINESITE_END:
			// Fall through
		case S_END:
			{
				if (blockIdx == 0)
				{
					// Parent was omitted
					skipEntry = true;
				}
				else								
				{
					/*struct _SRCSYM // Either a BLOCKSYM32, PROCSYM32, WITHSYM32...
					{
						unsigned short  reclen;     // Record length
						unsigned short  rectyp;     // S_BLOCK32
						unsigned long   pParent;    // pointer to the parent
						unsigned long   pEnd;       // pointer to this blocks end
					};*/

					long* pEnd = (long*)mWriter.GetStreamPtr(blockPtrs[--blockIdx] + 8);
					BF_ASSERT(*pEnd == 0);
					*pEnd = curStreamPos + streamDataOfs;
				}
			}
			break;		
		case S_FRAMECOOKIE:
			{
				FRAMECOOKIE& frameCookie = *(FRAMECOOKIE*)dataStart;				
			}
		break;
		case S_LABEL32:
			{
				DECL_SYM(LABELSYM32, labelSym);
				FIX_SYM_ADDRESS(labelSym.off);
			}
			break;
		case S_CALLSITEINFO:
			{
				DECL_SYM(CALLSITEINFO, callSiteInfo);
				FIX_SYM_ADDRESS(callSiteInfo.off);
				FIXTPI(callSiteInfo.typind);
			}
			break;
		case S_HEAPALLOCSITE:
			{
				DECL_SYM(HEAPALLOCSITE, heapAllocSite);
				FIX_SYM_ADDRESS(heapAllocSite.off);
				FIXTPI(heapAllocSite.typind);
			}
			break;
		case S_FILESTATIC:
			{
				DECL_SYM(FILESTATICSYM, fileStaticSym);
				FIXTPI(fileStaticSym.typind);				
			}
		break;
		case S_CALLEES:
			{
				DECL_SYM(FUNCTIONLIST, calleeList);
				for (int i = 0; i < (int)calleeList.count; i++)
				{						
					calleeList.funcs[i] = old_calleeList.funcs[i];
					FIXTPI(calleeList.funcs[i]);
				}
				dataOut = (uint8*)(&calleeList.funcs[calleeList.count]);
			}
			break;
		case S_CALLERS:
			{
				DECL_SYM(FUNCTIONLIST, callerList);
				for (int i = 0; i < (int)callerList.count; i++)
				{
					callerList.funcs[i] = old_callerList.funcs[i];
					FIXTPI(callerList.funcs[i]);
				}
				dataOut = (uint8*)(&callerList.funcs[callerList.count]);
			}
			break;
		case S_POGODATA:
			{
				DECL_SYM(POGOINFO, pogoInfo);				
			}
			break;
		case S_INLINESITE:
			{
				DECL_SYM(INLINESITESYM, inlineSite);
				FIXIPI(inlineSite.inlinee);
				
				if (blockIdx > 0)
					inlineSite.pParent = blockPtrs[blockIdx - 1] + streamDataOfs;
				blockPtrs[blockIdx++] = curStreamPos;
			}
			break;		
		case S_ANNOTATION:
			{
				DECL_SYM(ANNOTATIONSYM, annotation);
				FIX_SYM_ADDRESS(annotation.off);
			}
			break;
		case S_TRAMPOLINE:
			break;
		case S_COFFGROUP:
			{
				DECL_SYM(COFFGROUPSYM, coffGroup);
				FIX_SYM_ADDRESS(coffGroup.off);
			}
			break;
		case S_SECTION:
			NotImpl();
			break;
		case S_SSEARCH:
			{
				SEARCHSYM* searchSym = (SEARCHSYM*)dataStart;				
			}
			break;
		default:
			NotImpl();
			break;
		}
		
		data = dataEnd;		

		if (addrIsInvalid)
		{
			dataOut = dataOutStart;
			continue;
		}
		
		if (dataOut < dataOutEnd)
		{
			//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms EndCopy");

			int copyBytes = (int)(dataOutEnd - dataOut);
			memcpy(dataOut, data - copyBytes, copyBytes);
			dataOut = dataOutEnd;
		}

		bool writeToSymRecord = false;
		if (globalsName != NULL)
		{			
			// We only include the first instance of each name
			int idx;
			Val128 hashVal;
			hashVal = Hash128(globalsName, (int)strlen(globalsName));
			idx = mGlobals.TryInsert(globalsName, hashVal);			
			//idx = -1;
			if (idx >= 0)
			{
				if (isProcRef)
				{
					//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms ProcRef");

					int nameSize = (int)strlen(globalsName) + 1;
					REFSYM2 refSym = { 0 };
					refSym.reclen = (int)offsetof(REFSYM2, name) + nameSize - 2;
					refSym.rectyp = S_PROCREF;
					refSym.imod = module->mIdx + 1;
					refSym.ibSym = curStreamPos + streamDataOfs;
					int addBytes = ((nameSize + 2 + 3) & ~3) - (nameSize + 2);
					refSym.reclen += addBytes;
					//if (deferredOutIdx >= 0)
						//mSymRecordDeferredPositions.Append((int)mSymRecordsWriter.GetStreamPos());
					mSymRecordsWriter.Write(&refSym, (int)offsetof(REFSYM2, name));
					mSymRecordsWriter.Write(globalsName, nameSize);
					if (addBytes > 0)
					{
						int zero = 0;
						mSymRecordsWriter.Write(&zero, addBytes);
					}
				}
				else
				{
					writeToSymRecord = true;					
				}				
			}			

			/*auto pair = mGlobals.mMap.insert(hashVal);
			if (pair.second)
			{
				int idx = mSymRecords.GetSize();
				mGlobals.mRecordIndices.Append(idx);
				mSymRecords.Write(dataOutStart, (int)(dataOutEnd - dataOutStart));
			}*/
		}

		if ((skipEntry) && (!writeToSymRecord))
		{			
			dataOut = dataOutStart;
			continue;
		}
		
		// Align
		if ((intptr)dataOut % 4 != 0)
		{	
			//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms Finish");

			dataOutEnd = (uint8*)(((intptr)dataOut + 3) & ~3);
			int addBytes = (int)(dataOutEnd - dataOut);
			if (addBytes > 0)
			{				
				*(int32*)(dataOut) = 0;
				// Add to length
				*(int16*)dataOutStart += (int16)addBytes;
			}
			dataOut = dataOutEnd;
		}

		if (writeToSymRecord)
		{
			//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms WriteToSymRecord");
			if (deferredOutIdx >= 0)
				mSymRecordDeferredPositions.push_back((int)mSymRecordsWriter.GetStreamPos());
			mSymRecordsWriter.Write(dataOutStart, (int)(dataOutEnd - dataOutStart));
		}

		if (skipEntry)
		{			
			//dataOut = dataOutStart;
			continue;
		}

		if (deferredOutIdx != -1)
		{
			mDeferedSegOffsets.push_back(deferredOutIdx);
			module->mDeferredSegOfsLen++;
		}

		int dataSize = (int)(dataOutEnd - dataOutStart);
		mWriter.Write(dataOutStart, dataSize);
		curStreamPos += dataSize;
		
		BF_ASSERT(curStreamPos == mWriter.GetStreamPos());

		//BF_ASSERT(dataOut < dataOutMax);
	}
	BF_ASSERT(nextReloc == dataBlock->mRelocEnd);

	BF_ASSERT(blockIdx == 0);
	
	//int outSize = (int)(dataOut - dataOutHead);
	dataBlock->mOutSize = mWriter.GetStreamPos() - streamStartPos;

	{
		//BL_AUTOPERF_CV("BlCodeView::CreateModuleStreamSyms Write");
		//mWriter.Write(dataOutHead, outSize);
	}
	//delete [] dataOutHead;
}

void BlCodeView::CreateLinkerSymStream()
{
#define SALIGN(recLen) recLen = ((recLen + 2 + 3) & ~3) - 2;
#define OUT_WITH_STR(typeName, recName, nameMember) \
	symLen = (int)offsetof(typeName, nameMember); \
	recName.reclen = (symLen - 2) + (int)str.length() + 1; \
	SALIGN(recName.reclen); \
	mWriter.Write(&recName, symLen); \
	mWriter.Write((void*)str.c_str(), (int)str.length() + 1); \
	mWriter.StreamAlign(4);

	String str;
	int symLen;

	OBJNAMESYM objNameSym = { 0 };
	objNameSym.rectyp = S_OBJNAME;
	str = "* Linker *";
	OUT_WITH_STR(OBJNAMESYM, objNameSym, name);

	COMPILESYM3 compileSym = { 0 };
	compileSym.rectyp = S_COMPILE3;
	compileSym.flags.iLanguage = 7; // Link
	compileSym.machine = 0xD0;	
	compileSym.verMajor = 14;
	compileSym.verMinor = 0;
	compileSym.verFEBuild = 24215;
	compileSym.verQFE = 1;
	str = "Microsoft (R) Link";
	OUT_WITH_STR(COMPILESYM3, compileSym, verSz);
	
	char cwd[MAX_PATH];
	_getcwd(cwd, MAX_PATH);
	char moduleFileName[MAX_PATH];
	GetModuleFileNameA(NULL, moduleFileName, MAX_PATH);
	
	ENVBLOCKSYM envBlock = { 0 };
	envBlock.rectyp = S_ENVBLOCK;	
	str = "cwd"; str.Append(0);
	str += cwd; str.Append(0);
	str += "exe"; str.Append(0);
	str += moduleFileName; str.Append(0);	
	str += "pdb"; str.Append(0);
	str += mMsf.mFileName; str.Append(0);
	str += "cmd"; str.Append(0);
	str += GetCommandLineA(); str.Append(0);
	OUT_WITH_STR(ENVBLOCKSYM, envBlock, rgsz);

	for (auto segment : mContext->mSegments)
	{
		auto outSection = mContext->mOutSections[segment->mOutSectionIdx];

		COFFGROUPSYM coffGroupSym = { 0 };
		coffGroupSym.rectyp = S_COFFGROUP;
		coffGroupSym.seg = segment->mOutSectionIdx + 1;
		coffGroupSym.off = (int)(segment->mRVA - outSection->mRVA);
		coffGroupSym.characteristics = segment->mCharacteristics;
		coffGroupSym.cb = segment->mCurSize;
		str = segment->mName;
		OUT_WITH_STR(COFFGROUPSYM, coffGroupSym, name);
	}

	for (auto outSection : mContext->mOutSections)
	{
		SECTIONSYM sectionSym = { 0 };
		sectionSym.rectyp = S_SECTION;
		sectionSym.rva = outSection->mRVA;
		sectionSym.align = outSection->mAlign;
		sectionSym.isec = outSection->mIdx + 1;				
		sectionSym.characteristics = outSection->mCharacteristics;		
		sectionSym.cb = outSection->mRawSize;
		str = outSection->mName;
		OUT_WITH_STR(SECTIONSYM, sectionSym, name);
	}
}


bool BlCodeView::OnlyHasSimpleRelocs(BlCvModuleInfo* module)
{
	/*for (auto& block : module->mSymData)
	{
		for (COFFRelocation* coffReloc = block.mRelocStart; coffReloc < block.mRelocEnd; coffReloc++)
		{
			auto& symInfo = module->mSymInfo[coffReloc->mSymbolTableIndex];
			if (symInfo.mSegmentIdx < 0)
				return false;			
			if (symInfo.mSegmentIdx >= mContext->mNumFixedSegs)
			{
				auto seg = mContext->mSegments[symInfo.mSegmentIdx];
				OutputDebugStrF("Failed on: %s in %s\n", seg->mName.c_str(), module->mObjectData->mName);
				return false;
			}
		}		
	}*/

	return true;
}

void BlCodeView::CreateModuleStream(BlCvModuleInfo* module)
{
	BP_ZONE("BlCodeView::CreateModuleStream");
	//BL_AUTOPERF_CV("BlCodeView::CreateModuleStream");

	//OutputDebugStrF("CreateModuleStream %d\n", module->mIdx);

	//
	{
		BP_ZONE("BlCodeView::CreateModuleStream WaitForDoneSignal");
		/*if (module->mTypeSource != NULL)
			module->mTypeSource->mDoneSignal.WaitFor();*/
	}	

	if (mContext->mFailed)
		return;

	module->mSymStreamIdx = StartStream();

	mWriter.WriteT((int32)CV_SIGNATURE_C13);
	//int startPos = mWriter.GetStreamPos();
	if (module->mObjectData == NULL)
	{
		int startSize = mWriter.GetStreamPos();

		if (module->mName == "* Linker *")		
			CreateLinkerSymStream();

		BlCvSymDataBlock data = { 0 };
		data.mOutSize = mWriter.GetStreamPos() - startSize;
		module->mSymData.push_back(data);
	}
	else
	{
		int dataOfs = 4;

		/*const char* invalidStrs[] = { "delete_scalar", "new_scalar" };
		for (const char* invalidStr : invalidStrs)
		{
			//TODO:
			if (strstr(module->mObjectData->mName, invalidStr) != NULL)
			{				
				module->mSymData.clear();
				module->mLineInfo.clear();
				module->mFileInfos.clear();
				break;
			}
		}*/

		{
			BL_AUTOPERF_CV("BlCodeView::CreateModuleStream CreateModuleStreamSyms");

			module->mDeferredSegOfsStart = (int)mDeferedSegOffsets.size();
			for (auto& block : module->mSymData)
			{
				CreateModuleStreamSyms(module, &block, dataOfs);
				dataOfs += block.mOutSize;
			}
		}
	}
	//mFS.Align(4);
	// TODO: Write symbol data...
	//module->mSymSize = mWriter.GetStreamPos() - startPos;	
	
	if (!module->mFileInfos.empty())
	{
		int sectStart = mWriter.GetStreamPos();
		StartSection(DEBUG_S_FILECHKSMS);
		for (auto& fileInfo : module->mFileInfos)
		{
			struct _CVFileInfo
			{
				int32 mFileTabOfs;
				int8 mHashLen;
				int8 mHashType;
				uint8 mHash[16];
				uint16 mPadding;
			};

			BF_ASSERT(fileInfo.mChksumOfs == (mWriter.GetStreamPos() - sectStart) - 8);

			_CVFileInfo cvFileInfo;
			cvFileInfo.mFileTabOfs = fileInfo.mStrTableIdx;
			if (fileInfo.mHashType == 1)
			{
				cvFileInfo.mHashLen = 16;
				cvFileInfo.mHashType = 1; //MD5
				memcpy(cvFileInfo.mHash, fileInfo.mHash, 16);
				mWriter.WriteT(cvFileInfo);
			}
			else
			{				
				cvFileInfo.mHashLen = 0;
				cvFileInfo.mHashType = 0;
				cvFileInfo.mHash[0] = 0; // Padding
				cvFileInfo.mHash[1] = 0;
				mWriter.Write(&cvFileInfo, 8);
			}
			
		}
		module->mLineInfoSize += EndSection();
	}
	
	for (auto lineInfo : module->mLineInfo)
	{
		StartSection(DEBUG_S_LINES);

		CV_DebugSLinesHeader_t lineSect = { 0 };
		lineSect.flags = 0;
		if (!lineInfo->mLineInfoBlocks.empty())
		{
			if (!lineInfo->mLineInfoBlocks[0].mColumns.empty())
				lineSect.flags |= CV_LINES_HAVE_COLUMNS;
		}
		
		//GetOutSectionAddr(lineInfo->mStartSegmentIdx, lineInfo->mStartSegmentOffset, lineSect.segCon, lineSect.offCon);
		lineSect.segCon = lineInfo->mStartSegmentIdx;
		lineSect.offCon = lineInfo->mStartSegmentOffset;

		mDeferedSegOffsets.push_back(mWriter.GetStreamPos());
		module->mDeferredSegOfsLen++;

		lineSect.cbCon = lineInfo->mContribBytes;
		mWriter.WriteT(lineSect);
		for (auto& lineBlocks : lineInfo->mLineInfoBlocks)
		{
			//TODO: Ensure cbBlock is correct
			CV_DebugSLinesFileBlockHeader_t lineBlockHeader = { 0 };
			lineBlockHeader.cbBlock = (int)sizeof(CV_DebugSLinesFileBlockHeader_t) + (int)(lineBlocks.mLines.size() * sizeof(CV_Line_t));
			lineBlockHeader.cbBlock += (int)(lineBlocks.mColumns.size() * sizeof(int16) * 2);
			lineBlockHeader.nLines = (int)lineBlocks.mLines.size();
			//int bytesPerFile = 24;			
			//lineBlockHeader.offFile = lineBlocks.mFileInfoIdx * bytesPerFile;
			auto fileInfo = module->mFileInfos[lineBlocks.mFileInfoIdx];
			lineBlockHeader.offFile = fileInfo.mChksumOfs;
			mWriter.WriteT(lineBlockHeader);
			for (auto& line : lineBlocks.mLines)
			{
				CV_Line_t cvLineData;
				cvLineData.offset = line.mOffset;
				cvLineData.linenumStart = line.mLineNumStart;
				cvLineData.fStatement = 0;
				cvLineData.deltaLineEnd = 0;
				mWriter.WriteT(cvLineData);
			}
			for (auto column : lineBlocks.mColumns)
			{
				int16 colVals[2] = { column, column };
				mWriter.WriteT(colVals);
			}
		}
		module->mLineInfoSize += EndSection();
	}

	for (auto& inlineData : module->mInlineData)
	{
		StartSection(DEBUG_S_INLINEELINES);

		uint8* data = (uint8*)inlineData.mData;
		uint8* dataEnd = data + inlineData.mSize;

		int linesType = GET(int);
		mWriter.WriteT(linesType);

		BF_ASSERT(linesType == 0);
		while (data < dataEnd)
		{
			CodeViewInfo::InlineeSourceLine lineInfo = GET(CodeViewInfo::InlineeSourceLine);
			FixIPITag(module, lineInfo.inlinee);
			mWriter.WriteT(lineInfo);
		}

		module->mLineInfoSize += EndSection();
	}
	
	mWriter.WriteT(0); // GlobalRefsSize

	bool flushNow = module->mDeferredSegOfsLen == 0;
	EndStream(flushNow);
}

void BlCodeView::FinishModuleStream(BlCvModuleInfo* module)
{
	BP_ZONE("BlCodeView::FinishModuleStream");

	auto streamInfo = mStreams[module->mSymStreamIdx];

	auto fixItr = mDeferedSegOffsets.GetIterator(module->mDeferredSegOfsStart);
	for (int deferIdx = 0; deferIdx < module->mDeferredSegOfsLen; deferIdx++)
	{		
		int deferredIdx = *fixItr;
		
		int dataIdx = deferredIdx;
		int blockIdx = streamInfo->mBlocks[dataIdx / CV_BLOCK_SIZE];
		uint8* data = (uint8*)mMsf.mBlocks[blockIdx]->mData + (dataIdx % CV_BLOCK_SIZE);
		int32* ofsPtr = (int32*)data;

		dataIdx = deferredIdx + 4;
		blockIdx = streamInfo->mBlocks[dataIdx / CV_BLOCK_SIZE];
		data = (uint8*)mMsf.mBlocks[blockIdx]->mData + (dataIdx % CV_BLOCK_SIZE);
		int16* segPtr = (int16*)data;
		
		auto segment = mContext->mSegments[*segPtr];
		auto outSection = mContext->mOutSections[segment->mOutSectionIdx];
		*ofsPtr += segment->mRVA - outSection->mRVA;
		*segPtr = outSection->mIdx + 1;

		++fixItr;
	}

	FlushStream(streamInfo);
}

void BlCodeView::CreateLinkerModule()
{
	auto module = mModuleInfo.Alloc();
	module->mName = "* Linker *";	
	module->mIdx = (int)mModuleInfo.size() - 1;		
}

void BlCodeView::DoFinish()
{
	//TODO:
	//StopWorkThread();

	int simpleRelocCount = 0;


	for (auto module : mModuleInfo)
	{
		if (module->mSymStreamIdx == -1)
		{
			//simpleRelocCount += OnlyHasSimpleRelocs(module);
			CreateModuleStream(module);
		}

		if (module->mDeferredSegOfsLen > 0)
		{
			FinishModuleStream(module);
		}
	}	

	/*for (auto module : mModuleInfo)
	{
		String dbgStr = StrFormat("Module #%d:", module->mIdx);
		for (auto block : mStreams[module->mSymStreamIdx]->mBlocks)
		{
			dbgStr += StrFormat(" %d", block);
		}
		dbgStr += "\n";
		OutputDebugStringA(dbgStr.c_str());
	}*/

	//StopWorkThread();
	if (mContext->mFailed)
	{
		// We can't continue if any types may have failed
		return;
	}

	CreateDBIStream();
	// CreateDBIStream does CreateGlobalsStream/CreatePublicsStream, so the symRecords
	//  aren't done until after that
	FinishSymRecords();		

	CreateTypeStream();
	CreateIPIStream();		
	CreateNamesStream();
	CreateLinkInfoStream();
	CreateHeaderBlockStream();
	CreatePDBInfoStream();		

	// Stream dir
	//int streamDirLoc = mMSF.Alloc();

	BlCVStream dirStream;
	StartUnnamedStream(dirStream);

	mWriter.WriteT((int32)mStreams.size());
	for (auto& stream : mStreams)
	{
		BF_ASSERT(stream->mSize >= 0);
		mWriter.WriteT((int32)stream->mSize);		
	}

	std::vector<int> streamBlocks;

	uint8* streamDataPos = NULL;
	uint8* streamDataEnd = NULL;

	for (auto& stream : mStreams)
	{
		//int numBlocks = (stream.mSize + CV_BLOCK_SIZE - 1) / CV_BLOCK_SIZE;
		//for (int blockIdx = stream.mBlockStart; blockIdx < stream.mBlockStart + numBlocks; blockIdx++)
			//mWriter.WriteT(blockIdx);
		for (auto blockIdx : stream->mBlocks)
			mWriter.WriteT((int32)blockIdx);
	}	
	//int streamDirLen = mWriter.GetStreamPos() - (streamDirLoc * CV_BLOCK_SIZE);
	//int numStreamDirBlocks = (streamDirLen + CV_BLOCK_SIZE - 1) / CV_BLOCK_SIZE;
	//EndBlock();
	EndStream();
	
	// Root block
	//int rootBlockNum = mMSF.Alloc();
	BlCVStream rootStream;
	StartUnnamedStream(rootStream);
	for (auto block : dirStream.mBlocks)
		mWriter.WriteT((int32)block);
	EndStream();

	//for (int streamDirBlockIdx = streamDirLoc; streamDirBlockIdx < streamDirLoc + numStreamDirBlocks; streamDirBlockIdx++)	
		//mWriter.WriteT((int32)streamDirBlockIdx);	
	//EndBlock();

	// Fix header	
	
	/*MemStream headerStream(mMSF.mBlocks[0]->mData, CV_BLOCK_SIZE, false);
	int headerStart = 32;
	headerStream.SetPos(headerStart + 8);
	headerStream.Write((int32)mNumBlocks);
	headerStream.Write((int32)streamDirLen);
	headerStream.Write((int32)0); // Unknown
	headerStream.Write((int32)rootBlockNum);*/

	// Fix bitmap
	/*mFS.SetPos(CV_BLOCK_SIZE);
	BfBitSet bitset;
	bitset.Init(mNumBlocks);
	int numBytes = (mNumBlocks + 7) / 8;
	memset(bitset.mBits, 0xFF, numBytes);
	for (int i = 1; i < mNumBlocks; i++)
		bitset.Clear(i);
	mWriter.WriteT(bitset.mBits, numBytes);

	BF_ASSERT(mFS.GetSize() == mNumBlocks * CV_BLOCK_SIZE);
	mFS.Close();*/

	BF_ASSERT(rootStream.mBlocks.size() == 1);

	//AutoPerf autoPerf("Write", &gCVThreadPerfManager);
	BP_ZONE("BlCodeView::DoFinish Write");
	{
		BP_ZONE("BlCodeView::DoFinish Write Wait");
		mMsf.mWorkThread.Stop();
	}
	mMsf.Finish(rootStream.mBlocks[0], dirStream.mSize);
}

void BlCodeView::Finish()
{
	/*if (mWorkThread == NULL)
	{
		DoFinish();
	}
	else
	{
		mWorkDone = true;
		mWorkEvent.Set();
	}*/

	mTypeWorkThread.mTypesDone = true;
	mTypeWorkThread.mWorkEvent.Set();	

	mModuleWorkThread.mModulesDone = true;
	mModuleWorkThread.mWorkEvent.Set();
}
