#include "Heap.h"
#include "DLIList.h"
#include "HashSet.h"

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

//#define BFCE_DEBUG_HEAP

void ContiguousHeap::Clear(int maxAllocSize)
{
#ifdef BFCE_DEBUG_HEAP
	OutputDebugStrF("ContiguousHeap::Clear\n");
#endif

	if (mBlockDataOfs == 0)
		return;
	
	if ((mMemorySize != -1) && (mMemorySize > maxAllocSize))
	{		
		free(mMetadata);
		mMetadata = NULL;
		mMemorySize = 0;
		mBlockDataOfs = 0;
		mFreeList.Clear();
		return;
	}

	for (auto idx : mFreeList)
	{
		auto block = CH_REL_TO_ABS(idx);
		block->mKind = (ChBlockKind)0;
	}

	auto blockList = (ChList*)mMetadata;
	if (blockList->mHead != -1)
	{	
		auto block = CH_REL_TO_ABS(blockList->mHead);
		while (block != NULL)
		{
			block->mKind = (ChBlockKind)0;
			if (block->mNext == -1)
				break;
			block = CH_REL_TO_ABS(block->mNext);
		}
	}

	blockList->mHead = -1;
	blockList->mTail = -1;
	mBlockDataOfs = 0;
	mFreeList.Clear();

	Validate();
}

static int gAllocCount = 0;

ContiguousHeap::AllocRef ContiguousHeap::Alloc(int size)
{
	if (size == 0)
		return 0;

#ifdef BFCE_DEBUG_HEAP
	int allocCount = ++gAllocCount;
	if (allocCount == 358)
	{
		NOP;
	}	
#endif

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
#ifdef BFCE_DEBUG_HEAP
				OutputDebugStrF("ContiguousHeap::Alloc %d removing merged %d\n", allocCount, (uint8*)block - (uint8*)mMetadata);
#endif

				itr--;
				block->mKind = (ChBlockKind)0;
				mFreeList.RemoveAtFast(mFreeIdx);
				if (mFreeIdx >= mFreeList.mSize)
					mFreeIdx = 0;
				continue;
			}

			BF_ASSERT(block->mKind == ChBlockKind_Unused);

			if (block->mSize >= size)
			{
				mFreeList.RemoveAtFast(mFreeIdx);
				if (block->mSize >= size + 64)
				{
					int oldSize = block->mSize;
					
					// Split block
					auto newBlock = (ChBlock*)((uint8*)block + size);			
					if (newBlock->mKind == 0)
					{
						mFreeList.Add(CH_ABS_TO_REL(newBlock));
					}
					else
					{
						BF_ASSERT(newBlock->mKind == ChBlockKind_Merged);
#ifdef BFCE_DEBUG_HEAP
						BF_ASSERT(mFreeList.Contains(CH_ABS_TO_REL(newBlock)));
#endif
					}
					newBlock->mPrev = -1;
					newBlock->mNext = -1;
					newBlock->mSize = block->mSize - size;
					newBlock->mKind = ChBlockKind_Unused;	
					blockList->AddAfter(CH_ABS_TO_REL(block), CH_ABS_TO_REL(newBlock));
					block->mSize = size;					

#ifdef BFCE_DEBUG_HEAP
					OutputDebugStrF("ContiguousHeap::Alloc %d alloc %d size: %d remainder in %d size: %d\n", allocCount, CH_ABS_TO_REL(block), size, CH_ABS_TO_REL(newBlock), newBlock->mSize);					
#endif
				}
				else
				{
#ifdef BFCE_DEBUG_HEAP
					OutputDebugStrF("ContiguousHeap::Alloc %d alloc %d size: %d\n", allocCount, CH_ABS_TO_REL(block), size);
#endif
				}

				block->mKind = ChBlockKind_Used;

#ifdef BFCE_DEBUG_HEAP
				Validate();
#endif

				return CH_ABS_TO_REL(block);
			}

			mFreeIdx = (mFreeIdx + 1) % mFreeList.mSize;
		}

		int wantSize = BF_MAX(mMemorySize + mMemorySize / 2, mMemorySize + BF_MAX(size, 64 * 1024));
		wantSize = BF_ALIGN(wantSize, 16);
		mMetadata = realloc(mMetadata, wantSize);
		int prevSize = mMemorySize;
			
		memset((uint8*)mMetadata + prevSize, 0, wantSize - prevSize);

		blockList = (ChList*)mMetadata;
		mMemorySize = wantSize;

		ChBlock* block;
		if (mBlockDataOfs == 0)
		{
			blockList = new (mMetadata) ChList();			
			mBlockDataOfs = BF_ALIGN(sizeof(ChList), 16);
			prevSize = mBlockDataOfs;
		}
		
		blockList->mMetadata = mMetadata;
		block = new ((uint8*)mMetadata + prevSize) ChBlock();
		block->mSize = wantSize - prevSize;
		block->mKind = ChBlockKind_Unused;		
		blockList->PushBack(CH_ABS_TO_REL(block));

		mFreeList.Add(CH_ABS_TO_REL(block));

#ifdef BFCE_DEBUG_HEAP
		Validate();
		OutputDebugStrF("ContiguousHeap::Alloc %d alloc %d size: %d\n", allocCount, (uint8*)block - (uint8*)mMetadata, block->mSize);
#endif

		if (mFreeIdx >= mFreeList.mSize)
			mFreeIdx = 0;
	}
}

bool ContiguousHeap::Free(AllocRef ref)
{
	if ((ref < 0) || (ref > mMemorySize - sizeof(ChBlock)))
		return false;

#ifdef BFCE_DEBUG_HEAP
	int allocCount = ++gAllocCount;
	if (allocCount == 64)
	{
		NOP;
	}
#endif

	auto blockList = (ChList*)mMetadata;
 	auto block = CH_REL_TO_ABS(ref);
 	
	if (block->mKind != ChBlockKind_Used)
	{
#ifdef BFCE_DEBUG_HEAP
		OutputDebugStrF("Invalid free\n");
		Validate();
#endif
		return false;
	}
	
#ifdef BFCE_DEBUG_HEAP
	OutputDebugStrF("ContiguousHeap::Free %d block:%d size: %d\n", allocCount, CH_ABS_TO_REL(block), block->mSize);
#endif

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
#ifdef BFCE_DEBUG_HEAP
		OutputDebugStrF(" Setting Merged block %d\n", CH_ABS_TO_REL(mergeHead));
#endif
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
#ifdef BFCE_DEBUG_HEAP
			OutputDebugStrF(" Setting Merged block %d\n", CH_ABS_TO_REL(mergeTail));
#endif
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

	if (mergeHead != block)
	{
		// We weren't in the free list so don't mark as Merged
		block->mKind = (ChBlockKind)0;
	}

#ifdef BFCE_DEBUG_HEAP
	Validate();
#endif

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
	for (auto idx : mFreeList)
	{
		auto block = CH_REL_TO_ABS(idx);
		const char* kind = "??";
		if (block->mKind == ChBlockKind_Unused)
			kind = "Unused";
		else
			kind = "Merged";
		str += StrFormat("@%d %s\n", idx, kind);
	}
	str += "\n";

	OutputDebugStrF(str.c_str());
}

void ContiguousHeap::Validate()
{
	if (!mFreeList.IsEmpty())
	{
		BF_ASSERT_REL(mMetadata != NULL);
	}

	HashSet<int> freeSet;
	for (auto idx : mFreeList)
		freeSet.Add(idx);
	
	auto blockList = (ChList*)mMetadata;

	bool deepValidate = true;

	if (deepValidate)
	{
		if (blockList->mHead != -1)
		{
			int totalSize = 0;

			auto block = CH_REL_TO_ABS(blockList->mHead);
			auto blockEnd = (ChBlock*)((uint8*)blockList + mMemorySize);

			while (block != blockEnd)
			{
				switch (block->mKind)
				{
				case (ChBlockKind)0:
					BF_ASSERT_REL(!freeSet.Contains(CH_ABS_TO_REL(block)));
					break;
				case ChBlockKind_Unused:
					BF_ASSERT_REL(freeSet.Remove(CH_ABS_TO_REL(block)));
					break;
				case ChBlockKind_Merged:
					BF_ASSERT_REL(freeSet.Remove(CH_ABS_TO_REL(block)));
					break;
				case ChBlockKind_Used:
					BF_ASSERT_REL(!freeSet.Contains(CH_ABS_TO_REL(block)));
					break;
				default:
					BF_FATAL("Invalid state");
				}

				block = (ChBlock*)((uint8*)block + 16);				
			}
			BF_ASSERT_REL(freeSet.IsEmpty());
		}
	}
	else
	{
		if (blockList->mHead != -1)
		{
			int totalSize = 0;

			auto block = CH_REL_TO_ABS(blockList->mHead);
			while (block != NULL)
			{
				switch (block->mKind)
				{
				case ChBlockKind_Unused:
					BF_ASSERT_REL(freeSet.Remove(CH_ABS_TO_REL(block)));
					break;
				case ChBlockKind_Used:
					BF_ASSERT_REL(!freeSet.Contains(CH_ABS_TO_REL(block)));
					break;
				default:
					BF_FATAL("Invalid state");
				}

				if (block->mNext == -1)
					break;
				block = CH_REL_TO_ABS(block->mNext);
			}
		}
	}

	for (auto idx : freeSet)
	{
		auto block = CH_REL_TO_ABS(idx);
		BF_ASSERT_REL(block->mKind == ChBlockKind_Merged);
	}

	//BF_ASSERT(freeSet.IsEmpty());
}
