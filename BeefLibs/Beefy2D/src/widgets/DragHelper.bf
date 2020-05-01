using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using Beefy.events;

namespace Beefy.widgets
{
    public interface IDragInterface
    {
        void DragStart();
        void DragEnd();
        void MouseDrag(float x, float y, float dX, float dY);
    }

    public class DragHelper
    {
        public float mMouseX;
        public float mMouseY;
        public float mMouseDownX;
        public float mMouseDownY;
        public float mTriggerDist = 2.0f;
        public int32 mMinDownTicks = 0;
        public bool mAllowDrag = true;
        public bool mIsDragging = false;
        public MouseFlag mMouseFlags;
        IDragInterface mDragInterface;
        public Widget mWidget;
        public bool mAborted;
        public bool mPreparingForWidgetMove;        
        int32 mDownUpdateCnt;
        
        public WidgetWindow mTrackingWindow;

		//MouseEventHandler mMouseDownHandler;

		public float RelX
		{
			get
			{
				return mMouseX - mMouseDownX;
			}
		}

		public float RelY
		{
			get
			{
				return mMouseY - mMouseDownY;
			}
		}

        public this(Widget widget, IDragInterface dragInterface)
        {
			mWidget = widget;
			mDragInterface = dragInterface;

            mWidget.mOnMouseDown.Add(new => HandleMouseDown);
            mWidget.mOnMouseUp.Add(new => HandleMouseUp);
            mWidget.mOnMouseMove.Add(new => HandleMouseMove);
        }

		public ~this()
		{
			if (mIsDragging)
				CancelDrag();
			//Debug.Assert(!mIsDragging);

			mWidget.mOnMouseDown.Remove(scope => HandleMouseDown, true);
            mWidget.mOnMouseUp.Remove(scope => HandleMouseUp, true);
            mWidget.mOnMouseMove.Remove(scope => HandleMouseMove, true);
		}

		void HandleMouseDown(MouseEvent e)
		{
			MouseDown(e.mX, e.mY, e.mBtn, e.mBtnCount);
		}

		void HandleMouseUp(MouseEvent e)
		{
			MouseUp(e.mX, e.mY, e.mBtn);
		}

		void HandleMouseMove(MouseEvent e)
		{
			MouseMove(e.mX, e.mY);
		}

        public void HandleRemoved(Widget widget, Widget prevParent, WidgetWindow prevWindow)
        {
            CancelDrag();
        }

        public void HandleKeydown(KeyDownEvent theEvent)
        {
            if (theEvent.mKeyCode == KeyCode.Escape)
            {
                mAborted = true;
                CancelDrag();
            }
        }        

        public void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {
            mDownUpdateCnt = mWidget.mUpdateCnt;
            mMouseFlags |= (MouseFlag)(1 << btn);            
            mMouseDownX = x;
            mMouseDownY = y;
        }

        void SetHooks(bool doSet)
        {
			//Debug.WriteLine("SetHooks {0} {1}", this, doSet);

            if (doSet)                        
            {				
                mTrackingWindow = mWidget.mWidgetWindow;
                mTrackingWindow.mOnWindowKeyDown.Add(new => HandleKeydown);
                mWidget.mOnRemovedFromParent.Add(new => HandleRemoved);
            }
            else
            {                
                mTrackingWindow.mOnWindowKeyDown.Remove(scope => HandleKeydown, true);
                mWidget.mOnRemovedFromParent.Remove(scope => HandleRemoved, true);
            }
        }

        public void SetPreparingForWidgetMove(bool prepare)
        {
            SetHooks(!prepare);
        }

        public void CancelDrag()
        {
            if (!mIsDragging)
                return;
            SetHooks(false);
            mMouseFlags = default;
            mIsDragging = false;
            mDragInterface.DragEnd();            
        }        

        public void MouseUp(float x, float y, int32 btn)
        {
            mMouseFlags &= (MouseFlag)(~(1 << btn));
            if (((mMouseFlags & MouseFlag.Left) == 0) && (mIsDragging))
            {                
                CancelDrag();
            }
        }

        public void Update()
        {
            int32 ticksDown = mWidget.mUpdateCnt - mDownUpdateCnt;

            if (((mMouseFlags & MouseFlag.Left) != 0) && (ticksDown >= mMinDownTicks))
            {
                if ((!mIsDragging) && (mAllowDrag))
                {
                    if ((Math.Abs(mMouseX - mMouseDownX) >= mTriggerDist) || (Math.Abs(mMouseY - mMouseDownY) >= mTriggerDist))
                    {
                        SetHooks(true);
                        mPreparingForWidgetMove = false;
                        mAborted = false;
                        mIsDragging = true;
                        mDragInterface.DragStart();
                    }
                }

                if (mIsDragging)
                {
                    mIsDragging = true;
                    mDragInterface.MouseDrag(mMouseX, mMouseY, mMouseX - mMouseDownX, mMouseY - mMouseDownY);
                }
            }
        }

        public void MouseMove(float x, float y)
        {
            mMouseX = x;
            mMouseY = y;
            Update();
        }        
    }
}
