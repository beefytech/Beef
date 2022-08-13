#include "DbgTypeMap.h"
#include "DbgModule.h"

USING_NS_BF_DBG;
USING_NS_BF;

//DbgTypeMap::Iterator::Iterator(DbgTypeMap* map)
//{
//	mMap = map;
//	mCurBucket = 0;
//	mCurEntry = NULL;
//}
//
//DbgTypeMap::Iterator& DbgTypeMap::Iterator::operator++()
//{
//	if (mCurEntry != NULL)
//	{
//		mCurEntry = mCurEntry->mNext;
//		if (mCurEntry != NULL)
//			return *this;
//		mCurBucket++;
//	}
//
//	while (mCurBucket < HashSize)
//	{
//		mCurEntry = mMap->mHashHeads[mCurBucket];
//		while (mCurEntry != NULL)
//		{
//			if (mCurEntry->mValue != NULL)
//				return *this;
//			mCurEntry = mCurEntry->mNext;
//		}
//		mCurBucket++;
//	}
//
//	return *this; // At end
//}
//
//bool DbgTypeMap::Iterator::operator!=(const DbgTypeMap::Iterator& itr) const
//{
//	return ((itr.mCurEntry != mCurEntry) || (itr.mCurBucket != mCurBucket));
//}
//
//DbgTypeMap::Entry* DbgTypeMap::Iterator::operator*()
//{
//	return mCurEntry;
//}
//
////////////////////////////////////////////////////////////////////////////
//
//int DbgTypeMap::GetHash(const char* str, DbgLanguage language)
//{
//	int curHash = (int)language;
//	const char* curHashPtr = str;
//	while (*curHashPtr != 0)
//	{
//		char c = *curHashPtr;
//		if (c == ':')
//		{
//			if ((curHashPtr[1] == '_') && (curHashPtr[2] == '_') && (curHashPtr[3] == 'l'))
//			{
//				curHashPtr += 4;
//				continue;
//			}
//		}
//		else if ((c != ' ') && (c != '.') && (c != '`') && (c != '\''))
//			curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
//		curHashPtr++;
//	}
//
//	return curHash & 0x7FFFFFFF;
//}
//
//bool DbgTypeMap::StrEqual(const char* inStr, const char* mapStr)
//{
//	const char* inPtr = inStr;
//	const char* mapPtr = mapStr;
//
//	while (true)
//	{
//		char cIn;
//		do { cIn = *(inPtr++); } while (cIn == ' ');
//		char cMap;
//		do { cMap = *(mapPtr++); } while (cMap == ' ');
//		if (cIn != cMap)
//		{
//			if ((cIn == '`') || (cIn == '\''))
//			{
//				cIn = *(inPtr++);
//				if ((cIn >= '0') && (cIn <= '9'))
//				{
//					if ((cMap == '_') && (mapPtr[0] == '_') && (mapPtr[1] == 'l'))
//					{
//						mapPtr += 2;
//						cMap = *(mapPtr++);
//					}
//				}
//
//				if (cIn == cMap)
//					continue;
//			}
//
//			if ((cIn == '.') && (cMap == ':') && (mapPtr[0] == ':'))
//			{
//				mapPtr++;
//				continue;;
//			}
//			if ((cMap == '.') && (cIn == ':') && (inPtr[0] == ':'))
//			{
//				inPtr++;
//				continue;;
//			}
//			return false;
//		}
//		if (cIn == '\0')
//			return true;
//	}
//}
//
//DbgTypeMap::DbgTypeMap()
//{
//	mHashHeads = NULL;
//}
//
//bool DbgTypeMap::IsEmpty()
//{
//	if (mHashHeads == NULL)
//		return true;
//	return false;
//}
//
//void DbgTypeMap::Clear()
//{
//	mHashHeads = NULL;
//	mAlloc.Clear();
//}
//
//void DbgTypeMap::Insert(DbgType* value)
//{
//	if (mHashHeads == NULL)
//		mHashHeads = (Entry**)mAlloc.AllocBytes(sizeof(Entry*) * HashSize, alignof(Entry*));
//
//	int hash = GetHash(value->mName, value->mLanguage) % HashSize;
//	Entry* headEntry = mHashHeads[hash];
//	if ((headEntry != NULL) && (headEntry->mValue == NULL))
//	{
//		// If we removed an old entry, just take over the node
//		headEntry->mValue = value;
//		return;
//	}
//
//	Entry* newEntry = mAlloc.Alloc<Entry>();
//	newEntry->mValue = value;
//	newEntry->mNext = headEntry;
//
//	mHashHeads[hash] = newEntry;
//}
//
//DbgTypeMap::Entry* DbgTypeMap::Find(const char* name, DbgLanguage language)
//{
//	if (language == DbgLanguage_BeefUnfixed)
//	{
//		std::string str = name;
//		name = str.c_str();
//
//		/*char* nameStart = (char*)name;
//		if (strncmp(nameStart, "Box<", 4) == 0)
//		{
//			for (int i = 0; i < 4; i++)
//				nameStart[i] = ' ';
//			nameStart[strlen(nameStart) - 1] = '^';
//		}*/
//
//		for (char* cPtr = (char*)name; true; cPtr++)
//		{
//			char c = *cPtr;
//			if (c == 0)
//				break;
//
//			if (c == '*')
//			{
//				char* nameStart = NULL;
//
//				// Find our way to the end and remove the star
//				int chevCount = 0;
//				for (char* cTestPtr = cPtr - 1; cTestPtr >= name; cTestPtr--)
//				{
//					char c = *cTestPtr;
//					if (c == 0)
//						break;
//					if (c == '<')
//					{
//						chevCount--;
//						if (chevCount < 0)
//						{
//							nameStart = cTestPtr + 1;
//							break;
//						}
//					}
//					else if (c == '>')
//						chevCount++;
//					else if (chevCount == 0)
//					{
//						if ((c == '&') || (c == '*'))
//							break; // Invalid
//
//						if (cTestPtr == name)
//						{
//							nameStart = cTestPtr;
//							break;
//						}
//
//						if ((c == ' ') || (c == ','))
//						{
//							nameStart = cTestPtr + 1;
//							break;
//						}
//					}
//				}
//
//				if (nameStart != NULL)
//				{
//					if (strncmp(nameStart, "System.Array1", 13) == 0)
//					{
//						for (int i = 0; i < 14; i++)
//							nameStart[i] = ' ';
//						cPtr[-1] = '[';
//						cPtr[0] = ']';
//					}
//					else
//					{
//						*cPtr = 0;
//						auto foundEntry = Find(nameStart, DbgLanguage_Beef);
//
//						bool isObject = false;
//						if (foundEntry != NULL)
//						{
//							isObject = foundEntry->mValue->IsBfObject();
//						}
//
//						if (isObject)
//						{
//							*cPtr = ' ';
//						}
//						else
//							*cPtr = '*';
//					}
//				}
//				nameStart = NULL;
//			}
//			/*else if ((c == ' ') || (c == '<') || (c == '>') || (c == '&'))
//				nameStart = NULL;
//			else if (nameStart == NULL)
//				nameStart = cPtr;*/
//		}
//
//		return Find(name, DbgLanguage_Beef);
//	}
//
//	if (mHashHeads == NULL)
//		return NULL;
//
//	int hash = GetHash(name, language) % HashSize;
//	Entry* checkEntry = mHashHeads[hash];
//	while (checkEntry != NULL)
//	{
//		if ((checkEntry->mValue->mLanguage == language) && (StrEqual(name, checkEntry->mValue->mName)))
//			return checkEntry;
//		checkEntry = checkEntry->mNext;
//	}
//	return NULL;
//}
//
//DbgTypeMap::Iterator DbgTypeMap::begin()
//{
//	return ++Iterator(this);
//}
//
//DbgTypeMap::Iterator DbgTypeMap::end()
//{
//	Iterator itr(this);
//	itr.mCurBucket = HashSize;
//	return itr;
//}
//

int DbgTypeMap::GetHash(const char* str, DbgLanguage language)
{
	int curHash = (int)language;
	const char* curHashPtr = str;
	while (*curHashPtr != 0)
	{
		char c = *curHashPtr;
		if (c == ':')
		{
			if ((curHashPtr[1] == '_') && (curHashPtr[2] == '_') && (curHashPtr[3] == 'l'))
			{
				curHashPtr += 4;
				continue;
			}
		}
		else if ((c != ' ') && (c != '.') && (c != '`') && (c != '\''))
			curHash = ((curHash ^ *curHashPtr) << 5) - curHash;
		curHashPtr++;
	}

	return curHash & 0x7FFFFFFF;
}

int DbgTypeMap::GetHash(DbgType* dbgType)
{
	return GetHash(dbgType->mName, dbgType->mLanguage);
}

bool DbgTypeMap::IsEmpty()
{
	return mMap.IsEmpty();
}

void DbgTypeMap::Clear()
{
	mMap.Clear();
}

void DbgTypeMap::Insert(DbgType* value)
{
	Entry entry;
	entry.mValue = value;
	mMap.Add(entry);
}

bool DbgTypeMap::StrEqual(const char* inStr, const char* mapStr)
{
	const char* inPtr = inStr;
	const char* mapPtr = mapStr;

	while (true)
	{
		char cIn;
		do { cIn = *(inPtr++); } while (cIn == ' ');
		char cMap;
		do { cMap = *(mapPtr++); } while (cMap == ' ');
		if (cIn != cMap)
		{
			if ((cIn == '`') || (cIn == '\''))
			{
				cIn = *(inPtr++);
				if ((cIn >= '0') && (cIn <= '9'))
				{
					if ((cMap == '_') && (mapPtr[0] == '_') && (mapPtr[1] == 'l'))
					{
						mapPtr += 2;
						cMap = *(mapPtr++);
					}
				}

				if (cIn == cMap)
					continue;
			}

			if ((cIn == '.') && (cMap == ':') && (mapPtr[0] == ':'))
			{
				mapPtr++;
				continue;;
			}
			if ((cMap == '.') && (cIn == ':') && (inPtr[0] == ':'))
			{
				inPtr++;
				continue;;
			}
			return false;
		}
		if (cIn == '\0')
			return true;
	}
}

DbgTypeMap::Entry* DbgTypeMap::Find(const char* name, DbgLanguage language)
{
	if (language == DbgLanguage_BeefUnfixed)
	{
		std::string str = name;
		name = str.c_str();

		for (char* cPtr = (char*)name; true; cPtr++)
		{
			char c = *cPtr;
			if (c == 0)
				break;

			if (c == '*')
			{
				char* nameStart = NULL;

				// Find our way to the end and remove the star
				int chevCount = 0;
				for (char* cTestPtr = cPtr - 1; cTestPtr >= name; cTestPtr--)
				{
					char c = *cTestPtr;
					if (c == 0)
						break;
					if (c == '<')
					{
						chevCount--;
						if (chevCount < 0)
						{
							nameStart = cTestPtr + 1;
							break;
						}
					}
					else if (c == '>')
						chevCount++;
					else if (chevCount == 0)
					{
						if ((c == '&') || (c == '*'))
							break; // Invalid

						if (cTestPtr == name)
						{
							nameStart = cTestPtr;
							break;
						}

						if ((c == ' ') || (c == ','))
						{
							nameStart = cTestPtr + 1;
							break;
						}
					}
				}

				if (nameStart != NULL)
				{
					if (strncmp(nameStart, "System.Array1", 13) == 0)
					{
						for (int i = 0; i < 14; i++)
							nameStart[i] = ' ';
						cPtr[-1] = '[';
						cPtr[0] = ']';
					}
					else
					{
						*cPtr = 0;
						auto foundEntry = Find(nameStart, DbgLanguage_Beef);

						bool isObject = false;
						if (foundEntry != NULL)
						{
							isObject = foundEntry->mValue->IsBfObject();
						}

						if (isObject)
						{
							*cPtr = ' ';
						}
						else
							*cPtr = '*';
					}
				}
				nameStart = NULL;
			}
			/*else if ((c == ' ') || (c == '<') || (c == '>') || (c == '&'))
				nameStart = NULL;
			else if (nameStart == NULL)
				nameStart = cPtr;*/
		}

		return Find(name, DbgLanguage_Beef);
	}

	int hashCode = GetHash(name, language) & 0x7FFFFFFF;
	for (int i = mMap.mBuckets[hashCode % mMap.mAllocSize]; i >= 0; i = mMap.mEntries[i].mNext)
	{
		if (mMap.mEntries[i].mHashCode == hashCode)
		{
			Entry* entry = (Entry*)&mMap.mEntries[i].mKey;
			if (StrEqual(name, entry->mValue->mName))
				return entry;
		}
	}

	return NULL;
}

Beefy::HashSet<DbgTypeMap::Entry>::iterator DbgTypeMap::begin()
{
	return mMap.begin();
}

Beefy::HashSet<DbgTypeMap::Entry>::iterator DbgTypeMap::end()
{
	return mMap.end();
}