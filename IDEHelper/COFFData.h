#pragma once

#include "BeefySysLib/Common.h"

struct CV_LVAR_ADDR_RANGE;
struct CV_LVAR_ADDR_GAP;

#ifndef BF_PLATFORM_WINDOWS
#define IMAGE_SIZEOF_SHORT_NAME 8

#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_DEBUG 6
#define IMAGE_DIRECTORY_ENTRY_TLS 9
#define IMAGE_DEBUG_TYPE_CODEVIEW 2

#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

#define IMAGE_REL_AMD64_ADDR64 0x0001
#define IMAGE_REL_AMD64_ADDR32 0x0002
#define IMAGE_REL_AMD64_ADDR32NB 0x0003
#define IMAGE_REL_AMD64_REL32 0x0004
#define IMAGE_REL_AMD64_REL32_5 0x0009
#define IMAGE_REL_AMD64_SECTION 0x000A
#define IMAGE_REL_AMD64_SECREL 0x000B

#define FIELD_OFFSET(type, field) ((int32)offsetof(type, field))

struct IMAGE_EXPORT_DIRECTORY
{
	uint32 Characteristics;
	uint32 TimeDateStamp;
	uint16 MajorVersion;
	uint16 MinorVersion;
	uint32 Name;
	uint32 Base;
	uint32 NumberOfFunctions;
	uint32 NumberOfNames;
	uint32 AddressOfFunctions;
	uint32 AddressOfNames;
	uint32 AddressOfNameOrdinals;
};

struct IMAGE_DEBUG_DIRECTORY
{
	uint32 Characteristics;
	uint32 TimeDateStamp;
	uint16 MajorVersion;
	uint16 MinorVersion;
	uint32 Type;
	uint32 SizeOfData;
	uint32 AddressOfRawData;
	uint32 PointerToRawData;
};

struct IMAGE_RESOURCE_DIRECTORY
{
	uint32 Characteristics;
	uint32 TimeDateStamp;
	uint16 MajorVersion;
	uint16 MinorVersion;
	uint16 NumberOfNamedEntries;
	uint16 NumberOfIdEntries;
};

struct IMAGE_RESOURCE_DIRECTORY_ENTRY
{
	union
	{
		uint32 Name;
		uint16 Id;
	};
	uint32 OffsetToData;
};

struct IMAGE_RESOURCE_DATA_ENTRY
{
	uint32 OffsetToData;
	uint32 Size;
	uint32 CodePage;
	uint32 Reserved;
};

struct VS_FIXEDFILEINFO
{
	uint32 dwSignature;
	uint32 dwStrucVersion;
	uint32 dwFileVersionMS;
	uint32 dwFileVersionLS;
	uint32 dwProductVersionMS;
	uint32 dwProductVersionLS;
	uint32 dwFileFlagsMask;
	uint32 dwFileFlags;
	uint32 dwFileOS;
	uint32 dwFileType;
	uint32 dwFileSubtype;
	uint32 dwFileDateMS;
	uint32 dwFileDateLS;
};
#endif

NS_BF_BEGIN

#define PE_SIZEOF_SHORT_NAME              8
#define PE_DIRECTORY_ENTRY_EXPORT         0   // Export Directory
#define PE_NUMBEROF_DIRECTORY_ENTRIES    16
#define PE_NT_SIGNATURE                  0x00004550  // PE00
#define PE_DOS_SIGNATURE                 0x5A4D      // MZ

#define PE_MACHINE_X86 0x14c
#define PE_MACHINE_X64 0x8664

// DOS .EXE header
struct PEHeader
{
	WORD   e_magic;                     // Magic number
	WORD   e_cblp;                      // Bytes on last page of file
	WORD   e_cp;                        // Pages in file
	WORD   e_crlc;                      // Relocations
	WORD   e_cparhdr;                   // Size of header in paragraphs
	WORD   e_minalloc;                  // Minimum extra paragraphs needed
	WORD   e_maxalloc;                  // Maximum extra paragraphs needed
	WORD   e_ss;                        // Initial (relative) SS value
	WORD   e_sp;                        // Initial SP value
	WORD   e_csum;                      // Checksum
	WORD   e_ip;                        // Initial IP value
	WORD   e_cs;                        // Initial (relative) CS value
	WORD   e_lfarlc;                    // File address of relocation table
	WORD   e_ovno;                      // Overlay number
	WORD   e_res[4];                    // Reserved words
	WORD   e_oemid;                     // OEM identifier (for e_oeminfo)
	WORD   e_oeminfo;                   // OEM information; e_oemid specific
	WORD   e_res2[10];                  // Reserved words
	LONG   e_lfanew;                    // File address of new exe header
};

struct PEFileHeader
{
	WORD    mMachine;
	WORD    mNumberOfSections;
	DWORD   mTimeDateStamp;
	DWORD   mPointerToSymbolTable;
	DWORD   mNumberOfSymbols;
	WORD    mSizeOfOptionalHeader;
	WORD    mCharacteristics;
};

struct PEImportObjectHeader
{
	WORD    mSig1;
	WORD    mSig2;
	WORD    mVersion;
	WORD    mMachine;
	DWORD   mTimeDateStamp;
	DWORD   mDataSize;
	WORD    mHint;
	WORD    mType;
};

struct PEDataDirectory
{
	DWORD   mVirtualAddress;
	DWORD   mSize;
};

struct PEOptionalHeader32
{
	//
	// Standard fields.
	//

	WORD    mMagic;
	BYTE    mMajorLinkerVersion;
	BYTE    mMinorLinkerVersion;
	DWORD   mSizeOfCode;
	DWORD   mSizeOfInitializedData;
	DWORD   mSizeOfUninitializedData;
	DWORD   mAddressOfEntryPoint;
	DWORD   mBaseOfCode;
	DWORD   mBaseOfData;

	//
	// NT additional fields.
	//

	DWORD   mImageBase;
	DWORD   mSectionAlignment;
	DWORD   mFileAlignment;
	WORD    mMajorOperatingSystemVersion;
	WORD    mMinorOperatingSystemVersion;
	WORD    mMajorImageVersion;
	WORD    mMinorImageVersion;
	WORD    mMajorSubsystemVersion;
	WORD    mMinorSubsystemVersion;
	DWORD   mReserved1;
	DWORD   mSizeOfImage;
	DWORD   mSizeOfHeaders;
	DWORD   mCheckSum;
	WORD    mSubsystem;
	WORD    mDllCharacteristics;
	DWORD   mSizeOfStackReserve;
	DWORD   mSizeOfStackCommit;
	DWORD   mSizeOfHeapReserve;
	DWORD   mSizeOfHeapCommit;
	DWORD   mLoaderFlags;
	DWORD   mNumberOfRvaAndSizes;
	PEDataDirectory mDataDirectory[16];
};

struct PEOptionalHeader64
{
	//
	// Standard fields.
	//

	WORD    mMagic;
	BYTE    mMajorLinkerVersion;
	BYTE    mMinorLinkerVersion;
	DWORD   mSizeOfCode;
	DWORD   mSizeOfInitializedData;
	DWORD   mSizeOfUninitializedData;
	DWORD   mAddressOfEntryPoint;
	DWORD   mBaseOfCode;

	//
	// NT additional fields.
	//

	uint64	mImageBase;
	DWORD   mSectionAlignment;
	DWORD   mFileAlignment;
	WORD    mMajorOperatingSystemVersion;
	WORD    mMinorOperatingSystemVersion;
	WORD    mMajorImageVersion;
	WORD    mMinorImageVersion;
	WORD    mMajorSubsystemVersion;
	WORD    mMinorSubsystemVersion;
	DWORD   mReserved1;
	DWORD   mSizeOfImage;
	DWORD   mSizeOfHeaders;
	DWORD   mCheckSum;
	WORD    mSubsystem;
	WORD    mDllCharacteristics;
	uint64  mSizeOfStackReserve;
	uint64  mSizeOfStackCommit;
	uint64  mSizeOfHeapReserve;
	uint64  mSizeOfHeapCommit;
	DWORD   mLoaderFlags;
	DWORD   mNumberOfRvaAndSizes;
	PEDataDirectory mDataDirectory[16];
};

struct PE_NTHeaders32
{
	DWORD mSignature;
	PEFileHeader mFileHeader;
	PEOptionalHeader32 mOptionalHeader;
};

struct PE_NTHeaders64
{
	DWORD mSignature;
	PEFileHeader mFileHeader;
	PEOptionalHeader64 mOptionalHeader;
};

struct PESectionHeader
{
	char    mName[IMAGE_SIZEOF_SHORT_NAME];
	DWORD   mVirtualSize;
	DWORD   mVirtualAddress;
	DWORD   mSizeOfRawData;
	DWORD   mPointerToRawData;
	DWORD   mPointerToRelocations;
	DWORD   mPointerToLineNumbers;
	WORD    mNumberOfRelocations;
	WORD    mNumberOfLineNumbers;
	DWORD   mCharacteristics;
};

#pragma pack(push, 1)
struct COFFRelocation
{
	uint32 mVirtualAddress;
	uint32 mSymbolTableIndex;
	uint16 mType;
};

struct PE_SymInfo
{
	union
	{
		char mName[8];
		int32 mNameOfs[2];
	};

	int mValue;
	uint16 mSectionNum;
	uint16 mType;
	int8 mStorageClass;
	int8 mNumOfAuxSymbols;
};

struct PE_SymInfoAux
{
	uint32 mLength;
	uint16 mNumberOfRelocations;
	uint16 mNumberOfLinenumbers;
	uint32 mCheckSum;
	uint16 mNumber;
	uint8 mSelection;
	char mUnused;
	char mUnused2;
	char mUnused3;
};

//struct COFFFrameDescriptor
//{
//	int32 mOffset; // Offset 1st byte of function code
//	int32 mSize;   // bytes in function
//	int32 mNumLocals; // # bytes in locals/4
//	int16 mNumParams; // # bytes in params/4
//	int16 mAttributes;
//
//	// # bytes in prolog
//	int GetPrologSize() const { return mAttributes & 0xF; }
//
//	// # regs saved
//	int GetNumSavedRegs() const { return (mAttributes >> 8) & 0x7; }
//	bool HasSEH() const { return (mAttributes >> 9) & 1; }
//	bool UseBP() const { return (mAttributes >> 10) & 1; }
//
//	// cbFrame: frame pointer
//	int GetFP() const { return mAttributes >> 14; }
//};

struct COFFFrameDescriptor
{
	uint32 mRvaStart;
	uint32 mCodeSize;
	uint32 mLocalSize;
	uint32 mParamsSize;
	uint32 mMaxStackSize;
	uint32 mFrameFunc;
	uint16 mPrologSize;
	uint16 mSavedRegsSize;
	uint32 mFlags;
};

struct COFFFrameProgram
{
	enum Command : uint8
	{
		Command_None,
		Command_EIP,
		Command_ESP,
		Command_EBP,
		Command_EAX,
		Command_EBX,
		Command_ECX,
		Command_EDX,
		Command_ESI,
		Command_EDI,
		Command_T0,
		Command_T1,
		Command_T2,
		Command_T3,
		Command_RASearch,
		Command_Add,
		Command_Subtract,
		Command_Align,
		Command_Set,
		Command_Deref,
		Command_Value,
		Command_Value8
	};

	Command* mCommands;
};

struct COFFFrameDescriptorEntry
{
	COFFFrameDescriptor* mFrameDescriptor;
	COFFFrameProgram mProgram;
};

#pragma pack(pop)

NS_BF_END
