#if BF_PLATFORM_WINDOWS

namespace System.IO
{
	static class Shell
	{

		public struct COM_IPersist : Windows.COM_IUnknown
		{
			public struct VTable : COM_IUnknown.VTable
			{
				public function HResult(COM_IPersistFile* self, Guid* pClassID) GetClassID;
			}
		}

		public struct COM_IPersistFile : COM_IPersist
		{
			public static Guid sIID = .(0x0000010b, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);

			public struct VTable : COM_IPersist.VTable
			{
				public function HResult(COM_IPersistFile* self) IsDirty;
				public function HResult(COM_IPersistFile* self, char16* pszFileName) Load;
				public function HResult(COM_IPersistFile* self, char16* pszFileName, Windows.IntBool remember) Save;
				public function HResult(COM_IPersistFile* self, char16* pszFileName) SaveCompleted;
				public function HResult(COM_IPersistFile* self, char16* pszName) GetCurFile;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		public struct COM_IShellLink : Windows.COM_IUnknown
		{
			public static Guid sCLSID = .(0x00021401, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
			public static Guid sIID = .(0x000214F9, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);

			struct IDLIST;

			public struct VTable : Windows.COM_IUnknown.VTable
			{
				public function HResult(COM_IShellLink* self, char16* pszFile, int32 cch, Windows.NativeFindData* pfd, uint32 fFlags) GetPath;
				public function HResult(COM_IShellLink* self, IDLIST** ppidl) GetIDList;
				public function HResult(COM_IShellLink* self, IDLIST* pidl) SetIDList;
				public function HResult(COM_IShellLink* self, char16* pszName, int32 cch) GetDescription;
				public function HResult(COM_IShellLink* self, char16* pszName) SetDescription;
				public function HResult(COM_IShellLink* self, char16* pszDir, int32 cch) GetWorkingDirectory;
				public function HResult(COM_IShellLink* self, char16* pszDir) SetWorkingDirectory;
				public function HResult(COM_IShellLink* self, char16* pszArgs, int32 cch) GetArguments;
				public function HResult(COM_IShellLink* self, char16* pszArgs) SetArguments;
				public function HResult(COM_IShellLink* self, uint16 *pwHotkey) GetHotkey;
				public function HResult(COM_IShellLink* self, uint16 wHotkey) SetHotkey;
				public function HResult(COM_IShellLink* self, int32 *piShowCmd) GetShowCmd;
				public function HResult(COM_IShellLink* self, int32 iShowCmd) SetShowCmd;
				public function HResult(COM_IShellLink* self, char16* pszIconPath, int32 cch, int32 *piIcon) GetIconLocation;
				public function HResult(COM_IShellLink* self, char16* pszIconPath, int32 iIcon) SetIconLocation;
				public function HResult(COM_IShellLink* self, char16* pszPathRel, uint32 dwReserved) SetRelativePath;
				public function HResult(COM_IShellLink* self, Windows.HWnd hwnd, uint32 fFlags) Resolve;
				public function HResult(COM_IShellLink* self, char16* pszFile) SetPath;
			    
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		public enum ShellError
		{
			case AccessDenied;
			case UnknownError;

			public this(Windows.COM_IUnknown.HResult result)
			{
				switch (result)
				{
				case .E_ACCESSDENIED: this = .AccessDenied;
				default: this = .UnknownError;
				}
			}
		}

		public static Result<void, ShellError> CreateShortcut(StringView linkPath, StringView targetPath, StringView arguments, StringView workingDirectory, StringView description)
		{
			COM_IShellLink* shellLink = null;
			COM_IPersistFile* persistFile = null;

			defer
			{
				if (persistFile != null)
					persistFile.VT.Release(persistFile);
				if (shellLink != null)
					shellLink.VT.Release(shellLink);
			}

			mixin TryHR(Windows.COM_IUnknown.HResult result)
			{
				if (result != .OK)
				{
					return .Err(ShellError(result));
				}
			}

			TryHR!(Windows.COM_IUnknown.CoCreateInstance(ref COM_IShellLink.sCLSID, null, .INPROC_SERVER, ref COM_IShellLink.sIID, (void**)&shellLink));
			TryHR!(shellLink.VT.SetPath(shellLink, targetPath.ToScopedNativeWChar!()));
			if (!arguments.IsEmpty)
				TryHR!(shellLink.VT.SetArguments(shellLink, arguments.ToScopedNativeWChar!()));
			if (!workingDirectory.IsEmpty)
				TryHR!(shellLink.VT.SetWorkingDirectory(shellLink, workingDirectory.ToScopedNativeWChar!()));
			if (!description.IsEmpty)
				TryHR!(shellLink.VT.SetDescription(shellLink, description.ToScopedNativeWChar!()));
			TryHR!(shellLink.VT.QueryInterface(shellLink, ref COM_IPersistFile.sIID, (void**)&persistFile));
			TryHR!(persistFile.VT.Save(persistFile, linkPath.ToScopedNativeWChar!(), true));

			return .Ok;
		}
	}
}

#endif