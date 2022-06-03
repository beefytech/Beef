using Beefy.theme.dark;
using System;
using System.IO;
using System.Threading.Tasks;
using System.Threading;
using System.Collections;

namespace IDE.ui
{
	class PathEditWidget : ExpressionEditWidget
	{
		enum PathKind
		{
			Unknown,
			Folder,
			File
		}

		class DirectoryTask : Task
		{
			class Entry
			{
				public bool mIsDirectory;
				public String mFileName ~ delete _;
			}

			public PathEditWidget mPathEditWidget;
			public String mDirectoryPath = new .() ~ delete _;
			public List<Entry> mEntries = new .() ~ DeleteContainerAndItems!(_);

			public this() 
			{

			}

			protected override void InnerInvoke()
			{
				var searchStr = scope String();
				searchStr.Append(mDirectoryPath);
				searchStr.Append("/*");
				for (var dirEntry in Directory.Enumerate(searchStr, .Directories | .Files))
				{			
					var entry = new Entry();
					entry.mIsDirectory = dirEntry.IsDirectory;
					entry.mFileName = new String();
					dirEntry.GetFileName(entry.mFileName);
					mEntries.Add(entry);					
				}

				if (!mDetachState.HasFlag(.Deatched))
					mPathEditWidget.mAutocompleteDirty = true;
			}
		}

		DirectoryTask mDirectoryTask ~ { if (_ != null) _.Detach(); };
		bool mAutocompleteDirty;
		public String mRelPath ~ delete _;
		PathKind mPathKind;
		DarkButton mBrowseButton;
		public String mDefaultFolderPath ~ delete _;

		public this(PathKind pathKind = .Unknown)
		{
			mPathKind = pathKind;

			if (mPathKind != .Unknown)
			{
				mBrowseButton = new DarkButton();
				mBrowseButton.Label = "...";
				AddWidget(mBrowseButton);
				mBrowseButton.mOnMouseClick.Add(new (mouseArgs) =>
					{
						if (mPathKind == .Folder)
						{
							String path = scope .();
							GetText(path);

							if ((path.IsWhiteSpace) && (mDefaultFolderPath != null))
								path.Set(mDefaultFolderPath);
#if !CLI
							FolderBrowserDialog folderDialog = scope .();
							folderDialog.SelectedPath = path;
							mWidgetWindow.PreModalChild();
							if (folderDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
							{
								SetText(scope String()..Append(folderDialog.SelectedPath));
							}
#endif							
						}
						else if (mPathKind == .File)
						{
							String path = scope .();
							GetText(path);


							String dirPath = scope .();
							Path.GetDirectoryPath(path, dirPath).IgnoreError();
							if ((dirPath.IsWhiteSpace) && (mDefaultFolderPath != null))
								dirPath.Set(mDefaultFolderPath);

#if !CLI
							OpenFileDialog fileDialog = scope .();
							fileDialog.FileName = path;
							if (!dirPath.IsWhiteSpace)
								fileDialog.InitialDirectory = dirPath;
							mWidgetWindow.PreModalChild();
							if (fileDialog.ShowDialog(gApp.GetActiveWindow()).GetValueOrDefault() == .OK)
							{
								if (!fileDialog.FileNames.IsEmpty)
									SetText(scope String()..Append(fileDialog.FileNames[0]));
							}
#endif						
						}
					});
				mEditWidgetContent.mTextInsets.mRight += GS!(20);
			}
		}

		public override void UpdateText(char32 keyChar, bool doAutoComplete)
		{
			if (!doAutoComplete)
				return;

			var editText = scope String();
			GetText(editText);
			
			int cursorPos = mEditWidgetContent.CursorTextPos;
			int firstSlash = -1;
			int startSlash = -1;
			int nextSlash = -1;

			for (int i < editText.Length)
			{
				let c = editText[i];
				if ((c == '\\') || (c == '/'))
				{
					if (firstSlash == -1)
						firstSlash = i;
					if (cursorPos >= i)
					{
						startSlash = i;
					}
					else
					{
						if (nextSlash == -1)
							nextSlash = i;
					}
				}
			}

			var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
			
			if ((firstSlash != -1) && (keyChar != '\'') && (keyChar != '/'))
			{
				String selStr = scope String();
				if (nextSlash != -1)
					selStr.Append(editText, startSlash + 1, nextSlash - startSlash - 1);
				else
					selStr.Append(editText, startSlash + 1, Math.Max(cursorPos - startSlash - 1, 0));

				String info = scope .();
				info.Append("uncertain\n");
				info.AppendF("insertRange\t{0} {1}\n", startSlash + 1, cursorPos);
				//info.Append("directory\thi\n");
				//info.Append("directory\thi2\n");

				String subRelPath = scope String();
				if (startSlash != -1)
					subRelPath.Append(editText, 0, startSlash);
				var subAbsPath = scope String();
				if (mRelPath != null)
					Path.GetAbsolutePath(subRelPath, mRelPath, subAbsPath);
				else
					Path.GetAbsolutePath(subRelPath, gApp.mInstallDir, subAbsPath);

				if ((mDirectoryTask == null) || (mDirectoryTask.mDirectoryPath != subAbsPath))
				{
					if (mDirectoryTask != null)
					{
						mDirectoryTask.Detach();
						mDirectoryTask = null;
					}

					if (editWidgetContent.mAutoComplete != null)
						editWidgetContent.mAutoComplete.Close();

					

					mDirectoryTask = new DirectoryTask();
					mDirectoryTask.mPathEditWidget = this;
					mDirectoryTask.mDirectoryPath.Set(subAbsPath);
					mDirectoryTask.Start();
					return;
				}

				if (!mDirectoryTask.IsCompleted)
				{
					return;
				}

				for (var entry in mDirectoryTask.mEntries)
				{
					if (!entry.mFileName.StartsWith(selStr, .OrdinalIgnoreCase))
						continue;
					if (entry.mIsDirectory)
						info.AppendF("folder\t{0}\n", entry.mFileName);
					else
						info.AppendF("file\t{0}\n", entry.mFileName);
				}

				var autoComplete = GetAutoComplete();
				autoComplete.mUncertain = true;
				SetAutoCompleteInfo(info, 0);
			}
			else
			{
				if (mDirectoryTask != null)
				{
					mDirectoryTask.Detach();
					mDirectoryTask = null;
				}

				if (editWidgetContent.mAutoComplete != null)
					editWidgetContent.mAutoComplete.Close();
			}
		}

		public override void Update()
		{
			base.Update();
			if (mAutocompleteDirty)
			{
				UpdateText(0, true);
				mAutocompleteDirty = false;
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			let btnSize = (int)(height - GS!(4));
			if (mBrowseButton != null)
			{
				mBrowseButton.Resize(mWidth - btnSize - GS!(2), GS!(2), btnSize, btnSize);
			}
		}
	}

	class PathComboBox : DarkComboBox
	{
		public override bool WantsKeyHandling()
		{
			if (mEditWidget == null)
				return true;

			var expressionEditWidget = (ExpressionEditWidgetContent)mEditWidget.mEditWidgetContent;
			return expressionEditWidget.mAutoComplete == null;
		}
	}
}
