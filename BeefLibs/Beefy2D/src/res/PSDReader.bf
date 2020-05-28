using System;
using System.Collections;
using System.Text;
using Beefy.gfx;

namespace Beefy.res
{
#if !STUDIO_CLIENT
    public class PSDLayer
    {
        [CallingConvention(.Stdcall), CLink]
        extern static void Res_PSDLayer_GetSize(void* layerInfo, out int32 x, out int32 y, out int32 width, out int32 height);
        [CallingConvention(.Stdcall), CLink]
        extern static int32 Res_PSDLayer_GetLayerId(void* layerInfo);
        [CallingConvention(.Stdcall), CLink]
        extern static char8* Res_PSDLayer_GetName(void* layerInfo);
     	[CallingConvention(.Stdcall), CLink]
        extern static int32 Res_PSDLayer_IsVisible(void* layerInfo);

        public void* mNativeLayerInfo;
        public int32 mIdx;

        public this(void* nativeLayerInfo, int32 idx)
        {
            mNativeLayerInfo = nativeLayerInfo;
            mIdx = idx;
        }

        public void GetName(String str)
        {
            str.Append(Res_PSDLayer_GetName(mNativeLayerInfo));
        }

        public int32 GetLayerId()
        {
            return Res_PSDLayer_GetLayerId(mNativeLayerInfo);
        }

        public void GetSize(out int32 x, out int32 y, out int32 width, out int32 height)
        {
            Res_PSDLayer_GetSize(mNativeLayerInfo, out x, out y, out width, out height);
        }

        public bool IsVisible()
        {
            return Res_PSDLayer_IsVisible(mNativeLayerInfo) != 0;
        }
    }

    public class PSDReader
    {
        [CallingConvention(.Stdcall), CLink]
        extern static void* Res_OpenPSD(String fileName);

        [CallingConvention(.Stdcall), CLink]
        extern static void Res_DeletePSDReader(void* pSDReader);

        [CallingConvention(.Stdcall), CLink]
        extern static int32 Res_PSD_GetLayerCount(void* pSDReader);

        [CallingConvention(.Stdcall), CLink]
        extern static void* Res_PSD_GetLayerTexture(void* pSDReader, int32 layerIdx, out int32 xOfs, out int32 yOfs);

        [CallingConvention(.Stdcall), CLink]
        extern static void* Res_PSD_GetMergedLayerTexture(void* pSDReader, void* layerIndices, int32 count, out int32 xOfs, out int32 yOfs);

        [CallingConvention(.Stdcall), CLink]
        extern static void* Res_PSD_GetLayerInfo(void* pSDReader, int32 layerIdx);
        
        void* mNativePSDReader;

        protected this()
        {            
        }

        public int32 GetLayerCount()
        {
            return Res_PSD_GetLayerCount(mNativePSDReader);
        }

        public PSDLayer GetLayer(int32 layerIdx)
        {
            return new PSDLayer(Res_PSD_GetLayerInfo(mNativePSDReader, layerIdx), layerIdx);
        }

        public static PSDReader OpenFile(String fileName)
        {
            void* nativePSDReader = Res_OpenPSD(fileName);
            if (nativePSDReader == null)
                return null;
            PSDReader aPSDReader = new PSDReader();
            aPSDReader.mNativePSDReader = nativePSDReader;
            return aPSDReader;
        }

        public Image LoadLayerImage(int32 layerIdx)
        {
            int32 aXOfs = 0;
            int32 aYOfs = 0;
            void* texture = Res_PSD_GetLayerTexture(mNativePSDReader, layerIdx, out aXOfs, out aYOfs);
            if (texture == null)
                return null;
            Image image = Image.CreateFromNativeTextureSegment(texture);
            image.mX = aXOfs;
            image.mY = aYOfs;
            return image;
        }

        public Image LoadMergedLayerImage(int32 [] layerIndices)
        {
            int32 aXOfs = 0;
            int32 aYOfs = 0;
            void* texture = Res_PSD_GetMergedLayerTexture(mNativePSDReader, layerIndices.CArray(), (int32)layerIndices.Count, out aXOfs, out aYOfs);

            if (texture == null)
                return null;
            Image image = Image.CreateFromNativeTextureSegment(texture);
            image.mX = aXOfs;
            image.mY = aYOfs;            
            return image;
        }

        public void Dipose()
        {
            if (mNativePSDReader != null)
            {
                Res_DeletePSDReader(mNativePSDReader);
                mNativePSDReader = null;
            }
        }
    }
#else
    public class PSDLayer
    {        
        public int mIdx;

        public PSDLayer(int nativeLayerInfo, int idx)
        {
            //mNativeLayerInfo = nativeLayerInfo;
            mIdx = idx;
        }

        public string GetName()
        {
            return null;
        }

        public int GetLayerId()
        {
            return 0;
        }

        public void GetSize(out int x, out int y, out int width, out int height)
        {
            x = 0;
            y = 0;
            width = 0;
            height = 0;
            return;
        }

        public bool IsVisible()
        {
            return false;
        }
    }

    public class PSDReader
    {        
        protected PSDReader()
        {            
        }

        public int GetLayerCount()
        {
            return 0;
        }

        public PSDLayer GetLayer(int layerIdx)
        {
            return null;
        }

        public static PSDReader OpenFile(string fileName)
        {
            return null;
        }

        public Image LoadLayerImage(int layerIdx)
        {            
            return null;
        }

        public Image LoadMergedLayerImage(int [] layerIndices)
        {
            return null;
        }

        public void Dipose()
        {
            //TODO: Dispose
        }
    }
#endif
}
