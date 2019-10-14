#pragma once

#pragma once

#include "Array.h"

NS_BF_BEGIN;

template <typename T, typename TAlloc = AllocatorCLib<T> >
class SizedArrayBase : protected TAlloc
{
public:
	typedef T value_type;

	T* mVals;
	intptr mSize;
	intptr mAllocSize;

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

		T& operator*()
		{
			return *mPtr;
		}

		T* operator->()
		{
			return mPtr;
		}

		bool operator<(const iterator& val2)
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

		const T& operator*()
		{
			return *mPtr;
		}

		const T* operator->()
		{
			return mPtr;
		}

		bool operator<(const const_iterator& val2)
		{
			return mPtr < val2.mPtr;
		}
	};

private:


public:
	SizedArrayBase()
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;
	}

	SizedArrayBase(SizedArrayBase<T, TAlloc>&& val)
	{
		this->mVals = val.mVals;
		this->mSize = val.mSize;
		this->mAllocSize = val.mAllocSize;

		val.mVals = NULL;
		val.mSize = 0;
		val.mAllocSize = 0;
	}

	T& operator[](intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);
		return this->mVals[idx];
	}

	const T& operator[](intptr idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mSize);
		return this->mVals[idx];
	}

	bool operator==(const SizedArrayBase& arrB) const
	{
		if (this->mSize != arrB.mSize)
			return false;
		for (int i = 0; i < this->mSize; i++)
			if (this->mVals[i] != arrB.mVals[i])
				return false;
		return true;
	}

	bool operator!=(const SizedArrayBase& arrB) const
	{
		if (this->mSize != arrB.mSize)
			return true;
		for (int i = 0; i < this->mSize; i++)
			if (this->mVals[i] != arrB.mVals[i])
				return true;
		return false;
	}

	const_iterator begin() const
	{
		return this->mVals;
	}

	const_iterator end() const
	{
		return this->mVals + this->mSize;
	}

	iterator begin()
	{
		return this->mVals;
	}

	iterator end()
	{
		return this->mVals + this->mSize;
	}

	T& front() const
	{
		return this->mVals[0];
	}

	T& back() const
	{
		return this->mVals[this->mSize - 1];
	}

	intptr size() const
	{
		return this->mSize;
	}

	bool empty() const
	{
		return this->mSize == 0;
	}

	bool IsEmpty() const
	{
		return this->mSize == 0;
	}

	void clear()
	{
		this->mSize = 0;
	}

	/*void Free()
	{
	if (this->mVals != NULL)
	{
	deallocate(this->mVals);
	}
	this->mVals = NULL;
	this->mAllocSize = 0;
	this->mSize = 0;
	}*/

	T GetSafe(intptr idx)
	{
		if ((idx < 0) || (idx >= this->mSize))
			return T();
		return this->mVals[idx];
	}

	T GetLastSafe()
	{
		if (this->mSize == 0)
			return T();
		return this->mVals[this->mSize - 1];
	}

	T GetFirstSafe()
	{
		if (this->mSize == 0)
			return T();
		return this->mVals[0];
	}

	bool Contains(T val) const
	{
		for (int i = 0; i < this->mSize; i++)
			if (this->mVals[i] == val)
				return true;
		return false;
	}

	intptr IndexOf(T val) const
	{
		for (int i = 0; i < this->mSize; i++)
			if (this->mVals[i] == val)
				return i;
		return -1;
	}
};

// NON-POD
template <typename T, typename TAlloc, bool TIsPod>
class SizedArrayBaseT : public SizedArrayBase<T, TAlloc>
{
public:
	typedef typename std::aligned_storage<sizeof(T), alignof(T)>::type TStorage;
	TStorage mFirstVal;

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

	void Grow(intptr newSize)
	{
		T* newVals = TAlloc::allocate(newSize);
		if (this->mVals != NULL)
		{
			if (this->mSize > 0)
				MoveArray(newVals, this->mVals, this->mSize);
			if (this->mVals != (T*)&mFirstVal)
				 TAlloc::deallocate(this->mVals);
		}
		this->mVals = newVals;
		this->mAllocSize = newSize;
	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			Grow(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}

public:
	using SizedArrayBase<T, TAlloc>::SizedArrayBase;

	SizedArrayBaseT() : SizedArrayBase<T, TAlloc>()
	{

	}

	SizedArrayBaseT(const SizedArrayBaseT& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		*this = val;
	}

	SizedArrayBaseT(SizedArrayBaseT&& val) : SizedArrayBase<T, TAlloc>(val)
	{

	}

	~SizedArrayBaseT()
	{
		for (int i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		if (this->mVals != (T*)&mFirstVal)
		{
			 TAlloc::deallocate(this->mVals);
		}		
	}

	void resize(intptr size)
	{
		while (size < this->mSize)
			pop_back();
		if (size > this->mSize)
		{
			reserve(size);
			while (size > this->mSize)
				new (&this->mVals[this->mSize++]) T();
		}
	}

	void reserve(intptr size)
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

	SizedArrayBaseT& operator=(const SizedArrayBaseT& val)
	{
		if (&val == this)
			return *this;
		for (int i = 0; i < this->mSize; i++)
			this->mVals[i].~T();
		this->mSize = 0;
		if (val.mSize > this->mAllocSize)
			Grow(val.mSize);
		resize(val.mSize);
		for (int i = 0; i < val.mSize; i++)
			new (&this->mVals[i]) T(val.mVals[i]);
		this->mSize = val.mSize;
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
		this->mSize -= length;
	}

	void Insert(intptr idx, T val)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize >= this->mAllocSize)
		{
			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;

			T* newVals =  TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					MoveArray(newVals, this->mVals, idx);
				if (idx < this->mSize) // Copy right of idx
					MoveArray(newVals + idx + 1, this->mVals + idx, this->mSize - idx);
				if (this->mVals != (T*)&mFirstVal)
					 TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = newSize;
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

			T* newVals =  TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					MoveArray(newVals, this->mVals, idx);
				if (idx < this->mSize) // Copy right of idx
					MoveArray(newVals + idx + size, this->mVals + idx, this->mSize - idx);
				if (this->mVals != (T*)&mFirstVal)
					 TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			MoveArray(this->mVals + idx + size, this->mVals + idx, moveCount);
		}
		for (int i = 0; i < size; i++)
			new (&this->mVals[idx + i]) T(vals[i]);
		this->mSize += size;
	}

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

	typename SizedArrayBase<T, TAlloc>::iterator erase(typename SizedArrayBase<T, TAlloc>::iterator itr)
	{
		RemoveAt(itr.mPtr - this->mVals);
		return itr;
	}

	void push_back(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&this->mVals[this->mSize++]) T(val);
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);
		this->mVals[this->mSize - 1].~T();
		--this->mSize;
	}

	void Add(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		new (&this->mVals[this->mSize++]) T(val);
	}
};

// POD
template <typename T, typename TAlloc>
class SizedArrayBaseT<T, TAlloc, true> : public SizedArrayBase<T, TAlloc>
{
public:
	typedef T TStorage;
	T mFirstVal;

protected:
	void Grow(intptr newSize)
	{
		T* newVals =  TAlloc::allocate(newSize);
		if (this->mVals != NULL)
		{
			if (this->mSize > 0)
				memcpy(newVals, this->mVals, this->mSize * sizeof(T));
			if (this->mVals != &mFirstVal)
				 TAlloc::deallocate(this->mVals);
		}
		this->mVals = newVals;
		this->mAllocSize = newSize;
	}

	void EnsureFree(intptr freeCount)
	{
		if (this->mSize + freeCount > this->mAllocSize)
			Grow(std::max(this->mAllocSize + this->mAllocSize / 2 + 1, this->mSize + freeCount));
	}
	
public:
	using SizedArrayBase<T, TAlloc>::SizedArrayBase;

	SizedArrayBaseT() : SizedArrayBase<T, TAlloc>()
	{

	}

	SizedArrayBaseT(const SizedArrayBaseT& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		*this = val;
	}

	SizedArrayBaseT(SizedArrayBaseT&& val) : SizedArrayBase<T, TAlloc>(val)
	{

	}

	~SizedArrayBaseT()
	{
		if (this->mVals != &mFirstVal)
		{
			 TAlloc::deallocate(this->mVals);
		}
	}

	SizedArrayBaseT& operator=(const SizedArrayBaseT& val)
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

	void resize(intptr size)
	{
		while (size < this->mSize)
			pop_back();
		if (size > this->mSize)
		{
			reserve(size);
			while (size > this->mSize)
				new (&this->mVals[this->mSize++]) T();
		}
	}

	void reserve(intptr size)
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
		this->mSize -= length;
	}

	void Insert(intptr idx, T val)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize >= this->mAllocSize)
		{
			intptr newSize = this->mAllocSize + this->mAllocSize / 2 + 1;

			T* newVals =  TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					memmove(newVals, this->mVals, idx * sizeof(T));
				if (idx < this->mSize) // Copy right of idx
					memmove(newVals + idx + 1, this->mVals + idx, (this->mSize - idx) * sizeof(T));
				if (this->mVals != &mFirstVal)
					 TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			memmove(this->mVals + idx + 1, this->mVals + idx, moveCount * sizeof(T));
		}
		this->mVals[idx] = val;
		this->mSize++;
	}

	void Insert(intptr idx, const T* vals, intptr size)
	{
		BF_ASSERT((uintptr)idx <= (uintptr)this->mSize);
		if (this->mSize + size > this->mAllocSize)
		{
			intptr newSize = BF_MAX(this->mSize + size, this->mAllocSize + this->mAllocSize / 2 + 1);

			T* newVals =  TAlloc::allocate(newSize);
			if (this->mVals != NULL)
			{
				if (idx > 0) // Copy left of idx
					memmove(newVals, this->mVals, idx * sizeof(T));
				if (idx < this->mSize) // Copy right of idx
					memmove(newVals + idx + size, this->mVals + idx, (this->mSize - idx) * sizeof(T));
				if (this->mVals != &mFirstVal)
					 TAlloc::deallocate(this->mVals);
			}
			this->mVals = newVals;
			this->mAllocSize = newSize;
		}
		else if (idx != this->mSize)
		{
			intptr moveCount = this->mSize - idx;
			memmove(this->mVals + idx + size, this->mVals + idx, moveCount * sizeof(T));
		}
		for (int i = 0; i < size; i++)
			this->mVals[idx + i] = vals[i];
		this->mSize += size;
	}

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

	typename SizedArrayBase<T, TAlloc>::iterator erase(typename SizedArrayBase<T, TAlloc>::iterator itr)
	{
		RemoveAt(itr.mPtr - this->mVals);
		return itr;
	}

	void push_back(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		this->mVals[this->mSize++] = val;
	}

	void pop_back()
	{
		BF_ASSERT(this->mSize > 0);
		--this->mSize;
	}

	void Add(T val)
	{
		if (this->mSize >= this->mAllocSize)
			Grow(this->mAllocSize + this->mAllocSize / 2 + 1);
		this->mVals[this->mSize++] = val;
	}
};

template <typename T, typename TAlloc = AllocatorCLib<T> >
class SizedArrayImpl : public SizedArrayBaseT<T, TAlloc, std::is_pod<T>::value>
{
public:
	typedef SizedArrayBaseT<T, TAlloc, std::is_pod<T>::value> _Base;
};

template <typename T, int TInternalSize, typename TAlloc = AllocatorCLib<T> >
class SizedArray : public SizedArrayImpl<T, TAlloc>
{
public:
	typedef SizedArrayImpl<T, TAlloc> _Base;
	typedef typename _Base::_Base _BaseBase;

	typename _Base::TStorage mInternalBuffer[TInternalSize - 1];

public:
	using SizedArrayImpl<T, TAlloc>::SizedArrayImpl;
	using _Base::operator=;
	using _Base::operator==;
	using _Base::operator!=;

	SizedArray()
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = TInternalSize;
	}

	SizedArray(const Array<T>& arr)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = TInternalSize;
		for (auto& val : arr)
			this->Add(val);
	}

	SizedArray(const SizedArray& val)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = TInternalSize;

		_BaseBase::operator=(val);
	}

	SizedArray(const typename _Base::_Base& val)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = TInternalSize;

		_BaseBase::operator=(val);
	}

	SizedArray(SizedArray&& val)
	{
		if (val.mVals == (T*)&val.mFirstVal)
		{
			this->mVals = (T*)&this->mFirstVal;
			this->mSize = 0;
			this->mAllocSize = TInternalSize;
			_BaseBase::operator=(val);
		}
		else
		{
			this->mVals = val.mVals;
			this->mSize = val.mSize;
			this->mAllocSize = val.mAllocSize;
			val.mVals = (T*)&val.mFirstVal;
		}
	}

	SizedArray(std::initializer_list<T> il)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = TInternalSize;
		for (auto& val : il)
			this->push_back(val);
	}

	_BaseBase& operator=(const SizedArray& val)
	{
		if (&val == this)
			return *this;
		return _BaseBase::operator=(val);
	}

	_BaseBase& operator=(std::initializer_list<T> il)
	{
		this->mSize = 0;
		for (auto& val : il)
			this->push_back(val);
		return *this;
	}
};

template <typename T, typename TAlloc>
class SizedArray<T, 1, TAlloc> : public SizedArrayImpl<T, TAlloc>
{
public:
	typedef SizedArrayImpl<T, TAlloc> _Base;
	typedef typename _Base::_Base _BaseBase;

	using _Base::SizedArrayImpl;
	using _Base::operator=;
	using _Base::operator==;
	using _Base::operator!=;

	SizedArray()
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = 1;
	}

	SizedArray(const SizedArray& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		_BaseBase::operator=(val);
	}

	SizedArray(const _BaseBase& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		_BaseBase::operator=(val);
	}

	SizedArray(SizedArray&& val)
	{
		if (val.mVals == val.mInternalBuffer)
		{
			this->mVals = (T*)&this->mFirstVal;
			this->mSize = 0;
			this->mAllocSize = 1;
			_BaseBase::operator=(val);
		}
		else
		{
			this->mVals = val.mVals;
			this->mSize = val.mSize;
			this->mAllocSize = val.mAllocSize;

			val.mVals = NULL;
		}
	}

	SizedArray(std::initializer_list<T> il)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = 1;
		for (auto& val : il)
			this->push_back(val);
	}

	_BaseBase& operator=(const SizedArray& val)
	{
		if (&val == this)
			return *this;
		return _BaseBase::operator=(val);
	}
};

template <typename T, typename TAlloc>
class SizedArray<T, 0, TAlloc> : public SizedArrayImpl<T, TAlloc>
{
public:
	typedef SizedArrayImpl<T, TAlloc> _Base;
	typedef typename _Base::_Base _BaseBase;

	using _Base::SizedArrayImpl;
	using _Base::operator=;
	using _Base::operator==;
	using _Base::operator!=;

	SizedArray()
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = 1;
	}

	SizedArray(const SizedArray& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		_BaseBase::operator=(val);
	}

	SizedArray(const _BaseBase& val)
	{
		this->mVals = NULL;
		this->mSize = 0;
		this->mAllocSize = 0;

		_BaseBase::operator=(val);
	}

	SizedArray(SizedArray&& val)
	{
		if (val.mVals == val.mInternalBuffer)
		{
			this->mVals = (T*)&this->mFirstVal;
			this->mSize = 0;
			this->mAllocSize = 1;
			_BaseBase::operator=(val);
		}
		else
		{
			this->mVals = val.mVals;
			this->mSize = val.mSize;
			this->mAllocSize = val.mAllocSize;

			val.mVals = &val.mInternalBuffer;
		}
	}

	SizedArray(std::initializer_list<T> il)
	{
		this->mVals = (T*)&this->mFirstVal;
		this->mSize = 0;
		this->mAllocSize = 1;
		for (auto& val : il)
			this->push_back(val);
	}

	_BaseBase& operator=(const SizedArray& val)
	{
		return _BaseBase::operator=(val);
	}
};

NS_BF_END;

/*namespace std
{
	template<typename T>
	struct hash<Beefy::Array<T> >
	{
		size_t operator()(const Beefy::Array<T>& val) const
		{
			return _Hash_seq((const uint8*)val.mVals, sizeof(T) * val.mSize);
		}
	};
}*/

