using System;
using System.Collections;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Beefy.theme.dark;
using System.Diagnostics;

namespace IDE.ui
{
    public class FileChangedDialog : DarkDialog
    {
		public enum DialogKind
		{
			Changed,
			Deleted,

			COUNT
		}

        public List<String> mFileNames = new List<String>() ~ DeleteContainerAndItems!(_);
        public bool?[(int)DialogKind.COUNT] mApplyAllResult;
		public DialogKind mDialogKind;

        public this()
        {

        }

		public ~this()
		{
			Debug.Assert(gApp.mFileChangedDialog != this);
			gApp.mFileChangedDialog = null;
		}

        public void Show(ContentPanel contentPanel)
        {
            String fileName = contentPanel.mFilePath;
            bool exists = File.Exists(fileName);
            mFileNames.Add(new String(fileName));

			if (var sourceViewPanel = contentPanel as SourceViewPanel)
			{
				if ((sourceViewPanel.mProjectSource != null) && (sourceViewPanel.mProjectSource.mEditData != null))
				{
					var editData = sourceViewPanel.mProjectSource.mEditData;
					editData.mFileDeleted = !exists;
				}
			}

            if (exists)
            {
				mDialogKind = .Changed;

                if (mFileNames.Count == 1)
                    Title = "File Changed";
                else
                    Title = "Files Changed";

                mText = new String();
                mText.AppendF("{0}\n\nThe file has unsaved changes inside this editor and has been changed externally.\nDo you want to reload it and lose the changes made in the source editor?", fileName);

                if (!mInPopupWindow)
                {
                    AddYesNoButtons(new (evt) =>
                    {
                        contentPanel.Reload();
                    }, new (evt) =>
                    {
						contentPanel.RefusedReload();
                    });
                }
            }
            else
            {
				mDialogKind = .Deleted;

                if (mFileNames.Count == 1)
                    Title = "File Deleted";
                else
                    Title = "Files Deleted";

				/*if (sourceViewPanel.mEditData != null)
				{
					sourceViewPanel.mEditData.mLastFileTextVersion = -1;
				}*/
				//sourceViewPanel.mEditData.mWasDeleted = true;

                mText = new String();
                mText.AppendF("{0}\n\nThe file is open in this editor and has been deleted externally.\nDo you want to close this file?", fileName);

                if (!mInPopupWindow)
                {
                    AddYesNoButtons(new (evt) =>
                    {
                        contentPanel.HandleFileDeleted();
                    });
                }
            }

            var button = AddButton("Yes To All");
            button.mOnMouseClick.Add(new (evt) =>
            {
				if (mDialogKind == .Deleted)
                	contentPanel.HandleFileDeleted();
				else
					contentPanel.Reload();
                mApplyAllResult[(int)mDialogKind] = true;
            });
            button = AddButton("No To All");
            button.mOnMouseClick.Add(new (evt) =>
            {
				if (mDialogKind == .Deleted)
				{
					// Nothing
				}
				else
					contentPanel.RefusedReload();
                mApplyAllResult[(int)mDialogKind] = false;
            });
            PopupWindow(IDEApp.sApp.mMainWindow);
        }

		public void Rehup()
		{
			base.Update();
			if (mDialogKind == .Deleted)
			{
				bool allFilesExist = true;
				for (var fileName in mFileNames)
				{
					allFilesExist &= File.Exists(fileName);
				}

				if (allFilesExist)
					Close();
			}
		}
    }
}
