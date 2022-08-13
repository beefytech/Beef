#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/BumpAllocator.h"

NS_BF_BEGIN

extern int sRadixMapCount;
extern int sRootSize;
extern int sMidSize;
extern int sLeafSize;

template <typename T>
class RadixMap32
{
public:
	// Put 32 entries in the root and (2^BITS)/32 entries in each leaf.
	static const int BLOCK_SHIFT = 8;
	static const int BITS = 32 - BLOCK_SHIFT;

	static const int ROOT_BITS = 12;
	static const int ROOT_LENGTH = 1 << ROOT_BITS;

	static const int LEAF_BITS = BITS - ROOT_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	struct Iterator
	{
	public:
		RadixMap32* mRadixMap;
		int mRootIdx;
		int mLeafIdx;

	public:
		Iterator()
		{
		}

		Iterator& operator++()
		{
			mLeafIdx++;
			while (true)
			{
				if (mLeafIdx == LEAF_LENGTH)
				{
					mLeafIdx = 0;

					mRootIdx++;
					while (true)
					{
						if (mRootIdx == ROOT_LENGTH)
							return *this;

						if (mRadixMap->mRoot[mRootIdx] != NULL)
							break;

						mRootIdx++;
					}
				}

				if (mRadixMap->mRoot[mRootIdx]->mValues[mLeafIdx] != NULL)
					return *this;

				mLeafIdx++;
			}

			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return (itr.mRootIdx != mRootIdx) || (itr.mLeafIdx != mLeafIdx);
		}

		bool operator==(const Iterator& itr) const
		{
			return (itr.mRootIdx == mRootIdx) && (itr.mLeafIdx == mLeafIdx);
		}

		T operator*()
		{
			return mRadixMap->mRoot[mRootIdx]->mValues[mLeafIdx];
		}
	};

	struct Leaf
	{
		T mValues[LEAF_LENGTH];

		T GetFirst(int startLeaf = 0)
		{
			for (int i = startLeaf; i < LEAF_LENGTH; i++)
			{
				auto value = mValues[i];
				if (value != NULL)
				{
					auto lowestValue = value;
					while (value != NULL)
					{
						if (value->mAddress < lowestValue->mAddress)
							lowestValue = value;
						value = value->mNext;
					}
					return lowestValue;
				}
			}
			return NULL;
		}
	};

	Leaf* mRoot[ROOT_LENGTH]; // Pointers to 32 child nodes
	typedef uint32 Number;

public:
	RadixMap32()
	{
		memset(mRoot, 0, sizeof(mRoot));
	}

	~RadixMap32()
	{
		int leafCount = 0;
		for (int i = 0; i < ROOT_LENGTH; i++)
		{
			if (mRoot[i] != NULL)
			{
				leafCount++;
				delete mRoot[i];
			}
		}
	}

	Iterator begin()
	{
		Iterator itr;
		itr.mRadixMap = this;
		itr.mRootIdx = -1;
		itr.mLeafIdx = LEAF_LENGTH - 1;
		return ++itr;
	}

	Iterator end()
	{
		Iterator itr;
		itr.mRadixMap = this;
		itr.mRootIdx = ROOT_LENGTH;
		itr.mLeafIdx = 0;
		return itr;
	}

	void Clear()
	{
		for (int i = 0; i < ROOT_LENGTH; i++)
		{
			if (mRoot[i] != NULL)
			{
				delete mRoot[i];
				mRoot[i] = NULL;
			}
		}
	}

	bool IsEmpty()
	{
		for (int i = 0; i < ROOT_LENGTH; i++)
			if (mRoot[i] != NULL)
				return false;
		return true;
	}

	T FindFirstLeafAt(Number addr)
	{
		int blockAddr = (int)(addr >> BLOCK_SHIFT);
		int i1 = (int)(blockAddr >> LEAF_BITS);
		int i2 = (int)(blockAddr & (LEAF_LENGTH - 1));

		Leaf* leaf = mRoot[i1];
		if (leaf == NULL)
			return NULL;
		return leaf->mValues[i2];
	}

	T Get(Number addr, int maxOffset = 0) const
	{
		int blockOffsetsLeft = (maxOffset + (1 << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT;
		int blockAddr = (int)(addr >> BLOCK_SHIFT);
		int i1 = (int)(blockAddr >> LEAF_BITS);
		int i2 = (int)(blockAddr & (LEAF_LENGTH - 1));

		// Find closest entry to requested addr
		Leaf* leaf = mRoot[i1];
		int bestDist = 0x7FFFFFFF;
		T bestValue = NULL;
		if (leaf != NULL)
		{
			T curValue = leaf->mValues[i2];
			while (curValue != NULL)
			{
				if (addr >= curValue->mAddress)
				{
					int dist = (int)(addr - curValue->mAddress);
					if (dist < bestDist)
					{
						bestDist = dist;
						bestValue = curValue;
					}
				}
				curValue = curValue->mNext;
			}
			if ((bestValue != NULL) || (maxOffset == 0))
				return bestValue;
		}

		// Start searching for the highest address below the current i1:i2
		while (blockOffsetsLeft > 0)
		{
			blockOffsetsLeft--;
			i2--;
			if (i2 < 0)
			{
				i1--;
				if (i1 < 0)
					break;
				i2 = LEAF_LENGTH - 1;
			}
			leaf = mRoot[i1];
			if (leaf != NULL)
			{
				auto value = leaf->mValues[i2];
				if (value != NULL)
				{
					auto bestValue = value;
					while (value != NULL)
					{
						if (value->mAddress > bestValue->mAddress)
							bestValue = value;
						value = value->mNext;
					}
					return bestValue;
				}
			}
		}

		return NULL;
	}

	T GetNext(T value)
	{
		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		const Number i1 = blockAddr >> LEAF_BITS;
		const Number i2 = blockAddr & (LEAF_LENGTH - 1);

		// Find closest entry to requested addr
		Leaf* leaf = mRoot[i1];
		if (leaf == NULL)
			return NULL;
		T curValue = leaf->mValues[i2];
		int bestDist = 0x7FFFFFFF;
		T bestValue = NULL;
		while (curValue != NULL)
		{
			if (curValue->mAddress > value->mAddress)
			{
				int dist = curValue->mAddress - value->mAddress;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestValue = curValue;
				}
			}
			curValue = curValue->mNext;
		}
		if (bestValue != NULL)
			return bestValue;

		// Get lowest value in next leaf
		curValue = leaf->GetFirst(i2 + 1);
		while (curValue != NULL)
		{
			BF_ASSERT(curValue != value);
			if (curValue->mAddress > value->mAddress)
			{
				int dist = curValue->mAddress - value->mAddress;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestValue = curValue;
				}
			}
			curValue = curValue->mNext;
		}
		if (bestValue != NULL)
			return bestValue;

		// Get first value in next root nodes
		for (int rootIdx = i1 + 1; rootIdx < ROOT_LENGTH; rootIdx++)
		{
			if (mRoot[rootIdx] != NULL)
			{
				curValue = mRoot[rootIdx]->GetFirst();
				if ((curValue != NULL) && (curValue->mAddress > value->mAddress))
					return curValue;
			}
		}

		return NULL;
	}

	T GetUnordered(Number addr, int maxReverseOffset) const
	{
		int blockOffsetsLeft = (maxReverseOffset + (1 << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT;
		int blockAddr = (int)(addr >> BLOCK_SHIFT);
		int i1 = (int)(blockAddr >> LEAF_BITS);
		int i2 = (int)(blockAddr & (LEAF_LENGTH - 1));

		while (blockOffsetsLeft > 0)
		{
			Leaf* leaf = mRoot[i1];
			if (leaf != NULL)
			{
				auto value = leaf->mValues[i2];
				if (value != NULL)
					return value;
				i2++;
				if (i2 == LEAF_LENGTH)
				{
					i2 = 0;
					i1++;
				}
				blockOffsetsLeft--;
			}
			else
			{
				i1++;
				blockOffsetsLeft -= LEAF_LENGTH;
			}
		}

		return NULL;
	}

	void RemoveRange(Number startAddr, Number length)
	{
		Number endAddr = startAddr + length;

		int blockAddrStart = (int)(startAddr >> BLOCK_SHIFT);
		int blockAddrEnd = (int)(endAddr >> BLOCK_SHIFT);

 		int i1Start = (int)(blockAddrStart >> LEAF_BITS);
 		int i1End = (int)((blockAddrEnd - 1) >> LEAF_BITS) + 1;

		for (int i1 = i1Start; i1 < i1End; i1++)
		{
			Leaf* leaf = mRoot[i1];
			if (leaf == NULL)
				continue;

			int i2Start;
			int i2End;
			if (i1 == i1Start)
			{
				i2Start = (int)(blockAddrStart & (LEAF_LENGTH - 1));
				i2End = LEAF_LENGTH;
			}
			else if (i1 == i1End - 1)
			{
				i2Start = 0;
				i2End = (int)((blockAddrEnd - 1) & (LEAF_LENGTH - 1)) + 1;
			}
			else
			{
				i2Start = 0;
				i2End = LEAF_LENGTH;
			}

			for (int i2 = i2Start; i2 < i2End; i2++)
			{
				T curValue = leaf->mValues[i2];
				if (curValue == NULL)
					continue;

				T* nextPtr = &leaf->mValues[i2];
				while (curValue != NULL)
				{
					if ((curValue->mAddress >= startAddr) && (curValue->mAddress < endAddr))
					{
						// We're removing it implicitly
					}
					else
					{
						*nextPtr = curValue;
						nextPtr = &curValue->mNext;
					}
					curValue = curValue->mNext;
				}
				*nextPtr = NULL;
			}
		}
	}

	void Insert(T value)
	{
		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		const Number i1 = blockAddr >> LEAF_BITS;
		const Number i2 = blockAddr & (LEAF_LENGTH - 1);

		Leaf* leaf = mRoot[i1];
		if (leaf == NULL)
		{
			leaf = new Leaf();
			sLeafSize += sizeof(Leaf);
			mRoot[i1] = leaf;
		}

		T prevValue = leaf->mValues[i2];
		mRoot[i1]->mValues[i2] = value;
		value->mNext = prevValue;
	}

	void Remove(T value)
	{
		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		const Number i1 = blockAddr >> LEAF_BITS;
		const Number i2 = blockAddr & (LEAF_LENGTH - 1);

		// Find closest entry to requested addr
		Leaf* leaf = mRoot[i1];
		T prevValue = NULL;
		T curValue = leaf->mValues[i2];
		while (curValue != NULL)
		{
			if (curValue == value)
			{
				if (prevValue == NULL)
					leaf->mValues[i2] = curValue->mNext;
				else
					prevValue->mNext = curValue->mNext;
				curValue->mNext = NULL;
				return;
			}

			prevValue = curValue;
			curValue = curValue->mNext;
		}
	}
};

template <typename T>
class RadixMap64
{
public:
	// Higher BLOCK_SHIFT causes more searches but reduces memory usage

	static const int BLOCK_SHIFT = 8;
	static const int BITS = 48 - BLOCK_SHIFT;

	static const int ROOT_BITS = 14;
	static const int ROOT_LENGTH = 1 << ROOT_BITS;

	static const int MID_BITS = 13;
	static const int MID_LENGTH = 1 << MID_BITS;

	static const int LEAF_BITS = BITS - ROOT_BITS - MID_BITS;
	static const int LEAF_LENGTH = 1 << LEAF_BITS;

	struct Iterator
	{
	public:
		RadixMap64* mRadixMap;
		int mRootIdx;
		int mMidIdx;
		int mLeafIdx;

	public:
		Iterator()
		{
		}

		Iterator& operator++()
		{
			mLeafIdx++;
			while (true)
			{
				if (mLeafIdx == LEAF_LENGTH)
				{
					mLeafIdx = 0;

					mMidIdx++;
					while (true)
					{
						if (mMidIdx == MID_LENGTH)
						{
							mMidIdx = 0;

							mRootIdx++;
							while (true)
							{
								if (mRootIdx == ROOT_LENGTH)
									return *this;

								if (mRadixMap->mRoot[mRootIdx] != NULL)
									break;

								mRootIdx++;
							}
						}

						if (mRadixMap->mRoot[mRootIdx]->mLeafs[mMidIdx] != NULL)
							break;

						mMidIdx++;
					}
				}

				if (mRadixMap->mRoot[mRootIdx]->mLeafs[mMidIdx]->mValues[mLeafIdx] != NULL)
					return *this;

				mLeafIdx++;
			}

			return *this;
		}

		bool operator!=(const Iterator& itr) const
		{
			return (itr.mRootIdx != mRootIdx) || (itr.mMidIdx != mMidIdx) || (itr.mLeafIdx != mLeafIdx);
		}

		bool operator==(const Iterator& itr) const
		{
			return (itr.mRootIdx == mRootIdx) && (itr.mMidIdx == mMidIdx) && (itr.mLeafIdx == mLeafIdx);
		}

		T operator*()
		{
			return mRadixMap->mRoot[mRootIdx]->mLeafs[mMidIdx]->mValues[mLeafIdx];
		}
	};

	struct Leaf
	{
		T mValues[LEAF_LENGTH];

		T GetFirst(int startLeaf = 0)
		{
			for (int i = startLeaf; i < LEAF_LENGTH; i++)
			{
				auto value = mValues[i];
				if (value != NULL)
				{
					auto lowestValue = value;
					while (value != NULL)
					{
						if (value->mAddress < lowestValue->mAddress)
							lowestValue = value;
						value = value->mNext;
					}
					return lowestValue;
				}
			}
			return NULL;
		}
	};

	struct Mid
	{
		Leaf* mLeafs[MID_LENGTH];

		~Mid()
		{
			for (int i = 0; i < MID_LENGTH; i++)
				if (mLeafs[i] != NULL)
					delete mLeafs[i];
		}
	};

	Mid* mRoot[ROOT_LENGTH]; // Pointers to 32 child nodes
	typedef uint64 Number;
	int mAllocSize;

public:
	RadixMap64()
	{
		memset(mRoot, 0, sizeof(mRoot));
		sRadixMapCount++;
		sRootSize += sizeof(RadixMap64);
		mAllocSize = sizeof(RadixMap64);
	}

	~RadixMap64()
	{
		int leafCount = 0;
		for (int i = 0; i < ROOT_LENGTH; i++)
		{
			if (mRoot[i] != NULL)
			{
				leafCount++;
				delete mRoot[i];
			}
		}
	}

	Iterator begin()
	{
		Iterator itr;
		itr.mRadixMap = this;
		itr.mRootIdx = -1;
		itr.mMidIdx = MID_LENGTH - 1;
		itr.mLeafIdx = LEAF_LENGTH - 1;
		return ++itr;
	}

	Iterator end()
	{
		Iterator itr;
		itr.mRadixMap = this;
		itr.mRootIdx = ROOT_LENGTH;
		itr.mMidIdx = 0;
		itr.mLeafIdx = 0;
		return itr;
	}

	void Clear()
	{
		for (int i = 0; i < ROOT_LENGTH; i++)
		{
			if (mRoot[i] != NULL)
			{
				delete mRoot[i];
				mRoot[i] = NULL;
			}
		}
	}

	bool IsEmpty()
	{
		for (int i = 0; i < ROOT_LENGTH; i++)
			if (mRoot[i] != NULL)
				return false;
		return true;
	}

	T FindFirstLeafAt(Number addr)
	{
		if ((addr & 0xFFFF000000000000LL) != 0)
		{
			return T();
		}

		int64 blockAddr = (int64)(addr >> BLOCK_SHIFT);
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		Mid* mid = mRoot[i1];
		if (mid == NULL)
			return NULL;
		Leaf* leaf = mid->mLeafs[i2];
		if (leaf == NULL)
			return NULL;
		return leaf->mValues[i3];
	}

	T Get(Number addr, int maxOffset = 0) const
	{
		if ((addr & 0xFFFF000000000000LL) != 0)
		{
			// On overflow return default
			return T();
		}

		int blockOffsetsLeft = (maxOffset + (1 << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT;

		int64 blockAddr = (int64)(addr >> BLOCK_SHIFT);
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		// Find closest entry to requested addr
		Mid* mid = mRoot[i1];
		Leaf* leaf = NULL;
		if (mid != NULL)
			leaf = mid->mLeafs[i2];
		int bestDist = 0x7FFFFFFF;
		T bestValue = NULL;
		if (leaf != NULL)
		{
			T curValue = leaf->mValues[i3];
			while (curValue != NULL)
			{
				if (addr >= curValue->mAddress)
				{
					int dist = (int)(addr - curValue->mAddress);
					if (dist < bestDist)
					{
						bestDist = dist;
						bestValue = curValue;
					}
				}
				curValue = curValue->mNext;
			}
			if ((bestValue != NULL) || (maxOffset == 0))
				return bestValue;
		}

		// Start searching for the highest address below the current i1:i2
		while (blockOffsetsLeft > 0)
		{
			blockOffsetsLeft--;
			i3--;
			if (i3 < 0)
			{
				i2--;
				if (i2 < 0)
				{
					i1--;
					if (i1 < 0)
						break;
					i2 = MID_LENGTH - 1;
				}
				i3 = LEAF_LENGTH - 1;
			}

			mid = mRoot[i1];
			if (mid != NULL)
			{
				leaf = mid->mLeafs[i2];
				if (leaf != NULL)
				{
					auto value = leaf->mValues[i3];
					if (value != NULL)
					{
						auto bestValue = value;
						while (value != NULL)
						{
							if (value->mAddress > bestValue->mAddress)
								bestValue = value;
							value = value->mNext;
						}
						return bestValue;
					}
				}
			}
		}

		return NULL;
	}

	T GetNext(T value)
	{
		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		// Find closest entry to requested addr
		Mid* mid = mRoot[i1];
		if (mid == NULL)
			return NULL;
		Leaf* leaf = mid->mLeafs[i2];
		if (leaf == NULL)
			return NULL;
		T curValue = leaf->mValues[i3];
		int bestDist = 0x7FFFFFFF;
		T bestValue = NULL;
		while (curValue != NULL)
		{
			if (curValue->mAddress > value->mAddress)
			{
				int dist = curValue->mAddress - value->mAddress;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestValue = curValue;
				}
			}
			curValue = curValue->mNext;
		}
		if (bestValue != NULL)
			return bestValue;

		// Get lowest value in next leaf
		curValue = leaf->GetFirst(i2 + 1);
		while (curValue != NULL)
		{
			BF_ASSERT(curValue != value);
			if (curValue->mAddress > value->mAddress)
			{
				int dist = curValue->mAddress - value->mAddress;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestValue = curValue;
				}
			}
			curValue = curValue->mNext;
		}
		if (bestValue != NULL)
			return bestValue;

		for (int midIdx = i2 + 1; midIdx < MID_LENGTH; midIdx++)
		{
			if (mid->mLeafs[midIdx] != NULL)
			{
				curValue = mid->mLeafs[midIdx]->GetFirst();
				if ((curValue != NULL) && (curValue->mAddress > value->mAddress))
					return curValue;
			}
		}

		// Get first value in next root nodes
		for (int rootIdx = i1 + 1; rootIdx < ROOT_LENGTH; rootIdx++)
		{
			if (mRoot[rootIdx] != NULL)
			{
				mid = mRoot[rootIdx];
				for (int midIdx = 0; midIdx < MID_LENGTH; midIdx++)
				{
					curValue = mid->mLeafs[midIdx]->GetFirst();
					if ((curValue != NULL) && (curValue->mAddress > value->mAddress))
						return curValue;
				}
			}
		}

		return NULL;
	}

	void RemoveRange(Number startAddr, Number length)
	{
		Number endAddr = BF_MIN(startAddr + length, 0x0001000000000000LL);
		startAddr = BF_MIN(startAddr, 0x0000FFFFFFFFFFFFLL);

		Number blockAddrStart = startAddr >> BLOCK_SHIFT;
		Number blockAddrEnd = endAddr >> BLOCK_SHIFT;

		int i1Start = (int)(blockAddrStart >> (LEAF_BITS + MID_BITS));
		int i1End   = (int)((blockAddrEnd - 1) >> (LEAF_BITS + MID_BITS)) + 1;

		for (int i1 = i1Start; i1 < i1End; i1++)
		{
			Mid* mid = mRoot[i1];
			if (mid == NULL)
				continue;

			int i2Start;
			int i2End;
			if (i1 == i1Start)
			{
				i2Start = (int)((blockAddrStart >> LEAF_BITS) & (MID_LENGTH - 1));
				i2End = MID_LENGTH;
			}
			else if (i1 == i1End)
			{
				i2Start = 0;
				i2End = (int)(((blockAddrEnd - 1) >> LEAF_BITS) & (MID_LENGTH - 1)) + 1;
			}
			else
			{
				i2Start = 0;
				i2End = MID_LENGTH;
			}

			for (int i2 = i2Start; i2 < i2End; i2++)
			{
				Leaf* leaf = mid->mLeafs[i2];
				if (leaf == NULL)
					continue;

				int i3Start;
				int i3End;
				if ((i1 == i1Start) && (i2 == i2Start))
				{
					i3Start = (int)(blockAddrStart & (LEAF_LENGTH - 1));
					i3End = LEAF_LENGTH;
				}
				else if ((i1 == i1End - 1) && (i2 == i2End - 1))
				{
					i3Start = 0;
					i3End = (int)((blockAddrEnd - 1) & (LEAF_LENGTH - 1)) + 1;
				}
				else
				{
					i3Start = 0;
					i3End = LEAF_LENGTH;
				}

				for (int i3 = i3Start; i3 < i3End; i3++)
				{
					T curValue = leaf->mValues[i3];
					if (curValue == NULL)
						continue;

					T* nextPtr = &leaf->mValues[i3];
					while (curValue != NULL)
					{
						if ((curValue->mAddress >= startAddr) && (curValue->mAddress < endAddr))
						{
							// We're removing it implicitly
						}
						else
						{
							*nextPtr = curValue;
							nextPtr = &curValue->mNext;
						}
						curValue = curValue->mNext;
					}
					*nextPtr = NULL;
				}
			}
		}
	}

	T GetUnordered(Number addr, int maxOffset) const
	{
		if ((addr & 0xFFFF000000000000LL) != 0)
		{
			return T();
		}

		int blockOffsetsLeft = (maxOffset + (1 << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT;

		const Number blockAddr = addr >> BLOCK_SHIFT;
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		while (blockOffsetsLeft > 0)
		{
			Mid* mid = mRoot[i1];
			if (mid != NULL)
			{
				Leaf* leaf = mid->mLeafs[i2];
				if (leaf != NULL)
				{
					auto value = leaf->mValues[i3];
					if (value != NULL)
						return value;
					i3++;
					if (i3 == LEAF_LENGTH)
					{
						i3 = 0;
						i2++;
						if (i2 == MID_LENGTH)
						{
							i2 = 0;
							i1++;
						}
					}
					blockOffsetsLeft--;
				}
				else
				{
					i2++;
					blockOffsetsLeft -= LEAF_LENGTH;
				}
			}
			else
			{
				// MID_LENGTH * LEAF_LENGTH is > 4GB, so a mRoot[i1] being NULL indicates no data
				return NULL;
			}
		}

		return NULL;
	}

	T GetNextUnordered(T value, int maxOffset) const
	{
		if (value->mNext != NULL)
			return value->mNext;
		return GetUnordered(value->mAddress + 1, maxOffset - 1);
	}

	void Insert(T value)
	{
		BF_ASSERT((value->mAddress & 0xFFFF000000000000LL) == 0);

		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		BF_ASSERT((i1 >= 0) && (i1 < ROOT_LENGTH));
		Mid* mid = mRoot[i1];
		if (mid == NULL)
		{
			mid = new Mid();
			mAllocSize += sizeof(Mid);
			sMidSize += sizeof(Mid);
			mRoot[i1] = mid;
		}

		BF_ASSERT((i2 >= 0) && (i2 < MID_LENGTH));
		Leaf* leaf = mid->mLeafs[i2];
		if (leaf == NULL)
		{
			leaf = new Leaf();
			sLeafSize += sizeof(Leaf);
			mAllocSize += sizeof(Leaf);
			mid->mLeafs[i2] = leaf;
		}

		T prevValue = leaf->mValues[i3];
		leaf->mValues[i3] = value;
		value->mNext = prevValue;
	}

	void Remove(T value)
	{
		const Number blockAddr = value->mAddress >> BLOCK_SHIFT;
		int i1 = (int)(blockAddr >> (LEAF_BITS + MID_BITS));
		int i2 = (int)((blockAddr >> LEAF_BITS) & (MID_LENGTH - 1));
		int i3 = (int)(blockAddr & (LEAF_LENGTH - 1));

		// Find closest entry to requested addr
		Mid* mid = mRoot[i1];
		Leaf* leaf = mid->mLeafs[i2];
		T prevValue = NULL;
		T curValue = leaf->mValues[i3];
		while (curValue != NULL)
		{
			if (curValue == value)
			{
				if (prevValue == NULL)
					leaf->mValues[i3] = curValue->mNext;
				else
					prevValue->mNext = curValue->mNext;
				curValue->mNext = NULL;
				return;
			}

			prevValue = curValue;
			curValue = curValue->mNext;
		}
	}
};

NS_BF_END