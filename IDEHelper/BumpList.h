#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_BEGIN

template <typename T>
class BumpList
{
public:
	struct Node
	{		
		T mValue;
		Node* mNext;
	};

	struct Iterator
	{
	public:
		Node* mNode;		

	public:
		Iterator(Node* node)
		{
			mNode = node;
		}
		
		Iterator& operator++()
		{
			mNode = mNode->mNext;
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return mNode != itr.mNode;
		}

		bool operator==(const Iterator& itr) const
		{
			return mNode == itr.mNode;
		}

		T operator*()
		{
			return mNode->mValue;
		}
	};

	struct RemovableIterator
	{
	public:		
		Node** mPrevNextPtr;

	public:
		RemovableIterator(Node** headPtr)
		{			
			mPrevNextPtr = headPtr;
		}

		RemovableIterator& operator++()
		{
			Node* newNode = *mPrevNextPtr;			
			if (newNode != NULL)			
				mPrevNextPtr = &newNode->mNext;			
			return *this;
		}

		bool operator!=(const RemovableIterator& itr) const
		{
			if (itr.mPrevNextPtr == NULL)
				return *mPrevNextPtr != NULL;
			return *itr.mPrevNextPtr != *mPrevNextPtr;
		}

		bool operator==(const RemovableIterator& itr) const
		{
			if (itr.mPrevNextPtr == NULL)
				return *mPrevNextPtr == NULL;
			return *itr.mPrevNextPtr == *mPrevNextPtr;
		}

		T operator*()
		{
			return (*mPrevNextPtr)->mValue;
		}
	};

public:
	Node* mHead;

	BumpList()
	{
		mHead = NULL;
	}

	void PushFront(T value, BumpAllocator* bumpAllocator)
	{				
		auto newHead = bumpAllocator->Alloc<Node>();
		newHead->mValue = value;
		newHead->mNext = mHead;
		mHead = newHead;
	}

	bool IsEmpty()
	{
		return mHead == NULL;
	}

	T front()
	{
		return mHead->mValue;
	}

	RemovableIterator begin()
	{
		return RemovableIterator(&mHead);
	}

	RemovableIterator end()
	{
		return RemovableIterator(NULL);
	}

	RemovableIterator erase(RemovableIterator itr)
	{
		T removedVal = *itr;
		(*itr.mPrevNextPtr) = (*itr.mPrevNextPtr)->mNext;
		return itr;
	}
};

// This is a simple construct that acts as a push-only stack.
//  The 'NodeBlock' is designed to take advantage of being exactly 16-byte aligned when storing
//  Ts of 32-bit pointers.
template <typename T>
class BlockBumpList
{
public:
	struct NodeBlock
	{
		static const int NodeCount = 3;

		T mVals[NodeCount];
		NodeBlock* mNextBlock;

		NodeBlock()
		{
			mVals[0] = NULL;
			mVals[1] = NULL;
			mVals[2] = NULL;
			mNextBlock = NULL;
		}
	};	

	struct Iterator
	{
	public:
		NodeBlock* mNodeBlock;
		int mMemberIdx;

	public:
		Iterator(NodeBlock* nodeBlock)
		{
			SetNodeBlock(nodeBlock);			
		}

		void SetNodeBlock(NodeBlock* nodeBlock)
		{
			mNodeBlock = nodeBlock;
			if (mNodeBlock != NULL)
			{
				mMemberIdx = NodeBlock::NodeCount - 1;
				while ((mMemberIdx >= 0) && (!mNodeBlock->mVals[mMemberIdx]))
					mMemberIdx--;
			}
			else
				mMemberIdx = -1;
		}

		Iterator& operator++()
		{
			mMemberIdx--;
			if (mMemberIdx < 0)
				SetNodeBlock(mNodeBlock->mNextBlock);							
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return (mNodeBlock != itr.mNodeBlock) || (mMemberIdx != itr.mMemberIdx);
		}

		bool operator==(const Iterator& itr) const
		{
			return (mNodeBlock == itr.mNodeBlock) && (mMemberIdx == itr.mMemberIdx);
		}

		T operator*()
		{
			return mNodeBlock->mVals[mMemberIdx];
		}
	};

public:
	NodeBlock* mHeadBlock;

	BlockBumpList()
	{
		mHeadBlock = NULL;
	}	

	void Add(T value, BumpAllocator* bumpAllocator)
	{
		if (mHeadBlock == NULL)		
			mHeadBlock = bumpAllocator->Alloc<NodeBlock>();
		else if (mHeadBlock->mVals[NodeBlock::NodeCount - 1])
		{
			auto newHeadBlock = bumpAllocator->Alloc<NodeBlock>();
			newHeadBlock->mNextBlock = mHeadBlock;
			mHeadBlock = newHeadBlock;
		}
		for (int i = 0; i < NodeBlock::NodeCount; i++)
		{
			if (!mHeadBlock->mVals[i])
			{
				mHeadBlock->mVals[i] = value;
				break;
			}
		}
	}

	Iterator begin()
	{
		return Iterator(mHeadBlock);
	}

	Iterator end()
	{
		return Iterator(NULL);
	}
};

NS_BF_END