#include "MemReporter.h"

USING_NS_BF;

MemReporter::MemReporter()
{
	mCurEntry = &mRoot;
	mRoot.mName = "Root";
	mShowInKB = true;
}

MemReporter::~MemReporter()
{
}

int MemReporter::GetChildSizes(Entry* entry)
{
	int childSizes = 0;
	for (auto& childPair : entry->mChildren)		
	{
		auto child = childPair.mValue;
		childSizes += child->mSize + GetChildSizes(child);
	}
	return childSizes;
}

void MemReporter::Report(int depth, Entry* entry)
{
	String str;
	for (int i = 0; i < depth; i++)
		str += "  ";
	
	str += entry->mName;
	while (str.length() < 64)
		str +=  ' ';	

	if (entry->mChildSize == -1)
		entry->mChildSize = GetChildSizes(entry);

	if (mShowInKB)
		str += StrFormat("%6d %6dk %6dk\r\n", entry->mCount, entry->mSize / 1024, (entry->mSize + entry->mChildSize) / 1024);
	else
		str += StrFormat("%6d %6d %6d\r\n", entry->mCount, entry->mSize, (entry->mSize + entry->mChildSize));
    BfpOutput_DebugString(str.c_str());

	Array<Entry*> entries;
	for (auto& kv : entry->mChildren)
	{ 
		auto* entry = kv.mValue;
		entry->mChildSize = GetChildSizes(entry);
		entries.Add(kv.mValue);
	}

	entries.Sort([](Entry* lhs, Entry* rhs) { return (lhs->mSize + lhs->mChildSize) > (rhs->mSize + rhs->mChildSize); });

	for (auto& entry : entries)
	{
		Report(depth + 1, entry);
	}
}

void MemReporter::BeginSection(const StringImpl& name)
{
	Entry** entryPtr;
	if (!mCurEntry->mChildren.TryAdd(name, NULL, &entryPtr))
	{		
		mCurEntry = *entryPtr;
		mCurEntry->mCount++;
		return;
	}

	auto newEntry = mAlloc.Alloc<Entry>();
	newEntry->mCount++;
	newEntry->mName = name;
	newEntry->mParent = mCurEntry;
	*entryPtr = newEntry;
	mCurEntry = newEntry;
}

void MemReporter::Add(int size)
{
	mCurEntry->mSize += size;
}

void MemReporter::Add(const StringImpl& name, int size)
{
	BeginSection(name);
	Add(size);
	EndSection();
}

void MemReporter::EndSection()
{
	mCurEntry = mCurEntry->mParent;
}

void MemReporter::Report()
{		
	Report(0, &mRoot);
}
