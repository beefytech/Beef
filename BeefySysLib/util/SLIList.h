#pragma once

#include "../Common.h"

// Singly Linked Intrusive List (no 'node' wrapper, requires mNext member in T)

NS_BF_BEGIN;

template <typename T>
class SLIList
{
public:
	T mHead;
	T mTail;

	struct Iterator
	{
	public:
		T mMember;

	public:
		Iterator(T member)
		{
			mMember = member;
		}

		Iterator& operator++()
		{
			mMember = mMember->mNext;
			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return itr.mMember != mMember;
		}

		bool operator==(const Iterator& itr) const
		{
			return itr.mMember == mMember;
		}

		T operator*()
		{
			return mMember;
		}
	};

	struct RemovableIterator
	{
	public:
		T mPrevVal;
		T* mPrevNextPtr;

	public:
		RemovableIterator(T* headPtr)
		{
			mPrevVal = NULL;
			mPrevNextPtr = headPtr;
		}

		RemovableIterator& operator++()
		{
			T newNode = *mPrevNextPtr;
			mPrevVal = newNode;
			if (newNode != NULL)
			{
				mPrevNextPtr = &newNode->mNext;
			}
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
			return *mPrevNextPtr;
		}
	};

public:
	SLIList()
	{
		mHead = NULL;
		mTail = NULL;
	}

	int Size()
	{
		int size = 0;
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			size++;
			checkNode = checkNode->mNext;
		}
		return size;
	}

	T operator[](int idx)
	{
		int curIdx = 0;
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			if (idx == curIdx)
				return checkNode;
			T next = checkNode->mNext;
			curIdx++;
			checkNode = next;
		}
		return NULL;
	}

	void PushBack(T node)
	{
		BF_ASSERT(node->mNext == NULL);

		if (mHead == NULL)
			mHead = node;
		else
			mTail->mNext = node;
		mTail = node;
	}

	void PushFront(T node)
	{
		BF_ASSERT(node->mNext == NULL);

		if (mHead == NULL)
			mTail = node;
		else
			node->mNext = mHead;
		mHead = node;
	}

	T PopFront()
	{
		T node = mHead;
		mHead = node->mNext;
		node->mNext = NULL;
		return node;
	}	

	void Remove(T node, T prevNode)
	{
		if (prevNode != NULL)
			prevNode->mNext = node->mNext;
		else
			mHead = node->mNext;
		if (mTail == node)
			mTail = prevNode;
		node->mNext = NULL;					
	}

	void Remove(T node)
	{
		T prevNode = NULL;
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			if (checkNode == node)
			{
				Remove(node, prevNode);
				return;
			}
			prevNode = checkNode;
			checkNode = checkNode->mNext;
		}
	}

	void Clear()
	{
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			T next = checkNode->mNext;
			checkNode->mNext = NULL;
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

	void DeleteAll()
	{
		T checkNode = mHead;
		while (checkNode != NULL)
		{
			T next = checkNode->mNext;
			delete checkNode;
			checkNode = next;
		}
		mHead = NULL;
		mTail = NULL;
	}
	
	
	bool IsEmpty()
	{
		return mHead == NULL;
	}

	T front()
	{
		return mHead;
	}

	T back()
	{
		return mTail;
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
		(*itr.mPrevNextPtr) = (*itr)->mNext;		
		if (removedVal == mTail)
		{			
			mTail = itr.mPrevVal;			
		}
				
		return itr;
	}
};

NS_BF_END;