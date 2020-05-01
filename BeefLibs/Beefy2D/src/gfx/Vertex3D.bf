using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
    //[StructLayout(LayoutKind.Sequential)]
    public struct Vertex3D
    {
        public float mX;
        public float mY;
        public float mZ;
        public float mU;
        public float mV;
        public uint32 mColor;

        public this(float x, float y, float z, float u, float v, uint32 color)
        {
            mX = x;
            mY = y;
            mZ = z;
            mU = u;
            mV = v;
            mColor = color;
        }
    }
}
