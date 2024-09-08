
#if BF_PLATFORM_WINDOWS
namespace System.IO
{
	class FolderBrowserDialog : CommonDialog
	{
		public enum FolderKind
		{
			Open,
			Save
		}

		String mSelectedPath = new String() ~ delete _;
		public bool ShowNewFolderButton;
		String mDescriptionText = new String() ~ delete _;
		bool mSelectedPathNeedsCheck;
		static FolderBrowserDialog sCurrentThis;
		FolderKind mFolderKind;

		public this(FolderKind kind = .Open)
		{
			mFolderKind = kind;
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

		protected Result<DialogResult> RunDialog_New(Windows.HWnd hWndOwner, Windows.COM_IFileDialog* fileDialog)
		{
			//COM_IFileDialogEvents evts;
			/*COM_IFileDialogEvents.VTable funcs;
			funcs.QueryInterface = (self, riid, result) =>
				{
					return .E_FAIL;
				};
			funcs.AddRef = (self) =>
				{
					return .OK;
				};
			funcs.Release = (self) =>
				{
					return .OK;
				};
			funcs.OnFileOk = (self, fileDialog) => 
				{
					return .OK;
				};
			funcs.OnFolderChanging = (self, fileDialog, folder) => 
				{
					return .OK;
				};
			funcs.OnFolderChange = (self, fileDialog) => 
				{
					return .OK;
				};
			funcs.OnSelectionChange = (self, fileDialog) => 
				{
					return .OK;
				};
			funcs.OnShareViolation = (self, fileDialog, result) => 
				{
					return .OK;
				};
			funcs.OnTypeChange = (self, fileDialog) => 
				{
					return .OK;
				};
			funcs.OnOverwrite = (self, fileDialog, item, result) => 
				{
					return .OK;
				};
			evts.[Friend]mVT = &funcs;
			fileDialog.VT.Advise(fileDialog, &evts, var adviseCookie);*/

			if (!mSelectedPath.IsEmpty)
			{
				Windows.COM_IShellItem* folderShellItem = null;
				Windows.SHCreateItemFromParsingName(mSelectedPath.ToScopedNativeWChar!(), null, Windows.COM_IShellItem.sIID, (void**)&folderShellItem);
				if (folderShellItem != null)
				{
					fileDialog.VT.SetFolder(fileDialog, folderShellItem);
					folderShellItem.VT.Release(folderShellItem);
				}
			}

			fileDialog.VT.SetOptions(fileDialog, .PICKFOLDERS);
			fileDialog.VT.Show(fileDialog, hWndOwner);

			DialogResult result = .Cancel;

			mSelectedPath.Clear();
			Windows.COM_IShellItem* shellItem = null;
			fileDialog.VT.GetResult(fileDialog, out shellItem);
			if (shellItem != null)
			{
				char16* cStr = null;
				if (shellItem.VT.GetDisplayName(shellItem, .FILESYSPATH, out cStr) == .OK)
				{
					let str = scope String()..Append(cStr);
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
			Windows.COM_IFileDialog* fileDialog = null;
			Windows.COM_IUnknown.HResult hr;
			//if (mFolderKind == .Open)
				hr = Windows.COM_IUnknown.CoCreateInstance(ref Windows.COM_IFileDialog.sCLSID, null, .INPROC_SERVER, ref Windows.COM_IFileDialog.sIID, (void**)&fileDialog);
			//else
				//hr = Windows.COM_IUnknown.CoCreateInstance(ref FolderBrowserDialog.COM_FileSaveDialog.sCLSID, null, .INPROC_SERVER, ref FolderBrowserDialog.COM_FileSaveDialog.sIID, (void**)&fileDialog);
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
                        Windows.SendMessageW(hWnd, Windows.BFFM_SETSELECTIONA, 1, (int)(void*)sCurrentThis.mSelectedPath.ToScopedNativeWChar!());
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
	}	
}
#elif BF_PLATFORM_LINUX
namespace System.IO;

public class FolderBrowserDialog : CommonDialog
{
	public enum FolderKind
	{
		Open,
		Save
	}

	public this(FolderKind kind = .Open)
	{
		// Only OpenFile allows directory selection so FolderKind isn't stored
		Reset();
	}

	public StringView SelectedPath
	{
		get
		{
			return mDone ? (mFileNames == null) ? "" : mFileNames[0] : mInitialDir; //Response gets stored in mFileNames
		}
		set
		{
			mInitialDir.Set(value);
		}
	}

	protected override char8* Method => "OpenFile";
	protected override void AddOptions(Linux.DBusMsg* m)
	{
		Linux.SdBusMessageOpenContainer(m, .DictEntry, "sv");
		Linux.SdBusMessageAppend(m, "sv", "directory", "b", 1);
		Linux.SdBusMessageCloseContainer(m);
	}
}
#endif