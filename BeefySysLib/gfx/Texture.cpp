#include "Texture.h"

#include "util/AllocDebug.h"

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
