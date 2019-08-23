using System;
using System.Collections.Generic;
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
using System.Diagnostics;

namespace IDE.ui
{
	public class CallStackListViewItem : DarkVirtualListViewItem
	{
		public override void Draw(Graphics g)
		{
			base.Draw(g);
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
		}

		public ~this()
		{
			NOP!();
		}
	}

    public class CallStackListView : DarkVirtualListView
    {
        CallStackPanel mCallStackPanel;
		public bool mIconsDirty;

        public this(CallStackPanel panel)
        {
            mCallStackPanel = panel;
        }

		protected override ListViewItem CreateListViewItem()
		{
			mIconsDirty = true;
			return new CallStackListViewItem();
		}

		static void InsertColorChange(String str, int idx, uint32 color)
		{
			char8* insertChars = scope char8[5]*;
			insertChars[0] = (char8)1;
			*(uint32*)(insertChars + 1) = (color >> 1) & 0x7F7F7F7F;
			str.Insert(idx, scope String(insertChars, 5));
		}

		public static void ColorizeLocationString(String label, int line = -1)
		{
			int prevStart = -1;
			bool foundOpenParen = false;
			// Check to see if this is just a Mixin name, don't mistake for the bang that separates module name 
			bool awaitingBang = label.Contains('!') && !label.EndsWith("!"); 
			bool awaitingParamName = false;
			int chevronCount = 0;
			int parenCount = 0;

			int lastTopStart = -1;
			int lastTopEnd = -1;

			for (int32 i = 0; i < label.Length; i++)
			{
				char8 c = label[i];
				if ((c == '0') && (i == 0))
					break; // Don't colorize addresses

				if ((c == '<') && (i == 0))
				{
					uint32 color = 0xFFA0A0A0;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
					InsertColorChange(label, 0, color);
					label.Append('\x02');
					break;
				}

				if (awaitingBang)
				{
					if ((c == ':') || (c == '<'))
					{
                        awaitingBang = false;
						i = -1;
						continue;
					}

					if (c == '!')
					{
						bool endNow = false;

						if (i + 1 < label.Length)
						{
                            char16 nextC = label[i + 1];
							if ((nextC == '(') || (nextC == '='))
							{
								awaitingBang = false;
								i = -1;
								continue;
							}
							else if ((nextC == '0') || (nextC == '<'))
							{
								endNow = true; // Just a raw string
							}
						}

						uint32 color = 0xFFA0A0A0;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
						InsertColorChange(label, 0, color);
						awaitingBang = false;

						i += 5;
						label.Insert(i, '\x02');

						if (endNow)
						{
							InsertColorChange(label, i + 2, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method]);
                            break;
						}
					}
				}
				else if (c == '$')
				{
					uint32 color = 0xFF80A080;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
					InsertColorChange(label, i, color);
					i += 5;
				}
				else if ((c.IsLetterOrDigit) || (c == '_') || (c == '$') || (c == '@') /*|| (c == '>') || (c == '<') || (c == ':') || (c == '[') || (c == ']')*/)
				{					
					if ((prevStart == -1) && (!awaitingParamName))
						prevStart = i;
				}
				else
				{
					if (prevStart != -1)
					{
						//label.Insert(prevStart, SourceEditWidgetContent.sTextColors[(int)SourceElementType.TypeRef]);
						uint32 color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.TypeRef];
						/*if ((c == '+') || (c == '('))
						{
							foundOpenParen = true;
							color = SourceEditWidgetContent.sTextColors[(int)SourceElementType.Method];
						}*/

						if (chevronCount == 0)
						{
							lastTopStart = prevStart;
							lastTopEnd = i;
						}

						InsertColorChange(label, prevStart, color);
						i += 5;

						label.Insert(i, '\x02');
						prevStart = -1;
						awaitingParamName = false;

						i++;
					}

					if (c == ',')
						awaitingParamName = false;

					if ((c == ')') && (parenCount > 0))
						parenCount--;

					if ((c == '(') && ((i == 0) || (chevronCount > 0)))
					{
						parenCount++;
					}
					else if ((c == '(') || (c == '+'))
					{
						foundOpenParen = true;
						if (lastTopStart != -1)
						{
							char8* insertChars = label.CStr() + lastTopStart;
							uint32 color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method];
							*(uint32*)(insertChars + 1) = (color >> 1) & 0x7F7F7F7F;
						}
						else
						{
							int checkIdx = i - 1;
							while (checkIdx > 0)
							{
								char8 checkC = label[checkIdx];
								if (checkC == ':')
								{
									checkIdx++;
                                    break;
								}
								checkIdx--;
							}
							if (checkIdx >= 0)
							{
                                InsertColorChange(label, checkIdx, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method]);
								i += 5;
							}
						}
					}

					if ((foundOpenParen) && (!awaitingParamName) && (chevronCount == 0))
					{
						if (c == ' ')
						{							
							bool nextIsName = true;
							int32 spaceCount = 0;
							for (int32 checkIdx = i + 1; checkIdx < label.Length; checkIdx++)
							{
								char8 checkC = label[checkIdx];
								if (checkC == ' ')
								{
									spaceCount++;
									if (spaceCount > 1)
										nextIsName = false;
								}
								if ((checkC == '<') || (checkC == '*') || (checkC == '['))
									nextIsName = false;
								if ((checkC == ',') || (checkC == ')'))
								{
									if (spaceCount > 0)
										nextIsName = false;
									break;
								}
							}

							if (nextIsName)
								awaitingParamName = true;
						}						
					}
				}

				if (c == '<')
					chevronCount++;
				else if (c == '>')
					chevronCount--;
			}

			if (line != -1)
			    label.AppendF(" Line {0}", (int32)(line + 1));
		}

        public override void PopulateVirtualItem(DarkVirtualListViewItem listViewItem)
        {
            base.PopulateVirtualItem(listViewItem);
            if (!gApp.mExecutionPaused)
                return;

			var callStackPanel = (CallStackPanel)listViewItem.mListView.mParent;
			callStackPanel.SetStackFrame!();

            listViewItem.mOnMouseDown.Add(new => mCallStackPanel.ValueClicked);

        	int addr;
            String file = scope String();
            int hotIdx;
            int defLineStart;
            int defLineEnd;
            int line;
            int column;
            int language;
            int stackSize;
            String label = scope String(256);
			DebugManager.FrameFlags frameFlags;
            gApp.mDebugger.GetStackFrameInfo(listViewItem.mVirtualIdx, label, out addr, file, out hotIdx, out defLineStart, out defLineEnd, out line, out column, out language, out stackSize, out frameFlags);
			ColorizeLocationString(label, line);
            listViewItem.Label = label;


            var subItem = listViewItem.CreateSubItem(1);
            subItem.Label = ToStackString!((int32)stackSize);
            subItem.mOnMouseDown.Add(new => mCallStackPanel.ValueClicked);

            subItem = listViewItem.CreateSubItem(2);
            if (language == 1)
			{
				if (frameFlags.HasFlag(DebugManager.FrameFlags.Optimized))
                	subItem.Label = "C++ (Optimized)";
				else
					subItem.Label = "C++";
			}
            else if (language == 2)
			{
				if (frameFlags.HasFlag(DebugManager.FrameFlags.Optimized))
                	subItem.Label = "Beef (Optimized)";
				else
					subItem.Label = "Beef";
			}
            subItem.mOnMouseDown.Add(new => mCallStackPanel.ValueClicked);

            int32 callStackCount = gApp.mDebugger.GetCallStackCount();
            DarkVirtualListViewItem headItem = (DarkVirtualListViewItem)GetRoot().GetMainItem();
            headItem.mVirtualCount = callStackCount;            
        }
    }

    public class CallStackPanel : Panel
    {
        public CallStackListView mListView;
        public bool mCallStackDirty = true;
        public int32 mCallStackIdx;
        public bool mDisabled;
        public int32 mDisabledTicks;
        public int32 mCallStackUpdateCnt;
		public bool mWantsKeyboardFocus;
		public int mThreadId = -1;
		public int32 mActiveCallStackIdx = -1;
		public int32 mSelectedCallStackIdx = -1;

		public int32 ActiveCallStackIdx
		{
			get
			{
				if (mThreadId == -1)
					return gApp.mDebugger.mActiveCallStackIdx;
				return mActiveCallStackIdx;
			}

			set
			{
				if (mThreadId == -1)
					gApp.mDebugger.mActiveCallStackIdx = value;
				mActiveCallStackIdx = value;
			}
		}

        public this()
        {
            mListView = new CallStackListView(this);
			mListView.mShowGridLines = true;
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
			mListView.mOnLostFocus.Add(new (evt) => { mListView.GetRoot().SelectItemExclusively(null); });
			mListView.mAutoFocus = true;
            mListView.UpdateScrollbars();

            AddWidget(mListView);

            ListViewColumn column = mListView.AddColumn(GS!(400), "Location");
            column.mMinWidth = GS!(100);
            column = mListView.AddColumn(GS!(80), "Stack");
            column = mListView.AddColumn(GS!(200), "Language");

			SetScaleData();

            //RebuildUI();
        }

		void SetScaleData()
		{
			mListView.mIconX = GS!(4);
			mListView.mOpenButtonX = GS!(4);
			mListView.mLabelX = GS!(26);
			mListView.mChildIndent = GS!(16);
			mListView.mHiliteOffset = GS!(-2);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "CallStackPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

        void UpdateIcons()
        {
            for (int32 callStackIdx = 0; callStackIdx < mListView.GetRoot().GetChildCount(); callStackIdx++)
            {
                var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().GetChildAtIndex(callStackIdx);
				int32 virtIdx = listViewItem.mVirtualIdx;
				int32 breakStackFrameIdx = gApp.mDebugger.GetBreakStackFrameIdx();

				listViewItem.IconImage = null;
				if ((virtIdx == breakStackFrameIdx) && (gApp.mDebugger.IsActiveThreadWaiting()))
				{
					listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RedDot);
				}

                if (virtIdx == 0)
                {
                    if (virtIdx == ActiveCallStackIdx)
                        listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.LinePointer);
                }
                else if (virtIdx == ActiveCallStackIdx)
                    listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.LinePointer_Prev);
            }
        }

        public void ValueClicked(MouseEvent theEvent)
        {
			SetStackFrame!();

            DarkVirtualListViewItem clickedItem = (DarkVirtualListViewItem)theEvent.mSender;
            DarkVirtualListViewItem item = (DarkVirtualListViewItem)clickedItem.GetSubItem(0);

            mListView.SetFocus();

			if (theEvent.mBtn == 1)
			{
				if (!item.Selected)
					mListView.GetRoot().SelectItem(item, true);
			}
			else
			{
				mListView.GetRoot().SelectItem(item, true);
				//mListView.GetRoot().SelectItemExclusively(item);
			}

            if ((theEvent.mBtn == 0) && (theEvent.mBtnCount > 1))
            {
                for (int32 childIdx = 1; childIdx < mListView.GetRoot().GetChildCount(); childIdx++)
                {
                    var checkListViewItem = mListView.GetRoot().GetChildAtIndex(childIdx);
                    checkListViewItem.IconImage = null;
                }

                int32 selectedIdx = item.mVirtualIdx;
                ActiveCallStackIdx = selectedIdx;
                gApp.ShowPCLocation(selectedIdx, false, true);
                gApp.StackPositionChanged();                
            }

            UpdateIcons();
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mListView.Resize(0, 0, width, height);
        }

        public void MarkCallStackDirty()
        {
            mCallStackDirty = true;
        }

        /*public override void LostFocus()
        {
            base.LostFocus();
            mListView.GetRoot().SelectItemExclusively(null);            
        }*/

		public override void FocusForKeyboard()
		{
			mListView.SetFocus();
			mWantsKeyboardFocus = true;
		}

		public mixin SetStackFrame()
		{
			if (mThreadId != -1)
			{
				int prevActiveThread = gApp.mDebugger.GetActiveThread();
				gApp.mDebugger.SetActiveThread((.)mThreadId);
				defer:mixin gApp.mDebugger.SetActiveThread((.)prevActiveThread);
			}
		}

		public void UpdateCallStack()
		{
			if ((!gApp.mExecutionPaused) || (!mCallStackDirty) || (mDisabled))
				return;

			int focusedIdx = -1;
			HashSet<int> selectedIndices = scope .();

			for (int itemIdx < (int)mListView.GetRoot().GetChildCount())
			{
				let lvItem = (CallStackListViewItem)mListView.GetRoot().GetChildAtIndex(itemIdx);
				if (lvItem.Focused)
					focusedIdx = lvItem.mVirtualIdx;
				else if (lvItem.Selected)
					selectedIndices.Add(lvItem.mVirtualIdx);
			}

			let prevScrollPos = mListView.mVertPos.v;

			//Debug.WriteLine("CallStackPanel.Cleared {0}", gApp.mUpdateCnt);
			mListView.VertScrollTo(0);
			mListView.GetRoot().Clear();
			gApp.mDebugger.CheckCallStack();

			var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().CreateChildItem();
			listViewItem.mVirtualHeadItem = listViewItem;
			listViewItem.mVirtualCount = gApp.mDebugger.GetCallStackCount();
			mListView.PopulateVirtualItem(listViewItem);
			mCallStackUpdateCnt = 0;

			mCallStackDirty = false;
			UpdateIcons();

			mListView.VertScrollTo(prevScrollPos);
			mListView.UpdateAll();

			for (int itemIdx < (int)mListView.GetRoot().GetChildCount())
			{
				let lvItem = (CallStackListViewItem)mListView.GetRoot().GetChildAtIndex(itemIdx);
				if (lvItem.mVirtualIdx == focusedIdx)
					lvItem.Focused = true;
				else if (selectedIndices.Contains(lvItem.mVirtualIdx))
					lvItem.Selected = true;
			}
		}

        public override void Update()
        {
            base.Update();

			SetStackFrame!();

            if (!gApp.mDebugger.mIsRunning)
            {
                mListView.GetRoot().Clear();
            }
            else if ((gApp.mExecutionPaused) && (mCallStackDirty) && (!mDisabled))
            {
				UpdateCallStack();
            }
            else if (!mDisabled)
            {
                if ((mListView.GetRoot().GetChildCount() > 0) && (gApp.mIsUpdateBatchStart))
                {
                    mCallStackUpdateCnt++;
                    var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().GetChildAtIndex(0);
                    gApp.mDebugger.CheckCallStack();
                    listViewItem.mVirtualCount = gApp.mDebugger.GetCallStackCount();
                    if (mCallStackUpdateCnt <= 2)
                        UpdateIcons();
                }
            }
            else            
            {
                mDisabledTicks++;
                if ((mDisabledTicks > 40) && (!gApp.mDebuggerPerformingTask))
                {
					var root = mListView.GetRoot();
					if (root.GetChildCount() != 0)
					{
	                    mListView.GetRoot().Clear();
						MarkDirty();
					}
                }
            }            

			if (mListView.mIconsDirty)
			{
				UpdateIcons();
				mListView.mIconsDirty = false;
				MarkDirty();
			}

			if (!mListView.mHasFocus)
				mWantsKeyboardFocus = false;

			if (mWantsKeyboardFocus)
			{	
				int32 maxVirtIdx = 0;

				int32 wantSelectIdx = gApp.mDebugger.mActiveCallStackIdx;
				for (int32 i = 0; i < mListView.GetRoot().GetChildCount(); i++)
				{
					var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().GetChildAtIndex(i);
					int32 virtIdx = listViewItem.mVirtualIdx;

					if (virtIdx == wantSelectIdx)
					{
						mListView.GetRoot().SelectItem(listViewItem);
						mListView.EnsureItemVisible(listViewItem, true);
						mWantsKeyboardFocus = false;
					}

					maxVirtIdx = virtIdx;
				}
				/*if ((wantSelectIdx != -1) && (wantSelectIdx < mListView.GetRoot().GetChildCount()))
					mListView.GetRoot().SelectItem(mListView.GetRoot().GetChildAtIndex(wantSelectIdx));*/

				if (wantSelectIdx > maxVirtIdx)
				{					
					mListView.VertScrollTo(mListView.mFont.GetLineSpacing() * maxVirtIdx);
				}
			}			
        }

		bool TrySelectIdx(int idx)
		{
			for (int i < mListView.GetRoot().GetChildCount())
			{
				var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().GetChildAtIndex(i);
				int32 virtIdx = listViewItem.mVirtualIdx;
				if (virtIdx == idx)
				{
					mWantsKeyboardFocus = false;
					mListView.GetRoot().SelectItemExclusively(listViewItem);
					mListView.EnsureItemVisible(listViewItem, true);
					return true;
				}
			}
			return false;
		}

		public void SelectCallStackIdx(int idx)
		{
			UpdateCallStack();
			if (TrySelectIdx(idx))
				return;

			float lineHeight = mListView.mFont.GetLineSpacing();
			float approxScroll = lineHeight * (idx + 0.5f) - mListView.mScrollContentContainer.mHeight/2;
			mListView.VertScrollTo(approxScroll);
			mListView.UpdateAll();
			TrySelectIdx(idx);
		}

        public void SetDisabled(bool disabled)
        {
            mDisabled = disabled;
            mMouseVisible = !disabled;
            mDisabledTicks = 0;
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);         
        }

		public override bool HasAffinity(Widget otherPanel)
		{
			return base.HasAffinity(otherPanel) || (otherPanel is ThreadPanel);
		}
    }
}
