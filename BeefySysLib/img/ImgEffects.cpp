#include "ImgEffects.h"
#include "ImageData.h"
#include <math.h>
#include "util/CatmullRom.h"
#include "util/Vector.h"
#include <fstream>
#include "PSDReader.h"
#include "ImageUtils.h"
#include "ImageAdjustments.h"
#include "util/PerfTimer.h"

USING_NS_BF;

static inline float BFRound(float val)
{
	if (val < 0)
		return (float) (int) (val - 0.5f);
	else
		return (float) (int) (val + 0.5f);
}

static uint16* CreateContourDataTable(ImageCurve* contour, float range = 1.0f)
{
	uint16* contourData = new uint16[CONTOUR_DATA_SIZE];
	for (int i = 0; i < CONTOUR_DATA_SIZE; i++)
	{
		float aX = i / (float) (CONTOUR_DATA_SIZE - 1);

		float tableX = std::min(1.0f, aX / range);

		float yVal = contour->GetVal(tableX * 255.0f) / 255.0f;

		contourData[i] = BFClamp((int) (yVal * (GRADIENT_DATA_SIZE - 1) + 0.5f), 0, GRADIENT_DATA_SIZE - 1);
	}

	return contourData;
}

static uint32* CreateGradientDataTable(ImageGradient* colorGradient)
{
	uint32* gradientData = new uint32[GRADIENT_DATA_SIZE];
	for (int i = 0; i < GRADIENT_DATA_SIZE; i++)
	{
		float yVal = i / (float) (GRADIENT_DATA_SIZE - 1);

		uint32 color = 0;
		for (int colorIdx = 0; colorIdx < 4; colorIdx++)
		{
			float gradientPos = 1.0f - yVal;
			if ((colorIdx == 3) && (colorGradient[colorIdx].mPoints.size() > 2))
			{
				// Strange effect where alpha gradient never fully reaches end when there's more than one stop				
				gradientPos = std::min(gradientPos, 0.95f);
			}

			color |= (colorGradient[colorIdx].GetVal(gradientPos * colorGradient[colorIdx].mXSize)) << (colorIdx * 8);
		}
		
		gradientData[i] = color;
	}

	return gradientData;
}



ImageCurvePoint::ImageCurvePoint()
{
	mSpline = NULL;
	mIsSplineOwner = false;
}

ImageCurvePoint::~ImageCurvePoint()
{
	if (mIsSplineOwner)
		mSpline = NULL;
}

void ImageCurve::Init()
{
	CubicFuncSpline* curSpline = NULL;

	for (int i = 0; i < (int) mPoints.size(); i++)
	{
		ImageCurvePoint* pt = &mPoints[i];
		if (pt->mIsCorner)
		{
			if (curSpline != NULL)
			{
				curSpline->AddPt(pt->mX, pt->mY);
				curSpline = NULL;
			}
		}
		else
		{
			if (curSpline == NULL)
			{
				curSpline = new CubicFuncSpline();
				pt->mIsSplineOwner = true;
			}

			pt->mSpline = curSpline;
			curSpline->AddPt(pt->mX, pt->mY);
		}		
	}

	/*{
		std::fstream stream("c:\\temp\\curve.csv", std::ios::out);
		for (int i = 0; i < (int) 255; i++)
		{					
			float aVal = GetVal(i);
			stream << aVal << std::endl;
		}
		BF_ASSERT(stream.is_open());
		stream.flush();
	}

	_asm nop;*/
}

float ImageCurve::GetVal(float x)
{
	ImageCurvePoint* prevPt = NULL;
	for (int i = 0; i < (int) mPoints.size(); i++)
	{
		ImageCurvePoint* nextPt = &mPoints[i];

		if (x == nextPt->mX)
			return nextPt->mY;

		if ((x <= nextPt->mX) && (prevPt != NULL))
		{			
			if (prevPt->mSpline != NULL)
				return prevPt->mSpline->Evaluate(x);
				//return BFClamp(prevPt->mSpline->Evaluate(x), 0.0f, 255.0f);

			return prevPt->mY + 
				((x - prevPt->mX) / (nextPt->mX - prevPt->mX)) * (nextPt->mY - prevPt->mY);
		}

		prevPt = nextPt;
	}

	return mPoints.back().mY;
}

bool ImageCurve::IsDefault()
{
	return (mPoints.size() == 2) && (mPoints[0].mX == 0) && (mPoints[0].mY == 0) && (mPoints[1].mX == 255.0f) && (mPoints[1].mY == 255.0f);
}

static uint32 LerpColor(uint32 color1, uint32 color2, float pct)
{
    if (color1 == color2)
        return color1;

    uint32 a = (uint32)(pct * 256.0f);
    uint32 oma = 256 - a;
    uint32 color =
        (((((color1 & 0x000000FF) * oma) + ((color2 & 0x000000FF) * a)) >> 8) & 0x000000FF) |
        (((((color1 & 0x0000FF00) * oma) + ((color2 & 0x0000FF00) * a)) >> 8) & 0x0000FF00) |
        (((((color1 & 0x00FF0000) * oma) + ((color2 & 0x00FF0000) * a)) >> 8) & 0x00FF0000) |
        (((((color1 >> 24) & 0xFF) * oma) + (((color2 >> 24) & 0xFF) * a) & 0x0000FF00) << 16);

    return color;
}

int ImageGradient::GetVal(float x)
{	
	if (mSpline.mInputPoints.size() == 0)
	{	
		int n = (int)mPoints.size() - 1;
		
		mSpline.AddPt((float) mPoints[0].mValue);
		for (int i = 0; i < (int) mPoints.size(); i++)
			mSpline.AddPt((float) mPoints[i].mValue);		
		mSpline.AddPt((float) mPoints[n].mValue);

		/*{
			std::fstream stream("c:\\temp\\curve.csv", std::ios::out);
			for (int i = 0; i < (int) 256; i++)
			{					
				int aVal = GetVal((float) i * 4096 / 256);
				stream << aVal << std::endl;
			}
			BF_ASSERT(stream.is_open());
			stream.flush();
		}

		_asm nop;*/
	}

	for (int i = 0; i < (int) mPoints.size(); i++)
	{
		if (x < mPoints[i].mX)
		{
			if (i == 0)
				return mPoints[i].mValue;	

			float prevVal = (float) mPoints[i - 1].mValue;
			float nextVal = (float) mPoints[i].mValue;

			float aPct = (x - mPoints[i - 1].mX) / (mPoints[i].mX - mPoints[i - 1].mX);
			float linearVal = (prevVal * (1.0f - aPct)) + (nextVal * aPct);
			if ((mSmoothness == 0) || (mPoints.size() <= 2))
				return (int) (linearVal + 0.5f);

			float curveVal = mSpline.Evaluate(i  + aPct);
			float result = (curveVal * mSmoothness) + (linearVal * (1.0f - mSmoothness));
			if (prevVal < nextVal)
				result = BFClamp(result, prevVal, nextVal);
			else
				result = BFClamp(result, nextVal, prevVal);

			return (int) (result + 0.5f);
		}		
	}

	return mPoints.back().mValue;
}


ImageEffects::ImageEffects()
{	
	mSwapImages[0] = NULL;
	mSwapImages[1] = NULL;
}

ImageEffects::~ImageEffects()
{
	for (int i = 0; i < (int) mImageEffectVector.size(); i++)
		delete mImageEffectVector[i];	
}


ImageData* ImageEffects::GetDestImage(ImageData* usingImage)
{
	if ((mSwapImages[0] != usingImage) && (mSwapImages[0] != NULL))
		return mSwapImages[0];
	if ((mSwapImages[1] != usingImage) && (mSwapImages[1] != NULL))
		return mSwapImages[1];
	
	ImageData* anImage = new ImageData();
	anImage->mWidth = usingImage->mWidth;
	anImage->mHeight = usingImage->mHeight;
	anImage->mBits = new uint32[anImage->mWidth*anImage->mHeight];

	if (mSwapImages[0] == NULL)
		mSwapImages[0] = anImage;
	else
		mSwapImages[1] = anImage;

	return anImage;
}

#define BOXBLUR_IN(x) (((x < 0) || (x >= width)) ? edgeValue : in[inIndex + x])

static void BoxBlur(uint32* in, uint32* out, int width, int height, float radius, uint32 edgeValue) 
{
	AutoPerf gPerf("ImgEffects - BoxBlur");

	int widthMinus1 = width-1;
	int r = (int)radius;
	int tableSize = 2*r+1;
	float frac = radius - r;

	int a = (int) (frac * 256);
	int oma = 256 - a;
	
	int div = (2*r+1)*256 + a*2;

	int inIndex = 0;

	if (radius > 1)
	{
		for ( int y = 0; y < height; y++ ) 
		{
			int outIndex = y;
			uint32 ta = 0;

			for ( int i = -r; i <= r; i++ ) 
			{
				int rgb = BOXBLUR_IN(i);
				ta += rgb * 256;
			}
			ta += a * BOXBLUR_IN(-r-1);
			ta += a * BOXBLUR_IN(r+1);

			for ( int x = 0; x < width; x++ ) 
			{
				out[outIndex] = ta / div;

				uint32 r1Value = edgeValue;
				uint32 r2Value = edgeValue;
				int r1 = x+r+1;
				int r2 = r1+1;
				if (r2 < width)
				{
					r1Value = in[inIndex + r1];
					r2Value = in[inIndex + r2];
				}
				else if (r1 < width)
				{
					r1Value = in[inIndex + r1];
				}

				uint32 l1Value = edgeValue;
				uint32 l2Value = edgeValue;
				int l1 = x-r-1;
				int l2 = l1+1;
				
				if (l1 >= 0)
				{
					l1Value = in[inIndex + l1];
					l2Value = in[inIndex + l2];
				}
				else if (l2 >= 0)
				{
					l2Value = in[inIndex + l2];
				}

				int rgbL = (l1Value * a) + (l2Value * oma);
				int rgbR = (r1Value * oma) + (r2Value * a);

				ta += rgbR;
				ta -= rgbL;
				outIndex += height;
			}
			inIndex += width;
		}	
	}
	else
	{
		for ( int y = 0; y < height; y++ ) 
		{
			int outIndex = y;
			
			for ( int x = 0; x < width; x++ ) 
			{				
				int r = x+1;
				if (r > widthMinus1)
					r = widthMinus1;
				
				int l = x-1;
				if (l < 0)
					l = 0;
			
				int rgbL = in[inIndex+l] * a;
				int rgbR = in[inIndex+r] * a;
				int rgbM = in[inIndex+x] * 256;

				out[outIndex] = (rgbL + rgbM + rgbR) / div;
				outIndex += height;
			}
			inIndex += width;
		}	
	}
}

float GetSoftBlurRadius(float origRadius, float radiusLeft)
{
	//float radiusOffset = 1.90f - 0.7f * std::min(1.0f, origRadius / 10);
	//return radiusLeft - radiusOffset;

	if (radiusLeft == 0)
		return 0;

	float radiusOffset = 1.2f;
	float blurRadius = radiusLeft - radiusOffset;
	if (radiusLeft <= 2)
		blurRadius = 0.5f;
	return blurRadius;
}

float GetSoftBlurRadius(float origRadius)
{
	return GetSoftBlurRadius(origRadius, origRadius);
}

void SoftBlurInit(ImageData* inImage, ImageData* outImage, bool invert)
{
	uint32* in = inImage->mBits;
	uint32* out = outImage->mBits;

	int iw = inImage->mWidth;
	int ih = inImage->mHeight;
	int ow = outImage->mWidth;
	int oh = outImage->mHeight;

	int ox = inImage->mX - outImage->mX;
	int oy = inImage->mY - outImage->mY;
	
	if (!invert)
	{
		for (int y = 0; y < oh; y++)			
			for (int x = 0; x < ow; x++)
				out[x+y*ow] = 0;

		for (int y = 0; y < ih; y++)	
		{
			for (int x = 0; x < iw; x++)
			{				
				int anAlpha = in[x+y*iw]>>24;
				out[(x+ox)+(y+oy)*ow] = anAlpha * 256;
			}
		}
	}
	else
	{
		for (int y = 0; y < oh; y++)			
			for (int x = 0; x < ow; x++)
				out[x+y*ow] = 255*256;

		for (int y = 0; y < ih; y++)	
		{
			for (int x = 0; x < iw; x++)
			{				
				int anAlpha = in[x+y*iw]>>24;
				out[(x+ox)+(y+oy)*ow] =  (255 - (anAlpha)) * 256;
			}
		}
	}
}

void SoftBlur(uint32* data, int w, int h, float radius, uint32 defaultValue)
{
	uint32* tempBuffer = new uint32[w*h];

	int expandedX = 0;
	int expandedY = 0;

	int itrCount = (radius < 0.9f) ? 1 : 2;

	
	float d = radius / itrCount;
	if (d > 0)
	{		
		for (int i = 0; i < itrCount; i++)
		{
			BoxBlur(data, tempBuffer, w, h, d, defaultValue);
			BoxBlur(tempBuffer, data, h, w, d, defaultValue);
		}
	}	
	delete [] tempBuffer;
}

static int gAlphaCheckOfs [][2] =
	{{-1, -1}, {0, -1}, {1, -1},
	{-1, 0},			{1, 0},
	{-1, 1}, {0, 1}, {1, 1}};

static int gAlphaScaleVals [] =
	{358, 254, 358,
	254,      256,
	358, 254, 358};

static int gKernelOfsFwd[][2] = 
	{          {-1, -2},          {1, -2}, 
	 {-2, -1}, {-1, -1}, {0, -1}, {1, -1}, {2, -1}, 
	           {-1, 0}}; 

static int gKernelValsFwd[] =
	{567, 567,
	567, 358, 254, 358, 567,
	254};

static int gKernelOfsRev[][2] = 
	{                            
		                          {1, 0},
	  {-2, 1},  {-1, 1}, {0, 1},  {1, 1}, {2, 1}, 
	            {-1, 2},          {1, 2}}; 

static int gKernelValsRev[] =
	{    254,
	567, 358, 254, 358, 567,
	567, 567};

static void ChamferedDistanceTransformInit(ImageData* inImage, ImageData* outImage, bool invert, int softSize = 0)
{
	AutoPerf gPerf("ImgEffects - ChamferedDistanceTransformInit");

	uint32* in = inImage->mBits;
	uint32* out = outImage->mBits;

	int iw = inImage->mWidth;
	int ih = inImage->mHeight;
	int ow = outImage->mWidth;
	int oh = outImage->mHeight;

	int ox = inImage->mX - outImage->mX;
	int oy = inImage->mY - outImage->mY;

	int inf = (ow+oh)*564;
	int alphaCheckCount = sizeof(gAlphaScaleVals) / sizeof(int);

	if (!invert)
	{
		for (int y = 0; y < oh; y++)			
			for (int x = 0; x < ow; x++)
				out[x+y*ow] = inf;

		for (int y = 0; y < ih; y++)	
		{
			for (int x = 0; x < iw; x++)
			{
				int anAlpha = in[x+y*iw]>>24;
				if (anAlpha != 0)
				{					
					int aVal = (255 - anAlpha);
					out[(x+ox)+(y+oy)*ow] = aVal + aVal / 32;					
				}
				else
					out[(x+ox)+(y+oy)*ow] = inf;
			}	
		}
	}
	else
	{
		for (int y = 0; y < oh; y++)			
			for (int x = 0; x < ow; x++)
				out[x+y*ow] = 0;

		if (softSize != 0)
		{
			for (int y = 0; y < ih; y++)	
			{
				for (int x = 0; x < iw; x++)
				{
					int anAlpha = in[x+y*iw]>>24;
					int aVal;

					if (anAlpha > 128)					
						aVal = (anAlpha - 128) * (softSize + 1)*2;
					else 					
						aVal = 0;
					
					out[(x+ox)+(y+oy)*ow] = aVal;					
				}
			}
		}
		else
		{
			for (int y = 0; y < ih; y++)	
			{
				for (int x = 0; x < iw; x++)
				{
					int anAlpha = in[x+y*iw]>>24;
					if (anAlpha == 255)
					{					
						out[(x+ox)+(y+oy)*ow] = inf;
					}
					else
					{					
						out[(x+ox)+(y+oy)*ow] = anAlpha + anAlpha / 32;
					}
				}
			}
		}
	}
}

static void ChamferedDistanceTransformSlow(uint32* out, int width, int height, int startX, int startY, int endX, int endY)
	{
	AutoPerf gPerf("ImgEffects - ChamferedDistanceTransform");

	int kernelCountFwd = sizeof(gKernelValsFwd) / sizeof(int);
	int kernelCountRev = sizeof(gKernelValsRev) / sizeof(int);
			
	for (int y = startY; y < endY; y++)
	{
		for (int x = startX; x < endX; x++)
		{
			for (int kernIdx = 0; kernIdx < kernelCountFwd; kernIdx++)
			{
				int cx = BFClamp(x + gKernelOfsFwd[kernIdx][0], 0, width - 1);
				int cy = BFClamp(y + gKernelOfsFwd[kernIdx][1], 0, height - 1);
				int aVal = out[cx+cy*width] + gKernelValsFwd[kernIdx];
				if (aVal < (int) out[x+y*width])
					out[x+y*width] = aVal;
			}
		}
	}	

	for (int y = endY - 1; y >= startY; y--)
	{
		for (int x = endX - 1; x >= startX; x--)
		{
			for (int kernIdx = 0; kernIdx < kernelCountRev; kernIdx++)
			{
				int cx = BFClamp(x + gKernelOfsRev[kernIdx][0], 0, width - 1);
				int cy = BFClamp(y + gKernelOfsRev[kernIdx][1], 0, height - 1);
				int aVal = out[cx+cy*width] + gKernelValsRev[kernIdx];
				if (aVal < (int) out[x+y*width])
					out[x+y*width] = aVal;
			}
		}
	}
}

static void ChamferedDistanceTransform(uint32* out, int width, int height)
{
	AutoPerf gPerf("ImgEffects - ChamferedDistanceTransform");

	if ((width < 4) || (height < 4))
	{
		ChamferedDistanceTransformSlow(out, width, height, 0, 0, width, height);
		return;
	}

	// Do 2 pixel border (where we have to clamp)
	ChamferedDistanceTransformSlow(out, width, height, 0, 0, width, 2);
	ChamferedDistanceTransformSlow(out, width, height, 0, height-2, width, 2);
	ChamferedDistanceTransformSlow(out, width, height, 0, 2, 2, height - 2);
	ChamferedDistanceTransformSlow(out, width, height, width - 2, 2, 2, height - 2);

	// Do inner region (no clamping)
	int kernelCountFwd = sizeof(gKernelValsFwd) / sizeof(int);
	int kernelCountRev = sizeof(gKernelValsRev) / sizeof(int);
	
	int aStartX = 2;
	int aStartY = 2;
	int aEndX = width - 2;
	int aEndY = height - 2;

	for (int y = aStartY; y < aEndY; y++)
	{
		for (int x = aStartX; x < aEndX; x++)
		{
			for (int kernIdx = 0; kernIdx < kernelCountFwd; kernIdx++)
			{
				int cx = x + gKernelOfsFwd[kernIdx][0];
				int cy = y + gKernelOfsFwd[kernIdx][1];
				int aVal = out[cx+cy*width] + gKernelValsFwd[kernIdx];
				if (aVal < (int) out[x+y*width])
					out[x+y*width] = aVal;
			}
		}
	}	

	for (int y = aEndY - 1; y >= aStartY; y--)
	{
		for (int x = aEndX - 1; x >= aStartX; x--)
		{
			for (int kernIdx = 0; kernIdx < kernelCountRev; kernIdx++)
			{
				int cx = x + gKernelOfsRev[kernIdx][0];
				int cy = y + gKernelOfsRev[kernIdx][1];
				int aVal = out[cx+cy*width] + gKernelValsRev[kernIdx];
				if (aVal < (int) out[x+y*width])
					out[x+y*width] = aVal;
			}
		}
	}
}

#define IN_PIXEL(xval,yval) in[(xval) + (yval)*width]
#define IN_PIXEL_YX(yval,xval) in[(xval) + (yval)*width]

static void CreateNormalMap(uint32* in, uint32* out, int width, int height, float depth, Vector3* dotVector)
{
	AutoPerf gPerf("ImgEffects - CreateNormalMap");

	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			// surrounding pixels
            const uint32 topLeft = IN_PIXEL_YX(std::max(y - 1, 0), std::max(x - 1, 0));
            const uint32 top = IN_PIXEL_YX(std::max(y - 1, 0), x);
            const uint32 topRight = IN_PIXEL_YX(std::max(y - 1, 0), std::min(x + 1, width - 1));
            const uint32 right = IN_PIXEL_YX(y, std::min(x + 1, width - 1));
            const uint32 bottomRight = IN_PIXEL_YX(std::min(y + 1, height - 1), std::min(x + 1, width - 1));
            const uint32 bottom = IN_PIXEL_YX(std::min(y + 1, height - 1), x);
            const uint32 bottomLeft = IN_PIXEL_YX(std::min(y + 1, height - 1), std::max(x - 1, 0));
            const uint32 left = IN_PIXEL_YX(y, std::max(x - 1, 0));

            // their intensities
            const float tl = (float)(topLeft);
            const float t = (float)(top);
            const float tr = (float)(topRight);
            const float r = (float)(right);
            const float br = (float)(bottomRight);
            const float b = (float)(bottom);
            const float bl = (float)(bottomLeft);
            const float l = (float) (left);
			
            // sobel filter
            float dX = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            float dY = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);
            float dZ = depth * 10 * 0x100;
			
			float len = sqrt(dX*dX + dY*dY + dZ*dZ);
			dX /= len;
			dY /= len;
			dZ /= len;

			if (dotVector != NULL)
			{
				Vector3 aNorm(dX, dY, dZ);
				float dot = Vector3::Dot(*dotVector, aNorm);

				out[x+y*width] = (uint32) (0xFF00 * dot) + 0x100000;
			}
			else
			{
				out[x+y*width] = 
					((int) (dX * 0x7F + 0x80 + 0.5f)) |
					((int) (dY * 0x7F + 0x80 + 0.5f) << 8) |
					((int) (dZ * 0xFF) << 16) | 
					0xFF000000;
			}
		}
	}
}

static void AntiAliasIndices(uint32* in, int width, int height)
{
	int w = width;
	int h = height;

	for (int y = 0; y < h - 1; y++)
	{
		for (int x = 0; x < w - 1; x++)
		{
			int val1 = in[x+y*w];
			int val2 = in[(x+1)+y*w];
			int val3 = in[x+(y+1)*w];
			int val4 = in[(x+1)+(y+1)*w];
			in[x+y*w] = ((val1 * 114) + (val2 * 57) + (val3 * 57) + (val4 * 28)) / 256;			
		}
	}
}

static inline void BlendColors(PackedColor* destColor, PackedColor* underColor, PackedColor* overColor)
{	
	int a = 255 - overColor->a;			
	int oma = 255 - a;

	destColor->r = ((underColor->r * a) + (overColor->r * oma)) / 255;
	destColor->g = ((underColor->g * a) + (overColor->g * oma)) / 255;
	destColor->b = ((underColor->b * a) + (overColor->b * oma)) / 255;		
}

template <class CompareFunctor>
void ApplyBlendingRange(ImageData* origImage, ImageData* destImage, ImageData* checkImage, int rangeStart, int rangeEnd, CompareFunctor functor)
{
	if ((rangeStart == 0) && (rangeEnd == 0xFF))
		return;

	int aStartX = std::max(checkImage->mX, origImage->mX);
	int aStartY = std::max(checkImage->mY, origImage->mY);
	int aEndX = std::min(checkImage->mX + checkImage->mWidth, origImage->mX + origImage->mWidth);
	int aEndY = std::min(checkImage->mY + checkImage->mHeight, origImage->mY + origImage->mHeight);

	if (rangeStart <= rangeEnd)
	{
		for (int y = aStartY; y < aEndY; y++)
		{
			PackedColor* aDestColor = (PackedColor*) (destImage->mBits + (aStartX - destImage->mX) + ((y - destImage->mY) * destImage->mWidth));
			PackedColor* origColor = (PackedColor*) (origImage->mBits + (aStartX - origImage->mX) + ((y - origImage->mY) * origImage->mWidth));
			PackedColor* checkColor = (PackedColor*) (checkImage->mBits + (aStartX - checkImage->mX) + ((y - checkImage->mY) * checkImage->mWidth));

			for (int x = aStartX; x < aEndX; x++)
			{
				if ((functor(*checkColor) < rangeStart) || (functor(*checkColor) > rangeEnd))
					*aDestColor = *origColor;			
				aDestColor++;
				origColor++;
				checkColor++;
			}
		}
	}
	else // Inverted range
	{
		for (int y = aStartY; y < aEndY; y++)
		{
			PackedColor* aDestColor = (PackedColor*) (destImage->mBits + (aStartX - destImage->mX) + ((y - destImage->mY) * destImage->mWidth));
			PackedColor* origColor = (PackedColor*) (origImage->mBits + (aStartX - origImage->mX) + ((y - origImage->mY) * origImage->mWidth));
			PackedColor* checkColor = (PackedColor*) (checkImage->mBits + (aStartX - checkImage->mX) + ((y - checkImage->mY) * checkImage->mWidth));

			for (int x = aStartX; x < aEndX; x++)
			{
				if ((functor(*checkColor) < rangeStart) && (functor(*checkColor) > rangeEnd))
					*aDestColor = *origColor;			
				aDestColor++;
				origColor++;
				checkColor++;
			}
		}
	}
}

ImageData* ImageEffects::FlattenInto(ImageData* dest, PSDLayerInfo* srcLayer, ImageData* srcImage, ImageData* knockoutBottom, ImageData* insideImage)
{
	AutoPerf gPerf("ImageEffects::FlattenInto");

	bool hasComplexBlending = srcLayer->mBlendMode != 'norm';

	BF_ASSERT((dest == NULL) || (!dest->mAlphaPremultiplied));
	BF_ASSERT(!srcLayer->mAlphaPremultiplied);

	ImageData* aSrcImage = srcImage;

	if (srcLayer->mImageAdjustment != NULL)
	{	
		if (((srcLayer->mLayerMask != NULL) && (srcLayer->mLayerMaskEnabled)) || (srcLayer->mVectorMask != NULL))
		{			
			if (srcLayer->mWidth == 0)
			{
				aSrcImage = new ImageData();
				if (srcLayer->mLayerMaskDefault == 255)
					aSrcImage->CreateNew(0, 0, srcLayer->mPSDReader->mWidth, srcLayer->mPSDReader->mHeight, false);
				else
					aSrcImage->CreateNew(srcLayer->mLayerMaskX, srcLayer->mLayerMaskY, srcLayer->mLayerMaskWidth, srcLayer->mLayerMaskHeight, false);
				aSrcImage->Fill(0xFFFFFFFF);
				srcLayer->ApplyMask(aSrcImage);
			}
		}
		else
		{
			// No mask - just directly apply the effects
			ImageData* aDestImage = srcLayer->mImageAdjustment->CreateAdjustedImage(srcLayer, dest);

			ImageEffectCtx aCtx;
			aCtx.mOrigImage = NULL;
			aCtx.mBlendX = dest->mX;
			aCtx.mBlendY = dest->mY;
			aCtx.mBlendWidth = dest->mWidth;
			aCtx.mBlendHeight = dest->mHeight;
			aCtx.mInnerImage = NULL;
			aCtx.mOuterImage = NULL;
			aCtx.mLayerInfo = srcLayer;	
			aCtx.mLayerImage = NULL;
			
			return aDestImage;
		}
	}

	int borderSize = 0;
	bool hasInnerEffect = false;
	bool hasOuterEffect = false;
	bool needsOrigBits = false;
	for (int i = 0; i < (int) mImageEffectVector.size(); i++)
	{
		BaseImageEffect* anEffect = mImageEffectVector[i];
		borderSize = std::max(borderSize, anEffect->GetNeededBorderSize());
		int mixType = anEffect->GetMixType();
		hasInnerEffect |= (mixType == IMAGEMIX_INNER) || (mixType == IMAGEMIX_OVER);
		hasOuterEffect |= (mixType == IMAGEMIX_OUTER) || (mixType == IMAGEMIX_OVER);
		needsOrigBits |= anEffect->NeedsOrigBits(this);
		hasComplexBlending |= anEffect->mBlendMode != 'Nrml';
	}

	bool hasBlendingRanges = 
		(srcLayer->mBlendingRangeSourceStart != 0x00000000) ||
		(srcLayer->mBlendingRangeSourceEnd != 0xFFFFFFFF) ||
		(srcLayer->mBlendingRangeDestStart != 0x00000000) ||
		(srcLayer->mBlendingRangeDestEnd != 0xFFFFFFFF);
	bool doPostOpacity = hasComplexBlending || hasOuterEffect;

	uint8* aMask = NULL;

	//hasInnerEffect = true;

	if (insideImage != NULL)
		hasInnerEffect = true;

	hasInnerEffect = true;
	hasOuterEffect = true;

	needsOrigBits |= srcLayer->mLayerMaskHidesEffects;	

	if (!srcLayer->mTransparencyShapesLayer)	
		hasOuterEffect = false;	

	int minDestX = aSrcImage->mX - borderSize;
	int maxDestX = aSrcImage->mX + aSrcImage->mWidth + borderSize;
	int minDestY = aSrcImage->mY - borderSize;
	int maxDestY = aSrcImage->mY + aSrcImage->mHeight + borderSize;
	if (dest != NULL)	
	{
		minDestX = std::min(dest->mX, minDestX);
		maxDestX = std::max(dest->mX + dest->mWidth, maxDestX);
		minDestY = std::min(dest->mY, minDestY);
		maxDestY = std::max(dest->mY + dest->mHeight, maxDestY);
	}	
	
	ImageEffectCtx aCtx;
	aCtx.mOrigImage = NULL;
	aCtx.mBlendX = minDestX;
	aCtx.mBlendY = minDestY;
	aCtx.mBlendWidth = maxDestX - minDestX;
	aCtx.mBlendHeight = maxDestY - minDestY;
	aCtx.mInnerImage = NULL;
	aCtx.mOuterImage = NULL;
	aCtx.mLayerInfo = srcLayer;	
	aCtx.mLayerImage = aSrcImage;
	
	ImageData* aDestImage = NULL;
		
	int minCopyX = aSrcImage->mX - borderSize;
	int maxCopyX = aSrcImage->mX + aSrcImage->mWidth + borderSize;
	int minCopyY = aSrcImage->mY - borderSize;
	int maxCopyY = aSrcImage->mY + aSrcImage->mHeight + borderSize;

	// This gets set if we need the 'dest' initialized with bits
	bool needsDestInit = (!hasOuterEffect) || (!hasInnerEffect);

	for (int i = 0; i < 4; i++)
	{
		bool needed = ((i == 0) && (hasInnerEffect)) || ((i == 1) && (hasOuterEffect)) || 
			((i == 2) && (needsOrigBits)) || ((i == 3) && (needsDestInit));
		if (!needed)
			continue;
		
		ImageData* effectImage = new ImageData();
		effectImage->mX = aCtx.mBlendX;
		effectImage->mY = aCtx.mBlendY;
		effectImage->CreateNew(aCtx.mBlendWidth, aCtx.mBlendHeight);		

		if (dest != NULL)
		{						
			int minX = dest->mX;
			int maxX = dest->mX + dest->mWidth;
			int minY = dest->mY;
			int maxY = dest->mY + dest->mHeight;

			if ((i == 0) && (insideImage != NULL))
			{
				CopyImageBits(effectImage, insideImage);
			}
			else if ((i == 0) && (srcLayer->mKnockout == 1))
			{
				// Shallow knockout
				if (knockoutBottom != NULL)
				{
					for (int y = knockoutBottom->mY; y < knockoutBottom->mY + knockoutBottom->mHeight; y++)
					{
						for (int x = knockoutBottom->mX; x < knockoutBottom->mX + knockoutBottom->mWidth; x++)
						{
							effectImage->mBits[(x - effectImage->mX) + (y - effectImage->mY)*effectImage->mWidth] =
								knockoutBottom->mBits[(x - knockoutBottom->mX) + (y - knockoutBottom->mY)*knockoutBottom->mWidth];
						}
					}
				}
			}
			else if ((i == 0) && (srcLayer->mKnockout == 2))
			{
				// Deep knockout - leave blank
			}
			else
			{
				for (int y = minY; y < maxY; y++)
				{
					for (int x = minX; x < maxX; x++)
					{
						effectImage->mBits[(x - effectImage->mX) + (y - effectImage->mY)*effectImage->mWidth] =
							dest->mBits[(x - dest->mX) + (y - dest->mY)*dest->mWidth];
					}
				}
			}
		}

		if (i == 0)
			aCtx.mInnerImage = effectImage;
		else if (i == 1)
			aCtx.mOuterImage = effectImage;
		else if (i == 2)
			aCtx.mOrigImage = effectImage;
		else
			aDestImage = effectImage;		
	}

	if (aDestImage == NULL)
	{
		aDestImage = new ImageData();
		aDestImage->mX = aCtx.mBlendX;
		aDestImage->mY = aCtx.mBlendY;
		aDestImage->CreateNew(aCtx.mBlendWidth, aCtx.mBlendHeight);	
	}

	if (aCtx.mInnerImage != NULL)
	{
		if (insideImage != NULL)
		{			
			//BlendImage(aCtx.mInnerImage, aSrcImage, insideImage->mX - aCtx.mBlendX, insideImage->mY - aCtx.mBlendY, 1.0f, 'Nrml');
		}
		else if (srcLayer->mImageAdjustment != NULL)
		{			
			srcLayer->mImageAdjustment->ApplyImageAdjustment(srcLayer, aCtx.mInnerImage);
			CrossfadeImage(dest, aCtx.mInnerImage, srcLayer->mFillOpacity / 255.0f);
		}		
		else
		{
			int blendMode = srcLayer->mBlendMode;
				
			float anAlpha = srcLayer->mFillOpacity / 255.0f;
			if (srcLayer->mBlendInteriorEffectsAsGroup)
				anAlpha = 1.0f;
			if (!doPostOpacity)
				anAlpha *= (srcLayer->mOpacity / 255.0f);
			BlendImage(aCtx.mInnerImage, aSrcImage, aSrcImage->mX - aCtx.mBlendX, aSrcImage->mY - aCtx.mBlendY, anAlpha, blendMode, true);
		}
	}	
	else 
	{
		float anAlpha = srcLayer->mFillOpacity / 255.0f;
		if (srcLayer->mBlendInteriorEffectsAsGroup)
			anAlpha = 1.0f;
		if (!doPostOpacity)
			anAlpha *= (srcLayer->mOpacity / 255.0f);
		BlendImage(aDestImage, aSrcImage, aSrcImage->mX - aCtx.mBlendX, aSrcImage->mY - aCtx.mBlendY, anAlpha, srcLayer->mBlendMode, aCtx.mOuterImage != NULL);
	}
	
	if ((srcLayer->mLayerMaskHidesEffects) && (srcLayer->mLayerMaskEnabled))
	{
		aMask = new uint8[aCtx.mBlendWidth * aCtx.mBlendHeight];
		memset(aMask, srcLayer->mLayerMaskDefault, aCtx.mBlendWidth * aCtx.mBlendHeight);
		ImageData* anImage = aCtx.mOrigImage;
				
		int maskStartX = std::max(srcLayer->mLayerMaskX, anImage->mX);
		int maskStartY = std::max(srcLayer->mLayerMaskY, anImage->mY);
		int maskEndX = std::min(srcLayer->mLayerMaskX + srcLayer->mLayerMaskWidth, anImage->mX + anImage->mWidth);
		int maskEndY = std::min(srcLayer->mLayerMaskY + srcLayer->mLayerMaskHeight, anImage->mY + anImage->mHeight);
		
		for (int y = maskStartY; y < maskEndY; y++)
		{			
			uint8* maskSrc = srcLayer->mLayerMask + (y - srcLayer->mLayerMaskY)*srcLayer->mLayerMaskWidth;
			uint8* maskDest = aMask + (y - anImage->mY)*anImage->mWidth;
			for (int x = maskStartX; x < maskEndX; x++)			
				maskDest[x - anImage->mX] = maskSrc[x - srcLayer->mLayerMaskX];							
		}
	}

	for (int i = 0; i < (int) mImageEffectVector.size(); i++)
	{
		BaseImageEffect* anEffect = mImageEffectVector[i];

		if (!anEffect->mInitialized)
		{
			anEffect->Init();
			anEffect->mInitialized = true;
		}

		AutoPerf gPerf("ImageEffects::FlattenInto ImageEffect");
		anEffect->Apply(&aCtx);
	}

	// Did we create a temporary source image (for adjustment layers, primary)
	if (aSrcImage != srcImage)
	{
		//delete aSrcImage;
		//aSrcImage = srcImage;

		// Add back in the destination alpha that was factored out during adjustment image creation
		//MultiplyImageAlpha(aSrcImage, dest);
	}

	//aCtx.mOuterImage->mBits[0] = 0xFFFFFFFF;
	//aCtx.mOuterImage->mBits[aCtx.mBlendWidth*aCtx.mBlendHeight-1] = 0xFFFFFFFF;
	
	if (srcLayer->mBlendInteriorEffectsAsGroup)
	{
		for (int y = 0; y < aSrcImage->mHeight; y++)
		{
			PackedColor* srcColor = (PackedColor*) (aSrcImage->mBits + y*aSrcImage->mWidth);			

			for (int x = 0; x < aSrcImage->mWidth; x++)			
			{
				srcColor->a = (srcColor->a * srcLayer->mFillOpacity) / 255;			
				srcColor++;
			}
		}
	}

	if (aCtx.mOuterImage != NULL)
	{
		//TODO: Do this right...
		/*for (int x = 0; x < aSrcImage->mWidth; x++)
		{
			for (int y = 0; y < borderSize; y++)
				aDestImage->mBits[x+y*aCtx.mBlendWidth] = aCtx.mOuterImage->mBits[x+y*aCtx.mBlendWidth];
			for (int y = aCtx.mBlendHeight-borderSize; y < aCtx.mBlendHeight; y++)							
				aDestImage->mBits[x+y*aCtx.mBlendWidth] = aCtx.mOuterImage->mBits[x+y*aCtx.mBlendWidth];
		}

		for (int y = borderSize; y < aCtx.mBlendHeight-borderSize; y++)
		{
			for (int x = 0; x < borderSize; x++)
				aDestImage->mBits[x+y*aCtx.mBlendWidth] = aCtx.mOuterImage->mBits[x+y*aCtx.mBlendWidth];
			for (int x = aCtx.mBlendWidth-borderSize; x < aCtx.mBlendWidth; x++)
				aDestImage->mBits[x+y*aCtx.mBlendWidth] = aCtx.mOuterImage->mBits[x+y*aCtx.mBlendWidth];
		}*/

		for (int y = aCtx.mBlendY; y < aCtx.mBlendY + aCtx.mBlendHeight; y++)
		{
			for (int x = aCtx.mBlendX; x < aCtx.mBlendX + aCtx.mBlendWidth; x++)
			{
				aDestImage->mBits[(x-aCtx.mBlendX)+(y-aCtx.mBlendY)*aCtx.mBlendWidth] = 
					aCtx.mOuterImage->mBits[(x-aCtx.mBlendX)+(y-aCtx.mBlendY)*aCtx.mBlendWidth];
			}
		}


		/*int aSize = aCtx.mBlendWidth*aCtx.mBlendHeight;
		for (int i = 0; i < aSize; i++)
			aDestImage->mBits[i] = aCtx.mOuterImage->mBits[i];*/
			
			
		if (aCtx.mInnerImage == NULL)
		{
			for (int y = 0; y < aSrcImage->mHeight; y++)
			{
				PackedColor* srcColor = (PackedColor*) (aSrcImage->mBits + y*aSrcImage->mWidth);
				PackedColor* aDestColor = (PackedColor*) (aDestImage->mBits + borderSize + (y + borderSize) * aDestImage->mWidth);
				PackedColor* effectOutsideColor = (PackedColor*) (aCtx.mOuterImage->mBits + borderSize + ((y + borderSize) * aCtx.mBlendWidth));

				for (int x = 0; x < aSrcImage->mWidth; x++)
				{						
					int a = srcColor->a;			
					
					int oma = 255 - a;												

					int newDestAlpha = ((aDestColor->a * a) + (effectOutsideColor->a * oma)) / 255;
					if (newDestAlpha != 0)
					{
						int ca = (255 * aDestColor->a * a) / (aDestColor->a * a + effectOutsideColor->a * oma);
						int coma = 255 - ca;

						aDestColor->a = newDestAlpha;
						aDestColor->r = ((aDestColor->r * ca) + (effectOutsideColor->r * coma)) / 255;
						aDestColor->g = ((aDestColor->g * ca) + (effectOutsideColor->g * coma)) / 255;
						aDestColor->b = ((aDestColor->b * ca) + (effectOutsideColor->b * coma)) / 255;
					}

					srcColor++;
					aDestColor++;
					effectOutsideColor++;							
				}
			}
		}			
	}

	if (aCtx.mInnerImage != NULL)
	{
		if (aCtx.mOuterImage != NULL)
		{
			if (aMask != NULL)
			{
				for (int y = 0; y < aSrcImage->mHeight; y++)
				{	
					uint8* maskData = (aMask + borderSize + ((y + borderSize) * aCtx.mBlendWidth));
					PackedColor* srcColor = (PackedColor*) (aSrcImage->mBits + y*aSrcImage->mWidth);
					PackedColor* aDestColor = (PackedColor*) (aDestImage->mBits + borderSize + (y + borderSize) * aDestImage->mWidth);
					PackedColor* effectInsideColor = (PackedColor*) (aCtx.mInnerImage->mBits + borderSize + ((y + borderSize) * aCtx.mBlendWidth));
					PackedColor* effectOutsideColor = (PackedColor*) (aCtx.mOuterImage->mBits + borderSize + ((y + borderSize) * aCtx.mBlendWidth));

					PackedColor* origColor = (PackedColor*) (aCtx.mOrigImage->mBits + borderSize + ((y + borderSize) * aCtx.mBlendWidth));

					for (int x = 0; x < aSrcImage->mWidth; x++)
					{
						/*if ((aCtx.mBlendX + x == 60) && (aCtx.mBlendY + y == 60))
						{
							_asm nop;
						}*/
						
						uint32 insidePct = srcColor->a * (*maskData);
						uint32 outsidePct = (255 - srcColor->a) * (*maskData);
						uint32 origPct = 255 * (255 - *maskData);
							
						uint32 insideContrib = effectInsideColor->a * insidePct;
						uint32 outsideContrib = effectOutsideColor->a * outsidePct;
						uint32 origContrib = origColor->a * origPct;
						uint32 totalContrib = insideContrib + outsideContrib + origContrib;

						int newDestAlpha = (insideContrib + outsideContrib + origContrib) / 255 / 255;
						if (newDestAlpha != 0)
						{	
							int ma = *maskData;
							int moma = 255 - ma;
														
							aDestColor->r = ((insideContrib * effectInsideColor->r) + (outsideContrib * effectOutsideColor->r) + (origContrib * origColor->r)) / totalContrib;
							aDestColor->g = ((insideContrib * effectInsideColor->g) + (outsideContrib * effectOutsideColor->g) + (origContrib * origColor->g)) / totalContrib;
							aDestColor->b = ((insideContrib * effectInsideColor->b) + (outsideContrib * effectOutsideColor->b) + (origContrib * origColor->b)) / totalContrib;								
						}
						
						aDestColor->a = newDestAlpha;
					
						origColor++;
						maskData++;
						srcColor++;
						aDestColor++;
						effectInsideColor++;
						effectOutsideColor++;				
					}
				}
			}
			else
			{
				for (int y = 0; y < aSrcImage->mHeight; y++)
				{
					int ctxOffset = (aSrcImage->mX - aCtx.mBlendX) + (aSrcImage->mY - aCtx.mBlendY + y)*aCtx.mBlendWidth;
					PackedColor* srcColor = (PackedColor*) (aSrcImage->mBits + y*aSrcImage->mWidth);
					PackedColor* aDestColor = (PackedColor*) (aDestImage->mBits + ctxOffset);
					PackedColor* effectInsideColor = (PackedColor*) (aCtx.mInnerImage->mBits + ctxOffset);
					PackedColor* effectOutsideColor = (PackedColor*) (aCtx.mOuterImage->mBits + ctxOffset);
					
					for (int x = 0; x < aSrcImage->mWidth; x++)
					{
						if (srcColor->a == 0)
						{
							*aDestColor = *effectOutsideColor;
						}
						else
						{
							int a = srcColor->a;			
							int oma = 255 - a;
											
							int newDestAlpha = ((effectInsideColor->a * a) + (effectOutsideColor->a * oma)) / 255;					
							if (newDestAlpha != 0)
							{									
								int ca = (255 * effectInsideColor->a * a) / (effectInsideColor->a * a + effectOutsideColor->a * oma);
								int coma = 255 - ca;

								aDestColor->a = newDestAlpha;
								aDestColor->r = ((effectInsideColor->r * ca) + (effectOutsideColor->r * coma)) / 255;
								aDestColor->g = ((effectInsideColor->g * ca) + (effectOutsideColor->g * coma)) / 255;
								aDestColor->b = ((effectInsideColor->b * ca) + (effectOutsideColor->b * coma)) / 255;
							}
							else
							{
								*aDestColor = *effectOutsideColor;
							}
						}							
						
						srcColor++;
						aDestColor++;
						effectInsideColor++;
						effectOutsideColor++;				
					}
				}
			}
		}
		else
		{
			for (int y = 0; y < aSrcImage->mHeight; y++)
			{			
				PackedColor* srcColor = (PackedColor*) (aSrcImage->mBits + y*aSrcImage->mWidth);
				PackedColor* aDestColor = (PackedColor*) (aDestImage->mBits + borderSize + (y + borderSize) * aDestImage->mWidth);
				PackedColor* effectInsideColor = (PackedColor*) (aCtx.mInnerImage->mBits + borderSize + ((y + borderSize) * aCtx.mBlendWidth));					

				for (int x = 0; x < aSrcImage->mWidth; x++)
				{
					//if (srcColor->a != 0)
					{
						int a = 255 - srcColor->a;						

						if (!srcLayer->mTransparencyShapesLayer)
							a = 0;

						int oma = 255 - a;

						int newDestAlpha = ((aDestColor->a * a) + (effectInsideColor->a * oma)) / 255;
						if (newDestAlpha != 0)
						{
							int ca = (255 * aDestColor->a * a) / (aDestColor->a * a + effectInsideColor->a * oma);
							int coma = 255 - ca;

							aDestColor->a = newDestAlpha;
							aDestColor->r = ((aDestColor->r * ca) + (effectInsideColor->r * coma)) / 255;
							aDestColor->g = ((aDestColor->g * ca) + (effectInsideColor->g * coma)) / 255;
							aDestColor->b = ((aDestColor->b * ca) + (effectInsideColor->b * coma)) / 255;
						}
					}

					srcColor++;
					aDestColor++;
					effectInsideColor++;							
				}
			}
		}
	}

	/*if (srcLayer->mImageAdjustment != NULL)
	{
		// Just use the "inner image" as the dest since there's no mask to apply
		delete aDestImage;
		aDestImage = aCtx.mInnerImage;
		aCtx.mInnerImage = NULL;
	}*/

	if (hasBlendingRanges)
	{
		int aStartX = std::max(aSrcImage->mX, dest->mX);
		int aStartY = std::max(aSrcImage->mY, dest->mY);
		int aEndX = std::min(aSrcImage->mX + aSrcImage->mWidth, dest->mX + dest->mWidth);
		int aEndY = std::min(aSrcImage->mY + aSrcImage->mHeight, dest->mY + dest->mHeight);

		PackedColor* blendingRangeSourceStart = (PackedColor*) &(srcLayer->mBlendingRangeSourceStart);
		PackedColor* blendingRangeSourceEnd = (PackedColor*) &(srcLayer->mBlendingRangeSourceEnd);
		PackedColor* blendingRangeDestStart = (PackedColor*) &(srcLayer->mBlendingRangeDestStart);
		PackedColor* blendingRangeDestEnd = (PackedColor*) &(srcLayer->mBlendingRangeDestEnd);
		
		ApplyBlendingRange(dest, aDestImage, aSrcImage, blendingRangeSourceStart->r, blendingRangeSourceEnd->r, PackedColorGetR());
		ApplyBlendingRange(dest, aDestImage, dest, blendingRangeDestStart->r, blendingRangeDestEnd->r, PackedColorGetR());
		
		ApplyBlendingRange(dest, aDestImage, aSrcImage, blendingRangeSourceStart->g, blendingRangeSourceEnd->g, PackedColorGetG());
		ApplyBlendingRange(dest, aDestImage, dest, blendingRangeDestStart->g, blendingRangeDestEnd->g, PackedColorGetG());

		ApplyBlendingRange(dest, aDestImage, aSrcImage, blendingRangeSourceStart->b, blendingRangeSourceEnd->b, PackedColorGetB());
		ApplyBlendingRange(dest, aDestImage, dest, blendingRangeDestStart->b, blendingRangeDestEnd->b, PackedColorGetB());

		ApplyBlendingRange(dest, aDestImage, aSrcImage, blendingRangeSourceStart->a, blendingRangeSourceEnd->a, PackedColorGetGray());
		ApplyBlendingRange(dest, aDestImage, dest, blendingRangeDestStart->a, blendingRangeDestEnd->a, PackedColorGetGray());
	}

	if (srcLayer->mChannelMask != 0xFFFFFFFF)
	{		
		for (int i = 0; i < aDestImage->mWidth*aDestImage->mHeight; i++)
			aDestImage->mBits[i] &= srcLayer->mChannelMask;

		int aStartX = dest->mX;
		int aStartY = dest->mY;
		int aEndX = dest->mX + dest->mWidth;
		int aEndY = dest->mY + dest->mHeight;
		
		for (int y = aStartY; y < aEndY; y++)
		{
			uint32* aDestColor = aDestImage->mBits + (aStartX - aDestImage->mX) + ((y - aDestImage->mY) * aDestImage->mWidth);
			uint32* origColor = dest->mBits + (aStartX - dest->mX) + ((y - dest->mY) * dest->mWidth);
			
			for (int x = aStartX; x < aEndX; x++)
			{
				*aDestColor |= (*origColor & ~srcLayer->mChannelMask);				
				aDestColor++;
				origColor++;				
			}
		}
	}

	if ((srcLayer->mOpacity != 255) && (doPostOpacity))
		CrossfadeImage(dest, aDestImage, srcLayer->mOpacity / 255.0f);

	if (aSrcImage != srcImage)
	{
		// If we have a temporary source image (for an adjustment layer, for example)
		delete aSrcImage;
	}

	delete aMask;	
	return aDestImage;	
}

void ImageEffects::AddEffect(BaseImageEffect* effect)
{
	mImageEffectVector.push_back(effect);
}

//

BaseImageEffect::BaseImageEffect()
{	
	mContourData = NULL;
	mGradientData = NULL;
	mInitialized = false;
}

BaseImageEffect::~BaseImageEffect()
{
	delete mContourData;
	delete mGradientData;
}

void BaseImageEffect::Init()
{
}

void BaseImageEffect::Apply(ImageEffectCtx* ctx)
{
	ImageData* effectImage = new ImageData();

	effectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);	
	effectImage->mX = ctx->mBlendX;
	effectImage->mY = ctx->mBlendY;
	Apply(ctx->mLayerInfo, ctx->mLayerImage, effectImage);
	
	int mixType = GetMixType();
	if ((mixType == IMAGEMIX_INNER) || (mixType == IMAGEMIX_OVER))	
		BlendImage(ctx->mInnerImage, effectImage, 0, 0, (float) mOpacity / 100.0f, mBlendMode);	
	if ((mixType == IMAGEMIX_OUTER) || (mixType == IMAGEMIX_OVER))
		BlendImage(ctx->mOuterImage, effectImage, 0, 0, (float) mOpacity / 100.0f, mBlendMode);

	delete effectImage;
}


int BaseImageEffect::GetMixType() // Otherwise interior
{
	return IMAGEMIX_INNER;
}

int BaseImageEffect::GetNeededBorderSize()
{
	return 0;
}

bool BaseImageEffect::NeedsOrigBits(ImageEffects* effects)
{
	return false;
}


void ImageShadowEffect::Init()
{
	mContourData = CreateContourDataTable(&mContour, 1.0f);
}

void ImageShadowEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int w = destImageData->mWidth;
	int h = destImageData->mHeight;
	int aSize = w*h;

	float spread = (float) mSpread / 100.0f;

	float spreadSize = (float) (int) (mSize * spread + 0.5f);		
	int distTransSize = 0;

	ImageData* tempImage = new ImageData();
	tempImage->CreateNew(w, h);
	tempImage->mX = destImageData->mX;
	tempImage->mY = destImageData->mY;

	float aRadius = (float) mSize;
	float aRadiusLeft = (float) mSize;

	bool isInside = GetMixType() == IMAGEMIX_INNER;

	if (spreadSize != 0)
	{
		// Do distance transform first to "grow" area

		float distLenOffset = spreadSize / 20.0f;
		float distLen = spreadSize - 1.0f + distLenOffset;

		ChamferedDistanceTransformInit(imageData, tempImage, isInside);
		ChamferedDistanceTransform(tempImage->mBits, w, h);
		for (int i = 0; i < aSize; i++)
		{				
			float dist = tempImage->mBits[i] / 256.0f;
			dist -= spreadSize;
			if (dist < 0)
				tempImage->mBits[i] = 0xFF00;
			else if (dist < 1.0f)
				tempImage->mBits[i] = BFClamp((int) ((1.0f - dist) * 255.0f * 256.0f), 0, 0xFF00);				
			else
				tempImage->mBits[i] = 0;				
		}

		aRadiusLeft -= spreadSize;
	}
	else
	{
		SoftBlurInit(imageData, tempImage, isInside);
	}
	
	float radiusOffset = 1.2f;
	float blurRadius = aRadiusLeft - radiusOffset;
	if (aRadiusLeft <= 2)
		blurRadius = 0.5f;
	if (aRadiusLeft != 0)
		SoftBlur(tempImage->mBits, w, h, blurRadius, 0);
	
	bool doFrontTaper = (!isInside) &&
		(((mContour.GetVal(0) != 0) || (mNoise != 0)));

	if ((mAntiAliased) && (!mContour.IsDefault()) && (!doFrontTaper))
		AntiAliasIndices(tempImage->mBits, w, h);

	memset(destImageData->mBits, 0, w*h*sizeof(uint32));

	float noise = (float) mNoise / 100.0f;	

	float angle = mUseGlobalLight ? (float) layerInfo->mPSDReader->mGlobalAngle : (float) mLocalAngle;
	angle *= BF_PI / 180.0f;
	int ofsX = (int) (BFRound(-cos(angle) * (float) mDistance));
	int ofsY = (int) (BFRound(sin(angle) * (float) mDistance));

	int maxX = std::min(w, w + ofsX);
	int maxY = std::min(h, h + ofsY);
	for (int y = std::max(0, ofsY); y < maxY; y++)
	{
		for (int x = std::max(0, ofsX); x < maxX; x++)
		{
			int i = x+y*w;
			
			PackedColor* aDestColor = (PackedColor*) (destImageData->mBits + i);
		
			int blurVal = tempImage->mBits[(x-ofsX)+(y-ofsY)*w];

			int idx = (blurVal * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
		
			BF_ASSERT(idx >= 0);
			BF_ASSERT(idx < CONTOUR_DATA_SIZE);		

			int gradientIdx = mContourData[idx];
					
			BF_ASSERT(gradientIdx >= 0);
			BF_ASSERT(gradientIdx < GRADIENT_DATA_SIZE);	

			destImageData->mBits[i] = ((gradientIdx * 255 / (GRADIENT_DATA_SIZE - 1)) << 24) | (mColor & 0x00FFFFFF);
			//TODO: Apply 'front taper?'				

			if (mNoise > 0)
			{				
				if (rand() % 5 < 4)
				{				
					float pixNoise = 256 * noise;				
					if (aDestColor->a > 0)
					{
						float randPct = ((Rand() % 10000) / 5000.0f) - 1.0f;
						aDestColor->a = BFClamp(aDestColor->a + (int) (randPct * pixNoise + 0.5f), 0, 255);
					}
				}
			}

			if (doFrontTaper)
			{
				float minAlpha = std::min(1.0f, (idx / 4095.0f) * 8.4f);
				aDestColor->a = std::min((int) aDestColor->a, (int) (minAlpha * 255));
			}

			/*if (i % 7 == 0)
			{
				aDestColor->a = 255;
				aDestColor->r = 255;
			}*/
		}
	}		

	delete tempImage;
}

int ImageShadowEffect::GetNeededBorderSize()
{
	return (int) mSize + (int) mDistance;	
}

int ImageDropShadowEffect::GetMixType()
{
	return IMAGEMIX_OUTER;
}

void ImageGlowEffect::Init()
{
	CreateContourAndGradientData();
}

///

void ImageGlowEffect::CreateContourAndGradientData()
{		
	mContourData = CreateContourDataTable(&mContour, (float) mRange / 100.0f);
	mGradientData = CreateGradientDataTable(mColorGradient);	
}

int ImageGlowEffect::GetNeededBorderSize()
{
	//TODO: Empirically we only need (mSize / 2) for soft inner glow...
	return (int) mSize;
}

void ChokedPixelTransform(ImageData* src, ImageData* dest, float radius, float chokePct, bool invert, bool soften = false)
{
	int w = dest->mWidth;
	int h =  dest->mHeight;
	uint32* aDest = dest->mBits;

	ChamferedDistanceTransformInit(src, dest, invert, soften ? (int) radius : 0);	
	ChamferedDistanceTransform(dest->mBits, w, h);
		
	int aSize = w*h;

	int chokePixels = (int) (radius * chokePct + 0.5f);
	
	bool fullChoke = chokePixels == radius;

	uint32* tempBuffer = new uint32[w*h]; 
	uint32* exterior = dest->mBits;
	uint32* anInterior = tempBuffer;

	float rad = radius + 0.5f - chokePixels;
	int inf = (int) (radius + 2) * 256;

	if (soften)
	{
		uint32* in = src->mBits;		

		int iw = src->mWidth;
		int ih = src->mHeight;
		
		int ox = src->mX - dest->mX;
		int oy = src->mY - dest->mY;
		
		for (int y = 0; y < ih; y++)	
		{
			for (int x = 0; x < iw; x++)
			{
				int anAlpha = in[x+y*iw]>>24;
		
				int i = (x+ox)+(y+oy)*w;

				float dist = aDest[i] / 256.0f;		
				dist -= chokePixels + 0.001f;		

				if (dist < 0)					
					anInterior[i] = 0;				
				else if (dist < 1.0f)				
					anInterior[i] = (int) (dist * 256);				
				else				
					anInterior[i] = (int) (dist * 256);					

				if (anAlpha <= 128)
					exterior[i] = (int) ((128.0f - anAlpha) * (radius * 2 + 2) + 0.5f);
				else
					exterior[i] = 0;
			}
		}
				
		inf = (int) (128.0f * (radius * 2 + 2) + 0.5f);
		for (int x = 0; x < w; x++)
		{
			for (int y = 0; y < oy; y++)			
				exterior[x+y*w] = inf;			
			for (int y = oy+ih; y < h; y++)			
				exterior[x+y*w] = inf;			
		}

		for (int y = oy; y < oy+ih; y++)
		{
			for (int x = 0; x < ox; x++)
				exterior[x+y*w] = inf;
			for (int x = ox+iw; x < w; x++)
				exterior[x+y*w] = inf;
		}
	}
	else
	{
		for (int i = 0; i < aSize; i++)
		{	
			float dist = aDest[i] / 256.0f;		
			dist -= chokePixels + 0.001f;		

			if (dist < 0)
			{			
				exterior[i] = inf;
				anInterior[i] = 0;
			}
			else if (dist < 1.0f)
			{
				exterior[i] = BFClamp((int) ((1.0f - dist) * 256.0f), 0, 0xFF);								
				anInterior[i] = (int) (dist * 256);
			}
			else
			{
				exterior[i] = 0;
				anInterior[i] = (int) (dist * 256);
			}
		}
	}
	
	ChamferedDistanceTransform(exterior, w, h);

	float interiorDiv = (rad + 0.5f);		

	float interiorOffset = 0;

	float exteriorOffset = -255;
	float exteriorDivide = (radius + 1.0f - chokePixels);
	
	if (soften)	
	{
		exteriorOffset = 0;			
	}

	if (fullChoke)
	{
		for (int i = 0; i < aSize; i++)		
			aDest[i] = std::min((uint32)0xFF00, exterior[i] * 256);		
	}
	else
	{
		for (int i = 0; i < aSize; i++)
		{	
			uint32 alphaVal = (uint32) (std::max(0.0f, (int) exterior[i] + exteriorOffset ) * 256 / exteriorDivide);			
			alphaVal = std::min((uint32) 0xFF00, (uint32) (alphaVal * 0.5f + 0x7F00));
			
			uint32 alphaVal2 = (uint32) (std::max(0.0f, (int) anInterior[i] + interiorOffset) * 256 / interiorDiv);			

			if (alphaVal <= 0x7F80)
				alphaVal = (uint32) std::min((uint32) 0xFF00, (uint32) std::max(0.0f, (0xFF00 - (int) alphaVal2) * 0.5f));
			
			aDest[i] = alphaVal;
			BF_ASSERT(alphaVal <= 0xFF00);
		}	
	}

	delete [] tempBuffer;
}

void ImageOuterGlowEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int sw = imageData->mWidth;
	int sh = imageData->mHeight;

	int w = destImageData->mWidth;
	int h = destImageData->mHeight;
	
	ImageData* newImage = destImageData;

	float radius = (float) mSize;
	
	int aSize = w*h;
		
	float spread = (float) mSpread / 100.0f;

	float spreadSize = (float) (int) (mSize * spread + 0.5f);		
	int distTransSize = 0;

	if (mTechnique == 'PrBL')
	{
		ChokedPixelTransform(imageData, newImage, radius, spread, false);
	}	
	else if (mTechnique == 'SfBL')
	{	
		radius = std::max(radius, 2.0f); // Radius of '1' gets bumped up to 2
		float aRadiusLeft = radius;

		if (spreadSize != 0)
		{
			// Do distance transform first to "grow" area

			float distLenOffset = spreadSize / 20.0f;
			float distLen = spreadSize - 1.0f + distLenOffset;

			ChamferedDistanceTransformInit(imageData, newImage, false);
			ChamferedDistanceTransform(newImage->mBits, w, h);
			for (int i = 0; i < aSize; i++)
			{				
				float dist = newImage->mBits[i] / 256.0f;
				dist -= spreadSize;
				if (dist < 0)
					newImage->mBits[i] = 0xFF00;
				else if (dist < 1.0f)
					newImage->mBits[i] = BFClamp((int) ((1.0f - dist) * 255.0f * 256.0f), 0, 0xFF00);				
				else
					newImage->mBits[i] = 0;				
			}

			aRadiusLeft -= spreadSize;
		}
		else
		{
			SoftBlurInit(imageData, newImage, false);
		}
		
		float radiusOffset = 1.2f;
		float blurRadius = aRadiusLeft - radiusOffset;
		if (aRadiusLeft <= 2)
			blurRadius = 0.5f;
		if ((aRadiusLeft != 0) && (mSize != 0))
			SoftBlur(newImage->mBits, w, h, blurRadius, 0);
	}		

	//OutputDebugStrF("Contour Time: %d\r\n", timeGetTime() - tickStart);
		
	bool doFrontTaper = 
		((mColorGradient[3].GetVal((1.0f - mContour.GetVal(0) / 255.0f) * mColorGradient[3].mXSize) != 0) || (mNoise != 0));
	
	if ((mAntiAliased) && (!mContour.IsDefault()) && (!doFrontTaper))
		AntiAliasIndices(newImage->mBits, w, h);

	float noise = (float) mNoise / 100.0f;
	float jitter = (float) mJitter / 100.0f;

	for (int i = 0; i < aSize; i++)
	{		
		PackedColor* srcColor = (PackedColor*) (imageData->mBits + i);
		PackedColor* aDestColor = (PackedColor*) (newImage->mBits + i);
		
		int blurVal = newImage->mBits[i];			

		int idx = (blurVal * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
		
		BF_ASSERT(idx >= 0);
		BF_ASSERT(idx < CONTOUR_DATA_SIZE);		

		int gradientIdx = mContourData[idx];

		if ((jitter > 0) && (mHasGradient))
		{
			if (rand() % 5 < 4)
			{	
				float idxJitter = GRADIENT_DATA_SIZE * jitter;
				if (idx != 0)
				{
					float randPct = ((Rand() % 10000) / 5000.0f) - 1.0f;
					gradientIdx = BFClamp(gradientIdx + (int) (randPct * idxJitter + 0.5f), 0, CONTOUR_DATA_SIZE - 1);
				}
			}
		}

		BF_ASSERT(gradientIdx >= 0);
		BF_ASSERT(gradientIdx < GRADIENT_DATA_SIZE);	

		newImage->mBits[i] = mGradientData[gradientIdx];

		if (mNoise > 0)
		{
			if (rand() % 5 < 4)
			{				
				float pixNoise = 256 * noise;				
				if (aDestColor->a > 0)
				{
					float randPct = ((Rand() % 10000) / 5000.0f) - 1.0f;
					aDestColor->a = BFClamp(aDestColor->a + (int) (randPct * pixNoise + 0.5f), 0, 255);
				}
			}
		}

		if (doFrontTaper)
		{
			float minAlpha = std::min(1.0f, (idx / 4095.0f) * 8.4f);
			aDestColor->a = std::min(aDestColor->a, (uint8) (minAlpha * 255));
		}		
	}
}

int ImageOuterGlowEffect::GetMixType()
{
	return IMAGEMIX_OUTER;
}

///

void ImageInnerGlowEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int sw = destImageData->mWidth;
	int sh = destImageData->mHeight;

	int w = sw;
	int h = sh;
	
	ImageData* newImage = destImageData;	

	float radius = (float) mSize;
	
	int aSize = w*h;
	float choke = (float) mChoke / 100.0f;
		
	float chokePixels = (float) (int) (choke * mSize + 0.5f);
	
	bool fullChoke = chokePixels == mSize;
	
	float rad = radius + 0.5f - chokePixels;
	float inf = (radius + 1.5f) * 256;
	
	if (mTechnique == 'PrBL')
	{		
		ChokedPixelTransform(imageData, newImage, radius, choke, true);
	}	
	else if (mTechnique == 'SfBL')
	{	
		// Soft blur
		float aRadiusLeft = radius;
				
		if (chokePixels > 0)
		{
			aRadiusLeft -= chokePixels;
			
			ChamferedDistanceTransformInit(imageData, newImage, true);
			ChamferedDistanceTransform(newImage->mBits, w, h);

			for (int i = 0; i < aSize; i++)
			{
				float dist = newImage->mBits[i] / 256.0f;
				dist -= chokePixels + 0.001f;

				/*if (((int) (imageData->mBits[i] >> 24) == 255) && (dist >= 0))				
					newImage->mBits[i] = std::max(0, 256 - (int) (dist * 256)) * 256;					
				else	
					newImage->mBits[i] = 255*256;				*/
				if (dist < 0)
					newImage->mBits[i] = 0xFF00;
				else if (dist < 1.0f)
					newImage->mBits[i] = BFClamp((int) ((1.0f - dist) * 255.0f * 256.0f), 0, 0xFF00);				
				else
					newImage->mBits[i] = 0;				
			}
		}
		else
		{
			SoftBlurInit(imageData, newImage, true);
		}
		
		SoftBlur(newImage->mBits, w, h, GetSoftBlurRadius(radius, aRadiusLeft), 0xFF00);		
	}	

	//OutputDebugStrF("Contour Time: %d\r\n", timeGetTime() - tickStart);
	
	if ((mAntiAliased) && (!mContour.IsDefault()))
		AntiAliasIndices(newImage->mBits, w, h);

	float noise = (float) mNoise / 100.0f;
	float jitter = (float) mJitter / 100.0f;

	for (int i = 0; i < aSize; i++)
	{
		PackedColor* aDestColor = (PackedColor*) (newImage->mBits + i);
		
		int blurVal = newImage->mBits[i];			

		int idx = (blurVal * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
		
		BF_ASSERT(idx >= 0);
		BF_ASSERT(idx < CONTOUR_DATA_SIZE);		
		
		int gradientIdx = mContourData[idx];

		if ((jitter > 0) && (mHasGradient))
		{
			if (rand() % 5 < 4)
			{	
				float idxJitter = GRADIENT_DATA_SIZE * jitter;
				if (idx != 0)
				{
					float randPct = ((Rand() % 10000) / 5000.0f) - 1.0f;
					gradientIdx = BFClamp(gradientIdx + (int) (randPct * idxJitter + 0.5f), 0, CONTOUR_DATA_SIZE - 1);
				}
			}
		}

		BF_ASSERT(gradientIdx >= 0);
		BF_ASSERT(gradientIdx < GRADIENT_DATA_SIZE);		
				
		newImage->mBits[i] = mGradientData[gradientIdx];
		
		if (mIsCenter)
			aDestColor->a = std::max(0, 255 - aDestColor->a);
		
		if (mNoise > 0)
		{
			if (rand() % 5 < 4)
			{				
				float pixNoise = 256 * noise;				
				if (aDestColor->a > 0)
				{
					float randPct = ((Rand() % 10000) / 5000.0f) - 1.0f;
					aDestColor->a = BFClamp(aDestColor->a + (int) (randPct * pixNoise + 0.5f), 0, 255);
				}
			}
		}
	}	
}

///

ImageBevelEffect::ImageBevelEffect()
{
	mGlossContourData = NULL;
}

ImageBevelEffect::~ImageBevelEffect()
{
	delete mGlossContourData;
}

void ImageBevelEffect::Init()
{
	mGlossContourData = CreateContourDataTable(&mGlossContour);	

	if (mUseContour)
	{
		float aRange = (float) (mBevelContourRange / 100.0f);
		mBevelContourData = new int32[CONTOUR_DATA_SIZE];
		int minVal = 0;
		for (int i = 0; i < CONTOUR_DATA_SIZE; i++)
		{
			float aX = i / (float) (CONTOUR_DATA_SIZE - 1);
			aX = (aX - (1.0f - aRange)) / aRange;
			float yVal = mBevelContour.GetVal(aX * 255.0f) / 255.0f;
			int aVal = (int) (yVal * 0xFF00 + 0.5f);
			minVal = std::min(minVal, aVal);
			mBevelContourData[i] = aVal;
		}

		for (int i = 0; i < CONTOUR_DATA_SIZE; i++)
			mBevelContourData[i] -= minVal;
	}
}

void ImageBevelEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	
}

int ImageBevelEffect::GetMixType()
{
	if (mStyle == 'OtrB') // Outer bevel
		return IMAGEMIX_OUTER;
	if (mStyle == 'Embs') // Emboss
		return IMAGEMIX_OVER;
	if (mStyle == 'PlEb') // Pillow emboss
		return IMAGEMIX_OVER;

	return IMAGEMIX_INNER;
}

void ImageBevelEffect::Apply(int pass, int style, PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* hiliteImage, ImageData* shadowImage)
{
	int w = hiliteImage->mWidth;
	int h = hiliteImage->mHeight;
	int aSize = w*h;
	uint32* normalMap = new uint32[aSize];

	PSDLayerInfo* aLayerInfo = layerInfo;
	ImageData* anImageData = imageData;

	ImageData* newImage = hiliteImage;
		
	float aDepth = (float) mDepth / 100.0f;
	float aRadius = (float) mSize;
		
	float angle = mUseGlobalLight ? (float) aLayerInfo->mPSDReader->mGlobalAngle : (float) mLocalAngle;
	float altitude = mUseGlobalLight ? (float) aLayerInfo->mPSDReader->mGlobalAltitude : (float) mLocalAltitude;

	float lightAngle = angle * BF_PI / 180.0f;
	float lightElevation = altitude * BF_PI / 180.0f;

	bool doFlip = !mDirectionUp;
	if (pass == 1)
		doFlip = !doFlip;

	if (doFlip)
		lightAngle = lightAngle + BF_PI;	

	Vector3 shadowVec(cosf((float) lightElevation) * -cosf(lightAngle),
		cosf(lightElevation) * sinf(lightAngle),
		sinf(lightElevation));	
	
	bool doOuterTaper = (style == 'OtrB') || (style == 'Embs') || (style == 'PlEb');
	float blurRadius = aRadius;

	if ((style == 'Embs') || (style == 'PlEb'))		
		blurRadius = (float) (int) (blurRadius / 2 + 0.75f);

	if (mTechnique == 'SfBL') // Technique:Smooth
	{
		SoftBlurInit(anImageData, newImage, false);
		SoftBlur(newImage->mBits, w, h, blurRadius - 1.0f, 0);
			
		for (int i = 0; i < aSize; i++)
		{
			uint32 aVal = newImage->mBits[i];
			newImage->mBits[i] = std::min(0xFF00, (int) (aVal + 256 / blurRadius));
		}		
	}
	else
	{
		ChokedPixelTransform(anImageData, newImage, blurRadius, 0, true, mTechnique == 'Slmt');
			
		for (int i = 0; i < aSize; i++)
		{
			uint32 aVal = newImage->mBits[i];			
			newImage->mBits[i] = 0xFF00 - aVal;
		}		
	}

	if (mUseContour)
	{		
		for (int i = 0; i < aSize; i++)
		{
			uint32 aVal = newImage->mBits[i];
			int idx = (aVal * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
			newImage->mBits[i] = mBevelContourData[idx];
		}
	}

	if (mUseTexture)
	{		
		PSDPattern* pattern = aLayerInfo->mPSDReader->mPSDPatternMap[mTexture.mPatternName];

		int32 texMultiply = (int32) (0x100 * mTextureDepth / 100.0f);
		int32 texOffset = texMultiply * 255;
		if (!mTextureInvert)
			texMultiply = -texMultiply;

		int mipLevel = 0;

		float dU = 1.0f / (float) (mTexture.mScale / 100.0f);
		float dV = dU;

		float virtPW = (float) pattern->mWidth;
		float virtPH = (float) pattern->mHeight;

		while (dU > 1.0001f)
		{			
			// Select next mip level
			mipLevel++;
			PSDPattern* aMip = pattern->GetNextMipLevel();			
			dU /= 2.0f;
			dV /= 2.0f;
			virtPW /= 2.0f;
			virtPH /= 2.0f;
			pattern = aMip;
		}

		int pw = pattern->mWidth;
		int ph = pattern->mHeight;

		float startU = (float) (newImage->mX - (int) BFRound((float) mTexture.mPhaseX)) * dU;
		while (startU < 0)
			startU += virtPW;
		float startV = (float) (newImage->mY - (int) BFRound((float) mTexture.mPhaseY)) * dV;
		if (startV < 0)
			startV += virtPH;
		
		float aU = startU;
		float aV = startV;
						
		for (int y = 0; y < h; y++)
		{
			aU = startU;

			uint32* aBits = newImage->mBits + (y*w);
			
			for (int x = 0; x < w; x++)
			{								
				int u1 = ((int) aU) % pw;
				int v1 = ((int) aV) % ph;

				float ua = aU - (int) aU;				
				float va = aV - (int) aV;

				int u2 = (u1 + 1) % pw;
				int v2 = (v1 + 1) % ph;

				int intensity = (int) ((
					((float) pattern->mIntensityBits[u1+v1*pw] * (1.0f - ua) * (1.0f - va)) +
					((float) pattern->mIntensityBits[u2+v1*pw] * (       ua) * (1.0f - va)) +
					((float) pattern->mIntensityBits[u1+v2*pw] * (1.0f - ua) * (       va)) +
					((float) pattern->mIntensityBits[u2+v2*pw] * (       ua) * (       va))
					) * texMultiply + 0.5f);
				
				*aBits = *aBits + intensity + texOffset;
				
				aBits++;				
				aU += dU;
				aU -= (int) (aU / virtPW) * virtPW;
			}
			
			aV += dV;
			aV -= (int) (aV / virtPH) * virtPH;
		}
	}

	CreateNormalMap(newImage->mBits, normalMap, w, h, 255.0f / blurRadius / aDepth * 0.80f, &shadowVec);

	// Shadows need to be weightedly differently for 'softening' purposes
	//  This equation fits the proper curve, but I don't really understand why
	float shadowWeighting = sin(lightElevation) / (1.0f - sin(lightElevation));	

	if ((mAntiAliased) && (!mGlossContour.IsDefault()))
		AntiAliasIndices(normalMap, w, h);

	for (int i = 0; i < aSize; i++)
	{			
		float dot = (int) (normalMap[i] - 0x100000) / (float) 0xFF00;
			
		float zeroPt = shadowVec.mZ;		

		//TODO: Cache gloss contour
		float curvePos = mGlossContour.GetVal(255.0f * dot) / 255.0f;
		curvePos -= zeroPt;

		float curveIdx = 0;
		
		if (curvePos > 0)
		{
			float hilitePct = curvePos / (1.0f - zeroPt);
			curvePos = hilitePct;

			curveIdx = curvePos;
		}
		else
		{
			float shadowPct = curvePos / zeroPt;
			curvePos /= zeroPt;
		}

		float lightVal = curvePos;
		lightVal = BFClamp(lightVal, -1.0f, 1.0f);
		
		if (lightVal < 0)
			lightVal *= shadowWeighting;

		normalMap[i] = std::max((uint32) (0xFF00 * lightVal) + 0x100000, (uint32) 0);
	}

	float aSoften = (float) mSoften;
	if (aSoften > 0)
	{
		float d = GetSoftBlurRadius(aSoften) / 2;		
		if (d > 0)
		{		
			uint32* tempBuffer = newImage->mBits;
			for (int i = 0; i < 2; i++)
			{
				BoxBlur(normalMap, tempBuffer, w, h, d, 0);
				BoxBlur(tempBuffer, normalMap, h, w, d, 0);
			}			
		}
	}
	
	for (int i = 0; i < aSize; i++)
	{	
		float lightVal = (int) (normalMap[i] - 0x100000) / (float) 0xFF00;

		if (lightVal < 0)
			lightVal /= shadowWeighting;

		if (doOuterTaper)
		{
			float taperVal = newImage->mBits[i] / 256.0f / 255.0f * 7.85f;
			
			if (lightVal > 0)
				lightVal = std::min(lightVal, taperVal);
			else 
				lightVal = std::max(lightVal, -taperVal);
		}
		
		if (lightVal > 0)
		{
			hiliteImage->mBits[i] = ((int) (BFClamp(lightVal, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) | (mHiliteColor & 0x00FFFFFF);
			shadowImage->mBits[i] = 0;
		}
		else
		{
			hiliteImage->mBits[i] = 0;
			shadowImage->mBits[i] = ((int) (BFClamp(-lightVal, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) | (mShadowColor & 0x00FFFFFF);
		}
	}

	delete [] normalMap;
}

void ImageBevelEffect::Apply(ImageEffectCtx* ctx)
{
	if (mStyle == 'stro')
		return; 

	ImageData* hiliteEffectImage = new ImageData();
	hiliteEffectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);
	hiliteEffectImage->mX = ctx->mBlendX;
	hiliteEffectImage->mY = ctx->mBlendY;

	ImageData* shadowEffectImage = new ImageData();
	shadowEffectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);
	shadowEffectImage->mX = ctx->mBlendX;
	shadowEffectImage->mY = ctx->mBlendY;
	
	// Pillow emboss takes two passes, the others only take one
	for (int aPass = 0; aPass < 2; aPass++)
	{
		bool needsMorePasses = false;

		Apply(aPass, mStyle, ctx->mLayerInfo, ctx->mLayerImage, hiliteEffectImage, shadowEffectImage);		
		
		if ((aPass == 0) && ((mStyle == 'Embs') || (mStyle == 'PlEb') || (mStyle == 'InrB') || (mStyle == 'PlEb'))) // Emboss or pillow emboss or inner
		{
			BlendImage(ctx->mInnerImage, hiliteEffectImage, 0, 0, (float) mHiliteOpacity / 100.0f, mHiliteMode);	
			BlendImage(ctx->mInnerImage, shadowEffectImage, 0, 0, (float) mShadowOpacity / 100.0f, mShadowMode);	

			needsMorePasses |= (mStyle == 'PlEb');
		}

		if ((mStyle == 'Embs') || (mStyle == 'OtrB') || ((mStyle == 'PlEb') && (aPass == 1))) // Emboss or pillow emboss or outer
		{
			BlendImage(ctx->mOuterImage, hiliteEffectImage, 0, 0, (float) mHiliteOpacity / 100.0f, mHiliteMode);
			BlendImage(ctx->mOuterImage, shadowEffectImage, 0, 0, (float) mShadowOpacity / 100.0f, mShadowMode);
		}

		if (!needsMorePasses)
			break;
	}

	delete shadowEffectImage;
	delete hiliteEffectImage;
}

int ImageBevelEffect::GetNeededBorderSize()
{
	return (int) mSize;
}

///

void ImageSatinEffect::Init()
{
	mContourData = CreateContourDataTable(&mContour, 1.0f);
}

void ImageSatinEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int sw = destImageData->mWidth;
	int sh = destImageData->mHeight;

	int w = sw;
	int h = sh;
	
	ImageData* newImage = destImageData;	
	
	int aSize = w*h;
	
	ImageData* tempImage = new ImageData();
	tempImage->CreateNew(w, h);

	SoftBlurInit(imageData, tempImage, false);							
	SoftBlur(tempImage->mBits, w, h, GetSoftBlurRadius((float) mSize), 0);
	
	float angle = (float) mAngle * BF_PI / 180.0f;
	int ofsx = (int) (BFRound(cosf(angle) * (float) mDistance));
	int ofsy = (int) (BFRound(-sinf(angle) * (float) mDistance));

	uint32* tempData = tempImage->mBits;

	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{			
			int x1 = BFClamp(x + ofsx, 0, w - 1);
			int y1 = BFClamp(y + ofsy, 0, h - 1);						
			int idx1 = (tempData[x1+y1*w] * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
			int val1 = mContourData[idx1] * 255 / (GRADIENT_DATA_SIZE - 1);
			
			int x2 = BFClamp(x - ofsx, 0, w - 1);
			int y2 = BFClamp(y - ofsy, 0, h - 1);			
			int idx2 = (tempData[x2+y2*w] * (CONTOUR_DATA_SIZE - 1) / 255) / 256;
			int val2 = mContourData[idx2] * 255 / (GRADIENT_DATA_SIZE - 1);			

			uint32 aVal = (uint32) abs((int) val1 - (int) val2);			
			newImage->mBits[x+y*w] = aVal;
		}
	}

	if ((mAntiAliased) && (!mContour.IsDefault()))
		AntiAliasIndices(newImage->mBits, w, h);

	if (mInvert)
	{
		for (int i = 0; i < aSize; i++)
			newImage->mBits[i] = ((255 - newImage->mBits[i]) << 24) | (mColor & 0x00FFFFFF);			
	}
	else
	{
		for (int i = 0; i < aSize; i++)
			newImage->mBits[i] = (newImage->mBits[i] << 24) | (mColor & 0x00FFFFFF);
	}

	delete tempImage;
}

int ImageSatinEffect::GetMixType() // Default:Interior
{
	return IMAGEMIX_INNER;
}

int ImageSatinEffect::GetNeededBorderSize()
{
	return (int) mSize;
}

void ImageColorOverlayEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	mColorFill.Apply(layerInfo, imageData, destImageData);
}

///

void ImageColorFill::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int w = destImageData->mWidth;
	int h = destImageData->mHeight;
	int aSize = w*h;
	for (int i = 0; i < aSize; i++)
		destImageData->mBits[i] = mColor;
}

ImageGradientFill::ImageGradientFill()
{
	mGradientData = NULL;
}

ImageGradientFill::~ImageGradientFill()
{
	delete mGradientData;
}

void ImageGradientFill::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int w = destImageData->mWidth;
	int h = destImageData->mHeight;

	int iw = layerInfo->mWidth;
	int ih = layerInfo->mHeight;

	int ox = layerInfo->mX - destImageData->mX;
	int oy = layerInfo->mY - destImageData->mY;

	if (mGradientData == NULL)
	{
		mGradientData = CreateGradientDataTable(mColorGradient);
		if (mReverse)
		{
			for (int i = 0; i < GRADIENT_DATA_SIZE / 2; i++)
			{
				int endIdx = (GRADIENT_DATA_SIZE - 1) - i;
				uint32 swap = mGradientData[i];
				mGradientData[i] = mGradientData[endIdx];
				mGradientData[endIdx] = swap;
			}
		}
	}

	int minX = w;
	int maxX = 0;
	int minY = h;
	int maxY = 0;

	for (int y = 0; y < ih; y++)
	{
		uint32* checkBits = imageData->mBits + (y*iw);
		
		bool hadPixel = false;
		
		for (int x = 0; x < iw; x++)
		{
			if ((checkBits[x] >> 24) >= 128)
			{
				minX = std::min(minX, x + ox);
				hadPixel = true;
				break;
			}
		}

		for (int x = iw - 1; x >= 0; x--)
		{
			if ((checkBits[x] >> 24) >= 128)
			{
				maxX = std::max(maxX, x + ox);
				hadPixel = true;
				break;
			}
		}

		if (hadPixel)
		{
			minY = std::min(minY, y + oy);
			maxY = std::max(maxY, y + oy);
		}
	}

	int contentW = std::max(0, maxX - minX + 1);
	int contentH = std::max(0, maxY - minY + 1);

	float angleDeg = (float) mAngle;
	// There seems to be some inaccuracy with photoshop with angles.
	//  There's a large gap between 180 degrees and 179 degrees than there should be, for example
	//  This is not yet simulated here.  Offsets are also slightly effected.
	float angle = BF_PI * angleDeg / 180.0f;
	
	float matA = -cosf(angle);
	float matB = sinf(angle);		
	float matC = sin(angle);
	float matD = cos(angle);	

	float xOfs = 0;
	float yOfs = 0;
	
	float gradWidth;
	float gradHeight;

	float offsetScale = 1.0f;

	float sizeBasis;
	if (mAlignWithLayer)
	{		
		gradWidth = (float) contentW;
		gradHeight = (float) contentH;		
		xOfs = -(float)contentW/2 - minX;
		yOfs = -(float)contentH/2 - minY;		
	}
	else	
	{		
		gradWidth = (float) layerInfo->mPSDReader->mWidth;
		gradHeight = (float) layerInfo->mPSDReader->mHeight;
		xOfs = destImageData->mX - (float) gradWidth/2;
		yOfs = destImageData->mY - (float) gradHeight/2;
	}

	xOfs -= ((float) mOffsetX / 100.0f) * gradWidth;
	yOfs -= ((float) mOffsetY / 100.0f) * gradHeight;

	float sizeBasisX = gradWidth / fabs(matA);
	float sizeBasisY = gradHeight / fabs(matB);
	sizeBasis = std::min(sizeBasisX, sizeBasisY);

	float scale = (float) mScale / 100.0f;	
	
	float gradientScale = (float) (GRADIENT_DATA_SIZE - 1) / sizeBasis / scale * 2;

	for (int y = 0; y < h; y++)
	{
		uint32* aBits = destImageData->mBits + (y*w);
			
		for (int x = 0; x < w; x++)
		{
			float aX = (float) x + xOfs;
			float aY = (float) y + yOfs;

			float xRot = (aX * matA) + (aY * matB);
			float yRot = (aX * matC) + (aY * matD);

			int gradientIdx;
			switch (mStyle)
			{
			case 'Lnr ':
				gradientIdx = BFClamp((int) ((xRot/2) * gradientScale + GRADIENT_DATA_SIZE/2 + 0.5f), 0, GRADIENT_DATA_SIZE - 1);
				break;
			case 'Rdl ':
				{
					float dist = sqrt(xRot*xRot + yRot * yRot);
					gradientIdx = (GRADIENT_DATA_SIZE - 1) - BFClamp((int) ((dist) * gradientScale + 0.5f), 0, GRADIENT_DATA_SIZE - 1);
				}
				break;
			case 'Angl':
				{
					float ang = (atan2(yRot, xRot) / BF_PI / 2.0f) + 0.5f;
					gradientIdx = BFClamp((int) ((ang) * (GRADIENT_DATA_SIZE - 1) + 0.5f), 0, GRADIENT_DATA_SIZE - 1);
				}
				break;
			case 'Rflc':
				gradientIdx = (GRADIENT_DATA_SIZE - 1) - BFClamp((int) (fabs(xRot) * gradientScale + 0.5f), 0, GRADIENT_DATA_SIZE - 1);
				break;			
			case 'Dmnd':				
				gradientIdx = (GRADIENT_DATA_SIZE - 1) - BFClamp((int) ((fabs(xRot) + fabs(yRot)) * gradientScale + 0.5f), 0, GRADIENT_DATA_SIZE - 1);				
				break;
			}
						
			aBits[x] = mGradientData[gradientIdx];
		}
	}
}

void ImagePatternFill::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{		
	PSDPattern* pattern = layerInfo->mPSDReader->mPSDPatternMap[mPatternName];
		
	int mipLevel = 0;

	int w = destImageData->mWidth;
	int h = destImageData->mHeight;

	float dU = 1.0f / (float) (mScale / 100.0f);
	float dV = dU;

	float virtPW = (float) pattern->mWidth;
	float virtPH = (float) pattern->mHeight;

	while ((dU > 1.0001f) && (pattern->mWidth >= 4) && (pattern->mHeight >= 4))
	{			
		// Select next mip level
		mipLevel++;
		PSDPattern* aMip = pattern->GetNextMipLevel();			
		dU /= 2.0f;
		dV /= 2.0f;
		virtPW /= 2.0f;
		virtPH /= 2.0f;
		pattern = aMip;
	}

	int pw = pattern->mWidth;
	int ph = pattern->mHeight;

	float phaseX = (float) mPhaseX;
	float phaseY = (float) mPhaseY;

	if (mLinkWithLayer)
	{
		phaseX += (float) layerInfo->mRefX;
		phaseY += (float) layerInfo->mRefY;
	}

	float startU = (float) (destImageData->mX - (int) BFRound((float) phaseX)) * dU;
	while (startU < 0)
		startU += virtPW;
	float startV = (float) (destImageData->mY - (int) BFRound((float) phaseY)) * dV;
	if (startV < 0)
		startV += virtPH;
		
	float aU = startU;
	float aV = startV;
						
	for (int y = 0; y < h; y++)
	{
		aU = startU;

		uint32* aBits = destImageData->mBits + (y*w);
			
		for (int x = 0; x < w; x++)
		{								
			int u1 = ((int) aU) % pw;
			int v1 = ((int) aV) % ph;

			float ua = aU - (int) aU;				
			float va = aV - (int) aV;

			int u2 = (u1 + 1) % pw;
			int v2 = (v1 + 1) % ph;

			uint32 aColor1 = pattern->mBits[u1+v1*pw];
			uint32 aColor2 = pattern->mBits[u2+v1*pw];
			uint32 color3 = pattern->mBits[u1+v2*pw];
			uint32 color4 = pattern->mBits[u2+v2*pw];

			uint32 a1 = (int) ((1.0f - ua) * (1.0f - va) * 256 + 0.5f);
			uint32 a2 = (int) ((	   ua) * (1.0f - va) * 256 + 0.5f);
			uint32 a3 = (int) ((1.0f - ua) * (       va) * 256 + 0.5f);
			uint32 a4 = (int) ((       ua) * (       va) * 256 + 0.5f);

			*aBits =
                (((((aColor1 & 0x00FF00FF) * a1) + ((aColor2 & 0x00FF00FF) * a2) + 
					((color3 & 0x00FF00FF) * a3) + ((color4 & 0x00FF00FF) * a4)) >> 8) & 0x00FF00FF) |                
                (((((aColor1 >> 8) & 0x00FF00FF) * a1) + (((aColor2 >> 8) & 0x00FF00FF) * a2) +
					(((color3 >> 8) & 0x00FF00FF) * a3) + (((color4 >> 8) & 0x00FF00FF) * a4)) & 0xFF00FF00);

			//*aBits = aColor1;
							
			aBits++;				
			aU += dU;
			aU -= (int) (aU / virtPW) * virtPW;
		}
			
		aV += dV;
		aV -= (int) (aV / virtPH) * virtPH;
	}
}

void ImagePatternOverlayEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	mPattern.Apply(layerInfo, imageData, destImageData);
}

void ImageGradientOverlayEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	mGradientFill.Apply(layerInfo, imageData, destImageData);
}

void ImageStrokeEffect::Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData)
{
	int w = destImageData->mWidth;
	int h = destImageData->mHeight;
	int aSize = w*h;

	ImageData* tempImage = new ImageData();
	tempImage->CreateNew(w, h);
	tempImage->mX = destImageData->mX;
	tempImage->mY = destImageData->mY;

	float aRadius = (float) mSize;	
	float blurRadius = aRadius;

	if (mPosition == 'CtrF')
		blurRadius /= 2;
	
	uint32* aDest = tempImage->mBits;

	ChamferedDistanceTransformInit(imageData, tempImage, false, 0);	
	ChamferedDistanceTransform(aDest, w, h);
			
	uint32* tempBuffer = destImageData->mBits;//new uint32[w*h]; 
	uint32* exterior = aDest;
	uint32* anInterior = tempBuffer;

	float rad = blurRadius;
	int inf = (int) (blurRadius + 2) * 256;
	
	for (int i = 0; i < aSize; i++)
	{	
		float dist = aDest[i] / 256.0f;		
		dist -= 0.001f;

		if (dist < 0)
		{			
			exterior[i] = inf;
			anInterior[i] = 0;
		}
		else if (dist < 1.0f)
		{
			exterior[i] = BFClamp((int) ((1.0f - dist) * 256.0f), 0, 0xFF);								
			anInterior[i] = (int) (dist * 256);
		}
		else
		{
			exterior[i] = 0;
			anInterior[i] = (int) (dist * 256);
		}
	}	
	
	ChamferedDistanceTransform(exterior, w, h);

	for (int i = 0; i < aSize; i++)
	{
		float distInterior = (anInterior[i] / 256.0f) - rad;
		float distExterior = (exterior[i] / 256.0f) - rad;
		float maxDist = std::max(distInterior, distExterior);		

		if (maxDist < 0)
			aDest[i] = 0xFF000000;
		else if (maxDist < 1.0f)
			aDest[i] = ((int) (255.0f * (1.0f - maxDist))) << 24;
		else
			aDest[i] = 0;
	}

	tempImage->mX = destImageData->mX;
	tempImage->mY = destImageData->mY;

	if (mFillType == 'Ptrn')
		mPatternFill.Apply(layerInfo, imageData, destImageData);
	else if (mFillType == 'GrFl')
		mGradientFill.Apply(layerInfo, imageData, destImageData);
	else
		mColorFill.Apply(layerInfo, imageData, destImageData);

	for (int i = 0; i < aSize; i++)
	{		
		uint32 srcAlpha = tempImage->mBits[i] >> 24;
		uint32 destAlpha = destImageData->mBits[i] >> 24;

		destImageData->mBits[i] = ((int) (srcAlpha * destAlpha / 255 + 0.5f) << 24) | (destImageData->mBits[i] & 0x00FFFFFF);
	}

	delete tempImage;
}

void ImageStrokeEffect::Apply(ImageEffectCtx* ctx)
{
	// Try to find a bevel effect with a 'Stroke Emboss' style
	ImageBevelEffect* bevelEffect = NULL;
	for (int effectIdx = 0; effectIdx < (int) ctx->mLayerInfo->mImageEffects->mImageEffectVector.size(); effectIdx++)
	{
		BaseImageEffect* anEffect = ctx->mLayerInfo->mImageEffects->mImageEffectVector[effectIdx];

		bevelEffect = dynamic_cast<ImageBevelEffect*>(anEffect);
		if ((bevelEffect != NULL) && (bevelEffect->mStyle == 'stro')) 
			break;
		bevelEffect = NULL;
	}

	if ((mOpacity == 100.0) && (bevelEffect == NULL))
	{
		BaseImageEffect::Apply(ctx);
		return;
	}

	ImageData* effectImage = new ImageData();
	effectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);
	effectImage->mX = ctx->mBlendX;
	effectImage->mY = ctx->mBlendY;
	Apply(ctx->mLayerInfo, ctx->mLayerImage, effectImage);

	float opacity = (float) mOpacity / 100.0f;	

	ImageData* mixImage = CreateResizedImageUnion(ctx->mOrigImage, effectImage->mX, effectImage->mY, effectImage->mWidth, effectImage->mHeight);
	BlendImage(mixImage, effectImage, effectImage->mX - mixImage->mX, effectImage->mY - mixImage->mY, opacity, mBlendMode, true);

	if (bevelEffect != NULL)
	{
		ImageData* hiliteEffectImage = new ImageData();
		hiliteEffectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);
		hiliteEffectImage->mX = ctx->mBlendX;
		hiliteEffectImage->mY = ctx->mBlendY;

		ImageData* shadowEffectImage = new ImageData();
		shadowEffectImage->CreateNew(ctx->mBlendWidth, ctx->mBlendHeight);
		shadowEffectImage->mX = ctx->mBlendX;
		shadowEffectImage->mY = ctx->mBlendY;

		int aStyle = 'Embs';
		if (mPosition == 'OutF')
			aStyle = 'OtrB';
		else if (mPosition == 'InsF')
			aStyle = 'InrB';
		bevelEffect->Apply(0, aStyle, ctx->mLayerInfo, ctx->mLayerImage, hiliteEffectImage, shadowEffectImage);
		
		BlendImage(mixImage, hiliteEffectImage, hiliteEffectImage->mX - mixImage->mX, hiliteEffectImage->mY - mixImage->mY, (float) bevelEffect->mHiliteOpacity / 100.0f, bevelEffect->mHiliteMode);	
		BlendImage(mixImage, shadowEffectImage, shadowEffectImage->mX - mixImage->mX, shadowEffectImage->mY - mixImage->mY, (float) bevelEffect->mShadowOpacity / 100.0f, bevelEffect->mShadowMode);

		delete shadowEffectImage;
		delete hiliteEffectImage;
	}
	
	int mixType = GetMixType();
	if ((mixType == IMAGEMIX_INNER) || (mixType == IMAGEMIX_OVER))	
		BlendImagesTogether(ctx->mInnerImage, mixImage, effectImage);
	if ((mixType == IMAGEMIX_OUTER) || (mixType == IMAGEMIX_OVER))
		BlendImagesTogether(ctx->mOuterImage, mixImage, effectImage);
	
	delete mixImage;

	delete effectImage;
}

int ImageStrokeEffect::GetMixType() // Default:Interior
{
	if (mPosition == 'OutF') // Outside
		return IMAGEMIX_OUTER;
	if (mPosition == 'InsF') // Inside
		return IMAGEMIX_INNER;
	if (mPosition == 'CtrF') // Inside
		return IMAGEMIX_OVER;

	return IMAGEMIX_INNER;
}

int ImageStrokeEffect::GetNeededBorderSize()
{
	return (int)(mSize + 2.5);
}

bool ImageStrokeEffect::NeedsOrigBits(ImageEffects* effects)
{
	ImageBevelEffect* bevelEffect = NULL;
	for (int effectIdx = 0; effectIdx < (int) effects->mImageEffectVector.size(); effectIdx++)
	{
		BaseImageEffect* anEffect = effects->mImageEffectVector[effectIdx];

		bevelEffect = dynamic_cast<ImageBevelEffect*>(anEffect);
		if ((bevelEffect != NULL) && (bevelEffect->mStyle == 'stro')) 
			return true;
	}

	return mOpacity != 100.0;
}
