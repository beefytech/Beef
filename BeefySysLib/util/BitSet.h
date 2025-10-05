#pragma once

#include "BFPlatform.h"

NS_BF_BEGIN;

#define BF_BITSET_ELEM_BYTECOUNT sizeof(uint32)
#define BF_BITSET_ELEM_BITCOUNT (BF_BITSET_ELEM_BYTECOUNT*8)

class BitSet
{
public:
	uint32* mBits;
	uint32 mInner;
	int mNumBits;

public:
	BitSet()
	{
		mNumBits = 0;
		mInner = 0;
		mBits = NULL;
	}

	BitSet(int numBits)
	{
		mNumBits = 0;
		mInner = 0;
		mBits = NULL;
		this->Resize(numBits);
	}

	BitSet(BitSet&& other)
	{
		mNumBits = other.mNumBits;
		mInner = other.mInner;
		mBits = other.mBits;		
		other.mNumBits = 0;
		other.mBits = NULL;
	}

	BitSet(const BitSet& other)
	{
		mNumBits = 0;
		mInner = 0;
		mBits = NULL;
		*this = other;
	}

	~BitSet()
	{
		if (this->mBits != &mInner)
			delete [] this->mBits;
	}

	void Resize(int numBits)
	{
		int numInts = (numBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		mNumBits = numBits;
		if (numInts == curNumInts)		
			return;		
		this->mNumBits = numBits;
		if (this->mBits != &mInner)
			delete this->mBits;
		if (numInts > 1)
			this->mBits = new uint32[numInts];
		else
			this->mBits = &mInner;
		memset(this->mBits, 0, numInts * BF_BITSET_ELEM_BYTECOUNT);
	}

	void Clear()
	{
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		memset(mBits, 0, curNumInts * BF_BITSET_ELEM_BYTECOUNT);
	}

	bool IsSet(int idx) const
	{
		if (mNumBits == 0)
			return false;
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		return (this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] & ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT))) != 0;
	}

	bool IsSet(int start, int length) const
	{
		if (mNumBits == 0)
			return false;

		BF_ASSERT((uintptr)start + length <= (uintptr)mNumBits);

		if (length == 1)
		{
			return (this->mBits[start / BF_BITSET_ELEM_BITCOUNT] & ((uintptr)1 << (start % BF_BITSET_ELEM_BITCOUNT))) != 0;
		}

		if ((start % 32 == 0) && (length <= 32))
		{
			if (length == 32)
				return mBits[start / 32] == 0xFFFFFFFF;
			uint32 mask = (1 << length) - 1;
			return (mBits[start / 32] & mask) == mask;			
		}

		if ((start % 8 == 0) && (length >= 8))
		{
			int byteCount = length / 8;
			for (int i = 0; i < byteCount; i++)
			{
				if (*((uint8*)mBits + start + i) != 0xFF)
					return false;
			}

			if (length == byteCount * 8)
				return true;
			return IsSet(start + byteCount * 8, length - byteCount * 8);			
		}

		int idx = start;
		int lengthLeft = length;
		while (lengthLeft > 0)
		{
			if ((idx % 32 == 0) && (lengthLeft >= 32))
			{
				if (*((uint32*)this->mBits + idx / 32) != 0xFFFFFFFF)
					return false;
				idx += 32;
				lengthLeft -= 32;
			}
			else if ((idx % 8 == 0) && (lengthLeft >= 8))
			{
				if (*((uint8*)this->mBits + idx / 8) != 0xFF)
					return false;
				idx += 8;
				lengthLeft -= 8;
			}
			else
			{
				if ((this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] & ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT))) == 0)
					return false;
				idx++;
				lengthLeft--;
			}
		}

		return true;
	}
	
	void Set(int idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] |= ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT));
	}

	void Set(int start, int length)
	{
		if (length == 0)
			return;

		BF_ASSERT((uintptr)start + length <= (uintptr)mNumBits);		

		if (length == 1)
		{
			this->mBits[start / BF_BITSET_ELEM_BITCOUNT] |= ((uintptr)1 << (start % BF_BITSET_ELEM_BITCOUNT));
			return;
		}

		if ((start % 32 == 0) && (length <= 32))
		{
			if (length == 32)
				mBits[start / 32] = 0xFFFFFFFF;
			else
				mBits[start / 32] = (1 << length) - 1;
			return;
		}

 		if ((start % 8 == 0) && (length >= 8))
 		{
 			int byteCount = length / 8;
			memset((uint8*)mBits + start / 8, 0xFF, byteCount);
			Set(start + byteCount * 8, length - byteCount * 8);
			return;
 		}

		int idx = start;
		int lengthLeft = length;
		while (lengthLeft > 0)
		{
			if ((idx % 32 == 0) && (lengthLeft >= 32))
			{
				*((uint32*)this->mBits + idx / 32) = 0xFFFFFFFF;
				idx += 32;
				lengthLeft -= 32;
			}
			else if ((idx % 8 == 0) && (lengthLeft >= 8))
			{
				*((uint8*)this->mBits + idx / 8) = 0xFF;
				idx += 8;
				lengthLeft -= 8;
			}
			else
			{
				this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] |= ((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT));
				idx++;
				lengthLeft--;
			}
		}
	}

	bool Contains(int idx)
	{
		return ((uintptr)idx < (uintptr)mNumBits);
	}

	void Clear(int idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)mNumBits);
		this->mBits[idx / BF_BITSET_ELEM_BITCOUNT] &= ~((uintptr)1 << (idx % BF_BITSET_ELEM_BITCOUNT));
	}

	bool IsClear()
	{
		int curNumInts = (mNumBits + BF_BITSET_ELEM_BITCOUNT - 1) / BF_BITSET_ELEM_BITCOUNT;
		for (int i = 0; i < curNumInts; i++)
			if (this->mBits[i] != 0)
				return false;
		return true;
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
		memcpy(mBits, other.mBits, curNumInts * BF_BITSET_ELEM_BYTECOUNT);
		return *this;
	}
};

NS_BF_END;