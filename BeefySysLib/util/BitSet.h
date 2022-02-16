#pragma once

#include "BFPlatform.h"

NS_BF_BEGIN;

#define BF_BITSET_ELEM_BITCOUNT (sizeof(uintptr)*8)

class BitSet
{
public:
	uintptr* mBits;
	int mNumBits;

public:
	BitSet()
	{
		mNumBits = 0;
		mBits = NULL;
	}

	BitSet(int numBits)
	{
		mNumBits = 0;
		mBits = NULL;
		this->Resize(numBits);
	}

	BitSet(BitSet&& other)
	{
		mNumBits = other.mNumBits;
		mBits = other.mBits;
		other.mNumBits = 0;
		other.mBits = NULL;
	}

	BitSet(const BitSet& other)
	{
		mNumBits = 0;
		mBits = NULL;
		*this = other;
	}

	~BitSet()
	{
		delete this->mBits;
	}

	void Resize(int numBits)
	{
		int numInts = (numBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		mNumBits = numBits;
		if (numInts == curNumInts)		
			return;		
		this->mNumBits = numBits;
		delete this->mBits;
		this->mBits = new uintptr[numInts];
		memset(this->mBits, 0, numInts * sizeof(uintptr));
	}

	void Clear()
	{
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		memset(mBits, 0, curNumInts * sizeof(uintptr));
	}

	bool IsSet(int idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		return (this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] & ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT))) != 0;
	}
	
	void Set(int idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] |= ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT));
	}

	void Clear(int idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] &= ~((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT));
	}

	bool IsEmpty()
	{
		return mNumBits == 0;
	}

	bool operator==(const BitSet& other) const
	{
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		if (mNumBits != other.mNumBits)
			return false;
		for (int i = 0; i < curNumInts; i++)
			if (mBits[i] != other.mBits[i])
				return false;
		return true;
	}

	bool operator!=(const BitSet& other) const
	{
		return !(*this == other);
	}

	BitSet& operator=(const BitSet& other)
	{
		Resize(other.mNumBits);
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		memcpy(mBits, other.mBits, curNumInts * sizeof(uintptr));
		return *this;
	}
};

NS_BF_END;