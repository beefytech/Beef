// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics;
using System.Collections;

#if BF_PLATFORM_WINDOWS
namespace System.IO
{
	extension FileDialog
	{
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
#endif