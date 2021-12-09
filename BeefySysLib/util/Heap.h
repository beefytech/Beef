#pragma once

#include "../Common.h"

NS_BF_BEGIN

class ContiguousHeap
{
public:
	typedef int AllocRef;
	
public:	
	void* mMetadata;

	int mMemorySize;
	int mBlockDataOfs;
	Array<AllocRef> mFreeList;
	int mFreeIdx;

public:
	ContiguousHeap();
	virtual ~ContiguousHeap();
	
	void Clear(int maxAllocSize = -1);

	AllocRef Alloc(int size);
	bool Free(AllocRef ref);	

	void Validate();
	void DebugDump();	
};

NS_BF_END