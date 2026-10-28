// Loads an ELF image through ElfDbgModule and prints what the Beef debug info layer sees, in a stable text form.
// Usage: DbgModuleCheck <elf> [--load-base 0xADDR] [--addr 0xADDR]... [--addr-file FILE] [--name NAME]...
// Exit codes: 0 loaded, 1 the image was rejected, 2 bad arguments.

#include "ElfDbgModule.h"
#include "DebugTarget.h"
#include "BeefySysLib/FileStream.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

USING_NS_BF_DBG;

static int CountUnits(ElfDbgModule* dbgModule, int wantVersion)
{
	auto section = dbgModule->FindSection(".debug_info");
	if ((section == NULL) || (section->mData == NULL))
		return 0;

	int count = 0;
	const uint8* data = section->mData;
	const uint8* dataEnd = data + section->mSize;
	while (dataEnd - data >= 16)
	{
		uint64 length = *(const uint32*)data;
		int headerSize = 4;
		if (length == 0xFFFFFFFF)
		{
			length = *(const uint64*)(data + 4);
			headerSize = 12;
		}
		int version = *(const uint16*)(data + headerSize);
		if ((version == wantVersion) || ((wantVersion >= 5) && (version >= 5)))
			count++;
		if (length > (uint64)(dataEnd - data - headerSize))
			break;
		data += headerSize + length;
	}
	return count;
}

static void PrintAddress(DebugTarget* debugTarget, addr_target addr)
{
	DbgSubprogram* subprogram = debugTarget->FindSubProgram(addr, DbgOnDemandKind_None);
	if (subprogram == NULL)
	{
		printf("addr 0x%llx <none>\n", (unsigned long long)addr);
		return;
	}

	DbgModule::FixupLineDataForSubprogram(subprogram);
	DbgSrcFile* srcFile = NULL;
	DbgLineData* lineData = subprogram->FindClosestLine(addr, NULL, &srcFile);
	if ((lineData == NULL) || (srcFile == NULL))
	{
		printf("addr 0x%llx %s ?:0\n", (unsigned long long)addr, subprogram->mName);
		return;
	}
	printf("addr 0x%llx %s %s:%d\n", (unsigned long long)addr, subprogram->mName, srcFile->mFilePath.c_str(), lineData->mLine + 1);
}

static void PrintName(DbgModule* dbgModule, const char* name)
{
	Array<addr_target> addrs;
	for (auto subprogram : dbgModule->mSubprograms)
	{
		if ((subprogram->mInlineeInfo != NULL) || (subprogram->mBlock.mLowPC == 0) || (subprogram->mName == NULL))
			continue;
		if (strcmp(subprogram->mName, name) == 0)
			addrs.Add(subprogram->mBlock.mLowPC);
	}
	std::sort(addrs.begin(), addrs.end());
	printf("name %s", name);
	for (auto addr : addrs)
		printf(" 0x%llx", (unsigned long long)addr);
	printf("\n");
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: DbgModuleCheck <elf> [--load-base 0xADDR] [--addr 0xADDR]... [--addr-file FILE] [--name NAME]...\n");
		return 2;
	}

	DebugTarget* debugTarget = new DebugTarget(NULL);
	ElfDbgModule* dbgModule = new ElfDbgModule(debugTarget);
	dbgModule->mFilePath = argv[1];
	dbgModule->mDisplayName = GetFileName(dbgModule->mFilePath);
	bool hasLoadBase = (argc > 3) && (strcmp(argv[2], "--load-base") == 0);
	if (hasLoadBase)
		dbgModule->mImageBase = (uintptr)strtoull(argv[3], NULL, 0);

	FileStream fileStream;
	if ((!fileStream.Open(dbgModule->mFilePath, "rb")) || (!dbgModule->ReadImage(&fileStream, DbgModuleKind_Module)))
	{
		fprintf(stderr, "failed to read %s\n", argv[1]);
		return 1;
	}
	debugTarget->AddDbgModule(dbgModule);
	debugTarget->mTargetBinary = dbgModule;
	debugTarget->mLaunchBinary = dbgModule;

	int definedCount = 0;
	int definedWithLinesCount = 0;
	int inlinedCount = 0;
	for (auto subprogram : dbgModule->mSubprograms)
	{
		if (subprogram->mInlineeInfo != NULL)
		{
			inlinedCount++;
			continue;
		}
		if (subprogram->mBlock.mLowPC == 0)
			continue;
		definedCount++;
		if ((subprogram->mLineInfo != NULL) && (subprogram->mLineInfo->mLines.mSize > 0))
			definedWithLinesCount++;
	}

	printf("module %s\n", dbgModule->mDisplayName.c_str());
	printf("image_base 0x%llx\n", (unsigned long long)dbgModule->mImageBase);
	printf("dwarf4_units %d\n", (int)dbgModule->mCompileUnits.size() - 1);
	printf("dwarf4_units_in_file %d\n", CountUnits(dbgModule, 4));
	printf("dwarf5_units_skipped %d\n", CountUnits(dbgModule, 5));
	printf("types %d\n", (int)dbgModule->mTypes.size());
	printf("subprograms %d\n", (int)dbgModule->mSubprograms.size());
	printf("subprograms_inlined %d\n", inlinedCount);
	printf("subprograms_defined %d\n", definedCount);
	printf("subprograms_defined_with_lines %d\n", definedWithLinesCount);
	printf("src_files %d\n", (int)debugTarget->mSrcFiles.size());
	printf("global_symbols %d\n", dbgModule->mSymbolNameMap.mCount);

	for (int argIdx = hasLoadBase ? 4 : 2; argIdx < argc; argIdx++)
	{
		String arg = argv[argIdx];
		if ((arg == "--addr") && (argIdx + 1 < argc))
		{
			PrintAddress(debugTarget, (addr_target)strtoull(argv[++argIdx], NULL, 0));
		}
		else if ((arg == "--name") && (argIdx + 1 < argc))
		{
			PrintName(dbgModule, argv[++argIdx]);
		}
		else if ((arg == "--addr-file") && (argIdx + 1 < argc))
		{
			FILE* fp = fopen(argv[++argIdx], "r");
			if (fp == NULL)
			{
				fprintf(stderr, "failed to open %s\n", argv[argIdx]);
				return 1;
			}
			char line[256];
			while (fgets(line, sizeof(line), fp) != NULL)
			{
				if ((line[0] == '\n') || (line[0] == 0))
					continue;
				PrintAddress(debugTarget, (addr_target)strtoull(line, NULL, 0));
			}
			fclose(fp);
		}
		else
		{
			fprintf(stderr, "unknown argument %s\n", argv[argIdx]);
			return 2;
		}
	}

	return 0;
}
