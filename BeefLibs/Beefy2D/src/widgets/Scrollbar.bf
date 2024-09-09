using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using Beefy.events;

namespace Beefy.widgets
{   
    public delegate void ScrollEventHandler(ScrollEvent theEvent);

    public abstract class Scrollbar : Widget
    {
        public class Thumb : Widget, IDragInterface
        {
            public Scrollbar mScrollbar;
            public DragHelper mDraggableHelper ~ delete _;
			public Event<delegate void(float, float)> mOnDrag ~ _.Dispose();

            public this()
            {
                mDraggableHelper = new DragHelper(this, this);
            }

            public void DragStart()
            {
            }

            public void DragEnd()
            {
            }

            public void MouseDrag(float x, float y, float dX, float dY)
            {                
                float parentX;
                float parentY;
                SelfToParentTranslate(x - mDraggableHelper.mMouseDownX, y - mDraggableHelper.mMouseDownY, out parentX, out parentY);
				mOnDrag(parentX, parentY);
                mScrollbar.ScrollTo(mScrollbar.GetContentPosAt(parentX, parentY));
            }

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                // For popups that normally don't get focus, give them focus so thumb tracking works
                if ((mWidgetWindow.mParent != null) && (mWidgetWindow.mParent.mHasFocus))                
                    mWidgetWindow.GotFocus();                                    

                base.MouseDown(x, y, btn, btnCount);                
            }
        }

        public class Arrow : Widget
        {
            public Scrollbar mScrollbar;
            public int32 mDownTick;
            public bool mBack;

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                base.MouseDown(x, y, btn, btnCount);
                DoScroll();

                mDownTick = 0;
            }

            protected virtual void DoScroll(float pct = 1.0f)
            {
                float anAmt = mScrollbar.GetScrollIncrement() * pct;
                if (mBack)
                    anAmt = -anAmt;
                mScrollbar.Scroll(anAmt);
            }

            public override void Update()
            {
                base.Update();

                if ((mMouseDown) && (mMouseOver))
                {
                    mDownTick++;
                    
                    if (mDownTick > 20)
                    {
                        if (mScrollbar.mAlignItems)
                        {
                            if ((mDownTick - 20) % 3 == 0)
                                DoScroll();
                        }
                        else
                        {
                            DoScroll(1.0f / 3.0f);
                        }
                    }
                }                
            }
        }

        public enum Orientation
        {
            Horz,
            Vert
        }

		public double mContentStart;
        public double mContentSize;
        public double mPageSize;
        public double mContentPos;        
        public Orientation mOrientation;

        public Thumb mThumb;        
        public Arrow mStartArrow;
        public Arrow mEndArrow;

        public float mBaseSize;
        public float mDualBarSizeOffset;

        public int32 mScrollDir;
        public int32 mDownTick;
        public float mLastMouseX;
        public float mLastMouseY;

        public float mScrollIncrement;
        public bool mAlignItems;
		public bool mDoAutoClamp = true;
		public bool mAllowMouseWheel = true;

        public Event<ScrollEventHandler> mOnScrollEvent ~ _.Dispose();

        public float GetScrollIncrement()
        {
            if (mScrollIncrement == 0)
                return (float)(mPageSize / 10.0f);
            return mScrollIncrement;
        }

        public virtual void Init()
        {
        }

        public virtual void ResizeContent()
        {
        }

        public virtual double AlignPosition(double pos)
        {
            if (mAlignItems)
                return (float)Math.Round(pos / mScrollIncrement) * mScrollIncrement;
            return pos;
        }

        public virtual void ScrollTo(double pos)
        {
			var pos;
			pos -= mContentStart;

			MarkDirty();
            double oldPos = mContentPos;

            mContentPos = pos;
            mContentPos = Math.Min(mContentPos, mContentSize - mPageSize);
            mContentPos = Math.Max(mContentPos, 0);

            if (mAlignItems)
                mContentPos = (float)Math.Round(mContentPos / mScrollIncrement) * mScrollIncrement;

            ResizeContent();            
            if ((mOnScrollEvent.HasListeners) && (oldPos != mContentPos))
            {
                ScrollEvent scrollEvent = scope ScrollEvent();
                scrollEvent.mOldPos = oldPos + mContentStart;
                scrollEvent.mNewPos = mContentPos + mContentStart;
                mOnScrollEvent(scrollEvent);
            }
        }

        public virtual void UpdateData()
        {
			if (mDoAutoClamp)
            	Scroll(0);
            ResizeContent();
        }

        public virtual void Scroll(double amt)
        {
            ScrollTo(mContentPos + amt + mContentStart);
        }

        public virtual double GetContentPosAt(float x, float y)
        {
            return 0;
        }

        public override void Update()
        {
            base.Update();

            if ((mMouseDown) && (mMouseOver))
            {
                mDownTick++;

                if ((mDownTick > 20) && (mUpdateCnt % 5 == 0))
                {
                    if (((mOrientation == Orientation.Horz) && (mLastMouseX < mThumb.mX)) ||
                        ((mOrientation == Orientation.Vert) && (mLastMouseY < mThumb.mY)))
                        Scroll(-mPageSize);
                    else
                        Scroll(mPageSize);
                }
            }
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);
            mLastMouseX = x;
            mLastMouseY = y;
        }

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            mLastMouseX = x;
            mLastMouseY = y;

            base.MouseDown(x, y, btn, btnCount);
            if (((mOrientation == Orientation.Horz) && (mLastMouseX < mThumb.mX)) ||
                ((mOrientation == Orientation.Vert) && (mLastMouseY < mThumb.mY)))
                Scroll(-mPageSize);
            else
                Scroll(mPageSize);
            mDownTick = 0;
        }

        public override void MouseWheel(float x, float y, float deltaX, float deltaY)
        {
			if (!mAllowMouseWheel)
			{
				base.MouseWheel(x, y, deltaX, deltaY);
				return;
			}

			float delta = (mOrientation == .Horz) ? deltaX : -deltaY;
            Scroll(GetScrollIncrement() * delta);
        }
    }
}
