#if BF_PLATFORM_WINDOWS

using System.IO;
using System.Collections;
using System.Text;

namespace System
{
	class Windows
	{
		public struct COM_IUnknown
		{
			public enum ClsContext : uint32
			{
				NONE = 0,
				INPROC_SERVER	= 0x1,
				INPROC_HANDLER	= 0x2,
				LOCAL_SERVER	= 0x4,
				INPROC_SERVER16	= 0x8,
				REMOTE_SERVER	= 0x10,
				INPROC_HANDLER16	= 0x20,
				RESERVED1	= 0x40,
				RESERVED2	= 0x80,
				RESERVED3	= 0x100,
				RESERVED4	= 0x200,
				NO_CODE_DOWNLOAD	= 0x400,
				RESERVED5	= 0x800,
				NO_CUSTOM_MARSHAL	= 0x1000,
				ENABLE_CODE_DOWNLOAD	= 0x2000,
				NO_FAILURE_LOG	= 0x4000,
				DISABLE_AAA	= 0x8000,
				ENABLE_AAA	= 0x10000,
				FROM_DEFAULT_CONTEXT	= 0x20000,
				ACTIVATE_X86_SERVER	= 0x40000,
#unwarn
				ACTIVATE_32_BIT_SERVER	= ACTIVATE_X86_SERVER,
				ACTIVATE_64_BIT_SERVER	= 0x80000,
				ENABLE_CLOAKING	= 0x100000,
				APPCONTAINER	= 0x400000,
				ACTIVATE_AAA_AS_IU	= 0x800000,
				RESERVED6	= 0x01000000,
				ACTIVATE_ARM32_SERVER	= 0x02000000,
				PS_DLL	= 0x80000000,

				ALL = INPROC_SERVER | INPROC_HANDLER | LOCAL_SERVER | REMOTE_SERVER
			}

			public struct VTable
			{
				public function [CallingConvention(.Stdcall)] HResult(COM_IUnknown* self, ref Guid riid, void** result) QueryInterface;
				public function [CallingConvention(.Stdcall)] uint32(COM_IUnknown* self) AddRef;
				public function [CallingConvention(.Stdcall)] uint32(COM_IUnknown* self) Release;
			}

			public enum HResult : int32
			{
				case OK;

				public const HResult NOERROR = (.)0;
				public const HResult E_INVALIDARG = (.)0x80000003L;
				public const HResult E_ABORT = (.)0x80004004L;
				public const HResult E_FAIL = (.)0x80004005L;
				public const HResult E_ACCESSDENIED = (.)0x80070005L;

				public bool Failed
				{
					get
					{
						return this != .OK;
					}
				}
			}
			protected VTable* mVT;
			public VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}

			[Import("ole32.lib"), CLink, CallingConvention(.Stdcall)]
			public static extern HResult CoCreateInstanceFromApp(ref Guid clsid, COM_IUnknown* unkOuter, ClsContext clsCtx, void* reserved, uint32 count, MULTI_QI* result);

			struct MULTI_QI
			{
				public Guid* mIID;
				public COM_IUnknown* mItf;
				public HResult mHR;
			}

			[Import("ole32.lib"), CLink, CallingConvention(.Stdcall)]
			public static extern HResult CoCreateInstance(ref Guid clsId, COM_IUnknown* unkOuter, ClsContext clsCtx, ref Guid iid, void** result);

			[Import("ole32.lib"), CLink, CallingConvention(.Stdcall)]
			public static extern void CoTaskMemFree(void* ptr);
		}

		public struct COM_IBindCtx;

		public function int WndProc(HWnd hWnd, int32 msg, int wParam, int lParam);
		public delegate IntBool EnumThreadWindowsCallback(HWnd hWnd, void* extraParameter);

		[CRepr]
		public struct OpenFileName
		{
			public int32 mStructSize = (int32)sizeof(OpenFileName); //ndirect.DllLib.sizeOf(this);
			public HWnd mHwndOwner;
			public HInstance mHInstance;
			public char16* mFilter;   // use embedded nulls to separate filters
			public int mCustomFilter = 0;
			public int32 nMaxCustFilter = 0;
			public int32 nFilterIndex;
			public char16* mFile;
			public int32 nMaxFile = Windows.MAX_PATH;
			public int mFileTitle = 0;
			public int32 mMaxFileTitle = Windows.MAX_PATH;
			public char16* mInitialDir;
			public char16* mTitle;
			public int32 mFlags;
			public int16 mFileOffset = 0;
			public int16 mFileExtension = 0;
			public char16* mDefExt;
			public int mCustData = 0;
			public WndProc mHook;
			public char16* mTemplateName = null;
			public int mReserved1 = 0;
			public int32 mReserved2 = 0;
			public int32 mFlagsEx;
		}

		public enum BrowseInfos
		{
		    NewDialogStyle      = 0x0040,   // Use the new dialog layout with the ability to resize
		    HideNewFolderButton = 0x0200    // Don't display the 'New Folder' button
		}

		[CRepr]
		public struct BrowseInfo
		{
		    public HWnd mHWndOwner;
		    public int mIdlRoot;

		    public char8* mDisplayName;

		    public char8* mTitle;
		    public int32 mFlags;
		    public WndProc mCallback;
		    public int mLParam;
		    public int32 mImage;
		}

		[CRepr]
		public struct SHFileInfo
		{
			public Handle mHIcon;                      // out: icon
			public int32 mIIcon;                      // out: icon index
			public uint32 mAttributes;               // out: SFGAO_ flags
			public char16[Windows.MAX_PATH] mDisplayName;    // out: display name (or path)
			public char16[80] mTypeName;             // out: type name
		}

		public const int32 SHGFI_ICON            = 0x00000100;     // get icon
		public const int32 SHGFI_DISPLAYNAME     = 0x00000200;     // get display name
		public const int32 SHGFI_TYPENAME        = 0x00000400;     // get type name
		public const int32 SHGFI_ATTRIBUTES      = 0x00000800;     // get attributes
		public const int32 SHGFI_ICONLOCATION    = 0x00001000;     // get icon location
		public const int32 SHGFI_EXETYPE         = 0x00002000;     // return exe type
		public const int32 SHGFI_SYSICONINDEX    = 0x00004000;     // get system icon index
		public const int32 SHGFI_LINKOVERLAY     = 0x00008000;     // put a link overlay on icon
		public const int32 SHGFI_SELECTED        = 0x00010000;     // show icon in selected state
		public const int32 SHGFI_ATTR_SPECIFIED  = 0x00020000;     // get only specified attributes
		public const int32 SHGFI_LARGEICON       = 0x00000000;     // get large icon
		public const int32 SHGFI_SMALLICON       = 0x00000001;     // get small icon
		public const int32 SHGFI_OPENICON        = 0x00000002;     // get open icon
		public const int32 SHGFI_SHELLICONSIZE   = 0x00000004;     // get shell size icon
		public const int32 SHGFI_PIDL             = 0x00000008;     // pszPath is a pidl
		public const int32 SHGFI_USEFILEATTRIBUTES = 0x00000010;     // use passed dwFileAttribute
		public const int32 SHGFI_ADDOVERLAYS     = 0x00000020;     // apply the appropriate overlays
		public const int32 SHGFI_OVERLAYINDEX    = 0x00000040;     // Get the index of the overlay

		public const int32 REG_NONE              = 0; // No value type
		public const int32 REG_SZ                = 1; // Unicode nul terminated string
		public const int32 REG_EXPAND_SZ         = 2; // Unicode nul terminated string
		// (with environment variable references)
		public const int32 REG_BINARY            = 3; // Free form binary
		public const int32 REG_DWORD             = 4; // 32-bit number
		public const int32 REG_DWORD_LITTLE_ENDIAN = 4; // 32-bit number (same as REG_DWORD)
		public const int32 REG_DWORD_BIG_ENDIAN = 5; // 32-bit number
		public const int32 REG_LINK              = 6; // Symbolic Link (unicode)
		public const int32 REG_MULTI_SZ          = 7; // Multiple Unicode strings
		public const int32 REG_RESOURCE_LIST     = 8; // Resource list in the resource map
		public const int32 REG_FULL_RESOURCE_DESCRIPTOR = 9; // Resource list in the hardware description
		public const int32 REG_RESOURCE_REQUIREMENTS_LIST = 10;
		public const int32 REG_QWORD             = 11; // 64-bit number
		public const int32 REG_QWORD_LITTLE_ENDIAN = 11; // 64-bit number (same as REG_QWORD)

		public const int32 REG_OPTION_NON_VOLATILE = 0;

		public const int32 MB_OK 				= 0;
		public const int32 MB_OKCANCEL			= 1;
		public const int32 MB_YESNO 			= 4;
		public const int32 MB_ICONHAND 			= 0x10;
		public const int32 MB_ICONQUESTION		= 0x20;
		public const int32 IDOK 				= 1;
		public const int32 IDYES 				= 6;

		public const int32 LOAD_LIBRARY_AS_DATAFILE = 0x00000002;
		public const int32 LOAD_STRING_MAX_LENGTH = 500;

		public const int32 MUI_PREFERRED_UI_LANGUAGES = 0x10;

		public const int32 SECTION_QUERY		= 0x0001;
		public const int32 SECTION_MAP_WRITE 	= 0x0002;
		public const int32 SECTION_MAP_READ		= 0x0004;
		public const int32 SECTION_MAP_EXECUTE	= 0x0008;
		public const int32 SECTION_EXTEND_SIZE	= 0x0010;

		public const int32 FILE_MAP_WRITE		= SECTION_MAP_WRITE;
		public const int32 FILE_MAP_READ		= SECTION_MAP_READ;
		//public const int32 FILE_MAP_ALL_ACCESS	= SECTION_ALL_ACCESS;

		public enum FOS : uint32
		{
		    FOS_OVERWRITEPROMPT = 0x00000002,
		    FOS_STRICTFILETYPES = 0x00000004,
		    FOS_NOCHANGEDIR = 0x00000008,
		    FOS_PICKFOLDERS = 0x00000020,
		    FOS_FORCEFILESYSTEM = 0x00000040,
		    FOS_ALLNONSTORAGEITEMS = 0x00000080,
		    FOS_NOVALIDATE = 0x00000100,
		    FOS_ALLOWMULTISELECT = 0x00000200,
		    FOS_PATHMUSTEXIST = 0x00000800,
		    FOS_FILEMUSTEXIST = 0x00001000,
		    FOS_CREATEPROMPT = 0x00002000,
		    FOS_SHAREAWARE = 0x00004000,
		    FOS_NOREADONLYRETURN = 0x00008000,
		    FOS_NOTESTFILECREATE = 0x00010000,
		    FOS_HIDEMRUPLACES = 0x00020000,
		    FOS_HIDEPINNEDPLACES = 0x00040000,
		    FOS_NODEREFERENCELINKS = 0x00100000,
		    FOS_DONTADDTORECENT = 0x02000000,
		    FOS_FORCESHOWHIDDEN = 0x10000000,
		    FOS_DEFAULTNOMINIMODE = 0x20000000
		}

		public struct Handle : int
		{
			public const Handle InvalidHandle = (Handle)-1;
			public const Handle NullHandle = (Handle)+0;

			public bool IsInvalid
			{
				get
				{
					return ((int)this == (int)(-1)) || ((int)this == (int)0);
				}
			}

			public void Close()
			{
				Windows.CloseHandle(this);
			}
		}

		public struct HKey : int
		{
			public bool IsInvalid
			{
				get
				{
					return ((int)this == (int)(-1)) || ((int)this == (int)0);
				}
			}

			public void Close()
			{
				Windows.RegCloseKey(this);
			}

			protected Result<void> GetValue(StringView name, delegate void(uint32 regType, Span<uint8> regData) onData)
			{
				uint8[4096] buffer;

				uint8* bufPtr = &buffer;
				int bufSize = sizeof(decltype(buffer));
				defer
				{
					if (bufPtr != &buffer)
						delete bufPtr;
				}			  

				while (true)
				{
					uint32 regType = 0;
					uint32 newBufSize = (uint32)bufSize;
					int ret = Windows.RegQueryValueExW(this, name.ToScopedNativeWChar!(), null, &regType, bufPtr, &newBufSize);
					if (ret == 0)
					{
						onData(regType, .(bufPtr, (int)newBufSize));
						break;
					}
					
					if (ret != ERROR_MORE_DATA)
						return .Err;

					bufSize = (.)newBufSize;
					let newBuf = new uint8[bufSize]*;
					if (bufPtr != &buffer)
						delete bufPtr;
					bufPtr = newBuf;
				}

				return .Ok;
			}

			public Result<Variant> GetValue(StringView name)
			{
				Variant val = default;

				GetValue(name, scope [&] (regType, regData) =>
					{
						switch (regType)
						{
						case Windows.REG_DWORD:
							val = Variant.Create<int32>(*(int32*)regData.Ptr);
						case Windows.REG_SZ:
							var span = Span<char16>((char16*)regData.Ptr, regData.Length / 2);
							if ((span.Length > 0) && (span[span.Length - 1] == 0))
								span.RemoveFromEnd(1);
							String str = scope String(span);
							val = Variant.Create<String>(str);
						case Windows.REG_QWORD:
							val = Variant.Create<int64>(*(int64*)regData.Ptr);
						default:
							Runtime.NotImplemented();
						}
					}).IgnoreError();
				
				if (!val.HasValue)
					return .Err;
				return val;
			}

			public Result<void> GetValue(StringView name, List<uint8> outData)
			{
				bool gotData = false;
				GetValue(name, scope [?] (regType, regData) =>
					{
						if (regType == Windows.REG_BINARY)
						{
							gotData = true;
							outData.AddRange(regData);
						}
					}).IgnoreError();
				if (!gotData)
					return .Err;
				return .Ok;
			}

			public Result<void> GetValue(StringView name, String outData)
			{
				bool gotData = false;
				GetValue(name, scope [?] (regType, regData) =>
					{
						if ((regType == Windows.REG_SZ) || (regType == Windows.REG_EXPAND_SZ))
						{
							gotData = true;
							var span = Span<char16>((char16*)regData.Ptr, regData.Length / 2);
							if ((span.Length > 0) && (span[span.Length - 1] == 0))
								span.RemoveFromEnd(1);
							outData.Append(span);
						}
					}).IgnoreError();
				if (!gotData)
					return .Err;
				return .Ok;
			}

			public Result<void> GetValueT<T>(StringView name, ref T data)
			{
				bool gotData = false;
				int sizeofT = sizeof(T);
				GetValue(name, scope [&] (regType, regData) =>
					{
						if ((regType == Windows.REG_BINARY) && (regData.Length == sizeofT))
						{
							Internal.MemCpy(&data, regData.Ptr, sizeofT);
							gotData = true;
						}
					}).IgnoreError();
				if (!gotData)
					return .Err;
				return .Ok;
			}

			public Result<Variant> EnumValue(int idx, String outName)
			{
				char16[2048] nameBuffer;
				uint8[4096] buffer;

				char16* namePtr = &nameBuffer;
				uint8* bufPtr = &buffer;
				uint32 nameSize = sizeof(decltype(nameBuffer));
				uint32 bufSize = sizeof(decltype(buffer));
				defer
				{
					if (bufPtr != &buffer)
						delete bufPtr;
					if (namePtr != &nameBuffer)
						delete namePtr;
				}

				while (true)
				{
					uint32 regType = 0;
					uint32 newNameSize = (.)nameSize;
					uint32 newBufSize = (.)bufSize;
					int ret = Windows.RegEnumValueW(this, (.)idx, namePtr, &newNameSize, null, &regType, bufPtr, &newBufSize);
					if (ret == 0)
						break;
					
					if (ret != ERROR_MORE_DATA)
						return .Err;
					if (newNameSize > nameSize)
					{
						nameSize = newNameSize;
						let newBuf = new uint8*[(.)nameSize]*;
						if (namePtr != &nameBuffer)
							delete namePtr;
						namePtr = (char16*)newBuf;
					}
					if (newBufSize > bufSize)
					{
						bufSize = newBufSize;
						let newBuf = new uint8[(.)bufSize]*;
						if (bufPtr != &buffer)
							delete bufPtr;
						bufPtr = newBuf;
					}
				}

				Runtime.NotImplemented();
				//return Variant.Create<int>(1234);
			}

			public Result<void> SetValue(StringView name, uint32 val)
			{
				var val;
				let result = Windows.RegSetValueExA(this, name.ToScopeCStr!(), 0,  Windows.REG_DWORD, &val, 4);
				if (result != 0)
					return .Err;
				return .Ok;
			}

			public Result<void> SetValue(StringView name, StringView strValue)
			{
				let result = Windows.RegSetValueExA(this, name.ToScopeCStr!(), 0,  Windows.REG_SZ, strValue.ToScopeCStr!(), (uint32)strValue.Length + 1);
				if (result != 0)
					return .Err;
				return .Ok;
			}

			public Result<void> SetValueExpand(StringView name, StringView strValue)
			{
				let result = Windows.RegSetValueExA(this, name.ToScopeCStr!(), 0,  Windows.REG_EXPAND_SZ, strValue.ToScopeCStr!(), (uint32)strValue.Length + 1);
				if (result != 0)
					return .Err;
				return .Ok;
			}
		}

		public struct HWnd : int
		{
			public const HWnd Broadcast = (.)0xFFFF;
		}

		public struct HModule : int
		{
			public bool IsInvalid
			{
				get
				{
					return ((int)this == (int)(-1)) || ((int)this == (int)0);
				}
			}
		}

		public struct HInstance : HModule
		{

		}

		public struct FindHandle : int
		{
			public const FindHandle InvalidHandle = (FindHandle)-1;
			public const FindHandle NullHandle = (FindHandle)+0;

			public bool IsInvalid
			{
				get
				{
					return ((int)this == (int)(-1)) || ((int)this == (int)0);
				}
			}

			public void Close()
			{
				Windows.FindClose(this);				
			}
		}

		public struct IntBool : int32
		{
			public static implicit operator IntBool(bool value)
			{
			    return (IntBool)(value ? 1 : 0);
			}

			public static implicit operator bool(IntBool value)
			{
			    return value != 0;
			}
		}

		public struct FileHandle : Handle
		{
			public new const FileHandle InvalidHandle = (FileHandle)-1;
		}

		public struct EventHandle : Handle
		{

		}

		public struct ProcessHandle : Handle
		{

		}

		public struct IOCompletionHandle : Handle
		{
			public new const IOCompletionHandle NullHandle = (IOCompletionHandle)+0;
		}

		[CRepr]
		public struct FileNotifyInformation
		{
			public int32 mNextEntryOffset;
			public int32 mAction;
			public int32 mFileNameLength;
			public uint16 mFileName;
		}

		[CRepr]
		public struct Overlapped
		{
			public int mInternal;
			public int mInternalHigh;
			public uint32 mOffset;
			public uint32 mOffsetHigh;
			public Handle mHEvent;
		}

		[CRepr]
		public struct ShellExecuteInfo
		{
			public uint32 mSize = (uint32)sizeof(ShellExecuteInfo);
			public uint32 mMask;
			public Handle mHWnd;
			public char8* mVerb;
			public char8* mFile;
			public char8* mParameters;
			public char8* mDirectory;
			public int32 mShow;
			public Handle mInstApp;
			public void* mIDList;
			public char8* mClass;
			public Handle mHKeyClass;
			public uint32 mHotKey;
			public Handle mMonitorOrIcon;
			public ProcessHandle mProcess;
		}

		[CRepr]
		public struct StartupInfo
        {
			public int32 mCb = (int32)sizeof(StartupInfo);
			public char8* mReserved;
			public char8* mDesktop;
			public char8* mTitle;
			public int32 mX;
			public int32 mY;
			public int32 mXSize;
			public int32 mYSize;
			public int32 mXCountChars;
			public int32 mYCountChars;
			public int32 mFillAttribute;
			public int32 mFlags;
			public int16 mShowWindow;
			public int16 mReserved2;
			public uint8* mReserved3;
			public FileHandle mStdInput;
			public FileHandle mStdOutput;
			public FileHandle mStdError;
		}

		[CRepr]
		public struct ProcessInformation
		{
			public ProcessHandle mProcess;
			public Handle mThread;
			public int32 mProcessId;
			public int32 mThreadId;
		}

		[CRepr]
		public struct SystemTime
		{
			public uint16 mYear;
			public uint16 mMonth;
			public uint16 mDayOfWeek;
			public uint16 mDay;
			public uint16 mHour;
			public uint16 mMinute;
			public uint16 mSecond;
			public uint16 mMilliseconds;
		}

		[CRepr]
		public struct TimeZoneInformation
		{
			public int32 mBias;
			public char16[32] mStandardName;
			public SystemTime mStandardDate;
			public int32 mStandardBias;
			public char16[32] mDaylightName;
			public SystemTime mDaylightDate;
			public int32 mDaylightBias;

			public this()
			{
				this = default;
			}

			public this(DynamicTimeZoneInformation dtzi)
			{
				mBias = dtzi.mBias;
				mStandardName = dtzi.mStandardName;
				mStandardDate = dtzi.mStandardDate;
				mStandardBias = dtzi.mStandardBias;
				mDaylightName = dtzi.mDaylightName;
				mDaylightDate = dtzi.mDaylightDate;
				mDaylightBias = dtzi.mDaylightBias;
			}
		}

		[CRepr]
		public struct RegistryTimeZoneInformation
		{
			public int32 mBias;
			public int32 mStandardBias;
			public int32 mDaylightBias;
			public SystemTime mStandardDate;
			public SystemTime mDaylightDate;

			public this()
			{
				this = default;
			}

			public this(TimeZoneInformation tzi)
			{
			    mBias = tzi.mBias;
			    mStandardDate = tzi.mStandardDate;
			    mStandardBias = tzi.mStandardBias;
			    mDaylightDate = tzi.mDaylightDate;
			    mDaylightBias = tzi.mDaylightBias;
			}
		}

		[CRepr]
		public struct DynamicTimeZoneInformation
		{
			public int32 mBias;
			public char16[32] mStandardName;
			public SystemTime mStandardDate;
			public int32 mStandardBias;
			public char16[32] mDaylightName;
			public SystemTime mDaylightDate;
			public int32 mDaylightBias;
			public char16[128] mTimeZoneKeyName;
			public IntBool mDynamicDaylightTimeDisabled;
		}

		public struct Int64_Rev
		{
			public int32 mHigh;
			public int32 mLow;

			public int64 Value
			{
				get
				{
					return ((int64)mHigh<<32) | mLow;
				}

				set mut
				{
					mLow = (int32)value;
					mHigh = (int32)(value >> 32);
				}
			}
		}

		[CRepr, Packed]
		public struct FileAttributeData
		{
			public int32 mFileAttributes;
			public uint64 mCreationTime;
			public uint64 mLastAccessTime;
			public uint64 mLastWriteTime;
			public Int64_Rev mFileSize;

			public void PopulateFrom(NativeFindDataA findData) mut
            {
				// Copy the information to data
			    mFileAttributes = findData.mFileAttributes; 
			    mCreationTime = findData.mCreationTime;
			    mLastAccessTime = findData.mLastAccessTime; 
			    mLastWriteTime = findData.mLastWriteTime; 
			    mFileSize = findData.mFileSize; 
			}

			public void PopulateFrom(NativeFindData findData) mut
			{
				// Copy the information to data
			    mFileAttributes = findData.mFileAttributes; 
			    mCreationTime = findData.mCreationTime;
			    mLastAccessTime = findData.mLastAccessTime; 
			    mLastWriteTime = findData.mLastWriteTime;
			    mFileSize = findData.mFileSize; 
			}
		}

		[CRepr, Packed]
		public struct NativeFindDataA
		{
			public int32 mFileAttributes;
			public uint64 mCreationTime;
			public uint64 mLastAccessTime;
			public uint64 mLastWriteTime;
			public Int64_Rev mFileSize;
			public int32 mReserved0;
			public int32 mReserved1;
			public char8[260] mFileName;
			public char8[14] mAlternateFileName;
		}

		[CRepr, Packed]
		public struct NativeFindData
		{
			public int32 mFileAttributes;
			public uint64 mCreationTime;
			public uint64 mLastAccessTime;
			public uint64 mLastWriteTime;
			public Int64_Rev mFileSize;
			public int32 mReserved0;
			public int32 mReserved1;
			public char16[260] mFileName;
			public char16[14] mAlternateFileName;
		}

		public const int32 S_OK = 0;
		public const int32 MAX_PATH = 260;

		public const int32 SEE_MASK_DEFAULT = 0;
		public const int32 SEE_MASK_NOCLOSEPROCESS  = 0x40;
		public const int32 SEE_MASK_FLAG_NO_UI = 0x400;
		public const int32 SEE_MASK_FLAG_DDEWAIT = 0x100;

		public const int32 FILE_ATTRIBUTE_READONLY      = 0x00000001;
		public const int32 FILE_ATTRIBUTE_DIRECTORY     = 0x00000010;
		public const int32 FILE_ATTRIBUTE_REPARSE_POINT = 0x00000400;

		public const int32 GENERIC_READ = (.)0x80000000;
		public const int32 GENERIC_WRITE = 0x40000000;
		public const int32 GENERIC_EXECUTE = 0x20000000;
		public const int32 GENERIC_ALL = 0x10000000;

		public const int32 SW_HIDE = 0;
		public const int32 SW_MAXIMIZE = 3;
		public const int32 SW_MINIMIZE = 6;
		public const int32 SW_RESTORE = 9;
		public const int32 SW_SHOW = 5;
		public const int32 SW_SHOWDEFAULT = 10;
		public const int32 SW_SHOWMAXIMIZED = 3;
		public const int32 SW_SHOWMINIMIZED = 2;
		public const int32 SW_SHOWMINNOACTIVE = 7;
		public const int32 SW_SHOWNA = 8;
		public const int32 SW_SHOWNOACTIVATE = 4;
		public const int32 SW_SHOWNORMAL = 1;
		public const int32 GW_OWNER = 4;

		public const int32 STD_INPUT_HANDLE = -10;
		public const int32 STD_OUTPUT_HANDLE = -11;
		public const int32 STD_ERROR_HANDLE = -12;

		public const int32 CREATE_SUSPENDED = 0x00000004;
		public const int32 CREATE_NO_WINDOW = 0x08000000;

		// From WinBase.h
        public const int32 FILE_TYPE_DISK = 0x0001;
        public const int32 FILE_TYPE_CHAR = 0x0002;
        public const int32 FILE_TYPE_PIPE = 0x0003;

		public const int32 FILE_READ_DATA = (0x0001);
		public const int32 FILE_WRITE_DATA = (0x0002);
		public const int32 FILE_LIST_DIRECTORY = (0x0001);
		public const int32 FILE_SHARE_READ = 0x00000001;
		public const int32 FILE_SHARE_WRITE = 0x00000002;
		public const int32 FILE_SHARE_DELETE = 0x00000004;

		public const int32 FILE_BEGIN = 0;
		public const int32 FILE_CURRENT = 1;
		public const int32 FILE_END = 2;

		public const int32 FILE_FLAG_OVERLAPPED = 0x40000000;
		public const int32 FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
		public const int32 FILE_FLAG_FIRST_PIPE_INSTANCE = 0x00080000;
		public const int32 FILE_FLAG_WRITE_THROUGH = (int32)0x80000000;		

		public const int32 DUPLICATE_CLOSE_SOURCE = 0x1;
		public const int32 DUPLICATE_SAME_ACCESS = 0x2;

		public const int32 WAIT_ABANDONED = 0x80;
		public const int32 WAIT_OBJECT_0 = 0;
		public const int32 WAIT_TIMEOUT = 0x102;
		public const int32 WAIT_FAILED = -1;

		public const int32 STARTF_USESTDHANDLES = 0x00000100;

		public const int32 ERROR_BAD_EXE_FORMAT = 193;
		public const int32 ERROR_EXE_MACHINE_TYPE_MISMATCH = 216;
		public const int32 ERROR_ABANDONED_WAIT_0 = 735;
		public const int32 ERROR_PIPE_CONNECTED = 535;
		public const int32 ERROR_BROKEN_PIPE = 109;
		public const int32 ERROR_MORE_DATA = 234;
		public const int32 ERROR_NO_DATA = 232;
		public const int32 ERROR_NO_MORE_ITEMS = 259;

		public const int32 PROCESS_QUERY_INFORMATION = 0x0400;

		public const int32 STATUS_PENDING = 0x00000103;

		public const int32 STILL_ACTIVE = STATUS_PENDING;

		public const int32 SYNCHRONIZE = 0x00100000;

		public const int32 SEM_FAILCRITICALERRORS = 1;

		public const int32 ERROR_SUCCESS = 0x0;
		public const int32 ERROR_INVALID_FUNCTION = 0x1;
		public const int32 ERROR_FILE_NOT_FOUND = 0x2;
		public const int32 ERROR_PATH_NOT_FOUND = 0x3;
		public const int32 ERROR_ACCESS_DENIED = 0x5;
		public const int32 ERROR_INVALID_HANDLE = 0x6;
		public const int32 ERROR_NOT_ENOUGH_MEMORY = 0x8;
		public const int32 ERROR_INVALID_DATA = 0xd;
		public const int32 ERROR_INVALID_DRIVE = 0xf;
		public const int32 ERROR_NO_MORE_FILES = 0x12;
		public const int32 ERROR_NOT_READY = 0x15;
		public const int32 ERROR_SHARING_VIOLATION = 32;
		public const int32 ERROR_ALREADY_EXISTS = 0xB7;

		public const int32 GetFileExInfoStandard = 0;

		public const int32 GWL_WNDPROC = (-4);
		public const int32 GWL_HWNDPARENT = (-8);
		public const int32 GWL_STYLE = (-16);
		public const int32 GWL_EXSTYLE = (-20);
		public const int32 GWL_ID = (-12);
		public const int32 GW_HWNDFIRST = 0;
		public const int32 GW_HWNDLAST = 1;
		public const int32 GW_HWNDNEXT = 2;
		public const int32 GW_HWNDPREV = 3;

		public const int32 OFN_READONLY = 0x00000001;
		public const int32 OFN_OVERWRITEPROMPT = 0x00000002;
        public const int32 OFN_HIDEREADONLY = 0x00000004;
        public const int32 OFN_NOCHANGEDIR = 0x00000008;
        public const int32 OFN_SHOWHELP = 0x00000010;
        public const int32 OFN_ENABLEHOOK = 0x00000020;
        public const int32 OFN_NOVALIDATE = 0x00000100;
        public const int32 OFN_ALLOWMULTISELECT = 0x00000200;
        public const int32 OFN_PATHMUSTEXIST = 0x00000800;
        public const int32 OFN_FILEMUSTEXIST = 0x00001000;
        public const int32 OFN_CREATEPROMPT = 0x00002000;
        public const int32 OFN_EXPLORER = 0x00080000;
        public const int32 OFN_NODEREFERENCELINKS = 0x00100000;
        public const int32 OFN_ENABLESIZING = 0x00800000;
		public const int32 OFN_USESHELLITEM = 0x01000000;

		public const int32 CSIDL_DESKTOP = 0x0000;
		public const int32 CSIDL_PROGRAMS = 0x0002; // Start Menu\Programs
		public const int32 CSIDL_PERSONAL = 0x0005; // My Documents
		public const int32 CSIDL_STARTUP = 0x0007; // Start Menu\Programs\Startup
		public const int32 CSIDL_LOCAL_APPDATA = 0x001c;// <user name>\Local Settings\Applicaiton Data (non roaming)
		public const int32 CSIDL_COMMON_APPDATA = 0x0023; // All Users\Application Data
		public const int32 CSIDL_PROGRAM_FILES = 0x0026; // C:\Program Files

		public const int32 WM_CLOSE = 0x0010;
		public const int32 WM_DESTROY = 0x0002;
		public const int32 WM_INITDIALOG = 0x0110;
		public const int32 WM_SETFOCUS = 0x0007;
		public const int32 WM_USER = 0x0400;
		public const int32 WM_KEYDOWN = 0x0100;
		public const int32 WM_KEYUP = 0x0101;
		public const int32 WM_CHAR = 0x0102;
		public const int32 WM_SETTINGCHANGE = 0x001A;

		public const int32 BFFM_INITIALIZED = 1;
		public const int32 BFFM_SELCHANGED = 2;
		public const int32 BFFM_SETSELECTIONA = 0x400 + 102;
		public const int32 BFFM_SETSELECTIONW = 0x400 + 103;
		public const int32 BFFM_ENABLEOK = 0x400 + 101;

		public const int32 STANDARD_RIGHTS_REQUIRED = 0x000F0000;
		public const int32 PROCESS_ALL_ACCESS = STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0xFFF;

		public const uint32 STATUS_INFO_LENGTH_MISMATCH = 0xC0000004;

		public const int32 PIPE_ACCESS_DUPLEX = 0x00000003;
		public const int32 PIPE_ACCESS_INBOUND = 0x00000001;
		public const int32 PIPE_ACCESS_OUTBOUND = 0x00000002;		
		public const int32 WRITE_DAC = 0x00040000;
		public const int32 WRITE_OWNER = 0x00080000;
		public const int32 ACCESS_SYSTEM_SECURITY = 0x01000000;

		public const int32 PIPE_TYPE_BYTE = 0x00000000;
		public const int32 PIPE_TYPE_MESSAGE = 0x00000004;		
		public const int32 PIPE_READMODE_BYTE = 0x00000000;
		public const int32 PIPE_READMODE_MESSAGE = 0x00000002;		
		public const int32 PIPE_WAIT = 0x00000000;
		public const int32 PIPE_NOWAIT = 0x00000001;		
		public const int32 PIPE_ACCEPT_REMOTE_CLIENTS = 0x00000000;
		public const int32 PIPE_REJECT_REMOTE_CLIENTS = 0x00000008;
		public const int32 PIPE_UNLIMITED_INSTANCES = 255;		

		public const int32 MEM_COMMIT = 0x1000;
		public const int32 MEM_RESERVE = 0x2000;
		public const int32 MEM_DECOMMIT = 0x4000;
		public const int32 MEM_RELEASE = 0x8000;

		public const int32 PAGE_NOACCESS = 0x01;
		public const int32 PAGE_READONLY = 0x02;
		public const int32 PAGE_READWRITE = 0x04;
		public const int32 PAGE_WRITECOPY = 0x08;

		public const int32 KEY_EXECUTE     		= (0x20019);
		public const int32 KEY_READ     		= (0x20019);
		public const int32 KEY_WRITE     		= (0x20006);
		public const int32 KEY_ALL_ACCESS               = (0xf003f);
		public const int32 KEY_QUERY_VALUE         	= (0x0001);
		public const int32 KEY_SET_VALUE           	= (0x0002);
		public const int32 KEY_CREATE_SUB_KEY      	= (0x0004);
		public const int32 KEY_ENUMERATE_SUB_KEYS  	= (0x0008);
		public const int32 KEY_NOTIFY        		= (0x0010);
		public const int32 KEY_CREATE_LINK   		= (0x0020);
		public const int32 KEY_WOW64_32KEY   		= (0x0200);
		public const int32 KEY_WOW64_64KEY   		= (0x0100);
		public const int32 KEY_WOW64_RES     		= (0x0300);

		public const HKey HKEY_CLASSES_ROOT         = (HKey)0x80000000;
		public const HKey HKEY_CURRENT_USER         = (HKey)0x80000001;
		public const HKey HKEY_LOCAL_MACHINE        = (HKey)0x80000002;
		public const HKey HKEY_USERS                = (HKey)0x80000003;
		public const HKey HKEY_PERFORMANCE_DATA     = (HKey)0x80000004;
		public const HKey HKEY_PERFORMANCE_TEXT     = (HKey)0x80000050;
		public const HKey HKEY_PERFORMANCE_NLSTEXT  = (HKey)0x80000060;
		public const HKey HKEY_CURRENT_CONFIG       = (HKey)0x80000005;
		public const HKey HKEY_DYN_DATA             = (HKey)0x80000006;
		public const HKey HKEY_CURRENT_USER_LOCAL_SETTINGS = (HKey)0x80000007;

		public const int32 NtQueryProcessBasicInfo = 0;          
		public const int32 NtQuerySystemProcessInformation = 5;

		public const int32 TIME_ZONE_ID_INVALID = -1;

		public const int32 OBJECT_INHERIT_ACE = 1;
		public const int32 CONTAINER_INHERIT_ACE = 2;
		public const int32 NO_PROPAGATE_INHERIT_ACE = 4;
		public const int32 INHERIT_ONLY_ACE = 8;
		public const int32 INHERITED_ACE = 0x10;
		public const int32 VALID_INHERIT_FLAGS = 0x1F;

		public const int32 SMTO_NORMAL = 0x0000;
		public const int32 SMTO_BLOCK = 0x0001;
		public const int32 SMTO_ABORTIFHUNG = 0x0002;

		public const int32 VS_FF_DEBUG             = 0x00000001;
		public const int32 VS_FF_PRERELEASE        = 0x00000002;
		public const int32 VS_FF_PATCHED           = 0x00000004;
		public const int32 VS_FF_PRIVATEBUILD      = 0x00000008;
		public const int32 VS_FF_INFOINFERRED      = 0x00000010;
		public const int32 VS_FF_SPECIALBUILD 		= 0x00000020L;

		[CRepr]
		public struct VS_FIXEDFILEINFO
		{
		    public uint32   dwSignature;            /* e.g. 0xfeef04bd */
		    public uint32   dwStrucVersion;         /* e.g. 0x00000042 = "0.42" */
		    public uint32   dwFileVersionMS;        /* e.g. 0x00030075 = "3.75" */
		    public uint32   dwFileVersionLS;        /* e.g. 0x00000031 = "0.31" */
		    public uint32   dwProductVersionMS;     /* e.g. 0x00030010 = "3.10" */
		    public uint32   dwProductVersionLS;     /* e.g. 0x00000031 = "0.31" */
		    public uint32   dwFileFlagsMask;        /* = 0x3F for version "0.42" */
		    public uint32   dwFileFlags;            /* e.g. VFF_DEBUG | VFF_PRERELEASE */
		    public uint32   dwFileOS;               /* e.g. VOS_DOS_WINDOWS16 */
		    public uint32   dwFileType;             /* e.g. VFT_DRIVER */
		    public uint32   dwFileSubtype;          /* e.g. VFT2_DRV_KEYBOARD */
		    public uint32   dwFileDateMS;           /* e.g. 0 */
		    public uint32   dwFileDateLS;           /* e.g. 0 */
		}

		enum SECURITY_INFORMATION : int32
		{
			DACL_SECURITY_INFORMATION = 4
		}

		enum SE_OBJECT_TYPE : int32
		{
		  SE_UNKNOWN_OBJECT_TYPE,
		  SE_FILE_OBJECT,
		  SE_SERVICE,
		  SE_PRINTER,
		  SE_REGISTRY_KEY,
		  SE_LMSHARE,
		  SE_KERNEL_OBJECT,
		  SE_WINDOW_OBJECT,
		  SE_DS_OBJECT,
		  SE_DS_OBJECT_ALL,
		  SE_PROVIDER_DEFINED_OBJECT,
		  SE_WMIGUID_OBJECT,
		  SE_REGISTRY_WOW64_32KEY,
		  SE_REGISTRY_WOW64_64KEY
		}

		public struct SID;
		public struct SECURITY_DESCRIPTOR;

		[CRepr]
		public struct ACL
		{
		    uint8 AclRevision;
		    uint8  Sbz1;
		    uint16   AclSize;
		    uint16   AceCount;
		    uint16   Sbz2;
		}

		public enum ACCESS_MODE : int32
		{
			NOT_USED_ACCESS = 0,
			GRANT_ACCESS,
			SET_ACCESS,
			DENY_ACCESS,
			REVOKE_ACCESS,
			SET_AUDIT_SUCCESS,
			SET_AUDIT_FAILURE
		}

		public enum MULTIPLE_TRUSTEE_OPERATION : int32
		{
		    NO_MULTIPLE_TRUSTEE,
		    TRUSTEE_IS_IMPERSONATE,
		} 

		public enum TRUSTEE_FORM : int32
		{
		    TRUSTEE_IS_SID,
		    TRUSTEE_IS_NAME,
		    TRUSTEE_BAD_FORM,
		    TRUSTEE_IS_OBJECTS_AND_SID,
		    TRUSTEE_IS_OBJECTS_AND_NAME
		}

		public enum TRUSTEE_TYPE : int32
		{
		    TRUSTEE_IS_UNKNOWN,
		    TRUSTEE_IS_USER,
		    TRUSTEE_IS_GROUP,
		    TRUSTEE_IS_DOMAIN,
		    TRUSTEE_IS_ALIAS,
		    TRUSTEE_IS_WELL_KNOWN_GROUP,
		    TRUSTEE_IS_DELETED,
		    TRUSTEE_IS_INVALID,
		    TRUSTEE_IS_COMPUTER
		}

		[CRepr]
		public struct TRUSTEE_W
		{
			TRUSTEE_W*					pMultipleTrustee;
			MULTIPLE_TRUSTEE_OPERATION  MultipleTrusteeOperation;
			TRUSTEE_FORM                TrusteeForm;
			TRUSTEE_TYPE                TrusteeType;
			char16*                     ptstrName;
		}

		[CRepr]
		public struct EXPLICIT_ACCESS_W
		{
		    uint32        grfAccessPermissions;
		    ACCESS_MODE  grfAccessMode;
		    uint32        grfInheritance;
		    TRUSTEE_W    Trustee;
		}

		struct COM_IFileDialogEvents : Windows.COM_IUnknown
		{
			struct FDE_SHAREVIOLATION_RESPONSE;
			struct FDE_OVERWRITE_RESPONSE;

			public struct VTable : Windows.COM_IUnknown.VTable
			{
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog) OnFileOk;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog, COM_IShellItem* psiFolder) OnFolderChanging;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog) OnFolderChange;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog) OnSelectionChange;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog, FDE_SHAREVIOLATION_RESPONSE* pResponse) OnShareViolation;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog) OnTypeChange;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialogEvents* self, COM_IFileDialog* fileDialog, COM_IShellItem* shellItem, FDE_OVERWRITE_RESPONSE* response) OnOverwrite;
			}
		}

		public struct COM_IShellItem : Windows.COM_IUnknown
		{
			public static Guid sIID = .(0x43826d1e, 0xe718, 0x42ee, 0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe);

			public enum SIGDN : uint32
			{
			    NORMALDISPLAY = 0x00000000,           // SHGDN_NORMAL
			    PARENTRELATIVEPARSING = 0x80018001,   // SHGDN_INFOLDER | SHGDN_FORPARSING
			    DESKTOPABSOLUTEPARSING = 0x80028000,  // SHGDN_FORPARSING
			    PARENTRELATIVEEDITING = 0x80031001,   // SHGDN_INFOLDER | SHGDN_FOREDITING
			    DESKTOPABSOLUTEEDITING = 0x8004c000,  // SHGDN_FORPARSING | SHGDN_FORADDRESSBAR
			    FILESYSPATH = 0x80058000,             // SHGDN_FORPARSING
			    URL = 0x80068000,                     // SHGDN_FORPARSING
			    PARENTRELATIVEFORADDRESSBAR = 0x8007c001,     // SHGDN_INFOLDER | SHGDN_FORPARSING | SHGDN_FORADDRESSBAR
			    PARENTRELATIVE = 0x80080001           // SHGDN_INFOLDER
			}

			public struct VTable : Windows.COM_IUnknown.VTable
			{
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItem* self, void* pbc, ref Guid bhid, ref Guid riid, void** ppv) BindToHandler;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItem* self, out COM_IShellItem* ppsi) GetParent;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItem* self, SIGDN sigdnName, out char16* ppszName) GetDisplayName;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItem* self, uint sfgaoMask, out uint psfgaoAttribs) GetAttributes;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItem* self, COM_IShellItem* psi, uint32 hint, out int32 piOrder) Compare;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		public struct COM_IShellItemArray : Windows.COM_IUnknown
		{
			public enum GETPROPERTYSTOREFLAGS : uint
			{
			    DEFAULT = 0x00000000,
			    HANDLERPROPERTIESONLY = 0x00000001,
			    READWRITE = 0x00000002,
			    TEMPORARY = 0x00000004,
			    FASTPROPERTIESONLY = 0x00000008,
			    OPENSLOWITEM = 0x00000010,
			    DELAYCREATION = 0x00000020,
			    BESTEFFORT = 0x00000040,
			    NO_OPLOCK = 0x00000080,
			    PREFERQUERYPROPERTIES = 0x00000100,
			    EXTRINSICPROPERTIES = 0x00000200,
			    EXTRINSICPROPERTIESONLY = 0x00000400,
			    VOLATILEPROPERTIES = 0x00000800,
			    VOLATILEPROPERTIESONLY = 0x00001000,
			    MASK_VALID = 0x00001FFF
			}

			[AllowDuplicates]
			public enum SIATTRIBFLAGS : uint
			{
			    AND = 0x1,
			    OR = 0x2,
			    APPCOMPAT = 0x3,
			    MASK = 0x3,
			    ALLITEMS = 0x4000
			}

			public struct PROPERTYKEY;

			public static Guid sIID = .(0xB63EA76D, 0x1F85, 0x456F, 0xA1, 0x9C, 0x48, 0x15, 0x9E, 0xFA, 0x85, 0x8B);

			public struct VTable : Windows.COM_IUnknown.VTable
			{
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, void* pbc, ref Guid rbhid, ref Guid riid, out void* ppvOut) BindToHandler;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, GETPROPERTYSTOREFLAGS flags, ref Guid riid, out void* ppv) GetPropertyStore;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, ref PROPERTYKEY keyType, ref Guid riid, out void* ppv) GetPropertyDescriptionList;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, SIATTRIBFLAGS dwAttribFlags, uint32 sfgaoMask, out uint32 psfgaoAttribs) GetAttributes;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, out uint32 pdwNumItems) GetCount;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, uint32 dwIndex, out COM_IShellItem* ppsi) GetItemAt;
				public function [CallingConvention(.Stdcall)] HResult(COM_IShellItemArray* self, out void* ppenumShellItems) EnumItems;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		public struct COMDLG_FILTERSPEC
		{
		    public char16* pszName;
		    public char16* pszSpec;
		}

		enum FDAP : uint32
		{
		    FDAP_BOTTOM = 0x00000000,
		    FDAP_TOP = 0x00000001,
		}

		public struct COM_IFileDialog : Windows.COM_IUnknown
		{
			public static Guid sIID = .(0x42f85136, 0xdb7e, 0x439c, 0x85, 0xf1, 0xe4, 0x07, 0x5d, 0x13, 0x5f, 0xc8);
			public static Guid sCLSID = .(0xdc1c5a9c, 0xe88a, 0x4dde, 0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7);

			///s
			public enum FOS : uint32
			{
			    OVERWRITEPROMPT = 0x00000002,
			    STRICTFILETYPES = 0x00000004,
			    NOCHANGEDIR = 0x00000008,
			    PICKFOLDERS = 0x00000020,
			    FORCEFILESYSTEM = 0x00000040,
			    ALLNONSTORAGEITEMS = 0x00000080,
			    NOVALIDATE = 0x00000100,
			    ALLOWMULTISELECT = 0x00000200,
			    PATHMUSTEXIST = 0x00000800,
			    FILEMUSTEXIST = 0x00001000,
			    CREATEPROMPT = 0x00002000,
			    SHAREAWARE = 0x00004000,
			    NOREADONLYRETURN = 0x00008000,
			    NOTESTFILECREATE = 0x00010000,
			    HIDEMRUPLACES = 0x00020000,
			    HIDEPINNEDPLACES = 0x00040000,
			    NODEREFERENCELINKS = 0x00100000,
			    DONTADDTORECENT = 0x02000000,
			    FORCESHOWHIDDEN = 0x10000000,
			    DEFAULTNOMINIMODE = 0x20000000
			}

			public struct VTable : Windows.COM_IUnknown.VTable
			{
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, Windows.HWnd parent) Show;
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, uint cFileTypes, COMDLG_FILTERSPEC* rgFilterSpec) SetFileTypes;
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, uint iFileType) SetFileTypeIndex;
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out uint piFileType) GetFileTypeIndex;
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, COM_IFileDialogEvents* pfde, out uint pdwCookie) Advise;
			    public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, uint dwCookie) Unadvise;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, FOS fos) SetOptions;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out FOS pfos) GetOptions;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, COM_IShellItem* psi) SetDefaultFolder;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, COM_IShellItem* psi) SetFolder;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetFolder;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetCurrentSelection;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, char16* pszName) SetFileName;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out char16* pszName) GetFileName;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, char16* pszTitle) SetTitle;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, char16* pszText) SetOkButtonLabel;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, char16* pszLabel) SetFileNameLabel;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetResult;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, COM_IShellItem* psi, FDAP fdap) AddPlace;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, char16* pszDefaultExtension) SetDefaultExtension;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, int hr) Close;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, ref Guid guid) SetClientGuid;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self) ClearClientData;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileDialog* self, void* pFilter) SetFilter;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		public struct COM_IFileSaveDialog : COM_IFileDialog
		{
			public static new Guid sIID = .(0x84BCCD23, 0x5FDE, 0x4CDB, 0xAE, 0xA4, 0xAF, 0x64, 0xB8, 0x3D, 0x78, 0xAB);
			public static new Guid sCLSID = .(0xC0B4E2F3, 0xBA21, 0x4773, 0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B);
		}

		public struct COM_IFileOpenDialog : COM_IFileDialog
		{
			public static new Guid sIID = .(0xD57C7288, 0xD4AD, 0x4768, 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60);

			public struct VTable : COM_IFileDialog.VTable
			{
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileOpenDialog* self, out COM_IShellItemArray* ppenum) GetResults;
				public function [CallingConvention(.Stdcall)] HResult(COM_IFileOpenDialog* self, out COM_IShellItemArray* ppsai) GetSelectedItems;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetFileVersionInfoW(char16* lptstrFilename, uint32 dwHandle, uint32 dwLen, void* lpData);

		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetFileVersionInfoSizeW(char16* lptstrFilename, out uint32 lpdwHandle);

		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool VerQueryValueW(void* pBlock, char16* lpSubBlock, ref void* lplpBuffer, out int32 puLen);

		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 VerLanguageNameW(uint32  wLang, char16* szLang, uint32 cchLang);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetNamedSecurityInfoW(
		  char16*              pObjectName,
		  SE_OBJECT_TYPE       ObjectType,
		  SECURITY_INFORMATION SecurityInfo,
		  SID**					ppsidOwner,
		  SID**					ppsidGroup,
		  ACL**					ppDacl,
		  ACL**					ppSacl,
		  SECURITY_DESCRIPTOR* *ppSecurityDescriptor
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern void BuildExplicitAccessWithNameW(
		  EXPLICIT_ACCESS_W* pExplicitAccess,
		  char16*             pTrusteeName,
		  uint32              AccessPermissions,
		  ACCESS_MODE        AccessMode,
		  uint32              Inheritance
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 SetEntriesInAclW(
		  uint32              cCountOfExplicitEntries,
		  EXPLICIT_ACCESS_W* pListOfExplicitEntries,
		  ACL*               OldAcl,
		  ACL**           	 NewAcl
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 SetNamedSecurityInfoW(
		  char16*              pObjectName,
		  SE_OBJECT_TYPE       ObjectType,
		  SECURITY_INFORMATION SecurityInfo,
		  SID*                 psidOwner,
		  SID*                 psidGroup,
		  ACL*                 pDacl,
		  ACL*                 pSacl
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetExplicitEntriesFromAclW(
		  ACL*               	pacl,
		  uint32*	            pcCountOfExplicitEntries,
		  EXPLICIT_ACCESS_W** 	pListOfExplicitEntries
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetEffectiveRightsFromAclW(
		  ACL*         pacl,
		  TRUSTEE_W*   pTrustee,
		  uint32* 	   pAccessRights
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool LookupAccountSidW(
		  char16*      lpSystemName,
		  SID*          Sid,
		  char16*       Name,
		  uint32*       cchName,
		  char16*         ReferencedDomainName,
		  uint32*       cchReferencedDomainName,
		  int* 		    peUse
		);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ConvertStringSidToSidW(
		  char16* StringSid,
		  SID** Sid
		);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void LocalFree(void* ptr);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetTimeZoneInformation(out TimeZoneInformation dynamicTimeZoneInformation);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetDynamicTimeZoneInformation(out DynamicTimeZoneInformation dynamicTimeZoneInformation);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegOpenKeyExW(HKey hKey, char16* lpSubKey, uint32 ulOptions, uint32 samDesired, out HKey phkResult);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegOpenKeyExA(HKey hKey, char8* lpSubKey, uint32 ulOptions, uint32 samDesired, out HKey phkResult);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegCreateKeyExW(HKey hKey, char16* lpSubKey, uint32 reserved, char16* lpClass, uint32 dwOptions, uint32 samDesired,
			SecurityAttributes* lpSecurityAttributes, out HKey phkResult, uint32* lpdwDisposition);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegCreateKeyExA(HKey hKey, char8* lpSubKey, uint32 reserved, char8* lpClass, uint32 dwOptions, uint32 samDesired,
			SecurityAttributes* lpSecurityAttributes, out HKey phkResult, uint32* lpdwDisposition);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegCloseKey(HKey hKey);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegDeleteKeyW(HKey hKey, char16* lpSubKey);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegDeleteKeyA(HKey hKey, char8* lpSubKey);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegDeleteValueW(HKey hKey, char16* lpSubKey);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegDeleteValueA(HKey hKey, char8* lpSubKey);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegQueryValueExW(HKey hKey, char16* lpValueName, uint32* lpReserved, uint32* lpType, void* lpData, uint32* lpcbData);
		
		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegQueryValueExA(HKey hKey, char8* lpValueName, uint32* lpReserved, uint32* lpType, void* lpData, uint32* lpcbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegEnumValueW(HKey hKey, int32 dwIndex, char16* lpValueName, uint32* lpcchValueName, uint32* lpReserved, uint32* lpType, void* lpData, uint32* lpcbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegEnumValueA(HKey hKey, int32 dwIndex, char8* lpValueName, uint32* lpcchValueName, uint32* lpReserved, uint32* lpType, void* lpData, uint32* lpcbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegGetValueW(HKey hkey, char16* lpSubKey, char16* lpValue, uint32 dwFlags, uint32* pdwType, void* pvData, uint32* pcbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegGetValueA(HKey hkey, char8* lpSubKey, char8* lpValue, uint32 dwFlags, uint32* pdwType, void* pvData, uint32* pcbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegSetValueExW(HKey hkey, char16* lpValue, uint32 reserved, uint32 dwType, void* pvData, uint32 cbData);

		[Import("advapi32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 RegSetValueExA(HKey hkey, char8* lpValue, uint32 reserved, uint32 dwType, void* pvData, uint32 cbData);

		[Import("shell32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 SHGetSpecialFolderLocation(HWnd hwnd, int32 csidl, ref int ppidl);

		[Import("shell32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern bool SHGetPathFromIDList(int pidl, char8* path);

		[Import("shell32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int SHBrowseForFolder(ref BrowseInfo bi);

		[Import("shell32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern COM_IUnknown.HResult SHCreateItemFromParsingName(char16* pszPath, COM_IBindCtx *pbc, Guid riid, void **ppv);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetLastError();

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetExitCodeProcess(ProcessHandle process, out int32 exitCode);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetExitCodeThread(Handle process, out int32 exitCode);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 ResumeThread(Handle thread);

		[CLink, CallingConvention(.Stdcall)]
		public static extern ProcessHandle GetCurrentProcess();

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetCurrentProcessId();

		[CLink, CallingConvention(.Stdcall)]
		public static extern ProcessHandle OpenProcess(int32 desiredAccess, IntBool inheritHandle, int32 processId);

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle CreateRemoteThread(ProcessHandle process, SecurityAttributes* threadAttributes, int stackSize, void* startAddress, void* parameter, int32 creationFlags, int32* threadId);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ReadProcessMemory(ProcessHandle process, void* baseAddress, void* buffer, int size, int* numberOfBytesRead);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool WriteProcessMemory(ProcessHandle process, void* baseAddress, void* buffer, int size, int* numberOfBytesWritten);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void* GetProcAddress(HModule module, char8* procName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void* VirtualAllocEx(ProcessHandle process, void* address, int size, int32 allocationType, int32 protect);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void* VirtualFreeEx(ProcessHandle process, void* address, int size, int32 allocationType);

		[CLink, CallingConvention(.Stdcall)]
		public static extern FileHandle GetStdHandle(int32 stdHandle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle ShellExecuteW(Handle hwnd, char16* operation, char16* file, char16* parameters, char16* directory, int32 showCmd); 

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle ShellExecuteA(Handle hwnd, char8* operation, char8* file, char8* parameters, char8* directory, int32 showCmd); 

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ShellExecuteExA(ShellExecuteInfo* shellExecuteInfo); 

		[CLink, CallingConvention(.Stdcall)]
		public static extern void GetStartupInfoA(StartupInfo* startupInfo);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CreateProcessW(char16* applicationName, char16* commandLine, SecurityAttributes* processAttributes, SecurityAttributes* threadAttributes,
        	IntBool inheritHandles, int32 creationFlags, void* environment, char16* currentDirectory, StartupInfo* startupInfo, ProcessInformation* processInformation);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CreateProcessA(char8* applicationName, char8* commandLine, SecurityAttributes* processAttributes, SecurityAttributes* threadAttributes,
        	IntBool inheritHandles, int32 creationFlags, void* environment, char8* currentDirectory, StartupInfo* startupInfo, ProcessInformation* processInformation);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool TerminateProcess(ProcessHandle process);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CreatePipe(out Handle readPipe, out Handle writePipe, SecurityAttributes* pipeAttributes, int32 size);

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle CreateNamedPipeA(char8* lpName, uint32 dwOpenMode, uint32 dwPipeMode, uint32 nMaxInstances, uint32 nOutBufferSize, uint32 nInBufferSize,
   			uint32 nDefaultTimeOut, SecurityAttributes* lpSecurityAttributes);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ConnectNamedPipe(Handle handle, Overlapped* overlapped);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CallNamedPipeA(char8* name, void* inBuffer, int32 inBufferSize, void* outBuffer, int32 outBufferSize, int32* bytesRead, int32 timeOut);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool DisconnectNamedPipe(Handle handle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern EventHandle CreateEventA(SecurityAttributes* eventAttribuetes, IntBool manualReset, IntBool initialState, char8* name);

		[CLink, CallingConvention(.Stdcall)]
		public static extern EventHandle OpenEventA(uint32 dwDesiredAccess, IntBool bInheritHandle, char8* lpName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool SetEvent(EventHandle eventHandle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ResetEvent(EventHandle eventHandle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 WaitForSingleObject(Handle handle, int32 milliseconds);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 WaitForMultipleObjects(int32 count, Handle* handles, IntBool waitAll, int32 milliseconds);

		[CLink, CallingConvention(.Stdcall)]
        public static extern IOCompletionHandle CreateIoCompletionPort(FileHandle fileHandle, IOCompletionHandle existingCompletionPort, int completionKey, int32 numberOfConcurrentThreads);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetQueuedCompletionStatus(IOCompletionHandle completionPort, out int32 numberOfBytes, out int completionKey, out Overlapped* overlapped, int32 milliseconds);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool PostQueuedCompletionStatus(IOCompletionHandle completionPort, int32 numberOfBytes, int completionKey, Overlapped* overlapped);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool DuplicateHandle(Handle sourceProcessHandle, Handle sourceHandle, Handle targetProcessHandle, Handle* targetHandle, int32 desiredAccess, IntBool inheritHandle, int32 options);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CloseHandle(Handle handle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetFileType(FileHandle handle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetFileSize(FileHandle handle, int32* sizeHigh);

		[CLink, CallingConvention(.Stdcall)]
		public static extern FileHandle CreateFileA(char8* lpFileName,
		            int32 dwDesiredAccess, System.IO.FileShare dwShareMode,
		            SecurityAttributes* securityAttrs, System.IO.FileMode dwCreationDisposition,
		            int32 dwFlagsAndAttributes, Handle hTemplateFile);

		[CLink, CallingConvention(.Stdcall)]
		public static extern FileHandle CreateFileW(char16* lpFileName,
                    int32 dwDesiredAccess, System.IO.FileShare dwShareMode,
                    SecurityAttributes* securityAttrs, System.IO.FileMode dwCreationDisposition,
                    int32 dwFlagsAndAttributes, Handle hTemplateFile);

		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetWindowsDirectoryW(char16* lpBuffer, uint32 uSize);

		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetWindowsDirectoryA(char8* lpBuffer, uint32 uSize);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CreateDirectoryW(char16* pathName, SecurityAttributes* securityAttributes);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CreateDirectoryA(char8* pathName, SecurityAttributes* securityAttributes);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool RemoveDirectoryW(char16* pathName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool RemoveDirectoryA(char8* pathName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool DeleteFileW(char16* pathName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool DeleteFileA(char8* pathName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CopyFileW(char16* srcName, char16* dstName, IntBool failIfExists);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool CopyFileA(char8* srcName, char8* dstName, IntBool failIfExists);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool MoveFileW(char16* srcName, char16* dstName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool MoveFileA(char8* srcName, char8* dstName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 ReadFile(Handle handle, uint8* bytes, int32 numBytesToRead, out int32 numBytesRead, Overlapped* overlapped);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 WriteFile(Handle handle, uint8* bytes, int32 numBytesToWrite, out int32 numBytesWritten, Overlapped* overlapped);

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle OpenFileMappingA(uint32 dwDesiredAccess, IntBool bInheritHandle, char8* lpName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern Handle CreateFileMappingA(Handle hFile, SecurityAttributes* securityAttrs, uint32 flProtect, uint32 dwMaximumSizeHigh, uint32 dwMaximumSizeLow, char8* lpName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern void* MapViewOfFile(Handle hFileMappingObject, uint32 dwDesiredAccess, uint32 dwFileOffsetHigh, uint32 dwFileOffsetLow, int dwNumberOfBytesToMap);

		[CLink, CallingConvention(.Stdcall)]
		public static extern FindHandle FindFirstFileW(char16* fileName, ref NativeFindData findFileData);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool FindNextFileW(FindHandle findHandle, ref NativeFindData findFileData);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool FindClose(FindHandle findHandle);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetFileAttributesExW(char16* name, int32 fileInfoLevel, FileAttributeData* fileInformation);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool SetFileAttributesW(char16* name, int32 attribs);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 MessageBoxA(HWnd hWnd, char8* text, char8* caption, int32 type);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 MessageBoxW(HWnd hWnd, char16* text, char16* caption, int32 type);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 SetErrorMode(int32 errorMode);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd GetActiveWindow();

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd SetActiveWindow(HWnd wnd);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int CallWindowProcA(int wndProc, HWnd hWnd, int32 msg, int wParam, int lParam);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int CallWindowProcW(int wndProc, HWnd hWnd, int32 msg, int wParam, int lParam);

		public static int GetWindowLong(HWnd hWnd, int32 nIndex)
		{
#if BF_32_BIT			
			//if (sizeof(int) == 4)
			return (int)GetWindowLongA((int)hWnd, nIndex);
#else
				
			//else
			return GetWindowLongPtrW((int)hWnd, nIndex);
#endif
		}

#if BF_32_BIT
		[CLink, CallingConvention(.Stdcall)]
		public static extern int GetWindowLongA(int hWnd, int nIndex);
#else
		[CLink, CallingConvention(.Stdcall)]
		public static extern int GetWindowLongPtrW(int hWnd, int32 nIndex);
#endif

		public static int SetWindowLong(HWnd hWnd, int32 nIndex, int value)
		{
#if BF_32_BIT
				return (int)SetWindowLongA((int)hWnd, nIndex, value);
#else
				return SetWindowLongPtrW((int)hWnd, nIndex, value);
#endif
		}

#if BF_32_BIT
		[CLink, CallingConvention(.Stdcall)]
		public static extern int SetWindowLongA(int hWnd, int nIndex, int value);
#else
		[CLink, CallingConvention(.Stdcall)]
		public static extern int SetWindowLongPtrW(int hWnd, int32 nIndex, int value);
#endif

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool PostMessageW(HWnd hWnd, int32 msg, int wParam, int lParam);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 SendMessageW(HWnd hWnd, int32 msg, int wParam, int lParam);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 SendMessageTimeoutW(HWnd hWnd, int32 msg, int wParam, int lParam, int32 flags, int32 timeout, int32* result);

		[Import("user32.lib "), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd SetFocus(HWnd hWnd);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool EnumWindows(void* callback, void* extraData);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd GetWindow(HWnd hWnd, int32 uCmd);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd FindWindowW(char16* className, char16* windowName);
		
		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern HWnd FindWindowA(char8* className, char8* windowName);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool IsWindowVisible(HWnd hWnd);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetWindowTextLengthW(HWnd hWnd);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetWindowTextW(HWnd hWnd, char16* ptr, int32 length);
		
		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetWindowTextA(HWnd hWnd, char8* ptr, int32 length);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 GetWindowThreadProcessId(HWnd handle, out int32 processId);

		[Import("comdlg32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetOpenFileNameW(ref OpenFileName ofn);

		[Import("comdlg32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetSaveFileNameW(ref OpenFileName ofn);

		[CLink, CallingConvention(.Stdcall)]
		public static extern HModule GetModuleHandleW(char16* modName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern HModule GetModuleHandleA(char8* modName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetTempFileNameW(char16* tmpPath, char16* prefix, uint32 uniqueIdOrZero, char16* tmpFileName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetTempFileNameA(char8* tmpPath, char8* prefix, uint32 uniqueIdOrZero, char8* tmpFileName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern uint32 GetTempPathW(int32 bufferLen, char16* buffer);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetComputerNameA(char8* buffer, ref int32 size);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 NtQuerySystemInformation(int32 query, void* dataPtr, int32 size, out int32 returnedSize);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool ReadDirectoryChangesW(FileHandle handle, uint8* buffer, int32 bufferLength, IntBool watchSubtree, NotifyFilters notifyFilter, 
        	out int32 bytesReturned, Overlapped* overlapped, void* completionRoutine);

		[CLink, CallingConvention(.Stdcall)]
		public static extern int32 SetFilePointer(FileHandle handle, int32 distanceToMove, int32* distanceToMoveHigh, int32 moveMethod);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool SHGetFileInfoW(char16* pszPath, uint32 fileAttributes, SHFileInfo* psfi, uint32 cbFileInfo, uint32 flags);

		[CLink, CallingConvention(.Stdcall)]
		public static extern char16* GetEnvironmentStringsW();

		[CLink, CallingConvention(.Stdcall)]
		public static extern void FreeEnvironmentStringsW(char16* ptr);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool GetFileMUIPath(uint32 dwFlags, char16* pcwszFilePath, char16* pwszLanguage, uint32* pcchLanguage,
			char16* pwszFileMUIPath, uint32* pcchFileMUIPath, uint64* pululEnumerator);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool SetDllDirectoryW(char16* libFileName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern HInstance LoadLibraryW(char16* libFileName);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern HInstance LoadLibraryA(char8* libFileName);

		[CLink, CallingConvention(.Stdcall)]
		public static extern HInstance LoadLibraryExW(char16* libFileName, HModule hFile, uint32 dwFlags);
		
		[CLink, CallingConvention(.Stdcall)]
		public static extern HInstance LoadLibraryExA(char8* libFileName, HModule hFile, uint32 dwFlags);

		[CLink, CallingConvention(.Stdcall)]
		public static extern IntBool FreeLibrary(HModule module);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		public static extern int32 LoadStringW(HInstance hInstance, uint32 uID, char16* lpBuffer, int32 cchBufferMax);

		public static Result<FileHandle, FileOpenError> SafeCreateFile(String lpFileName,
                    int32 dwDesiredAccess, System.IO.FileShare dwShareMode,
                    SecurityAttributes* securityAttrs, System.IO.FileMode dwCreationDisposition,
                    int32 dwFlagsAndAttributes, Handle hTemplateFile)
        {
            FileHandle handle = CreateFileW(lpFileName.ToScopedNativeWChar!(), dwDesiredAccess, dwShareMode,
				securityAttrs, dwCreationDisposition,
                dwFlagsAndAttributes, hTemplateFile );

            if (handle.IsInvalid)
			{
				int32 lastErr = GetLastError();
				switch (lastErr)
				{
				case ERROR_FILE_NOT_FOUND:
					fallthrough;
				case ERROR_PATH_NOT_FOUND:
					return .Err(FileOpenError.NotFound);
				case ERROR_SHARING_VIOLATION:
					return .Err(FileOpenError.SharingViolation);
				default:
					return .Err(FileOpenError.Unknown);
				}
			}

            int32 fileType = GetFileType(handle);
            if (fileType != FILE_TYPE_DISK) 
            {
                //return new NotSupportedException(Environment.GetResourceString("NotSupported_FileStreamOnNonFiles"));
				return .Err(FileOpenError.NotFile);
            }

			//if (handle.IsInvalid)
            return handle;
        }
	}
}

#endif
