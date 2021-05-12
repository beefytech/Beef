using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.geom;

namespace Beefy.gfx
{
    public enum DepthFunc
    {
        Never,
        Less,
        LessEqual,
        Equal,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    }

#if !STUDIO_CLIENT
    public class RenderState
    {        
        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_CreateRenderState(void* srcNativeRenderState);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_Delete(void* renderState);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_SetClip(void* renderState, float x, float y, float width, float height);

		[CallingConvention(.Stdcall), CLink]
		static extern void RenderState_SetTexWrap(void* renderState, bool texWrap);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_DisableClip(void* renderState);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_SetShader(void* nativeRenderState, void* nativeShader);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_SetDepthFunc(void* nativeRenderState, int32 depthFunc);

        [CallingConvention(.Stdcall), CLink]
        static extern void RenderState_SetDepthWrite(void* nativeRenderState, int32 depthWrite);

        public void* mNativeRenderState;
        public bool mIsFromDefaultRenderState;

        public this()
        {

        }

		public ~this()
		{
			RenderState_Delete(mNativeRenderState);
		}

        public static RenderState Create(RenderState srcRenderState = null)
        {
            void* nativeRenderState = Gfx_CreateRenderState((srcRenderState != null) ? srcRenderState.mNativeRenderState : null);
            if (nativeRenderState == null)
                return null;

            RenderState renderState = new RenderState();
            renderState.mNativeRenderState = nativeRenderState;
            return renderState;
        }

        public Shader Shader
        {            
            set
            {
                RenderState_SetShader(mNativeRenderState, value.mNativeShader);
            }
        }

        public DepthFunc DepthFunc
        {
            set
            {
                RenderState_SetDepthFunc(mNativeRenderState, (int32)value);
            }
        }

        public bool DepthWrite
        {
            set
            {
                RenderState_SetDepthWrite(mNativeRenderState, value ? 1 : 0);
            }
        }

        public Rect? ClipRect
        {
            set
            {
                if (value.HasValue)
                {
                    Rect rect = value.Value;
                    RenderState_SetClip(mNativeRenderState, rect.mX, rect.mY, rect.mWidth, rect.mHeight);
                }
                else
                    RenderState_DisableClip(mNativeRenderState);
            }
        }

		public bool TexWrap
		{
			set
			{
				RenderState_SetTexWrap(mNativeRenderState, value);
			}
		}
    }
#else
    public class RenderState
    {
        public IPCProxy<IStudioRenderState> mStudioRenderState;
        public bool mIsFromDefaultRenderState;
 
        internal RenderState()
        {

        }

        public static RenderState Create(RenderState srcRenderState = null)
        {
            /*if (nativeRenderState == IntPtr.Zero)
                return null;*/

            RenderState renderState = new RenderState();
            var renderStateRef = new IPCStudioObjectRef<IStudioRenderState>();
            if (srcRenderState != null)
                renderStateRef = srcRenderState.mStudioRenderState;
            var objId = BFApp.sApp.mStudioHost.Proxy.CreateRenderState(renderStateRef);
            renderState.mStudioRenderState = IPCProxy<IStudioRenderState>.Create(objId);
            return renderState;
        }

        public Shader Shader
        {
            set
            {
                mStudioRenderState.Proxy.SetShader(value.mStudioShader);                
            }
        }

        public DepthFunc DepthFunc
        {
            set
            {
                mStudioRenderState.Proxy.SetDepthFunc((int)value); ;                
            }
        }

        public bool DepthWrite
        {
            set
            {
                mStudioRenderState.Proxy.SetDepthWrite(value);                
            }
        }

        public Rect? ClipRect
        {
            set
            {
                if (value.HasValue)
                {
                    Rect rect = value.Value;
                    mStudioRenderState.Proxy.SetClipRect(rect.mX, rect.mY, rect.mWidth, rect.mHeight);                    
                }
                else
                {
                    mStudioRenderState.Proxy.DisableClipRect();                    
                }
            }
        }
    }
#endif
}
