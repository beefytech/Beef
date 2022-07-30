#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/util/HashSet.h"
#include "Debugger.h"

NS_BF_DBG_BEGIN

namespace TCFake
{
	struct Span;
	static const size_t kPageShift = 13;
	static const size_t kNumClasses = 88;

	struct PageHeap
	{
		struct PageMap
		{
#ifdef BF_DBG_32
			static const int BITS = 32 - kPageShift;

			static const int ROOT_BITS = 5;
			static const int ROOT_LENGTH = 1 << ROOT_BITS;

			static const int LEAF_BITS = BITS - ROOT_BITS;
			static const int LEAF_LENGTH = 1 << LEAF_BITS;

			struct Leaf
			{
				addr_target ptrs[LEAF_LENGTH];
			};

			struct Root
			{
				addr_target ptrs[ROOT_LENGTH];
			};
#else
			static const int BITS = 48 - kPageShift;

			static const int INTERIOR_BITS = (BITS + 2) / 3; // Round-up
			static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;

			static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
			static const int LEAF_LENGTH = 1 << LEAF_BITS;

			struct Node
			{
				addr_target ptrs[INTERIOR_LENGTH];
			};

			struct Leaf
			{
				addr_target ptrs[LEAF_LENGTH];
			};
#endif
		};
	};
}

class WinDebugger;
class DbgSubprogram;

enum BfRtFlags
{
	BfRtFlags_ObjectHasDebugFlags = 1,
	BfRtFlags_LeakCheck = 2,
	BfRtFlags_SilentCrash = 4,
	BfRtFlags_DebugAlloc = 8
};

class DbgGCData
{
public:
	BfRtFlags mDbgFlags;
	addr_target mObjRootPtr;
	addr_target mRawRootPtr;
	addr_target mRawObjectSentinel;

	int mSizeClasses[TCFake::kNumClasses];
};

class DbgHotScanner
{
public:
	WinDebugger* mDebugger;
	DbgGCData mDbgGCData;
	Beefy::Dictionary<addr_target, int> mFoundClassVDataAddrs;
	Beefy::Dictionary<addr_target, int> mFoundRawAllocDataAddrs;
	Beefy::Dictionary<addr_target, int> mFoundTypeAddrs;
	Beefy::HashSet<addr_target> mFoundFuncPtrs;

#ifdef BF_DBG_32
	TCFake::PageHeap::PageMap::Root mRoot;
	TCFake::PageHeap::PageMap::Leaf mNode;
#else
	TCFake::PageHeap::PageMap::Node mRoot;
	TCFake::PageHeap::PageMap::Node mNode1;
	TCFake::PageHeap::PageMap::Leaf mNode2;
#endif
	Beefy::Array<uint8> mScanData;

public:
	void AddSubProgram(DbgSubprogram* subProgram, bool followInlineParent, const Beefy::StringImpl& prefix);
	void PopulateHotCallstacks();
	void ScanSpan(TCFake::Span* span, int expectedStartPage, int memKind);
	void ScanRoot(addr_target rootPtr, int memKind);

public:
	DbgHotScanner(WinDebugger* debugger);

	void Scan(Beefy::DbgHotResolveFlags flags);
};

NS_BF_DBG_END