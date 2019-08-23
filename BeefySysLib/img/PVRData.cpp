#include "PVRData.h"

USING_NS_BF;

struct PVRTextureHeader
{
    unsigned int dwHeaderSize;			
    unsigned int dwHeight;				
    unsigned int dwWidth;				
    unsigned int dwMipMapCount;			
    unsigned int dwpfFlags;				
    unsigned int dwTextureDataSize;		
    unsigned int dwBitCount;			
    unsigned int dwRBitMask;			
    unsigned int dwGBitMask;			
    unsigned int dwBBitMask;			
    unsigned int dwAlphaBitMask;		
    unsigned int dwPVR;					
    unsigned int dwNumSurfs;
};


typedef struct TextureMIPLevelData_TAG
{
    unsigned char* pData;
    unsigned int dwDataSize;
    unsigned int dwMIPLevel;
    unsigned int dwSizeX;
    unsigned int dwSizeY;
} TextureMIPLevelData;


typedef struct TextureImageData_TAG
{
    TextureMIPLevelData* pLevels;
    int textureFormat;
    int textureType;
    bool isCompressed;
    unsigned int dwMipLevels;
    unsigned int dwWidth;
    unsigned int dwHeight;
} TextureImageData;

static int DisableTwiddlingRoutine = 0;

static unsigned long TwiddleUV(unsigned long YSize, unsigned long XSize, unsigned long YPos, unsigned long XPos)
{
    unsigned long Twiddled;

    unsigned long MinDimension;
    unsigned long MaxValue;

    unsigned long SrcBitPos;
    unsigned long DstBitPos;

    int ShiftCount;

    if(YSize < XSize)
    {
        MinDimension = YSize;
        MaxValue	 = XPos;
    }
    else
    {
        MinDimension = XSize;
        MaxValue	 = YPos;
    }

    /*
    // Nasty hack to disable twiddling
    */
    if(DisableTwiddlingRoutine)
    {
        return (YPos* XSize + XPos);
    }

    /*
    // Step through all the bits in the "minimum" dimension
    */
    SrcBitPos = 1;
    DstBitPos = 1;
    Twiddled  = 0;
    ShiftCount = 0;

    while(SrcBitPos < MinDimension)
    {
        if(YPos & SrcBitPos)
        {
            Twiddled |= DstBitPos;
        }

        if(XPos & SrcBitPos)
        {
            Twiddled |= (DstBitPos << 1);
        }


        SrcBitPos <<= 1;
        DstBitPos <<= 2;
        ShiftCount += 1;

    }/*end while*/

    /*
    // prepend any unused bits
    */
    MaxValue >>= ShiftCount;

    Twiddled |=  (MaxValue << (2*ShiftCount));

    return Twiddled;
}

#define PVR_TEXTURE_FLAG_TYPE_MASK 0xFF
#define PVR_TEXTURE_FLAG_TYPE_PVRTC_2 24
#define PVR_TEXTURE_FLAG_TYPE_PVRTC_4 25

bool PVRData::ReadData()
{
	PVRTextureHeader* aHeader = (PVRTextureHeader*) (mSrcData);

	if (aHeader->dwHeaderSize != 52)
		return false;

	if ((aHeader->dwpfFlags & PVR_TEXTURE_FLAG_TYPE_MASK) == PVR_TEXTURE_FLAG_TYPE_PVRTC_2)
		mHWBitsType = HWBITS_PVRTC_2BPPV1;
	else if ((aHeader->dwpfFlags & PVR_TEXTURE_FLAG_TYPE_MASK) == PVR_TEXTURE_FLAG_TYPE_PVRTC_4)
	{		
		if (aHeader->dwNumSurfs > 1)
			mHWBitsType = HWBITS_PVRTC_2X4BPPV1;
		else
			mHWBitsType = HWBITS_PVRTC_4BPPV1;
	}
	
	mHWBits = (uint8*)mSrcData + aHeader->dwHeaderSize;
	mHWBitsLength = mSrcDataLen - aHeader->dwHeaderSize;
	mWidth = aHeader->dwWidth;
	mHeight = aHeader->dwHeight;
	mKeepSrcDataValid = true;

	/*mWidth = aHeader->dwWidth / 4;	
	mHeight = aHeader->dwHeight / 4;
	mBits = new uint32[mWidth*mHeight];

	int blockW = aHeader->dwWidth / 4;
	int blockH = aHeader->dwHeight / 4;

	int numBlocks = blockW * blockH;
		
	for (int blockNum = 0; blockNum < numBlocks; blockNum++)
	{
		int blockX = (blockNum % blockW);
		int blockY = blockH - (blockNum / blockW) - 1;

		uint8* srcData = mSrcData + aHeader->dwHeaderSize + TwiddleUV(blockH, blockW, blockY, blockX)*8 + 4;
		uint16 baseColorB = *((uint16*) (srcData));

		uint32 color = 0xFF000000 | 
			(((uint32) baseColorB & 0x7C00) >> 7) | 
			(((uint32) baseColorB & 0x03E0) << 6) |
			(((uint32) baseColorB & 0x001F) << 19);
		mBits[blockNum] = color;
	}*/	

	return true;
}