#pragma once

#include "BFPlatform.h"

NS_BF_BEGIN;

#define BF_BITSET_ELEM_SIZE (sizeof(uintptr)*8)

class BitSet
{
public:
	uintptr* mBits;
	int mNumInts;

public:
	BitSet()
	{
		mNumInts = 0;
		mBits = NULL;
	}

	BitSet(int numBits)
	{
		mNumInts = 0;
		mBits = NULL;
		this->Resize(numBits);
	}

	~BitSet()
	{
		delete this->mBits;
	}

	void Resize(int numBits)
	{
		int numInts = (numBits + BF_BITSET_ELEM_SIZE - 1) / BF_BITSET_ELEM_SIZE;
		if (numInts == mNumInts)
			return;
		this->mNumInts = numInts;
		delete this->mBits;
		this->mBits = new uintptr[numInts];
		memset(this->mBits, 0, numInts * sizeof(uintptr));
	}

	bool IsSet(int idx)
	{
		return (this->mBits[idx / BF_BITSET_ELEM_SIZE] & ((uintptr)1 << (idx % BF_BITSET_ELEM_SIZE))) != 0;
	}
	
	void Set(int idx)
	{
		this->mBits[idx / BF_BITSET_ELEM_SIZE] |= ((uintptr)1 << (idx % BF_BITSET_ELEM_SIZE));
	}

	void Clear(int idx)
	{
		this->mBits[idx / BF_BITSET_ELEM_SIZE] &= ~((uintptr)1 << (idx % BF_BITSET_ELEM_SIZE));
	}
};

NS_BF_END;