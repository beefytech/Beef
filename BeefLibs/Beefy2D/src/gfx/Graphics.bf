using System;
using System.Collections;
using System.Text;
using Beefy.utils;
using Beefy.geom;
using System.Diagnostics;

namespace Beefy.gfx
{    
    public abstract class GraphicsBase
    {
        struct StateStack<T>
        {
            public T[] mStack;
            public int32 mIdx;
            DisposeProxy mDisposeProxy;

            public this(int32 size, T defaultValue, DisposeProxyDelegate disposeProxy)
            {
                mStack = new T[size];
                mStack[0] = defaultValue;
                mIdx = 0;
                mDisposeProxy = new DisposeProxy();
                mDisposeProxy.mDisposeProxyDelegate = disposeProxy;
            }

			public void Dispose()
			{
				delete mStack;
				delete mDisposeProxy;
			}

            public DisposeProxy Push(T newValue) mut
            {
                mStack[++mIdx] = newValue;
                return mDisposeProxy;
            }

            public T Pop() mut
            {
                return mStack[--mIdx];
            }
        }

        public Shader mDefaultShader ~ delete _;
		public Shader mTextShader ~ delete _;
        public RenderState mDefaultRenderState ~ delete _;
        public Font mFont;
        public Image mWhiteDot ~ delete _;
        public float ZDepth { get; set; }
        
        protected DisposeProxy mMatrixDisposeProxy ~ delete _;
        const int32 MATIX_STACK_SIZE = 256;
        public Matrix[] mMatrixStack = new Matrix[MATIX_STACK_SIZE] ~ delete _;
        public int32 mMatrixStackIdx = 0;
        public Matrix mMatrix;

        protected DisposeProxy mDrawLayerDisposeProxy ~ delete _;
        const int32 DRAW_LAYER_SIZE = 256;
        public DrawLayer[] mDrawLayerStack = new DrawLayer[DRAW_LAYER_SIZE] ~ delete _;
        public int32 mDrawLayerStackIdx = 0;
        public DrawLayer mDrawLayer;

        protected DisposeProxy mColorDisposeProxy ~ delete _;
        const int32 COLOR_STACK_SIZE = 256;
        public Color[] mColorStack = new Color[COLOR_STACK_SIZE] ~ delete _;
        public int32 mColorStackIdx = 0;
        public Color mColor = Color.White;

        /*protected DisposeProxy mZDepthDisposeProxy;
        const int COLOR_STACK_SIZE = 256;
        public uint[] mColorStack = new uint[COLOR_STACK_SIZE];
        public int mColorStackIdx = 0;
        public uint mColor = Color.White;*/

        StateStack<float> mZDepthStack ~ _.Dispose();

        protected DisposeProxy mClipDisposeProxy ~ delete _;
        const int32 CLIP_STACK_SIZE = 256;
        public Rect?[] mClipStack = new Rect?[CLIP_STACK_SIZE] ~ delete _;
		
        public int32 mClipStackIdx = 0;
        public Rect? mClipRect = null;

        protected DisposeProxy mRenderStateDisposeProxy ~ delete _;
        const int32 RENDERSTATE_STACK_SIZE = 256;
        public RenderState[] mRenderStateStack = new RenderState[RENDERSTATE_STACK_SIZE] ~ delete _;
        public int32 mRenderStateStackIdx = 0;
        
        public List<RenderState> mRenderStatePool = new List<RenderState>() ~ DeleteContainerAndItems!(_);
        public int32 mClipPoolIdx = 0;
        public List<int32> mClipPoolStarts = new List<int32>() ~ delete _;

        public int32 mDrawNestingDepth;

        public this()
        {
            mZDepthStack = StateStack<float>(256, 0.0f, new => PopZDepth);

            mMatrixDisposeProxy = new DisposeProxy();
            mMatrixDisposeProxy.mDisposeProxyDelegate = new => PopMatrix;
            mDrawLayerDisposeProxy = new DisposeProxy();
            mDrawLayerDisposeProxy.mDisposeProxyDelegate = new => PopDrawLayer;
            mColorDisposeProxy = new DisposeProxy();
            mColorDisposeProxy.mDisposeProxyDelegate = new => PopColor;
            mClipDisposeProxy = new DisposeProxy();
            mClipDisposeProxy.mDisposeProxyDelegate = new => PopClip;
            mRenderStateDisposeProxy = new DisposeProxy();

            mWhiteDot = Image.LoadFromFile("!white");

            for (int32 i = 0; i < MATIX_STACK_SIZE; i++)
                mMatrixStack[i] = Matrix.IdentityMatrix;
            mMatrix = mMatrixStack[0];
            
            mColorStack[0] = Color.White;            
        }

        public ~this()
        {
            
        }

        public void StartDraw()
        {
            if (mDrawNestingDepth == 0)
            {
                if (mDefaultRenderState != null)                
                    mRenderStateStack[0] = mDefaultRenderState;
                
                Debug.Assert(mMatrixStackIdx == 0);
                Debug.Assert(mDrawLayerStackIdx == 0);
                Debug.Assert(mRenderStateStackIdx == 0);
                Debug.Assert(mColorStackIdx == 0);
                Debug.Assert(mClipStackIdx == 0);
            }
            mDrawNestingDepth++;
            mClipPoolStarts.Add(mClipPoolIdx);

            PushMatrixOverride(Matrix.IdentityMatrix);
            PushRenderState(mDefaultRenderState);
            PushClipDisable();
            PushColorOverride(Color.White);
            PushZDepth(0.0f);
        }

        public void EndDraw()
        {            
            mDrawNestingDepth--;
            
            PopMatrix();
            mRenderStateStack[0] = mDefaultRenderState; // So the 'pop' gets the correct render state
            PopRenderState();
            PopClip();
            PopColor();
            PopZDepth();

            if (mDrawNestingDepth == 0)
            {
                Debug.Assert(mZDepthStack.mIdx == 0);
                Debug.Assert(mMatrixStackIdx == 0);
                Debug.Assert(mDrawLayerStackIdx == 0);
                Debug.Assert(mRenderStateStackIdx == 0);
                Debug.Assert(mColorStackIdx == 0);
                Debug.Assert(mClipStackIdx == 0);
            }

            mClipPoolIdx = mClipPoolStarts[mClipPoolStarts.Count - 1];
            mClipPoolStarts.RemoveAt(mClipPoolStarts.Count - 1);
        }

        public DisposeProxy PushTranslate(float x, float y)
        {
            mMatrixStackIdx++;
            mMatrixStack[mMatrixStackIdx].SetMultiplied(x, y, ref mMatrix);
            mMatrix = mMatrixStack[mMatrixStackIdx];

            return mMatrixDisposeProxy;
        }

        public DisposeProxy PushScale(float scaleX, float scaleY)
        {
            Matrix m = Matrix.IdentityMatrix;
            m.Identity();
            m.Scale(scaleX, scaleY);
            return PushMatrix(m);
        }

        public DisposeProxy PushScale(float scaleX, float scaleY, float x, float y)
        {
            Matrix m = Matrix.IdentityMatrix;
            m.Identity();
            m.Translate(-x, -y);
            m.Scale(scaleX, scaleY);
            m.Translate(x, y);
            return PushMatrix(m);
        }

        public DisposeProxy PushRotate(float rot)
        {
            Matrix m = Matrix.IdentityMatrix;
            m.Identity();
            m.Rotate(rot);    
            return PushMatrix(m);
        }

		public DisposeProxy PushRotate(float rot, float x, float y)
		{
			Matrix m = Matrix.IdentityMatrix;
			m.Identity();
			m.Translate(-x, -y);
			m.Rotate(rot);
			m.Translate(x, y);
			return PushMatrix(m);
		}

        public DisposeProxy PushMatrix(Matrix matrix)
        {
            mMatrixStackIdx++;
            mMatrixStack[mMatrixStackIdx].SetMultiplied(matrix, mMatrix);
            mMatrix = mMatrixStack[mMatrixStackIdx];

            return mMatrixDisposeProxy;
        }

        public DisposeProxy PushMatrixOverride(Matrix matrix)
        {
            mMatrixStackIdx++;
            mMatrixStack[mMatrixStackIdx].Set(matrix);
            mMatrix = mMatrixStack[mMatrixStackIdx];

            return mMatrixDisposeProxy;
        }

        public void PopMatrix()
        {
            mMatrix = mMatrixStack[--mMatrixStackIdx];
        }

        public DisposeProxy PushDrawLayer(DrawLayer drawLayer)
        {
            mDrawLayerStackIdx++;
            mDrawLayerStack[mDrawLayerStackIdx] = drawLayer;
            mDrawLayer = drawLayer;
            mDrawLayer.Activate();

            return mDrawLayerDisposeProxy;
        }

        public void PopDrawLayer()
        {
            mDrawLayer = mDrawLayerStack[--mDrawLayerStackIdx];
            if (mDrawLayer != null)
                mDrawLayer.Activate();
        }

        public DisposeProxy PushZDepth(float zDepth)
        {
            ZDepth = zDepth;
            return mZDepthStack.Push(ZDepth);
        }

        public void PopZDepth()
        {
            ZDepth = mZDepthStack.Pop();
        }

        public DisposeProxy PushColor(Color color)
        {
            Color physColor = color;

            mColorStackIdx++;
            mColor = mColorStack[mColorStackIdx] = Color.Mult(mColor, physColor);

            return mColorDisposeProxy;
        }

        public DisposeProxy PushColorOverride(Color color)
        {
            Color physColor = color;

            mColorStackIdx++;
            mColor = mColorStack[mColorStackIdx] = physColor;

            return mColorDisposeProxy;
        }

        public void SetColor(Color color)
        {
            Color physColor = (color & 0xFF00FF00) | ((color & 0x00FF0000) >> 16) | ((color & 0x000000FF) << 16);

            if (mColorStackIdx > 0)
                mColor = mColorStack[mColorStackIdx] = Color.Mult(mColorStack[mColorStackIdx - 1], physColor);
            else
                mColor = mColorStack[mColorStackIdx] = physColor;
        }

        public void PopColor()
        {
            mColor = mColorStack[--mColorStackIdx];
        }

        public void SetFont(Font font)
        {
            mFont = font;
        }

        public abstract DisposeProxy PushRenderState(RenderState renderState);
        public abstract void PopRenderState();
        
        public DisposeProxy PushClip(float x, float y, float width, float height)
        {            
            Matrix m = Matrix();
            m.SetMultiplied(x, y, width, height, ref mMatrix);

            mClipStackIdx++;
            if (!mClipRect.HasValue)
                mClipStack[mClipStackIdx] = Rect(m.tx, m.ty, m.a, m.d);
            else
                mClipStack[mClipStackIdx] = mClipRect.Value.Intersection(Rect(m.tx, m.ty, m.a, m.d));

            //mClipStack[mClipStackIdx] = new Rect(m.tx, m.ty, m.a, m.d);

            mClipRect = mClipStack[mClipStackIdx];             

            /*RenderState clipRenderState = null;
            var curRenderState = mRenderStateStack[mRenderStateStackIdx];
            if (curRenderState.mIsFromDefaultRenderState)
            {
                if (mRenderStatePool.Count <= mClipPoolIdx)
                {
                    clipRenderState = RenderState.Create(mDefaultRenderState);
                    clipRenderState.mIsFromDefaultRenderState = true;
                    mRenderStatePool.Add(clipRenderState);
                }
                clipRenderState = mRenderStatePool[mClipPoolIdx++];
            }
            else
                clipRenderState = RenderState.Create(curRenderState);*/
			
            Rect rectThing = mClipRect.Value;
            mClipRect = rectThing;

			var clipRenderState = AllocRenderState(mDefaultShader, mClipRect);

            //clipRenderState.ClipRect = mClipRect;
            PushRenderState(clipRenderState);            

            return mClipDisposeProxy;
        }

		RenderState AllocRenderState(Shader shader, Rect? clipRect)
		{
			RenderState renderState = null;
			var curRenderState = mRenderStateStack[mRenderStateStackIdx];
			if (curRenderState.mIsFromDefaultRenderState)
			{
			    if (mRenderStatePool.Count <= mClipPoolIdx)
			    {                    
			        renderState = RenderState.Create(mDefaultRenderState);
			        renderState.mIsFromDefaultRenderState = true;
			        mRenderStatePool.Add(renderState);
			    }
			    renderState = mRenderStatePool[mClipPoolIdx++];
			}
			else
			    renderState = RenderState.Create(curRenderState);
			renderState.Shader = shader;
			renderState.ClipRect = clipRect;
			return renderState;
		}

        public DisposeProxy PushClipDisable()
        {
			mClipStackIdx++;
			mClipStack[mClipStackIdx] = null;
			mClipRect = null;
			var clipRenderState = AllocRenderState(mDefaultShader, null);
            //clipRenderState.ClipRect = null;
            PushRenderState(clipRenderState);

            return mClipDisposeProxy;
        }

		public void PushTextRenderState()
		{
			var textRenderState = AllocRenderState(mTextShader, mClipRect);
			//textRenderState.ClipRect = mClipRect;
			//textRenderState.Shader = mTextShader;
			PushRenderState(textRenderState);
		}

        public void PopClip()
        {            
            mClipRect = mClipStack[--mClipStackIdx];                        
            PopRenderState();
        }        
    }

#if !STUDIO_CLIENT
    public class Graphics : GraphicsBase
    {               
        [CallingConvention(.Stdcall), CLink]	
        static extern void Gfx_SetRenderState(void* renderState);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_AllocTris(void* textureSegment, int32 vtxCount);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_SetDrawVertex(int32 idx, float x, float y, float z, float u, float v, uint32 color);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_CopyDrawVertex(int32 destIdx, int32 srcIdx);

        //[CallingConvention(.Stdcall), CLink]
        //static unsafe extern void Gfx_DrawIndexedVertices2D(void* vtxData, int vtxCount, int* idxData, int idxCount, float a, float b, float c, float d, float tx, float ty, float z);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_DrawIndexedVertices2D(int32 vertexSize, void* vtxData, int32 vtxCount, uint16* idxData, int32 idxCount, float a, float b, float c, float d, float tx, float ty, float z);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_SetShaderConstantData(int32 slotIdx, void* data, int32 size);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_SetShaderConstantDataTyped(int32 slotIdx, void* data, int32 size, int32* typeData, int32 typeCount);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_DrawQuads(void* textureSegment, Vertex3D* vertices, int32 vtxCount);

        [CallingConvention(.Stdcall), CLink]
        extern static void* Gfx_QueueRenderCmd(void* nativeRenderCmd);

        [CallingConvention(.Stdcall), CLink]
        extern static void Gfx_SetTexture_TextureSegment(int32 textureIdx, void* textureSegment);
        
        public this()
        {
            mRenderStateDisposeProxy.mDisposeProxyDelegate = new => PopRenderState;
        }

        public void SetTexture(int32 textureIdx, Image image)
        {
            Debug.Assert(image.mSrcTexture == null);
            Gfx_SetTexture_TextureSegment(textureIdx, image.mNativeTextureSegment);
        }

        /*public void StartDraw()
        {
            Debug.Assert(mRenderStateStackIdx == 0);
            Debug.Assert(mColorStackIdx == 0);
            Debug.Assert(mClipStackIdx == 0);

            if ((mDefaultRenderState != null) && (mRenderStateStack[0] == null))
            {
                mRenderStateStack[0] = mDefaultRenderState;
                Gfx_SetRenderState(mRenderStateStack[0].mNativeRenderState);
            }            
            mMatrix.Identity();
            mClipPoolIdx = 0;            
        }

        public void EndDraw()
        {
            Debug.Assert(mMatrixStackIdx == 0);
            Debug.Assert(mColorStackIdx == 0);
        }*/

        public void SetRenderState(RenderState renderState)
        {
            mRenderStateStack[mRenderStateStackIdx] = renderState;
            Gfx_SetRenderState(renderState.mNativeRenderState);
        }

        public override DisposeProxy PushRenderState(RenderState renderState)
        {
            Gfx_SetRenderState(renderState.mNativeRenderState);
            mRenderStateStack[++mRenderStateStackIdx] = renderState;
            return mRenderStateDisposeProxy;
        }

        public override void PopRenderState()
        {
            Gfx_SetRenderState(mRenderStateStack[--mRenderStateStackIdx].mNativeRenderState);            
        }

        public void Draw(RenderCmd renderCmd)
        {
            Gfx_QueueRenderCmd(renderCmd.mNativeRenderCmd);
        }
        
        public void Draw(IDrawable drawable, float x = 0, float y = 0)
        {
            Matrix newMatrix = Matrix();
            newMatrix.SetMultiplied(x, y, ref mMatrix);
            
            //newMatrix.tx *= g.mScale;
            //newMatrix.ty *= g.mScale;
            
            drawable.Draw(newMatrix, ZDepth, mColor);

            /*SexyExport.MatrixDrawImageInst(cSGraphics.mGraphics, mImageInstInfos[cel], newMatrix.a, newMatrix.b, newMatrix.c, newMatrix.d, newMatrix.tx, newMatrix.ty,
                (int)mPixelSnapping, mSmoothing ? 1 : 0, mAdditive ? 1 : 0, (int)g.mColor);*/
        }

        public void DrawButton(Image image, float x, float y, float width)
        {
            Matrix m = Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            Gfx_AllocTris(image.mNativeTextureSegment, 6 * 3);

			float useWidth = width;
            if (SnapMatrix(ref m, image.mPixelSnapping))
            {
                useWidth = (int32)useWidth;
            }

            float segmentW = Math.Min(image.mWidth / 2, useWidth / 2); 

            float a;
            float b;
            float c = m.c * image.mHeight;
            float d = m.d * image.mHeight;

            float u1 = 0;
            float u2 = segmentW / (float)image.mWidth;
            int32 vtx = 0;

            for (int32 col = 0; col < 3; col++)
            {
                if ((col == 0) || (col == 2))
                {
                    a = m.a * segmentW;
                    b = m.b * segmentW;
                    if (col == 2)
                    {
                        u1 = 1.0f - u1;
                        u2 = 1.0f;
                    }
                }
                else if (useWidth > image.mWidth)
                {
                    a = m.a * (useWidth - segmentW * 2);
                    b = m.b * (useWidth - segmentW * 2);
                    u1 = u2;
                }
                else
                {
                    a = 0;
                    b = 0;
                    u1 = u2;
                }

                Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, u1, 0, mColor);
                Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, u2, 0, mColor);
                Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, u1, 1, mColor);
                Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, u2, 1, mColor);

                m.tx += a;
                m.ty += b;                
                vtx += 6;
            }
        }

        public bool SnapMatrix(ref Matrix m, PixelSnapping pixelSnapping)
        {
            bool wantSnap = pixelSnapping == PixelSnapping.Always;
            if (pixelSnapping == PixelSnapping.Auto)
            {
                if ((m.b == 0) && (m.c == 0))
                    wantSnap = true;
            }
            if (wantSnap)
            {
                m.tx = (int32)m.tx;
                m.ty = (int32)m.ty;
            }
            return wantSnap;
        }

        public void DrawButtonVert(Image image, float x, float y, float height)
        {
            Matrix m = Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            Gfx_AllocTris(image.mNativeTextureSegment, 6 * 3);

			float useHeight = height;
            if (SnapMatrix(ref m, image.mPixelSnapping))
                useHeight = (int32)useHeight;

            float segmentH = Math.Min(image.mHeight / 2, useHeight / 2);

            float a = m.a * image.mWidth;
            float b = m.b * image.mWidth;
            float c = 0;
            float d = 0;

            float v1 = 0;
            float v2 = segmentH / (float)image.mHeight;
            int32 vtx = 0;

            /*for (int col = 0; col < 3; col++)
            {
                if ((col == 0) || (col == 2))
                {
                    a = m.a * segmentW;
                    b = m.b * segmentW;
                    if (col == 2)
                    {
                        u1 = 1.0f - u1;
                        u2 = 1.0f;
                    }
                }
                else
                {
                    a = m.a * (width - segmentW * 2);
                    b = m.b * (width - segmentW * 2);
                    u1 = u2;
                }

                Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, u1, 0, mColor);
                Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, u2, 0, mColor);
                Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, u1, 1, mColor);
                Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, u2, 1, mColor);

                m.tx += a;
                m.ty += b;
                vtx += 6;
            }*/

            for (int32 row = 0; row < 3; row++)
            {
                if ((row == 0) || (row == 2))
                {
                    c = m.c * segmentH;
                    d = m.d * segmentH;
                    if (row == 2)
                    {
                        v1 = 1.0f - v1;
                        v2 = 1.0f;
                    }
                }
                else if (useHeight > image.mHeight)
                {
                    c = m.c * (useHeight - image.mHeight);
                    d = m.d * (useHeight - image.mHeight);
                    v1 = v2;
                }
                else
                {
                    c = 0;
                    d = 0;
                    v1 = v2;
                }

                Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, 0, v1, mColor);
                Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, 1, v1, mColor);
                Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, 0, v2, mColor);
                Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, 1, v2, mColor);

                m.tx += c;
                m.ty += d;
                vtx += 6;
            }
        }

        public void DrawBox(Image image, float x, float y, float width, float height)
        {
            Matrix m = Matrix.IdentityMatrix;
            m.SetMultiplied(x, y, ref mMatrix);
            
			float useWidth = width;
			float useHeight = height;
            if (SnapMatrix(ref m, image.mPixelSnapping))
            {
                useWidth = (int32)useWidth;
                useHeight = (int32)useHeight;
            }

			float heightDiff = useHeight - image.mHeight;
			if (heightDiff >= 0)
				Gfx_AllocTris(image.mNativeTextureSegment, 6 * 3 * 3);
			else
				Gfx_AllocTris(image.mNativeTextureSegment, 6 * 3 * 2);

            float a;
            float b;
            float c;
            float d;
            
            int32 vtx = 0;

            //float prevX = m.tx;

            float segmentW = Math.Min(image.mWidth / 2, useWidth / 2);

            float v1 = 0;
            float v2 = segmentW / (float) image.mWidth;

            for (int32 row = 0; row < 3; row++)
            {
                if ((row == 0) || (row == 2))
                {
					float halfHeight = Math.Min(image.mHeight / 2, useHeight/2);					
                    c = m.c * halfHeight;
                    d = m.d * halfHeight;

                    if (row == 2)
                    {
                        v1 = 1.0f - v1;
                        v2 = 1.0f;
                    }
                }
                else
                {
					v1 = v2;
					if (heightDiff < 0)
						continue;
                    c = m.c * heightDiff;
                    d = m.d * heightDiff;                    
                }

                float u1 = 0;
                float u2 = 0.5f;

                float prevTX = m.tx;
                float prevTY = m.ty;

                for (int32 col = 0; col < 3; col++)
                {                    
                    if ((col == 0) || (col == 2))
                    {
                        a = m.a * image.mWidth / 2;
                        b = m.b * image.mWidth / 2;
                        if (col == 2)
                            u2 = 1.0f;
                    }
                    else
                    {
                        a = m.a * (useWidth - image.mWidth);
                        b = m.b * (useWidth - image.mWidth);
                        u1 = u2;
                    }

                    Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, u1, v1, mColor);
                    Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, u2, v1, mColor);
                    Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, u1, v2, mColor);
                    Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                    Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                    Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, u2, v2, mColor);

                    m.tx += a;
                    m.ty += b;
                    vtx += 6;                    
                }

                m.tx = prevTX + c;
                m.ty = prevTY + d;
            }
        }

        public void DrawIndexedVertices(VertexDefinition vertexDef, void* vertices, int vtxCount, uint16[] indices)
        {
            Gfx_DrawIndexedVertices2D(vertexDef.mVertexSize, vertices, (int32)vtxCount, indices.CArray(), (int32)indices.Count,
                mMatrix.a, mMatrix.b, mMatrix.c, mMatrix.d, mMatrix.tx, mMatrix.ty, ZDepth);
        }

        public void DrawIndexedVertices(VertexDefinition vertexDef, void* vertices, int vtxCount, uint16* indices, int idxCount)
        {
            Gfx_DrawIndexedVertices2D(vertexDef.mVertexSize, vertices, (int32)vtxCount, indices, (int32)idxCount,
                mMatrix.a, mMatrix.b, mMatrix.c, mMatrix.d, mMatrix.tx, mMatrix.ty, ZDepth);
        }

        public void SetShaderConstantData(int slotIdx, void* data, int size)
        {
            Gfx_SetShaderConstantData((int32)slotIdx, data, (int32)size);
        }

        public void SetShaderConstantData(int32 slotIdx, void* data, ConstantDataDefinition constantDataDefinition)
        {
            int32* dataTypesPtr = (int32*)constantDataDefinition.mDataTypes.CArray();
            Gfx_SetShaderConstantDataTyped(slotIdx, data, constantDataDefinition.mDataSize, dataTypesPtr, (int32)constantDataDefinition.mDataTypes.Count);
        }

        public void SetShaderConstantData(int32 slotIdx, Matrix4 matrix)
        {
			var mtx = matrix;
            Gfx_SetShaderConstantData(slotIdx, &mtx, (int32)sizeof(Matrix4));
        }        

        public float DrawString(StringView theString, float x, float y, FontAlign alignment = FontAlign.Left, float width = 0, FontOverflowMode overflowMode = FontOverflowMode.Overflow, FontMetrics* fontMetrics = null)
        {
            return mFont.Draw(this, theString, x, y, (int32)alignment, width, overflowMode, fontMetrics);
        }

        public void DrawQuad(Image image, float x, float y, float u1, float v1, float width, float height, float u2, float v2)
        {
            Matrix m = Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            float a = m.a * width;
            float b = m.b * width;
            float c = m.c * height;
            float d = m.d * height;

            Gfx_AllocTris(image.mNativeTextureSegment, 6);
            Gfx_SetDrawVertex(0, m.tx, m.ty, 0, u1, 0, mColor);
            Gfx_SetDrawVertex(1, m.tx + a, m.ty + b, 0, u2, 0, mColor);
            Gfx_SetDrawVertex(2, m.tx + c, m.ty + d, 0, u1, 1, mColor);
            Gfx_CopyDrawVertex(3, 2);
            Gfx_CopyDrawVertex(4, 1);
            Gfx_SetDrawVertex(5, m.tx + (a + c), m.ty + (b + d), 0, u2, 1, mColor);
        }

        // Untranslated
        public void DrawQuads(Image img, Vertex3D[] vertices, int32 vtxCount)
        {
            Vertex3D* vtxPtr = vertices.CArray();
            Gfx_DrawQuads(img.mNativeTextureSegment, vtxPtr, vtxCount);
        }        

        /*public TextMetrics GetTextMetrics(string theString, int justification, float width, StringEndMode stringEndMode)
        {
            
        }*/
        
        public void FillRect(float x, float y, float width, float height)
        {
            Matrix newMatrix = Matrix.IdentityMatrix;
            newMatrix.SetMultiplied(x, y, width, height, ref mMatrix);

            mWhiteDot.Draw(newMatrix, ZDepth, mColor);
        }

        public void OutlineRect(float x, float y, float width, float height, float thickness = 1)
        {
            FillRect(x, y, width, thickness); // Top
            FillRect(x, y + thickness, thickness, height - thickness * 2); // Left            
            FillRect(x + width - thickness, y + thickness, thickness, height - thickness * 2); // Right
            FillRect(x, y + height - thickness, width, thickness);
        }

        public void FillRectGradient(float x, float y, float width, float height,
            Color colorTopLeft, Color colorTopRight, Color colorBotLeft, Color colorBotRight)
        {
            Matrix m = Matrix.IdentityMatrix;
            m.SetMultiplied(x, y, width, height, ref mMatrix);

            //TODO: Multiply color

            Gfx_AllocTris(mWhiteDot.mNativeTextureSegment, 6);
            
            Gfx_SetDrawVertex(0, m.tx, m.ty, 0, 0, 0, Color.Mult(mColor, colorTopLeft));
            Gfx_SetDrawVertex(1, m.tx + m.a, m.ty + m.b, 0, 0, 0, Color.Mult(mColor, colorTopRight));
            Gfx_SetDrawVertex(2, m.tx + m.c, m.ty + m.d, 0, 0, 0, Color.Mult(mColor, colorBotLeft));

            Gfx_CopyDrawVertex(3, 2);
            Gfx_CopyDrawVertex(4, 1);
            Gfx_SetDrawVertex(5, m.tx + (m.a + m.c), m.ty + (m.b + m.d), 0, 0, 0, Color.Mult(mColor, colorBotRight));
        }
        
        public void PolyStart(Image image, int32 vertices)
        {
            Gfx_AllocTris(image.mNativeTextureSegment, vertices);            
        }

        public void PolyVertex(int32 idx, float x, float y, float u, float v, Color color = Color.White)
        {
            Matrix m = mMatrix;
            float aX = m.tx + m.a * x + m.c * y;
            float aY = m.ty + m.b * x + m.d * y;
            Gfx_SetDrawVertex(idx, aX, aY, 0, u, v, color);
        }

        public void PolyVertexCopy(int32 idx, int32 srcIdx)
        {
            Gfx_CopyDrawVertex(idx, srcIdx);
        }
    }
#else
    public class Graphics : GraphicsBase
    {        
        internal Graphics()
        {            
            mRenderStateDisposeProxy.mDisposeProxyDelegate = PopRenderState;
        }

        public void SetTexture(int textureIdx, Image image)
        {
            Debug.Assert(image.mSrcTexture == null);
            BFApp.StudioHostProxy.Gfx_SetTexture(textureIdx, image.mStudioImage);            
        }

        /*public void StartDraw()
        {            
            Debug.Assert(mRenderStateStackIdx == 0);
            Debug.Assert(mColorStackIdx == 0);
            Debug.Assert(mClipStackIdx == 0);

            if (mDefaultRenderState != null)
            {
                mRenderStateStack[0] = mDefaultRenderState;
                //Gfx_SetRenderState(mRenderStateStack[0].mNativeRenderState);
            }
            mMatrix.Identity();
            mClipPoolIdx = 0;
        }

        public void EndDraw()
        {
            Debug.Assert(mMatrixStackIdx == 0);
            Debug.Assert(mColorStackIdx == 0);
        }*/

        public override DisposeProxy PushRenderState(RenderState renderState)
        {
            BFApp.sApp.mStudioHost.Proxy.Gfx_SetRenderState(renderState.mStudioRenderState);
            mRenderStateStack[++mRenderStateStackIdx] = renderState;
            return mRenderStateDisposeProxy;            
        }

        public override void PopRenderState()
        {
            BFApp.sApp.mStudioHost.Proxy.Gfx_SetRenderState(mRenderStateStack[--mRenderStateStackIdx].mStudioRenderState);
        }

        public void Draw(IDrawable drawable, float x = 0, float y = 0, int cel = 0)
        {
#if STUDIO_CLIENT
            BFApp.sApp.TrackDraw();
#endif

            Matrix newMatrix = new Matrix();
            newMatrix.SetMultiplied(x, y, ref mMatrix);

            //newMatrix.tx *= g.mScale;
            //newMatrix.ty *= g.mScale;

            drawable.Draw(newMatrix, ZDepth, mColor, cel);

            /*SexyExport.MatrixDrawImageInst(cSGraphics.mGraphics, mImageInstInfos[cel], newMatrix.a, newMatrix.b, newMatrix.c, newMatrix.d, newMatrix.tx, newMatrix.ty,
                (int)mPixelSnapping, mSmoothing ? 1 : 0, mAdditive ? 1 : 0, (int)g.mColor);*/
        }

        public void DrawButton(Image image, float x, float y, float width)
        {
            Matrix m = new Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            //Gfx_AllocDrawVertices(image.mNativeTextureSegment, 6 * 3);

            //TODO: Snap?            

            float segmentW = Math.Min(image.mSrcWidth / 2, width / 2);

            float a;
            float b;
            float c = m.c * image.mSrcHeight;
            float d = m.d * image.mSrcHeight;

            float u1 = 0;
            float u2 = segmentW / (float)image.mSrcWidth;
            int vtx = 0;

            for (int col = 0; col < 3; col++)
            {
                if ((col == 0) || (col == 2))
                {
                    a = m.a * segmentW;
                    b = m.b * segmentW;
                    if (col == 2)
                    {
                        u1 = 1.0f - u1;
                        u2 = 1.0f;
                    }
                }
                else if (width > image.mSrcWidth)
                {
                    a = m.a * (width - segmentW * 2);
                    b = m.b * (width - segmentW * 2);
                    u1 = u2;
                }
                else
                {
                    a = 0;
                    b = 0;
                    u1 = u2;
                }

                /*Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, u1, 0, mColor);
                Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, u2, 0, mColor);
                Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, u1, 1, mColor);
                Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, u2, 1, mColor);*/

                m.tx += a;
                m.ty += b;
                vtx += 6;
            }
        }

        public void DrawButtonVert(Image image, float x, float y, float height)
        {
            Matrix m = new Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            //Gfx_AllocDrawVertices(image.mNativeTextureSegment, 6 * 3);

            //TODO: Snap?            

            float segmentH = Math.Min(image.mSrcHeight / 2, height / 2);

            float a = m.a * image.mSrcWidth;
            float b = m.b * image.mSrcWidth;
            float c = 0;
            float d = 0;

            float v1 = 0;
            float v2 = segmentH / (float)image.mSrcHeight;
            int vtx = 0;

            for (int row = 0; row < 3; row++)
            {
                if ((row == 0) || (row == 2))
                {
                    c = m.c * segmentH;
                    d = m.d * segmentH;
                    if (row == 2)
                    {
                        v1 = 1.0f - v1;
                        v2 = 1.0f;
                    }
                }
                else if (height > image.mSrcHeight)
                {
                    c = m.c * (height - image.mSrcHeight);
                    d = m.d * (height - image.mSrcHeight);
                    v1 = v2;
                }
                else
                {
                    c = 0;
                    d = 0;
                    v1 = v2;
                }

                /*Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, 0, v1, mColor);
                Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, 1, v1, mColor);
                Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, 0, v2, mColor);
                Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, 1, v2, mColor);*/

                m.tx += c;
                m.ty += d;
                vtx += 6;
            }
        }

        public void DrawBox(Image image, float x, float y, float width, float height)
        {
            Matrix m = new Matrix();
            m.SetMultiplied(x, y, ref mMatrix);

            //Gfx_AllocDrawVertices(image.mNativeTextureSegment, 6 * 3 * 3);

            //TODO: Snap?            

            float a;
            float b;
            float c;
            float d;


            int vtx = 0;

            float prevX = m.tx;

            float segmentW = Math.Min(image.mSrcWidth / 2, width / 2);

            float v1 = 0;
            float v2 = segmentW / (float)image.mSrcWidth;

            for (int row = 0; row < 3; row++)
            {
                if ((row == 0) || (row == 2))
                {
                    c = m.c * image.mSrcHeight / 2;
                    d = m.d * image.mSrcHeight / 2;
                    if (row == 2)
                    {
                        v1 = 1.0f - v1;
                        v2 = 1.0f;
                    }
                }
                else
                {
                    c = m.c * (height - image.mSrcHeight);
                    d = m.d * (height - image.mSrcHeight);
                    v1 = v2;
                }

                float u1 = 0;
                float u2 = 0.5f;

                float prevTX = m.tx;
                float prevTY = m.ty;

                for (int col = 0; col < 3; col++)
                {
                    if ((col == 0) || (col == 2))
                    {
                        a = m.a * image.mSrcWidth / 2;
                        b = m.b * image.mSrcWidth / 2;
                        if (col == 2)
                            u2 = 1.0f;
                    }
                    else
                    {
                        a = m.a * (width - image.mSrcWidth);
                        b = m.b * (width - image.mSrcWidth);
                        u1 = u2;
                    }

                    /*Gfx_SetDrawVertex(vtx + 0, m.tx, m.ty, 0, u1, v1, mColor);
                    Gfx_SetDrawVertex(vtx + 1, m.tx + a, m.ty + b, 0, u2, v1, mColor);
                    Gfx_SetDrawVertex(vtx + 2, m.tx + c, m.ty + d, 0, u1, v2, mColor);
                    Gfx_CopyDrawVertex(vtx + 3, vtx + 2);
                    Gfx_CopyDrawVertex(vtx + 4, vtx + 1);
                    Gfx_SetDrawVertex(vtx + 5, m.tx + (a + c), m.ty + (b + d), 0, u2, v2, mColor);*/

                    m.tx += a;
                    m.ty += b;
                    vtx += 6;
                }

                m.tx = prevTX + c;
                m.ty = prevTY + d;
            }
        }

        public unsafe void DrawIndexedVertices(VertexDefinition vertexDef, void* vertices, int vtxCount, ushort[] indices)
        {
            fixed (ushort* indicesPtr = indices)
                BFApp.sApp.mStudioHost.Proxy.Gfx_DrawIndexedVertices(vertexDef.mStudioVertexDefinition, vertexDef.mVertexSize * vtxCount, vertices, vtxCount,
                    indices.Length * sizeof(ushort), indicesPtr, indices.Length);
        }

        public unsafe void SetShaderConstantData(int slotIdx, Matrix4 matrix)
        {            
            BFApp.StudioHostProxy.Gfx_SetShaderConstantData(slotIdx, Marshal.SizeOf(matrix), &matrix);
        }

        public unsafe void Draw(RenderCmd renderCmd)
        {
            BFApp.StudioHostProxy.Gfx_DrawRenderCmd(renderCmd.mStudioRenderCmd);
        }

        public float DrawString(string theString, float x, float y, FontAlign alignment = FontAlign.Left, float width = 0, FontOverflowMode overflowMode = FontOverflowMode.Overflow, FontMetrics fontMetrics = null)
        {
            return mFont.Draw(this, theString, x, y, (int)alignment, width, overflowMode, fontMetrics);
        }

        /*public TextMetrics GetTextMetrics(string theString, int justification, float width, StringEndMode stringEndMode)
        {
            
        }*/

        public void FillRect(float x, float y, float width, float height)
        {
            Matrix newMatrix = new Matrix();
            newMatrix.SetMultiplied(x, y, width, height, ref mMatrix);

            mWhiteDot.Draw(newMatrix, ZDepth, mColor, 0);
        }

        public void FillRectGradient(float x, float y, float width, float height,
            uint colorTopLeft, uint colorTopRight, uint colorBotLeft, uint colorBotRight)
        {
            Matrix m = new Matrix();
            m.SetMultiplied(x, y, width, height, ref mMatrix);

            //TODO: Multiply color

            /*Gfx_AllocDrawVertices(mWhiteDot.mNativeTextureSegment, 6);

            Gfx_SetDrawVertex(0, m.tx, m.ty, 0, 0, 0, Color.Mult(mColor, colorTopLeft));
            Gfx_SetDrawVertex(1, m.tx + m.a, m.ty + m.b, 0, 0, 0, Color.Mult(mColor, colorTopRight));
            Gfx_SetDrawVertex(2, m.tx + m.c, m.ty + m.d, 0, 0, 0, Color.Mult(mColor, colorBotLeft));

            Gfx_CopyDrawVertex(3, 2);
            Gfx_CopyDrawVertex(4, 1);
            Gfx_SetDrawVertex(5, m.tx + (m.a + m.c), m.ty + (m.b + m.d), 0, 0, 0, Color.Mult(mColor, colorBotRight));*/
        }

        public void PolyStart(Image image, int vertices)
        {
            //TODO:Implement
        }

        public void PolyVertex(int idx, float x, float y, float u, float v, uint color = Color.White)
        {
            //TODO:Implement
        }

        public void PolyVertexCopy(int idx, int srcIdx)
        {
            //TODO:Implement
        }
    }
#endif
}
