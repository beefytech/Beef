using System;
using System.Collections.Generic;
using System.Text;
using Beefy.gfx;

namespace Beefy.geom
{
    public struct Rect
    {
        public float mX;
        public float mY;
        public float mWidth;
        public float mHeight;

		public float Left
		{
			get
			{
				return mX;
			}

			set mut
			{
				mWidth = mX + mWidth - value;
				mX = value;
			}
		}

		public float Top
		{
			get
			{
				return mY;
			}

			set mut
			{
				mHeight = mY + mHeight - value;
				mY = value;
			}
		}

		public float Right
		{
			get
			{
				return mX + mWidth;
			}

			set mut
			{
				mWidth = value - mX;
			}
		}

		public float Bottom
		{
			get
			{
				return mY + mHeight;
			}

			set mut
			{
				mHeight = value - mY;
			}
		}

		public float Width
		{
			get
			{
				return mWidth;
			}

			set mut
			{
				mWidth = value;
			}
		}

		public float Height
		{
			get
			{
				return mHeight;
			}

			set mut
			{
				mHeight = value;
			}
		}

        public this(float x = 0, float y = 0, float width = 0, float height = 0)
        {
            mX = x;
            mY = y;
            mWidth = width;
            mHeight = height;
        }

        public void Set(float x = 0, float y = 0, float width = 0, float height = 0) mut
        {
            mX = x;
            mY = y;
            mWidth = width;
            mHeight = height;
        }

        public bool Intersects(Rect rect)
        {
            return !((rect.mX + rect.mWidth <= mX) ||
                    (rect.mY + rect.mHeight <= mY) ||
                    (rect.mX >= mX + mWidth) ||
                    (rect.mY >= mY + mHeight));
        }

        public void SetIntersectionOf(Rect rect1, Rect rect2) mut
        {            
            float x1 = Math.Max(rect1.mX, rect2.mX);
            float x2 = Math.Min(rect1.mX + rect1.mWidth, rect2.mX + rect2.mWidth);
            float y1 = Math.Max(rect1.mY, rect2.mY);
            float y2 = Math.Min(rect1.mY + rect1.mHeight, rect2.mY + rect2.mHeight);
            if (((x2 - x1) < 0) || ((y2 - y1) < 0))
            {
                mX = 0;
                mY = 0;
                mWidth = 0;
                mHeight = 0;
            }
            else
            {
                mX = x1;
                mY = y1;
                mWidth = x2 - x1;
                mHeight = y2 - y1;
            }
        }

        public void SetIntersectionOf(Rect rect1, float x, float y, float width, float height) mut
        {
            float x1 = Math.Max(rect1.mX, x);
            float x2 = Math.Min(rect1.mX + rect1.mWidth, x + width);
            float y1 = Math.Max(rect1.mY, y);
            float y2 = Math.Min(rect1.mY + rect1.mHeight, y + height);
            if (((x2 - x1) < 0) || ((y2 - y1) < 0))
            {
                mX = 0;
                mY = 0;
                mWidth = 0;
                mHeight = 0;
            }
            else
            {
                mX = x1;
                mY = y1;
                mWidth = x2 - x1;
                mHeight = y2 - y1;
            }
        }

        public Rect Intersection(Rect rect)
        {
            float x1 = Math.Max(mX, rect.mX);
            float x2 = Math.Min(mX + mWidth, rect.mX + rect.mWidth);
            float y1 = Math.Max(mY, rect.mY);
            float y2 = Math.Min(mY + mHeight, rect.mY + rect.mHeight);
            if (((x2 - x1) < 0) || ((y2 - y1) < 0))
                return Rect(0, 0, 0, 0);
            else
                return Rect(x1, y1, x2 - x1, y2 - y1);
        }

        public Rect Union(Rect rect)
        {
            float x1 = Math.Min(mX, rect.mX);
            float x2 = Math.Max(mX + mWidth, rect.mX + rect.mWidth);
            float y1 = Math.Min(mY, rect.mY);
            float y2 = Math.Max(mY + mHeight, rect.mY + rect.mHeight);
            return Rect(x1, y1, x2 - x1, y2 - y1);
        }

        public bool Contains(float x, float y)
        {
            return ((x >= mX) && (x < mX + mWidth) &&
                    (y >= mY) && (y < mY + mHeight));
        }

        public bool Contains(Point pt)
        {
            return Contains(pt.x, pt.y);
        }

        public bool Contains(Rect rect)
        {
            return Contains(rect.mX, rect.mY) && Contains(rect.mX + rect.mWidth, rect.mY + rect.mHeight);
        }

        public void Offset(float x, float y) mut
        {
            mX += x;
            mY += y;
        }

        public void Inflate(float x, float y) mut
        {
            mX -= x;
            mWidth += x * 2;
            mY -= y;
            mHeight += y * 2;
        }

        public void Scale(float scaleX, float scaleY) mut
        {
            mX *= scaleX;
            mY *= scaleY;
            mWidth *= scaleX;
            mHeight *= scaleY;
        }

        public void ScaleFrom(float scaleX, float scaleY, float centerX, float centerY) mut
        {
            Offset(-centerX, -centerY);
            Scale(scaleX, scaleY);
            Offset(centerX, centerY);
        }
    }
}
