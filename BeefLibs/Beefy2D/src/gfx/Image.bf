using System;
using System.Collections.Generic;
using System.Text;
using Beefy.utils;
using System.Diagnostics;

#if STUDIO_CLIENT
using Beefy.ipc;
#endif

namespace Beefy.gfx
{
#if !STUDIO_CLIENT
    public class Image : IDrawable
    {
		enum LoadFlags
		{
			None = 0,
			Additive = 1,
			NoPremult = 2,
			AllowRead = 4
		}

        public Image mSrcTexture;
        public int32 mSrcX;
        public int32 mSrcY;
        public int32 mSrcWidth;
        public int32 mSrcHeight;
        public float mWidth;
        public float mHeight;
        public float mX;
        public float mY;
        public void* mNativeTextureSegment;
        public PixelSnapping mPixelSnapping = PixelSnapping.Auto;
        
        [StdCall, CLink]
        public static extern void Gfx_DrawTextureSegment(void* textureSegment, float a, float b, float c, float d, float tx, float ty, float z, uint32 color, int32 pixelSnapping);

		[StdCall, CLink]
		static extern void* Gfx_LoadTexture(char8* fileName, int32 flags);

        [StdCall, CLink]
        static extern void* Gfx_CreateDynTexture(int32 width, int32 height);

        [StdCall, CLink]
        static extern void* Gfx_CreateRenderTarget(int32 width, int32 height, int32 destAlpha);

        [StdCall, CLink]
        static extern void* Gfx_ModifyTextureSegment(void* destSegment, void* srcTextureSegment, int32 srcX, int32 srcY, int32 srcWidth, int32 srcHeight);

		[StdCall, CLink]
		static extern void* Gfx_CreateTextureSegment(void* textureSegment, int32 srcX, int32 srcY, int32 srcWidth, int32 srcHeight);

		[StdCall, CLink]
		public static extern void Gfx_SetDrawSize(void* textureSegment, int32 width, int32 height);

		[StdCall, CLink]
		static extern void Gfx_Texture_SetBits(void* textureSegment, int32 destX, int32 destY, int32 destWidth, int32 destHeight, int32 srcPitch, uint32* bits);

		[StdCall, CLink]
		static extern void Gfx_Texture_GetBits(void* textureSegment, int32 srcX, int32 srcY, int32 srcWidth, int32 srcHeight, int32 destPitch, uint32* bits);

        [StdCall, CLink]
        static extern void Gfx_Texture_Delete(void* textureSegment);

        [StdCall, CLink]
        static extern int32 Gfx_Texture_GetWidth(void* textureSegment);

        [StdCall, CLink]
        static extern int32 Gfx_Texture_GetHeight(void* textureSegment);

        public this()
        {            
        }

		public ~this()
		{
			Gfx_Texture_Delete(mNativeTextureSegment);
		}

        public void Draw(Matrix matrix, float z, uint32 color)
        {            
            Image.Gfx_DrawTextureSegment(mNativeTextureSegment, matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty, z,
                color, (int32)mPixelSnapping);            
        }

        public static Image CreateRenderTarget(int32 width, int32 height, bool destAlpha = false)
        {
            void* aNativeTextureSegment = Gfx_CreateRenderTarget(width, height, destAlpha ? 1 : 0);
            if (aNativeTextureSegment == null)
                return null;

            return CreateFromNativeTextureSegment(aNativeTextureSegment);
        }

        public static Image LoadFromFile(String fileName, LoadFlags flags = .None)
        {
			scope AutoBeefPerf("Image.LoadFromFile");

            void* aNativeTextureSegment = Gfx_LoadTexture(fileName, (int32)flags);
            if (aNativeTextureSegment == null)
                return null;

            return CreateFromNativeTextureSegment(aNativeTextureSegment);
        }

		public static Image CreateDynamic(int width, int height)
		{
			void* nativeTextureSegment = Gfx_CreateDynTexture((.)width, (.)height);
			if (nativeTextureSegment == null)
				return null;

			return CreateFromNativeTextureSegment(nativeTextureSegment);
		}

        public static Image CreateFromNativeTextureSegment(void* nativeTextureSegment)
        {
            Image texture = new Image();
            texture.mNativeTextureSegment = nativeTextureSegment;
            texture.mSrcWidth = Gfx_Texture_GetWidth(nativeTextureSegment);
            texture.mSrcHeight = Gfx_Texture_GetHeight(nativeTextureSegment);
            texture.mWidth = texture.mSrcWidth;
            texture.mHeight = texture.mSrcHeight;
            return texture;
        }

        public Image CreateImageSegment(int srcX, int srcY, int srcWidth, int srcHeight)
        {
            Image textureSegment = new Image();
            textureSegment.mSrcTexture = this;
            textureSegment.mSrcX = (int32)srcX;
            textureSegment.mSrcY = (int32)srcY;
            textureSegment.mSrcWidth = (int32)srcWidth;
            textureSegment.mSrcHeight = (int32)srcHeight;
            textureSegment.mWidth = Math.Abs(srcWidth);
            textureSegment.mHeight = Math.Abs(srcHeight);
            textureSegment.mNativeTextureSegment = Gfx_CreateTextureSegment(mNativeTextureSegment, (int32)srcX, (int32)srcY, (int32)srcWidth, (int32)srcHeight);
            return textureSegment;
        }

		public void CreateImageSegment(Image srcImage, int srcX, int srcY, int srcWidth, int srcHeight)
		{
			if (mNativeTextureSegment != null)
			{
				Gfx_Texture_Delete(mNativeTextureSegment);
			}
		    
		    mSrcTexture = srcImage;
		    mSrcX = (int32)srcX;
		    mSrcY = (int32)srcY;
		    mSrcWidth = (int32)srcWidth;
		    mSrcHeight = (int32)srcHeight;
		    mWidth = Math.Abs(srcWidth);
		    mHeight = Math.Abs(srcHeight);
		    mNativeTextureSegment = Gfx_CreateTextureSegment(srcImage.mNativeTextureSegment, (int32)srcX, (int32)srcY, (int32)srcWidth, (int32)srcHeight);
		}

		public void SetDrawSize(int width, int height)
		{
			mWidth = width;
			mHeight = height;
			Gfx_SetDrawSize(mNativeTextureSegment, (int32)width, (int32)height);
		}

		public void Scale(float scale)
		{
			mWidth = (int32)(mSrcWidth * scale);
			mHeight = (int32)(mSrcHeight * scale);
			Gfx_SetDrawSize(mNativeTextureSegment, (int32)mWidth, (int32)mHeight);
		}

		public void Modify(Image srcImage, int srcX, int srcY, int srcWidth, int srcHeight)
		{
			mSrcTexture = srcImage;
			mSrcX = (int32)srcX;
			mSrcY = (int32)srcY;
			mSrcWidth = (int32)srcWidth;
			mSrcHeight = (int32)srcHeight;
			mWidth = Math.Abs(srcWidth);
			mHeight = Math.Abs(srcHeight);
			Gfx_ModifyTextureSegment(mNativeTextureSegment, srcImage.mNativeTextureSegment, (int32)srcX, (int32)srcY, (int32)srcWidth, (int32)srcHeight);
		}

		public void SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits)
		{
			Gfx_Texture_SetBits(mNativeTextureSegment, (.)destX, (.)destY, (.)destWidth, (.)destHeight, (.)srcPitch, bits);
		}

		public void GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits)
		{
			Gfx_Texture_GetBits(mNativeTextureSegment, (.)srcX, (.)srcY, (.)srcWidth, (.)srcHeight, (.)destPitch, bits);
		}

        public void CreateImageCels(Image[,] celImages)
        {
			int32 rows = (int32)celImages.GetLength(0);
			int32 cols = (int32)celImages.GetLength(1);

            int32 celW = mSrcWidth / cols;
            int32 celH = mSrcHeight / rows;

            Debug.Assert(celW * cols == mSrcWidth);
            Debug.Assert(celH * rows == mSrcHeight);
            
            for (int32 row = 0; row < rows; row++)
            {
                for (int32 col = 0; col < cols; col++)
                {
                    celImages[row, col] = CreateImageSegment(col * celW, row * celH, celW, celH);
                }
            }
        }
    }
#else    
    public class Image : IDrawable
    {
        public Image mSrcTexture;
        public int mSrcX;
        public int mSrcY;
        public int mSrcWidth;
        public int mSrcHeight;
        public float mWidth;
        public float mHeight;
        public float mX;
        public float mY;        
        public IPCProxy<IStudioImage> mStudioImage;
        public Image[] mCelImages;

        PixelSnapping mPixelSnapping = PixelSnapping.Auto;        

        public void Draw(Matrix matrix, float z, uint color, int cel)
        {
            //Image.Gfx_DrawTextureSegment(mNativeTextureSegment, matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty,
                //color, mAdditive ? 1 : 0, (int)mPixelSnapping);
            mStudioImage.Proxy.DrawTextureSegment(matrix.a, matrix.b, matrix.c, matrix.d, matrix.tx, matrix.ty, z,
                color, (int)mPixelSnapping);
        }

        public static Image LoadFromFile(string fileName, bool additive = false)
        {
            //void* aNativeTextureSegment = Gfx_LoadTexture(fileName);

            IPCObjectId aNativeTextureSegmentId = BFApp.sApp.mStudioHost.Proxy.LoadImage(fileName, additive);
            if (aNativeTextureSegmentId.IsNull())
                return null;

            return CreateFromNativeTextureSegment(aNativeTextureSegmentId);
        }

        public static Image CreateFromNativeTextureSegment(IPCObjectId nativeTextureSegmentId)
        {
            Image texture = new Image();
            texture.mStudioImage = IPCProxy<IStudioImage>.Create(nativeTextureSegmentId);
            texture.mSrcWidth = texture.mStudioImage.Proxy.GetSrcWidth();
            texture.mSrcHeight = texture.mStudioImage.Proxy.GetSrcHeight();
            texture.mWidth = texture.mSrcWidth;
            texture.mHeight = texture.mSrcHeight;
            return texture;
        }

        internal Image()
        {
        }

        public Image CreateImageSegment(int srcX, int srcY, int srcWidth, int srcHeight)
        {
            Image aTextureSegment = new Image();
            aTextureSegment.mStudioImage = IPCProxy<IStudioImage>.Create(mStudioImage.Proxy.CreateImageSegment(srcX, srcY, srcWidth, srcHeight));
            aTextureSegment.mSrcTexture = this;
            aTextureSegment.mSrcX = srcX;
            aTextureSegment.mSrcY = srcY;
            aTextureSegment.mSrcWidth = srcWidth;
            aTextureSegment.mSrcHeight = srcHeight;
            aTextureSegment.mWidth = Math.Abs(srcWidth);
            aTextureSegment.mHeight = Math.Abs(srcHeight);   
            return aTextureSegment;
        }

        public void CreateImageCels(int cols, int rows)
        {
            int celW = mSrcWidth / cols;
            int celH = mSrcHeight / rows;

            Debug.Assert(celW * cols == mSrcWidth);
            Debug.Assert(celH * rows == mSrcHeight);

            mCelImages = new Image[cols * rows];
            for (int row = 0; row < rows; row++)
            {
                for (int col = 0; col < cols; col++)
                {
                    mCelImages[col + row * cols] = CreateImageSegment(col * celW, row * celH, celW, celH);
                }
            }
        }
    }
#endif
}
