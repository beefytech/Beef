#include "Texture.h"

#include "util/AllocDebug.h"
#include "img/ImageData.h"

USING_NS_BF;

Texture::Texture()
{
	mRefCount = 0;
}

void Texture::AddRef()
{
	mRefCount++;
}

void Texture::Release()
{
	mRefCount--;
	if (mRefCount == 0)
		delete this;
}

void TextureSegment::InitFromTexture(Texture* texture)
{	
	mTexture = texture;
	mU1 = 0;
	mV1 = 0;
	mU2 = 1.0f;
	mV2 = 1.0f;
	mScaleX = (float) mTexture->mWidth;
	mScaleY = (float) mTexture->mHeight;	
}

void TextureSegment::SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits)
{
	int x1 = (int)(mU1 * mTexture->mWidth + 0.5f);
	int y1 = (int)(mV1 * mTexture->mHeight + 0.5f);
	mTexture->SetBits(destX + x1, destY + y1, destWidth, destHeight, srcPitch, bits);
}

void TextureSegment::GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits)
{
	int x1 = (int)(mU1 * mTexture->mWidth + 0.5f);
	int y1 = (int)(mV1 * mTexture->mHeight + 0.5f);
	mTexture->GetBits(srcX + x1, srcY + y1, srcWidth, srcHeight, destPitch, bits);
}

void TextureSegment::GetImageData(ImageData& imageData)
{
	int x1 = (int)(mU1 * mTexture->mWidth + 0.5f);
	int x2 = (int)(mU2 * mTexture->mWidth + 0.5f);
	int y1 = (int)(mV1 * mTexture->mHeight + 0.5f);
	int y2 = (int)(mV2 * mTexture->mHeight + 0.5f);
	imageData.CreateNew(x2 - x1, y2 - y1);
	mTexture->GetBits(x1, y1, x2 - x1, y2 - y1, x2 - x1, imageData.mBits);
}

void TextureSegment::GetImageData(ImageData& imageData, int destX, int destY)
{	
	int x1 = (int)(mU1 * mTexture->mWidth + 0.5f);
	int x2 = (int)(mU2 * mTexture->mWidth + 0.5f);
	int y1 = (int)(mV1 * mTexture->mHeight + 0.5f);
	int y2 = (int)(mV2 * mTexture->mHeight + 0.5f);
	mTexture->GetBits(x1, y1, x2 - x1, y2 - y1, imageData.mWidth, imageData.mBits + destX + destY * imageData.mWidth);
}

void TextureSegment::SetImageData(ImageData& imageData)
{		
	SetBits(0, 0, imageData.mWidth, imageData.mHeight, imageData.mStride, imageData.mBits);
}

RectF TextureSegment::GetRect()
{
	float x1 = mU1 * mTexture->mWidth;
	float x2 = mU2 * mTexture->mWidth;
	float y1 = mV1 * mTexture->mHeight;
	float y2 = mV2 * mTexture->mHeight;
	return RectF(x1, y1, x2 - x1, y2 - y1);
}
