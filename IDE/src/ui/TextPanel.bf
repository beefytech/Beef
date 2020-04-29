using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.widgets;
using Beefy.geom;
using Beefy.theme.dark;

namespace IDE.ui
{
    public abstract class TextPanel : Panel
    {
        public HoverWatch mHoverWatch;
		public bool mDisposed;
		public int32 mLastFocusTick;

        public abstract SourceEditWidget EditWidget
        {
             get;
        }

        public QuickFind mQuickFind;

		public ~this()
		{
			Widget.RemoveAndDelete(mQuickFind);
		}

        public virtual bool EscapeHandler()
        {
            if (mHoverWatch != null)
            {
                mHoverWatch.Close();
                return true;
            }

            if (mQuickFind != null)
            {
                if (mQuickFind.Close())
                    return true;
            }

            var editWidget = EditWidget;
            if (editWidget.Content.HasSelection())
            {
                editWidget.Content.mSelection = null;
                return true;
            }

            return false;
        }

        public virtual void Dispose()
        {
            if (mQuickFind != null)
            {
                // 
            }

            if (mHoverWatch != null)
                mHoverWatch.Close();

			mDisposed = true;
        }

		public override void ParentDeleted()
		{
			if (!mDisposed)
				Dispose();

			base.ParentDeleted();
		}

        public virtual void EditGotFocus()
        {

        }

        public virtual void EditLostFocus()
        {
            
        }

        public bool CheckAllowHoverWatch()
        {
            if (mHoverWatch != null)
            {
				bool hasActiveHoverWatch = false;

                if (mHoverWatch.mEditWidget != null)
                    hasActiveHoverWatch = true;

                // Allow a sloped area for the cursor to move down-right to the hover widget
                //  without causing the hover watch to close because we moved the cursor away
                //  from the hovered text
                float xOfs = DarkTooltipManager.sLastRelMousePos.x - mHoverWatch.mOpenMousePos.x;
                float yOfs = DarkTooltipManager.sLastRelMousePos.y - mHoverWatch.mOpenMousePos.y;
                if ((xOfs >= 0) && (yOfs >= 0) && (yOfs < 24))
                {
                    if (xOfs < yOfs * 4 + 6)
                    {
                        hasActiveHoverWatch = true;
                    }
                }

                if ((mHoverWatch.mWidgetWindow != null) && (mHoverWatch.mWidgetWindow.mCaptureWidget != null))
                {
                    // Don't close if hover has mouse mousecapture (particularly while dragging scrollbar)
                    hasActiveHoverWatch = true;
                }

				if ((mHoverWatch.mWidgetWindow != null) && (mHoverWatch.mWidgetWindow.mOverWidget != null))
				{
				    // Don't close if hover has mouse mousecapture (particularly while dragging scrollbar)
				    hasActiveHoverWatch = true;
				}

				/*if (mHoverWatch.mWidgetWindow.mHasMouseInside)
					hasActiveHoverWatch = true;*/

				if (hasActiveHoverWatch)
				{
					//if (mHoverWatch.mCloseCountdown > 0)
						//Debug.WriteLine("CheckAllowHoverWatch mCloseCountdown = 0");
					mHoverWatch.mCloseCountdown = 0;
					return false;
				}
				else
				{
					//Debug.WriteLine("CheckAllowHoverWatch !hasActiveHoverWatch");
				}
            }

			if (!mWidgetWindow.mHasMouseInside)
				return false;

            for (WidgetWindow childWindow in mWidgetWindow.mChildWindows)
            {
                // We're showing an error dialog, don't close mouseover
                var dialog = childWindow.mRootWidget as Dialog;
                if ((dialog != null) && (dialog.mWindowFlags.HasFlag(BFWindow.Flags.Modal)))
                {
                    return false;
                }
            }

			if (DarkTooltipManager.sTooltip != null)
				return false;

            var checkWidget = DarkTooltipManager.sLastMouseWidget;
            while ((checkWidget != null) && (checkWidget.mWidgetWindow != null))
            {
				if (checkWidget.mWidgetWindow != mWidgetWindow)
					return false; // We have to at least be over this window
                if (checkWidget is HoverWatch)
                {
                    var hw = (HoverWatch)checkWidget;
                    if (hw.HasContentAt(hw.mWidgetWindow.mMouseX, hw.mWidgetWindow.mMouseY))
                        return false;
                }
                if (checkWidget is AutoComplete.AutoCompleteListWidget)
                    return false;
                checkWidget = checkWidget.mParent;
            }

            return true;
        }

        public override void SetFocus()
        {
            base.SetFocus();
            EditWidget.SetFocus();
        }

        public virtual void ShowQuickFind(bool isReplace)
        {
            if (mQuickFind != null)
			{
                mQuickFind.Close();
				delete mQuickFind;
			}
            mQuickFind = new QuickFind(this, EditWidget, isReplace);
            mWidgetWindow.SetFocus(mQuickFind.mFindEditWidget);
            Resize(mX, mY, mWidth, mHeight);
        }

        public virtual void FindNext(int32 dir = 1)
        {
            if (mQuickFind == null)
            {
                ShowQuickFind(false);
				if (mQuickFind == null)
					return;
                mQuickFind.Close();
            }
            mQuickFind.FindNext(dir, false);
        }

        protected virtual void ResizeComponents()
        {
            if (mQuickFind != null)
				mQuickFind.ResizeSelf();
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        public virtual void RecordHistoryLocation(bool ignoreIfClose = false)
        {

        }

        public override void Update()
        {
            base.Update();
            if (mQuickFind != null)
                mQuickFind.UpdateData();
			if (mHoverWatch != null)
			{
				Debug.Assert(mHoverWatch.mTextPanel == this);
			}
        }

		public virtual void Clear()
		{

		}
    }

	class EmptyTextPanel : TextPanel
	{
		public bool mAllowHoverWatch = false;
		public Rect? mHoverWatchRect;

		public override SourceEditWidget EditWidget
		{
		    get
			{
				return null;
			}
		}

		public override void Update()
		{
			base.Update();

			if (!CheckAllowHoverWatch())
                return;

			if (IDEApp.sApp.HasPopupMenus())
				return;

			if (mHoverWatchRect.HasValue)
			{
				float x;
				float y;
				RootToSelfTranslate(mWidgetWindow.mMouseX, mWidgetWindow.mMouseY, out x, out y);
				if (mHoverWatchRect.Value.Contains(x, y))
					return;
			}

			if ((mHoverWatch != null) && (mHoverWatch.mCloseDelay == 0))
                mHoverWatch.Close();
		}
	}
}
