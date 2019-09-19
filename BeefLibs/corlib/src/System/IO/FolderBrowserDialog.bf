
#if BF_PLATFORM_WINDOWS
namespace System.IO
{
	class FolderBrowserDialog : CommonDialog
	{
		String mSelectedPath = new String() ~ delete _;
		public bool ShowNewFolderButton;
		String mDescriptionText = new String() ~ delete _;
		bool mSelectedPathNeedsCheck;
		static FolderBrowserDialog sCurrentThis;

		public this()
		{
			Reset();
		}

		public StringView SelectedPath
		{
			get
			{
				return mSelectedPath;
			}

			set
			{
				mSelectedPath.Set(value);
			}
		}

		public StringView Description
		{
			get
			{
				return mDescriptionText;
			}

			set
			{
				mDescriptionText.Set(value);
			}
		}

		public void Reset()
		{
			mSelectedPath.Clear();
			mDescriptionText.Clear();
			mSelectedPathNeedsCheck = false;
			ShowNewFolderButton = true;
		}

		protected Result<DialogResult> RunDialog_New(Windows.HWnd hWndOwner, FolderBrowserDialog.COM_IFileDialog* fileDialog)
		{
			if (!mSelectedPath.IsEmpty)
			{
				COM_IShellItem* folderShellItem = null;
				Windows.SHCreateItemFromParsingName(mSelectedPath.ToScopedNativeWChar!(), null, COM_IShellItem.sIID, (void**)&folderShellItem);
				if (folderShellItem != null)
				{
					fileDialog.VT.SetDefaultFolder(fileDialog, folderShellItem);
					folderShellItem.VT.Release(folderShellItem);
				}
			}

			fileDialog.VT.SetOptions(fileDialog, .PICKFOLDERS);
			fileDialog.VT.Show(fileDialog, hWndOwner);

			DialogResult result = .Cancel;

			mSelectedPath.Clear();
			COM_IShellItem* shellItem = null;
			fileDialog.VT.GetResult(fileDialog, out shellItem);
			if (shellItem != null)
			{
				char16* cStr = null;
				if (shellItem.VT.GetDisplayName(shellItem, .FILESYSPATH, out cStr) == .OK)
				{
					let str = scope String..Append(cStr);
					mSelectedPath.Append(str);
					Windows.COM_IUnknown.CoTaskMemFree(cStr);
					result = .OK;
				}
				shellItem.VT.Release(shellItem);
			}

			fileDialog.VT.Release(fileDialog);
			return .Ok(result);
		}

		protected override Result<DialogResult> RunDialog(Windows.HWnd hWndOwner)
		{
			FolderBrowserDialog.COM_IFileDialog* fileDialog = null;
			let hr = Windows.COM_IUnknown.CoCreateInstance(ref FolderBrowserDialog.COM_IFileDialog.sCLSID, null, .INPROC_SERVER, ref FolderBrowserDialog.COM_IFileDialog.sIID, (void**)&fileDialog);
			if (hr == 0)
				return RunDialog_New(hWndOwner, fileDialog);

			int pidlRoot = 0;
			//Windows.SHGetSpecialFolderLocation(hWndOwner, (int32)mRootFolder, ref pidlRoot);
			if (pidlRoot == (int)0)
			{
				Windows.SHGetSpecialFolderLocation(hWndOwner, Windows.CSIDL_DESKTOP, ref pidlRoot);
				if (pidlRoot == (int)0)
					return .Err;
			}

			int32 mergedOptions = (int32)Windows.BrowseInfos.NewDialogStyle;
			if (!ShowNewFolderButton)
				mergedOptions |= (int32)Windows.BrowseInfos.HideNewFolderButton;

			String displayName = scope String(Windows.MAX_PATH);

			Windows.WndProc callback = => FolderBrowserDialog_BrowseCallbackProc;

			Windows.BrowseInfo bi;
			bi.mHWndOwner = hWndOwner;
			bi.mIdlRoot = pidlRoot;
			bi.mDisplayName = displayName;
			bi.mTitle = mDescriptionText;
			bi.mFlags = mergedOptions;
			bi.mCallback = callback;
			bi.mLParam = 0;
			bi.mImage = 0;

			sCurrentThis = this;
			int pidlRet = Windows.SHBrowseForFolder(ref bi);
			sCurrentThis = null;

			if (pidlRet == (int)0)
				return DialogResult.Cancel;
			
            char8* selectedPathCStr = scope char8[Windows.MAX_PATH]*;
			Windows.SHGetPathFromIDList(pidlRet, selectedPathCStr);
			mSelectedPath.Clear();
			mSelectedPath.Append(selectedPathCStr);

			return DialogResult.OK;
		}

		public static int FolderBrowserDialog_BrowseCallbackProc(Windows.HWnd hWnd, int32 msg, int wParam, int lParam)
		{
			switch (msg)
            {
                case Windows.BFFM_INITIALIZED: 
                    // Indicates the browse dialog box has finished initializing. The lpData value is zero. 
                    if (sCurrentThis.mSelectedPath.Length != 0) 
                    {
                        // Try to select the folder specified by selectedPath
                        Windows.SendMessageW(hWnd, Windows.BFFM_SETSELECTIONA, 1, (int)sCurrentThis.mSelectedPath.ToScopedNativeWChar!());
                    }
                    break;
                case Windows.BFFM_SELCHANGED: 
                    // Indicates the selection has changed. The lpData parameter points to the item identifier list for the newly selected item. 
                    int selectedPidl = lParam;
                    if (selectedPidl != (int)0)
                    {
                        char8* pszSelectedPath = scope char8[Windows.MAX_PATH]*;
                        // Try to retrieve the path from the IDList
                        bool isFileSystemFolder = Windows.SHGetPathFromIDList(selectedPidl, pszSelectedPath);
                        Windows.SendMessageW(hWnd, Windows.BFFM_ENABLEOK, 0, (int)(isFileSystemFolder ? 1 : 0));
                    }
                    break;
            }
            return 0;
		}

		struct COM_IFileDialogEvents
		{

		}

		struct COM_IShellItem : Windows.COM_IUnknown
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
				public function HResult(COM_IShellItem* self, void* pbc, ref Guid bhid, ref Guid riid, void** ppv) BindToHandler;
				public function HResult(COM_IShellItem* self, out COM_IShellItem* ppsi) GetParent;
				public function HResult(COM_IShellItem* self, SIGDN sigdnName, out char16* ppszName) GetDisplayName;
				public function HResult(COM_IShellItem* self, uint sfgaoMask, out uint psfgaoAttribs) GetAttributes;
				public function HResult(COM_IShellItem* self, COM_IShellItem* psi, uint32 hint, out int32 piOrder) Compare;

			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}

		internal struct COMDLG_FILTERSPEC
		{
		    internal char16* pszName;
		    internal char16* pszSpec;
		}

		internal enum FDAP : uint32
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
			    public function HResult(COM_IFileDialog* self, Windows.HWnd parent) Show;
			    public function HResult(COM_IFileDialog* self, uint cFileTypes, COMDLG_FILTERSPEC* rgFilterSpec) SetFileTypes;
			    public function HResult(COM_IFileDialog* self, uint iFileType) SetFileTypeIndex;
			    public function HResult(COM_IFileDialog* self, out uint piFileType) GetFileTypeIndex;
			    public function HResult(COM_IFileDialog* self, COM_IFileDialogEvents* pfde, out uint pdwCookie) Advise;
			    public function HResult(COM_IFileDialog* self, uint dwCookie) Unadvise;
				public function HResult(COM_IFileDialog* self, FOS fos) SetOptions;
				public function HResult(COM_IFileDialog* self, out FOS pfos) GetOptions;
				public function HResult(COM_IFileDialog* self, COM_IShellItem* psi) SetDefaultFolder;
				public function HResult(COM_IFileDialog* self, COM_IShellItem* psi) SetFolder;
				public function HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetFolder;
				public function HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetCurrentSelection;
				public function HResult(COM_IFileDialog* self, char16* pszName) SetFileName;
				public function HResult(COM_IFileDialog* self, out char16* pszName) GetFileName;
				public function HResult(COM_IFileDialog* self, char16* pszTitle) SetTitle;
				public function HResult(COM_IFileDialog* self, char16* pszText) SetOkButtonLabel;
				public function HResult(COM_IFileDialog* self, char16* pszLabel) SetFileNameLabel;
				public function HResult(COM_IFileDialog* self, out COM_IShellItem* ppsi) GetResult;
				public function HResult(COM_IFileDialog* self, COM_IShellItem* psi, FDAP fdap) AddPlace;
				public function HResult(COM_IFileDialog* self, char16* pszDefaultExtension) SetDefaultExtension;
				public function HResult(COM_IFileDialog* self, int hr) Close;
				public function HResult(COM_IFileDialog* self, ref Guid guid) SetClientGuid;
				public function HResult(COM_IFileDialog* self) ClearClientData;
				public function HResult(COM_IFileDialog* self, void* pFilter) SetFilter;
			}
			public new VTable* VT
			{
				get
				{
					return (.)mVT;
				}
			}
		}
	}	
}
#endif