using System;
using System.Collections;
using System.Text;

namespace Beefy.geom
{
    public struct Point<T>
		where T : operator T + T, operator T - T, operator T * T, operator -T, IIsNaN, operator implicit int
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
    }

	typealias Point = Point<float>;
	typealias PointD = Point<double>;
}
