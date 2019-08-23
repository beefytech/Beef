#include "ImageAdjustments.h"
#include "ImageData.h"
#include "PSDReader.h"
#include "ImageUtils.h"
#include "ImgEffects.h"

USING_NS_BF;

ImageAdjustment::~ImageAdjustment()
{
}

void ImageAdjustment::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{
}

ImageData* ImageAdjustment::CreateAdjustedImage(PSDLayerInfo* layerInfo, ImageData* destImage)
{	
	ImageData* newImage = destImage->Duplicate();
	ApplyImageAdjustment(layerInfo, newImage);
	CrossfadeImage(destImage, newImage, layerInfo->mFillOpacity / 255.0f);
	return newImage;
}

void InvertImageAdjustement::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{
	int size = image->mWidth*image->mHeight;
	for (int i = 0; i < size; i++)
	{
		image->mBits[i] = 
			(image->mBits[i] & 0xFF000000) |
			((0xFFFFFFFF - image->mBits[i]) & 0x00FFFFFF);
	}
}

void SolidColorImageAdjustement::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{
	int size = image->mWidth*image->mHeight;
	for (int i = 0; i < size; i++)	
		image->mBits[i] = mColor;	
}

GradientImageAdjustement::~GradientImageAdjustement()
{
	delete mFill;
}

void GradientImageAdjustement::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{	
	mFill->Apply(layerInfo, image, image);
}

PatternImageAdjustement::~PatternImageAdjustement()
{
	delete mFill;
}

void PatternImageAdjustement::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{
	mFill->Apply(layerInfo, image, image);
}

void BrightnessContrastImageAdjustment::ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image)
{
	int size = image->mWidth*image->mHeight;	
	for (int i = 0; i < size; i++)	
	{		
		PackedColor* color = (PackedColor*) (&image->mBits[i]);
		//int effect = 256 - (int) (pow(abs(color->r - mMeanValue)/127.0, 2.0)*127);		
		//color->r = BFClamp(color->r + effect*mBrightness/256, 0, 255);

		color->r = (int) (pow(color->r / 255.0f, 1.0f - mBrightness/200.0f) * 255);

		image->mBits[i] = *((uint32*) color);
	}
}