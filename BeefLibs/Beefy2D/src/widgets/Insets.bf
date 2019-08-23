using System;
using System.Collections.Generic;
using System.Text;
using Beefy.geom;

namespace Beefy.widgets
{
    public class Insets
    {
        public float mTop;
        public float mLeft;        
        public float mBottom;
        public float mRight;

        public float Horz
        {
            get { return mRight + mLeft; }
        }

        public float Vert
        {
            get { return mBottom + mTop; }
        }

        public this(float top = 0, float left = 0, float bottom = 0, float right = 0)
        {
            mTop = top;
            mLeft = left;
            mBottom = bottom;
            mRight = right;
        }

        public void Set(float top, float left, float bottom, float right)
        {
            mTop = top;
            mLeft = left;
            mBottom = bottom;
            mRight = right;
        }
		
		public void ApplyTo(ref Rect rect)
		{
			rect.mX += mLeft;
			rect.mY += mTop;
			rect.mWidth -= mLeft + mRight;
			rect.mHeight -= mTop + mBottom;
		}
		
		public void Scale(float scale)
		{
			Utils.RoundScale(ref mTop, scale);
			Utils.RoundScale(ref mLeft, scale);
			Utils.RoundScale(ref mRight, scale);
			Utils.RoundScale(ref mBottom, scale);
		}
    }
}
