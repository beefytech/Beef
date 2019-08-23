#pragma once

#include "../Common.h"
#include "Array.h"

NS_BF_BEGIN;

#define DEQUE_IDX(i) (this->mVals[((i) + this->mOffset) % this->mAllocSize])
#define DEQUE_IDX_ON(arr, i) ((arr).mVals[((i) + (arr).mOffset) % (arr).mAllocSize])

template <typename T, typename TAlloc = AllocatorCLib<T> >
class DequeBase : public TAlloc
{
public:
	typedef T value_type;

	T* mVals;
	intptr mSize;
	intptr mAllocSize;
	intptr mOffset;

	struct iterator
	{
	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef T value_type;
		typedef intptr difference_type;

		typedef T* pointer;
		typedef T& reference;

	public:
		DequeBase* mDeque;
		intptr mIdx;

	public:
		iterator()
		{
			mIdx = -1;
		}

		iterator(DequeBase* deque, intptr idx)
		{
			mDeque = deque;
			mIdx = idx;
		}

		iterator& operator++()
		{
			mIdx++;
			return *this;
		}

		iterator operator++(int)
		{
			auto prevVal = *this;
			mIdx++;
			return prevVal;
		}

		iterator& operator--()
		{
			mIdx--;
			return *this;
		}

		iterator operator--(int)
		{
			auto prevVal = *this;
			mIdx--;
			return prevVal;
		}

		iterator& operator+=(intptr offset)
		{
			mIdx += offset;
			return *this;
		}

		bool operator!=(const iterator& itr) const
		{
			return itr.mIdx != mIdx;
		}

		bool operator==(const iterator& itr) const
		{
			return itr.mIdx == mIdx;
		}

		intptr operator-(const iterator& itr) const
		{
			return mIdx - itr.mIdx;
		}

		iterator operator+(intptr offset) const
		{
			iterator itr(mIdx + offset);
			return itr;
		}

		iterator operator-(intptr offset) const
		{
			iterator itr(mIdx - offset);
			return itr;
		}

		T& operator*() const
		{
			return DEQUE_IDX_ON(*mDeque, mIdx);
		}

		T* operator->() const
		{
			return &DEQUE_IDX_ON(*mDeque, mIdx);
		}

		bool operator<(const iterator& val2) const
		{
			return mIdx < val2.mIdx;
		}
	};

// 	struct const_iterator
// 	{
// 	public:
// 		typedef std::random_access_iterator_tag iterator_category;
// 		typedef T value_type;
// 		typedef intptr difference_type;
// 
// 		typedef const T* pointer;
// 		typedef const T& reference;
// 
// 	public:
// 		const T* mPtr;
// 
// 	public:
// 		const_iterator(const T* ptr)
// 		{
// 			mPtr = ptr;
// 		}
// 
// 		const_iterator& operator++()
// 		{
// 			mPtr++;
// 			return *this;
// 		}
// 
// 		const_iterator operator++(int)
// 		{
// 			auto prevVal = *this;
// 			mPtr++;
// 			return prevVal;
// 		}
// 
// 		bool operator!=(const const_iterator& itr) const
// 		{
// 			return itr.mPtr != mPtr;
// 		}
// 
// 		bool operator==(const const_iterator& itr) const
// 		{
// 			return itr.mPtr == mPtr;
// 		}
// 
// 		intptr operator-(const iterator& itr) const
// 		{
// 			return mPtr - itr.mPtr;
// 		}
// 
// 		const_iterator operator+(intptr offset) const
// 		{
// 			const_iterator itr(mPtr + offset);
// 			return itr;
// 		}
// 
// 		const T& operator*() const
// 		{
// 			return *mPtr;
// 		}
// 
// 		const T* operator->() const
// 		{
// 			return mPtr;
// 		}
// 
// 		bool operator<(const const_iterator& val2) const
// 		{
// 			return mPtr < val2.mPtr;
// 		}
// 	};

private:


public:
	DequeBase()
	{
		mVals = NULL;
		mSize = 0;
		mAllocSize = 0;
		mOffset = 0;
	}

	DequeBase(DequeBase<T, TAlloc>&& val)
	{
		mVals = val.mVals;
		mSize = val.mSize;
		mAllocSize = val.mAllocSize;
		mOffset = val.mOffset;

		val.mVals = NULL;
		val.mSize = 0;
		val.mAllocSize = 0;
		val.mOffset = 0;
	}

	T& operator[](intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mSize);
		return DEQUE_IDX(idx);
	}

	const T& operator[](intptr idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)mSize);
		return DEQUE_IDX(idx);
	}

	bool operator==(const DequeBase& arrB) const
	{
		if (mSize != arrB.mSize)
			return false;
		for (int i = 0; i < mSize; i++)
			if (DEQUE_IDX(i) != DEQUE_IDX_ON(arrB, i))
				return false;
		return true;
	}

	bool operator!=(const DequeBase& arrB) const
	{
		if (mSize != arrB.mSize)
			return true;
		for (int i = 0; i < mSize; i++)
			if (DEQUE_IDX(i) != DEQUE_IDX_ON(arrB, i))
				return true;
		return true;
	}

// 	const_iterator begin() const
// 	{
// 		return mVals;
// 	}
// 
// 	const_iterator end() const
// 	{
// 		return mVals + mSize;
// 	}

	iterator begin()
	{
		return iterator(this, 0);
	}

	iterator end()
	{
		return iterator(this, mSize);
	}

	T& front() const
	{
		return DEQUE_IDX(0);
	}

	T& back() const
	{
		return DEQUE_IDX(mSize - 1);
	}

	intptr size() const
	{
		return mSize;
	}

	bool empty() const
	{
		return mSize == 0;
	}

	bool IsEmpty() const
	{
		return mSize == 0;
	}

	/*void Free()
	{
	if (mVals != NULL)
	{
	deallocate(mVals);
	}
	mVals = NULL;
	mAllocSize = 0;
	mSize = 0;
	}*/

	T GetSafe(intptr idx)
	{
		if ((idx < 0) || (idx >= mSize))
			return T();
		return DEQUE_IDX(idx);
	}

	T GetLastSafe()
	{
		if (mSize == 0)
			return T();
		return DEQUE_IDX(mSize - 1);
	}

	T GetFirstSafe()
	{
		if (mSize == 0)
			return T();
		return DEQUE_IDX(0);
	}

	bool Contains(T val)
	{
		for (int i = 0; i < mSize; i++)
			if (DEQUE_IDX(i) == val)
				return true;
		return false;
	}

	intptr IndexOf(T val)
	{
		for (int i = 0; i < mSize; i++)
			if (DEQUE_IDX(i) == val)
				return i;
		return -1;
	}

	intptr IndexOf(T val, int startIdx)
	{
		for (int i = startIdx; i < mSize; i++)
			if (DEQUE_IDX(i) == val)
				return i;
		return -1;
	}
};

// NON-POD
template <typename T, typename TAlloc, bool TIsPod>
class DequeImpl : public DequeBase<T, TAlloc>
{
protected:
	void MoveDeque(T* to, T* from, intptr count)
	{
		if (to < from)
		{
			// Prefer in-order moves 
			for (intptr i = 0; i < count; i++)
				new (&to[i]) T(std::move(from[i]));
		}
		else
		{
			for (intptr i = count - 1; i >= 0; i--)
				new (&to[i]) T(std::move(from[i]));
		}
	}
	
 	void Grow(intptr newSize)
 	{
 		T* newVals = TAlloc::allocate(newSize);
 		if (this->mVals != NULL)
 		{
			if (this->mSize > 0)
			{
				for (int i = 0; i < this->mSize; i++)
					new (&newVals[i]) T(std::move(DEQUE_IDX(i)));
			} 				
 			TAlloc::deallocate(this->mVals);
 		}
 		this->mVals = newVals;
 		this->mAllocSize = newSize;
		this->mOffset = 0;
 	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			Grow(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}

public:
	using DequeBase<T, TAlloc>::DequeBase;

	DequeImpl() : DequeBase<T, TAlloc>()
	{

	}

	DequeImpl(const DequeImpl& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;
		this->mOffset = 0;

		*this = val;
	}

	DequeImpl(DequeImpl&& val) : DequeBase<T, TAlloc>(std::move(val))
	{

	}

	~DequeImpl()
	{
		for (int i = 0; i < this->mSize; i++)
			this->mVals[i].~T(); //-V595
		if (this->mVals != NULL)
		{
			TAlloc::deallocate(this->mVals);
		}
	}

	void Resize(intptr size)
	{
		while (size < this->mSize)
			pop_back();
		if (size > this->mSize)
		{
			Reserve(size);
			while (size > this->mSize)
				new (&DEQUE_IDX(this->mSize++)) T();
		}
	}

	void Reserve(intptr size)
	{
		if (size > this->mAllocSize)
			Grow(size);
	}

	void SetSize(intptr size)
	{
		if (size > this->mAllocSize)
			Grow(size);

		this->mSize = size;
	}

	void Clear()
	{
		for (int i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		this->mSize = 0;
		this->mOffset = 0;
	}

	DequeImpl& operator=(const DequeImpl& val)
	{
		if (&val == this)
			return *this;
		for (int i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		this->mSize = 0;
		if (val.mSize > this->mAllocSize)
			Grow(val.mSize);
		Resize(val.mSize);
		for (int i = 0; i < val.mSize; i++)
			new (&this->mVals[i]) T(val.mVals[i]);
		this->mSize = val.mSize;
		return *this;
	}

	DequeImpl& operator=(DequeImpl&& val)
	{
		if (this->mVals != NULL)
		{
			for (int i = 0; i < this->mSize; i++)
				this->mVals[i].~T();
			TAlloc::deallocate(this->mVals);
		}

		this->mVals = val.mVals;
		this->mSize = val.mSize;
		this->mAllocSize = val.mAllocSize;
		this->mOffset = val.mOffset;

		val.mVals = NULL;

		return *this;
	}

	void RemoveAt(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		DEQUE_IDX(idx).~T();

		if (idx == 0)
		{
			this->mOffset = (this->mOffset + 1) % this->mAllocSize;
		}
		else
		{
			// If we're removing the last element then we don't have to move anything
			if (idx != this->mSize - 1)
			{
				for (intptr i = idx; i < this->mSize - 1; i++)
					new (&DEQUE_IDX(i)) T(std::move(DEQUE_IDX(i + 1)));
			}
		}
		this->mSize--;
	}

	// 'Fast' because it's allowed to change item order
	void RemoveAtFast(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		DEQUE_IDX(idx).~T();

		if (idx == 0)
		{
			this->mOffset = (this->mOffset + 1) % this->mAllocSize;
		}
		else
		{
			// If we're removing the last element then we don't have to move anything
			if (idx != this->mSize - 1)
			{
				new (&DEQUE_IDX(idx)) T(std::move(DEQUE_IDX(this->mSize - 1)));
			}
		}
		this->mSize--;
	}

	void RemoveRange(intptr idx, intptr length)
	{
		BF_ASSERT(
			((uintptr)idx < (uintptr)this->mSize) &&
			((uintptr)length > 0) &&
			((uintptr)(idx + length) <= (uintptr)this->mSize));

		for (intptr i = idx; i < idx + length; i++)
			DEQUE_IDX(i).~T();

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - length)
		{
			for (intptr i = idx; i < this->mSize - length; i++)
				new (&DEQUE_IDX(i)) T(std::move(DEQUE_IDX(i + 1)));
		}
		this->mSize -= length;
	}

// 	void Insert(intptr idx, T val)
// 	{
// 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
// 		if (this->mSize >= this->mAllocSize)
// 		{
// 			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;
// 
// 			T* newVals = TAlloc::allocate(newSize);
// 			if (this->mVals != NULL)
// 			{
// 				if (idx > 0) // Copy left of idx
// 					MoveDeque(newVals, this->mVals, idx);
// 				if (idx < this->mSize) // Copy right of idx
// 					MoveDeque(newVals + idx + 1, this->mVals + idx, this->mSize - idx);
// 				TAlloc::deallocate(this->mVals);
// 			}
// 			this->mVals = newVals;
// 			this->mAllocSize = newSize;
// 		}
// 		else if (idx != this->mSize)
// 		{
// 			intptr moveCount = this->mSize - idx;
// 			MoveDeque(this->mVals + idx + 1, this->mVals + idx, moveCount);
// 		}
// 		new (&this->mVals[idx]) T(val);
// 		this->mSize++;
// 	}
// 
// 	void Insert(intptr idx, T* vals, intptr size)
// 	{
// 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
// 		if (this->mSize + size > this->mAllocSize)
// 		{
// 			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);
// 
// 			T* newVals = TAlloc::allocate(newSize);
// 			if (this->mVals != NULL)
// 			{
// 				if (idx > 0) // Copy left of idx
// 					MoveDeque(newVals, this->mVals, idx);
// 				if (idx < this->mSize) // Copy right of idx
// 					MoveDeque(newVals + idx + size, this->mVals + idx, this->mSize - idx);
// 				TAlloc::deallocate(this->mVals);
// 			}
// 			this->mVals = newVals;
// 			this->mAllocSize = newSize;
// 		}
// 		else if (idx != this->mSize)
// 		{
// 			intptr moveCount = this->mSize - idx;
// 			MoveDeque(this->mVals + idx + size, this->mVals + idx, moveCount);
// 		}
// 		for (int i = 0; i < size; i++)
// 			new (&this->mVals[idx + i]) T(vals[i]);
// 		this->mSize += size;
// 	}
// 
// 	void Insert(intptr idx, T val, intptr count)
// 	{
// 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
// 		if (this->mSize + count > this->mAllocSize)
// 		{
// 			intptr newSize = BF_MAX(this->mSize + count, this->mAllocSize + this->mAllocSize / 2 + 1);
// 
// 			T* newVals = TAlloc::allocate(newSize);
// 			if (this->mVals != NULL)
// 			{
// 				if (idx > 0) // Copy left of idx
// 					MoveDeque(this->newVals, this->mVals, idx);
// 				if (idx < this->mSize) // Copy right of idx
// 					MoveDeque(newVals + idx + count, this->mVals + idx, this->mSize - idx);
// 				TAlloc::deallocate(this->mVals);
// 			}
// 			this->mVals = newVals;
// 			this->mAllocSize = newSize;
// 		}
// 		else if (idx != this->mSize)
// 		{
// 			intptr moveCount = this->mSize - idx;
// 			MoveDeque(this->mVals + idx + count, this->mVals + idx, moveCount);
// 		}
// 		for (int i = 0; i < count; i++)
// 			new (&this->mVals[idx + i]) T(val);
// 		this->mSize += count;
// 	}

	bool Remove(T val)
	{
		for (intptr i = 0; i < this->mSize; i++)
		{
			if (this->mVals[i] == val)
			{
				RemoveAt(i);
				return true;
			}
		}

		return false;
	}

// 	typename DequeBase<T, TAlloc>::iterator erase(typename DequeBase<T, TAlloc>::iterator itr)
// 	{
// 		RemoveAt(itr.mPtr - this->mVals);
// 		return itr;
// 	}

	void push_back(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&DEQUE_IDX(this->mSize++)) T(val);
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);
		DEQUE_IDX(this->mSize - 1).~T();
		--this->mSize;
	}

	T PopBack()
	{
		BF_ASSERT(this->mSize > 0);
		T value = DEQUE_IDX(this->mSize - 1);
		DEQUE_IDX(this->mSize - 1).~T();
		--this->mSize;
		return value;
	}

	T RemoveBack()
	{
		BF_ASSERT(this->mSize > 0);		
		DEQUE_IDX(this->mSize - 1).~T();
		--this->mSize;		
	}

	void Add(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&DEQUE_IDX(this->mSize++)) T(val);
	}
};

// POD
template <typename T, typename TAlloc>
class DequeImpl<T, TAlloc, true> : public DequeBase<T, TAlloc>
{
protected:
	void Grow(intptr newSize)
	{
		T* newVals = TAlloc::allocate(newSize);
		if (this->mVals != NULL)
		{
			if (this->mSize > 0)
			{
				intptr endSegSize = this->mAllocSize - this->mOffset;
				if (endSegSize < this->mSize)
				{
					memcpy(newVals, this->mVals + this->mOffset, endSegSize * sizeof(T));
					memcpy(newVals + endSegSize, this->mVals, (this->mSize - endSegSize) * sizeof(T));
				}
				else
				{
					memcpy(newVals, this->mVals + this->mOffset, this->mSize * sizeof(T));
				}				
			}
			this->mOffset = 0;
			TAlloc::deallocate(this->mVals);
		}
		this->mVals = newVals;
		this->mAllocSize = newSize;
		BF_ASSERT(this->mOffset < this->mAllocSize);
	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			Grow(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}

public:
	using DequeBase<T, TAlloc>::DequeBase;

	DequeImpl() : DequeBase<T, TAlloc>::DequeBase()
	{

	}

	DequeImpl(const DequeImpl& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;
		this->mOffset = 0;

		*this = val;
	}

	DequeImpl(DequeImpl&& val) : DequeBase<T, TAlloc>(std::move(val))
	{

	}

	~DequeImpl()
	{
		if (this->mVals != NULL)
		{
			TAlloc::deallocate(this->mVals);
		}
	}

	DequeImpl& operator=(const DequeImpl& val)
	{
		if (&val == this)
			return *this;
		this->mSize = 0;
		if (val.mSize > this->mAllocSize)
			Grow(val.mSize);
		memcpy(this->mVals, val.mVals, val.mSize * sizeof(T));
		this->mSize = val.mSize;
		return *this;
	}

	void Resize(intptr size)
	{
		if (size < this->mSize)
			this->mSize = size;
		else if (size > this->mSize)
		{
			Reserve(size);
			while (size > this->mSize)
			{
				int idx = this->mSize;
				DEQUE_IDX(idx) = T();
				this->mSize = idx;
			}
		}
	}

	void ResizeRaw(intptr size)
	{
		if (size < this->mSize)
			this->mSize = size;
		else if (size > this->mSize)
		{
			Reserve(size);
			this->mSize = size;
		}
	}

	void Reserve(intptr size)
	{
		if (size > this->mAllocSize)
			Grow(size);
	}

	void SetSize(intptr size)
	{
		if (size > this->mAllocSize)
			Grow(size);

		this->mSize = size;
	}

	void Clear()
	{
		this->mSize = 0;
		this->mOffset = 0;
	}

	void RemoveAt(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		if (idx == 0)
		{
			this->mOffset = (this->mOffset + 1) % this->mAllocSize;
		}
		else
		{
			// If we're removing the last element then we don't have to move anything
			if (idx != this->mSize - 1)
			{
				//intptr moveCount = this->mSize - idx - 1;
				//memmove(this->mVals + idx, this->mVals + idx + 1, moveCount * sizeof(T));
				for (intptr i = idx; i < this->mSize - 1; i++)
					DEQUE_IDX(i) = DEQUE_IDX(i + 1);
			}
		}
		this->mSize--;
	}

	// 'Fast' because it's allowed to change item order
	void RemoveAtFast(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		if (idx == 0)
		{
			this->mOffset = (this->mOffset + 1) % this->mAllocSize;
		}
		else
		{
			// If we're removing the last element then we don't have to move anything
			if (idx != this->mSize - 1)
			{
				DEQUE_IDX(idx) = DEQUE_IDX(this->mSize - 1);
			}
		}
		this->mSize--;
	}

	void RemoveRange(intptr idx, intptr length)
	{
		BF_ASSERT(
			((uintptr)idx < (uintptr)this->mSize) &&
			((uintptr)length > 0) &&
			((uintptr)(idx + length) <= (uintptr)this->mSize));

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - length)
		{			
			for (intptr i = idx; i < this->mSize - length; i++)
				DEQUE_IDX(i) = DEQUE_IDX(i + length);
		}
		this->mSize -= length;
	}

 	void Insert(intptr idx, T val)
 	{
 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
 		if (this->mSize >= this->mAllocSize)
 		{
 			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;
 
 			T* newVals = TAlloc::allocate(newSize);
 			if (this->mVals != NULL)
 			{
				for (intptr i = 0; i < idx; i++) // Copy left of idx
					newVals[i] = DEQUE_IDX(i);
				for (intptr i = idx; i < this->mSize; i++) // Copy right of idx
					newVals[i + 1] = DEQUE_IDX(i);
 				TAlloc::deallocate(this->mVals);
 			}
 			this->mVals = newVals;
 			this->mAllocSize = newSize;
			this->mOffset = 0;
 		}
 		else if (idx != this->mSize)
 		{
			for (intptr i = this->mSize; i > idx; i--)
				DEQUE_IDX(i) = DEQUE_IDX(i - 1); 			
 		}
 		DEQUE_IDX(idx) = val;
 		this->mSize++;
 	}
// 
// 	void Insert(intptr idx, T* vals, intptr size)
// 	{
// 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
// 		if (this->mSize + size > this->mAllocSize)
// 		{
// 			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);
// 
// 			T* newVals = TAlloc::allocate(newSize);
// 			if (this->mVals != NULL)
// 			{
// 				if (idx > 0) // Copy left of idx
// 					memmove(newVals, this->mVals, idx * sizeof(T));
// 				if (idx < this->mSize) // Copy right of idx
// 					memmove(newVals + idx + size, this->mVals + idx, (this->mSize - idx) * sizeof(T));
// 				TAlloc::deallocate(this->mVals);
// 			}
// 			this->mVals = newVals;
// 			this->mAllocSize = newSize;
// 		}
// 		else if (idx != this->mSize)
// 		{
// 			intptr moveCount = this->mSize - idx;
// 			memmove(this->mVals + idx + size, this->mVals + idx, moveCount * sizeof(T));
// 		}
// 		for (int i = 0; i < size; i++)
// 			DEQUE_IDX(idx + i) = vals[i];
// 		this->mSize += size;
// 	}
// 
// 	void Insert(intptr idx, T val, intptr size)
// 	{
// 		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
// 		if (this->mSize + size > this->mAllocSize)
// 		{
// 			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);
// 
// 			T* newVals = TAlloc::allocate(newSize);
// 			if (this->mVals != NULL)
// 			{
// 				if (idx > 0) // Copy left of idx
// 					memmove(newVals, this->mVals, idx * sizeof(T));
// 				if (idx < this->mSize) // Copy right of idx
// 					memmove(newVals + idx + size, this->mVals + idx, (this->mSize - idx) * sizeof(T));
// 				TAlloc::deallocate(this->mVals);
// 			}
// 			this->mVals = newVals;
// 			this->mAllocSize = newSize;
// 		}
// 		else if (idx != this->mSize)
// 		{
// 			intptr moveCount = this->mSize - idx;
// 			memmove(this->mVals + idx + size, this->mVals + idx, moveCount * sizeof(T));
// 		}
// 		for (int i = 0; i < size; i++)
// 			DEQUE_IDX(idx + i) = val;
// 		this->mSize += size;
// 	}

	bool Remove(T val)
	{
		for (intptr i = 0; i < this->mSize; i++)
		{
			if (DEQUE_IDX(i) == val)
			{
				RemoveAt(i);
				return true;
			}
		}

		return false;
	}

	bool RemoveAll(T val)
	{
		bool found = false;

		for (intptr i = 0; i < this->mSize; i++)
		{
			if (DEQUE_IDX(i) == val)
			{
				found = true;
				RemoveAt(i);
				i--;
			}
		}

		return found;
	}

	typename DequeBase<T, TAlloc>::iterator erase(typename DequeBase<T, TAlloc>::iterator itr)
	{
		RemoveAt(itr.mIdx);
		return itr;
	}

	void push_back(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		DEQUE_IDX(this->mSize++) = val;
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);
		--this->mSize;
	}

	void RemoveBack()
	{
		BF_ASSERT(this->mSize > 0);
		--this->mSize;
	}

	T PopBack()
	{
		BF_ASSERT(this->mSize > 0);		
		--this->mSize;
		return this->mVals[this->mSize];
	}

	void Add(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		DEQUE_IDX(this->mSize++) = val;
	}
};

template <typename T, typename TAlloc = AllocatorCLib<T> >
class Deque : public DequeImpl<T, TAlloc, std::is_pod<T>::value>
{
public:
	typedef DequeImpl<T, TAlloc, std::is_pod<T>::value> _DequeImpl;

	using _DequeImpl::DequeImpl;
	using _DequeImpl::operator=;
	using _DequeImpl::operator==;
	using _DequeImpl::operator!=;

	Deque() : _DequeImpl()
	{

	}

	Deque(const Deque& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;
		this->mOffset = 0;

		*this = val;
	}

	Deque(Deque&& val) : _DequeImpl(std::move(val))
	{

	}

	_DequeImpl& operator=(const Deque& val)
	{
		return _DequeImpl::operator=(val);
	}

	_DequeImpl& operator=(Deque&& val)
	{
		return _DequeImpl::operator=(val);
	}
};

NS_BF_END;

namespace std
{
	template<typename T>
	struct hash<Beefy::Deque<T> >
	{
		size_t operator()(const Beefy::Deque<T>& val) const
		{
			return HashBytes((const uint8*)val.mVals, sizeof(T) * val.mSize);
		}
	};
}
