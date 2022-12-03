using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.events;
using System.Diagnostics;

namespace Beefy.theme.dark
{
    public class DarkTabbedView : TabbedView
    {        
        public DarkTabEnd mTabEnd;
        public DarkTabButton mRightTab; // Shows between tabs and tabEnd
        public float mLeftObscure;
        float mAllowRightSpace;
        float cMinTabSize = 5.0f;

        public class DarkTabButtonClose : ButtonWidget
        {
            public override void Draw(Graphics g)
            {
                base.Draw(g);
                if (mMouseOver)
                {
                    using (g.PushColor(mMouseDown ? 0xFFFF0000 : Color.White))
                        g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.CloseOver), GS!(-4), GS!(-4));
                }
                else
				{
                    var tabButton = mParent as TabButton;
					if (tabButton.mIsPinned == true)
						g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.PinnedTab), GS!(-5), GS!(-5));
					else
						g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Close), GS!(-4), GS!(-4));
				}
            }

            public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
            {
                base.MouseClicked(x, y, origX, origY, btn);

                var tabButton = (DarkTabButton)mParent;
                if (tabButton.mCloseClickedEvent.HasListeners)
                    tabButton.mCloseClickedEvent();
            }

			public override void MouseMove(float x, float y)
			{
				base.MouseMove(x, y);
				MarkDirty();
			}

			public override void MouseLeave()
			{
				base.MouseLeave();
				MarkDirty();
			}
        }

        public class DarkTabButton : TabbedView.TabButton
        {
            public Action mForceCloseEvent;
            public DarkTabButtonClose mCloseButton;
            public bool mIsEndTab;
            public bool mIsRightTab;
            public float mObscuredDir; // <0 = left, >0 = right
            public uint32? mTextColor;
			public float mTabWidthOffset = 30;

            public this(bool isEnd = false)
            {
                mContentInsets.Set(0, GS!(1), GS!(1), GS!(1));

                mIsEndTab = isEnd;
                if (!mIsEndTab)
                {
                    mCloseButton = new DarkTabButtonClose();
                    AddWidget(mCloseButton);
                }

                mDragHelper.mMinDownTicks = 15;
                mDragHelper.mTriggerDist = GS!(2);
            }

			public override void RehupScale(float oldScale, float newScale)
			{
				base.RehupScale(oldScale, newScale);
				mContentInsets.Set(0, GS!(1), GS!(1), GS!(1));
				mDragHelper.mTriggerDist = GS!(2);
				if (mLabel != null)
					mWantWidth = DarkTheme.sDarkTheme.mSmallFont.GetWidth(mLabel) + GS!(mTabWidthOffset);
				//mHeight = DarkTheme.sUnitSize;
			}

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                base.MouseDown(x, y, btn, btnCount);
                mDragHelper.mAllowDrag = mObscuredDir == 0;                
            }

            public override void Resize(float x, float y, float width, float height)
            {
                base.Resize(x, y, width, height);
                if (mCloseButton != null)
                    mCloseButton.Resize(mWidth - GS!(13), GS!(4), GS!(12), GS!(12));
            }

            public override void Draw(Graphics g)
            {
                base.Draw(g);

                if (mIsEndTab)
                    return;

                /*float drawWidth = mDrawWidth;
                if (drawWidth == 0)
                    drawWidth = mWidth;*/
                float drawWidth = mWidth;

                Image image = null;
                if (mIsActive)
                    image = (mMouseOver || mMouseDown) ? DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.TabActiveOver] : DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.TabActive];
                else
                    image = ((!mIsEndTab) && (mMouseOver || mMouseDown)) ? DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.TabInactiveOver] : DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.TabInactive];
                g.DrawButton(image, 0, 0, drawWidth + 1);

				if ((mIsActive) && (DarkTheme.sScale != 1.0f))
				{
					// When scaling, we can end up with a subpixel we don't want
					//using (g.PushColor(0xFFFF0000))
						g.DrawButton(DarkTheme.sDarkTheme.mWindowTopImage, GS!(2), (float)Math.Ceiling(DarkTheme.sScale * (20)) - 1, drawWidth - GS!(4));
				}

                g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
                if ((mLabel != null) && (drawWidth > GS!(16)))
                {
                    //using (g.PushColor(((DarkTabbedView)mParent).mTextColor))
                    using (g.PushColor(mTextColor ?? DarkTheme.COLOR_TEXT))
					{
						float textWidth = g.mFont.GetWidth(mLabel);
						float useWidth = mWidth - GS!(12)*2;

						if (textWidth < useWidth)
                        	g.DrawString(mLabel, GS!(9) + (useWidth - textWidth)/2, 0, .Left, useWidth, .Truncate);
						else
							g.DrawString(mLabel, GS!(12), 0, .Left, useWidth, .Truncate);
                    }
                }
            }

            public override void DrawDockPreview(Graphics g)
            {
                using (g.PushTranslate(-mX - mDragHelper.mMouseDownX, -mY - mDragHelper.mMouseDownY))
                {
                    if (IsTotalWindowContent())
                        ((DarkTabbedView)mTabbedView).DrawDockPreview(g);
                    else
                        ((DarkTabbedView)mTabbedView).DrawDockPreview(g, this);
                }
            }            

			public override void DragEnd()
			{
				if (mIsRightTab == true)
				{
					mTextColor = Color.White;

					DarkTabbedView darkTabbedView = mTabbedView as DarkTabbedView;
					darkTabbedView.SetRightTab(null, false);
					darkTabbedView.AddTab(this, darkTabbedView.GetInsertPositionFromCursor());

					Activate();
				}

				base.DragEnd();
			}
        }

        public class DarkTabDock : ICustomDock
        {
            public TabbedView mTabbedView;
            public bool mAlreadyContains;
            
            public this(TabbedView tabbedView, bool alreadyContains)
            {
                mTabbedView = tabbedView;
                mAlreadyContains = alreadyContains;
            }

            public void Draw(Graphics g)
            {
                if (!mAlreadyContains)
                {
                    using (g.PushColor(0x60FFFFFF))
                        g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.WhiteCircle], mTabbedView.mX - GS!(6), mTabbedView.mY - GS!(6), mTabbedView.mWidth + GS!(6) * 2, GS!(32));
                }
            }

            public void Dock(IDockable dockable)
            {
                dockable.Dock(mTabbedView.mParentDockingFrame, mTabbedView, DockingFrame.WidgetAlign.Inside);
            }
        }

        public class DarkTabMenuButton : ButtonWidget
        {
            public override void Draw(Graphics g)
            {
                base.Draw(g);
                if (mMouseOver || mMouseDown)
                {
                    using (g.PushColor(0xFFF7A900))
                        g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropMenuButton), GS!(-4), GS!(-4));                    
                }
                else
                    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropMenuButton), GS!(-4), GS!(-4));
            }
        }

        public class DarkTabEnd : DarkTabButton
        {
            public DarkTabMenuButton mMenuButton;
            public int32 mMenuClosedTick;            

            public this()
                : base(true)
            {
                mMenuButton = new DarkTabMenuButton();
                AddWidget(mMenuButton);
                mMenuButton.mOnMouseDown.Add(new => MenuClicked);
            }

            void ShowMenu(float x, float y)
            {
                Menu menu = new Menu();
                /*menu.AddItem("Item 1");
                menu.AddItem("Item 2");
                menu.AddItem();
                menu.AddItem("Item 3");*/

				var menuItem = menu.AddItem("Frame Type");

				var subItem = menuItem.AddItem("Static");
				subItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mTabbedView.mIsFillWidget = false;
						mTabbedView.mSizePriority = 0;
						mTabbedView.mRequestedWidth = mTabbedView.mWidth;
						mTabbedView.GetRootDockingFrame().Rehup();
						mTabbedView.GetRootDockingFrame().ResizeContent();
					});
				if (!mTabbedView.mIsFillWidget)
					subItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);

				subItem = menuItem.AddItem("Documents");
				subItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						/*for (var tabSibling in mTabbedView.mParentDockingFrame.mDockedWidgets)
						{
							tabSibling.mSizePriority = 0;
							if (mTabbedView.mParentDockingFrame.mSplitType == .Horz)
								tabSibling.mRequestedWidth = tabSibling.mWidth;
							else
								tabSibling.mRequestedHeight = tabSibling.mHeight;
						}*/
						mTabbedView.mIsFillWidget = true;
						mTabbedView.GetRootDockingFrame().Rehup();
						mTabbedView.GetRootDockingFrame().ResizeContent();
					});
				if (mTabbedView.mIsFillWidget)
					subItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);

				menuItem = menu.AddItem("Permanent");
				if (!mTabbedView.mAutoClose)
					menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Check);
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mTabbedView.mAutoClose = !mTabbedView.mAutoClose;
					});

				menuItem = menu.AddItem("Close");
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mTabbedView.CloseTabs(true, true, true);
					});

				menuItem = menu.AddItem("Close Tabs");
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mTabbedView.CloseTabs(false, true, true);
					});

				menuItem = menu.AddItem("Close Tabs Except Current");
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
					{
						mTabbedView.CloseTabs(false, false, true);
					});
				
				menuItem = menu.AddItem("Close All Except Pinned");
				menuItem.mOnMenuItemSelected.Add(new (menu) =>
					{
						mTabbedView.CloseTabs(false, true, false);
					});

				menu.AddItem();

                for (var tab in mTabbedView.mTabs)
                {
                    menuItem = menu.AddItem(tab.mLabel);
					if (tab.mIsPinned)
						menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.PinnedTab);

                    menuItem.mOnMenuItemSelected.Add(new (selMenuItem) =>
	                    {
	                        TabbedView.TabButton activateTab = tab;
	                        activateTab.Activate();
	                    });
                }

                if (mTabbedView.mPopulateMenuEvent != null)
                    mTabbedView.mPopulateMenuEvent(menu);

                if (menu.mItems.Count > 0)
                {
                    MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);

                    menuWidget.Init(this, x, y, .AllowScrollable);
                    menuWidget.mWidgetWindow.mOnWindowClosed.Add(new => MenuClosed);
                }
				else
					delete menu;
            }

            void MenuClicked(MouseEvent theEvent)
            {
                if (mMenuClosedTick != mUpdateCnt)
                {
                    ShowMenu(mMenuButton.mX + GS!(14), mMenuButton.mY + GS!(14));                    
                }
            }

            void MenuClosed(BFWindow window)
            {
                mMenuClosedTick = mUpdateCnt;
            }

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                base.MouseDown(x, y, btn, btnCount);
                if (btn == 1)
                    ShowMenu(x, y);
            }

            public override void Dock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
            {
                mTabbedView.Dock(frame, refWidget, align);
            }

            public override void Draw(Graphics g)
            {
                base.Draw(g);
                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Grabber), mWidth - DarkTheme.sUnitSize, 0);

                /*if (mMouseOver)
                {
                    using (g.PushColor(0x80FF0000))
                        g.FillRect(0, 0, mWidth, mHeight);
                }*/
            }

            public override void Activate(bool setFocus)
            {                
            }

            public override void Resize(float x, float y, float width, float height)
            {
                base.Resize(x, y, width, height);
                if (mMenuButton != null)
                    mMenuButton.Resize(mWidth - GS!(20), GS!(3), GS!(14), GS!(12));
            }

            public override bool IsTotalWindowContent()
            {
                return (mTabbedView.mParentDockingFrame.mParentDockingFrame == null) &&
                    (mTabbedView.mParentDockingFrame.GetDockedWindowCount() == 1);
            }
        }        

        public this(SharedData sharedData = null) : base(sharedData)
        {
            mTabHeight = DarkTheme.sUnitSize;
            mTabEnd = new DarkTabEnd();
            mTabEnd.mTabbedView = this;
            AddWidget(mTabEnd);

			Object obj = this;
			obj.[Friend]GCMarkMembers();
        }
        
		public ~this()
		{
			if (mRightTab != null)
			{
				Widget.RemoveAndDelete(mRightTab);
				mRightTab = null;
			}
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			mTabHeight = DarkTheme.sUnitSize;
		}

		public override int GetTabCount()
		{
			int tabCount = base.GetTabCount();
			if (mRightTab != null)
				tabCount++;
			return tabCount;
		}

        public override void WithTabs(delegate void(TabbedView.TabButton) func)
        {
            for (var tab in mTabs)
                func(tab);
            if (mRightTab != null)
                func(mRightTab);
        }

        public override TabButton AddTab(String label, float width, Widget content, bool ownsContent, int insertIdx)
        {
			float useWidth = width;
            if (useWidth == 0)
                useWidth = DarkTheme.sDarkTheme.mSmallFont.GetWidth(label) + GS!(30);
            return base.AddTab(label, useWidth, content, ownsContent, insertIdx);
        }

        public override void RemoveTab(TabButton tabButton, bool deleteTab = true)
        {
            if (mRightTab == tabButton)
                SetRightTab(null, deleteTab);
            else
                base.RemoveTab(tabButton, deleteTab);            
        }

        protected override TabButton CreateTabButton()
        {
            return new DarkTabButton();
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);
            using (g.PushColor(0x80FFFFFF))
                g.DrawButton(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.TabInactive], 0, 0, mWidth);
            g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.Window], 0, mTabHeight - GS!(2), mWidth, mHeight - mTabHeight + GS!(2));
        }

        protected override void ResizeTabs(bool immediate)
        {
            if (mWidgetWindow == null)
                return;

			List<Widget> afterTabWidgets = scope List<Widget>(8);

            // Remove tabs, then them back
            for (int childIdx = mChildWidgets.Count - 1; childIdx >= 0; childIdx--)
            {
                var widget = mChildWidgets[childIdx];
                if ((widget is TabButton) /*&& (widget != mTabEnd) && (widget != mRightTab)*/)
				{
                    mChildWidgets.RemoveAt(childIdx);
				}
				else
				{
					afterTabWidgets.Add(widget);
					mChildWidgets.RemoveAt(childIdx);
				}
            }

            if (mRightTab != null)
                mRightTab.mWidth = mRightTab.mWantWidth;
            
            float maxAreaWidth = mWidth - GS!(36);
            if (mRightTab != null)
                maxAreaWidth -= mRightTab.mWidth + GS!(2);

            float leftObscure = mLeftObscure;

            int32 maxStackedDocs = 16;

            float curX = 0;
            int32 tabIdx = 0;
            // Before active tab button            

            bool foundActiveTab = false;
            for (tabIdx = 0; tabIdx < mTabs.Count; tabIdx++)
            {
                var tabButton = (DarkTabButton)mTabs[tabIdx];

				float useWidth = tabButton.mWantWidth;
				if (curX + useWidth > mWidth - GS!(36))
					useWidth = Math.Max(mWidth - GS!(36), 0);
				useWidth = (float)Math.Round(useWidth);

                tabButton.Resize(curX, tabButton.mY, useWidth, DarkTheme.sUnitSize);

                //float offset = tabIdx - leftObscure;
                float widthSubtract = Math.Max(0, leftObscure);
				leftObscure -= tabButton.mWantWidth;

                float showWidth = 0;
                tabButton.mVisible = leftObscure < 1536.0f;
                if (tabButton.mVisible)
                    showWidth = (float)Math.Round(Math.Max(tabButton.mWantWidth - widthSubtract, GS!(cMinTabSize)));

                if (widthSubtract > 0)
                {
                    tabButton.mObscuredDir = -widthSubtract;
                }
                else
                    tabButton.mObscuredDir = 0;

				if (mTabs.Count > 1)
				{
	                // Truncate drawing so we don't have the right side of the button showing from under the right side of the 
	                //  next tab (if next tab is shorter and on top of the previous one)
	                if (tabIdx < mTabs.Count - 1)
	                {
	                    tabButton.mWidth = (float)Math.Round(Math.Min(tabButton.mWantWidth, showWidth + GS!(8)));
	                    tabButton.mCloseButton.mVisible = tabButton.mWidth + 1 >= tabButton.mWantWidth;
	                }
	                else
	                {
	                    tabButton.mWidth = (float)Math.Round(tabButton.mWantWidth);
	                    tabButton.mCloseButton.mVisible = true;
	                }
				}

                mChildWidgets.Add(tabButton);                

                float pixelsOffscreen = curX + tabButton.mWidth + Math.Min(maxStackedDocs, mTabs.Count - tabIdx - 1) * GS!(cMinTabSize) - maxAreaWidth;
                curX += showWidth;

                if ((pixelsOffscreen > 0) && (leftObscure <= 0))
                {
                    if (tabButton.mObscuredDir != 0)
                    {
                        tabButton.mObscuredDir = 0;
                    }
                    else
                    {                        
                        tabButton.mObscuredDir = Math.Round(pixelsOffscreen);
                    }
                }
                tabButton.mVisible = true;                

                foundActiveTab |= tabButton.mIsActive;
                if ((mRightTab != null) && (mRightTab.mIsActive))
                    foundActiveTab = true;

                if ((foundActiveTab) && (tabButton.mObscuredDir >= -2))
                {
                    tabIdx++;
                    break;
                }
            }
            
            int32 numRightObscuredButtons = 0;
            bool foundEnd = false;
            float stackedSize = 0;

            int selInsertPos = mChildWidgets.Count;

            // After the active button
            for (; tabIdx < mTabs.Count; tabIdx++)
            {                
                var tabButton = (DarkTabButton)mTabs[tabIdx];
                
                float showWidth = (float)Math.Round(tabButton.mWantWidth);
                if (!foundEnd)
                    stackedSize = Math.Min(maxStackedDocs, mTabs.Count - tabIdx - 1) * GS!(cMinTabSize);
                float maxX = (float)Math.Round(maxAreaWidth - showWidth - stackedSize);

                tabButton.mWidth = showWidth;
                tabButton.mVisible = true;

                if (maxX < curX)
                {
                    curX = maxX;
                    tabButton.mObscuredDir = 1;                    
                    if (numRightObscuredButtons > maxStackedDocs)
                    {
                        //int a = 0;
                        tabButton.mVisible = false;
                    }
                    numRightObscuredButtons++;
                    foundEnd = true;
                    stackedSize -= GS!(cMinTabSize);

                    var prevButton = (DarkTabbedView.DarkTabButton)mTabs[tabIdx - 1];
                    if (prevButton.mWidth < prevButton.mWantWidth - 1)
                    {
                        // Super-squished, fix for small label, make small enough that the label doesn't draw
                        prevButton.mWidth = GS!(16);
                    }
                }
                else
                    tabButton.mObscuredDir = 0;                

                tabButton.mCloseButton.mVisible = tabButton.mObscuredDir == 0;

                mChildWidgets.Insert(0, tabButton);

                tabButton.Resize(curX, tabButton.mY, tabButton.mWidth, DarkTheme.sUnitSize);
                
                curX += showWidth;                
            }

            if ((curX < maxAreaWidth) && (mLeftObscure > 0))
            {
                var activeTab = (DarkTabButton)GetActiveTab();
                float pixelsLeft = maxAreaWidth - curX;
                if ((activeTab != null) && (pixelsLeft > mAllowRightSpace + 1))
                    activeTab.mObscuredDir = -pixelsLeft;
            }

            float tabX = 0;
            if (mTabs.Count > 0)
                tabX = Math.Min(mWidth - GS!(36), mTabs[mTabs.Count - 1].mX + mTabs[mTabs.Count - 1].mWidth);
            if (mRightTab != null)
            {
                if (mRightTab.mIsActive)
                    selInsertPos = mChildWidgets.Count;

                //tabX = Math.Min(tabX, maxAreaWidth);
                //tabX += 2;
                mRightTab.Resize(maxAreaWidth, 0, mRightTab.mWidth, DarkTheme.sUnitSize);
                //tabX += mRightTab.mWidth;
                mChildWidgets.Insert(selInsertPos, mRightTab);
            }

			for (int afterTabIdx = afterTabWidgets.Count - 1; afterTabIdx >= 0; afterTabIdx--)
			{
				mChildWidgets.Add(afterTabWidgets[afterTabIdx]);
			}

            mTabEnd.Resize(tabX, 0, mWidth - tabX - GS!(1), DarkTheme.sUnitSize);
            mChildWidgets.Insert(selInsertPos, mTabEnd);
            
            mNeedResizeTabs = false;

			if (immediate)
				UpdateTabView(true);
        }

        public void SetRightTab(DarkTabButton tabButton, bool deletePrev = true)
        {
			bool hadFocus = mWidgetWindow.mFocusWidget != null;
			bool needsNewFocus = false;

            mNeedResizeTabs = true;
            if (mRightTab != null)
            {
                if (mRightTab.mIsActive)
                {
					needsNewFocus = true;
					mRightTab.Deactivate();
				}
                mRightTab.mIsRightTab = false;
                RemoveWidget(mRightTab);
                mTabs.Remove(mRightTab);
                /*if ((GetActiveTab() == null) && (mTabs.Count > 0))
                    mTabs[0].Activate((hadFocus) && (mWidgetWindow.mFocusWidget == null));*/

                mRightTab.mTabbedView = null;   
				if (deletePrev)
             		BFApp.sApp.DeferDelete(mRightTab);
            }

            mRightTab = tabButton;
            if (tabButton != null)
            {
                tabButton.mIsRightTab = true;
                tabButton.mTabbedView = this;
                AddWidgetAtIndex(0, mRightTab);

				if (needsNewFocus)
					tabButton.Activate((hadFocus) && (mWidgetWindow.mFocusWidget == null));
            }
			else
			{
				if (mTabs.Count > 0)
					mTabs[0].Activate((hadFocus) && (mWidgetWindow.mFocusWidget == null));
			}
        }

		void UpdateTabView(bool immediate)
		{
			var darkTabButton = (DarkTabButton)GetActiveTab();
			if (darkTabButton != null)
			{
			    if (darkTabButton.mObscuredDir != 0)
			    {
			        float obscureAdd = darkTabButton.mObscuredDir * 0.2f;
			        obscureAdd += Math.Sign(obscureAdd) * 1.5f;
			        
			        if ((Math.Abs(obscureAdd) > Math.Abs(darkTabButton.mObscuredDir)) || (immediate))
			        {
			            obscureAdd = darkTabButton.mObscuredDir;
			            darkTabButton.mObscuredDir = 0;
			        }                    

			        mLeftObscure = Math.Max(0, mLeftObscure + obscureAdd);
			        if (mLeftObscure == 0)
			            darkTabButton.mObscuredDir = 0;

			        if (obscureAdd > 0)                    
			            mAllowRightSpace = GS!(cMinTabSize) + 0.1f; // To remove oscillations                    
			        ResizeTabs(false);
			        mAllowRightSpace = 0;

					MarkDirty();
			    }
			}
		}

        public override void Update()
        {
            base.Update();

			UpdateTabView(false);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeTabs(true);
        }

        public override ICustomDock GetCustomDock(IDockable dockable, float x, float y)
        {
            if (y < mTabHeight)
            {
                bool alreadyContains = false;

				var tabButton = dockable as TabButton;
                if ((tabButton != null) && (mTabs.Contains(tabButton)))
                {
                    // Resizing our own tabs...
                    //  We do this twice so it switches back if our new mouse location doesn't contain ourselves.
                    //  This avoids rapidly switching position back and foreth on edges of unequally-sized tabs
                    for (int32 pass = 0; pass < 2; pass++)
                    {
                        TabButton foundTab = FindWidgetByCoords(x, y) as TabButton;
                        if ((foundTab != null) && (foundTab != dockable))
                        {
                            int foundIndex = mTabs.IndexOf(foundTab);
                            if (foundIndex != -1)
                            {
                                int dragIndex = mTabs.IndexOf((TabButton)dockable);

                                mTabs[dragIndex] = mTabs[foundIndex];
                                mTabs[foundIndex] = (TabButton)dockable;
                                ResizeTabs(false);
                            }
                        }
                    }

                    alreadyContains = true;
                }
                
                return new DarkTabDock(this, alreadyContains);
            }

            return null;
        }
        
        public virtual void DrawDockPreview(Graphics g, TabButton tabButton)
        {
            using (g.PushColor(0x80FFFFFF))
            {                    
                g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.Window], 0, mTabHeight - GS!(2), mWidth, mHeight - mTabHeight + GS!(2));   
                using (g.PushTranslate(tabButton.mX, tabButton.mY))
                    tabButton.Draw(g);
            }            
        }
    }
}
