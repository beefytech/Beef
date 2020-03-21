using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Diagnostics;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.utils;
using IDE.Debugger;
using IDE.Compiler;

namespace IDE.ui
{    
    public class SourceEditBatchHelper
    {
        SourceEditWidget mEditWidget;

        UndoBatchStart mUndoBatchStart;
        PersistentTextPosition mTrackedCursorPosition;
        PersistentTextPosition mSelStartPostion;
        PersistentTextPosition mSelEndPostion;

        public this(SourceEditWidget editWidget, String batchName, UndoAction firstUndoAction = null)
        {
            mEditWidget = editWidget;
            var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;

            mUndoBatchStart = new UndoBatchStart(batchName);
            editWidgetContent.mData.mUndoManager.Add(mUndoBatchStart);
            if (firstUndoAction != null)
                editWidgetContent.mData.mUndoManager.Add(firstUndoAction);
            editWidgetContent.mData.mUndoManager.Add(new EditWidgetContent.SetCursorAction(editWidgetContent));

            mTrackedCursorPosition = new PersistentTextPosition((int32)editWidgetContent.CursorTextPos);
            editWidgetContent.PersistentTextPositions.Add(mTrackedCursorPosition);
            
            if (editWidgetContent.HasSelection())
            {
                mSelStartPostion = new PersistentTextPosition(editWidgetContent.mSelection.Value.mStartPos);
                editWidgetContent.PersistentTextPositions.Add(mSelStartPostion);

                mSelEndPostion = new PersistentTextPosition(editWidgetContent.mSelection.Value.mEndPos);
                editWidgetContent.PersistentTextPositions.Add(mSelEndPostion);
            }
            editWidgetContent.mSelection = null;
        }

        public void Finish(UndoAction lastUndoAction = null)
        {
            var editWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
            editWidgetContent.CursorTextPos = mTrackedCursorPosition.mIndex;
            editWidgetContent.PersistentTextPositions.Remove(mTrackedCursorPosition);
			delete mTrackedCursorPosition;

            if (mSelStartPostion != null)
            {
                editWidgetContent.mSelection = EditSelection(mSelStartPostion.mIndex, mSelEndPostion.mIndex);
                editWidgetContent.PersistentTextPositions.Remove(mSelEndPostion);
				delete mSelEndPostion;
                editWidgetContent.PersistentTextPositions.Remove(mSelStartPostion);
				delete mSelStartPostion;
            }

            editWidgetContent.mData.mUndoManager.Add(new EditWidgetContent.SetCursorAction(editWidgetContent));
            if (lastUndoAction != null)
                editWidgetContent.mData.mUndoManager.Add(lastUndoAction);
            editWidgetContent.mData.mUndoManager.Add(mUndoBatchStart.mBatchEnd);
        }
    }

    public class ReplaceSourceAction : EditWidgetContent.TextAction
    {
        public String mOldText ~ delete _;
        public String mNewText ~ delete _;

        public this(EditWidgetContent editWidget, String newText, bool restoreSelectionOnUndo = true)
            : base(editWidget)
        {
            mNewText = newText;
            mOldText = new String();
            editWidget.ExtractString(0, editWidget.mData.mTextLength, mOldText);
            mRestoreSelectionOnUndo = restoreSelectionOnUndo;
        }
        
        public override bool Undo()
        {
			var editWidgetContent = EditWidgetContent;
            var sourceEditWidgetContent = (SourceEditWidgetContent)editWidgetContent;

            sourceEditWidgetContent.mSourceViewPanel.ClearTrackedElements();

            //int startIdx = (mSelection != null) ? mSelection.Value.MinPos : mCursorTextPos;
            editWidgetContent.RemoveText(0, mEditWidgetContentData.mTextLength);
            editWidgetContent.InsertText(0, mOldText);
            editWidgetContent.ContentChanged();
            base.Undo();
            editWidgetContent.ContentChanged();
            mEditWidgetContentData.mCurTextVersionId = mPrevTextVersionId;
            editWidgetContent.ClampCursor();

            return true;
        }

        public override bool Redo()
        {
            base.Redo();
            
			var editWidgetContent = EditWidgetContent;
            var sourceEditWidgetContent = (SourceEditWidgetContent)editWidgetContent;

            sourceEditWidgetContent.mSourceViewPanel.ClearTrackedElements();

            //int startIdx = (mSelection != null) ? mSelection.Value.MinPos : mCursorTextPos;
            editWidgetContent.RemoveText(0, mEditWidgetContentData.mTextLength);
            editWidgetContent.InsertText(0, mNewText);
            editWidgetContent.ContentChanged();
            editWidgetContent.ClampCursor();

            return true;
        }
    }

    public class SourceEditWidgetContent : DarkEditWidgetContent
    {
		public class Data : EditWidgetContent.Data
		{
			public List<PersistentTextPosition> mPersistentTextPositions = new List<PersistentTextPosition>() ~ DeleteContainerAndItems!(_);
			public QuickFind mCurQuickFind; // Only allow one QuickFind on this buffer at a time
		}

		struct QueuedTextEntry
		{
		    public String mString;
		    public float mX;
		    public float mY;
		    public uint16 mTypeIdAndFlags;

			public void Dispose()
			{
				delete mString;
			}
		}

		struct QueuedUnderlineEntry
		{
		    public float mX;
		    public float mY;
		    public float mWidth;
		    public uint8 mFlags;
		}

		class FastCursorState
		{
			public double mX;
			public double mY;

			public struct DrawSect
			{
				public double mX;
				public double mY;
				public float mPct;
			}

			public List<DrawSect> mDrawSects = new List<DrawSect>() ~ delete _;
			public int mUpdateCnt;
		}

		public enum AutoCompleteOptions
		{
			None = 0,
			HighPriority = 1,
			UserRequested = 2,
			OnlyShowInvoke = 4
		}

        public Action<char32, AutoCompleteOptions> mOnGenerateAutocomplete ~ delete _;
        public Action mOnFinishAsyncAutocomplete ~ delete _;
        public Action mOnCancelAsyncAutocomplete ~ delete _;
        public Func<bool> mOnEscape ~ delete _; // returns 'true' if did work
        public AutoComplete mAutoComplete ~ delete _;
        List<QueuedTextEntry> mQueuedText = new List<QueuedTextEntry>() ~ delete _;
        List<QueuedUnderlineEntry> mQueuedUnderlines = new List<QueuedUnderlineEntry>() ~ delete _;        
        public bool mHadPersistentTextPositionDeletes;
        public SourceViewPanel mSourceViewPanel;
        //public bool mAsyncAutocomplete;
        public bool mIsInKeyChar;
        public bool mDbgDoTest;
        public int32 mCursorStillTicks;
        public static bool sReadOnlyErrorShown;
		public bool mIgnoreKeyChar; // This fixes cases where a KeyDown changes focus to us but then we get a KeyChar that doesn't belong to us
		public bool mIgnoreSetHistory;
        public static uint32[] sTextColors = new uint32[]
            {
                0xFFFFFFFF, // Normal
                0xFFE1AE9A, // Keyword
                0XFFC8A0FF, // Literal
                0xFFFFFFFF, // Identifier
                0XFF66D9EF, // Type
                0XFF75715E, // Comment
                0XFFA6E22A, // Method
                0XFF66D9EF, // TypeRef
                0xFF7BEEB7, // Namespace

                0xFFB0B0B0, // Disassembly_Text
                0XFFFF0000, // Disassembly_FileName

                0xFFFF0000, // Error

				0xFFFF8080, // BuildError
				0xFFFFFF80, // BuildWarning

				0xFF9090C0, // VisibleWhiteSpace
            } ~ delete _;
		bool mHasCustomColors;
		FastCursorState mFastCursorState ~ delete _;

        public this(EditWidgetContent refContent = null) : base(refContent)
        {            
            mAllowVirtualCursor = true;
            SetFont(IDEApp.sApp.mCodeFont, true, true);
			//SetFont(DarkTheme.sDarkTheme.mSmallFont, false, false);

            mTabSize = mFont.GetWidth("    ");
            mTextColors = sTextColors;
            mExtendDisplayFlags = (uint8)(SourceElementFlags.SpellingError | SourceElementFlags.SymbolReference);
            mShowLineBottomPadding = 2;
        }

		public ~this()
		{
			ClearColors();
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			mTabSize = mFont.GetWidth("    ");
		}

		protected override EditWidgetContent.Data CreateEditData()
		{
			return new Data();
		}

		public List<PersistentTextPosition> PersistentTextPositions
		{
			get
			{
				return ((Data)mData).mPersistentTextPositions;
			}
		}
        
		public Data	Data
		{
			get
			{
				return (Data)mData;
			}
		}

		[StdCall, CLink]
		static extern char8* BfDiff_DiffText(char8* text1, char8* text2);

		struct TextLineSegment
		{
		    public int32 mIndex;
		    public int32 mLength;
		    public String mLine;
		}

		public bool Reload(String filePath, String queuedContent = null)
		{
			//TODO: Only do the 'return false' if the file doesn't actually exist.
			//  Otherwise keep trying to reload (for a while) before giving up
		    var text = scope String();
			if (queuedContent != null)
			{
				queuedContent.MoveTo(text);
			}
			else
			{
			    if (gApp.LoadTextFile(filePath, text) case .Err)
			    {				
					//ClearText();
			        return false;
			    }
			}

			//

		    var editWidgetContent = this;

		    String curText = scope String();
		    mEditWidget.GetText(curText);

		    char8* diffCmdsPtr = BfDiff_DiffText(curText, text);
		    String diffCmds = scope String();
			diffCmds.Append(diffCmdsPtr);

		    UndoBatchStart undoBatchStart = new UndoBatchStart("applyDiff");
		    editWidgetContent.mData.mUndoManager.Add(undoBatchStart);

		    editWidgetContent.mSelection = null;

		    int32 curSrcLineIdx = -1;
		    List<TextLineSegment> deletedLineSegments = scope List<TextLineSegment>();


			String cmd = scope String();
			List<StringView> cmdResults = scope List<StringView>(diffCmds.Split('\n'));

			List<StringView> cmdParts = scope List<StringView>();

			String val = scope String();
		    for (var cmdView in cmdResults)
		    {
				cmd.Set(cmdView);
		        if (cmd == "")
		            continue;

				cmdParts.Clear();
				for (var strView in cmd.Split(' '))
					cmdParts.Add(strView);
				val.Set(cmdParts[0]);

		        if (val == "-")
		        {
		            int32 pos;
		            int32 len;
					val.Set(cmdParts[1]);
		            pos = int32.Parse(val);
					val.Set(cmdParts[2]);
		            len = int32.Parse(val);

		            if (cmdParts.Count >= 4)
		            {
						val.Set(cmdParts[3]);
		                int32 deleteLineIdx= int32.Parse(val);

		                if (deleteLineIdx == curSrcLineIdx)
		                {
		                    curSrcLineIdx++;
		                }
		                else
		                {
		                    deletedLineSegments.Clear();
		                }
		            }

		            editWidgetContent.mHadPersistentTextPositionDeletes = false;
		            editWidgetContent.mSelection = EditSelection(pos, pos + len);
		            editWidgetContent.DeleteSelection(false);

		            // If we have modified a section of text containing breakpoint (for example), this gets encoded by
		            //  'adds' of the new lines and then 'removes' of the old lines.  We do a Levenshtein search for
		            //  the most applicable line to bind to.
		            if (editWidgetContent.mHadPersistentTextPositionDeletes)
		            {
		                var deleteSelectionAction = (EditWidgetContent.DeleteSelectionAction)editWidgetContent.mData.mUndoManager.GetLastUndoAction();
		                String oldLineText = deleteSelectionAction.mSelectionText;

		                for (int32 persistentIdx = 0; persistentIdx < editWidgetContent.PersistentTextPositions.Count; persistentIdx++)
		                {
		                    var persistentTextPosition = editWidgetContent.PersistentTextPositions[persistentIdx];
		                    if (persistentTextPosition.mWasDeleted)
		                    {
								int bestDist = Int32.MaxValue;
								int bestSegIdx = -1;

		                        for (int segIdx = 0; segIdx < deletedLineSegments.Count; segIdx++)
		                        {
		                            var textSegment = deletedLineSegments[segIdx];
		                            if (textSegment.mLine == null)
		                            {
		                                textSegment.mLine = scope:: String();
		                                editWidgetContent.ExtractString(textSegment.mIndex, textSegment.mLength, textSegment.mLine);
		                                deletedLineSegments[segIdx] = textSegment;
		                            }

		                            int dist = Utils.LevenshteinDistance(oldLineText, textSegment.mLine);
		                            if (dist < bestDist)
		                            {
		                                bestDist = dist;
		                                bestSegIdx = segIdx;
		                            }
		                        }

		                        if (bestSegIdx != -1)
		                        {
		                            var textSegment = deletedLineSegments[bestSegIdx];
		                            persistentTextPosition.mWasDeleted = false;
		                            persistentTextPosition.mIndex = textSegment.mIndex;
		                            // Find first non-whitespace
		                            for (int32 char8Idx = 0; char8Idx < textSegment.mLength; char8Idx++)
		                            {                                        
		                                if (!((char8)editWidgetContent.mData.mText[persistentTextPosition.mIndex].mChar).IsWhiteSpace)
		                                    break;
		                                persistentTextPosition.mIndex++;
		                            }
		                            deletedLineSegments.RemoveAt(bestSegIdx);
		                        }
		                    }
		                }
		            }
		        }
		        else if (val == "+")
		        {
					val.Set(cmdParts[1]);
		            int32 posTo = int32.Parse(val).Get();
					val.Set(cmdParts[2]);
		            int32 posFrom = int32.Parse(val).Get();
					val.Set(cmdParts[3]);
		            int32 len = int32.Parse(val).Get();
		            
		            editWidgetContent.CursorTextPos = posTo;
					var subStr = scope String();
					subStr.Append(text, posFrom, len);
		            editWidgetContent.InsertAtCursor(subStr, .NoMoveCursor);

		            if (cmdParts.Count >= 5)
		            {
						val.Set(cmdParts[4]);
		                int32 insertLineIdx = int32.Parse(val);
		                if (curSrcLineIdx != insertLineIdx)
		                {
		                    curSrcLineIdx = insertLineIdx;
		                    deletedLineSegments.Clear();
		                }
		                var lineSeg = TextLineSegment();
		                lineSeg.mIndex = posTo;
		                lineSeg.mLength = len;
		                deletedLineSegments.Add(lineSeg);
		            }
		        }                
		    }

		    editWidgetContent.mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

		    //QueueFullRefresh(false);

			var endingText = scope String();
			mEditWidget.GetText(endingText);

			if (endingText != text)
			{
				Utils.WriteTextFile("c:\\temp\\dbg_curText.txt", curText);
				Utils.WriteTextFile("c:\\temp\\dbg_text.txt", text);
				Utils.WriteTextFile("c:\\temp\\dbg_endingText.txt", endingText);
			}

		    Debug.Assert(endingText == text);
		    
			//mClangSourceChanged = false;
			//mLastFileTextVersion = mEditWidget.Content.mData.mCurTextVersionId;
			//if ((mProjectSource != null) && (mProjectSource.mEditData != null))
			    //mProjectSource.mEditData.mLastFileTextVersion = mLastFileTextVersion;
			return true;
		}

		void ClearColors()
		{
			if (mHasCustomColors)
			{
				delete mTextColors;
				mTextColors = null;
				mHasCustomColors = false;
			}
		}

        public void SetOldVersionColors(bool old = true)
        {
			ClearColors();
			mTextColors = sTextColors;
			mHasCustomColors = false;
            if (!old)
            {
                return;
            }

            var newTextColor = new uint32[mTextColors.Count];
            for (int32 colorIdx = 0; colorIdx < mTextColors.Count; colorIdx++)
            {
                uint32 color = sTextColors[colorIdx];
                float h;
                float s;
                float v;
                Color.ToHSV(color, out h, out s, out v);
                s = s * 0.5f;
                v = Math.Min(1.0f, v * 1.0f);
                uint32 newColor = Color.FromHSV(h, s, v);
                newTextColor[colorIdx] = newColor;
            }		
			ClearColors();
            mTextColors = newTextColor;
			mHasCustomColors = true;
        }

        public override bool CheckReadOnly()
        {
			if ((gApp.mSymbolReferenceHelper != null) && (gApp.mSymbolReferenceHelper.mKind == .Rename))
				gApp.mSymbolReferenceHelper.EnsureWorkStarted();

            if (!base.CheckReadOnly())
			{
				if ((mSourceViewPanel != null) && (mSourceViewPanel.IsReadOnly))
				{
					IDEApp.Beep(IDEApp.MessageBeepType.Error);
					mSourceViewPanel.mLockFlashPct = 0.00001f;
					return true;
				}
                return false;
			}
            if (mSourceViewPanel != null)
            {
                if ((!sReadOnlyErrorShown) && (mSourceViewPanel.[Friend]mCurrentVersionPanel != null))
                    IDEApp.sApp.Fail("Switch to the current version of this file to edit it.");
                else
                    IDEApp.Beep(IDEApp.MessageBeepType.Error);
            }
            sReadOnlyErrorShown = true;

            return true;
        }

		public override void ApplyTextFlags(int index, String text, uint8 typeNum, uint8 flags)
		{
			uint8 curTypeNum = typeNum;

			for (int i = 0; i < text.Length; i++)
			{
			    char8 c = text[i];
				if ((curTypeNum != (uint8)BfSourceElementType.Normal) &&
				    (curTypeNum != (uint8)BfSourceElementType.Comment))
				{
					if ((!c.IsLetterOrDigit) && (c != '_'))
						curTypeNum = (uint8)BfSourceElementType.Normal;
				}

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

        public override float DrawText(Graphics g, String str, float x, float y, uint16 typeIdAndFlags)
        {
            /*using (g.PushColor(mTextColors[typeIdAndFlags & 0xFF]))
                return DoDrawText(g, str, x, y);*/
 
            QueuedTextEntry queuedTextEntry;
            queuedTextEntry.mString = new String(str);
            queuedTextEntry.mX = x;
            queuedTextEntry.mY = y;
            queuedTextEntry.mTypeIdAndFlags = typeIdAndFlags;
            mQueuedText.Add(queuedTextEntry);
            return DoDrawText(null, str, x, y);
        }

        public override void DrawSectionFlagsOver(Graphics g, float x, float y, float width, uint8 flags)
        {
            if ((flags & ~(uint8)SourceElementFlags.Skipped) == 0)
                return;

            if ((flags & (uint8)SourceElementFlags.SymbolReference) != 0)
            {
                bool isRenameSymbol = (IDEApp.sApp.mSymbolReferenceHelper != null) && (IDEApp.sApp.mSymbolReferenceHelper.mKind == SymbolReferenceHelper.Kind.Rename);
                using (g.PushColor(isRenameSymbol ? (uint32)0x28FFFFFF : (uint32)0x18FFFFFF))
                    g.FillRect(x, y, width, mFont.GetLineSpacing());

                DrawSectionFlagsOver(g, x, y, width, (uint8)(flags & ~(uint8)SourceElementFlags.SymbolReference));
                return;
            }

			if ((flags & (uint8)SourceElementFlags.Find_CurrentSelection) != 0)
			{
			    using (g.PushColor(0x80FFE0B0))
			        g.FillRect(x, y, width, mFont.GetLineSpacing());

			    DrawSectionFlagsOver(g, x, y, width, (uint8)(flags & ~(uint8)(SourceElementFlags.Find_CurrentSelection | .Find_Matches)));
			    return;
			}

            if ((flags & (uint8)SourceElementFlags.Find_Matches) != 0)
            {
                using (g.PushColor(0x50D0C090))
                    g.FillRect(x, y, width, mFont.GetLineSpacing());

                DrawSectionFlagsOver(g, x, y, width, (uint8)(flags & ~(uint8)SourceElementFlags.Find_Matches));
                return;
            }

            QueuedUnderlineEntry queuedUnderlineEntry;
            queuedUnderlineEntry.mX = x;
            queuedUnderlineEntry.mY = y;
            queuedUnderlineEntry.mWidth = width;
            queuedUnderlineEntry.mFlags = flags;
            mQueuedUnderlines.Add(queuedUnderlineEntry);
        }

        void DoDrawSectionFlags(Graphics g, float x, float y, float width, uint8 flags)
        {
            SourceElementFlags elementFlags = (SourceElementFlags)flags;
            
            if (elementFlags.HasFlag(SourceElementFlags.IsAfter))
            {
                elementFlags = elementFlags & ~SourceElementFlags.IsAfter;
                DoDrawSectionFlags(g, x + width, y, GS!(6), (uint8)elementFlags);
            }

            if (elementFlags != 0)
            {
                uint32 underlineColor = 0;

                if (elementFlags.HasFlag(SourceElementFlags.Error))
                {
                    underlineColor = 0xFFFF0000;
                }
                else if (elementFlags.HasFlag(SourceElementFlags.Warning))
                {
                    underlineColor = 0xFFFFD200;
                }
                else if (elementFlags.HasFlag(SourceElementFlags.SpellingError))
                {
                    underlineColor = 0xE0FF3000;
                }

                if (underlineColor != 0)
                {
                    using (g.PushColor(underlineColor))
                        gApp.DrawSquiggle(g, x, y, width);                    
                }
            }
        }

		public int32 GetActualLineStartColumn(int line)
		{
			String lineStr = scope String();
			GetLineText(line, lineStr);
			int32 spaceCount = 0;
			for (int idx < lineStr.Length)
			{
				var c = lineStr[idx];
				switch (c)
				{
				case ' ':
					spaceCount++;
				case '\t':
					spaceCount += GetTabSpaceCount();
				default:
					return spaceCount;
				}
			}
			return spaceCount;
		}

		void GetBlockStart(int line, out int foundBlockStartIdx, out int blockOpenSpaceCount)
		{
			int lineStart;
			int lineEnd;
			GetLinePosition(line, out lineStart, out lineEnd);

			int parenDepth = 0;
			int blockDepth = 0;

			int checkLineNum = line;
			foundBlockStartIdx = -1;
			blockOpenSpaceCount = 0;
			// Go backwards and find block opening
			for (int checkIdx = lineStart - 1; checkIdx >= 0; checkIdx--)
			{
				char8 c = mData.mText[checkIdx].mChar;
				var elementType = (SourceElementType)mData.mText[checkIdx].mDisplayTypeId;
				if (elementType == SourceElementType.Comment)
					c = ' '; // Ignore

				if (foundBlockStartIdx != -1)
				{
					if (c == '\n') // Done?
						break;
					else if (c == ' ')
						blockOpenSpaceCount++;
					else if (c == '\t')
						blockOpenSpaceCount += 4; // SpacesInTab
					else
					{
						blockOpenSpaceCount = 0;
					}
				}
				else if (elementType == SourceElementType.Normal)
				{
					switch (c)
					{
					case '\n':
						checkLineNum--;
					case '}':
						blockDepth++;
					case '{':
						blockDepth--;
						if (blockDepth == -1)
						{
							foundBlockStartIdx = checkIdx;
							break;
						}
					case ')':
						parenDepth++;
					case '(':
						parenDepth--;
					}
				}
			}
		}

		bool IsInTypeDef(int line)
		{
			int foundBlockStartIdx;
			int blockOpenSpaceCount;
			GetBlockStart(line, out foundBlockStartIdx, out blockOpenSpaceCount);

			char8 prevC = 0;
			for (int checkIdx = foundBlockStartIdx - 1; checkIdx >= 0; checkIdx--)
			{
				char8 c = mData.mText[checkIdx].mChar;
				var elementType = (SourceElementType)mData.mText[checkIdx].mDisplayTypeId;
				if (elementType == SourceElementType.Normal)
				{
					if (c == '}')
					{

					}

					if ((c == ';') || (c == ')'))
						return false;
				}

				if (elementType == SourceElementType.Keyword)
				{
					if ((checkIdx == 0) || ((SourceElementType)mData.mText[checkIdx - 1].mDisplayTypeId != .Keyword))
					{
						if (((c == 'c') && (prevC == 'l')) || // class
							((c == 'e') && (prevC == 'n')) || // enum
							((c == 's') && (prevC == 't')) || // struct
                            ((c == 'e') && (prevC == 'x')))  // extension
							return true;
					}
				}
				prevC = c;
			}

			return false;
		}

		public int32 GetTabbedSpaceCount(String str)
		{
			int32 spaceCount = 0;
			for (let c in str.RawChars)
			{
				if (c == '\t')
				{
					spaceCount = (spaceCount / mTabLength + 1) * mTabLength;
				}
				else if (c == ' ')
				{
					spaceCount++;
				}
				else
					Runtime.FatalError();
			}
			return spaceCount;
		}

		public int GetLineEndColumn(int line, bool openingBlock, bool force, bool ignoreLineText = false, bool insertingElseStmt = false, float* outWidth = null)
		{
			String curLineStr = scope String();
			GetLineText(line, curLineStr);
			//int lineLen = curLineStr.Length; // 2

			String curLineTrimmed = scope String(curLineStr);
			curLineTrimmed.TrimEnd();
			int trimmedLineLen = curLineTrimmed.Length;

			//int semiCount = 0;
			float totalTabbedWidth = GetTabbedWidth(curLineStr, 0);
			int32 actualSpaceCount = (int32)(totalTabbedWidth / mCharWidth + 0.001f);
			if (openingBlock || ignoreLineText)
			    actualSpaceCount = 0;

			if (((trimmedLineLen > 0) && (!force)) || (!mAllowVirtualCursor))
			{
				if (outWidth != null)
					*outWidth = totalTabbedWidth;
				return actualSpaceCount;
			}

			///

			int lineStart;
			int lineEnd;
			GetLinePosition(line, out lineStart, out lineEnd);
			int foundBlockStartIdx;
			int blockOpenSpaceCount;
			GetBlockStart(line, out foundBlockStartIdx, out blockOpenSpaceCount);

			if (foundBlockStartIdx == -1)
				return 0;

			int endingPos = lineEnd;
			if (ignoreLineText)
				endingPos = lineStart;

			int blockDepth = 0;
			int parenDepth = 0;
			String keywordStr = scope String();
			int ifDepth = 0;
			bool isDoingCtl = false;
			bool justFinishedCtlCond = false;
			bool justFinishedCtl = false;
			int ctlDepth = 0; // for, while, using

			int extraTab = 1;
			bool hasUnterminatedStatement = false;
			bool doingIf = false;
			bool justFinishedIf = false;
			bool doingElse = false;
			bool isAttribute = false;
			int bracketDepth = 0;
			bool isLineStart = true;
			bool skippingLine = false;
			bool startedWithType = false; // to distinguish between a list like a data or enum value list vs a variable declaration with multiple names across multiple lines
			bool checkedStartedWithType = false;
			bool inCaseExpr = false;
			bool inCaseExprNext = false; // Got a comma
			bool inCaseExprNextLine = false; // Got a comma and then a newline
			int caseStartPos = -1;

			/*String debugStr = scope String();
			mEditWidget.GetText(debugStr);*/

			//TODO: Make List append allocate
			//List<int> ifDepthStack = scope List<int>(8);
			List<int32> ifCtlDepthStack = scope List<int32>();

			bool keepBlockIndented = false;
			for (int checkIdx = foundBlockStartIdx + 1; checkIdx < endingPos; checkIdx++)
			{
				char8 c = mData.mText[checkIdx].mChar;
				var elementType = (SourceElementType)mData.mText[checkIdx].mDisplayTypeId;
				bool isWhitespace = c.IsWhiteSpace;
				if (isWhitespace)
					elementType = .Normal;
				if ((c == ':') && (elementType == .Method))
					elementType = .Normal;
				
				if (c == '\n')
				{
					isLineStart = true;
					skippingLine = false;
					if (inCaseExprNext)
						inCaseExprNextLine = true;
				}
				else if ((c == '#') && (elementType == .Normal) && (isLineStart))
				{
					isLineStart = false;
					skippingLine = true;
				}

				if ((inCaseExpr) && (c == ':') && (elementType == .Normal) && (parenDepth == 0))
				{
					inCaseExpr = false;
					inCaseExprNext = false;
					inCaseExprNextLine = false;
				}
				if ((inCaseExpr) && (c == ',') && (parenDepth == 0) && (bracketDepth == 0))
				{
					inCaseExprNext = true;
					inCaseExprNextLine = false;
				}

				if ((inCaseExprNextLine) && (!isWhitespace) && (elementType == .Normal))
					inCaseExprNextLine = false;

				if (skippingLine)
				{
					continue;
				}

				if (elementType == SourceElementType.Keyword)
				{
					if (blockDepth == 0)
						keywordStr.Append(c);
				}
				else if (keywordStr.Length > 0)
				{
					if (blockDepth == 0)
					{
						if (((justFinishedIf) || (justFinishedCtl)) && (keywordStr != "else"))
						{
							// This catches the case of:
							// if (a) Thing();
							// if (b) ...
							ifDepth = 0;
							justFinishedIf = false;
							ifCtlDepthStack.Clear();
							ctlDepth = 0;

							isDoingCtl = false;
						}

						justFinishedCtl = false;
						justFinishedIf = false;
						
						switch (keywordStr)
						{
						case "if":
							if (isDoingCtl)
							{
								ctlDepth++;
								isDoingCtl = false;
							}

							doingIf = true;
							if (!doingElse)
							{
								ifDepth++;
								ifCtlDepthStack.Add((int32)ctlDepth);
							}
						case "else":
							isDoingCtl = false;
							justFinishedIf = false;
							doingElse = true;
						case "for", "while", "using":
							justFinishedCtlCond = false;
							if (isDoingCtl)
								ctlDepth++;
							isDoingCtl = true;
						case "case":
							if (parenDepth == 0)
							{
								caseStartPos = checkIdx - 4;
								inCaseExpr = true;
							}
						case "switch":
							ifDepth = 0;
						}

						doingElse = keywordStr == "else";
					}

					keywordStr.Clear();
				}

				if ((isWhitespace) && (!checkedStartedWithType) && (checkIdx > foundBlockStartIdx + 1))
				{
					char8 prevC = mData.mText[checkIdx - 1].mChar;
					if (!prevC.IsWhiteSpace)
					{
                        var prevElementType = (SourceElementType)mData.mText[checkIdx - 1].mDisplayTypeId;
						startedWithType = prevElementType == SourceElementType.TypeRef;
						checkedStartedWithType = true;
					}
				}

				if ((elementType == SourceElementType.Comment) || (isWhitespace))
					continue;

				if ((!checkedStartedWithType) && (elementType != SourceElementType.Namespace) && (elementType != SourceElementType.TypeRef))
				{
					checkedStartedWithType = true;
				}

				if (elementType == SourceElementType.Normal)
				{
					switch (c)
					{
					case '=', ':', '?', '!':
						keepBlockIndented = true;
					default:
						keepBlockIndented = false;
					}

					switch (c)
					{
					case ',':
						if ((!startedWithType) && (parenDepth == 0) && (blockDepth == 0))
							hasUnterminatedStatement = false;
					case '{':
						blockDepth++;
					case '}':
						blockDepth--;
					case '(':
						parenDepth++;
					case ')':
						parenDepth--;
						if ((parenDepth == 0) && (blockDepth == 0))
						{
							if (isDoingCtl)
								justFinishedCtlCond = true;

							if (doingElse)
							{
								if (!doingIf)
								{
									if (ifDepth > 0)
									{
										ifDepth--;
										ifCtlDepthStack.PopBack();
										if (ifCtlDepthStack.Count > 0)
											ctlDepth = ifCtlDepthStack[ifCtlDepthStack.Count - 1];
										else
											ctlDepth = 0;
									}
								}
								doingElse = false;
							}

							if (doingIf)
							{
								doingIf = false;
								hasUnterminatedStatement = false;
							}
						}
					case '[':
						if (!hasUnterminatedStatement)
						{
							isAttribute = true;
						}
						bracketDepth++;
					case ']':
						bracketDepth--;
					default:
						hasUnterminatedStatement = true;
					}
	
					if ((c == ']') && (isAttribute) && (bracketDepth == 0))
					{
						hasUnterminatedStatement = false;
					}

					if (((c == '}') || (c == ';') || (c == ':')) && (blockDepth == 0) && (parenDepth == 0))
					{
						if (ifCtlDepthStack.Count > 0)
						{
							// This is important for an 'for,if,for,for,stmt;,else' - so the 'else' properly sees a ctlDepth of 1 instead of 3
							ctlDepth = ifCtlDepthStack[ifCtlDepthStack.Count - 1];
						}

						justFinishedCtlCond = false;
						startedWithType = false;
						checkedStartedWithType = false;

						if (isDoingCtl)
							justFinishedCtl = true;

						if (ifDepth == 0)
						{
							isDoingCtl = false;
							ctlDepth = 0;
						}

						if (doingElse)
						{
							if (!doingIf)
							{
								if (ifDepth > 0)
								{
									ifDepth--;
									ifCtlDepthStack.PopBack();
									if (ifCtlDepthStack.Count > 0)
										ctlDepth = ifCtlDepthStack[ifCtlDepthStack.Count - 1];
									else
										ctlDepth = 0;
								}
							}
							doingElse = false;
						}

						if (justFinishedIf)
						{
							// We had a non-else statement
							ifDepth = 0;
							justFinishedIf = false;
							ctlDepth = 0;
							ifCtlDepthStack.Clear();

							//isDoingCtl = false;
						}
	
						// Had statement
						hasUnterminatedStatement = false;
						if (ifDepth > 0)
							justFinishedIf = true;

						inCaseExpr = false;
						inCaseExprNext = false;
						inCaseExprNextLine = false;
					}
				}
				else
				{
					if ((isDoingCtl) && (justFinishedCtlCond))
					{
						isDoingCtl = false;
						ctlDepth++;
					}

					keepBlockIndented = false;
					hasUnterminatedStatement = true;
					//if (blockDepth == 0)
						//doingIf = false;
				}

				if (!isWhitespace)
					isLineStart = false;
			}

			if (inCaseExpr)
			{
				int startIdx = -1;

				int checkIdx = caseStartPos - 1;
				while (checkIdx > 0)
				{
					char8 c = mData.mText[checkIdx].mChar;
					if (c == '\n')
					{
						startIdx = checkIdx + 1;
						break;
					}
					else if ((c != ' ') && (c != '\t'))
					{
						startIdx = -1;
						break;
					}
					checkIdx--;
				}

				if (startIdx != -1)
				{
					var str = scope String();
					ExtractString(startIdx, caseStartPos - checkIdx - 1, str);

					return GetTabbedSpaceCount(str) + 5;
				}
				
			}


			if ((ctlDepth > 0) && ((!justFinishedIf) || (insertingElseStmt)))
				extraTab += ctlDepth;

			if ((ifDepth > 0) && ((!justFinishedIf) || (insertingElseStmt)))
			{
				extraTab += ifDepth - 0;
				if ((doingElse) || (insertingElseStmt))
					extraTab--;
			}

			// If we have an opening block in a lambda statement like "handlers.Add(scope (evt) => \n{" then indent the block still
			if ((hasUnterminatedStatement) && (!doingIf))
				extraTab++;

			if ((openingBlock) && (!keepBlockIndented) && (parenDepth == 0))
				extraTab = Math.Max(1, extraTab - 1);

			int wantSpaceCount = blockOpenSpaceCount;
			//if (!openingBlock)
				wantSpaceCount += extraTab * 4;

			if (inCaseExprNextLine)
				wantSpaceCount++;

			if ((!curLineStr.IsEmpty) && (!ignoreLineText) && (!force))
			{
				int tabbedSpaceCount = GetTabbedSpaceCount(curLineStr);
				if (tabbedSpaceCount >= wantSpaceCount)
					return tabbedSpaceCount;
			}

			return wantSpaceCount;
		}

        public HistoryEntry RecordHistoryLocation(bool ignoreIfClose = false)
        {
            if ((mSourceViewPanel != null) && (mSourceViewPanel.mFilePath != null))
            {
                int line;
                int lineChar;
                GetCursorLineChar(out line, out lineChar);
                return IDEApp.sApp.mHistoryManager.CreateHistory(mSourceViewPanel, mSourceViewPanel.mFilePath, line, lineChar, ignoreIfClose);
            }
			return null;
        }

        public bool IsAtCurrentHistory()
        {
            if ((mSourceViewPanel == null) || (mSourceViewPanel.mFilePath == null))
                return false;
            
            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);
            return IDEApp.sApp.mHistoryManager.IsAtCurrentHistory(mSourceViewPanel, mSourceViewPanel.mFilePath, line, lineChar, false);
        }
        
		public void PasteText(String str, bool forceMatchIndent = false)
		{
			String useString = str;

			int32 tabSpaceCount = GetTabSpaceCount();

			var lineAndColumn = CursorLineAndColumn;
			var lineText = scope String();
			if (HasSelection())
			{
				int startPos;
				int endPos;
				mSelection.Value.GetAsForwardSelect(out startPos, out endPos);
				int line;
				int lineChar;
				GetLineCharAtIdx(startPos, out line, out lineChar);
				lineAndColumn.mLine = (int32)line;
				int column;
				GetColumnAtLineChar(line, lineChar, out column);
				lineAndColumn.mColumn = (int32)column;

				int lineStart;
				int lineEnd;
				GetLinePosition(line, out lineStart, out lineEnd);

				ExtractString(lineStart, startPos - lineStart, lineText);
			}
			else
			{
				GetLineText(lineAndColumn.mLine, lineText);
			}
			
			bool startsWithNewline = (forceMatchIndent) && (str.StartsWith("\n"));
			bool isMultiline = str.Contains("\n");

			if (startsWithNewline || isMultiline)
			{
				var undoBatchStart = new UndoBatchStart("pasteText");
				mData.mUndoManager.Add(undoBatchStart);
				defer(stack) mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
			}

			if (startsWithNewline)
			{
				CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, 0);
				InsertAtCursor("\n");
				CursorLineAndColumn = lineAndColumn;
				useString = scope:: String(str, 1, str.Length - 1);
			}

			bool didFormattedPaste = false;
			bool hadUnevenTabs = false;

			bool doDeepPaste = false;
			if (mAllowVirtualCursor)
            	doDeepPaste = isMultiline || (str.Contains(':'));

			// We had a rule where we wouldn't format if we're pasting at the start of the line, but this
			//  is not a valid check since we could be copying a whole class block outside a namespace
			if ((/*(lineAndColumn.mColumn > 0) &&*/ (doDeepPaste) && (String.IsNullOrWhiteSpace(lineText))) ||
				(forceMatchIndent))
			{
             	didFormattedPaste = true;

			    int32 minColumn = int32.MaxValue;
				int32 maxColumn = 0;
			    int32 column = 0;
			    bool atLineStart = false;
			    bool isBlock = false;
			    bool isFirstChar = true;
			    for (int32 idx = 0; idx < str.Length; idx++)
			    {
			        char8 c = str[idx];
			        if (c == '\n')
			        {
			            column = 0;
			            atLineStart = true;                        
			        }
			        else if (c == '\t')
			        {
			            column += tabSpaceCount;
			        }
			        else if (c == ' ')
			        {
			            column++;
			        }
			        else if (c != '\r')
			        {
			            if ((c == '#') && (column == 0) && (atLineStart))
			            {
			                // Don't consider left-aligned preprocessor nodes in the minSpacing
			                atLineStart = false;
			            }

			            if (isFirstChar)
			            {
			                if (c == '{')
			                    isBlock = true;
			                isFirstChar = false;
			            }

			            if (atLineStart)
			            {                            
			                minColumn = Math.Min(minColumn, column);
							maxColumn = Math.Max(maxColumn, column);
			                atLineStart = false;
			            }
			        }
			    }
				hadUnevenTabs = minColumn != maxColumn;
			    
			    String sb = scope:: String();

				int alignColumn = GetLineEndColumn(lineAndColumn.mLine, isBlock, true,  true);

			    String linePrefix = scope String('\t', alignColumn / tabSpaceCount);
			    CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, alignColumn);

				bool isFullSwitch = false;
				if (useString.StartsWith("{"))
					isFullSwitch = true;

				if ((useString.Contains(':')) &&
					((useString.StartsWith("case ")) ||
					 (useString.StartsWith("when ")) ||
					 (useString.StartsWith("default:")) ||
					 (useString.StartsWith("default "))))
				{
					CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine, Math.Max(0, alignColumn - tabSpaceCount));

					if (maxColumn > 0)
					{
						maxColumn += tabSpaceCount;
						minColumn += tabSpaceCount;
					}
				}

			    column = 0;
			    atLineStart = true;
			    bool didLinePrefix = true;
			    for (int32 idx = 0; idx < useString.Length; idx++)
			    {
			        bool allowChar = (!atLineStart) || (column >= minColumn);
			        char8 c = useString[idx];
			        if (c == '\t')
			        {
			            column += tabSpaceCount;
			        }
			        else if (c == ' ')
			        {
			            column++;
			        }
			        else
			        {
			            if ((c == '#') && (column == 0))                        
			                didLinePrefix = true;                        

			            allowChar = true;
			            if (c == '\n')
			            {                            
			                column = 0;
			                atLineStart = true;
			                didLinePrefix = false;                            
			            }
			            else
			            {                            
			                atLineStart = false;
			            }
			        }                    

			        if (allowChar)
			        {
			            if ((!didLinePrefix) && (c != '\n'))
			            {
							String lineStartString = scope String();
							lineStartString.Reference(useString);
							lineStartString.AdjustPtr(idx);

							if (lineStartString.StartsWith("{"))
								isFullSwitch = true;

							if ((lineStartString.StartsWith("case ")) ||
								(lineStartString.StartsWith("when ")) ||
								(lineStartString.StartsWith("default:")) ||
								(lineStartString.StartsWith("default ")))
							{
								if ((linePrefix.StartsWith("\t")) && (!isFullSwitch))
									sb.Append(linePrefix.CStr() + 1);
								else
									sb.Append(linePrefix);
							}
							else
			                	sb.Append(linePrefix);
			                didLinePrefix = true;
			            }

			            sb.Append(c);
			        }
			    }

			    useString = sb;                
			}

			int prevCursorTextPos = CursorTextPos;
			InsertAtCursor(useString);			

			// If we paste in "if (a)\n\tDoThing();", we want "DoThing();" to be indented, so we need to fix this.
			//  This isn't a problem if we had "if (a)\n\tDoThing()\nDoOtherThing();" because minColumn would match
			//  DoOtherThing so DoThing would indent properly (for example)
			if ((didFormattedPaste) && (lineAndColumn.mLine < GetLineCount() - 1))
			{
				LineAndColumn endLineAndColumn = CursorLineAndColumn;

				String nextLineText = scope String();
					GetLineText(lineAndColumn.mLine + 1, nextLineText);
				nextLineText.Trim();
				bool isOpeningBlock = nextLineText.StartsWith("{");

				int wantIndent1 = GetLineEndColumn(lineAndColumn.mLine, false, true, true);
				mSourceViewPanel.DoFastClassify(prevCursorTextPos, CursorTextPos - prevCursorTextPos);
				int wantIndent2 = GetLineEndColumn(lineAndColumn.mLine + 1, isOpeningBlock, true, true);
				int haveIndent2 = GetActualLineStartColumn(lineAndColumn.mLine + 1);

				if ((wantIndent2 == wantIndent1 + GetTabSpaceCount()) && (wantIndent1 == haveIndent2))
				{
					for (int32 lineNum = lineAndColumn.mLine + 1; lineNum <= endLineAndColumn.mLine; lineNum++)
					{
						String line = scope String();
						GetLineText(lineNum, line);

						line.Trim();
						/*if (line.StartsWith("{")) // Fix for pasting "if (a)\n{\n}" 
							break;*/

						if (!line.IsWhiteSpace)
						{
							CursorLineAndColumn = LineAndColumn(lineNum, 0);
							InsertAtCursor("\t");
						}
					}

					CursorToLineEnd();
				}
			}
		}

        public override void PasteText(String theString)
        {
            PasteText(theString, false);
        }

        public override void InsertAtCursor(String theString, InsertFlags insertFlags = .None)
        {
			var insertFlags;

			if ((HasSelection()) && (gApp.mSymbolReferenceHelper != null) && (gApp.mSymbolReferenceHelper.mKind == .Rename))
			{
				bool hasSymbolSelection = true;
				mSelection.Value.GetAsForwardSelect(var startPos, var endPos);
				var text = mEditWidget.mEditWidgetContent.mData.mText;
				for (int i = startPos; i < endPos; i++)
				{
					if (!((SourceElementFlags)text[i].mDisplayFlags).HasFlag(.SymbolReference))
					{
						hasSymbolSelection = false;
					}
				}
				if (hasSymbolSelection)
					mInsertDisplayFlags = (uint8)SourceElementFlags.SymbolReference;
			}

			if ((mSourceViewPanel != null) && (!insertFlags.HasFlag(.IsGroupPart)))
			{
				mSourceViewPanel.EnsureTrackedElementsValid();
			}
         
            base.InsertAtCursor(theString, insertFlags);
			mInsertDisplayFlags = 0;
			if ((!mIgnoreSetHistory) && (!insertFlags.HasFlag(.NoMoveCursor)) && (!insertFlags.HasFlag(.NoRecordHistory)))
            	RecordHistoryLocation();
        }        

        public override void CursorToLineEnd()
        {
            if (!mAllowVirtualCursor)
            {
                base.CursorToLineEnd();
                return;
            }

            int line;
            int lineChar;
            GetCursorLineChar(out line, out lineChar);            
            //CursorLineAndColumn = LineAndColumn(line, GetLineEndColumn(line, false, false));            
            //CursorMoved();

			float x;
			float y;
			float wantWidth = 0;
			int column = GetLineEndColumn(line, false, false, false, false, &wantWidth);
			GetTextCoordAtLineAndColumn(line, column, out x, out y);
			if (wantWidth != 0)
				x = wantWidth + mTextInsets.mLeft;
			MoveCursorToCoord(x, y);

            return;
        }

		bool IsTextSpanEmpty(int32 start, int32 length)
		{
			for (int32 i = start; i < start + length; i++)
			{
				char8 c = mData.mText[i].mChar;
				if (!c.IsWhiteSpace)
					return false;
			}
			return true;
		}

        public bool OpenCodeBlock()
        {
            int lineIdx;

            if (HasSelection())
            {
                int minLineIdx = 0;
                int minLineCharIdx = 0;
                int maxLineIdx = 0;
                int maxLineCharIdx = 0;

				var selection = mSelection.Value;

                GetLineCharAtIdx(selection.MinPos, out minLineIdx, out minLineCharIdx);
                GetLineCharAtIdx(selection.MaxPos, out maxLineIdx, out maxLineCharIdx);

				bool isMultilineSelection = minLineIdx != maxLineIdx;
				
                String lineText = scope String();
                GetLineText(minLineIdx, lineText);
                lineText.Trim();
                if (lineText.Length == 0)
                    minLineIdx++;
                /*if ((lineText.Length > 0) && (lineText[lineText.Length - 1] == '{'))
                {
                    minLineIdx++;                    
                }*/

                //lineText = GetLineText(maxLineIdx).Trim();

                int maxLineStartIdx;
                int maxLineEndIdx;
                GetLinePosition(maxLineIdx, out maxLineStartIdx, out maxLineEndIdx);
				int embeddedEndIdx = int32.MaxValue;

				UndoBatchStart undoBatchStart = null;

				if (!isMultilineSelection)
				{
					// Must have start of line selected
					for (int i = maxLineStartIdx; i < selection.MinPos; i++)
						if (!mData.mText[i].mChar.IsWhiteSpace)
							return false;
					for (int i = selection.MaxPos; i < maxLineEndIdx; i++)
						if (!mData.mText[i].mChar.IsWhiteSpace)
						{
							undoBatchStart = new UndoBatchStart("embeddedOpenCodeBlock");
							mData.mUndoManager.Add(undoBatchStart);

							mSelection = null;
							CursorTextPos = i;
							InsertAtCursor("\n");
							embeddedEndIdx = i;
							break;
						}
				}

                int maxSelPos = selection.MaxPos;
                if ((selection.MaxPos < mData.mTextLength) && (mData.mText[maxSelPos].mChar == '}'))
                    maxSelPos++;

				lineText.Clear();
                ExtractString(maxLineStartIdx, maxSelPos - maxLineStartIdx, lineText);
                lineText.Trim();

                if (lineText.Length == 0)
                    maxLineIdx--;
                /*if ((lineText.Length > 0) && (lineText[lineText.Length - 1] == '}'))
                    maxLineIdx--;*/

                if (minLineIdx > maxLineIdx)
                {
                    minLineIdx--;
                    maxLineIdx++;
                }                

                int selectedIndentCount = int32.MaxValue;
                for (lineIdx = minLineIdx; lineIdx <= maxLineIdx; lineIdx++)
                {
                    lineText.Clear();
                    GetLineText(lineIdx, lineText);
                    int32 curIndentCount = 0;
					int32 curSpaceCount = 0;
					bool hadContent = false;
                    for (int32 i = 0; i < lineText.Length; i++)
                    {
						if (lineText[i] == ' ')
						{
							curSpaceCount++;
							continue;
						}
                        if (lineText[i] != '\t')
						{
							hadContent = true;
                            break;
						}
						
                        curIndentCount++;
                    }
					if (hadContent)
					{
						curIndentCount += curSpaceCount / GetTabSpaceCount();
	                    selectedIndentCount = Math.Min(selectedIndentCount, curIndentCount);
					}
                }                 

				int indentCount = GetLineEndColumn(minLineIdx, true, true, true) / 4;
				bool wantsContentTab = indentCount == selectedIndentCount;
				
                /*if (mAllowVirtualCursor)
                {
					// Why did we have this?
                    int expectedIndentCount = GetLineEndColumn(minLineIdx, true, true) / 4;
					if ((!isMultilineSelection) && (indentCount == expectedIndentCount + 1))
						wantsContentTab = false;
					indentCount = expectedIndentCount;
                }*/

                var indentTextAction = new EditWidgetContent.IndentTextAction(this);
                EditSelection newSel = selection;

                selection.MakeForwardSelect();        
				mSelection = selection;

                int startAdjust = 0;
                int endAdjust = 0;

				//GetTextData();
				//Debug.Assert(mData.mLineStarts != null);

				int32[] lineStarts = scope int32[maxLineIdx - minLineIdx + 2];
				int32[] lineEnds = scope int32[maxLineIdx - minLineIdx + 2];

				for (lineIdx = minLineIdx; lineIdx <= maxLineIdx + 1; lineIdx++)
				{
					int lineStart;
					int lineEnd;
					GetLinePosition(lineIdx, out lineStart, out lineEnd);
					lineStarts[lineIdx - minLineIdx] = (int32)lineStart;
					lineEnds[lineIdx - minLineIdx] = (int32)lineEnd;
				}

                startAdjust += indentCount + 3;
                for (lineIdx = minLineIdx; lineIdx <= maxLineIdx + 1; lineIdx++)
                {
                    int lineStart = lineStarts[lineIdx - minLineIdx];
                    int lineEnd = lineEnds[lineIdx - minLineIdx];
					bool isEmbeddedEnd = false;
					if (lineStart > embeddedEndIdx)
					{
						isEmbeddedEnd = true;
						//lineStart = maxLineEndIdx;
						//lineEnd = maxLineEndIdx;
					}

					if (lineEnd != mData.mTextLength)
					{
                    	lineStart += endAdjust;
						lineEnd += endAdjust;
					}
                    if (lineIdx == minLineIdx)
                    {
                        int32 i;
                        for (i = 0; i < indentCount; i++)
                        {
                            InsertText(lineStart + i, "\t");
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '\t'));
                            endAdjust++;
                        }

                        newSel.mStartPos = (int32)(lineStart + i);

                        if (wantsContentTab)
                        {
                            InsertText(lineStart + i, "{\n\t");
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '{'));
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i + 1), '\n'));
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i + 2), '\t'));
                            endAdjust += 3;
                        }
                        else
                        {
                            InsertText(lineStart + i, "{\n");
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '{'));
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i + 1), '\n'));
                            startAdjust -= 1;
                            endAdjust += 2;
                        }
						// Make sure those InsertText's don't clear our mLineStarts!  That would slow things down
						//  and it would screw up our endAdjust calculation
						//Debug.Assert(mData.mLineStarts != null);

                        newSel.mEndPos = (int32)(lineStart + i + 1);
                    }
                    else if (lineIdx == maxLineIdx + 1)
                    {
						if (lineEnd == mData.mTextLength)
						{
							InsertText(lineStart, "\n");
							lineStart++;
						}

                        int32 i;
                        for (i = 0; i < indentCount; i++)
                        {
                            InsertText(lineStart + i, "\t");
                            indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '\t'));
                        }

						if (isEmbeddedEnd)
						{
							InsertText(lineStart + i, "}");
							indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '}'));
						}
						else
						{
	                        InsertText(lineStart + i, "}\n");
	                        indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i), '}'));
	                        indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart + i + 1), '\n'));
						}

						newSel.mEndPos = (int32)(lineStart + i + 1);
                    }
                    else if (wantsContentTab)
                    {
                     	if (lineEnd - lineStart >= 1)
						{
							char8 c = mData.mText[lineStart].mChar;
							if (c != '#')
							{
		                        InsertText(lineStart, "\t");
		                        indentTextAction.mInsertCharList.Add(Tuple<int32, char8>((int32)(lineStart), '\t'));
		                        endAdjust++;
							}
						}
                    }                    
                }
                // We shouldn't do ContentChanged until the end, otherwise we thrash on calculating mLineStarts in GetTextData
                ContentChanged();
                ResetWantX();
                mCursorBlinkTicks = 0;
                
                CursorTextPos = newSel.mEndPos;

                indentTextAction.mNewSelection = newSel;
                if ((indentTextAction.mInsertCharList.Count > 0) ||
                    (indentTextAction.mRemoveCharList.Count > 0))
                    mData.mUndoManager.Add(indentTextAction);

				if (undoBatchStart != null)
					mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

                mSelection = newSel;
                return true;
            }

            int lineCharIdx;
            GetLineCharAtIdx(CursorTextPos, out lineIdx, out lineCharIdx);

            // Duplicate the tab start from the previous line
            if (lineIdx > 0)
            {
                int indentCount = 0;

                String lineText = scope String();
                GetLineText(lineIdx, lineText);
                if (String.IsNullOrWhiteSpace(lineText)) // Is on new line?
                {
                    GetLineText(lineIdx - 1, lineText);
                    for (int32 i = 0; i < lineText.Length; i++)
                    {
                        if (lineText[i] != '\t')
                            break;
                        indentCount++;
                    }

                    lineText.Trim();
                    if ((lineText.Length > 0) && (lineText[lineText.Length - 1] == '{'))
                        indentCount++;                    

                    if (mAllowVirtualCursor)
                    {
                        int column = GetLineEndColumn(lineIdx, true, false, true);
                        CursorLineAndColumn = LineAndColumn(lineIdx, column);
                        indentCount = column / 4;
                    }

                    if (lineIdx < GetLineCount() - 1)
                    {
                        String nextLineText = scope String();
                        GetLineText(lineIdx + 1, nextLineText);
                        nextLineText.TrimEnd();
                        int32 spaceCount = 0;
                        for (int32 i = 0; i < nextLineText.Length; i++)
                        {
                            if (nextLineText[i] == '\t')
                                spaceCount += 4;
                            else if (nextLineText[i] == ' ')
                                spaceCount++;
                            else
                                break;                            
                        }
                        if (spaceCount > indentCount * 4)
                        {
                            // Next line is indented? Just simple open
                            InsertAtCursor("{");
							return true;
                        }
                    }

                    String sb = scope String();

                    if (mAllowVirtualCursor)
                    {
                        GetLineText(lineIdx, lineText);
                        if ((lineText.Length > 0) && (String.IsNullOrWhiteSpace(lineText)))
                        {
                            ClearLine();
                            CursorLineAndColumn = LineAndColumn(lineIdx, indentCount * 4);                
                        }

                        // This will already insert at correct position
                        InsertAtCursor("{");
                        sb.Append("\n");
                        sb.Append("\n");
                        for (int32 i = 0; i < indentCount; i++)
                            sb.Append("\t");
                        sb.Append("}");
                        InsertAtCursor(sb);
                    }
                    else
                    {
                        lineText.Clear();
                        GetLineText(lineIdx, lineText);
                        for (int i = lineText.Length; i > indentCount; i--)
                        {
                            CursorTextPos--;
                            DeleteChar();
                            lineText.Remove(i - 1);
                        }

                        for (int i = lineText.Length; i < indentCount; i++)
                            sb.Append("\t");
                        sb.Append("{\n");
                        for (int i = 0; i < indentCount; i++)
                            sb.Append("\t");
                        sb.Append("\t\n");
                        for (int i = 0; i < indentCount; i++)
                            sb.Append("\t");
                        sb.Append("}");
                        InsertAtCursor(sb);
                    }

                    CursorTextPos -= indentCount + 2;
                    CursorToLineEnd();
                    return true;
                }
            }

			return false;
        }

		public bool CloseCodeBlock()
		{
			if (!HasSelection())
				return false;

			int minIdx = mSelection.Value.MinPos;
			int maxIdx = mSelection.Value.MaxPos;

			while (true)
			{
				if (minIdx >= mData.mTextLength)
					return false;

				char8 c = mData.mText[minIdx].mChar;
				if (c.IsWhiteSpace)
				{
					minIdx++;
					continue;
				}

				if (c != '{')
					return false;
				break;
			}

			while (true)
			{
				if (maxIdx == 0)
					break;
				char8 c = mData.mText[maxIdx - 1].mChar;
				if (c.IsWhiteSpace)
				{
					maxIdx--;
					continue;
				}
				if (c != '}')
					return false;
				break;
			}

			UndoBatchStart undoBatchStart = new UndoBatchStart("closeCodeBlock");
			mData.mUndoManager.Add(undoBatchStart);
			mSelection = EditSelection(maxIdx - 1, maxIdx);
			GetLineCharAtIdx(maxIdx - 1, var endLine, var endLineChar);
			String endStr = scope .();
			ExtractLine(endLine, endStr);
			endStr.Trim();
			if (endStr == "}")
			{
				CursorLineAndColumn = .(endLine, 0);
				DeleteLine();
				endLine--;
			}
			else
				DeleteSelection();

			mSelection = EditSelection(minIdx, minIdx + 1);
			GetLineCharAtIdx(minIdx, var startLine, var startLineChar);
			String startStr = scope .();
			ExtractLine(startLine, startStr);
			startStr.Trim();
			if (startStr == "{")
			{
				CursorLineAndColumn = .(startLine, 0);
				DeleteLine();
				endLine--;
			}
			else
				DeleteSelection();

			GetLinePosition(startLine, var startLineStart, var startLineEnd);
			GetLinePosition(endLine, var endLineStart, var endLineEnd);

			while (startLineStart < startLineEnd)
			{
				char8 c = mData.mText[startLineStart].mChar;
				if (!c.IsWhiteSpace)
					break;
				startLineStart++;
			}
			CursorTextPos = startLineStart;

			mSelection = EditSelection(startLineStart, endLineEnd);
			
			if (undoBatchStart != null)
				mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

			return true;
		}

		public bool ToggleComment()
		{
		    if ((HasSelection()) && (mSelection.Value.Length > 1))
		    {
				var startLineAndCol = CursorLineAndColumn ;

				UndoBatchStart undoBatchStart = new UndoBatchStart("embeddedToggleComment");
				mData.mUndoManager.Add(undoBatchStart);

				int minPos = mSelection.GetValueOrDefault().MinPos;
				int maxPos = mSelection.GetValueOrDefault().MaxPos;
				mSelection = null;

				var str = scope String();
				ExtractString(minPos, maxPos - minPos, str);
				
				var trimmedStr = scope String();
				trimmedStr.Append(str);
				int32 startLen = (int32)trimmedStr.Length;
				trimmedStr.TrimStart();
				int32 afterTrimStart = (int32)trimmedStr.Length;
				trimmedStr.TrimEnd();
				int32 afterTrimEnd = (int32)trimmedStr.Length;

				int firstCharPos = minPos + (startLen - afterTrimStart);
				int lastCharPos = maxPos - (afterTrimStart - afterTrimEnd);

				if (trimmedStr.StartsWith("/*"))
				{
					if (trimmedStr.EndsWith("*/"))
					{
						mSelection = EditSelection(firstCharPos, firstCharPos + 2);
						DeleteChar();
						mSelection = EditSelection(lastCharPos - 4, lastCharPos - 2);
						DeleteChar();
					}
				}
				else
				{
					CursorTextPos = firstCharPos;
					InsertAtCursor("/*");
					CursorTextPos = lastCharPos + 2;
					InsertAtCursor("*/");
				}
				
				if (undoBatchStart != null)
					mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

				CursorLineAndColumn = startLineAndCol;

		        mSelection = null;
		        return true;
		    }

			return false;
		}

        public override void ContentChanged()
        {
            base.ContentChanged();
            mCursorStillTicks = 0;
			if (mSourceViewPanel != null)
			{
				if (mSourceViewPanel.mProjectSource != null)
					mSourceViewPanel.mProjectSource.HasChangedSinceLastCompile = true;
			}
            //if ((IDEApp.sApp.mSymbolReferenceHelper != null) && (IDEApp.sApp.mSymbolReferenceHelper.mKind != ))

            /*if ((!mIsInKeyChar) && (mAutoComplete != null))
                mAutoComplete.CloseListWindow();*/
        }

        public override void KeyChar(char32 keyChar)
        {
			scope AutoBeefPerf("SEWC.KeyChar");

			if (mIgnoreKeyChar)
			{
				mIgnoreKeyChar = false;
				return;
			}

            if (mIsReadOnly)
            {
                base.KeyChar(keyChar);
                return;
            }

			bool isTab = (keyChar == '\t') && (!mWidgetWindow.IsKeyDown(.Shift));
            if ((gApp.mSymbolReferenceHelper != null) && (gApp.mSymbolReferenceHelper.IsRenaming))
            {         
                if ((keyChar == '\r') || (keyChar == '\n'))
                {                    
                    gApp.mSymbolReferenceHelper.Close();
                    return;
                }
                else if (keyChar == '\t')
                {
                    if (HasSelection())
                    {
                        mSelection = null;
                        return;
                    }
                }
                else if (keyChar == '\b')
                {
                    if (HasSelection())                    
                        mSelection = null;                    
                }
            }

            int32 startRevision = mData.mCurTextVersionId;
            
            bool doAutocomplete = isTab;
			if ((mAutoComplete != null) && (keyChar == '\r') &&
				((!mIsMultiline) || (mAutoComplete.mIsUserRequested)))
				doAutocomplete = true;
            bool hasEmptyAutocompleteReplace = true;
            if (mAutoComplete != null)
                hasEmptyAutocompleteReplace = mAutoComplete.mInsertEndIdx == -1;
			bool didAutoComplete = false;

            bool isEndingChar = (keyChar >= (char8)32) && !keyChar.IsLetterOrDigit && (keyChar != '_') && (keyChar != '~') && (keyChar != '=') && (keyChar != '!') && (keyChar != ':');

			if (gApp.mSettings.mEditorSettings.mAutoCompleteRequireTab)
			{
				doAutocomplete = isTab;
				if (keyChar == '\r')
				{
					if (mAutoComplete != null)
						mAutoComplete.Close();
				}
				isEndingChar = false;
			}
            
			if (isEndingChar)
            {
				bool forceAsyncFinish = false;
				if (mCursorTextPos > 0)
				{
					char8 c = mData.mText[mCursorTextPos - 1].mChar;
					var displayType = (SourceElementType)mData.mText[mCursorTextPos - 1].mDisplayTypeId;
					if ((displayType != .Comment) && (displayType != .Literal))
					{
						if ((c.IsLetterOrDigit) || (c == '_'))
						{
							// Could be a symbol autocomplete?
							forceAsyncFinish = true;
						}
					}
				}

				if (forceAsyncFinish)
				{
					if (mOnFinishAsyncAutocomplete != null)
						mOnFinishAsyncAutocomplete();
					doAutocomplete = true;
				}
            }
			else
			{
				if ((doAutocomplete) && (mOnFinishAsyncAutocomplete != null))
					mOnFinishAsyncAutocomplete();
			}

            if ((mAutoComplete != null) && (mAutoComplete.mAutoCompleteListWidget != null))
            {
				if ((mAutoComplete.mInsertEndIdx != -1) && (mAutoComplete.mInsertEndIdx != mCursorTextPos) && (keyChar != '\t') && (keyChar != '\r') && (keyChar != '\n'))
					doAutocomplete = false;
				
                if ((mAutoComplete.IsInsertEmpty()) && (!mAutoComplete.mIsFixit) && (keyChar != '.') && (keyChar != '\t'))
                {
                    // Require a '.' or tab to insert autocomplete when we don't have any insert section (ie: after an 'enumVal = ')
                    doAutocomplete = false;
                }

				if ((keyChar == '[') && (mAutoComplete.mInsertStartIdx >= 0) && (mData.mText[mAutoComplete.mInsertStartIdx].mChar != '.'))
				{
					// Don't autocomplete for ".[" (member access attributes)
					doAutocomplete = false;
				}

				if (((keyChar == '.') || (keyChar == '*')) &&
					((mAutoComplete.mInsertEndIdx == -1) || (mAutoComplete.IsInsertEmpty())))
				{
					// Don't autocomplete for object allocation when we haven't typed anything yet but then we press '.' (for 'inferred type')
					doAutocomplete = false;
				}

				if ((mAutoComplete.mUncertain) && (keyChar != '\t'))
					doAutocomplete = false;

                if (doAutocomplete)
                {
					if (mOnFinishAsyncAutocomplete != null)
						mOnFinishAsyncAutocomplete();

                    UndoBatchStart undoBatchStart = new UndoBatchStart("autocomplete");
                    mData.mUndoManager.Add(undoBatchStart);

                    bool allowChar = true;
                    mIsInKeyChar = true;
                    String insertType = scope String();
					String insertStr = scope String();
                    mAutoComplete.InsertSelection(keyChar, insertType, insertStr);
                    mIsInKeyChar = false;
                    if (insertType != null)
                    {
						//mGenerateAutocompleteHandler(false, false);
                        if (((insertType == "method") && (keyChar == '(')) ||
							((insertType == "token") && (insertStr == "override")))
                        {
							if (IsCursorVisible(false))
                            	mOnGenerateAutocomplete('\0', default);
							didAutoComplete = true;
						}
                        else
						{
							//Debug.WriteLine("Autocomplete InsertSelection {0}", mAutoComplete);
							if (mAutoComplete != null)
                            	mAutoComplete.CloseListWindow();
							if ((mAutoComplete != null) && (mAutoComplete.mInvokeWindow != null))
							{
								// Update invoke since the autocompletion may have selected a different overload
								if (IsCursorVisible(false))
									mOnGenerateAutocomplete(keyChar, .OnlyShowInvoke);
							}
						}

                        if ((insertType == "override") || (insertType == "fixit"))
                        {                            
                            allowChar = false;
                        }

                        if ((insertType == "mixin") && (keyChar == '!'))
                        {
                            // It already ends with the char8 that we have
                            allowChar = false;
                        }
                    }
                    else
                        mAutoComplete.CloseListWindow();

                    if ((keyChar == '\t') || (keyChar == '\r')) // Let other chars besides explicit-insert chrars pass through       
                    {                        
                        allowChar = false;
                    }

                    mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
                    if (!allowChar)
                        return;
                }
            }            

			SourceElementType prevElementType = SourceElementType.Normal;
			char8 prevChar = 0;
			int cursorTextPos = CursorTextPos;
			if (cursorTextPos != 0)
			{
			    prevElementType = (SourceElementType)mData.mText[cursorTextPos - 1].mDisplayTypeId;
				prevChar = mData.mText[cursorTextPos - 1].mChar;
			}

            if (((keyChar == '\n') || (keyChar == '\r')) && (mIsMultiline) && (!CheckReadOnly()))
            {
                UndoBatchStart undoBatchStart = new UndoBatchStart("newline");
                mData.mUndoManager.Add(undoBatchStart);                

                var lineAndColumn = CursorLineAndColumn;
                int lineStart;
                int lineEnd;
                GetLinePosition(lineAndColumn.mLine, out lineStart, out lineEnd);
                String beforeCursorLineText = scope String();
                ExtractString(lineStart, CursorTextPos - lineStart, beforeCursorLineText);
                String fullCursorLineText = scope String();
                ExtractString(lineStart, lineEnd - lineStart, fullCursorLineText);
                if ((String.IsNullOrWhiteSpace(beforeCursorLineText)) && (!String.IsNullOrWhiteSpace(fullCursorLineText)))
                {
                    // Cursor is within whitesapce at line start, insert newline at actual start of line
                    InsertAtCursor(""); // To save cursor position
                    var newLineAndColumn = lineAndColumn;
                    newLineAndColumn.mColumn = 0;
                    CursorLineAndColumn = newLineAndColumn;
                    InsertAtCursor("\n");
                    CursorLineAndColumn = LineAndColumn(lineAndColumn.mLine + 1, lineAndColumn.mColumn);
                }
                else
                {
					EditWidgetContent.InsertFlags insertFlags = .None;
					if (!HasSelection())
					{
						// Select whitespace at the end of the line so we trim it as we InsertAtCursor
						mSelection = EditSelection(CursorTextPos, CursorTextPos);
						for (int checkIdx = beforeCursorLineText.Length - 1; checkIdx >= 0; checkIdx--)
						{
							char8 c = beforeCursorLineText[checkIdx];
							if (!c.IsWhiteSpace)
								break;
							mSelection.ValueRef.mStartPos--;
						}
						insertFlags |= .NoRestoreSelectionOnUndo;
					}

                    InsertAtCursor("\n", insertFlags);

                    int line;
                    int lineChar;
                    GetCursorLineChar(out line, out lineChar);
                    String lineText = scope String();
                    GetLineText(line, lineText);
                    if (!String.IsNullOrWhiteSpace(lineText))
                    {
                        // If we're breaking a line, clear the previous line if we just have whitespace on it
						String prevLineText = scope String();
						GetLineText(line - 1, prevLineText);
						prevLineText.Trim();
                        if (prevLineText.Length == 0)
                        {
                            lineAndColumn = CursorLineAndColumn;
                            CursorLineAndColumn = LineAndColumn(line - 1, 0);
                            ClearLine();
                            CursorLineAndColumn = lineAndColumn;
                        }

                        String lineTextAtCursor = scope String();
                        GetLineText(line, lineTextAtCursor);
						// Only treat as block open if it's a lonely '{', not a complete one-line block
                        bool isBlockOpen = (lineTextAtCursor.Length > 0) && (lineTextAtCursor[0] == '{') && (!lineTextAtCursor.Contains("}"));
                        int column = GetLineEndColumn(line, isBlockOpen, true, true);
						if (lineTextAtCursor.StartsWith("}"))
						{
							column -= 4; // SpacesInTab
						}
                        if (column > 0)
                        {
							String tabStr = scope String();
							tabStr.Append('\t', column / 4);
							tabStr.Append(' ', column % 4);
                            InsertAtCursor(tabStr);
						}

						// Insert extra blank line if we're breaking between a { and a }
						if ((lineTextAtCursor.StartsWith("}")) && (prevLineText.EndsWith("{")))
						{
							CursorLineAndColumn = LineAndColumn(line - 1, 0);
							CursorToLineEnd();
							InsertAtCursor("\n");
							CursorLineAndColumn = LineAndColumn(line, 0);
							CursorToLineEnd();
						}
                    }
                    else
                        CursorToLineEnd();
                }
                
                if ((mAutoComplete != null) && (mAutoComplete.mInvokeWidget != null))
                {
                    // Update the position of the invoke widget
					if (IsCursorVisible(false))
                    	mOnGenerateAutocomplete('\0', default);
                }

                mData.mUndoManager.Add(undoBatchStart.mBatchEnd);

				if ((prevElementType == .Normal) &&
					((prevChar == '.') || (prevChar == '(')))
				{
					// Leave it be
				}
				else
				{
					if (mAutoComplete != null)
						mAutoComplete.CloseListWindow();
				}
				
				mAutoComplete?.UpdateAsyncInfo();

                return;
            }

			if ((keyChar == '/') && (ToggleComment()))
			{
				return;
			}

            if (prevElementType == SourceElementType.Comment)
            {
                if ((cursorTextPos < mData.mTextLength - 1) && (mData.mText[cursorTextPos - 1].mChar == '\n'))
                    prevElementType = (SourceElementType)mData.mText[cursorTextPos].mDisplayTypeId;

				if (keyChar == '*')
				{
					if (cursorTextPos >= 3)
					{
						if ((mData.mText[cursorTextPos - 2].mChar == '/') &&
							(mData.mText[cursorTextPos - 1].mChar == '*') &&
							(mData.mText[cursorTextPos].mChar == '\n'))
						{
							InsertAtCursor("*");
							let prevLineAndColumn = mEditWidget.mEditWidgetContent.CursorLineAndColumn;
							int column = GetLineEndColumn(prevLineAndColumn.mLine, false, true, true);
							var str = scope String();
							str.Append("\n");
							str.Append('\t', column / mTabLength);
							str.Append("*/");
							InsertAtCursor(str);
							mEditWidget.mEditWidgetContent.CursorLineAndColumn = prevLineAndColumn;
							return;
						}
					}
				}
            }

            bool doChar = true;
            if (prevElementType != SourceElementType.Comment)
            {
                doChar = false;
                char8 char8UnderCursor = (char8)0;
				bool cursorInOpenSpace = false;

                //int cursorTextPos = CursorTextPos;
                if (cursorTextPos < mData.mTextLength)
                {
                    char8UnderCursor = (char8)mData.mText[cursorTextPos].mChar;
					cursorInOpenSpace = ((char8UnderCursor == ')') || (char8UnderCursor == ']') || (char8UnderCursor == (char8)0) || (char8UnderCursor.IsWhiteSpace));

                    if ((char8UnderCursor == keyChar) && (!HasSelection()))
                    {
						var wantElementType = SourceElementType.Normal;

						bool ignore = false;
						int checkPos = cursorTextPos;
						if ((keyChar == '"') || (keyChar == '\''))
						{
							checkPos = Math.Max(cursorTextPos - 1, 0);
							wantElementType = .Literal;
							if (cursorTextPos > 0)
							{
								if (mData.mText[cursorTextPos - 1].mChar == '\\')
									ignore = true;
							}
						}

						if (!ignore)
						{
	                        if ((mData.mText[checkPos].mDisplayTypeId == (int32)wantElementType) &&
	                            ((keyChar == '"') || (keyChar == '\'') || (keyChar == ')') || (keyChar == ']') || (keyChar == '>') || (keyChar == '}')))
	                        {
	                            mJustInsertedCharPair = false;
	                            CursorTextPos++;
	                            return;
	                        }
							else
							{
								if ((keyChar == '"') || (keyChar == '\''))
									cursorInOpenSpace = true;
							}
						}
                    }
					else if ((keyChar == '}') && (!HasSelection())) // Jump down to block close, as long as it's just whitespace between us and there
					{
						int checkPos = cursorTextPos;
						while (checkPos < mData.mTextLength - 1)
						{
							char8 checkC = mData.mText[checkPos].mChar;
							if (checkC == '}')
							{
								int closeLine;
								int closeLineChar;
								GetLineCharAtIdx(checkPos, out closeLine, out closeLineChar);

								int expectedColumn = GetLineEndColumn(closeLine, true, true, true) - 4;
								int actualColumn = GetActualLineStartColumn(closeLine);

								if (expectedColumn == actualColumn)
								{
									CursorTextPos = checkPos + 1;
									return;
								}
							}
							if (!checkC.IsWhiteSpace)
								break;
							checkPos++;
						}
					}
                }
				else
					cursorInOpenSpace = true;

                bool cursorAfterText = false;
                if (cursorTextPos > 0)
                {
                    char8 char8Before = (char8)mData.mText[cursorTextPos - 1].mChar;
                    if (char8Before.IsLetterOrDigit)
                        cursorAfterText = true;
                }

				if (HasSelection())
				{
					if ((keyChar == '{') && (OpenCodeBlock()))
 					{
						// OpenCodeBlock handled this char
					}
					else if ((keyChar == '{') || (keyChar == '('))
					{
						UndoBatchStart undoBatchStart = new UndoBatchStart("blockSurround");
						mData.mUndoManager.Add(undoBatchStart);                

						int minPos = mSelection.GetValueOrDefault().MinPos;
						int maxPos = mSelection.GetValueOrDefault().MaxPos;
						mSelection = null;
						CursorTextPos = minPos;
						String insertStr = scope String();
						insertStr.Append(keyChar);
						InsertAtCursor(insertStr);
						CursorTextPos = maxPos + 1;
						insertStr.Clear();
						if (keyChar == '(')
							insertStr.Append(')');
						else if (keyChar == '{')
							insertStr.Append('}');
						InsertAtCursor(insertStr);

						mData.mUndoManager.Add(undoBatchStart.mBatchEnd);
					}
					else if ((keyChar == '}') && (CloseCodeBlock()))
					{
						// CloseCodeBlock handled this char
					}
					else
						doChar = true;
				}
                else if (prevElementType == SourceElementType.Literal)
                    doChar = true;
                else if (keyChar == '#')
                {
                    int32 line = CursorLineAndColumn.mLine;
                    String lineText = scope String();
                    GetLineText(line, lineText);
                    lineText.Trim();
                    if ((lineText.Length == 0) && (mAllowVirtualCursor))
                        CursorLineAndColumn = LineAndColumn(line, 0);
                    doChar = true;
                }
                else if ((keyChar == '{') && (OpenCodeBlock()))
				{
                    // OpenCodeBlock handled this char8
				}
                else if (keyChar == '}')
                {
                    int32 line = CursorLineAndColumn.mLine;
                    String lineText = scope String();
                    GetLineText(line, lineText);
                    lineText.Trim();
                    if (lineText.Length == 0)
                    {
                        ClearLine();
                        CursorLineAndColumn = LineAndColumn(line, Math.Max(0, GetLineEndColumn(line, false, false) - 4));
                        CursorMoved();
                    }
                    base.KeyChar(keyChar);
                }
                else if ((keyChar == '(') && (cursorInOpenSpace))
                    InsertCharPair("()");
				else if ((keyChar == '{') && (cursorInOpenSpace))
				{
					/*int lineStart;
					int lineEnd;
					GetLinePosition(CursorLineAndColumn.mLine, out lineStart, out lineEnd);
					String endingStr = scope String();
					ExtractString(CursorTextPos, lineEnd - CursorTextPos, endingStr);
					endingStr.Trim();

					// Only do the "{}" pair if it's inside a a param call or something already, IE: 
					//  handler.Add(new () => {});
					if (endingStr.Length != 0)
						InsertCharPair("{}");
					else
						doChar = true;*/
					InsertCharPair("{}");
				}
                else if ((keyChar == '[') && (cursorInOpenSpace))
                    InsertCharPair("[]");
                else if ((keyChar == '\"') && (cursorInOpenSpace) && (!cursorAfterText))
                    InsertCharPair("\"\"");
                else if ((keyChar == '\'') && (cursorInOpenSpace) && (!cursorAfterText))
                    InsertCharPair("\'\'");
                else
                    doChar = true;
            }
            
            if (doChar)
            {
                mIsInKeyChar = true;
                base.KeyChar(keyChar);
                mIsInKeyChar = false;
            }

            if ((keyChar == '\b') || (keyChar == '\r') || (keyChar >= (char8)32))
            {
                bool isHighPri = (keyChar == '(') || (keyChar == '.');
				bool needsFreshAutoComplete = ((isHighPri) /*|| (!mAsyncAutocomplete)*/ || (mAutoComplete == null) || (mAutoComplete.mAutoCompleteListWidget == null));

				if ((needsFreshAutoComplete) && (keyChar == '\b'))
				{
					if ((prevChar != 0) && (prevChar.IsWhiteSpace) && (prevElementType != .Comment))
					{
						needsFreshAutoComplete = false;
					}
				}

				if (mAutoComplete != null)
				{
				    mAutoComplete.UpdateAsyncInfo();
					needsFreshAutoComplete = true;
				}

                if ((needsFreshAutoComplete) && (!didAutoComplete))
                {
					if (IsCursorVisible(false))
						mOnGenerateAutocomplete(keyChar, isHighPri ? .HighPriority : default);
                }
            }
            else if (mData.mCurTextVersionId != startRevision)
            {
                if (mAutoComplete != null)
                    mAutoComplete.CloseListWindow();
            }

            if ((keyChar.IsLower) || (keyChar == ' ') || (keyChar == ':'))
            {
                int cursorTextIdx = CursorTextPos;
                int line;
                int lineChar;
                GetLineCharAtIdx(cursorTextIdx, out line, out lineChar);

                String lineText = scope String();
                GetLineText(line, lineText);
                String trimmedLineText = scope String(lineText);
                trimmedLineText.TrimStart();

                if ((keyChar == ' ') || (keyChar == ':'))
                {
					bool isLabel = false;
					if (keyChar == ':')
					{
						isLabel = trimmedLineText != "scope:";
						for (var c in trimmedLineText.RawChars)
						{
							if (c.IsWhiteSpace)
								isLabel = false;
						}
					}

                    if (((trimmedLineText == "case ") || (trimmedLineText == "when ") || isLabel) &&
						(prevElementType != .Comment) && (!IsInTypeDef(line)))
                    {
                        int wantLineColumn = 0;
                        if (line > 0)
                        {
							if ((isLabel) && (trimmedLineText != "default:"))
								wantLineColumn = GetLineEndColumn(line, true, true, true);
							else
                            	wantLineColumn = GetLineEndColumn(line, false, true, true) - 4;
						}

                        // Move "case" back to be inline with switch statement
						if (lineChar > trimmedLineText.Length)
						{
	                        String tabStartStr = scope String();
	                        tabStartStr.Append(lineText, 0, lineChar - trimmedLineText.Length);
	                        int32 columnPos = (int32)(GetTabbedWidth(tabStartStr, 0) / mCharWidth + 0.001f);
	                        if (columnPos >= wantLineColumn + 4)
	                        {
	                            mSelection = EditSelection();
	                            mSelection.ValueRef.mEndPos = (int32)(cursorTextIdx - trimmedLineText.Length);
	                            if (lineText.EndsWith(scope String("    ", trimmedLineText), StringComparison.Ordinal))
	                                mSelection.ValueRef.mStartPos = mSelection.Value.mEndPos - 4;
	                            else if (lineText.EndsWith(scope String("\t", trimmedLineText), StringComparison.Ordinal))
	                                mSelection.ValueRef.mStartPos = mSelection.Value.mEndPos - 1;
	                            if (mSelection.Value.mStartPos > 0)
	                            {
	                                DeleteSelection();
	                                CursorToLineEnd();
	                            }
	                            mSelection = null;
	                        }
						}
                    }
                }
                else
                {
                    if (trimmedLineText == "else")
                    {
                        int wantLineColumn = GetLineEndColumn(line, false, true, true, true);
                        //Debug.WriteLine("ElsePos: {0}", wantLineColumn);

                        int textStartPos = lineChar - (int32)trimmedLineText.Length;
                        String tabStartStr = scope String();
                        tabStartStr.Append(lineText, 0, textStartPos);
                        int32 columnPos = (int32)(GetTabbedWidth(tabStartStr, 0) / mCharWidth + 0.001f);
                        if (wantLineColumn > columnPos)
                        {
                            String insertStr = scope String(' ', wantLineColumn - columnPos);
							insertStr.Append("else");

                            var cursorPos = CursorTextPos;
                            mSelection = EditSelection();
                            mSelection.ValueRef.mStartPos = (int32)cursorPos - 4;
                            mSelection.ValueRef.mEndPos = (int32)cursorPos;
                            InsertAtCursor(insertStr, .NoRestoreSelectionOnUndo);

                            //var indentTextAction = new EditWidgetContent.IndentTextAction(this);
                            /*for (int i = 0; i < wantLineColumn - columnPos; i++)
                            {
                                InsertText(insertPos, " ");
                                //indentTextAction.mInsertCharList.Add(new Tuple<int, char8>(textStartPos, ' '));
                            }*/
                            //mUndoManager.Add(indentTextAction);
                        }
                    }
                }
            }

			mCursorImplicitlyMoved = true;
        }

		public void ShowAutoComplete(bool isUserRequested)
		{
			if (gApp.mSettings.mEditorSettings.mAutoCompleteShowKind != .None)
			{
				if (IsCursorVisible(false))
					mOnGenerateAutocomplete('\0', isUserRequested ? .UserRequested : .None);
			}
		}

        /// summary = "Hey This is a summary"
        /// param.keyCode = "Keycode of pressed key"
        /// param.isRepeat = "Whether the key is repeated"
        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
			 mIgnoreKeyChar = false;

			if (((keyCode == .Up) || (keyCode == .Down)) &&
				(mAutoComplete != null) && (mAutoComplete.IsShowing()) && (mAutoComplete.mListWindow != null) &&
				(!mAutoComplete.IsInPanel()) &&
				(!gApp.mSettings.mTutorialsFinished.mCtrlCursor))
			{
				if (mWidgetWindow.IsKeyDown(.Control))
				{
					if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mAllowMouseOutside))
						DarkTooltipManager.CloseTooltip();
					gApp.mSettings.mTutorialsFinished.mCtrlCursor = true;
				}
				else
				{
					GetTextCoordAtCursor(var cursorX, var cursorY);

					let tooltip = DarkTooltipManager.ShowTooltip("Hold CTRL when using UP and DOWN", this, cursorX - GS!(24), cursorY - GS!(40));
					if (tooltip != null)
						tooltip.mAllowMouseOutside = true;
					return;
				}
			}

			/*if (keyCode == .Tilde)
			{
				Thread.Sleep(300);
			}*/

            /*if ((keyCode == KeyCode.Space) && (mWidgetWindow.IsKeyDown(KeyCode.Control)))
            {
				//Debug.WriteLine("CursorPos: {0}", CursorTextPos);
				ShowAutoComplete();
                return;
            }*/

			if (keyCode == KeyCode.Apps)
			{
				GetTextCoordAtCursor(var x, var y);
				MouseClicked(x, y, x, y, 1);
				return;
			}

            if ((keyCode == KeyCode.Escape) && (mAutoComplete != null) && (mAutoComplete.IsShowing()))
            {                
                mAutoComplete.Close();
                return;
            }

            if ((keyCode == KeyCode.Escape) && (mOnEscape != null) && (mOnEscape()))
            {
                return;
            }
                       

            if ((keyCode == KeyCode.Escape) && (mSelection != null) && (mSelection.Value.HasSelection))
            {
                mSelection = null;
            }

            if (((keyCode == KeyCode.Up) || (keyCode == KeyCode.Down) || (keyCode == KeyCode.PageUp) || (keyCode == KeyCode.PageDown)) &&
                (mWidgetWindow.IsKeyDown(KeyCode.Control)))
            {
                if (mAutoComplete != null)
                {
                    if (mAutoComplete.mAutoCompleteListWidget != null)
                    {
                        int32 pageSize = (int32)(mAutoComplete.mAutoCompleteListWidget.mScrollContentContainer.mHeight / mAutoComplete.mAutoCompleteListWidget.mItemSpacing - 0.5f);
                        int32 moveDir = 0;
                        switch (keyCode)
                        {
                        case KeyCode.Up: moveDir = -1; break;
                        case KeyCode.Down: moveDir = 1; break;
                        case KeyCode.PageUp: moveDir = -pageSize; break;
                        case KeyCode.PageDown: moveDir = pageSize; break;
						default:
                        }
                        mAutoComplete.mAutoCompleteListWidget.SelectDirection(moveDir);
                    }
                    else if (mAutoComplete.mInvokeWidget != null)
                    {
                        mAutoComplete.mInvokeWidget.SelectDirection(((keyCode == KeyCode.Up) || (keyCode == KeyCode.PageUp)) ? -1 : 1);
                    }
                }

				// Disabled window-scroll code for ctrl+up/ctrl+down when autocomplete is not up
				return;
            }

            //var lineAndColumn = CursorLineAndColumn;

            int prevCursorPos;
            TryGetCursorTextPos(out prevCursorPos);
            int prevTextLength = mData.mTextLength;
            base.KeyDown(keyCode, isRepeat);

            if ((mAutoComplete != null) &&
				(keyCode != .Control) &&
				(keyCode != .Shift))
            {
				mAutoComplete.MarkDirty();
                bool isCursorInRange = prevCursorPos == CursorTextPos;
                if (mAutoComplete.mInvokeSrcPositions != null)
                {
                    isCursorInRange = (CursorTextPos > mAutoComplete.mInvokeSrcPositions[0]) &&
                        (CursorTextPos <= mAutoComplete.mInvokeSrcPositions[mAutoComplete.mInvokeSrcPositions.Count - 1]);
                }

                bool wasNormalTyping =
                    ((isCursorInRange) && (prevTextLength == mData.mTextLength))/* ||
                    ((prevCursorPos + 1 == CursorTextPos) && (prevTextLength + 1 == mTextLength)) ||
                    ((prevCursorPos - 1 == CursorTextPos) && (prevTextLength - 1 == mTextLength))*/;

                /*if ((lineAndColumn.mColumn != CursorLineAndColumn.mColumn) && (prevTextLength == mTextLength))
                    wasNormalTyping = false; // Moved into virtual space*/

                if (!wasNormalTyping)
                {
                    mAutoComplete.CloseInvoke();                    
                }
				else if ((keyCode == .Right) || (keyCode == .Left))
				{
					mAutoComplete.CloseListWindow();
				}
            }
        }

        void ReplaceWord(int leftIdx, int rightIdx, String origWord, String newWord)
        {
            mSelection.ValueRef.mStartPos = (int32)leftIdx;
            mSelection.ValueRef.mEndPos = (int32)rightIdx;
            InsertAtCursor(newWord, .NoRestoreSelectionOnUndo);
        }

        public bool DoSpellingPopup(Menu menu)
        {
            var spellChecker = IDEApp.sApp.mSpellChecker;
			if (spellChecker == null)
				return false;
            spellChecker.CancelBackground();

            int leftIdx = CursorTextPos;
            int rightIdx = leftIdx;

            while (leftIdx >= 1)
            {
                if ((mData.mText[leftIdx - 1].mDisplayFlags & (uint8)SourceElementFlags.SpellingError) == 0)
                    break;
                leftIdx--;
            }

            while (rightIdx < mData.mTextLength)
            {
                if ((mData.mText[rightIdx].mDisplayFlags & (uint8)SourceElementFlags.SpellingError) == 0)
                    break;
                rightIdx++;
            }

            if (rightIdx - leftIdx == 0)
                return false;

            String word = scope String();
            for (int i = leftIdx; i < rightIdx; i++)
                word.Append((char8)mData.mText[i].mChar);

            Menu item;

            bool hadSuggestion = false;
            List<String> suggestions = scope List<String>();
			defer (scope) ClearAndDeleteItems(suggestions);
            spellChecker.GetSuggestions(word, suggestions);
            for (var suggestion in suggestions)
            {
                if (suggestion.Length == 0)
                    break;
                hadSuggestion = true;
                item = menu.AddItem(suggestion);

				String replaceStr = new String(suggestion);
                item.mOnMenuItemSelected.Add(new (evt) =>
                    {
                        ReplaceWord(leftIdx, rightIdx, word, replaceStr);
                    }
                    ~
                    {
						delete replaceStr;
                    });
            }

            if (hadSuggestion)
                menu.AddItem();
			
			{
				item = menu.AddItem("Add word to dictionary");
				String wordCopy = new String(word);

	            item.mOnMenuItemSelected.Add(new (evt) =>
	                {                    
	                    using (spellChecker.mMonitor.Enter())
	                    {
	                        spellChecker.mCustomDictionaryWordList.Add(new String(wordCopy));
	                    }
	                    spellChecker.CancelBackground();
	                    spellChecker.AddWord(wordCopy);
	                    mSourceViewPanel.mWantsSpellCheck = true;
	                }
					~ delete wordCopy
                	);
			}

			{
	            item = menu.AddItem("Ignore all occurrences");
				String wordCopy = new String(word);
	            item.mOnMenuItemSelected.Add(new (evt) =>
	                {
	                    using (spellChecker.mMonitor.Enter())
	                    {
							word.ToLower();
	                        spellChecker.mIgnoreWordList.Add(word);
	                    }                    
	                    mSourceViewPanel.mWantsSpellCheck = true;
	                }
					~ delete wordCopy
                );
			}

            return true;
        }

		protected void ClampMenuCoords(ref float x, ref float y)
		{
			IDEUtils.ClampMenuCoords(ref x, ref y, mEditWidget, scope .(0, 0, GetLineHeight(0), GS!(32)));
		}

        public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
        {
            base.MouseClicked(x, y, origX, origY, btn);

			var useX = x;
			var useY = y;

			if ((btn == 0) && (mWidgetWindow.IsKeyDown(.Control)) && (x == origX) && (y == origY))
			{
				gApp.GoToDefinition();
				return;
			}

            if ((btn == 1) && (mSourceViewPanel != null))
            {
#unwarn
				ClampMenuCoords(ref useX, ref useY);

                Menu menu = new Menu();
                if (!DoSpellingPopup(menu))
                {
					bool hasText = false;
					bool hasSelection = HasSelection();
					if (hasSelection)
						hasText = true;
					else if ((GetLineCharAtCoord(x, y, var line, var lineChar, var overflowX)) || (overflowX < 2))
					{
						int textIdx = GetTextIdx(line, lineChar);
						for (int checkIdx = textIdx; checkIdx < Math.Min(textIdx + 1, mData.mTextLength); checkIdx++)
						{
							if (mData.mText[checkIdx].mDisplayTypeId != (uint8)BfSourceElementType.Comment)
							{
								char8 c = mData.mText[checkIdx].mChar;
								if (!c.IsWhiteSpace)
									hasText = true;
							}
						}
					}

					if (mSourceViewPanel.mIsSourceCode)
					{
	                    Menu menuItem;

						menuItem = menu.AddItem("Go to Definition");
						menuItem.SetDisabled(!hasText);
	                    menuItem.mOnMenuItemSelected.Add(new (evt) => gApp.GoToDefinition());

						menuItem = menu.AddItem("Rename Symbol");
						menuItem.SetDisabled(!hasText);
						menuItem.mOnMenuItemSelected.Add(new (evt) => gApp.Cmd_RenameSymbol());

	                    menuItem = menu.AddItem("Add Watch");
						menuItem.SetDisabled(!hasText);
	                    menuItem.mOnMenuItemSelected.Add(new (evt) =>
	                        {
	                            int line, lineChar;
	                            float overflowX;
								bool addedWatch = false;
	                            if (GetLineCharAtCoord(x, y, out line, out lineChar, out overflowX))
	                            {
	                                int textIdx = GetTextIdx(line, lineChar);
	                                String debugExpr = scope String();

	                                BfSystem bfSystem = IDEApp.sApp.mBfResolveSystem;
	                                BfPassInstance passInstance = null;
	                                BfParser parser = null;

	                                if ((mSelection != null) &&
	                                    (textIdx >= mSelection.Value.MinPos) &&
	                                    (textIdx < mSelection.Value.MaxPos))
	                                {
	                                    GetSelectionText(debugExpr);
	                                }
	                                else if (bfSystem != null)
	                                {
	                                    parser = bfSystem.CreateEmptyParser(null);
										defer(stack) delete parser;
	                                    var text = scope String();
	                                    mEditWidget.GetText(text);
	                                    parser.SetSource(text, mSourceViewPanel.mFilePath);
	                                    parser.SetAutocomplete(textIdx);
	                                    passInstance = bfSystem.CreatePassInstance();
	                                    parser.Parse(passInstance, !mSourceViewPanel.mIsBeefSource);
	                                    parser.Reduce(passInstance);
	                                    debugExpr = scope:: String();
	                                    parser.GetDebugExpressionAt(textIdx, debugExpr);
	                                }

	                                if (!String.IsNullOrEmpty(debugExpr))
	                                {
	                                    IDEApp.sApp.AddWatch(debugExpr);
										addedWatch = true;
	                                }
	                            }

								if (!addedWatch)
								{
									gApp.Fail("No watch text selected");
								}
	                        });

						// Fixits
						if ((mSourceViewPanel.mIsBeefSource) && (mSourceViewPanel.mProjectSource != null))
						{
							ResolveParams resolveParams = scope .();
							mSourceViewPanel.DoClassify(ResolveType.GetFixits, resolveParams, true);
							menuItem = menu.AddItem("Fixit");
							
							if (resolveParams.mNavigationData != null)
							{
								int32 fixitIdx = 0;
								for (let str in resolveParams.mNavigationData.Split('\n'))
								{
									var strItr = str.Split('\t');
									let cmd = strItr.GetNext().Value;
									if (cmd != "fixit")
										continue;
									let arg = strItr.GetNext().Value;
									var fixitItem = menuItem.AddItem(arg);

									var infoCopy = new String(resolveParams.mNavigationData);
									fixitItem.mOnMenuItemSelected.Add(new (menu) =>
										{
											mAutoComplete?.Close();
											var autoComplete = new AutoComplete(mEditWidget);
											autoComplete.SetInfo(infoCopy);
											autoComplete.mAutoCompleteListWidget.mSelectIdx = fixitIdx;
											autoComplete.InsertSelection(0);
											autoComplete.Close();
										}
										~
										{
											delete infoCopy;
										});
									fixitIdx++;
								}
							}

							if (!menuItem.IsParent)
							{
								menuItem.IsParent = true;
								menuItem.SetDisabled(true);
							}
						}

						menu.AddItem();
						menuItem = menu.AddItem("Cut|Ctrl+X");
						menuItem.mOnMenuItemSelected.Add(new (menu) =>
							{
								CutText();
							});
						menuItem.SetDisabled(!hasSelection);

						menuItem = menu.AddItem("Copy|Ctrl+C");
						menuItem.mOnMenuItemSelected.Add(new (menu) =>
							{
								CopyText();
							});
						menuItem.SetDisabled(!hasSelection);

						menuItem = menu.AddItem("Paste|Ctrl+V");
						menuItem.mOnMenuItemSelected.Add(new (menu) =>
							{
								PasteText();
							});

						// Debugger options
						menu.AddItem();
						var debugger = IDEApp.sApp.mDebugger;
						bool isPaused = debugger.IsPaused();
						menuItem = menu.AddItem("Show Disassembly");
						menuItem.SetDisabled(!isPaused);
						menuItem.mOnMenuItemSelected.Add(new (evt) => IDEApp.sApp.ShowDisassemblyAtCursor());

					    var stepIntoSpecificMenu = menu.AddItem("Step into Specific");
						stepIntoSpecificMenu.SetDisabled(!isPaused);
						stepIntoSpecificMenu.IsParent = true;
					    var stepFilterMenu = menu.AddItem("Step Filter");
						stepFilterMenu.SetDisabled(!isPaused);
						stepFilterMenu.IsParent = true;

						if (isPaused)
						{
						    int addr;
						    String file = scope String();
							String stackFrameInfo = scope String();
						    debugger.GetStackFrameInfo(debugger.mActiveCallStackIdx, out addr, file, stackFrameInfo);
						    if (addr != (int)0)
						    {
						        HashSet<String> foundFilters = scope HashSet<String>();

						        String lineCallAddrs = scope String();
								var checkAddr = addr;
								if (debugger.mActiveCallStackIdx > 0)
									checkAddr--; // Bump back to an address in a calling instruction

								if (debugger.mActiveCallStackIdx > 0)
									checkAddr = debugger.GetStackFrameCalleeAddr(debugger.mActiveCallStackIdx);

						        debugger.FindLineCallAddresses(checkAddr, lineCallAddrs);
						        for (var callStr in String.StackSplit!(lineCallAddrs, '\n'))
						        {
						            if (!String.IsNullOrEmpty(callStr))
						            {
						                Menu callMenuItem;

						                var callData = String.StackSplit!(callStr, '\t');
						                String callInstLocStr = callData[0];
										bool isPastAddr = false;
										if (callInstLocStr[0] == '-')
										{
											callInstLocStr.Remove(0);
											isPastAddr = true;
										}
						                int callInstLoc = (int)int64.Parse(callInstLocStr, System.Globalization.NumberStyles.HexNumber);
						                if (callData.Count == 1)
						                {
						                    callMenuItem = stepIntoSpecificMenu.AddItem(StackStringFormat!("Indirect call at 0x{0:X}", callInstLoc));
						                }
						                else
						                {
						                    String name = callData[1];
						                    StepFilter stepFilter = null;

											debugger.mStepFilterList.TryGetValue(name, out stepFilter);

											bool isDefaultFiltered = false;
											if (callData.Count >= 3)
											{
												if (callData[2].Contains('d'))
													isDefaultFiltered = true;
											}

						                    callMenuItem = stepIntoSpecificMenu.AddItem(name);

						                    if (!foundFilters.Contains(name))
						                    {
						                        foundFilters.Add(scope:: String(name));
						                        var filteredItem = stepFilterMenu.AddItem(name);
						                        for (int32 scopeIdx = 0; scopeIdx < 2; scopeIdx++)
						                        {
						                            bool isGlobal = scopeIdx != 0;
						                            var scopeItem = filteredItem.AddItem((scopeIdx == 0) ? "Workspace" : "Global");
						                            if ((stepFilter != null) && (stepFilter.mIsGlobal == isGlobal))
						                            {
														if (stepFilter.mKind == .Filtered)
						                                	scopeItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.StepFilter);
														else
															scopeItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.LinePointer);
						                                filteredItem.mIconImage = scopeItem.mIconImage;
						                                scopeItem.mOnMenuItemSelected.Add(new (evt) =>
						                                    {
						                                        debugger.DeleteStepFilter(stepFilter);
						                                    });
						                            }
						                            else
						                            {
														if (isDefaultFiltered)
														{
															scopeItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.StepFilteredDefault);
															filteredItem.mIconImage = scopeItem.mIconImage;
														}

														String nameCopy = new String(name);
						                                scopeItem.mOnMenuItemSelected.Add(new (evt) =>
						                                    {
						                                        debugger.CreateStepFilter(nameCopy, isGlobal, isDefaultFiltered ? .NotFiltered : .Filtered);
						                                    }
						                                    ~ delete nameCopy
						                                    );
						                            }
						                        }
						                    }
						                }
										if (isPastAddr)
											callMenuItem.mDisabled = true ;

						                callMenuItem.mOnMenuItemSelected.Add(new (evt) =>
						                    {
						                        IDEApp.sApp.StepIntoSpecific(callInstLoc);                                            
						                    });
						            }
						        }
							}
					    }

					    stepIntoSpecificMenu.mDisabled |= stepIntoSpecificMenu.mItems.Count == 0;
					    stepFilterMenu.mDisabled |= stepFilterMenu.mItems.Count == 0;
					}
					else // (!mSourceViewPanel.mIsSourceCode)
					{
						var menuItem = menu.AddItem("Word Wrap");
						if (mWordWrap)
							menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
						menuItem.mOnMenuItemSelected.Add(new (evt) =>
	                        {
								mWordWrap = !mWordWrap;
								ContentChanged();
	                        });
					}

                    

                    /*menu.AddItem("Item 2");
                    menu.AddItem();
                    menu.AddItem("Item 3");*/
                }

                MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);

                menuWidget.Init(this, useX, useY);
                //menuWidget.mWidgetWindow.mWindowClosedHandler += MenuClosed;                
            }
        }

        public override void Undo()
        {
			var symbolReferenceHelper = IDEApp.sApp.mSymbolReferenceHelper;
            if ((symbolReferenceHelper != null) && (symbolReferenceHelper.mKind == SymbolReferenceHelper.Kind.Rename))
            {
                symbolReferenceHelper.Revert();
                return;
            }

            base.Undo();            
        }

        public override void Redo()
        {
            base.Redo();
        }        

		public bool IsSymbolChar(char8 c)
		{
		    return (c.IsLetterOrDigit) || (c == '_');
		}

		public override void GetInsertFlags(int index, ref uint8 typeId, ref uint8 flags)
		{
			if ((index > 0) && (IsSymbolChar((char8)mData.mText[index - 1].mChar)))
			{
			    typeId = mData.mText[index - 1].mDisplayTypeId; // Copy attr from prev attr
			    flags = (uint8)(mData.mText[index - 1].mDisplayFlags & mExtendDisplayFlags) | mInsertDisplayFlags;
			}
			else if (index < mData.mTextLength - 1)
			{
				if (IsSymbolChar((char8)mData.mText[index].mChar))
				{
				    typeId = mData.mText[index].mDisplayTypeId; // Copy attr from prev attr
				    flags = (uint8)(mData.mText[index].mDisplayFlags & mExtendDisplayFlags) | mInsertDisplayFlags;
				}
				else
				{
					let displayTypeId = (SourceElementType)mData.mText[index].mDisplayTypeId;
					if ((displayTypeId == .Literal) || (displayTypeId == .Comment))
					{
						typeId = (uint8)displayTypeId;
					}
				}
			}
		}

        public override void InsertText(int index, String text)
        {
            if (IDEApp.sApp.mSymbolReferenceHelper != null)
                IDEApp.sApp.mSymbolReferenceHelper.SourcePreInsertText(this, index, text);

            for (var persistentTextPosition in PersistentTextPositions)
            {
                if (index <= persistentTextPosition.mIndex)
                    persistentTextPosition.mIndex += (int32)text.Length;
            }

            base.InsertText(index, text);

            //if (mAsyncAutocomplete)
            {
                // Try to copy "skipped" flags
                int prevIdx = index;
                bool prevWasSkipped = false;
                while (prevIdx > 0)
                {
                    prevIdx--;
                    if (!((char8)mData.mText[prevIdx].mChar).IsWhiteSpace)
                    {
                        prevWasSkipped = ((mData.mText[prevIdx].mDisplayFlags & (uint8)SourceElementFlags.Skipped) != 0);
                        break;
                    }
                }
                if (prevWasSkipped)
                {
                    for (int32 ofs = 0; ofs < text.Length; ofs++)
                        mData.mText[index + ofs].mDisplayFlags |= (uint8)SourceElementFlags.Skipped;
                }
            }

            if ((text.Length > 1) && (mSourceViewPanel != null))
            {
                // We need to do a full refresh after a paste
                mSourceViewPanel.QueueFullRefresh(false);
            }

			
            if ((mSourceViewPanel != null) && (IDEApp.sApp.mSymbolReferenceHelper != null))
                IDEApp.sApp.mSymbolReferenceHelper.SourceUpdateText(this, index);
        }

        public override void RemoveText(int index, int length)
        {
            if (IDEApp.sApp.mSymbolReferenceHelper != null)
                IDEApp.sApp.mSymbolReferenceHelper.SourcePreRemoveText(this, index, length);

            for (var persistentTextPosition in PersistentTextPositions)
            {                
                if (persistentTextPosition.mIndex >= index)
                {                    
                    if (persistentTextPosition.mIndex <= index + length)
                    {                        
                        int32 lineRight = persistentTextPosition.mIndex;
                        while ((lineRight < mData.mText.Count - 1) && (mData.mText[lineRight + 1].mChar != '\n'))
                            lineRight++;

                        if (lineRight < index + length)
                        {
                            // Even end of line is deleted
                            mHadPersistentTextPositionDeletes = true;
                            persistentTextPosition.mWasDeleted = true;
                            persistentTextPosition.mIndex = (int32)index;
                            continue;
                        }
                        else
                        {                            
                            persistentTextPosition.mIndex = (int32)index;
                            continue;
                        }                    
                    }                                        

                    persistentTextPosition.mIndex -= (int32)length;
                }
            }

            base.RemoveText(index, length);

            if ((mSourceViewPanel != null) && (IDEApp.sApp.mSymbolReferenceHelper != null))
                IDEApp.sApp.mSymbolReferenceHelper.SourceUpdateText(this, index);
        }

        public override void PhysCursorMoved()
        {			
			//Debug.WriteLine("Cursor moved {0} {1}", CursorLineAndColumn.mLine, CursorLineAndColumn.mColumn);

            base.PhysCursorMoved();
            mCursorStillTicks = 0;

			if ((mSourceViewPanel != null) && (mSourceViewPanel.mHoverWatch != null))
				mSourceViewPanel.mHoverWatch.Close();
        }

        public override void Update()
        {
            base.Update();

            if (mAutoComplete != null)
			{
                mAutoComplete.Update();
			}

			if ((mAutoComplete != null) && (mAutoComplete.mAutoCompleteListWidget != null))
			{
				if ((mAutoComplete.mAutoCompleteListWidget.mDocumentationDelay == 0) &&
					((mSourceViewPanel == null) || (!mSourceViewPanel.ResolveCompiler.mThreadWorkerHi.mThreadRunning)))
				{
					mAutoComplete.mIsDocumentationPass = true;
					mAutoComplete.mAutoCompleteListWidget.mDocumentationDelay = -1;
					//Debug.WriteLine("Rehup for documentation");
					ShowAutoComplete(mAutoComplete.mIsUserRequested);
					if (mAutoComplete != null)
						mAutoComplete.mIsDocumentationPass = false;
				}	
			}

            if (mDbgDoTest)
            {
                if (mUpdateCnt % 8 == 0)
                {
                    if ((mUpdateCnt / 8) % 2 == 0)
                        KeyChar('z');
                    else
                        KeyChar('\b');
                }
            }

			if (mFastCursorState != null)
			{
				float dirX = 0;
				float dirY = 0;
				if (mWidgetWindow.IsKeyDown(KeyCode.Left))
					dirX--;
				if (mWidgetWindow.IsKeyDown(KeyCode.Right))
					dirX++;
				if (mWidgetWindow.IsKeyDown(KeyCode.Up))
					dirY--;
				if (mWidgetWindow.IsKeyDown(KeyCode.Down))
					dirY++;

				mFastCursorState.mX += dirX * 8.0;
				mFastCursorState.mY += dirY * 8.0;
				int line;
				int column;
				GetLineAndColumnAtCoord((float)mFastCursorState.mX, (float)mFastCursorState.mY, out line, out column);
				CursorLineAndColumn = LineAndColumn(line, column);

				if ((dirX == 0) && (dirY == 0))
				{
					float x;
					float y;
					GetTextCoordAtCursor(out x, out y);
					mFastCursorState.mX = x;
					mFastCursorState.mY = y;
				}

				if ((mFastCursorState.mUpdateCnt % 4 == 0) || (dirX != 0) || (dirY != 0))
				{
					mFastCursorState.mDrawSects.Add(default(FastCursorState.DrawSect));
				}
				mFastCursorState.mUpdateCnt++;

				var lastDrawSect = ref mFastCursorState.mDrawSects[mFastCursorState.mDrawSects.Count - 1];
				lastDrawSect.mX = mFastCursorState.mX;
				lastDrawSect.mY = mFastCursorState.mY;

				for (var drawSect in ref mFastCursorState.mDrawSects)
				{
					drawSect.mPct += 0.2f;
					if (drawSect.mPct >= 1.0)
					{
						@drawSect.Remove();
						continue;
					}
				}

				if ((!mWidgetWindow.IsKeyDown(KeyCode.Control)) || ((!mWidgetWindow.IsKeyDown(KeyCode.Alt))))
				{
					DeleteAndNullify!(mFastCursorState);
				}
			}
        }

        public override uint32 GetSelectionColor(uint8 flags)
        {
            bool hasFocus = mEditWidget.mHasFocus;
            if ((mSourceViewPanel != null) && (mSourceViewPanel.mQuickFind != null))
                hasFocus |= mSourceViewPanel.mQuickFind.mFindEditWidget.mHasFocus;

            if ((flags & (uint8)SourceElementFlags.Find_Matches) != 0)
            {
                return 0x50FFE0B0;
            }

            return hasFocus ? mHiliteColor : mUnfocusedHiliteColor;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            using (g.PushTranslate(mTextInsets.mLeft, mTextInsets.mTop))
            {
                for (var queuedUnderline in mQueuedUnderlines)
                {
                    DoDrawSectionFlags(g, queuedUnderline.mX, queuedUnderline.mY, queuedUnderline.mWidth, queuedUnderline.mFlags);
                }
                mQueuedUnderlines.Clear();

                for (var queuedTextEntry in mQueuedText)
                {
                    uint32 textColor = mTextColors[queuedTextEntry.mTypeIdAndFlags & 0xFF];
                    if (((queuedTextEntry.mTypeIdAndFlags >> 8) & (uint8)SourceElementFlags.Skipped) != 0)
                        textColor = mTextColors[(int32)SourceElementType.Comment];
                    using (g.PushColor(textColor))
                    {
                        DoDrawText(g, queuedTextEntry.mString, queuedTextEntry.mX, queuedTextEntry.mY);
                    }
					queuedTextEntry.Dispose();
                }
                mQueuedText.Clear();
            }

			if (mFastCursorState != null)
			{
				for (var drawSect in mFastCursorState.mDrawSects)
				{
					using (g.PushTranslate((float)drawSect.mX - 32, (float)drawSect.mY - 24))
					{
						//float showPct = (float)Math.Sin(drawSect.mPct * Math.PI) * 0.35f;
						float showPct = (drawSect.mPct + 0.1f) * 0.3f;

						using (g.PushColor(Color.Get(1.0f - drawSect.mPct)))
							using (g.PushScale(showPct, showPct, 32, 32))
								g.Draw(IDEApp.sApp.mCircleImage);
					}
				}
			}
        }
    }
}
