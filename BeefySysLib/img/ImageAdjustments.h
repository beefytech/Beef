#pragma once

#include "Common.h"

NS_BF_BEGIN;

class ImageData;
class PSDLayerInfo;
class ImageGradientFill;
class ImagePatternFill;

class ImageAdjustment
{
public:	
	~ImageAdjustment();

	virtual ImageData*		CreateAdjustedImage(PSDLayerInfo* layerInfo, ImageData* destImage);
	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

class InvertImageAdjustement : public ImageAdjustment
{
public:
	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

class SolidColorImageAdjustement : public ImageAdjustment
{
public:
	uint32					mColor;

public:
	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

class GradientImageAdjustement : public ImageAdjustment
{
public:
	ImageGradientFill*		mFill;

public:
	~GradientImageAdjustement();

	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

class PatternImageAdjustement : public ImageAdjustment
{
public:
	ImagePatternFill*		mFill;

public:
	~PatternImageAdjustement();

	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

class BrightnessContrastImageAdjustment : public ImageAdjustment
{
public:
	int						mBrightness;
	int						mContrast;
	int						mMeanValue;
	bool					mLabColorOnly;

public:
	virtual void			ApplyImageAdjustment(PSDLayerInfo* layerInfo, ImageData* image);
};

NS_BF_END;
