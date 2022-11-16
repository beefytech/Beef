using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.events;
using IDE.Compiler;
using IDE.Util;
using Beefy.utils;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace IDE.ui
{
    public class SymbolReferenceHelper : Widget
    {
        public enum Kind
        {
            Rename,
            ShowFileReferences,
			FindAllReferences,
			GoToDefinition
        }

        struct ReplaceSpan
        {
            public int32 mSpanStart;
            public int32 mSpanLength;
        }

        class ReplaceSymbolData
        {
            public FileEditData mFileEditData;
            public List<ReplaceSpan> mSpans = new List<ReplaceSpan>() ~ delete _;
			public bool mIsLocked;
        }

		enum BackgroundKind
		{
			None,
			GettingSymbolReferences
		}

        public bool mInitialized;
        public bool mFailed;
		public bool mGettingSymbolInfo;
        public bool mStartedWork;
		public bool mStartingWork;
		public bool mWantsClose;
        public SourceViewPanel mSourceViewPanel;
		public bool mAwaitingGetSymbolInfo;
		bool mOwnsParser;
		BfParser mParser ~ if (mOwnsParser) delete _;
		BackgroundKind mBackgroundKind;
        ResolveParams mResolveParams = new ResolveParams() ~ delete _;
        int32 mCursorPos;
        double mVertPos;
        BfPassInstance mPassInstance;
        BfResolvePassData mResolvePassData;
		bool mUsingResolvePassData;
        String mNewReplaceStr = new String() ~ delete _;
        String mReplaceStr = new String() ~ delete _;
        String mOrigReplaceStr = new String() ~ delete _;
		String mModifiedParsers ~ delete _;
		bool mRenameHadChange;
        bool mIgnoreTextChanges;
        bool mTextChanged;
        int32 mStartIdx;
        int32 mEndIdx;
        int32 mLastDeletedIdx = -1;
        bool mSkipNextUpdate;
		bool mDoLock;
        bool mDidRevert;
		bool mClosed;
		bool mOverrideSpanLengths;

        int32 mUpdateTextCount = 0;
        public Kind mKind;

        List<ReplaceSymbolData> mUpdatingProjectSources ~ DeleteContainerAndItems!(_);
        
		public bool HasStarted
		{
			get
			{
				return mStartedWork;
			}
		}

		public bool IsStarting
		{
			get
			{
				return mStartingWork;
			}
		}
		
		public bool IsRenaming
		{
			get
			{
				return mKind == Kind.Rename;
			}
		}

		public bool IsLocked
		{
			get
			{
				return mDoLock;
			}
		}

		public this()
		{

			//Debug.WriteLine("SymbolReferenceHelper this {0}", this);
		}

		public ~this()
		{
			//Debug.WriteLine("SymbolReferenceHelper ~this {0}", this);
		}

        public void Init(SourceViewPanel sourceViewPanel, Kind kind)
        {
            mSourceViewPanel = sourceViewPanel;
            mKind = kind;
        
            mCursorPos = (int32)sourceViewPanel.mEditWidget.Content.CursorTextPos;
            mVertPos = sourceViewPanel.mEditWidget.mVertPos.mDest;

			mAwaitingGetSymbolInfo = true;
			CheckGetSymbolInfo((kind == .ShowFileReferences) || (kind == .GoToDefinition));
        }


		void WaitForResolvedAll()
		{
			while (!gApp.mBfResolveCompiler.HasResolvedAll())
			{
				gApp.mBfResolveCompiler.WaitForBackground();
				gApp.mBfResolveCompiler.Update();
			}
		}
		
		bool CheckGetSymbolInfo(bool force)
		{
			if (!mAwaitingGetSymbolInfo)
				return true;

			if (gApp.mBfResolveCompiler == null)
				return false;

			if (gApp.mBfResolveCompiler.IsPerformingBackgroundOperationHi())
				return false;

			if (!force)
			{
				if (!gApp.mBfResolveCompiler.HasResolvedAll())
					return false;
			}

			mAwaitingGetSymbolInfo = false;
			mSourceViewPanel.Classify((mKind == .GoToDefinition) ? .GoToDefinition : .GetSymbolInfo);
			mInitialized = true;
			mGettingSymbolInfo = true;
			return true;
		}

        public void SetSymbolInfo(String symbolInfo)
        {
			mGettingSymbolInfo = false;
            String foundStr = null;
            bool foundSymbol = false;
			bool isTypeRef = false;

            for (var infoLine in symbolInfo.Split('\n'))
            {
                var lineDataItr = infoLine.Split('\t');
				var dataType = lineDataItr.GetNext().Get();
				switch (dataType)
				{
	            case "insertRange":
					var dataItr = lineDataItr.GetNext().Get().Split(' ');
                    mStartIdx = int32.Parse(dataItr.GetNext().Get());
                    mEndIdx = int32.Parse(dataItr.GetNext().Get());
					Debug.Assert(mEndIdx >= mStartIdx);
                    if (mEndIdx > mStartIdx)
					{
						foundStr = scope:: String();
                        mSourceViewPanel.mEditWidget.Content.ExtractString(mStartIdx, mEndIdx - mStartIdx, foundStr);
					}
	            case "localId":
                    int32 localId = int32.Parse(lineDataItr.GetNext().Get());
                    mResolveParams.mLocalId = localId;
                    foundSymbol = true;
	            case "typeRef":
					mOverrideSpanLengths = true;
					isTypeRef = true;
                    mResolveParams.mTypeDef = new String(lineDataItr.GetNext().Get());
                    foundSymbol = true;
	            case "fieldRef":
                    mResolveParams.mTypeDef = new String(lineDataItr.GetNext().Get());
                    mResolveParams.mFieldIdx = int32.Parse(lineDataItr.GetNext().Get());
                    foundSymbol = true;
                case "methodRef", "ctorRef":
                    mResolveParams.mTypeDef = new String(lineDataItr.GetNext().Get());
                    mResolveParams.mMethodIdx = int32.Parse(lineDataItr.GetNext().Get());
                    foundSymbol = true;
					if (dataType == "ctorRef")
					{
						// For ctor refs, the method name is really 'this' so we can't rename it...
						if (mKind != .FindAllReferences)
							foundSymbol = false;
					}
                case "invokeMethodRef":
                    if (mResolveParams.mTypeDef == null)
                    {
                        mResolveParams.mTypeDef = new String(lineDataItr.GetNext().Get());
                        mResolveParams.mMethodIdx = int32.Parse(lineDataItr.GetNext().Get());
                        foundSymbol = true;
                    }
                case "propertyRef":
                    mResolveParams.mTypeDef = new String(lineDataItr.GetNext().Get());
                    mResolveParams.mPropertyIdx = int32.Parse(lineDataItr.GetNext().Get());
                    foundSymbol = true;
				case "typeGenericParam":
				    mResolveParams.mTypeGenericParamIdx = int32.Parse(lineDataItr.GetNext().Get());
				case "methodGenericParam":
				    mResolveParams.mMethodGenericParamIdx = int32.Parse(lineDataItr.GetNext().Get());
				case "namespaceRef":
					mResolveParams.mNamespace = new String(lineDataItr.GetNext().Get());
					foundSymbol = true;
				case "defLoc":
					StringView filePath = lineDataItr.GetNext().Get();
					int32 line = int32.Parse(lineDataItr.GetNext().Value);
					int32 lineChar = int32.Parse(lineDataItr.GetNext().Value);

					if (mKind == .GoToDefinition)
					{
					    mSourceViewPanel.RecordHistoryLocation();

						var usePath = scope String(filePath);
						if (usePath.StartsWith("$Emit$"))
							usePath.Insert("$Emit$".Length, "Resolve$");

					    var sourceViewPanel = gApp.ShowSourceFileLocation(usePath, -1, -1, line, lineChar, LocatorType.Smart, true);
					    sourceViewPanel.RecordHistoryLocation(true);
						Close();
						return;
					}

					if (mKind == .Rename)
					{
						let editData = gApp.GetEditData(scope String(filePath), false, false);
						if (editData != null)
						{
							for (var projectSource in editData.mProjectSources)
							{
								if (projectSource.mProject.mLocked)
								{
									gApp.Fail(scope String()..AppendF("Symbol definition is located in locked project '{}' and cannot be renamed until the lock is removed.", projectSource.mProject.mProjectName));
									mFailed = true;
									Close();
									return;
								}
							}

							if (editData.IsLocked())
							{
								gApp.Fail(scope String()..AppendF("Symbol definition is located in locked file '{}' and cannot be renamed until the lock is removed.", filePath));
								mFailed = true;
								Close();
								return;
							}
						}
					}
				}
            }

			if (mKind == .GoToDefinition)
			{
				gApp.Fail("Unable to locate definition");
				Close();
			}

            if ((!foundSymbol) || (foundStr == null))
            {
                if ((mKind == .Rename) || (mKind == .FindAllReferences))
                    IDEApp.sApp.Fail("Unable to locate symbol");
                mFailed = true;
				Close();
                return;
            }

			if (foundStr != null)
            	mReplaceStr.Set(foundStr);

			if ((isTypeRef) && (mReplaceStr.EndsWith("Attribute")))
			{
				mReplaceStr.RemoveFromEnd("Attribute".Length);
				mEndIdx = mStartIdx + (.)mReplaceStr.Length;
			}

			if ((mKind == .Rename) && (mEndIdx > 0))
			{
				var ewc = (SourceEditWidgetContent)mSourceViewPanel.mEditWidget.mEditWidgetContent;
				for (int i = mStartIdx; i < mEndIdx; i++)
				{
					ewc.mData.mText[i].mDisplayFlags |= (uint8)(SourceElementFlags.SymbolReference);
				}

				// We do the full-selection in the normal case, but if we've moved the cursor since starting the rename
				//  then that would have removed the selection anyway so we don't do the full-select in that case
				if (ewc.CursorTextPos == mCursorPos)
				{
					ewc.mSelection = EditSelection(mStartIdx, mEndIdx);
					ewc.CursorTextPos = mEndIdx;
				}
			}

            mNewReplaceStr.Set(mReplaceStr);
            mOrigReplaceStr.Set(mReplaceStr);
        }

		void PrintAllReferences()
		{
			gApp.ShowFindResults(false);

			gApp.mFindResultsPanel.Clear();
			gApp.mFindResultsPanel.QueueLine("Symbol results:");

			for (int32 sourceIdx = 0; sourceIdx < mUpdatingProjectSources.Count; sourceIdx++)            
			{
			    var replaceSymbolData = mUpdatingProjectSources[sourceIdx];
			    //var projectSource = replaceSymbolData.mProjectSource;
			    //var editData = IDEApp.sApp.GetEditData(replaceSymbolData.mProjectSource, true);
				var editData = replaceSymbolData.mFileEditData;

				List<int32> sortedIndices = scope List<int32>();

			    for (int32 i = 0; i < replaceSymbolData.mSpans.Count; i++)
			    {
			        int32 spanStart = replaceSymbolData.mSpans[i].mSpanStart;
			        //int32 spanLen = replaceSymbolData.mSpans[i].mSpanLength;
			        sortedIndices.Add(spanStart);
				}

				sortedIndices.Sort(scope (a, b) => b - a);

				int32 lineStart = 0;
				int32 lineNum = 0;
				bool wantsLine = false;
				
				var editWidgetContent = editData.mEditWidget.mEditWidgetContent;
				for (int32 idx < editWidgetContent.mData.mTextLength)
				{
					char8 c = editWidgetContent.mData.mText[idx].mChar;
					if (c == '\n')
					{
						if (wantsLine)
						{
							String lineStr = scope String();
							editWidgetContent.ExtractString(lineStart, idx - lineStart, lineStr);

							//String fileName = editData.mFilePath;
							//String fileName = scope String();
							//projectSource.GetFullImportPath(fileName);

							gApp.mFindResultsPanel.QueueLine(editData, lineNum, 0, lineStr);
							
							wantsLine = false;
						}
						lineNum++;
						lineStart = idx + 1;
					}

					if ((sortedIndices.Count > 0) && (idx == sortedIndices[sortedIndices.Count - 1]))
					{
						sortedIndices.PopBack();
						wantsLine = true;
					}
				}
				//editData.mEditWidget.Index
			}
		}

		void DoWork()
		{
			//TODO:
			//Thread.Sleep(2000);

			Debug.Assert(mStartingWork);

			var bfSystem = IDEApp.sApp.mBfResolveSystem;
			var bfCompiler = IDEApp.sApp.mBfResolveCompiler;

			bfSystem.Lock(2);
			mUsingResolvePassData = true;
			bfCompiler.GetSymbolReferences(mPassInstance, mResolvePassData, mModifiedParsers);
			mUsingResolvePassData = false;
			bfSystem.Unlock();
		}

        void StartWork()
        {
            var bfSystem = IDEApp.sApp.mBfResolveSystem;
            var bfCompiler = IDEApp.sApp.mBfResolveCompiler;

			if (mGettingSymbolInfo)
			{
				gApp.Fail("Cannot rename symbols here");
				mGettingSymbolInfo = false;
				return;
			}

			Debug.Assert(!mGettingSymbolInfo);

			StopWork();
			
			mStartedWork = true;
			mStartingWork = true;

			Debug.Assert(mParser == null);
            if ((mKind == Kind.ShowFileReferences) || (mResolveParams.mLocalId != -1))
            {                
                mParser = IDEApp.sApp.mBfResolveSystem.FindParser(mSourceViewPanel.mProjectSource);
				if ((mResolveParams != null) && (mResolveParams.mLocalId != -1))
					mParser.SetAutocomplete(mCursorPos);
				else
					mParser.SetAutocomplete(-1);
            }
			else
			{
                mParser = new BfParser(null);
				mOwnsParser = true;
			}

            mPassInstance = bfSystem.CreatePassInstance();
            mResolvePassData = mParser.CreateResolvePassData(((mKind == Kind.Rename) || (mKind == Kind.FindAllReferences)) ? ResolveType.RenameSymbol : ResolveType.ShowFileSymbolReferences);
            var resolveParams = mResolveParams;
            if (resolveParams != null)
            {                
                if (resolveParams.mTypeDef != null)
                    mResolvePassData.SetSymbolReferenceTypeDef(resolveParams.mTypeDef);
                if (resolveParams.mFieldIdx != -1)
                    mResolvePassData.SetSymbolReferenceFieldIdx(resolveParams.mFieldIdx);
                if (resolveParams.mMethodIdx != -1)
                    mResolvePassData.SetSymbolReferenceMethodIdx(resolveParams.mMethodIdx);
                if (resolveParams.mPropertyIdx != -1)
                    mResolvePassData.SetSymbolReferencePropertyIdx(resolveParams.mPropertyIdx);
                if (resolveParams.mLocalId != -1)
                    mResolvePassData.SetLocalId(resolveParams.mLocalId);
				if (resolveParams.mTypeGenericParamIdx != -1)
					mResolvePassData.SetTypeGenericParamIdx(resolveParams.mTypeGenericParamIdx);
				if (resolveParams.mMethodGenericParamIdx != -1)
					mResolvePassData.SetMethodGenericParamIdx(resolveParams.mMethodGenericParamIdx);
				if (resolveParams.mNamespace != null)
					mResolvePassData.SetSymbolReferenceNamespace(resolveParams.mNamespace);
            }

			mDoLock = mKind == Kind.Rename;

			Debug.Assert(mModifiedParsers == null);
			mModifiedParsers = new String();

			bool doBackground = true;

			if (doBackground)
			{
				bfCompiler.DoBackground(new => DoWork, new => FinishStartingWork);
			}
			else
			{
				//TODO: This never locked for find all references.  Why not?
				bfCompiler.GetSymbolReferences(mPassInstance, mResolvePassData, mModifiedParsers);
				FinishStartingWork();
			}
        }

		void FinishStartingWork()
		{
			var bfSystem = IDEApp.sApp.mBfResolveSystem;

			if (mDoLock)
				bfSystem.Lock(2);

			Debug.Assert(!mUsingResolvePassData);
			Debug.Assert(mStartingWork);
			mStartingWork = false;

			mUpdatingProjectSources = new List<ReplaceSymbolData>();
			for (var parserDataStr in mModifiedParsers.Split('\n'))
			{
			    if (parserDataStr == "")
			        break;
			    var parserData = parserDataStr.Split('\t');
			    //var projectSource = IDEApp.sApp.FindProjectSourceItem(parserData[0]);
				var filePath = parserData.GetNext().Get();

				if ((mKind == .ShowFileReferences) && (!Path.Equals(filePath, mParser.mFileName)))
				{
					//TODO: Assert?!
					continue;
				}

			    ReplaceSymbolData replaceSymbolData = new ReplaceSymbolData();
				var editData = gApp.GetEditData(scope String(filePath));
				replaceSymbolData.mFileEditData = editData;
			    //replaceSymbolData.mProjectSource = projectSource;

				if (mKind == .Rename)
				{
					replaceSymbolData.mIsLocked = editData.IsLocked();
				}

				var spanData = parserData.GetNext().Get().Split(' ');
				int count = 0;
				while (true)
				{
					if (spanData.GetNext() case .Err)
						break;
					count++;
				}
				spanData.Reset();
				count /= 2;

				replaceSymbolData.mSpans.Capacity = count;
				for (int i < count)
				{
				    ReplaceSpan replaceSpan;
				    replaceSpan.mSpanStart = int32.Parse(spanData.GetNext().Get());
				    replaceSpan.mSpanLength = int32.Parse(spanData.GetNext().Get());
					if (mOverrideSpanLengths)
						replaceSpan.mSpanLength = (.)mReplaceStr.Length;
				    replaceSymbolData.mSpans.Add(replaceSpan);
				}

			    replaceSymbolData.mSpans.Sort(scope (a, b) => a.mSpanStart - b.mSpanStart);
			    for (int32 i = 1; i < replaceSymbolData.mSpans.Count; i++)
			    {
			        if (replaceSymbolData.mSpans[i - 1].mSpanStart == replaceSymbolData.mSpans[i].mSpanStart)
			        {
			            replaceSymbolData.mSpans.RemoveAt(i);
			            i--;
			        }
			    }

			    mUpdatingProjectSources.Add(replaceSymbolData);
			}

			if ((mKind == .Rename) && (mUpdatingProjectSources.IsEmpty))
			{
				gApp.Fail("Cannot rename element");
				Close();
				return;
			}

			if (mKind == Kind.FindAllReferences)
			{
				PrintAllReferences();
				Close();
				return;
			}

			UpdateText();
		}

		public void EnsureWorkStarted()
		{
			if (mAwaitingGetSymbolInfo)
			{
				WaitForResolvedAll();
				if (!CheckGetSymbolInfo(false))
					return;
				mSourceViewPanel.[Friend]ProcessDeferredResolveResults(-1);
				StartWork();
			}

			if (!mStartingWork)
				return;

			var bfCompiler = IDEApp.sApp.mBfResolveCompiler;
			bfCompiler.WaitForBackground();
			Debug.Assert(!mStartingWork);
		}

        void UpdateText()
        {
            if (mUpdatingProjectSources == null)
                return;
			if (mClosed)
				return;

            mIgnoreTextChanges = true;
            
			String prevReplaceStr = scope String();
			prevReplaceStr.Set(mReplaceStr);

            int32 strLenDiff = (int32)(mNewReplaceStr.Length - mReplaceStr.Length);
            mReplaceStr.Set(mNewReplaceStr);

			var activeSourceEditWidgetContent = (SourceEditWidgetContent)mSourceViewPanel.mEditWidget.Content;

            String newStr = mReplaceStr;
            bool didUndo = false;

			if (mKind == .Rename)
			{
				if (!mRenameHadChange)
				{
					if (newStr != mOrigReplaceStr)
						mRenameHadChange = true;
				}
			}

			GlobalUndoData globalUndoData = null;
			if (mKind == Kind.Rename)
			    globalUndoData = IDEApp.sApp.mGlobalUndoManager.CreateUndoData();

            List<int32> cursorPositions = scope List<int32>();
            for (var replaceSymbolData in mUpdatingProjectSources)
            {
                //var editData = IDEApp.sApp.GetEditData(replaceSymbolData.mProjectSource, true);
				var editData = replaceSymbolData.mFileEditData;
				if (editData.mEditWidget == null)
				{
					cursorPositions.Add(-1);
					continue;
				}	
                var sourceEditWidgetContent = (SourceEditWidgetContent)editData.mEditWidget.Content;
                cursorPositions.Add((int32)sourceEditWidgetContent.CursorTextPos);
            }

			var prevSelection = activeSourceEditWidgetContent.mSelection;

            for (int sourceIdx = 0; sourceIdx < mUpdatingProjectSources.Count; sourceIdx++)            
            {
                var replaceSymbolData = mUpdatingProjectSources[sourceIdx];
				if (replaceSymbolData.mIsLocked)
					continue;

				var editData = replaceSymbolData.mFileEditData;
				if (editData.mEditWidget == null)
					continue;

				if ((mKind == Kind.Rename) && (mRenameHadChange))
				{
					for (var projectSource in editData.mProjectSources)
					{
						projectSource.HasChangedSinceLastCompile = true;
					}
				}

                int32 cursorPos = cursorPositions[sourceIdx];

                var editWidgetContent = editData.mEditWidget.Content;

                if ((mUpdateTextCount != 0) && (!didUndo) && (mKind == Kind.Rename))
                {
                    while (true)
                    {
                        bool isGlobalBatchEnd = false;

                        var undoAction = editWidgetContent.mData.mUndoManager.GetLastUndoAction();
                        if (undoAction is UndoBatchEnd)
                        {
                            var undoBatchEnd = (UndoBatchEnd)undoAction;
                            isGlobalBatchEnd = undoBatchEnd.Name.StartsWith("#");                            
                        }
                        bool success = editWidgetContent.mData.mUndoManager.Undo();
                        Debug.Assert(success);
                        if (!success)
                            break;
                        if (isGlobalBatchEnd)
                            break;
                    }
                    didUndo = true;
                }

                SourceEditBatchHelper sourceEditBatchHelper = null;
                if (globalUndoData != null)
                {
                    globalUndoData.mFileEditDatas.Add(editData);
                    sourceEditBatchHelper = scope:: SourceEditBatchHelper(editData.mEditWidget, "#renameSymbol", new GlobalUndoAction(editData, globalUndoData));
                }

                int32 idxOffset = 0;
                for (int32 i = 0; i < replaceSymbolData.mSpans.Count; i++)
                {
                    int32 spanStart = replaceSymbolData.mSpans[i].mSpanStart + idxOffset;
                    int32 spanLen = replaceSymbolData.mSpans[i].mSpanLength;
                    
                    if ((mKind == Kind.Rename) &&
						((mRenameHadChange) || (editWidgetContent == activeSourceEditWidgetContent)))
                    {
                        editWidgetContent.CursorTextPos = spanStart;
                        var deleteCharAction = new EditWidgetContent.DeleteCharAction(editWidgetContent, 0, spanLen);
                        deleteCharAction.mMoveCursor = false;
                        editWidgetContent.mData.mUndoManager.Add(deleteCharAction);

                        editWidgetContent.PhysDeleteChars(0, spanLen);

                        editWidgetContent.CursorTextPos = spanStart;
                        var insertTextAction = new EditWidgetContent.InsertTextAction(editWidgetContent, newStr, .None);
                        insertTextAction.mMoveCursor = false;
                        editWidgetContent.mData.mUndoManager.Add(insertTextAction);
                        editWidgetContent.PhysInsertAtCursor(newStr, false);

                        if (spanStart <= cursorPos)
                            cursorPos += strLenDiff;
                    }                    

                    for (int32 attrIdx = spanStart; attrIdx < spanStart + newStr.Length; attrIdx++)
                    {
                        editWidgetContent.mData.mText[attrIdx].mDisplayFlags |= (uint8)(SourceElementFlags.SymbolReference);
                    }

					if (mKind == Kind.Rename)
                    	idxOffset += (int32)newStr.Length - spanLen;
                }
                
                if (sourceEditBatchHelper != null)
                    sourceEditBatchHelper.Finish();
                
				// Not a perfect check - theoretically the focus could have changed since renaming is delayed by a tick, not a huge issue...
                if ((!mDidRevert) && (editWidgetContent.mEditWidget.mHasFocus) && (mKind == Kind.Rename))
				{
                    editWidgetContent.CursorTextPos = (int32)Math.Max(0, cursorPos - strLenDiff);
				}
            }

            if ((mUpdateTextCount == 0) && (mKind == Kind.Rename))
            {
				activeSourceEditWidgetContent.mSelection = prevSelection;
            }

            mUpdateTextCount++;
            mIgnoreTextChanges = false;
            mDidRevert = false;
        }

		void StopWork()
		{
			var bfCompiler = IDEApp.sApp.mBfResolveCompiler;
			while (mStartingWork)
			{
				bfCompiler.CancelBackground();
			}
		}

        void Dispose()
        {
			bool hadChange = (mKind == Kind.Rename) && (mNewReplaceStr != mOrigReplaceStr);

			mSourceViewPanel.CancelResolve(.GetSymbolInfo);
            if (mPassInstance != null)
            {
				StopWork();

                var bfSystem = IDEApp.sApp.mBfResolveSystem;
				if (mDoLock)
                	bfSystem.Unlock();

				Debug.Assert(!mUsingResolvePassData);
                delete mPassInstance;
                mPassInstance = null;
                delete mResolvePassData;
                mResolvePassData = null;

				if (mUpdatingProjectSources != null)
				{
					if ((mUpdatingProjectSources.IsEmpty) && (mSourceViewPanel.mEditData != null))
					{
						// If we have an error then won't necessarily even include ourselves, so add this here so we clear out the initial selection
						ReplaceSymbolData replaceSymbolData = new ReplaceSymbolData();
						replaceSymbolData.mFileEditData = mSourceViewPanel.mEditData;
						mUpdatingProjectSources.Add(replaceSymbolData);
					}

	                for (var replaceSymbolData in mUpdatingProjectSources)
	                {
						var editData = replaceSymbolData.mFileEditData;
						if (editData.mEditWidget == null)
							continue;
	                    var editWidgetContent = editData.mEditWidget.Content;
	                    for (int32 i = 0; i < editWidgetContent.mData.mTextLength; i++)
	                    {
	                        editWidgetContent.mData.mText[i].mDisplayFlags &= 0xFF ^ (uint8)(SourceElementFlags.SymbolReference);
	                    }

						if (hadChange)
						{
							using (gApp.mMonitor.Enter())
								editData.SetSavedData(null, IdSpan());

							if (!editData.HasEditPanel())
							{
								gApp.SaveFile(editData);
							}

							if (IDEApp.IsBeefFile(editData.mFilePath))
							{
								for (var projectSource in editData.mProjectSources)
									gApp.mBfResolveCompiler.QueueProjectSource(projectSource, .None, false);
								gApp.mBfResolveCompiler.QueueDeferredResolveAll();
							}
						}

	                }
				}
            }
        }
        
        void RenameSymbolSubmit(bool isFinal)
        {
            //var editWidgetContent = mSourceViewPanel.mEditWidget;
            
            UpdateText();
        }

        public void Cancel()
        {            
            if ((mUpdatingProjectSources != null) && (mKind == Kind.Rename))
            {
                for (var replaceSymbolData in mUpdatingProjectSources)
                {
					if (replaceSymbolData.mIsLocked)
						continue;

					var editData = replaceSymbolData.mFileEditData;
                    var editWidgetContent = editData.mEditWidget.Content;

                    while (true)
                    {
                        bool isGlobalBatchEnd = false;

                        var undoAction = editWidgetContent.mData.mUndoManager.GetLastUndoAction();
                        if (undoAction is UndoBatchEnd)
                        {
                            var undoBatchEnd = (UndoBatchEnd)undoAction;
                            isGlobalBatchEnd = undoBatchEnd.Name.StartsWith("#");
                        }
                        bool success = editWidgetContent.mData.mUndoManager.Undo();
                        Debug.Assert(success);
                        if (!success)
                            break;
                        if (isGlobalBatchEnd)
                            break;
                    }

                    break;
                }
            }

            Close();
        }

        public override void Update()
        {
            base.Update();

			if (gApp.mBfResolveCompiler == null)
			{
				Close();
				return;
			}

			if ((mUpdatingProjectSources == null) && (mUpdateCnt > 30) && (gApp.mUpdateCnt % 4 == 0))
				MarkDirty();

			if (mAwaitingGetSymbolInfo)
			{
				if (!CheckGetSymbolInfo(false))
					return;
			}

			if (mKind == .GoToDefinition)
			{
				/*if (gApp.mBfResolveCompiler.HasResolvedAll())
				{
					Close();
					gApp.GoToDefinition(true);
				}*/

				if (mSourceViewPanel.EditWidget.Content.CursorTextPos != mCursorPos)
				{
					Close();
				}

				return;
			}

			if (mStartingWork)
				return;

            /*if (mReplaceStr == "")
            {
                //Cancel();
                //return;
                mReplaceStr = " ";
            }
            else*/ if (mTextChanged)
            {
                mTextChanged = false;
                UpdateText();
            }

            //var bfSystem = IDEApp.sApp.mBfResolveSystem;
            var bfCompiler = IDEApp.sApp.mBfResolveCompiler;

			if ((!mStartedWork) && (bfCompiler != null))
            {
				bool hasWorkLeft = bfCompiler.IsPerformingBackgroundOperation();
				if (mSourceViewPanel.[Friend]mWantsFullClassify)
					hasWorkLeft = true;
				if (mSourceViewPanel.HasDeferredResolveResults())
				{
					hasWorkLeft = true;
				}
                if (!hasWorkLeft)
                {
                    StartWork();
                }
            }
        }

		public override bool Contains(float x, float y)
		{
			if (mKind == Kind.ShowFileReferences)
				return false;
			return base.Contains(x, y);
		}

        public void Close()
        {
			if (mClosed)
				return;

			mClosed = true;

			mSourceViewPanel.CancelResolve((mKind == .GoToDefinition) ? .GoToDefinition : .GetSymbolInfo);
			if (mBackgroundKind != .None)
			{
				gApp.mBfResolveCompiler.CancelBackground();
				Debug.Assert(mBackgroundKind == .None);
			}

            if (mParent != null)
                RemoveSelf();
			if (mKind == .Rename)
				mSourceViewPanel.QueueFullRefresh(false);
            mSourceViewPanel.mRenameSymbolDialog = null;
            IDEApp.sApp.mSymbolReferenceHelper = null;
            Dispose();
			BFApp.sApp.DeferDelete(this);
        }

        public override void Draw(Graphics g)
        {
            if (mKind == Kind.ShowFileReferences)
                return;

			if ((mKind == .GoToDefinition) && (mUpdateCnt < 40))
			{
				// Reduce "flashing" for very fast finds
				return;
			}

			int symCount = 0;
			int readOnlyRefCount = 0;
			int lockedFileCount = 0;
			if (mUpdatingProjectSources != null)
			{
			    for (var replaceSymbolData in mUpdatingProjectSources)
			    {
			        symCount += replaceSymbolData.mSpans.Count;
					if (replaceSymbolData.mIsLocked)
					{
						lockedFileCount++;
						readOnlyRefCount += replaceSymbolData.mSpans.Count;
					}
			    }
			}

			float boxHeight = mHeight - GS!(8);

			if (lockedFileCount > 0)
			{
				boxHeight += GS!(14);
			}

            base.Draw(g);
            using (g.PushColor((lockedFileCount == 0) ? 0xFFFFFFFF : 0xFFF0B0B0))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth - GS!(8), boxHeight);

            using (g.PushColor(0xFFE0E0E0))
            {
                g.SetFont(DarkTheme.sDarkTheme.mSmallBoldFont);
				float border = GS!(8);

				String drawStr = null;
				if (!mOrigReplaceStr.IsEmpty)
				{
					var formatStr = mKind == .FindAllReferences ? "Finding '{0}'" : "Renaming '{0}'";
					drawStr = scope:: String()..AppendF(formatStr, mOrigReplaceStr);
				}
				else
				{
					switch (mKind)
					{
					case .FindAllReferences, .GoToDefinition:
						drawStr = "Finding...";
					case .Rename:
						drawStr = "Renaming...";
					default:
					}
				}

				if (drawStr != null)
					g.DrawString(drawStr, border, GS!(5), FontAlign.Centered, mWidth - border * 2 - GS!(8), FontOverflowMode.Ellipsis); 

                if (mUpdatingProjectSources != null)
                {
                    g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
					var drawString = scope String();
					drawString.AppendF("{0} {1} in {2} {3}",
						symCount, (symCount == 1) ? "reference" : "references",
						mUpdatingProjectSources.Count, (mUpdatingProjectSources.Count == 1) ? "file" : "files");
                    g.DrawString(drawString, GS!(8), GS!(22), FontAlign.Centered, mWidth - GS!(8) - GS!(16));

					if (lockedFileCount > 0)
					{
						g.SetFont(DarkTheme.sDarkTheme.mSmallBoldFont);
						using (g.PushColor(((mUpdateCnt > 200) || (mUpdateCnt / 10 % 2 == 0)) ? 0xFFFF7070 : 0xFFFFB0B0))
							g.DrawString(scope String()..AppendF("{} {} LOCKED!", lockedFileCount, (lockedFileCount == 1) ? "FILE" : "FILES"),
								GS!(8), GS!(38), FontAlign.Centered, mWidth - GS!(8) - GS!(16));
					}
                }
            }

            if ((mUpdatingProjectSources == null) && (mUpdateCnt > 30))
                IDEUtils.DrawWait(g, mWidth / 2, mHeight / 2 + 4, mUpdateCnt);
        }

        public void SourcePreInsertText(SourceEditWidgetContent sourceEditWidgetContent, int index, String text)
        {
            if (mIgnoreTextChanges)
                return;

			if (mStartingWork)
				return;

			bool hadSel = false;
			if ((mTextChanged) && (sourceEditWidgetContent.HasSelection()))
			{
				hadSel = true;
			}

            if ((mNewReplaceStr == "") && (index == mLastDeletedIdx))
            {
                mNewReplaceStr.Set(text);
                mSkipNextUpdate = true;
                mTextChanged = true;
                return;
            }

			uint8 curTypeNum = 0;
			uint8 curFlags = sourceEditWidgetContent.mInsertDisplayFlags;
			sourceEditWidgetContent.GetInsertFlags(index, ref curTypeNum, ref curFlags);
			if ((curFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0)
			{
				// The last char was non-extending
				Close();
				return;
			}

            // Close if insert is outside match area
            if ((index > 0) && (index < sourceEditWidgetContent.mData.mTextLength) &&
                ((sourceEditWidgetContent.mData.mText[index - 1].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0) &&
                ((sourceEditWidgetContent.mData.mText[index].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0))
            {
                Close();
            }
        }

        public void SourcePreRemoveText(SourceEditWidgetContent sourceEditWidgetContent, int index, int length)
        {
            if (mIgnoreTextChanges)
                return;
			if (mStartingWork)
				return;

            // Close if any char8 isn't within the match area
            bool isValid = true;
            for (int i = index; i < index + length; i++)
            {
                if ((sourceEditWidgetContent.mData.mText[i].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0)
                    isValid = false;
            }

            mLastDeletedIdx = (int32)index;

            if (!isValid)
                Close();
        }

        public void SourceUpdateText(SourceEditWidgetContent sourceEditWidgetContent, int index)
        {
            if (mKind == Kind.ShowFileReferences)
            {
                Close();
                return;
            }

            if (mSkipNextUpdate)
            {
                mSkipNextUpdate = false;
                return;
            }

            if (mIgnoreTextChanges)
                return;

            int left = index;
            while (left > 0)
            {
                if ((sourceEditWidgetContent.mData.mText[left - 1].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0)
                    break;
                left--;
            }

            int right = index;
            while (right < sourceEditWidgetContent.mData.mTextLength)
            {
                if ((sourceEditWidgetContent.mData.mText[right].mDisplayFlags & (uint8)(SourceElementFlags.SymbolReference)) == 0)
				{
                    break;
				}
                right++;
            }

            mNewReplaceStr.Clear();
            sourceEditWidgetContent.ExtractString(left, right - left, mNewReplaceStr);
            mTextChanged = true;
        }

        public void Revert()
        {
            mDidRevert = true;
            if (mNewReplaceStr == mOrigReplaceStr)
            {
                Cancel();
            }
            else
            {
                mNewReplaceStr.Set(mOrigReplaceStr);
                mTextChanged = true;
            }
        }
    }
}
