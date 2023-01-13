using System;
using System.Collections;
using System.Text;

namespace Beefy.geom
{
    public struct Vector2
    {
		private static Vector2 sZero = Vector2(0f, 0f);
		private static Vector2 sOne = Vector2(1f, 1f);
		private static Vector2 sUnitX = Vector2(1f, 0f);
		private static Vector2 sUnitY = Vector2(0f, 1f);

        public float mX;
        public float mY;

        public float Length
        {
            get
            {
                return (float)Math.Sqrt(mX * mX + mY * mY);
            }
        }

        public float LengthSquared
        {
            get
            {
                return mX * mX + mY * mY;
            }
        }

		public float Angle
		{
			get
			{
				return Math.Atan2(mY, mX);
			}
		}

        public this(float x, float y)
        {
            mX = x;
            mY = y;
        }

		public Vector2 Normalized => Vector2(mX, mY)..Normalize();

		public static Vector2 Normalize(Vector2 vector)
		{
			Vector2 newVec;
		    Normalize(vector, out newVec);
		    return newVec;
		}

		public static void Normalize(Vector2 value, out Vector2 result)
		{
		    float factor= Distance(value, sZero);
		    factor = 1f / factor;
		    result.mX = value.mX * factor;
		    result.mY = value.mY * factor;
		}

		public void Normalize() mut
		{
		    Normalize(this, out this);
		}

		public static float Cross(Vector2 vector1, Vector2 vector2)
		{
		    return vector1.mX * vector2.mY - vector1.mY * vector2.mX;
		}

        public static void DistanceSquared(Vector2 value1, Vector2 value2, out float result)
        {
            result = (value1.mX - value2.mX) * (value1.mX - value2.mX) +
                     (value1.mY - value2.mY) * (value1.mY - value2.mY);
        }

        public static float Distance(Vector2 vector1, Vector2 vector2)
        {
            float result;
            DistanceSquared(vector1, vector2, out result);
            return (float)Math.Sqrt(result);
        }

        public static Vector2 Add(Vector2 vec1, Vector2 vec2)
        {
            return Vector2(vec1.mX + vec2.mX, vec1.mY + vec2.mY);
        }

        public static Vector2 Subtract(Vector2 vec1, Vector2 vec2)
        {
            return Vector2(vec1.mX - vec2.mX, vec1.mY - vec2.mY);
        }

        public static float Dot(Vector2 vec1, Vector2 vec2)
        {
            return vec1.mX * vec2.mX + vec1.mY * vec2.mY;            
        }

        public static Vector2 FromAngle(float angle, float length = 1.0f)
        {
            return Vector2((float)Math.Cos(angle) * length, (float)Math.Sin(angle) * length);
        }

        public static Vector2 operator +(Vector2 vec1, Vector2 vec2)
        {
            return Vector2(vec1.mX + vec2.mX, vec1.mY + vec2.mY);
        }

        public static Vector2 operator -(Vector2 vec1, Vector2 vec2)
        {
            return  Vector2(vec1.mX - vec2.mX, vec1.mY - vec2.mY);
        }

        public static Vector2 operator *(Vector2 vec1, float factor)
        {
            return Vector2(vec1.mX * factor, vec1.mY * factor);
        }

		public static Vector2 operator /(Vector2 vec1, float factor)
		{
		    return Vector2(vec1.mX / factor, vec1.mY / factor);
		}
    }
}
