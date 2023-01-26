using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;
using System.Diagnostics;

namespace Beefy.theme.dark
{
    public class DarkMenuItem : MenuItemWidget
    {                
        int32 mSelectedTicks;
        int32 mDeselectedTicks;

        public this(Menu menuItem) : base(menuItem)
        {
            if (mMenuItem.mDisabled)
                mMouseVisible = false;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

			if (mMenuItem.mWidget != null)
				return;

            if (mMenuItem.mLabel == null)
                g.DrawButton(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MenuSepHorz), GS!(28), 0, mWidth - GS!(32));
            else
            {
				let darkMenuWidget = (DarkMenuWidget)mMenuWidget;
				g.SetFont(mMenuItem.mBold ? darkMenuWidget.mBoldFont : darkMenuWidget.mFont);

				using (g.PushColor(mMenuItem.mDisabled ? 0xFFA8A8A8 : 0xFFFFFFFF))
				{
					StringView leftStr = mMenuItem.mLabel;
					StringView rightStr = default;
					int barIdx = leftStr.IndexOf('|');
					if (barIdx != -1)
					{
						rightStr = .(leftStr, barIdx + 1);
						leftStr.RemoveToEnd(barIdx);
					}

					using (g.PushColor(DarkTheme.COLOR_TEXT))
					{
						g.DrawString(leftStr, GS!(36), 0);
						if (!rightStr.IsEmpty)
							g.DrawString(rightStr, mWidth - GS!(8), 0, .Right);
					}
				}

                if (mMenuItem.mIconImage != null)
                    g.Draw(mMenuItem.mIconImage, GS!(4), 0);
            }

            if (mMenuItem.IsParent)
            {
				using (g.PushColor(mMenuItem.mDisabled ? 0xFFA8A8A8 : 0xFFFFFFFF))
                	g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RightArrow), mWidth - GS!(16), 0);
            }
        }

        public void CloseSubMenu()
        {
            mSubMenu.Close();
        }

        void SubMenuClosed(Menu menu, Menu selectedItem)
        {            
            mSubMenu.mMenu.mOnMenuClosed.Remove(scope => SubMenuClosed, true);
            mSubMenu = null;
            if ((!mMouseOver) && (mMenuWidget.mSelectIdx == mIndex))
                mMenuWidget.SetSelection(-1);
            if (selectedItem != null)
            {
                mMenuWidget.mItemSelected = (Menu)selectedItem;
                mMenuWidget.Close();
            }            
        }

        public void OpenSubMenu(bool setFocus)
        {
			if (mWidgetWindow.mHasClosed)
				return;

			mMenuWidget.mOpeningSubMenu = true;
            mSubMenu = new DarkMenuWidget(mMenuItem);
			if (setFocus)
				mSubMenu.mSelectIdx = 0;
			else
				mSubMenu.mWindowFlags = .ClientSized | .NoActivate | .DestAlpha | .FakeFocus;
            mSubMenu.Init(mWidgetWindow.mRootWidget, mX + mWidth + GS!(10), mY);
            mSubMenu.mMenu.mOnMenuClosed.Add(new => SubMenuClosed);
            mSubMenu.mParentMenuItemWidget = this;
			mMenuWidget.mOpeningSubMenu = false;
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            base.MouseDown(x, y, btn, btnCount);

            if ((mMenuItem.mItems.Count > 0) && (mSubMenu == null))
            {
                OpenSubMenu(true);
            }
        }

        public override void Update()
        {
            base.Update();

			for (var item in mMenuItem.mItems)
			{
				if (item.mWidget != null)
				{
					if (item.mWidget.mMouseOver)
						mDeselectedTicks = 0;
				}
			}

            if (mMenuItem.mItems.Count > 0)
            {
                if (mIndex == mMenuWidget.mSelectIdx)
                {
                    mSelectedTicks++;
                    mDeselectedTicks = 0;
                    if ((mSelectedTicks == 10) && (mSubMenu == null))
                    {
                        for (DarkMenuItem item in mMenuWidget.mItemWidgets)
                        {
                            if ((item != this) && (item.mSubMenu != null))
                                item.CloseSubMenu();
                        }
                        OpenSubMenu(false);
                    }
                }
                else
                {
                    mDeselectedTicks++;
                    mSelectedTicks = 0;
                    if ((mDeselectedTicks > 20) && (mSubMenu != null))
                        CloseSubMenu();
                }
            }

			if (mMenuItem.mTooltip != null)
			{
				if (DarkTooltipManager.CheckMouseover(this, 20, var mousePoint))
				{
					DarkTooltipManager.ShowTooltip(mMenuItem.mTooltip, this, mWidth + GS!(8), GS!(-8));
				}
			}
        }

        public override void Submit()
        {
            mMenuWidget.mItemSelected = mMenuItem;
			mMenuWidget.Close();
            if (mMenuItem.mOnMenuItemSelected.HasListeners)
                mMenuItem.mOnMenuItemSelected(mMenuItem);
        }

        public override void MouseUp(float x, float y, int32 btn)
        {
            if (mMenuItem.mItems.Count > 0)
                return;

			if (mIndex != mMenuWidget.mSelectIdx)
				return;

            if ((btn == 0) && (mMouseOver))
            {
                Submit();
            }
            
            base.MouseUp(x, y, btn);
            
        }        
    }

    public class DarkMenuContainer : MenuContainer
    {
        public float mShowPct = 1.0f;
        public float mDrawHeight;
		public float mDrawY;

        public void DrawSelf(Graphics g)
        {
            base.Draw(g);

            using (g.PushColor(0x80000000))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropShadow), GS!(2), mDrawY + GS!(2), mWidth - GS!(2), mDrawHeight - GS!(2));

            base.Draw(g);
            using (g.PushColor(0xFFFFFFFF))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, mDrawY, mWidth - GS!(8), mDrawHeight - GS!(8));
        }

        public override void DrawAll(Graphics g)
        {
            float toDeg = Math.PI_f * 0.4f;
            float toVal = Math.Sin(toDeg);
            float curvedOpenPct = Math.Min(1.0f, (float)Math.Sin(mShowPct * toDeg) / toVal + 0.01f);
			
            mDrawHeight = Math.Min(curvedOpenPct * mHeight + GS!(20), mHeight);
			if (mReverse)
			{
				mDrawY = mHeight - mDrawHeight;
			}
			else
			{
				mDrawY = 0;
			}

            using (g.PushClip(0, mDrawY, mWidth, mDrawHeight))
            {
                DrawSelf(g);
            }

            using (g.PushClip(0, mDrawY, mWidth, mDrawHeight - GS!(10)))
            {
                //using (g.PushColor(Color.Get(Math.Min(1.0f, curvedOpenPct * 1.5f))))
                base.DrawAll(g);
            }
        }

        public override void Update()
        {
            base.Update();

            var darkMenuWidget = (DarkMenuWidget)mScrollContent;

            if (mShowPct < 1.0f)
            {
                float openSpeed = 0.08f + (0.15f / (darkMenuWidget.mItemWidgets.Count + 1));
                mShowPct = Math.Min(1.0f, mShowPct + openSpeed);
				MarkDirty();
            }
        }        
    }

    public class DarkMenuWidget : MenuWidget
    {
        public float mItemSpacing;
        public Font mFont;
		public Font mBoldFont;
        public bool mHasDrawn;        

        public this(Menu menu) :
            base(menu)
        {
            mFont = DarkTheme.sDarkTheme.mSmallFont;
			mBoldFont = DarkTheme.sDarkTheme.mSmallBoldFont;
            mItemSpacing = mFont.GetLineSpacing();
            mWindowFlags |= BFWindow.Flags.DestAlpha;

            mPopupInsets.Set(GS!(2), GS!(2), GS!(10), GS!(10));
        }

        public override MenuContainer CreateMenuContainer()
        {
            DarkMenuContainer menuContainer = new DarkMenuContainer();
            menuContainer.mScrollbarInsets.Set(GS!(2), 0, GS!(10), GS!(10));
            //menuContainer.mScrollContentInsets = new Insets(0, 0, 0, 0);
            return menuContainer;
        }

        public override MenuItemWidget CreateMenuItemWidget(Menu menuItem)
        {
            return new DarkMenuItem(menuItem);
        }

		public override float GetReverseAdjust()
		{
			return GS!(10);
		}

		ScrollableWidget GetScrollableParent()
		{
			ScrollableWidget scrollableWidget = mParent as ScrollableWidget;
			if (scrollableWidget != null)
				return scrollableWidget;

			if (mParent != null)
			{
				scrollableWidget = mParent.mParent as ScrollableWidget;
				if (scrollableWidget != null)
					return scrollableWidget;
			}

			return null;
		}

        public override void EnsureItemVisible(int itemIdx)
        {
            base.EnsureItemVisible(itemIdx);

            if (itemIdx == -1)
                return;

            var item = mItemWidgets[itemIdx];

            float aX;
            float aY;
            item.SelfToOtherTranslate(this, 0, 0, out aX, out aY);

            float topInsets = 0;
            float lineHeight = item.mHeight;

            ScrollableWidget scrollableWidget = GetScrollableParent();
            if (scrollableWidget == null)
                return;

            float bottomInset = GS!(8);

            if (aY < scrollableWidget.mVertPos.mDest + topInsets)
            {
                float scrollPos = aY - topInsets;                
                scrollableWidget.VertScrollTo(scrollPos);
            }
            else if (aY + lineHeight + bottomInset >= scrollableWidget.mVertPos.mDest + scrollableWidget.mScrollContentContainer.mHeight)
            {
                float scrollPos = aY + lineHeight + bottomInset - scrollableWidget.mScrollContentContainer.mHeight;                
                scrollableWidget.VertScrollTo(scrollPos);
            }            
        }

        public override void Draw(Graphics g)
        {
            /*using (g.PushColor(0xFFFF0000))
                g.FillRect(0, 0, mWidth, mHeight);*/

			for (var item in mItemWidgets)
				if (item.mMenuItem.mWidget != null)
					return;

            float mDrawHeight = mHeight;
            g.DrawButtonVert(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MenuSepVert), GS!(18), GS!(2), mDrawHeight - GS!(4));

            g.SetFont(mFont);
            for (int32 itemIdx = 0; itemIdx < mItemWidgets.Count; itemIdx++)
            {
#unwarn
                MenuItemWidget item = (MenuItemWidget)mItemWidgets[itemIdx];
                
                float curY = GS!(2) + mItemSpacing * itemIdx;

                if (itemIdx == mSelectIdx)
                {
                    using (g.PushColor(DarkTheme.COLOR_MENU_FOCUSED))
                        g.DrawButton(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.MenuSelect), GS!(4), curY, mWidth - GS!(6));
                }

                //if (item.mMenu.mLabel != null)
                //g.Draw(DarkTheme.mDarkTheme.GetImage(DarkTheme.ImageIdx.Check), 6, curY + 1);                
            }            
        }

        public void SetShowPct(float showPct)
        {
            DarkMenuContainer darkMenuContainer = (DarkMenuContainer)mParent.mParent;
            darkMenuContainer.mShowPct = showPct;
        }

        public override void MouseLeave()
        {
            base.MouseLeave();
            mSelectIdx = -1;
        }

        public override void ResizeComponents()
        {
            float maxWidth = 0;
            float curY = GS!(2);
            for (MenuItemWidget item in mItemWidgets)
            {
				if (item.mMenuItem.mWidget != null)
				{
					item.mMenuItem.mWidget.mX = GS!(4);
					item.mMenuItem.mWidget.mY = curY;
				}
				else
				{
	                if (item.mMenuItem.mLabel != null)
	                    maxWidth = Math.Max(maxWidth, mFont.GetWidth(item.mMenuItem.mLabel));
	                
	                item.Resize(0, curY, mWidth - GS!(8), mItemSpacing);
	                item.mMouseInsets = new Insets(0, GS!(6), 0, 0);
	                curY += mItemSpacing;
				}
            }
        }

        public override void CalcSize()
        {   
            float maxWidth = 0;
            for (MenuItemWidget item in mItemWidgets)
            {
				if (item.mMenuItem.mWidget != null)
				{
					mWidth = item.mMenuItem.mWidget.mWidth + GS!(6);
					mHeight = item.mMenuItem.mWidget.mHeight + GS!(4);
					return;
				}

                if (item.mMenuItem.mLabel != null)
                    maxWidth = Math.Max(maxWidth, mFont.GetWidth(item.mMenuItem.mLabel));
            }
            
            mWidth = Math.Max(mWidth, GS!(25) + maxWidth + GS!(40));
            mHeight = mMenu.mItems.Count * mItemSpacing + GS!(6);
        }

        public override void Update()
        {
            base.Update();
            
        }

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			base.KeyDown(keyCode, isRepeat);

			switch (keyCode)
			{
			case .Right:
				if (mSelectIdx != -1)
				{
					var darkMenuItem = (DarkMenuItem)mItemWidgets[mSelectIdx];
					if (darkMenuItem.mSubMenu == null)
					{
						if (darkMenuItem.mMenuItem.mItems.Count > 0)
							darkMenuItem.OpenSubMenu(true);
					}
					else
					{
						mOpeningSubMenu = true;
						darkMenuItem.mSubMenu.SetFocus();
						if (darkMenuItem.mSubMenu.mSelectIdx == -1)
						{
							darkMenuItem.mSubMenu.mSelectIdx = 0;
							darkMenuItem.mSubMenu.MarkDirty();
						}
						darkMenuItem.mSubMenu.mWidgetWindow.SetForeground();
						mOpeningSubMenu = false;
					}
				}
			case .Left:
				
				if (mParentMenuItemWidget != null)
				{
					var darkMenuItem = (DarkMenuItem)mParentMenuItemWidget;
					int32 selectIdx = darkMenuItem.mMenuWidget.mSelectIdx;
					darkMenuItem.CloseSubMenu();
					darkMenuItem.mMenuWidget.mSelectIdx = selectIdx;
				}
			default:
			}
		}
    }
}
