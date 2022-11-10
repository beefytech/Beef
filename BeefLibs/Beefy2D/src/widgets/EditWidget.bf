using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading;
using Beefy.events;
using Beefy.gfx;
using Beefy.utils;
using System.IO;

//#define INCLUDE_CHARDATA_CHARID

namespace Beefy.widgets
{
    public struct EditSelection
    {
        public int32 mStartPos;        
        public int32 mEndPos;

        public this(int startPos = 0, int endPos = 0)
        {
			Debug.Assert(startPos >= 0);
			Debug.Assert(endPos >= 0);
            mStartPos = (int32)startPos;
            mEndPos = (int32)endPos;
        }

        public EditSelection Clone()
        {
            EditSelection aSelection = EditSelection();
            aSelection.mStartPos = mStartPos;            
            aSelection.mEndPos = mEndPos;            
            return aSelection;
        }

        public bool HasSelection
        {
            get { return (mStartPos != mEndPos); }
        }

        public bool IsForwardSelect
        {
            get { return (mStartPos < mEndPos); }
        }
        
        public int MinPos
        {
            get { return (IsForwardSelect) ? mStartPos : mEndPos; }
        }

        public int MaxPos
        {
            get { return (IsForwardSelect) ? mEndPos : mStartPos; }
        }

		public int Length
		{
			get { return Math.Abs(mEndPos - mStartPos); }
		}

        public void GetAsForwardSelect(out int startPos, out int endPos)
        {
            if (IsForwardSelect)
            {
                startPos = mStartPos;                
                endPos = mEndPos;                
            }
            else
            {
                startPos = mEndPos;                
                endPos = mStartPos;                
            }
        }

        public void MakeForwardSelect() mut
        {
            if (mEndPos < mStartPos)
            {
                int32 swap = mStartPos;
                mStartPos = mEndPos;
                mEndPos = swap;
            }
        }
    }  

    public delegate void EditWidgetEventHandler(EditEvent theEvent);

    public class EditWidgetContent : Widget
    {
        public enum TextFlags
        {
            Wrap = 1
        };        

        //[StructLayout(LayoutKind.Explicit) ]
        public struct CharData
        {
            public char8 mChar;
            public uint8 mDisplayPassId;
            public uint8 mDisplayTypeId; // For colorization, for example
            public uint8 mDisplayFlags;
        }

        public class TextAction : UndoAction
        {
            public EditWidgetContent.Data mEditWidgetContentData;
            public EditSelection? mSelection;
            public bool mRestoreSelectionOnUndo;
            public bool mMoveCursor = true;

            public int32 mPrevTextVersionId;
            public int32 mCursorTextPos;            
            public LineAndColumn? mVirtualCursorPos;

			// Return either the focused edit widget content, or the last one added
			public EditWidgetContent EditWidgetContent
			{
				get
				{
					EditWidgetContent editWidgetContent = null;
					for (var user in mEditWidgetContentData.mUsers)
					{
						editWidgetContent = user;
						if (editWidgetContent.mEditWidget.mHasFocus)
							break;
					}
					return editWidgetContent;
				}
			}

            public this(EditWidgetContent editWidget)
            {
                mPrevTextVersionId = editWidget.mData.mCurTextVersionId;
                mEditWidgetContentData = editWidget.mData;
				if (editWidget.HasSelection())
                	mSelection = editWidget.mSelection;
                mCursorTextPos = (int32)editWidget.CursorTextPos;
                mVirtualCursorPos = editWidget.mVirtualCursorPos;
            }

            public void SetPreviousState(bool force)
            {
				var editWidgetContent = EditWidgetContent;
                mEditWidgetContentData.mCurTextVersionId = mPrevTextVersionId;
                if ((mRestoreSelectionOnUndo) || (force))
                    editWidgetContent.mSelection = mSelection;
                else
                    editWidgetContent.mSelection = null;
                editWidgetContent.mCursorTextPos = mCursorTextPos;
                editWidgetContent.mVirtualCursorPos = mVirtualCursorPos;
                if (mMoveCursor)
                    editWidgetContent.EnsureCursorVisible();                
            }
        }

        public class SetCursorAction : TextAction
        {
            public this(EditWidgetContent editWidget) : base(editWidget)
            {
                mRestoreSelectionOnUndo = true;
                mMoveCursor = false;
            }

            public override bool Undo()
            {
                SetPreviousState(false);
                return true;
            }

            public override bool Redo()
            {
                SetPreviousState(true);
                return true;
            }
        }

        public class TextUndoBatchStart : TextAction, IUndoBatchStart
        {
            public String mName;
            public UndoBatchEnd mBatchEnd;
            public String Name
            {
                get
                {
                    return mName;
                }
            }

            public this(EditWidgetContent editWidget, String name) 
                : base(editWidget)
            {
                mName = name;
                mBatchEnd = new UndoBatchEnd();
                mBatchEnd.mBatchStart = this;
            }

            public override bool Undo()
            {
                SetPreviousState(false);
                return true;
            }

            public IUndoBatchEnd BatchEnd
            {
                get
                {
                    return mBatchEnd;
                }
            }
        }

        public class DeleteSelectionAction : TextAction
        {
            public String mSelectionText;            

            public this(EditWidgetContent editWidgetContent)
                : base(editWidgetContent)
            {
                if (mSelection != null)
				{
					mSelectionText = new String();
                    editWidgetContent.GetSelectionText(mSelectionText);
				}
            }

			~this()
			{
				delete mSelectionText;
			}

            public override bool Undo()
            {
				var editWidgetContent = EditWidgetContent;
                if (mSelection != null)
                {
                    bool wantSnapScroll = mEditWidgetContentData.mTextLength == 0;
                    editWidgetContent.CursorTextPos = mSelection.Value.MinPos;
                    editWidgetContent.PhysInsertAtCursor(mSelectionText, mMoveCursor);
                    if (wantSnapScroll)
                        editWidgetContent.mEditWidget.FinishScroll();
                }
                SetPreviousState(false);
                return true;
            }

            public override bool Redo()
            {
                SetPreviousState(true);
                if (mSelection != null)
                    EditWidgetContent.PhysDeleteSelection(mMoveCursor);
                return true;
            }
        }

        public class IndentTextAction : TextAction
        {
            // InsertCharList is for block indent, RemoveCharList is for unindent (shift-tab)
            public List<(int32, char8)> mRemoveCharList = new .() ~ delete _;
            public List<(int32, char8)> mInsertCharList = new .() ~ delete _;
            public EditSelection mNewSelection;

            public this(EditWidgetContent editWidget)
                : base(editWidget)
            {
                
            }

            public override bool Undo()
            {
				var editWidgetContent = EditWidgetContent;
                for (int idx = mRemoveCharList.Count - 1; idx >= 0; idx--)
                {
                    var idxChar = mRemoveCharList[idx];
					
                    editWidgetContent.InsertText(idxChar.0, ToStackString!(idxChar.1));
                }
                
                for (int idx = mInsertCharList.Count - 1; idx >= 0; idx--)                
                {
                    var idxChar = mInsertCharList[idx];
                    editWidgetContent.RemoveText(idxChar.0, 1);
                }
                editWidgetContent.ContentChanged();
                SetPreviousState(false);                
                return true;
            }

            public override bool Redo()
            {
				var editWidgetContent = EditWidgetContent;
                SetPreviousState(true);
                for (var idxChar in mRemoveCharList)
                    editWidgetContent.RemoveText(idxChar.0, 1);
				var charStr = scope String(' ', 1);
                for (var idxChar in mInsertCharList)
				{
					charStr[0] = idxChar.1;
                    editWidgetContent.InsertText(idxChar.0, charStr);
				}
                editWidgetContent.ContentChanged();
                editWidgetContent.mSelection = mNewSelection;
                editWidgetContent.CursorTextPos = mNewSelection.mEndPos;

                return true;
            }
        }

        public class InsertTextAction : DeleteSelectionAction
        {            
            public String mText ~ delete _;

            public this(EditWidgetContent editWidget, String text, InsertFlags insertFlags = .NoMoveCursor) 
                : base(editWidget)
            {                
                mText = new String(text);
                mRestoreSelectionOnUndo = !insertFlags.HasFlag(.NoRestoreSelectionOnUndo);                
            }

            public override bool Merge(UndoAction nextAction)
            {
                InsertTextAction insertTextAction = nextAction as InsertTextAction;
                if (insertTextAction == null)
                    return false;
                if (insertTextAction.mSelection != null)
                {
                    if (mSelection == null)
                        return false;

                    if (mSelection.Value.mEndPos != insertTextAction.mSelection.Value.mStartPos)
                        return false;

                    mSelection.ValueRef.mEndPos = insertTextAction.mSelection.Value.mEndPos;
					if (!mSelection.Value.HasSelection)
						mSelection = null;
                    mSelectionText.Append(insertTextAction.mSelectionText);
                }

                int curIdx = mCursorTextPos;
                int nextIdx = insertTextAction.mCursorTextPos;
                mRestoreSelectionOnUndo &= insertTextAction.mRestoreSelectionOnUndo;
                
                if ((nextIdx != curIdx + mText.Length) ||
                    (mText.EndsWith("\n")) ||
                    (insertTextAction.mText == "\n"))
                    return false;

                mText.Append(insertTextAction.mText);
                return true;
            }

            public override bool Undo()
            {
				if (mSelection != null)
					Debug.Assert(mSelection.Value.HasSelection);

				var editWidgetContent = EditWidgetContent;
                int startIdx = (mSelection != null) ? mSelection.Value.MinPos : mCursorTextPos;
                editWidgetContent.RemoveText(startIdx, (int32)mText.Length);
                editWidgetContent.ContentChanged();
                base.Undo();
                editWidgetContent.ContentChanged();
                editWidgetContent.mData.mCurTextVersionId = mPrevTextVersionId;
                //mEditWidget.mVirtualCursorPos = mPrevVirtualCursorPos;
                return true;
            }

            public override bool Redo()
            {
				var editWidgetContent = EditWidgetContent;
                base.Redo();
                bool wantSnapScroll = mEditWidgetContentData.mTextLength == 0;
                editWidgetContent.PhysInsertAtCursor(mText, mMoveCursor);
                if (wantSnapScroll)
                    editWidgetContent.mEditWidget.FinishScroll();
                return true;
            }
        }

        public class DeleteCharAction : TextAction
        {
            String mText ~ delete _;
            int32 mOffset;

            public this(EditWidgetContent editWidget, int offset, int count) : 
                base(editWidget)
            {
				var editWidgetContent = EditWidgetContent;
                mOffset = (int32)offset;
                int32 textPos = mCursorTextPos;                
				mText = new String(count);
                editWidgetContent.ExtractString(textPos + offset, count, mText);
            }

            public override bool Merge(UndoAction nextAction)
            {
                DeleteCharAction deleteCharAction = nextAction as DeleteCharAction;
                if (deleteCharAction == null)
                    return false;                
                if ((deleteCharAction.mOffset < 0) != (mOffset < 0))
                    return false;

                int32 curIdx = mCursorTextPos;
                int32 nextIdx = deleteCharAction.mCursorTextPos;                

                if (nextIdx != curIdx + mOffset)
                    return false;

                mOffset += deleteCharAction.mOffset;

                if (mOffset < 0)
				{
					mText.Insert(0, deleteCharAction.mText);
				}
                else
				{
					mText.Append(deleteCharAction.mText);
				}

                return true;
            }

            public override bool Undo()
            {
                int32 textPos = mCursorTextPos;
                var editWidgetContent = EditWidgetContent;

                editWidgetContent.CursorTextPos = textPos + mOffset;

                editWidgetContent.PhysInsertAtCursor(mText, mMoveCursor);
				editWidgetContent.ContentChanged();
                SetPreviousState(false);
                return true;
            }

            public override bool Redo()
            {
				var editWidgetContent = EditWidgetContent;
                SetPreviousState(true);
                editWidgetContent.PhysDeleteChars(mOffset, (int32)mText.Length);
				editWidgetContent.ContentChanged();
                return true;
            }
        }

        public struct LineAndColumn
        {
            public int32 mLine;
            public int32 mColumn;

            public this(int line, int column)
            {
                mLine = (int32)line;
                mColumn = (int32)column;
            }
        }        

		public class Data
		{
			public CharData[] mText = new CharData[1] ~ delete _;
			public int32 mTextLength;
			public IdSpan mTextIdData = IdSpan.Empty ~ _.Dispose();
			public uint8[] mTextFlags ~ delete _;
			public List<int32> mLineStarts ~ delete _;
			public UndoManager mUndoManager = new UndoManager() ~ delete _;
			public int32 mNextCharId = 1; //
			public int32 mCurTextVersionId = 1; // Changes when text is modified
			//public int mCurComplexChangeId = 1; // Changes when text is modified by more than a single-character insertion or deletion

			public List<EditWidgetContent> mUsers = new List<EditWidgetContent>() ~ delete _;

			public ~this()
			{
				
			}

			public void Ref(EditWidgetContent content)
			{
				mUsers.Add(content);
			}

			public void Deref(EditWidgetContent content)
			{
				mUsers.Remove(content);
				if (mUsers.Count == 0)
					delete this;
			}

			public char8 SafeGetChar(int idx)
			{
				if ((idx < 0) || (idx >= mTextLength))
					return 0;
				return mText[idx].mChar;
			}

			public char32 GetChar32(int idx)
			{
				int checkIdx = idx;
				char32 c = (char32)SafeGetChar(checkIdx++);
				if (c < (char32)0x80)
					return c;

				int8 trailingBytes = UTF8.sTrailingBytesForUTF8[c];
				switch (trailingBytes)
				{
				case 3: c <<= 6; c += (int)SafeGetChar(checkIdx++); fallthrough;
				case 2: c <<= 6; c += (int)SafeGetChar(checkIdx++); fallthrough;
				case 1: c <<= 6; c += (int)SafeGetChar(checkIdx++); fallthrough;
				}
				return c - (int32)UTF8.sOffsetsFromUTF8[trailingBytes];
			}

			public char32 GetChar32(ref int idx)
			{
				char32 c = (char32)SafeGetChar(idx++);
				if (c < (char32)0x80)
					return c;

				int8 trailingBytes = UTF8.sTrailingBytesForUTF8[c];
				switch (trailingBytes)
				{
				case 3: c <<= 6; c += (int)SafeGetChar(idx++); fallthrough;
				case 2: c <<= 6; c += (int)SafeGetChar(idx++); fallthrough;
				case 1: c <<= 6; c += (int)SafeGetChar(idx++); fallthrough;
				}
				return c - (int32)UTF8.sOffsetsFromUTF8[trailingBytes];
			}
		}

		public enum DragSelectionKind
		{
			None,
			Dragging,
			ClickedInside,
			DraggingInside
		}

		public Data mData ~ _.Deref(this);

        public Insets mTextInsets = new Insets() ~ delete _;
        public float mHorzJumpSize = 1;
        public bool mAutoHorzScroll = true;
        public float mShowLineBottomPadding;
		public bool mContentChanged;

        public EditWidget mEditWidget;
                
        public float mTabSize;
		public bool	mWantsTabsAsSpaces;
        public float mCharWidth = -1;
		public int32 mTabLength = 4;
        public uint8 mExtendDisplayFlags;
		public uint8 mInsertDisplayFlags;
		
        public int32 mCursorBlinkTicks;                
		public bool mCursorImplicitlyMoved;
        public bool mJustInsertedCharPair; // Pressing backspace will delete last char8, even though cursor is between char8 pairs (ie: for brace pairs 'speculatively' inserted)
        public EditSelection? mSelection;
		public EditSelection? mDragSelectionUnion; // For double-clicking a word and then "dragging" the selection
		public DragSelectionKind mDragSelectionKind;
        public bool mIsReadOnly = false;
		public bool mWantsUndo = true;
        public bool mIsMultiline = false;
        public bool mWordWrap = false;
        public float mCursorWantX; // For keyboard cursor selection, accounting for when we truncate to line end        
        public bool mOverTypeMode = false;                
        
        public int32 mCursorTextPos;
        public bool mShowCursorAtLineEnd;
        public LineAndColumn? mVirtualCursorPos;
        public bool mEnsureCursorVisibleOnModify = true;
        public bool mAllowVirtualCursor;
        public bool mAllowMaximalScroll = true; // Allows us to scroll down such that edit widget is blank except for one line of content at the top
        public float mMaximalScrollAddedHeight; // 0 is mAllowMaximalScroll is false

        public int CursorTextPos
        {
            get
            {                
                if (mCursorTextPos == -1)
                {
                    float x;
                    float y;
                    GetTextCoordAtLineAndColumn(mVirtualCursorPos.Value.mLine, mVirtualCursorPos.Value.mColumn, out x, out y);

                    int lineChar;
                    float overflowX;
                    GetLineCharAtCoord(mVirtualCursorPos.Value.mLine, x, out lineChar, out overflowX);

                    mCursorTextPos = (int32)GetTextIdx(mVirtualCursorPos.Value.mLine, lineChar);   
                }

                return mCursorTextPos;
            }

            set
            {   
             	Debug.Assert(value >= 0);
                mVirtualCursorPos = null;
                mCursorTextPos = (int32)value;
            }
        }
        
        public LineAndColumn CursorLineAndColumn
        {
            get
            {                
                if (mVirtualCursorPos.HasValue)
                    return mVirtualCursorPos.Value;
                LineAndColumn lineAndColumn;

                int line;
                int lineChar;
                GetLineCharAtIdx(mCursorTextPos, out line, out lineChar);
                
				int coordLineColumn;
                GetLineAndColumnAtLineChar(line, lineChar, out coordLineColumn);
				lineAndColumn.mLine = (int32)line;
				lineAndColumn.mColumn = (int32)coordLineColumn;
                return lineAndColumn;
            }

            set
            {
                mVirtualCursorPos = value;
                mCursorTextPos = -1;
                Debug.Assert(mAllowVirtualCursor);
                Debug.Assert(mVirtualCursorPos.Value.mColumn >= 0);
            }
        }

		public int32 CursorLine
		{
			get
			{
				if (mVirtualCursorPos.HasValue)
				    return mVirtualCursorPos.Value.mLine;
				
				int line;
				int lineChar;
				GetLineCharAtIdx(mCursorTextPos, out line, out lineChar);
				return (.)line;
			}
		}

		public bool WantsUndo
		{
			get
			{
				return (!mIsReadOnly) && (mWantsUndo);
			}
		}

        public this(EditWidgetContent refContent = null)
        {
			if (refContent != null)			
				mData = refContent.mData;
			else
				mData = CreateEditData();
			mData.Ref(this);
			mContentChanged = true;
        }


		protected virtual Data CreateEditData()
		{
			return new Data();
		}

        public uint8[] mTempCopyArr;

        public virtual bool CheckReadOnly()
        {
            return mIsReadOnly;
        }

        public bool TryGetCursorTextPos(out int textPos, out float overflowX)
        {
            if (mVirtualCursorPos.HasValue)
            {
				int32 line = mVirtualCursorPos.Value.mLine;
                float x;
                float y;
                GetTextCoordAtLineAndColumn(line, mVirtualCursorPos.Value.mColumn, out x, out y);

                int lineChar;                
                bool success = GetLineCharAtCoord(line, x, out lineChar, out overflowX);

                textPos = GetTextIdx(line, lineChar);
                return success;
            }
            textPos = mCursorTextPos;
            overflowX = 0;
            return true;
        }

        public bool TryGetCursorTextPos(out int textPos)
        {
            float overflowX;
            return TryGetCursorTextPos(out textPos, out overflowX);
        }

		public (char32, int) GetChar32(int idx)
		{
			char32 c = mData.mText[idx].mChar;
			if (c < '\x80')
				return (c, 1);
		
			EditWidgetContent.CharData* readPtr = &mData.mText[idx + 1];
			
			int trailingBytes = UTF8.sTrailingBytesForUTF8[c];
			if (idx + trailingBytes + 1 >= mData.mTextLength)
			{
				return (c, 1);
			}

			switch (trailingBytes)
			{
			case 4: c <<= 6; c += (uint8)(readPtr++).mChar; fallthrough;
			case 3: c <<= 6; c += (uint8)(readPtr++).mChar; fallthrough;
			case 2:
				c <<= 6;
				c += (uint8)(readPtr++).mChar;
				fallthrough;
			case 1:
				c <<= 6;
				c += (uint8)(readPtr++).mChar;
				fallthrough;
			}
			c -= (int32)UTF8.sOffsetsFromUTF8[trailingBytes];

			return (c, trailingBytes + 1);
		}

        public void ExtractString(int startIdx, int length, String outStr)
        {
			Debug.Assert(length >= 0);
			char8* ptr = outStr.PrepareBuffer(length);

			CharData* char8DataPtr = mData.mText.CArray();
			for (int32 idx = 0; idx < length; idx++)
				ptr[idx] = char8DataPtr[idx + startIdx].mChar;
        }

		public void ExtractLine(int line, String outStr)
		{
			GetLinePosition(line, var lineStart, var lineEnd);
			ExtractString(lineStart, lineEnd - lineStart, outStr);
		}

		public char8 SafeGetChar(int idx)
		{
			if ((idx < 0) || (idx >= mData.mTextLength))
				return 0;
			return mData.mText[idx].mChar;
		}

        static bool IsNonBreakingChar(char8 c)
        {
			// Assume any 'high' codepoints are non-breaking
            return (c.IsLetterOrDigit) || (c == '_') || (c >= '\x80');
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
            mEditWidget.SetFocus();

			if (HasSelection())
			{
				GetLineCharAtCoord(x, y, var line, var lineChar, ?);
				int textPos = GetTextIdx(line, lineChar);
				if ((textPos > mSelection.Value.MinPos) && (textPos < mSelection.Value.MaxPos))
				{
					// Leave selection
					mDragSelectionKind = .ClickedInside;
					return;
				}
			}

            if ((mSelection == null) && (mWidgetWindow.IsKeyDown(KeyCode.Shift)))
                StartSelection();

            MoveCursorToCoord(x, y);
			ClampCursor();

            //PrintF("~TestStruct() %d\n", mInner.mVal1);            

			if ((btn == 0) && (btnCount >= 3) && (!mWidgetWindow.IsKeyDown(KeyCode.Shift)))
			{
				GetLinePosition(CursorLineAndColumn.mLine, var lineStart, var lineEnd);
				mSelection = EditSelection(lineStart, lineEnd);
			}
            else if ((btn == 0) && (btnCount >= 2) && (!mWidgetWindow.IsKeyDown(KeyCode.Shift)))
            {
                // Select word
                StartSelection();

                int cursorTextPos = CursorTextPos;
                int textPos = cursorTextPos;

                if (mData.mTextLength == 0)
                {   
                    //Nothing
                }
                else if ((textPos < mData.mTextLength) && (!IsNonBreakingChar((char8)mData.mText[textPos].mChar)))
                {                
   					if ((char8)mData.mText[textPos].mChar == '\n')
						return;
                    mSelection.ValueRef.mEndPos++;
                }
                else
                {
                    while ((textPos > 0) && (IsNonBreakingChar((char8)mData.mText[textPos - 1].mChar)))
                    {
                        mSelection.ValueRef.mStartPos--;
                        textPos--;
                    }

                    textPos = cursorTextPos + 1;
                    while ((textPos <= mData.mTextLength) && ((textPos == mData.mTextLength) || (mData.mText[textPos - 1].mChar != '\n')) &&
                        (IsNonBreakingChar((char8)mData.mText[textPos - 1].mChar)))
                    {
                        mSelection.ValueRef.mEndPos++;
                        textPos++;
                    }
                }

				mDragSelectionUnion = mSelection;
				CursorTextPos = mSelection.Value.MaxPos;
            }
            else if (!mWidgetWindow.IsKeyDown(KeyCode.Shift))
            {
				StartSelection();
			}
            else
                SelectToCursor();
        }

		public override void MouseUp(float x, float y, int32 btn)
		{
		    base.MouseUp(x, y, btn);

		    if (mMouseFlags == 0)
		    {
		        mDragSelectionUnion = null;
				if ((mDragSelectionKind == .ClickedInside) && (btn == 0))
				{
					mSelection = null;
					MoveCursorToCoord(x, y);
				}
				mDragSelectionKind = .None;
		    }
		}

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);

            if ((mMouseDown) && (mMouseFlags == .Left))
            {
				if (mDragSelectionKind == .ClickedInside)
					mDragSelectionKind = .DraggingInside;
				if (mDragSelectionKind == .DraggingInside)
					return;

                MoveCursorToCoord(x, y);
				ClampCursor();
				if (mDragSelectionKind == .Dragging)
                	SelectToCursor();
            }
        }

        public override void MouseEnter()
        {            
            base.MouseEnter();
            BFApp.sApp.SetCursor(Cursor.Text);
        }

        public override void MouseLeave()
        {            
            base.MouseLeave();
            BFApp.sApp.SetCursor(Cursor.Pointer);
        }

        public virtual void ClearText()
        {
            SelectAll();
            DeleteSelection();
        }

        public void PhysDeleteSelection(bool moveCursor = true)
        {
            int startIdx = 0;            
            int endIdx = 0;            
            mSelection.Value.GetAsForwardSelect(out startIdx, out endIdx);
            
            RemoveText(startIdx, endIdx - startIdx);
            mSelection = null;
            
            CursorTextPos = startIdx;

            if (endIdx != startIdx)
                ContentChanged();
			if (moveCursor)
			{
	            EnsureCursorVisible();
				ResetWantX();
			}
        }

        public virtual void DeleteSelection(bool moveCursor = true)
        {
            if (mSelection != null)
            {
				var action = new DeleteSelectionAction(this);
				action.mMoveCursor = moveCursor;
                mData.mUndoManager.Add(action);
                PhysDeleteSelection(moveCursor);
            }
        }

        public bool GetSelectionText(String outStr)
        {
            if (mSelection == null)
                return false;

            int startIdx = 0;
            int endIdx = 0;
            mSelection.Value.GetAsForwardSelect(out startIdx, out endIdx);

            ExtractString(startIdx, endIdx - startIdx, outStr);
			return true;
        }

        void VerifyTextIds()
        {
#if INCLUDE_CHARDATA_CHARID
            /*if (mUpdateCnt > 0)
                Debug.WriteLine("IdLen: {0}", mTextIdDataLength);*/

            int curTextIdx = 0;
            int encodeIdx = 0;
            int curTextId = 1;
            while (true)
            {
                int cmd = Utils.DecodeInt(mTextIdData, ref encodeIdx); ;
                if (cmd == 0)
                {
                    Debug.Assert(encodeIdx == mTextIdDataLength);
                    break;
                }

                if (cmd > 0)
                {
                    curTextId = cmd;
                }
                else
                {
                    for (int i = 0; i < -cmd; i++)
                    {
                        Debug.Assert(mText[curTextIdx].mCharId == curTextId);
                        curTextIdx++;
                        curTextId++;
                    }
                }
            }
            Debug.Assert(curTextIdx == mTextLength);
#endif
        }

		protected virtual void AdjustCursorsAfterExternalEdit(int index, int ofs)
		{
#unwarn
			int cursorPos = CursorTextPos;
			if (cursorPos >= index)
				CursorTextPos = Math.Clamp(mCursorTextPos + (int32)ofs, 0, mData.mTextLength + 1);
			if (HasSelection())
			{				
				if (((ofs > 0) && (mSelection.Value.mStartPos >= index)) ||
					((ofs < 0) && (mSelection.Value.mStartPos > index)))
					mSelection.ValueRef.mStartPos += (int32)ofs;
				if (mSelection.Value.mEndPos > index)
					mSelection.ValueRef.mEndPos += (int32)ofs;
			}
		}

		public virtual void ApplyTextFlags(int index, String text, uint8 typeNum, uint8 flags)
		{
			uint8 curTypeNum = typeNum;

			for (int i = 0; i < text.Length; i++)
			{
			    char8 c = text[i];
			    mData.mText[i + index].mChar = (char8)c;
#if INCLUDE_CHARDATA_CHARID
			    mText[i + index].mCharId = mNextCharId;
#endif                
			    mData.mNextCharId++;
			    mData.mText[i + index].mDisplayTypeId = curTypeNum;
			    mData.mText[i + index].mDisplayFlags = flags;
			    if (c.IsWhiteSpace)
			        curTypeNum = 0;
			}
		}

		public virtual void GetInsertFlags(int index, ref uint8 typeId, ref uint8 flags)
		{
			// Leave it
		}

        public virtual void InsertText(int index, String text)
        {
			scope AutoBeefPerf("EWC.InsertText");
            int wantLen = mData.mTextLength + text.Length + 1;
            while (wantLen > mData.mText.Count)
            {
				scope AutoBeefPerf("EWC.InsertText:Resize");

                CharData[] oldText = mData.mText;
				int allocSize = Math.Max(mData.mText.Count * 3 / 2, wantLen);
                mData.mText = new CharData[allocSize];
				oldText.CopyTo(mData.mText, 0, 0, mData.mTextLength);
				delete oldText;
            }

            mData.mText.CopyTo(mData.mText, index, index + text.Length, mData.mTextLength - index);

            int32 curId = mData.mNextCharId;
            mData.mTextIdData.Insert(index, (int32)text.Length, ref curId);
            
            // We break attribute copying on spaces and revert back to zero
            uint8 curTypeNum = 0;
            uint8 curFlags = mInsertDisplayFlags;
            GetInsertFlags(index, ref curTypeNum, ref curFlags);

			ApplyTextFlags(index, text, curTypeNum, curFlags);

            mData.mTextLength += (int32)text.Length;
            mData.mCurTextVersionId++;
			TextChanged();

            VerifyTextIds();

			for (var user in mData.mUsers)
			{
				if (user != this)
				{
					user.AdjustCursorsAfterExternalEdit(index, (int32)text.Length);
					user.ContentChanged();
				}
			}
        }
    

        public virtual void RemoveText(int index, int length)
        {
			for (var user in mData.mUsers)
			{
				if (user != this)
				{
					user.AdjustCursorsAfterExternalEdit(index, -length);
					user.ContentChanged();
				}
			}

            if (length == 0)
                return;
			mData.mText.CopyTo(mData.mText, index + length, index, mData.mTextLength - (index + length));
            mData.mTextIdData.Remove(index, length);
            mData.mTextLength -= (int32)length;

			mData.mCurTextVersionId++;
			TextChanged();

            VerifyTextIds();			
        }

        /*public void PhysInsertAtCursor(String theString, bool moveCursor = true)
        {            
            int textPos = CursorTextPos;

			bool atEnd = textPos == mData.mTextLength;

            String str = theString;
            int strStartIdx = 0;
            while (true)
            {
                int crPos = str.IndexOf('\n', strStartIdx);
                if (crPos == -1)
                {
                    if (strStartIdx != 0)
						str = stack String(theString, strStartIdx);
                    break;
                }

                if (mIsMultiline)
                {
                    //string lineString = aString.Substring(strStartIdx, crPos - strStartIdx + 1);
					String lineString = scope String();
					str.Substring(strStartIdx, crPos - strStartIdx + 1, lineString);
                    strStartIdx = crPos + 1;                    

                    InsertText((int32)textPos, lineString);
                    textPos += lineString.Length;
                }
                else
                {
                    // Only allow the first line in
                    //aString = aString.Substring(0, crPos);
					//aString.RemoveToEnd(crPos);

					str = stack String(theString, 0, crPos);
                }
            } 

           	InsertText((int32)textPos, str);

			//if (atEnd)
				//ContentAppended(str);
			//else
				ContentChanged();
            if (moveCursor)
            {
                textPos += str.Length;
                MoveCursorToIdx((int32)textPos);
                if (mEnsureCursorVisibleOnModify)
                    EnsureCursorVisible();
            }
        }*/

		public void PhysInsertAtCursor(String text, bool moveCursor = true)
		{
		    int textPos = CursorTextPos;

			bool atEnd = textPos == mData.mTextLength;

		    String insertStr = text;
		   	InsertText((int32)textPos, insertStr);

			if (atEnd)
				TextAppended(insertStr);
			else
				ContentChanged();
		    if (moveCursor)
		    {
		        textPos += insertStr.Length;
		        MoveCursorToIdx((int32)textPos, false, .FromTyping);
		        if (mEnsureCursorVisibleOnModify)
		            EnsureCursorVisible();
		    }
		}

        public void AppendText(String text)
        {
			scope AutoBeefPerf("EWC.AppendText");

			if (mAllowVirtualCursor)
			{
				let prevCursorPos = CursorLineAndColumn;
				CursorTextPos = mData.mTextLength;
				PhysInsertAtCursor(text);
				CursorLineAndColumn = prevCursorPos;
			}
			else
			{
				int prevCursorPos = CursorTextPos;
				CursorTextPos = mData.mTextLength;
				PhysInsertAtCursor(text);
				CursorTextPos = prevCursorPos;
			}
        }

		public enum InsertFlags
		{
			None,
			NoRestoreSelectionOnUndo = 1,
			NoMoveCursor = 2,
			NoRecordHistory = 4,
			IsGroupPart = 8,
			IsGroupStart = 0x10
		}

        public virtual void InsertAtCursor(String theString, InsertFlags insertFlags = .None)
        {
			scope AutoBeefPerf("EWC.InsertAtCursor");

			//String insertText = scope String();

			String insertString = theString;
			if (!mIsMultiline)
			{
				int crPos = insertString.IndexOf('\n');
				if (crPos != -1)
					insertString = scope:: String(theString, 0, crPos);
			}

			bool doCheck = true;

			//
			{
				scope AutoBeefPerf("FilterCheck");

				if (theString.Length > 32*1024)
				{
					bool[128] hasChar = default;

					doCheck = false;
					for (char32 c in theString.DecodedChars)
					{
						if (c < (char32)128)
							hasChar[(int)c] = true;
						else if (!AllowChar(c))
						{
							doCheck = true;
							break;
						}
					}

					if (!doCheck)
					{
						for (int i < 128)
						{
							if ((hasChar[i]) && (!AllowChar((char32)i)))
							{
								doCheck = true;
								break;
							}
						}
					}
				}

				if (doCheck)
				{
					//TODO: Make this use DecodedChars
				    for (int32 i = 0; i < insertString.Length; i++)
				    {
				        if (!AllowChar(insertString[i]))
				        {
							if (insertString == (Object)theString)
							{
								insertString = scope:: String();
								insertString.Set(theString);
							}

				            insertString.Remove(i, 1);
				            i--;
				        }
				    }
				}
			}

			Debug.Assert(!theString.StartsWith("\x06"));

            if ((!HasSelection()) && (mVirtualCursorPos.HasValue) && (theString != "\n"))
            {
                int textPos;
                TryGetCursorTextPos(out textPos);

                int line;
                int lineChar;
                GetLineCharAtIdx(textPos, out line, out lineChar);

                float textX;
                float textY;
                GetTextCoordAtLineChar(line, lineChar, out textX, out textY);

                float cursorX;
                float cursorY;
                GetTextCoordAtLineAndColumn(mVirtualCursorPos.Value.mLine, mVirtualCursorPos.Value.mColumn, out cursorX, out cursorY);

				if (cursorX > textX)
				{
					int lineStart;
					int lineEnd;

					GetLinePosition(line, out lineStart, out lineEnd);
					if (textPos == lineEnd)
					{
		                int prevTabCount = (int)((textX + 0.5f) / mTabSize);
		                int wantTabCount = (int)((cursorX + 0.5f) / mTabSize);
		                int wantAddTabs = 0;
						if (!mWantsTabsAsSpaces)
							wantAddTabs = wantTabCount - prevTabCount;                                
		                if (wantAddTabs > 0)                
		                    textX = wantTabCount * mTabSize + mTextInsets.mLeft;                

		                int wantSpaces = (int)((cursorX - textX + 0.5f) / mCharWidth);
		                //theString = new string(' ', wantSpaces) + theString;

						let prevString = insertString;
						insertString = scope:: String();

						insertString.Append('\t', wantAddTabs);
						insertString.Append(' ', wantSpaces);
						insertString.Append(prevString);
					}
				}
                //if (wantAddTabs > 0)
                    //theString = new string('\t', wantAddTabs) + theString;
            }
		    
			if ((insertString.Length == 0) && (!HasSelection()))
                return;

			if (WantsUndo)
            {
				var action = new InsertTextAction(this, insertString, insertFlags);
				action.mMoveCursor = !insertFlags.HasFlag(.NoMoveCursor);
				mData.mUndoManager.Add(action);
			}

            if (HasSelection())
                PhysDeleteSelection(!insertFlags.HasFlag(.NoMoveCursor));

            //StringBuilder insertText = new StringBuilder(theString);
            /*for (int32 i = 0; i < insertText.Length; i++)
            {
                if (!AllowChar(insertText[i]))
                {
                    insertText.Remove(i, 1);
                    i--;
                }
            }*/

			/*int curIdx = 0;
			for (var c in insertText.DecodedChars)
			{
				if (!AllowChar(c))
				{
				    insertText.Remove(curIdx, @c.NextIndex - curIdx);
					@c.NextIndex = curIdx;
				    continue;
				}

				curIdx = @c.NextIndex;
			}*/

            //theString = insertText.ToString();
            PhysInsertAtCursor(insertString, !insertFlags.HasFlag(.NoMoveCursor));
        }

        public virtual void ReplaceAtCursor(char8 c)
        {

        }

        public virtual void ClampCursor()
        {
            if (mVirtualCursorPos.HasValue)
            {
                var cursorPos = mVirtualCursorPos.Value;
                cursorPos.mLine = Math.Min(GetLineCount() - 1, cursorPos.mLine);
                mVirtualCursorPos = cursorPos;
            }
            mCursorTextPos = Math.Min(mCursorTextPos, mData.mTextLength);
        }

        public virtual void ContentChanged()
        {
            //TODO:
            //mCursorPos = GetTextIdx(mCursorLine, mCursorCharIdx);
			MarkDirty();

            mContentChanged = true;
			delete mData.mTextFlags;
            mData.mTextFlags = null;
			delete mData.mLineStarts;
            mData.mLineStarts = null;
            mData.mCurTextVersionId++;
			mDragSelectionUnion = null;

            mEditWidget.ContentChanged();

			TextChanged();
        }

		public virtual void LineStartsChanged()
		{

		}

		public virtual void TextAppended(String str)
		{
			if ((mWordWrap) || (mData.mLineStarts == null))
			{
				ContentChanged();
				return;
			}

			scope AutoBeefPerf("EWC.TextAppended");

			int textIdx = mData.mTextLength - str.Length;

			mData.mLineStarts.PopBack();
			for (char8 c in str.RawChars)
			{
				textIdx++;
				if (c == '\n')
				{
					mData.mLineStarts.Add((int32)textIdx);
				}
			}
			mData.mLineStarts.Add(mData.mTextLength);
			LineStartsChanged();

			mContentChanged = true;

			mData.mCurTextVersionId++;
			mDragSelectionUnion = null;

			//Debugging
			//ContentChanged();
			//GetTextData();

			mEditWidget.ContentChanged();
		}

        //Thread mCheckThread;

        public virtual void GetTextData()
        {
            // Generate line starts and text flags if we need to
            
            if (mData.mLineStarts != null)
				return;
            
			scope AutoBeefPerf("EWC.GetTextData");

			CharData* char8DataPtr = mData.mText.CArray();
			uint8* textFlagsPtr = null;
			if (mData.mTextFlags != null)
				textFlagsPtr = mData.mTextFlags.CArray();

            int32 lineIdx = 0;
			if (textFlagsPtr != null)
			{
				for (int32 i < mData.mTextLength)
				{
				    if ((char8DataPtr[i].mChar == '\n') || ((textFlagsPtr != null) && ((textFlagsPtr[i] & ((int32)TextFlags.Wrap)) != 0)))
				        lineIdx++;                    
				}
			}
            else
			{
				for (int32 i < mData.mTextLength)
				{
				    if (char8DataPtr[i].mChar == '\n')
				        lineIdx++;                    
				}
			}

            mData.mLineStarts = new List<int32>();
            mData.mLineStarts.GrowUnitialized(lineIdx + 2);
			int32* lineStartsPtr = mData.mLineStarts.Ptr;
			lineStartsPtr[0] = 0;
            
            lineIdx = 0;
			if (textFlagsPtr != null)
			{
                for (int32 i < mData.mTextLength)
                {
                    if ((textFlagsPtr != null) && ((textFlagsPtr[i] & ((int32)TextFlags.Wrap)) != 0))
                    {                        
                        lineIdx++;
                        lineStartsPtr[lineIdx] = i;                        
                    }
                    else if ((char8)char8DataPtr[i].mChar == '\n')
                    {                        
                        lineIdx++;
                        lineStartsPtr[lineIdx] = i + 1;
                    }
                }
			}
			else
			{
				for (int32 i < mData.mTextLength)
				{
				    if ((char8)char8DataPtr[i].mChar == '\n')
				    {                        
				        lineIdx++;
				        lineStartsPtr[lineIdx] = i + 1;
				    }
				}
			}
            
            mData.mLineStarts[lineIdx + 1] = mData.mTextLength;
			LineStartsChanged();
        }

        public virtual void Backspace()
        {
            if (mJustInsertedCharPair)
            {
                CursorTextPos++;
                mJustInsertedCharPair = false;
            }

            if (HasSelection())
            {
                DeleteSelection();
                return;
            }
            mSelection = null;

            int textPos = 0;
            if (!TryGetCursorTextPos(out textPos))
            {
                var lineAndColumn = CursorLineAndColumn;
                mCursorBlinkTicks = 0;
                CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, lineAndColumn.mColumn - 1);
                CursorMoved();
                return;
            }

            if (textPos == 0)
                return;

            int32 removeNum = -1;
            if (mData.mText[CursorTextPos - 1].mChar == '\n')
            {
                // Pulling up line - delete trailing whitespace if the line has no content
                int lineIdx = 0;
                int lineCharIdx = 0;
                GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineCharIdx);
                String lineText = scope String();
                GetLineText(lineIdx, lineText);
				String trimmedLineText = scope String(lineText);
				trimmedLineText.Trim();

                if ((lineText.Length > 0) && (trimmedLineText.Length == 0))
                {
                    CursorTextPos += (int32)lineText.Length;
                    removeNum = (int32)-lineText.Length - 1;
                }
            }

			// Roll back past UTF8 data if necessary
			while (true)
			{
				char8 c = mData.SafeGetChar(mCursorTextPos + removeNum);
				if ((uint8)c & 0xC0 != 0x80)
					break;
				removeNum--;
			}

            mData.mUndoManager.Add(new DeleteCharAction(this, removeNum, -removeNum));
            PhysDeleteChars(removeNum, -removeNum);
        }
        
        public virtual void PhysDeleteChars(int32 offset, int32 length = 1)
        {
            int textPos = CursorTextPos;

            RemoveText(textPos + offset, length);            
            textPos += offset;
            ContentChanged();
            if (offset != 0)
            {
                MoveCursorToIdx(textPos, false, .FromTyping_Deleting);
                EnsureCursorVisible();
            }
        }

        public virtual void ClearLine()
        {
            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);

            String lineText = scope String();
            GetLineText(line, lineText);

            var prevCursorLineAndCol = CursorLineAndColumn;

            mSelection = EditSelection();
            mSelection.ValueRef.mStartPos = (int32)(CursorTextPos - lineChar);
            mSelection.ValueRef.mEndPos = mSelection.Value.mStartPos + (int32)lineText.Length;
            DeleteSelection();

            CursorLineAndColumn = prevCursorLineAndCol;
        }

        public virtual void DeleteLine()
        {
            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);

            String lineText = scope String();
            GetLineText(line, lineText);

            var prevCursorLineAndCol = CursorLineAndColumn;

            mSelection = EditSelection();
            mSelection.ValueRef.mStartPos = (int32)(CursorTextPos - lineChar);
            mSelection.ValueRef.mEndPos = mSelection.ValueRef.mStartPos + (int32)lineText.Length + 1;
            DeleteSelection();

            CursorLineAndColumn = prevCursorLineAndCol;
        }

		public virtual void LinePullup(int textPos)
		{

		}

        public virtual void DeleteChar()
        {
			mJustInsertedCharPair = false;
            if (HasSelection())
            {
                DeleteSelection();
                return;
            }                        
            mSelection = null;

            int textPos = CursorTextPos;
            if (textPos >= mData.mTextLength)
                return;

            int line;
            int lineChar;
            GetLineCharAtIdx(textPos, out line, out lineChar);

            bool wasLineEnd = false;
			for (int i = textPos; i < mData.mTextLength; i++)
			{
				char8 c = mData.mText[i].mChar;
				if (c == '\n')
				{
					wasLineEnd = true;
					break;
				}
				else if (!c.IsWhiteSpace)
					break;
			}

            String lineText = scope String();
            GetLineText(line, lineText);

            if (wasLineEnd)
            {
                if (lineText.IsWhiteSpace)
                {                    
                    DeleteLine();
  
                    GetLineCharAtIdx(CursorTextPos, out line, out lineChar);

					lineText.Clear();
                    GetLineText(line, lineText);
                    int32 contentIdx;
                    for (contentIdx = 0; contentIdx < lineText.Length; contentIdx++)
                    {
                        if (!lineText[contentIdx].IsWhiteSpace)
                        {
                            CursorTextPos = GetTextIdx(line, contentIdx);
                            break;
                        }
                    }
                    
                    return;
                }

				UndoBatchStart undoBatchStart = new UndoBatchStart("PullUpLine");
				mData.mUndoManager.Add(undoBatchStart);

				// Add in any required virtual spaces
				InsertAtCursor("");
				textPos = CursorTextPos;

                while ((textPos < mData.mTextLength) && (lineChar != 0))
                {
                    mData.mUndoManager.Add(new DeleteCharAction(this, 0, 1));
                    PhysDeleteChars(0);

                    char8 c = (char8)mData.mText[textPos].mChar;
                    if ((c == '\n') || (!c.IsWhiteSpace))
					{
						LinePullup(textPos);
                        break;
					}
                }

				mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
            }
            else
            {
				int32 charCount = 1;
				int checkIdx = textPos + 1;
				while (true)
				{
					char8 c = mData.SafeGetChar(checkIdx);
					if (((uint8)c & 0xC0) != 0x80)
					{
						// Delete all the combining marks after the char8
						var checkChar = mData.GetChar32(checkIdx);
						if (!checkChar.IsCombiningMark)
							break;
					}
					charCount++;
					checkIdx++;
				}

                mData.mUndoManager.Add(new DeleteCharAction(this, 0, charCount));
                PhysDeleteChars(0, charCount);
            }                        
        }

        public virtual bool AllowChar(char32 c)
        {
			if ((c == '\t') && (!mIsMultiline))
				return false;
            return true;
        }
        
        public void InsertCharPair(String charPair)
        {
			if (CheckReadOnly())
				return;
            InsertAtCursor(charPair);
            MoveCursorToIdx(CursorTextPos - 1, false, .FromTyping);
            mJustInsertedCharPair = true;
        }

        public virtual bool WantsInsertCharPair(char8 theChar)
        {            
            return false;
        }

		public virtual int32 GetTabSpaceCount()
		{
		    return (int32)Math.Round(mTabSize / mCharWidth);
		}

        public override void KeyChar(char32 keyChar)
        {
            base.KeyChar(keyChar);

			if (keyChar == '\x7F') // Ctrl+Backspace
			{
				if (!CheckReadOnly())
				{
					int line;
					int lineChar;
					GetCursorLineChar(out line, out lineChar);

					int startIdx = CursorTextPos;
					SelectLeft(line, lineChar, true, false);
					mSelection = EditSelection(CursorTextPos, startIdx);
					
					var action = new DeleteSelectionAction(this);
					action.mMoveCursor = true;
					mData.mUndoManager.Add(action);
					action.mCursorTextPos = (.)startIdx;
					PhysDeleteSelection(true);
				}

				return;
			}

			char32 useChar = keyChar;

            mCursorBlinkTicks = 0;

            if (useChar == '\b')
            {
                if (!CheckReadOnly())
                {
					if (HasSelection())
					{
						DeleteSelection();
						return;
					}

                    int32 column = CursorLineAndColumn.mColumn;
                    int32 tabSpaceCount = GetTabSpaceCount();
                    if ((mAllowVirtualCursor) && (column != 0) && ((column % tabSpaceCount) == 0))
                    {
                        int lineStart;
                        int lineEnd;
                        GetLinePosition(CursorLineAndColumn.mLine, out lineStart, out lineEnd);

                        bool doBlockUnindent = false;
                        int cursorTextPos = 0;
                        float overflowX = 0;
                        if (!TryGetCursorTextPos(out cursorTextPos, out overflowX))
                        {
                            cursorTextPos += (int32)Math.Round(overflowX / mCharWidth);

                            for (int32 i = 0; i < tabSpaceCount; i++)
                            {
                                if (cursorTextPos - 1 < lineEnd)
                                {
                                    if (!((char8)mData.mText[cursorTextPos - 1].mChar).IsWhiteSpace)
                                        break;
                                    Backspace();
                                }
                                else
                                {                                    
                                    var virtualCursorPos = CursorLineAndColumn;
                                    virtualCursorPos.mColumn--;
                                    CursorLineAndColumn = virtualCursorPos;
                                }
                                cursorTextPos--;
                            }

                            return;
                        }
                        else if (cursorTextPos > 0)
                        {
                            Debug.Assert((cursorTextPos >= lineStart) && (cursorTextPos <= lineEnd));
                            if (((char8)mData.mText[cursorTextPos - 1].mChar).IsWhiteSpace)
                            {
                                doBlockUnindent = true;
                                // Cursor has to be at line end or only have whitespace infront of it (line start)
                                if (cursorTextPos < lineEnd)
                                {
                                    for (int i = lineStart; i < cursorTextPos; i++)
                                    {
                                        if (!((char8)mData.mText[i].mChar).IsWhiteSpace)
                                        {
                                            doBlockUnindent = false;
                                            break;
                                        }
                                    }
                                }                                    
                            }                            
                        }

                        if (doBlockUnindent)
                        {
                            BlockIndentSelection(true);
                            return;
                        }
                    }

                    Backspace();
                }
                return;
            }

            mJustInsertedCharPair = false;

            switch (useChar)
            {
            case '\r':
                useChar = '\n';
                break; 
            case '\t':
				if (AllowChar(useChar))
                	BlockIndentSelection(mWidgetWindow.IsKeyDown(KeyCode.Shift));
                return;
            }            
            
            if (useChar == '\n')
            {
                if ((!mIsMultiline) || (mWidgetWindow.IsKeyDown(KeyCode.Control)))
                {
					// Submit is handled in KeyDown
                    //mEditWidget.Submit();
                    return;
                }
            }                        

            if ((AllowChar(useChar)) && (!CheckReadOnly()))
            {                
                if ((useChar == '\n') && (!mAllowVirtualCursor))
                {              
                    int lineIdx;
                    int lineCharIdx;
                    GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineCharIdx);                    

                    // Duplicate the tab start from the previous line
                    if (lineIdx > 0)
                    {
                        int32 indentCount = 0;
                        String aLine = scope String();
                        GetLineText(lineIdx, aLine);
                        for (int32 i = 0; i < Math.Min(aLine.Length, lineCharIdx); i++)
                        {
                            if (aLine[i] != '\t')
                                break;
                            indentCount++;                            
                        }
                        aLine.Trim();
                        
                        UndoBatchStart undoBatchStart = new UndoBatchStart("newline");
                        mData.mUndoManager.Add(undoBatchStart);

                        if (aLine.Length == 0)
                        {
                            // Remove previous line's tabs (since that line was blank)
                            for (int32 i = 0; i < indentCount; i++)
                                Backspace();
                        }

                        InsertAtCursor(ToStackString!(useChar));
                        for (int32 i = 0; i < indentCount; i++)
                            InsertAtCursor("\t");                        
                        if (aLine.Length > 0)
                        {
                            char8 c = aLine[aLine.Length - 1];
                            if ((c == '(') || (c == '[') || (c == '{'))
                                InsertAtCursor("\t");
                        }

						if (WantsUndo)
                        	mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
                        return;
                    }                                                                  
                }

                bool restoreSelectionOnUndo = true;
                if ((mOverTypeMode) && (!HasSelection()))
                {
                    int cursorPos = CursorTextPos;
                    if (cursorPos < mData.mTextLength)
                    {
                        char8 c = (char8)mData.mText[cursorPos].mChar;
                        if ((c != '\n') && (c != '\r'))
                        {                            
                            mSelection = EditSelection(cursorPos, cursorPos + 1);
                            restoreSelectionOnUndo = false;
                        }
                    }
                }

				if ((useChar >= '\x20') || (useChar == '\n'))
                {
					InsertAtCursor(ToStackString!(useChar), restoreSelectionOnUndo ? .None : .NoRestoreSelectionOnUndo);
				}
            }

			mCursorImplicitlyMoved = true;
        }

        public virtual float GetPageScrollTextHeight()
        {
            return mEditWidget.mScrollContentContainer.mHeight;
        }

        public virtual float AdjustPageScrollPos(float newY, int dir)
        {
            return Math.Max(0, Math.Min(newY, mHeight - mEditWidget.mScrollContentContainer.mHeight - mMaximalScrollAddedHeight));
        }

		enum CharType
		{
			Unknown = -1,
			NewLine,
			WhiteSpace,
			NonBreaking,
			Opening,
			Other
		}
        CharType GetCharType(char8 theChar)
        {
            if (theChar == '\n')
                return .NewLine;
            if (theChar.IsWhiteSpace)
                return .WhiteSpace;
            if (IsNonBreakingChar(theChar))
                return .NonBreaking;

            // We break on each instance of these
            switch (theChar)
            {
            case '<', '>', '(', ')', '[', ']', '{', '}':
                return .Opening;
            }
            return .Other;
        }

        public virtual void GetTextCoordAtCursor(out float x, out float y)
        {
            if (mVirtualCursorPos.HasValue)
            {
                GetTextCoordAtLineAndColumn(mVirtualCursorPos.Value.mLine, mVirtualCursorPos.Value.mColumn, out x, out y);
            }
            else
            {
                int line;
                int lineChar;
                GetLineCharAtIdx(mCursorTextPos, out line, out lineChar);
                GetTextCoordAtLineChar(line, lineChar, out x, out y);
            }
        }

        public bool GetCursorLineChar(out int line, out int lineChar)
        {
            if (mVirtualCursorPos == null)
            {
                GetLineCharAtIdx_Fast(mCursorTextPos, true, out line, out lineChar);
                return true;
            }

            line = mVirtualCursorPos.Value.mLine;

            float x;
            float y;
            GetTextCoordAtLineAndColumn(mVirtualCursorPos.Value.mLine, mVirtualCursorPos.Value.mColumn, out x, out y);


			float overflowX;
            return GetLineCharAtCoord(line, x, out lineChar, out overflowX);
        }

        public virtual bool PrepareForCursorMove(int dir = 0)
        {
            if (mWidgetWindow.IsKeyDown(KeyCode.Shift))
                return false;
            if ((mSelection == null) || (!mSelection.Value.HasSelection))
                return false;

            if (dir < 0)
                CursorTextPos = mSelection.Value.MinPos;
            else if (dir > 0)
                CursorTextPos = mSelection.Value.MaxPos;
            mSelection = null;
            return true;
        }
        
        public virtual void Undo()
        {
			scope AutoBeefPerf("EWC.Undo");

			if (CheckReadOnly())
				return;

			//Profiler.StartSampling();
			if (WantsUndo)
            	mData.mUndoManager.Undo();
			//Profiler.StopSampling();
        }

        public virtual void Redo()
        {
			scope AutoBeefPerf("EWC.Redo");

			if (CheckReadOnly())
				return;

			if (WantsUndo)
            	mData.mUndoManager.Redo();
        }

		public virtual void PasteText(String text)
		{
		    InsertAtCursor(text);
		}

		void CopyText(bool cut)
		{
			bool selectedLine = false;
			String extra = scope .();
			if (!HasSelection())
			{
				selectedLine = true;
				GetLinePosition(CursorLineAndColumn.mLine, var lineStart, var lineEnd);
				mSelection = .(lineStart, lineEnd);
				extra.Append("line");
			}
			
			String selText = scope String();
			GetSelectionText(selText);
	        BFApp.sApp.SetClipboardText(selText, extra);
			if ((cut) && (!CheckReadOnly()))
	        {
				if (selectedLine)
				{
					// Remove \n
					if (mSelection.Value.mEndPos < mData.mTextLength)
						mSelection.ValueRef.mEndPos++;
				}
				DeleteSelection();
			}

			if (selectedLine)
				mSelection = null;
		}

		public void CutText()
		{
			CopyText(true);
		}

		public void CopyText()
		{
			CopyText(false);
		}

		public void PasteText(String text, String extra)
		{
			if ((extra == "line") && (mAllowVirtualCursor))
			{
				UndoBatchStart undoBatchStart = new UndoBatchStart("paste");
				mData.mUndoManager.Add(undoBatchStart);

				var setCursorAction = new SetCursorAction(this);
				mData.mUndoManager.Add(setCursorAction);

				var origLineAndColumn = CursorLineAndColumn;
				CursorLineAndColumn = .(origLineAndColumn.mLine, 0);
				var lineStartPosition = CursorLineAndColumn;
				InsertAtCursor("\n");
				CursorLineAndColumn = lineStartPosition;
				
				CursorToLineStart(false);

				// Adjust to requested column
				if (CursorLineAndColumn.mColumn != 0)
				{
					for (let c in text.RawChars)
					{
						if (!c.IsWhiteSpace)
						{
							text.Remove(0, @c.Index);
							break;
						}
					}
				}

				PasteText(text);
				CursorLineAndColumn = .(origLineAndColumn.mLine + 1, origLineAndColumn.mColumn);
				mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
			}
			else
				PasteText(text);
		}

		public void PasteText()
		{
			String aText = scope String();
			String extra = scope .();
			BFApp.sApp.GetClipboardText(aText, extra);
			aText.Replace("\r", "");
			if ((aText != null) && (!CheckReadOnly()))
			{
				PasteText(aText, extra);
			}
		}

		protected void SelectLeft(int lineIdx, int lineChar, bool isChunkMove, bool isWordMove)
		{
			var lineIdx;
			var lineChar;

			int anIndex = GetTextIdx(lineIdx, lineChar);
			char8 prevC = 0;
			CharType prevCharType = (anIndex > 0) ? GetCharType((char8)mData.mText[anIndex - 1].mChar) : .Unknown;
			while (true)
			{
			    if (lineChar > 0)
			        MoveCursorTo(lineIdx, lineChar - 1, false, 0, .SelectLeft);
			    else if (lineIdx > 0)
			    {
			        int cursorIdx = mCursorTextPos;
					String lineText = scope String();
					GetLineText(lineIdx - 1, lineText);
			        MoveCursorTo(lineIdx - 1, (int32)lineText.Length, false, 0, .SelectLeft);
			        if ((!mAllowVirtualCursor) && (cursorIdx == mCursorTextPos))
			            MoveCursorTo(lineIdx - 1, (int32)lineText.Length - 1, false, 0, .SelectLeft);
			        break;
			    }

			    if (!isChunkMove)
			        break;

			    //mInner.mVal1 = inVal;
			    //PrintF("TestStruct() %d\n", inVal);

			    GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineChar);
			    anIndex = CursorTextPos;
			    if (anIndex == 0)
			        break;

			    char8 c = (char8)mData.mText[anIndex - 1].mChar;
			    CharType char8Type = GetCharType(c);
			    if (prevCharType == .Opening)
			        break;
			    if (char8Type != prevCharType)
			    {
			        if ((char8Type == 0) && (prevCharType != 0))
			            break;
			        if ((prevCharType == .NewLine) || (prevCharType == .NonBreaking) || (prevCharType == .Other))
			            break;
			    }

				if ((isWordMove) && (c.IsLower) && (prevC.IsUpper))
					break;

			    prevCharType = char8Type;
				prevC = c;
			}
		}

		protected void SelectRight(int lineIdx, int lineChar, bool isChunkMove, bool isWordMove)
		{
			var lineIdx;
			var lineChar;

			int anIndex = GetTextIdx(lineIdx, lineChar);
			char8 prevC = 0;
			CharType prevCharType = (anIndex < mData.mTextLength) ? GetCharType((char8)mData.mText[anIndex].mChar) : .Unknown;
			while (true)
			{
			    int lineStart;
			    int lineEnd;
			    GetLinePosition(lineIdx, out lineStart, out lineEnd);
			    int lineLen = lineEnd - lineStart;

			    int nextLineChar = lineChar + 1;
			    bool isWithinLine = nextLineChar < lineLen;
			    if (nextLineChar == lineLen)
			    {
			        GetTextData();
			        if ((mData.mTextFlags == null) || ((mData.mTextFlags[lineEnd] & (int32)TextFlags.Wrap) == 0))
			        {
			            isWithinLine = true;
			        }
			    }

			    if (isWithinLine)
			        MoveCursorTo(lineIdx, lineChar + 1, false, 1, .SelectRight);
			    else if (lineIdx < GetLineCount() - 1)
			        MoveCursorTo(lineIdx + 1, 0, false, 0, .SelectRight);

			    if (!mWidgetWindow.IsKeyDown(KeyCode.Control))
			        break;

			    GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineChar);
			    anIndex = GetTextIdx(lineIdx, lineChar);
			    if (anIndex == mData.mTextLength)
			        break;

			    char8 c = (char8)mData.mText[anIndex].mChar;
			    CharType char8Type = GetCharType(c);
			    if (char8Type == .Opening)
			        break;
			    if (char8Type != prevCharType)
			    {
			        if ((char8Type != .WhiteSpace) && (prevCharType == .WhiteSpace))
			            break;
			        if ((char8Type == .NewLine) || (char8Type == .NonBreaking) || (char8Type == .Other))
			            break;
			    }

				if ((isWordMove) && (c.IsUpper) && (prevC.IsLower))
					break;

			    prevCharType = char8Type;
				prevC = c;
			}
		}

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);

            if (keyCode == KeyCode.Escape)
            {
                mEditWidget.Cancel();
                if (mWidgetWindow == null)
                    return;
            }

            int lineIdx; 
            int lineChar;
            GetCursorLineChar(out lineIdx, out lineChar);

            bool wasMoveKey = false;            
            int prevCursorPos;
            bool gotCursorPos = TryGetCursorTextPos(out prevCursorPos);

            if (mWidgetWindow.GetKeyFlags() == .Ctrl)
            {
                switch (keyCode)
                {
                case (.)'A':
                    SelectAll();
                case (.)'C':
					CopyText();
                case (.)'X':
                    CutText();
                case (.)'V':
                    PasteText();
                case (.)'Z':
                    Undo();
                case (.)'Y':
                    Redo();
				case .Return:
					if (mIsMultiline)
					{
						mEditWidget.Submit();
						return;
					}
				default:
                }
            }

			if (mWidgetWindow.GetKeyFlags() == .Ctrl | .Shift)
			{
				switch (keyCode)
				{
				case (.)'Z':
					Redo();
				default:
				}
			}

            switch (keyCode)
            {
			case .Return:
				if (!mIsMultiline)
					mEditWidget.Submit();
            case KeyCode.Left:
                {
                    if (!PrepareForCursorMove(-1))
                    {
                        PrepareForCursorMove(-1);

						if (mAllowVirtualCursor)
						{
							bool doVirtualMove = true;

							if ((mWidgetWindow.IsKeyDown(KeyCode.Shift)) || (mWidgetWindow.IsKeyDown(KeyCode.Control)))
								doVirtualMove = false;
							else
							{
								int lineStart;
								int lineEnd;
								GetLinePosition(lineIdx, out lineStart, out lineEnd);
								
								//if (gotCursorPos)
								if (gotCursorPos)
									doVirtualMove = false;
							}

						    if (doVirtualMove)
						    {
						        var lineAndColumn = CursorLineAndColumn;
								if (lineAndColumn.mColumn > 0)
								{
								    /*mSelection = null;
								    mCursorBlinkTicks = 0;
								    CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, lineAndColumn.mColumn - 1);
								    EnsureCursorVisible(true, false, false);
								    CursorMoved();*/

									float x;
									float y;
									GetTextCoordAtLineAndColumn(lineAndColumn.mLine, lineAndColumn.mColumn - 1, out x, out y);
									MoveCursorToCoord(x, y);

								    break;
								}
						    }
						}

						
                        wasMoveKey = true;
						SelectLeft(lineIdx, lineChar, mWidgetWindow.IsKeyDown(KeyCode.Control), mWidgetWindow.IsKeyDown(KeyCode.Alt));
                    }
                }
                break;
            case KeyCode.Right:
                {                        
                    if (!PrepareForCursorMove(1))
                    {
						if (mAllowVirtualCursor)
						{
							bool doVirtualMove = true;

							if ((mWidgetWindow.IsKeyDown(KeyCode.Shift)) || (mWidgetWindow.IsKeyDown(KeyCode.Control)))
								doVirtualMove = false;
							else
							{
								int lineStart;
								int lineEnd;
								GetLinePosition(lineIdx, out lineStart, out lineEnd);

								if (prevCursorPos < lineEnd)
									doVirtualMove = false;
							}

	                        if (doVirtualMove)
	                        {
	                            mCursorBlinkTicks = 0;
	                            mSelection = null;
	                            var lineAndColumn = CursorLineAndColumn;
	                            CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, lineAndColumn.mColumn + 1);
	                            EnsureCursorVisible(true, false, false);
	                            PhysCursorMoved(.SelectRight);

								ClampCursor();
								if (lineAndColumn != CursorLineAndColumn)
	                            	break;
	                        }
						}

                        wasMoveKey = true;
						SelectRight(lineIdx, lineChar, mWidgetWindow.IsKeyDown(KeyCode.Control), mWidgetWindow.IsKeyDown(KeyCode.Alt));
                    }
                }
                break;
            case KeyCode.Up:
				fallthrough;
            case KeyCode.Down:
                {                        
                    int32 aDir = (keyCode == KeyCode.Up) ? -1 : 1;
					if ((HasSelection()) && (!mWidgetWindow.IsKeyDown(KeyCode.Shift)))
					{
						if (mAllowVirtualCursor)
						{
							var lineAndCol = CursorLineAndColumn;
							var usePos = (aDir < 0) ? (int32)mSelection.Value.MinPos : mSelection.Value.MaxPos;
							GetLineCharAtIdx(usePos, var selLine, var selLineChar);
							CursorLineAndColumn = .(selLine, lineAndCol.mColumn);
							mSelection = null;
						}
						else
							PrepareForCursorMove(aDir);
					}
                    
                    GetCursorLineChar(out lineIdx, out lineChar);

                    if (mWidgetWindow.IsKeyDown(KeyCode.Control))
                    {
                        mEditWidget.VertScrollTo(mEditWidget.mVertPos.mDest + aDir * mEditWidget.mScrollContentContainer.mHeight * 0.25f);
                        EnsureCursorVisible(false);
                        return;
                    }

                    wasMoveKey = true;
                    if ((lineIdx + aDir >= 0) && (lineIdx + aDir < GetLineCount()))
                    {
						float wantedX = mCursorWantX;
						float wantY = 0;

						if (mAllowVirtualCursor)
						{
							float cursorX;
							float cursorY;
							GetTextCoordAtCursor(out cursorX, out cursorY);

							GetLineAndColumnAtCoord(cursorX, cursorY, var virtLine, ?);

							/*GetTextCoordAtLineAndColumn(lineIdx, 0, ?, var lineY);
							Debug.WriteLine($"Line:{lineIdx} LineY:{lineY} Cursor:{cursorX},{cursorY}");*/

							if (aDir < 0)
							{
								wantY = cursorY - 0.1f;
							}
							else
							{
								wantY = cursorY + GetLineHeight(virtLine) + 0.1f;
							}
							//mCursorWantX = cursorX;
						}
						else
						{
	                        lineIdx += aDir;

	                        float aX;
	                        float aY;
	                        GetTextCoordAtLineChar(lineIdx, 0, out aX, out aY);
							wantY = aY;
						}
						MoveCursorToCoord(mCursorWantX, wantY);

						ClampCursor();

						// Restore old desired X
						mCursorWantX = wantedX;
                        
                    }
                }
                break;
            case KeyCode.Home:
                PrepareForCursorMove(-1);
                wasMoveKey = true;
                if (mWidgetWindow.IsKeyDown(KeyCode.Control))
                    CursorToStart();
                else
                    CursorToLineStart(true);
				//mCursorImplicitlyMoved = true;
                break;
            case KeyCode.End:
                PrepareForCursorMove(1);
                wasMoveKey = true;
                if (mWidgetWindow.IsKeyDown(KeyCode.Control))
                {
                    CursorToEnd();
                }
                else
                {
                    CursorToLineEnd();
                    if ((!mAllowVirtualCursor) && (mWordWrap))
                        mShowCursorAtLineEnd = true;
                }
                break;
            case KeyCode.PageUp:
				fallthrough;
            case KeyCode.PageDown:
                {                        
                    if (!mIsMultiline)
                        break;

                    wasMoveKey = true;
                    int32 aDir = (keyCode == KeyCode.PageUp) ? -1 : 1;
                    PrepareForCursorMove(aDir);

                    float cursorX;
                    float cursorY;

                    if (mAllowVirtualCursor)
                    {
                        var lineAndCol = CursorLineAndColumn;
                        GetTextCoordAtLineAndColumn(lineAndCol.mLine, lineAndCol.mColumn, out cursorX, out cursorY);
                        mCursorWantX = cursorX;
                    }
                    else
                    {
                        GetTextCoordAtLineChar(lineIdx, lineChar, out cursorX, out cursorY);
                    }
                    
                    cursorY -= (float)mEditWidget.mVertPos.mDest;                        
                    float pageScrollTextSize = GetPageScrollTextHeight();
                    
                    float adjustY = pageScrollTextSize * aDir;
                    float scrollToVal = (float)(mEditWidget.mVertPos.mDest + adjustY);
                    
                    float newScrollToVal = AdjustPageScrollPos(scrollToVal, aDir);
                    cursorY += scrollToVal - newScrollToVal;
                    scrollToVal = newScrollToVal;

                    if (mEditWidget.VertScrollTo(scrollToVal))
                    {
                        cursorY += (float)mEditWidget.mVertPos.mDest;
                        MoveCursorToCoord(cursorX, cursorY);

                        // Restore old desired X
                        mCursorWantX = cursorX;
                    }
                    else
                    {
                        if (aDir == -1)
                        {
                            lineIdx = 0;
                            CursorTextPos = 0;
                        }
                        else
                        {
                            lineIdx = GetLineCount() - 1;
                            CursorTextPos = GetTextIdx(lineIdx, lineChar);
                        }
                                                   
                        float wantedX = mCursorWantX;

                        float aX;
                        float aY;
                        GetTextCoordAtLineChar(lineIdx, 0, out aX, out aY);
                        MoveCursorToCoord(mCursorWantX, aY);

                        // Restore old desired X
                        mCursorWantX = wantedX;                            
                    }                        
                }
                break;
            case KeyCode.Insert:
				if ((mWidgetWindow.IsKeyDown(.Control)) && (mWidgetWindow.IsKeyDown(.Shift)))
					break;
				if (mWidgetWindow.IsKeyDown(.Control))
				{
					CopyText();
					break;
				}
				if (mWidgetWindow.IsKeyDown(.Shift))
				{
					PasteText();
					break;
				}
                mOverTypeMode = !mOverTypeMode;
                break;
            case KeyCode.Delete:
				if (mWidgetWindow.IsKeyDown(.Control))
				{
					if (!CheckReadOnly())
					{
						if (mWidgetWindow.IsKeyDown(.Shift))
						{
							int startIdx = CursorTextPos;
							CursorToLineEnd();
							mSelection = EditSelection(CursorTextPos, startIdx);
							var action = new DeleteSelectionAction(this);
							action.mMoveCursor = true;
							mData.mUndoManager.Add(action);
							action.mCursorTextPos = (.)startIdx;
							PhysDeleteSelection(true);
							break;
						}

						int line;
						int lineChar2;
						GetCursorLineChar(out line, out lineChar2);

						int startIdx = CursorTextPos;
						SelectRight(line, lineChar, true, false);
						mSelection = EditSelection(CursorTextPos, startIdx);

						var action = new DeleteSelectionAction(this);
						action.mMoveCursor = true;
						mData.mUndoManager.Add(action);
						action.mCursorTextPos = (.)startIdx;
						PhysDeleteSelection(true);
					}
					break;
				}

				if (mWidgetWindow.IsKeyDown(.Shift))
				{
					CutText();
					break;
				}

                if (!CheckReadOnly())
				{
                    DeleteChar();
				}
				mCursorImplicitlyMoved = true;
                break;                
			default:
            }

            if (wasMoveKey)
            {
                if (mWidgetWindow.IsKeyDown(KeyCode.Shift))
                {
                    if (!HasSelection())
                    {
                        StartSelection();
                        mSelection.ValueRef.mStartPos = (int32)prevCursorPos;                        
                    }
                    SelectToCursor();                
                }
                else
                    mSelection = null;

                EnsureCursorVisible();
            }
        }
        
        public float GetCursorScreenRelY()
        {
            float cursorX;
            float cursorY;
            GetTextCoordAtCursor(out cursorX, out cursorY);
            return (float)(cursorY - mEditWidget.mVertScrollbar.mContentPos);
        }

        public void SetCursorScreenRelY(float scrollTopDelta)
        {
            float cursorX;
            float cursorY;
            GetTextCoordAtCursor(out cursorX, out cursorY);
            mEditWidget.mVertScrollbar.ScrollTo(cursorY - scrollTopDelta);
        }

        public override bool Contains(float x, float y)
        {
            return true;
        }

        public override void Update()
        {
			Debug.Assert((mCursorTextPos != -1) || (mVirtualCursorPos != null));

 	         base.Update();
             if (mContentChanged)
                 RecalcSize();
             mCursorBlinkTicks++;
			if (mEditWidget.mHasFocus)
				MarkDirty();

			if (!mData.mUndoManager.[Friend]mUndoList.IsEmpty)
			{
				if (var textInsertAction = mData.mUndoManager.[Friend]mUndoList.Back as InsertTextAction)
				{
					if (textInsertAction.mSelection != null)
						Debug.Assert(textInsertAction.mSelection.Value.HasSelection);
				}
			}
        }        

        public virtual void RecalcSize()
        {            
            mContentChanged = false;
            mEditWidget.UpdateScrollbars();
        }

        public virtual void TextChanged()
        {
        }

        // Returns true if we have text at that location
        //  If we return 'false' then line/theChar are set to closest positions
        public virtual bool GetLineCharAtCoord(float x, float y, out int line, out int theChar, out float overflowX)
        {
            line = -1;
            theChar = -1;
            overflowX = 0;
            return false;
        }

		public virtual bool GetLineCharAtCoord(int line, float x, out int theChar, out float overflowX)
		{
			GetTextCoordAtLineAndColumn(line, 0, ?, var y);
		    return GetLineCharAtCoord(x, y, ?, out theChar, out overflowX);
		}

        public virtual bool GetLineAndColumnAtCoord(float x, float y, out int line, out int column)
        {
            line = -1;
            column = -1;
            return false;
        }

		public void GetColumnAtLineChar(int line, int lineChar, out int column)
        {
			float x;
			float y;
			GetTextCoordAtLineChar(line, lineChar, out x, out y);

			int line2;
			GetLineAndColumnAtCoord(x, y, out line2, out column);
		}

		public void GetLineColumnAtIdx(int idx, out int line, out int column)
		{
			GetLineCharAtIdx(idx, out line, var lineChar);
			GetColumnAtLineChar(line, lineChar, out column);
		}

		public virtual void GetLineCharAtIdx(int idx, out int line, out int theChar)
		{
			int lineA;
			int char8A;
			GetLineCharAtIdx_Fast(idx, false, out lineA, out char8A);

			/*int32 lineB;
			int32 char8B;
			GetLineCharAtIdx_Slow(idx, out lineB, out char8B);

			Debug.Assert((lineA == lineB) && (char8A == char8B));*/

			line = lineA;
			theChar = char8A;
		}

		public virtual void GetLineCharAtIdx_Fast(int idx, bool checkCursorAtLineEnd, out int line, out int theChar)
		{
			GetTextData();

			//bool isCurCursor = true;

			if (idx == mData.mTextLength)
			{
				int32 lastLine = GetLineCount() - 1;
				line = (int32)lastLine;
				theChar = idx - mData.mLineStarts[lastLine];
				return;
			}	

			int lo = 0;
			int hi = mData.mLineStarts.Count - 2;

			if (idx < 0)
			{
				line = 0;
				theChar = 0;
			}

			while (lo <= hi)
			{
			    int i = (lo + hi) / 2;
				var midVal = mData.mLineStarts[i];
				int c = midVal - idx;
			    if (c == 0)
                {
					if ((checkCursorAtLineEnd) && (mShowCursorAtLineEnd) && (i > 0))
					{
						i--;
						midVal = mData.mLineStarts[i];
					}

					line = (int32)i;
					theChar = idx - midVal;
					return;
				}    
			    if (c < 0)
			        lo = i + 1;
			    else
			        hi = i - 1;
			}

			if (hi < GetLineCount())
			{
				line = (int32)hi;
				theChar = idx - mData.mLineStarts[hi];
			}
			else
			{
				line = (int32)mData.mLineStarts.Count - 2;
				theChar = mData.mLineStarts[mData.mLineStarts.Count - 1] - mData.mLineStarts[mData.mLineStarts.Count - 2];
			}
		}

        public void GetLineCharAtIdx_Slow(int32 idx, out int32 line, out int32 theChar)
        {            
            GetTextData();

            for (int32 lineIdx = 0; lineIdx < mData.mLineStarts.Count; lineIdx++)
            {
                int32 lineStart = mData.mLineStarts[lineIdx];

                if ((idx < lineStart) || ((idx == lineStart) && (lineIdx > 0) && (mCursorTextPos == lineStart) && (mShowCursorAtLineEnd)))
                {
                    line = lineIdx - 1;
                    theChar = idx - mData.mLineStarts[lineIdx - 1];
                    return;
                }
            }

            if (idx < 0)
            {
                line = 0;
                theChar = 0;
            }
            else
            {
                line = (int32)mData.mLineStarts.Count - 2;
                theChar = mData.mLineStarts[mData.mLineStarts.Count - 1] - mData.mLineStarts[mData.mLineStarts.Count - 2];
            }
        }

        public virtual float GetLineHeight(int line)
        {
            return 0;
        }

        public virtual int32 GetLineCount()
        {
            GetTextData();
            return (int32)mData.mLineStarts.Count - 1;
        }

        public virtual void GetLinePosition(int line, out int lineStart, out int lineEnd)
        {
            GetTextData();

            if (line >= mData.mLineStarts.Count - 1)
            {
                lineStart = Math.Max(0, mData.mTextLength);
                lineEnd = mData.mTextLength;
                return;
            }

            lineStart = mData.mLineStarts[line];
            lineEnd = mData.mLineStarts[line + 1];
            if ((lineEnd > lineStart) && (mData.mText[lineEnd - 1].mChar == '\n'))
                lineEnd--;
        }

		public bool IsLineWhiteSpace(int line)
		{
			String str = scope .();
			GetLineText(line, str);
			return str.IsWhiteSpace;
		}

        public virtual void GetLineText(int line, String outStr)
        {
            GetTextData();

            int lineStart = mData.mLineStarts[line];
            int lineEnd = mData.mLineStarts[line + 1];

            if ((lineEnd > lineStart) && (mData.mText[lineEnd - 1].mChar == '\n'))
                ExtractString(lineStart, lineEnd - lineStart - 1, outStr); // Remove last char8 (it's a '\n')
            else
                ExtractString(lineStart, lineEnd - lineStart, outStr); // Full line
        }

        public int GetTextIdx(int line, int lineChar)
        {
            GetTextData();
            int useLine = Math.Min(line, mData.mLineStarts.Count - 1);
            return mData.mLineStarts[useLine] + lineChar;
        }

        public int GetCharIdIdx(int32 findCharId)
        {
			mData.mTextIdData.Prepare();
            return mData.mTextIdData.GetIndexFromId(findCharId);
        }

        public int32 GetSourceCharIdAtLineChar(int line, int column)
        {
            int encodeIdx = 0;
            int spanLeft = 0;
            int32 char8Id = 0;

            int curLine = 0;
            int curColumn = 0;
            int charIdx = 0;
			mData.mTextIdData.Prepare();
            while (true)
            {
                while (spanLeft == 0)
                {
                    if (mData.mTextIdData.mData == null)
                    {
                        spanLeft = mData.mTextLength;
                        continue;
                    }

                    int32 cmd = Utils.DecodeInt(mData.mTextIdData.mData, ref encodeIdx);
                    if (cmd > 0)
                        char8Id = cmd;
                    else
                        spanLeft = -cmd;
                    if (cmd == 0)
                        return 0;
                }

                if ((curLine == line) && (curColumn == column))
                    return char8Id;

                char8 c = (char8)mData.mText[charIdx++].mChar;
                if (c == '\n')
                {
                    if (curLine == line)
                        return char8Id;
                    curLine++;
                    curColumn = 0;
                }
                else
                    curColumn++;
                spanLeft--;
                char8Id++;
            }            
        }

        public virtual void GetTextCoordAtLineChar(int line, int charIdx, out float x, out float y)
        {
            x = 0;
            y = 0;
        }

        public virtual void GetTextCoordAtLineAndColumn(int line, int column, out float x, out float y)
        {
            x = 0;
            y = 0;
        }

		public enum CursorMoveKind
		{
			case FromTyping;
			case FromTyping_Deleting;
			case Unknown;
			case SelectRight;
			case SelectLeft;
			case QuickFind;

			public bool IsFromTyping => (this == FromTyping) || (this == FromTyping_Deleting);
		}

		// We used to have a split between PhysCursorMoved and CursorMoved.  CursorMoved has a "ResetWantX" and was non-virtual... uh-
		//  so what was that for?
        public virtual void PhysCursorMoved(CursorMoveKind moveKind)
        {
            mJustInsertedCharPair = false;
            mShowCursorAtLineEnd = false;
            mCursorBlinkTicks = 0;
			mCursorImplicitlyMoved = false;

			// 
			ResetWantX();
        }

        public void CursorMoved()
        {
			PhysCursorMoved(.Unknown);

            /*mJustInsertedCharPair = false;
            mShowCursorAtLineEnd = false;
            mCursorBlinkTicks = 0;
            ResetWantX();*/
        }

        public virtual void CursorToStart()
        {            
            MoveCursorTo(0, 0);            
        }

        public virtual void CursorToEnd()
        {
			String lineStr = scope String();
			GetLineText(GetLineCount() - 1, lineStr);
            MoveCursorTo(GetLineCount() - 1, (int32)lineStr.Length);
            //mCursorWantX = GetDrawPos;
        }

        public virtual void CursorToLineStart(bool allowToScreenStart)
        {
            int lineIdx;
            int lineChar;
			GetCursorLineChar(out lineIdx, out lineChar);

            String lineText = scope String();
            GetLineText(lineIdx, lineText);

			bool hasNonWhitespace = false;
            int32 contentIdx;
            for (contentIdx = 0; contentIdx < lineText.Length; contentIdx++)
			{
                if (!lineText[contentIdx].IsWhiteSpace)
				{
					hasNonWhitespace = true;
                    break;
				}
			}

            if ((mAllowVirtualCursor) && (!hasNonWhitespace))
            {
                var prevLineAndColumn = CursorLineAndColumn;
                CursorToLineEnd();
                if ((!allowToScreenStart) || (CursorLineAndColumn.mColumn < prevLineAndColumn.mColumn))
                    return;
            }

            if ((allowToScreenStart) && (lineChar == contentIdx))
                MoveCursorTo(lineIdx, 0);
            else
                MoveCursorTo(lineIdx, contentIdx);
        }

        public virtual void CursorToLineEnd()
        {
            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);            

            String curLineStr = scope String();
            GetLineText(line, curLineStr);
            int32 lineLen = (int32)curLineStr.Length;

			
            MoveCursorTo(line, lineLen);            
        }        

        public void StartSelection()
        {
			mDragSelectionKind = .Dragging;
            mSelection = EditSelection();
            int textPos;
            TryGetCursorTextPos(out textPos);
            mSelection.ValueRef.mEndPos = mSelection.ValueRef.mStartPos = (int32)textPos;
        }

        public void SelectToCursor()
        {
            /*int textPos;
            TryGetCursorTextPos(out textPos);
            if (mSelection != null)
            {
                mSelection.Value.mEndPos = textPos;
            }*/

			int textPos;
			TryGetCursorTextPos(out textPos);

			if (mDragSelectionUnion != null)
			{
			    mSelection = EditSelection(mDragSelectionUnion.Value.MinPos, mDragSelectionUnion.Value.MaxPos);
			    if (textPos <= mSelection.Value.mStartPos)
			    {
			        mSelection.ValueRef.mStartPos = (int32)Math.Max(0, textPos - 1);
			        while ((textPos > 0) && (IsNonBreakingChar((char8)mData.mText[textPos - 1].mChar)))
			        {                        
			            textPos--;
			            mSelection.ValueRef.mStartPos = (int32)textPos;
			        }
			        CursorTextPos = mSelection.Value.mStartPos;
			    }
			    else
			    {
			        if (textPos > mSelection.Value.mEndPos)
			        {
			            mSelection.ValueRef.mEndPos = (int32)textPos;
			            while ((textPos <= mData.mTextLength) && ((textPos == mData.mTextLength) || (mData.mText[textPos - 1].mChar != '\n')) &&
			                (IsNonBreakingChar((char8)mData.mText[textPos - 1].mChar)))
			            {
			                mSelection.ValueRef.mEndPos = (int32)textPos;
			                textPos++;
			            }
			        }
			        CursorTextPos = mSelection.Value.mEndPos;
			    }                
			}
			else if (mSelection != null)
			{                
			    mSelection.ValueRef.mEndPos = (int32)textPos;
			}
        }

        public virtual bool IsLineVisible(int line, bool useDestScrollPostion = true)
        {
            if (!mIsMultiline)
                return true;

            float aX;
            float aY;            
            GetTextCoordAtLineChar(line, 0, out aX, out aY);
            
            float lineHeight = GetLineHeight(line);

			double scrollPos = useDestScrollPostion ? mEditWidget.mVertPos.mDest : mEditWidget.mVertPos.mSrc;
            if (aY + lineHeight*0.9f < scrollPos + mTextInsets.mTop)
            {
                return false;
            }
            else if (aY + lineHeight*0.1f + mShowLineBottomPadding > scrollPos + mEditWidget.mScrollContentContainer.mHeight)
            {
                return false;
            }
            return true;
        }

		public bool IsCursorVisible(bool useDestScrollPostion = true)
		{
			return IsLineVisible(CursorLineAndColumn.mLine, useDestScrollPostion);
		}

        public virtual void EnsureCursorVisible(bool scrollView = true, bool centerView = false, bool doHorzJump = true)
        {
            if (mEditWidget.mScrollContentContainer.mWidth <= 0)
                return; // Not sized yet

            if (mContentChanged)
                RecalcSize();

            float horzJumpSize = doHorzJump ? mHorzJumpSize : 0;

            float aX;
            float aY;
            GetTextCoordAtCursor(out aX, out aY);            

            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);

            float lineHeight = GetLineHeight(line);

			double yOfs = (mEditWidget.mVertScrollbar?.mContentStart).GetValueOrDefault();

            if (mIsMultiline)
            {
                if (aY < mEditWidget.mVertPos.mDest + mTextInsets.mTop + yOfs)
                {
                    if (scrollView)
                    {
                        double scrollPos = aY - mTextInsets.mTop - yOfs;
                        if (centerView)
                        {                            
                            scrollPos -= mEditWidget.mScrollContentContainer.mHeight * 0.50f;
                            scrollPos = (float)Math.Round(scrollPos / lineHeight) * lineHeight;
                        }
                        mEditWidget.VertScrollTo(scrollPos);
                    }
                    else
                    {
                        int aLine;
                        int aCharIdx;
                        float overflowX;
                        GetLineCharAtCoord(aX, (float)(mEditWidget.mVertPos.mDest + mTextInsets.mTop + yOfs), out aLine, out aCharIdx, out overflowX);

                        float newX;
                        float newY;
                        GetTextCoordAtLineChar(aLine, aCharIdx, out newX, out newY);
                        if (aY < mEditWidget.mVertPos.mDest + mTextInsets.mTop - 0.01f)
                            GetLineCharAtCoord(newX, newY + GetLineHeight(aLine), out aLine, out aCharIdx, out overflowX);

                        MoveCursorTo(aLine, aCharIdx);
                    }
                }
                else if (aY + lineHeight + mShowLineBottomPadding > mEditWidget.mVertPos.mDest + mEditWidget.mScrollContentContainer.mHeight + yOfs)
                {
                    if (scrollView)
                    {
                        double scrollPos = aY + lineHeight + mShowLineBottomPadding - mEditWidget.mScrollContentContainer.mHeight - yOfs;
                        if (centerView)
                        {
                            // Show slightly more content on bottom
                            scrollPos += mEditWidget.mScrollContentContainer.mHeight * 0.50f;
                            scrollPos = (float)Math.Round(scrollPos / lineHeight) * lineHeight;
                        }
                        mEditWidget.VertScrollTo(scrollPos);
                    }
                    else
                        MoveCursorToCoord(aX, (float)(mEditWidget.mVertPos.mDest + mEditWidget.mScrollContentContainer.mHeight - lineHeight + yOfs));
                }
            }

            if (mAutoHorzScroll)
            {
                if (aX < mEditWidget.mHorzPos.mDest + mTextInsets.mLeft)
                    mEditWidget.HorzScrollTo(Math.Max(0, aX - mTextInsets.mLeft - horzJumpSize));
                else if (aX >= mEditWidget.mHorzPos.mDest + mEditWidget.mScrollContentContainer.mWidth - mTextInsets.mRight)
                {
                    float wantScrollPos = aX - mEditWidget.mScrollContentContainer.mWidth + horzJumpSize + mTextInsets.mRight;
                    if (mAllowVirtualCursor)
                        TrySetHorzScroll(wantScrollPos);
                    mEditWidget.HorzScrollTo(Math.Min(mWidth - mEditWidget.mScrollContentContainer.mWidth, wantScrollPos));
                }
            }
        }

        protected virtual void TrySetHorzScroll(float horzScroll)
        {
            mWidth = Math.Max(mWidth, horzScroll + mEditWidget.mScrollContentContainer.mWidth);
            mEditWidget.UpdateScrollbars();
        }

		protected void GetTabString(String str, int tabCount = 1)
		{
			if (mWantsTabsAsSpaces)
				str.Append(' ', mTabLength * tabCount);
			else
				str.Append('\t', tabCount);
		}

        public void BlockIndentSelection(bool unIndent = false)
        {
			if (CheckReadOnly())
				return;

            int minLineIdx = 0;
            int minLineCharIdx = 0;
            int maxLineIdx = 0;
            int maxLineCharIdx = 0;

            bool isMultilineSelection = HasSelection();
            if (isMultilineSelection)
            {
                GetLineCharAtIdx(mSelection.Value.MinPos, out minLineIdx, out minLineCharIdx);
                GetLineCharAtIdx(mSelection.Value.MaxPos, out maxLineIdx, out maxLineCharIdx);
                isMultilineSelection = maxLineIdx != minLineIdx;
            }

            if (isMultilineSelection)
            {
                var indentTextAction = new EditWidgetContent.IndentTextAction(this);
                EditSelection newSel = mSelection.Value;

                mSelection.ValueRef.MakeForwardSelect();
                mSelection.ValueRef.mStartPos -= (int32)minLineCharIdx;

                //int32 minPos = newSel.MinPos;
                int32 startAdjust = 0;
                int32 endAdjust = 0;

                if (unIndent)
                {
                    //bool hadWsLeft = (minPos > 0) && (Char8.IsWhiteSpace((char8)mData.mText[minPos - 1].mChar));
                    for (int lineIdx = minLineIdx; lineIdx <= maxLineIdx; lineIdx++)
                    {
                        int lineStart;
                        int lineEnd;
                        GetLinePosition(lineIdx, out lineStart, out lineEnd);
                        
						if (lineStart == lineEnd)
							continue;

                        for (int32 i = 0; i < mTabLength; i++)
                        {
                            char8 c = (char8)mData.mText[lineStart + i].mChar;
                            if (((c == '\t') && (i == 0)) || (c == ' '))
                            {
                                indentTextAction.mRemoveCharList.Add(((int32)lineStart + i + endAdjust, c));
                                endAdjust--;
                            }
                            if (c != ' ')
                                break;
                        }
                        if ((lineIdx == minLineIdx) && (endAdjust != 0))
                            startAdjust = endAdjust;
                    }
					indentTextAction.Redo();

                }
                else
                {
					if (mWantsTabsAsSpaces)
						startAdjust += mTabLength;
					else
						startAdjust++;
						
                    for (int lineIdx = minLineIdx; lineIdx <= maxLineIdx; lineIdx++)
                    {
                        int lineStart;
                        int lineEnd;
                        GetLinePosition(lineIdx, out lineStart, out lineEnd);
                        lineStart += endAdjust;
						if (mWantsTabsAsSpaces)
						{
							for (int i < mTabLength)
							{
								indentTextAction.mInsertCharList.Add(((int32)lineStart, ' '));
								endAdjust++;
							}
						}
						else
						{
	                        indentTextAction.mInsertCharList.Add(((int32)lineStart, '\t'));
	                        endAdjust++;
						}
                    }

					indentTextAction.Redo();
                }
                ContentChanged();

                if (newSel.IsForwardSelect)
                {
                    newSel.mStartPos = Math.Max(newSel.mStartPos + startAdjust, 0);
                    newSel.mEndPos += endAdjust;
                    CursorTextPos = newSel.mEndPos;
                }
                else
                {
                    newSel.mEndPos = Math.Max(newSel.mEndPos + startAdjust, 0);
                    newSel.mStartPos += endAdjust;
                    CursorTextPos = newSel.mEndPos;
                }

                indentTextAction.mNewSelection = newSel;
                if ((indentTextAction.mInsertCharList.Count > 0) ||
                    (indentTextAction.mRemoveCharList.Count > 0))
                {
                    mData.mUndoManager.Add(indentTextAction);
				}
				else
					delete indentTextAction;

                mSelection = newSel;
                return;
            }
            else // Non-block
            {
                int lineIdx;
                int lineCharIdx;
                GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineCharIdx);
                
                if (unIndent)
                {
					var prevSelection = mSelection;
					mSelection = null;

                    if (lineCharIdx > 0)
                    {
                        String aLine = scope String();
                        GetLineText(lineIdx, aLine);
                        for (int32 i = 0; i < mTabLength; i++)
                        {
							if (lineCharIdx == 0)
								break;
                            char8 c = aLine[lineCharIdx - 1];
                            if (!c.IsWhiteSpace)
                                break;
                            lineCharIdx--;
							if (prevSelection != null)
							{
								prevSelection.ValueRef.mStartPos--;
								prevSelection.ValueRef.mEndPos--;
							}
                            Backspace();
                            if (c == '\t')
                                break;
                        }
                    }
                    else if (mAllowVirtualCursor)
                    {
                        var cursorPos = CursorLineAndColumn;
                        cursorPos.mColumn = Math.Max(0, (cursorPos.mColumn - 1) / mTabLength * mTabLength);
                        CursorLineAndColumn = cursorPos;
                    }

					mSelection = prevSelection;

                    return;
                }

                String curLineText = scope String();
                GetLineText(lineIdx, curLineText);

                int32 indentCount = 1;
				
				String trimmedCurLineText = scope String(curLineText);
				trimmedCurLineText.Trim();

                if ((lineCharIdx == 0) && (trimmedCurLineText.Length == 0)) // Smart indent
                {
                    if (mAllowVirtualCursor)
                    {
                        if (HasSelection())
                            DeleteSelection();
                        var prevCursorPos = CursorLineAndColumn;
                        CursorToLineEnd();
                        if (prevCursorPos.mColumn >= CursorLineAndColumn.mColumn)
                        {
                            var cursorPos = prevCursorPos;
                            cursorPos.mColumn = (prevCursorPos.mColumn + mTabLength) / mTabLength * mTabLength;
                            CursorLineAndColumn = cursorPos;
                        }
                        return;
                    }

                    int checkLineIdx = lineIdx - 1;
                    while (checkLineIdx >= 0)
                    {
                        String lineText = scope String();
                        GetLineText(checkLineIdx, lineText);
                        lineText.TrimEnd();
                        int32 checkIndentCount = 0;
                        for (; checkIndentCount < lineText.Length; checkIndentCount++)
                        {
                            if (lineText[checkIndentCount] != '\t')
                                break;
                        }
                        if ((checkIndentCount < lineText.Length) && (lineText[checkIndentCount] == '{'))
                            checkIndentCount++;
                        if (checkIndentCount > 0)
                        {
                            indentCount = checkIndentCount;
                            break;
                        }
                        checkLineIdx--;
                    }
                }

				if (HasSelection())
				{
					GetLinePosition(minLineIdx, var lineStart, var lineEnd);
					String str = scope .();
					ExtractString(lineStart, CursorTextPos - lineStart, str);
					if (str.IsWhiteSpace)
					{
						let prevSelection = mSelection.Value;
						mSelection = null;
						for (int32 i = 0; i < indentCount; i++)
						    InsertAtCursor(GetTabString(.. scope .()));
						GetLinePosition(minLineIdx, out lineStart, out lineEnd);
						mSelection = EditSelection(prevSelection.mStartPos + indentCount, prevSelection.mEndPos + indentCount);
					}
					else
						InsertAtCursor(GetTabString(.. scope .()));
				}
				else
				{
					InsertAtCursor(GetTabString(.. scope .(), indentCount));
				}
            }
        }

        public void SelectAll()
        {
            CursorToStart();
            StartSelection();
            CursorToEnd();
            SelectToCursor();
            Debug.Assert(mSelection.Value.mEndPos <= mData.mTextLength);
        }

		public virtual void GetLineAndColumnAtLineChar(int line, int lineChar, out int lineColumn)
		{
			float x;
			float y;
			GetTextCoordAtLineChar(line, lineChar, out x, out y);

			int coordLine;
			if (!GetLineAndColumnAtCoord(x, y, out coordLine, out lineColumn))
			{
				GetLinePosition(line, var lineStart, var lineEnd);

				lineColumn = 0;
				int checkTextPos = lineStart;
				for (int i < lineChar)
				{
					let c32 = mData.GetChar32(ref checkTextPos);
					if (!c32.IsCombiningMark)
						lineColumn++;
				}
			}
		}

        public virtual void MoveCursorTo(int line, int charIdx, bool centerCursor = false, int movingDir = 0, CursorMoveKind cursorMoveKind = .Unknown)
        {
			int useCharIdx = charIdx;

            mShowCursorAtLineEnd = false;
            CursorTextPos = GetTextIdx(line, charIdx);

			// Skip over UTF8 parts AND unicode combining marks (ie: when we have a letter with an accent mark following it)
			while (true)
			{
				char8 c = mData.SafeGetChar(mCursorTextPos);
				if (c < (char8)0x80)
					break;
				if ((uint8)c & 0xC0 != 0x80)
				{
					var checkChar = mData.GetChar32(mCursorTextPos);
					if (!checkChar.IsCombiningMark)
						break;
				}
				if (movingDir > 0)
				{
					if ((useCharIdx == charIdx) && (c < (.)0xC0))
					{
						// This is an invalid UTF8 sequence
						break;
					}

					useCharIdx++;
					mCursorTextPos++;
				}
				else
				{
					if (mCursorTextPos == 0)
						break;
					useCharIdx--;
					mCursorTextPos--;
				}
			}
            
            float aX;
            float aY;
            GetTextCoordAtLineChar(line, useCharIdx, out aX, out aY);
            mCursorWantX = aX;

            mCursorBlinkTicks = 0;
            if (mEnsureCursorVisibleOnModify)
                EnsureCursorVisible(true, centerCursor);
            PhysCursorMoved(cursorMoveKind);
        }

        public void ResetWantX()
        {
			GetTextCoordAtCursor(var x, var y);
            mCursorWantX = x;
        }

        public void MoveCursorToIdx(int index, bool centerCursor = false, CursorMoveKind cursorMoveKind = .Unknown)
        {
            int line;
            int charIdx;
            GetLineCharAtIdx(index, out line, out charIdx);
            MoveCursorTo(line, charIdx, centerCursor, 0, cursorMoveKind);
        }
        
        public virtual void MoveCursorToCoord(float x, float y)
        {
			bool failed = false;

            int aLine;
            int aCharIdx;
            float overflowX;
            failed = !GetLineCharAtCoord(x, y, out aLine, out aCharIdx, out overflowX);
			if ((mAllowVirtualCursor) && (failed))
			{
			    int line;
			    int column;
			    GetLineAndColumnAtCoord(x, y, out line, out column);
			    CursorLineAndColumn = LineAndColumn(line, column);
			    mCursorBlinkTicks = 0;
				PhysCursorMoved(.Unknown);
				mShowCursorAtLineEnd = false;
			}
			else
			{
				if ((mWordWrap) && (failed))
					mShowCursorAtLineEnd = true;
	            MoveCursorTo(aLine, aCharIdx);

	            int lineStart;
	            int lineEnd;
	            GetLinePosition(aLine, out lineStart, out lineEnd);
	            if ((mWordWrap) && (aCharIdx == lineEnd - lineStart) && (lineEnd != lineStart))
	                mShowCursorAtLineEnd = true;
			}
        }

        public bool HasSelection()
        {
            return (mSelection != null) && (mSelection.Value.HasSelection);
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			mTextInsets.Scale(newScale / oldScale);
			//ContentChanged
		}

        //public virtual void DrawContent(
		
		public void ClearUndoData()
		{
			if (mData.mUndoManager != null)
			{
				mData.mUndoManager.Clear();
			}
		}

		public bool HasUndoData()
		{
			if (mData.mUndoManager != null)
			{
				return mData.mUndoManager.GetActionCount() > 0;
			}
			return false;
		}
    }

    public abstract class EditWidget : ScrollableWidget
    {
        public EditWidgetContent mEditWidgetContent;
        public Event<EditWidgetEventHandler> mOnSubmit ~ _.Dispose();
        public Event<EditWidgetEventHandler> mOnCancel ~ _.Dispose();
        public Event<EditWidgetEventHandler> mOnContentChanged ~ _.Dispose();
        
        public EditWidgetContent Content
        {
            get { return mEditWidgetContent; }
        }

        public void GetText(String outStr)
		{
			mEditWidgetContent.ExtractString(0, mEditWidgetContent.mData.mTextLength, outStr);
		}

		public bool IsWhiteSpace()
		{
			for (int i < mEditWidgetContent.mData.mTextLength)
			{
				char8 c = mEditWidgetContent.mData.mText[i].mChar;
				if (!c.IsWhiteSpace)
					return false;
			}
			return true;
		}

        public virtual void Submit()
        {
            if (mOnSubmit.HasListeners)
            {
                EditEvent anEvent = scope EditEvent();
                anEvent.mSender = this;                
                mOnSubmit(anEvent);
            }
        }

        public void Cancel()
        {
            if (mOnCancel.HasListeners)
            {
                EditEvent editEvent = scope EditEvent();
                editEvent.mSender = this;
                mOnCancel(editEvent);
                return;
            }
        }

        public void ContentChanged()
        {
            if (mOnContentChanged.HasListeners)
            {
                EditEvent editEvent = scope EditEvent();
                editEvent.mSender = this;
                mOnContentChanged(editEvent);
                return;
            }
        }

        public virtual void SetText(String text)
        {
			scope AutoBeefPerf("EditWidget.SetText");

            var prevLineAndColumn = mEditWidgetContent.CursorLineAndColumn;
            float cursorPos = (float)mVertPos.mDest;

			
            EditWidgetContent.TextUndoBatchStart undoBatchStart = null;
			if (mEditWidgetContent.WantsUndo)
			{
	            undoBatchStart = new EditWidgetContent.TextUndoBatchStart(mEditWidgetContent, "setText");
	            mEditWidgetContent.mData.mUndoManager.Add(undoBatchStart);
			}

            mEditWidgetContent.CursorToStart();
            mEditWidgetContent.SelectAll();

            mEditWidgetContent.InsertAtCursor(text);
            if (mEditWidgetContent.mAllowVirtualCursor)
                mEditWidgetContent.CursorLineAndColumn = prevLineAndColumn;
            mEditWidgetContent.ClampCursor();

            mVertPos.Set(cursorPos, true);
			if (mEditWidgetContent.WantsUndo)
            	mEditWidgetContent.mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

			if ((!mHasFocus) && (mHorzPos != null))
				mHorzPos.Set(0, true);
            //TextChanged();
            //mEditWidgetContent.mUndoManager.Clear();
        }

        public IdSpan DuplicateTextIdData()
        {
            return mEditWidgetContent.mData.mTextIdData.Duplicate();
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);
            
            // If we get clicked on ourselves then just translate into editwidget space to get the focus
            float childX;
            float childY;
            mEditWidgetContent.ParentToSelfTranslate(x, y, out childX, out childY);
            mEditWidgetContent.MouseDown(childX, childY, btn, btnCount);
        }

        public override void SetFocus()
        {            
            mWidgetWindow.SetFocus(this);            
        }

        public override void GotFocus()
        {
            base.GotFocus();
			MouseEventHandler handler = new => HandleWindowMouseDown;
			//Debug.WriteLine("Adding handler {0}", handler);
            mWidgetWindow.mOnMouseDown.Add(handler);
        }

		public override void LostFocus()
		{
			MarkDirty();
			var widgetWindow = mWidgetWindow;
			base.LostFocus();
			widgetWindow.mOnMouseDown.Remove(scope => HandleWindowMouseDown, true);
			//Debug.WriteLine("Removing handler (LostFocus)");
		}

		protected virtual bool WantsUnfocus()
		{
			return (!mMouseOver) && (mWidgetWindow != null) && (!ContainsWidget(mWidgetWindow.mOverWidget));
		}

        protected virtual void HandleWindowMouseDown(MouseEvent theEvent)
        {
            if (WantsUnfocus())
            {
                // Someone else got clicked on, remove focus
                //mWidgetWindow.mMouseDownHandler.Remove(scope => HandleWindowMouseDown, true);
				//Debug.WriteLine("Removing handler (HandleWindowMouseDown)");
                mWidgetWindow.SetFocus(null);  
            }
        }

        public override void RemovedFromParent(Widget previousParent, WidgetWindow previousWidgetWindow)
        {
            base.RemovedFromParent(previousParent, previousWidgetWindow);

            //TODO: Does this cause problems if we didn't have the handler added?
            if (previousWidgetWindow != null)
			{
                //previousWidgetWindow.mMouseDownHandler.Remove(scope => HandleWindowMouseDown, true);
				//Debug.WriteLine("Removing handler (RemovedFromParent)");
			}
        }

        public override void KeyChar(char32 theChar)
        {
            base.KeyChar(theChar);

            mEditWidgetContent.KeyChar(theChar);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

            if (mEditWidgetContent.mWordWrap) // Need to recalculate word wrapping
                mEditWidgetContent.ContentChanged();
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);
            if (mWidgetWindow != null)
                mEditWidgetContent.KeyDown(keyCode, isRepeat);
        }

        public void FinishScroll()
        {
			mVertPos.mPct = 1.0f;
            UpdateContentPosition();
        }
    }
}