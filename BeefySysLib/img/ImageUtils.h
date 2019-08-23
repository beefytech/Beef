#pragma once

#include "Common.h"

NS_BF_BEGIN;

class ImageData;

struct PackedColor
{
	uint8 r;
	uint8 g;
	uint8 b;
	uint8 a;
};

class PackedColorGetR
{
public:
	int operator()(PackedColor color) { return color.r; }
};

class PackedColorGetG
{
public:
	int operator()(PackedColor color) { return color.g; }
};

class PackedColorGetB
{
public:
	int operator()(PackedColor color) { return color.b; }
};

class PackedColorGetGray
{
public:
	int operator()(PackedColor color) { return ((color.r * 300) + (color.g * 586) + (color.b * 113)) / 1000; }
};

ImageData* CreateResizedImageUnion(ImageData* src, int x, int y, int width, int height);
ImageData* CreateEmptyResizedImageUnion(ImageData* src, int x, int y, int width, int height);
void CrossfadeImage(ImageData* origImage, ImageData* newImage, float opacity);
void BlendImage(ImageData* dest, ImageData* src, int destX, int destY, float alpha = 1.0f, int mixType = 'Nrml', bool fullAlpha = false);
void BlendImagesTogether(ImageData* bottomImage, ImageData* topImage, ImageData* alphaImage);
void SetImageAlpha(ImageData* image, ImageData* alphaImage);
void MultiplyImageAlpha(ImageData* image, ImageData* alphaImage);
void SetImageAlpha(ImageData* image, int alpha);
void CopyImageBits(ImageData* dest, ImageData* src);

NS_BF_END;
