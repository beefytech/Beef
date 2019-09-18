#pragma once

#include "Common.h"
#include "RenderTarget.h"

NS_BF_BEGIN;

class ImageData;

class Texture : public RenderTarget
{
public:	
	int						mRefCount;

public:
	Texture();
	virtual ~Texture() {}

	virtual void			AddRef();
	virtual void			Release();
	virtual void			PhysSetAsTarget() = 0;

	virtual void			Blt(ImageData* imageData, int x, int y) { };
	virtual void			SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits) {}
	virtual void			GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) {}
};

class TextureSegment
{
public:
	Texture*				mTexture;
	float					mU1;
	float					mV1;
	float					mU2;
	float					mV2;
	float					mScaleX;
	float					mScaleY;

public:
	void					InitFromTexture(Texture* texture);
};

NS_BF_END;
