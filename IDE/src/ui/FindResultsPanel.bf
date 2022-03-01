using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using Beefy.utils;
using Beefy.theme.dark;
using System.Threading;
using Beefy;
using System.IO;
using System.Diagnostics;
using IDE.Util;

namespace IDE.ui
{    
    public class FindResultsPanel : OutputPanel
    {
        public static String sCurrentDocument = "Current Document";
		public static String sCurrentProject = "Current Project";
		public static String sUnlockedProjects = "Unlocked Projects";
        public static String sEntireSolution = "Entire Solution";
		public static String[] sLocationStrings = new .(sCurrentDocument, sCurrentProject, sUnlockedProjects, sEntireSolution) ~ delete _;

		class QueuedEntry
		{
			public String mFileName ~ delete _;
			public int32 mLine;
			public int32 mColumn;
			public String mText ~ delete _;
		}

        Queue<String> mPendingLines = new .() ~ DeleteContainerAndItems!(_);
		Queue<QueuedEntry> mQueuedEntries = new .() ~ DeleteContainerAndItems!(_);

		int32 mCurLineNum;
		HashSet<String> mFoundPathSet ~ DeleteContainerAndItems!(_);
        List<String> mSearchPaths ~ DeleteContainerAndItems!(_);
        SearchOptions mSearchOptions ~ delete _;
        Thread mSearchThread ~ delete _;
		Monitor mMonitor = new Monitor() ~ delete _;
		bool mCancelling;
        //bool mAbortSearch;

        public class SearchOptions
        {
            public String mSearchString ~ delete _;
			public String mReplaceString ~ delete _;
            public String mSearchLocation ~ delete _;
			public List<String> mFileTypes ~ DeleteContainerAndItems!(_);
			public bool mMatchCase;
			public bool mMatchWholeWord;
			public bool mRecurseDirectories;
			public bool mDeferredFindFiles;
        }

		// This is so we can remap to the correct source location even if we add or remove lines above the found line
		class LineSrcInfo
		{
			public FileEditData mEditData;
			public int32 mCharId;
			public int32 mLine;
			public int32 mLineChar;
		}
		List<LineSrcInfo> mLineSrcInfo = new List<LineSrcInfo>() ~ DeleteContainerAndItems!(_);
		bool mHasUnboundLineSrcInfos;
		int32 mLastEditDataRevision;

		class FindInfo
		{
			public String mContent ~ delete _;
		}

		Dictionary<String, FindInfo> mFindInfoMap = new Dictionary<String, FindInfo>() ~ { for (var kv in mFindInfoMap) delete kv.value; delete _; };
		List<String> mDeferredReplacePaths = new List<String>() ~ DeleteContainerAndItems!(_);

        public this()
        {
        	((OutputWidgetContent)mOutputWidget.mEditWidgetContent).mGotoReferenceEvent.Add(new => GotoRefrenceAtLine);
        }        

		bool GotoRefrenceAtLine(int line, int lineOfs)
		{
			if (line >= mLineSrcInfo.Count)
				return false;

			var lineSrcInfo = ref mLineSrcInfo[line];
			if (lineSrcInfo == null)
				return false;
			if (lineSrcInfo.mEditData.mEditWidget == null)
				return false;

			var editWidgetContent = lineSrcInfo.mEditData.mEditWidget.mEditWidgetContent;
			if (editWidgetContent == null)
				return false;

			int charIdx = editWidgetContent.mData.mTextIdData.GetPrepared().GetIndexFromId(lineSrcInfo.mCharId);
			if (charIdx == -1)
				return false;

			int remappedLine;
			int remappedLineChar;
			editWidgetContent.GetLineCharAtIdx(charIdx, out remappedLine, out remappedLineChar);

			var filePath = lineSrcInfo.mEditData.mFilePath;
			IDEApp.sApp.ShowSourceFileLocation(filePath, -1, -1/*IDEApp.sApp.mWorkspace.GetHighestCompileIdx()*/, remappedLine, remappedLineChar, LocatorType.Always);
			return true;
		}

		bool IsWordChar(char32 c32)
		{
			return c32.IsLetterOrDigit || (c32 == '_');
		}

		void SearchThread()
		{
		    int32 linesMatched = 0;
		    int32 filesMatched = 0;
		    int32 filesSearched = 0;

			if (mSearchOptions.mDeferredFindFiles)
			{
				AddFilesFromDirectory(mSearchOptions.mSearchLocation, mSearchOptions);
			}

			String line = scope String();

		    for (var filePath in mSearchPaths)
			FileBlock:
		    {
		        filesSearched++;

				if (mCancelling)
					break;

				StreamReader reader = null;

				String fileText = null;

				if (mSearchOptions.mSearchString.IsEmpty)
					break;

				FindInfo findInfo;
				if (mFindInfoMap.TryGetValue(filePath, out findInfo))
				{
					fileText = findInfo.mContent;
				}

				if (fileText != null)
				{
					let strStream = scope:FileBlock StringStream(fileText, .Reference);
					reader = scope:FileBlock StreamReader(strStream, UTF8Encoding.UTF8, false, 1024);
				}
				else
				{
					let streamReader = new StreamReader();
					if (streamReader.Open(filePath) case .Err(let errCode))
					{
						delete streamReader;
						if (errCode != .NotFound)
							QueueLine(scope String()..AppendF("Failed to open file: {0}", filePath));
						continue;
					}

					reader = streamReader;
					defer:FileBlock delete reader;
				}

				bool hasDeferredReplace = false;

		        if (reader != null)
		        {
					char8* searchPtr = mSearchOptions.mSearchString.Ptr;
					int searchLength = mSearchOptions.mSearchString.Length;

		            bool hadMatch = false;
		            int32 lineNum = 0;
		            while (!reader.EndOfStream)
		            {
						if (mCancelling)
							break;

						line.Clear();
		                reader.ReadLine(line);

						char8* linePtr = line.Ptr;

						bool lineMatched;
						if (mSearchOptions.mMatchWholeWord)
						{
							bool isNewStart = true;
							int lineIdx = 0;
							for (let c32 in line.DecodedChars)
							{
								if ((isNewStart) && (mSearchOptions.mMatchCase ?
									String.[Friend]EqualsHelper(linePtr + lineIdx, searchPtr, searchLength) :
									String.[Friend]EqualsIgnoreCaseHelper(linePtr + lineIdx, searchPtr, searchLength)))
								{
									int checkIdx = lineIdx + searchLength;
									if (checkIdx >= line.Length)
									{
										lineMatched = true;
										break;
									}

									char32 nextC = line.GetChar32(checkIdx).c;
									if (!IsWordChar(nextC))
									{
										lineMatched = true;
										break;
									}
								}	

								isNewStart = !IsWordChar(c32);
								lineIdx = @c32.NextIndex;
							}
						}
						else
							lineMatched = line.IndexOf(mSearchOptions.mSearchString, !mSearchOptions.mMatchCase) != -1;

		                if (lineMatched)
		                {
		                    linesMatched++;
		                    hadMatch = true;
							if (mSearchOptions.mReplaceString != null)
							{
								hasDeferredReplace = true;
								break;
							}
							else
		                    {
								const int maxLen = 4096;
								if (line.Length >= maxLen)
								{
									line.RemoveToEnd(maxLen);
									line.Append("...");
								}	
								for (var c in ref line.RawChars)
								{
									if (c.IsControl)
										c = ' ';
								}
								QueueLine(filePath, lineNum, 0, line);
							}
		                }

						lineNum++;
		            }

		            if (hadMatch)
		                filesMatched++;
		        }

   				if (hasDeferredReplace)
				{
					mDeferredReplacePaths.Add(filePath);
				}
		    }
		    
			String outLine = scope String();
			if (mSearchOptions.mReplaceString != null)
				outLine.AppendF("Replacing text in {0} file{1}. Total files searched: {2}", filesMatched, (filesMatched == 1) ? "" : "s", filesSearched);
			else
				outLine.AppendF("Matching lines: {0}  Matching files: {1}  Total files searched: {2}", linesMatched, filesMatched, filesSearched);
		    QueueLine(outLine);

			if (mCancelling)
				QueueLine("Search cancelled");
		}

		public bool IsSearching
		{
			get
			{
				return mSearchThread != null;
			}
		}

		public void CancelSearch()
		{
			mCancelling = true;
		}

        void StopSearch()
        {
            if (mSearchThread != null)
            {                
                mSearchThread.Join();
				DeleteAndNullify!(mSearchThread);
				DeleteAndNullify!(mSearchOptions);
				mPendingLines.ClearAndDeleteItems();
				mQueuedEntries.ClearAndDeleteItems();
				DeleteContainerAndItems!(mSearchPaths);
				mSearchPaths = null;
				DeleteContainerAndItems!(mFoundPathSet);
				mFoundPathSet = null;
				for (var kv in mFindInfoMap)
					delete kv.value;
				mFindInfoMap.Clear();
				mDeferredReplacePaths.Clear();
				mCancelling = false;
            }
        }

		bool PassesFilter(StringView fileName, SearchOptions searchOptions)
		{
			bool passes = false;

			if (searchOptions.mFileTypes.IsEmpty)
				passes = true;

			for (let fileType in searchOptions.mFileTypes)
			{
				if (fileType == "*")
				{
					passes = true;
					continue;
				}

				if (Path.WildcareCompare(fileName, fileType))
				{
					passes = true;
					break;
				}
			}

			return passes;
		}

        void AddFromFilesFolder(ProjectFolder projectFolder, SearchOptions searchOptions)
        {
            for (var projectEntry in projectFolder.mChildItems)
            {
                var childFolder = projectEntry as ProjectFolder;
                if (childFolder != null)
                    AddFromFilesFolder(childFolder, searchOptions);

                var projectSource = projectEntry as ProjectSource;
                if (projectSource != null)
                {
                    var path = scope String();
                    projectSource.GetFullImportPath(path);

					var upperPath = scope String(path);
					IDEUtils.FixFilePath(upperPath);
					if (!Environment.IsFileSystemCaseSensitive)
						upperPath.ToUpper();

                    if ((!mFoundPathSet.Contains(upperPath)) && (PassesFilter(path, searchOptions)))
                    {
						mSearchPaths.Add(new String(path));
						mFoundPathSet.Add(new String(upperPath));
					}
                }
            }
        }

		void AddFilesFromDirectory(StringView dirPath, SearchOptions searchOptions)
		{
			if (mCancelling)
				return;

			for (var fileEntry in Directory.EnumerateFiles(dirPath))
			{
				if (mCancelling)
					return;

				var fileName = scope String();
				fileEntry.GetFileName(fileName);
				if (PassesFilter(fileName, searchOptions))
				{
					var filePath = new String();
					fileEntry.GetFilePath(filePath);

					var upperPath = new String(filePath);
					IDEUtils.FixFilePath(upperPath);
					if (!Environment.IsFileSystemCaseSensitive)
						upperPath.ToUpper();
					if (mFoundPathSet.Add(upperPath))
					{
						mSearchPaths.Add(filePath);
					}
					else
					{
						delete upperPath;
						delete filePath;
					}
				}
			}

			if (searchOptions.mRecurseDirectories)
			{
				for (var dirEntry in Directory.EnumerateDirectories(dirPath))
				{
					if (mCancelling)
						return;

					var newDirPath = scope String();
					dirEntry.GetFilePath(newDirPath);
					AddFilesFromDirectory(newDirPath, searchOptions);
				}
			}
		}

        public void Search(SearchOptions searchOptions)
        {
            if (mSearchThread != null)
                StopSearch();

			mCancelling = false;
            Clear();
            QueueLine("Searching...");

			bool isReplace = searchOptions.mReplaceString != null;

			mSearchPaths = new List<String>();
			mFoundPathSet = new HashSet<String>();
			if (searchOptions.mSearchLocation == sEntireSolution)
			{
				for (var project in IDEApp.sApp.mWorkspace.mProjects)
				{
					if ((isReplace) && (project.mLocked))
						continue;

				    AddFromFilesFolder(project.mRootFolder, searchOptions);
				}
			}
			else if (searchOptions.mSearchLocation == sUnlockedProjects)
			{
				for (var project in IDEApp.sApp.mWorkspace.mProjects)
				{
					if (project.mLocked)
						continue;

				    AddFromFilesFolder(project.mRootFolder, searchOptions);
				}
			}
			else if (searchOptions.mSearchLocation == sCurrentDocument)
			{
				var sourceViewPanel = gApp.GetActiveSourceViewPanel(true);
				if (sourceViewPanel != null)
				{
					if (!sourceViewPanel.mEditWidget.mEditWidgetContent.CheckReadOnly())
						mSearchPaths.Add(new String(sourceViewPanel.mFilePath));
				}
			}
			else if (searchOptions.mSearchLocation == sCurrentProject)
			{
				var sourceViewPanel = gApp.GetActiveSourceViewPanel(true);
				if (sourceViewPanel != null)
				{
					if (sourceViewPanel.mProjectSource != null)
					{
						var project = sourceViewPanel.mProjectSource.mProject;
						if ((isReplace) && (project.mLocked))
						{
							QueueLine(scope String()..AppendF("ERROR: Project '{}' not processed for Find and Replace because it's locked", project.mProjectName));
						}
						else
							AddFromFilesFolder(project.mRootFolder, searchOptions);
					}
				}
			}
			else
			{
				searchOptions.mDeferredFindFiles = true;
			}

			for (var filePath in mSearchPaths)
			{
				String fileText = null;

				var editData = gApp.GetEditData(filePath, false, false);
				if (editData != null)
				{
					if (editData.mEditWidget != null)
					{
						fileText = new String();
						editData.mEditWidget.GetText(fileText);
					}
					else if (editData.mSavedContent != null)
					{
						fileText = new String();
						fileText.Append(editData.mSavedContent);
					}
				}

				if (fileText != null)
				{
					FindInfo findInfo = new FindInfo();
					findInfo.mContent = fileText;
					mFindInfoMap[filePath] = findInfo;
				}
			}

			if (searchOptions.mDeferredFindFiles)
			{
				String dirPath = scope String(searchOptions.mSearchLocation);
				IDEUtils.FixFilePath(dirPath);
				if (!dirPath.EndsWith(Path.DirectorySeparatorChar))
					dirPath.Append(Path.DirectorySeparatorChar);

				using (gApp.mMonitor.Enter())
				{
					for (let kv in gApp.mFileEditData)
					{
						if (kv.key.StartsWith(dirPath, Environment.IsFileSystemCaseSensitive ? .Ordinal : .OrdinalIgnoreCase))
						{
							let editData = kv.value;

							if (!PassesFilter(editData.mFilePath, searchOptions))
								continue;

							String fileText = null;
							if (editData.mEditWidget != null)
							{
								fileText = new String();
								editData.mEditWidget.GetText(fileText);
							}
							else if (editData.mSavedContent != null)
							{
								fileText = new String();
								fileText.Append(editData.mSavedContent);
							}

							if (fileText != null)
							{
								let filePath = new String(editData.mFilePath);
								FindInfo findInfo = new FindInfo();
								findInfo.mContent = fileText;
								mFindInfoMap[filePath] = findInfo;
								mSearchPaths.Add(filePath);

								mFoundPathSet.Add(new String(kv.key));
							}
						}
					}
				}
			}

            mSearchOptions = searchOptions;
            mSearchThread = new Thread(new => SearchThread);
            mSearchThread.Start(false);
        }

        public override void Serialize(StructuredData data)
        {            
            data.Add("Type", "FindResultsPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }
        
        public void QueueLine(String text)
        {
			mCurLineNum++;
            using (mMonitor.Enter())
                mPendingLines.Add(new String(text));
        }

		void RecordLineData(int32 ouputLineNum, FileEditData fileEditData)
		{

		}

		public void QueueLine(FileEditData fileEditData, int32 line, int32 lineChar, String lineStr)
		{
			RecordLineData(mCurLineNum, fileEditData);

			int charId = -1;
			if (fileEditData.mEditWidget != null)
			{
				let editWidgetContent = fileEditData.mEditWidget.mEditWidgetContent;
				int textIdx = editWidgetContent.GetTextIdx(line, lineChar);
				charId = editWidgetContent.mData.mTextIdData.GetIdAtIndex(textIdx);
			}
			else
			{
				mHasUnboundLineSrcInfos = true;
			}

			while (mCurLineNum >= mLineSrcInfo.Count)
				mLineSrcInfo.Add(null);
			var lineSrcInfo = new LineSrcInfo();
			lineSrcInfo.mCharId = (int32)charId;
			lineSrcInfo.mEditData = fileEditData;
			lineSrcInfo.mLine = line;
			lineSrcInfo.mLineChar = lineChar;
			mLineSrcInfo[mCurLineNum] = lineSrcInfo;

			String outStr = scope String();
			outStr.AppendF("{0}({1}):{2}", fileEditData.mFilePath, line + 1, lineStr);
			gApp.mFindResultsPanel.QueueLine(outStr);
		}

		public void QueueLine(String fileName, int32 line, int32 column, String text)
		{
			using (mMonitor.Enter())
			{
				QueuedEntry entry = new .();
				entry.mFileName = new .(fileName);
				entry.mLine = line;
				entry.mColumn = column;
				entry.mText = new .(text);
				mQueuedEntries.Add(entry);
			}
		}
        
        public override void Update()
        {
            base.Update();

			// Try to bind locations to charIds if new files have been loaded
			if ((mHasUnboundLineSrcInfos) && (mLastEditDataRevision != IDEApp.sApp.mFileDataDataRevision))
			{
				mHasUnboundLineSrcInfos = false;
				for (var lineSrcInfo in mLineSrcInfo)
				{
					if (lineSrcInfo == null)
						continue;
					if (lineSrcInfo.mCharId != -1)
						continue;
					
					if (lineSrcInfo.mEditData.mEditWidget == null)
					{
						mHasUnboundLineSrcInfos = true;
						continue;
					}

					let editWidgetContent = lineSrcInfo.mEditData.mEditWidget.mEditWidgetContent;
					int textIdx = editWidgetContent.GetTextIdx(lineSrcInfo.mLine, lineSrcInfo.mLineChar);
					lineSrcInfo.mCharId = (int32)editWidgetContent.mData.mTextIdData.GetIdAtIndex((int32)textIdx);
				}

				mLastEditDataRevision = IDEApp.sApp.mFileDataDataRevision;
			}

            using (mMonitor.Enter())
            {
				while (!mQueuedEntries.IsEmpty)
				{
					var entry = mQueuedEntries.PopFront();
					QueueLine(gApp.GetEditData(entry.mFileName, true, false), entry.mLine, entry.mColumn, entry.mText);
					delete entry;
				}

                if (mPendingLines.Count > 0)
                {
                    String sb = scope String();

                    while (mPendingLines.Count > 0)
                    {
						var pendingLine = mPendingLines.PopFront();
                        sb.Append(pendingLine, "\n");
                        delete pendingLine;
                    }

                    WriteSmart(sb);
                }
            }

			if ((mSearchThread != null) && (mSearchThread.Join(0)))
			{
				if (!mDeferredReplacePaths.IsEmpty)
				{
					GlobalUndoData globalUndoData = gApp.mGlobalUndoManager.CreateUndoData();

					int replaceCount = 0;

					for (var filePath in mDeferredReplacePaths)
					{
						int replaceInFileCount = 0;
						var editData = gApp.GetEditData(filePath);

						bool hasUnsavedChanges = editData.HasTextChanged();

						globalUndoData.mFileEditDatas.Add(editData);
						var sourceEditBatchHelper = scope:: SourceEditBatchHelper(editData.mEditWidget, "#renameInFiles", new GlobalUndoAction(editData, globalUndoData));

						bool matchCase = mSearchOptions.mMatchCase;

						let editWidgetContent = editData.mEditWidget.mEditWidgetContent;
						let data = editData.mEditWidget.mEditWidgetContent.mData;
						//let searchStrPtr = mSearchOptions.mSearchString.Ptr;

						bool isNewStart = true;
						//for (int i = 0; i < data.mTextLength - mSearchOptions.mSearchString.Length; i++)
						int i = 0;

						while (i <= data.mTextLength - mSearchOptions.mSearchString.Length)
						{
							if ((isNewStart) || (!mSearchOptions.mMatchWholeWord))
							{
								bool matches = true;
								int searchOfs = 0;
								while (searchOfs < mSearchOptions.mSearchString.Length)
								{
									var (checkC, charBytes) = editWidgetContent.GetChar32(i + searchOfs);

									char32 searchC = mSearchOptions.mSearchString.GetChar32(searchOfs).c;
									if (!matchCase)
									{
										checkC = checkC.ToUpper;
										searchC = searchC.ToUpper;
									}

									if (checkC != searchC)
									{
										matches = false;
										break;
									}

									searchOfs += charBytes;
								}

								if ((matches) && (mSearchOptions.mMatchWholeWord))
								{
									int nextIdx = i + mSearchOptions.mSearchString.Length;
									if (nextIdx < data.mTextLength)
									{
										var nextC = editWidgetContent.GetChar32(nextIdx).0;
										if (IsWordChar(nextC))
											matches = false;
									}
								}

								if (matches)
								{
									editWidgetContent.CursorTextPos = i;
									editWidgetContent.mSelection = EditSelection(i, i + mSearchOptions.mSearchString.Length);
									var insertTextAction = new EditWidgetContent.InsertTextAction(editWidgetContent, mSearchOptions.mReplaceString, .None);
									insertTextAction.mMoveCursor = false;
									editWidgetContent.mData.mUndoManager.Add(insertTextAction);

									editWidgetContent.PhysDeleteSelection(false);
									editWidgetContent.PhysInsertAtCursor(mSearchOptions.mReplaceString, false);
									i += mSearchOptions.mReplaceString.Length - 1;
									replaceCount++;
									replaceInFileCount++;
								}
							}

							var (c32, charBytes) = editWidgetContent.GetChar32(i);

							if (mSearchOptions.mMatchWholeWord)
								isNewStart = !IsWordChar(c32);
							i += charBytes;
						}

						sourceEditBatchHelper.Finish();

						if (!hasUnsavedChanges)
						{
							gApp.SaveFile(editData);
						}

						if ((IDEApp.IsBeefFile(filePath)) && (gApp.mBfResolveCompiler != null))
						{
							for (var projectSource in editData.mProjectSources)
								gApp.mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
							gApp.mBfResolveCompiler.QueueDeferredResolveAll();
						}

						var line = scope String();
						line.AppendF(" {0}: Replaced {1} instance{2}\n", filePath, replaceInFileCount, (replaceInFileCount == 1) ? "" : "s");
						Write(line);
					}

					if (replaceCount > 0)
					{
						Write("\n");
						base.Update();
						var ewc = (OutputWidgetContent)EditWidget.mEditWidgetContent;

						var actionButton = new UndoRenameActionButton();
						actionButton.mPanel = this;
						actionButton.mLine = ewc.GetLineCount() - 1;
						actionButton.Label = "Undo Replace";
						actionButton.mGlobalUndoData = globalUndoData;
						ewc.AddWidget(actionButton);
						ewc.mActionWidgets.Add(actionButton);
					}
				}

				StopSearch();
			}
        }

		class UndoRenameActionButton : OutputActionButton
		{
			public GlobalUndoData mGlobalUndoData;

			bool IsValid => ((!gApp.mGlobalUndoManager.mGlobalUndoDataList.IsEmpty) &&
				(gApp.mGlobalUndoManager.mGlobalUndoDataList.Back == mGlobalUndoData) &&
				(mGlobalUndoData.mUndoCount == 0));

			public override void Update()
			{
				base.Update();

				if (!IsValid)
					Close();
			}

			public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
			{
				base.MouseClicked(x, y, origX, origY, btn);
				if (!IsValid)
				{
					Close();
					return;
				}

				GlobalUndoAction undoAction = scope .(null, mGlobalUndoData);
				undoAction.Undo();
			}
		}


		public override void Clear()
		{
			base.Clear();
			ClearAndDeleteItems(mLineSrcInfo);
			mHasUnboundLineSrcInfos = false;
			mCurLineNum = 0;
		}

		public override bool HasAffinity(Widget otherPanel)
		{
			return base.HasAffinity(otherPanel) || (otherPanel is OutputPanel);
		}
    }
}
