#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/util/Dictionary.h"
#include "BeefySysLib/util/Array.h"

NS_BF_DBG_BEGIN

class HotHeap
{
public:
	enum HotUseFlags
	{
		HotUseFlags_None,
		HotUseFlags_Allocated,
		HotUseFlags_Referenced
	};

public:
	static const int BLOCK_SIZE = 4096;
	
	addr_target mHotAreaStart;
	int mHotAreaSize;
	Beefy::Array<HotUseFlags> mHotAreaUsed;	

	int mCurBlockIdx;
	int mBlockAllocIdx; // Total blocks allocated, doesn't decrease with frees

	Beefy::Dictionary<addr_target, int> mTrackedMap;

public:
	HotHeap(addr_target hotStart, int size);
	HotHeap();
	
	void AddTrackedRegion(addr_target hotStart, int size);
		
	addr_target Alloc(int size);
	void Release(addr_target addr, int size);
	void MarkBlockReferenced(addr_target addr);
	bool IsReferenced(addr_target addr, int size);
	bool IsBlockUsed();
	void ClearReferencedFlags();
	intptr_target GetUsedSize();
};

NS_BF_DBG_END