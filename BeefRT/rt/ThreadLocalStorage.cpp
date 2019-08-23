#include "ThreadLocalStorage.h"

BfpTLS* BfTLSManager::sInternalThreadKey = 0;

BfTLSManager gBfTLSManager;

uint32 BfTLSManager::Alloc()
{
	Beefy::AutoCrit autoCrit(mCritSect);
	int idx = 0;
	if (mFreeIndices.size() != 0)
	{
		idx = mFreeIndices.back();
		mFreeIndices.pop_back();
	}
	else
	{
		idx = mAllocIdx++;
		if (mAllocIdx >= mAllocSize)
		{
			int newSize = std::max(mAllocSize, 4);
            BfpTLS** newArr = new BfpTLS*[newSize];
			BfTLSDatumRoot** newDatumArr = new BfTLSDatumRoot*[newSize];

			if (mAllocSize > 0)
			{
                memcpy(newArr, mAllocatedKeys, mAllocSize*sizeof(BfpTLS*));
				memcpy(newDatumArr, mAssociatedTLSDatums, mAllocSize*sizeof(BfTLSDatumRoot*));
			}

			mAllocSize = newSize;
			BF_FULL_MEMORY_FENCE();
			mAllocatedKeys = newArr;
			mAssociatedTLSDatums = newDatumArr;
		}
	}

    mAllocatedKeys[idx] = BfpTLS_Create();
	mAssociatedTLSDatums[idx] = NULL;
	return idx;
}

void BfTLSManager::RegisterTlsDatum(uint32 key, BfTLSDatumRoot* tlsDatum)
{
	mAssociatedTLSDatums[key] = tlsDatum;
}

void BfTLSManager::Free(uint32 idx)
{
	BfTLSDatumRoot* tlsDatumRoot = (BfTLSDatumRoot*) mAssociatedTLSDatums[idx];
	if (tlsDatumRoot != NULL)
		tlsDatumRoot->Finalize();

	Beefy::AutoCrit autoCrit(mCritSect);
	mFreeIndices.push_back(idx);
    BfpTLS_Release(mAllocatedKeys[idx]);
}
