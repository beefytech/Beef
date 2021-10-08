using System;
using Beefy.theme;
using System.Collections;
using System.Text;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.geom;

namespace Beefy.theme.dark
{
    public class DarkInfiniteScrollbar : InfiniteScrollbar
    {
        /*public class DarkThumb : InfiniteScrollbar.Thumb
        {               
            public override void Draw(Graphics g)
            {
                base.Draw(g);
                DarkInfiniteScrollbar scrollbar = (DarkInfiniteScrollbar)mScrollbar;
                g.DrawButtonVert(scrollbar.GetImage((mMouseOver || mMouseDown) ? DarkTheme.ImageIdx.ScrollbarThumb : DarkTheme.ImageIdx.ScrollbarThumbOver), 0, 0, mHeight);
            }
        }*/

        public class DarkArrow : InfiniteScrollbar.Arrow
        {            
            public this(bool isBack)
            {
                mBack = isBack;
            }

            public override void Draw(Graphics g)
            {
                base.Draw(g);

                DarkInfiniteScrollbar scrollbar = (DarkInfiniteScrollbar)mScrollbar;

                if (mMouseOver && mMouseDown)
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ShortButtonDown));
                else if (mMouseOver)
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ShortButton));

                if (mBack)
                {
                    g.PushScale(1, -1);
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ScrollbarArrow), 0, -DarkTheme.sUnitSize);
                    g.PopMatrix();
                }
                else
                {
                    g.Draw(scrollbar.GetImage(DarkTheme.ImageIdx.ScrollbarArrow));
                }
            }
        }

		public override void RehupScale(float oldScale, float newScale)
		{
			mBaseSize = DarkTheme.sUnitSize - 1;
			base.RehupScale(oldScale, newScale);
		}

        public this()
        {
            mBaseSize = DarkTheme.sUnitSize - 1;
            mDualBarSizeOffset = -2;

            /*mThumb = new DarkThumb();
            mThumb.mScrollbar = this;
            AddWidget(mThumb);*/
            
            mStartArrow = new DarkArrow(true);
            mStartArrow.mScrollbar = this;
            AddWidget(mStartArrow);
            
            mEndArrow = new DarkArrow(false);
            mEndArrow.mScrollbar = this;
            AddWidget(mEndArrow);
        }

        public Image GetImage(DarkTheme.ImageIdx image)
        {
            return DarkTheme.sDarkTheme.GetImage((DarkTheme.ImageIdx)((int32)image + (int32)DarkTheme.ImageIdx.VertScrollbar - (int32)DarkTheme.ImageIdx.Scrollbar));
        }

		public override Beefy.geom.Rect GetThumbPos()
		{
			float btnMargin = GS!(18);
			float sizeLeft = (mHeight - btnMargin * 2);

			float pagePct = 0.125f;
			float thumbSize = Math.Min(sizeLeft, Math.Max(GS!(16), sizeLeft * pagePct));

			bool wasNeg = mScrollThumbFrac < 0;
			float trackPct = (float)Math.Pow(Math.Abs(mScrollThumbFrac) * 1000, 0.5f) / 200;

			if (wasNeg)
				trackPct = 0.5f - trackPct;
			else
				trackPct = 0.5f + trackPct;

			float thumbPos = Math.Clamp(btnMargin + trackPct*sizeLeft - thumbSize/2, btnMargin, mHeight - btnMargin - thumbSize);

			return Rect(0, thumbPos, mWidth, thumbSize);
		}

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            g.DrawButtonVert(GetImage(DarkTheme.ImageIdx.Scrollbar), 0, 0, mHeight);

			let thumbPos = GetThumbPos();
			bool isOver = mMouseOver && thumbPos.Contains(mLastMouseX, mLastMouseY);

			using (g.PushColor(ThemeColors.Widget.DarkInfiniteScrollbar030.Color))
			{
				float y0 = mHeight / 2;
				float y1 = thumbPos.mY + thumbPos.mHeight / 2;
				g.FillRect(GS!(6), Math.Min(y0, y1), mWidth - GS!(12), Math.Abs(y0 - y1));
			}

			g.DrawButtonVert(GetImage(isOver ? DarkTheme.ImageIdx.ScrollbarThumb : DarkTheme.ImageIdx.ScrollbarThumbOver), 0, thumbPos.mY, thumbPos.mHeight);

			//mThumb.mVisible = true;
			//mThumb.Resize(0, btnMargin + thumbPos - (int)(thumbSize*0.5f), mBaseSize, thumbSize);
        }

        public override float GetAccelFracAt(float dx, float dy)
        {
            float btnMargin = GS!(18);
            float sizeLeft = (mHeight - btnMargin * 2);
            
            float trackSize = sizeLeft - GS!(20);
            return dy / trackSize * 2.0f;
        }

        public override void ResizeContent()
        {
            mStartArrow.Resize(0, 0, mBaseSize, mBaseSize);
            mEndArrow.Resize(0, mHeight - mBaseSize, mBaseSize, mBaseSize);

            //float btnMargin = GS!(18);
            //float sizeLeft = (mHeight - btnMargin * 2);

            /*float pagePct = 0.125f;
            float thumbSize = Math.Min(sizeLeft, Math.Max(GS!(12), sizeLeft * pagePct));
            float trackSize = sizeLeft + 1; // sizeLeft - ((thumbSize) - (sizeLeft * pagePct)) + 1;
            float trackPct = ((float)mScrollThumbFrac + 1.0f) * 0.5f;*/

            //float thumbPos = Utils.Lerp(thumbSize * 0.5f, trackSize - (thumbSize * 0.5f), trackPct);
            
            //mThumb.mVisible = true;
            //mThumb.Resize(0, btnMargin + thumbPos - (int)(thumbSize*0.5f), mBaseSize, thumbSize);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

            ResizeContent();
        }
    }
}
