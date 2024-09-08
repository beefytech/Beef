// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Text;
using System.Collections;
using System.Threading;
using System.Diagnostics;

#if BF_PLATFORM_WINDOWS
namespace System.IO
{
	enum DialogResult
	{
		None = 0,
		OK = 1,
		Cancel = 2
	}

	abstract class CommonDialog
	{
		public Windows.HWnd mHWnd;
		public Windows.HWnd mDefaultControlHwnd;
		public int mDefWndProc;

		private const int32 CDM_SETDEFAULTFOCUS = Windows.WM_USER + 0x51;

		public static Dictionary<int, CommonDialog> sHookMap = new Dictionary<int, CommonDialog>() ~
	        {
				Debug.Assert(sHookMap.Count == 0);
	            delete _;
			};
		public static Monitor sMonitor = new Monitor() ~ delete _;

		public Result<DialogResult> ShowDialog(INativeWindow owner = null)
		{
			Windows.HWnd hwndOwner = 0;
			if (owner != null)
				hwndOwner = (.)owner.Handle;
			//Native.WndProc wndProc = scope => OwnerWndProc;

			//mDefWndProc = Native.SetWindowLong(mHWnd, Native.GWL_WNDPROC, (intptr)wndProc.GetFuncPtr().Value);

			var result = RunDialog(hwndOwner);
			return result;
		}

		public virtual int OwnerWndProc(Windows.HWnd hWnd, int32 msg, int wParam, int lParam)
		{
			return Windows.CallWindowProcW(mDefWndProc, hWnd, msg, wParam, lParam);
		}

		protected virtual int HookProc(Windows.HWnd hWnd, int32 msg, int wParam, int lparam)
		{
			if (msg == Windows.WM_INITDIALOG)
			{
				//TODO: MoveToScreenCenter(hWnd);
				// Under some circumstances, the dialog
				// does not initially focus on any control. We fix that by explicitly
				// setting focus ourselves. See ASURT 39435.
				//
				mDefaultControlHwnd = (Windows.HWnd)wParam;
				if (mDefaultControlHwnd != 0)
					Windows.SetFocus(mDefaultControlHwnd);
			}
			else if (msg == Windows.WM_SETFOCUS)
			{
				Windows.PostMessageW(hWnd, CDM_SETDEFAULTFOCUS, 0, 0);
			}
			else if (msg == CDM_SETDEFAULTFOCUS)
			{
				// If the dialog box gets focus, bounce it to the default control.
				// so we post a message back to ourselves to wait for the focus change then push it to the default
				// control. See ASURT 84016.
				//
				if (mDefaultControlHwnd != 0)
					Windows.SetFocus(mDefaultControlHwnd);
			}
			return 0;
		}

		protected abstract Result<DialogResult> RunDialog(Windows.HWnd hWndOwner);
	}

	abstract class FileDialog : CommonDialog
	{
		protected abstract Result<DialogResult> RunFileDialog(ref Windows.OpenFileName ofn);

		protected override Result<DialogResult> RunDialog(Windows.HWnd hWndOwner)
		{
			if (TryRunDialogVista(hWndOwner) case .Ok(let result))
				return .Ok(result);

			return RunDialogOld(hWndOwner);
		}

		private const int32 FILEBUFSIZE = 8192;
		protected const int32 OPTION_ADDEXTENSION = (int32)0x80000000;

		protected int32 mOptions;
		private String mTitle ~ delete _;
		private String mInitialDir ~ delete _;
		private String mDefaultExt ~ delete _;
		protected String[] mFileNames ~ DeleteContainerAndItems!(_);
		private bool mSecurityCheckFileNames;
		private String mFilter ~ delete _;
		private String mFilterBuffer = new String() ~ delete _;
		private int32 mFilterIndex;
		private bool mSupportMultiDottedExtensions;
		private bool mIgnoreSecondFileOkNotification;  // Used for VS Whidbey 95342
		private int32 mOKNotificationCount;              // Same
		//private String char8Buffer = new String(FILEBUFSIZE) ~ delete _;

		public this()
		{
			Reset();
		}

		public virtual void Reset()
		{
			DeleteAndNullify!(mTitle);
			DeleteAndNullify!(mInitialDir);
			DeleteAndNullify!(mDefaultExt);
			DeleteContainerAndItems!(mFileNames);
			mFileNames = null;
			DeleteAndNullify!(mFilter);
			mFilterIndex = 1;
			mSupportMultiDottedExtensions = false;
			mOptions = Windows.OFN_HIDEREADONLY | Windows.OFN_PATHMUSTEXIST |
				OPTION_ADDEXTENSION;
		}



		protected int32 Options
		{
			get
			{
				return mOptions & (
					Windows.OFN_READONLY |
					Windows.OFN_HIDEREADONLY |
					Windows.OFN_NOCHANGEDIR |
					Windows.OFN_SHOWHELP |
					Windows.OFN_NOVALIDATE |
					Windows.OFN_ALLOWMULTISELECT |
					Windows.OFN_PATHMUSTEXIST |
					Windows.OFN_FILEMUSTEXIST |
					Windows.OFN_NODEREFERENCELINKS |
					Windows.OFN_OVERWRITEPROMPT);
				//return mOptions;
			}
		}

		public StringView Title
		{
			set
			{
				String.NewOrSet!(mTitle, value);
			}

			get
			{ 
		        return mTitle;
			}
		}

		public StringView InitialDirectory
		{
			set
			{
				String.NewOrSet!(mInitialDir, value);
			}

			get
			{ 
		        return mInitialDir;
			}
		}

		public String[] FileNames
		{
			get
			{
				return mFileNames;
			}
		}

		public StringView FileName
		{
			set
			{
				if (mFileNames == null)
				{
					mFileNames = new String[](new String(value));
				}	
			}
		}

		public bool AddExtension
		{
			get
			{
				return GetOption(OPTION_ADDEXTENSION);
			}

			set
			{
				SetOption(OPTION_ADDEXTENSION, value);
			}
		}

		public virtual bool CheckFileExists
		{
		    get
			{
		        return GetOption(Windows.OFN_FILEMUSTEXIST);
		    }

		    set
			{
		        SetOption(Windows.OFN_FILEMUSTEXIST, value);
		    }
		}

		public bool DereferenceLinks
		{
			get
			{
				return !GetOption(Windows.OFN_NODEREFERENCELINKS);
			}
			set
			{
				SetOption(Windows.OFN_NODEREFERENCELINKS, !value);
			}
		}

		public bool CheckPathExists
		{
		    get
			{
		        return GetOption(Windows.OFN_PATHMUSTEXIST);
		    }

		    set
			{
		        SetOption(Windows.OFN_PATHMUSTEXIST, value);
		    }
		}

		public bool Multiselect
		{
		    get
		    {
		        return GetOption(Windows.OFN_ALLOWMULTISELECT);
		    }

		    set
		    {
		        SetOption(Windows.OFN_ALLOWMULTISELECT, value);
		    }
		}

		public bool ValidateNames
		{
			get
			{
				return !GetOption(Windows.OFN_NOVALIDATE);
			}

			set
			{
				SetOption(Windows.OFN_NOVALIDATE, !value);
			}
		}		

		public StringView DefaultExt
		{
			get
			{
				return mDefaultExt == null ? "" : mDefaultExt;
			}

			set
			{
				delete mDefaultExt;
				mDefaultExt = null;

				//if (!String.IsNullOrEmpty(value))
				if (value.Length > 0)
				{					
					mDefaultExt = new String(value);
					if (mDefaultExt.StartsWith("."))
						mDefaultExt.Remove(0, 1);
				}				
			}
		}		

		public void GetFilter(String outFilter)
		{
			if (mFilter != null)
				outFilter.Append(mFilter);
		}

		public Result<void> SetFilter(StringView value)
		{
			String useValue = scope String(value);
			if (useValue != null && useValue.Length > 0)
			{
				var formats = String.StackSplit!(useValue, '|');
				if (formats == null || formats.Count % 2 != 0)
				{
					return .Err;
				}
				///
				/*String[] formats = value.Split('|');
				if (formats == null || formats.Length % 2 != 0)
				{
					throw new ArgumentException(SR.GetString(SR.FileDialogInvalidFilter));
				}*/
				String.NewOrSet!(mFilter, useValue);
			}
			else
			{
				useValue = null;
				DeleteAndNullify!(mFilter);
			}
			
			return .Ok;
		}

		protected bool GetOption(int32 option)
		{
			return (mOptions & option) != 0;
		}

		protected void SetOption(int32 option, bool value)
		{
			if (value)
			{
				mOptions |= option;
			}
			else
			{
				mOptions &= ~option;
			}
		}

		private static Result<void> MakeFilterString(String s, bool dereferenceLinks, String filterBuffer) 
	    {
			String useStr = s;
		    if (useStr == null || useStr.Length == 0)
		    {
				// Workaround for Whidbey bug #5165
				// Apply the workaround only when DereferenceLinks is true and OS is at least WinXP.
		        if (dereferenceLinks && System.Environment.OSVersion.Version.Major >= 5)
		        {
		            useStr = " |*.*";
		        }
		        else if (useStr == null)
		        {
		            return .Err;
		        }
		    }
			
			filterBuffer.Set(s);
		    for (int32 i = 0; i < filterBuffer.Length; i++)
		        if (filterBuffer[i] == '|')
	                filterBuffer[i] = (char8)0;
		    filterBuffer.Append((char8)0);
		    return .Ok;
		}

		private Result<DialogResult> RunDialogOld(Windows.HWnd hWndOwner)
		{
			//RunDialogTest(hWndOwner);
			
		    Windows.WndProc hookProcPtr = => StaticHookProc;
		    Windows.OpenFileName ofn = Windows.OpenFileName();

			char16[FILEBUFSIZE] char16Buffer = .(0, ?);

	        if (mFileNames != null && !mFileNames.IsEmpty)
	        {
				//int len = UTF16.GetEncodedLen(fileNames[0]);
				//char16Buffer = scope:: char16[len + 1]*;
				UTF16.Encode(mFileNames[0], (char16*)&char16Buffer, FILEBUFSIZE);
	        }
			// Degrade to the older style dialog if we're not on Win2K.
			// We do this by setting the struct size to a different value
			//

			if (Environment.OSVersion.Platform != System.PlatformID.Win32NT ||
				Environment.OSVersion.Version.Major < 5) {
				ofn.mStructSize = 0x4C;
			}
	        ofn.mHwndOwner = hWndOwner;
	        ofn.mHInstance = (Windows.HInstance)Windows.GetModuleHandleW(null);

			if (mFilter != null)
			{
				Try!(MakeFilterString(mFilter, this.DereferenceLinks, mFilterBuffer));
				ofn.mFilter = mFilterBuffer.ToScopedNativeWChar!::();
			}
	        ofn.nFilterIndex = mFilterIndex;
	        ofn.mFile = (char16*)&char16Buffer;
	        ofn.nMaxFile = FILEBUFSIZE;
			if (mInitialDir != null)
	        	ofn.mInitialDir = mInitialDir.ToScopedNativeWChar!::();
			if (mTitle != null)
	        	ofn.mTitle = mTitle.ToScopedNativeWChar!::();
	        ofn.mFlags = Options | (Windows.OFN_EXPLORER | Windows.OFN_ENABLEHOOK | Windows.OFN_ENABLESIZING);
	        ofn.mHook = hookProcPtr;
			ofn.mCustData = (int)Internal.UnsafeCastToPtr(this);
	        ofn.mFlagsEx = Windows.OFN_USESHELLITEM;
	        if (mDefaultExt != null && AddExtension)
	            ofn.mDefExt = mDefaultExt.ToScopedNativeWChar!::();

			DeleteContainerAndItems!(mFileNames);
			mFileNames = null;
			//Security checks happen here
	        return RunFileDialog(ref ofn);
		}

		static int StaticHookProc(Windows.HWnd hWnd, int32 msg, int wParam, int lparam)
		{
			if (msg == Windows.WM_INITDIALOG)
			{
				using (sMonitor.Enter())
				{
					var ofn = (Windows.OpenFileName*)(void*)lparam;
					sHookMap[(int)hWnd] = (CommonDialog)Internal.UnsafeCastToObject((void*)ofn.mCustData);
				}
			}

			CommonDialog dlg;
			using (sMonitor.Enter())
			{
				sHookMap.TryGetValue((int)hWnd, out dlg);
			}
			if (dlg == null)
				return 0;

			dlg.[Friend]HookProc(hWnd, msg, wParam, lparam);
			if (msg == Windows.WM_DESTROY)
			{
				using (sMonitor.Enter())
				{
					sHookMap.Remove((int)hWnd);
				}
			}
			return 0;
		}
		//TODO: Add ProcessFileNames for validation

		protected abstract Result<Windows.COM_IFileDialog*> CreateVistaDialog();

		private Result<DialogResult> TryRunDialogVista(Windows.HWnd hWndOwner)
		{
			Windows.COM_IFileDialog* dialog;
			if (!(CreateVistaDialog() case .Ok(out dialog)))
				return .Err;

			OnBeforeVistaDialog(dialog);
			dialog.VT.Show(dialog, hWndOwner);

			List<String> files = scope .();
			ProcessVistaFiles(dialog, files);

			DeleteContainerAndItems!(mFileNames);
			mFileNames = new String[files.Count];
			files.CopyTo(mFileNames);

			dialog.VT.Release(dialog);

			return .Ok(files.IsEmpty ? .Cancel : .OK);
		}

		private void OnBeforeVistaDialog(Windows.COM_IFileDialog* dialog)
		{
		    dialog.VT.SetDefaultExtension(dialog, DefaultExt.ToScopedNativeWChar!());

			if (mFileNames != null && !mFileNames.IsEmpty)
		    	dialog.VT.SetFileName(dialog, mFileNames[0].ToScopedNativeWChar!());

		    if (!String.IsNullOrEmpty(mInitialDir))
		    {
				Windows.COM_IShellItem* folderShellItem = null;
				Windows.SHCreateItemFromParsingName(mInitialDir.ToScopedNativeWChar!(), null, Windows.COM_IShellItem.sIID, (void**)&folderShellItem);
				if (folderShellItem != null)
				{
					dialog.VT.SetDefaultFolder(dialog, folderShellItem);
					dialog.VT.SetFolder(dialog, folderShellItem);
					folderShellItem.VT.Release(folderShellItem);
				}
		    }

		    dialog.VT.SetTitle(dialog, mTitle.ToScopedNativeWChar!());
		    dialog.VT.SetOptions(dialog, GetOptions());
		    SetFileTypes(dialog);
		}

		private Windows.COM_IFileDialog.FOS GetOptions()
		{
		    const Windows.COM_IFileDialog.FOS BlittableOptions =
		        Windows.COM_IFileDialog.FOS.OVERWRITEPROMPT
		      | Windows.COM_IFileDialog.FOS.NOCHANGEDIR
		      | Windows.COM_IFileDialog.FOS.NOVALIDATE
		      | Windows.COM_IFileDialog.FOS.ALLOWMULTISELECT
		      | Windows.COM_IFileDialog.FOS.PATHMUSTEXIST
		      | Windows.COM_IFileDialog.FOS.FILEMUSTEXIST
		      | Windows.COM_IFileDialog.FOS.CREATEPROMPT
		      | Windows.COM_IFileDialog.FOS.NODEREFERENCELINKS;

		    const int32 UnexpectedOptions =
		        (int32)(Windows.OFN_SHOWHELP // If ShowHelp is true, we don't use the Vista Dialog
		        | Windows.OFN_ENABLEHOOK 	 // These shouldn't be set in options (only set in the flags for the legacy dialog)
		        | Windows.OFN_ENABLESIZING 	 // These shouldn't be set in options (only set in the flags for the legacy dialog)
		        | Windows.OFN_EXPLORER); 	 // These shouldn't be set in options (only set in the flags for the legacy dialog)

		    Debug.Assert((UnexpectedOptions & mOptions) == 0, "Unexpected FileDialog options");

		    Windows.COM_IFileDialog.FOS ret = (Windows.COM_IFileDialog.FOS)mOptions & BlittableOptions;

		    // Force no mini mode for the SaveFileDialog
		    ret |= Windows.COM_IFileDialog.FOS.DEFAULTNOMINIMODE;

		    // Make sure that the Open dialog allows the user to specify
		    // non-file system locations. This flag will cause the dialog to copy the resource
		    // to a local cache (Temporary Internet Files), and return that path instead. This
		    // also affects the Save dialog by disallowing navigation to these areas.
		    // An example of a non-file system location is a URL (http://), or a file stored on
		    // a digital camera that is not mapped to a drive letter.
		    // This reproduces the behavior of the "classic" Open and Save dialogs.
		    ret |= Windows.COM_IFileDialog.FOS.FORCEFILESYSTEM;

		    return ret;
		}

		protected abstract void ProcessVistaFiles(Windows.COM_IFileDialog* dialog, List<String> files);

		private Result<void> SetFileTypes(Windows.COM_IFileDialog* dialog)
		{
		    List<Windows.COMDLG_FILTERSPEC> filterItems = scope .();

			// Expected input types
			// "Text files (*.txt)|*.txt|All files (*.*)|*.*"
			// "Image Files(*.BMP;*.JPG;*.GIF)|*.BMP;*.JPG;*.GIF|All files (*.*)|*.*"
			if (!String.IsNullOrEmpty(mFilter))
			{
			    StringView[] tokens = mFilter.Split!('|');
			    if (0 == tokens.Count % 2)
			    {
			        // All even numbered tokens should be labels
			        // Odd numbered tokens are the associated extensions
			        for (int i = 1; i < tokens.Count; i += 2)
			        {
			            Windows.COMDLG_FILTERSPEC ext;
			            ext.pszSpec = tokens[i].ToScopedNativeWChar!::(); // This may be a semicolon delimited list of extensions (that's ok)
			            ext.pszName = tokens[i - 1].ToScopedNativeWChar!::();
			            filterItems.Add(ext);
			        }
			    }
			}

			if (filterItems.IsEmpty)
				return .Ok;

		    Windows.COM_IUnknown.HResult hr = dialog.VT.SetFileTypes(dialog, (uint32)filterItems.Count, filterItems.Ptr);
		    if (hr.Failed)
				return .Err;

		    hr = dialog.VT.SetFileTypeIndex(dialog, (uint32)mFilterIndex);
		    if (hr.Failed)
				return .Err;

			return .Ok;
		}
	}
}
#elif BF_PLATFORM_LINUX
namespace System.IO;

enum DialogResult
{
	None = 2,
	OK = 0,
	Cancel = 1
}

abstract class CommonDialog
{
	protected Linux.DBus* mBus ~ Linux.SdBusUnref(_);
	protected Linux.DBusMsg* mRequest ~ Linux.SdBusMessageUnref(_);
	protected Linux.DBusErr mError ~ Linux.SdBusErrorFree(&_);
	protected String mTitle ~ delete _;
	protected String mInitialDir ~ delete _;
	protected String[] mFileNames ~ DeleteContainerAndItems!(_);
	protected bool mDone;

	private uint32 mResult;
	

	public virtual void Reset()
	{
		DeleteAndNullify!(mTitle);
		DeleteAndNullify!(mInitialDir);
		DeleteContainerAndItems!(mFileNames);
		mFileNames = null;
	}

	public StringView Title
	{
		set
		{
			String.NewOrSet!(mTitle, value);
		}

		get
		{ 
	        return mTitle;
		}
	}

	public Result<DialogResult> ShowDialog(INativeWindow owner = null)
	{
		TryC!(Linux.SdBusOpenUser(&mBus)); // Maybe keep the bus open while the program is running ?
		
		Linux.DBusMsg* call = ?;
		TryC!(Linux.SdBusNewMethodCall(
			mBus,
			&call,
			"org.freedesktop.portal.Desktop",
			"/org/freedesktop/portal/desktop",
			"org.freedesktop.portal.FileChooser",
			Method));

		Linux.SdBusMessageAppend(call, "ss", "", Title.ToScopeCStr!()); //TODO : set parent_window to X11/Wayland handle
		Linux.SdBusMessageOpenContainer(call, .Array, "{sv}");
		if(mInitialDir != null)
		{
			Linux.SdBusMessageOpenContainer(call, .DictEntry, "sv");
			Linux.SdBusMessageAppendBasic(call, .String, "current_folder");
			Linux.SdBusMessageOpenContainer(call, .Variant, "ay");
			Linux.SdBusMessageAppendArray(call, .Byte, mInitialDir.CStr(), (.)mInitialDir.Length);
			Linux.SdBusMessageCloseContainer(call);
			Linux.SdBusMessageCloseContainer(call);
		}
		AddOptions(call);
		Linux.SdBusMessageCloseContainer(call);
		TryC!(Linux.SdBusCall(mBus, call, uint32.MaxValue, &mError, &mRequest)); // TODO : change timeout

		Linux.SdBusMessageUnref(call);

		char8* path = ?;
		TryC!(Linux.SdBusMessageRead(mRequest, "o", &path));
		TryC!(Linux.SdBusMatchSignal(mBus,
                                  null,
                                  null,
                                  path,
                                  "org.freedesktop.portal.Request",
                                  "Response",
                                  => ParseResponse,
                                  Internal.UnsafeCastToPtr(this)
                                  ));

		while(!mDone)
		{
		    Linux.DBusMsg* m = ?;
			TryC!(Linux.SdBusWait(mBus, uint64.MaxValue));
			TryC!(Linux.SdBusProcess(mBus, &m));
		    Linux.SdBusMessageUnref(m);
		}

		return (DialogResult)mResult;
	}

	private static int32 ParseResponse(Linux.DBusMsg* response, void* ptr, Linux.DBusErr* error)
	{
		Self dia = (.)Internal.UnsafeCastToObject(ptr);

		char8* key = ?;

		Linux.SdBusMessageReadBasic(response, .UInt32, &dia.mResult);
		Linux.SdBusMessageEnterContainer(response, .Array, "{sv}");
		while(Linux.SdBusMessagePeekType(response, null, null) != 0)
		{
		    Linux.SdBusMessageEnterContainer(response, .DictEntry, "sv");
		    Linux.SdBusMessageReadBasic(response, .String, &key);
		    switch(StringView(key))
		    {
		    case "uris":
		        List<String> uris = scope .();
		        
		        Linux.SdBusMessageEnterContainer(response, .Variant, "as");
		        Linux.SdBusMessageEnterContainer(response, .Array, "s");
		        while(Linux.SdBusMessagePeekType(response, null, null) != 0)
		        {
		            char8* uri = ?;
		            Linux.SdBusMessageReadBasic(response, .String, &uri);
		            uris.Add(new .(StringView(uri+7))); // Removing the "file://" prefix
		        }
		        Linux.SdBusMessageExitContainer(response);
		        Linux.SdBusMessageExitContainer(response);

		        dia.mFileNames = new .[uris.Count];
		        uris.CopyTo(dia.mFileNames);
		    default:
		        Linux.SdBusMessageSkip(response, "v");
		    }
		    Linux.SdBusMessageExitContainer(response);
		}
		Linux.SdBusMessageExitContainer(response);

		dia.mDone = true;
		return 0;
	}

	protected abstract char8* Method { get; }
	protected abstract void AddOptions(Linux.DBusMsg* m);
}

public abstract class FileDialog : CommonDialog
{
	protected int32 mOptions;
	private String mFilter ~ delete _;

	public this()
	{
		Reset();
	}

	public override void Reset()
	{
		base.Reset();
		DeleteAndNullify!(mFilter);
	}

	public StringView InitialDirectory
	{
		set
		{
			String.NewOrSet!(mInitialDir, value);
		}

		get
		{ 
	        return mInitialDir;
		}
	}

	public String[] FileNames
	{
		get
		{
			return mFileNames;
		}
	}

	public StringView FileName
	{
		set
		{
			if (mFileNames == null)
			{
				mFileNames = new String[](new String(value));
			}	
		}
	}

	public bool Multiselect
	{
	    get
	    {
	        return GetOption(512);
	    }

	    set
	    {
	        SetOption(512, value);
	    }
	}

	public bool ValidateNames // Unused kept for compatibility
	{
		get
		{
			return !GetOption(256);
		}

		set
		{
			SetOption(256, !value);
		}
	}		

	public StringView DefaultExt { get; set; } // Unused kept for compatibility		 

	public void GetFilter(String outFilter)
	{
		if (mFilter != null)
			outFilter.Append(mFilter);
	}

	public Result<void> SetFilter(StringView value)
	{
		String useValue = scope String(value);
		if (useValue != null && useValue.Length > 0)
		{
			var formats = String.StackSplit!(useValue, '|');
			if (formats == null || formats.Count % 2 != 0)
			{
				return .Err;
			}
			///
			/*String[] formats = value.Split('|');
			if (formats == null || formats.Length % 2 != 0)
			{
				throw new ArgumentException(SR.GetString(SR.FileDialogInvalidFilter));
			}*/
			String.NewOrSet!(mFilter, useValue);
		}
		else
		{
			useValue = null;
			DeleteAndNullify!(mFilter);
		}
		
		return .Ok;
	}

	protected bool GetOption(int32 option)
	{
		return (mOptions & option) != 0;
	}

	protected void SetOption(int32 option, bool value)
	{
		if (value)
		{
			mOptions |= option;
		}
		else
		{
			mOptions &= ~option;
		}
	}

	protected override void AddOptions(Linux.DBusMsg* m)
	{
		if(Multiselect)
		{
			Linux.SdBusMessageOpenContainer(m, .DictEntry, "sv");
			Linux.SdBusMessageAppend(m, "sv", "multiple", "b", 1);
			Linux.SdBusMessageCloseContainer(m);
		}

		if(mFilter != null)
		{
			Linux.SdBusMessageOpenContainer(m, .DictEntry, "sv");
			Linux.SdBusMessageAppendBasic(m, .String, "filters");
			Linux.SdBusMessageOpenContainer(m, .Variant, "a(sa(us))");
			Linux.SdBusMessageOpenContainer(m, .Array, "(sa(us))");
			for(let filter in mFilter.Split('|'))
			{
				Linux.SdBusMessageOpenContainer(m, .Struct, "sa(us)");
				Linux.SdBusMessageAppendBasic(m, .String, filter.ToScopeCStr!());
				Linux.SdBusMessageOpenContainer(m, .Array, "(us)");
				@filter.MoveNext();
				for(let ext in @filter.Current.Split(';'))
				{
					Linux.SdBusMessageAppend(m, "(us)", 0, ext.ToScopeCStr!());
				}
				Linux.SdBusMessageCloseContainer(m);
				Linux.SdBusMessageCloseContainer(m);
			}
			Linux.SdBusMessageCloseContainer(m);
			Linux.SdBusMessageCloseContainer(m);
			Linux.SdBusMessageCloseContainer(m);
		}
	}
}
#endif