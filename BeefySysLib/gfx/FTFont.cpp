#include "FTFont.h"
#include "gfx/Texture.h"
#include "gfx/RenderDevice.h"
#include "img/ImageData.h"
#include "BFApp.h"

#include "freetype/ftsizes.h"

#include "util/AllocDebug.h"

USING_NS_BF;

const int FT_PAGE_WIDTH = 1024;
const int FT_PAGE_HEIGHT = 1024;

//const int FT_PAGE_WIDTH = 128;
//const int FT_PAGE_HEIGHT = 128;

static FT_Library gFTLibrary = NULL;
static FTFontManager gFTFontManager;

FTFont::FTFont()
{
	mFace = NULL;
	mFaceSize = NULL;
	mHeight = 0;
	mAscent = 0;
	mDescent = 0;
	mMaxAdvance = 0;
}

FTFont::~FTFont()
{
	Dispose(false);
}

void FTFont::Dispose(bool cacheRetain)
{
	if (mFaceSize != NULL)
	{
		BF_ASSERT((mFaceSize->mRefCount > 0) && (mFaceSize->mRefCount < 1000000));
		mFaceSize->mRefCount--;

		if (mFaceSize->mRefCount == 0)
		{
			mFace->mFaceSizes.Remove(mFaceSize->mPointSize);
			delete mFaceSize;

			if (!cacheRetain)
			{
				if (mFace->mFaceSizes.IsEmpty())
				{
					bool removed = gFTFontManager.mFaces.Remove(mFace->mFileName);
					BF_ASSERT(removed);
					delete mFace;
				}
			}
		}
		mFaceSize = NULL;
	}
}

FTFontManager::FaceSize::~FaceSize()
{
	if (mFTSize != 0)
		FT_Done_Size(mFTSize);
}

FTFontManager::Face::~Face()
{
	if (mFTFace != 0)
		FT_Done_Face(mFTFace);
}

FTFontManager::FTFontManager()
{
	for (int i = 0; i < 256; i++)
	{
		uint8 whiteVal = (uint8)(pow(i / 255.0f, 0.75) * 255.0f);
		uint8 blackVal = (uint8)(pow(i / 255.0f, 0.9) * 255.0f);

		mWhiteTab[i] = whiteVal;
		mBlackTab[i] = blackVal;
	}
}

FTFontManager::~FTFontManager()
{
	for (auto page : mPages)
		delete page;
	mPages.Clear();
	for (auto faceKV : mFaces)
		delete faceKV.mValue;
	mFaces.Clear();
}

void FTFontManager::DoClearCache()
{
	for (auto page : mPages)
		delete page;
	mPages.Clear();

	for (auto itr = mFaces.begin(); itr != mFaces.end(); )
	{
		auto face = itr->mValue;
		if (face->mFaceSizes.IsEmpty())
		{
			delete face;
			itr = mFaces.Remove(itr);
		}
		else
			++itr;
	}
}

void FTFontManager::ClearCache()
{
	gFTFontManager.DoClearCache();
}

FTFontManager::Page::~Page()
{
	mTexture->Release();
	//delete mTexture;
}

/*FTFontManager::Glyph::~Glyph()
{
	//delete mTextureSegment;
}*/

bool FTFont::Load(const StringImpl& fileName, float pointSize)
{
	if (gFTLibrary == NULL)
		FT_Init_FreeType(&gFTLibrary);

	FTFontManager::Face* face = NULL;

	FTFontManager::Face** facePtr = NULL;
	if (gFTFontManager.mFaces.TryAdd(fileName, NULL, &facePtr))
	{
		face = new FTFontManager::Face();
		*facePtr = face;

		face->mFileName = fileName;
		FT_Face ftFace = NULL;

		String useFileName = fileName;
		int faceIdx = 0;
		int atPos = (int)useFileName.IndexOf('@');
		if (atPos != -1)
		{
			faceIdx = atoi(useFileName.c_str() + atPos + 1);
			useFileName.RemoveToEnd(atPos);
		}
		auto error = FT_New_Face(gFTLibrary, useFileName.c_str(), faceIdx, &ftFace);
		face->mFTFace = ftFace;
	}
	else
	{
		face = *facePtr;
	}
	if (face->mFTFace == NULL)
		return false;

	mFace = face;

	FTFontManager::FaceSize** faceSizePtr = NULL;
	if (face->mFaceSizes.TryAdd(pointSize, NULL, &faceSizePtr))
	{
		//OutputDebugStrF("Created face %s %f\n", fileName.c_str(), pointSize);

		mFaceSize = new FTFontManager::FaceSize();
		*faceSizePtr = mFaceSize;

		FT_Size ftSize = NULL;
		FT_New_Size(mFace->mFTFace, &ftSize);
		FT_Activate_Size(ftSize);
		auto error = FT_Set_Char_Size(mFace->mFTFace, 0, (int)(pointSize * 64), 72, 72);

		mFaceSize->mFace = mFace;
		mFaceSize->mFTSize = ftSize;
		mFaceSize->mPointSize = pointSize;
	}
	else
	{
		mFaceSize = *faceSizePtr;
	}
	mFaceSize->mRefCount++;

	mHeight = mFaceSize->mFTSize->metrics.height / 64;
	mAscent = mFaceSize->mFTSize->metrics.ascender / 64;
	mDescent = mFaceSize->mFTSize->metrics.descender / 64;
	mMaxAdvance = mFaceSize->mFTSize->metrics.max_advance / 64;

	return true;
}

TextureSegment* BF_CALLTYPE Gfx_CreateTextureSegment(TextureSegment* textureSegment, int srcX, int srcY, int srcWidth, int srcHeight);

FTFontManager::Glyph* FTFont::AllocGlyph(int charCode, bool allowDefault)
{
	FT_Activate_Size(mFaceSize->mFTSize);

	int glyph_index = FT_Get_Char_Index(mFace->mFTFace, charCode);
	if ((glyph_index == 0) && (!allowDefault))
		return NULL;

	auto error = FT_Load_Glyph(mFace->mFTFace, glyph_index, FT_LOAD_NO_BITMAP);
	if (error != FT_Err_Ok)
		return NULL;

	error = FT_Render_Glyph(mFace->mFTFace->glyph, FT_RENDER_MODE_NORMAL);
	if (error != FT_Err_Ok)
		return NULL;

	auto& bitmap = mFace->mFTFace->glyph->bitmap;

	if ((bitmap.rows > FT_PAGE_HEIGHT) || (bitmap.width > FT_PAGE_WIDTH))
	{
		return NULL;
	}

	FTFontManager::Page* page = NULL;

	if (!gFTFontManager.mPages.empty())
	{
		page = gFTFontManager.mPages.back();
		if (page->mCurX + (int)bitmap.width > page->mTexture->mWidth)
		{
			// Move down to next row
			page->mCurX = 0;
			page->mCurY += page->mMaxRowHeight;
			page->mMaxRowHeight = 0;
		}

		if (page->mCurY + (int)bitmap.rows > page->mTexture->mHeight)
		{
			// Doesn't fit
			page = NULL;
		}
	}

	if (page == NULL)
	{
		page = new FTFontManager::Page();
		gFTFontManager.mPages.push_back(page);
	}

	//auto glyph = new FTFontManager::Glyph();

	static FTFontManager::Glyph staticGlyph;
	auto glyph = &staticGlyph;

	auto ftFace = mFace->mFTFace;

	glyph->mXAdvance = ftFace->glyph->advance.x / 64;
	glyph->mXOffset = ftFace->glyph->bitmap_left;
	glyph->mYOffset = ftFace->size->metrics.ascender / 64 - ftFace->glyph->bitmap_top;
	glyph->mPage = page;
	glyph->mX = page->mCurX;
	glyph->mY = page->mCurY;
	glyph->mWidth = bitmap.width;
	glyph->mHeight = bitmap.rows;

	if (page->mTexture == NULL)
	{
		ImageData* img = new ImageData();
		img->CreateNew(FT_PAGE_WIDTH, FT_PAGE_HEIGHT);
		auto* bits = img->mBits;
		for (int i = 0; i < FT_PAGE_HEIGHT * FT_PAGE_WIDTH; i++)
		{
			if (i % 3 == 0)
				bits[i] = 0xFFFF0000;
			else if (i % 3 == 1)
				bits[i] = 0xFF00FF00;
			else
				bits[i] = 0xFF0000FF;
		}
		page->mTexture = gBFApp->mRenderDevice->LoadTexture(img, TextureFlag_NoPremult);
		img->Deref();
	}

	if (bitmap.width > 0)
	{
		ImageData* img = new ImageData();
		img->CreateNew(bitmap.width, bitmap.rows);
		auto* bits = img->mBits;
		int width = img->mWidth;
		for (int y = 0; y < (int)bitmap.rows; y++)
		{
			for (int x = 0; x < (int)bitmap.width; x++)
			{
				uint8 val = bitmap.buffer[y * bitmap.pitch + x];

				uint8 whiteVal = gFTFontManager.mWhiteTab[val];
				uint8 blackVal = gFTFontManager.mBlackTab[val];

				bits[y * width + x] = ((int32)whiteVal << 24) |
					((int32)blackVal) | ((int32)0xFF << 8) | ((int32)0xFF << 16);
			}
		}
		page->mTexture->Blt(img, page->mCurX, page->mCurY);
		img->Deref();
	}

	page->mCurX += bitmap.width;
	page->mMaxRowHeight = std::max(page->mMaxRowHeight, (int)bitmap.rows);

	auto texture = page->mTexture;
	texture->AddRef();

	TextureSegment* textureSegment = new TextureSegment();
	textureSegment->mTexture = texture;
	textureSegment->mU1 = (glyph->mX / (float)texture->mWidth);
	textureSegment->mV1 = (glyph->mY / (float)texture->mHeight);
	textureSegment->mU2 = ((glyph->mX + glyph->mWidth) / (float)texture->mWidth);
	textureSegment->mV2 = ((glyph->mY + glyph->mHeight) / (float)texture->mHeight);
	textureSegment->mScaleX = (float)abs(glyph->mWidth);
	textureSegment->mScaleY = (float)abs(glyph->mHeight);

	glyph->mTextureSegment = textureSegment;
	return glyph;
}

int FTFont::GetKerning(int charA, int charB)
{
	FT_Activate_Size(mFaceSize->mFTSize);
	FT_Vector kerning;
	int glyph_indexA = FT_Get_Char_Index(mFace->mFTFace, charA);
	int glyph_indexB = FT_Get_Char_Index(mFace->mFTFace, charB);
	FT_Get_Kerning(mFace->mFTFace, glyph_indexA, glyph_indexB, FT_KERNING_DEFAULT, &kerning);
	return kerning.x / 64;
}

void FTFont::Release(bool cacheRetain)
{
	Dispose(cacheRetain);
	delete this;
}


//////////////////////////////////////////////////////////////////////////

BF_EXPORT FTFont* BF_CALLTYPE FTFont_Load(const char* fileName, float pointSize)
{
	auto ftFont = new FTFont();
	if (!ftFont->Load(fileName, pointSize))
	{
		delete ftFont;
		return NULL;
	}

	return ftFont;
}

BF_EXPORT void BF_CALLTYPE FTFont_ClearCache()
{
	FTFontManager::ClearCache();
}

BF_EXPORT void BF_CALLTYPE FTFont_Delete(FTFont* ftFont, bool cacheRetain)
{
	ftFont->Release(cacheRetain);
}

BF_EXPORT FTFontManager::Glyph* BF_CALLTYPE FTFont_AllocGlyph(FTFont* ftFont, int charCode, bool allowDefault)
{
	return ftFont->AllocGlyph(charCode, allowDefault);
}

BF_EXPORT int BF_CALLTYPE FTFont_GetKerning(FTFont* ftFont, int charCodeA, int charCodeB)
{
	auto kerning = ftFont->GetKerning(charCodeA, charCodeB);
	return kerning;
}
