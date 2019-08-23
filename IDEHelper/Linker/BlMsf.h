#pragma once

#include "../Beef/BfCommon.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/WorkThread.h"
#include "../Compiler/BfUtil.h"
#include <queue>

NS_BF_BEGIN

struct BlMsfBlock
{
public:
	int mIdx;
	bool mIsUsed;
	void* mData;	

public:
	BlMsfBlock()
	{		
		mIdx = -1;
		mIsUsed = true;
		mData = NULL;
	}
};

class BlCodeView;
class BlMsf;

class BlMsfWorkThread : public WorkThread
{
public:
	BlMsf* mMsf;
	bool mDone;
	CritSect mCritSect;
	SyncEvent mWorkEvent;
	std::deque<BlMsfBlock*> mSortedWriteBlockWorkQueue;
	std::deque<BlMsfBlock*> mWriteBlockWorkQueue;
	bool mSortDirty;


public:
	BlMsfWorkThread();
	~BlMsfWorkThread();

	virtual void Stop() override;
	virtual void Run() override;

	void Add(BlMsfBlock* block);
};

class BlMsf
{
public:
	SysFileStream mFS;
	String mFileName;
	int mFileSize;
	int mAllocatedFileSize;

	BlCodeView* mCodeView;
	int mBlockSize;

	int mFreePageBitmapIdx; // Either 1 or 2

	OwnedVector<BlMsfBlock> mBlocks;	

	BlMsfWorkThread mWorkThread;
	
public:
	void WriteBlock(BlMsfBlock* block);

public:	
	BlMsf();
	~BlMsf();

	bool Create(const StringImpl& fileName);
	
	int Alloc(bool clear = true, bool skipFPMBlocks = true);
	void FlushBlock(int blockIdx);

	void Finish(int rootBlockNum, int streamDirLen);
};

NS_BF_END