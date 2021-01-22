// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System.Diagnostics
{
	using System.Text;
	using System.IO;
	using System.Security;
	using System;
	using System.Globalization;

	public class FileVersionInfo
	{
		public enum Error
		{
			FileNotFound,
			NotSupported
		}

		private String mFileName ~ delete _;
		private String mCompanyName ~ delete _;
		private String mFileDescription ~ delete _;
		private String mFileVersion ~ delete _;
		private String mInternalName ~ delete _;
		private String mLegalCopyright ~ delete _;
		private String mOriginalFilename ~ delete _;
		private String mProductName ~ delete _;
		private String mProductVersion ~ delete _;
		private String mComments ~ delete _;
		private String mLegalTrademarks ~ delete _;
		private String mPrivateBuild ~ delete _;
		private String mSpecialBuild ~ delete _;
		private String mLanguage ~ delete _;
		private int32 mFileMajor;
		private int32 mFileMinor;
		private int32 mFileBuild;
		private int32 mFilePrivate;
		private int32 mProductMajor;
		private int32 mProductMinor;
		private int32 mProductBuild;
		private int32 mProductPrivate;
		private int32 mFileFlags;

		public void Dispose()
		{
			DeleteAndNullify!(mFileName);
			DeleteAndNullify!(mCompanyName);
			DeleteAndNullify!(mFileDescription);
			DeleteAndNullify!(mFileVersion);
			DeleteAndNullify!(mInternalName);
			DeleteAndNullify!(mLegalCopyright);
			DeleteAndNullify!(mOriginalFilename);
			DeleteAndNullify!(mProductName);
			DeleteAndNullify!(mProductVersion);
			DeleteAndNullify!(mComments);
			DeleteAndNullify!(mLegalTrademarks);
			DeleteAndNullify!(mPrivateBuild);
			DeleteAndNullify!(mSpecialBuild);
			DeleteAndNullify!(mLanguage);
			mFileMajor = 0;
			mFileMajor = 0;
			mFileBuild = 0;
			mFilePrivate = 0;
			mProductMajor = 0;
			mProductMinor = 0;
			mProductBuild = 0;
			mProductPrivate = 0;
			mFileFlags = 0;
		}

		/// Gets the comments associated with the file.
		public StringView Comments
		{
			get
			{
				return mComments;
			}
		}

		/// >Gets the name of the company that produced the file.
		public StringView CompanyName
		{
			get
			{
				return mCompanyName;
			}
		}

		/// Gets the build number of the file.
		public int FileBuildPart
		{
			get
			{
				return mFileBuild;
			}
		}

		/// Gets the description of the file.
		///
		public String FileDescription
		{
			get
			{
				return mFileDescription;
			}
		}

		///
		/// Gets the major part of the version number.
		///
		public int FileMajorPart
		{
			get
			{
				return mFileMajor;
			}
		}

		///
		/// Gets the minor
		/// part of the version number of the file.
		///
		public int FileMinorPart
		{
			get
			{
				return mFileMinor;
			}
		}

		///
		/// Gets the name of the file that this instance describes.
		///
		public String FileName
		{
			get
			{
				return mFileName;
			}
		}

		///
		/// Gets the file private part number.
		///
		public int FilePrivatePart
		{
			get
			{
				return mFilePrivate;
			}
		}

		///
		/// Gets the file version number.
		///
		public String FileVersion
		{
			get
			{
				return mFileVersion;
			}
		}

		///
		/// Gets the internal name of the file, if one exists.
		///
		public String InternalName
		{
			get
			{
				return mInternalName;
			}
		}

		///
		/// Gets a value that specifies whether the file
		/// contains debugging information or is compiled with debugging features enabled.
		///
		public bool IsDebug
		{
			get
			{
#if BF_PLATFORM_WINDOWS
				return (mFileFlags & Windows.VS_FF_DEBUG) != 0;
#else
				return false;
#endif
			}
		}

		///
		/// Gets a value that specifies whether the file has been modified and is not identical to
		/// the original shipping file of the same version number.
		///
		public bool IsPatched
		{
			get
			{
#if BF_PLATFORM_WINDOWS
				return (mFileFlags & Windows.VS_FF_PATCHED) != 0;
#else
				return false;
#endif
			}
		}

		///
		/// Gets a value that specifies whether the file was built using standard release procedures.
		///
		public bool IsPrivateBuild
		{
			get
			{
#if BF_PLATFORM_WINDOWS
				return (mFileFlags & Windows.VS_FF_PRIVATEBUILD) != 0;
#else
				return false;
#endif
			}
		}

		///
		/// Gets a value that specifies whether the file
		/// is a development version, rather than a commercially released product.
		///
		public bool IsPreRelease
		{
			get
			{
#if BF_PLATFORM_WINDOWS
				return (mFileFlags & Windows.VS_FF_PRERELEASE) != 0;
#else
				return false;
#endif
			}
		}

		///
		/// Gets a value that specifies whether the file is a special build.
		///
		public bool IsSpecialBuild
		{
			get
			{
#if BF_PLATFORM_WINDOWS
				return (mFileFlags & Windows.VS_FF_SPECIALBUILD) != 0;
#else
				return false;
#endif
			}
		}

		///
		/// 
		/// Gets the default language String for the version info block.
		///    
		///
		public String Language
		{
			get
			{
				return mLanguage;
			}
		}

		///
		/// Gets all copyright notices that apply to the specified file.
		///
		public String LegalCopyright
		{
			get
			{
				return mLegalCopyright;
			}
		}

		///
		/// Gets the trademarks and registered trademarks that apply to the file.
		///
		public String LegalTrademarks
		{
			get
			{
				return mLegalTrademarks;
			}
		}

		///
		/// Gets the name the file was created with.
		///
		public String OriginalFilename
		{
			get
			{
				return mOriginalFilename;
			}
		}

		///
		/// Gets information about a private version of the file.
		///
		public String PrivateBuild
		{
			get
			{
				return mPrivateBuild;
			}
		}

		///
		/// Gets the build number of the product this file is associated with.
		///
		public int ProductBuildPart
		{
			get
			{
				return mProductBuild;
			}
		}

		///
		/// Gets the major part of the version number for the product this file is associated with.
		///
		public int ProductMajorPart
		{
			get
			{
				return mProductMajor;
			}
		}

		///
		/// Gets the minor part of the version number for the product the file is associated with.
		///
		public int ProductMinorPart
		{
			get
			{
				return mProductMinor;
			}
		}

		///
		/// Gets the name of the product this file is distributed with.
		///
		public String ProductName
		{
			get
			{
				return mProductName;
			}
		}

		///
		/// Gets the private part number of the product this file is associated with.
		///
		public int ProductPrivatePart
		{
			get
			{
				return mProductPrivate;
			}
		}

		///
		/// Gets the version of the product this file is distributed with.
		///
		public String ProductVersion
		{
			get
			{
				return mProductVersion;
			}
		}

		///
		/// Gets the special build information for the file.
		///
		public String SpecialBuild
		{
			get
			{
				return mSpecialBuild;
			}
		}

		private static void ConvertTo8DigitHex(int value, String s)
		{
			s.AppendF("{:X8}", value);
		}

#if BF_PLATFORM_WINDOWS
		private static Windows.VS_FIXEDFILEINFO GetFixedFileInfo(void* memPtr)
		{
			void* memRef = null;
			if (Windows.VerQueryValueW(memPtr, "\\".ToScopedNativeWChar!(), ref memRef, var memLen))
				return *(Windows.VS_FIXEDFILEINFO*)memRef;
			return .();
		}

		private static String GetFileVersionLanguage(void* memPtr)
		{
			int langid = GetVarEntry(memPtr) >> 16;

			char16[256] langChars = .(0, ?);
			Windows.VerLanguageNameW((uint32)langid, &langChars, (uint32)langChars.Count);
			return new String()..Append(&langChars);
		}

		private static String GetFileVersionString(void* memPtr, String name)
		{
			String str = new String();
			void* memRef = null;
			if (Windows.VerQueryValueW(memPtr, name.ToScopedNativeWChar!(), ref memRef, var memLen))
			{
				if (memRef != null)
				{
					str.Append((char16*)memRef);
				}
			}
			return str;
		}

		private static int32 GetVarEntry(void* memPtr)
		{
			void* memRef = null;
			if (Windows.VerQueryValueW(memPtr, "\\VarFileInfo\\Translation".ToScopedNativeWChar!(), ref memRef, var memLen))
			{
				return ((int32)(*(int16*)(memRef)) << 16) + *(int16*)((uint8*)memRef + 2);
			}
			return 0x040904E4;
		}

		// 
		// This function tries to find version informaiton for a specific codepage.
		// Returns true when version information is found.
		//
		private bool GetVersionInfoForCodePage(void* memIntPtr, String codepage)
		{
			StringView template = "\\\\StringFileInfo\\\\{0}\\\\{1}";

			mCompanyName = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "CompanyName"));
			mFileDescription = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "FileDescription"));
			mFileVersion = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "FileVersion"));
			mInternalName = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "InternalName"));
			mLegalCopyright = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "LegalCopyright"));
			mOriginalFilename = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "OriginalFilename"));
			mProductName = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "ProductName"));
			mProductVersion = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "ProductVersion"));
			mComments = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "Comments"));
			mLegalTrademarks = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "LegalTrademarks"));
			mPrivateBuild = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "PrivateBuild"));
			mSpecialBuild = GetFileVersionString(memIntPtr, scope String()..AppendF(CultureInfo.InvariantCulture, template, codepage, "SpecialBuild"));

			mLanguage = GetFileVersionLanguage(memIntPtr);

			Windows.VS_FIXEDFILEINFO ffi = GetFixedFileInfo(memIntPtr);
			mFileMajor = HIWORD(ffi.dwFileVersionMS);
			mFileMinor = LOWORD(ffi.dwFileVersionMS);
			mFileBuild = HIWORD(ffi.dwFileVersionLS);
			mFilePrivate = LOWORD(ffi.dwFileVersionLS);
			mProductMajor = HIWORD(ffi.dwProductVersionMS);
			mProductMinor = LOWORD(ffi.dwProductVersionMS);
			mProductBuild = HIWORD(ffi.dwProductVersionLS);
			mProductPrivate = LOWORD(ffi.dwProductVersionLS);
			mFileFlags = (.)ffi.dwFileFlags;

			// fileVersion is chosen based on best guess. Other fields can be used if appropriate.
			return (mFileVersion != String.Empty);
		}

		///
		/// <para>Returns a System.Windows.Forms.FileVersionInfo representing the version information associated with the specified file.
		///
		public Result<void, Error> GetVersionInfo(StringView fileName)
		{
			if (mFileName != null)
			{
				Dispose();
			}

			// Check for the existence of the file. File.Exists returns false
			// if Read permission is denied.
			if (!File.Exists(fileName))
			{
				// 
				// The previous version of this code in the success case would require 
				// one imperative Assert for PathDiscovery permission, one Demand for 
				// PathDiscovery permission (blocked by the Assert), and 2 demands for
				// Read permission. It turns out that File.Exists does a demand for 
				// Read permission, so in the success case, we only need to do a single Demand. 
				// In the success case, this change increases the performance of this
				// function dramatically.
				// 
				// In the failure case, we want to remain backwardly compatible by throwing 
				// a SecurityException in the case where Read access is denied 
				// (it can be argued that this is less secure than throwing a FileNotFoundException, 
				// but perhaps not so much as to be worth a breaking change).
				// File.Exists eats a SecurityException, so we need to Demand for it
				// here. Since performance in the failure case is not crucial, as an
				// exception will be thrown anyway, we do a Demand for Read access.
				// If that does not throw an exception, then we will throw a FileNotFoundException.
				//
				// We also change the code to do a declarative Assert for PathDiscovery,
				// as that performs much better than an imperative Assert.
				//
				return .Err(.FileNotFound);
			}

			mFileName = new String(fileName);
			char16* wcStr = fileName.ToScopedNativeWChar!();

			uint32 handle;  // This variable is not used, but we need an out variable.
			uint32 infoSize = Windows.GetFileVersionInfoSizeW(wcStr, out handle);
			
			if (infoSize != 0)
			{
				uint8* memPtr = new:ScopedAlloc! uint8[infoSize]*;
				if (Windows.GetFileVersionInfoW(wcStr, 0, infoSize, memPtr))
				{
					int langid = GetVarEntry(memPtr);
					String langStr = scope .();
					ConvertTo8DigitHex(langid, langStr);
					if (!GetVersionInfoForCodePage(memPtr, langStr))
					{
						// Some dlls might not contain correct codepage information. In this case we will fail during lookup. 
						// Explorer will take a few shots in dark by trying following ID:
						//
						// 040904B0 // US English + CP_UNICODE
						// 040904E4 // US English + CP_USASCII
						// 04090000 // US English + unknown codepage
						// Explorer also randomly guess 041D04B0=Swedish+CP_UNICODE and 040704B0=German+CP_UNICODE) sometimes.
						// We will try to simulate similiar behavior here.
						int[] ids = scope .(0x040904B0, 0x040904E4, 0x04090000);
						for (int id in ids)
						{
							if (id != langid)
							{
								String idStr = scope .();
								ConvertTo8DigitHex(id, idStr);
								if (GetVersionInfoForCodePage(memPtr, idStr))
								{
									break;
								}
							}
						}
					}
				}
			}
			return .Ok;
		}
#else
		public Result<void, Error> GetVersionInfo(StringView fileName)
		{
			return .Err(.NotSupported);
		}
#endif

		private static int32 HIWORD(uint32 val)
		{
			return (.)((val << 16) & 0xFFFF);
		}

		private static int32 LOWORD(uint32 val)
		{
			return (.)(val & 0xFFFF);
		}

		///
		/// <para>Returns a partial list of properties in System.Windows.Forms.FileVersionInfo
		/// and their values.
		///
		public override void ToString(String str)
		{
			String nl = "\r\n";
			str.Append("File:             "); str.Append(FileName); str.Append(nl);
			str.Append("InternalName:     "); str.Append(InternalName); str.Append(nl);
			str.Append("OriginalFilename: "); str.Append(OriginalFilename); str.Append(nl);
			str.Append("FileVersion:      "); str.Append(FileVersion); str.Append(nl);
			str.Append("FileDescription:  "); str.Append(FileDescription); str.Append(nl);
			str.Append("Product:          "); str.Append(ProductName); str.Append(nl);
			str.Append("ProductVersion:   "); str.Append(ProductVersion); str.Append(nl);
			str.Append("Debug:            "); IsDebug.ToString(str); str.Append(nl);
			str.Append("Patched:          "); IsPatched.ToString(str); str.Append(nl);
			str.Append("PreRelease:       "); IsPreRelease.ToString(str); str.Append(nl);
			str.Append("PrivateBuild:     "); IsPrivateBuild.ToString(str); str.Append(nl);
			str.Append("SpecialBuild:     "); IsSpecialBuild.ToString(str); str.Append(nl);
			str.Append("Language:         "); str.Append(Language); str.Append(nl);
		}

	}
}
