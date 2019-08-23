// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics;
using System.Collections.Generic;
using System.Threading;
using System.Text;

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
		internal abstract Result<DialogResult> RunFileDialog(ref Windows.OpenFileName ofn);

		protected override Result<DialogResult> RunDialog(Windows.HWnd hWndOwner)
		{
			return RunDialogOld(hWndOwner);
		}

		private const int32 FILEBUFSIZE = 8192;
		internal const int32 OPTION_ADDEXTENSION = (int32)0x80000000;

		internal int32 mOptions;
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
			DeleteAndNullify!(mFileNames);
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

		internal bool GetOption(int32 option)
		{
			return (mOptions & option) != 0;
		}

		internal void SetOption(int32 option, bool value)
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

		public static mixin Testie()
		{
			int a = 123;
			char16* buf;
			if (a == 0)
			{
				buf = null;
			}
			else
			{
				buf = new char16[123]* { ? };
				defer:mixin delete buf;
			}
			buf
		}

		private Result<DialogResult> RunDialogOld(Windows.HWnd hWndOwner)
		{
			//RunDialogTest(hWndOwner);
			
		    Windows.WndProc hookProcPtr = => StaticHookProc;
		    Windows.OpenFileName ofn = Windows.OpenFileName();

			char16[FILEBUFSIZE] char16Buffer = .(0, ?);

	        if (mFileNames != null)
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
	        	ofn.mTitle = mTitle.ToScopedNativeWChar!();
	        ofn.mFlags = Options | (Windows.OFN_EXPLORER | Windows.OFN_ENABLEHOOK | Windows.OFN_ENABLESIZING);
	        ofn.mHook = hookProcPtr;
			ofn.mCustData = (int)(void*)this;
	        ofn.mFlagsEx = Windows.OFN_USESHELLITEM;
	        if (mDefaultExt != null && AddExtension)
	            ofn.mDefExt = mDefaultExt;

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
					var ofn = (Windows.OpenFileName*)lparam;
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
	}

	class OpenFileDialog : FileDialog
	{
		public override void Reset()
		{
			base.Reset();
			SetOption(Windows.OFN_FILEMUSTEXIST, true);
		}

		public bool ReadOnlyChecked
		{
		    get
		    {
		        return GetOption(Windows.OFN_READONLY);
		    }
		    set
		    {
		        SetOption(Windows.OFN_READONLY, value);
		    }
		}

		public bool ShowReadOnly
		{
		    get
		    {
		        return !GetOption(Windows.OFN_HIDEREADONLY);
		    }
		    set
		    {
		        SetOption(Windows.OFN_HIDEREADONLY, !value);
		    }
		}

		internal override Result<DialogResult> RunFileDialog(ref Windows.OpenFileName ofn)
		{
			bool result = Windows.GetOpenFileNameW(ref ofn);
			if (!result)
				return .Err;

			if (!Multiselect)
			{
				let pathName = new String();
				UTF16.Decode(ofn.mFile, pathName);
				mFileNames = new String[](pathName);
				return DialogResult.OK;
			}

			int32 entryCount = 0;
			int32 prevNull = -1;
			for (int32 i = 0; true; i++)
			{
				if (ofn.mFile[i] == (char8)0)
				{
					if (prevNull == i - 1)
						break;
					prevNull = i;
					entryCount++;
				}
			}

			String pathName = null;
			prevNull = -1;
			mFileNames = new String[Math.Max(1, entryCount - 1)];
			entryCount = 0;
			for (int32 i = 0; true; i++)
			{
				if (ofn.mFile[i] == (char8)0)
				{
					if (prevNull == i - 1)
						break;
					if (prevNull == -1)
					{
						pathName = scope:: String();
						UTF16.Decode(ofn.mFile, pathName);
					}
					else
					{
						var str = new String(pathName.Length + 1 + i - prevNull - 1);
						str.Append(pathName);
						str.Append(Path.DirectorySeparatorChar);
						UTF16.Decode(ofn.mFile + prevNull + 1, str);

						mFileNames[entryCount++] = str;
					}
					prevNull = i;
				}
			}

			if ((entryCount == 0) && (pathName != null))
				mFileNames[0] = new String(pathName);

			return DialogResult.OK;
		}
	}
}

#endif