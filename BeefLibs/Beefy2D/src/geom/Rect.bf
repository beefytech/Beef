using System;
using System.Collections;
using System.Text;
using Beefy.gfx;

namespace Beefy.geom
{
    public struct Rect<T>
		where T : operator T + T, operator T - T, operator T * T, operator T / T, operator -T, operator T / int8, IIsNaN, operator implicit int8
		where int : operator T <=> T
    {
        public T mX;
        public T mY;
        public T mWidth;
        public T mHeight;

		public T Left
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

		public T Top
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

		public T Right
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

		public T Bottom
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

		public T Width
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

		public T Height
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

		public T CenterX => mX + mWidth / 2;
		public T CenterY => mY + mHeight / 2;

        public this(T x = default, T y = default, T width = default, T height = default)
        {
            mX = x;
            mY = y;
            mWidth = width;
            mHeight = height;
        }

        public void Set(T x = 0, T y = 0, T width = 0, T height = 0) mut
        {
            mX = x;
            mY = y;
            mWidth = width;
            mHeight = height;
        }

        public bool Intersects(Self rect)
        {
            return !((rect.mX + rect.mWidth <= mX) ||
                    (rect.mY + rect.mHeight <= mY) ||
                    (rect.mX >= mX + mWidth) ||
                    (rect.mY >= mY + mHeight));
        }

        public void SetIntersectionOf(Self rect1, Self rect2) mut
        {            
            T x1 = Math.Max(rect1.mX, rect2.mX);
            T x2 = Math.Min(rect1.mX + rect1.mWidth, rect2.mX + rect2.mWidth);
            T y1 = Math.Max(rect1.mY, rect2.mY);
            T y2 = Math.Min(rect1.mY + rect1.mHeight, rect2.mY + rect2.mHeight);
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

        public void SetIntersectionOf(Self rect1, T x, T y, T width, T height) mut
        {
            T x1 = Math.Max(rect1.mX, x);
            T x2 = Math.Min(rect1.mX + rect1.mWidth, x + width);
            T y1 = Math.Max(rect1.mY, y);
            T y2 = Math.Min(rect1.mY + rect1.mHeight, y + height);
            if (((x2 - x1) < default) || ((y2 - y1) < default))
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

        public Self Intersection(Self rect)
        {
            T x1 = Math.Max(mX, rect.mX);
            T x2 = Math.Min(mX + mWidth, rect.mX + rect.mWidth);
            T y1 = Math.Max(mY, rect.mY);
            T y2 = Math.Min(mY + mHeight, rect.mY + rect.mHeight);
            if (((x2 - x1) < default) || ((y2 - y1) < default))
                return default;
            else
                return Self(x1, y1, x2 - x1, y2 - y1);
        }

        public Self Union(Self rect)
        {
            T x1 = Math.Min(mX, rect.mX);
            T x2 = Math.Max(mX + mWidth, rect.mX + rect.mWidth);
            T y1 = Math.Min(mY, rect.mY);
            T y2 = Math.Max(mY + mHeight, rect.mY + rect.mHeight);
            return Self(x1, y1, x2 - x1, y2 - y1);
        }

		public void Include(Point<T> pt) mut
		{
		    T left = mX;
			T top = mY;
			T right = mX + mWidth;
			T bottom = mY + mHeight;
			mX = Math.Min(pt.x, left);
			mY = Math.Min(pt.y, top);
			mWidth = Math.Max(pt.x, right) - mX;
			mHeight = Math.Max(pt.y, bottom) - mY;
		}

        public bool Contains(T x, T y)
        {
            return ((x >= mX) && (x < mX + mWidth) &&
                    (y >= mY) && (y < mY + mHeight));
        }

        public bool Contains(Point<T> pt)
        {
            return Contains(pt.x, pt.y);
        }

        public bool Contains(Self rect)
        {
            return Contains(rect.mX, rect.mY) && Contains(rect.mX + rect.mWidth, rect.mY + rect.mHeight);
        }

        public void Offset(T x, T y) mut
        {
            mX += x;
            mY += y;
        }

        public void Inflate(T x, T y) mut
        {
            mX -= x;
            mWidth += x + x;
            mY -= y;
            mHeight += y + y;
        }

        public void Scale(T scaleX, T scaleY) mut
        {
            mX *= scaleX;
            mY *= scaleY;
            mWidth *= scaleX;
            mHeight *= scaleY;
        }

        public void ScaleFrom(T scaleX, T scaleY, T centerX, T centerY) mut
        {
            Offset(-centerX, -centerY);
            Scale(scaleX, scaleY);
            Offset(centerX, centerY);
        }
    }

	typealias Rect = Rect<float>;
	typealias RectD = Rect<double>;
}
