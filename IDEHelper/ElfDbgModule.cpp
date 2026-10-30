#include "ElfDbgModule.h"
#include "DebugTarget.h"
#include "BeefySysLib/util/BeefPerf.h"
#include <elf.h>

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

static bool RangeInFile(uint64 offset, uint64 length, int64 size)
{
	return (offset <= (uint64)size) && (length <= (uint64)size - offset);
}

static bool TableInFile(uint64 offset, uint64 count, uint64 entrySize, int64 size)
{
	return (offset <= (uint64)size) && (count <= ((uint64)size - offset) / entrySize);
}

static const char* GetBoundedString(const uint8* table, int64 tableSize, uint64 offset)
{
	if ((table == NULL) || (tableSize <= 0) || (offset >= (uint64)tableSize))
		return NULL;
	if (memchr(table + offset, 0, (size_t)((uint64)tableSize - offset)) == NULL)
		return NULL;
	return (const char*)(table + offset);
}

ElfDbgModule::ElfDbgModule(DebugTarget* debugTarget) : DbgModule(debugTarget)
{
	mIsPositionIndependent = false;
	mEHFrameSize = 0;
	mTLSDataSize = 0;
	mTLSBssSize = 0;
}

ElfDbgModule::~ElfDbgModule()
{
	// Section pointers alias the file data, so the base destructor must not free them
	mDebugLineData = NULL;
	mDebugInfoData = NULL;
	mDebugFrameData = NULL;
	mDebugLocationData = NULL;
	mDebugRangesData = NULL;
	mDebugAbbrevData = NULL;
	mDebugStrData = NULL;
	mEHFrameData = NULL;
	delete [] mDebugAbbrevPtrData;
	mDebugAbbrevPtrData = NULL;
}

const ElfSection* ElfDbgModule::FindSection(const char* name)
{
	for (auto& section : mElfSections)
	{
		if (strcmp(section.mName, name) == 0)
			return &section;
	}
	return NULL;
}

addr_target ElfDbgModule::RemapAddr(addr_target addr)
{
	if (addr == 0)
		return 0;
	return addr + (addr_target)(mImageBase - mPreferredImageBase);
}

bool ElfDbgModule::ReadImage(DataStream* stream, DbgModuleKind moduleKind)
{
	if ((!mFilePath.IsEmpty()) && (mMappedFile.Open(mFilePath)))
		return ReadELF((const uint8*)mMappedFile.mData, mMappedFile.mFileSize, moduleKind);

	int size = stream->GetSize();
	if (size <= 0)
	{
		mLoadState = DbgModuleLoadState_Failed;
		return false;
	}
	mFileData.Resize(size);
	stream->SetPos(0);
	if (stream->Read(mFileData.mVals, size) != size)
	{
		mLoadState = DbgModuleLoadState_Failed;
		return false;
	}
	return ReadELF(mFileData.mVals, size, moduleKind);
}

bool ElfDbgModule::ReadELF(const uint8* data, int64 size, DbgModuleKind moduleKind)
{
	BP_ZONE("ElfDbgModule::ReadELF");

	mModuleKind = moduleKind;
	mLoadState = DbgModuleLoadState_Failed;

	if ((size < (int64)sizeof(Elf64_Ehdr)) || (memcmp(data, ELFMAG, SELFMAG) != 0))
		return false;
	auto ehdr = (const Elf64_Ehdr*)data;
	if ((ehdr->e_ident[EI_CLASS] != ELFCLASS64) || (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) || (ehdr->e_machine != EM_X86_64))
		return false;
	if ((ehdr->e_type != ET_EXEC) && (ehdr->e_type != ET_DYN))
		return false;
	if ((ehdr->e_phentsize != sizeof(Elf64_Phdr)) || (ehdr->e_shentsize != sizeof(Elf64_Shdr)) || (ehdr->e_shnum == 0) ||
		(!TableInFile(ehdr->e_phoff, ehdr->e_phnum, sizeof(Elf64_Phdr), size)) ||
		(!TableInFile(ehdr->e_shoff, ehdr->e_shnum, sizeof(Elf64_Shdr), size)) ||
		(ehdr->e_shstrndx >= ehdr->e_shnum))
		return false;

	addr_target lowAddr = (addr_target)-1;
	addr_target highAddr = 0;
	auto phdrs = (const Elf64_Phdr*)(data + ehdr->e_phoff);
	for (int phdrIdx = 0; phdrIdx < ehdr->e_phnum; phdrIdx++)
	{
		auto& phdr = phdrs[phdrIdx];
		if (phdr.p_type != PT_LOAD)
			continue;
		addr_target align = (phdr.p_align > 1) ? (addr_target)phdr.p_align : 1;
		lowAddr = BF_MIN(lowAddr, (addr_target)phdr.p_vaddr & ~(align - 1));
		highAddr = BF_MAX(highAddr, (addr_target)(phdr.p_vaddr + phdr.p_memsz));
	}
	if (lowAddr > highAddr)
		return false;

	mIsPositionIndependent = ehdr->e_type == ET_DYN;
	mPreferredImageBase = lowAddr;
	if (mImageBase == 0)
		mImageBase = mPreferredImageBase;
	mImageSize = (uint32)(highAddr - lowAddr);
	mEntryPoint = ehdr->e_entry - lowAddr;

	auto shdrs = (const Elf64_Shdr*)(data + ehdr->e_shoff);
	auto& shStrHdr = shdrs[ehdr->e_shstrndx];
	if (!RangeInFile(shStrHdr.sh_offset, shStrHdr.sh_size, size))
		return false;
	const uint8* shStrTab = data + shStrHdr.sh_offset;
	for (int sectIdx = 0; sectIdx < ehdr->e_shnum; sectIdx++)
	{
		auto& shdr = shdrs[sectIdx];
		ElfSection section;
		const char* name = GetBoundedString(shStrTab, (int64)shStrHdr.sh_size, shdr.sh_name);
		section.mName = (name != NULL) ? name : "";
		section.mData = NULL;
		// Compressed sections would need inflating first; treat them as absent
		if ((shdr.sh_type != SHT_NOBITS) && ((shdr.sh_flags & SHF_COMPRESSED) == 0) && (RangeInFile(shdr.sh_offset, shdr.sh_size, size)))
			section.mData = data + shdr.sh_offset;
		section.mSize = (int64)shdr.sh_size;
		section.mAddress = (addr_target)shdr.sh_addr;
		section.mType = shdr.sh_type;
		section.mFlags = shdr.sh_flags;
		mElfSections.Add(section);
	}

	auto _GetSectionData = [&](const char* name, int64* outSize)
	{
		const uint8* sectionData = NULL;
		int64 sectionSize = 0;
		auto section = FindSection(name);
		if (section != NULL)
		{
			sectionData = section->mData;
			sectionSize = section->mSize;
		}
		if (outSize != NULL)
			*outSize = sectionSize;
		return sectionData;
	};

	int64 abbrevSize = 0;
	mDebugInfoData = _GetSectionData(".debug_info", &mDebugInfoSize);
	mDebugAbbrevData = _GetSectionData(".debug_abbrev", &abbrevSize);
	mDebugLineData = _GetSectionData(".debug_line", NULL);
	mDebugStrData = _GetSectionData(".debug_str", NULL);
	mDebugRangesData = _GetSectionData(".debug_ranges", NULL);
	mDebugLocationData = _GetSectionData(".debug_loc", NULL);
	mDebugFrameData = _GetSectionData(".debug_frame", NULL);
	mEHFrameData = _GetSectionData(".eh_frame", &mEHFrameSize);
	if (mEHFrameData != NULL)
		mEHFrameAddress = RemapAddr(FindSection(".eh_frame")->mAddress);
	if ((mDebugInfoData == NULL) || (mDebugAbbrevData == NULL))
		mDebugInfoData = NULL;
	else
		mDebugAbbrevPtrData = new const uint8*[abbrevSize]();

	if (auto section = FindSection(".tdata"))
		mTLSDataSize = section->mSize;
	if (auto section = FindSection(".tbss"))
		mTLSBssSize = section->mSize;

	auto linkedModule = GetLinkedModule();
	mStartTypeIdx = (int)linkedModule->mTypes.size();
	mStartSubprogramIdx = (int)mSubprograms.size();

	ParseDWARFUnits();

	mEndTypeIdx = (int)linkedModule->mTypes.size();
	mEndSubprogramIdx = (int)mSubprograms.size();

	ParseDWARFLines();

	auto symTab = FindSection(".symtab");
	if (symTab != NULL)
		ReadSymbols(symTab, FindSection(".strtab"));
	else
		ReadSymbols(FindSection(".dynsym"), FindSection(".dynstr"));

	RemoveUnusedSrcFiles();

	mLoadState = DbgModuleLoadState_Loaded;
	return true;
}

void ElfDbgModule::ReadSymbols(const ElfSection* symTab, const ElfSection* strTab)
{
	if ((symTab == NULL) || (symTab->mData == NULL) || (strTab == NULL) || (strTab->mData == NULL))
		return;
	if (((symTab->mType != SHT_SYMTAB) && (symTab->mType != SHT_DYNSYM)) || (strTab->mType != SHT_STRTAB))
		return;

	auto linkedModule = GetLinkedModule();
	auto syms = (const Elf64_Sym*)symTab->mData;
	int symCount = (int)(symTab->mSize / (int64)sizeof(Elf64_Sym));
	for (int symIdx = 1; symIdx < symCount; symIdx++)
	{
		auto& sym = syms[symIdx];
		int symType = ELF64_ST_TYPE(sym.st_info);
		if ((symType != STT_FUNC) && (symType != STT_OBJECT))
			continue;
		if ((sym.st_shndx == SHN_UNDEF) || (sym.st_value == 0))
			continue;
		const char* name = GetBoundedString(strTab->mData, strTab->mSize, sym.st_name);
		if (name == NULL)
			continue;

		DbgSymbol* dbgSymbol = mAlloc.Alloc<DbgSymbol>();
		dbgSymbol->mDbgModule = this;
		dbgSymbol->mName = name;
		dbgSymbol->mAddress = RemapAddr((addr_target)sym.st_value);
		mDebugTarget->mSymbolMap.Insert(dbgSymbol);
		if (ELF64_ST_BIND(sym.st_info) != STB_LOCAL)
			linkedModule->mSymbolNameMap.Insert(dbgSymbol);
	}
}

void ElfDbgModule::RemoveUnusedSrcFiles()
{
	auto srcFilesItr = mDebugTarget->mSrcFiles.begin();
	while (srcFilesItr != mDebugTarget->mSrcFiles.end())
	{
		DbgSrcFile* srcFile = srcFilesItr->mValue;
		if ((!srcFile->mHadLineData) && (srcFile->mLocalPath.IsEmpty()))
		{
			mEmptySrcFiles.push_back(srcFile);
			srcFilesItr = mDebugTarget->mSrcFiles.Remove(srcFilesItr);
		}
		else
		{
			++srcFilesItr;
		}
	}
}
