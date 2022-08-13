#include "DbgMiniDump.h"
#include <DbgHelp.h>

USING_NS_BF;

enum DbgMiniDumpFlags
{
	DbgMiniDumpFlag_MiniDumpNormal = 0x00000000,
	DbgMiniDumpFlag_MiniDumpWithDataSegs = 0x00000001,
	DbgMiniDumpFlag_MiniDumpWithFullMemory = 0x00000002,
	DbgMiniDumpFlag_MiniDumpWithHandleData = 0x00000004,
	DbgMiniDumpFlag_MiniDumpFilterMemory = 0x00000008,
	DbgMiniDumpFlag_MiniDumpScanMemory = 0x00000010,
	DbgMiniDumpFlag_MiniDumpWithUnloadedModules = 0x00000020,
	DbgMiniDumpFlag_MiniDumpWithIndirectlyReferencedMemory = 0x00000040,
	DbgMiniDumpFlag_MiniDumpFilterModulePaths = 0x00000080,
	DbgMiniDumpFlag_MiniDumpWithProcessThreadData = 0x00000100,
	DbgMiniDumpFlag_MiniDumpWithPrivateReadWriteMemory = 0x00000200,
	DbgMiniDumpFlag_MiniDumpWithoutOptionalData = 0x00000400,
	DbgMiniDumpFlag_MiniDumpWithFullMemoryInfo = 0x00000800,
	DbgMiniDumpFlag_MiniDumpWithThreadInfo = 0x00001000,
	DbgMiniDumpFlag_MiniDumpWithCodeSegs = 0x00002000,
	DbgMiniDumpFlag_MiniDumpWithoutAuxiliaryState = 0x00004000,
	DbgMiniDumpFlag_MiniDumpWithFullAuxiliaryState = 0x00008000,
	DbgMiniDumpFlag_MiniDumpWithPrivateWriteCopyMemory = 0x00010000,
	DbgMiniDumpFlag_MiniDumpIgnoreInaccessibleMemory = 0x00020000,
	DbgMiniDumpFlag_MiniDumpWithTokenInformation = 0x00040000,
	DbgMiniDumpFlag_MiniDumpWithModuleHeaders = 0x00080000,
	DbgMiniDumpFlag_MiniDumpFilterTriage = 0x00100000,
	DbgMiniDumpFlag_MiniDumpValidTypeFlags = 0x001fffff
};

bool DbgMiniDump::StartLoad(const StringImpl& path)
{
	if (!mMF.Open(path))
		return false;

	struct _Header
	{
		uint32 mSignature;
		uint32 mVersion;
		uint32 mNumberOfStreams;
		uint32 mStreamDirectoryRVA;
		uint32 mCheckSum;
		uint32 TimeDateStamp;
		uint64 Flags;
	};

	_Header& header = *(_Header*)((uint8*)mMF.mData);
	if (header.mSignature != 'PMDM')
		return false;

	mDirectory.mVals = (StreamDirectoryEntry*)((uint8*)mMF.mData + header.mStreamDirectoryRVA);
	mDirectory.mSize = header.mNumberOfStreams;

	return true;
}

int DbgMiniDump::GetTargetBitCount()
{
	for (auto& dirEntry : mDirectory)
	{
		if (dirEntry.mStreamType == DbgMiniDumpStreamType_SystemInfo)
		{
			MINIDUMP_SYSTEM_INFO& sysInfo = *(MINIDUMP_SYSTEM_INFO*)((uint8*)mMF.mData + dirEntry.mDataRVA);
			if ((sysInfo.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) || (sysInfo.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64))
				return 64;
			if (sysInfo.ProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
				return 32;
			return 0;
		}
	}

	return 0;
}