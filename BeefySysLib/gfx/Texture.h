#pragma once

#include "Common.h"
#include "RenderTarget.h"
#include "../util/Rect.h"

NS_BF_BEGIN;

class ImageData;

class Texture : public RenderTarget
{
public:	
	int						mRefCount;
	// Bound as render target 1 (SV_Target1) whenever this texture is the target; same size, unowned.
	Texture*				mSecondaryTarget;

public:
	Texture();
	virtual ~Texture() {}

	virtual void			AddRef();
	virtual void			Release();
	virtual void			PhysSetAsTarget() = 0;

	virtual void			Blt(ImageData* imageData, int x, int y) { }
	virtual void			SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits) {}
	virtual void			GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) {}
	// Raw float bits from a render target's depth buffer -- see DXTexture::GetDepthBits.
	virtual void			GetDepthBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits) {}
	// Wraps a render target's depth buffer as its own sampleable texture -- see DXTexture::CreateDepthRef.
	virtual Texture*		CreateDepthRef() { return NULL; }

	virtual void*			GetSharedHandle() { return NULL; }
	virtual bool			AcquireKeyedMutex(uint64 key, uint32 timeoutMs) { return false; }
	virtual void			ReleaseKeyedMutex(uint64 key) {}

	// Resolves this MSAA render target into a matching-size single-sample target.
	virtual void			ResolveTo(Texture* dest) {}
	// Regenerates the mip chain from level 0 (render targets created with the mipmaps flag only).
	virtual void			GenerateMips() {}
	// Copies the top-left width x height of `src` (mip 0) into this texture's mip `mipLevel`;
	// formats must match.
	virtual void			CopyToMip(int mipLevel, Texture* src, int width, int height) {}
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

	virtual void			SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits);
	virtual void			GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits);
	virtual void			GetDepthBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits);

	void					GetImageData(ImageData& imageData);
	void					GetImageData(ImageData& imageData, int destX, int destY);
	void					SetImageData(ImageData& imageData);

	RectF					GetRect();
};

NS_BF_END;
