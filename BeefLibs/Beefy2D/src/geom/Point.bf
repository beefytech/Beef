using System;
using System.Collections;
using System.Text;

namespace Beefy.geom
{
    public struct Point
    {
        public float x;
        public float y;

        public this(float x, float y)
        {
            this.x = x;
            this.y = y;
        }

		public static Point operator-(Self lhs, Self rhs) => .(lhs.x - rhs.x, lhs.y - rhs.y);
		public static Point operator+(Self lhs, Self rhs) => .(lhs.x + rhs.x, lhs.y + rhs.y);
    }
}
