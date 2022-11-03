#pragma once

#include "../Common.h"

NS_BF_BEGIN;

enum
{
	HWBITS_UNKNOWN,
	HWBITS_PVRTC_2BPPV1,
	HWBITS_PVRTC_4BPPV1,
	HWBITS_PVRTC_2X4BPPV1
};

class ImageData
{
public:
	int						mRefCount;
	int						mX;
	int						mY;
	int						mWidth;
	int						mHeight;
	void*					mHWBits;
	int						mHWBitsLength;
	int						mHWBitsType;
	uint32*					mBits;
	uint8*					mSrcData;
	int						mSrcDataLen;
	bool					mKeepSrcDataValid;
	bool					mOwnsSrcData;

	bool					mWantsAlphaPremultiplied;
	bool					mAlphaPremultiplied;
	bool					mIsAdditive;

public:
	ImageData();
	virtual ~ImageData();

	void					AddRef();
	void					Deref();
	void					SwapRAndB();
	void					CreateNew(int x, int y, int width, int height, bool clear = true);
	void					CreateNew(int width, int height, bool clear = true);
	void					CopyFrom(ImageData* img, int x, int y);
	void					Fill(uint32 color);
	virtual ImageData*		Duplicate();
	void					SetSrcData(uint8* data, int dataLen);
	virtual bool			LoadFromMemory(void* ptr, int size);
	virtual bool			LoadFromFile(const StringImpl& path);
	virtual	bool			ReadData() { return false; }
	virtual void			PremultiplyAlpha();
	virtual void			UnPremultiplyAlpha();
};

NS_BF_END;
