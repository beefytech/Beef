#pragma once

#include "BFPlatform.h"

NS_BF_BEGIN;

template <typename T>
class AllocatorCLib
{
public:
	T* allocate(intptr count)
	{
		return (T*)malloc(sizeof(T) * count);
	}

	void deallocate(T* ptr)
	{
		free(ptr);
	}

	void* rawAllocate(intptr size)
	{
		return malloc(size);
	}

	void rawDeallocate(void* ptr)
	{
		free(ptr);
	}
};

template <typename T, typename TAlloc = AllocatorCLib<T> >
class ArrayBase : public TAlloc
{
public:
	typedef T value_type;
	typedef int int_cosize;

	T* mVals;
	int_cosize mSize;
	int_cosize mAllocSize;

	struct iterator
	{
	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef T value_type;
		typedef intptr difference_type;

		typedef T* pointer;
		typedef T& reference;

	public:
		T* mPtr;

	public:
		iterator()
		{
			mPtr = NULL;
		}

		iterator(T* ptr)
		{
			mPtr = ptr;
		}

		iterator& operator++()
		{
			mPtr++;
			return *this;
		}

		iterator operator++(int)
		{
			auto prevVal = *this;
			mPtr++;
			return prevVal;
		}

		iterator& operator--()
		{
			mPtr--;
			return *this;
		}

		iterator operator--(int)
		{
			auto prevVal = *this;
			mPtr--;
			return prevVal;
		}

		iterator& operator+=(intptr offset)
		{
			mPtr += offset;
			return *this;
		}

		bool operator!=(const iterator& itr) const
		{
			return itr.mPtr != mPtr;
		}

		bool operator==(const iterator& itr) const
		{
			return itr.mPtr == mPtr;
		}

		intptr operator-(const iterator& itr) const
		{
			return mPtr - itr.mPtr;
		}

		iterator operator+(intptr offset) const
		{
			iterator itr(mPtr + offset);
			return itr;
		}

		iterator operator-(intptr offset) const
		{
			iterator itr(mPtr - offset);
			return itr;
		}

		T& operator*() const
		{
			return *mPtr;
		}

		T* operator->() const
		{
			return mPtr;
		}

		bool operator<(const iterator& val2) const
		{
			return mPtr < val2.mPtr;
		}
	};

	struct const_iterator
	{
	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef T value_type;
		typedef intptr difference_type;

		typedef const T* pointer;
		typedef const T& reference;

	public:
		const T* mPtr;

	public:
		const_iterator(const T* ptr)
		{
			mPtr = ptr;
		}

		const_iterator& operator++()
		{
			mPtr++;
			return *this;
		}

		const_iterator operator++(int)
		{
			auto prevVal = *this;
			mPtr++;
			return prevVal;
		}

		bool operator!=(const const_iterator& itr) const
		{
			return itr.mPtr != mPtr;
		}

		bool operator==(const const_iterator& itr) const
		{
			return itr.mPtr == mPtr;
		}

		intptr operator-(const iterator& itr) const
		{
			return mPtr - itr.mPtr;
		}

		const_iterator operator+(intptr offset) const
		{
			const_iterator itr(mPtr + offset);
			return itr;
		}

		const T& operator*() const
		{
			return *mPtr;
		}

		const T* operator->() const
		{
			return mPtr;
		}

		bool operator<(const const_iterator& val2) const
		{
			return mPtr < val2.mPtr;
		}
	};

private:
	

public:
	ArrayBase()
	{
		mVals = NULL;
		mSize = 0;
		mAllocSize = 0;
	}
	
	ArrayBase(ArrayBase<T, TAlloc>&& val)
	{
		mVals = val.mVals;
		mSize = val.mSize;
		mAllocSize = val.mAllocSize;

		val.mVals = NULL;
		val.mSize = 0;
		val.mAllocSize = 0;
	}
		
	T& operator[](intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mSize);
		return mVals[idx];
	}

	const T& operator[](intptr idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)mSize);
		return mVals[idx];
	}

	bool operator==(const ArrayBase& arrB) const
	{
		if (mSize != arrB.mSize)
			return false;
		for (intptr i = 0; i < mSize; i++)
			if (mVals[i] != arrB.mVals[i])
				return false;
		return true;
	}

	bool operator!=(const ArrayBase& arrB) const
	{
		if (mSize != arrB.mSize)
			return true;
		for (intptr i = 0; i < mSize; i++)
			if (mVals[i] != arrB.mVals[i])
				return true;
		return true;
	}

	const_iterator begin() const
	{
		return mVals;
	}

	const_iterator end() const
	{
		return mVals + mSize;
	}

	iterator begin()
	{
		return mVals;
	}

	iterator end()
	{
		return mVals + mSize;
	}

	T& front() const
	{
		return mVals[0];
	}

	T& back() const
	{
		return mVals[mSize - 1];
	}

	intptr size() const
	{
		return mSize;
	}

	int Count() const
	{
		return (int)mSize;
	}

	bool empty() const
	{
		return mSize == 0;
	}

	bool IsEmpty() const
	{
		return mSize == 0;
	}

	intptr GetFreeCount()
	{
		return mAllocSize - mSize;
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
		return mVals[idx];
	}

	T GetLastSafe()
	{
		if (mSize == 0)
			return T();
		return mVals[mSize - 1];
	}

	T GetFirstSafe()
	{
		if (mSize == 0)
			return T();
		return mVals[0];
	}

	bool Contains(const T& val)
	{
		for (intptr i = 0; i < mSize; i++)
			if (mVals[i] == val)
				return true;
		return false;
	}

	intptr IndexOf(const T& val)
	{
		for (intptr i = 0; i < mSize; i++)
			if (mVals[i] == val)
				return i;
		return -1;
	}
	
	intptr IndexOf(const T& val, int startIdx)
	{
		for (intptr i = startIdx; i < mSize; i++)
			if (mVals[i] == val)
				return i;
		return -1;
	}

	intptr LastIndexOf(const T& val)
	{
		for (intptr i = mSize - 1; i >= 0; i--)
			if (mVals[i] == val)
				return i;
		return -1;
	}

	void MoveTo(ArrayBase<T>& dest)
	{
		dest.mVals = this->mVals;
		dest.mSize = this->mSize;
		dest.mAllocSize = this->mAllocSize;

		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;
	}

	void Sort(std::function<bool(const T&, const T&)> pred)
	{
		std::sort(this->begin(), this->end(), pred);
	}
};

// NON-POD
template <typename T, typename TAlloc, bool TIsPod>
class ArrayImpl : public ArrayBase<T, TAlloc>
{
public:
	typedef int int_cosize;

protected:
	void MoveArray(T* to, T* from, intptr count)
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

	void SetBufferSize(intptr newSize)
	{
		T* newVals = TAlloc::allocate(newSize);
		if (this->mVals != NULL)
		{
			if (this->mSize > 0)
				MoveArray(newVals, this->mVals, this->mSize);
			TAlloc::deallocate(this->mVals);
		}
		this->mVals = newVals;
		this->mAllocSize = (int_cosize)newSize;
	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			SetBufferSize(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}

public:
	using ArrayBase<T, TAlloc>::ArrayBase;

	ArrayImpl() : ArrayBase<T, TAlloc>()
	{

	}

	ArrayImpl(const ArrayImpl& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		*this = val;
	}

	ArrayImpl(ArrayImpl&& val) : ArrayBase<T, TAlloc>(std::move(val))
	{

	}

	~ArrayImpl()
	{		
		for (intptr i = 0; i < this->mSize; i++)
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
				new (&this->mVals[this->mSize++]) T();
		}
	}

	void Reserve(intptr size)
	{
		if (size > this->mAllocSize)
			SetBufferSize(size);
	}

	void SetSize(intptr size)
	{
		if (size > this->mAllocSize)
			SetBufferSize(size);

		this->mSize = (int_cosize)size;
	}

	void Clear()
	{
		for (intptr i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		this->mSize = 0;
	}

	void Dispose()
	{
		Clear();
		delete this->mVals;
		this->mVals = NULL;
		this->mAllocSize = 0;
	}

	ArrayImpl& operator=(const ArrayImpl& val)
	{
		if (&val == this)
			return *this;
		for (intptr i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		this->mSize = 0;
		if (val.mSize > this->mAllocSize)
			SetBufferSize(val.mSize);
		Resize(val.mSize);			
		for (intptr i = 0; i < val.mSize; i++)
			new (&this->mVals[i]) T(val.mVals[i]);
		this->mSize = val.mSize;
		return *this;
	}

	ArrayImpl& operator=(ArrayImpl&& val)
	{
		if (this->mVals != NULL)
		{
			for (intptr i = 0; i < this->mSize; i++)
				this->mVals[i].~T();
			TAlloc::deallocate(this->mVals);
		}
		
		this->mVals = val.mVals;
		this->mSize = val.mSize;
		this->mAllocSize = val.mAllocSize;

		val.mVals = NULL;

		return *this;
	}

	void RemoveAt(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		this->mVals[idx].~T();

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - 1)
		{
			intptr moveCount = this->mSize - idx - 1;
			MoveArray(this->mVals + idx, this->mVals + idx + 1, moveCount);
		}
		this->mSize--;
	}

	// 'Fast' because it's allowed to change item order
	void RemoveAtFast(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		this->mVals[idx].~T();

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - 1)
		{
			new (&this->mVals[idx]) T(std::move(this->mVals[this->mSize - 1]));
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
			this->mVals[i].~T();

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - length)
		{
			intptr moveCount = this->mSize - idx - length;
			MoveArray(this->mVals + idx, this->mVals + idx + length, moveCount);
		}
		this->mSize -= (int_cosize)length;
	}

	void Insert(intptr idx, const T& val)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize >= this->mAllocSize)
		{
			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					MoveArray(newVals, this->mVals, idx);
				if (idx < this->mSize) // Copy right of idx
					MoveArray(newVals + idx + 1, this->mVals + idx, this->mSize - idx);
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			MoveArray(this->mVals + idx + 1, this->mVals + idx, moveCount);
		}
		new (&this->mVals[idx]) T(val);
		this->mSize++;
	}

	void Insert(intptr idx, T* vals, intptr size)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize + size > this->mAllocSize)
		{
			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					MoveArray(newVals, this->mVals, idx);
				if (idx < this->mSize) // Copy right of idx
					MoveArray(newVals + idx + size, this->mVals + idx, this->mSize - idx);
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			MoveArray(this->mVals + idx + size, this->mVals + idx, moveCount);
		}
		for (intptr i = 0; i < size; i++)
			new (&this->mVals[idx + i]) T(vals[i]);
		this->mSize += (int_cosize)size;
	}

	void Insert(intptr idx, const T& val, intptr count)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize + count > this->mAllocSize)
		{
			intptr newSize = BF_MAX(this->mSize + count, this->mAllocSize + this->mAllocSize / 2 + 1);

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					MoveArray(this->newVals, this->mVals, idx);
				if (idx < this->mSize) // Copy right of idx
					MoveArray(newVals + idx + count, this->mVals + idx, this->mSize - idx);
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			MoveArray(this->mVals + idx + count, this->mVals + idx, moveCount);
		}
		for (intptr i = 0; i < count; i++)
			new (&this->mVals[idx + i]) T(val);
		this->mSize += (int_cosize)count;
	}

	bool Remove(const T& val)
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

	typename ArrayBase<T, TAlloc>::iterator erase(typename ArrayBase<T, TAlloc>::iterator itr)
	{
		RemoveAt(itr.mPtr - this->mVals);
		return itr;
	}

	void push_back(const T& val)
	{
		if (this->mSize >= this->mAllocSize)
			SetBufferSize(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&this->mVals[this->mSize++]) T(val);
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);
		this->mVals[this->mSize - 1].~T();
		--this->mSize;
	}

	void Add(const T& val)
	{
		if (this->mSize >= this->mAllocSize)
			SetBufferSize(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&this->mVals[this->mSize++]) T(val);
	}
};

// POD
template <typename T, typename TAlloc>
class ArrayImpl<T, TAlloc, true> : public ArrayBase<T, TAlloc>
{
public:
	typedef int int_cosize;

protected:
	void SetBufferSize(intptr newSize)
	{
		T* newVals = TAlloc::allocate(newSize);
		if (this->mVals != NULL)
		{
			if (this->mSize > 0)
				memcpy(newVals, this->mVals, this->mSize * sizeof(T));
			TAlloc::deallocate(this->mVals);
		}
		this->mVals = newVals;
		this->mAllocSize = (int_cosize)newSize;
	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			SetBufferSize(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}

public:
	using ArrayBase<T, TAlloc>::ArrayBase;

	ArrayImpl() : ArrayBase<T, TAlloc>::ArrayBase()
	{

	}

	ArrayImpl(const ArrayImpl& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		*this = val;
	}

	ArrayImpl(ArrayImpl&& val) : ArrayBase<T, TAlloc>(std::move(val))
	{

	}

	~ArrayImpl()
	{				
		if (this->mVals != NULL)
		{
			TAlloc::deallocate(this->mVals);
		}
	}
	
	ArrayImpl& operator=(const ArrayImpl& val)
	{
		if (&val == this)
			return *this;
		this->mSize = 0;
		if (val.mSize > this->mAllocSize)
			SetBufferSize(val.mSize);
		memcpy(this->mVals, val.mVals, val.mSize * sizeof(T));
		this->mSize = val.mSize;
		return *this;
	}

	void Resize(intptr size)
	{
		if (size < this->mSize)
			this->mSize = (int_cosize)size;
		else if (size > this->mSize)
		{
			Reserve(size);
			while (size > this->mSize)
				this->mVals[this->mSize++] = T();
		}		
	}

	void ResizeRaw(intptr size)
	{		
		if (size < this->mSize)
			this->mSize = (int_cosize)size;
		else if (size > this->mSize)
		{
			Reserve(size);
			this->mSize = (int_cosize)size;
		}		
	}

	void Reserve(intptr size)
	{
		if (size > this->mAllocSize)
			SetBufferSize(size);
	}

	void SetSize(intptr size)
	{
		if (size > this->mAllocSize)
			SetBufferSize(size);

		this->mSize = (int_cosize)size;
	}

	void Clear()
	{		
		this->mSize = 0;
	}

	void TrimExcess()
	{
		if (this->mSize > this->mAllocSize)
			SetBufferSize(this->mSize);
	}

	void Dispose()
	{
		Clear();
		delete this->mVals;
		this->mVals = NULL;
		this->mAllocSize = 0;
	}

	void RemoveAt(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - 1)
		{
			intptr moveCount = this->mSize - idx - 1;
			memmove(this->mVals + idx, this->mVals + idx + 1, moveCount * sizeof(T));
		}
		this->mSize--;
	}

	// 'Fast' because it's allowed to change item order
	void RemoveAtFast(intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);

		// If we're removing the last element then we don't have to move anything
		if (idx != this->mSize - 1)
		{
			this->mVals[idx] = this->mVals[this->mSize - 1];
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
			intptr moveCount = this->mSize - idx - length;
			memmove(this->mVals + idx, this->mVals + idx + length, moveCount * sizeof(T));
		}
		this->mSize -= (int_cosize)length;
	}

	void Insert(intptr idx, const T& val)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize >= this->mAllocSize)
		{
			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					memmove(newVals, this->mVals, idx * sizeof(T));
				if (idx < this->mSize) // Copy right of idx
					memmove(newVals + idx + 1, this->mVals + idx, (this->mSize - idx) * sizeof(T));
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			memmove(this->mVals + idx + 1, this->mVals + idx, moveCount * sizeof(T));
		}
		this->mVals[idx] = val;
		this->mSize++;
	}

	void Insert(intptr idx, T* vals, intptr size)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize + size > this->mAllocSize)
		{
			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					memmove(newVals, this->mVals, idx * sizeof(T));
				if (idx < this->mSize) // Copy right of idx
					memmove(newVals + idx + size, this->mVals + idx, (this->mSize - idx) * sizeof(T));
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			memmove(this->mVals + idx + size, this->mVals + idx, moveCount * sizeof(T));
		}
		for (intptr i = 0; i < size; i++)
			this->mVals[idx + i] = vals[i];
		this->mSize += (int_cosize)size;
	}

	void Insert(intptr idx, const T& val, intptr size)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize + size > this->mAllocSize)
		{
			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);

			T* newVals = TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					memmove(newVals, this->mVals, idx * sizeof(T));
				if (idx < this->mSize) // Copy right of idx
					memmove(newVals + idx + size, this->mVals + idx, (this->mSize - idx) * sizeof(T));
				TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = (int_cosize)newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			memmove(this->mVals + idx + size, this->mVals + idx, moveCount * sizeof(T));
		}
		for (intptr i = 0; i < size; i++)
			this->mVals[idx + i] = val;
		this->mSize += (int_cosize)size;
	}

	bool Remove(const T& val)
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

	bool RemoveAll(const T& val)
	{
		bool found = false;

		for (intptr i = 0; i < this->mSize; i++)
		{
			if (this->mVals[i] == val)
			{
				found = true;
				RemoveAt(i);
				i--;
			}
		}

		return found;
	}

	typename ArrayBase<T, TAlloc>::iterator erase(typename ArrayBase<T, TAlloc>::iterator itr)
	{
		RemoveAt(itr.mPtr - this->mVals);
		return itr;
	}

	void push_back(const T& val)
	{
		if (this->mSize >= this->mAllocSize)
			SetBufferSize(this->mAllocSize + this->mAllocSize / 2 + 1);
		this->mVals[this->mSize++] = val;
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);		
		--this->mSize;
	}

	void Add(const T& val)
	{
		if (this->mSize >= this->mAllocSize)
			SetBufferSize(this->mAllocSize + this->mAllocSize / 2 + 1);
		this->mVals[this->mSize++] = val;
	}		
};

template <typename T, typename TAlloc = AllocatorCLib<T> >
class Array : public ArrayImpl<T, TAlloc, std::is_pod<T>::value>
{
public:
	typedef ArrayImpl<T, TAlloc, std::is_pod<T>::value> _ArrayImpl;

	using _ArrayImpl::ArrayImpl;
	using _ArrayImpl::operator=;
	using _ArrayImpl::operator==;
	using _ArrayImpl::operator!=;
	
	Array() : _ArrayImpl()
	{

	}

	Array(const Array& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		*this = val;
	}

	Array(Array&& val) : _ArrayImpl(std::move(val))
	{

	}

	_ArrayImpl& operator=(const Array& val)
	{
		return _ArrayImpl::operator=(val);
	}

	_ArrayImpl& operator=(Array&& val)
	{
		return _ArrayImpl::operator=(val);
	}
};

NS_BF_END;

namespace std
{
	template<typename T>
	struct hash<Beefy::Array<T> >
	{
		size_t operator()(const Beefy::Array<T>& val) const
		{
			return HashBytes((const uint8*)val.mVals, sizeof(T) * val.mSize);
		}
	};
}
