using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy;
using Beefy.gfx;
using Beefy.events;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.geom;
using Beefy.utils;

namespace IDE.ui
{
	class DocumentationParser
	{
		public String mDocString = new String(256) ~ delete _;
		public String mBriefString ~ delete _;
		public Dictionary<String, String> mParamInfo ~ DeleteDictionyAndKeysAndItems!(_);

		public String ShowDocString
		{
			get
			{
				return mBriefString ?? mDocString;
			}
		}

		public this(StringView info)
		{
			bool atLineStart = true;
			bool lineHadStar = false;
			int blockDepth = 0;
			bool queuedSpace = false;
			bool docStringDone = false;
			bool lineHadContent = false;
			String curDocStr = null;

			for (int idx = 0; idx < info.Length; idx++)
			{
				char8 c = info[idx];
				char8 nextC = 0;
				if (idx < info.Length - 1)
					nextC = info[idx + 1];

				if ((c == '/') && (nextC == '*'))
				{
					idx++;
					blockDepth++;

					if ((idx < info.Length - 1) && (info[idx + 1] == '*'))
					{
						idx++;
						if ((idx < info.Length - 1) && (info[idx + 1] == '<'))
							idx++;
					}

					continue;
				}

				if ((c == '*') && (nextC == '/'))
				{
					idx++;
					blockDepth--;
					continue;
				}

				if (c == '\x03') // \n
				{
					if (!lineHadContent)
					{
						if (curDocStr != null)
							curDocStr = null;
						else if (!mDocString.IsEmpty)
							docStringDone = true;
					}	
					queuedSpace = false;
					atLineStart = true;
					lineHadStar = false;
					lineHadContent = false;
					continue;
				}

				if (atLineStart)
				{
					if ((c == '*') && (blockDepth > 0) && (!lineHadStar))
					{
						lineHadStar = false;
						continue;
					}

					if ((c == '/') && (!lineHadStar))
					{
						if ((nextC == '<') && (!queuedSpace))
						{
							idx++;
						}
						// Ignore any amount of '/' strings at a line start
						continue;
					}

					if ((c == '@') || (c == '\\'))
					{
						int pragmaEndPos = info.IndexOf('\x03', idx);
						if (pragmaEndPos == -1)
							pragmaEndPos = info.Length;
						StringView pragma = .(info, idx + 1, pragmaEndPos - idx - 1);
						var splitEnum = pragma.Split(' ');
						if (splitEnum.GetNext() case .Ok(var pragmaName))
						{
							if (pragmaName == "param")
							{
								if (splitEnum.GetNext() case .Ok(var paramName))
								{
									if (mParamInfo == null)
										mParamInfo = new .();
									curDocStr = new String(pragma, splitEnum.MatchPos + 1);
									mParamInfo[new String(paramName)] = curDocStr;
									lineHadContent = true;
								}
							}
							else if (pragmaName == "brief")
							{
								if (mBriefString == null)
									mBriefString = new String(pragma.Length);
								else if (mBriefString != null)
								{
									if (!mBriefString[mBriefString.Length - 1].IsWhiteSpace)
										mBriefString.Append(" ");
								}
								var briefStr = StringView(pragma, splitEnum.MatchPos + 1);
								briefStr.Trim();
								mBriefString.Append(briefStr);
								curDocStr = mBriefString;
								lineHadContent = true;
							}
						}

						idx = pragmaEndPos - 1;
						continue;
					}

					if (c.IsWhiteSpace)
					{
						continue;
					}
					else
					{
						queuedSpace = true;
						atLineStart = false;
					}
				}

				if (c.IsWhiteSpace)
				{
					queuedSpace = true;
					continue;
				}

				if ((curDocStr != null) && (docStringDone))
					continue;

				String docStr = curDocStr ?? mDocString;
				if (queuedSpace)
				{
					if (!docStr.IsEmpty)
					{
						char8 endC = docStr[docStr.Length - 1];
						if (!endC.IsWhiteSpace)
							docStr.Append(" ");
					}
					queuedSpace = false;
				}
				lineHadContent = true;
				docStr.Append(c);
			}
		}
	}

    public class AutoComplete
    {
        public class AutoCompleteContent : ScrollableWidget
        {
            public AutoComplete mAutoComplete;
            public bool mIsInitted;
            public bool mIgnoreMove;
			public bool mOwnsWindow;
			public float mRightBoxAdjust;

            public this(AutoComplete autoComplete)
            {
                mAutoComplete = autoComplete;
            }

			public ~this()
			{
			    if (mIsInitted)
			        Cleanup();
			}

            void LostFocusHandler(BFWindow window, BFWindow newFocus)
            {
				if (gApp.mRunningTestScript)
					return;

                if ((newFocus != mWidgetWindow) && (newFocus != mAutoComplete.mTargetEditWidget.mWidgetWindow))
                    mAutoComplete.Close();
            }

            void HandleWindowMoved(BFWindow window)
            {
				if (gApp.mRunningTestScript)
					return;

				if ((mWidgetWindow == null) || (mWidgetWindow.mRootWidget != this))
					return; // We're being replaced as root

				if (let widgetWindow = window as WidgetWindow)
				{
					if (widgetWindow.mRootWidget is DarkTooltipContainer)
						return;
				}

                if ((!mIgnoreMove) && (mWidgetWindow != null) && (!mWidgetWindow.mHasClosed))
                    mAutoComplete.Close();
            }

            public void Init()
            {
                Debug.Assert(!mIsInitted);
                mIsInitted = true;

                //Console.WriteLine("AutoCompleteContent Init");

				if (mOwnsWindow)
				{
	                WidgetWindow.sOnWindowLostFocus.Add(new => LostFocusHandler);
	                WidgetWindow.sOnMouseDown.Add(new => HandleMouseDown);
	                WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
	                WidgetWindow.sOnWindowMoved.Add(new => HandleWindowMoved);
	                WidgetWindow.sOnMenuItemSelected.Add(new => HandleSysMenuItemSelected);
				}
            }

            void HandleMouseWheel(MouseEvent evt)
            {
				if (gApp.mRunningTestScript)
					return;
				if (mWidgetWindow == null)
					return;

                WidgetWindow widgetWindow = (WidgetWindow)evt.mSender;
                if (!(widgetWindow.mRootWidget is AutoCompleteContent))
                {
                    float mouseScreenX = widgetWindow.mClientX + evt.mX;
                    float mouseScreenY = widgetWindow.mClientY + evt.mY;
                    let windowRect = Rect(mWidgetWindow.mX, mWidgetWindow.mY, mWidgetWindow.mWindowWidth, mWidgetWindow.mWindowHeight);
                    if (windowRect.Contains(mouseScreenX, mouseScreenY))
                    {
                        MouseWheel(evt.mX - mWidgetWindow.mX, evt.mY - mWidgetWindow.mY, evt.mWheelDelta);
                        evt.mHandled = true;
                    }
                    else
                        mAutoComplete.Close();                    
                }
            }

            void HandleMouseDown(MouseEvent evt)
            {
                WidgetWindow widgetWindow = (WidgetWindow)evt.mSender;
                if (!(widgetWindow.mRootWidget is AutoCompleteContent))
                    mAutoComplete.Close();
            }

            void HandleSysMenuItemSelected(IMenu sysMenu)
            {
                mAutoComplete.Close();
            }

            public void Cleanup()
            {
				if (!mIsInitted)
					return;

                //Console.WriteLine("AutoCompleteContent Dispose");
				if (mOwnsWindow)
				{
	                WidgetWindow.sOnWindowLostFocus.Remove(scope => LostFocusHandler, true);
	                WidgetWindow.sOnMouseDown.Remove(scope => HandleMouseDown, true);
	                WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
	                WidgetWindow.sOnWindowMoved.Remove(scope => HandleWindowMoved, true);
	                WidgetWindow.sOnMenuItemSelected.Remove(scope => HandleSysMenuItemSelected, true);
	                mIsInitted = false;
				}
            }

            public override void Draw(Graphics g)
            {
                base.Draw(g);

				if (mOwnsWindow)
				{
	                using (g.PushColor(0x80000000))
	                    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropShadow), GS!(2), GS!(2), mWidth - GS!(2) - mRightBoxAdjust, mHeight - GS!(2));

	                base.Draw(g);
	                using (g.PushColor(0xFFFFFFFF))
	                    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth - GS!(8) - mRightBoxAdjust, mHeight - GS!(8));
				}

                g.SetFont(IDEApp.sApp.mCodeFont);

				/*using (g.PushColor(0x80FF0000))
					g.FillRect(0, 0, mWidth, mHeight);*/
            }
        }

        public class AutoCompleteListWidget : AutoCompleteContent
        {
			BumpAllocator mAlloc = new BumpAllocator(.Ignore) ~ delete _;
			public List<EntryWidget> mFullEntryList = new List<EntryWidget>() ~ delete _;
			public List<EntryWidget> mEntryList = mFullEntryList;
			public float mItemSpacing = GS!(18);
			public int32 mSelectIdx = -1;
			public float mMaxWidth;
			public float mDocWidth;
			public int mDocumentationDelay = -1;

			public ~this()
			{
				if (mEntryList != mFullEntryList)
					delete mEntryList;
			}

            public override void MouseEnter()
            {
                base.MouseEnter();
            }

            public class EntryWidget
            {
				public int32 mShowIdx;
                public AutoCompleteListWidget mAutoCompleteListWidget;
                public String mEntryType;
                public String mEntryDisplay;
                public String mEntryInsert;
				public String mDocumentation;
                public Image mIcon;

				public float Y
				{
					get
					{
						return GS!(6) + mShowIdx * mAutoCompleteListWidget.mItemSpacing;
					}
				}

				public ~this()
				{

				}

                public void Draw(Graphics g)
                {
                    if (mIcon != null)
                        g.Draw(mIcon, 0, 0);

                    g.SetFont(IDEApp.sApp.mCodeFont);
                    g.DrawString(mEntryDisplay, GS!(20), 0);
                }                
            }

            class Content : Widget
            {
                AutoCompleteListWidget mAutoCompleteListWidget;

                public this(AutoCompleteListWidget autoCompleteListWidget)
                {
                    mAutoCompleteListWidget = autoCompleteListWidget;
                }

                public override void Draw(Graphics g)
                {
                    base.Draw(g);

					float absX;
					float absY;
					mParent.SelfToRootTranslate(0, 0, out absX, out absY);

					float scrollPos = -g.mMatrix.ty + absY;
					int32 startIdx = (int32)(scrollPos / mAutoCompleteListWidget.mItemSpacing);
					int32 endIdx = Math.Min((int32)((scrollPos + mAutoCompleteListWidget.mHeight)/ mAutoCompleteListWidget.mItemSpacing) + 1, (int32)mAutoCompleteListWidget.mEntryList.Count);

                    for (int32 itemIdx = startIdx; itemIdx < endIdx; itemIdx++)
                    {
                        var entry = (EntryWidget)mAutoCompleteListWidget.mEntryList[itemIdx];

						float curY = entry.Y;
                        using (g.PushTranslate(4, curY))
							entry.Draw(g);
                    }

					if (mAutoCompleteListWidget.mSelectIdx != -1)
					{
						var selectedEntry = mAutoCompleteListWidget.mEntryList[mAutoCompleteListWidget.mSelectIdx];
						using (g.PushColor(DarkTheme.COLOR_MENU_FOCUSED))
						{
							let dispWidth = g.mFont.GetWidth(selectedEntry.mEntryDisplay) + GS!(24);

						    float width = mWidth - GS!(16) - mAutoCompleteListWidget.mRightBoxAdjust;
						    if (mAutoCompleteListWidget.mVertScrollbar != null)
						        width -= GS!(18);
							width = Math.Max(dispWidth, width);

						    g.DrawButton(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MenuSelect), GS!(4), selectedEntry.Y - GS!(2), width);
						}
					}
                }

				public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
				{
					base.MouseDown(x, y, btn, btnCount);

					int32 idx = (int32)((y - GS!(4.0f)) / mAutoCompleteListWidget.mItemSpacing);
					if ((idx >= 0) && (idx < mAutoCompleteListWidget.mEntryList.Count))
					{
						mAutoCompleteListWidget.Select(idx);
						if (!mAutoCompleteListWidget.mOwnsWindow)
							mAutoCompleteListWidget.SetFocus();
						if (btnCount > 1)
						{
						    mAutoCompleteListWidget.mAutoComplete.InsertSelection((char8)0);
						    mAutoCompleteListWidget.mAutoComplete.Close();
						}
					}
				}
            }

            public this(AutoComplete autoComplete) : base(autoComplete)
            {
                mScrollContent = new Content(this);
                mScrollContentContainer.AddWidget(mScrollContent);                
            }

			public void UpdateWidth()
			{
				int firstEntry = (int)(-(int)mScrollContent.mY / mItemSpacing);
				int lastEntry = (int)((-(int)mScrollContent.mY + mScrollContentContainer.mHeight) / mItemSpacing);

				if (mScrollContentContainer.mHeight == 0)
				{
					firstEntry = Math.Max(mSelectIdx - 3, 0);
					lastEntry = mSelectIdx + 7;
				}

				lastEntry = Math.Min(lastEntry, mEntryList.Count);

				float prevMaxWidth = mMaxWidth;
				var font = IDEApp.sApp.mCodeFont;

				for (int i = firstEntry; i < lastEntry; i++)
				{
					var entry = mEntryList[i];
					float entryWidth = font.GetWidth(entry.mEntryDisplay) + GS!(32);
					mMaxWidth = Math.Max(mMaxWidth, entryWidth);
				}


				float docWidth = 0.0f;
				if ((mSelectIdx != -1) && (mSelectIdx < mEntryList.Count))
				{
					let selectedEntry = mEntryList[mSelectIdx];
					if (selectedEntry.mDocumentation != null)
					{
						DocumentationParser docParser = scope DocumentationParser(selectedEntry.mDocumentation);
						var showDocString = docParser.ShowDocString;
						docWidth = font.GetWidth(showDocString) + GS!(24);
					}
				}

				if ((mOwnsWindow) && ((prevMaxWidth != mMaxWidth) || (docWidth != mDocWidth)) && (mWidgetWindow != null))
				{
					mDocWidth = docWidth;
					mRightBoxAdjust = docWidth;
					int32 windowWidth = (int32)mMaxWidth;
					windowWidth += (.)mDocWidth;
					windowWidth += GS!(16);

					if (mVertScrollbar != null)
					{
						windowWidth += GS!(12);
					}

					mIgnoreMove = true;
					if (mAutoComplete.mInvokeWidget != null)
						mAutoComplete.mInvokeWidget.mIgnoreMove = true;
					mWidgetWindow.Resize(mWidgetWindow.mX, mWidgetWindow.mY, windowWidth, mWidgetWindow.mWindowHeight);
					mScrollContent.mWidth = mWidth;
					//Resize(0, 0, mWidgetWindow.mClientWidth, mWidgetWindow.mClientHeight);
					mIgnoreMove = false;
					if (mAutoComplete.mInvokeWidget != null)
						mAutoComplete.mInvokeWidget.mIgnoreMove = false;
					ResizeContent(-1, -1, mVertScrollbar != null);
				}
			}

            public void UpdateEntry(EntryWidget entry, int showIdx)
            {
                if (showIdx == -1)
                {
                    //entry.mVisible = false;
					entry.mShowIdx = (int32)showIdx;
                    return;
                }

                //entry.mVisible = true;
                //entry.Resize(GS!(4), GS!(6) + showIdx * mItemSpacing, int32.MaxValue, mItemSpacing);
				entry.mShowIdx = (int32)showIdx;

				/*if (showIdx < 10)
				{
	                var font = IDEApp.sApp.mCodeFont;
	                float entryWidth = font.GetWidth(entry.mEntryDisplay) + GS!(32);
	                mMaxWidth = Math.Max(mMaxWidth, entryWidth);
				}*/
            }

            public void AddEntry(StringView entryType, StringView entryDisplay, Image icon, StringView entryInsert = default, StringView documentation = default)
            {                
                var entryWidget = new:mAlloc EntryWidget();
                entryWidget.mAutoCompleteListWidget = this;
                entryWidget.mEntryType = new:mAlloc String(entryType);
                entryWidget.mEntryDisplay = new:mAlloc String(entryDisplay);
				if (!entryInsert.IsEmpty)
                	entryWidget.mEntryInsert = new:mAlloc String(entryInsert);
				if (!documentation.IsEmpty)
					entryWidget.mDocumentation = new:mAlloc String(documentation);
                entryWidget.mIcon = icon;

                UpdateEntry(entryWidget, mEntryList.Count);
                mEntryList.Add(entryWidget);
                //mScrollContent.AddWidget(entryWidget);
            }            

            public void EnsureSelectionVisible()
            {
                if (mVertScrollbar != null)
				{
					float extraSpacing = mOwnsWindow ? 0 : GS!(4);

					//int numItemsVisible = (int)((mVertScrollbar.mPageSize - GS!(6)) / mItemSpacing);
					//float usableHeight = numItemsVisible * mItemSpacing;
					float usableHeight = (float)mVertScrollbar.mPageSize;

					float height = mItemSpacing;
	                var selectItem = mEntryList[mSelectIdx];
	                if (selectItem.Y - extraSpacing < mVertScrollbar.mContentPos)
	                    mVertScrollbar.ScrollTo(selectItem.Y - extraSpacing);
	                if (selectItem.Y + height > mVertScrollbar.mContentPos + usableHeight)
	                    mVertScrollbar.ScrollTo(selectItem.Y + height - usableHeight);
				}
				UpdateWidth();
            }

            public void CenterSelection()
            {
				if (mSelectIdx == -1)
					return;
                if (mVertScrollbar == null)
                    return;

                var selectItem = mEntryList[mSelectIdx];
                VertScrollTo(selectItem.Y + mItemSpacing - mVertScrollbar.mPageSize / 2, true);
				UpdateWidth();
            }

            public void Select(int32 idx)
            {
				if (mSelectIdx == idx)
					return;

				MarkDirty();
                mSelectIdx = idx;
				if ((gApp.mSettings.mEditorSettings.mAutoCompleteShowDocumentation) && (!mAutoComplete.mIsDocumentationPass))
				{
					// Show faster when we have a panel to show within
					mDocumentationDelay = mOwnsWindow ? 40 : 20;
				}
                EnsureSelectionVisible();
            }

            public void SelectDirection(int32 dir)
            {
                int32 newSelection = mSelectIdx + dir;
                if ((newSelection >= 0) && (newSelection < mEntryList.Count))
                {
                    if (mEntryList[newSelection].mShowIdx != -1)
                        Select(newSelection);
                }
            }

            public override void ScrollPositionChanged()
            {
                if (mVertScrollbar != null)
                {
                    //mVertScrollbar.mContentPos = (float)Math.Round(mVertScrollbar.mContentPos / mItemSpacing) * mItemSpacing;
                    
                }

                base.ScrollPositionChanged();
            }

			public override void Draw(Graphics g)
			{
				base.Draw(g);
				if (mSelectIdx != -1)
				{
					let selectedEntry = mEntryList[mSelectIdx];
					if ((selectedEntry.mDocumentation != null) && (mDocumentationDelay <= 0))
					{
						DocumentationParser docParser = scope .(selectedEntry.mDocumentation);
						if (mOwnsWindow)
						{
							if (mDocWidth > 0)
							{
								float drawX = mWidth - mDocWidth - GS!(6);
								float drawY = GS!(4);
								float drawHeight = GS!(32);
								
							    using (g.PushColor(0x80000000))
							        g.DrawBox(DarkTheme.sDarkTheme.GetImage(.DropShadow), drawX + GS!(2), drawY + GS!(2), mRightBoxAdjust - GS!(2), drawHeight - GS!(2));

							    using (g.PushColor(0xFFFFFFFF))
							        g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Menu), drawX, drawY, mRightBoxAdjust - GS!(8), drawHeight - GS!(8));

								using (g.PushColor(0xFFC0C0C0))
									g.DrawString(docParser.ShowDocString, drawX + GS!(8), drawY + GS!(4), .Left, mDocWidth, .Ellipsis);
							}
						}
						else
						{
							/*float drawX = GS!(8);
							float drawY = mHeight + GS!(2);
							using (g.PushColor(0xFFC0C0C0))
								g.DrawString(docParser.ShowDocString, drawX, drawY, .Left, mWidth - drawX, .Wrap);*/
						}
					}
				}
			}

			public override void DrawAll(Graphics g)
			{
				base.DrawAll(g);
				/*using (g.PushColor(0x20FF0000))
					g.FillRect(0, 0, mWidth, mHeight);*/
			}

			public override void Update()
			{
				base.Update();
				if (mDocumentationDelay > 0)
					--mDocumentationDelay;
			}

            public void ResizeContent(int32 width, int32 height, bool wantScrollbar)
            {
                InitScrollbars(false, wantScrollbar);
                if ((wantScrollbar) && (mOwnsWindow))
                {
                    mVertScrollbar.mScrollIncrement = mItemSpacing;
                    mVertScrollbar.mAlignItems = true;
                }
				if (mOwnsWindow)
				{
	                mScrollbarInsets.mTop = GS!(2);
	                mScrollbarInsets.mBottom = GS!(10);
	                mScrollbarInsets.mRight = GS!(10) + mRightBoxAdjust;
				}
				else
				{
					mScrollbarInsets.mTop = GS!(2);
					mScrollbarInsets.mBottom = GS!(2);
					mScrollbarInsets.mRight = GS!(2);
				}

				if (width != -1)
				{
	                mScrollContent.mWidth = width /*- mRightBoxAdjust*/;
	                mScrollContent.mHeight = height;
				}
                UpdateScrollbars();
            }

			public override void RehupScale(float oldScale, float newScale)
			{
				base.RehupScale(oldScale, newScale);
				mAutoComplete.Close();
			}

			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				base.KeyDown(keyCode, isRepeat);

				switch (keyCode)
				{
				case .Up:
					SelectDirection(-1);
				case .Down:
					SelectDirection(1);
				default:
				}
			}
        }

        public class InvokeWidget : AutoCompleteContent
        {
            public class Entry
            {
                public String mText ~ delete _;
				public String mDocumentation ~ delete _;

				public int GetParamCount()
				{
					int  splitCount = 0;
					for (int32 i = 0; i < mText.Length; i++)
					{
						char8 c = mText[i];
					    if (c == '\x01')
						{
					        splitCount++;
							i++;
						}
					}
					return splitCount - 1;
				}

				public bool HasParamsParam()
				{
					return mText.Contains("\x01params ");
				}
            }

            public List<Entry> mEntryList = new List<Entry>() ~ DeleteContainerAndItems!(_);
            public int32 mSelectIdx;
            public float mMaxWidth;
            public int32 mLeftParenIdx;
            public bool mIsAboveText;

            public this(AutoComplete autoComplete)
                : base(autoComplete)
            {

            }

			public ~this()
			{
			}

            public void AddEntry(Entry entry)
            {
                mEntryList.Add(entry);

                var font = IDEApp.sApp.mCodeFont;
				String checkString = scope String(entry.mText);
				checkString.Replace("\x01", "");
                float entryWidth = font.GetWidth(checkString) + GS!(32);
                mMaxWidth = Math.Max(mMaxWidth, entryWidth);
            }

			public void ResizeContent(bool resizeWindow)
			{
				if (mOwnsWindow)
				{
					int workspaceX;
					int workspaceY;
					int workspaceWidth;
					int workspaceHeight;
					BFApp.sApp.GetWorkspaceRect(out workspaceX, out workspaceY, out workspaceWidth, out workspaceHeight);
					mWidth = workspaceWidth;
				}
				else
					mWidth = gApp.mAutoCompletePanel.mWidth;

				float extWidth;
				float extHeight;
				DrawInfo(null, out extWidth, out extHeight);

				mWidth = extWidth;
				mHeight = extHeight;

				if (resizeWindow)
				{
					if (mOwnsWindow)
					{
						mIgnoreMove = true;
						mAutoComplete.UpdateWindow(ref mWidgetWindow, this, mAutoComplete.mInvokeSrcPositions[0], (int32)extWidth, (int32)extHeight);
						mIgnoreMove = false;
					}
					else
					{
						gApp.mAutoCompletePanel.ResizeComponents();
					}
				}
			}

            public void Select(int32 idx)
            {
                mSelectIdx = idx;
				ResizeContent(mWidgetWindow != null);
            }

            public new void Init()
            {
                base.Init();

                if (mSelectIdx >= mEntryList.Count)
                    mSelectIdx = 0;
            }

            public void SelectDirection(int32 dir)
            {
                int32 newSelection = mSelectIdx + dir;
                if ((newSelection >= 0) && (newSelection < mEntryList.Count))
                    Select(newSelection);
            }

            public override void Update()
            {
                base.Update();

            }

			void DrawInfo(Graphics g, out float extWidth, out float extHeight)
			{
				var font = IDEApp.sApp.mCodeFont;

				extWidth = 0;

				float curX = GS!(8);
				float curY = GS!(5);

				if (mEntryList.Count > 1)
				{
					String numStr = scope String();
					numStr.AppendF("{0}/{1}", mSelectIdx + 1, mEntryList.Count);
					if (g != null)
					{
					    using (g.PushColor(0xFFB0B0B0))
					        g.DrawString(numStr, curX, curY);
					}
					curX += font.GetWidth(numStr) + GS!(8);
				}

				var selectedEntry = mEntryList[mSelectIdx];
				
				float maxWidth = mWidth;

				StringView paramName = .();
				List<StringView> textSections = scope List<StringView>(selectedEntry.mText.Split('\x01'));

				int cursorPos = mAutoComplete.mTargetEditWidget.Content.CursorTextPos;
				int cursorSection = -1;
				for (int sectionIdx = 0; sectionIdx < mAutoComplete.mInvokeSrcPositions.Count - 1; sectionIdx++)
				{
				    if (cursorPos > mAutoComplete.mInvokeSrcPositions[sectionIdx])
				        cursorSection = sectionIdx + 1;
				}

				// Just show last section hilighted even if we have too many params.
				//  This accounts for variadic cases
				if (cursorSection >= textSections.Count - 1)
				    cursorSection = textSections.Count - 2;

				float paramX = 0;
				for (int sectionIdx = 0; sectionIdx < textSections.Count; sectionIdx++)
				{
				    bool isParam = (sectionIdx > 0) && (sectionIdx < textSections.Count - 1);
					if ((isParam) && (paramX == 0))
						paramX = curX;

					StringView sectionStr = .(textSections[sectionIdx]);

					float sectionWidth = font.GetWidth(sectionStr);
					if (curX + sectionWidth > maxWidth)
					{
						curX = paramX;
						curY += font.GetLineSpacing();
						while (sectionStr.StartsWith(" "))
							sectionStr.RemoveFromStart(1);
					}
					if (sectionIdx == cursorSection)
					{
						int lastSpace = sectionStr.LastIndexOf(' ');
						if (lastSpace != -1)
						{
							paramName = .(sectionStr, lastSpace + 1);
							if (paramName.EndsWith(','))
								paramName.RemoveFromEnd(1);
						}
					}

					if (g != null)
					{
						using (g.PushColor(((sectionIdx == cursorSection) && (isParam)) ? 0xFFB0B0FF : 0xFFFFFFFF))
							g.DrawString(sectionStr, curX, curY);
					}
			        curX += sectionWidth;

					extWidth = Math.Max(extWidth, curX);
				}

				extWidth += GS!(16);
				extHeight = curY + font.GetLineSpacing() + GS!(16);

				if ((selectedEntry.mDocumentation != null) && (gApp.mSettings.mEditorSettings.mAutoCompleteShowDocumentation))
				{
					DocumentationParser docParser = scope .(selectedEntry.mDocumentation);
					var docString = docParser.mBriefString ?? docParser.mDocString;

					curX = GS!(32);
					curY += font.GetLineSpacing() + GS!(4);
					if (g != null)
					{
						using (g.PushColor(0xFFC0C0C0))
							g.DrawString(docString, curX, curY, .Left, mWidth, .Ellipsis);
					}
					extWidth = Math.Max(extWidth, font.GetWidth(docString) + GS!(48));
					extHeight += font.GetLineSpacing() + GS!(4);

					if (docParser.mParamInfo != null)
					{
						if (docParser.mParamInfo.TryGetValue(scope String(paramName), var paramDoc))
						{
							curY += font.GetLineSpacing() + GS!(4);
							if (g != null)
							{
								using (g.PushColor(0xFFFFFFFF))
								{
									g.DrawString(scope String(paramName.Length + 1)..AppendF("{0}:", paramName), curX, curY, .Left, mWidth, .Ellipsis);
								}

								using (g.PushColor(0xFFC0C0C0))
								{
									g.DrawString(paramDoc, curX + font.GetWidth(paramName) + font.GetWidth(": "), curY, .Left, mWidth, .Ellipsis);
								}
							}
						}

						for (var paramDocKV in docParser.mParamInfo)
						{
							extWidth = Math.Max(extWidth, font.GetWidth(paramDocKV.key) + font.GetWidth(": ") + font.GetWidth(paramDocKV.value) + GS!(48));
						}
						extHeight += font.GetLineSpacing() + GS!(4);
					}
				}
			}

            public override void Draw(Beefy.gfx.Graphics g)
            {
                base.Draw(g);
                
				float extWidth;
				float extHeight;
				DrawInfo(g, out extWidth, out extHeight);
            }

			public override void RehupScale(float oldScale, float newScale)
			{
				base.RehupScale(oldScale, newScale);
				mAutoComplete.Close();
			}
        }

        public EditWidget mTargetEditWidget;
        public Event<Action> mOnAutoCompleteInserted ~ _.Dispose();
        public Event<Action> mOnClosed ~ _.Dispose();
        public WidgetWindow mListWindow;
        public AutoCompleteListWidget mAutoCompleteListWidget;
        public WidgetWindow mInvokeWindow;
        public InvokeWidget mInvokeWidget;
        public List<InvokeWidget> mInvokeStack = new List<InvokeWidget>() ~ delete _; // Previous invokes (from async)
        public int32 mInsertStartIdx = -1;
        public int32 mInsertEndIdx = -1;
		public String mInfoFilter ~ delete _;
        public List<int32> mInvokeSrcPositions ~ delete _;
        public static int32 sAutoCompleteIdx = 1;
        public static Dictionary<String, int32> sAutoCompleteMRU = new Dictionary<String, int32>() ~ delete _;
        public bool mIsAsync = true;
        public bool mIsMember;        
		public bool mIsFixit;
		public bool mInvokeOnly;
		public bool mUncertain;
		public bool mIsDocumentationPass;
		public bool mIsUserRequested;

		bool mClosed;
		bool mPopulating;
		float mWantX;
		float mWantY;

        public this(EditWidget targetEditWidget)
        {
            mTargetEditWidget = targetEditWidget;
        }

		static ~this()
		{
			for (var key in sAutoCompleteMRU.Keys)
				delete key;
		}

        public void UpdateWindow(ref WidgetWindow widgetWindow, Widget rootWidget, int textIdx, int width, int height)
        {
			var textIdx;

			// This makes typing '..' NOT move the window after pressing the second '.'
			if (mTargetEditWidget.Content.SafeGetChar(textIdx - 2) == '.')
			{
				textIdx--;
			}

            Debug.Assert(textIdx >= 0);
            int line = 0;
            int column = 0;
            if (textIdx >= 0)
                mTargetEditWidget.Content.GetLineCharAtIdx(textIdx, out line, out column);

            float x;
            float y;
            mTargetEditWidget.Content.GetTextCoordAtLineChar(line, column, out x, out y);            

			mTargetEditWidget.Content.GetTextCoordAtCursor(var cursorX, var cursorY);
			y = Math.Max(y, cursorY);

            float screenX;
            float screenY;
            mTargetEditWidget.Content.SelfToRootTranslate(x, y, out screenX, out screenY);
			
            /// 

            /*if ((mInvokeSrcPositions != null) && (mInvokeSrcPositions.Count > 0))
            {
                textIdx = mInvokeSrcPositions[mInvokeSrcPositions.Count - 1];
                mTargetEditWidget.Content.GetLineCharAtIdx(textIdx, out line, out column);                
                mTargetEditWidget.Content.GetTextCoordAtLineChar(line, column, out x, out y);

                float endScreenX;
                float endScreenY;
                mTargetEditWidget.Content.SelfToRootTranslate(x, y, out endScreenX, out endScreenY);

                screenY = endScreenY;
            }*/

            ///

			int screenWidth = width;
			int screenHeight = height;

            screenX += mTargetEditWidget.mWidgetWindow.mClientX;
            screenY += mTargetEditWidget.mWidgetWindow.mClientY;
            screenX -= GS!(24);
            screenY += GS!(20);

			float startScreenY = screenY;
            if (rootWidget == mInvokeWidget)
            {
                if (mInvokeWidget.mIsAboveText)
                    screenY -= height + GS!(16);
            }

            //TODO: Do better positioning
            if ((mInvokeWindow != null) && (widgetWindow == mListWindow))
            {
                if (!mInvokeWidget.mIsAboveText)
                    screenY += mInvokeWindow.mWindowHeight - 6;
            }

			mWantX = screenX;
			mWantY = screenY;

			int workspaceX;
			int workspaceY;
			int workspaceWidth;
			int workspaceHeight;
			BFApp.sApp.GetWorkspaceRect(out workspaceX, out workspaceY, out workspaceWidth, out workspaceHeight);
			if (screenX + width > workspaceWidth)
				screenX = workspaceWidth - width;
			if (screenX < 0)
				screenX = 0;

			if (rootWidget == mAutoCompleteListWidget)
			{
				// May clip of bottom?
				if (screenY + GetMaxWindowHeight() >= workspaceHeight)
				{
					screenY = startScreenY - (height + GS!(16));
				}
			}

			/*if (width > workspaceWidth)
			{
				screenWidth = workspaceWidth;
				var font = IDEApp.sApp.mCodeFont;
				//font.GetWrapHeight()
			}*/

            if (widgetWindow == null)
            {

                BFWindow.Flags windowFlags = BFWindow.Flags.ClientSized | BFWindow.Flags.PopupPosition | BFWindow.Flags.NoActivate | BFWindow.Flags.NoMouseActivate | BFWindow.Flags.DestAlpha;
                widgetWindow = new WidgetWindow(mTargetEditWidget.mWidgetWindow,
                    "Autocomplete",
                    (int32)screenX, (int32)screenY,
                    screenWidth, screenHeight,
                    windowFlags,
                    rootWidget);
            }
            else 
            {
                if (widgetWindow.mRootWidget != rootWidget)
				{
					var prevRoot = widgetWindow.mRootWidget;
					//Debug.WriteLine("Setting window {0} to root {1} from root {2}", widgetWindow, rootWidget, prevRoot);
                    widgetWindow.SetRootWidget(rootWidget);
					delete prevRoot;
				}
                widgetWindow.Resize((int)screenX, (int)screenY, width, height);
            }
        }        

        public void UpdateAsyncInfo()
        {
            GetAsyncTextPos();
            UpdateData(null, true);
        }

        public void Update()
        {
            if ((mInvokeWindow != null) && (!mInvokeWidget.mIsAboveText))
            {
                int textIdx = mTargetEditWidget.Content.CursorTextPos;
                int line = 0;
                int column = 0;
                if (textIdx >= 0)
                    mTargetEditWidget.Content.GetLineCharAtIdx(textIdx, out line, out column);
                float x;
                float y;
                mTargetEditWidget.Content.GetTextCoordAtLineChar(line, column, out x, out y);

                float screenX;
                float screenY;
                mTargetEditWidget.Content.SelfToRootTranslate(x, y, out screenX, out screenY);

                screenX += mTargetEditWidget.mWidgetWindow.mClientX;
                screenY += mTargetEditWidget.mWidgetWindow.mClientY;

                if (screenY >= mInvokeWindow.mY - 8)
                {
                    mInvokeWidget.mIgnoreMove = true;
                    if (mListWindow != null)
                        mAutoCompleteListWidget.mIgnoreMove = true;
                    mInvokeWidget.mIsAboveText = true;
					mInvokeWidget.ResizeContent(false);
                    UpdateWindow(ref mInvokeWindow, mInvokeWidget, mInvokeSrcPositions[0], (int32)mInvokeWidget.mWidth, (int32)mInvokeWidget.mHeight);                    
                    if (mListWindow != null)
                    {
                        UpdateWindow(ref mListWindow, mAutoCompleteListWidget, mInsertStartIdx, mListWindow.mWindowWidth, mListWindow.mWindowHeight);
                        mAutoCompleteListWidget.mIgnoreMove = false;
                    }
                    mInvokeWidget.mIgnoreMove = false;
                }
            }

			if (mAutoCompleteListWidget != null)
				mAutoCompleteListWidget.UpdateWidth();

			if ((IsShowing()) && (!IsInPanel()))
			{
				bool hasFocus = false;
				if ((mListWindow != null) && (mListWindow.mHasFocus))
					hasFocus = true;
				if (mTargetEditWidget.mHasFocus)
					hasFocus = true;
				if (!hasFocus)
				{
					Close();
				}
			}
        }

		public void GetFilter(String outFilter)
		{
			if ((mInsertEndIdx != -1) && (mInsertStartIdx != -1))
			{
				mTargetEditWidget.Content.ExtractString(mInsertStartIdx, Math.Max(mInsertEndIdx - mInsertStartIdx, 0), outFilter);
			}
		}

        public void GetAsyncTextPos()
        {
			//Debug.WriteLine("GetAsyncTextPos start {0} {1}", mInsertStartIdx, mInsertEndIdx);

            mInsertEndIdx = (int32)mTargetEditWidget.Content.CursorTextPos;
			while ((mInsertStartIdx != -1) && (mInsertStartIdx < mInsertEndIdx))
			{
				char8 c = (char8)mTargetEditWidget.Content.mData.mText[mInsertStartIdx].mChar;
				if ((c != ' ') && (c != ','))
				    break;
				mInsertStartIdx++;
			}

            /*mInsertStartIdx = mInsertEndIdx;
            while (mInsertStartIdx > 0)
            {
                char8 c = (char8)mTargetEditWidget.Content.mData.mText[mInsertStartIdx - 1].mChar;
                if ((!c.IsLetterOrDigit) && (c != '_'))
                {
                    break;
                }
                mInsertStartIdx--;
            }*/

            if ((mInvokeWidget != null) && (mInvokeWidget.mEntryList.Count > 0))
            {
				var data = mTargetEditWidget.Content.mData;

				int32 startIdx = mInvokeSrcPositions[0];
				if ((startIdx < data.mTextLength) && (data.mText[startIdx].mChar == '('))
				{
	                mInvokeSrcPositions.Clear();
	                int32 openDepth = 0;
	                int32 checkIdx = startIdx;
	                mInvokeSrcPositions.Add(startIdx);
	                int32 argCount = 0;

					void HadContent()
					{
						if (argCount == 0)
							argCount++;
					}

					bool failed = false;
	                while (checkIdx < mTargetEditWidget.Content.mData.mText.Count)
	                {
	                    var char8Data = mTargetEditWidget.Content.mData.mText[checkIdx];
	                    if (char8Data.mDisplayTypeId == 0)
	                    {
							if (char8Data.mChar == '{')
							{
								openDepth++;
								failed = true;
								break;
							}
							else if (char8Data.mChar == '}')
								openDepth--;
	                        else if (char8Data.mChar == '(')
	                            openDepth++;
	                        else if (char8Data.mChar == ')')
	                        {
								openDepth--;
	                        }
	                        else if ((char8Data.mChar == ',') && (openDepth == 1))
	                        {
	                            mInvokeSrcPositions.Add(checkIdx);
	                            argCount++;
	                        }
	                        else if (!((char8)char8Data.mChar).IsWhiteSpace)
	                            HadContent();

							if (openDepth == 0)
							{
							    mInvokeSrcPositions.Add(checkIdx);
								break;
							}
	                    }
						else if (char8Data.mDisplayPassId != (.)SourceElementType.Comment)
						{
							HadContent();
						}
	                    checkIdx++;
	                }

					bool hasTooFewParams = false;
					if (!failed)
					{
						if (mInvokeWidget.mSelectIdx != -1)
						{
							let entry = mInvokeWidget.mEntryList[mInvokeWidget.mSelectIdx];
							if (!entry.HasParamsParam())
								hasTooFewParams = entry.GetParamCount() < argCount;
						}
					}

					if (hasTooFewParams)
					{
		                // Make sure the current method has enough params to support the args coming in
		                for (int checkOffset = 0; checkOffset < mInvokeWidget.mEntryList.Count; checkOffset++)
		                {
		                    int checkEntryIdx = (mInvokeWidget.mSelectIdx + checkOffset) % mInvokeWidget.mEntryList.Count;

		                    let entry = mInvokeWidget.mEntryList[checkEntryIdx];
							bool matches = false;
							int paramCount = entry.GetParamCount();
							if (argCount <= paramCount)
							{
								matches = true;
							}
							if (entry.HasParamsParam())
							{
								matches = true;
							}

							if ((matches) && (mInvokeWidget.mSelectIdx != -1))
							{
								let prevEntry = mInvokeWidget.mEntryList[mInvokeWidget.mSelectIdx];
								int prevMatchDiff = prevEntry.GetParamCount() - argCount;
								int newMatchDiff = entry.GetParamCount() - argCount;
								if ((prevMatchDiff >= 0) && (prevMatchDiff < newMatchDiff))
									matches = false;
							}

		                    if (matches)
		                    {
		                        mInvokeWidget.mSelectIdx = (int32)checkEntryIdx;
		                    }
		                }
					}
				}
            }

			//Debug.WriteLine("GetAsyncTextPos end {0} {1}", mInsertStartIdx, mInsertEndIdx);
        }

        bool SelectEntry(String curString)
        {
            if (mAutoCompleteListWidget == null)
                return false;

            int32 caseMatchMRUPriority = -1;
            int32 caseNotMatchMRUPriority = -1;            

			int32 selectIdx = mAutoCompleteListWidget.mSelectIdx;

            bool hadMatch = false;
            for (int32 i = 0; i < mAutoCompleteListWidget.mEntryList.Count; i++)
            {
                var entry = mAutoCompleteListWidget.mEntryList[i];
                if (entry.mEntryDisplay == curString)
                {
                    hadMatch = true;
                    selectIdx = i;
                    break;
                }

                if (curString.Length > entry.mEntryDisplay.Length)
                    continue;

                if (String.Compare(curString, 0, entry.mEntryDisplay, 0, curString.Length, false) == 0)
                {
                    hadMatch = true;
                    int32 priority = -1;
                    sAutoCompleteMRU.TryGetValue(entry.mEntryDisplay, out priority);
                    if (priority > caseMatchMRUPriority)
                    {
						selectIdx = i;
                        caseMatchMRUPriority = priority;
                    }
                }
                else if ((caseMatchMRUPriority == -1) && (String.Compare(curString, 0, entry.mEntryDisplay, 0, curString.Length, true) == 0))
                {
                    hadMatch = true;
                    int32 priority = -1;
                    sAutoCompleteMRU.TryGetValue(entry.mEntryDisplay, out priority);
                    if (priority > caseNotMatchMRUPriority)
                    {
                        selectIdx = i;
                        caseNotMatchMRUPriority = priority;
                    }
                }
            }

			if (selectIdx == -1)
				selectIdx = 0;

			if (!mAutoCompleteListWidget.mEntryList.IsEmpty)
				mAutoCompleteListWidget.Select(selectIdx);

            return hadMatch;
        }

		public void SetIgnoreMove(bool ignoreMove)
		{
			if (mAutoCompleteListWidget != null)
			    mAutoCompleteListWidget.mIgnoreMove = ignoreMove;
			if (mInvokeWidget != null)
			    mInvokeWidget.mIgnoreMove = ignoreMove;	
		}

		bool DoesFilterMatch(String entry, String filter)
		{	
			if (filter.Length == 0)
				return true;

			char8* entryPtr = entry.Ptr;
			char8* filterPtr = filter.Ptr;

			int filterLen = (int)filter.Length;
			int entryLen = (int)entry.Length;

			bool hasUnderscore = false;
			bool checkInitials = filterLen > 1;
			for (int i = 0; i < (int)filterLen; i++)
			{
				char8 c = filterPtr[i];
				if (c == '_')
					hasUnderscore = true;
				else if (filterPtr[i].IsLower)
					checkInitials = false;
			}

			if (hasUnderscore)
				//return strnicmp(filter, entry, filterLen) == 0;
				return (entryLen >= filterLen) && (String.Compare(entryPtr, filterLen, filterPtr, filterLen, true) == 0);

			char8[256] initialStr;
			char8* initialStrP = &initialStr;

			//String initialStr;
			bool prevWasUnderscore = false;
			
			for (int entryIdx = 0; entryIdx < entryLen; entryIdx++)
			{
				char8 entryC = entryPtr[entryIdx];

				if (entryC == '_')
				{
					prevWasUnderscore = true;
					continue;
				}

				if ((entryIdx == 0) || (prevWasUnderscore) || (entryC.IsUpper) || (entryC.IsDigit))
				{
					/*if (strnicmp(filter, entry + entryIdx, filterLen) == 0)
						return true;*/
					if ((entryLen - entryIdx >= filterLen) && (String.Compare(entryPtr + entryIdx, filterLen, filterPtr, filterLen, true) == 0))
						return true;
					if (checkInitials)
						*(initialStrP++) = entryC;
				}
				prevWasUnderscore = false;

				if (filterLen == 1)
					break; // Don't check inners for single-character case
			}	

			if (!checkInitials)
				return false;
			int initialLen = initialStrP - (char8*)&initialStr;
			return (initialLen >= filterLen) && (String.Compare(&initialStr, filterLen, filterPtr, filterLen, true) == 0);

			//*(initialStrP++) = 0;
			//return strnicmp(filter, initialStr, filterLen) == 0;
		}

        void UpdateData(String selectString, bool changedAfterInfo)
        {
			if ((mInsertEndIdx != -1) && (mInsertEndIdx < mInsertStartIdx))
			{
				mPopulating = false;
				Close();
				return;
			}

            int visibleCount = 0;
            if (mAutoCompleteListWidget != null)
                visibleCount = mAutoCompleteListWidget.mEntryList.Count;
            if ((mAutoCompleteListWidget != null) && ((mInsertEndIdx != -1) || (selectString != null)))
            {
                String curString;
                if (selectString != null)
                    curString = selectString;
                else
				{
					curString = scope:: String();
                    mTargetEditWidget.Content.ExtractString(mInsertStartIdx, mInsertEndIdx - mInsertStartIdx, curString);
				}
                
                //if (selectString == null)
				if (changedAfterInfo)
                {
					mAutoCompleteListWidget.mSelectIdx = -1;

                    if ((curString.Length == 0) && (!mIsMember) && (mInvokeSrcPositions == null))
                    {
						mPopulating = false;
                        Close();
                        return;
                    }
                
                    // Only show applicable entries                    
                    mAutoCompleteListWidget.mMaxWidth = 0;
					mAutoCompleteListWidget.mDocWidth = 0;
					mAutoCompleteListWidget.mRightBoxAdjust = 0;
                    visibleCount = 0;
					if (mAutoCompleteListWidget.mEntryList == mAutoCompleteListWidget.mFullEntryList)
						mAutoCompleteListWidget.mEntryList = new List<AutoCompleteListWidget.EntryWidget>();
					mAutoCompleteListWidget.mEntryList.Clear();

					int spaceIdx = curString.LastIndexOf(' ');
					if (spaceIdx != -1)
						curString.Remove(0, spaceIdx + 1);

					curString.Trim();
					if (curString == ".")
						curString.Clear();

                    for (int i < mAutoCompleteListWidget.mFullEntryList.Count)
                    {
                        var entry = mAutoCompleteListWidget.mFullEntryList[i];
                        //if (String.Compare(entry.mEntryDisplay, 0, curString, 0, curString.Length, true) == 0)
						if (DoesFilterMatch(entry.mEntryDisplay, curString))
                        {
							mAutoCompleteListWidget.mEntryList.Add(entry);
                            mAutoCompleteListWidget.UpdateEntry(entry, visibleCount);
                            visibleCount++;
                        }
                        else
                        {
                            mAutoCompleteListWidget.UpdateEntry(entry, -1);
                        }                                        
                    }

                    if ((visibleCount == 0) && (mInvokeSrcPositions == null))
                    {
						mPopulating = false;
                        Close();
                        return;
                    }
                }

				// Only take last part, useful for "overide <methodName>" autocompletes
				for (int32 i = 0; i < curString.Length; i++)
				{
					char8 c = curString[i];
					if ((c == '<') || (c == '('))
						break;
					if (c.IsWhiteSpace)
					{
						curString.Remove(0, i + 1);
						i = 0;
					}
				}

                if ((!SelectEntry(curString)) && (curString.Length > 0))
                {
                    // If we can't find any matches, at least select a string that starts with the right char8acter
                    curString.RemoveToEnd(1);
                    SelectEntry(curString);
                }

				if (mAutoCompleteListWidget != null)
				{
					mAutoCompleteListWidget.UpdateWidth();
				}
            }
            else if (selectString == null)
            {
                SelectEntry("");
            }

            SetIgnoreMove(true);

			gApp.mAutoCompletePanel.StartBind(this);

			int32 prevInvokeSelect = 0;
            if (mInvokeWidget != null)
            {
				prevInvokeSelect = mInvokeWidget.mSelectIdx;
                if ((mInvokeWidget.mEntryList.Count > 0) && (!mInvokeSrcPositions.IsEmpty))
                {
					if (IsInPanel())
					{
						mInvokeWidget.mOwnsWindow = false;
					}
					else
					{
						mInvokeWidget.mOwnsWindow = true;
						mInvokeWidget.ResizeContent(false);
	                    UpdateWindow(ref mInvokeWindow, mInvokeWidget, mInvokeSrcPositions[0], (int32)mInvokeWidget.mWidth, (int32)mInvokeWidget.mHeight);
					}
                }
                else
                {
					if ((mInvokeWindow == null) || (mInvokeWindow.mRootWidget != mInvokeWidget))
						delete mInvokeWidget;
                    if (mInvokeWindow != null)
                    {
                        mInvokeWindow.Close();
                        mInvokeWindow = null;
                    }
                    mInvokeWidget = null;
                }
            }

            if (mAutoCompleteListWidget != null)
            {
                if (mAutoCompleteListWidget.mEntryList.Count > 0)
                {
					mAutoCompleteListWidget.mOwnsWindow = !IsInPanel();
					mAutoCompleteListWidget.mAutoFocus = IsInPanel();
					int32 windowWidth = (int32)mAutoCompleteListWidget.mMaxWidth;
					windowWidth += (int32)mAutoCompleteListWidget.mDocWidth;
					windowWidth += GS!(16);

					int32 contentHeight = (int32)(visibleCount * mAutoCompleteListWidget.mItemSpacing);
					int32 windowHeight = contentHeight + GS!(20);
					int32 maxWindowHeight = GetMaxWindowHeight();
					bool wantScrollbar = false;
					if (windowHeight > maxWindowHeight)
					{
					    windowHeight = maxWindowHeight;
					    wantScrollbar = true;
					    windowWidth += GS!(12);
					}
					contentHeight += GS!(8);
					mAutoCompleteListWidget.ResizeContent(windowWidth, contentHeight, wantScrollbar);
					if ((mInsertStartIdx != -1) && (!IsInPanel()))
						UpdateWindow(ref mListWindow, mAutoCompleteListWidget, mInsertStartIdx, windowWidth, windowHeight);
					mAutoCompleteListWidget.UpdateScrollbars();
					mAutoCompleteListWidget.CenterSelection();
					mAutoCompleteListWidget.UpdateWidth();
                }
                else
                {
					if ((mListWindow == null) || (mListWindow.mRootWidget != mAutoCompleteListWidget))
                    	delete mAutoCompleteListWidget;
                    if (mListWindow != null)
                    {
                        mListWindow.Close();
                        mListWindow = null;
                    }
                    mAutoCompleteListWidget = null;
                }
            }
			gApp.mAutoCompletePanel.FinishBind();
            SetIgnoreMove(false);

            if ((mAutoCompleteListWidget != null) && (!mAutoCompleteListWidget.mIsInitted))
                mAutoCompleteListWidget.Init();
            if ((mInvokeWidget != null) && (!mInvokeWidget.mIsInitted))
			{
				mInvokeWidget.mSelectIdx = prevInvokeSelect;
                mInvokeWidget.Init();
			}
        }

		public void UpdateInfo(String info)
		{
			for (var entryView in info.Split('\n'))
			{
				StringView entryType = StringView(entryView);
				int tabPos = entryType.IndexOf('\t');
				StringView entryDisplay = default;
				if (tabPos != -1)
				{
					entryDisplay = StringView(entryView, tabPos + 1);
					entryType = StringView(entryType, 0, tabPos);
				}

				StringView documentation = default;
				int docPos = entryDisplay.IndexOf('\x03');
				if (docPos != -1)
				{
					documentation = StringView(entryDisplay, docPos + 1);
					entryDisplay = StringView(entryDisplay, 0, docPos);
				}

				StringView entryInsert = default;
				tabPos = entryDisplay.IndexOf('\t');
				if (tabPos != -1)
				{
					entryInsert = StringView(entryDisplay, tabPos + 1);
					entryDisplay = StringView(entryDisplay, 0, tabPos);
				}

				int entryIdx = 0;
				switch (entryType)
				{
				case "insertRange":
				case "invoke":
				case "invoke_cur":
				case "isMember":
				case "invokeInfo":
				case "invokeLeftParen":
				case "select":
				default:
				    {
						if ((!documentation.IsEmpty) && (mAutoCompleteListWidget != null))
						{
							while (entryIdx < mAutoCompleteListWidget.mEntryList.Count)
							{
								let entry = mAutoCompleteListWidget.mEntryList[entryIdx];
								if ((entry.mEntryDisplay == entryDisplay) && (entry.mEntryType == entryType))
								{
									if (entry.mDocumentation == null)
										entry.mDocumentation = new:(mAutoCompleteListWidget.[Friend]mAlloc) String(documentation);
									break;
								}
								entryIdx++;
							}
						}
				    }                        
				}

				if (mAutoCompleteListWidget != null)
					mAutoCompleteListWidget.UpdateWidth();
			}
			MarkDirty();

			//Debug.WriteLine("UpdateInfo {0} {1}", mInsertStartIdx, mInsertEndIdx);
		}

        public void SetInfo(String info, bool clearList = true, int32 textOffset = 0, bool changedAfterInfo = false)
        {
			scope AutoBeefPerf("AutoComplete.SetInfo");

			DeleteAndNullify!(mInfoFilter);

			mPopulating = true;
			//defer { mPopulating = false; };

			Debug.Assert(!mClosed);

			mIsFixit = false;
            mInsertStartIdx = -1;
            mInsertEndIdx = -1;
			delete mInvokeSrcPositions;
            mInvokeSrcPositions = null;
			mUncertain = false;

            if (clearList)
            {
                if (mAutoCompleteListWidget != null)
                {
					mAutoCompleteListWidget.mIgnoreMove = true;
					if (IsInPanel())
					{
						mAutoCompleteListWidget.RemoveSelf();
						delete mAutoCompleteListWidget;
					}
					if (mListWindow != null)
					{
						// Will get deleted later...
						Debug.Assert(mListWindow.mRootWidget == mAutoCompleteListWidget);
					}
                    mAutoCompleteListWidget = null;
                }
            }
            if (mAutoCompleteListWidget == null)
			{
                mAutoCompleteListWidget = new AutoCompleteListWidget(this);
				//Debug.WriteLine("Created mAutoCompleteListWidget {0}", mAutoCompleteListWidget);
			}
            
			bool queueClearInvoke = false;
            if (queueClearInvoke)
            {
                mInvokeSrcPositions = null;
            }
            else
            {
				if (IsInPanel())
				{
					if (mInvokeWidget != null)
					{
						mInvokeWidget.RemoveSelf();
						delete mInvokeWidget;
					}
				}

                mInvokeWidget = new InvokeWidget(this);
            }

            InvokeWidget oldInvokeWidget = null;
            String selectString = null;
			for (var entryView in info.Split('\n'))
            {
				
				Image entryIcon = null;
				StringView entryType = StringView(entryView);
				int tabPos = entryType.IndexOf('\t');
				StringView entryDisplay = default;
				if (tabPos != -1)
				{
					entryDisplay = StringView(entryView, tabPos + 1);
					entryType = StringView(entryType, 0, tabPos);
				}

				StringView documentation = default;
				int docPos = entryDisplay.IndexOf('\x03');
				if (docPos != -1)
				{
					documentation = StringView(entryDisplay, docPos + 1);
					entryDisplay = StringView(entryDisplay, 0, docPos);
				}

				StringView entryInsert = default;
				tabPos = entryDisplay.IndexOf('\t');
				if (tabPos != -1)
				{
					entryInsert = StringView(entryDisplay, tabPos + 1);
					entryDisplay = StringView(entryDisplay, 0, tabPos);
				}

				if (entryDisplay.Ptr == null)
				{
					if (entryView == "uncertain")
					{
						mUncertain = true;
					}
					continue;
				}

                switch (entryType)
                {
                case "method":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Method);
                case "field":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Field);
                case "property":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Property);
                case "namespace":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Namespace);
                case "class":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Type_Class);
                case "interface":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Interface);
                case "valuetype":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.Type_ValueType);
                case "object":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.IconObject);
                case "pointer":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.IconPointer);
                case "value":
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.IconValue);
				case "payloadEnum":
					entryIcon = DarkTheme.sDarkTheme.GetImage(.IconPayloadEnum);
                case "generic": //TODO: make icon
                    entryIcon = DarkTheme.sDarkTheme.GetImage(.IconValue);
				case "folder":
					entryIcon = DarkTheme.sDarkTheme.GetImage(.ProjectFolder);
				case "file":
					entryIcon = DarkTheme.sDarkTheme.GetImage(.Document);
                }

				bool isInvoke = false;
                switch (entryType)
                {
                case "insertRange":
                    {
                        //var infoSections = scope List<StringView>(entryDisplay.Split(' '));
						int spacePos = entryDisplay.IndexOf(' ');
						if (spacePos != -1)
						{
							String str = scope String();
							//infoSections[0].ToString(str);
							str.Append(StringView(entryDisplay, 0, spacePos));
	                        mInsertStartIdx = int32.Parse(str).Get() + textOffset;
							str.Clear();
							str.Append(StringView(entryDisplay, spacePos + 1));
							//infoSections[1].ToString(str);
	                        mInsertEndIdx = int32.Parse(str).Get();
							if (mInsertEndIdx != -1)
								mInsertEndIdx += textOffset;
						}
                    }
                case "invoke":
                    {
						isInvoke = true;
                    }
                case "invoke_cur":
                    // Only use the "invoke_cur" if we don't already have an invoke widget
                    if ((mInvokeWidget == null) || (mInvokeWidget.mEntryList.Count <= 1))
                    {
                        isInvoke = true;
                    }
                    else
                    {
                        for (int32 invokeIdx = 0; invokeIdx < mInvokeWidget.mEntryList.Count; invokeIdx++)
                        {
                            var invokeEntry = mInvokeWidget.mEntryList[invokeIdx];                                
                            if (invokeEntry.mText == entryDisplay)
                            {
                                mInvokeWidget.mSelectIdx = invokeIdx;
                            }                                
                        }
                    }
                    break;
                case "isMember":
                    mIsMember = true;
                case "invokeInfo":
                    {
						String invokeStr = scope String(entryDisplay);

                        var infoSections = scope List<StringView>(invokeStr.Split(' '));
						var str = scope String();
						infoSections[0].ToString(str);
                        mInvokeWidget.mSelectIdx = int32.Parse(str);

                        mInvokeSrcPositions = new List<int32>();
                        for (int32 i = 1; i < infoSections.Count; i++)
						{
							str.Clear();
							infoSections[i].ToString(str);
                            mInvokeSrcPositions.Add(int32.Parse(str));
						}
                    }
                case "invokeLeftParen":
                    {
                        mInvokeWidget.mLeftParenIdx = int32.Parse(entryDisplay);
                    }
                case "select":
                    {
                        selectString = scope:: String(entryDisplay);
                    }
                default:
                    {
						if (!mInvokeOnly)
						{
							mIsFixit |= entryType == "fixit";
                            mAutoCompleteListWidget.AddEntry(entryType, entryDisplay, entryIcon, entryInsert, documentation);
						}
                    }                        
                }

				if (isInvoke)
				{
					if (queueClearInvoke)
					{
					    oldInvokeWidget = mInvokeWidget;                                    
					    mInvokeWidget = new InvokeWidget(this);
					    queueClearInvoke = false;
					}

					var invokeEntry = new InvokeWidget.Entry();
					invokeEntry.mText = new String(entryDisplay);
					if (!documentation.IsEmpty)
						invokeEntry.mDocumentation = new String(documentation);
					mInvokeWidget.AddEntry(invokeEntry);                            
				}
            }

            if (oldInvokeWidget != null)
            {
                /*if ((!mIsAsync) || (oldInvokeWidget.mLeftParenIdx == mInvokeWidget.mLeftParenIdx))
                {
                    // If it's not another embedded invoke then just get rid of it
                    delete oldInvokeWidget;
                }
                else
                {
                    mInvokeStack.Add(oldInvokeWidget);
                    oldInvokeWidget.Cleanup();
                }*/
            }
            
            if (changedAfterInfo)
            {
                GetAsyncTextPos();                
            }

            if ((mInvokeWidget != null) && (mInvokeSrcPositions != null))
            {
                int invokeLine = 0;
                int invokeColumn = 0;                
                mTargetEditWidget.Content.GetLineCharAtIdx(mInvokeSrcPositions[0], out invokeLine, out invokeColumn);
                int insertLine = 0;
                int insertColumn = 0;
                mTargetEditWidget.Content.GetLineCharAtIdx(mTargetEditWidget.Content.CursorTextPos, out insertLine, out insertColumn);

                if (insertLine != invokeLine)
                    mInvokeWidget.mIsAboveText = true;
            }

			mInfoFilter = new String();
			GetFilter(mInfoFilter);
            UpdateData(selectString, changedAfterInfo);
			mPopulating = false;

			//Debug.WriteLine("SetInfo {0} {1}", mInsertStartIdx, mInsertEndIdx);
        }

        public bool HasSelection()
        {
            return mAutoCompleteListWidget != null;
        }

        public bool IsShowing()
        {
            return (mInvokeWidget != null) || (mAutoCompleteListWidget != null);
        }

		public bool IsInPanel()
		{
			return (gApp.mAutoCompletePanel != null) && (this == gApp.mAutoCompletePanel.mAutoComplete);
		}

        public void Close(bool deleteSelf = true)
        {
			Debug.Assert(!mPopulating);

			if (!mClosed)
			{
				if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mAllowMouseOutside))
					DarkTooltipManager.CloseTooltip();

				if (IsInPanel())
				{
					gApp.mAutoCompletePanel.Unbind(this);
					if (mTargetEditWidget.mWidgetWindow != null)
						mTargetEditWidget.SetFocus();
				}

				if (deleteSelf)
					BFApp.sApp.DeferDelete(this);
				mClosed = true;

				if (mInvokeStack != null)
				{
				    for (var oldInvoke in mInvokeStack)
				        delete oldInvoke;
				}

				if (mOnClosed.HasListeners)
				    mOnClosed();

				if ((mAutoCompleteListWidget != null) && (mAutoCompleteListWidget.mWidgetWindow == null))
				{
					mAutoCompleteListWidget.Cleanup();
					delete mAutoCompleteListWidget;
				}
				
				if (mListWindow != null)
				{
				    mListWindow.Dispose();
				    mListWindow = null;
				}

				if ((mInvokeWidget != null) && (mInvokeWidget.mWidgetWindow == null))
				{
					mInvokeWidget.Cleanup();
					delete mInvokeWidget;
				}

				if (mInvokeWindow != null)
				{
				    mInvokeWindow.Dispose();
				    mInvokeWindow = null;
				}
			}
        }

        public void CloseListWindow()
        {
            if (mInvokeSrcPositions == null)
            {
                Close();
                return;
            }

			if (IsInPanel())
			{
				if (mAutoCompleteListWidget != null)
				{
					mAutoCompleteListWidget.RemoveSelf();
					delete mAutoCompleteListWidget;
					mAutoCompleteListWidget = null;
				}
			}
            else if (mListWindow != null)
            {
                mListWindow.Dispose();
                mListWindow = null;
                mAutoCompleteListWidget = null;
            }
        }

        public void ClearAsyncEdit()
        {
            CloseListWindow();
            if (mInvokeSrcPositions != null)
                UpdateAsyncInfo();            
        }

        public void CloseInvoke()
        {
            if (mInvokeStack.Count > 0)
            {
                delete mInvokeWidget;
                mInvokeWidget = mInvokeStack[mInvokeStack.Count - 1];
                mInvokeStack.RemoveAt(mInvokeStack.Count - 1);
                UpdateAsyncInfo();
                return;
            }

            Close();
        }

        public ~this()
        {
            Close(false);
        }

        public bool IsInsertEmpty()
        {
            return mInsertStartIdx == mInsertEndIdx;
        }

		/*void ApplyFixit(String fixitType, String fileName, String fixitParam)
		{			
			var projectSource = IDEApp.sApp.FindProjectSourceItem(fileName);

			var editData = IDEApp.sApp.GetEditData(projectSource);
			var sourceEditWidgetContent = (SourceEditWidgetContent)editData.mEditWidget.Content;
			//cursorPositions.Add(sourceEditWidgetContent.CursorTextPos);
			
			var bfSystem = IDEApp.sApp.mBfResolveSystem;
			var parser = bfSystem.CreateEmptyParser(null);
			defer:: delete parser;
			var text = scope String();
			editData.mEditWidget.GetText(text);
			parser.SetSource(text, fileName);
			var passInstance = bfSystem.CreatePassInstance();
			defer:: delete passInstance;
			parser.Parse(passInstance, false);
			parser.Reduce(passInstance);
			
			parser.Ref
		}*/

		void ApplyFixit(String data)
		{
			var parts = String.StackSplit!(data, '|');
			//String fixitType = parts[0];
			String fixitFileName = parts[1];
			int32 fixitIdx = int32.Parse(parts[2]).GetValueOrDefault();

			int dataIdx = 3;

			//for (int32 i = 4; i < parts.Count; i++)
			while (dataIdx < parts.Count)
			{
				int32 lenAdd = 0;

				String fixitInsert = scope String(parts[dataIdx++]);
				while (dataIdx < parts.Count)
				{
					var insertStr = parts[dataIdx++];
					if (insertStr.StartsWith("`"))
					{
						lenAdd = int32.Parse(StringView(insertStr, 1));
						break;
					}

					fixitInsert.Append('\n');
					fixitInsert.Append(insertStr);
				}

#unwarn
				bool hasMore = dataIdx < parts.Count;

				SourceViewPanel sourceViewPanel = IDEApp.sApp.ShowSourceFile(fixitFileName);
				if (sourceViewPanel != null)
				{
					//var targetContent = mTargetEditWidget.Content as SourceEditWidgetContent;
					var targetSourceEditWidgetContent = mTargetEditWidget.Content as SourceEditWidgetContent;
					var history = targetSourceEditWidgetContent.RecordHistoryLocation();
					history.mNoMerge = true;
	
					var editWidgetContent = (SourceEditWidgetContent)sourceViewPanel.mEditWidget.mEditWidgetContent;				
					editWidgetContent.CursorTextPos = fixitIdx;				
					editWidgetContent.EnsureCursorVisible(true, true);
					editWidgetContent.PasteText(fixitInsert, fixitInsert.StartsWith("\n"));

					fixitIdx = (int32)editWidgetContent.CursorTextPos + lenAdd;
				}
			}

			/*switch (parts[0])
			{
			case "using":
				//ApplyFixit(parts[0], parts[1], parts[2]);
				
				break;
			case "addMethod":
				break;
			}*/
		}

        public void InsertSelection(char32 keyChar, String insertType = null, String insertStr = null)
        {
			//Debug.WriteLine("InsertSelection");

            EditSelection editSelection = EditSelection();            
            editSelection.mStartPos = mInsertStartIdx;
            if (mInsertEndIdx != -1)
                editSelection.mEndPos = mInsertEndIdx;
            else
                editSelection.mEndPos = mInsertStartIdx;
            
            var entry = mAutoCompleteListWidget.mEntryList[mAutoCompleteListWidget.mSelectIdx];
            if (keyChar == '!')
            {
                if (!entry.mEntryDisplay.EndsWith("!"))
                {
                    // Try to find one that DOES end with a '!'
                    for (var checkEntry in mAutoCompleteListWidget.mEntryList)
                    {
                        if (checkEntry.mEntryDisplay.EndsWith("!"))
                            entry = checkEntry;
                    }
                }
            }

			if (insertStr != null)
				insertStr.Append(entry.mEntryInsert ?? entry.mEntryDisplay);

			if (entry.mEntryType == "fixit")
			{
				if (insertType != null)
					insertType.Append(entry.mEntryType);            
				ApplyFixit(entry.mEntryInsert);
				return;
			}

            String insertText = entry.mEntryInsert ?? entry.mEntryDisplay;
            if ((keyChar == '=') && (insertText.EndsWith("=")))
				insertText.RemoveToEnd(insertText.Length - 1);
                //insertText = insertText.Substring(0, insertText.Length - 1);
            String implText = null;
            int tabIdx = insertText.IndexOf('\t');
            if (tabIdx != -1)
            {
                implText = scope:: String();
                implText.Append(insertText, tabIdx);
                insertText.RemoveToEnd(tabIdx);
            }
            String prevText = scope String();
            mTargetEditWidget.Content.ExtractString(editSelection.mStartPos, editSelection.mEndPos - editSelection.mStartPos, prevText);

            //sAutoCompleteMRU[insertText] = sAutoCompleteIdx++;
			String* keyPtr;
			int32* valuePtr;
			if (sAutoCompleteMRU.TryAdd(entry.mEntryDisplay, out keyPtr, out valuePtr))
			{
				// Only create new string if this entry doesn't exist already
				*keyPtr = new String(entry.mEntryDisplay);
			}
			*valuePtr = sAutoCompleteIdx++;

            if (insertText == prevText)
                return;

            var sourceEditWidgetContent = mTargetEditWidget.Content as SourceEditWidgetContent;

            PersistentTextPosition[] persistentInvokeSrcPositons = null;
            if (mInvokeSrcPositions != null)
            {
                persistentInvokeSrcPositons = scope:: PersistentTextPosition[mInvokeSrcPositions.Count];
                for (int32 i = 0; i < mInvokeSrcPositions.Count; i++)
                {
                    persistentInvokeSrcPositons[i] = new PersistentTextPosition(mInvokeSrcPositions[i]);
                    sourceEditWidgetContent.PersistentTextPositions.Add(persistentInvokeSrcPositons[i]);
                }
            }

            mTargetEditWidget.Content.mSelection = editSelection;
            //bool isMethod = (entry.mEntryType == "method");
            if (insertText.EndsWith("<>"))
            {
                if (keyChar == '\t')
                    mTargetEditWidget.Content.InsertCharPair(insertText);
                else if (keyChar == '<')
				{
					String str = scope String();
					str.Append(insertText, 0, insertText.Length - 2);
                    mTargetEditWidget.Content.InsertAtCursor(str, .NoRestoreSelectionOnUndo);
				}
                else
                    mTargetEditWidget.Content.InsertAtCursor(insertText, .NoRestoreSelectionOnUndo);
            }
            else
                mTargetEditWidget.Content.InsertAtCursor(insertText, .NoRestoreSelectionOnUndo);

            /*if (mIsAsync)
                UpdateAsyncInfo();*/

            if (implText != null)
            {
				String implSect = scope .();
				
				int startIdx = 0;
				for (int i < implText.Length)
				{
					char8 c = implText[i];
					if ((c == '\t') || (c == '\b') || (c == '\r'))
					{
						implSect.Clear();
						implSect.Append(implText, startIdx, i - startIdx);
						if (!implSect.IsEmpty)
						{
							sourceEditWidgetContent.InsertAtCursor(implSect);
						}

						if (c == '\t')
						{
							sourceEditWidgetContent.InsertAtCursor("\n");
							sourceEditWidgetContent.CursorToLineEnd();
							sourceEditWidgetContent.OpenCodeBlock();
						}
						else if (c == '\r')
						{
							sourceEditWidgetContent.InsertAtCursor("\n");
							sourceEditWidgetContent.CursorToLineEnd();
						}
						else
						{
							let lc = sourceEditWidgetContent.CursorLineAndColumn;
							sourceEditWidgetContent.CursorLineAndColumn = .(lc.mLine + 1, 0);
							sourceEditWidgetContent.CursorToLineEnd();
							sourceEditWidgetContent.InsertAtCursor("\n");
							sourceEditWidgetContent.CursorToLineEnd();
						}

						startIdx = i + 1;
					}
				}

				implSect.Clear();
				implSect.Append(implText, startIdx, implText.Length - startIdx);
				if (!implSect.IsEmpty)
				{
					sourceEditWidgetContent.InsertAtCursor(implSect);
				}
            }

            if (persistentInvokeSrcPositons != null)
            {
                for (int32 i = 0; i < mInvokeSrcPositions.Count; i++)
                {
					//TEST
                    //var persistentTextPositon = persistentInvokeSrcPositons[i + 100];
					var persistentTextPositon = persistentInvokeSrcPositons[i];
                    mInvokeSrcPositions[i] = persistentTextPositon.mIndex;
                    sourceEditWidgetContent.PersistentTextPositions.Remove(persistentTextPositon);
					delete persistentTextPositon;
                }
            };                        

            mTargetEditWidget.Content.EnsureCursorVisible();
            if ((insertType != null) && (insertText.Length > 0))
                insertType.Append(entry.mEntryType);
        }
		
		public void MarkDirty()
		{
			if (mInvokeWidget != null)
				mInvokeWidget.MarkDirty();
			if (mAutoCompleteListWidget != null)
				mAutoCompleteListWidget.MarkDirty();
		}
		
		int32 GetMaxWindowHeight()
		{
			return (int32)(9 * mAutoCompleteListWidget.mItemSpacing) + GS!(20);
		}
    }
}
