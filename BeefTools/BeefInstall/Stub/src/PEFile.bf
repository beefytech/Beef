using System;

namespace System.Windows
{
	class PEFile
	{
		public const uint16 PE_MACHINE_X86 = 0x14c;
		public const uint16 PE_MACHINE_X64 = 0x8664;

		[CRepr]
		public struct PEHeader
		{
			public uint16   e_magic;                     // Magic number
			public uint16   e_cblp;                      // uint8s on last page of file
			public uint16   e_cp;                        // Pages in file
			public uint16   e_crlc;                      // Relocations
			public uint16   e_cparhdr;                   // Size of header in paragraphs
			public uint16   e_minalloc;                  // Minimum extra paragraphs needed
			public uint16   e_maxalloc;                  // Maximum extra paragraphs needed
			public uint16   e_ss;                        // Initial (relative) SS value
			public uint16   e_sp;                        // Initial SP value
			public uint16   e_csum;                      // Checksum
			public uint16   e_ip;                        // Initial IP value
			public uint16   e_cs;                        // Initial (relative) CS value
			public uint16   e_lfarlc;                    // File address of relocation table
			public uint16   e_ovno;                      // Overlay number
			public uint16[4]   e_res;                    // Reserved uint16s
			public uint16   e_oemid;                     // OEM identifier (for e_oeminfo)
			public uint16   e_oeminfo;                   // OEM information; e_oemid specific
			public uint16[10]   e_res2;                  // Reserved uint16s
			public int32   e_lfanew;                    // File address of new exe header
		};


		[CRepr]
		public struct PEFileHeader
		{
			public uint16    mMachine;
			public uint16    mNumberOfSections;
			public uint32   mTimeDateStamp;
			public uint32   mPointerToSymbolTable;
			public uint32   mNumberOfSymbols;
			public uint16    mSizeOfOptionalHeader;
			public uint16    mCharacteristics;
		};

		[CRepr]
		public struct PEImportObjectHeader
		{
			public uint16    mSig1;
			public uint16    mSig2;
			public uint16    mVersion;
			public uint16    mMachine;
			public uint32   mTimeDateStamp;
			public uint32   mDataSize;
			public uint16    mHint;
			public uint16    mType;
		};

		[CRepr]
		public struct PEDataDirectory
		{
			public uint32   mVirtualAddress;
			public uint32   mSize;
		};

		[CRepr]
		public struct PEOptionalHeader32
		{
			//
			// Standard fields.
			//

			public uint16    mMagic;
			public uint8    mMajorLinkerVersion;
			public uint8    mMinorLinkerVersion;
			public uint32   mSizeOfCode;
			public uint32   mSizeOfInitializedData;
			public uint32   mSizeOfUninitializedData;
			public uint32   mAddressOfEntryPoint;
			public uint32   mBaseOfCode;
			public uint32   mBaseOfData;

			//
			// NT additional fields.
			//

			public uint32   mImageBase;
			public uint32   mSectionAlignment;
			public uint32   mFileAlignment;
			public uint16    mMajorOperatingSystemVersion;
			public uint16    mMinorOperatingSystemVersion;
			public uint16    mMajorImageVersion;
			public uint16    mMinorImageVersion;
			public uint16    mMajorSubsystemVersion;
			public uint16    mMinorSubsystemVersion;
			public uint32   mReserved1;
			public uint32   mSizeOfImage;
			public uint32   mSizeOfHeaders;
			public uint32   mCheckSum;
			public uint16    mSubsystem;
			public uint16    mDllCharacteristics;
			public uint32   mSizeOfStackReserve;
			public uint32   mSizeOfStackCommit;
			public uint32   mSizeOfHeapReserve;
			public uint32   mSizeOfHeapCommit;
			public uint32   mLoaderFlags;
			public uint32   mNumberOfRvaAndSizes;
			public PEDataDirectory[16] mDataDirectory;
		};

		[CRepr]
		public struct PEOptionalHeader64
		{
			//
			// Standard fields.
			//

			public uint16    mMagic;
			public uint8    mMajorLinkerVersion;
			public uint8    mMinorLinkerVersion;
			public uint32   mSizeOfCode;
			public uint32   mSizeOfInitializedData;
			public uint32   mSizeOfUninitializedData;
			public uint32   mAddressOfEntryPoint;
			public uint32   mBaseOfCode;

			//
			// NT additional fields.
			//

			public uint64	mImageBase;
			public uint32   mSectionAlignment;
			public uint32   mFileAlignment;
			public uint16    mMajorOperatingSystemVersion;
			public uint16    mMinorOperatingSystemVersion;
			public uint16    mMajorImageVersion;
			public uint16    mMinorImageVersion;
			public uint16    mMajorSubsystemVersion;
			public uint16    mMinorSubsystemVersion;
			public uint32   mReserved1;
			public uint32   mSizeOfImage;
			public uint32   mSizeOfHeaders;
			public uint32   mCheckSum;
			public uint16    mSubsystem;
			public uint16    mDllCharacteristics;
			public uint64  mSizeOfStackReserve;
			public uint64  mSizeOfStackCommit;
			public uint64  mSizeOfHeapReserve;
			public uint64  mSizeOfHeapCommit;
			public uint32   mLoaderFlags;
			public uint32   mNumberOfRvaAndSizes;
			public PEDataDirectory[16] mDataDirectory;
		};

		[CRepr]
		struct PE_NTHeaders32
		{
			uint32 mSignature;
			PEFileHeader mFileHeader;
			PEOptionalHeader32 mOptionalHeader;
		};

		[CRepr]
		public struct PE_NTHeaders64
		{
			public uint32 mSignature;
			public PEFileHeader mFileHeader;
			public PEOptionalHeader64 mOptionalHeader;
		};

		const int IMAGE_SIZEOF_SHORT_NAME              = 8;

		[CRepr]
		public struct PESectionHeader
		{
			public char8[IMAGE_SIZEOF_SHORT_NAME]    mName;	
			public uint32   mVirtualSize;	
			public uint32   mVirtualAddress;
			public uint32   mSizeOfRawData;
			public uint32   mPointerToRawData;
			public uint32   mPointerToRelocations;
			public uint32   mPointerToLineNumbers;
			public uint16    mNumberOfRelocations;
			public uint16    mNumberOfLineNumbers;
			public uint32   mCharacteristics;
		};

		[CRepr]
		struct COFFRelocation
		{
			uint32 mVirtualAddress;
			uint32 mSymbolTableIndex;
			uint16 mType;
		};

		[CRepr]
		struct PE_SymInfo
		{
			[Union]
			public struct Name
			{
				public char8[8] mName;
				public int32[2] mNameOfs;
			}

			public Name mName;

			/*union
			{
				char mName[8];
				int32 mNameOfs[2];
			};*/

			public int32 mValue;
			public uint16 mSectionNum;
			public uint16 mType;
			public int8 mStorageClass;
			public int8 mNumOfAuxSymbols;
		};

		[CRepr]
		struct PE_SymInfoAux
		{
			public uint32 mLength;
			public uint16 mNumberOfRelocations;
			public uint16 mNumberOfLinenumbers;
			public uint32 mCheckSum;
			public uint16 mNumber;
			public uint8 mSelection;
			public char8 mUnused;
			public char8 mUnused2;
			public char8 mUnused3;
		};
	}
}
