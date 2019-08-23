using System;
using System.Collections.Generic;
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
    }
}
