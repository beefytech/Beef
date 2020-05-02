using System;
using System.Collections;
using System.Text;
using Beefy.gfx;
using Beefy.widgets;

namespace Beefy.theme.dark
{
    public class DarkScrollbar : Scrollbar
    {
        public class DarkThumb : Scrollbar.Thumb
        {               
            public override void Draw(Graphics g)
            {
                base.Draw(g);
                DarkScrollbar scrollbar = (DarkScrollbar)mScrollbar;
                if (mScrollbar.mOrientation == Scrollbar.Orientation.Horz)
                    g.DrawButton(scrollbar.GetImage((mMouseOver || mMouseDown) ? DarkTheme.ImageIdx.ScrollbarThumb : DarkTheme.ImageIdx.ScrollbarThumbOver), 0, 0, mWidth);
                else
                    g.DrawButtonVert(scrollbar.GetImage((mMouseOver || mMouseDown) ? DarkTheme.ImageIdx.ScrollbarThumb : DarkTheme.ImageIdx.ScrollbarThumbOver), 0, 0, mHeight);
            }
        }

        public class DarkArrow : Scrollbar.Arrow
        {            
            public this(bool isBack)
            {
                mBack = isBack;
            }

            public override void Draw(Graphics g)
            {				
                base.Draw(g);

                DarkScrollbar scrollbar = (DarkScrollbar)mScrollbar;

                if (mMouseOver && mMouseDown)
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ShortButtonDown));
                else if (mMouseOver)
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ShortButton));

                if (mBack)
                {
                    if (mScrollbar.mOrientation == Scrollbar.Orientation.Horz)
                    {
                        g.PushScale(-1.0f, 1);
                        g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ScrollbarArrow), -DarkTheme.sUnitSize, 0);
                        g.PopMatrix();
                    }
                    else
                    {
                        g.PushScale(1, -1);
                        g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ScrollbarArrow), 0, -DarkTheme.sUnitSize);
                        g.PopMatrix();
                    }
                }
                else
                {
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ScrollbarArrow));
                }
            }
        }

        public this()
        {
            mBaseSize = DarkTheme.sUnitSize - 1;
            mDualBarSizeOffset = -2;

            mThumb = new DarkThumb();
            mThumb.mScrollbar = this;
            AddWidget(mThumb);
            
            mStartArrow = new DarkArrow(true);
            mStartArrow.mScrollbar = this;
            AddWidget(mStartArrow);
            
            mEndArrow = new DarkArrow(false);
            mEndArrow.mScrollbar = this;
            AddWidget(mEndArrow);
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			mBaseSize = DarkTheme.sUnitSize - 1;
			base.RehupScale(oldScale, newScale);
		}

        public Image GetImage(DarkTheme.ImageIdx image)
        {
            if (mOrientation == Orientation.Horz)
                return DarkTheme.sDarkTheme.GetImage(image);
            else
                return DarkTheme.sDarkTheme.GetImage((DarkTheme.ImageIdx)((int32)image + (int32)DarkTheme.ImageIdx.VertScrollbar - (int32)DarkTheme.ImageIdx.Scrollbar));
        }
        
        public override void Draw(Graphics g)
        {
            base.Draw(g);

            if (mOrientation == Orientation.Horz)
            {                
                g.DrawButton(GetImage(DarkTheme.ImageIdx.Scrollbar), 0, 0, mWidth);
            }
            else
            {                
                g.DrawButtonVert(GetImage(DarkTheme.ImageIdx.Scrollbar), 0, 0, mHeight);
            }
        }

		public override void DrawAll(Graphics g)
		{
			if ((mWidth <= 0) || (mHeight <= 0))
				return;

			base.DrawAll(g);
		}

        public override double GetContentPosAt(float x, float y)
        {
            float btnMargin = GS!(18);
            float sizeLeft = (mOrientation == Orientation.Horz) ? (mWidth - btnMargin * 2) : (mHeight - btnMargin * 2);

            if (mOrientation == Orientation.Horz)
            {                
                float trackSize = sizeLeft - mThumb.mWidth;
                float trackPct = (x - btnMargin) / trackSize;
                double contentPos = (mContentSize - mPageSize) * trackPct;
                return contentPos;
            }
            else
            {
                float trackSize = sizeLeft - mThumb.mHeight;
                float trackPct = (y - btnMargin) / trackSize;
                double contentPos = (mContentSize - mPageSize) * trackPct;
                return contentPos;
            }
        }

        public override void ResizeContent()
        {
            mStartArrow.Resize(0, 0, mBaseSize, mBaseSize);
            if (mOrientation == Orientation.Horz)
                mEndArrow.Resize(mWidth - mBaseSize, 0, mBaseSize, mBaseSize);
            else
                mEndArrow.Resize(0, mHeight - mBaseSize, mBaseSize, mBaseSize);

            float btnMargin = GS!(18);

            float sizeLeft = (mOrientation == Orientation.Horz) ? (mWidth - btnMargin * 2) : (mHeight - btnMargin * 2);

            if ((mPageSize < mContentSize) && (mContentSize > 0))
            {
                double pagePct = mPageSize / mContentSize;
                mThumb.mVisible = true;
				
				if (sizeLeft >= GS!(12))
				{
					mThumb.mVisible = true;
					mStartArrow.mVisible = true;
					mEndArrow.mVisible = true;
				}
				else
				{						       
     				mThumb.mVisible = false;
					mStartArrow.mVisible = false;
					mEndArrow.mVisible = false;
	                /*sizeLeft = 0;
					sizeLeft = (mOrientation == Orientation.Horz) ? (mWidth) : (mHeight);
					btnMargin = 0;*/
				}

				double thumbSize = Math.Max(Math.Round(Math.Min(sizeLeft, Math.Max(GS!(12), sizeLeft * pagePct))), 0);
				double trackSize = Math.Max(sizeLeft - ((thumbSize) - (sizeLeft * pagePct)) + 1, 0);
				if (mOrientation == Orientation.Horz)
				    mThumb.Resize((float)(btnMargin + (float)Math.Round(mContentPos / mContentSize * trackSize)), 0, (float)thumbSize, mBaseSize);
				else
				    mThumb.Resize(0, (float)(btnMargin + (float)Math.Round(mContentPos / mContentSize * trackSize)), mBaseSize, (float)thumbSize);
            }
            else
            {
                mThumb.mVisible = false;
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

            ResizeContent();
        }
    }
}
