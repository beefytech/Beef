using System;
using System.Collections;
using System.Text;
using Beefy.events;
using Beefy.geom;
using System.Diagnostics;

namespace Beefy.widgets
{   
    public delegate void InfiniteScrollEventHandler(double scrollDelta);

    public abstract class InfiniteScrollbar : Widget, IDragInterface
    {
        /*public class Thumb : Widget, IDragInterface
        {
            public InfiniteScrollbar mScrollbar;
            public DragHelper mDraggableHelper ~ delete _;

            public this()
            {
                mDraggableHelper = new DragHelper(this, this);
            }

            public void DragStart()
            {
            }

            public void DragEnd()
            {
                mScrollbar.ScrollSetLevel(0.0);
            }

            public void MouseDrag(float x, float y, float dX, float dY)
            {                
                float parentX;
                float parentY;
                SelfToParentTranslate(x - mDraggableHelper.mMouseDownX, y - mDraggableHelper.mMouseDownY, out parentX, out parentY);

                mScrollbar.ScrollSetLevel(mScrollbar.GetAccelFracAt(parentX, parentY));
            }
        }*/

        public class Arrow : Widget
        {
            public InfiniteScrollbar mScrollbar;
            public int32 mDownTick;
            public bool mBack;

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
                base.MouseDown(x, y, btn, btnCount);
                DoScroll();

                mDownTick = 0;
            }

            protected virtual void DoScroll()
            {
                mScrollbar.FixedScroll(mBack ? -1 : 1);
            }

            public override void Update()
            {
                base.Update();

                if ((mMouseDown) && (mMouseOver))
                {
                    mDownTick++;

                    if (mDownTick > 20)
                        DoScroll();
                }                
            }
        }

        // configuration
        public double mFixedScrollAmt; // base amount to scroll for fixed scroll events e.g arrow clicks
        public double mScrollDeltaLevelAmount; // amount to change level during update while mouse is held down
        //public double mScrollMaxAccel; // absolute value maximum of acceleration (transient accel will range from [-this,this])
        public double mScrollMaxVelocity; // absolute value maximum of velocity; effective limit will be adjusted based on canonical acceleration
        
        public double mScrollThumbFrac; // scaled canonical acceleration, [-1,1]
        //public double mScrollAccel;
        public double mScrollVelocity;

        //public Thumb mThumb;        
        public Arrow mStartArrow;
        public Arrow mEndArrow;

        public float mBaseSize;
        public float mDualBarSizeOffset;

        public int32 mDownTick;
        public float mLastMouseX;
        public float mLastMouseY;

        public Event<InfiniteScrollEventHandler> mOnInfiniteScrollEvent ~ _.Dispose();

		public DragHelper mDraggableHelper ~ delete _;

		public this()
		{
			mDraggableHelper = new DragHelper(this, this);
		}

        public void HandleScroll(double scrollDelta)
        {
            if (mOnInfiniteScrollEvent.HasListeners)
                mOnInfiniteScrollEvent(scrollDelta);
        }

		public void DragStart()
		{
			let thumbPos = GetThumbPos();
			if (thumbPos.Contains(mDraggableHelper.mMouseDownX, mDraggableHelper.mMouseDownY))
			{
				// Is dragging thumb now
			}
			else
			{
				mDraggableHelper.CancelDrag();
			}
		}

		public void DragEnd()
		{
		    ScrollSetLevel(0.0);
		}

		public void MouseDrag(float x, float y, float dX, float dY)
		{
		    //float parentX;
		    //float parentY;
		    //SelfToParentTranslate(x - mDraggableHelper.mMouseDownX, y - mDraggableHelper.mMouseDownY, out parentX, out parentY);

		    //ScrollSetLevel(GetAccelFracAt(parentX, parentY));

			float accelFrac = GetAccelFracAt(mDraggableHelper.RelX, mDraggableHelper.RelY);
			ScrollSetLevel(accelFrac);
		}

        public virtual void FixedScroll(int32 delta)
        {
            HandleScroll(delta * mFixedScrollAmt);
        }

        public virtual void Init()
        {
            // test config values
            mFixedScrollAmt = 1;
            mScrollDeltaLevelAmount = 1.0;
            //mScrollMaxAccel = 1000;
            mScrollMaxVelocity = 250;
        }

        public virtual void ResizeContent()
        {
        }

        public void UpdateScrollPosition()
        {
            if (Math.Abs(mScrollVelocity) > 0.0001)
                HandleScroll(mScrollVelocity * 0.01);
        }

        public virtual void ScrollSetLevel(double scrollLevel)
        {
			double setScrollLevel = scrollLevel;
            setScrollLevel = Math.Min(Math.Max(-1.0, setScrollLevel), 1.0);
            mScrollThumbFrac = scrollLevel;
        
            //double vel = Math.Exp(Math.Log(mScrollMaxVelocity)*Math.Abs(mScrollThumbFrac)) - 1.0;

			double vel = Math.Pow(Math.Abs(mScrollThumbFrac) * 20, 1.8);
            if (mScrollThumbFrac < 0)
                vel = -vel;
            mScrollVelocity = vel;

            ResizeContent();
        }
        public virtual void ScrollDeltaLevel(double scrollAmount)
        {
            //ScrollSetLevel(mScrollThumbFrac + scrollAmount);
            HandleScroll(scrollAmount); // clicking in the bar feels better as a fixed scroll rather than changing the acceleration
        }

        public virtual float GetAccelFracAt(float dx, float dy)
        {
            return 0;
        }

		public virtual Rect GetThumbPos()
		{
			return .();
		}

        public override void Update()
        {
            base.Update();

            if ((mMouseDown) && (mMouseOver))
            {
                mDownTick++;

                if ((mDownTick > 20) && (mUpdateCnt % 5 == 0) && (!mDraggableHelper.mIsDragging))
                {
					 let thumbPos = GetThumbPos();

                    if (mLastMouseY < thumbPos.Top)
                        ScrollDeltaLevel(-mScrollDeltaLevelAmount);
                    else if (mLastMouseY > thumbPos.Bottom)
                        ScrollDeltaLevel(mScrollDeltaLevelAmount);
                }
            }

            UpdateScrollPosition();
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

			let thumbPos = GetThumbPos();

            base.MouseDown(x, y, btn, btnCount);
            if (mLastMouseY < thumbPos.Top)
                ScrollDeltaLevel(-mScrollDeltaLevelAmount);
            else if (mLastMouseY > thumbPos.Bottom)
                ScrollDeltaLevel(mScrollDeltaLevelAmount);
            mDownTick = 0;
        }

        public override void MouseWheel(float x, float y, int32 delta)
        {
            FixedScroll(-delta);
        }
    }
}
