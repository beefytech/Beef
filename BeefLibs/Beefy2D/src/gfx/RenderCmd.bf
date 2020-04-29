using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

#if STUDIO_CLIENT
using Beefy.ipc;
#endif

namespace Beefy.gfx
{
#if !STUDIO_CLIENT
    public class RenderCmd
    {
        public void* mNativeRenderCmd;
    }
#else
    public class RenderCmd
    {
        public IPCProxy<IStudioRenderCmd> mStudioRenderCmd;
    }
#endif
}
