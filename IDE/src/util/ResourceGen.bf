using System;
using System.IO;
using System.Collections.Generic;
using System.Text;
namespace IDE.util
{
	class ResourceGen
	{
		[CRepr]
		struct IconDir
        {
		    public uint16 mReserved;
		    public uint16 mType;
		    public uint16 mCount;
		}

		[CRepr]
		struct IconDirectoryEntry
        {
		    public uint8 mWidth;
		    public uint8 mHeight;
		    public uint8 mColorCount;
		    public uint8 mReserved;
		    public uint16 mPlanes;
		    public uint16 mBitCount;
		    public uint32 mBytesInRes;
		    public uint32 mImageOffset;
		}

		[CRepr]
		struct ResourceHeader
        {
		  public uint32 mDataSize;
		  public uint32 mHeaderSize;
		  public uint32 mType;
		  public uint32 mName;
		  public uint32 mDataVersion;
		  public uint16 mMemoryFlags;
		  public uint16 mLanguageId;
		  public uint32 mVersion;
		  public uint32 mCharacteristics;
		}

		[CRepr]
		struct VsFixedFileInfo
        {
			public uint32 mSignature;
			public uint32 mStrucVersion;
			public uint32 mFileVersionMS;
			public uint32 mFileVersionLS;
			public uint32 mProductVersionMS;
			public uint32 mProductVersionLS;
			public uint32 mFileFlagsMask;
			public uint32 mFileFlags;
			public uint32 mFileOS;
			public uint32 mFileType;
			public uint32 mFileSubtype;
			public uint32 mFileDateMS;
			public uint32 mFileDateLS;
		};

		FileStream mOutStream = new FileStream() ~ delete _;

		public Result<void> AddIcon(String iconFile)
		{
			if (!iconFile.EndsWith(".ico", .OrdinalIgnoreCase))
			{
				gApp.OutputErrorLine("Invalid icon file: {0}. Expected '.ico' file.", iconFile);
				return .Err;
			}

			FileStream stream = scope FileStream();
			if (stream.Open(iconFile, .Read) case .Err)
			{
				gApp.OutputErrorLine("Failed to open icon file: {0}", iconFile);
				return .Err;
			}

			let iconDir = Try!(stream.Read<IconDir>());

			if ((iconDir.mReserved != 0) || (iconDir.mType != 1) || (iconDir.mCount > 0x100))
			{
				gApp.OutputErrorLine("Invalid file format: {0}", iconFile);
				return .Err;
			}

			var entries = scope List<IconDirectoryEntry>();

			for (int idx < iconDir.mCount)
			{
				entries.Add(Try!(stream.Read<IconDirectoryEntry>()));
			}

			for (int idx < iconDir.mCount)
			{
				let iconEntry = ref entries[idx];

				Try!(stream.Seek(iconEntry.mImageOffset));

				uint8* data = new:ScopedAlloc! uint8[iconEntry.mBytesInRes]*;

				Try!(stream.TryRead(.(data, iconEntry.mBytesInRes)));

				ResourceHeader res;
				res.mDataSize = iconEntry.mBytesInRes;
				res.mHeaderSize = 32;
				res.mType = 0xFFFF | (3 << 16);
				res.mName = 0xFFFF | ((uint32)(idx + 1) << 16);
				res.mDataVersion = 0;
				res.mMemoryFlags = 0x1010;
				res.mLanguageId = 1033;
				res.mVersion = 0;
				res.mCharacteristics = 0;
				Try!(mOutStream.Write(res));
				Try!(mOutStream.TryWrite(Span<uint8>(data, iconEntry.mBytesInRes)));
				mOutStream.Align(4);
			}

			ResourceHeader res;
			res.mDataSize = 6 + 14 * iconDir.mCount;
			res.mHeaderSize = 32;
			res.mType = 0xFFFF | (14 << 16);
			res.mName = 0xFFFF | (101 << 16);
			res.mDataVersion = 0;
			res.mMemoryFlags = 0x1030;
			res.mLanguageId = 1033;
			res.mVersion = 0;
			res.mCharacteristics = 0;
			Try!(mOutStream.Write(res));

			Try!(mOutStream.Write(iconDir));

			for (int idx < iconDir.mCount)
			{
				var iconEntry = ref entries[idx];

				// Write entry without mImageOffset
				Try!(mOutStream.TryWrite(Span<uint8>((uint8*)&iconEntry, 12)));
				Try!(mOutStream.Write((int16)idx + 1));
			}

	 		mOutStream.Align(4);
			return .Ok;
		}

		public Result<void> Start(String resFileName)
		{
			if (mOutStream.Create(resFileName) case .Err)
			{
				return .Err;
			}

			ResourceHeader res;
			res.mDataSize = 0;
			res.mHeaderSize = 32;
			res.mType = 0xFFFF;
			res.mName = 0xFFFF;
			res.mDataVersion = 0;
			res.mMemoryFlags = 0;
			res.mLanguageId = 0;
			res.mVersion = 0;
			res.mCharacteristics = 0;
			Try!(mOutStream.Write(res));

			return .Ok;
		}

		public Result<void> AddVersion(String description, String comments, String company, String product, String copyright, String fileVersion, String productVersion, String originalFileName)
		{
			MemoryStream strStream = scope MemoryStream();

			void AddString(String key, String value)
			{
				if (value.IsEmpty)
					return;

				let key16 = scope EncodedString(key, Encoding.UTF16);
				let value16 = scope EncodedString(value, Encoding.UTF16);

				int keyPadding = ((key16.Size / 2) & 1) + 1;
				int valuePadding = 2 - ((value16.Size / 2) & 1);

				if (value16.Size != 0)
				{
					strStream.Write((uint16)(key16.Size + keyPadding*2 + value16.Size + 8)); //wLength
					strStream.Write((uint16)(value16.Size / 2) + 1); //wValueLength
				}
				else
				{
					strStream.Write((uint16)(key16.Size + keyPadding*2 + value16.Size + 6)); //wLength
					valuePadding = 0;
					strStream.Write((uint16)0); //wValueLength
				}

				strStream.Write((uint16)1); //wType

				strStream.TryWrite(.(key16.Ptr, key16.Size));
				for (int pad < keyPadding)
					strStream.Write((char16)0);
				strStream.TryWrite(.(value16.Ptr, value16.Size));
				for (int pad < valuePadding)
					strStream.Write((char16)0);
			}

			void WriteStr16(Stream stream, String str, int length)
			{
				for (var c in str.RawChars)
                    stream.Write((char16)c);
				for (int i = str.Length; i < length; i++)
					stream.Write((char16)0);
			}

			let ext = scope String();
			Path.GetExtension(originalFileName, ext);

			let internalName = scope String();
			Path.GetFileNameWithoutExtension(originalFileName, internalName);

			AddString("CompanyName", company);
			AddString("FileDescription", description);
			AddString("Comments", comments);
			AddString("FileVersion", fileVersion);
			AddString("InternalName", internalName);
			AddString("LegalCopyright", copyright);
			AddString("OriginalFilename", originalFileName);
			AddString("ProductName", product);
			AddString("ProductVersion", productVersion);

			int verSectionCount = 0;
			uint64 version = 0;
			for (let verSection in fileVersion.Split('.'))
			{
				int32 verSect = 0;
				bool endNow = false;
				switch (int32.Parse(verSection))
				{
				case .Ok(out verSect):
				case .Err(.InvalidChar(out verSect)):
					endNow = true;
				default:
				}

				version <<= 16;
				version |= (uint64)verSect;
				if (++verSectionCount == 4)
					break;
				if (endNow)
					break;
			}

			if (verSectionCount < 4)
				version <<= (int)(16 * (4 - verSectionCount));

			int strTableSize = strStream.Length;

			ResourceHeader res;
			res.mDataSize = (uint32)strTableSize + 0xDC;
			res.mHeaderSize = 32;
			res.mType = 0xFFFF | (16 << 16);
			res.mName = 0xFFFF | (1 << 16);
			res.mDataVersion = 0;
			res.mMemoryFlags = 0x30;
			res.mLanguageId = 1033;
			res.mVersion = 0;
			res.mCharacteristics = 0;
			Try!(mOutStream.Write(res));

			Try!(mOutStream.Write((uint16)(strTableSize + 0xDC)));
			Try!(mOutStream.Write((uint16)0x34));
			Try!(mOutStream.Write((uint16)0)); // wType = binary

			WriteStr16(mOutStream, "VS_VERSION_INFO", 17);

			VsFixedFileInfo ffi;
			ffi.mSignature = 0xFEEF04BD;
			ffi.mStrucVersion = 0x10000;
			ffi.mFileVersionMS = (uint32)(version >> 32);
			ffi.mFileVersionLS = (uint32)(version);
			ffi.mProductVersionMS = (uint32)(version >> 32);
			ffi.mProductVersionLS = (uint32)(version);
			ffi.mFileFlagsMask = 0x3F;
			ffi.mFileFlags = 0;
			ffi.mFileOS = 0x40004;
			if (ext.Equals(".DLL", .OrdinalIgnoreCase))
				ffi.mFileType = 2; //DLL
			else
				ffi.mFileType = 1; //APP
			ffi.mFileSubtype = 0;
			ffi.mFileDateMS = 0;
			ffi.mFileDateLS = 0;
			Try!(mOutStream.Write(ffi));

			// StringFileInfo
			Try!(mOutStream.Write((uint16)(strTableSize + 0x3A)));
			Try!(mOutStream.Write((uint16)0)); // wValueLength
			Try!(mOutStream.Write((uint16)1)); // wType
			WriteStr16(mOutStream, "StringFileInfo", 15);

			// StringTable
			Try!(mOutStream.Write((uint16)(strTableSize + 0x16))); // wLength
			Try!(mOutStream.Write((uint16)0)); // mValueLength
			Try!(mOutStream.Write((uint16)1)); // mType
			WriteStr16(mOutStream, "040904b0", 9); // szKey
			strStream.Position = 0;
			strStream.CopyTo(mOutStream);

			// StringTable
			mOutStream.Write((uint16)0x44);
			mOutStream.Write((uint16)0x0);
			mOutStream.Write((uint16)0x1);
			WriteStr16(mOutStream, "VarFileInfo", 13);

			mOutStream.Write((uint16)0x24);
			mOutStream.Write((uint16)0x4);
			mOutStream.Write((uint16)0x0);
			WriteStr16(mOutStream, "Translation", 13);
			mOutStream.Write((uint32)0x04B00409);

			return .Ok;
		}

		public Result<void> AddManifest(String manifestPath)
		{
			String manifestText = scope String();
			if (File.ReadAllText(manifestPath, manifestText) case .Err)
			{
				gApp.OutputLine("Failed to open manifest file: {0}", manifestPath);
				return .Err;
			}

			ResourceHeader res;
			res.mDataSize = (uint32)manifestText.Length + 3;
			res.mHeaderSize = 32;
			res.mType = 0xFFFF | (0x18 << 16);
			res.mName = 0xFFFF | (2 << 16);
			res.mDataVersion = 0;
			res.mMemoryFlags = 0x1030;
			res.mLanguageId = 1033;
			res.mVersion = 0;
			res.mCharacteristics = 0;
			Try!(mOutStream.Write(res));

			Try!(mOutStream.Write(uint8[3] (0xEF, 0xBB, 0xBF))); // UTF8 BOM
			if (manifestText.Length > 0)
				Try!(mOutStream.TryWrite(Span<uint8>((uint8*)manifestText.Ptr, manifestText.Length)));

			mOutStream.Align(4);

			return .Ok;
		}

		public Result<void> Finish()
		{
			return .Ok;
		}
	}
}
