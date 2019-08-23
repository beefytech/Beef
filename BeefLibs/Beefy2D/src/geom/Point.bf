using System;
using System.Collections.Generic;
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
    }
}
