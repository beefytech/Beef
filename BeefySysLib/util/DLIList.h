#pragma once

#include "../Common.h"

// Doubly Linked Intrusive List (no 'node' wrapper, requires mNext member in T)

NS_BF_BEGIN;

template <typename T>
class DLIList
{
public:
	T mHead;
	T mTail;

	struct RemovableIterator
	{
	public:		
		T* mPrevNextPtr;

	public:
		RemovableIterator(T* prevNextPtr)
		{						
			mPrevNextPtr = prevNextPtr;
		}

		RemovableIterator& operator++()
		{
			T newNode = *mPrevNextPtr;			
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

		T operator*()
		{			
			return *mPrevNextPtr;
		}
	};

public:
	DLIList()
	{
		mHead = NULL;
		mTail = NULL;
	}

	void Size()
	{
		int size = 0;
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			size++;
			checkNode = checkNode->mNext;
		}
	}

	void PushBack(T node)
	{
		BF_ASSERT(node->mNext == NULL);

		if (mHead == NULL)
			mHead = node;
		else
		{
			mTail->mNext = node;
			node->mPrev = mTail;
		}
		mTail = node;
	}

	void AddAfter(T refNode, T newNode)
	{
		auto prevNext = refNode->mNext;
		refNode->mNext = newNode;
		newNode->mPrev = refNode;
		newNode->mNext = prevNext;
		if (prevNext != NULL)
			prevNext->mPrev = newNode;
		if (refNode == mTail)
			mTail = newNode;
	}

	void PushFront(T node)
	{
		if (mHead == NULL)
			mTail = node;
		else
		{
			mHead->mPrev = node;
			node->mNext = mHead;
		}
		mHead = node;
	}

	T PopFront()
	{
		T node = mHead;
		mHead = node->mNext;
		mHead->mPrev = NULL;
		node->mNext = NULL;
		return node;
	}

	void Remove(T node)
	{		
		if (node->mPrev == NULL)
		{
			mHead = node->mNext;
			if (mHead != NULL)
				mHead->mPrev = NULL;
		}
		else		
			node->mPrev->mNext = node->mNext;

		if (node->mNext == NULL)
		{
			mTail = node->mPrev;
			if (mTail != NULL)
				mTail->mNext = NULL;
		}
		else		
			node->mNext->mPrev = node->mPrev;					

		node->mPrev = NULL;
		node->mNext = NULL;
	}

	void Replace(T oldNode, T newNode)
	{		
		if (oldNode->mPrev != NULL)
		{
			oldNode->mPrev->mNext = newNode;
			newNode->mPrev = oldNode->mPrev;
			oldNode->mPrev = NULL;
		}
		else
			mHead = newNode;

		if (oldNode->mNext != NULL)
		{
			oldNode->mNext->mPrev = newNode;
			newNode->mNext = oldNode->mNext;
			oldNode->mNext = NULL;
		}
		else
			mTail = newNode;
	}

	void Clear()
	{
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			T next = checkNode->mNext;
			checkNode->mNext = NULL;
			checkNode->mPrev = NULL;
			checkNode = next;
		}

		mHead = NULL;
		mTail = NULL;
	}

	void ClearFast()
	{
		mHead = NULL;
		mTail = NULL;
	}

	bool IsEmpty()
	{
		return mHead == NULL;
	}

	RemovableIterator begin()
	{
		return RemovableIterator(&mHead);
	}

	RemovableIterator end()
	{
		return RemovableIterator(NULL);
	}
};

NS_BF_END;