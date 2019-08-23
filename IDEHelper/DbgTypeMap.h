#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/Util/BumpAllocator.h"
#include "BeefySysLib/Util/HashSet.h"

namespace Beefy
{
	enum DbgLanguage : int8;
}

NS_BF_DBG_BEGIN

class DbgType;

/*class DbgTypeMap
{
public:
	static const int HashSize = 9973;
	Beefy::BumpAllocator mAlloc;

	struct Entry
	{
		DbgType* mValue;
		Entry* mNext;
	};

	struct Iterator
	{
	public:
		DbgTypeMap* mMap;
		Entry* mCurEntry;
		int mCurBucket;

	public:
		Iterator(DbgTypeMap* map);
		Iterator& operator++();
		bool operator!=(const Iterator& itr) const;
		Entry* operator*();
	};

public:
	int GetHash(const char* str, Beefy::DbgLanguage language);

	bool StrEqual(const char* strA, const char* strB);

public:
	Entry** mHashHeads;

public:
	DbgTypeMap();

	bool IsEmpty();
	void Clear();
	void Insert(DbgType* value);
	Entry* Find(const char* name, Beefy::DbgLanguage language);
	Iterator begin();
	Iterator end();
};*/

class DbgTypeMap
{
public:
	struct Entry
	{
		DbgType* mValue;

		bool operator==(const Entry& rhs)
		{
			return mValue == rhs.mValue;
		}
	};

public:
	Beefy::HashSet<Entry> mMap;

public:
	static int GetHash(const char* str, Beefy::DbgLanguage language);
	static int GetHash(DbgType* dbgType);
	static bool StrEqual(const char* strA, const char* strB);

	bool IsEmpty();
	void Clear();

	void Insert(DbgType* value);
	Entry* Find(const char* name, Beefy::DbgLanguage language);
	Beefy::HashSet<Entry>::iterator begin();
	Beefy::HashSet<Entry>::iterator end();
};

NS_BF_DBG_END

namespace std
{
	template<>
	struct hash<NS_BF_DBG::DbgTypeMap::Entry>
	{
		size_t operator()(const NS_BF_DBG::DbgTypeMap::Entry& val) const
		{
			return NS_BF_DBG::DbgTypeMap::GetHash(val.mValue);
		}
	};
}