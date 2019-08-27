using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using System.Diagnostics;
using System.IO;

namespace IDE.ui
{
    public class ImmediateWidgetContent : OutputWidgetContent
    {   
     	bool mSkipCheckSelection;

        public this()
        {
            mIsReadOnly = false;
            mAllowVirtualCursor = false;

            mIsMultiline = true;
            //mWordWrap = true;
        }
        
        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            if ((btn == 0) && (btnCount > 1))
            {
                for (int32 lineOfs = 0; lineOfs >= -1; lineOfs--)
                    if (GotoRefrenceAtLine(CursorLineAndColumn.mLine, lineOfs))
                        return;
            }

            base.MouseDown(x, y, btn, btnCount);
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            ImmediateWidget watchEditWidget = (ImmediateWidget)mEditWidget;

            if (watchEditWidget.mErrorEnd != 0)
            {
                var text = scope String();
                watchEditWidget.GetText(text);
                //float indent = mTextInsets.mLeft;
                //float strStarts = indent + g.mFont.GetWidth(text.Substring(0, watchEditWidget.mErrorStart));
                //float strEnds = indent + g.mFont.GetWidth(text.Substring(0, Math.Min(watchEditWidget.mErrorEnd, text.Length)));

                float startX;
                float startY;
                float endX;
                float endY;

                int line;
                int lineChar;
                GetLineCharAtIdx(watchEditWidget.mErrorStart, out line, out lineChar);
                GetTextCoordAtLineChar(line, lineChar, out startX, out startY);
                GetLineCharAtIdx(watchEditWidget.mErrorEnd, out line, out lineChar);
                GetTextCoordAtLineChar(line, lineChar, out endX, out endY);

                using (g.PushColor(0xFFFF4040))
                    IDEApp.sApp.DrawSquiggle(g, startX, startY, endX - startX);
            }

			//TEST:
			/*g.SetFont(IDEApp.sApp.mCodeFont);
			using (g.PushColor(0xFFFFFFFF))
				g.FillRect(0, 0, 200, 50);
			using (g.PushColor(0xFF000000))
			{
				g.DrawString("Hey Bro, goofy man!", 4, 4);
			}

			using (g.PushColor(0xFF000000))
				g.FillRect(0, 50, 200, 50);
			using (g.PushColor(0xFFFFFFFF))
			{
				g.DrawString("Hey Bro, goofy man!", 4, 50 + 4);
			}*/
        }

        bool IsInsideEntry(int idx)
        {
            var immediateWidget = (ImmediateWidget)mEditWidget;
            return idx > immediateWidget.mEntryStartPos.mIndex;
        }

        void CheckSelection()
        {
            if ((mSelection != null) && (!mSkipCheckSelection))
            {
                if ((!IsInsideEntry(mSelection.Value.MinPos) && (IsInsideEntry(mSelection.Value.MaxPos))))
                {
                    var immediateWidget = (ImmediateWidget)mEditWidget;
                    mSelection = EditSelection(immediateWidget.mEntryStartPos.mIndex + 1, mSelection.Value.MaxPos);
                }

                if ((!IsInsideEntry(mSelection.Value.mStartPos)) || (!IsInsideEntry(mSelection.Value.mEndPos)))
                    return;
            }
        }

        public override void DeleteSelection(bool moveCursor)
        {
            CheckSelection();
            base.DeleteSelection();
        }

		public override void ClearText()
		{
			mSkipCheckSelection = true;
			base.ClearText();
			mSkipCheckSelection = false;
			var immediateWidget = mEditWidget as ImmediateWidget;
			immediateWidget.ClearEntry();
			immediateWidget.HandleResult(scope String(""));
		}

        public override void Backspace()
        {
            if (!IsInsideEntry(mCursorTextPos - 1))
                return;
            base.Backspace();
        }

        public override bool CheckReadOnly()
        {
            var immediateWidget = (ImmediateWidget)mEditWidget;
            if (immediateWidget.mChangeInvalidatesHistory)
                immediateWidget.mHistoryIdx = -1;

            CheckSelection();
            if (!IsInsideEntry(mCursorTextPos))
            {
				mSelection = null;
				MoveCursorToIdx(mData.mTextLength);
			}

            return base.CheckReadOnly();
        }

        public override void CursorToLineStart(bool allowToScreenStart)
        {
            if (IsInsideEntry(CursorTextPos))
            {
                var immediateWidget = (ImmediateWidget)mEditWidget;
                MoveCursorToIdx(immediateWidget.mEntryStartPos.mIndex + 1);
                return;
            }

            base.CursorToLineStart(allowToScreenStart);
        }

        public override bool PrepareForCursorMove(int dir = 0)
        {
            if (dir == -1)
            {
                var immediateWidget = (ImmediateWidget)mEditWidget;
                if (CursorTextPos == immediateWidget.mEntryStartPos.mIndex + 1)
                    return true;                
            }

            return base.PrepareForCursorMove(dir);
        }		
    }

    public class ImmediateInfoButton : Widget
    {
        public ImmediateWidget mImmediateWidget;

		public this()
		{
			
		}

		public ~this()
		{
			
		}

        public override void Draw(Graphics g)
        {
            g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MoreInfo));
        }

        public override void MouseEnter()
        {
            base.MouseEnter();
            
            if (mImmediateWidget.mPanel.mHoverWatch != null)
                mImmediateWidget.mPanel.mHoverWatch.Close();

            var hoverWatch = new HoverWatch();
            hoverWatch.mAllowLiterals = true;
            hoverWatch.mAllowSideEffects = true;
            hoverWatch.mOrigEvalString.Set(mImmediateWidget.mResultHoverWatch.mOrigEvalString);
            hoverWatch.SetListView(mImmediateWidget.mResultHoverWatch.mListView);
            hoverWatch.Show(mImmediateWidget.mPanel, mX + GS!(2), mY + GS!(3), null);
			hoverWatch.mRehupEvent.Add(new () =>
                {
					mImmediateWidget.RehupResult();
                });
            mImmediateWidget.mPanel.mHoverWatch = hoverWatch;
        }        

        public bool IsOnLine()
        {
            if (mWidgetWindow == null)
                return false;

            float rootX;
            float rootY;
            SelfToRootTranslate(0, 0, out rootX, out rootY);
            return (mWidgetWindow.mHasMouseInside) && (mWidgetWindow.mMouseY >= rootY) && (mWidgetWindow.mMouseY < rootY + mHeight) && 
                (mWidgetWindow.mMouseX < rootX + GS!(80));
        }
    }

    public class ImmediateWidget : SourceEditWidget
    {
        public PersistentTextPosition mEntryStartPos;
        public int32 mErrorStart;
        public int32 mErrorEnd;
        int32 mLastTextVersionId;
        public bool mWasTextModified;
        public bool mIsAddress;
        public bool mChangeInvalidatesHistory = true;
        public List<String> mHistoryList = new List<String>() ~ DeleteContainerAndItems!(_);
        public int32 mHistoryIdx = -1;
        public bool mUsedHistory = false;
        public HoverWatch mResultHoverWatch ~ delete _;
		public HoverWatch mPendingHoverWatch ~ delete _;
        public ImmediateInfoButton mInfoButton;
		public String mLastEvalString ~ delete _;
		List<char32> mDeferredChars = new .() ~ delete _;

		public bool IsPending
		{
			get
			{
				if (mResultHoverWatch == null)
					return false;
				if (mResultHoverWatch.mListView == null)
					return false;
				var listViewItem = (HoverWatch.HoverListViewItem)mResultHoverWatch.mListView.GetRoot().GetChildAtIndex(0);
				return listViewItem.mWatchEntry.mIsPending;
			}
		}

        public this()
            : base(null, new ImmediateWidgetContent())
        {
            var editWidgetContent = (ImmediateWidgetContent)mEditWidgetContent;
            editWidgetContent.mOnGenerateAutocomplete = new (char, options) => UpdateText(true);

            mEntryStartPos = new PersistentTextPosition(0);
            editWidgetContent.PersistentTextPositions.Add(mEntryStartPos);            
        }

		public ~this()
		{
			if (mInfoButton != null)
			{
				if (mInfoButton.mParent != null)
					mInfoButton.RemoveSelf();
				delete mInfoButton;
			}
		}

        public override void LostFocus()
        {
            base.LostFocus();
            //var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidgetContent;
            //TODO: This was causing autocomplete to close when you clicked on the scroll thumb.  Why did we need this?
            //if (sourceEditWidgetContent.mAutoComplete != null)
                //sourceEditWidgetContent.mAutoComplete.Close();
        }

        public bool GetCmdText(String outCmd)
        {
            int32 len = mEditWidgetContent.mData.mTextLength - mEntryStartPos.mIndex - 1;
            if (len <= 0)
                return false;
            mEditWidgetContent.ExtractString(mEntryStartPos.mIndex + 1, len, outCmd);
			return true;
        }

		[AlwaysInclude]
		public void ShowStr()
		{
			String str = scope String();
			mEditWidgetContent.ExtractString(0, mEditWidgetContent.mData.mTextLength, str);
		}

		public void HandleResult(String cmdText)
		{
			/*var root = (HoverWatch.HoverListViewItem)mResultHoverWatch.mListView.GetRoot();
			HoverWatch.HoverListViewItem result = null;
			if (root.GetChildCount() > 0)
				result = (HoverWatch.HoverListViewItem)root.GetChildAtIndex(0);*/

			mEditWidgetContent.CursorToEnd();

			if (mResultHoverWatch != null)
				delete mResultHoverWatch;

			mResultHoverWatch = new HoverWatch();
			mResultHoverWatch.mAllowLiterals = true;
			mResultHoverWatch.mAllowSideEffects = true;

			HoverWatch.HoverListViewItem result = null;
			if (cmdText == null)
			{
				// Is a continuation
				if (mLastEvalString != null)
					result = mResultHoverWatch.Eval(mLastEvalString, true);
				gApp.mIsImmediateDebugExprEval = true;
			}
			else
			{
				cmdText.Trim();

				if (cmdText == "cls")
				{
					mEditWidgetContent.ClearText();
					return;
				}

				if (cmdText.Length > 0)
				{
				    if ((mHistoryList.Count == 0) || (mHistoryList[mHistoryList.Count - 1] != cmdText))
				        mHistoryList.Add(new String(cmdText));
	
				    mUsedHistory = false;
					if (cmdText.StartsWith("%"))
					{
						gApp.mScriptManager.Clear();
						gApp.mScriptManager.QueueCommands(StringView(cmdText, 1), "Immediate", .NoLines);
					}
					else
				    	result = mResultHoverWatch.Eval(cmdText, false);
					mLastTextVersionId = mEditWidgetContent.mData.mCurTextVersionId;
					gApp.mIsImmediateDebugExprEval = true;
				}
			}

			if (result != null)
			{
				if (result.mWatchEntry.mIsPending)
				{
					if (cmdText != null)
					{
						delete mLastEvalString;
						mLastEvalString = new String(cmdText);
					}

					//delete mPendingHoverWatch;
					mEditWidgetContent.mIsReadOnly = true;
					return;
				}

				IDEApp.sApp.MemoryEdited();
			}

			delete mLastEvalString;
			mLastEvalString = null;
			mEditWidgetContent.mIsReadOnly = false;

			float resultX;
			float resultY;
			mEditWidgetContent.GetTextCoordAtCursor(out resultX, out resultY);

			int32 startPos = mEditWidgetContent.mData.mTextLength;
			if (result != null)
			{
				var subItemLabel = result.GetSubItem(1).mLabel;
				if (subItemLabel == null)
					subItemLabel = "";
			    mEditWidgetContent.AppendText(scope String("   ", subItemLabel, "\n"));

				if (result.mWatchEntry.mWarnings != null)
				{
					int32 warnStart = mEditWidgetContent.mData.mTextLength;

					for (var warning in result.mWatchEntry.mWarnings)
					{
						mEditWidgetContent.AppendText("   ");
						mEditWidgetContent.AppendText(warning);
						mEditWidgetContent.AppendText("\n");
					}

					for (int32 i = warnStart; i < mEditWidgetContent.mData.mTextLength; i++)
						mEditWidgetContent.mData.mText[i].mDisplayTypeId = (uint8)SourceElementType.BuildWarning;
				}
			}

			if (mInfoButton != null)
			{
			    if (mInfoButton.mParent != null)
			        mInfoButton.RemoveSelf();
			}
			else
			{
			    mInfoButton = new ImmediateInfoButton();
			    mInfoButton.mImmediateWidget = this;
			}

			if (result != null)
			{
			    if (result.Failed)
			    {
			        for (int32 i = startPos; i < mEditWidgetContent.mData.mTextLength; i++)
			            mEditWidgetContent.mData.mText[i].mDisplayTypeId = (uint8)SourceElementType.Error;
			    }
			    else
			    {
			        mInfoButton.Resize(resultX - GS!(3), resultY - GS!(2), GS!(20), GS!(20));
			        mEditWidgetContent.AddWidget(mInfoButton);
			    }
			}

			//mEditWidgetContent.AppendText(vals[0] + "\n");
			StartEntry();
		}

        public override void KeyChar(char32 c)
        {
			if (mEditWidgetContent.mIsReadOnly)
			{
				mDeferredChars.Add(c);
				return;
			}

            String cmdText = scope String();
			bool hasCmd = false;
            
            mWasTextModified = true;
            
            if ((c == '\r') || (c == '\n'))
            {
                //mChangeInvalidatesHistory = false;

				//We removed this because we don't really want to autocomplete on ENTER
                /*var editWidgetContent = (ImmediateWidgetContent)mEditWidgetContent;
                if ((editWidgetContent.mAutoComplete != null) && (editWidgetContent.mAutoComplete.IsShowing()))
                {
                    // Fake a 'tab' so it just completes
                    base.KeyChar('\t');
                }*/

				var editWidgetContent = (ImmediateWidgetContent)mEditWidgetContent;
				if (editWidgetContent.mAutoComplete != null)
					editWidgetContent.mAutoComplete.Close();

                GetCmdText(cmdText);
				hasCmd = true;
                /*if (mEditWidgetContent.mText[mEditWidgetContent.mTextLength - 1].mChar == (byte)'\n')
                    hasCR = true;*/
                //mChangeInvalidatesHistory = true;
            }
            else
            {
                //IDEApp.sApp.mBfResolveSystem.StartTiming();
                base.KeyChar(c);
                //IDEApp.sApp.mBfResolveSystem.StopTiming();
                //IDEApp.sApp.mBfResolveSystem.DbgPrintTimings();
            }

            if (hasCmd)
            {
                mEditWidgetContent.AppendText("\n");
                mEditWidgetContent.CursorTextPos = mEditWidgetContent.mData.mTextLength;
				mEditWidgetContent.mIsReadOnly = true;

				HandleResult(cmdText);
            }
        }
		
        void SelectEntry()
        {
            mEditWidgetContent.mSelection = EditSelection(mEntryStartPos.mIndex + 1, mEditWidgetContent.mData.mTextLength);
            mEditWidgetContent.CursorTextPos = mEntryStartPos.mIndex + 1;
        }

        public void ClearEntry()
        {
            SelectEntry();
            mEditWidgetContent.DeleteSelection();
            mHistoryIdx = -1;
            mUsedHistory = false;
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {         
            base.KeyDown(keyCode, isRepeat);

            var editWidgetContent = (ImmediateWidgetContent)mEditWidgetContent;
            if ((editWidgetContent.mAutoComplete != null) && (editWidgetContent.mAutoComplete.IsShowing()))
                return;

            /*if (keyCode == KeyCode.Escape)
            {
                ClearEntry();
            }*/

            if (((keyCode == KeyCode.Up) || (keyCode == KeyCode.Down)) && (mHistoryList.Count > 0))
            {
                if (mUsedHistory)
                {
                    if (keyCode == KeyCode.Up)
                        mHistoryIdx = Math.Max(mHistoryIdx - 1, 0);                    
                    else
                        mHistoryIdx = Math.Min(mHistoryIdx + 1, (int32)mHistoryList.Count - 1);
                }
                else
                    mUsedHistory = true;
                if (mHistoryIdx == -1)
                    mHistoryIdx = (int32)mHistoryList.Count - 1;

                String command = mHistoryList[mHistoryIdx];
                SelectEntry();
                int32 cmdRevIdx = mHistoryIdx;
                mEditWidgetContent.InsertAtCursor(command);
                // Restore this, because InsertAtCursor sets mCommandRevIdx to -1
                mHistoryIdx = cmdRevIdx;
            }
        }

        public virtual void UpdateText(bool doAutoComplete)
        {
            if (mLastTextVersionId != 0)
                mWasTextModified = true;

            String val = scope String();
            String cmdText = scope String();
            GetCmdText(cmdText);
			cmdText.Trim();

            if (cmdText.Length > 0)
			{
				if (cmdText.StartsWith("%"))
				{
					// Do nothing
				}
				else
				{
	                gApp.DebugEvaluate(null, cmdText, val, mEditWidgetContent.CursorTextPos - mEntryStartPos.mIndex - 2);
					gApp.mIsImmediateDebugExprEval = true;
				}
			}

            //var vals = String.StackSplit!(val, '\n');
			var valEnum = val.Split('\n');

            if (doAutoComplete)
            {
                var editWidgetContent = (ImmediateWidgetContent)mEditWidgetContent;

                int32 idx = (int32)val.IndexOf(":autocomplete\n", false);
                if (idx != -1)
                {
                    if (editWidgetContent.mAutoComplete == null)
                    {
                        editWidgetContent.mAutoComplete = new AutoComplete(this);
                        editWidgetContent.mAutoComplete.mOnAutoCompleteInserted.Add(new () => UpdateText(true));
                        editWidgetContent.mAutoComplete.mOnClosed.Add(new () => { editWidgetContent.mAutoComplete = null; });
                    }
					var info = scope String()..Append(val, idx + ":autocomplete\n".Length);
					if (!editWidgetContent.mAutoComplete.mIsDocumentationPass)
                    	editWidgetContent.mAutoComplete.SetInfo(info, true, mEntryStartPos.mIndex + 1);
                }
                else if (editWidgetContent.mAutoComplete != null)
                    editWidgetContent.mAutoComplete.Close();                
            }
            
            mErrorStart = 0;
            mErrorEnd = 0;
			if (valEnum.MoveNext())
			{
	            String result = scope String(valEnum.Current);
	            if (result.StartsWith("!", StringComparison.Ordinal))
	            {
					result.Remove(0);
	                var errorVals = String.StackSplit!(result, '\t');
	                if (errorVals.Count > 1)
	                {
	                    mErrorStart = int32.Parse(errorVals[0]).Get();
	                    mErrorEnd = mErrorStart + int32.Parse(errorVals[1]).Get();

						if (mErrorStart >= cmdText.Length)
						{
							// Just make it be the whole expression
							mErrorStart = 0;
						}

						mErrorEnd = (int32)Math.Min(mErrorEnd, cmdText.Length);

						int32 errOfs = mEntryStartPos.mIndex + 1;
						mErrorStart += errOfs;
						mErrorEnd += errOfs;
	                }                
	            }
			}
            
            mLastTextVersionId = mEditWidgetContent.mData.mCurTextVersionId;
        }

        public override void RemovedFromParent(Widget previousParent, WidgetWindow previousWidgetWindow)
        {
            base.RemovedFromParent(previousParent, previousWidgetWindow);

            var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
            if (editWidgetContent.mAutoComplete != null)
                editWidgetContent.mAutoComplete.Close();
        }

        public override void Update()
        {
            base.Update();

            if ((mLastTextVersionId != mEditWidgetContent.mData.mCurTextVersionId) && (!IsPending))
            {
                UpdateText(false);
            }

            if (IDEApp.sApp.mDebugger.mIsRunning)
            {
                ////
            }

			if ((mResultHoverWatch != null) && (mResultHoverWatch.mListView != null))
			{
				var listViewItems = (HoverWatch.HoverListViewItem)mResultHoverWatch.mListView.GetRoot().GetChildAtIndex(0);
				if ((listViewItems.mWatchEntry.mIsPending) && (gApp.mDebugger.IsPaused(true)) && (!gApp.mDebugger.HasMessages()))
				{
					HandleResult(null);
				}
			}

			if (!mDeferredChars.IsEmpty)
			{
				var c = mDeferredChars.PopFront();
				KeyChar(c);
			}
        }

        public void StartEntry()
        {
            mEditWidgetContent.AppendText("> ");
			mEditWidgetContent.mData.mUndoManager.Clear();
            mEditWidgetContent.mSelection = null;
            mEditWidgetContent.mCursorTextPos = mEditWidgetContent.mData.mTextLength;
			mEditWidgetContent.mIsReadOnly = false;
            mEntryStartPos.mWasDeleted = false;
            mEntryStartPos.mIndex = mEditWidgetContent.mData.mTextLength - 1;
        }        
		
		public void RehupResult()
		{
			// Replace the old string with the new format
			var listViewItem = mResultHoverWatch.mListView.GetRoot().GetChildAtIndex(0);
			var subItem1 = listViewItem.GetSubItem(1);
			
			var content = Content;
			let prevSelection = content.mSelection;
			defer { content.mSelection = prevSelection; }
			
			int line;
			int lineChar;
			float overflowX;
			content.GetLineCharAtCoord(mInfoButton.CenterX, mInfoButton.CenterY, out line, out lineChar, out overflowX);

			int lineStart;
			int lineEnd;
			content.GetLinePosition(line, out lineStart, out lineEnd);

			String replaceStr = scope String();
			replaceStr.Append("   ");
			replaceStr.Append(subItem1.Label);

			content.mSelection = EditSelection(lineStart, lineEnd);
			content.CursorTextPos = lineStart;
			content.InsertAtCursor(replaceStr);

			content.CursorTextPos = content.mData.mTextLength;
		}
    }
}
