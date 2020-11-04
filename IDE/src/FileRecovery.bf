using System;
using System.Collections;
using System.Threading;
using System.Security.Cryptography;
using System.IO;
using Beefy;

namespace IDE
{
	class FileRecovery
	{
		public class Entry
		{
			public FileRecovery mFileRecovery;
			public String mPath ~ delete _;
			public String mContents ~ delete _;
			public String mRecoveryFileName ~ delete _;
			public MD5Hash mLastSavedHash;
			public MD5Hash mContentHash;
			public int mCursorPos;

			public this()
			{

			}

			public this(FileRecovery fileRecovery, StringView path, MD5Hash lastSavedHash, StringView contents, int cursorPos)
			{
				mFileRecovery = fileRecovery;
				mPath = new String(path);
				mContents = new String(contents);
				mLastSavedHash = lastSavedHash;
				mCursorPos = cursorPos;
				using (mFileRecovery.mMonitor.Enter())
				{
					mFileRecovery.mFileSet.Add(this);
					mFileRecovery.mDirty = true;
				}
			}

			public ~this()
			{
				if (mFileRecovery != null)
				{
					using (mFileRecovery.mMonitor.Enter())
					{
						mFileRecovery.mFileSet.Remove(this);
					}
				}
			}
		}

		Monitor mMonitor = new .() ~ delete _;
		HashSet<FileRecovery.Entry> mFileSet = new .() ~ DeleteContainerAndItems!(_);
		bool mDirty = false;
		WaitEvent mProcessingEvent ~ delete _;
		String mWorkspaceDir = new String() ~ delete _;
		bool mWantWorkspaceCleanup;

		public ~this()
		{
			if (mProcessingEvent != null)
				mProcessingEvent.WaitFor();
		}

		public void GetRecoveryFileName(StringView filePath, String recoveryFileName)
		{
			Path.GetFileNameWithoutExtension(filePath, recoveryFileName);
			recoveryFileName.Append("_");
			var hash = MD5.Hash(.((.)filePath.Ptr, filePath.Length));
			hash.ToString(recoveryFileName);
			Path.GetExtension(filePath, recoveryFileName);
		}

		public void Process()
		{
			defer mProcessingEvent.Set(true);

			HashSet<String> serializedFileNames = scope .();
			List<Entry> indexEntries = scope .();
			defer ClearAndDeleteItems(indexEntries);

			bool wantWorkspaceCleanup = false;
			using (mMonitor.Enter())
			{
				for (var entry in mFileSet)
				{
					if (entry.mRecoveryFileName == null)
					{
						entry.mRecoveryFileName = new String();
						GetRecoveryFileName(entry.mPath, entry.mRecoveryFileName);
					}

					if (entry.mContents != null)
					{
						entry.mContentHash = MD5.Hash(.((.)entry.mContents.Ptr, entry.mContents.Length));
						if (entry.mContentHash == entry.mLastSavedHash)
							continue;
					}

					if (entry.mRecoveryFileName != null)
						serializedFileNames.Add(entry.mRecoveryFileName);

					Entry indexEntry = new Entry();
					indexEntry.mPath = new String(entry.mPath);
					indexEntry.mRecoveryFileName = new String(entry.mRecoveryFileName);
					indexEntry.mLastSavedHash = entry.mLastSavedHash;
					indexEntry.mContents = entry.mContents;
					indexEntry.mContentHash = entry.mContentHash;
					indexEntry.mCursorPos = entry.mCursorPos;
					entry.mContents = null;
					indexEntries.Add(indexEntry);
				}

				wantWorkspaceCleanup = mWantWorkspaceCleanup;
				mDirty = false;
				mWantWorkspaceCleanup = false;
			}

			String recoverPath = scope String();
			recoverPath.Append(mWorkspaceDir);
			recoverPath.Append("/recovery");

			if (wantWorkspaceCleanup)
			{
				DateTime timeNow = DateTime.Now;
				for (var entry in Directory.EnumerateFiles(recoverPath))
				{
					String filePath = scope .();
					entry.GetFilePath(filePath);
					// Just being paranoid
					if ((!filePath.Contains('_')) || (filePath.Length < 33))
						continue;
					DateTime lastWriteTime = entry.GetLastWriteTime();
					if ((timeNow - lastWriteTime).TotalDays > 7)
						File.Delete(filePath).IgnoreError();
				}
			}

			if (mFileSet.IsEmpty)
				return;

			
			if (Directory.CreateDirectory(recoverPath) case .Err)
				return;

			indexEntries.Sort(scope (lhs, rhs) => lhs.mRecoveryFileName <=> rhs.mRecoveryFileName);

			String index = scope .();

			for (var indexEntry in indexEntries)
			{
				if (indexEntry.mContents != null)
				{
					String outPath = scope String()..AppendF("{}/{}", recoverPath, indexEntry.mRecoveryFileName);
					File.WriteAllText(outPath, indexEntry.mContents).IgnoreError();
				}

				index.AppendF("{}\t{}\t{}\t{}\n", indexEntry.mPath, indexEntry.mLastSavedHash, indexEntry.mContentHash, indexEntry.mCursorPos);
			}

			String indexPath = scope .();
			indexPath.AppendF("{}/index.txt", recoverPath);
			File.WriteAllText(indexPath, index).IgnoreError();
		}

		public void WorkspaceClosed()
		{
			if (!gApp.mWorkspace.IsInitialized)
				return;

			String indexPath = scope String();
			indexPath.Append(gApp.mWorkspace.mDir);
			indexPath.Append("/recovery/index.txt");
			File.Delete(indexPath).IgnoreError();
		}

		public void CheckWorkspace()
		{
			if (gApp.mSettings.mEditorSettings.mEnableFileRecovery != .Yes)
				return;

			mWantWorkspaceCleanup = true;

			String recoverPath = scope String();
			recoverPath.Append(gApp.mWorkspace.mDir);
			recoverPath.Append("/recovery");

			String indexPath = scope .();
			indexPath.AppendF("{}/index.txt", recoverPath);

			String index = scope .();
			File.ReadAllText(indexPath, index).IgnoreError();

			bool ReadFile(StringView path, String outText, out MD5Hash hash)
			{
				if (Utils.LoadTextFile(path, outText) case .Err)
				{
					hash = default;
					return false;
				}
				hash = MD5.Hash(.((.)outText.Ptr, outText.Length));
				return true;
			}

			for (var line in index.Split('\n'))
			{
				var lineEnum = line.Split('\t');
				StringView savedFilePath;
				StringView indexSavedHashStr;
				StringView indexRecoveryHashStr;
				StringView cursorPosStr;
				if (!(lineEnum.GetNext() case .Ok(out savedFilePath))) continue;
				if (!(lineEnum.GetNext() case .Ok(out indexSavedHashStr))) continue;
				if (!(lineEnum.GetNext() case .Ok(out indexRecoveryHashStr))) continue;
				if (!(lineEnum.GetNext() case .Ok(out cursorPosStr))) continue;

				MD5Hash indexSavedHash = MD5Hash.Parse(indexSavedHashStr).GetValueOrDefault();
				if (indexSavedHash.IsZero)
					continue;
				MD5Hash indexRecoveryHash = MD5Hash.Parse(indexRecoveryHashStr).GetValueOrDefault();
				if (indexRecoveryHash.IsZero)
					continue;

				MD5Hash savedHash;
				String savedFileContents = scope String();
				ReadFile(savedFilePath, savedFileContents, out savedHash);
				if (savedHash != indexSavedHash)
					continue;

				String recoveryFilePath = scope String()..AppendF("{}/", recoverPath);
				GetRecoveryFileName(savedFilePath, recoveryFilePath);
				MD5Hash recoveryFileHash;
				String recoveryFileContents = scope String();
				ReadFile(recoveryFilePath, recoveryFileContents, out recoveryFileHash);
				if (recoveryFileHash != indexRecoveryHash)
					continue;

				var sourceViewPanel = gApp.ShowSourceFile(scope String(savedFilePath));
				if (sourceViewPanel == null)
					continue;

				sourceViewPanel.mEditData.mRecoveryHash = savedHash;
				sourceViewPanel.mEditWidget.SetText(recoveryFileContents);
				sourceViewPanel.mEditWidget.mEditWidgetContent.CursorTextPos = int.Parse(cursorPosStr).GetValueOrDefault();
				sourceViewPanel.mEditWidget.mEditWidgetContent.EnsureCursorVisible();

				gApp.OutputLine("File recovered: {}", savedFilePath);
			}
		}

		public void Update()
		{
			if (mProcessingEvent != null)
			{
				if (!mProcessingEvent.WaitFor(0))
					return;
				DeleteAndNullify!(mProcessingEvent);
			}

			using (mMonitor.Enter())
			{
				if ((!mDirty) && (!mWantWorkspaceCleanup))
					return;
			}

			if (!gApp.mWorkspace.IsInitialized)
				return;

			mWorkspaceDir.Set(gApp.mWorkspace.mDir);
			mProcessingEvent = new WaitEvent();
			ThreadPool.QueueUserWorkItem(new => Process);
		}
	}
}
