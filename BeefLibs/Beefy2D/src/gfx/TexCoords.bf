using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
    public struct TexCoords
    {
		[Reflect]
        public float mU;
		[Reflect]
        public float mV;

        public this(float u, float v)
        {
            mU = u;
            mV = v;
        }
    }
}
