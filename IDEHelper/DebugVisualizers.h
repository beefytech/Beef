#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Hash.h"
#include "Compiler/BfUtil.h"
#include "DebugCommon.h"
#include "extern/toml/toml.h"

NS_BF_BEGIN

class DebugVisualizerEntry
{
public:
	class ExpandItem
	{
	public:
		String mName;
		String mValue;
		String mCondition;
	};

	class DisplayStringEntry
	{
	public:
		String mCondition;
		String mString;
	};

	enum CollectionType
	{
		CollectionType_None,
		CollectionType_Array,
		CollectionType_IndexItems,
		CollectionType_TreeItems,
		CollectionType_LinkedList,
		CollectionType_Delegate,
		CollectionType_Dictionary,
		CollectionType_ExpandedItem
	};

public:
	String mName;
	DbgFlavor mFlavor;
	OwnedVector<DisplayStringEntry> mDisplayStrings;
	OwnedVector<DisplayStringEntry> mStringViews;
	String mAction;

	OwnedVector<ExpandItem> mExpandItems;
	CollectionType mCollectionType;
	
	String mSize;
	Array<String> mLowerDimSizes;
	String mNextPointer;
	String mHeadPointer;
	String mEndPointer;
	String mLeftPointer;
	String mRightPointer;
	String mValueType;
	String mValuePointer;
	String mTargetPointer;		
	String mCondition;
	String mBuckets;
	String mEntries;
	String mKey;		
	bool mShowedError;
	bool mShowElementAddrs;

	String mDynValueType;
	String mDynValueTypeIdAppend;

public:
	DebugVisualizerEntry()
	{
		mFlavor = DbgFlavor_Unknown;
		mCollectionType = CollectionType_None;
		mShowedError = false;
		mShowElementAddrs = false;
	}	
};

class DebugVisualizers
{
public:
	Val128 mHash;
	String mErrorString;
	String mCurFileName;
	const char* mSrcStr;
	OwnedVector<DebugVisualizerEntry> mDebugVisualizers;
	
	void Fail(const StringImpl& error);
	void Fail(const StringImpl& error, const toml::Value& value);	

	String ExpectString(const toml::Value& value);
	bool ExpectBool(const toml::Value& value);
	const toml::Table& ExpectTable(const toml::Value& value);
	const toml::Array& ExpectArray(const toml::Value& value);

public:
	DebugVisualizers();

	bool ReadFileTOML(const StringImpl& fileName);
	bool Load(const StringImpl& fileNamesStr);
	DebugVisualizerEntry* FindEntryForType(const StringImpl& typeName, DbgFlavor wantFlavor, Array<String>* wildcardCaptures);

	String DoStringReplace(const StringImpl& origStr, const Array<String>& wildcardCaptures);
};

NS_BF_END