using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.theme;
using Beefy.events;
using Beefy.utils;
using Beefy.geom;
using Beefy.gfx;
using System.Diagnostics;

namespace Beefy.widgets
{
    public class ScrollContentContainer : Widget
    {
        public this()
        {
            mSelfMouseVisible = false;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);
            //using (g.PushColor(0x80FF0000))
                //g.FillRect(0, 0, mWidth, mHeight);
        }

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
		}
    }    

    public class ScrollableWidget : Widget
    {
        public Scrollbar mHorzScrollbar;
        public Scrollbar mVertScrollbar;
        public Insets mScrollbarInsets = new Insets() ~ delete _;
        public ScrollContentContainer mScrollContentContainer;
        public Widget mScrollContent;
        public Insets mScrollContentInsets = new Insets() ~ delete _;
        public SmoothValue mVertPos = new SmoothValue() ~ delete _;
        public SmoothValue mHorzPos = new SmoothValue() ~ delete _;
        public bool mUpdatingContentPosition;
        public float mScrollbarBaseContentSizeOffset = 0;

        public this()
        {
            mScrollContentContainer = new ScrollContentContainer();
            mScrollContentContainer.mClipGfx = true;
            mScrollContentContainer.mClipMouse = true;
            AddWidget(mScrollContentContainer);
        }

		public ~this()
		{
			//TODO: Assert mScrollContentContainer is deleted
		}

        public virtual void InitScrollbars(bool wantHorz, bool wantVert)
        {            
            // We use 'AddWidgetAtIndex' to ensure that scrollbars are always the first child widget
            //  This is for enduring update order - so we make sure the scroll position is updated
            //  before the children are updated
            if (wantHorz)
            {
                if (mHorzScrollbar == null)
                {
                    mHorzScrollbar = ThemeFactory.mDefault.CreateScrollbar(Scrollbar.Orientation.Horz);
                }

				if (mHorzScrollbar.mParent == null)
				{
					mHorzScrollbar.Init();
					mHorzScrollbar.mOnScrollEvent.Add(new => ScrollEventHandler);
					AddWidgetAtIndex(0, mHorzScrollbar);
				}
            }
            if (wantVert)
            {
                if (mVertScrollbar == null)
                {
                    mVertScrollbar = ThemeFactory.mDefault.CreateScrollbar(Scrollbar.Orientation.Vert);
                }

				if (mVertScrollbar.mParent == null)
				{
					mVertScrollbar.Init();
					mVertScrollbar.mOnScrollEvent.Add(new => ScrollEventHandler);
					AddWidgetAtIndex(0, mVertScrollbar);
				}
            }
            else 
            {
                if (mVertScrollbar != null)
                {
                    RemoveWidget(mVertScrollbar);
                    mVertPos.Set(0, true);
                    mScrollContent.Resize(0, 0, mScrollContent.mWidth, mScrollContent.mHeight);
					delete mVertScrollbar;
                    mVertScrollbar = null;					
                }			  	
            }
        }

        public bool HorzScrollTo(double horzPos)
        {
            double aHorzPos = Math.Max(0, Math.Min(horzPos, mScrollContent.mWidth - mScrollContentContainer.mWidth));
            if (aHorzPos == mHorzPos.mDest)
                return false;

            mHorzPos.Set(aHorzPos);
            if (mHorzScrollbar != null)
            {
                mHorzScrollbar.mContentPos = mHorzPos.v;
                mHorzScrollbar.ResizeContent();                
            }
            UpdateContentPosition();
            return true;
        }

        public bool VertScrollTo(double vertPos, bool immediate = false)
        {
            double aVertPos = Math.Max(0, Math.Min(vertPos, mScrollContent.mHeight - mScrollContentContainer.mHeight));
            if (aVertPos == mVertPos.mDest)
                return false;

            if (mVertScrollbar != null)
                aVertPos = mVertScrollbar.AlignPosition(aVertPos);
            mVertPos.Set(aVertPos, immediate);
            if (mVertScrollbar != null)
            {                
                mVertScrollbar.mContentPos = mVertPos.v;
                mVertScrollbar.ResizeContent();                
            }
            UpdateContentPosition();
            return true;
        }

        public virtual void UpdateScrollbarData()
        {
            if (mHorzScrollbar != null)
            {
                mHorzScrollbar.mPageSize = mScrollContentContainer.mWidth;
                mHorzScrollbar.mContentSize = mScrollContent.mWidth;
                mHorzScrollbar.UpdateData();
            }

            if (mVertScrollbar != null)
            {
                mVertScrollbar.mPageSize = mScrollContentContainer.mHeight;
                mVertScrollbar.mContentSize = mScrollContent.mHeight;
                mVertScrollbar.UpdateData();
            }
        }

        public virtual void UpdateScrollbars()
        {            
            bool hasMultipleScrollbars = (mHorzScrollbar != null) && (mVertScrollbar != null);
                        
            if (mHorzScrollbar != null)
			{
				float yPos = Math.Max(0, mHeight - mScrollbarInsets.mBottom - mHorzScrollbar.mBaseSize);
                mHorzScrollbar.Resize(mScrollbarInsets.mLeft, yPos,
                    Math.Max(mWidth - mScrollbarInsets.mLeft - mScrollbarInsets.mRight - (hasMultipleScrollbars ? mVertScrollbar.mBaseSize + mVertScrollbar.mDualBarSizeOffset : 0), 0),
                    mHorzScrollbar.mBaseSize);								
			}

            if (mVertScrollbar != null)
                mVertScrollbar.Resize(Math.Max(mWidth - mScrollbarInsets.mRight - mVertScrollbar.mBaseSize, 0), mScrollbarInsets.mTop, mVertScrollbar.mBaseSize,
                    Math.Max(mHeight - mScrollbarInsets.mTop - mScrollbarInsets.mBottom - (hasMultipleScrollbars ? mHorzScrollbar.mBaseSize + mVertScrollbar.mDualBarSizeOffset : 0), 0));

            mScrollContentContainer.Resize(mScrollbarInsets.mLeft + mScrollContentInsets.mLeft, mScrollbarInsets.mTop + mScrollContentInsets.mTop,
                Math.Max(mWidth - mScrollbarInsets.mLeft - mScrollbarInsets.mRight - mScrollContentInsets.mLeft - mScrollContentInsets.mRight - ((mVertScrollbar != null) ? (mVertScrollbar.mBaseSize - mScrollbarBaseContentSizeOffset) : 0), 0),
                Math.Max(mHeight - mScrollbarInsets.mTop - mScrollbarInsets.mBottom - mScrollContentInsets.mTop - mScrollContentInsets.mBottom - ((mHorzScrollbar != null) ? (mHorzScrollbar.mBaseSize - mScrollbarBaseContentSizeOffset) : 0), 0));

            UpdateScrollbarData();
            ScrollPositionChanged();
        }

        public Rect CalcRectFromContent()
        {
            return Rect(-mScrollbarInsets.mLeft - mScrollContentInsets.mLeft, -mScrollbarInsets.mTop - mScrollContentInsets.mTop,
                Math.Max(mScrollContent.mWidth + mScrollbarInsets.mLeft + mScrollbarInsets.mRight + mScrollContentInsets.mLeft + mScrollContentInsets.mRight + ((mVertScrollbar != null) ? (mVertScrollbar.mBaseSize + mScrollbarBaseContentSizeOffset) : 0), 0),
                Math.Max(mScrollContent.mHeight + mScrollbarInsets.mTop + mScrollbarInsets.mBottom + mScrollContentInsets.mTop + mScrollContentInsets.mBottom + ((mHorzScrollbar != null) ? (mHorzScrollbar.mBaseSize + mScrollbarBaseContentSizeOffset) : 0), 0));
        }

        void ScrollEventHandler(ScrollEvent theEvent)
        {
            ScrollPositionChanged();
        }

        public virtual void UpdateContentPosition()
        {
            mUpdatingContentPosition = true;

            if ((mHorzScrollbar != null) && (mHorzScrollbar.mContentPos != mHorzPos.v))
            {
                mHorzScrollbar.mContentPos = mHorzPos.v;
                mHorzScrollbar.UpdateData();
				MarkDirty();
            }
            if ((mVertScrollbar != null) && (mVertScrollbar.mContentPos != mVertPos.v))
            {
                mVertScrollbar.mContentPos = mVertPos.v;
                mVertScrollbar.UpdateData();
				MarkDirty();
            }
            if (mScrollContent != null)
            {
                mScrollContent.Resize((int32)(-mHorzPos.v), (int32)(-mVertPos.v),
                    mScrollContent.mWidth, mScrollContent.mHeight);
            }

            mUpdatingContentPosition = false;
        }

        public virtual void ScrollPositionChanged()
        {
            if (mUpdatingContentPosition)
                return;

            if (mScrollContent != null)
            {
                if ((mHorzScrollbar != null) && (!mHorzPos.IsMoving))
                    mHorzPos.Set(mHorzScrollbar.mContentPos, true);
                if ((mVertScrollbar != null) && (!mVertPos.IsMoving))
                    mVertPos.Set(mVertScrollbar.mContentPos, true);

                UpdateContentPosition();
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            UpdateScrollbars();
        }

        public override void Update()
        {
            base.Update();

            if ((mHorzPos.IsMoving) || (mVertPos.IsMoving))
            {
                mHorzPos.Update();
                mVertPos.Update();
                UpdateContentPosition();
				MarkDirty();
            }
        }

        public override void MouseWheel(float x, float y, int32 delta)
        {
            base.MouseWheel(x, y, delta);
            if (mVertScrollbar != null)
                mVertScrollbar.MouseWheel(x, y, delta);
            else if (mHorzScrollbar != null)
                mHorzScrollbar.MouseWheel(x, y, delta);
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			double showPctV = 0;
			//double showPctH = 0;

			if (mVertScrollbar != null)
			{
				showPctV = (mVertScrollbar.mContentPos + mVertScrollbar.mPageSize * 0.5f) / mVertScrollbar.mContentSize;
			}

			mScrollContentInsets.Scale(newScale / oldScale);
			base.RehupScale(oldScale, newScale);

			if ((mVertScrollbar != null) && (mVertScrollbar.mContentPos != 0))
			{
				mVertScrollbar.mContentPos = showPctV * mVertScrollbar.mContentSize - mVertScrollbar.mPageSize * 0.5f;
				mVertScrollbar.Scroll(0);
			}
		}

		public virtual Rect GetVisibleContentRange()
		{
			return Rect(-mScrollContent.mX, -mScrollContent.mY, mScrollContentContainer.mWidth, mScrollContentContainer.mHeight);
		}
    }
}
