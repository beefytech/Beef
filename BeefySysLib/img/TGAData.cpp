#include "TGAData.h"

USING_NS_BF;

///

bool TGAData::ReadData()
{
	size_t step = sizeof(unsigned char) * 2;
    //CC_BREAK_IF((step + sizeof(unsigned char)) > bufSize);
	
	#pragma pack(push, 1)
	struct Header
	{
		char  mIdLength;
		char  mColourMapType;
		char  mDataTypeCode;
		short int mColourMapOrigin;
		short int mColourMapLength;
		char  mColourMapDepth;
		short int mXOrigin;
		short int mYOrigin;
		short mWidth;
		short mHeight;
		char  mBitsPerPixel;
		char  mImageDescriptor;
	};
	#pragma pack(pop)

	Header* hdr = (Header*)mSrcData;

    /*memcpy(&aType, mSrcData, sizeof(unsigned char));

    step += sizeof(unsigned char) * 2;
    step += sizeof(signed short) * 4;
    //CC_BREAK_IF((step + sizeof(signed short) * 2 + sizeof(unsigned char)) > bufSize);
    memcpy(&width, mSrcData + step, sizeof(signed short));
    memcpy(&height, mSrcData + step + sizeof(signed short), sizeof(signed short));
    memcpy(&pixelDepth, mSrcData + step + sizeof(signed short) * 2, sizeof(unsigned char));

    step += sizeof(unsigned char);
    step += sizeof(signed short) * 2;
    //CC_BREAK_IF((step + sizeof(unsigned char)) > bufSize);
    unsigned char cGarbage;
    memcpy(&cGarbage, mSrcData + step, sizeof(unsigned char));

	bool flipped = (cGarbage & 0x20) != 0;*/

	bool flipped = (hdr->mImageDescriptor & 0x20) != 0;

	mWidth = hdr->mWidth;
	mHeight = hdr->mWidth;
    mBits = new uint32[mWidth * mHeight];

	if (hdr->mDataTypeCode == 10) // RLE
	{
		int total;
		size_t step = (sizeof(unsigned char) + sizeof(signed short)) * 6;

		// mode equal the number of components for each pixel
		int aMode = hdr->mBitsPerPixel / 8;
		// total is the number of unsigned chars we'll have to read
		total = mHeight * mWidth * aMode;

		size_t dataSize = sizeof(unsigned char) * total;
		//CC_BREAK_IF((step + dataSize) > bufSize);

		uint8* srcPtr = mSrcData + step;
		uint32* destPtr = mBits;

		int destAdd = 0;
		if (!flipped)
		{
			destPtr = mBits + mWidth*(mHeight - 1);
			destAdd = -mWidth * 2;
		}

		if (aMode == 4)
		{
			int y = 0;
			int x = 0;
			
readSpanHeader:
			int spanLen = 0;
			uint32 spanColor = 0;

			uint8 spanHeader = *(srcPtr++);
			spanLen = (spanHeader & 0x7F) + 1;
			if ((spanHeader & 0x80) != 0)
			{
				// Repeat color								
				int b = *(srcPtr++);
				int g = *(srcPtr++);
				int r = *(srcPtr++);
				int a = *(srcPtr++);

				if (mWantsAlphaPremultiplied)
				{
					r = (r * a) / 255;
					g = (g * a) / 255;
					b = (b * a) / 255;
				}

				spanColor = (a << 24) | (b << 16) | (g << 8) | r;

				for (; y < mHeight; y++)
				{
					for (; x < mWidth; x++)
					{						
						if (spanLen == 0)
							goto readSpanHeader;
						*(destPtr++) = spanColor;						
						spanLen--;
					}

					x = 0;
					destPtr += destAdd;					
				}
			}
			else
			{
				for (; y < mHeight; y++)
				{
					for (; x < mWidth; x++)
					{
						if (spanLen == 0)
							goto readSpanHeader;

						int b = *(srcPtr++);
						int g = *(srcPtr++);
						int r = *(srcPtr++);
						int a = *(srcPtr++);

						if (mWantsAlphaPremultiplied)
						{
							r = (r * a) / 255;
							g = (g * a) / 255;
							b = (b * a) / 255;
						}

						*(destPtr++) = (a << 24) | (b << 16) | (g << 8) | r;
						
						spanLen--;
					}
										
					x = 0;
					destPtr += destAdd;					
				}
			}

			NOP;
		}
	}
	else
	{
		int total;        
        size_t step = (sizeof(unsigned char) + sizeof(signed short)) * 6;

        // mode equal the number of components for each pixel
        int aMode = hdr->mBitsPerPixel / 8;
        // total is the number of unsigned chars we'll have to read
        total = mHeight * mWidth * aMode;

        size_t dataSize = sizeof(unsigned char) * total;
        //CC_BREAK_IF((step + dataSize) > bufSize);

		uint8* srcPtr = mSrcData + step;
		uint32* destPtr = mBits;

		int destAdd = 0;
		if (!flipped)
		{
			destPtr = mBits + mWidth*(mHeight - 1);
			destAdd = -mWidth*2;
		}

		if (aMode == 4)
		{			
			for (int y = 0; y < mHeight; y++)
			{				
				for (int x = 0; x < mWidth; x++)
				{					
					int b = *(srcPtr++);				
					int g = *(srcPtr++);
					int r = *(srcPtr++);
					int a = *(srcPtr++);

					if (mWantsAlphaPremultiplied)
					{
						r = (r * a) / 255;
						g = (g * a) / 255;
						b = (b * a) / 255;
					}

					*(destPtr++) = (a << 24) | (b << 16) | (g << 8) | r;
				}

				destPtr += destAdd;				
			}
		}
		else if (aMode == 3)
		{			
			for (int y = 0; y < mHeight; y++)
			{				
				for (int x = 0; x < mWidth; x++)
				{					
					int b = *(srcPtr++);				
					int g = *(srcPtr++);
					int r = *(srcPtr++);
					int a = 255;

					*(destPtr++) = (a << 24) | (b << 16) | (g << 8) | r;
				}

				destPtr += destAdd;
			}			
		}
		else if (aMode == 1)
		{
			for (int y = 0; y < mHeight; y++)
			{				
				for (int x = 0; x < mWidth; x++)
				{
					int a = *(srcPtr++);
		
#ifdef BF_PLATFORM_WINDOWS
                    // Only windows has alpha correction for colored fonts (ATM)
					//int r = (int) (pow(a / 255.0f, 0.7f) * 255.0f);
					int r = (int)(pow(a / 255.0f, 1.2f) * 255.0f);
					//int r = a;
					int g = 255;
					int b = 255;
#else
                    int r = a;
                    int g = a;
                    int b = a;
#endif

					*(destPtr++) = (a << 24) | (b << 16) | (g << 8) | r;
				}

				destPtr += destAdd;
			}
		}

		

        //memcpy(psInfo->imageData, Buffer + step, dataSize);

        // mode=3 or 4 implies that the image is RGB(A). However TGA
        // stores it as BGR(A) so we'll have to swap R and B.
        /*if (mode >= 3)
        {
            for (i=0; i < total; i+= mode)
            {
                aux = psInfo->imageData[i];
                psInfo->imageData[i] = psInfo->imageData[i+2];
                psInfo->imageData[i+2] = aux;
            }
        }*/
	}	

	mAlphaPremultiplied = mWantsAlphaPremultiplied;
	return true;
}
