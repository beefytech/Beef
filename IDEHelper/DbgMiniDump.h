#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/util/MappedFile.h"
#include "Beef/BfCommon.h"

NS_BF_BEGIN

enum DbgMiniDumpStreamType : uint32
{
	DbgMiniDumpStreamType_ThreadList = 3,
	DbgMiniDumpStreamType_ModuleList = 4,
	DbgMiniDumpStreamType_MemoryLists = 5,
	DbgMiniDumpStreamType_Exception = 6,
	DbgMiniDumpStreamType_SystemInfo = 7,
	DbgMiniDumpStreamType_ThreadEx = 8,
	DbgMiniDumpStreamType_Memory64List = 9,
	DbgMiniDumpStreamType_CommentA = 10,
	DbgMiniDumpStreamType_CommentW = 11,
	DbgMiniDumpStreamType_HandleData = 12,
	DbgMiniDumpStreamType_FunctionTable = 13,
	DbgMiniDumpStreamType_UnloadedModuleList = 14,
	DbgMiniDumpStreamType_MiscInfo = 15,
	DbgMiniDumpStreamType_MemoryInfoList = 16,
	DbgMiniDumpStreamType_ThreadInfoList = 17,
	DbgMiniDumpStreamType_HandleOperationList = 18
};

//MINIDUMP_SYSTEM_INFO

struct StreamDirectoryEntry
{
	DbgMiniDumpStreamType mStreamType;
	uint32 mDataSize;
	uint32 mDataRVA;
};

class DbgMiniDump
{
public:
	MappedFile mMF;
	BfSizedArray<StreamDirectoryEntry> mDirectory;

public:

	bool StartLoad(const StringImpl& path);
	int GetTargetBitCount();

	template <typename T> 
	T& GetStreamData(const StreamDirectoryEntry& entry)
	{
		return *(T*)((uint8*)mMF.mData + entry.mDataRVA);
	}

	template <typename T>
	T& GetData(uint32 rva)
	{
		return *(T*)((uint8*)mMF.mData + rva);
	}
};

NS_BF_END
