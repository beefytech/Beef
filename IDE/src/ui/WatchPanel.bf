using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.events;
using Beefy.utils;
using IDE.Debugger;

namespace IDE.ui
{
    public enum WatchResultType
    {
        None   		= 0,
        Value  		= 1,
        Int 		= 2,
        MM128 		= 4,
        Object 		= 8,
        Pointer 	= 0x10,
        TypeClass 	= 0x20,
        TypeValueType = 0x40,
        Namespace 	= 0x80,
        Text 		= 0x100
    }

    public class WatchEntry
    {        
        public bool mIsDeleted;
        public bool mIsAppendAlloc;
        public bool mIsStackAlloc;
        public WatchResultType mResultType;
		public DebugManager.Language mLanguage = .NotSet;

        public String mName ~ delete _;
        public String mEvalStr ~ delete _;
        public bool mCanEdit;
        public String mEditInitialize ~ delete _;
        public bool mHadValue;
        public bool mHasValue;
        public bool mIsNewExpression;
		public bool mIsPending;
		public int mMemoryBreakpointAddr;
        public int32 mHadStepCount;
        public String mReferenceId ~ delete _;
        public String mResultTypeStr ~ delete _;
		public String mAction ~ delete _;
		public List<String> mWarnings ~ DeleteContainerAndItems!(_);

		public int32 mSeriesFirstVersion = -1;
		public int32 mSeriesVersion = -1;

		public bool IsConstant
		{
			get
			{
				if (mEvalStr.IsEmpty)
					return true;
				if (mEvalStr[0].IsNumber)
					return true;
				return false;
			}
		}

        public bool ParseCmd(List<StringView> cmd)
        {
            switch (scope String(cmd[0]))
            {
                case ":type":
                    switch (scope String(cmd[1]))
                    {
                        case "object":
                            mResultType |= WatchResultType.Object;
                            return true;
                        case "pointer":
                            mResultType |= WatchResultType.Pointer;
                            return true;
                        case "class":
                            mResultType |= WatchResultType.TypeClass;
                            return true;
                        case "valuetype":
                            mResultType |= WatchResultType.TypeValueType;
                            return true;
                        case "namespace":
                            mResultType |= WatchResultType.Namespace;
                            return true;
                        case "int":
                            mResultType |= WatchResultType.Int;
                            return true;
                        case "mm128":
                            mResultType |= WatchResultType.MM128;
                            return true;
                    }
                    break;
                case ":appendAlloc":
                    mIsAppendAlloc = true;
                    return true;
                case ":stack":
                    mIsStackAlloc = true;
                    return true;                
                case ":deleted":
                    mIsDeleted = true;
                    return true;
            }

            return false;
        }
    }

    public interface IWatchOwner
    {
        WatchListView GetWatchListView();
        void UpdateWatch(WatchListViewItem watchEntry);
        void SetupListViewItem(WatchListViewItem listViewItem, String name, String evalStr);
    }

    public class WatchListView : IDEListView
    {
        public IWatchOwner mWatchOwner;
        public static int32 sCurDynReferenceId;

        public this(IWatchOwner IWatchOwner)
        {
            mWatchOwner = IWatchOwner;
        }

        protected override ListViewItem CreateListViewItem()
        {
            if (mWatchOwner == null) // For root node, mWatchOwner not set yet
                return new IDEListViewItem();

            WatchListViewItem anItem = new WatchListViewItem(mWatchOwner, this);
            return anItem;
        }

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			if (var watchPanel = mParent as WatchPanel)
			{
				watchPanel.SetFocus();
			}
		}
    }

    public class ExpressionEditWidgetContent : SourceEditWidgetContent
    {
        public this()
        {
            SetFont(DarkTheme.sDarkTheme.mSmallFont, false, false);
			mTextInsets.Set(GS!(-1), GS!(2), 0, GS!(2));
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            ExpressionEditWidget watchEditWidget = (ExpressionEditWidget)mEditWidget;

            if (watchEditWidget.mErrorEnd != 0)
            {
                var text = scope String();
                watchEditWidget.GetText(text);
				if (watchEditWidget.mErrorStart < text.Length)
				{
	                float indent = mTextInsets.mLeft;
	                float strStarts = indent + g.mFont.GetWidth(scope String(text, 0, watchEditWidget.mErrorStart));
	                float strEnds = indent + g.mFont.GetWidth(scope String(text, 0, Math.Min(watchEditWidget.mErrorEnd, text.Length)));
	                using (g.PushColor(0xFFFF4040))
	                    IDEApp.sApp.DrawSquiggle(g, strStarts, GS!(2), strEnds - strStarts);
				}
            }
        }

		public override float GetLineHeight(int line)
		{
			return GS!(21);
		}
    }

    public class ExpressionEditWidget : DarkEditWidget
    {
        public int32 mErrorStart;
        public int32 mErrorEnd;
        int32 mLastTextVersionId;
        public bool mIsAddress;
		public bool mIsSymbol;
		public int mEvalAtAddress;
		public DebugManager.Language mLanguage;
		public String mExprPre ~ delete _;
		public String mExprPost ~ delete _;
		public String mLastError ~ delete _;
		public bool mIgnoreErrors;
		public String mExpectingType;
        //public AutoComplete mAutoComplete;

        public this()
            : base(new ExpressionEditWidgetContent())
        {
            var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
            editWidgetContent.mOnGenerateAutocomplete = new (keyChar, options) =>
				{
					if ((editWidgetContent.mAutoComplete?.mIsDocumentationPass).GetValueOrDefault())
						return;
					UpdateText(keyChar, true);
				};
			editWidgetContent.mScrollToStartOnLostFocus = true;
			mScrollContentInsets.Set(GS!(4), GS!(3), GS!(1), GS!(3));

			mOnKeyDown.Add(new => EditKeyDownHandler);
        }

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			if (height <= GS!(24))
				mScrollContentInsets.Set(GS!(2), GS!(3), GS!(1), GS!(3));
			else
				mScrollContentInsets.Set(GS!(4), GS!(3), GS!(1), GS!(3));
		}

		void EditKeyDownHandler(KeyDownEvent evt)
		{
			if (evt.mKeyCode == .Escape)
			{
				var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
				if (editWidgetContent.mAutoComplete != null)
				{
					editWidgetContent.mAutoComplete.Close();
					evt.mHandled = true;
					return;
				}
			}
		}

		public void ResizeAround(float targetX, float targetY, float width)
		{
			Resize(targetX - GS!(1), targetY - GS!(1), width + GS!(2), GS!(23));
		}

        public override void LostFocus()
        {
            base.LostFocus();
            //var sourceEditWidgetContent = (SourceEditWidgetContent)mEditWidgetContent;
            //TODO: This was causing autocomplete to close when you clicked on the scroll thumb.  Why did we need this?
            //if (sourceEditWidgetContent.mAutoComplete != null)
                //sourceEditWidgetContent.mAutoComplete.Close();
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            /*if ((keyCode == KeyCode.Escape) && (mAutoComplete != null))
            {
                mAutoComplete.Close();
                return;
            }*/

            base.KeyDown(keyCode, isRepeat);
        }

		public virtual EditSelection GetCurExprRange()
		{
			return EditSelection(0, Content.mData.mTextLength);
		}

		public virtual void SetAutoCompleteInfo(String autoCompleteInfo, int textOffset)
		{
			var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
			editWidgetContent.mAutoComplete.SetInfo(autoCompleteInfo, true, (.)textOffset);
		}

		public AutoComplete GetAutoComplete()
		{
			var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;
			if (editWidgetContent.mAutoComplete == null)
			{
			    editWidgetContent.mAutoComplete = new AutoComplete(this);
			    editWidgetContent.mAutoComplete.mOnAutoCompleteInserted.Add(new () => UpdateText(0, true));
			    editWidgetContent.mAutoComplete.mOnClosed.Add(new () => { editWidgetContent.mAutoComplete = null; });
			}
			return editWidgetContent.mAutoComplete;
		}

		public bool IsShowingAutoComplete()
		{
			let autoComplete = GetAutoComplete();
			if (autoComplete == null)
				return false;
			return autoComplete.IsShowing();
		}

        public virtual void UpdateText(char32 keyChar, bool doAutoComplete)
        {
			scope AutoBeefPerf("ExpressionEditWidget.UpdateText");

			if ((keyChar == 0) && (doAutoComplete))
				return; // Is documentation pass

			DeleteAndNullify!(mLastError);

			var editWidgetContent = (ExpressionEditWidgetContent)mEditWidgetContent;

			let exprRange = GetCurExprRange();
			if (exprRange.Length == 0)
			{
				if (editWidgetContent.mAutoComplete != null)
					editWidgetContent.mAutoComplete.Close();
				mErrorStart = 0;
				mErrorEnd = 0;
				return;
			}

			String text = scope String();

			int32 cursorOfs = 0;
			if (mExprPre != null)
			{
				text.Append(mExprPre);
				cursorOfs = (.)mExprPre.Length;
			}
            editWidgetContent.ExtractString(exprRange.MinPos, exprRange.Length, text);
			if (mExprPost != null)
				text.Append(mExprPost);

            String val = scope String();
			if (mEvalAtAddress != (int)0)
				IDEApp.sApp.mDebugger.EvaluateAtAddress(text, mEvalAtAddress, val, mEditWidgetContent.CursorTextPos - 1 - exprRange.mStartPos + cursorOfs);
            else if (mIsAddress)
			{
				gApp.mDebugger.Evaluate(text, val, mEditWidgetContent.CursorTextPos - 1 - exprRange.mStartPos + cursorOfs, -1, .MemoryWatch);
			}
			else if (mIsSymbol)
			{
				gApp.mDebugger.Evaluate(text, val, mEditWidgetContent.CursorTextPos - 1 - exprRange.mStartPos + cursorOfs, -1, .Symbol);
			}
            else
                gApp.DebugEvaluate(mExpectingType, text, val, mEditWidgetContent.CursorTextPos - 1 - exprRange.mStartPos + cursorOfs);

            var vals = scope List<StringView>(val.Split('\n'));

            if (doAutoComplete)
            {
                int32 idx = (int32)val.IndexOf(":autocomplete\n");
                if (idx != -1)
                {
                    GetAutoComplete();
					var autoCompleteInfo = scope String(256);
					autoCompleteInfo.Append(val, idx + ":autocomplete\n".Length);
					SetAutoCompleteInfo(autoCompleteInfo, exprRange.mStartPos - cursorOfs);
                }
                else if (editWidgetContent.mAutoComplete != null)
                    editWidgetContent.mAutoComplete.Close();
            }

            mErrorStart = 0;
            mErrorEnd = 0;
            String result = scope String(vals[0]);
            if ((result.StartsWith("!", StringComparison.Ordinal)) && (!mIgnoreErrors))
            {
				result.Remove(0, 1);
				var errorVals = scope List<StringView>(result.Split('\t'));
                if (errorVals.Count > 1)
                {
                    mErrorStart = int32.Parse(scope String(errorVals[0])).Get() + exprRange.mStartPos - cursorOfs;
                    mErrorEnd = mErrorStart + int32.Parse(scope String(errorVals[1])).Get();
					mLastError = new String(errorVals[2]);

					if ((mErrorEnd > 0) && (mErrorStart < text.Length) && (mErrorEnd <= text.Length))
						mLastError.Append(": ", scope String(text, mErrorStart, mErrorEnd - mErrorStart));
                }
                else
                {
					mLastError = new String(errorVals[0]);
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

            if (mLastTextVersionId != mEditWidgetContent.mData.mCurTextVersionId)
            {
				mLastTextVersionId = mEditWidgetContent.mData.mCurTextVersionId;
                UpdateText(0, false);
            }
        }

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
			if (Content.mIsReadOnly)
			{
				using (g.PushColor(0x60404040))
					g.FillRect(0, 0, mWidth, mHeight);
			}	
		}
    }

	/*class RefCounted
	{
		int32 mRefCount = 1;

		public this()
		{			
			//Debug.WriteLine("RefCount (this) {0} {1}", this, mRefCount);
		}

		public ~this()
		{
			//Debug.WriteLine("RefCount (~this) {0} {1}", this, mRefCount);
			Debug.Assert(mRefCount == 0);
		}

		public void AddRef()
		{
			mRefCount++;
			//Debug.WriteLine("RefCount (AddRef) {0} {1}", this, mRefCount);
		}

		public void Release()
		{	
			//Debug.WriteLine("RefCount (Release) {0} {1}", this, mRefCount - 1);
			if (--mRefCount == 0)
				delete this;							
		}
	}*/

    public class WatchSeriesInfo : RefCounted
    {
        public String mDisplayTemplate ~ delete _;
        public String mEvalTemplate ~ delete _;
        public int32 mStartMemberIdx;
        public int mStartIdx;
        public int mCount;
        public int mCurCount; // When counting up unsized series
        public int32 mShowPages;
        public int32 mShowPageSize;
        public String mAddrs ~ delete _;
        public int32 mAddrsEntrySize;
        public String mContinuationData ~ delete _;
        public DarkButton mMoreButton;
        public DarkButton mLessButton;
		public static int32 sIdx;
		public int32 mSeriesVersion = ++sIdx;
		public int32 mSeriesFirstVersion = mSeriesVersion;
    }

    public class WatchRefreshButton : ButtonWidget
    {
        public override void Draw(Graphics g)
        {
            base.Draw(g);            
            g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RefreshArrows));
        }
    }

	public class ActionButton : ButtonWidget
	{
		public override void Draw(Graphics g)
		{
		    base.Draw(g);
			if (mX < mParent.mWidth - GS!(10))
		    	g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.GotoButton));
		}
	}

    public class WatchStringEditWidgetContent : DarkEditWidgetContent
    {
		public WatchStringEdit mWatchStringEdit;

		public this()
		{
			mHiliteColor = 0xFF384858;
			mUnfocusedHiliteColor = 0x80384858;
		}

        public override bool AllowChar(char32 theChar)
        {
            return true;
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            base.KeyDown(keyCode, isRepeat);
            if (keyCode == KeyCode.Escape)
                DarkTooltipManager.CloseTooltip();
        }

		public override void RecalcSize()
		{
			base.RecalcSize();
			if (mWatchStringEdit.mMoreButton != null)
				mHeight += GS!(32);
		}

		public override void CursorToEnd()
		{
			base.CursorToEnd();

			if (mWatchStringEdit.mMoreButton != null)
			{
				mEditWidget.VertScrollTo(mEditWidget.mVertPos.mDest + GS!(32));
				mEditWidget.HorzScrollTo(0);
			}
		}

		public override uint32 GetSelectionColor(uint8 flags)
		{
		    bool hasFocus = mEditWidget.mHasFocus;
		    if ((mWatchStringEdit != null) && (mWatchStringEdit.mQuickFind != null))
		        hasFocus |= mWatchStringEdit.mQuickFind.mFindEditWidget.mHasFocus;

		    if ((flags & (uint8)SourceElementFlags.Find_Matches) != 0)
		    {
		        return 0x50FFE0B0;
		    }

		    return hasFocus ? mHiliteColor : mUnfocusedHiliteColor;
		}

		public override void DrawSectionFlagsOver(Graphics g, float x, float y, float width, uint8 flags)
		{
			if ((flags & (uint8)SourceElementFlags.Find_Matches) != 0)
			{
			    using (g.PushColor(0x34FFE0B0))
			        g.FillRect(x, y, width, mFont.GetLineSpacing());

			    DrawSectionFlagsOver(g, x, y, width, (uint8)(flags & ~(uint8)SourceElementFlags.Find_Matches));
			    return;
			}
		}
    }

	public class WatchStringEditWidget : DarkEditWidget
	{
		public this(WatchStringEditWidgetContent content) : base(content)
		{

		}

		public override void DrawAll(Graphics g)
		{
			using (g.PushColor(0xD0FFFFFF))
				base.DrawAll(g);
		}
	}

    public class WatchStringEdit : Widget
    {
		public const int cWordWrapMax = 512*1024;
		public const int cMoreBlockSize = 1024*1024;

        public WatchStringEditWidget mEditWidget;
		public String mEvalString = new String() ~ delete _;
		public String mOrigContent ~ delete _;
		public int mMaxShowSize = cMoreBlockSize;
		public DarkButton mMoreButton;
		public QuickFind mQuickFind;
		public DarkTabbedView.DarkTabMenuButton mMenuButton;
		public bool mViewWhiteSpace;

        public this(String text, String evalStr)
        {
			scope AutoBeefPerf("WatchStringEdit.this");

			let editWidgetContent = new WatchStringEditWidgetContent();
			editWidgetContent.mWatchStringEdit = this;

            mEditWidget = new WatchStringEditWidget(editWidgetContent);
            mEditWidget.Content.mIsMultiline = true;
            mEditWidget.Content.mIsReadOnly = true;
            mEditWidget.Content.mWordWrap = text.Length < cWordWrapMax;
            mEditWidget.Content.mAllowMaximalScroll = false;

			bool needsStrCleaning = false;

			for (let c in text.RawChars)
				if (c <= '\x02')
					needsStrCleaning = true;
			if (needsStrCleaning)
			{
				mOrigContent = new String(text);
				String cleanedStr = scope String()..Append(text);
				for (let c in cleanedStr.RawChars)
				{
					if (c <= '\x02')
						@c.Current = '\x03';
				}
				mEditWidget.SetText(cleanedStr);
			}
			else
            	mEditWidget.SetText(text);
            
            AddWidget(mEditWidget);

			if (text.Length >= mMaxShowSize)
			{
				ShowMoreButton();
			}

			if (evalStr != null)
				mEvalString.Set(evalStr);

			mMenuButton = new DarkTabbedView.DarkTabMenuButton();
			AddWidget(mMenuButton);
			
			mMenuButton.mOnMouseDown.Add(new (evt) =>
				{
					float x = mMenuButton.mX + GS!(14);
					float y = mMenuButton.mY + GS!(6);

					Menu menu = new Menu();
					var menuItem = menu.AddItem("Show Whitespace");
					if (mViewWhiteSpace)
						menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
					menuItem.mOnMenuItemSelected.Add(new (menu) =>
						{
							mViewWhiteSpace = !mViewWhiteSpace;
							var darkEditWidgetContent = (DarkEditWidgetContent)mEditWidget.Content;
							darkEditWidgetContent.mViewWhiteSpaceColor = mViewWhiteSpace ? SourceEditWidgetContent.sTextColors[(int)SourceElementType.VisibleWhiteSpace] : 0;
						});

					MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
					menuWidget.Init(this, x, y, true);

					menu.mOnMenuClosed.Add(new (menu, itemSelected) =>
						{
							if (DarkTooltipManager.sTooltip != null)
								DarkTooltipManager.sTooltip.mAutoCloseDelay = 90;
						});
					//menuWidget.mWidgetWindow.mOnWindowClosed.Add(new => MenuClosed);
				});
        }

		public void ShowQuickFind(bool isReplace)
		{
		    if (mQuickFind != null)
			{
		        mQuickFind.Close();
				delete mQuickFind;
			}
		    mQuickFind = new QuickFind(this, mEditWidget, isReplace);
		    mWidgetWindow.SetFocus(mQuickFind.mFindEditWidget);
		    RehupSize();
		}

		public void FindNext(int32 dir)
		{
			if (mQuickFind == null)
				return;
			mQuickFind.FindNext(dir, false);
		}

		void ShowMore()
		{
			mMoreButton.RemoveSelf();
			gApp.DeferDelete(mMoreButton);
			mMoreButton = null;

			mMaxShowSize *= 2;

			String str = scope String();
			String evalStr = scope:: String();
			evalStr.AppendF("{0}, rawStr, maxcount={1}", mEvalString, mMaxShowSize);
			// We purposely don't add mResultTypeStr here because the parse fails on std::basic_string and we know this will never be needed for an short enum name anyway
			gApp.DebugEvaluate(null, evalStr, str);

			mEditWidget.SetText(str);

			if (str.Length >= mMaxShowSize)
			{
				ShowMoreButton();
				mEditWidget.Content.RecalcSize();
			}
		}

		void ShowMoreButton()
		{
			let editWidgetContent = (WatchStringEditWidgetContent)mEditWidget.Content;
			var contentHeight = mEditWidget.Content.GetLineCount() * editWidgetContent.mFont.GetLineSpacing();
			mMoreButton = (DarkButton)DarkTheme.sDarkTheme.CreateButton(editWidgetContent, "More", mX + GS!(4), contentHeight + GS!(4), GS!(68), GS!(20));
			mMoreButton.mOnMouseClick.Add(new (evt) =>
				{
					ShowMore();
				});
		}

        public override void DrawAll(Graphics g)
        {
            g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			var editWidgetContent = mEditWidget.Content;
			int cursorTextPos = mEditWidget.Content.CursorTextPos;
            
            int line;
            int lineChar;
            mEditWidget.Content.GetLineCharAtIdx(mEditWidget.Content.CursorTextPos, out line, out lineChar);            
            var textY = mHeight - GS!(16);

            String textPosString = scope String();
            if (mEditWidget.Content.HasSelection())
            {
                var selection = mEditWidget.Content.mSelection;
                textPosString.AppendF("Start {0}  Len {1}", selection.Value.MinPos, selection.Value.MaxPos - selection.Value.MinPos);
            }
            else
            {
                textPosString.AppendF("Index {0}", cursorTextPos);
            }

			int lineStart;
			int lineEnd;
			editWidgetContent.GetLinePosition(line, out lineStart, out lineEnd);

			int col = 0;
			int checkTextPos = lineStart;
			while (checkTextPos < cursorTextPos)
			{
				let c32 = editWidgetContent.mData.GetChar32(ref checkTextPos);
				if (!c32.IsCombiningMark)
					col++;
			}

			String charStr = null;
			checkTextPos = cursorTextPos;
			while (true)
			{
				bool isFirst = checkTextPos == cursorTextPos;
				char32 c32;
				int encodeLen;
				if (mOrigContent != null)
				{
					if (checkTextPos >= mOrigContent.Length)
						break;
					(c32, encodeLen) = mOrigContent.GetChar32(checkTextPos);
				}
				else
				{
					if (checkTextPos >= editWidgetContent.mData.mTextLength)
						break;
					(c32, encodeLen) = editWidgetContent.GetChar32(checkTextPos);
				}
				if ((c32 == 0) || ((!c32.IsCombiningMark) && (!isFirst)))
					break;

				if (charStr == null)
					charStr = scope:: String("  ");

				if (encodeLen > 1)
				{
					charStr.AppendF("\\u{{{0:X}}}  ", (int64)c32);
				}

				for (int ofs < encodeLen)
				{
					if (mOrigContent != null)
						charStr.AppendF("\\x{0:X2}", (uint8)mOrigContent[checkTextPos + ofs]);
					else
						charStr.AppendF("\\x{0:X2}", (int64)editWidgetContent.mData.mText[checkTextPos + ofs].mChar);
				}

				checkTextPos += encodeLen;
			}
			if (charStr != null)
			{
				textPosString.Append(charStr);
			}

			g.DrawString(textPosString, 16, textY, .Left, mWidth - GS!(140), .Ellipsis);
            g.DrawString(StackStringFormat!("Ln {0}", line + 1), mWidth - GS!(130), textY);
            g.DrawString(StackStringFormat!("Col {0}", col + 1), mWidth - GS!(70), textY);

			//using (g.PushColor(0xD0FFFFFF))
				base.DrawAll(g);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

            mEditWidget.Resize(0, 0, width, height - GS!(16));
			if (mQuickFind != null)
				mQuickFind.ResizeSelf();

			mMenuButton.Resize(width - GS!(26), height - GS!(12), GS!(16), GS!(16));
        }

        public float GetWantHeight(float wantWidth)
        {
            mEditWidget.Resize(0, 0, wantWidth, mEditWidget.mHeight);
            mEditWidget.Content.RecalcSize();
            return mEditWidget.mScrollContent.mHeight + mEditWidget.mScrollContentInsets.mTop + mEditWidget.mScrollContentInsets.mBottom + GS!(20);
        }

		public bool Close()
		{
			if (mQuickFind != null)
			{
				mQuickFind.Close();
				DeleteAndNullify!(mQuickFind);
				return false;
			}

			return true;
		}

		protected override void RemovedFromWindow()
		{
			base.RemovedFromWindow();
		}
    }

    public class WatchListViewItem : IDEListViewItem
    {
        public IWatchOwner mWatchOwner;
        public WatchEntry mWatchEntry ~ delete _;
        public bool mFailed;
        public int32 mErrorStart;
        public int32 mErrorEnd;
        
        public WatchSeriesInfo mWatchSeriesInfo;
		//public bool mOwnsWatchSeriesInfo;
        public int32 mSeriesMemberIdx;

        public WatchListViewItem mPrevPlaceholder;
        public int32 mPlaceholderStartIdx;
        public bool mIsPlaceholder;
        public bool mDisabled = false;
        public bool mAllowRefresh = false;
        public bool mValueChanged = false;		
		bool mWantRemoveSelf;

        public WatchRefreshButton mWatchRefreshButton;
		public ActionButton mActionButton;
		public String mTextAction ~ delete _;
		public bool mMustUpdateBeforeEvaluate;

        public override bool Selected
        {
            get
            {
                return base.Selected;
            }

            set
            {
                /*if (value)
                    mParent.UpdateAll();*/
                base.Selected = value;
            }
        }

        public bool Failed
        {
            get
            {
                return ((WatchListViewItem)GetSubItem(1)).mFailed;
            }
        }

        public this(IWatchOwner watchOwner, IDEListView listView)
        {
            mWatchOwner = watchOwner;
        }

		public ~this()
		{
			if (mWatchSeriesInfo != null)
			{
				mWatchSeriesInfo.ReleaseRef();
			}
		}

		public virtual WatchListViewItem GetWatchListViewItemParent()
		{
			return mParentItem as WatchListViewItem;
		}

        protected bool WatchIsString()
        {
            var mainWatchListViewItem = (WatchListViewItem)GetSubItem(0);
            if ((mainWatchListViewItem.mWatchEntry.mResultType == WatchResultType.TypeClass) || (mainWatchListViewItem.mWatchEntry.mResultType == WatchResultType.TypeValueType))
                return false;
            String resultTypeStr = mainWatchListViewItem.mWatchEntry.mResultTypeStr;
            return (resultTypeStr == "System.String") ||
                (resultTypeStr == "std::string") ||
                (resultTypeStr == "char8*") ||
                (resultTypeStr == "const char8*");
        }

        public override bool WantsTooltip(float mouseX, float mouseY)
        {
            if (mColumnIdx == 1)
            {
                //if (WatchIsString())
                return true;
            }

            return base.WantsTooltip(mouseX, mouseY);
        }

        public override void ShowTooltip(float mouseX, float mouseY)
        {
			scope AutoBeefPerf("WatchPanel.ShowTooltip");

            if (mColumnIdx != 1)
            {
                base.ShowTooltip(mouseX, mouseY);
                return;
            }

            var mainWatchListViewItem = (WatchListViewItem)GetSubItem(0); 

			String evalStr = null;
			String str = scope String();
			if (mColumnIdx == 1)
			{
				evalStr = scope:: String();
				evalStr.AppendF("{0}, rawStr, maxcount={1}", mainWatchListViewItem.mWatchEntry.mEvalStr, WatchStringEdit.cMoreBlockSize);
				// We purposely don't add mResultTypeStr here because the parse fails on std::basic_string and we know this will never be needed for an short enum name anyway
				gApp.DebugEvaluate(null, evalStr, str);
				if ((str.Length == 0) || (str.StartsWith("!")))
				{
					if (!base.WantsTooltip(mouseX, mouseY))
						return;
				}
			}

            if (str.Length > 0)
            {
                if (DarkTooltipManager.IsTooltipShown(this))
					return;

				//Profiler.StartSampling();

                float x = 8;
                if (mColumnIdx == 0)
                    x = LabelX + GS!(8);

                //string str = "This is a really long string, I want to see if we can do a scrollable mouseover view of this string.  We can even see if we can\nembed\r\nsome lines...";
                //str = "Hey\nDef\nEfg\nzzap";
               
                
                if (str.Length == 0)
                    return;

                float parentX;
                float parentY;
                SelfToOtherTranslate(mListView.mScrollContentContainer, x, 0, out parentX, out parentY);
                float wantWidth = Math.Max(GS!(320), mListView.mScrollContentContainer.mWidth - parentX - GS!(8));

                var watchStringEdit = new WatchStringEdit(str, evalStr);
				let ewc = watchStringEdit.mEditWidget.Content;

                float wantHeight = watchStringEdit.GetWantHeight(wantWidth - GS!(6) * 2);
				
                float maxHeight = GS!(78);
				if (!ewc.mWordWrap)
					maxHeight += GS!(20);
                if (wantHeight > maxHeight + 0.5f)
                {
                    watchStringEdit.mEditWidget.InitScrollbars(!ewc.mWordWrap, true);
                    wantHeight = maxHeight;
                }

                wantHeight += GS!(6) * 2;
                var tooltip = DarkTooltipManager.ShowTooltip("", this, x, mSelfHeight + mBottomPadding, wantWidth, wantHeight, true, true);
                if (tooltip != null)
                {
					tooltip.mRelWidgetMouseInsets = new Insets(0, 0, GS!(-8), 0);
					tooltip.mAllowMouseInsideSelf = true;
                    tooltip.AddWidget(watchStringEdit);
                    tooltip.mOnResized.Add(new (widget) => watchStringEdit.Resize(GS!(6), GS!(6), widget.mWidth - GS!(6) * 2, widget.mHeight - GS!(6) * 2));
                    tooltip.mOnResized(tooltip);
					tooltip.mWidgetWindow.mOnWindowKeyDown.Add(new => gApp.[Friend]SysKeyDown);
					tooltip.mOnKeyDown.Add(new (evt) =>
						{
							if ((evt.mKeyCode == .Escape) && (!watchStringEdit.Close()))
								evt.mHandled = true;
						});
                }

				//Profiler.StopSampling();

                return;
            }

            base.ShowTooltip(mouseX, mouseY);            
        }

        public void AddRefreshButton()
        {
            if ((mWatchRefreshButton == null) && (mColumnIdx == 0) && (mSubItems.Count >= 3))
            {
                mWatchRefreshButton = new WatchRefreshButton();
                mWatchRefreshButton.Resize(-16, 0, 20, 20);
                mWatchRefreshButton.mOnMouseDown.Add(new (evt) => RefreshWatch());
                var typeSubItem = GetSubItem(2);
                typeSubItem.AddWidget(mWatchRefreshButton);
                mListView.mListSizeDirty = true;
            }
        }

		public void AddActionButton()
		{
			if ((mActionButton == null) && (mColumnIdx == 0))
			{
				if (mWatchEntry.mAction == "ShowCodeAddr 0x'00000000")
					return;
				if (mWatchEntry.mAction == "ShowCodeAddr 0x00000000")
					return;
				
			    mActionButton = new ActionButton();
				//var darkListView = (DarkListView)mListView;
			    mActionButton.mOnMouseDown.Add(new (evt) =>
                    {
						gApp.PerformAction(mWatchEntry.mAction);
                    });
			    var typeSubItem = (WatchListViewItem)GetSubItem(1);
			    typeSubItem.AddWidget(mActionButton);
				mActionButton.Resize(typeSubItem.Font.GetWidth(typeSubItem.mLabel) + 8, 0, 20, 20);
			    mListView.mListSizeDirty = true;
			}
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			if (mActionButton != null)
			{
				//var darkListView = (DarkListView)mListView;
				var typeSubItem = (WatchListViewItem)GetSubItem(1);
                mActionButton.Resize(typeSubItem.Font.GetWidth(typeSubItem.mLabel) + 8, 0, 20, 20);
			}
		}

        public void SetDisabled(bool disabled, bool allowRefresh)
        {
            if ((mDisabled == disabled) && (mAllowRefresh == allowRefresh))
                return;
            mDisabled = disabled;
            mAllowRefresh = allowRefresh;

            if ((mDisabled) && (allowRefresh))
            {
                AddRefreshButton();             
            }
            else if (mWatchRefreshButton != null)
            {
				Widget.RemoveAndDelete(mWatchRefreshButton);
                mWatchRefreshButton = null;
            }

            if (mOpenButton != null)
                mOpenButton.mAllowOpen = (!disabled) || (mOpenButton.mIsOpen);

            if (mChildItems != null)
            {
                for (WatchListViewItem childItem in mChildItems)
                    childItem.SetDisabled(disabled, allowRefresh);
            }
        }

        public override float ResizeComponents(float xOffset)
        {
            float retVal = base.ResizeComponents(xOffset);
            if ((mColumnIdx == 1) && (((WatchListViewItem)GetSubItem(0)).mWatchRefreshButton != null))
                mTextAreaLengthOffset = -16;
            else
                mTextAreaLengthOffset = 0;
            return retVal;
        }

        void RefreshWatch()
        {
            var parentWatchListViewItem = mParentItem as WatchListViewItem;
            if (parentWatchListViewItem != null)
            {
                parentWatchListViewItem.RefreshWatch();
                return;
            }

            mWatchEntry.mIsNewExpression = true;
            mWatchOwner.UpdateWatch(this);
        }

        public override void DrawAll(Graphics g)
        {
            if (mParentItem.mChildAreaHeight == 0)
                return;

			if (mMustUpdateBeforeEvaluate)
			{
				Update();
			}

            if (!IsVisible(g))
				return;

            if ((mWatchEntry != null) && (!mWatchEntry.mHasValue))
            {
                if (mParentItem.mChildAreaHeight > 0)
                {
                    var parentWatchListViewItem = mParentItem as WatchListViewItem;

                    if ((!mDisabled) || (parentWatchListViewItem == null) || (mWatchEntry.mIsNewExpression))
                    {
                        mWatchOwner.UpdateWatch(this);
                    }
                    else if (mColumnIdx == 0)
                    {
                        var valSubItem = GetSubItem(1);
                        if (valSubItem.Label == "")
                            valSubItem.Label = "???";
                    }
                }
            }
            
            base.DrawAll(g);
        }

		public bool IsBold
		{
			get
			{
				var headItem = (WatchListViewItem)GetSubItem(0);
				return (mValueChanged) && (!headItem.mDisabled) && (!mFailed);
			}
		}

		public Font Font
		{
			get
			{
				return IsBold ? DarkTheme.sDarkTheme.mSmallBoldFont : DarkTheme.sDarkTheme.mSmallFont;
			}
		}

        public override void Draw(Graphics g)
        {
            uint32 color = Color.White;
            var headItem = (WatchListViewItem)GetSubItem(0);
            var valueItem = (WatchListViewItem)GetSubItem(1);
            if (headItem.mDisabled)
                color = 0x80FFFFFF;
            else if (mFailed)
                color = 0xFFFF4040;
            
            var watchListView = (WatchListView)mListView;
            if (IsBold)
                watchListView.mFont = DarkTheme.sDarkTheme.mSmallBoldFont;
            using (g.PushColor(color))
            {
                base.Draw(g);                
            }
            watchListView.mFont = DarkTheme.sDarkTheme.mSmallFont;

            if ((this == headItem) && (mWatchEntry.mResultType != WatchResultType.None))
            {
                DarkTheme.ImageIdx imageIdx = .IconValue;
				if (mWatchEntry.mMemoryBreakpointAddr != 0)
				{
					imageIdx = .RedDot;
				}
                else if (mWatchEntry.mResultType.HasFlag(WatchResultType.Object))
                {
                    if (mWatchEntry.mIsDeleted)
                        imageIdx = .IconObjectDeleted;
                    else
                        imageIdx = .IconObject;
                }
                else if (mWatchEntry.mResultType.HasFlag(WatchResultType.TypeClass))
                {
                    imageIdx = .Type_Class;
                }
                else if (mWatchEntry.mResultType.HasFlag(WatchResultType.TypeValueType))
                {
                    imageIdx = .Type_ValueType;
                }
                else if (mWatchEntry.mResultType.HasFlag(WatchResultType.Namespace))
                {
                    imageIdx = .Namespace;
                }
                else if (mWatchEntry.mResultType.HasFlag(WatchResultType.Pointer))
                {
                    imageIdx = .IconPointer;
                }

                if (valueItem.mFailed)
                    imageIdx = .IconError;

                var listView = (WatchListView)mListView;
                using (g.PushColor(headItem.mDisabled ? 0x80FFFFFF : Color.White))
                {
                    g.Draw(DarkTheme.sDarkTheme.GetImage(imageIdx), listView.mLabelX - GS!(22), 0);

                    if (mWatchEntry.mIsAppendAlloc)
                        g.Draw(DarkTheme.sDarkTheme.GetImage(.IconObjectAppend), listView.mLabelX - GS!(22), 0);
                    if (mWatchEntry.mIsStackAlloc)
                        g.Draw(DarkTheme.sDarkTheme.GetImage(.IconObjectStack), listView.mLabelX - GS!(22), 0);
                }
            }

            if (mErrorEnd != 0)
            {
                float labelX = 44;
				String subStr = scope String();
				subStr.Append(mLabel, 0, Math.Min(mErrorStart, mLabel.Length));
                float strStarts = labelX + g.mFont.GetWidth(subStr);
				subStr.Clear();
				subStr.Append(mLabel, 0, Math.Min(mErrorEnd, mLabel.Length));
                float strEnds = labelX + g.mFont.GetWidth(subStr);
                strEnds = Math.Min(mListView.mColumns[0].mWidth - LabelX + labelX, strEnds);
                using (g.PushColor(0xFFFF4040))
                    IDEApp.sApp.DrawSquiggle(g, strStarts, GS!(2), strEnds - strStarts);
            }
        }

        public override void Update()
        {
			mMustUpdateBeforeEvaluate = false;

			if ((mWatchEntry != null) && (mWatchEntry.mIsPending))
			{
				if (!gApp.mDebugger.mIsRunning)
				{
					mWatchEntry.mIsPending = false;
				}
				else
				{
					gApp.mDebugger.EvaluateContinueKeep();
					if (gApp.mDebugger.GetRunState() == .DebugEval_Done)
					{
						mWatchOwner.UpdateWatch(this);
					}
				}
			}
            var watchListView = (WatchListView)mListView;
            if (mParentItem.mChildAreaHeight != 0)                
            {
                float itemHeight = watchListView.mFont.GetLineSpacing();
                int32 addrSize = IDEApp.sApp.mDebugger.GetAddrSize() * 2;
                int32 dbgContinuationCount = 0;

                if ((mWatchSeriesInfo != null) && (addrSize != 0) && (mSeriesMemberIdx == 0))
                {
                    /*Stopwatch sw = new Stopwatch();
                    sw.Start();*/

                    /*float ofsX;
                    float ofsY;
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);*/

                    float ofsX;
                    float ofsY;
                    mParent.SelfToOtherTranslate(mListView, 0, 0, out ofsX, out ofsY);
                    ofsY -= (float)mListView.mVertPos.mDest + mListView.mScrollContent.mY;
            
                    int32 curMemberIdx = mWatchSeriesInfo.mStartMemberIdx;
                    WatchListViewItem prevWatchListViewItem = null;
                    WatchListViewItem nextWatchListViewItem = (WatchListViewItem)mParentItem.mChildItems[curMemberIdx];

                    int entryAddrSize = addrSize * mWatchSeriesInfo.mAddrsEntrySize;
                    int addrsCount = 0;
                    if (mWatchSeriesInfo.mAddrs != null)
                        addrsCount = mWatchSeriesInfo.mAddrs.Length / entryAddrSize;

                    int totalCount = mWatchSeriesInfo.mCount;
                    if (totalCount == -1)
                    {                        
                        //int wantNewCount = Math.Min(idx + 32, mWatchSeriesInfo.mCount) - addrsCount;
                        bool continuationDone = false;
                        if (BFApp.sApp.mIsUpdateBatchStart)
                        {
                            String continuationResult = scope String();
                            IDEApp.sApp.mDebugger.GetCollectionContinuation(mWatchSeriesInfo.mContinuationData, 256, continuationResult);
                            var continuationResultVals = scope List<StringView>(continuationResult.Split('\n'));
                            String extraAddrs = scope String(continuationResultVals[0]);
                            mWatchSeriesInfo.mAddrs.Append(extraAddrs);
                            if (continuationResultVals.Count > 1)
							{
								delete mWatchSeriesInfo.mContinuationData;
                                mWatchSeriesInfo.mContinuationData = new String(continuationResultVals[1]);
							}
                            if (extraAddrs.Length == 0)
                                continuationDone = true;

                            dbgContinuationCount++;
                        }

                        addrsCount = mWatchSeriesInfo.mAddrs.Length / entryAddrSize;
                        totalCount = addrsCount;
                        if (continuationDone)
                        {
                            // We finally have the count
                            mWatchSeriesInfo.mCount = totalCount;
                        }                        
                    }
                    mWatchSeriesInfo.mCurCount = totalCount;

                    int showCount = Math.Min(totalCount, mWatchSeriesInfo.mShowPages * mWatchSeriesInfo.mShowPageSize);

                    float curY = mY;
                    float prevY = curY;
                    float lastBottomPadding = 0;
                    for (int32 idx = 0; idx < showCount; idx++)
					ShowBlock:
                    {
                        WatchListViewItem curWatchListViewItem = null;                        
                                    
                        if ((nextWatchListViewItem != null) && (idx == nextWatchListViewItem.mSeriesMemberIdx))
                        {
                            curWatchListViewItem = nextWatchListViewItem;                            
                            curMemberIdx++;
                            if (curMemberIdx < mParentItem.mChildItems.Count)
                            {
                                nextWatchListViewItem = (WatchListViewItem)mParentItem.mChildItems[curMemberIdx];
                                if (nextWatchListViewItem.mWatchSeriesInfo == null)
                                    nextWatchListViewItem = null;
                                if (nextWatchListViewItem != null)
                                    lastBottomPadding = nextWatchListViewItem.mBottomPadding;
                            }
                            else
                                nextWatchListViewItem = null;
                        }                        
                        
                        bool wantsFillIn = (curY + ofsY + itemHeight >= 0) && (curY + ofsY < mListView.mHeight);
                        bool wantsDelete = !wantsFillIn;
						bool forceDelete = false;
						bool forceFillIn = false;

                        if (mDisabled)
                        {
                            wantsFillIn = false;
                            wantsDelete = false;
                        }

						if ((curWatchListViewItem != null) && (idx > 0) && (curWatchListViewItem.mWatchEntry.mSeriesFirstVersion != mWatchSeriesInfo.mSeriesFirstVersion))
						{
							// This logic gets invoked for Beef array views....
							forceDelete = true;
							wantsFillIn = true;
						}

						if ((curWatchListViewItem != null) && (idx > 0) && (curWatchListViewItem.mWatchEntry.mSeriesVersion != mWatchSeriesInfo.mSeriesVersion))
						{
							forceFillIn = true;
						}

						if ((forceDelete) || 
                            ((wantsDelete) && (idx != 0) && (curWatchListViewItem != null) && (curWatchListViewItem.mChildAreaHeight == 0) && (!curWatchListViewItem.mIsSelected)))
						{
							if (curWatchListViewItem == nextWatchListViewItem)
								nextWatchListViewItem = null;

						    curMemberIdx--;
						    mParentItem.RemoveChildItem(curWatchListViewItem);
						    curWatchListViewItem = null;
						}

                        if (((curWatchListViewItem == null) && (wantsFillIn)) ||
							(forceFillIn))
                        {
                            prevWatchListViewItem.mBottomPadding = (curY - prevWatchListViewItem.mY) - prevWatchListViewItem.mSelfHeight - prevWatchListViewItem.mChildAreaHeight;
							if (curWatchListViewItem == null)
                            	curWatchListViewItem = (WatchListViewItem)mParentItem.CreateChildItemAtIndex(curMemberIdx);
                            curWatchListViewItem.mX = mX;
							if (curWatchListViewItem.mWatchSeriesInfo == null)
							{
								curWatchListViewItem.mVisible = false;
								mWatchSeriesInfo.AddRef();
	                            curWatchListViewItem.mWatchSeriesInfo = mWatchSeriesInfo;
								curWatchListViewItem.mSeriesMemberIdx = idx;
							}
							else
							{
								Debug.Assert(curWatchListViewItem.mWatchSeriesInfo == mWatchSeriesInfo);
								Debug.Assert(curWatchListViewItem.mSeriesMemberIdx == idx);
							}
                            
                            Object[] formatParams = scope Object[mWatchSeriesInfo.mAddrsEntrySize + 1];
                            formatParams[0] = idx;
                            
                            if (mWatchSeriesInfo.mAddrs != null)
                            {                                
                                if (idx >= addrsCount)
                                {
                                    int wantNewCount = Math.Min(idx + 32, mWatchSeriesInfo.mCount) - addrsCount;
                                    String continuationResult = scope String();
                                    IDEApp.sApp.mDebugger.GetCollectionContinuation(mWatchSeriesInfo.mContinuationData, (int32)wantNewCount, continuationResult);
									if (!continuationResult.IsEmpty)
									{
	                                    var continuationResultVals = scope List<StringView>(continuationResult.Split('\n'));
	                                    mWatchSeriesInfo.mAddrs.Append(scope String(continuationResultVals[0]));
	                                    mWatchSeriesInfo.mContinuationData.Set(scope String(continuationResultVals[1]));
	                                    addrsCount = mWatchSeriesInfo.mAddrs.Length / entryAddrSize;
	                                    dbgContinuationCount++;
									}
                                }

                                for (int32 i = 0; i < mWatchSeriesInfo.mAddrsEntrySize; i++)
								{
									if (idx >= addrsCount)
									{
										formatParams[i + 1] = "?";
									}
									else
									{
										var str = scope:ShowBlock String();
										str.Append(mWatchSeriesInfo.mAddrs, idx * entryAddrSize + (i * addrSize), addrSize);
	                                    formatParams[i + 1] = str;
									}
								}
                            }

							var dispStr = scope String();
							dispStr.AppendF(mWatchSeriesInfo.mDisplayTemplate, params formatParams);
							var evalStr = scope String();
							evalStr.AppendF(mWatchSeriesInfo.mEvalTemplate, params formatParams);

                            watchListView.mWatchOwner.SetupListViewItem(curWatchListViewItem, dispStr, evalStr);
							curWatchListViewItem.mWatchEntry.mSeriesFirstVersion = mWatchSeriesInfo.mSeriesFirstVersion;
							curWatchListViewItem.mWatchEntry.mSeriesVersion = mWatchSeriesInfo.mSeriesVersion;
							if (!forceFillIn)
                            	curMemberIdx++;
                        }
                        

                        if (prevWatchListViewItem != null)
                        {
                            if (mDisabled)
                                prevWatchListViewItem.mBottomPadding = 0;
                            else
                                prevWatchListViewItem.mBottomPadding = (curY - prevY) - prevWatchListViewItem.mSelfHeight - prevWatchListViewItem.mChildAreaHeight;
                        }

                        if (curWatchListViewItem != null)
                            prevY = curY;

                        curY += itemHeight;
                        if (curWatchListViewItem != null)
                        {                            
                            curY += curWatchListViewItem.mChildAreaHeight;
                            prevWatchListViewItem = curWatchListViewItem;
                        }
                    }

                    if (totalCount > mWatchSeriesInfo.mShowPageSize)
                    {
                        if (mWatchSeriesInfo.mMoreButton == null)
                        {                            
                            mWatchSeriesInfo.mMoreButton = (DarkButton)DarkTheme.sDarkTheme.CreateButton(mParent, "More", mX + GS!(26), 0, GS!(68), GS!(20));
                            mWatchSeriesInfo.mMoreButton.mOnMouseClick.Add(new (evt) =>
	                            {
	                                if (mWatchSeriesInfo.mMoreButton.mDisabled)
	                                    return;
	                                mWatchSeriesInfo.mShowPages = (int32)Math.Min(mWatchSeriesInfo.mShowPages + 1, 1 + (mWatchSeriesInfo.mCurCount + mWatchSeriesInfo.mShowPageSize - 1) / mWatchSeriesInfo.mShowPageSize);
									mWatchSeriesInfo.mMoreButton.mVisible = false;
									mWatchSeriesInfo.mLessButton.mVisible = false;
	                            });
							mWatchSeriesInfo.mMoreButton.mOnDeleted.Add(new (widget) => 
                            	{ NOP!(); });
                            mWatchSeriesInfo.mLessButton = (DarkButton)DarkTheme.sDarkTheme.CreateButton(mParent, "Less", mX + GS!(26 + 68 + 2), 0, GS!(68), GS!(20));
                            mWatchSeriesInfo.mLessButton.mOnMouseClick.Add(new (evt) => { mWatchSeriesInfo.mShowPages = Math.Max(1, mWatchSeriesInfo.mShowPages - 1); });
                        }

                        float xOfs = 16;

                        float absX;
                        float absY;
                        SelfToOtherTranslate(mListView, xOfs, 0, out absX, out absY);

                        float btnX = mX + xOfs;
                        float buttonAreaWidth = (mListView.mColumns[0].mWidth + mListView.mColumns[1].mWidth - absX * 2);
                        float btnWidth = (buttonAreaWidth - 4) / 2;
                        curY += 3;

                        btnWidth = 62;

                        mWatchSeriesInfo.mMoreButton.mVisible = !mDisabled;
                        mWatchSeriesInfo.mMoreButton.mDisabled = showCount >= totalCount;
                        mWatchSeriesInfo.mMoreButton.Resize(btnX, curY, btnWidth, 20);

                        mWatchSeriesInfo.mLessButton.mVisible = !mDisabled;
                        mWatchSeriesInfo.mLessButton.mVisible = mWatchSeriesInfo.mShowPages > 1;
                        mWatchSeriesInfo.mLessButton.Resize(btnX + btnWidth + 2, curY, btnWidth, 20);

                        curY += 21;
                    }
                    else
                    {
                        if (mWatchSeriesInfo.mMoreButton != null)
                        {
                            Widget.RemoveAndDelete(mWatchSeriesInfo.mMoreButton);
                            mWatchSeriesInfo.mMoreButton = null;                        
                            Widget.RemoveAndDelete(mWatchSeriesInfo.mLessButton);
                            mWatchSeriesInfo.mLessButton = null;
                        }
                    }

                    if (prevWatchListViewItem != null)
                    {
                        if (mDisabled)
                            prevWatchListViewItem.mBottomPadding = 0;
                        else
                            prevWatchListViewItem.mBottomPadding = (curY - prevY) - prevWatchListViewItem.mSelfHeight - prevWatchListViewItem.mChildAreaHeight;
                        
                        if (prevWatchListViewItem.mBottomPadding != lastBottomPadding)
                            mListView.mListSizeDirty = true;
                    }

                    while (curMemberIdx < mParentItem.mChildItems.Count)
                    {
                        var curWatchListViewItem = (WatchListViewItem)mParentItem.mChildItems[curMemberIdx];
                        if (curWatchListViewItem.mWatchSeriesInfo == null)
                            break;
						if (curWatchListViewItem == this)
						{
							mWantRemoveSelf = true;
							return;
						}
                        mParentItem.RemoveChildItem(curWatchListViewItem);						
                        /*if (mParentItem == null) 
                            return;*/
                    }

                    /*sw.Stop();
                    int elapsedMs = (int)sw.ElapsedMilliseconds;
                    if (elapsedMs > 50)
                    {
                        Console.WriteLine("WatchPanel Update: {0}ms Cont:{1}", elapsedMs, dbgContinuationCount);
                    }*/
                }
            }

            base.Update();
        }

		public override void UpdateAll()
		{
			base.UpdateAll();
			if (mWantRemoveSelf)
				mParentItem.RemoveChildItem(this);
		}
    }

    public class WatchPanel : Panel, IWatchOwner
    {        
        public WatchListView mListView;

        public WatchListViewItem mSelectedParentItem;
        bool mCancelingEdit;
        bool mWatchesDirty;
        bool mHadStep;
        bool mClearHadStep;
        bool mDisabled = true;
        bool mIsAuto;
        int32 mDisabledTicks;
		bool mDeselectOnFocusLost = true;
        
        public WatchListViewItem mEditingItem;
        public ExpressionEditWidget mEditWidget;        

        public this(bool isAuto)
        {
            mIsAuto = isAuto;
            mListView = new WatchListView(this);
			mListView.mShowGridLines = true;
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
            mListView.UpdateScrollbars();            
            AddWidget(mListView);

			mListView.mOnMouseClick.Add(new => ListViewClicked);
            mListView.mOnMouseDown.Add(new => ListViewMouseDown);
			mListView.mOnItemMouseClicked.Add(new => ValueClicked);
			mListView.mOnItemMouseDown.Add(new => ValueMouseDown);
			
            ListViewColumn column = mListView.AddColumn(GS!(200), "Name");
            column.mMinWidth = GS!(40);
            column = mListView.AddColumn(GS!(200), "Value");
            column.mMinWidth = GS!(40);
            column = mListView.AddColumn(GS!(200), "Type");

            mListView.mOnDragEnd.Add(new => HandleDragEnd);
            mListView.mOnDragUpdate.Add(new => HandleDragUpdate);
            
            SetScaleData();

			//RebuildUI();
		}

		public void Reset()
		{
			mListView.GetRoot().Clear();
		}

		void SetScaleData()
		{
			mListView.mIconX = GS!(4);
			mListView.mOpenButtonX = GS!(4);
			mListView.mLabelX = GS!(44);
			mListView.mChildIndent = GS!(16);
			mListView.mHiliteOffset = GS!(-2);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

		public void Serialize(StructuredData data, bool includeItems)
		{
			data.Add("Type", mIsAuto ? "AutoWatchPanel" : "WatchPanel");

			IDEUtils.SerializeListViewState(data, mListView);

			if (includeItems && !mIsAuto)
			{
				using (data.CreateArray("Items"))
				{
				    var childItems = mListView.GetRoot().mChildItems;
				    if (childItems != null)
				    {
				        for (WatchListViewItem watchListViewItem in childItems)
				        {
				            var watchEntry = watchListViewItem.mWatchEntry;
				            data.Add(watchEntry.mEvalStr);
				        }
				    }
				}
			}
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);
            Serialize(data, true);
        }

        public override bool Deserialize(StructuredData data)
        {
            base.Deserialize(data);

            IDEUtils.DeserializeListViewState(data, mListView);
            for (let itemKey in data.Enumerate("Items"))
            {
                //for (int32 watchIdx = 0; watchIdx < data.Count; watchIdx++)
				//for (var watchKV in data)
                {
                    String watchEntry = scope String();
                    //data.GetString(watchIdx, watchEntry);
					data.GetCurString(watchEntry);
                    AddWatch(watchEntry);
                }
            }

            return true;
        }

		public override void FocusForKeyboard()
		{
			var root = mListView.GetRoot();
			if (root.GetChildCount() > 0)
			{
				var lastItem = root.GetChildAtIndex(root.GetChildCount() - 1);
				root.SelectItemExclusively(lastItem);
				mListView.EnsureItemVisible(lastItem, false);
			}
		}

        static void CompactChildExpression(WatchListViewItem listViewItem, String outExpr)
        {
            var parentItem = listViewItem.GetWatchListViewItemParent();
			if (parentItem == null)
			{
				outExpr.Append(listViewItem.mWatchEntry.mEvalStr);
				return;
			}
            String parentEvalStr = scope String();
            CompactChildExpression(parentItem, parentEvalStr);
            String evalStr = listViewItem.mWatchEntry.mEvalStr;
            String compactEvalStr = scope String();
            IDEApp.sApp.mDebugger.CompactChildExpression(evalStr, parentEvalStr, compactEvalStr);
            outExpr.Set(compactEvalStr);
        }

        void HandleDragUpdate(DragEvent evt)
        {
            evt.mDragAllowed = false;
            if (mIsAuto)
                return;

            var dragSource = evt.mSender as WatchListViewItem;
            var dragTarget = evt.mDragTarget as WatchListViewItem;
            if (dragSource == null)
                return;
            if (dragTarget == null)  
                return;
            if (dragSource.mLabel == "")
                return;
            while (dragTarget.mParentItem != mListView.GetRoot())
            {
                // Check for if we're dragging after the last open child item.  If so, treat it as if we're dragging to after the topmost parent
                /*if ((evt.mDragTargetDir != 1) || (dragTarget != dragTarget.mParentItem.mChildItems[dragTarget.mParentItem.mChildItems.Count - 1]))
                    return;*/
                evt.mDragTargetDir = 1;
                dragTarget = (WatchListViewItem)dragTarget.mParentItem;
                evt.mDragTarget = dragTarget;
            }
            if ((dragTarget.mLabel == "") && (evt.mDragTargetDir >= 0))
                evt.mDragTargetDir = -1;            
            if (evt.mDragTargetDir == 0)
                return;
            evt.mDragAllowed = true;
        }

        void HandleDragEnd(DragEvent theEvent)
        {
            if (!theEvent.mDragAllowed)
                return;

            if (theEvent.mDragTarget is IDEListViewItem)
            {
                var source = (WatchListViewItem)theEvent.mSender;
                var target = (WatchListViewItem)theEvent.mDragTarget;

                if (source.mListView == target.mListView)
                {                    
                    if (source == target)
                        return;

                    if (source.mParentItem == mListView.GetRoot())
                    {
                        // We're dragging a top-level item into a new position
                        source.mParentItem.RemoveChildItem(source, false);
                        if (theEvent.mDragTargetDir == -1) // Before
                            target.mParentItem.InsertChild(source, target);
                        else if (theEvent.mDragTargetDir == 1) // After
                            target.mParentItem.AddChild(source, target);
                    }
                    else
                    {
                        String compactEvalStr = scope String();
                        CompactChildExpression(source, compactEvalStr);

                        var rootItem = mListView.GetRoot();
                        int idx = rootItem.mChildItems.IndexOf(target);
                        if (theEvent.mDragTargetDir > 0)
                            idx += theEvent.mDragTargetDir;
                        var listViewItem = (WatchListViewItem)rootItem.CreateChildItemAtIndex(idx);
                        listViewItem.mVisible = false;
                        SetupListViewItem(listViewItem, compactEvalStr, compactEvalStr);
                        rootItem.SelectItemExclusively(listViewItem);
                        mListView.EnsureItemVisible(listViewItem, true);
                    }
                }
            }
        }

        WatchListView IWatchOwner.GetWatchListView()
        {
            return mListView;
        }

        public void SetupListViewItem(WatchListViewItem listViewItem, String name, String evalStr)
        {
            WatchEntry watchEntry = new WatchEntry();
            watchEntry.mName = new String(name);
            watchEntry.mEvalStr = new String(evalStr ?? name);

            listViewItem.Label = name;            
            listViewItem.AllowDragging = true;
            listViewItem.mOpenOnDoubleClick = false;

            var subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(1);
            subViewItem.Label = "";
            subViewItem.AllowDragging = true;

            subViewItem = (DarkListViewItem)listViewItem.GetOrCreateSubItem(2);
            subViewItem.Label = "";
			subViewItem.AllowDragging = true;

			delete listViewItem.mWatchEntry;
            listViewItem.mWatchEntry = watchEntry;
        }

        public WatchListViewItem AddWatchItem(String name)
        {
            // This is the public interface to AddWatch, which uses the first available blank watch slot or appends a new one if necessary
            var rootItem = mListView.GetRoot();
            int childCount = mListView.GetRoot().GetChildCount();
            for (int iChild=0; iChild<childCount; ++iChild)
            {
                if (rootItem.GetChildAtIndex(iChild).mLabel.Length > 0)
                    continue;
                
                rootItem.RemoveChildItem(rootItem.GetChildAtIndex(iChild));

                var listViewItem = (WatchListViewItem)rootItem.CreateChildItemAtIndex(iChild);
                listViewItem.mVisible = false;
                SetupListViewItem(listViewItem, name, null);
                rootItem.SelectItemExclusively(listViewItem);
                mListView.EnsureItemVisible(listViewItem, true);
                
                return listViewItem;
            }
            
            return AddWatch(name);
        }

        WatchListViewItem AddWatch(String name, String evalStr = null, ListViewItem parentItem = null)
        {
			MarkDirty();

			var useParentItem = parentItem;
            if (useParentItem == null)
                useParentItem = mListView.GetRoot();
            var listViewItem = (WatchListViewItem)useParentItem.CreateChildItem();
            listViewItem.mVisible = false;            

            SetupListViewItem(listViewItem, name, evalStr);

			if ((parentItem != null) && (parentItem.Selected))
			{
				// If the parentItem is selected and the next item after the parentItem is selected then we may have a selection span
				// so we want to expand that selection to the newly-created item

				int parentIdx = parentItem.mParentItem.GetIndexOfChild(parentItem);
				if (parentIdx + 1 < parentItem.mParentItem.mChildItems.Count)
				{
					let nextItem = parentItem.mParentItem.mChildItems[parentIdx + 1];
					if (nextItem.Selected)
					{
						if ((parentItem.mChildItems.Count == 1) || (parentItem.mChildItems[parentItem.mChildItems.Count - 2].Selected))
							listViewItem.Selected = true;
					}
				}
			}

            return listViewItem;
        }

        public void SetDisabled(bool disabled)
        {
			MarkDirty();
            mDisabled = disabled;
            mMouseVisible = !disabled;
            mDisabledTicks = 0;
			if (!gApp.mDebugger.mIsRunning)
				mDisabledTicks = 9999; // Show immediately grey
        }        

        public override void DrawAll(Graphics g)
        {
            bool showDisabled = (mDisabled) && (mDisabledTicks > 40);
            using (g.PushColor(showDisabled ? 0x80FFFFFF : Color.White))
                base.DrawAll(g);
        }

        public void MarkWatchesDirty(bool hadStep, bool clearHadStep = false)
        {
			MarkDirty();
            mWatchesDirty = true;
            if (hadStep)
                mHadStep = true;
            if (clearHadStep)
                mClearHadStep = true;
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mListView.Resize(0, 0, width, height);
        }

        void HandleRenameCancel(EditEvent theEvent)
        {
            mCancelingEdit = true;            		
			bool removedItem;
            DoHandleEditLostFocus((EditWidget)theEvent.mSender, out removedItem);
            if ((mHasFocus) && (!removedItem))
            {
                Update();
                /*var lastItem = mListView.GetRoot().mChildItems[mListView.GetRoot().mChildItems.Count - 1];
                lastItem.Focused = true;*/
            }
        }

        void HandleRenameSubmit(EditEvent theEvent)
        {
            var watchEditWidgetContent = (ExpressionEditWidgetContent)mEditWidget.Content;
            if ((watchEditWidgetContent.mAutoComplete != null) && (watchEditWidgetContent.mAutoComplete.HasSelection()))
                watchEditWidgetContent.mAutoComplete.InsertSelection((char8)0);            
            HandleEditLostFocus((EditWidget)theEvent.mSender);            
        }

		
		void HandleEditLostFocus(Widget widget)
		{
			bool removedItem;
			DoHandleEditLostFocus(widget, out removedItem);
		}

		void HandleEditKeyDown(KeyDownEvent evt)
		{
			if (((evt.mKeyCode == .Tab) || (evt.mKeyCode == .F2)) &&
				(evt.mKeyFlags == 0))
			{
				if (!mEditWidget.IsShowingAutoComplete())
				{
					let valueItem = (WatchListViewItem)mEditingItem.GetSubItem(1);
					if (!valueItem.Label.IsEmpty)
					{
						EditEvent editEvent = scope .();
						editEvent.mSender = evt.mSender;
						HandleRenameCancel(editEvent);
						EditListViewItem(valueItem);
					}
				}
			}
		}
        
		void DoHandleEditLostFocus(Widget widget, out bool removedItem)
        {
			removedItem = false;
            EditWidget editWidget = (EditWidget)widget;
            editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
            editWidget.mOnSubmit.Remove(scope => HandleRenameSubmit, true);
            editWidget.mOnCancel.Remove(scope => HandleRenameCancel, true);
			editWidget.mOnKeyDown.Remove(scope => HandleEditKeyDown, true);
            WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);

            if (!mEditWidget.Content.HasUndoData())
                mCancelingEdit = true;

            String newValue = scope String();
            editWidget.GetText(newValue);
            newValue.Trim();

            WatchListViewItem listViewItem = (WatchListViewItem)mEditingItem;
            int32 column = listViewItem.mColumnIdx;
            
            if (column == 0)
            {
				var trimmedLabel = scope String(listViewItem.mLabel);
				trimmedLabel.Trim();

                if (trimmedLabel != newValue)
                {
                    var watchEntry = listViewItem.mWatchEntry;
                    watchEntry.mIsNewExpression = true;

                    if (!mCancelingEdit)
                        listViewItem.Label = newValue;

                    if (listViewItem.mLabel.Length > 0)
                    {				
                        String.NewOrSet!(watchEntry.mName, listViewItem.mLabel);
                        String.NewOrSet!(watchEntry.mEvalStr, watchEntry.mName);
                        IDEApp.sApp.mMemoryPanel.MarkViewDirty();
                        gApp.MarkWatchesDirty();
                    }
                    else
                    {
                        mListView.GetRoot().RemoveChildItem(listViewItem);
						removedItem = true;
                    }
                }
            }
            else if ((column == 1) && (newValue.Length > 0) && (!mCancelingEdit))
            {
                var headListViewItem = (WatchListViewItem)listViewItem.GetSubItem(0);
                String evalStr = scope String(headListViewItem.mWatchEntry.mEvalStr, ", assign=", newValue);
                String val = scope String();
                gApp.DebugEvaluate(headListViewItem.mWatchEntry.mResultTypeStr, evalStr, val, -1, .NotSet, .AllowSideEffects | .AllowCalls);
                headListViewItem.mWatchEntry.mIsNewExpression = true;
                //IDEApp.sApp.OutputLine(val);

                if (val.StartsWith("!", StringComparison.Ordinal))
                {
                    String errorString = scope String();
                    errorString.Append(val, 1);
                    var errorVals = scope List<StringView>(errorString.Split('\t'));
                    if (errorVals.Count == 3)
                    {
                        int32 errorStart = int32.Parse(scope String(errorVals[0]));
                        int32 errorEnd = errorStart + int32.Parse(scope String(errorVals[1])).Get();
						if (errorEnd > evalStr.Length)
							errorEnd = (int32)evalStr.Length;
                        errorString.Set(errorVals[2]);
                        if ((errorEnd > 0) && (errorStart < evalStr.Length) && (!errorString.Contains(':')))
						{
							String appendStr = scope String();
							appendStr.Append(evalStr, errorStart, errorEnd - errorStart);
                            errorString.Append(": ", appendStr); 
						}
                    }
                    IDEApp.sApp.Fail(errorString);
                }

                IDEApp.sApp.mMemoryPanel.MarkViewDirty();
                gApp.MarkWatchesDirty();
            }

			editWidget.RemoveSelf();
			BFApp.sApp.DeferDelete(editWidget);
            mEditWidget = null;

            if (mWidgetWindow.mFocusWidget == null)
                SetFocus();
            if ((mHasFocus) && (!removedItem))
                listViewItem.GetSubItem(0).Focused = true;
        }

        public void EditListViewItem(WatchListViewItem listViewItem)
        {
            if (mEditWidget != null)
                return;

			if ((listViewItem.mColumnIdx == 0) && (mIsAuto))
				return;

            if ((listViewItem.mColumnIdx == 0) && (listViewItem.mParentItem.mParentItem != null))
                return;

			//mListView.UpdateContentPosition();
			mListView.UpdateListSize();

            var headListViewItem = (WatchListViewItem)listViewItem.GetSubItem(0);
            if ((listViewItem.mColumnIdx > 0) && (headListViewItem.mWatchEntry != null) && (!headListViewItem.mWatchEntry.mCanEdit))
			{
				if (headListViewItem.mActionButton != null)
					headListViewItem.mActionButton.mOnMouseDown(null);

                return;
			}

            mCancelingEdit = false;

            ExpressionEditWidget editWidget = new ExpressionEditWidget();
            mEditWidget = editWidget;
            String editVal = listViewItem.mLabel;
            if ((listViewItem.mColumnIdx > 0) && (headListViewItem.mWatchEntry.mEditInitialize != null))
                editVal = headListViewItem.mWatchEntry.mEditInitialize;
			editWidget.mExpectingType = headListViewItem.mWatchEntry.mResultTypeStr;
            editWidget.SetText(editVal);
            editWidget.Content.SelectAll();
			editWidget.Content.ClearUndoData();

            float x;
            float y;
            listViewItem.SelfToOtherTranslate(mListView, 0, 0, out x, out y);

            mEditingItem = (WatchListViewItem)listViewItem;

			bool isEmpty = (mEditingItem.mWatchEntry != null) && (mEditingItem.mWatchEntry.mEvalStr.IsEmpty);

			float width;
			if (isEmpty)
			{
				x = GS!(4);
				width = mListView.mScrollContentContainer.mWidth - GS!(8);
			}
			else
			{
				x = listViewItem.LabelX - GS!(4);
				width = listViewItem.LabelWidth + GS!(6);

				if (listViewItem.mColumnIdx == mListView.mColumns.Count - 1)
				    width = mListView.mWidth - x - GS!(20);
				else
				    width = Math.Min(width, mListView.mWidth - x - GS!(20));
			}
            
            //editWidget.Resize(x - GS!(1), y - GS!(3), width, GS!(28));
			editWidget.ResizeAround(x, y, width);
            mListView.AddWidget(editWidget);

            editWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
            editWidget.mOnSubmit.Add(new => HandleRenameSubmit);
            editWidget.mOnCancel.Add(new => HandleRenameCancel);
			editWidget.mOnKeyDown.Add(new => HandleEditKeyDown);
            WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
            editWidget.SetFocus();
        }

        void HandleMouseWheel(MouseEvent evt)
        {
            HandleEditLostFocus(mEditWidget);
        }

        public override void LostFocus()
        {
            base.LostFocus();
			if (mDeselectOnFocusLost)
            	mListView.GetRoot().SelectItemExclusively(null);
        }

		void ValueClicked(ListViewItem item, float x, float y, int32 btnNum)
		{
			if ((btnNum == 1) && (!mDisabled))
			{
				DarkListViewItem widget = (DarkListViewItem)item;
			    float clickX = x;
				float clickY = widget.mHeight + GS!(2);
				widget.SelfToOtherTranslate(mListView.GetRoot(), clickX, clickY, var aX, var aY);
				ShowRightClickMenu(mListView, aX, aY);
			}
		}

        void ListViewItemMouseDown(WatchListViewItem clickedItem, int32 btnCount)
        {
            if (mDisabled)
                return;

            WatchListViewItem item = (WatchListViewItem)clickedItem.GetSubItem(0);

            mListView.GetRoot().SelectItem(item, true);
            SetFocus();

            if ((mIsAuto) && (clickedItem.mColumnIdx == 0))
                return;

            if (clickedItem.mColumnIdx > 1)
                return;

            if (clickedItem.mColumnIdx > 0)
            {
                if (item.mDisabled)
                    return;
            }
            
            if ((btnCount > 1) ||
                ((clickedItem.mLabel.Length == 0) && (clickedItem.mColumnIdx == 0)))
                EditListViewItem(clickedItem);
        }
        
        void ValueMouseDown(ListViewItem item, float x, float y, int32 btnNum, int32 btnCount)
        {
            WatchListViewItem clickedItem = (WatchListViewItem)item;
            if (btnNum == 0)
                ListViewItemMouseDown(clickedItem, btnCount);
			else
			{
				if (!clickedItem.GetSubItem(0).Selected)
					ListViewItemMouseDown(clickedItem, 1);
			}
        }

        void ClearWatchValues(ListViewItem parentListViewItem)
        {
            if (parentListViewItem.mParentItem != null)
            {
                var watch = ((WatchListViewItem)parentListViewItem).mWatchEntry;
                watch.mHasValue = false;
                if (mHadStep)
                    watch.mHadStepCount++;
                if (mClearHadStep)
                    watch.mHadStepCount = 0;
            }

            if (parentListViewItem.mChildItems != null)
            {                
                for (var listViewItem in parentListViewItem.mChildItems)
                {
                    ClearWatchValues(listViewItem);
                }
            }            
        }
        
        void IWatchOwner.UpdateWatch(WatchListViewItem listViewItem)
        {
            if (mDisabled)
                return;

            bool isTopLevel = listViewItem.mParentItem == mListView.GetRoot();
            listViewItem.mErrorStart = 0;
            listViewItem.mErrorEnd = 0;

            var watch = listViewItem.mWatchEntry;
            bool wasNewExpression = watch.mIsNewExpression;
            String val = scope String();
			if (watch.mIsPending)
			{
				IDEApp.sApp.mDebugger.EvaluateContinue(val);
			}
            else if (watch.mEvalStr.Length > 0)
            {
                String evalStr = scope String(watch.mEvalStr);
                if ((watch.mReferenceId != null) && (watch.mReferenceId.StartsWith("0", StringComparison.Ordinal)))
                    evalStr.Append(",refid=", watch.mReferenceId);
                //gApp.DebugEvaluate(watch.mResultTypeStr, evalStr, val, -1, watch.mIsNewExpression, watch.mIsNewExpression);
				//TODO: Why did we have the mResultTypeStr in there?
				DebugManager.EvalExpressionFlags flags = default;
				if (watch.mIsNewExpression)
					flags |= .AllowSideEffects | .AllowCalls;
				gApp.DebugEvaluate(null, evalStr, val, -1, watch.mLanguage, flags);
                watch.mIsNewExpression = false;                
            }
			watch.mIsPending = false;

            var valViews = scope List<StringView>(val.Split('\n'));
            
			var vals = scope List<String>();
			for (var strView in valViews)
				vals.Add(scope:: String(strView));

            var valueSubItem = (WatchListViewItem)listViewItem.GetSubItem(1);
            if (vals[0].StartsWith("!", StringComparison.Ordinal))
            {   
				if ((vals[0] == "!pending") || (vals[0] == "!Not paused"))
				{
					watch.mIsPending = true;
					return;
				}

             	var errStr = scope String(vals[0]);
				errStr.Remove(0);
                bool hadSideEffects = vals[0] == "!sideeffects";				

                if ((!wasNewExpression) && (isTopLevel))
                {
                    if (((!valueSubItem.mFailed) && (watch.mHadValue)) || (hadSideEffects))
                    {
                        watch.mHasValue = true;
                        listViewItem.SetDisabled(true, hadSideEffects);
                        return;
                    }
                }

                listViewItem.SetDisabled(false, false);
                var errorVals = scope List<StringView>(errStr.Split('\t'));
				
                if (errorVals.Count > 1)
                {
                    listViewItem.mErrorStart = int32.Parse(scope String(errorVals[0]));
                    listViewItem.mErrorEnd = listViewItem.mErrorStart + int32.Parse(scope String(errorVals[1])).Get();
                    valueSubItem.Label = errorVals[2];
                }
                else
                    valueSubItem.Label = errorVals[0];
                valueSubItem.mFailed = true;
                watch.mHadStepCount = 0;
            }
            else
            {
                listViewItem.SetDisabled(false, false);
                String newVal = vals[0];

                if (watch.mHadStepCount == 1)
                    valueSubItem.mValueChanged = (newVal != valueSubItem.mLabel) || (valueSubItem.mFailed);
                else if (watch.mHadStepCount > 1)
                    valueSubItem.mValueChanged = false;                
                watch.mHadStepCount = 0;
                valueSubItem.Label = newVal;
                valueSubItem.mFailed = false;
            }

            var typeSubItem = (WatchListViewItem)listViewItem.GetSubItem(2);
            if (vals.Count > 1)
            {
                String.NewOrSet!(watch.mResultTypeStr, vals[1]);
                typeSubItem.Label = vals[1];
            }
            else
            {
                DeleteAndNullify!(watch.mResultTypeStr);
                typeSubItem.Label = "";
            }
            
            int cmdStringCount = Math.Max(0, vals.Count - 2);
            int memberCount = 0;

			watch.mMemoryBreakpointAddr = 0;
            watch.mResultType = WatchResultType.Value;
            watch.mIsDeleted = false;
            watch.mIsAppendAlloc = false;
            watch.mIsStackAlloc = false;
			watch.mLanguage = .NotSet;
            DeleteAndNullify!(watch.mEditInitialize);
			DeleteAndNullify!(watch.mAction);
            watch.mCanEdit = false;
			watch.mAction = null;

			if (listViewItem.mActionButton != null)
			{
				Widget.RemoveAndDelete(listViewItem.mActionButton);
				listViewItem.mActionButton = null;
			}

            WatchSeriesInfo watchSeriesInfo = null;
            for (int32 memberIdx = 0; memberIdx < cmdStringCount; memberIdx++)
            {
                var memberVals = scope List<StringView>(vals[memberIdx + 2].Split('\t'));
				var memberVals0 = scope String(memberVals[0]);

                if (memberVals0.StartsWith(":"))
                {
                    if (memberVals0 == ":canEdit")
                    {
                        watch.mCanEdit = true;
                        if (memberVals.Count > 1)
                            String.NewOrSet!(watch.mEditInitialize, scope String(memberVals[1]));
                    }
                    if (memberVals0 == ":editVal")
                    {                        
                        String.NewOrSet!(watch.mEditInitialize, scope String(memberVals[1]));
                    }
					else if (memberVals0 == ":break")
					{
						watch.mMemoryBreakpointAddr = (int)Int64.Parse(memberVals[1], .HexNumber);
					}
                    else if (memberVals0 == ":sideeffects")
                    {
                        listViewItem.AddRefreshButton();
                        IDEApp.sApp.RefreshWatches();
                        CheckClearDirtyWatches();
                        watch.mHasValue = true;
                    }
                    else if (memberVals0 == ":referenceId")
                    {
                        String.NewOrSet!(watch.mReferenceId, scope String(memberVals[1]));
                    }
                    else if (memberVals0 == ":repeat")
                    {
                        int startIdx = Int.Parse(scope String(memberVals[1]));
                        int count = Int.Parse(scope String(memberVals[2]));
                        int maxShow = Int.Parse(scope String(memberVals[3]));

                        String displayStr = scope String(memberVals[4]);
                        String evalStr = scope String(memberVals[5]);

                        if (count == 0)
                            continue;

                        Object[] formatParams = scope Object[1 + memberVals.Count - 6];
                        formatParams[0] = startIdx;
                        for (int32 i = 6; i < memberVals.Count; i++)
                            formatParams[1 + i - 6] = scope:: String(memberVals[i]);

                        watchSeriesInfo = new WatchSeriesInfo();
                        watchSeriesInfo.mStartMemberIdx = (int32)memberCount;
                        watchSeriesInfo.mDisplayTemplate = new String(displayStr);
                        watchSeriesInfo.mEvalTemplate = new String(evalStr);
                        watchSeriesInfo.mStartIdx = startIdx;
                        watchSeriesInfo.mCount = count;
                        watchSeriesInfo.mShowPageSize = (int32)maxShow;
                        watchSeriesInfo.mShowPages = 1;                        

                        WatchListViewItem memberItem;
                        if (memberCount >= listViewItem.GetChildCount())
                            memberItem = AddWatch("", "", listViewItem);
                        else
                        {
                            memberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount);
                            if (memberItem.mWatchSeriesInfo != null)
                            {
								var prevWatchSeriesInfo = memberItem.mWatchSeriesInfo;
                                watchSeriesInfo.mShowPages = prevWatchSeriesInfo.mShowPages;
                                watchSeriesInfo.mMoreButton = prevWatchSeriesInfo.mMoreButton;
                                watchSeriesInfo.mLessButton = prevWatchSeriesInfo.mLessButton;

								// Only keep the series ID if it's the same display and evaluation -
								//  This keeps expanded items expanded when single stepping, but will cause them to refresh and close
								//  when we are actually evaluating a different value with the same name in a different context
								if ((prevWatchSeriesInfo.mEvalTemplate == watchSeriesInfo.mEvalTemplate) && (prevWatchSeriesInfo.mDisplayTemplate == watchSeriesInfo.mDisplayTemplate))
									watchSeriesInfo.mSeriesFirstVersion = prevWatchSeriesInfo.mSeriesFirstVersion;
								
								memberItem.mWatchSeriesInfo.ReleaseRef();
                            }
                        }
						
                        memberItem.mWatchSeriesInfo = watchSeriesInfo;
						//memberItem.mOwnsWatchSeriesInfo = true;
                        memberItem.mSeriesMemberIdx = 0;                        
                        memberCount++;

                        var memberWatch = ((WatchListViewItem)memberItem).mWatchEntry;
						
						var formattedName = scope String();
						formattedName.AppendF(displayStr, params formatParams);
                        String.NewOrSet!(memberWatch.mName, formattedName);
						var formattedEval = scope String();
						formattedEval.AppendF(evalStr, params formatParams);
                        String.NewOrSet!(memberWatch.mEvalStr, formattedEval);

                        memberItem.Label = memberWatch.mName;

                        if (wasNewExpression)
                        {
                            while (memberCount < listViewItem.GetChildCount())
                            {
                                WatchListViewItem checkMemberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount);
                                if (checkMemberItem.mWatchSeriesInfo == null)
                                    break;
                                listViewItem.RemoveChildItemAt(memberCount);                                
                            }
                        }
                        else
                        {
                            while (memberCount < listViewItem.GetChildCount())
                            {
                                WatchListViewItem checkMemberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount);
                                if (checkMemberItem.mWatchSeriesInfo == null)
                                    break;
								checkMemberItem.mWatchSeriesInfo.ReleaseRef();
								watchSeriesInfo.AddRef();
                                checkMemberItem.mWatchSeriesInfo = watchSeriesInfo;
                                memberCount++;
                            }
                        }

						// We update here to avoid doing a Draw with old series information (such as addresses)
						memberItem.mMustUpdateBeforeEvaluate = true;
                    }                    
                    else if (memberVals0 == ":addrs")
                    {
                        WatchListViewItem memberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount - 1);
                        String.NewOrSet!(memberItem.mWatchSeriesInfo.mAddrs, memberVals[1]);
                        memberItem.mWatchSeriesInfo.mAddrsEntrySize = 1;
                    }
                    else if (memberVals0 == ":addrsEntrySize")
                    {
                        int32 addrsEntrySize = int32.Parse(scope String(memberVals[1]));
                        WatchListViewItem memberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount - 1);
                        memberItem.mWatchSeriesInfo.mAddrsEntrySize = addrsEntrySize;
                    }
                    else if (memberVals0 == ":continuation")
                    {
                        WatchListViewItem memberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount - 1);
                        String.NewOrSet!(memberItem.mWatchSeriesInfo.mContinuationData, memberVals[1]);
                    }
					else if (memberVals0 == ":action")
					{
						watch.mAction = new String(memberVals[1]);
						listViewItem.AddActionButton();
					}
                    else if (memberVals0 == ":language")
					{
						int32 language;
						if (int32.Parse(memberVals[1]) case .Ok(out language))
						{
							watch.mLanguage = (.)language;
						}	
					}
					else if (memberVals0 == ":warn")
					{
						if (wasNewExpression)
						{
							gApp.ShowOutput();
							gApp.OutputLineSmart("WARNING: {0}", memberVals[1]);
						}
					}
					else
                        watch.ParseCmd(memberVals);
                    continue;
                }

                if (memberVals.Count >= 2)
                {
                    WatchListViewItem memberItem;
                    if (memberCount >= listViewItem.GetChildCount())
                    {
                        memberItem = (WatchListViewItem)AddWatch("", "", listViewItem);
                    }
                    else
                        memberItem = (WatchListViewItem)listViewItem.GetChildAtIndex(memberCount);
                    var memberWatch = ((WatchListViewItem)memberItem).mWatchEntry;
					memberWatch.mLanguage = watch.mLanguage;

                    memberItem.mBottomPadding = 0;
					if (memberItem.mWatchSeriesInfo != null)
                    {
						//if (memberItem.mSeriesMemberIdx == 0)
							//delete memberItem.mWatchSeriesInfo;
						//memberItem.mOwnsWatchSeriesInfo = false;
						memberItem.mWatchSeriesInfo.ReleaseRef();
                        memberItem.mWatchSeriesInfo = null;
						memberItem.mSeriesMemberIdx = 0;
					}
                    String.NewOrSet!(memberWatch.mName, memberVals0);
					String evalStr = scope String();
					if (evalStr.AppendF(scope String(memberVals[1]), watch.mEvalStr) case .Err)
					{
						evalStr.Clear();
						evalStr.Append("Format Error: {0}", scope String(memberVals[1]));
					}
                    String.NewOrSet!(memberWatch.mEvalStr, evalStr);
                    memberItem.Label = memberWatch.mName;
                    memberCount++;
                }
            }

            if ((watch.mReferenceId == null) && (watch.mEvalStr.Length > 0) && (!valueSubItem.mFailed))
            {
                // Allocate a referenceId if we didn't get one (was a temporary expression)
                //  Make it start with a zero so it parses but is easy to identify as not-an-identifier
                watch.mReferenceId = new String();
                watch.mReferenceId.AppendF("0{0}", ++WatchListView.sCurDynReferenceId);
            }

            while (listViewItem.GetChildCount() > memberCount)
                listViewItem.RemoveChildItem(listViewItem.GetChildAtIndex(memberCount));

            if ((listViewItem.GetChildCount() == 0) && (listViewItem.mOpenButton != null))
            {
				Widget.RemoveAndDelete(listViewItem.mOpenButton);
                listViewItem.mOpenButton = null;
				DeleteAndNullify!(listViewItem.mChildItems);
            }

            if (watch.mEvalStr.Length == 0)
                watch.mResultType = WatchResultType.None;

            watch.mHasValue = true;
            watch.mHadValue = true;
        }

        public override void AddedToParent()
        {
            base.AddedToParent();
            if (mDisabled)
                mDisabledTicks = 9999;
        }

        public void CheckClearDirtyWatches()
        {
            var debugger = IDEApp.sApp.mDebugger;
            if ((mWatchesDirty) && (debugger.mIsRunning) && (debugger.IsPaused()))
            {
				MarkDirty();
                ClearWatchValues(mListView.GetRoot());
                mWatchesDirty = false;
                mHadStep = false;
                mClearHadStep = false;

                if (mIsAuto)
                {
                    var parentItem = mListView.GetRoot();

                    var app = IDEApp.sApp;
                    String autosStr = scope String();
                    app.mDebugger.GetAutoLocals(app.IsInDisassemblyMode(), autosStr);
                    var autos = scope List<StringView>(autosStr.Split('\n'));

                    int32 curIdx;
                    for (curIdx = 0; curIdx < autos.Count; curIdx++)
                    {
                        String auto = scope String(autos[curIdx]);
						if (auto.IsEmpty)
							continue;
                        bool found = false;

                        while (curIdx < parentItem.GetChildCount())
                        {
                            var listViewItem = parentItem.GetChildAtIndex(curIdx);
                            if (listViewItem.mLabel == auto)
                            {
                                found = true;
                                break;
                            }
                            parentItem.RemoveChildItemAt(curIdx);
                        }

                        if (!found)
                        {
                            var listViewItem = AddWatch(auto);
                            var itemAtIdx = parentItem.GetChildAtIndex(curIdx);
                            if (itemAtIdx != listViewItem)
                            {
                                // Wrong order
                                parentItem.RemoveChildItem(listViewItem);
                                parentItem.InsertChild(listViewItem, itemAtIdx);
                            }
                        }
                    }

                    while (curIdx < mListView.GetRoot().GetChildCount())
                        parentItem.RemoveChildItemAt(curIdx);
                }
            }
        }

        public override void Update()
        {
            base.Update();

            if (mDisabled)
                mDisabledTicks++;

            int childCount = mListView.GetRoot().GetChildCount();
            if ((!mIsAuto) && ((childCount == 0) || (mListView.GetRoot().GetChildAtIndex(childCount - 1).mLabel.Length > 0)))
                AddWatch("");

            CheckClearDirtyWatches();
        }

        public static void AddSelectableMenuItem(Menu menu, String label, bool selected, Action action)
        {
			Debug.AssertNotStack(action);
            Menu menuItem = menu.AddItem(label);
            if (selected)
                menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
            menuItem.mOnMenuItemSelected.Add(new (imenu) => action()  ~ { delete action; });			
        }

        public static void SetDisplayType(String referenceId, DebugManager.IntDisplayType intDisplayType, DebugManager.MmDisplayType mmDisplayType)
        {
            var debugger = IDEApp.sApp.mDebugger;
            debugger.SetDisplayTypes(referenceId, intDisplayType, mmDisplayType);
            IDEApp.sApp.RefreshWatches();
        }

        public static bool AddDisplayTypeMenu(String label, Menu menu, WatchResultType watchResultType, String referenceId, bool includeDefault)
        {
            bool hasInt = watchResultType.HasFlag(WatchResultType.Int);
            bool hasMM128 = watchResultType.HasFlag(WatchResultType.MM128);
            bool canSetFormat = hasInt || hasMM128;

            var debugger = IDEApp.sApp.mDebugger;
            DebugManager.IntDisplayType intDisplayType;
            DebugManager.MmDisplayType mmDisplayType;
            bool foundSpecific = debugger.GetDisplayTypes(referenceId, out intDisplayType, out mmDisplayType);
            if ((referenceId != null) && (!foundSpecific))
            {
                intDisplayType = DebugManager.IntDisplayType.Default;
                mmDisplayType = DebugManager.MmDisplayType.Default;
            }

            if (!canSetFormat)
                return false;
            
            Menu parentItem = menu.AddItem(label);                                
            //anItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Check);
            if (hasInt)
            {
                for (DebugManager.IntDisplayType i = default; i < DebugManager.IntDisplayType.COUNT; i++)
                {
                    if ((i == 0) && (!includeDefault))
                    {
                        if (intDisplayType == 0)
                            intDisplayType = DebugManager.IntDisplayType.Decimal;
                        continue;
                    }

                    var toType = i;
                    AddSelectableMenuItem(parentItem, ToStackString!(i), intDisplayType == i, 
                        new () => SetDisplayType(referenceId, toType, mmDisplayType));
                }
            }

            if (hasMM128)
            {
                for (DebugManager.MmDisplayType i = default; i < DebugManager.MmDisplayType.COUNT; i++)
                {                    
                    var toType = i;
                    AddSelectableMenuItem(parentItem, ToStackString!(i), mmDisplayType == i,
                        new () => SetDisplayType(referenceId, intDisplayType, toType));
                }
            }
            return true;
        }

		public static void ShowRightClickMenu(WatchPanel watchPanel, WatchListView listView, WatchListViewItem listViewItem, float x, float y)
		{
			if (watchPanel != null)
			{
				watchPanel.mDeselectOnFocusLost = false;
				defer:: { watchPanel.mDeselectOnFocusLost = true; }
			}

			Menu menu = new Menu();

			Menu anItem;

			/*if (listViewItem.mParent != mListView.GetRoot())
			{
				anItem = menu.AddItem("Add Watch");
			}*/

			if (listViewItem != null)
			{
				var clickedHoverItem = listViewItem.GetSubItem(0) as HoverWatch.HoverListViewItem;

				var watchEntry = listViewItem.mWatchEntry;
				if (listViewItem.mParentItem != listView.GetRoot())
				{
					anItem = menu.AddItem("Add Watch");
					anItem.mOnMenuItemSelected.Add(new (menu) =>
						{
							String compactEvalStr = scope String();
							CompactChildExpression(listViewItem, compactEvalStr);
							gApp.AddWatch(compactEvalStr);
						});
				}

			    AddDisplayTypeMenu("Default Display", menu, listViewItem.mWatchEntry.mResultType, null, false);

			    //Debug.WriteLine(String.Format("RefType: {0}", watchEntry.mReferenceId));

				if (menu.mItems.Count > 0)
					anItem = menu.AddItem();

				if (watchEntry.mReferenceId != null)
					AddDisplayTypeMenu("Watch Display", menu, listViewItem.mWatchEntry.mResultType, watchEntry.mReferenceId, true);

				if (!watchEntry.IsConstant)
				{
					anItem = menu.AddItem("Break When Value Changes");
					if (watchEntry.mMemoryBreakpointAddr == 0)
					{
						anItem.mOnMenuItemSelected.Add(new (evt) =>
							{
								String evalStr = scope String();
								CompactChildExpression(listViewItem, evalStr);
								evalStr.Insert(0, "&");
								gApp.mBreakpointPanel.CreateMemoryBreakpoint(evalStr);
								gApp.MarkDirty();
							});
					}
					else
					{
						anItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
						anItem.mOnMenuItemSelected.Add(new (evt) =>
							{
								for (int breakIdx < gApp.mDebugger.mBreakpointList.Count)
								{
									let breakpoint = gApp.mDebugger.mBreakpointList[breakIdx];
									if (breakpoint.mMemoryAddress == watchEntry.mMemoryBreakpointAddr)
									{
										gApp.mDebugger.DeleteBreakpoint(breakpoint);
										gApp.RefreshWatches();
										breakIdx--;
									}
								}
							});

						let configItem = menu.AddItem("Configure Breakpoint");
						configItem.mOnMenuItemSelected.Add(new (evt) =>
							{
								for (int breakIdx < gApp.mDebugger.mBreakpointList.Count)
								{
									let breakpoint = gApp.mDebugger.mBreakpointList[breakIdx];
									if (breakpoint.mMemoryAddress == watchEntry.mMemoryBreakpointAddr)
									{
										ConditionDialog dialog = new ConditionDialog();
										dialog.Init(breakpoint);
										dialog.PopupWindow(listView.mWidgetWindow);
									}
								}
							});
					}
				}

				if (watchEntry.mResultType.HasFlag(.Pointer))
				{
					if (int.Parse(watchEntry.mEditInitialize) case .Ok(let addr))
					{
						int threadId;
						gApp.mDebugger.GetStackAllocInfo(addr, out threadId, null);
						if (threadId != 0)
						{
							anItem = menu.AddItem("Find on Stack");
							anItem.mOnMenuItemSelected.Add(new (evt) =>
								{
									int stackIdx = -1;
									gApp.mDebugger.GetStackAllocInfo(addr, let threadId, &stackIdx);
									if (stackIdx != -1)
									{
										gApp.ShowCallstack();
										gApp.mCallStackPanel.SelectCallStackIdx(stackIdx);
									}
									else if (threadId != 0)
									{
										gApp.ShowThreads();
										gApp.mThreadPanel.SelectThreadId(threadId);
									}
								});
						}
					}
				}

				void WithSelected(Action<ListViewItem> func)
				{
					var root = listView.GetRoot();
					root.WithSelectedItems(func, false, true);

					if (clickedHoverItem != null)
						func(clickedHoverItem);
				}

				anItem = menu.AddItem("Copy Value");
				anItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						String selectedText = scope String();
						WithSelected(scope (listViewItem) =>
						    {
						        if (!selectedText.IsEmpty)
								{
									selectedText.Append("\n");
								}
								var subItem = listViewItem.GetSubItem(1);
								selectedText.Append(subItem.Label);
						    });
						gApp.SetClipboardText(selectedText);
					});

				anItem = menu.AddItem("Copy Expression");
				anItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						String selectedText = scope String();
						WithSelected(scope (listViewItem) =>
						    {
								String evalStr = scope String();
								CompactChildExpression((WatchListViewItem)listViewItem, evalStr);
						        if (!selectedText.IsEmpty)
								{
									selectedText.Append("\n");
								}
								selectedText.Append(evalStr);
						    });
						gApp.SetClipboardText(selectedText);
					});

				if (watchPanel != null)
				{
				    anItem = menu.AddItem("Delete Watch");
					anItem.mOnMenuItemSelected.Add(new (evt) =>
						{
							watchPanel.DeleteSelectedItems();
						});
				}
			}

			if (menu.mItems.IsEmpty)
			{
				delete menu;
				return;
			}
			MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
			menuWidget.Init(listView.GetRoot(), x, y);
		}

		protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
		{
			var focusedItem = (WatchListViewItem)mListView.GetRoot().FindFocusedItem();
			ShowRightClickMenu(this, mListView, focusedItem, x, y);
		}

		void ListViewClicked(MouseEvent theEvent)
		{
			if ((theEvent.mBtn == 1) && (!mDisabled))
			{
				float aX, aY;
				aX = theEvent.mX + mListView.GetRoot().mX;
				aY = theEvent.mY + mListView.GetRoot().mY;
				ShowRightClickMenu(mListView, aX, aY);
			}
		}

        void ListViewMouseDown(MouseEvent theEvent)
        {
            mListView.GetRoot().SelectItemExclusively(null);
        }

        public override void KeyChar(char32 theChar)
        {
            base.KeyChar(theChar);
            
            if ((mDisabled) || (theChar < (char8)32))
                return;

            var selectedItem = (WatchListViewItem)mListView.GetRoot().FindFirstSelectedItem();
            if (selectedItem != null)
            {
                ListViewItemMouseDown(selectedItem, 2);
                if (mEditWidget != null)
                    mEditWidget.KeyChar(theChar);
            }
        }

		void DeleteSelectedItems()
		{
			var root = mListView.GetRoot();
			List<ListViewItem> selectedItems = scope List<ListViewItem>();                    
			root.WithSelectedItems(scope (listViewItem) =>
			    {
			        selectedItems.Add(listViewItem);
			    });

			// Go through in reverse, to process children before their parents
			for (int itemIdx = selectedItems.Count - 1; itemIdx >= 0; itemIdx--)
			{
				var selectedItem = selectedItems[itemIdx];
			    if ((selectedItem != null) && (selectedItem.mLabel.Length > 0) && (selectedItem.mParentItem == root))
			    {
			        int idx = root.mChildItems.IndexOf(selectedItem);
			        root.RemoveChildItem(selectedItem);
			        if (idx < root.mChildItems.Count)
			            root.SelectItemExclusively(root.mChildItems[idx]);
			    }
			}
		}

		public void TryRenameItem()
		{
			if (mEditWidget != null)
			{
				if (mEditingItem.mColumnIdx == 0)
				{
					let valueItem = (WatchListViewItem)mEditingItem.GetSubItem(1);
					EditEvent evt = scope .();
					evt.mSender = mEditWidget;
					HandleRenameCancel(evt);
					EditListViewItem(valueItem);
				}
				return;
			}

			var focusedItem = mListView.GetRoot().FindFocusedItem();
			if (focusedItem != null)
			{
			    EditListViewItem((WatchListViewItem)focusedItem);
				if (mEditWidget == null)
				{
					EditListViewItem((WatchListViewItem)focusedItem.GetSubItem(1));
				}
			}
		}

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
			if (keyCode == .Apps)
			{
				ShowRightClickMenu(mListView);
				return;
			}

            base.KeyDown(keyCode, isRepeat);
            mListView.KeyDown(keyCode, isRepeat);
            if (!mIsAuto)
            {
                if (keyCode == KeyCode.F2)
                {
                    TryRenameItem();
                }
                if (keyCode == KeyCode.Delete)
                {
                   	DeleteSelectedItems();
                }
            }
        }
    }
}
