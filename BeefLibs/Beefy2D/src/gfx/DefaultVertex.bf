using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.geom;

namespace Beefy.gfx
{
    public struct DefaultVertex
    {
        [VertexMember(VertexElementUsage.Position2D)]
        public Vector3 mPos;
        [VertexMember(VertexElementUsage.TextureCoordinate)]
        public TexCoords mTexCoords;
        [VertexMember(VertexElementUsage.Color)]
        public uint32 mColor;
        
        public static VertexDefinition sVertexDefinition ~ delete _;

		public static void Init()
		{
			sVertexDefinition = new VertexDefinition(typeof(DefaultVertex));
		}

		public this()
		{
			this = default;
		}

		public this(float x, float y, float u, float v, Color color)
		{
			mPos.mX = x;
			mPos.mY = y;
			mPos.mZ = 0;

			mTexCoords.mU = u;
			mTexCoords.mV = v;

			mColor = (.)color;
		}
    }
}
