using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.events;
using Beefy.utils;
using IDE.ui;
using System.Diagnostics;
using Beefy.gfx;

namespace IDE.ui
{
    public class QuickFind : Widget
    {
        enum FindType
        {
            CurrentFile,
            Selection,
        }

        public class FindEdit : DarkEditWidget
        {
            QuickFind mQuickFind;
            int32 mFoundVersion;

            public this(QuickFind quickFind)
            {
                mQuickFind = quickFind;
            }            

            public override void KeyDown(KeyCode keyCode, bool isRepeat)
            {
                if (keyCode == KeyCode.Escape)
                {
                    mQuickFind.Close();
                    return;
                }                

                base.KeyDown(keyCode, isRepeat);
            }
            
            public override void KeyChar(char32 theChar)
            {
                /*if ((theChar == '\r') || (theChar == '\n'))
                {
                    mQuickFind.FindNext();
                    return;
                } */               

				if (theChar == '\t')
					return;

                base.KeyChar(theChar);
            }

            public override void Update()
            {
                base.Update();

                if (Content.mData.mCurTextVersionId != mFoundVersion)
                {
                    mFoundVersion = Content.mData.mCurTextVersionId;
                    mQuickFind.mCurFindIdx = -1;
                    mQuickFind.FindAll();
                    mQuickFind.ShowCurrentSelection();
                }
            }
        }

        public TextPanel mPanel;
		public EditWidget mEditWidget;
        public EditWidget mFindEditWidget;
        public EditWidget mReplaceEditWidget;
        public int32 mLastActiveCursorPos;
        bool mHasNewActiveCursorPos;
        public int32 mCurFindIdx = -1;
        public int32 mCurFindStart = 0;
        public int32 mCurFindCount;
		public int32 mCurFindDir;
        //public bool mSearchDidWrap;
        public bool mIsReplace;
        public int32 mLastTextVersion;
        bool mFoundMatches;        
        public bool mIsShowingMatches = false;
        static String sLastSearchString = new String() ~ delete _;

		public bool mOwnsSelection;
        PersistentTextPosition mSelectionStart ~ { Debug.Assert(_ == null); };
        PersistentTextPosition mSelectionEnd ~ { Debug.Assert(_ == null); };
		

        public this(Widget parent, EditWidget editWidget, bool isReplace)
        {
            mPanel = parent as TextPanel;
			mEditWidget = editWidget;
            parent.AddWidget(this);
            mIsReplace = isReplace;

            UpdateCursorPos();

            mFindEditWidget = new FindEdit(this);
            mFindEditWidget.Content.SelectAll();
            mFindEditWidget.mOnKeyDown.Add(new => KeyDownHandler);
            AddWidget(mFindEditWidget);            

            if (isReplace)
            {
                mReplaceEditWidget = new FindEdit(this);
                mReplaceEditWidget.mOnKeyDown.Add(new => KeyDownHandler);
                AddWidget(mReplaceEditWidget);
            }

            mLastTextVersion = mEditWidget.Content.mData.mCurTextVersionId;

			var content = editWidget.Content;
            var sourceContent = editWidget.Content as SourceEditWidgetContent;
            if (content.HasSelection())
            {
                int selStart = content.mSelection.Value.MinPos;
                int selEnd = content.mSelection.Value.MaxPos;
                bool isMultiline = false;
                for (int i = selStart; i < selEnd; i++)
                {
                    if (content.mData.mText[i].mChar == '\n')
                    {
                        isMultiline = true;
                    }
                }

                if (isMultiline)
                {
					Debug.Assert(mSelectionStart == null);

					mSelectionStart = new PersistentTextPosition((int32)selStart);
					mSelectionEnd = new PersistentTextPosition((int32)selEnd);
					if (sourceContent != null)
					{
	                    sourceContent.PersistentTextPositions.Add(mSelectionStart);
	                    sourceContent.PersistentTextPositions.Add(mSelectionEnd);
					}
					else
						mOwnsSelection = true;
                }
                else
                {
					String text = scope String();
					content.ExtractString(selStart, selEnd - selStart, text);
					text.Trim();
                    mFindEditWidget.SetText(text);
                    mFindEditWidget.Content.SelectAll();
                }

				content.mSelection = null;
            }
        }

		public ~this()
		{
			
		}

        void UpdateCursorPos()
        {
            if (mEditWidget.mHasFocus)
            {
                mHasNewActiveCursorPos = true;
                mLastActiveCursorPos = (int32)mEditWidget.Content.CursorTextPos;
                if (mEditWidget.Content.HasSelection())
                    mLastActiveCursorPos = (int32)mEditWidget.Content.mSelection.Value.MinPos;
            }
        }

        void DoReplace(bool all)
        {
            int32 replaceCount = -1;
            if (all)
            {
                mEditWidget.SetFocus();
                replaceCount = Replace(true);
                if (replaceCount > 0)
                    IDEApp.sApp.MessageDialog("Replace Results", scope String()..AppendF("{0} instance(s) replaced.", replaceCount));

				if (replaceCount != -1)
				{
				    if (replaceCount == 0)
				        IDEApp.sApp.Fail("The search text wasn't found.");
				    mWidgetWindow.mIsKeyDownHandled = true;
				}
            }
            else
            {
                //replaceCount = Replace((byte)SourceElementFlags.Find_CurrentSelection);
                /*var content = mEditWidget.Content;
                String replaceText = scope String();
                mReplaceEditWidget.GetText(replaceText);
                bool hasMatch = false;
                if (content.HasSelection())
                {
                    String selText = scope String();
                    content.GetSelectionText(selText);
					String findText = scope String();
					mFindEditWidget.GetText(findText);
                    if (selText == findText)
                        hasMatch = true;
                }

                if (hasMatch)
                {
                    content.InsertAtCursor(replaceText);
                    mCurFindIdx = (int32)content.CursorTextPos - 1;
                }*/
				Replace(false);
				FindNext(1, true);
            }
        }

        void KeyDownHandler(KeyDownEvent evt)
        {
            if (evt.mKeyCode == KeyCode.Escape)
                Close();

            if (evt.mKeyCode == KeyCode.Return)
            {
                if (evt.mSender == mFindEditWidget)
                {
                    FindNext(1, true);
                }
                else if (evt.mSender == mReplaceEditWidget)
                {
                    DoReplace(false);
                }
            }

            if (evt.mKeyCode == KeyCode.Tab)
            {
                IDEUtils.SelectNextChildWidget(this, mWidgetWindow.IsKeyDown(KeyCode.Shift));
            }
            
            //int replaceCount = -1;

            if ((evt.mKeyCode == (KeyCode)'R') && (mWidgetWindow.IsKeyDown(KeyCode.Menu)))
                DoReplace(false);

            if ((evt.mKeyCode == (KeyCode)'A') && (mWidgetWindow.IsKeyDown(KeyCode.Menu)))
                DoReplace(true);            
        }

        void EditWidgetSubmit(EditEvent editEvent)
        {            
            //FindNext(true);
        }

        public bool ClearFlags(bool clearMatches, bool clearSelection)
        {
            uint8 mask = 0xFF;
            if (clearMatches)
                mask = (uint8)(mask & ~(uint8)(SourceElementFlags.Find_Matches));
            if (clearSelection)
                mask = (uint8)(mask & ~(uint8)SourceElementFlags.Find_CurrentSelection);

            bool foundFlags = false;
            var text = mEditWidget.Content.mData.mText;
            for (int32 i = 0; i < text.Count; i++)
            {
                if ((text[i].mDisplayFlags & ~mask) != 0)
                {
                    text[i].mDisplayFlags = (uint8)(text[i].mDisplayFlags & mask);
                    foundFlags = true;
                }
            }
            return foundFlags;
        }

        public void FindAll(bool doSelect = true)
        {
            mIsShowingMatches = true;
            mFoundMatches = false;

            mCurFindStart = mLastActiveCursorPos;
            mCurFindIdx = mCurFindStart - 1;
            mCurFindCount = 0;
            //mSearchDidWrap = false;

            ClearFlags(true, true);
			if (mSelectionStart != null)
			{
				var data = mEditWidget.Content.mData;
				for (int i in mSelectionStart.mIndex..<mSelectionEnd.mIndex)
				{
					data.mText[i].mDisplayFlags |= (uint8)SourceElementFlags.Find_CurrentSelection;
				}
			}

			if (doSelect)
            	FindNext(1, true, ErrorReportType.None);
            int32 curFindIdx = mCurFindIdx;
            while (FindNext(1, false, ErrorReportType.None))
            {
            }
            mCurFindIdx = curFindIdx;
            mCurFindCount = 0;
        }

        public void ShowCurrentSelection()
        {
            if (mCurFindIdx == -1)
                return;
			if (!mFoundMatches)
				return;
            String findText = scope String();
            mFindEditWidget.GetText(findText);
			if (findText.Length == 0)
				return;
            var editWidgetContent = mEditWidget.Content;
            editWidgetContent.MoveCursorToIdx(mCurFindIdx + (int32)findText.Length, true, .QuickFind);


			editWidgetContent.mSelection = EditSelection(mCurFindIdx, mCurFindIdx + (int32)findText.Length);

			/*for (int32 idx = mCurFindIdx; idx < mCurFindIdx + findText.Length; idx++)
			{
			    uint8 flags = (uint8)SourceElementFlags.Find_CurrentSelection;
			    mEditWidget.Content.mData.mText[idx].mDisplayFlags = (uint8)(mEditWidget.Content.mData.mText[idx].mDisplayFlags | flags);
			}*/

			if ((mSelectionStart == null) || (mParent == null))
			{
	            /*if (mFoundMatches)
	                editWidgetContent.mSelection = EditSelection(mCurFindIdx, mCurFindIdx + (int32)findText.Length);
	            else if (!String.IsNullOrWhiteSpace(findText))
	                editWidgetContent.mSelection = null;*/
			}
            if (mHasNewActiveCursorPos)
            {
				if (mPanel != null)
                	mPanel.RecordHistoryLocation();
                mHasNewActiveCursorPos = false;
            }
        }

		public void SetFindIdx(int idx, bool resetFind)
		{
			mCurFindIdx = (.)idx;
			mLastActiveCursorPos = mCurFindIdx;
			if (resetFind)
			{
				mCurFindCount = 0;
				mCurFindStart = mCurFindIdx;
			}
		}

        public void FindNext(int32 dir, bool showMessage)
        {
            var editWidgetContent = mEditWidget.Content;
            
            if (!mIsShowingMatches)           
            {
                // This only happens after we close the quickfind and we need to reshow the matches
                if (sLastSearchString.Length > 0)
                    mFindEditWidget.SetText(sLastSearchString);
                mCurFindIdx = (int32)editWidgetContent.CursorTextPos;
                int32 curFindIdx = mCurFindIdx;
                FindAll();
                mCurFindIdx = curFindIdx;
            }

            if (!mFoundMatches)
            {
                IDEApp.sApp.Fail("The search text wasn't found.");
                return;
            }

            if (FindNext(dir, true, showMessage ? ErrorReportType.MessageBox : ErrorReportType.Sound))
            {
				ShowCurrentSelection();
			}
        }

        public enum ErrorReportType
        {
            None,
            Sound,
            MessageBox
        }

        void ShowDoneError(ErrorReportType errorType)
        {
            if (errorType != ErrorReportType.None)
            {
                IDEApp.Beep(IDEApp.MessageBeepType.Information);
                if (errorType == ErrorReportType.MessageBox)
                    IDEApp.sApp.MessageDialog("Search Done", "Find reached the starting point of the search.");
            }
        }

        public bool FindNext(int32 dir, bool isSelection, ErrorReportType errorType)
        {
            var editContent = mEditWidget.Content;

            String findText = scope String();
            mFindEditWidget.GetText(findText);
            sLastSearchString.Set(findText);
            if (findText.Length == 0)
                return false;

            String findTextLower = scope String(findText);
            findTextLower.ToLower();
            String findTextUpper = scope String(findText);
            findTextUpper.ToUpper();

            int32 selStart = (mSelectionStart != null) ? mSelectionStart.mIndex : 0;
            int32 selEnd = (mSelectionEnd != null) ? mSelectionEnd.mIndex : editContent.mData.mTextLength;

			mCurFindStart = Math.Max(mCurFindStart, selStart);


			/*mCurFindStart = Math.Clamp(mCurFindStart, selStart, selEnd - (.)findText.Length);*/
			if (mCurFindIdx != -1)
				mCurFindIdx = Math.Clamp(mCurFindIdx, selStart, selEnd - (.)findText.Length);

			if ((mCurFindIdx == -1) && (mSelectionStart != null) && (dir > 0))
			{
			    mCurFindIdx = mSelectionStart.mIndex - 1;
			}

            int32 nextIdx = -1;
            int32 searchStartIdx;

			if (dir < 0)
			{
				if (mCurFindIdx == -1)
				{
					searchStartIdx = selEnd + dir - (int32)findText.Length;
				}
				else
					searchStartIdx = mCurFindIdx + dir;
				/*if (searchStartIdx < selStart)
				    searchStartIdx = selStart;*/
			}
			else
			{
				searchStartIdx = mCurFindIdx + dir;
				/*if (searchStartIdx < selStart)
				    searchStartIdx = selStart;*/
			}            

            /*if ((searchStartIdx == mCurFindStart) && (mCurFindCount > 0))
            {
                mCurFindCount = 0;
                return false;
            }*/

            //for (int startIdx = searchStartIdx; startIdx <= selEnd - findText.Length; startIdx++)			
			for (int32 startIdx = searchStartIdx; true; startIdx += dir)
            {
				if (startIdx > selEnd - findText.Length)
					break;
				if (startIdx < selStart)
					break;

                bool isEqual = true;
                for (int32 i = 0; i < findText.Length; i++)
                {
                    if ((editContent.mData.mText[i + startIdx].mChar != findTextLower[i]) &&
                        (editContent.mData.mText[i + startIdx].mChar != findTextUpper[i]))
                    {
                        isEqual = false;
                        break;
                    }
                }
                if (isEqual)
                {
                    nextIdx = startIdx;
                    break;
                }
            }

            /*if (nextIdx == mCurFindIdx - 1)
            {
                int a = 0;
            }*/

			if (mCurFindDir != dir)
			{
				mCurFindDir = dir;
				mCurFindCount = 0;
				mCurFindStart = mCurFindIdx;
			}

			if (dir < 0)
			{
				int32 checkOfs = (mCurFindCount == 0) ? 1 : 0;
				if ((searchStartIdx >= mCurFindStart + checkOfs) && ((nextIdx == -1) || (nextIdx <= mCurFindStart)))
				{
				    if (isSelection)
				        ShowDoneError(errorType);
				        
				    mCurFindIdx = mCurFindStart + 1;
				    mCurFindCount = 0;
				    return false;
				}
			}
			else
			{
	            int32 checkOfs = (mCurFindCount == 0) ? -1 : 0;
	            if ((searchStartIdx <= mCurFindStart + checkOfs) && ((nextIdx == -1) || (nextIdx >= mCurFindStart)))
	            {
	                if (isSelection)
	                    ShowDoneError(errorType);
	                    
	                mCurFindIdx = mCurFindStart - 1;
	                mCurFindCount = 0;
	                return false;
	            }
			}

            /*if ((mSelectionEnd != null) && (nextIdx + findText.Length > mSelectionEnd.mIndex))
                return false;*/
                //nextIdx = -1;            

            if (nextIdx != -1)
            {
                mCurFindCount++;

                mFoundMatches = true;
                for (int32 idx = nextIdx; idx < nextIdx + findText.Length; idx++)
                {
                    uint8 flags = (uint8)SourceElementFlags.Find_Matches;
                    /*if (isSelection)
                        flags |= (uint8)SourceElementFlags.Find_CurrentSelection;*/

                    mEditWidget.Content.mData.mText[idx].mDisplayFlags = (uint8)(mEditWidget.Content.mData.mText[idx].mDisplayFlags | flags);
                }
                
                mCurFindIdx = nextIdx;
                return true;
            }
            else
            {                
                //if (isSelection)
                {
                    //if (doError)
                        //IDEApp.MessageBeep(IDEApp.MessageBeepType.Information);
                    
                    //return false;
                }
            }

            mCurFindIdx = -1;
            if (mCurFindStart == selStart)
            {
                // Special case for searching from start
                if (isSelection)
                {
                    ShowDoneError(errorType);
                    mCurFindCount = 0;
                }
                return false;
            }

            mCurFindIdx = -1;

            sReentryCount++;
            if (sReentryCount > 10)
            {
                Runtime.FatalError("Too many iterations!");
            }
            var result = FindNext(dir, isSelection, errorType);
            sReentryCount--;
            return result;
        }

        static int32 sReentryCount;

        public int32 Replace(bool replaceAll)
        {
			scope AutoBeefPerf("QuickFind.Replace");

            int32 searchCount = 0;
            int32 searchStart = 0;
            String findText = scope String();
            mFindEditWidget.GetText(findText);
            String replaceText = scope String();
            mReplaceEditWidget.GetText(replaceText);
            UndoBatchStart undoBatchStart = null;

			var ewc = mEditWidget.Content;

			//Profiler.StartSampling();

			SourceElementFlags findFlags = replaceAll ? .Find_Matches : .Find_CurrentSelection;

			if (!replaceAll)
			{
				if (!ewc.HasSelection())
					return 0;
			}

            while (true)
            {
				int32 selEnd = -1;
				int32 selStart = -1;
				if (replaceAll)
				{
	                var text = mEditWidget.Content.mData.mText;
	                for (int32 i = searchStart; i < mEditWidget.Content.mData.mTextLength; i++)
	                {
	                    if ((text[i].mDisplayFlags & (uint8)findFlags) != 0)
	                    {
	                        if (selStart == -1)
	                            selStart = i;
	                        selEnd = i;
	                    }
	                    else if (selEnd != -1)
	                        break;
	                }                

	                if (selStart == -1)
	                    break;

	                int32 selLen = selEnd - selStart + 1;
	                Debug.Assert(selLen % findText.Length == 0);
	                selEnd = selStart + (int32)findText.Length - 1;

	                if (searchCount == 0)
	                {
	                    undoBatchStart = new UndoBatchStart("replace");
	                    ewc.mData.mUndoManager.Add(undoBatchStart);
	                }

	                EditSelection selection = EditSelection();
	                selection.mStartPos = selStart;
	                selection.mEndPos = selEnd + 1;
	                ewc.mSelection = selection;
				}
				else
					selStart = (.)ewc.mSelection.Value.MinPos;
				EditWidgetContent.InsertFlags insertFlags = .NoMoveCursor | .NoRestoreSelectionOnUndo | .IsGroupPart;
				if (searchCount == 0)
					insertFlags |= .IsGroupStart;
                ewc.InsertAtCursor(replaceText, insertFlags);

				/*if (selStart <= mCurFindIdx)
					mCurFindIdx += (int32)(replaceText.Length - findText.Length);*/

                searchStart = selStart + (int32)replaceText.Length;
                searchCount++;
				DataUpdated();

				if (!replaceAll)
				{
					mCurFindIdx = searchStart;
					break;
				}

                /*if (flags == (byte)SourceElementFlags.Find_CurrentSelection)
                {
                    mLastTextVersion = mEditWidget.Content.mCurTextVersionId;
                    mCurFindIdx = searchStart;
                    FindNext(true, ErrorReportType.MessageBox);
                    break;
                }*/
            }

			//Profiler.StopSampling();

            if (undoBatchStart != null)
                ewc.mData.mUndoManager.Add(undoBatchStart.mBatchEnd);            

            return searchCount;
        }        

		void DataUpdated()
		{
			mLastTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
		}

        public void UpdateData()
        {
            if (mLastTextVersion != mEditWidget.Content.mData.mCurTextVersionId)
            {
                if (mIsShowingMatches)
                    FindAll(false);
                DataUpdated();
            }

            UpdateCursorPos();
        }

        public override void Draw(Beefy.gfx.Graphics g)
        {                        
            base.Draw(g);
            using (g.PushColor(0xFFFFFFFF))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth - GS!(8), mHeight - GS!(8));
        }

        public override void DrawAll(Beefy.gfx.Graphics g)
        {
            base.DrawAll(g);

			var findText = scope String();
			mFindEditWidget.GetText(findText);

			var replaceText = scope String();
			if (mReplaceEditWidget != null)
				mReplaceEditWidget.GetText(replaceText);

            if ((!mFoundMatches) && (!String.IsNullOrWhiteSpace(findText)))
            {
                using (g.PushColor(0xFFFF0000))
                    g.OutlineRect(mFindEditWidget.mX, mFindEditWidget.mY, mFindEditWidget.mWidth, mFindEditWidget.mHeight);
            }

            if ((!mFindEditWidget.mHasFocus) && (findText.Length == 0))
            {
                using (g.PushColor(Color.Mult(gApp.mSettings.mUISettings.mColors.mText, 0x60FFFFFF)))
                    g.DrawString("Find...", mFindEditWidget.mX + GS!(4), mFindEditWidget.mY);
            }

            if ((mReplaceEditWidget != null) && (!mReplaceEditWidget.mHasFocus) && (replaceText.Length == 0))
            {
                using (g.PushColor(Color.Mult(gApp.mSettings.mUISettings.mColors.mText, 0x60FFFFFF)))
                    g.DrawString("Replace...", mReplaceEditWidget.mX + 4, mReplaceEditWidget.mY);
            }

            using (g.PushColor(gApp.mSettings.mUISettings.mColors.mAutoCompleteSubText))
            {
                g.DrawString((mSelectionStart != null) ? "Selection" : "Current Document", GS!(6), mHeight - GS!(29));
            }
        }

        // We leave a pointer to this in the TextPanel after we close it so we can still use F3 after its closed
        public bool Close()
        {
            bool didSomething = false;

            var sourceContent = mEditWidget.Content as SourceEditWidgetContent;
            if (mSelectionStart != null)
			{
				if (mSelectionEnd != null)
					sourceContent.mSelection = .(mSelectionStart.mIndex, mSelectionEnd.mIndex);
				if (sourceContent != null)
                	sourceContent.PersistentTextPositions.Remove(mSelectionStart);
				DeleteAndNullify!(mSelectionStart);
			}

            if (mSelectionEnd != null)
			{
				if (sourceContent != null)
                	sourceContent.PersistentTextPositions.Remove(mSelectionEnd);
				DeleteAndNullify!(mSelectionEnd);
			}

            mIsShowingMatches = false;
            didSomething |= ClearFlags(true, true);
            if (mWidgetWindow != null)
            {
				bool hadFocus = (mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(this));
                didSomething = true;
				if (hadFocus)
                	mWidgetWindow.SetFocus(mEditWidget);
                RemoveSelf();
            }
            return didSomething;
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mFindEditWidget.Resize(GS!(6), GS!(6), width - GS!(20), GS!(22));

            if (mReplaceEditWidget != null)
                mReplaceEditWidget.Resize(GS!(6), GS!(6 + 22 + 4), width - GS!(20), GS!(22));
        }

		public void ResizeSelf()
		{
			if (mParent == null)
				return;

		    float findWidth = GS!(200);
		    float findHeight = mIsReplace ? GS!(80) : GS!(56);
		    var editWidget = mEditWidget;

			float x = mParent.mWidth - findWidth;
			if (mEditWidget.mVertScrollbar != null)
				x -= GS!(10);

		    Resize(x, editWidget.mY + 2, findWidth, findHeight);
		}
		
		public bool HasFocus()
		{
			if (mHasFocus)
				return true;
			if (mFindEditWidget.mHasFocus)
				return true;
			if ((mReplaceEditWidget != null) && (mReplaceEditWidget.mHasFocus))
				return true;
			return false;
		}
    }
}
