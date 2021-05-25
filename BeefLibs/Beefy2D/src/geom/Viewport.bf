// This file contains portions of code from the FNA project (github.com/FNA-XNA/FNA),
// released under the Microsoft Public License

using Beefy.geom;
using System.Diagnostics;

namespace Beefy.geom
{
	struct Viewport
	{
		private int mX;
		private int mY;
		private int mWidth;
		private int mHeight;
		private float mMinDepth;
		private float mMaxDepth;

		public this(int x, int y, int width, int height,float minDepth,float maxDepth)
		{
		    this.mX = x;
		    this.mY = y;
		    this.mWidth = width;
		    this.mHeight = height;
		    this.mMinDepth = minDepth;
		    this.mMaxDepth = maxDepth;
		}

		/// Unprojects a Vector3 from screen space into world space.
		/// @param source The Vector3 to unproject.
		/// @param projection The projection
		/// @param view The view
		/// @param world The world
		public Vector3 Unproject(Vector3 source, Matrix4 projection, Matrix4 view, Matrix4 world)
		{
			var source;

		    Matrix4 matrix = Matrix4.Invert(Matrix4.Multiply(Matrix4.Multiply(world, view), projection));
		    source.mX = (((source.mX - this.mX) / ((float) this.mWidth)) * 2f) - 1f;
		    source.mY = -((((source.mY - this.mY) / ((float) this.mHeight)) * 2f) - 1f);
		    source.mZ = (source.mZ - this.mMinDepth) / (this.mMaxDepth - this.mMinDepth);
		    Vector3 vector = Vector3.Transform(source, matrix);
		    float a = (((source.mX * matrix.m30) + (source.mY * matrix.m31)) + (source.mZ * matrix.m32)) + matrix.m33;
		    if (!WithinEpsilon(a, 1f))
		    {
		        vector.mX = vector.mX / a;
		        vector.mY = vector.mY / a;
		        vector.mZ = vector.mZ / a;
		    }
		    return vector;

		}

		private static bool WithinEpsilon(float a, float b)
		{
		    float num = a - b;
		    return ((-1.401298E-45f <= num) && (num <= float.Epsilon));
		}
	}
}
