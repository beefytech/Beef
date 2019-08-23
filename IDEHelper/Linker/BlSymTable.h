#pragma once

#include "../Beef/BfCommon.h"
#include <unordered_map>
#include "BeefySysLib/util/Hash.h"

//#define BL_USE_DENSEMAP_SYMTAB

#ifdef BL_USE_DENSEMAP_SYMTAB
#include <sparsehash/dense_hash_map>
#include <sparsehash/dense_hash_set>
#endif

NS_BF_BEGIN

class BlContext;
class BlSymbol;

class BlSymTable
{
public:
	BlContext* mContext;
#ifdef BL_USE_DENSEMAP_SYMTAB
	google::dense_hash_map<Val128, BlSymbol*, Val128::Hash, Val128::Equals> mMap;
#else
	std::unordered_map<Val128, BlSymbol*, Val128::Hash, Val128::Equals> mMap;
#endif

public:	
	BlSymTable();

	BlSymbol* Add(const char* name, bool* isNew = NULL);	
};

NS_BF_END

