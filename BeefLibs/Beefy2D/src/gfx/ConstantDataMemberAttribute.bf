using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
    public struct ConstantDataMemberAttribute : Attribute
    {
        public bool mVertexShaderWants;
        public bool mPixelShaderWants;

        public this(bool vertexShaderWants, bool pixelShaderWants)
        {
            mVertexShaderWants = vertexShaderWants;
            mPixelShaderWants = pixelShaderWants;
        }
    }
}
