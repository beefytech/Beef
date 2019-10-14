#pragma once

#include "Common.h"
#include "util/Point.h"
#include "util/CubicFuncSpline.h"
#include "ImageUtils.h"

NS_BF_BEGIN;

class ImageData;
class ImageEffectCtx;

enum MixType
{
	IMAGEMIX_INNER,
	IMAGEMIX_OUTER,
	IMAGEMIX_OVER
};

class ImageCurvePoint
{
public:
	float					mX;
	float					mY;
	CubicFuncSpline*		mSpline;
	bool					mIsSplineOwner;
	bool					mIsCorner;

public:
	ImageCurvePoint();
	~ImageCurvePoint();
};

typedef std::vector<ImageCurvePoint> ImageCurvePointVector;

class ImageCurve
{
public:
	String				mInterpType;
	ImageCurvePointVector	mPoints;			
	bool					mInitialized;

public:		
	void					Init();

	float					GetVal(float x);
	bool					IsDefault();
};

class ImageGradientPoint
{
public:
	float					mX;
	int						mValue;
};

typedef std::vector<ImageGradientPoint> ImageGradientPointVector;

class ImageGradient
{
public:
	String				mInterpType;
	ImageGradientPointVector mPoints;
	CubicUnitFuncSpline		mSpline;
	float					mSmoothness; // 0.0 - 1.0
	int						mXSize;	

public:
	int						GetVal(float x);	
};

///

const int CONTOUR_DATA_SIZE = 4096;
const int GRADIENT_DATA_SIZE = 4096;
class PSDLayerInfo;

class ImageStrokeEffect;
class ImageEffects;

class BaseImageEffect
{
public:
	double					mOpacity;
	int32					mBlendMode;
	uint16*					mContourData;
	uint32*					mGradientData;
	bool					mInitialized;

public:
	BaseImageEffect();
	virtual ~BaseImageEffect();

	virtual void			Init();
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) = 0;
	virtual void			Apply(ImageEffectCtx* ctx);
	virtual int				GetMixType(); // Default:Interior
	virtual int				GetNeededBorderSize();
	virtual bool			NeedsOrigBits(ImageEffects* effects);	
};

class ImageShadowEffect :public BaseImageEffect
{
public:
	uint32					mColor;
	bool					mUseGlobalLight;
	double					mLocalAngle;
	double					mDistance;
	double					mSpread; // Also 'Choke'
	double					mSize;
	ImageCurve				mContour;
	bool					mAntiAliased;
	double					mNoise;

public:
	virtual void			Init();
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData);
	virtual int				GetNeededBorderSize();
};

class ImageDropShadowEffect : public ImageShadowEffect
{
public:
	bool					mLayerKnocksOut;

public:
	virtual int				GetMixType() override;
};

class ImageInnerShadowEffect : public ImageShadowEffect
{
public:
	bool					mLayerKnocksOut;
};

class ImageGlowEffect : public BaseImageEffect
{
public:
	ImageGradient			mColorGradient[4];	
	bool					mHasGradient;

	double					mNoise;	
	
	int32					mTechnique; // 'SfBL' or 'PrBL'
	double					mSize;
	
	ImageCurve				mContour;
	double					mRange;
	double					mJitter;
	bool					mAntiAliased;		

public:
	virtual void			Init() override;
	virtual void			CreateContourAndGradientData();	
	virtual int				GetNeededBorderSize() override;
};

class ImageOuterGlowEffect : public ImageGlowEffect
{
public:	
	double					mSpread;	

public:
	void Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;

	virtual int				GetMixType() override;
};

class ImageInnerGlowEffect : public ImageGlowEffect
{
public:	
	double					mChoke;	
	bool					mIsCenter; // Otherwise 'Edge'

public:
	void					Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
};

class ImageFill
{
public:
	virtual ~ImageFill() {}
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) = 0;
};

class ImageColorFill : public ImageFill
{
public:
	uint32					mColor;

public:
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData);
};

class ImagePatternFill : public ImageFill
{
public:	
	double					mPhaseX;
	double					mPhaseY;
	bool					mLinkWithLayer;	
	double					mScale;	
	String				mPatternName;

public:	
	void					Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData);
};

class ImageGradientFill : public ImageFill
{
public:
	ImageGradient			mColorGradient[4];
	uint32*					mGradientData;
	int						mStyle; // 'Lnr ',
	bool					mReverse;
	bool					mAlignWithLayer;
	double					mOffsetX;
	double					mOffsetY;
	double					mAngle;
	double					mScale;

public:
	ImageGradientFill();
	~ImageGradientFill();

	void					Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData);
};

class ImageBevelEffect : public BaseImageEffect
{
public:
	int32					mStyle; // 'InrB'=Inner Bevel, 'OtrB'=Outer Bevel
	int32					mTechnique; // 'SfBL'=Smooth, 'PrBL'=Chisel Hard, 'Slmt'=Chisel Soft
	double					mDepth;
	bool					mDirectionUp;
	double					mSize;
	double					mSoften;

	double					mLocalAngle;
	bool					mUseGlobalLight;
	double					mLocalAltitude;
	ImageCurve				mGlossContour;	
	bool					mAntiAliased;	
	int32					mHiliteMode;
	uint32					mHiliteColor;
	double					mHiliteOpacity;
	int32					mShadowMode;
	uint32					mShadowColor;
	double					mShadowOpacity;

	bool					mUseContour;
	ImageCurve				mBevelContour;
	int32*					mBevelContourData;
	double					mBevelContourRange;
	
	bool					mUseTexture;
	ImagePatternFill		mTexture;
	double					mTextureDepth;
	bool					mTextureInvert;

	uint16*					mGlossContourData;	

public:
	ImageBevelEffect();
	~ImageBevelEffect();

	void					Init() override;
	void					Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;	
	virtual void			Apply(ImageEffectCtx* ctx) override;
	virtual int				GetMixType() override;
	void					Apply(int pass, int style, PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* hiliteImage, ImageData* shadowImage);	
	virtual int				GetNeededBorderSize() override;
};

typedef std::vector<BaseImageEffect*> ImageEffectVector;


class ImageSatinEffect : public BaseImageEffect
{
public:
	uint32					mColor;
	double					mAngle;
	double					mDistance;
	double					mSize;
	ImageCurve				mContour;
	bool					mAntiAliased;
	bool					mInvert;

public:
	virtual void			Init() override;
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
	virtual int				GetMixType() override; // Default:Interior
	virtual int				GetNeededBorderSize() override;
};

class ImageColorOverlayEffect : public BaseImageEffect
{
public:
	ImageColorFill			mColorFill;

public:
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
};

class ImageGradientOverlayEffect : public BaseImageEffect
{
public:
	ImageGradientFill		mGradientFill;

public:
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
};

class ImagePatternOverlayEffect : public BaseImageEffect
{
public:
	ImagePatternFill		mPattern;

public:
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
};


class ImageStrokeEffect : public BaseImageEffect
{
public:
	double					mSize;
	int						mPosition;
	
	int32					mFillType;	
	ImageGradientFill		mGradientFill;
	ImageColorFill			mColorFill;
	ImagePatternFill		mPatternFill;

public:
	virtual void			Apply(PSDLayerInfo* layerInfo, ImageData* imageData, ImageData* destImageData) override;
	virtual void			Apply(ImageEffectCtx* ctx) override;
	virtual int				GetMixType() override; // Default:Interior
	virtual int				GetNeededBorderSize() override;
	virtual bool			NeedsOrigBits(ImageEffects* effects) override;
};

///

class ImageEffectCtx
{
public:
	int						mBlendX;
	int						mBlendY;
	int						mBlendWidth;
	int						mBlendHeight;
	PSDLayerInfo*			mLayerInfo;
	ImageData*				mLayerImage;
	ImageData*				mInnerImage;
	ImageData*				mOuterImage;
	ImageData*				mOrigImage;
};

class ImageEffects
{
public:
	ImageEffectVector		mImageEffectVector;
	ImageData*				mSwapImages[2];

public:
	ImageData*				GetDestImage(ImageData* usingImage);
	

public:
	ImageEffects();
	~ImageEffects();

	ImageData*				FlattenInto(ImageData* dest, PSDLayerInfo* srcLayer, ImageData* srcImage, ImageData* knockoutBottom, ImageData* insideImage);
	void					AddEffect(BaseImageEffect* effect);
};

NS_BF_END;
