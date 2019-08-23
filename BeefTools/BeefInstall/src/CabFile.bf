using System;
using System.IO;
namespace BeefInstall
{
	class CabFile
	{
		struct HFDI : int
		{

		}

		struct HFile : int
		{
		}

		enum FDIERROR : int32
		{
		    FDIERROR_NONE,
		    FDIERROR_CABINET_NOT_FOUND,
		    FDIERROR_NOT_A_CABINET,
		    FDIERROR_UNKNOWN_CABINET_VERSION,
		    FDIERROR_CORRUPT_CABINET,
		    FDIERROR_ALLOC_FAIL,
		    FDIERROR_BAD_COMPR_TYPE,
		    FDIERROR_MDI_FAIL,
		    FDIERROR_TARGET_FILE,
		    FDIERROR_RESERVE_MISMATCH,
		    FDIERROR_WRONG_CABINET,
		    FDIERROR_USER_ABORT,
		    FDIERROR_EOF,
		}


		[CRepr]
		struct FDICABINETINFO
		{
		    public int32 cbCabinet;              // Total length of cabinet file
		    public uint16 cFolders;               // Count of folders in cabinet
		    public uint16 cFiles;                 // Count of files in cabinet
		    public uint16 setID;                  // Cabinet set ID
		    public uint16 iCabinet;               // Cabinet number in set (0 based)
		    public Windows.IntBool fReserve;               // TRUE => RESERVE present in cabinet
		    public Windows.IntBool hasprev;                // TRUE => Cabinet is chained prev
		    public Windows.IntBool hasnext;                // TRUE => Cabinet is chained next
		}

		[CRepr]
		struct FDINOTIFICATION
		{
		    public int32 cb;
		    public char8* psz1;
		    public char8* psz2;
		    public char8* psz3;                     // Points to a 256 character buffer
		    public void* pv;                       // Value for client

		    public HFile hf;

		    public uint16 date;
		    public uint16 time;
		    public uint16 attribs;

		    public uint16 setID;                    // Cabinet set ID
		    public uint16 iCabinet;                 // Cabinet number (0-based)
		    public uint16 iFolder;                  // Folder number (0-based)

		    public FDIERROR fdie;
		}

		enum NotificationType : int32
		{
		    CABINET_INFO,              // General information about cabinet
		    PARTIAL_FILE,              // First file in cabinet is continuation
		    COPY_FILE,                 // File to be copied
		    CLOSE_FILE_INFO,           // close the file, set relevant info
		    NEXT_CABINET,              // File continued to next cabinet
		    ENUMERATE,                 // Enumeration status
		}

		struct ERF
		{
		    public int32 erfOper;            // FCI/FDI error code -- see FDIERROR_XXX
		                                //  and FCIERR_XXX equates for details.

		    public int32 erfType;            // Optional error value filled in by FCI/FDI.
		                                // For FCI, this is usually the C run-time
		                                // *errno* value.
		    public Windows.IntBool fError;             // TRUE => error present
		}

		enum FDIDECRYPTTYPE : int32
		{
		    NEW_CABINET,                   // New cabinet
		    NEW_FOLDER,                    // New folder
		    DECRYPT,                       // Decrypt a data block
		}

		[CRepr]
		struct FDIDECRYPT
		{
			public FDIDECRYPTTYPE fdidt;            // Command type (selects union below)
			public void* pvUser;           // Decryption context
		}

		[CRepr]
		struct FDIDECRYPT_CABINET : FDIDECRYPT
		{
			public void* pHeaderReserve;   // RESERVE section from CFHEADER
			public uint16 cbHeaderReserve;  // Size of pHeaderReserve
			public uint16 setID;            // Cabinet set ID
			public int32 iCabinet;         // Cabinet number in set (0 based)
		}

		[CRepr]
		struct FDIDECRYPT_FOLDER : FDIDECRYPT
		{
			public void* pFolderReserve;   // RESERVE section from CFFOLDER
			public uint16 cbFolderReserve;  // Size of pFolderReserve
			public uint16 iFolder;          // Folder number in cabinet (0 based)
		}

		[CRepr]
		struct FDIDECRYPT_DECRYPT : FDIDECRYPT
		{
			public void* pDataReserve;     // RESERVE section from CFDATA
			public uint16 cbDataReserve;    // Size of pDataReserve
			public void* pbData;           // Data buffer
			public uint16 cbData;           // Size of data buffer
			public Windows.IntBool fSplit;           // TRUE if this is a split data block
			public uint16 cbPartial;        // 0 if this is not a split block, or
			                            //  the first piece of a split block;
			                            // Greater than 0 if this is the
			                            //  second piece of a split block.
		}

		function void* FNALLOC(uint32 cb);
		function void FNFREE(void* p);
		function HFile FNOPEN(char8* fileName, int32 oflag, int32 pmode);
		function uint32 FNREAD(HFile file, void* pv, uint32 cb);
		function uint32 FNWRITE(HFile file, void* pv, uint32 cb);
		function int32 FNCLOSE(HFile file);
		function int32 FNSEEK(HFile file, int32 dist, int32 seekType);
		function int FNFDINOTIFY(NotificationType notifyType, FDINOTIFICATION* notifyData);
		function int32 FNFDIDECRYPT(FDIDECRYPT* fdid);

		static void* CabMemAlloc(uint32 cb)
		{
			return new uint8[cb]*;
		}

		static void CabMemFree(void* p)
		{
			delete p;
		}

		static HFile CabFileOpen(char8* fileName, int32 oflag, int32 pmode)
		{
			String fileNameStr = scope .(fileName);

			Windows.Handle fh;
			if (oflag & 0x0100 != 0)
			{
				// Creating
				fh = Windows.CreateFileW(fileNameStr.ToScopedNativeWChar!(), Windows.GENERIC_READ | Windows.GENERIC_WRITE, .Read | .Write, null, .Create, 0, .NullHandle);
			}
			else
			{
				// Reading
				fh = Windows.CreateFileW(fileNameStr.ToScopedNativeWChar!(), Windows.GENERIC_READ, .Read | .Write, null, .Open, 0, .NullHandle);
			}

			return (.)fh;
		}

		static uint32 CabFileRead(HFile file, void* pv, uint32 cb)
		{
			Windows.ReadFile((.)file, (.)pv, (.)cb, var bytesRead, null);
			return (.)bytesRead;
		}

		static uint32 CabFileWrite(HFile file, void* pv, uint32 cb)
		{
			Windows.WriteFile((.)file, (.)pv, (.)cb, var bytesWritten, null);
			return (.)bytesWritten;
		}

		static int32 CabFileClose(HFile file)
		{
			((Windows.Handle)file).Close();
			return 0;
		}

		static int32 CabFileSeek(HFile file, int32 dist, int32 seekType)
		{
			return Windows.SetFilePointer((.)file, dist, null, seekType);
		}

		static int CabFDINotify(NotificationType notifyType, FDINOTIFICATION* notifyData)
		{
			if (notifyType == .COPY_FILE)
			{
				String destFileName = scope .();
				destFileName.Append(@"c:\temp\out\");
				destFileName.Append(notifyData.psz1);
				
				//_O_BINARY | _O_CREAT | _O_WRONLY | _O_SEQUENTIAL
				//_S_IREAD | _S_IWRITE

				String dirPath = scope String();
				Path.GetDirectoryPath(destFileName, dirPath);
				Directory.CreateDirectory(dirPath).IgnoreError();

				let hOutFile = CabFileOpen(destFileName, 0x8121, 0x0180);
				return (.)hOutFile;
			}
			else if (notifyType == .CLOSE_FILE_INFO)
			{
				CabFileClose(notifyData.hf);
				return 1;
			}

			return 0;
		}

		[Import("Cabinet.lib"), CLink]
		static extern HFDI FDICreate(FNALLOC pfnalloc,
			FNFREE  pfnfree,
			FNOPEN  pfnopen,
			FNREAD  pfnread,
			FNWRITE pfnwrite,
			FNCLOSE pfnclose,
			FNSEEK  pfnseek,
			int32 cpuType,
			ERF* erf);

		[CLink]
		static extern Windows.IntBool FDIDestroy(HFDI hfdi);

		[CLink]
		static extern Windows.IntBool FDICopy(HFDI hfdi,
			char8* pszCabinet,
			char8* pszCabPath,
			int32 flags,
			FNFDINOTIFY fnfdin,
			FNFDIDECRYPT    pfnfdid,
			void* pvUser);

		HFDI mHDFI;

		public ~this()
		{
			if (mHDFI != 0)
			{
				FDIDestroy(mHDFI);
			}
		}

		public void Init()
		{
			ERF erf;

			mHDFI = FDICreate(=> CabMemAlloc,
				=> CabMemFree,
				=> CabFileOpen,
				=> CabFileRead,
				=> CabFileWrite,
				=> CabFileClose,
				=> CabFileSeek,
				1 /*cpu80386*/,
				&erf);
		}

		public void Copy()
		{
			FDICopy(mHDFI, "test.cab", "c:\\temp\\", 0, => CabFDINotify, null, Internal.UnsafeCastToPtr(this));
		}
	}
}
