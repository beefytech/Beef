using System;
using System.Collections;
using System.Text;
using Beefy.geom;

namespace Beefy.gfx
{
    public struct Matrix
    {
        public float a;
        public float b;
        public float c;
        public float d;
        public float tx;
        public float ty;

		public this()
		{
			a = 1;
			b = 0;
			c = 0;
			d = 1;
			tx = 0;
			ty = 0;
		}

        public this(float _a, float _b, float _c, float _d, float _tx, float _ty)
        {
            a = _a;
            b = _b;
            c = _c;
            d = _d;
            tx = _tx;
            ty = _ty;
        }
        
        public static Matrix IdentityMatrix = Matrix(1, 0, 0, 1, 0, 0);       

        public Matrix Duplicate()
        {
            return Matrix(a, b, c, d, tx, ty);
        }

        public void Identity() mut
        {
            tx = 0;
            ty = 0;
            a = 1;
            b = 0;
            c = 0;
            d = 1;
        }

        public void Translate(float x, float y) mut
        {
            tx += x;
            ty += y;
        }

        public void Scale(float scaleX, float scaleY) mut
        {
            a *= scaleX;
            b *= scaleY;
            c *= scaleX;
            d *= scaleY;
            tx *= scaleX;
            ty *= scaleY;
        }

        public void Rotate(float angle) mut
        {
            float _a = a;
            float _b = b;
            float _c = c;
            float _d = d;
            float _tx = tx;
            float _ty = ty;

            float sin = (float)Math.Sin(angle);
            float cos = (float)Math.Cos(angle);

            a = _a * cos - _b * sin;
            b = _a * sin + _b * cos;
            c = _c * cos - _d * sin;
            d = _c * sin + _d * cos;
            tx = _tx * cos - _ty * sin;
            ty = _tx * sin + _ty * cos;
        }

        public void Multiply(Matrix mat2) mut
        {
            float _a = a;
            float _b = b;
            float _c = c;
            float _d = d;
            float _tx = tx;
            float _ty = ty;

            a = _a * mat2.a + _b * mat2.c;
            b = _a * mat2.b + _b * mat2.d;
            c = _c * mat2.a + _d * mat2.c;
            d = _c * mat2.b + _d * mat2.d;

            tx = _tx * mat2.a + _ty * mat2.c + mat2.tx;
            ty = _tx * mat2.b + _ty * mat2.d + mat2.ty;
        }

        public void Set(Matrix mat2) mut
        {
            a = mat2.a;
            b = mat2.b;
            c = mat2.c;
            d = mat2.d;

            tx = mat2.tx;
            ty = mat2.ty;
        }

        public void SetMultiplied(float x, float y, ref Matrix mat2) mut
        {            
            a = mat2.a;
            b = mat2.b;
            c = mat2.c;
            d = mat2.d;

            tx = x * mat2.a + y * mat2.c + mat2.tx;
            ty = x * mat2.b + y * mat2.d + mat2.ty;
        }
         
        public void SetMultiplied(float x, float y, float width, float height, ref Matrix mat2) mut
        {
            a = mat2.a * width;
            b = mat2.b * width;
            c = mat2.c * height;
            d = mat2.d * height;

            tx = x * mat2.a + y * mat2.c + mat2.tx;
            ty = x * mat2.b + y * mat2.d + mat2.ty;
        }

        public void SetMultiplied(Matrix mat1, Matrix mat2) mut
        {
            float a1 = mat1.a;
            float b1 = mat1.b;
            float c1 = mat1.c;
            float d1 = mat1.d;
            float tx1 = mat1.tx;
            float ty1 = mat1.ty;

            float a2 = mat2.a;
            float b2 = mat2.b;
            float c2 = mat2.c;
            float d2 = mat2.d;
            float tx2 = mat2.tx;
            float ty2 = mat2.ty;

            a = a1 * a2 + b1 * c2;
            b = a1 * b2 + b1 * d2;
            c = c1 * a2 + d1 * c2;
            d = c1 * b2 + d1 * d2;

            tx = tx1 * a2 + ty1 * c2 + tx2;
            ty = tx1 * b2 + ty1 * d2 + ty2;
        }

        public Point Multiply(Point point)
        {
            return Point(tx + a * point.x + c * point.y, ty + b * point.x + d * point.y);
        }

        public void Invert() mut
        {
            float _a = a;
            float _b = b;
            float _c = c;
            float _d = d;
            float _tx = tx;
            float _ty = ty;

            float den = a * d - b * c;

            a = _d / den;
            b = -_b / den;
            c = -_c / den;
            d = _a / den;
            tx = (_c * _ty - _d * _tx) / den;
            ty = -(_a * _ty - _b * _tx) / den;
        }

        public static Matrix Lerp(Matrix mat1, Matrix mat2, float pct)
        {
            float omp = 1.0f - pct;

            Matrix matrix = Matrix();
            matrix.a = (mat1.a * omp) + (mat2.a * pct);
            matrix.b = (mat1.b * omp) + (mat2.b * pct);
            matrix.c = (mat1.c * omp) + (mat2.c * pct);
            matrix.d = (mat1.d * omp) + (mat2.d * pct);
            matrix.tx = (mat1.tx * omp) + (mat2.tx * pct);
            matrix.ty = (mat1.ty * omp) + (mat2.ty * pct);

            return matrix;
        }
    }
}
