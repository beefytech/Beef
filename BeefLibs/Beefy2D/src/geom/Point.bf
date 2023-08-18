using System;
using System.Collections;
using System.Text;

namespace Beefy.geom
{
    public struct Point<T>
		where T : operator T + T, operator T - T, operator T * T, operator T / T, operator -T, IIsNaN, operator implicit int8
		where int : operator T <=> T
    {
        public T x;
        public T y;

        public this(T x, T y)
        {
            this.x = x;
            this.y = y;
        }

		public static Self operator-(Self lhs, Self rhs) => .(lhs.x - rhs.x, lhs.y - rhs.y);
		public static Self operator+(Self lhs, Self rhs) => .(lhs.x + rhs.x, lhs.y + rhs.y);
		public static Self operator*(Self lhs, T rhs) => .(lhs.x * rhs, lhs.y * rhs);
		public static Self operator/(Self lhs, T rhs) => .(lhs.x / rhs, lhs.y / rhs);
    }

	typealias Point = Point<float>;
	typealias PointD = Point<double>;
}
