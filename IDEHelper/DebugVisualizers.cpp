#include "DebugVisualizers.h"

USING_NS_BF;

void DebugVisualizers::Fail(const StringImpl& error)
{
	if (mErrorString.length() != 0)
		return;
	mErrorString = StrFormat("Debug visualizer failure: %s in %s", error.c_str(), mCurFileName.c_str());
}

void DebugVisualizers::Fail(const StringImpl& error, const toml::Value& value)
{
	if (mErrorString.length() != 0)
		return;

	if (value.lineNo != -1)
		mErrorString = StrFormat("Debug visualizer failure: %s at line %d in %s", error.c_str(), value.lineNo, mCurFileName.c_str());
	else
		mErrorString = StrFormat("Debug visualizer failure: %s in %s", error.c_str(), mCurFileName.c_str());
}

DebugVisualizers::DebugVisualizers()
{
	mSrcStr = NULL;
}

static bool StrEquals(const char* str1, const char* str2)
{
	if (str1 == str2)
		return true;
	if ((str1 == NULL) || (str2 == NULL))
		return false;
	return strcmp(str1, str2) == 0;
}

bool DebugVisualizers::ExpectBool(const toml::Value& value)
{
	if (!value.is<bool>())
	{
		Fail("Expected boolean", value);
		return false;
	}
	const auto& val = value.as<bool>();
	return val;
}

String DebugVisualizers::ExpectString(const toml::Value& value)
{
	if (!value.is<std::string>())
	{
		Fail("Expected string", value);
		return "";
	}
	const auto& val = value.as<std::string>();
	return val;
}

const toml::Table& DebugVisualizers::ExpectTable(const toml::Value& value)
{
	static toml::Table emptyTable;
	if (!value.is<toml::Table>())
	{
		Fail("Expected table", value);
		return emptyTable;
	}
	return value.as<toml::Table>();
}

const toml::Array& DebugVisualizers::ExpectArray(const toml::Value& value)
{
	static toml::Array emptyArray;
	if (!value.is<toml::Array>())
	{
		Fail("Expected array", value);
		return emptyArray;
	}
	return value.as<toml::Array>();
}

bool DebugVisualizers::ReadFileTOML(const StringImpl& fileName)
{
	std::ifstream ifs(fileName);
	toml::ParseResult pr = toml::parse(ifs);

	if (!pr.valid())
	{
		Fail(pr.errorReason);
		return false;
	}

	const toml::Table& root = pr.value.as<toml::Table>();
	for (auto& kv : root)
	{
		auto& topName = kv.first;
		auto& topValue = kv.second;

		if (topName == "Type")
		{
			for (auto typeValue : ExpectArray(topValue))
			{
				auto& value = kv.second;

				DebugVisualizerEntry* entry = mDebugVisualizers.Alloc();

				for (auto& kv : ExpectTable(typeValue))
				{
					auto& name = kv.first;
					auto& value = kv.second;

					if (name == "Name")
					{
						entry->mName = ExpectString(value);
					}
					else if (name == "Flavor")
					{
						String flavorName = ExpectString(value);
						if (flavorName == "GNU")
							entry->mFlavor = DbgFlavor_GNU;
						else if (flavorName == "MS")
							entry->mFlavor = DbgFlavor_MS;
						else
							Fail("Unexpected flavor", value);
					}
					else if ((name == "DisplayString") | (name == "StringView"))
					{
						bool isStringView = name == "StringView";

						if (value.is<toml::Array>())
						{
							for (auto& displayValue : ExpectArray(value))
							{
								DebugVisualizerEntry::DisplayStringEntry* displayStringEntry = new DebugVisualizerEntry::DisplayStringEntry();

								for (auto& kv : ExpectTable(displayValue))
								{
									auto& name = kv.first;
									auto& value = kv.second;
									if (name == "Condition")
										displayStringEntry->mCondition = ExpectString(value);
									else if (name == "String")
										displayStringEntry->mString = ExpectString(value);
									else
										Fail("Unexpected entry", value);
								}

								if (isStringView)
									entry->mStringViews.push_back(displayStringEntry);
								else
									entry->mDisplayStrings.push_back(displayStringEntry);
							}
						}
						else
						{
							DebugVisualizerEntry::DisplayStringEntry* displayStringEntry = new DebugVisualizerEntry::DisplayStringEntry();
							displayStringEntry->mString = ExpectString(value);
							if (isStringView)
								entry->mStringViews.push_back(displayStringEntry);
							else
								entry->mDisplayStrings.push_back(displayStringEntry);
						}
					}
					else if (name == "Action")
					{
						entry->mAction = ExpectString(value);
					}
					else if (name == "Expand")
					{
						for (auto& kv : ExpectTable(value))
						{
							auto& name = kv.first;
							auto& value = kv.second;
							if (name == "ExpandedItem")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_ExpandedItem;
								entry->mValuePointer = ExpectString(value);
							}
							else if (name == "Item")
							{
								for (auto& itemValue : ExpectArray(value))
								{
									DebugVisualizerEntry::ExpandItem* expandItem = entry->mExpandItems.Alloc();

									for (auto& kv : ExpectTable(itemValue))
									{
										auto& name = kv.first;
										auto& value = kv.second;

										if (name == "Name")
											expandItem->mName = ExpectString(value);
										else if (name == "Value")
											expandItem->mValue = ExpectString(value);
										else if (name == "Condition")
											expandItem->mCondition = ExpectString(value);
										else
											Fail("Unexpected entry", value);
									}
								}
							}
							else if (name == "ArrayItems")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_Array;

								for (auto& kv : ExpectTable(value))
								{
									auto& name = kv.first;
									auto& value = kv.second;
									if (name == "Size")
										entry->mSize = ExpectString(value);
									else if (name == "LowerDimSizes")
									{
										for (auto& dimValue : ExpectArray(value))
											entry->mLowerDimSizes.push_back(ExpectString(dimValue));
									}
									else if (name == "ValuePointer")
										entry->mValuePointer = ExpectString(value);
									else if (name == "Condition")
										entry->mCondition = ExpectString(value);
									else
										Fail("Unexpected entry", value);
								}
							}
							else if (name == "IndexListItems")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_IndexItems;

								for (auto& kv : ExpectTable(value))
								{
									auto& name = kv.first;
									auto& value = kv.second;

									if (name == "Size")
										entry->mSize = ExpectString(value);
									else if (name == "LowerDimSizes")
									{
										for (auto& dimValue : ExpectArray(value))
											entry->mLowerDimSizes.push_back(ExpectString(dimValue));
									}
									else if (name == "ValueNode")
										entry->mValuePointer = ExpectString(value);
									else
										Fail("Unexpected entry", value);
								}
							}
							else if (name == "TreeItems")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_TreeItems;

								for (auto& kv : ExpectTable(value))
								{
									auto& name = kv.first;
									auto& value = kv.second;

									if (name == "Size")
										entry->mSize = ExpectString(value);
									else if (name == "HeadPointer")
										entry->mHeadPointer = ExpectString(value);
									else if (name == "LeftPointer")
										entry->mLeftPointer = ExpectString(value);
									else if (name == "RightPointer")
										entry->mRightPointer = ExpectString(value);
									else if (name == "ValuePointer")
										entry->mValuePointer = ExpectString(value);
									else if (name == "ValueType")
										entry->mValueType = ExpectString(value);
									else if (name == "Condition")
										entry->mCondition = ExpectString(value);
									else
										Fail("Unexpected entry", value);
								}
							}
							else if (name == "LinkedListItems")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_LinkedList;

								for (auto& kv : ExpectTable(value))
								{
									auto& name = kv.first;
									auto& value = kv.second;

									if (name == "HeadPointer")
										entry->mHeadPointer = ExpectString(value);
									else if (name == "Size")
										entry->mSize = ExpectString(value);
									else if (name == "EndPointer")
										entry->mEndPointer = ExpectString(value);
									else if (name == "NextPointer")
										entry->mNextPointer = ExpectString(value);
									else if (name == "ValuePointer")
										entry->mValuePointer = ExpectString(value);
									else if (name == "ValueType")
										entry->mValueType = ExpectString(value);
									else if (name == "DynValueType")
										entry->mDynValueType = ExpectString(value);
									else if (name == "DynValueTypeIdAppend")
										entry->mDynValueTypeIdAppend = ExpectString(value);
									else if (name == "ShowElementAddrs")
										entry->mShowElementAddrs = ExpectBool(value);
									else
										Fail("Unexpected entry", value);
								}
							}
							else if (name == "DictionaryItems")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_Dictionary;

								for (auto& kv : ExpectTable(value))
								{
									auto& name = kv.first;
									auto& value = kv.second;

									if (name == "Size")
										entry->mSize = ExpectString(value);
									else if (name == "Buckets")
										entry->mBuckets = ExpectString(value);
									else if (name == "Entries")
										entry->mEntries = ExpectString(value);
									else if (name == "Key")
										entry->mKey = ExpectString(value);
									else if (name == "Value")
										entry->mValuePointer = ExpectString(value);
									else if (name == "Next")
										entry->mNextPointer = ExpectString(value);
									else
										Fail("Unexpected entry", value);
								}
							}
							else if (name == "CallStackList")
							{
								entry->mCollectionType = DebugVisualizerEntry::CollectionType_CallStackList;
							}
							else
								Fail("Unexpected entry", value);
						}
					}
					else
						Fail("Unexpected entry", value);
				}
			}
		}
		else
			Fail(StrFormat("Unexpected key '%s'", topName.c_str()), topValue);
	}

	return mErrorString.IsEmpty();
}

bool DebugVisualizers::Load(const StringImpl& fileNamesStr)
{
	bool hasError = !mErrorString.IsEmpty();
	mErrorString.Clear();

	Array<String> fileNames;
	int startIdx = 0;
	while (true)
	{
		int crPos = (int)fileNamesStr.IndexOf('\n', startIdx);
		if (crPos == -1)
		{
			fileNames.Add(fileNamesStr.Substring(startIdx));
			break;
		}

		fileNames.Add(fileNamesStr.Substring(startIdx, crPos - startIdx));
		startIdx = crPos + 1;
	}

	HashContext hashCtx;
	for (auto fileName : fileNames)
	{
		hashCtx.MixinStr(fileName);
		BfpTimeStamp lastWrite = BfpFile_GetTime_LastWrite(fileName.c_str());
		if (lastWrite == 0)
		{
			Fail(StrFormat("Failed to load debug visualizer file %s", fileName.c_str()));
		}
		hashCtx.Mixin(lastWrite);
	}

	Val128 hash = hashCtx.Finish128();
	if ((hash == mHash) && (!hasError))
		return true;
	mHash = hash;

	mDebugVisualizers.Clear();
	bool success = true;
	for (auto fileName : fileNames)
	{
		mCurFileName = fileName;
		if (!ReadFileTOML(fileName))
			success = false;
		mCurFileName.Clear();
	}
	return success;
}

DebugVisualizerEntry* DebugVisualizers::FindEntryForType(const StringImpl& typeName, DbgFlavor wantFlavor, Array<String>* wildcardCaptures)
{
	//TODO: Do smarter name matching. Right now we just compare up to the '*'

	for (auto entry : mDebugVisualizers)
	{
		if ((entry->mFlavor != DbgFlavor_Unknown) && (entry->mFlavor != wantFlavor))
			continue;

		const char* entryCharP = entry->mName.c_str();
		const char* typeCharP = typeName.c_str();
		bool isTemplate = false;

		while (true)
		{
			while (*typeCharP == ' ')
				typeCharP++;
			while (*entryCharP == ' ')
				entryCharP++;

			char typeC = *typeCharP;
			char entryC = *entryCharP;

			if (typeC == 0)
			{
				if (entryC == 0)
					return entry;
				break;
			}
			else if (entryC == 0)
				break;

			if (entryC == '<')
				isTemplate = true;

			if (entryC == '*')
			{
				int openDepth = 0;
				String wildcardCapture;
				while (true)
				{
					typeC = *typeCharP;

					bool isSep = typeC == ',';
					if ((!isTemplate) && ((typeC == '[') || (typeC == '?')))
						isSep = true;

					if (typeC == 0)
						break;
					if ((typeC == '<') || (typeC == '('))
						openDepth++;
					else if ((typeC == '>') || (typeC == ')'))
					{
						openDepth--;
						if (openDepth < 0)
						{
							typeCharP--;
							break;
						}
					}
					else if ((isSep) && (openDepth == 0))
					{
						typeCharP--;
						break;
					}

					wildcardCapture += typeC;
					typeCharP++;
				}

				if ((*typeCharP == 0) && (entryCharP[1] == 0))
					return entry;

				if (*typeCharP == 0)
					break;

				if (wildcardCaptures != NULL)
					wildcardCaptures->push_back(Trim(wildcardCapture));
			}
			else if (entryC != typeC)
				break;

			typeCharP++;
			entryCharP++;
		}

		if (wildcardCaptures != NULL)
			wildcardCaptures->Clear();
	}

	return NULL;
}

String DebugVisualizers::DoStringReplace(const StringImpl& origStr, const Array<String>& wildcardCaptures)
{
	String newString = origStr;
	for (int i = 0; i < (int)newString.length(); i++)
	{
		if ((newString[i] == '$') && (newString[i + 1] == 'T'))
		{
			int wildcardIdx = (int)newString[i + 2] - '1';
			if ((wildcardIdx >= 0) && (wildcardIdx < (int)wildcardCaptures.size()))
			{
				newString = newString.Substring(0, i) + wildcardCaptures[wildcardIdx] + newString.Substring(i + 3);
				i += 2;
			}
		}
	}
	return newString;
}