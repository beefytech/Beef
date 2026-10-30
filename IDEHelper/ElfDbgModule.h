#pragma once

#include "DbgModule.h"
#include "BeefySysLib/util/MappedFile.h"

NS_BF_DBG_BEGIN

struct ElfSection
{
	const char* mName;
	const uint8* mData;
	int64 mSize;
	addr_target mAddress;
	uint32 mType;
	uint64 mFlags;
};

class ElfDbgModule : public DbgModule
{
public:
	MappedFile mMappedFile;
	Array<uint8> mFileData;
	Array<ElfSection> mElfSections;
	bool mIsPositionIndependent;
	int64 mEHFrameSize;
	int64 mTLSDataSize;
	int64 mTLSBssSize;

public:
	ElfDbgModule(DebugTarget* debugTarget);
	~ElfDbgModule();

	const ElfSection* FindSection(const char* name);
	virtual addr_target RemapAddr(addr_target addr) override;
	virtual bool ReadImage(DataStream* stream, DbgModuleKind moduleKind) override;
	bool ReadELF(const uint8* data, int64 size, DbgModuleKind moduleKind);

protected:
	void ReadSymbols(const ElfSection* symTab, const ElfSection* strTab);
	void RemoveUnusedSrcFiles();
};

NS_BF_DBG_END
