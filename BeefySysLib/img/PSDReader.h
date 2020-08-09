#pragma once

#include "../Common.h"
#include "ImageData.h"
#include <vector>
#include <map>

NS_BF_BEGIN;

class FileStream;
class ImageData;
class ImageAdjustment;

class PSDChannelInfo 
{
public:
	int						mId;
	int						mLength;
};

typedef std::vector<PSDChannelInfo> PSDChannelInfoVector;

class PSDReader;
class ImageEffects;

enum
{
	PSDVal_None, // 0
	PSDVal_Descriptor, // 1
	PSDVal_KeyedString, // 2
	PSDVal_String, // 3
	PSDVal_Double, // 4
	PSDVal_Integer, // 5
	PSDVal_UnitFloat, // 6
	PSDVal_List // 7
};

class PSDDescriptor;

class PSDPathPoint
{
public:
	double					mCtlEnterX;
	double					mCtlEnterY;
	double					mAnchorX;
	double					mAnchorY;	
	double					mCtlLeaveX;
	double					mCtlLeaveY;
};

typedef std::vector<PSDPathPoint> PSDPathPointVector;

class PSDPath
{
public:
	bool					mClosed;
	PSDPathPointVector		mPoints;
};

class PSDValue
{
public:
	int						mType;
	String				mKey;
	String				mString;
	double					mDouble;
	int						mInteger;
	PSDDescriptor*			mDescriptor;
	PSDValue*				mList;

public:
	PSDValue();
	~PSDValue();		

	int32					GetMulticharInt();
};

typedef std::map<String, PSDValue> PSDValueMap;

class PSDDescriptor
{
public:
	PSDValueMap				mPSDValueMap;

public:
	bool					IsEmpty();
	bool					Contains(const StringImpl& value);
	PSDValue*				Get(const StringImpl& value);
	PSDDescriptor*			GetDescriptor(const StringImpl& value);
};

enum
{
	PSDDIVIDER_NONE,
	PSDDIVIDER_OPEN_FOLDER,
	PSDDIVIDER_CLOSED_FOLDER,
	PSDDIVIDER_SECTION_END
};

class PSDLayerInfo : public ImageData
{
public:
	PSDReader*				mPSDReader;	
	PSDLayerInfo*			mParent;
	String					mName;
	int						mIdx;
	uint32					mLayerId;
	int						mSectionDividerType;
	int32					mSectionBlendMode;	
	uint8					mOpacity;	
	uint8					mFillOpacity;
	int32					mBlendMode;
	bool					mBaseClipping;
	bool					mVisible;	
	bool					mLayerMaskEnabled;	
	bool					mLayerMaskInverted;
	uint8					mLayerMaskDefault;
	uint8*					mLayerMask;	
	int						mLayerMaskX;
	int						mLayerMaskY;
	int						mLayerMaskWidth;
	int						mLayerMaskHeight;	
	ImageEffects*			mImageEffects;
	PSDDescriptor*			mPSDDescriptor;
	PSDPath*				mVectorMask;
	double					mRefX;
	double					mRefY;
	int						mKnockout; // 1 = Shallow, 2 = Deep
	bool					mBlendInteriorEffectsAsGroup;
	bool					mBlendClippedElementsAsGroup;
	bool					mTransparencyShapesLayer;
	bool					mLayerMaskHidesEffects;
	bool					mVectorMaskHidesEffects;	
	uint32					mBlendingRangeSourceStart;
	uint32					mBlendingRangeSourceEnd;
	uint32					mBlendingRangeDestStart;
	uint32					mBlendingRangeDestEnd;
	ImageAdjustment*		mImageAdjustment;

	int						mImageDataStart;
	PSDChannelInfoVector	mChannels;
	int						mChannelMask;

public:
	PSDLayerInfo();
	~PSDLayerInfo();

	virtual	bool			ReadData();
	void					ApplyVectorMask(ImageData* imageData);
	void					ApplyMask(ImageData* imageData);
};

typedef std::vector<PSDLayerInfo*> PSDLayerInfoVector;

class PSDPattern : public ImageData
{
public:
	int						mTop;
	int						mLeft;
	int						mBottom;
	int						mRight;	
	uint8*					mIntensityBits;
	PSDPattern*				mNextMipLevel;

public:
	PSDPattern();
	~PSDPattern();

	PSDPattern*				GetNextMipLevel();
};

typedef std::map<String, PSDPattern*> PSDPatternMap;

class Texture;
class ImageGradient;
class ImageCurve;
class ImageGradientFill;
class ImagePatternFill;

class PSDReader
{
public:
	FileStream*				mFS;

	int						mVersion;
	int						mWidth;
	int						mHeight;
	int						mBitDepthPerChannel;	
	int						mMode;
	int						mGlobalAngle;
	int						mGlobalAltitude;

	int						mChannels;	
	int						mImageDataSectStart;

	PSDLayerInfoVector		mPSDLayerInfoVector;
	PSDPatternMap			mPSDPatternMap;

public:
	String					ReadIdString();
	void					ReadPSDValue(PSDValue* value);
	void					ReadPSDDescriptor(PSDDescriptor* descriptor);
	void					ReadEffectColor(PSDDescriptor* colorDesc, uint32* color);
	bool					ReadEffectGradient(PSDDescriptor* descriptor, ImageGradient* colorGradient);
	void					ReadEffectContour(PSDDescriptor* descriptor, ImageCurve* curve);
	int32					ReadBlendMode(PSDValue* value);
	
	void					ReadExtraInfo(int endPos);
	void					ReadEffectSection(ImageEffects* imageEffects, PSDDescriptor* desc);
	void					ReadGradientFill(PSDDescriptor* descriptor, ImageGradientFill* gradientFill);
	void					ReadPatternFill(PSDDescriptor* descriptor, ImagePatternFill* patternFill);

public:
	PSDReader();
	~PSDReader();

	bool					Init(const StringImpl& fileName);

	ImageData*				ReadImageData();
	Texture*				LoadLayerTexture(int layerIdx, int* ofsX, int* ofsY); // -1 = composited image
	ImageData*				MergeLayers(PSDLayerInfo* group, const std::vector<int>& layerIndices, ImageData* bottomImage);	
	Texture*				LoadMergedLayerTexture(const std::vector<int>& layerIndices, int* ofsX, int* ofsY);

};

NS_BF_END;
