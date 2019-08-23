#include "BlSymTable.h"
#include "BlContext.h"

USING_NS_BF;

BlSymTable::BlSymTable()
{
#ifdef BL_USE_DENSEMAP_SYMTAB
	mMap.set_empty_key(Val128(-1));
	//mMap.set_empty_key("");
	mMap.min_load_factor(0);	
	mMap.resize(100000);
#endif
}

BlSymbol* BlSymTable::Add(const char* name, bool* isNew)
{	
	Val128 val128 = Hash128(name, (int)strlen(name));
	auto itr = mMap.insert(std::make_pair(val128, (BlSymbol*)NULL));
	if (!itr.second)
	{
		if (isNew != NULL)
			*isNew = false;
		return itr.first->second;
	}

	if (isNew != NULL)
		*isNew = true;
	auto blSymbol = new BlSymbol();
	itr.first->second = blSymbol;	
	blSymbol->mName = name;
	blSymbol->mKind = BlSymKind_Undefined;
	blSymbol->mObjectDataIdx = -1;
	return blSymbol;	
}

