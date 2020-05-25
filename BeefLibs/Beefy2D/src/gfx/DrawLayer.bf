using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy;

#if STUDIO_CLIENT
using Beefy.ipc;
#endif

namespace Beefy.gfx
{
#if !STUDIO_CLIENT
    public class DrawLayer
    {
        [CallingConvention(.Stdcall), CLink]
        static extern void* DrawLayer_Create(void* window);

        [CallingConvention(.Stdcall), CLink]
        static extern void DrawLayer_Delete(void* drawLayer);

        [CallingConvention(.Stdcall), CLink]
        static extern void DrawLayer_Clear(void* drawLayer);

        [CallingConvention(.Stdcall), CLink]
        static extern void DrawLayer_Activate(void* drawLayer);

        [CallingConvention(.Stdcall), CLink]
        static extern void DrawLayer_DrawToRenderTarget(void* drawLayer, void* texture);

        public void* mNativeDrawLayer;

        public this(BFWindow window)
        {
            mNativeDrawLayer = DrawLayer_Create((window != null) ? (window.mNativeWindow) : null);
        }

        public ~this()
        {
            DrawLayer_Delete(mNativeDrawLayer);
        }

        public void Activate()
        {
            DrawLayer_Activate(mNativeDrawLayer);
        }

        public void Clear()
        {
            DrawLayer_Clear(mNativeDrawLayer);
        }

        public void DrawToRenderTarget(Image texture)
        {
            DrawLayer_DrawToRenderTarget(mNativeDrawLayer, texture.mNativeTextureSegment);
        }
    }
#else
    public class DrawLayer
    {
        IPCProxy<IStudioDrawLayer> mDrawLayer;

        public DrawLayer(BFWindow window)
        {
            IPCObjectId drawLayerObjId = BFApp.sApp.mStudioHost.Proxy.CreateDrawLayer((window != null) ? window.mRemoteWindow.ObjId : IPCObjectId.Null);
            mDrawLayer = IPCProxy<IStudioDrawLayer>.Create(drawLayerObjId);
        }

        public void Dispose()
        {            
        }

        public void Activate()
        {
            mDrawLayer.Proxy.Activate();
        }

        public void Clear()
        {            
        }

        public void DrawToRenderTarget(Image texture)
        {
        }
    }
#endif
}
