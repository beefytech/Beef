#pragma once

#include "../Common.h"
#include "../util/String.h"
#include "../util/Dictionary.h"
#include "../util/Array.h"
#include <unordered_map>
#include <vector>

#include "third_party/freetype/include/ft2build.h"
#include FT_FREETYPE_H

NS_BF_BEGIN

class Texture;
class TextureSegment;

class FTFontManager
{
public:
	class Face;

	class FaceSize
	{
	public:
		Face* mFace;
		FT_Size mFTSize;
		int mRefCount;
		float mPointSize;

	public:
		FaceSize()
		{
			mFace = NULL;
			mRefCount = 0;
			mFTSize = NULL;
			mPointSize = -1;
		}
		~FaceSize();
	};

	class Face
	{
	public:
		String mFileName;
		FT_Face mFTFace;		
		Dictionary<float, FaceSize*> mFaceSizes;

	public:
		Face()
		{
			mFTFace = NULL;
		}
		~Face();
	};
	
	class Page
	{
	public:
		Texture* mTexture;
		int mCurX;
		int mCurY;
		int mMaxRowHeight;

	public:
		Page()
		{
			mTexture = NULL;
			mCurX = 0;
			mCurY = 0;
			mMaxRowHeight = 0;
		}

		~Page();
	};

	class Glyph
	{
	public:
		Page* mPage;
		TextureSegment* mTextureSegment;
		int mX;
		int mY;
		int mWidth;
		int mHeight;
		int mXOffset;
		int mYOffset;
		int mXAdvance;

	public:
		Glyph()
		{
			mPage = NULL;
			mTextureSegment = NULL;
		}

		//~Glyph();
	};

public:
	Dictionary<String, Face*> mFaces;
	Array<Page*> mPages;

	uint8 mWhiteTab[256];
	uint8 mBlackTab[256];
	
	void DoClearCache();
public:
	FTFontManager();
	~FTFontManager();
		
	static void ClearCache();
};

class FTFont
{
public:
	int mHeight;
	int mAscent;
	int mDescent;
	int mMaxAdvance;
	FTFontManager::Face* mFace;
	FTFontManager::FaceSize* mFaceSize;

protected:
	void Dispose(bool cacheRetain);

public:
	FTFont();
	~FTFont();	
	
	bool Load(const StringImpl& file, float pointSize);	

	FTFontManager::Glyph* AllocGlyph(int charCode, bool allowDefault);
	int GetKerning(int charA, int charB);

	void Release(bool cacheRetain = false);
};

NS_BF_END
