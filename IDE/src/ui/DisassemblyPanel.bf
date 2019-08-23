using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.utils;
using Beefy.geom;
using IDE.Compiler;
using IDE.Debugger;
using System.IO;
using IDE.util;

namespace IDE.ui
{
    public class DisassemblyEditContent : SourceEditWidgetContent
    {
        public DisassemblyPanel mDisassemblyPanel;

		public float mJmpIconY;
		public int mJmpState = -1;

        public override void MouseClicked(float x, float y, int32 btn)
        {
            base.MouseClicked(x, y, btn);

            if (btn == 1)
            {
                //var sourceViewPanel = (SourceViewPanel)mParent.mParent;                

                Menu menu = new Menu();
                if (!String.IsNullOrEmpty(mDisassemblyPanel.mSourceFileName))
                {
                    var menuItem = menu.AddItem("Go to Source");
                    menuItem.mOnMenuItemSelected.Add(new (evt) => mDisassemblyPanel.GoToSource());
                }

                /*menu.AddItem("Item 2");
                menu.AddItem();
                menu.AddItem("Item 3");*/

                MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);

                if (menu.mItems.Count > 0)
                    menuWidget.Init(this, x, y);
				else
					delete menuWidget;
                //menuWidget.mWidgetWindow.mWindowClosedHandler += MenuClosed;                
            }

			/*if (btn == 0)
			{
				let fileName = scope String();
				int addr = mDisassemblyPanel.GetCursorAddress(fileName);
				Debug.WriteLine("{0} : {1:X}", fileName, addr);
			}*/
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            float lineSpacing = mFont.GetLineSpacing();
            int32 cursorLine = CursorLineAndColumn.mLine;

            for (int32 pass = 0; pass < 2; pass++)
            {
                for (var jumpEntry in mDisassemblyPanel.mJumpTable)
                {
					var minLineResult = mDisassemblyPanel.mAddrLines.GetValue(jumpEntry.mAddrMin);
					var maxLineResult = mDisassemblyPanel.mAddrLines.GetValue(jumpEntry.mAddrMax);

                    if ((minLineResult case .Ok) && (maxLineResult case .Ok))
                    {
						int32 minLine = minLineResult;
						int32 maxLine = maxLineResult;
                        
                        bool isHilighted = (cursorLine == minLine) || (cursorLine == maxLine);
                        
                        if (isHilighted != (pass == 1))
                            continue;
                        
                        uint32 color = jumpEntry.mIsReverse ? 
                            (isHilighted ? 0xFFffeafe : 0xFF8a7489) :
                            (isHilighted ? 0xFFeaf5ff : 0xFF72828f);

                        float minY = 0 + minLine * lineSpacing + 6;
                        float maxY = 0 + maxLine * lineSpacing + 6;
                        float width = 21 + jumpEntry.mDepth * 7;

                        using (g.PushColor(color))
                        {
							//g.FillRect(50 - width, minY - 4, width, maxY - minY + 7);

							//g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.AsmArrow), 50 - width, minY - 4, width, maxY - minY + 7);

                            if (!jumpEntry.mIsReverse)
                                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.AsmArrow), 50 - width, minY - 4, width, maxY - minY + 7);
                            else
                                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.AsmArrowRev), 50 - width, minY - 4, width, maxY - minY + 7);
                        }

                        for (var overlapDepth in jumpEntry.mOverlapsAtMin)
                        {
                            g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.AsmArrowShadow), 26 - overlapDepth * 7, minY - 10);
                        }

                        for (var overlapDepth in jumpEntry.mOverlapsAtMax)
                        {
                            g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.AsmArrowShadow), 26 - overlapDepth * 7, maxY - 10);
                        }
                    }
                }
            }

			if (mJmpState != -1)
			{
				var img = (mJmpState == 0) ? DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.NoJmp) : DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.YesJmp);
				g.Draw(img, 34, mJmpIconY);
			}
        }
    }

    public class DisassemblyEdit : SourceEditWidget
    {
        public this() : base(null, new DisassemblyEditContent())
        {

        }
    }

    public class DisassemblyPanel : TextPanel
    {
        public struct LineData
        {            
            public int mAddr;
            public int mAddrEnd;
            public String mSourceFile;
            public int32 mSourceLineNum;
        }

        public class JumpEntry
        {
            public int mAddrMin;
            public int mAddrMax;
            public bool mIsReverse;
            public int32 mDepth;

            public List<int32> mOverlapsAtMin = new List<int32>() ~ delete _;
            public List<int32> mOverlapsAtMax = new List<int32>() ~ delete _;
        }

        public static String sPanelName = "Disassembly";

        public DisassemblyEdit mEditWidget;
        PanelHeader mPanelHeader;
        //string[] mSourceLines;
        List<int32> mLineStarts ~ delete _;
        EditWidgetContent.CharData[] mCharData ~ delete _;
        String mDiasmSourceFileName ~ delete _;
        List<LineData> mLineDatas = new List<LineData>() ~ delete _;
        public List<JumpEntry> mJumpTable = new List<JumpEntry>() ~ DeleteContainerAndItems!(_);
        public Dictionary<int, int32> mAddrLines = new Dictionary<int, int32>() ~ delete _;
        public bool mIsInitialized;
        Dictionary<int, Breakpoint> mBreakpointAddrs = new .() ~ delete _;
        public bool mLocationDirty;
        private int mDeferredAddr;
        public DarkCheckBox mStayInDisassemblyCheckbox;
        public int32 mSourceStackIdx;
        int32 mSourceLine;
        int32 mSourceColumn;
        int32 mHotIdx;
        int32 mDefLineStart;
        int32 mDefLineEnd;
		bool mIsRawDiassembly;
        public String mSourceFileName ~ delete _;
		public String mAliasFilePath ~ delete _;
		public SourceHash mSourceHash;

        public this()
        {
            mEditWidget = new DisassemblyEdit();
            var content = (DisassemblyEditContent)mEditWidget.Content;
            content.mDisassemblyPanel = this;
            content.mIsMultiline = true;
            content.mIsReadOnly = true;
            content.mWordWrap = false;
            content.mTextInsets.mLeft = 48;
            content.SetFont(IDEApp.sApp.mCodeFont, true, true);

            content.mTextColors = SourceEditWidgetContent.sTextColors;

            mEditWidget.InitScrollbars(true, true);
            AddWidget(mEditWidget);

            Disable();
        }

		public ~this()
		{
			RemovePanelHeader();
			if (mStayInDisassemblyCheckbox != null)
				delete mStayInDisassemblyCheckbox;
		}

        public override SourceEditWidget EditWidget
        {
            get
            {
                return mEditWidget;
            }
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "DisassemblyPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

        public void FocusEdit()
        {            
            mEditWidget.SetFocus();
        }

		public void ClearQueuedData()
		{
			mDeferredAddr = 0;
		}

        void SetFile(String fileName, out bool isOld)
        {
            isOld = false;
            String useFileName = scope String(fileName);
			if (mAliasFilePath != null)
			{
                if (Path.Equals(useFileName, mAliasFilePath))
					useFileName.Set(mSourceFileName);
			}

            IDEUtils.FixFilePath(useFileName);
            String.NewOrSet!(mDiasmSourceFileName, useFileName);
            var app = IDEApp.sApp;
			delete mLineStarts;
			delete mCharData;
            mLineStarts = null;
            mCharData = null;

            String text = null;
            ProjectSource projectSource = app.FindProjectSourceItem(useFileName);
            if (projectSource != null)
            {
                IdSpan liveCharIdData;
                String liveText = scope:: String();
                app.FindProjectSourceContent(projectSource, out liveCharIdData, true, liveText);
				defer(stack) liveCharIdData.Dispose();
                
                var compileInstance = IDEApp.sApp.mWorkspace.GetProjectSourceCompileInstance(projectSource, mHotIdx);
                if (compileInstance == null)
                {
                    text = liveText;
                }
                else
                {
                    text = compileInstance.mSource;

                    int32 char8IdStart;
                    int32 char8IdEnd;
                    IdSpan char8IdData = IDEApp.sApp.mWorkspace.GetProjectSourceSelectionCharIds(projectSource, mHotIdx, mDefLineStart, mDefLineEnd, out char8IdStart, out char8IdEnd);
                    if (!char8IdData.IsEmpty)
                    {
                        if (!char8IdData.IsRangeEqual(liveCharIdData, (int32)char8IdStart, (int32)char8IdEnd))
                        {
                            isOld = true;
                            //var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
                            //sourceEditWidgetContent.SetOldVersionColors(true);
                        }
                    }
                }

                //var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
                //sourceEditWidgetContent.SetOldVersionColors(mHotIdx != -1);

                //app.mWorkspace.                
            }
            else
			{
                text = scope:: String();
                if (gApp.LoadTextFile(useFileName, text) case .Err)
					text = null;
			}

            if (text != null)
            {
                mCharData = new EditWidgetContent.CharData[text.Length];
                for (int32 i = 0; i < mCharData.Count; i++)
                {
                    mCharData[i].mChar = (char8)text[i];
                }
            }

            bool isBeefSource = IDEApp.IsBeefFile(useFileName);

            var bfSystem = app.mBfResolveSystem;
			if (bfSystem != null)
			{
	            //var compiler = app.mBfResolveCompiler;

	            BfProject bfProject = null;
	            if (projectSource != null)
	                bfProject = bfSystem.GetBfProject(projectSource.mProject);

	            if (text != null)
	            {
	                var parser = bfSystem.CreateEmptyParser(bfProject);
	                var resolvePassData = parser.CreateResolvePassData();
	                var passInstance = bfSystem.CreatePassInstance();
	            
					parser.SetIsClassifying();
	                parser.SetSource(text, useFileName);	                
	                parser.Parse(passInstance, !isBeefSource);
	                if (isBeefSource)
	                    parser.Reduce(passInstance);
	                parser.ClassifySource(mCharData, !isBeefSource);
	                delete resolvePassData;
					delete parser;
	                delete passInstance;
	            
	                mLineStarts = new List<int32>();
	                mLineStarts.Add(0);
	            
	                for (int32 i = 0; i < text.Length; i++)
	                    if (mCharData[i].mChar == '\n')
	                        mLineStarts.Add(i + 1);
	                mLineStarts.Add((int32)text.Length + 1);
	            }            
			}
        }

        /*string GetSourceLine(int lineNum)
        {
            if (lineNum >= mSourceLines.Length)
                return null;
            return mSourceLines[lineNum];
        }*/

		//////

        public void GoToSource()
        {            
            if (String.IsNullOrEmpty(mSourceFileName))
            {
                IDEApp.sApp.Fail("Unable to locate source");
                return;
            }
            var sourceViewPanel = gApp.ShowSourceFileLocation(mSourceFileName, -1, IDEApp.sApp.mWorkspace.GetHighestCompileIdx(), mSourceLine, mSourceColumn, LocatorType.None);
			if (sourceViewPanel.mLoadedHash case .None)
				sourceViewPanel.mLoadedHash = mSourceHash; // Set for when we do Auto Find
        }

        public bool SelectLine(int addr, bool jumpToPos)
        {
            var editContent = mEditWidget.Content;
            if (editContent.mData.mLineStarts == null)
                return false;
            //FocusEdit();
            
            for (int32 lineIdx = 0; lineIdx < mLineDatas.Count; lineIdx++)
            {
                var lineData = mLineDatas[lineIdx];

                if ((lineData.mAddr != (int)0) && (addr >= lineData.mAddr) && (addr < lineData.mAddrEnd))
                {
                    if (lineIdx >= editContent.mData.mLineStarts.Count)
                    {
                        // Wtf?
                        editContent.CursorTextPos = editContent.mData.mTextLength;
                    }
                    else
                        editContent.CursorTextPos = editContent.mData.mLineStarts[lineIdx];                    

                    editContent.EnsureCursorVisible(true, true);
                    if (jumpToPos) // Jump to whatever position we're scrolling to
                    {
                        mEditWidget.mVertPos.mPct = 1.0f;
                        mEditWidget.UpdateContentPosition();
                    }

                    return true;
                }
            }

            return false;
        }

        public int GetCursorAddress(String outSourceFileName)
        {
            int line;
            int lineChar;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out line, out lineChar);
            line = Math.Min(line, (int32)mLineDatas.Count - 1);

            for (int checkLine = line; checkLine >= 0; checkLine--)
            {
                var lineData = mLineDatas[checkLine];
                if (lineData.mSourceFile != null)
                {
                    outSourceFileName.Append(lineData.mSourceFile);
                    break;
                }
            }

            for (int checkLine = line; checkLine < mLineDatas.Count; checkLine++)
            {
                var lineData = mLineDatas[checkLine];
                if (lineData.mAddr != (int)0)
                    return lineData.mAddr;
            }
            for (int checkLine = line - 1; checkLine >= 0; checkLine--)
            {
                var lineData = mLineDatas[checkLine];
                if (lineData.mAddr != (int)0)
                    return lineData.mAddr;
            }

            return (int)0;
        }

		/*public void Show(int addr, String sourceFile = null, int32 sourceLine = -1, int32 sourceColumn = -1, int32 hotIdx = -1, int32 defLineStart = -1, int32 defLineEnd = -1)
		{
		    String codeData = scope String();
		    var lines = String.StackSplit!(codeData, '\n');
		}*/

		void RemovePanelHeader()
		{
			if (mStayInDisassemblyCheckbox != null)
				mStayInDisassemblyCheckbox.RemoveSelf();
			if (mPanelHeader != null)
			{
			    mPanelHeader.RemoveSelf();
				delete mPanelHeader;
			}
		}

		public void Show(int addr, String sourceFile = null, int sourceLine = -1, int sourceColumn = -1, int hotIdx = -1, int defLineStart = -1, int defLineEnd = -1)
		{
			if (sourceFile == null)
		    	DeleteAndNullify!(mSourceFileName);
			else
				String.NewOrSet!(mSourceFileName, sourceFile);
		    mSourceLine = (int32)sourceLine;
		    mSourceColumn = (int32)sourceColumn;
		    mHotIdx = (int32)hotIdx;
		    mDefLineStart = (int32)defLineStart;
		    mDefLineEnd = (int32)defLineEnd;
			mSourceHash = .();
		    
		    if (mParent == null)
		    {
		        mDeferredAddr = addr;
		        return;
		    }
		    mDeferredAddr = 0;

		    //bool wasChecked = false;
		    RemovePanelHeader();
		    mPanelHeader = new PanelHeader();
		    mPanelHeader.Label = "Showing disassembly.";

		    var font = DarkTheme.sDarkTheme.mSmallFont;
			if (mStayInDisassemblyCheckbox == null)
				mStayInDisassemblyCheckbox = new DarkCheckBox();
		    mStayInDisassemblyCheckbox.Label = "Stay in dis&assembly mode.";
		    mStayInDisassemblyCheckbox.Resize(font.GetWidth(mPanelHeader.mLabel) + 30, 6, mStayInDisassemblyCheckbox.CalcWidth(), 20);
		    //if (mStayInDisassemblyCheckbox != null)
		        //checkbox.mChecked = mStayInDisassemblyCheckbox.mChecked;
		    mPanelHeader.AddWidget(mStayInDisassemblyCheckbox);
		    //mStayInDisassemblyCheckbox = checkbox;

		    if (!String.IsNullOrEmpty(mSourceFileName))
		    {
		        var button = mPanelHeader.AddButton("Show Sour&ce");
		        button.mOnMouseClick.Add(new (evt) => GoToSource());
		    }
		    AddWidget(mPanelHeader);
		    mIsInitialized = true;
		    ResizeComponents();

			if ((mIsRawDiassembly) && (String.IsNullOrEmpty(sourceFile)))
			{
			    if (SelectLine(addr, false))
			        return;
			}

		    var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidget.Content;
		    sourceEditWidgetContent.SetOldVersionColors(false);

		    Clear();

			mIsRawDiassembly = false;
		    mEditWidget.SetText("");
		    mLineDatas.Clear();

		    String codeData = scope String();
		    IDEApp.sApp.mDebugger.DisassembleAt(addr, codeData);
		    int prevDisasmLineData = -1;
		    int lineIdx = 0;
			bool allowEmptyLine = false;
			bool allowOldColoring = true;

		    //var lines = String.StackSplit!(codeData, '\n');

			String line = scope String();
			//char8[] seps = scope char8[] { '\n' };
		    //for (var lineStrView in codeData.Split(seps))

			int checkIdx = 0;
			bool hadSourceLines = false;
			bool isOptimized = false;

			String sourceLineText = scope .(1024);

			for (var lineStrView in codeData.Split('\n'))
		    {
				checkIdx++;

				if (checkIdx == 0x82)
				{
					NOP!();
				}

				line.Reference(lineStrView);
		        if (line.Length == 0)
		            break;

		        int32 prevLen = mEditWidget.Content.mData.mTextLength;
		        int32 typeId = -1;

		        LineData lineData = .();
		        bool addLineData = true;
				bool nextAllowEmptyLine = false;

		        switch (line[0])
		        {
				case 'O':
					isOptimized = true;
					addLineData = false;
				case 'R':
					mIsRawDiassembly = true;
					addLineData = false;
	            case 'S':
	                {
	                    typeId = (int32)SourceElementType.Disassembly_FileName;
	                    String fileName = scope String(line, 2);
	                    bool isOld;
	                    SetFile(fileName, out isOld);
	                    String text = scope String("--- ", fileName);
	                    if (isOld)
						{
	                        text.Append(" (old version)");
							if (allowOldColoring)
							{
								sourceEditWidgetContent.SetOldVersionColors(true);
							}
						}
						else
						{
							allowOldColoring = false;
						}
						text.Append("\n");
	                    mEditWidget.Content.InsertText(prevLen, text);
	                    lineIdx++;
	                }
	                break;
	            case 'T':
	                {
						typeId = (int32)SourceElementType.Disassembly_FileName;
	                    String lineText = scope String(line, 2);
						lineText.Append("\n");
	                    mEditWidget.Content.InsertText(prevLen, lineText);
	                    lineIdx++;
	                }
	                break;
	            case 'D':
	                {
	                    typeId = (int32)SourceElementType.Disassembly_Text;
	                    String disasmLine = scope String(line, 2);
						disasmLine.Append("\n");
	                    mEditWidget.Content.InsertText(prevLen, disasmLine);

	                    int32 parenPos = (int32)disasmLine.IndexOf(':');
	                    //int ptrSize = 8;
	                    var addrString = scope String(disasmLine, 0, parenPos);
	                    addrString.Replace("'", "");
	                    lineData.mAddr = (int)int64.Parse(addrString, System.Globalization.NumberStyles.HexNumber);
	                    lineData.mAddrEnd = lineData.mAddr + 1;

	                    if (prevDisasmLineData != -1)
	                    {
	                        var prevLineData = mLineDatas[prevDisasmLineData];
	                        prevLineData.mAddrEnd = lineData.mAddr;
	                        mLineDatas[prevDisasmLineData] = prevLineData;
	                    }
	                    prevDisasmLineData = mLineDatas.Count;

	                    mAddrLines[lineData.mAddr] = (int32)lineIdx;

	                    lineIdx++;
	                }
	                break;
	            case 'L':
	                {
	                    typeId = -1;
						var itr = line.Split(' ');
						itr.MoveNext();
						int32 sourceLineStart = int32.Parse(itr.GetNext());
						int32 sourceLineCount = int32.Parse(itr.GetNext());

	                    if ((mLineStarts == null) || (sourceLineStart < 0) || (sourceLineStart + sourceLineCount >= mLineStarts.Count - 1))
						{
							//mEditWidget.Content.InsertText(prevLen, "- Error -\n");
	                        continue;
						}

						void ExtractLine(int sourceLineNum, String destString, out int32 startIdx, out int32 endIdx)
						{
							destString.Clear();
							startIdx = mLineStarts[sourceLineNum];
							endIdx = mLineStarts[sourceLineNum + 1] - 1;
							char8[] chars = scope char8[endIdx - startIdx];
							for (int32 idx = startIdx; idx < endIdx; idx++)
							    chars[idx - startIdx] = (char8)mCharData[idx].mChar;
							destString.Append(chars, 0, chars.Count);
						}
						

						if ((!hadSourceLines) && (!isOptimized))
						{
							// Try to trim off the bottom of the preceding method
							for (int32 checkLine = sourceLineStart + sourceLineCount - 2; checkLine >= sourceLineStart; checkLine--)
							{
								ExtractLine(checkLine, sourceLineText, var startIdx, var endIdx);

								if ((sourceLineText.Contains("}")) || (sourceLineText.Contains(";")))
								{
									int32 offset = checkLine - sourceLineStart + 1;
									sourceLineStart += offset;
									sourceLineCount -= offset;
									break;
								}
							}
						}

						for (int32 sourceLineNum = sourceLineStart; sourceLineNum < sourceLineStart + sourceLineCount; sourceLineNum++)
						{
							//int32 startIdx;
							//int32 endIdx;
							ExtractLine(sourceLineNum, sourceLineText, var startIdx, var endIdx);
							if ((!allowEmptyLine) && (sourceLineText.IsWhiteSpace))
							{
								continue;
							}

							allowEmptyLine = true;
							sourceLineText.Append("\n");
							prevLen = mEditWidget.Content.mData.mTextLength;
		                    mEditWidget.Content.InsertText(prevLen, sourceLineText);
		                    for (int32 i = 0; i < endIdx - startIdx; i++)
		                        mEditWidget.Content.mData.mText[i + prevLen].mDisplayTypeId = mCharData[i + startIdx].mDisplayTypeId;

							lineData = .();
		                    lineData.mSourceFile = mDiasmSourceFileName;
		                    lineData.mSourceLineNum = sourceLineNum;
		                    lineIdx++;

							mLineDatas.Add(lineData);
						}

						// Just about the only case we allow an empty line is when it occurs after a non-empty source line
						nextAllowEmptyLine = true;
						addLineData = false;
						hadSourceLines = true;
	                }
	                break;
	            case 'J':
	                {
	                    addLineData = false;
	                    JumpEntry jumpEntry = new JumpEntry();
	                    int64 addrFrom = (int64)mLineDatas[mLineDatas.Count - 1].mAddr;
	                    int64 addrTo = int64.Parse(scope String(line, 2), System.Globalization.NumberStyles.HexNumber);
	                    jumpEntry.mAddrMin = (int)Math.Min(addrFrom, addrTo);
	                    jumpEntry.mAddrMax = (int)Math.Max(addrFrom, addrTo);
	                    jumpEntry.mIsReverse = jumpEntry.mAddrMin == (int)addrTo;
	                    jumpEntry.mDepth = -1;

	                    if (jumpEntry.mAddrMin != jumpEntry.mAddrMax)
	                        mJumpTable.Add(jumpEntry);
						else
							delete jumpEntry;
	                }
	                break;
		        }
				
				allowEmptyLine = nextAllowEmptyLine;

		        if (addLineData)
		        {
		            mLineDatas.Add(lineData);

		            if (typeId != -1)
		            {
		                for (int32 i = prevLen; i <= mEditWidget.Content.mData.mTextLength; i++)
		                    mEditWidget.Content.mData.mText[i].mDisplayTypeId = (uint8)typeId;
		            }
		        }
		    }            

		    mJumpTable.Sort(scope (a, b) =>
		        {
		            int32 diff = (int32)((a.mAddrMax - a.mAddrMin) - (b.mAddrMax - b.mAddrMin));
		            if (diff != 0)
		                return diff;
		            return (int32)(a.mAddrMin - b.mAddrMin);
		        });

		    List<int32> arrowDepths = scope List<int32>();
		    for (int32 jumpIdx = 0; jumpIdx < mJumpTable.Count; jumpIdx++)
		    {
		        var jumpEntry = mJumpTable[jumpIdx];
		        
		        arrowDepths.Clear();                
		        for (int32 checkJumpIdx = 0; checkJumpIdx < mJumpTable.Count; checkJumpIdx++)
		        {
		            var checkJumpEntry = mJumpTable[checkJumpIdx];
		            if ((checkJumpEntry.mDepth != -1) &&
		                (((checkJumpEntry.mAddrMin > jumpEntry.mAddrMin) && (checkJumpEntry.mAddrMin < jumpEntry.mAddrMax)) ||
		                 ((checkJumpEntry.mAddrMax > jumpEntry.mAddrMin) && (checkJumpEntry.mAddrMax < jumpEntry.mAddrMax))))
		                arrowDepths.Add(checkJumpEntry.mDepth);
		        }

		        for (int32 checkDepth = 0; true; checkDepth++)
		        {
		            if (!arrowDepths.Contains(checkDepth))
		            {
		                jumpEntry.mDepth = checkDepth;
		                break;
		            }
		        }

		        for (int32 checkJumpIdx = 0; checkJumpIdx < mJumpTable.Count; checkJumpIdx++)
		        {
		            var checkJumpEntry = mJumpTable[checkJumpIdx];
		            if ((checkJumpEntry.mDepth == -1) || (checkJumpEntry.mDepth >= jumpEntry.mDepth))
		                continue;
		            if ((jumpEntry.mAddrMin > checkJumpEntry.mAddrMin) && (jumpEntry.mAddrMin < checkJumpEntry.mAddrMax))                    
		                jumpEntry.mOverlapsAtMin.Add(checkJumpEntry.mDepth);
		            if ((jumpEntry.mAddrMax > checkJumpEntry.mAddrMin) && (jumpEntry.mAddrMax < checkJumpEntry.mAddrMax))                    
		                jumpEntry.mOverlapsAtMax.Add(checkJumpEntry.mDepth);
		        }
		    }

		    mEditWidget.Content.ClampCursor();
		    mEditWidget.Content.ContentChanged();
		    mEditWidget.Content.RecalcSize();

		    //Debug.Assert(mLineDatas.Count == mEditWidget.Content.mLineStarts.Length);

		    SelectLine(addr, true);
		    // Go there immediately
		    mEditWidget.mVertPos.mPct = 1.0f;
		    mEditWidget.UpdateContentPosition();

		    if (mEditWidget.Content.mData.mTextLength == 0)
		    {
		        mEditWidget.SetText("Disassembly unavailable");
		    }

		    //mEditWidget.Text = codeData;
		}

        public override void Draw(Beefy.gfx.Graphics g)
        {
            base.Draw(g);

            DarkEditWidgetContent darkEditWidgetContent = (DarkEditWidgetContent)mEditWidget.Content;

            g.SetFont(IDEApp.sApp.mTinyCodeFont);

            int curCallStackAddr = 0;
			int32 jmpState = -1;
            if (IDEApp.sApp.mExecutionPaused)
            {
                String fileName = null;                
                IDEApp.sApp.mDebugger.GetStackFrameInfo(IDEApp.sApp.mDebugger.mActiveCallStackIdx, out curCallStackAddr, fileName, null);
				
				jmpState = IDEApp.sApp.mDebugger.GetJmpState(IDEApp.sApp.mDebugger.mActiveCallStackIdx);
            }

            mBreakpointAddrs.Clear();
            for (var breakpoint in IDEApp.sApp.mDebugger.mBreakpointList)
            {
                void* curBreakpointItr = null;
                while (true)
                {
                    int addr = breakpoint.GetAddress(ref curBreakpointItr);
                    if (addr != (int)0)
                        mBreakpointAddrs.TryAdd(addr, breakpoint);
                    if (curBreakpointItr == null)
                        break;
                }
            }

			String callstackFileName = scope String(Path.MaxPath);
			int callstackLineNum = -1;			

			Image linePointerImage = DarkTheme.sDarkTheme.GetImage(.LinePointer);
			if (IDEApp.sApp.mExecutionPaused)
			{
			    int addr;
			    
			    int hotIdx;
			    int defLineStart;
			    int defLineEnd;
			    //int lineNum;
			    int column;
			    int language;
			    int stackSize;
			    DebugManager.FrameFlags frameFlags;
			    IDEApp.sApp.mDebugger.GetStackFrameInfo(IDEApp.sApp.mDebugger.mActiveCallStackIdx, null, out addr, callstackFileName, out hotIdx, out defLineStart, out defLineEnd, out callstackLineNum, out column, out language, out stackSize, out frameFlags);
				int hashPos = callstackFileName.IndexOf('#');
				if (hashPos != -1)
					callstackFileName.RemoveToEnd(hashPos);
				if (frameFlags.HasFlag(.Optimized))
					linePointerImage = DarkTheme.sDarkTheme.GetImage(.LinePointer_Opt);
			    //IDEUtils.FixFilePath(fileName);

				if (IDEApp.sApp.mDebugger.mActiveCallStackIdx == 0)
					callstackLineNum = -1;
			}

			//jmpState = 0;
			var disasmEditContent = (DisassemblyEditContent)mEditWidget.Content;
			disasmEditContent.mJmpState = -1;

            using (g.PushClip(0, 0, mWidth, mHeight))
            {
                using (g.PushTranslate(0, mEditWidget.mY + mEditWidget.Content.Y))
                {
                    float lineSpacing = darkEditWidgetContent.mFont.GetLineSpacing();

                    int lineStart = Math.Max(0, (int32)((-mEditWidget.Content.Y) / lineSpacing) - 1);
                    int lineEnd = Math.Min(mLineDatas.Count, lineStart + (int32)(mHeight / lineSpacing) + 3);

                    using (g.PushColor(0x80FFFFFF))
                    {                                                
                        for (int lineIdx = lineStart; lineIdx < lineEnd; lineIdx++)
                        {
                            var lineData = mLineDatas[lineIdx];
                            if (lineData.mSourceFile != null)
                                g.DrawString(StackStringFormat!("{0}", lineData.mSourceLineNum + 1), 8, 2 + lineIdx * lineSpacing, FontAlign.Right, 18);
                        }
                    }

                    for (int lineIdx = lineStart; lineIdx < lineEnd; lineIdx++)
                    {
                        var lineData = mLineDatas[lineIdx];

                        if ((lineData.mAddr != (int)0) && (mBreakpointAddrs.GetValue(lineData.mAddr) case .Ok(let breakpoint)))
                        {
                            //g.Draw(DarkTheme.sDarkTheme.GetImage(.RedDot), mEditWidget.mX - 20,
                                //0 + lineIdx * lineSpacing);
							using (g.PushTranslate(mEditWidget.mX - 20, 0 + lineIdx * lineSpacing))
								breakpoint.Draw(g, false);
                        }

                        if ((lineData.mAddr != (int)0) && (curCallStackAddr >= lineData.mAddr) && (curCallStackAddr < lineData.mAddrEnd))
                        {
                            Image img = (IDEApp.sApp.mDebugger.mActiveCallStackIdx == 0) ? linePointerImage : DarkTheme.sDarkTheme.GetImage(.ReturnPointer);
                            g.Draw(img, mEditWidget.mX - 20,
                                0 + lineIdx * lineSpacing);

							/*if (jmpState != -1)
							{
								img = (jmpState == 0) ? DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.NoJmp) : DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.YesJmp);
								g.Draw(img, mEditWidget.mX + 20,
								    0 + lineIdx * lineSpacing);
							}*/
							disasmEditContent.mJmpState = jmpState;
							disasmEditContent.mJmpIconY = -4 + lineIdx * lineSpacing;
                        }                        

						if ((lineData.mSourceFile != null) && (lineData.mSourceLineNum == callstackLineNum) && (Path.Equals(lineData.mSourceFile, callstackFileName)))
						{                            
						    //RemapCompiledToActiveLine(hotIdx, ref lineNum, ref column);
						    Image img = (IDEApp.sApp.mDebugger.mActiveCallStackIdx == 0) ? linePointerImage : DarkTheme.sDarkTheme.GetImage(.LinePointer_Prev);
						    g.Draw(img, mEditWidget.mX - 20,
						        0 + lineIdx * lineSpacing);
						}
                    }                    
                }
            }
        }

        /*public IEnumerable<T> FindTrackedElementsAtCursor<T>(List<T> trackedElementList) where T : TrackedTextElement
        {
            int lineIdx;
            int lineCharIdx;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);

            int idx = 0;            
            while (idx < trackedElementList.Count)
            {
                var trackedElement = trackedElementList[idx];
                if (trackedElement.mLineNum == lineIdx)
                {
                    int prevSize = trackedElementList.Count;
                    yield return trackedElement;
                    if (trackedElementList.Count >= prevSize)
                        idx++;
                }
                else
                    idx++;
            }
        }*/

		// This is useful for a one-shot breakpoint within the current execution context- IE: "Run to cursor"
        public Breakpoint ToggleAddrBreakpointAtCursor(bool forceSet, int threadId = -1)
        {
            DebugManager debugManager = IDEApp.sApp.mDebugger;

            int lineIdx;
            int lineCharIdx;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);

            int checkIdx = lineIdx;            
            LineData lineData = mLineDatas[checkIdx];

            if (lineData.mAddr == (int)0)
                return null;

            bool hadBreakpoint = false;
			if (!forceSet)
			{
	            for (int32 breakIdx = 0; breakIdx < IDEApp.sApp.mDebugger.mBreakpointList.Count; breakIdx++)
	            {
	                var breakpoint = IDEApp.sApp.mDebugger.mBreakpointList[breakIdx];
	                if (breakpoint.ContainsAddress(lineData.mAddr))
	                {
	                    hadBreakpoint = true;
						BfLog.LogDbg("DisassemblyPanel.ToggleAddrBreakpointAtCursor\n");
	                    debugManager.DeleteBreakpoint(breakpoint);
	                    breakIdx--;
	                }
	            }
			}

            if (!hadBreakpoint)
            {
                var editWidgetContent = mEditWidget.Content;
                int textPos = mEditWidget.Content.CursorTextPos - lineCharIdx;
                lineCharIdx = 0;

                // Find first non-space char8
                while ((textPos < editWidgetContent.mData.mTextLength) && (((char8)editWidgetContent.mData.mText[textPos].mChar).IsWhiteSpace))
                {
                    textPos++;
                    lineCharIdx++;
                }

                let breakpoint = debugManager.CreateBreakpoint(lineData.mAddr);
				breakpoint.SetThreadId(threadId);
				return breakpoint;
            }
			return null;
        }

        public Breakpoint ToggleBreakpointAtCursor(bool forceSet = false, int threadId = -1)
        {         
            DebugManager debugManager = IDEApp.sApp.mDebugger;

            int lineIdx;
            int lineCharIdx;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out lineIdx, out lineCharIdx);

            int checkIdx = lineIdx;
            int instrOffsetCount = -1;
            LineData lineData;
			if (checkIdx >= mLineDatas.Count)
				return null;
            while (true)
            {
                if (checkIdx < 0)
                {
                    return ToggleAddrBreakpointAtCursor(forceSet);
                }
                lineData = mLineDatas[checkIdx];
                if (lineData.mSourceFile != null)
				{
					bool foundOrigin = false;

					// In the case of a split line (ie: a method call that contains an inlined method call as a param),
					//  we search for the first instance of this line
					for (int originIdx = 0; originIdx <= lineIdx; originIdx++)
					{
						var originLineData = mLineDatas[originIdx];
						if ((originLineData.mSourceFile != null) && (originLineData.mSourceFile == lineData.mSourceFile) && (originLineData.mSourceLineNum == lineData.mSourceLineNum))
						{
							foundOrigin = true;
						}
						else if ((foundOrigin) && (originLineData.mAddr != 0))
						{
							instrOffsetCount++;
						}
					}						 	

                    break;
				}
                checkIdx--;
            }

			//We DO need to allow "+0", otherwise we can't bind to the PRELUDE of a function
			// Is there any reason to allow explicitly binding to "+0"?
			/*if (instrOffsetCount == 0)
				instrOffsetCount = -1;*/

            bool hadBreakpoint = false;
			if (!forceSet)
			{
	            for (int32 breakIdx = 0; breakIdx < IDEApp.sApp.mDebugger.mBreakpointList.Count; breakIdx++)
	            {
	                var breakpoint = IDEApp.sApp.mDebugger.mBreakpointList[breakIdx];
	                if ((breakpoint.mFileName != null) && (Path.Equals(breakpoint.mFileName, lineData.mSourceFile)) && (breakpoint.mLineNum == lineData.mSourceLineNum) && 
	                    (Math.Max(0, breakpoint.mInstrOffset) == Math.Max(0, instrOffsetCount)))
	                {
	                    hadBreakpoint = true;
						BfLog.LogDbg("DisassemblyPanel.ToggleBreakpointAtCursor deleting breakpoint\n");
	                    debugManager.DeleteBreakpoint(breakpoint);
	                    breakIdx--;
	                }                
	            }
			}

            if (!hadBreakpoint)
            {
                var editWidgetContent = mEditWidget.Content;
                int textPos = mEditWidget.Content.CursorTextPos - lineCharIdx;
                lineCharIdx = 0;

                // Find first non-space char8
                while ((textPos < editWidgetContent.mData.mTextLength) && (((char8)editWidgetContent.mData.mText[textPos].mChar).IsWhiteSpace))
                {
                    textPos++;
                    lineCharIdx++;
                }
                
                let breakpoint = debugManager.CreateBreakpoint_Create(lineData.mSourceFile, lineData.mSourceLineNum, 0, instrOffsetCount);
				breakpoint.SetThreadId(threadId);
				debugManager.CreateBreakpoint_Finish(breakpoint);
				return breakpoint;
            }
			return null;
        }

        public override void Clear()
        {
            mEditWidget.SetText("");
			mLineDatas.Clear();
            ClearAndDeleteItems(mJumpTable);
            mAddrLines.Clear();
			mBreakpointAddrs.Clear();
        }

        public void Disable()
        {            
            Clear();
            mEditWidget.SetText("Disassembly is only available while debugging");
        }

        public bool Show(String file, int lineNum, int column)
        {            
            String addrs = scope String();
            IDEApp.sApp.mDebugger.FindCodeAddresses(file, lineNum, column, true, addrs);
            if (addrs.Length == 0)
            {
                IDEApp.sApp.Fail("Unable to locate code address");
                return false;
            }
            
            var addrEntries = String.StackSplit!(addrs, '\n');
            for (var addrEntry in addrEntries)
            {
                if (addrEntry.Length == 0)
                    break;
                var addrData = String.StackSplit!(addrEntry, '\t');
                int addr = (int)int64.Parse(addrData[0], System.Globalization.NumberStyles.HexNumber);                
                Show(addr, file, lineNum, column);
                break;   
            }
            return true;
        }

        public override void AddedToParent()
        {
            var app = IDEApp.sApp;

            if (!app.mDebugger.IsPaused())
                mDeferredAddr = (int)0;
            if (mDeferredAddr != (int)0)
                Show(mDeferredAddr, mSourceFileName, mSourceLine, mSourceColumn, mHotIdx, mDefLineStart, mDefLineEnd);
            
            app.AddToRecentDisplayedFilesList(sPanelName);
            //IDEApp.sApp.SetInDisassemblyView(true);
        }

        public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
        {
            base.RemovedFromParent(previousParent, window);
            IDEApp.sApp.SetInDisassemblyView(false);
        }

        protected override void ResizeComponents()
        {
            base.ResizeComponents();
            if (mPanelHeader != null)
                mPanelHeader.Resize(30, 0, Math.Max(mWidth - 30, 0), 32);
            mEditWidget.Resize(30, 32, Math.Max(mWidth - 30, 0), Math.Max(mHeight - 32, 0));
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        public void UpdateMouseover()
        {
            if (!CheckAllowHoverWatch())
                return;

            if (IDEApp.sApp.HasPopupMenus())
                return;

            if ((mHoverWatch != null) && (mHoverWatch.mCloseDelay > 0))
                return;

            var editWidgetContent = mEditWidget.Content;
            Point mousePos;
            bool mouseoverFired = DarkTooltipManager.CheckMouseover(editWidgetContent, 10, out mousePos);
            //CompilerBase compiler = ResolveCompiler;
            if ((mouseoverFired) || (mHoverWatch != null))
            {
                var content = mEditWidget.Content;
                //int cursorPos = content.CursorTextPos;
                int line;
                int lineChar;
                float overflowX;
                content.GetLineCharAtCoord(mousePos.x, mousePos.y, out line, out lineChar, out overflowX);
                int cursorPos = content.GetTextIdx(line, lineChar);

                int leftIdx = -1;
                int rightIdx = -1;
                String debugExpr = null;

                if ((cursorPos < content.mData.mTextLength) && (!((char8)content.mData.mText[cursorPos].mChar).IsWhiteSpace))
                {
                    var typeId = content.mData.mText[cursorPos].mDisplayTypeId;
                    if (typeId == (uint8)SourceElementType.Disassembly_Text)
                    {
                        int lineStart;
                        int lineEnd;
                        content.GetLinePosition(line, out lineStart, out lineEnd);

                        String lineStr = scope String();
                        content.ExtractString(lineStart, lineEnd - lineStart, lineStr);
                        int strIdx = 0;

                        int colonPos = -1;
                        int instrStartIdx = -1;
                        int instrEndIdx = -1;
                        int firstParamIdx = -1;
                        int commaIdx = -1;                        

                        for (; strIdx < lineStr.Length; strIdx++)
                        {
                            char8 c = lineStr[strIdx];
                            if (colonPos == -1)
                            {
                                if (c == ':')
                                    colonPos = strIdx;
                            }
                            else if (instrStartIdx == -1)
                            {
                                if (c.IsLower)
                                    instrStartIdx = strIdx;
                            }
                            else if (instrEndIdx == -1)
                            {
                                if (c.IsWhiteSpace)
                                    instrEndIdx = strIdx;
                            }
                            else if (firstParamIdx == -1)
                            {
                                if (!c.IsWhiteSpace)
                                    firstParamIdx = strIdx;
                            }
                            else if (commaIdx == -1)
                            {
                                if (c == ',')
                                    commaIdx = strIdx;
                            }
                        }

                        int cursorStrIdx = cursorPos - lineStart;
                        if (cursorStrIdx > commaIdx)
                        {
                            if (commaIdx != -1)
                                leftIdx = lineStart + commaIdx + 1;
                            else
                                leftIdx = lineStart + firstParamIdx;
                            rightIdx = lineEnd - 1;
                        }
                        else if (cursorStrIdx >= firstParamIdx)
                        {
                            leftIdx = lineStart + firstParamIdx;
                            if (commaIdx != -1)
                                rightIdx = lineStart + commaIdx - 1;
                            else
                                rightIdx = lineEnd - 1;
                        }

                        if (leftIdx != -1)
                        {
                            debugExpr = scope:: String();
                            content.ExtractString(leftIdx, rightIdx - leftIdx + 1, debugExpr);

                            int32 semiPos = (int32)debugExpr.IndexOf(';');
                            if (semiPos != -1)
                                debugExpr.RemoveToEnd(semiPos);

                            debugExpr.Replace('[', '(');
                            debugExpr.Replace(']', ')');
                            debugExpr.Replace("qword ptr", "(int64*)");
                            debugExpr.Replace("dword ptr", "(int32*)");
                            debugExpr.Replace("word ptr", "(int16*)");
                            debugExpr.Replace("byte ptr", "(int8*)");

                            if (line < mLineDatas.Count - 1)
                            {
                                if (mLineDatas[line].mAddrEnd != (int)0)
                                {
                                    String nextAddr = StackStringFormat!("0x{0:x}L", (int64)mLineDatas[line].mAddrEnd);
									nextAddr.Append(" +");
                                    debugExpr.Replace("rip +", nextAddr);
                                }
                            }

                            if (commaIdx != -1)
                            {
                                String firstParam = lineStr;
                                semiPos = (int32)lineStr.IndexOf(';');
                                if (semiPos != -1)
                                    debugExpr = scope:: String(lineStr, 0, semiPos);

                                if (firstParam.Contains("xmm"))
                                {
                                    debugExpr.Replace("(int64*)", "(double*)");
                                    debugExpr.Replace("(int32*)", "(float*)");
                                }
                            }
                        }
                    }
                    else if ((typeId != (uint8)SourceElementType.Comment) && (typeId != (uint8)SourceElementType.Literal))
                    {
                        leftIdx = cursorPos;
                        rightIdx = cursorPos;

                        while (leftIdx > 0)
                        {
                            int checkIdx = leftIdx - 1;
                            char8 c = (char8)content.mData.mText[checkIdx].mChar;
                            if ((!c.IsLetterOrDigit) && (c != '.'))
                                break;
                            leftIdx = checkIdx;
                        }
                        
                        while (rightIdx < content.mData.mTextLength)
                        {
                            int checkIdx = rightIdx + 1;
                            char8 c = (char8)content.mData.mText[checkIdx].mChar;
                            if ((!c.IsLetterOrDigit) && (c != '.'))
                                break;
                            rightIdx = checkIdx;
                        }
                    }                                        
                }

                if (leftIdx != -1)
                {
                    content.GetLineCharAtIdx(leftIdx, out line, out lineChar);
                    float x;
                    float y;
                    editWidgetContent.GetTextCoordAtLineChar(line, lineChar, out x, out y);
                    x = mousePos.x;                    
                    
                    if (debugExpr == null)
					{
	                	debugExpr = scope:: String();
                        content.ExtractString(leftIdx, rightIdx - leftIdx + 1, debugExpr);
					}
                    if ((mHoverWatch == null) || (mHoverWatch.mEvalString != debugExpr))
                    {
                        if (mHoverWatch != null)
                        {
                            mHoverWatch.Close();
                        }
                        else
                        {
                            mHoverWatch = new HoverWatch();
                            if (mHoverWatch.Show(this, x, y, debugExpr))
                            {
                                mHoverWatch.mOpenMousePos = DarkTooltipManager.sLastRelMousePos;
                                mHoverWatch.mEvalString.Set(debugExpr); // Set to old debugStr for comparison
                            }
                            else
                            {
                                mHoverWatch.Close();
                            }
                        }
                    }
                }
                else if (mHoverWatch != null)
                {                    
                    mHoverWatch.Close();
                }
            }
        }

        public override void Update()
        {
            base.Update();
            if (mEditWidget.mHasFocus)
                IDEApp.sApp.SetInDisassemblyView(true);
            UpdateMouseover();
        }
    }
}
