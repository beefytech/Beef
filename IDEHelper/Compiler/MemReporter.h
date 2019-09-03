#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/BumpAllocator.h"
#include "BeefySysLib/util/Dictionary.h"
#include <vector>

NS_BF_BEGIN

class MemReporter
{
public:
	struct Entry
	{
		Dictionary<String, Entry*> mChildren;
		String mName;
		int mSize;
		int mChildSize;
		int mCount;
		Entry* mParent;

		Entry()
		{
			mSize = 0;
			mChildSize = -1;
			mParent = NULL;
			mCount = 0;
		}
	};

	BumpAllocator mAlloc;
	Entry mRoot;
	Entry* mCurEntry;
	bool mShowInKB;

public:	
	int GetChildSizes(Entry* entry);
	void Report(int depth, Entry* entry);

public:	
	MemReporter();
	~MemReporter();

	void BeginSection(const StringView& name);
	void Add(int size);
	void Add(const StringView& name, int size);
	
	template <typename T>
	void AddVec(const T& vec, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(T) : 0) + (int)vec.mAllocSize * sizeof(typename T::value_type));
	}
	
	template <typename T>
	void AddVec(const StringView& name, const T& vec, bool addContainerSize = true)
	{
		BeginSection(name);
		Add((addContainerSize ? sizeof(T) : 0) + (int)vec.mAllocSize * sizeof(typename T::value_type));
		EndSection();
	}
	
	template <typename T>
	void AddVecPtr(const std::vector<T>& vec, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(T) : 0) +
			(int)vec.capacity() * sizeof(T) + 
			(int)vec.size() * sizeof(typename std::remove_pointer<T>::type)); //-V220
	}
	
	template <typename T>
	void AddVecPtr(const Array<T>& vec, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(T) : 0) +
			(int)vec.mAllocSize * sizeof(T) +
			(int)vec.size() * sizeof(typename std::remove_pointer<T>::type)); //-V220		
	}

	template <typename T>
	void AddVecPtr(const StringView& name, const Array<T>& vec, bool addContainerSize = true)
	{
		Add(name, (addContainerSize ? sizeof(T) : 0) +
			(int)vec.mAllocSize * sizeof(T) +
			(int)vec.size() * sizeof(typename std::remove_pointer<T>::type)); //-V220		
	}

	template <typename T>
	void AddMap(const StringView& name, const T& map, bool addContainerSize = true)
	{		
		Add(name, (addContainerSize ? sizeof(T) : 0) + map.mAllocSize * (sizeof(typename T::EntryPair) + sizeof(typename T::int_cosize)));
	}

	template <typename T>
	void AddMap(const T& map, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(T) : 0) + map.mAllocSize * (sizeof(typename T::EntryPair) + sizeof(typename T::int_cosize)));
	}

	template <typename T>
	void AddHashSet(const StringView& name, const T& map, bool addContainerSize = true)
	{
		Add(name, (addContainerSize ? sizeof(T) : 0) + map.mAllocSize * (sizeof(typename T::Entry) + sizeof(typename T::int_cosize)));
	}

	template <typename T>
	void AddHashSet(const T& map, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(T) : 0) + map.mAllocSize * (sizeof(typename T::Entry) + sizeof(typename T::int_cosize)));
	}
	
	void AddStr(const StringImpl& str, bool addContainerSize = true)
	{
		Add((addContainerSize ? sizeof(StringImpl) : 0) + (int)str.GetAllocSize());
	}

	void AddStr(const StringView& name, const StringImpl& str, bool addContainerSize = true)
	{
		Add(name, (addContainerSize ? sizeof(StringImpl) : 0) + (int)str.GetAllocSize());
	}

	void EndSection();
	void Report();


	template <typename T>
	void AddBumpAlloc(const StringView& name, const T& alloc)
	{		
		BeginSection(name);
				
		int usedSize = alloc.CalcUsedSize();
#ifdef BUMPALLOC_TRACKALLOCS

		T* allocPtr = (T*)&alloc;

		int usedSizeLeft = usedSize;

		Array<Entry> entries;
		for (const auto& kv : allocPtr->mTrackedAllocs)
		{
			const char* str = kv.mKey.c_str();
			BumpAllocTrackedEntry trackedEntry = kv.mValue;

			String name;

			if (strncmp(str, "class ", 6) == 0)
				name = str + 6;
			else if (strncmp(str, "struct ", 7) == 0)
				name = str + 7;
			else
				name = str;

			BeginSection(name);
			mCurEntry->mSize += trackedEntry.mSize;
			mCurEntry->mCount += trackedEntry.mCount;
			EndSection();

			usedSizeLeft -= trackedEntry.mSize;
		}
		if (usedSizeLeft > 0)
			Add("Unaccounted", usedSizeLeft);
#else
		Add("Used", usedSize);
#endif		
		Add("Waste", alloc.GetAllocSize() - usedSize);
		Add("Unused", alloc.GetTotalAllocSize() - alloc.GetAllocSize());		

		EndSection();
	}
};

class AutoMemReporter
{
public:
	MemReporter* mMemReporter;	

public:
	AutoMemReporter(MemReporter* memReporter, const StringImpl& name)
	{
		mMemReporter = memReporter;
		mMemReporter->BeginSection(name);
	}

	~AutoMemReporter()
	{
		mMemReporter->EndSection();
	}
};

NS_BF_END
