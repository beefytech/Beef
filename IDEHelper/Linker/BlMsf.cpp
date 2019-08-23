#include "BlMsf.h"
#include "BeefySysLib/MemStream.h"
#include "../Compiler/BfAstAllocator.h"

USING_NS_BF;

#define MSF_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\032DS\0\0"
#define CV_BLOCK_SIZE 0x1000
#define PTR_ALIGN(ptr, origPtr, alignSize) ptr = ( (origPtr)+( ((ptr - origPtr) + (alignSize - 1)) & ~(alignSize - 1) ) )

//////////////////////////////////////////////////////////////////////////


BlMsfWorkThread::BlMsfWorkThread()
{
	mMsf = NULL;
	mDone = false;
	mSortDirty = false;
}

BlMsfWorkThread::~BlMsfWorkThread()
{
	Stop();
}

void BlMsfWorkThread::Stop()
{
	mDone = true;
	mWorkEvent.Set();
	WorkThread::Stop();
}

void BlMsfWorkThread::Run()
{
	while (!mDone)
	{
		if (mSortedWriteBlockWorkQueue.empty())
		{
			// Only fill in new entries when our queue is empty. This is not a strong ordering guarantee.
			bool sortDirty = false;
			mCritSect.Lock();
			if (!mWriteBlockWorkQueue.empty())
			{
				sortDirty = true;
				mSortedWriteBlockWorkQueue.insert(mSortedWriteBlockWorkQueue.begin(), mWriteBlockWorkQueue.begin(), mWriteBlockWorkQueue.end());
				mWriteBlockWorkQueue.clear();
			}
			mCritSect.Unlock();

			BlMsfBlock* block = NULL;
			if (sortDirty)
			{
				std::sort(mSortedWriteBlockWorkQueue.begin(), mSortedWriteBlockWorkQueue.end(), [](BlMsfBlock* lhs, BlMsfBlock* rhs)
				{
					return lhs->mIdx < rhs->mIdx;
				});
			}
		}

		BlMsfBlock* block = NULL;
		if (!mSortedWriteBlockWorkQueue.empty())
		{
			block = mSortedWriteBlockWorkQueue.front();
			mSortedWriteBlockWorkQueue.pop_front();
		}		
		
		if (block == NULL)
		{
			mWorkEvent.WaitFor();
			continue;
		}
		
		mMsf->WriteBlock(block);
	}
}

void BlMsfWorkThread::Add(BlMsfBlock* block)
{
	AutoCrit autoCrit(mCritSect);
	mWriteBlockWorkQueue.push_back(block);
	mWorkEvent.Set();
	mSortDirty = true;
}


//////////////////////////////////////////////////////////////////////////
BlMsf::BlMsf()
{
	mCodeView = NULL;
	mBlockSize = 0x1000;	
	mFreePageBitmapIdx = 1;
	mFileSize = 0;
	mAllocatedFileSize = 0;

	mWorkThread.mMsf = this;
}

BlMsf::~BlMsf()
{
	for (auto block : mBlocks)	
		delete block->mData;	
}

void BlMsf::WriteBlock(BlMsfBlock* block)
{
	if (mAllocatedFileSize > mFileSize)
	{
		mFS.SetSizeFast(mAllocatedFileSize);
		mFileSize = mAllocatedFileSize;
	}

	int wantPos = block->mIdx * CV_BLOCK_SIZE;
	if (wantPos > mFileSize)
	{		
		mFS.SetSizeFast(wantPos);
		mFileSize = wantPos;
	}

	if (mFileSize == wantPos)
		mFileSize = wantPos + CV_BLOCK_SIZE;

	mFS.SetPos(block->mIdx * CV_BLOCK_SIZE);
	mFS.Write(block->mData, CV_BLOCK_SIZE);
	delete block->mData;
	block->mData = NULL;
}

bool BlMsf::Create(const StringImpl& fileName)
{
	mFileName = fileName;
	//if (!mFS.Open(fileName, GENERIC_WRITE | GENERIC_READ))
		//return false;

	if (!mFS.Open(fileName, BfpFileCreateKind_CreateAlways, (BfpFileCreateFlags)(BfpFileCreateFlag_Write | BfpFileCreateFlag_Read)))
		return false;
	
	// Header
	Alloc(true, false);
	
	// First FPM block
	Alloc(true, false);

	// 'Old' FPM block
	Alloc(true, false);
	
	return true;
}

int BlMsf::Alloc(bool clear, bool skipFPMBlock)
{
	int blockIdx = (int)mBlocks.size();

	BlMsfBlock* msfBlock = mBlocks.Alloc();
	msfBlock->mData = new uint8[CV_BLOCK_SIZE];
	if (clear)
		memset(msfBlock->mData, 0, CV_BLOCK_SIZE);
	msfBlock->mIdx = blockIdx;	

	if ((skipFPMBlock) && ((blockIdx % CV_BLOCK_SIZE) == mFreePageBitmapIdx))
	{
		// We need this page as a Free Page Bitmap.  This will be triggered once for 
		//  every 16MB of PDB.  It should be once every 128MB but isn't because of a
		//  Microsoft bug in their initial implementation.
		return Alloc();
	}

	mAllocatedFileSize = (int)(mBlocks.size() * CV_BLOCK_SIZE);
	return blockIdx;
}

void BlMsf::FlushBlock(int blockIdx)
{
	if (mWorkThread.mThread == NULL)
		mWorkThread.Start();
	mWorkThread.Add(mBlocks[blockIdx]);
}

void BlMsf::Finish(int rootBlockNum, int streamDirLen)
{
	mWorkThread.Stop();

	int numBlocks = (int)mBlocks.size();

	MemStream headerStream(mBlocks[0]->mData, CV_BLOCK_SIZE, false);
	headerStream.Write(MSF_SIGNATURE_700, 32);
	headerStream.Write((int32)CV_BLOCK_SIZE); // Page size
	headerStream.Write((int32)mFreePageBitmapIdx); // FreeBlockMapBlock - always use page 1.  It's allowed to flip between 1 and 2.
	headerStream.Write((int32)numBlocks); // Total page count
	headerStream.Write((int32)streamDirLen);
	headerStream.Write((int32)0); // Unknown
	headerStream.Write((int32)rootBlockNum);

	// Create Free Page Bitmap
	BfBitSet bitset;
	bitset.Init(numBlocks);
	int numBytes = (numBlocks + 7) / 8;
	memset(bitset.mBits, 0xFF, numBytes);
	for (int i = 0; i < numBlocks; i++)
		bitset.Clear(i);

	// Bits are written in blocks at block nums (mFreePageBitmapIdx + k*CV_BLOCK_SIZE)
	//  This is a little strange, but the actual mFreePageBitmapIdx block can only hold enough bits for
	//  128MB of pages, so PSBs over that size get spread at 'CV_BLOCK_SIZE' intervals.  This is technically
	//  wrong, as it should be 'CV_BLOCK_SIZE*8', but that's an Microsoft bug that can't be fixed now.
	uint8* data = (uint8*)bitset.mBits;
	int bytesLeft = numBytes;
	int curBlockNum = mFreePageBitmapIdx;
	while (bytesLeft > 0)
	{
		int writeBytes = std::min(bytesLeft, CV_BLOCK_SIZE);

		uint8* dataDest = (uint8*)mBlocks[curBlockNum]->mData;
		memcpy(dataDest, data, writeBytes);
		bytesLeft -= writeBytes;
		data += writeBytes;

		curBlockNum += CV_BLOCK_SIZE;
	}

	// Do actual write
	for (auto block : mBlocks)
	{
		if (block->mData != NULL)
			WriteBlock(block);				
	}	

	mFS.Close();
}
