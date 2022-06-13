using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.utils;
using Beefy.geom;

namespace Beefy.theme.dark
{
    public class DarkEditWidgetContent : EditWidgetContent
    {
		public class Embed
		{
			public enum Kind
			{
				LineStart,
				HideLine,
				LineEnd,
			}

			public Kind mKind;
			public int32 mLine = -1;

			public ~this()
			{

			}

			public virtual void Draw(Graphics g, Rect rect, bool hideLine)
			{

			}

			public virtual void MouseDown(Rect rect, float x, float y, int btn, int btnCount)
			{

			}

			public virtual float GetWidth(bool hideLine)
			{
				return GS!(24);
			}
		}

		public class Data : EditWidgetContent.Data
		{
			
		}

        public Font mFont;                
        public uint32[] mTextColors = sDefaultColors;
        public uint32 mHiliteColor = 0xFF2f5c88;
        public uint32 mUnfocusedHiliteColor = 0x00000000;
        public int32 mRecalcSizeLineNum = -1;
        public float mRecalcSizeCurMaxWidth = 0;
		public bool mHasQueuedRecalcSize;
		public int32 mTopCharId = -1;
		public double mTopCharIdVertPos = -1;
		public bool mWantsCheckScrollPosition;
		public uint32 mViewWhiteSpaceColor;
		public bool mScrollToStartOnLostFocus;
		public bool mHiliteCurrentLine;
		public Dictionary<int32, Embed> mEmbeds = new .() ~ DeleteDictionaryAndValues!(_);
		public Embed mEmbedSelected;
		public Range? mLineRange;

		protected static uint32[] sDefaultColors = new uint32[] ( Color.White ) ~ delete _;

        public this(EditWidgetContent refContent = null) : base(refContent)
        {
            //mTextInsets.Set(-3, 2, 0, 2);
			//mTextInsets.Set(GS!(-3), GS!(2), 0, GS!(2));

			mTextInsets.Set(GS!(0), GS!(2), 0, GS!(2));

            mWidth = GS!(100);
            mHeight = GS!(24);
            mHorzJumpSize = GS!(40);
            mFont = DarkTheme.sDarkTheme?.mSmallFont;
        }

		protected override EditWidgetContent.Data CreateEditData()
		{
			return new Data();
		}

		public virtual void CheckLineCoords()
		{
			if (mLineCoordTextVersionId == mData.mCurTextVersionId)
				return;

			mLineCoordTextVersionId = mData.mCurTextVersionId;

			if (mLineCoords == null)
				mLineCoords = new .();
			if (mLineCoordJumpTable == null)
				mLineCoordJumpTable = new .();

			mLineCoords.Clear();
			mLineCoords.GrowUnitialized(mData.mLineStarts.Count);
			mLineCoordJumpTable.Clear();

			float fontHeight = mFont.GetLineSpacing();
			int prevJumpIdx = -1;
			float jumpCoordSpacing = GetJumpCoordSpacing();

			double curY = 0;
			for (int line < mData.mLineStarts.Count)
			{
				float lineHeight = fontHeight;
				mLineCoords[line] = (float)curY;

				int jumpIdx = (.)(curY / jumpCoordSpacing);
				while (prevJumpIdx < jumpIdx)
				{
					mLineCoordJumpTable.Add(((int32)line, (int32)line + 1));
					prevJumpIdx++;
				}
				mLineCoordJumpTable[jumpIdx].max = (.)line + 1;
				curY += lineHeight;
			}
		}

        public override void GetTextData()
        {
            // Generate text flags if we need to...
            if ((mData.mTextFlags == null) && (mWordWrap))
            {
				scope AutoBeefPerf("DEWC.GetTextData");

                mData.mTextFlags = new uint8[mData.mTextLength + 1];

                int32 lineIdx = 0;
                int32 lineStartIdx = 0;
				String lineCheck = scope String();
                for (int32 i = 0; i < mData.mTextLength; i++)
                {
                    char8 c = (char8)mData.mText[i].mChar;
                    
                    lineCheck.Clear();
                    if (c == '\n')                    
                        ExtractString(lineStartIdx, i - lineStartIdx, lineCheck);
                    else if (i == mData.mTextLength - 1)
                        ExtractString(lineStartIdx, i - lineStartIdx + 1, lineCheck);

					if (lineCheck.Length > 0)
					{
						String lineCheckLeft = scope String();
						lineCheckLeft.Reference(lineCheck);
						while (true)
						{
						    int32 maxChars = GetTabbedCharCountToLength(lineCheckLeft, mEditWidget.mScrollContentContainer.mWidth - mTextInsets.mLeft - mTextInsets.mRight);
						    if (maxChars == 0)
						        maxChars = 1;
						    if (maxChars >= lineCheckLeft.Length)
						        break;
						    
						    int32 checkIdx = maxChars;
						    while ((checkIdx > 0) && (!lineCheckLeft[checkIdx].IsWhiteSpace))
						        checkIdx--;
	
						    if (checkIdx == 0)
						        checkIdx = maxChars - 1;
	
						    mData.mTextFlags[lineStartIdx + checkIdx + 1] |= (int32)TextFlags.Wrap;
						    lineStartIdx += checkIdx + 1;
						    
							//lineCheck.Remove(0, checkIdx + 1);
							lineCheckLeft.AdjustPtr(checkIdx + 1);
						}
					}

                    if (c == '\n')
                    {                        
                        lineStartIdx = i + 1;
                        lineIdx++;
                    }
                }
            }

            base.GetTextData();

			CheckLineCoords();
        }

		public int32 mLineCoordTextVersionId = -1;
		public List<float> mLineCoords ~ delete _;
		public List<(int32 min, int32 max)> mLineCoordJumpTable ~ delete _;

		public bool IsLineCollapsed(int line)
		{
			if (mLineCoords == null)
				return false;
			if ((line >= 0) && (line < mLineCoords.Count - 1))
				return (mLineCoords[line + 1] - mLineCoords[line]) < 0.1f;
			return false;
		}

		public float GetLineHeight(int line, float defaultVal)
		{
			if ((line >= 0) && (line < mLineCoords.Count - 1))
				return mLineCoords[line + 1] - mLineCoords[line];
			return defaultVal;
		}

		public float GetLineY(int line, float defaultVal)
		{
			if ((line >= 0) && (line < mLineCoords.Count))
				return mLineCoords[line];
			return defaultVal;
		}

		public int FindUncollapsedLine(int line)
		{
			var line;
			while ((line > 0) && (IsLineCollapsed(line)))
				line--;
			return line;
		}

		public bool IsInCollapseGroup(int anchorLine, int checkLine)
		{
			if (checkLine < anchorLine)
				return false;
			if (checkLine == anchorLine)
				return true;
			if (checkLine == anchorLine + 1)
				return IsLineCollapsed(checkLine);
			return mLineCoords[anchorLine + 1] == mLineCoords[checkLine + 1];
		}

		protected override void AdjustCursorsAfterExternalEdit(int index, int ofs)
 		{
			 base.AdjustCursorsAfterExternalEdit(index, ofs);
			 mWantsCheckScrollPosition = true;
		}

        public float GetTabbedPos(float startX)
        {
            float spaceWidth = mFont.GetWidth((char32)' ');
            if (mTabSize == 0)
                return startX + spaceWidth*4;
            return (float)Math.Truncate((startX + spaceWidth) / mTabSize + 0.999f) * mTabSize;
        }

		static mixin GetTabSection(var origString, var stringLeft, var subStr)
		{
			int32 tabIdx = (int32)stringLeft.IndexOf('\t');
			if (tabIdx == -1)
			    break;

			if (subStr == null)
			{
				subStr = scope:: String(stringLeft, 0, tabIdx);
				stringLeft = scope:: String(origString, tabIdx + 1);
			}
			else
			{
				subStr.Clear();
				subStr.Append(stringLeft, 0, tabIdx);
				stringLeft.Remove(0, tabIdx + 1);
			}

			tabIdx
		}

        public float DoDrawText(Graphics g, String origString, float x, float y)
        {
            String stringLeft = origString;
            float aX = x;
            float aY = y;

			void DrawString(String str, float x, float y)
			{
				if (str.Length == 0)
					return;

				g.DrawString(str, x, y);

				if (mViewWhiteSpaceColor != 0)
				{
					let prevColor = g.mColor;
					g.PopColor();
					g.PushColor(mViewWhiteSpaceColor);

					float curX = x;
					int lastNonSpace = 0;
					for (int i < str.Length)
					{
						char8 c = str[i];
						if (c == ' ')
						{
							// Flush length
							if (lastNonSpace < i)
							{
								var contentStr = scope String();
								contentStr.Reference(str.Ptr + lastNonSpace, i - lastNonSpace);
								curX += mFont.GetWidth(contentStr);
							}

							g.DrawString("·", curX, y);
							curX += mFont.GetWidth(' ');
							lastNonSpace = i + 1;
						}
					}

					g.PopColor();
					g.PushColorOverride(prevColor);
				}
			}

			String subStr = null;
            while (true)
            {
                GetTabSection!(origString, stringLeft, subStr);

                if (g != null)
                    DrawString(subStr, aX, aY);
                
                aX += mFont.GetWidth(subStr);

				if ((mViewWhiteSpaceColor != 0) && (g != null))
				{
					let prevColor = g.mColor;
					g.PopColor();
					g.PushColor(mViewWhiteSpaceColor);
					g.DrawString("→", aX, y);
					g.PopColor();
					g.PushColorOverride(prevColor);
				}

                aX = GetTabbedPos(aX);
            }
            
            if (g != null)
				DrawString(stringLeft, aX, aY);

			//TODO: This is just an "emergency dropout", remove when we optimize more?
			/*if ((mX + x >= 0) && (stringLeft.Length > 1000))
			{
				return aX + 10000;
			}*/

            aX += mFont.GetWidth(stringLeft);
            return aX;
        }        

        /*public int GetTabbedCharCountToLength(String origString, float len)
        {
            String stringLeft = origString;
            float aX = 0;
            int idx = 0;

			String subStr = null;
            while (true)
            {
                int tabIdx = GetTabSection!(origString, stringLeft, subStr);
                
                int char8Count = mFont.GetCharCountToLength(subStr, len - aX);
                if (char8Count < subStr.Length)
                    return idx + char8Count;

                idx += tabIdx + 1;
                aX += mFont.GetWidth(subStr);
                float prevX = aX;

                aX = GetTabbedPos(aX);

                if (len < aX)
                    return idx - 1;
            }

            return idx + mFont.GetCharCountToLength(stringLeft, len - aX);
        }*/

		public int32 GetTabbedCharCountToLength(String origString, float len)
		{
		    float aX = 0;
		    int32 idx = 0;

			String subStr = scope String();
			subStr.Reference(origString);
		    while (true)
		    {
				bool hitTabStop = false;
				int32 char8Count = (int32)mFont.GetCharCountToLength(subStr, len - aX, &hitTabStop);
				if (!hitTabStop)
					return idx + char8Count;

		        aX += mFont.GetWidth(StringView(subStr, 0, char8Count));
		        aX = GetTabbedPos(aX);
				if (aX > len + 0.001f)
					return idx + char8Count;
				idx += char8Count + 1;
				subStr.AdjustPtr(char8Count + 1);
		    }
		}

        public virtual void DrawSectionFlagsOver(Graphics g, float x, float y, float width, uint8 flags)
        {

        }

        public float GetTabbedWidth(String origString, float x, bool forceAccurate = false)
        {
            String stringLeft = origString;
            float aX = x;

			String subStr = null;
            while (true)
            {
#unwarn
                int32 tabIdx = GetTabSection!(origString, stringLeft, subStr);

                aX += mFont.GetWidth(subStr);
                aX = GetTabbedPos(aX);
            }

			//TODO: This is just an "emergency dropout", remove when we optimize more?
			/*if ((!forceAccurate) && (mX + x >= 0) && (stringLeft.Length > 1000))
			{
				return aX + 10000;
			}*/

            return aX + mFont.GetWidth(stringLeft);
        }

        public void SetFont(Font font, bool isMonospace, bool virtualCursor)
        {
            mFont = font;
            if (isMonospace)
            {
                mCharWidth = mFont.GetWidth(' ');
				if (mTabSize == 0)
                	mTabSize = mTabLength * mCharWidth;
				else
					mTabSize = (float)Math.Round(mTabSize / mCharWidth) * mCharWidth;
            }
            else
			{
                mCharWidth = -1;
				mTabSize = mTabLength * mFont.GetWidth('W');
			}
            if (virtualCursor)
                Debug.Assert(isMonospace);
            mAllowVirtualCursor = virtualCursor;
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			Utils.RoundScale(ref mTabSize, newScale / oldScale);
			SetFont(mFont, mCharWidth != -1, mAllowVirtualCursor);
			mContentChanged = true; // Defer calling of RecalcSize
			GetTextData();
			LineStartsChanged();
		}

        public virtual float DrawText(Graphics g, String str, float x, float y, uint16 typeIdAndFlags)
        {
            using (g.PushColor(mTextColors[typeIdAndFlags & 0xFF]))
                return DoDrawText(g, str, x, y);
        }

		public virtual uint32 GetSelectionColor(uint8 flags)
		{
		    return mEditWidget.mHasFocus ? mHiliteColor : mUnfocusedHiliteColor;
		}

		protected Rect GetEmbedRect(int lineIdx, DarkEditWidgetContent.Embed embed)
		{
			GetLinePosition(lineIdx, var lineStart, var lineEnd);

			bool hideLine = false;
			if ((embed.mKind == .HideLine) &&
				((!mEditWidget.mHasFocus) || (!IsInCollapseGroup(lineIdx, CursorLine))))
			   hideLine = true;

			int wsEnd = lineEnd;
			if ((hideLine) || (embed.mKind == .LineStart))
			{
				for (int i in lineStart..<lineEnd)
				{
					var checkEnd = ref mData.mText[i];
					if (!checkEnd.mChar.IsWhiteSpace)
					{
						wsEnd = i;
						break;
					}	
				}
			}

			String str = scope .(256);
			ExtractString(lineStart, wsEnd - lineStart, str);

			float selStartX = GetTabbedWidth(str, 0);

			if ((embed.mKind == .LineEnd) ||
				((embed.mKind == .HideLine) && (!hideLine)))
				selStartX += GS!(4);

			Rect rect = .(selStartX, mLineCoords[lineIdx] - GS!(1), embed.GetWidth(hideLine), mFont.GetLineSpacing() + GS!(3));
			if (rect.mY < 0)
				rect.mY = 0;
			return rect;
		}

        public override void Draw(Graphics g)
        {            
            base.Draw(g);

#unwarn
            int lineCount = GetLineCount();
            float lineSpacing = mFont.GetLineSpacing();

			float offsetY = mTextInsets.mTop;
			if (mHeight < lineSpacing)
				offsetY = (mHeight - lineSpacing) * 0.75f;

            g.PushTranslate(mTextInsets.mLeft, offsetY);

            int selStartLine = -1;
            int selStartCharIdx = -1;
            int selEndLine = -1;
            int selEndCharIdx = -1;

            int selStartIdx = -1;
            int selEndIdx = -1;

            if (mSelection != null)
            {                
                mSelection.Value.GetAsForwardSelect(out selStartIdx, out selEndIdx);
                GetLineCharAtIdx(selStartIdx, out selStartLine, out selStartCharIdx);
                GetLineCharAtIdx(selEndIdx, out selEndLine, out selEndCharIdx);
            }

            int firstLine;
            int firstCharIdx;
            float overflowX;
            GetLineCharAtCoord(0, -mY, out firstLine, out firstCharIdx, out overflowX);

            int lastLine;
            int lastCharIdx;
            float lastOverflowX;
            GetLineCharAtCoord(0, -mY + mEditWidget.mScrollContentContainer.mHeight, out lastLine, out lastCharIdx, out lastOverflowX);

			bool drewCursor = false;

			void DrawCursor(float x, float y)
			{
				if (mHiliteCurrentLine && selStartIdx == selEndIdx)
				{
					float thickness = 2 * (lineSpacing / 18);
					// This isn't quite the right value, but I'm not sure how to get this
					// to properly highlight the whole line without getting cut off - this works well for now.
					float totalLineWidth = mEditWidget.mScrollContentContainer.mWidth - thickness;

					float hiliteX = (int)mEditWidget.mHorzPos.v; // If we don't round to int we get jitter while scrolling.
					using (g.PushColor(DarkTheme.COLOR_CURRENT_LINE_HILITE))
						g.OutlineRect(hiliteX, y, totalLineWidth, lineSpacing + thickness, thickness);
				}

			    float brightness = (float)Math.Cos(Math.Max(0.0f, mCursorBlinkTicks - 20) / 9.0f);                            
			    brightness = Math.Clamp(brightness * 2.0f + 1.6f, 0, 1);
			    if (mEditWidget.mVertPos.IsMoving)
			        brightness = 0; // When we animate a pgup or pgdn, it's weird seeing the cursor scrolling around

				Color cursorColor = mTextColors[0];

			    if (mOverTypeMode)
			    {
			        if (mCharWidth <= 2)
			        {
			            using (g.PushColor(Color.Mult(cursorColor, Color.Get(brightness * 0.75f))))
			                g.FillRect(x, y, GS!(2), lineSpacing);
			        }
			        else
			        {
			            using (g.PushColor(Color.Mult(cursorColor, Color.Get(brightness * 0.30f))))
			                g.FillRect(x, y, mCharWidth, lineSpacing);
			        }
			    }
			    else
			    {
					using (g.PushColor(Color.Mult(cursorColor, Color.Get(brightness))))
			            g.FillRect(x, y, Math.Max(1.0f, GS!(1)), lineSpacing);
			    }
			    drewCursor = true;
			}

			if (mLineRange != null)
			{
				firstLine = Math.Max(firstLine, mLineRange.Value.Start);
				lastLine = Math.Min(lastLine, mLineRange.Value.End - 1);
			}

			String sectionText = scope String(256);
            for (int lineIdx = firstLine; lineIdx <= lastLine; lineIdx++)
            {
                //string lineText = GetLineText(lineIdx);
                int lineStart;
                int lineEnd;
                GetLinePosition(lineIdx, out lineStart, out lineEnd);

                int lineDrawStart = lineStart;
                float curX = 0;
                float curY = mLineCoords[lineIdx];
				float height = mLineCoords[lineIdx + 1] - curY;
				if (height <= 1.0f)
					continue;

				DarkEditWidgetContent.Embed embed = null;
				if (mEmbeds.GetValue((.)lineIdx) case .Ok(out embed))
				{
					if ((embed.mKind == .HideLine) &&
						((!IsInCollapseGroup(lineIdx, CursorLine)) || (!mEditWidget.mHasFocus)))
					{
						var embedRect = GetEmbedRect(lineIdx, embed);
						embed.Draw(g, embedRect, true);
						g.SetFont(mFont);
						continue;
					}
				}

				g.SetFont(mFont);

                while (true)
                {
                    int lineDrawEnd = lineDrawStart;
                    uint16 curTypeIdAndFlags = *(uint16*)&mData.mText[lineDrawStart].mDisplayTypeId;                    

                    // Check for transition of curTypeIdAndFlags - colors ignore whitespace, but if flags are set then we need 
                    //  to be exact
                    /*while ((lineDrawEnd < lineEnd) && ((*(uint16*)&mData.mText[lineDrawEnd].mDisplayTypeId == curTypeIdAndFlags) ||
                        ((curTypeIdAndFlags < 0x100) && (((char8)mData.mText[lineDrawEnd].mChar).IsWhiteSpace))))
                        lineDrawEnd++;*/

					while (true)
					{
						var checkEnd = ref mData.mText[lineDrawEnd];
						if ((lineDrawEnd < lineEnd) && ((*(uint16*)&checkEnd.mDisplayTypeId == curTypeIdAndFlags) ||
							((curTypeIdAndFlags < 0x100) && (checkEnd.mChar.IsWhiteSpace) && (checkEnd.mDisplayFlags == 0))))
							lineDrawEnd++;
						else
							break;
					}

					sectionText.Clear();
                    ExtractString(lineDrawStart, lineDrawEnd - lineDrawStart, sectionText);
					
                    int selStart = Math.Max(0, selStartIdx - lineDrawStart);
                    int selEnd = Math.Min(lineDrawEnd - lineDrawStart, selEndIdx - lineDrawStart);

                    uint8 flags = (uint8)(curTypeIdAndFlags >> 8);
                    if ((lineDrawStart >= selStartIdx) && (lineDrawEnd < selEndIdx) && (lineDrawEnd == lineDrawStart))
                    {
                        // Blank line selected
                        using (g.PushColor(GetSelectionColor(flags)))
                            g.FillRect(curX, curY, 4, lineSpacing);
                    }

                    if (selEnd > selStart)
                    {
						String selPrevString = new:ScopedAlloc! String(selStart);
						selPrevString.Append(sectionText, 0, selStart);
						String selIncludeString = new:ScopedAlloc! String(selEnd);
						selIncludeString.Append(sectionText, 0, selEnd);

                        float selStartX = GetTabbedWidth(selPrevString, curX);
                        float selEndX = GetTabbedWidth(selIncludeString, curX);

                        if (lineIdx != selEndLine)
                            selEndX += mFont.GetWidth((char32)' ');

                        using (g.PushColor(GetSelectionColor(flags)))
                            g.FillRect(selStartX, curY, selEndX - selStartX, lineSpacing);
                    }

                    float nextX = curX;
                    nextX = DrawText(g, sectionText, curX, curY, curTypeIdAndFlags);                                        
                    DrawSectionFlagsOver(g, curX, curY, nextX - curX, flags);

                    //int32 lineDrawStartColumn = lineDrawStart - lineStart;
                    //int32 lineDrawEndColumn = lineDrawEnd - lineStart;
                    if ((mEditWidget.mHasFocus) && (!drewCursor))
                    {
                        float aX = -1;
                        if (mVirtualCursorPos != null)
                        {
                            if ((lineIdx == mVirtualCursorPos.Value.mLine) && (lineDrawEnd == lineEnd))
                            {
                                aX = mVirtualCursorPos.Value.mColumn * mCharWidth;
                            }
                        }
                        else if (mCursorTextPos >= lineDrawStart)
                        {
                            bool isInside = mCursorTextPos < lineDrawEnd;
                            if ((mCursorTextPos == lineDrawEnd) && (lineDrawEnd == lineEnd))
                            {                                
                                if (lineDrawEnd == mData.mTextLength)
                                    isInside = true;
                                if (mWordWrap)
                                {
                                    if ((mShowCursorAtLineEnd) || (lineEnd >= mData.mTextFlags.Count) || (mData.mTextFlags[lineEnd] & (int32)TextFlags.Wrap) == 0)
                                        isInside = true;
                                }
                                else
                                    isInside = true;
                            }

                            if (isInside)
                            {
								String subText = new:ScopedAlloc! String(mCursorTextPos - lineDrawStart);
								subText.Append(sectionText, 0, mCursorTextPos - lineDrawStart);
                                aX = GetTabbedWidth(subText, curX);
                            }
                        }                        

                        if (aX != -1)
							DrawCursor(aX, curY);
                    }

                    lineDrawStart = lineDrawEnd;
                    curX = nextX;

                    if (lineDrawStart >= lineEnd)
                        break;
                }

				if (embed != null)
				{
					var embedRect = GetEmbedRect(lineIdx, embed);
					embed.Draw(g, embedRect, false);
					g.SetFont(mFont);

					if ((!drewCursor) && (IsLineCollapsed(CursorLineAndColumn.mLine)) &&
						(GetLineY(CursorLineAndColumn.mLine, -1) == GetLineY(lineIdx + 1, -1)))
						DrawCursor(embedRect.Right + GS!(2), curY);
				}
            }

            g.PopMatrix();

			/*using (g.PushColor(0x4000FF00))
				g.FillRect(-8, -8, mWidth + 16, mHeight + 16);*/

			/*if (mDbgX != -1)
				g.FillRect(mDbgX - 1, mDbgY - 1, 3, 3);*/
        }

        public override void AddWidget(Widget widget)
        {
            base.AddWidget(widget);
        }

        public override bool AllowChar(char32 theChar)
        {
			if ((int)theChar < 32)
            	return (theChar == '\n') || (mIsMultiline && (theChar == '\t'));
            return mFont.HasChar(theChar);
        }

        public override void InsertAtCursor(String theString, InsertFlags insertFlags)
        {
			scope AutoBeefPerf("DarkEditWidgetContent.InsertAtCursor");

            base.InsertAtCursor(theString, insertFlags);            
        }

        public override void GetTextCoordAtLineChar(int line, int lineChar, out float x, out float y)
        {
			GetTextData();
			
            String lineText = scope String(256);
            GetLineText(line, lineText);
            if (lineChar > lineText.Length)
                x = GetTabbedWidth(lineText, 0) + (mFont.GetWidth((char32)' ') * (lineChar - (int32)lineText.Length)) + mTextInsets.mLeft;
            else
			{
				String subText = scope String(Math.Min(lineChar, 256));
				subText.Append(lineText, 0, lineChar);
                x = GetTabbedWidth(subText, 0, true) + mTextInsets.mLeft;
			}
            y = mTextInsets.mTop + mLineCoords[line];
        }

        public override void GetTextCoordAtLineAndColumn(int line, int column, out float x, out float y)
        {
			GetTextData();
            Debug.Assert((mCharWidth != -1) || (column == 0));
            String lineText = scope String(256);
            GetLineText(line, lineText);
            x = mTextInsets.mLeft + column * mCharWidth;
            y = mTextInsets.mTop + mLineCoords[line];
        }

		protected int GetLineAt(float y)
		{
			if (y < 0)
				return 0;

			GetTextData();

			int lineCount = GetLineCount();
			var checkY = y - mTextInsets.mTop + 0.001f;
			var jumpEntry = mLineCoordJumpTable[Math.Clamp((int)(y / GetJumpCoordSpacing()), 0, mLineCoordJumpTable.Count - 1)];

			int line = jumpEntry.min - 1;
			for (int checkLine in jumpEntry.min ..< jumpEntry.max)
			{
				if (checkY >= mLineCoords[checkLine])
					line = checkLine;
			}

			if (line < 0)
			    line = 0;
			if (line >= lineCount)
			    line = lineCount - 1;
			return line;
		}

		public override void PhysCursorMoved(CursorMoveKind moveKind)
		{
			base.PhysCursorMoved(moveKind);

			if (mLineRange != null)
			{
				if (mLineRange.Value.Length == 0)
					CursorLineAndColumn = .(0, 0);
				else
				{
					var lineAndColumn = CursorLineAndColumn;
					if (lineAndColumn.mLine < mLineRange.Value.Start)
						CursorLineAndColumn = .(mLineRange.Value.Start, lineAndColumn.mColumn);
					else if (lineAndColumn.mLine >= mLineRange.Value.End)
						CursorLineAndColumn = .(mLineRange.Value.End - 1, lineAndColumn.mColumn);
				}
			}
		}

        public override bool GetLineCharAtCoord(float x, float y, out int line, out int lineChar, out float overflowX)
        {
			line = GetLineAt(y);
			return GetLineCharAtCoord(line, x, out lineChar, out overflowX);
        }

        public override bool GetLineAndColumnAtCoord(float x, float y, out int line, out int column)
        {
            line = GetLineAt(y);
            column = Math.Max(0, (int32)((x - mTextInsets.mLeft + 1) / mCharWidth + 0.6f));
            return mCharWidth != -1;
        }

		public override bool GetLineCharAtCoord(int line, float x, out int lineChar, out float overflowX)
		{
			String lineText = scope String(256);
			GetLineText(line, lineText);
			int32 char8Count = GetTabbedCharCountToLength(lineText, x - mTextInsets.mLeft);
			lineChar = char8Count;

			if (char8Count < lineText.Length)
			{
				String subString = new:ScopedAlloc! String(char8Count);
				subString.Append(lineText, 0, char8Count);
			    float subWidth = GetTabbedWidth(subString, 0);

				var utf8enumerator = lineText.DecodedChars(char8Count);
				if (utf8enumerator.MoveNext())
				{
					char32 c = utf8enumerator.Current;
			        float checkCharWidth = 0;
			        if (c == '\t')
			            checkCharWidth = mTabSize * 0.5f;
			        else
					{
						checkCharWidth = mFont.GetWidth(c) * 0.5f;
					}

			        if (x >= subWidth + mTextInsets.mLeft + checkCharWidth)
			            lineChar = (int32)utf8enumerator.NextIndex;
				}
			}
			else
			{
			    overflowX = (x - mTextInsets.mLeft) - (GetTabbedWidth(lineText, 0) + 0.001f);
			    return overflowX <= 0;                
			}

			overflowX = 0;
			return true;
		}

        void RecalcSize(int32 startLineNum, int32 endLineNum, bool forceAccurate = false)
        {
			scope AutoBeefPerf("DEWC.RecalcSize");

			String line = scope String();
            for (int32 lineIdx = startLineNum; lineIdx < endLineNum; lineIdx++)
            {
				line.Clear();
                GetLineText(lineIdx, line);
                mRecalcSizeCurMaxWidth = Math.Max(mRecalcSizeCurMaxWidth, GetTabbedWidth(line, 0, forceAccurate) + mHorzJumpSize);
                Debug.Assert(!mRecalcSizeCurMaxWidth.IsNaN);
            }
        }

		public override void CursorToLineEnd()
		{
			//int32 line;
			//int32 lineChar;
			//GetCursorLineChar(out line, out lineChar);
			/*RecalcSize(line, line + 1, true);
			if (mRecalcSizeCurMaxWidth > mWidth)
			{
				mRecalcSizeLineNum = -1;

			}*/
			mRecalcSizeLineNum = -1;
			RecalcSize(true);
			base.CursorToLineEnd();
		}

		public override void GetTextCoordAtCursor(out float x, out float y)
		{
			int32 line = CursorLine;
			if (IsLineCollapsed(line))
			{
				line = (.)FindUncollapsedLine(line);
				GetTextCoordAtLineAndColumn(line, CursorLineAndColumn.mColumn, out x, out y);
				return;
			}

			base.GetTextCoordAtCursor(out x, out y);
		}

		public override void MoveCursorToCoord(float x, float y)
		{
			base.MoveCursorToCoord(x, y);

			if ((!mEmbeds.IsEmpty) && (mEmbeds.GetValue(CursorLineAndColumn.mLine) case .Ok(let embed)))
			{
				var embedRect = GetEmbedRect(CursorLineAndColumn.mLine, embed);
				if (x > embedRect.Right)
				{
					int endLine = CursorLineAndColumn.mLine;
					while (true)
					{
						if (!IsLineCollapsed(endLine + 1))
							break;
						endLine++;
					}

					if (endLine != CursorLineAndColumn.mLine)
					{
						GetLinePosition(endLine, var endLineStart, var endLineEnd);
						GetLineAndColumnAtLineChar(endLine, endLineEnd - endLineStart, var column);
						CursorLineAndColumn = .(endLine, column);
					}
				}
			}
		}

		public override void CursorToLineStart(bool allowToScreenStart)
		{
			while (IsLineCollapsed(CursorLineAndColumn.mLine))
				CursorLineAndColumn = .(CursorLineAndColumn.mLine - 1, 1);
			base.CursorToLineStart(allowToScreenStart);
		}

		public void RecalcSize(bool forceAccurate = false)
		{
			mMaximalScrollAddedHeight = 0;
			if (mRecalcSizeLineNum == -1)
			{
				mRecalcSizeCurMaxWidth = 0;
				mHasQueuedRecalcSize = false;
			}
			else // We need to recalc again after our current pass
				mHasQueuedRecalcSize = true; 

			if (!mIsReadOnly)
			{
			    float cursorX;
			    float cursorY;
			    GetTextCoordAtCursor(out cursorX, out cursorY);
			    mRecalcSizeCurMaxWidth = Math.Max(mRecalcSizeCurMaxWidth, cursorX + mHorzJumpSize);
			}

			if (mUpdateCnt == 0)
			{                
			    RecalcSize(0, GetLineCount());
			    mWidth = mRecalcSizeCurMaxWidth + mTextInsets.mLeft + mTextInsets.mRight;
			    Debug.Assert(!mWidth.IsNaN);
			}
			else if (mRecalcSizeLineNum == -1)
			{
			   	mRecalcSizeLineNum = 0;

			    // The actual recalculation will take 16 ticks so just make sure we have enough width for 
			    //  the current line for now
			    var lineAndCol = CursorLineAndColumn;
			    RecalcSize(lineAndCol.mLine, lineAndCol.mLine + 1, forceAccurate);
			    mWidth = Math.Max(mWidth, mRecalcSizeCurMaxWidth + mTextInsets.mLeft + mTextInsets.mRight);
			    Debug.Assert(!mWidth.IsNaN);
			}

			GetTextData();
			mHeight = mLineCoords.Back + mTextInsets.mTop + mTextInsets.mBottom;
			Debug.Assert(mHeight > 0);
			UpdateMaximalScroll();
			base.RecalcSize();
		}

        public override void RecalcSize()
        {
            RecalcSize(false);
        }

		public override void ContentChanged()
		{
			base.ContentChanged();
			mRecalcSizeLineNum = -1;

			mLineCoordTextVersionId = -1;
			DeleteAndNullify!(mLineCoords);
			DeleteAndNullify!(mLineCoordJumpTable);
		}

		public float GetJumpCoordSpacing()
		{
			return mFont.GetLineSpacing() * 4;
		}

		public override void LineStartsChanged()
		{
			base.LineStartsChanged();
			mLineCoordTextVersionId = -1;
		}

		public override void TextAppended(String str)
		{
			if ((mData.mLineStarts != null) && (mIsReadOnly))
			{
				int32 recalcSizeLineNum = Math.Max((int32)mData.mLineStarts.Count - 2, 0);
				if ((mRecalcSizeLineNum == -1) || (recalcSizeLineNum < mRecalcSizeLineNum))
					mRecalcSizeLineNum = recalcSizeLineNum;
			}
			base.TextAppended(str);
		}

        protected void UpdateMaximalScroll()
        {            
            if (mAllowMaximalScroll)
            {
				let prevHeight = mHeight;

                mHeight -= mMaximalScrollAddedHeight;
                mMaximalScrollAddedHeight = mEditWidget.mScrollContentContainer.mHeight - mFont.GetLineSpacing();
                mHeight += mMaximalScrollAddedHeight;

				Debug.Assert(mHeight >= 0);

				if (mHeight != prevHeight)
					mEditWidget.UpdateScrollbars();
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            UpdateMaximalScroll();
        }

        public override float GetLineHeight(int line)
        {
			if (mLineCoords == null)
				GetTextData();
			return mLineCoords[line + 1] - mLineCoords[line];
        }

        public override float GetPageScrollTextHeight()
        {
            float numLinesVisible = mEditWidget.mScrollContentContainer.mHeight / mFont.GetLineSpacing();
            if (numLinesVisible - (int32)numLinesVisible < 0.90f)
                numLinesVisible = (int32) numLinesVisible;

            float val = numLinesVisible * mFont.GetLineSpacing();
            if (val <= 0)
                return base.GetPageScrollTextHeight();
            return val;
        }

		public void CheckRecordScrollTop(bool force = false)
		{
			if (mWantsCheckScrollPosition)
			{
				if (mTopCharId != -1)
				{
					int textIdx = mData.mTextIdData.GetPrepared().GetIndexFromId(mTopCharId);
					if (textIdx != -1)
					{
						int line;
						int lineChar;
						GetLineCharAtIdx(textIdx, out line, out lineChar);

						var vertPos = mEditWidget.mVertPos.mDest;
						var offset = vertPos % mFont.GetLineSpacing();
						mEditWidget.mVertScrollbar.ScrollTo(line * mFont.GetLineSpacing() + offset);
					}
					else
					{
						mTopCharId = -1;
					}
				}
				mWantsCheckScrollPosition = false;
			}

			if ((mEditWidget.mHasFocus) && (!force))
			{
				mTopCharId = -1;
			}
			else
			{
				var vertPos = mEditWidget.mVertPos.mDest;
				if ((mTopCharId == -1) || (mTopCharIdVertPos != vertPos))
				{
					float lineNum = (float)(vertPos / mFont.GetLineSpacing());
					int lineStart;
					int lineEnd;
					GetLinePosition((int32)lineNum, out lineStart, out lineEnd);
					int idAtStart = mData.mTextIdData.GetIdAtIndex((int32)lineStart);
					if (idAtStart == -1)
						idAtStart = 0;
					mTopCharId = (int32)idAtStart;
					mTopCharIdVertPos = vertPos;
				}
			}
		}

        public override void Update()
        {
            base.Update();

            if ((mRecalcSizeLineNum != -1) && (BFApp.sApp.mIsUpdateBatchStart))
            {
                int32 lineCount = GetLineCount();
                int32 toLine = Math.Min(lineCount, mRecalcSizeLineNum + Math.Max(1, lineCount / 16) + 80);
                RecalcSize(mRecalcSizeLineNum, toLine);
                if (toLine == lineCount)
                {
                    mRecalcSizeLineNum = -1;
                    mWidth = mRecalcSizeCurMaxWidth + mTextInsets.mLeft + mTextInsets.mRight;
                    base.RecalcSize();
                }
                else
                    mRecalcSizeLineNum = toLine;
            }

			if ((mRecalcSizeLineNum == -1) && (mHasQueuedRecalcSize))
				RecalcSize();

			CheckRecordScrollTop();
        }

		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);

			int line = GetLineAt(y);
			
			bool isOverEmbed = false;

			if (mEmbeds.GetValue((.)line) case .Ok(let embed))
			{
				Rect embedRect = GetEmbedRect(line, embed);
				if (embedRect.Contains(x, y))
					isOverEmbed = true;
			}

			if (isOverEmbed)
				BFApp.sApp.SetCursor(Cursor.Pointer);
			else
				BFApp.sApp.SetCursor(Cursor.Text);
		}
    }

    public class DarkEditWidget : EditWidget
    {
        public bool mDrawBox = true;     

        public this(DarkEditWidgetContent content = null)
        {
            mEditWidgetContent = content;
            if (mEditWidgetContent == null)
                mEditWidgetContent = new DarkEditWidgetContent();
            mEditWidgetContent.mEditWidget = this;
            mScrollContent = mEditWidgetContent;
            mScrollContentContainer.AddWidget(mEditWidgetContent);

            SetupInsets();
            //mScrollbarInsets.Set(18, 1, 0, 0);

            mHorzPos.mSpeed = 0.2f;
            mVertPos.mSpeed = 0.2f;
            mScrollbarBaseContentSizeOffset = GS!(3);
        }

		protected virtual void SetupInsets()
		{
			mScrollContentInsets.Set(GS!(3), GS!(3), GS!(3), GS!(3));
		}

		protected override void HandleWindowMouseDown(Beefy.events.MouseEvent event)
		{
			base.HandleWindowMouseDown(event);

			// If we got closed as part of this click, don't propagate the click through
			if (mParent == null)
			{
				event.mHandled = true;
			}
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetupInsets();
		}

        public override void DefaultDesignInit()
        {
            base.DefaultDesignInit();
            mWidth = GS!(80);
            mHeight = GS!(20);
            SetText("Edit Text");
        }

        public override void InitScrollbars(bool wantHorz, bool wantVert)
        {
            SetupInsets();

            base.InitScrollbars(wantHorz, wantVert);

            float scrollIncrement = ((DarkEditWidgetContent) mEditWidgetContent).mFont.GetLineSpacing();
            if (mHorzScrollbar != null)
                mHorzScrollbar.mScrollIncrement = scrollIncrement;
            if (mVertScrollbar != null)
                mVertScrollbar.mScrollIncrement = scrollIncrement;
        }

		public override void UpdateScrollbarData()
		{
			base.UpdateScrollbarData();

			var ewc = mEditWidgetContent as DarkEditWidgetContent;
			if (ewc.mLineRange != null)
			{
				ewc.GetTextData();
				mVertScrollbar.mContentStart = ewc.mLineCoords[Math.Min(ewc.mLineRange.Value.Start, ewc.mLineCoords.Count - 1)];
				mVertScrollbar.mContentSize = ewc.mLineCoords[Math.Min(ewc.mLineRange.Value.End, ewc.mLineCoords.Count - 1)] - mVertScrollbar.mContentStart;
				ScrollPositionChanged();
				mVertScrollbar.UpdateData();
			}
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            if (mDrawBox)
            {
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditBox), 0, 0, mWidth, mHeight);
                if (mHasFocus)
                {
                    using (g.PushColor(DarkTheme.COLOR_SELECTED_OUTLINE))
                        g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, mWidth, mHeight);
                }
            }

			/*using (g.PushColor(0x40FF0000))
				g.FillRect(0, 0, mWidth, mHeight);*/
        }

		public override void LostFocus()
		{
			base.LostFocus();
			var darkEditWidgetContent = (DarkEditWidgetContent)mEditWidgetContent;
			darkEditWidgetContent.CheckRecordScrollTop();
			if (darkEditWidgetContent.mScrollToStartOnLostFocus)
				HorzScrollTo(0);
		}
    }
}
