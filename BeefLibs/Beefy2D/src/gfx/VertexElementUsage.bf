using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
    public enum VertexElementUsage
    {
        Position2D,
        Position3D,
        Color,
        TextureCoordinate,
        Normal,
        Binormal,
        Tangent,
        BlendIndices,
        BlendWeight,
        Depth,
        Fog,
        PointSize,
        Sample,
        TessellateFactor
    }
}
