using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.gfx
{
    public interface IDrawable
    {
        void Draw(Matrix matrix, float z, uint32 color);
    }
}
