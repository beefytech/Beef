using System;
using System.Collections;
using System.Text;
using Beefy.gfx;
using System.Diagnostics;

namespace Beefy.widgets
{
    public interface IDockable
    {
        bool CanDock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align);
        void Dock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align);
        void DrawDockPreview(Graphics g);
    }

    public interface ICustomDock
    {
        void Draw(Graphics g);
        void Dock(IDockable dockingWidget);
    }

    public class DockedWidget : Widget, IDockable
    {
        public bool mHasFillWidget;
		public bool mIsFillWidget;
		public bool mAutoClose = true; // Close when last tab is removed
        public float mRequestedWidth; // Set when we drag a sizer
        public float mRequestedHeight; // Set when we drag a sizer
        public float mSizePriority;
        public DockingFrame mParentDockingFrame;

        public bool mAllowInterpolate;
        public float mInterpolatePct = 1.0f;
        public float mStartRootX;
        public float mStartRootY;
        public float mStartWidth;
        public float mStartHeight;

        public float mEndX;
        public float mEndY;
        public float mEndWidth;
        public float mEndHeight;

        public this()
        {
            mClipMouse = true;
            mClipGfx = true;
        }

		public virtual DockingFrame GetRootDockingFrame()
		{
			var parentFrame = mParentDockingFrame;
			while (parentFrame != null)
			{
				if (parentFrame.mParentDockingFrame == null)
					break;
				parentFrame = parentFrame.mParentDockingFrame;
			}
			return parentFrame;
		}

		public virtual void Rehup()
		{
			mHasFillWidget = mIsFillWidget;
		}

        public virtual void SetRequestedSize(float width, float height)
        {
            mRequestedWidth = width;
            mRequestedHeight = height;
        }

        public virtual ICustomDock GetCustomDock(IDockable dockable, float x, float y)
        {
            return null;
        }

        public bool CanDock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
        {
            return true;
        }

        public virtual void Dock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
        {
            if (mParentDockingFrame != null) // TODO: Undock
            {
            }
        }

        public virtual void DrawDockPreview(Graphics g)
        {
            using (g.PushColor(0x80FFFFFF))
                DrawAll(g);
        }

        public void StartInterpolate(float rootX, float rootY, float width, float height)
        {
            mInterpolatePct = 0;
            mStartRootX = rootX;
            mStartRootY = rootY;
            mStartWidth = mWidth;
            mStartHeight = mHeight;
        }

        public void StartInterpolate()
        {
            if ((mAllowInterpolate) && (mInterpolatePct == 1.0f))
            {
                mInterpolatePct = 0;
                mParent.SelfToRootTranslate(mX, mY, out mStartRootX, out mStartRootY);
                mStartWidth = mWidth;
                mStartHeight = mHeight;

                mEndX = mX;
                mEndY = mY;
                mEndWidth = mWidth;
                mEndHeight = mHeight;
            }
        }

        void UpdateInterpolation()
        {
            if (mInterpolatePct != 1.0f)
            {
                mInterpolatePct = Math.Min(mInterpolatePct + 0.1f, 1.0f);

                float startX;
                float startY;
                mParent.RootToSelfTranslate(mStartRootX, mStartRootY, out startX, out startY);
                Resize(
                    Utils.Lerp(startX, mEndX, mInterpolatePct),
                    Utils.Lerp(startY, mEndY, mInterpolatePct),
                    Utils.Lerp(mStartWidth, mEndWidth, mInterpolatePct),
                    Utils.Lerp(mStartHeight, mEndHeight, mInterpolatePct));
            }
        }

        public override void Update()
        {
            base.Update();
            UpdateInterpolation();

            Debug.Assert((mParent == mParentDockingFrame) || (mParentDockingFrame == null));

            // Allow to interpolate after first update
            mAllowInterpolate = true;
        }

        public virtual void ResizeDocked(float x, float y, float width, float height)
        {
            if (mInterpolatePct != 1.0f)
            {
                mEndX = x;
                mEndY = y;
                mEndWidth = width;
                mEndHeight = height;
                UpdateInterpolation();
            }
            else
                Resize(x, y, width, height);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
        }
    }

    public class DockingProxy : DockedWidget
    {
        Widget mWidget;

        public this(Widget widget)
        {
            mWidget = widget;
            AddWidget(mWidget);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mWidget.Resize(0, 0, width, height);
        }
    }
}
