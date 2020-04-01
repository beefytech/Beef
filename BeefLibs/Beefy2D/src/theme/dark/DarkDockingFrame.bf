using System;
using System.Collections.Generic;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace Beefy.theme.dark
{
    public class DarkDockingFrame : DockingFrame
    {
        IDockable mCurDragTarget;
		public bool mDrawBkg = true;

        public this()
        {
            mMinWindowSize = GS!(100);

			mMinWindowSize = GS!(32);
			mDragMarginSize = GS!(64);
			mDragWindowMarginSize = GS!(10);

			mWindowMargin = 0;
			mSplitterSize = GS!(6.0f);
			mWindowSpacing = GS!(2.0f);
        }

		public ~this()
		{
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);

			mMinWindowSize = GS!(100);

			mMinWindowSize = GS!(32);
			mDragMarginSize = GS!(64);
			mDragWindowMarginSize = GS!(10);

			if (mParentDockingFrame == null)
				mWindowMargin = GS!(1);
			mSplitterSize = GS!(6.0f);
			mWindowSpacing = GS!(2.0f);
		}

		public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
		{
			base.RemovedFromParent(previousParent, window);
		}

        public override void Draw(Graphics g)
        {
			if (mDrawBkg)
			{
	            using (g.PushColor(DarkTheme.COLOR_BKG))
	                g.FillRect(0, 0, mWidth, mHeight);
			}
            base.Draw(g);            
        }

        public override void DrawAll(Graphics g)
        {            
            base.DrawAll(g);           

            if ((mCurDragTarget != null) && (mWidgetWindow.mAlpha == 1.0f))
            {
                using (g.PushTranslate(mWidgetWindow.mMouseX, mWidgetWindow.mMouseY))
                {
                    mCurDragTarget.DrawDockPreview(g);
                }
            }
        }
        
        public override void DrawDraggingDock(Graphics g)
        {            
            if (mDraggingCustomDock != null)
            {
                mDraggingCustomDock.Draw(g);
            }
            else
            {
                if (mDraggingAlign == WidgetAlign.Inside)
                {
                    using (g.PushColor(0x30FFFFFF))
                        g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.WhiteCircle], mDraggingRef.mX, mDraggingRef.mY, mDraggingRef.mWidth, mDraggingRef.mHeight);
                }
                else
                {
                    DockedWidget widgetRef = mDraggingRef ?? this;

                    Matrix matrix = Matrix.IdentityMatrix;
                    float dist = 0;

                    if (mDraggingAlign == WidgetAlign.Left)
                    {
                        matrix.Rotate(Math.PI_f);
                        matrix.Translate(widgetRef.mX, widgetRef.mY + widgetRef.mHeight);
                        dist = widgetRef.mHeight;
                    }
                    else if (mDraggingAlign == WidgetAlign.Right)
                    {
                        matrix.Rotate(0);
                        matrix.Translate(widgetRef.mX + widgetRef.mWidth, widgetRef.mY);
                        dist = widgetRef.mHeight;
                    }
                    else if (mDraggingAlign == WidgetAlign.Top)
                    {
                        matrix.Rotate(-Math.PI_f / 2);
                        matrix.Translate(widgetRef.mX, widgetRef.mY);
                        dist = widgetRef.mWidth;
                    }
                    else if (mDraggingAlign == WidgetAlign.Bottom)
                    {
                        matrix.Rotate(Math.PI_f / 2);
                        matrix.Translate(widgetRef.mX + widgetRef.mWidth, widgetRef.mY + widgetRef.mHeight);
                        dist = widgetRef.mWidth;
                    }

                    using (g.PushMatrix(matrix))
                    {
                        if (mDraggingRef != null)
                            g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.GlowDot], GS!(-9), GS!(-8), GS!(20), dist + GS!(8) * 2);
                        else
                            g.DrawBox(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.WhiteCircle], GS!(-13), GS!(-8), GS!(20), dist + GS!(8) * 2);

                        int32 arrowCount = 3;
                        if (dist < 80)
                            arrowCount = 1;
                        else if (dist < 120)
                            arrowCount = 2;
                        float arrowSep = dist / (arrowCount + 1);
                        for (int32 arrowNum = 0; arrowNum < arrowCount; arrowNum++)
                            g.Draw(DarkTheme.sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.ArrowRight], GS!(-22), GS!(-9) + (arrowNum + 1) * arrowSep);
                    }
                }
                
            }
        }

        public override void ShowDragTarget(IDockable draggingItem)
        {
            base.ShowDragTarget(draggingItem);
            mCurDragTarget = draggingItem;
        }

        public override void HideDragTarget(IDockable draggingItem, bool executeDrag = false)
        {
            base.HideDragTarget(draggingItem, executeDrag);
            mCurDragTarget = null;
        }
    }
}
