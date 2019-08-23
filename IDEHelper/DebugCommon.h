#pragma once

#pragma warning(disable:4996)
#pragma warning(disable:4291)

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/String.h"

#ifdef BF_DBG_32
	#define NS_BF_DBG BeefyDbg32
	#define NS_BF_DBG_BEGIN namespace NS_BF_DBG {
	#define NS_BF_DBG_END }
	#define USING_NS_BF_DBG using namespace Beefy; using namespace NS_BF_DBG
	typedef uint32 addr_target;
	typedef int32 intptr_target;
	#define DbgRadixMap RadixMap32
#elif defined BF_DBG_64
	#define NS_BF_DBG BeefyDbg64
	#define NS_BF_DBG_BEGIN namespace NS_BF_DBG {
	#define NS_BF_DBG_END }
	#define USING_NS_BF_DBG using namespace Beefy; using namespace NS_BF_DBG
	typedef uint64 addr_target;
	typedef int64 intptr_target;
	#define DbgRadixMap RadixMap64
#else
	// Not targeted
#endif

NS_BF_BEGIN

enum DbgFlavor : uint8
{
	DbgFlavor_Unknown,
	DbgFlavor_GNU,
	DbgFlavor_MS
};

enum DbgLanguage : int8
{
	DbgLanguage_NotSet = -1,
	DbgLanguage_Unknown = 0,
	DbgLanguage_C,
	DbgLanguage_Beef,
	DbgLanguage_BeefUnfixed, // Has *'s after class names

	DbgLanguage_COUNT
};

enum DbgAddrType : uint8
{
	DbgAddrType_None,
	DbgAddrType_Value,
	DbgAddrType_Local,
	DbgAddrType_LocalSplat,
	DbgAddrType_Target,
	DbgAddrType_TargetDeref,
	DbgAddrType_Register,
	DbgAddrType_OptimizedOut,
	DbgAddrType_NoValue
};

NS_BF_END