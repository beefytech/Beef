#include "Heap.h"
#include "DLIList.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

#define CH_REL_TO_ABS(VAL) ((ChBlock*)((uint8*)mMetadata + (VAL)))
#define CH_REL_TO_ABS(VAL) ((ChBlock*)((uint8*)mMetadata + (VAL)))
#define CH_ABS_TO_REL(VAL) (int)((uint8*)(VAL) - (uint8*)mMetadata)

enum ChBlockKind
{
	ChBlockKind_Bad = 0xBEEF0BAD,
	ChBlockKind_Unused = 0xBEEF1212,
	ChBlockKind_Used = 0xBEEF2323,
	ChBlockKind_Merged = 0xBEEF3434,
};

struct ChBlock
{
	ContiguousHeap::AllocRef mPrev;
	ContiguousHeap::AllocRef mNext;
	int mSize;
	ChBlockKind mKind;

	ChBlock()
	{
		mPrev = -1;
		mNext = -1;
		mSize = 0;
		mKind = ChBlockKind_Bad;
	}
};

class ChList
{
public:
	void* mMetadata;
	int32 mHead;
	int32 mTail;

public:
	ChList()
	{
		mHead = -1;
		mTail = -1;
	}

	void Size()
	{
		int size = 0;
		int checkNode = mHead;
		while (checkNode != -1)
		{
			size++;
			checkNode = CH_REL_TO_ABS(checkNode)->mNext;
		}
	}

	void PushBack(int node)
	{
		BF_ASSERT(CH_REL_TO_ABS(node)->mNext == -1);

		if (mHead == -1)
			mHead = node;
		else
		{
			CH_REL_TO_ABS(mTail)->mNext = node;
			CH_REL_TO_ABS(node)->mPrev = mTail;
		}
		mTail = node;
	}

	void AddAfter(int refNode, int newNode)
	{
	 	int32 prevNext = CH_REL_TO_ABS(refNode)->mNext;
	 	CH_REL_TO_ABS(refNode)->mNext = newNode;
		CH_REL_TO_ABS(newNode)->mPrev = refNode;
		CH_REL_TO_ABS(newNode)->mNext = prevNext;
	 	if (prevNext != -1)
			CH_REL_TO_ABS(prevNext)->mPrev = newNode;
	 	if (refNode == mTail)
	 		mTail = newNode;
	}
	
	void Remove(int node)
	{
		if (CH_REL_TO_ABS(node)->mPrev == -1)
		{
			mHead = CH_REL_TO_ABS(node)->mNext;
			if (mHead != -1)
				CH_REL_TO_ABS(mHead)->mPrev = -1;
		}
		else
			CH_REL_TO_ABS(CH_REL_TO_ABS(node)->mPrev)->mNext = CH_REL_TO_ABS(node)->mNext;

		if (CH_REL_TO_ABS(node)->mNext == -1)
		{
			mTail = CH_REL_TO_ABS(node)->mPrev;
			if (mTail != -1)
				CH_REL_TO_ABS(mTail)->mNext = -1;
		}
		else
			CH_REL_TO_ABS(CH_REL_TO_ABS(node)->mNext)->mPrev = CH_REL_TO_ABS(node)->mPrev;

		CH_REL_TO_ABS(node)->mPrev = -1;
		CH_REL_TO_ABS(node)->mNext = -1;
	}

	bool IsEmpty()
	{
		return mHead == -1;
	}
};

//////////////////////////////////////////////////////////////////////////

ContiguousHeap::ContiguousHeap()
{	
	mMetadata = NULL;
	mMemorySize = 0;
	mBlockDataOfs = 0;
	mFreeIdx = 0;
}

ContiguousHeap::~ContiguousHeap()
{
	free(mMetadata);
}

void ContiguousHeap::Clear(int maxAllocSize)
{
	if (mBlockDataOfs == 0)
		return;

	mBlockDataOfs = 0;
	mFreeList.Clear();
	if ((mMemorySize != -1) && (mMemorySize > maxAllocSize))
	{
		free(mMetadata);
		mMetadata = NULL;
		mMemorySize = 0;
		return;
	}

	auto blockList = (ChList*)mMetadata;
	if (blockList->mHead != -1)
	{	
		auto block = CH_REL_TO_ABS(blockList->mHead);
		while (block != NULL)
		{
			block->mKind = ChBlockKind_Bad;
			if (block->mNext == -1)
				break;
			block = CH_REL_TO_ABS(block->mNext);
		}
	}	
	blockList->mHead = -1;
	blockList->mTail = -1;		
}

ContiguousHeap::AllocRef ContiguousHeap::Alloc(int size)
{
	if (size == 0)
		return 0;

	size = BF_ALIGN(size, 16);

	auto blockList = (ChList*)mMetadata;

	if (mFreeIdx >= mFreeList.mSize)
		mFreeIdx = 0;
	while (true)
	{
		for (int itr = 0; itr < (int)mFreeList.size(); itr++)
		{
			auto block = (ChBlock*)((uint8*)mMetadata + mFreeList[mFreeIdx]);
			
			if (block->mKind == ChBlockKind_Merged)
			{
				itr--;
				if (mFreeIdx >= mFreeList.mSize)
					mFreeIdx = 0;
				block->mKind = (ChBlockKind)0;
				mFreeList.RemoveAtFast(mFreeIdx);
				continue;
			}

			BF_ASSERT(block->mKind == ChBlockKind_Unused);

			if (block->mSize >= size)
			{
				mFreeList.RemoveAtFast(mFreeIdx);
				if (block->mSize >= size + 64)
				{
					// Split block
					auto newBlock = new ((uint8*)block + size) ChBlock();
					newBlock->mSize = block->mSize - size;
					newBlock->mKind = ChBlockKind_Unused;
					blockList->AddAfter(CH_ABS_TO_REL(block), CH_ABS_TO_REL(newBlock));
					block->mSize = size;

					mFreeList.Add(CH_ABS_TO_REL(newBlock));
				}

				block->mKind = ChBlockKind_Used;
				return CH_ABS_TO_REL(block);
			}

			mFreeIdx = (mFreeIdx + 1) % mFreeList.mSize;
		}

		int wantSize = BF_MAX(mMemorySize + mMemorySize / 2, mMemorySize + BF_MAX(size, 64 * 1024));
		mMetadata = realloc(mMetadata, wantSize);
			
		memset((uint8*)mMetadata + mMemorySize, 0, wantSize - mMemorySize);

		blockList = (ChList*)mMetadata;
		mMemorySize = wantSize;

		if (mBlockDataOfs == 0)
		{
			blockList = new (mMetadata) ChList();			
			mBlockDataOfs = sizeof(ChList);
		}
		blockList->mMetadata = mMetadata;

		auto block = new ((uint8*)mMetadata + mBlockDataOfs) ChBlock();		
		block->mSize = mMemorySize - mBlockDataOfs;
		block->mKind = ChBlockKind_Unused;
		mBlockDataOfs += block->mSize;
		blockList->PushBack(CH_ABS_TO_REL(block));

		mFreeList.Add(CH_ABS_TO_REL(block));

		if (mFreeIdx >= mFreeList.mSize)
			mFreeIdx = 0;
	}
}

bool ContiguousHeap::Free(AllocRef ref)
{
	if ((ref < 0) || (ref > mMemorySize - sizeof(ChBlock)))
		return false;

	auto blockList = (ChList*)mMetadata;
 	auto block = CH_REL_TO_ABS(ref);
 	
	if (block->mKind != ChBlockKind_Used)
		return false;
	
	int headAccSize = 0;
	auto mergeHead = block;
	while (mergeHead->mPrev != -1)
	{
		auto checkBlock = CH_REL_TO_ABS(mergeHead->mPrev);
		if (checkBlock->mKind != ChBlockKind_Unused)
			break;
		headAccSize += mergeHead->mSize;
		// Mark PREVIOUS as merged, only leave the current alive
		mergeHead->mKind = ChBlockKind_Merged;
		blockList->Remove(CH_ABS_TO_REL(mergeHead));
		mergeHead = checkBlock;
	}

	int tailAccSize = 0;
	if (mergeHead->mNext != -1)
	{		
		auto mergeTail = CH_REL_TO_ABS(mergeHead->mNext);
		while (mergeTail->mKind == ChBlockKind_Unused)
		{
			ChBlock* nextBlock = NULL;
			if (mergeTail->mNext != -1)
				nextBlock = CH_REL_TO_ABS(mergeTail->mNext);
			tailAccSize += mergeTail->mSize;
			mergeTail->mKind = ChBlockKind_Merged;
			blockList->Remove(CH_ABS_TO_REL(mergeTail));
			if (nextBlock == NULL)
				break;
			mergeTail = nextBlock;
		}
	}

	mergeHead->mSize += tailAccSize + headAccSize;
	if ((mergeHead->mKind != ChBlockKind_Unused) && (mergeHead->mKind != ChBlockKind_Merged))
	{
		// If it were MERGED that means it's still in the free list
		mFreeList.Add(CH_ABS_TO_REL(mergeHead));
	}
	mergeHead->mKind = ChBlockKind_Unused;
	return true;
}

void ContiguousHeap::DebugDump()
{
	String str = "Heap Dump:\n";

	auto blockList = (ChList*)mMetadata;

	if (blockList->mHead != -1)
	{
		int totalSize = 0;

		auto block = CH_REL_TO_ABS(blockList->mHead);
		while (block != NULL)
		{
			str += StrFormat("@%d: %d ", CH_ABS_TO_REL(block), block->mSize);
			switch (block->mKind)
			{
			case ChBlockKind_Unused:
				str += "Unused";
				break;
			case ChBlockKind_Used:
				str += "Used";
				break;
			case ChBlockKind_Merged:
				str += "Merged";
				break;
			default:
				str += "??????";				
			}

			str += "\n";

			totalSize += block->mSize;

			if (block->mNext == -1)
				break;
			block = CH_REL_TO_ABS(block->mNext);
		}

		str += StrFormat("Sum: %d Allocated: %d\n", totalSize, mMemorySize);
	}

	str += "\nFree List:\n";
	for (auto val : mFreeList)
		str += StrFormat("@%d\n", val);
	str += "\n";

	OutputDebugStrF(str.c_str());
}
