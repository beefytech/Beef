#include "ElfDbgModule.h"
#include "DebugTarget.h"
#include "DbgTargetHost.h"
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
	mTLSMemSize = 0;
	mTLSAlign = 1;
	mIsRelocatable = false;
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
	for (auto sectionData : mRelocatedSections)
		delete [] sectionData;
	// The host owns a hot object's memory, so the base destructor must not release it
	if (mIsRelocatable)
		mImageBase = 0;
}

void ElfDbgModule::SetHotLinkInfo(addr_target imageAddr, int64 imageSize, const Array<addr_target>& sectionAddrs, const Array<uint64>& symbolAddrs)
{
	mImageBase = imageAddr;
	mImageSize = (uint32)((imageSize + 0xFFF) & ~(int64)0xFFF);
	mHotSectionAddrs = sectionAddrs;
	mHotSymbolAddrs = symbolAddrs;
}

// Debug sections are relocated with the host linker's addresses, on private copies because the file is mapped read-only
bool ElfDbgModule::RelocateDebugSections(const uint8* data, const Elf64_Shdr* shdrs, int numSections)
{
	for (int relaIdx = 0; relaIdx < numSections; relaIdx++)
	{
		auto& relaShdr = shdrs[relaIdx];
		if (relaShdr.sh_type != SHT_RELA)
			continue;
		int targetIdx = (int)relaShdr.sh_info;
		if ((targetIdx <= 0) || (targetIdx >= numSections) || (strncmp(mElfSections[targetIdx].mName, ".debug_", 7) != 0))
			continue;
		ElfSection& target = mElfSections[targetIdx];
		if (target.mData == NULL)
			continue;
		if ((relaShdr.sh_link >= (uint32)numSections) || (shdrs[relaShdr.sh_link].sh_type != SHT_SYMTAB) ||
			(mElfSections[relaShdr.sh_link].mData == NULL) || (mElfSections[relaIdx].mData == NULL))
			return false;
		int symCount = (int)(mElfSections[relaShdr.sh_link].mSize / (int64)sizeof(Elf64_Sym));

		bool owned = false;
		for (auto sectionData : mRelocatedSections)
		{
			if (sectionData == target.mData)
				owned = true;
		}
		if (!owned)
		{
			uint8* copy = new uint8[target.mSize];
			memcpy(copy, target.mData, (size_t)target.mSize);
			mRelocatedSections.Add(copy);
			target.mData = copy;
		}
		uint8* targetData = (uint8*)target.mData;

		auto relas = (const Elf64_Rela*)mElfSections[relaIdx].mData;
		int relaCount = (int)(mElfSections[relaIdx].mSize / (int64)sizeof(Elf64_Rela));
		for (int entryIdx = 0; entryIdx < relaCount; entryIdx++)
		{
			auto& rela = relas[entryIdx];
			uint32 relocType = ELF64_R_TYPE(rela.r_info);
			int symIdx = (int)ELF64_R_SYM(rela.r_info);
			if (relocType == R_X86_64_NONE)
				continue;
			int relocSize = ((relocType == R_X86_64_64) || (relocType == R_X86_64_DTPOFF64)) ? 8 : 4;
			if ((symIdx >= symCount) || (symIdx >= (int)mHotSymbolAddrs.size()) || (!RangeInFile(rela.r_offset, relocSize, target.mSize)))
				return false;
			uint64 value = mHotSymbolAddrs[symIdx] + (uint64)rela.r_addend;
			switch (relocType)
			{
			case R_X86_64_64:
			case R_X86_64_DTPOFF64:
				memcpy(targetData + rela.r_offset, &value, 8);
				break;
			case R_X86_64_32:
			case R_X86_64_32S:
			case R_X86_64_DTPOFF32:
				{
					uint32 value32 = (uint32)value;
					memcpy(targetData + rela.r_offset, &value32, 4);
				}
				break;
			default:
				return false;
			}
		}
	}
	return true;
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

bool ElfDbgModule::ResolveTLSAddress(addr_target operand, CPURegisters* registers, addr_target& outAddr)
{
	if ((mTLSMemSize == 0) || (registers == NULL))
		return false;

	addr_target blockAddr = 0;
	if (this == mDebugTarget->mLaunchBinary)
	{
		// x86-64 variant II static TLS: the executable's block (module 1) ends at the thread pointer
		addr_target threadPointer = (addr_target)registers->mIntRegs.gs;
		if (threadPointer == 0)
			return false;
		blockAddr = threadPointer - ((mTLSMemSize + mTLSAlign - 1) & ~(mTLSAlign - 1));
	}
	else if ((mDebugger == NULL) || (!mDebugger->GetTLSBlockAddr(this, registers, blockAddr)))
		return false;

	outAddr = blockAddr + operand;
	return true;
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
	mIsRelocatable = (ehdr->e_type == ET_REL) && (moduleKind == DbgModuleKind_HotObject) && ((int)mHotSectionAddrs.size() == ehdr->e_shnum);
	if ((ehdr->e_type != ET_EXEC) && (ehdr->e_type != ET_DYN) && (!mIsRelocatable))
		return false;
	if (((!mIsRelocatable) && (ehdr->e_phentsize != sizeof(Elf64_Phdr))) || (ehdr->e_shentsize != sizeof(Elf64_Shdr)) || (ehdr->e_shnum == 0) ||
		((!mIsRelocatable) && (!TableInFile(ehdr->e_phoff, ehdr->e_phnum, sizeof(Elf64_Phdr), size))) ||
		(!TableInFile(ehdr->e_shoff, ehdr->e_shnum, sizeof(Elf64_Shdr), size)) ||
		(ehdr->e_shstrndx >= ehdr->e_shnum))
		return false;

	addr_target lowAddr = (addr_target)-1;
	addr_target highAddr = 0;
	auto phdrs = (const Elf64_Phdr*)(data + ehdr->e_phoff);
	for (int phdrIdx = 0; (!mIsRelocatable) && (phdrIdx < ehdr->e_phnum); phdrIdx++)
	{
		auto& phdr = phdrs[phdrIdx];
		if (phdr.p_type == PT_TLS)
		{
			addr_target tlsAlign = (phdr.p_align > 1) ? (addr_target)phdr.p_align : 1;
			if (((tlsAlign & (tlsAlign - 1)) == 0) && (phdr.p_memsz < 0x40000000))
			{
				mTLSMemSize = (addr_target)phdr.p_memsz;
				mTLSAlign = tlsAlign;
			}
			continue;
		}
		if (phdr.p_type != PT_LOAD)
			continue;
		addr_target align = (phdr.p_align > 1) ? (addr_target)phdr.p_align : 1;
		lowAddr = BF_MIN(lowAddr, (addr_target)phdr.p_vaddr & ~(align - 1));
		highAddr = BF_MAX(highAddr, (addr_target)(phdr.p_vaddr + phdr.p_memsz));
	}
	if (mIsRelocatable)
	{
		lowAddr = mImageBase;
		highAddr = mImageBase + mImageSize;
	}
	if (lowAddr > highAddr)
		return false;

	mIsPositionIndependent = ehdr->e_type == ET_DYN;
	mPreferredImageBase = lowAddr;
	if (mImageBase == 0)
		mImageBase = mPreferredImageBase;
	mImageSize = (uint32)(((highAddr - lowAddr) + 0xFFF) & ~(addr_target)0xFFF);
	mEntryPoint = ehdr->e_entry - lowAddr;

	for (int phdrIdx = 0; (!mIsRelocatable) && (phdrIdx < ehdr->e_phnum); phdrIdx++)
	{
		auto& phdr = phdrs[phdrIdx];
		if (phdr.p_type != PT_LOAD)
			continue;
		DbgSection dbgSection;
		dbgSection.mName = "LOAD";
		dbgSection.mAddrStart = (addr_target)phdr.p_vaddr - lowAddr;
		dbgSection.mAddrLength = (addr_target)phdr.p_memsz;
		dbgSection.mIsExecutable = (phdr.p_flags & PF_X) != 0;
		mSections.Add(dbgSection);
	}

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
		section.mAddress = mIsRelocatable ? mHotSectionAddrs[sectIdx] : (addr_target)shdr.sh_addr;
		section.mType = shdr.sh_type;
		section.mFlags = shdr.sh_flags;
		mElfSections.Add(section);

		if ((mIsRelocatable) && (section.mAddress != 0) && ((shdr.sh_flags & SHF_EXECINSTR) != 0))
		{
			DbgSection dbgSection;
			dbgSection.mName = "LOAD";
			dbgSection.mAddrStart = section.mAddress - mImageBase;
			dbgSection.mAddrLength = (addr_target)shdr.sh_size;
			dbgSection.mIsExecutable = true;
			mSections.Add(dbgSection);
		}
	}

	if ((mIsRelocatable) && (!RelocateDebugSections(data, shdrs, ehdr->e_shnum)))
	{
		DwarfFail("hot object debug info has a relocation that can't be applied");
		for (auto& section : mElfSections)
		{
			if (strncmp(section.mName, ".debug_", 7) == 0)
				section.mData = NULL;
		}
	}

	// A section without data (compressed, NOBITS or empty) reads as absent, so a non-NULL pointer always has a size
	auto _GetSectionData = [&](const char* name, int64* outSize)
	{
		const uint8* sectionData = NULL;
		int64 sectionSize = 0;
		auto section = FindSection(name);
		if ((section != NULL) && (section->mData != NULL) && (section->mSize > 0))
		{
			sectionData = section->mData;
			sectionSize = section->mSize;
		}
		if (outSize != NULL)
			*outSize = sectionSize;
		return sectionData;
	};

	mDebugInfoData = _GetSectionData(".debug_info", &mDebugInfoSize);
	mDebugAbbrevData = _GetSectionData(".debug_abbrev", &mDebugAbbrevSize);
	mDebugLineData = _GetSectionData(".debug_line", &mDebugLineSize);
	mDebugStrData = _GetSectionData(".debug_str", &mDebugStrSize);
	mDebugRangesData = _GetSectionData(".debug_ranges", &mDebugRangesSize);
	mDebugLocationData = _GetSectionData(".debug_loc", &mDebugLocationSize);
	mDebugFrameData = _GetSectionData(".debug_frame", &mDebugFrameSize);
	// A hot object's .eh_frame is not relocated, and LLDB unwinds hot code itself
	mEHFrameData = mIsRelocatable ? NULL : _GetSectionData(".eh_frame", &mEHFrameSize);
	if (mEHFrameData != NULL)
		mEHFrameAddress = RemapAddr(FindSection(".eh_frame")->mAddress);
	if ((mDebugInfoData == NULL) || (mDebugAbbrevData == NULL))
		mDebugInfoData = NULL;
	else
		mDebugAbbrevPtrData = new const uint8*[mDebugAbbrevSize]();

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

	if (mIsRelocatable)
		HotReplaceTypes();

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
		addr_target address = 0;
		if (mIsRelocatable)
		{
			// A hot object's data binds to the original storage, so only its code has addresses of its own
			if ((symType != STT_FUNC) || (sym.st_shndx >= (int)mHotSectionAddrs.size()) || (mHotSectionAddrs[sym.st_shndx] == 0))
				continue;
			address = mHotSectionAddrs[sym.st_shndx] + (addr_target)sym.st_value;
		}
		else
		{
			if ((sym.st_shndx == SHN_UNDEF) || (sym.st_value == 0))
				continue;
			address = RemapAddr((addr_target)sym.st_value);
		}
		const char* name = GetBoundedString(strTab->mData, strTab->mSize, sym.st_name);
		if (name == NULL)
			continue;

		DbgSymbol* dbgSymbol = mAlloc.Alloc<DbgSymbol>();
		dbgSymbol->mDbgModule = this;
		dbgSymbol->mName = name;
		dbgSymbol->mAddress = address;
		mDebugTarget->mSymbolMap.Insert(dbgSymbol);
		if ((ELF64_ST_BIND(sym.st_info) != STB_LOCAL) && (!mIsRelocatable))
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
