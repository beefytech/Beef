// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Text;

#if BF_PLATFORM_WINDOWS

namespace System.IO
{
	class SaveFileDialog : FileDialog
	{
		public this()
		{
			//mOptions &= ~Windows.OFN_PATHMUSTEXIST;
		}

		public override void Reset()
		{
			base.Reset();
			mOptions = 0;
		}

		public virtual bool OverwritePrompt
		{
		    get
			{
		        return GetOption(Windows.OFN_OVERWRITEPROMPT);
		    }

		    set
			{
		        SetOption(Windows.OFN_OVERWRITEPROMPT, value);
		    }
		}

		protected override Result<DialogResult> RunFileDialog(ref Windows.OpenFileName ofn)
		{
			bool result = Windows.GetSaveFileNameW(ref ofn);
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

		protected override void ProcessVistaFiles(Windows.COM_IFileDialog* dialog, System.Collections.List<String> files)
		{
			mixin GetFilePathFromShellItem(Windows.COM_IShellItem* shellItem)
			{
				String str = null;
				if (shellItem.VT.GetDisplayName(shellItem, .FILESYSPATH, let cStr) == .OK)
				{
					str = new String()..Append(cStr);
					Windows.COM_IUnknown.CoTaskMemFree(cStr);
				}
				str
			}

			Windows.COM_IFileSaveDialog* saveDialog = (.)dialog;
			saveDialog.VT.GetResult(saveDialog, let shellItem);

			if (shellItem != null)
			{
				let filePath = GetFilePathFromShellItem!(shellItem);
				if (filePath != null)
					files.Add(filePath);
				shellItem.VT.Release(shellItem);
			}
		}

		protected override Result<Windows.COM_IFileDialog*> CreateVistaDialog()
		{
			Windows.COM_IFileDialog* fileDialog = null;

		    Windows.COM_IUnknown.HResult hr = (Windows.COM_IUnknown.CoCreateInstance(
		        ref Windows.COM_IFileSaveDialog.sCLSID,
		        null,
		        .INPROC_SERVER | .LOCAL_SERVER | .REMOTE_SERVER,
		        ref Windows.COM_IFileSaveDialog.sIID,
		        (void**)&fileDialog));
		    if (hr.Failed)
				return .Err;

		    return fileDialog;
		}
	}
}

#else

namespace System.IO
{
	[Error("This class is only available on Windows")]
	class SaveFileDialog
	{

	}
}

#endif