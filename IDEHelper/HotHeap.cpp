#include "HotHeap.h"

USING_NS_BF_DBG;

HotHeap::HotHeap(addr_target hotStart, int size)
{
	mHotAreaStart = hotStart;
	mHotAreaSize = size;
	mCurBlockIdx = 0;
	mBlockAllocIdx = 0;

	int blockCount = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	mHotAreaUsed.Resize(blockCount);
}

HotHeap::HotHeap()
{	
	mHotAreaStart = 0xFFFFFFFF;
	mHotAreaSize = 0;
	mCurBlockIdx = 0;
	mBlockAllocIdx = 0;
}

void HotHeap::AddTrackedRegion(addr_target hotStart, int size)
{
	addr_target maxEnd = hotStart + size;
	if (mHotAreaStart != 0xFFFFFFFF)
		maxEnd = std::max(mHotAreaStart + mHotAreaSize, hotStart + size);

	mHotAreaStart = std::min(mHotAreaStart, hotStart);
	mHotAreaSize = (int)(maxEnd - mHotAreaStart);

	int blockCount = (mHotAreaSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
	mHotAreaUsed.Resize(blockCount);

	mTrackedMap[hotStart] = size;
}

addr_target HotHeap::Alloc(int size)
{
	int blockCount = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	int triesLeft = (int)mHotAreaUsed.size();
	while (triesLeft > 0)
	{
		if (mCurBlockIdx + blockCount < (int)mHotAreaUsed.size())
		{
			bool isOccupied = false;
			for (int checkIdx = 0; checkIdx < blockCount; checkIdx++)
			{
				if ((mHotAreaUsed[mCurBlockIdx + checkIdx] & HotUseFlags_Allocated) != 0)
				{
					isOccupied = true;

					mCurBlockIdx = mCurBlockIdx + 1;
					triesLeft -= checkIdx + 1;
					break;
				}
			}

			if (!isOccupied)
			{				
				mBlockAllocIdx += blockCount;
				addr_target addr = mHotAreaStart + mCurBlockIdx * BLOCK_SIZE;

				OutputDebugStrF("HotHeap Alloc %d length %d %@\n", mCurBlockIdx, blockCount, addr);

				for (int checkIdx = 0; checkIdx < blockCount; checkIdx++)
				{					
					mHotAreaUsed[mCurBlockIdx] = (HotUseFlags)(mHotAreaUsed[mCurBlockIdx] | HotUseFlags_Allocated);
					mCurBlockIdx++;
				}												
				return addr;
			}
		}
		else
		{
			mCurBlockIdx = 0;
			triesLeft--;
		}
	}

	return 0;
}

void HotHeap::Release(addr_target addr, int size)
{
	/*auto itr = mTrackedMap.find(addr);
	if (itr != mTrackedMap.end())
		mTrackedMap.erase(itr);*/
	mTrackedMap.Remove(addr);

	int blockStart = (int)(addr - mHotAreaStart) / BLOCK_SIZE;
	int blockCount = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	OutputDebugStrF("HotHeap Release %d length %d\n", blockStart, blockCount);

	for (int blockNum = blockStart; blockNum < blockStart + blockCount; blockNum++)
		mHotAreaUsed[blockNum] = HotUseFlags_None;
}

bool HotHeap::IsReferenced(addr_target addr, int size)
{	
	int blockStart = (int)(addr - mHotAreaStart) / BLOCK_SIZE;
	int blockCount = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	for (int blockNum = blockStart; blockNum < blockStart + blockCount; blockNum++)
		if ((mHotAreaUsed[blockNum] & HotUseFlags_Referenced) != 0)
			return true;
	return false;
}

void HotHeap::MarkBlockReferenced(addr_target addr)
{
	int blockNum = (int)(addr - mHotAreaStart) / BLOCK_SIZE;	
	mHotAreaUsed[blockNum] = (HotUseFlags)(mHotAreaUsed[blockNum] | HotUseFlags_Referenced);
}

void HotHeap::ClearReferencedFlags()
{
	for (int blockNum = 0; blockNum < (int)mHotAreaUsed.size(); blockNum++)
	{
		mHotAreaUsed[blockNum] = (HotUseFlags)(mHotAreaUsed[blockNum] & ~HotUseFlags_Referenced);
	}
}

intptr_target HotHeap::GetUsedSize()
{	
	if (mTrackedMap.size() != 0)
	{
		int usedBytes = 0;
		for (auto& pair : mTrackedMap)
			usedBytes += pair.mValue;
		return usedBytes;
	}

	int usedCount = 0;	
	for (auto flag : mHotAreaUsed)
		if ((flag & HotUseFlags_Allocated) != 0)
			usedCount++;
	return (intptr_target)usedCount * BLOCK_SIZE;
}