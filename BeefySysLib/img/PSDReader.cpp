#include "PSDReader.h"
#include "FileStream.h"
#include "BFApp.h"
#include "img/ImageData.h"
#include "gfx/Texture.h"
#include "gfx/RenderDevice.h"
#include "ImgEffects.h"
#include "ImageUtils.h"
#include "ImageAdjustments.h"
#include "util/PerfTimer.h"
#include <set>

/*
#include "agg_rendering_buffer.h"
#include "agg_trans_viewport.h"
#include "agg_path_storage.h"
#include "agg_conv_transform.h"
#include "agg_conv_curve.h"
#include "agg_conv_stroke.h"
#include "agg_gsv_text.h"
#include "agg_scanline_u.h"
#include "agg_scanline_bin.h"
#include "agg_renderer_scanline.h"
#include "agg_rasterizer_scanline_aa.h"
#include "agg_rasterizer_compound_aa.h"
#include "agg_span_allocator.h"
#include "agg_gamma_lut.h"
#include "agg_pixfmt_rgba.h"
#include "agg_pixfmt_gray.h"
#include "agg_bounding_rect.h"
#include "agg_color_gray.h"
#include "agg_ellipse.h"
#include "agg_scanline_u.h"
#include "agg_scanline_p.h"
 */

USING_NS_BF;

 PSDValue::PSDValue()
 {
	 mType = PSDVal_None;
	 mDescriptor = NULL;
	 mList = NULL;
	 mInteger = 0;
	 mDouble = 0;
 }

PSDValue::~PSDValue()
{
	if (mType == PSDVal_Descriptor)
		delete mDescriptor;
	else if (mType == PSDVal_List)
		delete [] mList;
}

int32 PSDValue::GetMulticharInt()
{
	if (mString.length() < 4)
		return 0;
	char chars [] = {(char) mString[3], (char) mString[2], (char) mString[1], (char) mString[0]};
	return *((int*) chars);
}

bool PSDDescriptor::IsEmpty()
{
	return mPSDValueMap.empty();
}

bool PSDDescriptor::Contains(const StringImpl& value)
{
	return mPSDValueMap.find(value) != mPSDValueMap.end();
}

PSDValue* PSDDescriptor::Get(const StringImpl& value)
{
	static PSDValue defaultValue;
	if (mPSDValueMap.find(value) == mPSDValueMap.end())
		return &defaultValue;
	return &mPSDValueMap[value];
}

PSDDescriptor* PSDDescriptor::GetDescriptor(const StringImpl& value)
{
	static PSDDescriptor defaultDescriptor;
	if (mPSDValueMap.find(value) == mPSDValueMap.end())
		return &defaultDescriptor;
	PSDValue* aValue = &mPSDValueMap[value];
	if (aValue == NULL)
		return &defaultDescriptor;
	return aValue->mDescriptor;
}

///

PSDPattern::PSDPattern()
{
	mIntensityBits = NULL;
	mNextMipLevel = NULL;
}

PSDPattern::~PSDPattern()
{
	delete mIntensityBits;
	delete mNextMipLevel;
}

PSDPattern* PSDPattern::GetNextMipLevel()
{
	PSDPattern* mip = new PSDPattern();

	int mw = (mWidth + 1) / 2;
	int mh = (mHeight + 1) / 2;

	mip->mWidth = mw;
	mip->mHeight = mh;
	mip->mBits = new uint32[mw*mh];
	mip->mIntensityBits = new uint8[mw*mh];

	for (int y = 0; y < mh; y++)
	{		
		uint8* destI = mip->mIntensityBits + y*mw;

		uint8* srcI1 = mIntensityBits + y*2*mWidth;
		uint8* srcI2 = (y + 1 >= mh) ? mIntensityBits : (srcI1 + mWidth);

		for (int x = 0; x < mw; x++)
		{
			int srcX1 = x*2;
			int srcX2 = (srcX1+1)%mWidth;			

			*(destI++) = (uint8) (((int) srcI1[srcX1] + (int) srcI1[srcX2] + 
				(int) srcI2[srcX1] + (int) (srcI2[srcX2])) / 4);			
		}
	}

	if (mBits != NULL)
	{
		for (int y = 0; y < mh; y++)
		{
			uint32* destBits = mip->mBits + y*mw;

			uint32* srcBits1 = mBits + y*2*mWidth;
			uint32* srcBits2 = (y + 1 >= mh) ? mBits : (srcBits1 + mWidth);

			for (int x = 0; x < mw; x++)
			{
				int srcX1 = x*2;
				int srcX2 = (srcX1+1)%mWidth;			

				*(destBits++) = 
					(((
					 (srcBits1[srcX1] & 0x00FF00FF) + 
					 (srcBits1[srcX2] & 0x00FF00FF) +
					 (srcBits2[srcX1] & 0x00FF00FF) +
					 (srcBits2[srcX2] & 0x00FF00FF)) / 4) & 0x00FF00FF) |
					(((
					 ((srcBits1[srcX1] >> 8) & 0x00FF00FF) + 
					 ((srcBits1[srcX2] >> 8) & 0x00FF00FF) +
					 ((srcBits2[srcX1] >> 8) & 0x00FF00FF) +
					 ((srcBits2[srcX2] >> 8) & 0x00FF00FF)) / 4) & 0x00FF00FF) << 8;
			}
		}
	}

	return mip;
}

///

PSDReader::PSDReader()
{
	mFS = NULL;
	mGlobalAltitude = 0;
	mGlobalAngle = 30;
}

PSDReader::~PSDReader()
{
	if (mFS != NULL)
		delete mFS;
	for (int i = 0; i < (int) mPSDLayerInfoVector.size(); i++)
		delete mPSDLayerInfoVector[i];
	PSDPatternMap::iterator itr = mPSDPatternMap.begin();
	while (itr != mPSDPatternMap.end())
	{
		delete itr->second;
		++itr;
	}
}

String PSDReader::ReadIdString()
{
	String idStr = mFS->ReadAscii32SizedString();
	int id = 0;
	if (idStr.length() == 0)
	{
		id = mFS->ReadInt32();
		char str[5] = {
            (char)((id & 0xFF000000) >> 24),
            (char)((id & 0x00FF0000) >> 16),
            (char)((id & 0x0000FF00) >> 8),
            (char)(id & 0x000000FF), 0};
		idStr = str;
	}
	return idStr;
}

void PSDReader::ReadPSDValue(PSDValue* value)
{
	int aType = mFS->ReadInt32();
	switch (aType)
	{
	case 'obj ':
		{
			int32 numItems = mFS->ReadInt32();
			for (int itemIdx = 0; itemIdx < numItems; itemIdx++)
			{
				int refType = mFS->ReadInt32();
				switch (refType)
				{
				case 'prop':
					{
						String aName = mFS->ReadUnicode32SizedString();
						String classId = ReadIdString();
						String keyId = ReadIdString();
					}
					break;
				case 'Clss':
					{
						String aName = mFS->ReadUnicode32SizedString();
						String classId = ReadIdString();
					}
					break;
				case 'Enmr':
					{
						String aName = mFS->ReadUnicode32SizedString();
						String classId = ReadIdString();
						String typeId = ReadIdString();
						String anEnum = ReadIdString();
					}
					break;
				case 'rele':
					{
						String aName = mFS->ReadUnicode32SizedString();
						String classId = ReadIdString();
						int32 offset = mFS->ReadInt32();
					}
					break;
				case 'Idnt':
					{
						int32 ident = mFS->ReadInt32();
					}
					break;
				case 'indx':
					{
						int32 ident = mFS->ReadInt32();
					}
					break;
				case 'name':
					{
						String aName = mFS->ReadAscii8SizedString();
					}
					break;
				}
			}
		}
		break;
	case 'GlbO':
	case 'Objc':
		{
			value->mType = PSDVal_Descriptor;
			value->mDescriptor = new PSDDescriptor();
			ReadPSDDescriptor(value->mDescriptor);				
		}
		break;
	case 'TEXT':		
		value->mType = PSDVal_String;			
		value->mString = mFS->ReadUnicode32SizedString();
		break;
	case 'VlLs':
		{
			value->mType = PSDVal_List;
			int count = mFS->ReadInt32();
			value->mInteger = count;
			value->mList = new PSDValue[count];
			for (int idx = 0; idx < count; idx++)
				ReadPSDValue(value->mList + idx);
		}
		break;
	case 'doub':
		value->mType = PSDVal_Double;
		value->mDouble = mFS->ReadDouble();
		break;
	case 'UntF':
		value->mType = PSDVal_UnitFloat;
		value->mInteger = mFS->ReadInt32();
		value->mDouble = mFS->ReadDouble();
		break;
	case 'enum':
		value->mType = PSDVal_KeyedString;
		value->mKey = ReadIdString();
		value->mString = ReadIdString();
		break;
	case 'long':
		value->mType = PSDVal_Integer;
		value->mInteger = mFS->ReadInt32();
		break;
	case 'bool':
		value->mType = PSDVal_Integer;
		value->mInteger = mFS->ReadInt8();
		break;
	case 'type':
	case 'GlbC':
		value->mType = PSDVal_String;
		value->mString = ReadIdString();
		break;
	case 'alis':
		{
			int aSize = mFS->ReadInt32();
			mFS->Seek(aSize);
		}
		break;
	case 'tdta':
		{
			int aSize = mFS->ReadInt32();
			mFS->Seek(aSize);
		}
		break;
	}		
}

void PSDReader::ReadPSDDescriptor(PSDDescriptor* descriptor)
{
	AutoPerf gPerf("PSDReader::ReadPSDDescriptor");

	PSDDescriptor* aDescriptor = descriptor;

	String textString;

	int pos = mFS->GetPos();

	String aName = mFS->ReadUnicode32SizedString();

	String classIdStr = mFS->ReadAscii32SizedString();
	int classIdType = 0;
	if (classIdStr.length() == 0)
		classIdType = mFS->ReadInt32();

	String aString;							
	int numItems = mFS->ReadInt32();							

	for (int itemIdx = 0; itemIdx < numItems; itemIdx++)
	{
		String keyStr = ReadIdString();				
		PSDValue* aValue = NULL;
		{
			//AutoPerf gPerf("PSDReader::ReadPSDDescriptor - insert");
			aValue = &(aDescriptor->mPSDValueMap.insert(PSDValueMap::value_type(keyStr, PSDValue())).first->second);
		}
		ReadPSDValue(aValue);
	}
}

void PSDReader::ReadEffectColor(PSDDescriptor* colorDesc, uint32* color)
{
	int vals[3];
	vals[0] = (int) colorDesc->Get("Rd  ")->mDouble;
	vals[1] = (int) colorDesc->Get("Grn ")->mDouble;
	vals[2] = (int) colorDesc->Get("Bl  ")->mDouble;
	*color = vals[0] | (vals[1] << 8) | (vals[2] << 16) | 0xFF000000;
}

bool PSDReader::ReadEffectGradient(PSDDescriptor* descriptor, ImageGradient* colorGradient)
{
	if (descriptor->Contains("Grad"))
	{
		PSDDescriptor* gradDesc = descriptor->GetDescriptor("Grad");
		int gridF = gradDesc->Get("GrdF")->GetMulticharInt(); // CstS
		double intr = gradDesc->Get("Intr")->mDouble;
		int trns = gradDesc->Get("Trns")->mInteger;
									
		PSDValue* colors = gradDesc->Get("Clrs");
		for (int colorIdx = 0; colorIdx < colors->mInteger; colorIdx++)
		{
			PSDDescriptor* colorDataDesc = colors->mList[colorIdx].mDescriptor;
																				
			PSDDescriptor* aColorDesc = colorDataDesc->GetDescriptor("Clr ");
			int vals[3];
			vals[0] = (int) aColorDesc->Get("Rd  ")->mDouble;
			vals[1] = (int) aColorDesc->Get("Grn ")->mDouble;
			vals[2] = (int) aColorDesc->Get("Bl  ")->mDouble;
										
			int loc = colorDataDesc->Get("Lctn")->mInteger;
			int mdPn = colorDataDesc->Get("Mdpn")->mInteger;
			int aType = colorDataDesc->Get("Type")->GetMulticharInt();

			for (int i = 0; i < 3; i++)
			{
				ImageGradientPoint pt;
				pt.mX = (float) loc;
				pt.mValue = vals[i];

				if ((colorGradient[i].mPoints.size() != 0) && (colorGradient[i].mPoints.back().mX == pt.mX))
					colorGradient[i].mPoints.back() = pt;
				else
					colorGradient[i].mPoints.push_back(pt);
			}
		}
		for (int i = 0; i < 4; i++)
		{
			colorGradient[i].mXSize = 4096;
			colorGradient[i].mSmoothness = (float) intr / 4096.0f;
		}
									
		PSDValue* transDesc = gradDesc->Get("Trns");
		for (int transIdx = 0; transIdx < transDesc->mInteger; transIdx++)
		{
			ImageGradientPoint pt;

			PSDDescriptor* transDataDesc = transDesc->mList[transIdx].mDescriptor;

			int loc = transDataDesc->Get("Lctn")->mInteger;
			double opacity = transDataDesc->Get("Opct")->mDouble;

			pt.mX = (float) loc;
			pt.mValue = (int) (opacity * 255 / 100.0f + 0.5f);
			colorGradient[3].mPoints.push_back(pt);
		}									

		return true;
	}
	else
	{									
		PSDDescriptor* aColorDesc = descriptor->GetDescriptor("Clr ");
		int vals[4];
		vals[0] = (int) aColorDesc->Get("Rd  ")->mDouble;
		vals[1] = (int) aColorDesc->Get("Grn ")->mDouble;
		vals[2] = (int) aColorDesc->Get("Bl  ")->mDouble;
		vals[3] = 255;									

		for (int i = 0; i < 4; i++)
		{
			ImageGradientPoint pt;
			pt.mX = 0;
			pt.mValue = vals[i];
			colorGradient[i].mPoints.push_back(pt);
			colorGradient[i].mXSize = 4096;
			colorGradient[i].mSmoothness = 0;
		}

		// Alpha out
		ImageGradientPoint pt;
		pt.mX = 4096;
		pt.mValue = 0;
		colorGradient[3].mPoints.push_back(pt);

		return false;
	}
}

void PSDReader::ReadEffectContour(PSDDescriptor* descriptor, ImageCurve* curve)
{
	PSDDescriptor* transDesc = descriptor;
	PSDValue* curvePts = transDesc->Get("Crv ");
	curve->mInterpType = transDesc->Get("Nm  ")->mString;
	if (curvePts->mList != NULL)
	{
		for (int ptIdx = 0; ptIdx < curvePts->mInteger; ptIdx++)
		{
			ImageCurvePoint pt;
			PSDDescriptor* ptDesc = curvePts->mList[ptIdx].mDescriptor;
			if (ptDesc != NULL)
			{
				pt.mX = (float) ptDesc->Get("Hrzn")->mDouble;
				pt.mY = (float) ptDesc->Get("Vrtc")->mDouble;
				pt.mIsCorner = (ptDesc->Contains("Cnty")) && (ptDesc->Get("Cnty")->mInteger == 0);
				curve->mPoints.push_back(pt);
			}
		}

		curve->Init();
	}
}

int32 PSDReader::ReadBlendMode(PSDValue* value)
{
	if (value->mString.length() > 4)
	{
		if (value->mString == "linearBurn")
			return 'lnBn';
		if (value->mString == "linearDodge")
			return 'lnDg';
		if (value->mString == "darkerColor")
			return 'dkCl';
		if (value->mString == "lighterColor")
			return 'ltCl';
		if (value->mString == "vividLight")
			return 'vivL';
		if (value->mString == "linearLight")
			return 'linL';
		if (value->mString == "pinLight")
			return 'pinL';
		if (value->mString == "hardMix")
			return 'hdMx';
		if (value->mString == "blendSubtraction")
			return 'subt';
		if (value->mString == "blendDivide")
			return 'divi';
		BF_ASSERT("Unknown blend mode" == 0);
	}
	
	return value->GetMulticharInt();
}

void PSDReader::ReadExtraInfo(int endPos)
{	
	while (mFS->GetPos() < endPos)
	{	
		int addlSignature = mFS->ReadInt32();
		BF_ASSERT(addlSignature == '8BIM');
		int addlType = mFS->ReadInt32();
		int addlSize = mFS->ReadInt32();

		int addlStart = mFS->GetPos();

		if (addlSize == 0)
			continue;
			
		if ((addlType == 'Patt') || (addlType == 'Pat2') || (addlType == 'Pat3'))		
		{
			while (true)
			{
				int pos = mFS->GetPos();

				PSDPattern* pattern = new PSDPattern();

				int aLength = mFS->ReadInt32();
				int patternStartPos = mFS->GetPos();
				
				int version = mFS->ReadInt32();
				BF_ASSERT(version == 1);
				int aMode = mFS->ReadInt32();
				int vert = mFS->ReadInt16();
				int horz = mFS->ReadInt16();
				String aName = mFS->ReadUnicode32SizedString();
				String id = mFS->ReadAscii8SizedString();

				int patVersion = mFS->ReadInt32();
				int patLength = mFS->ReadInt32();

				pos = mFS->GetPos();
				int patStart = mFS->GetPos();

				pattern->mTop = mFS->ReadInt32();
				pattern->mLeft = mFS->ReadInt32();
				pattern->mBottom = mFS->ReadInt32();
				pattern->mRight = mFS->ReadInt32();
				int maxChannels = mFS->ReadInt32();

				int aSize = horz*vert;
				pattern->mBits = new uint32[aSize];					
				pattern->mIntensityBits = new uint8[aSize];
				pattern->mWidth = horz;
				pattern->mHeight = vert;

				for (int i = 0; i < aSize; i++)
					pattern->mBits[i] = 0x00000000;

				int16* rowLengths = NULL;

				int usedChannels = 0;

				for (int channel = 0; channel < maxChannels + 2; channel++)
				{
					int shift = channel * 8;
					if (channel == maxChannels + 1)
						shift = 24;

					bool isWritten = mFS->ReadInt32() != 0;
					if (isWritten)
					{
						usedChannels++;
						int channelLen = mFS->ReadInt32();
						int channelStart = mFS->GetPos();
						int pixelDepth = mFS->ReadInt32();
					
						int compression = mFS->ReadInt32();
						int val = mFS->ReadInt32();
						int width = mFS->ReadInt32();
						int height = mFS->ReadInt32();					
						int bits = mFS->ReadInt16();
						compression = mFS->ReadInt8();
					
						if (compression == 0)
						{
							for (int pos = 0; pos < aSize; pos++)
								pattern->mBits[pos] |= ((uint32) (uint8) mFS->ReadInt8()) << shift;
						}
						else
						{
							if (rowLengths == NULL)
								rowLengths = new short[vert];

							for (int aY = 0; aY < vert; aY++)
								rowLengths[aY] = mFS->ReadInt16();

							for (int aY = 0; aY < vert; aY++)
							{
								int pos = aY * horz;
								int readSize = rowLengths[aY];
			
								while (readSize > 0)
								{
									int chunkSize = mFS->ReadInt8();
									readSize--;
									if (chunkSize >= 0)
									{
										// String of literal data
										chunkSize++;
										readSize -= chunkSize;
										while (chunkSize > 0)
										{						
											pattern->mBits[pos++] |= ((uint32) (uint8) mFS->ReadInt8()) << shift;							
											chunkSize--;
										}						
									}
									else if (chunkSize > -128)
									{
										// One byte repeated
										chunkSize = 1 - chunkSize;
										uint32 aData = ((uint32) (uint8) mFS->ReadInt8()) << shift;
										readSize--;
										while (chunkSize > 0)
										{
											pattern->mBits[pos++] |= aData;							
											chunkSize--;
										}						
									}
								}
							}
						}

						mFS->SetPos(channelStart + channelLen);
					}						
				}			

				pos = mFS->GetPos();		

				if (usedChannels == 1)
				{
					for (int i = 0; i < aSize; i++)
					{
						uint8 intensity = (uint8) (pattern->mBits[i] & 0xFF);
						pattern->mIntensityBits[i] = intensity;
						pattern->mBits[i] |= 0xFF000000 | (((uint32) intensity) << 8) | (((uint32) intensity) << 16);
					}
				}
				else if (usedChannels == 2)
				{
					for (int i = 0; i < aSize; i++)
					{
						uint8 aColor = (uint8) (pattern->mBits[i] & 0xFF);
						uint8 anAlpha = (uint8) ((pattern->mBits[i] >> 8) & 0xFF);
						pattern->mIntensityBits[i] = aColor * anAlpha / 255;
						pattern->mBits[i] = ((uint32) anAlpha << 24) | aColor | (((uint32) aColor) << 8) | (((uint32) aColor) << 16);
					}
				}
				else if (usedChannels == 3)
				{
					for (int i = 0; i < aSize; i++)
					{
						pattern->mBits[i] |= 0xFF000000;
						uint8 r = (uint8) ((pattern->mBits[i]) & 0xFF);
						uint8 g = (uint8) ((pattern->mBits[i] >> 8) & 0xFF);
						uint8 b = (uint8) ((pattern->mBits[i] >> 16) & 0xFF);				
						pattern->mIntensityBits[i] = (int) ((r * 0.299f) + (g * 0.587f) + (b * 0.114f) + 0.5f);
					}
				}
				else
				{
					for (int i = 0; i < aSize; i++)
					{
						uint8 r = (uint8) ((pattern->mBits[i]) & 0xFF);
						uint8 g = (uint8) ((pattern->mBits[i] >> 8) & 0xFF);
						uint8 b = (uint8) ((pattern->mBits[i] >> 16) & 0xFF);
						uint8 a = (uint8) ((pattern->mBits[i] >> 24) & 0xFF);
						pattern->mIntensityBits[i] = (int) ((((r * 0.299f) + (g * 0.587f) + (b * 0.114f)) * a / 255.0f) + (255.0f - a) + 0.5f);
					}
				}

				delete rowLengths;

				mPSDPatternMap[id] = pattern;			

				int newPos = patternStartPos + ((aLength + 3) & ~3);
				mFS->SetPos(newPos);

				pos = mFS->GetPos();
				if (mFS->GetPos() + 3 >= addlStart + addlSize)
					break;

				//int padding = mFS->ReadInt8();
			}
		}
		
		addlSize = (addlSize + 3) & ~3;		
		mFS->SetPos(addlStart + addlSize);
	}
}

void PSDReader::ReadEffectSection(ImageEffects* imageEffects, PSDDescriptor* desc)
{
	ImageEffects* anImageEffects = imageEffects;

	if (desc->Get("masterFXSwitch")->mInteger == 0)
		return;
				
	// Drop shadow
	PSDDescriptor* effectDesc = desc->GetDescriptor("DrSh");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageDropShadowEffect* effect = new ImageDropShadowEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;
		ReadEffectColor(effectDesc->GetDescriptor("Clr "), &effect->mColor);
						
		effect->mLocalAngle = effectDesc->Get("lagl")->mDouble;
		effect->mUseGlobalLight = effectDesc->Get("uglg")->mInteger != 0;
		effect->mDistance = effectDesc->Get("Dstn")->mDouble;
		effect->mSpread = effectDesc->Get("Ckmt")->mDouble;
		effect->mSize = effectDesc->Get("blur")->mDouble;

		ReadEffectContour(effectDesc->GetDescriptor("TrnS"), &effect->mContour);
		effect->mNoise = effectDesc->Get("Nose")->mDouble;

		effect->mAntiAliased = effectDesc->Get("AntA")->mInteger != 0;		
		effect->mLayerKnocksOut = effectDesc->Get("layerConceals")->mInteger != 0;

		anImageEffects->AddEffect(effect);
	}
	
	// Outer glow
	effectDesc = desc->GetDescriptor("OrGl");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageOuterGlowEffect* effect = new ImageOuterGlowEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;
		effect->mNoise = effectDesc->Get("Nose")->mDouble;

		effect->mHasGradient = ReadEffectGradient(effectDesc, effect->mColorGradient);

		effect->mTechnique = effectDesc->Get("GlwT")->GetMulticharInt();								
		effect->mSpread = effectDesc->Get("Ckmt")->mDouble;
		effect->mSize = effectDesc->Get("blur")->mDouble;

		ReadEffectContour(effectDesc->GetDescriptor("TrnS"), &effect->mContour);

		effect->mAntiAliased = effectDesc->Get("AntA")->mInteger != 0;
		effect->mRange = effectDesc->Get("Inpr")->mDouble;
		effect->mJitter = effectDesc->Get("ShdN")->mDouble;

		anImageEffects->AddEffect(effect);
	}

	// Pattern Overlay
	effectDesc = desc->GetDescriptor("patternFill");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImagePatternOverlayEffect* effect = new ImagePatternOverlayEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;		
		ReadPatternFill(effectDesc, &effect->mPattern);
		anImageEffects->AddEffect(effect);
	}

	// Gradient Overlay
	effectDesc = desc->GetDescriptor("GrFl");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageGradientOverlayEffect* effect = new ImageGradientOverlayEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;		
		ReadGradientFill(effectDesc, &effect->mGradientFill);
		anImageEffects->AddEffect(effect);
	}

	// Color Overlay
	effectDesc = desc->GetDescriptor("SoFi");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageColorOverlayEffect* effect = new ImageColorOverlayEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;		
		ReadEffectColor(effectDesc->GetDescriptor("Clr "), &effect->mColorFill.mColor);
		anImageEffects->AddEffect(effect);
	}

	// Satin
	effectDesc = desc->GetDescriptor("ChFX");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageSatinEffect* effect = new ImageSatinEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;

		effect->mAngle = effectDesc->Get("lagl")->mDouble;
		effect->mDistance = effectDesc->Get("Dstn")->mDouble;
		effect->mSize = effectDesc->Get("blur")->mDouble;
		
		ReadEffectColor(effectDesc->GetDescriptor("Clr "), &effect->mColor);
		ReadEffectContour(effectDesc->GetDescriptor("MpgS"), &effect->mContour);
		effect->mInvert = effectDesc->Get("Invr")->mInteger != 0;
		effect->mAntiAliased = effectDesc->Get("AntA")->mInteger != 0;

		anImageEffects->AddEffect(effect);
	}

	// Inner glow
	effectDesc = desc->GetDescriptor("IrGl");
	if (effectDesc->Get("enab")->mInteger != 0)
	{								
		ImageInnerGlowEffect* effect = new ImageInnerGlowEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;
		effect->mNoise = effectDesc->Get("Nose")->mDouble;

		effect->mHasGradient = ReadEffectGradient(effectDesc, effect->mColorGradient);

		effect->mIsCenter = effectDesc->Get("glwS")->mString == "SrcC";
		effect->mTechnique = effectDesc->Get("GlwT")->GetMulticharInt();								
		effect->mChoke = effectDesc->Get("Ckmt")->mDouble;
		effect->mSize = effectDesc->Get("blur")->mDouble;

		ReadEffectContour(effectDesc->GetDescriptor("TrnS"), &effect->mContour);

		effect->mAntiAliased = effectDesc->Get("AntA")->mInteger != 0;
		effect->mRange = effectDesc->Get("Inpr")->mDouble;
		effect->mJitter = effectDesc->Get("ShdN")->mDouble;

		anImageEffects->AddEffect(effect);
	}

	// Inner shadow
	effectDesc = desc->GetDescriptor("IrSh");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageInnerShadowEffect* effect = new ImageInnerShadowEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;
		ReadEffectColor(effectDesc->GetDescriptor("Clr "), &effect->mColor);
						
		effect->mLocalAngle = effectDesc->Get("lagl")->mDouble;
		effect->mUseGlobalLight = effectDesc->Get("uglg")->mInteger != 0;
		effect->mDistance = effectDesc->Get("Dstn")->mDouble;
		effect->mSpread = effectDesc->Get("Ckmt")->mDouble;
		effect->mSize = effectDesc->Get("blur")->mDouble;

		ReadEffectContour(effectDesc->GetDescriptor("TrnS"), &effect->mContour);
		effect->mNoise = effectDesc->Get("Nose")->mDouble;

		effect->mAntiAliased = effectDesc->Get("AntA")->mInteger != 0;				

		anImageEffects->AddEffect(effect);
	}

	// Stroke
	effectDesc = desc->GetDescriptor("FrFX");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageStrokeEffect* effect = new ImageStrokeEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;

		effect->mSize = effectDesc->Get("Sz  ")->mDouble;
		effect->mPosition = effectDesc->Get("Styl")->GetMulticharInt();
				
		effect->mFillType = effectDesc->Get("PntT")->GetMulticharInt();
		ReadEffectColor(effectDesc->GetDescriptor("Clr "), &effect->mColorFill.mColor);
		ReadGradientFill(effectDesc, &effect->mGradientFill);
		ReadPatternFill(effectDesc, &effect->mPatternFill);

		anImageEffects->AddEffect(effect);
	}

	// Bevel
	effectDesc = desc->GetDescriptor("ebbl");
	if (effectDesc->Get("enab")->mInteger != 0)
	{
		ImageBevelEffect* effect = new ImageBevelEffect();
		effect->mBlendMode = ReadBlendMode(effectDesc->Get("Md  "));
		effect->mOpacity = effectDesc->Get("Opct")->mDouble;

		effect->mStyle = effectDesc->Get("bvlS")->GetMulticharInt();
		effect->mTechnique = effectDesc->Get("bvlT")->GetMulticharInt();
		effect->mDepth = effectDesc->Get("srgR")->mDouble;
		effect->mDirectionUp = effectDesc->Get("bvlD")->mString == "In  ";
		effect->mSize = effectDesc->Get("blur")->mDouble;;
		effect->mSoften = effectDesc->Get("Sftn")->mDouble;

		effect->mLocalAngle = effectDesc->Get("lagl")->mDouble;
		effect->mUseGlobalLight = effectDesc->Get("uglg")->mInteger != 0;
		effect->mLocalAltitude = effectDesc->Get("Lald")->mDouble;
		ReadEffectContour(effectDesc->GetDescriptor("TrnS"), &effect->mGlossContour);
		effect->mAntiAliased = effectDesc->Get("antialiasGloss")->mInteger != 0;	
		effect->mHiliteMode = ReadBlendMode(effectDesc->Get("hglM"));
		ReadEffectColor(effectDesc->GetDescriptor("hglC"), &effect->mHiliteColor);
		effect->mHiliteOpacity = effectDesc->Get("hglO")->mDouble;

		effect->mShadowMode = ReadBlendMode(effectDesc->Get("sdwM"));
		ReadEffectColor(effectDesc->GetDescriptor("sdwC"), &effect->mShadowColor);
		effect->mShadowOpacity = effectDesc->Get("sdwO")->mDouble;
								
		effect->mUseContour = effectDesc->Get("useShape")->mInteger != 0;
		effect->mUseTexture = effectDesc->Get("useTexture")->mInteger != 0;
								
		ReadEffectContour(effectDesc->GetDescriptor("MpgS"), &effect->mBevelContour);
		effect->mBevelContourRange = effectDesc->Get("Inpr")->mDouble;

		effect->mTextureDepth = effectDesc->Get("textureDepth")->mDouble;		
		ReadPatternFill(effectDesc, &effect->mTexture);		

		effect->mTextureInvert = effectDesc->Get("InvT")->mInteger != 0;
		anImageEffects->AddEffect(effect);
	}
}

void PSDReader::ReadGradientFill(PSDDescriptor* descriptor, ImageGradientFill* gradientFill)
{
	ReadEffectGradient(descriptor, gradientFill->mColorGradient);
	gradientFill->mReverse = descriptor->Get("Rvrs")->mInteger != 0;
	gradientFill->mStyle = descriptor->Get("Type")->GetMulticharInt();
	gradientFill->mAlignWithLayer = descriptor->Get("Algn")->mInteger != 0;
	gradientFill->mAngle = descriptor->Get("Angl")->mDouble;
	gradientFill->mScale = descriptor->Get("Scl ")->mDouble;
	PSDDescriptor* offsetDesc = descriptor->GetDescriptor("Ofst");
	gradientFill->mOffsetX = offsetDesc->Get("Hrzn")->mDouble;
	gradientFill->mOffsetY = offsetDesc->Get("Vrtc")->mDouble;
}

void PSDReader::ReadPatternFill(PSDDescriptor* descriptor, ImagePatternFill* patternFill)
{
	PSDDescriptor* phaseDesc = descriptor->GetDescriptor("phase");
	patternFill->mPhaseX = phaseDesc->Get("Hrzn")->mDouble;
	patternFill->mPhaseY = phaseDesc->Get("Vrtc")->mDouble;
	patternFill->mLinkWithLayer = true;	
	if (descriptor->Contains("Algn"))
		patternFill->mLinkWithLayer = descriptor->Get("Algn")->mInteger != 0;		
	if (descriptor->Contains("Lnkd"))
		patternFill->mLinkWithLayer = descriptor->Get("Lnkd")->mInteger != 0;		
	patternFill->mScale = descriptor->Get("Scl ")->mDouble;
	if (patternFill->mScale == 0)
		patternFill->mScale = 100.0;		
	PSDDescriptor* patternDesc = descriptor->GetDescriptor("Ptrn");
	patternFill->mPatternName = patternDesc->Get("Idnt")->mString;	
}

bool PSDReader::Init(const StringImpl& fileName)
{
	AutoPerf gPerf("PSDReader::Init");

	/*mFS = gBFApp->OpenBinaryFile(fileName);
	if (mFS == NULL)
		return false;*/

	mFS = new FileStream();
	mFS->Open(fileName, "rb");

	if (!mFS->IsOpen())
	{
		delete mFS;
		mFS = NULL;
		return false;
	}

	//mFS->SetCacheSize(4096);

	mFS->mBigEndian = true;
	
	int32 header = mFS->ReadInt32();
	if (header != '8BPS')
		return false;
	
	mVersion = mFS->ReadInt16();
	mFS->Seek(6);
	mChannels = mFS->ReadInt16();
	mHeight = mFS->ReadInt32();
	mWidth = mFS->ReadInt32();	
	mBitDepthPerChannel = mFS->ReadInt16();
	mMode = mFS->ReadInt16();

	int colorModeLen = mFS->ReadInt32();
	if (colorModeLen > 0)
		mFS->Seek(colorModeLen);

	int imageResourcesLen = mFS->ReadInt32();
	int resourceStartPos = mFS->GetPos();
	if (imageResourcesLen > 0)
	{		
		while (mFS->GetPos() < resourceStartPos + imageResourcesLen)
		{
			int signature = mFS->ReadInt32();
			BF_ASSERT(signature == '8BIM');
			int aType = mFS->ReadInt16();			
			std::string aName = mFS->ReadAscii8SizedString();
			int extra = (int)(((aName.length() + 2) & ~1) - (aName.length() + 1));
			mFS->Seek(extra);
			int dataSize = mFS->ReadInt32();
			//BF_ASSERT(dataSize < 100000);
			int curPos = mFS->GetPos();
			
			if (aType == 0x40D)
			{
				mGlobalAngle = mFS->ReadInt32();
			}
			else if (aType == 0x419)
			{
				mGlobalAltitude = mFS->ReadInt32();
			}

			mFS->SetPos(curPos + (dataSize + 1) & ~1);
		}

		mFS->SetPos(resourceStartPos + imageResourcesLen);
	}
	
	int layerAndMaskLen = mFS->ReadInt32();
	int startPos = mFS->GetPos();
	if (layerAndMaskLen > 0)
	{		
		int layersLen = mFS->ReadInt32();
		int layerStart = mFS->GetPos();
		if (layersLen > 0)
		{
			int numLayers = abs(mFS->ReadInt16());

			for (int layerNum = 0; layerNum < numLayers; layerNum++)
			{
				PSDLayerInfo* pSDLayerInfo = new PSDLayerInfo();
				pSDLayerInfo->mPSDReader = this;

				pSDLayerInfo->mY = mFS->ReadInt32();
				pSDLayerInfo->mX = mFS->ReadInt32();				
				pSDLayerInfo->mHeight = mFS->ReadInt32() - pSDLayerInfo->mY;
				pSDLayerInfo->mWidth = mFS->ReadInt32() - pSDLayerInfo->mX;				

				int channelCount = mFS->ReadInt16();				
				for (int channelIdx = 0; channelIdx < channelCount; channelIdx++)
				{
					PSDChannelInfo pSDChannelInfo;
					pSDChannelInfo.mId = mFS->ReadInt16();
					pSDChannelInfo.mLength = mFS->ReadInt32();
					pSDLayerInfo->mChannels.push_back(pSDChannelInfo);
				}

				int blendModeSig = mFS->ReadInt32(); // Should be '8BIM'
				int blendMode = mFS->ReadInt32();
				pSDLayerInfo->mIsAdditive = (blendMode == 'lddg');
				pSDLayerInfo->mBlendMode = blendMode;
				pSDLayerInfo->mOpacity = mFS->ReadInt8();
				pSDLayerInfo->mBaseClipping = mFS->ReadInt8() == 0;				
				int flags = mFS->ReadInt8();
				int filler = mFS->ReadInt8();

				pSDLayerInfo->mVisible = (flags & (1<<1)) == 0;
				
				int extraDataLen = mFS->ReadInt32();
				int extraDataStart = mFS->GetPos();
				
				int layerMaskDataLen = mFS->ReadInt32();
				int layerMaskStart = mFS->GetPos();
				if (layerMaskDataLen != 0)
				{
					pSDLayerInfo->mLayerMaskY = mFS->ReadInt32();
					pSDLayerInfo->mLayerMaskX = mFS->ReadInt32();					
					pSDLayerInfo->mLayerMaskHeight = mFS->ReadInt32() - pSDLayerInfo->mLayerMaskY;
					pSDLayerInfo->mLayerMaskWidth = mFS->ReadInt32() - pSDLayerInfo->mLayerMaskX;					

					uint8 aColor = (uint8) mFS->ReadInt8();
					int flags = (uint8) mFS->ReadInt8();

					pSDLayerInfo->mLayerMaskEnabled = (flags & 2) == 0;
					pSDLayerInfo->mLayerMaskInverted = (flags & 4) == 0;
					pSDLayerInfo->mLayerMaskDefault = aColor;

					mFS->SetPos(layerMaskStart + layerMaskDataLen);
				}
				
				int layerBlendingRangesLen = mFS->ReadInt32();
				int layerBlendingRangesStart = mFS->GetPos();
				if (layerBlendingRangesLen > 0)
				{
					int idx = 0;
					// What's the 5th range value for?
					while ((mFS->GetPos() < layerBlendingRangesStart + layerBlendingRangesLen) && (idx < 4))
					{
						int source = mFS->ReadInt32();
						int dest = mFS->ReadInt32();
						int shift = ((idx + 3) % 4) * 8;
						pSDLayerInfo->mBlendingRangeSourceStart = (pSDLayerInfo->mBlendingRangeSourceStart & ~(0xFF<<shift)) | ((source>>16 & 0xFF)<<shift);
						pSDLayerInfo->mBlendingRangeSourceEnd = (pSDLayerInfo->mBlendingRangeSourceEnd & ~(0xFF<<shift)) | ((source & 0xFF)<<shift);
						pSDLayerInfo->mBlendingRangeDestStart = (pSDLayerInfo->mBlendingRangeDestStart & ~(0xFF<<shift)) | ((dest>>16 & 0xFF)<<shift);
						pSDLayerInfo->mBlendingRangeDestEnd = (pSDLayerInfo->mBlendingRangeDestEnd & ~(0xFF<<shift)) | ((dest & 0xFF)<<shift);						
						idx++;
					}

					mFS->SetPos(layerBlendingRangesStart + layerBlendingRangesLen);
				}

				pSDLayerInfo->mName = mFS->ReadAscii8SizedString();

				int extra = (int)(((pSDLayerInfo->mName.length() + 4) & ~3) - (pSDLayerInfo->mName.length() + 1));
				mFS->Seek(extra);

				AutoPerf gPerf("PSDReader::Init - Extras");
				while (mFS->GetPos() < extraDataStart + extraDataLen)
				{										
					int addlSignature = mFS->ReadInt32();
					BF_ASSERT(addlSignature == '8BIM');
					int addlType = mFS->ReadInt32();
					int addlSize = mFS->ReadInt32();

					int addlStart = mFS->GetPos();

					if (addlSignature == '8BIM')
					{
						if (addlType == 'lfx2')
						{
							int32 version = mFS->ReadInt32();
							int descriptorVersion = mFS->ReadInt32();
							PSDDescriptor* aDesc = new PSDDescriptor();
							ReadPSDDescriptor(aDesc);
							ReadEffectSection(pSDLayerInfo->mImageEffects, aDesc);
							delete aDesc;
						}
						else if (addlType == 'lyid')
						{							
							pSDLayerInfo->mLayerId = mFS->ReadInt32();
						}
						else if (addlType == 'lrFX')
						{
							int version = mFS->ReadInt16();
							int countCount = mFS->ReadInt16();
						}
						else if (addlType == 'TySh')
						{
							int typeVersion = mFS->ReadInt16();
							double a = mFS->ReadDouble();
							double b = mFS->ReadDouble();
							double c = mFS->ReadDouble();
							double d = mFS->ReadDouble();
							double tx = mFS->ReadDouble();
							double ty = mFS->ReadDouble();
							int textVersion = mFS->ReadInt16();
							
							int descriptorVersion = mFS->ReadInt32();
							PSDDescriptor* aDescriptor = new PSDDescriptor();
							ReadPSDDescriptor(aDescriptor);							
							delete aDescriptor;
						}
						else if (addlType == 'luni')
						{
							NOP;
						}
						else if (addlType == 'lnsr')
						{
							NOP;
						}						
						else if (addlType == 'clbl')
						{
							pSDLayerInfo->mBlendClippedElementsAsGroup = mFS->ReadInt8() != 0;
						}
						else if (addlType == 'infx')
						{
							pSDLayerInfo->mBlendInteriorEffectsAsGroup = mFS->ReadInt8() != 0;
						}	
						else if (addlType == 'knko')
						{
							pSDLayerInfo->mKnockout = mFS->ReadInt8();
						}	
						else if (addlType == 'lspf')
						{
							NOP;
						}	
						else if (addlType == 'tsly')
						{
							pSDLayerInfo->mTransparencyShapesLayer = mFS->ReadInt8() != 0;
						}	
						else if (addlType == 'lmgm')
						{
							pSDLayerInfo->mLayerMaskHidesEffects = mFS->ReadInt8() != 0;
						}
						else if (addlType == 'vmgm')
						{
							pSDLayerInfo->mVectorMaskHidesEffects = mFS->ReadInt8() != 0;
						}
						else if (addlType == 'lyvr')
						{
							NOP;
						}	
						else if (addlType == 'lclr')
						{
							NOP;
						}	
						else if (addlType == 'shmd') // Metadata
						{
							NOP;
						}	
						else if (addlType == 'fxrp')
						{
							pSDLayerInfo->mRefX = mFS->ReadDouble();
							pSDLayerInfo->mRefY = mFS->ReadDouble();
						}
						else if (addlType == 'iOpa')
						{
							pSDLayerInfo->mFillOpacity = mFS->ReadInt8();
						}
						else if (addlType == 'lsct')
						{
							pSDLayerInfo->mSectionDividerType = mFS->ReadInt32();
							int sig = mFS->ReadInt32();
							pSDLayerInfo->mSectionBlendMode = mFS->ReadInt32();
						}
						else if (addlType == 'brst')
						{
							for (int i = 0; i < addlSize/4; i++)
							{
								int channel = mFS->ReadInt32();
								pSDLayerInfo->mChannelMask &= ~(0xFF << (8*channel));
							}

							// Turn off alpha if RGB is all off
							if (pSDLayerInfo->mChannelMask == 0xFF000000)
								pSDLayerInfo->mChannelMask = 0;
						}
						else if (addlType == 'nvrt')
						{
							pSDLayerInfo->mImageAdjustment = new InvertImageAdjustement();
						}
						else if (addlType == 'SoCo')
						{
							int version = mFS->ReadInt32();
							PSDDescriptor aDescriptor;
							ReadPSDDescriptor(&aDescriptor);

							SolidColorImageAdjustement* adjustment = new SolidColorImageAdjustement();
							ReadEffectColor(aDescriptor.GetDescriptor("Clr "), &adjustment->mColor);
							pSDLayerInfo->mImageAdjustment = adjustment;
						}
						else if (addlType == 'GdFl')
						{
							int version = mFS->ReadInt32();
							PSDDescriptor aDescriptor;
							ReadPSDDescriptor(&aDescriptor);

							GradientImageAdjustement* adjustment = new GradientImageAdjustement();
							adjustment->mFill = new ImageGradientFill();
							adjustment->mFill->mAlignWithLayer = true;
							ReadGradientFill(&aDescriptor, adjustment->mFill);							
							pSDLayerInfo->mImageAdjustment = adjustment;
						}
						else if (addlType == 'PtFl')
						{
							int version = mFS->ReadInt32();
							PSDDescriptor aDescriptor;
							ReadPSDDescriptor(&aDescriptor);

							PatternImageAdjustement* adjustment = new PatternImageAdjustement();
							adjustment->mFill = new ImagePatternFill();							
							ReadPatternFill(&aDescriptor, adjustment->mFill);							
							pSDLayerInfo->mImageAdjustment = adjustment;
						}
						else if (addlType == 'brit')
						{							
							BrightnessContrastImageAdjustment* adjustment = new BrightnessContrastImageAdjustment();
							adjustment->mBrightness = mFS->ReadInt16();
							adjustment->mContrast = mFS->ReadInt16();
							adjustment->mMeanValue = mFS->ReadInt16();
							adjustment->mLabColorOnly = mFS->ReadInt8() != 0;
							pSDLayerInfo->mImageAdjustment = adjustment;
						}
						else if (addlType == 'CgEd')
						{							
							int version = mFS->ReadInt32();
							PSDDescriptor aDescriptor;
							ReadPSDDescriptor(&aDescriptor);
							
							BrightnessContrastImageAdjustment* brightnessAdjustment = dynamic_cast<BrightnessContrastImageAdjustment*>(pSDLayerInfo->mImageAdjustment);
							if (brightnessAdjustment != NULL)
							{
								brightnessAdjustment->mBrightness = aDescriptor.Get("Brgh")->mInteger;
								brightnessAdjustment->mContrast = aDescriptor.Get("Cntr")->mInteger;
								brightnessAdjustment->mMeanValue = aDescriptor.Get("means")->mInteger;
								brightnessAdjustment->mLabColorOnly = aDescriptor.Get("Lab")->mInteger != 0;
							}
						}
						else if (addlType == 'vmsk')
						{
							pSDLayerInfo->mVectorMask = new PSDPath();

							int version = mFS->ReadInt32();
							int flags = mFS->ReadInt32();

							int count = addlSize / 26;

							for (int i = 0; i < count; i++)
							{
								int aType = mFS->ReadInt16();
								if (aType == 0)
								{
									pSDLayerInfo->mVectorMask->mClosed = true;
									mFS->Seek(24);
								}
								else if (aType == 3)
								{
									pSDLayerInfo->mVectorMask->mClosed = false;
									mFS->Seek(24);
								}
								else if ((aType == 1) || (aType == 2))
								{	
									PSDPathPoint point;

									point.mCtlEnterX = (mFS->ReadInt32() / (double) 0x1000000) * mWidth;
									point.mCtlEnterY = (mFS->ReadInt32() / (double) 0x1000000) * mHeight;

									point.mAnchorX = (mFS->ReadInt32() / (double) 0x1000000) * mWidth;
									point.mAnchorY = (mFS->ReadInt32() / (double) 0x1000000) * mHeight;

									point.mCtlLeaveX = (mFS->ReadInt32() / (double) 0x1000000) * mWidth;
									point.mCtlLeaveY = (mFS->ReadInt32() / (double) 0x1000000) * mHeight;
									
									pSDLayerInfo->mVectorMask->mPoints.push_back(point);
									NOP;
								}
								else
								{
									mFS->Seek(24);
								}								
							}
						}
						else
						{
							NOP;
						}
					}

					mFS->SetPos(addlStart + addlSize);
				}

				int pos = mFS->GetPos();
				mFS->SetPos(extraDataStart + extraDataLen);				
				
				mPSDLayerInfoVector.insert(mPSDLayerInfoVector.begin(), pSDLayerInfo);
			}
			
			int filePos = mFS->GetPos();
			int channelsStart = filePos;
			int channelsLen = 0;

			for (int layerNum = (int) mPSDLayerInfoVector.size() - 1; layerNum >= 0; layerNum--)
			{
				PSDLayerInfo* pSDLayerInfo = mPSDLayerInfoVector[layerNum];
				pSDLayerInfo->mImageDataStart = filePos;

				for (int channelIdx = 0; channelIdx < (int) pSDLayerInfo->mChannels.size(); channelIdx++)
				{
					PSDChannelInfo* channel = &pSDLayerInfo->mChannels[channelIdx];
					filePos += channel->mLength;										
					channelsLen += channel->mLength;
				}
			}

			PSDLayerInfo* parent = NULL;
			for (int idx = 0; idx < (int) mPSDLayerInfoVector.size(); idx++)
			{
				PSDLayerInfo* aLayer = mPSDLayerInfoVector[idx];
				aLayer->mParent = parent;
				aLayer->mIdx = idx;
				if ((aLayer->mSectionDividerType == PSDDIVIDER_OPEN_FOLDER) || (aLayer->mSectionDividerType == PSDDIVIDER_CLOSED_FOLDER))				
					parent = aLayer;
				else if (aLayer->mSectionDividerType == PSDDIVIDER_SECTION_END)
				{
					parent = parent->mParent;

					delete aLayer;
					mPSDLayerInfoVector.erase(mPSDLayerInfoVector.begin() + idx);
					idx--;
				}				
			}
			
			mFS->SetPos(layerStart + layersLen);			
			int posThing = mFS->GetPos();
			int extraVersion = mFS->ReadInt32();			
			mFS->Seek(extraVersion);
			ReadExtraInfo(startPos + layerAndMaskLen);
		}
	}
	int pos = mFS->GetPos();
	mFS->SetPos(startPos + layerAndMaskLen);
	pos = mFS->GetPos();

	return true;
}

Texture* PSDReader::LoadLayerTexture(int layerIdx, int* ofsX, int* ofsY) // -1 = composited image
{
	if ((layerIdx < 0) || (layerIdx >= (int) mPSDLayerInfoVector.size()))
		return NULL;	

	//TODO: This is a special case for flattening here, just on the first layer...
	PSDLayerInfo* prevLayer = (layerIdx == 1) ? mPSDLayerInfoVector[layerIdx - 1] : NULL;	
	if (prevLayer != NULL)
	{
		if (prevLayer->mBits == NULL)
			prevLayer->ReadData();
	}

	PSDLayerInfo* aLayer = mPSDLayerInfoVector[layerIdx];	
	if (aLayer->mBits == NULL)
		aLayer->ReadData();
	
	ImageData* anImageData = aLayer;
	
	anImageData = aLayer->mImageEffects->FlattenInto(prevLayer, aLayer, aLayer, NULL, NULL);

	Texture* texture = gBFApp->mRenderDevice->LoadTexture(anImageData, false);
	*ofsX = anImageData->mX;
	*ofsY = anImageData->mY;
	if (anImageData != aLayer)
		delete anImageData;	
	return texture;
}

typedef std::vector<PSDLayerInfo*> PSDLayerVector;
typedef std::map<int, PSDLayerVector> LayerInfoMap;
typedef std::set<int> IntSet;

ImageData* PSDReader::MergeLayers(PSDLayerInfo* group, const std::vector<int>& layerIndices, ImageData* bottomImage)
{
	AutoPerf gPerf("PSDReader::MergeLayers");

	bool isImageAllocated = false;
	ImageData* combinedImage = bottomImage;
	
	ImageData* prevBottom = NULL;
	bool doPostGroupBlend = false;
	bool needsPrevBottom = (group != NULL) && (group->mOpacity != 255);

	for (int indexIdx = 0; indexIdx < (int) layerIndices.size(); indexIdx++)
	{		
		PSDLayerInfo* layerInfo = mPSDLayerInfoVector[layerIndices[indexIdx]];
		if (layerInfo->mBits == NULL)
			layerInfo->ReadData();
		if (layerInfo->mKnockout == 1) // Shallow
			needsPrevBottom = true;
	}

	if ((group != NULL) && (bottomImage != NULL) && (group->mSectionBlendMode != 'pass'))
	{
		doPostGroupBlend = true;
		combinedImage = NULL;	
	}	
	
	if (needsPrevBottom && (bottomImage != NULL))
		prevBottom = bottomImage->Duplicate();

	bool hasAllocatedClipBase = false;
	PSDLayerInfo* clipBaseLayer = NULL;
	ImageData* clipBaseImage = NULL;

	for (int indexIdx = 0; indexIdx <= (int) layerIndices.size(); indexIdx++)
	{		
		PSDLayerInfo* layerInfo = NULL;
		
		if (indexIdx < (int) layerIndices.size())
			layerInfo = mPSDLayerInfoVector[layerIndices[indexIdx]];

		PSDLayerInfo* pendingFlushLayer = NULL;
		ImageData* pendingFlushImage = NULL;		

		if ((layerInfo != NULL) && (!layerInfo->mBaseClipping))
		{
			if (clipBaseLayer != NULL)
			{
				if (clipBaseLayer->mBlendClippedElementsAsGroup)
				{					
					ImageData* newImage = layerInfo->mImageEffects->FlattenInto(clipBaseImage, layerInfo, layerInfo, prevBottom, NULL);
					SetImageAlpha(newImage, 255);					

					if (hasAllocatedClipBase)
						delete clipBaseImage;
					
					hasAllocatedClipBase = true;
					clipBaseImage = newImage;
				}
				else
				{					
					if (clipBaseImage == clipBaseLayer)
					{
						/*clipBaseImage = clipBaseImage->Duplicate();
						//SetImageAlpha(clipBaseImage, clipBaseLayer->mFillOpacity);
						SetImageAlpha(clipBaseImage, 0);
						hasAllocatedClipBase = true;*/
						//clipBaseImage = layerInfo->mImageEffects->FlattenInto(combinedImage, clipBaseLayer, clipBaseLayer, prevBottom, NULL);
						//hasAllocatedClipBase = true;

						//combinedImage = combinedImage->Duplicate();
						
						ImageData* newImage = NULL;
						if (clipBaseLayer->mKnockout == 0) // No Knockout
						{
							newImage = CreateResizedImageUnion(combinedImage, clipBaseLayer->mX, clipBaseLayer->mY, clipBaseLayer->mWidth, clipBaseLayer->mHeight);
						}
						else if (clipBaseLayer->mKnockout == 1) // Shallow Knockout
						{
							newImage = CreateResizedImageUnion(prevBottom, clipBaseLayer->mX, clipBaseLayer->mY, clipBaseLayer->mWidth, clipBaseLayer->mHeight);
						}
						else // Deep Knockout
						{							
							newImage = CreateEmptyResizedImageUnion(combinedImage, clipBaseLayer->mX, clipBaseLayer->mY, clipBaseLayer->mWidth, clipBaseLayer->mHeight);							
						}

						if (combinedImage != NULL)
							BlendImage(newImage, clipBaseImage, clipBaseImage->mX -  combinedImage->mX, clipBaseImage->mY - combinedImage->mY, clipBaseLayer->mFillOpacity / 255.0f, clipBaseLayer->mBlendMode, true);

						if (hasAllocatedClipBase)
							delete clipBaseImage;

						hasAllocatedClipBase = true;
						clipBaseImage = newImage;
					}
					
					//ImageData* tempImage = layerInfo->Duplicate();
					//SetImageAlpha(tempImage, 255);											
					//ImageData* newImage = layerInfo->mImageEffects->FlattenInto(clipBaseImage, layerInfo, tempImage, prevBottom);
					ImageData* newImage = layerInfo->mImageEffects->FlattenInto(clipBaseImage, layerInfo, layerInfo, prevBottom, NULL);
					
					if (hasAllocatedClipBase)
						delete clipBaseImage;
					
					hasAllocatedClipBase = true;
					clipBaseImage = newImage;
				}
			}
		}
		else
		{
			pendingFlushImage = clipBaseImage;
			pendingFlushLayer = clipBaseLayer;
			
			clipBaseImage = layerInfo;
			clipBaseLayer = layerInfo;
		}
			
		if (pendingFlushImage != NULL)
		{
			ImageData* newImage = NULL;			
			
			if ((pendingFlushLayer != pendingFlushImage) && (!pendingFlushLayer->mBlendClippedElementsAsGroup))
			{
				/*hasAllocatedClipBase = false;
				BlendImagesTogether(combinedImage, pendingFlushImage, pendingFlushLayer);
				newImage = combinedImage;*/
				newImage = pendingFlushLayer->mImageEffects->FlattenInto(combinedImage, pendingFlushLayer, pendingFlushLayer, prevBottom, pendingFlushImage);
			}
			else
			{
				if ((pendingFlushImage != pendingFlushLayer) && (pendingFlushLayer->mBlendClippedElementsAsGroup))
					SetImageAlpha(pendingFlushImage, pendingFlushLayer);
				newImage = pendingFlushLayer->mImageEffects->FlattenInto(combinedImage, pendingFlushLayer, pendingFlushImage, prevBottom, NULL);
			}			

			if (isImageAllocated)
				delete combinedImage;

			isImageAllocated = newImage == pendingFlushLayer;
			combinedImage = newImage;

			if (hasAllocatedClipBase)
				delete pendingFlushImage;
		}

		// Is this a group?
		if ((layerInfo != NULL) && (layerInfo->mParent != group))
		{
			std::vector<int> aChildren;
			//indexIdx--;
			for (; indexIdx < (int) layerIndices.size(); indexIdx++)
			{
				layerInfo = mPSDLayerInfoVector[layerIndices[indexIdx]];
				if (layerInfo->mParent == group)				
					break;

				aChildren.push_back(layerInfo->mIdx);
			}
				
			ImageData* newImage = MergeLayers(layerInfo, aChildren, combinedImage);

			if (isImageAllocated)
				delete combinedImage;

			isImageAllocated = newImage == layerInfo;
			combinedImage = newImage;

			if (hasAllocatedClipBase)
				delete pendingFlushImage;

			clipBaseImage = NULL;
			clipBaseLayer = NULL;
		}
	}

	if (hasAllocatedClipBase)
		delete clipBaseImage;

	if (doPostGroupBlend)
	{			
		ImageData* blendInto = CreateResizedImageUnion(bottomImage, combinedImage->mX, combinedImage->mY, combinedImage->mWidth, combinedImage->mHeight);				

		BlendImage(blendInto, combinedImage, combinedImage->mX - blendInto->mX, combinedImage->mY - blendInto->mY,
			1.0f, group->mSectionBlendMode);

		if (isImageAllocated)
			delete combinedImage;

		combinedImage = blendInto;
	}

	if ((group != NULL) && (group->mOpacity != 255))
	{
		// Crossfade together with previous bottom
		CrossfadeImage(prevBottom, combinedImage, group->mOpacity / 255.0f);
		delete prevBottom;
	}

	return combinedImage;
}

Texture* PSDReader::LoadMergedLayerTexture(const std::vector<int>& layerIndices, int* ofsX, int* ofsY)
{
	AutoPerf gPerf("PSDReader::LoadMergedLayerTexture");

	LayerInfoMap groupMap;

	if (layerIndices.size() == 0)
		return NULL;	

	IntSet layerSet;

	// Fix up the layer set - add parents and remove children whose parents don't appear in the list
	for (int indexIdx = 0; indexIdx < (int) layerIndices.size(); indexIdx++)
	{
		PSDLayerInfo* layerInfo = mPSDLayerInfoVector[layerIndices[indexIdx]];
		layerSet.insert(layerInfo->mIdx);		
	}
	
	
	for (int indexIdx = 0; indexIdx < (int) layerIndices.size(); indexIdx++)
	{
		PSDLayerInfo* layerInfo = mPSDLayerInfoVector[layerIndices[indexIdx]];
		
		PSDLayerInfo* parentCheck = layerInfo->mParent;
		bool includeLayer = true;

		if (!layerInfo->mBaseClipping)
		{
			int baseIdx = layerInfo->mIdx + 1;
			while (baseIdx < (int) mPSDLayerInfoVector.size())
			{
				PSDLayerInfo* baseCheck = mPSDLayerInfoVector[baseIdx];
				if (baseCheck->mBaseClipping)
				{
					includeLayer &= baseCheck->mVisible;
					break;
				}
				baseIdx++;
			}
		}

		while (parentCheck != NULL)
		{
			includeLayer &= layerSet.find(parentCheck->mIdx) != layerSet.end();
			parentCheck = parentCheck->mParent;
		}

		if (!includeLayer)
			layerSet.erase(layerSet.find(layerInfo->mIdx));				
	}

	std::vector<int> aLayerIndices;

	// Composite
	IntSet::reverse_iterator revItr = layerSet.rbegin();
	while (revItr != layerSet.rend())
	{
		//PSDLayerInfo* layerInfo = mPSDLayerInfoVector[*revItr];

		aLayerIndices.push_back(*revItr);
		revItr++;
	}
	
	ImageData* combinedImage = MergeLayers(NULL, aLayerIndices, NULL);

	Texture* texture = gBFApp->mRenderDevice->LoadTexture(combinedImage, false);
	*ofsX = combinedImage->mX;
	*ofsY = combinedImage->mY;	
	if (dynamic_cast<PSDLayerInfo*>(combinedImage) == NULL)
		delete combinedImage;	
	return texture;
}

///

PSDLayerInfo::PSDLayerInfo()
{
	mLayerId = 0;
	mSectionDividerType = 0;
	mSectionBlendMode = 0;	
	mPSDDescriptor = NULL;
	mRefX = 0;
	mRefY = 0;
	mFillOpacity = 255;
	mImageEffects = new ImageEffects();
	mKnockout = 0;
	mLayerMask = NULL;
	mLayerMaskEnabled = false;
	mLayerMaskInverted = false;
	mLayerMaskDefault = 255;
	mLayerMaskX = 0;
	mLayerMaskY = 0;
	mLayerMaskWidth = 0;
	mLayerMaskHeight = 0;
	mBlendInteriorEffectsAsGroup = false;
	mBlendClippedElementsAsGroup = true;
	mTransparencyShapesLayer = true;
	mLayerMaskHidesEffects = false;
	mVectorMaskHidesEffects = false;		
	mBlendingRangeSourceStart = 0;
	mBlendingRangeSourceEnd = 0xFFFFFFFF;
	mBlendingRangeDestStart = 0;
	mBlendingRangeDestEnd = 0xFFFFFFFF;
	mChannelMask = 0xFFFFFFFF;
	mImageAdjustment = NULL;
	mVectorMask = NULL;
}

PSDLayerInfo::~PSDLayerInfo()
{
	delete mPSDDescriptor;
	delete mLayerMask;
	delete mVectorMask;
	delete mImageEffects;
}

bool PSDLayerInfo::ReadData()
{
	AutoPerf gPerf("PSDLayerInfo::ReadData");

	FileStream* fS = mPSDReader->mFS;

	fS->SetPos(mImageDataStart);

	mBits = new uint32[mWidth*mHeight];
	memset(mBits, 0, mWidth*mHeight*sizeof(uint32));
	int aSize = mWidth*mHeight;
	if (mChannels.size() < 4)
	{
		for (int i = 0; i < aSize; i++)
			mBits[i] = 0xFF000000;
	}
	else
	{
		for (int i = 0; i < aSize; i++)
			mBits[i] = 0x00000000;
	}

		
	short* rowLengths = new short[std::max(mLayerMaskHeight, mHeight)];
		
	for (int channelIdx = 0; channelIdx < (int) mChannels.size(); channelIdx++)
	{
		PSDChannelInfo* channel = &mChannels[channelIdx];

		if (channel->mId == -2)
		{
			mLayerMask = new uint8[mLayerMaskWidth*mLayerMaskHeight];

			int compression = fS->ReadInt16();
			if (compression == 0)
			{
				for (int pos = 0; pos < aSize; pos++)
					mLayerMask[pos] = fS->ReadInt8();
			}
			else
			{				
				for (int aY = 0; aY < mLayerMaskHeight; aY++)
					rowLengths[aY] = fS->ReadInt16();

				for (int aY = 0; aY < mLayerMaskHeight; aY++)
				{
					int pos = aY * mLayerMaskWidth;
					int readSize = rowLengths[aY];
			
					while (readSize > 0)
					{
						int chunkSize = fS->ReadInt8();
						readSize--;
						if (chunkSize >= 0)
						{
							// String of literal data
							chunkSize++;
							readSize -= chunkSize;
							while (chunkSize > 0)
							{						
								mLayerMask[pos++] = fS->ReadInt8();
								chunkSize--;
							}						
						}
						else if (chunkSize > -128)
						{
							// One byte repeated
							chunkSize = 1 - chunkSize;
							uint8 aData = fS->ReadInt8();
							readSize--;
							while (chunkSize > 0)
							{
								mLayerMask[pos++] = aData;							
								chunkSize--;
							}						
						}
					}
				}
			}

			if (!mLayerMaskEnabled)
			{
				delete mLayerMask;
				mLayerMask = NULL;
			}
		}
		else
		{
			int shift = (2 - channel->mId) * 8;

			int compression = fS->ReadInt16();
			if (compression == 0)
			{
				for (int pos = 0; pos < aSize; pos++)
					mBits[pos] |= ((uint32) (uint8) fS->ReadInt8()) << shift;
			}
			else
			{				
				int maxSize = 0;
				for (int aY = 0; aY < mHeight; aY++)
				{
					rowLengths[aY] = fS->ReadInt16();
					maxSize = std::max(maxSize, (int) rowLengths[aY]);
				}				
				uint8* rowData = new uint8[maxSize];

				for (int aY = 0; aY < mHeight; aY++)
				{					
					int pos = aY * mWidth;
					int readSize = rowLengths[aY];
					int readPos = 0;			

					int fileOfs = fS->GetPos();
					fS->Read(rowData, readSize);					

					/*auto FixAlpha = [&]()
					{
						if (channel->mId == -1)
						{
							int fixedAlpha = std::min(255, rowData[readPos] * 0xFF / 0xB8);
							fS->SetPos(fileOfs + readPos);
							fS->Write((uint8)fixedAlpha);
							fS->SetPos(fileOfs + readSize);
						}
					};*/

					while (readPos < readSize)
					{
						int chunkSize = (int8) rowData[readPos++];						
						if (chunkSize >= 0)
						{
							// String of literal data
							chunkSize++;							
							while (chunkSize > 0)
							{						
								//FixAlpha();
								mBits[pos++] |= ((uint32) rowData[readPos++]) << shift;							
								chunkSize--;
							}						
						}
						else if (chunkSize > -128)
						{
							// One byte repeated
							chunkSize = 1 - chunkSize;
							//FixAlpha();
							uint32 aData = ((uint32) rowData[readPos++]) << shift;							
							while (chunkSize > 0)
							{
								mBits[pos++] |= aData;
								chunkSize--;
							}						
						}
					}
				}

				delete rowData;
			}
		}				
	}

	delete rowLengths;

	if (((mLayerMask != NULL) || (mVectorMask != NULL)) && (!mLayerMaskHidesEffects))
		ApplyMask(this);

 	uint32* ptr = mBits;
	for (int i = 0; i < aSize; i++)
	{
		uint32 aColor = *ptr;

		int a = (aColor & 0xFF000000) >> 24;
		int r = (aColor & 0x00FF0000) >> 16;
		int g = (aColor & 0x0000FF00) >> 8 ;
		int b = (aColor & 0x000000FF)      ;
			
		*(ptr++) = (a << 24) | (b << 16) | (g << 8) | r;
	}
	
	return true;
}

void PSDLayerInfo::ApplyVectorMask(ImageData* imageData)
{
	/*if (mVectorMask == NULL)
		return;

	agg::rendering_buffer g_alpha_mask_rbuf;
	uint8* aBuffer = new uint8[mWidth*mHeight];
    g_alpha_mask_rbuf.attach(aBuffer, mWidth, mHeight, mWidth);

	typedef agg::pixfmt_gray8 pixfmt;
	typedef agg::renderer_base<pixfmt> renderer_base;	
	typedef agg::renderer_scanline_aa_solid<renderer_base> renderer_scanline;
	typedef agg::scanline_u8 scanline;
	
	agg::rasterizer_scanline_aa<> g_rasterizer;

	pixfmt pixf(g_alpha_mask_rbuf);	
	
	agg::path_storage m_path;
	agg::gamma_lut<> m_gamma;

	agg::renderer_base<pixfmt> ren_base(pixf);
    ren_base.clear(agg::gray8(0));
    renderer_scanline ren(ren_base);
	agg::scanline_p8 sl;

	agg::ellipse ell;
	//renderer r(rb);

    int i;
    for(i = 0; i < 10; i++)
    {
        ell.init(rand() % mWidth, 
                    rand() % mHeight, 
                    rand() % 100 + 20, 
                    rand() % 100 + 20,
                    100);

        g_rasterizer.add_path(ell);
        ren.color(agg::gray8(rand() & 0xFF, rand() & 0xFF));
        agg::render_scanlines(g_rasterizer, sl, ren);
    }

	int aSize = mWidth*mHeight;

	for (int i = 0; i < aSize; i++)
	{		
		uint8 aMask = aBuffer[i];
		PackedColor* aColor = (PackedColor*) (&mBits[i]);
		aColor->a = (aColor->a * aMask) / 255;
		aColor->a = 255;
		aColor->g = 128;
	}

	delete aBuffer;*/
}

void PSDLayerInfo::ApplyMask(ImageData* imageData)
{
	AutoPerf gPerf("PSDLayerInfo::ApplyMask");

	ApplyVectorMask(imageData);

	if (mLayerMask == NULL)
		return;

	int maskStartX = std::max(mLayerMaskX, imageData->mX);
	int maskStartY = std::max(mLayerMaskY, imageData->mY);
	int maskEndX = std::min(mLayerMaskX + mLayerMaskWidth, imageData->mX + imageData->mWidth);
	int maskEndY = std::min(mLayerMaskY + mLayerMaskHeight, imageData->mY + imageData->mHeight);

	for (int y = maskStartY; y < maskEndY; y++)
	{
		uint32* colorBits = imageData->mBits + (y - imageData->mY)*imageData->mWidth;
		uint8* maskBits = mLayerMask + (y - mLayerMaskY)*mLayerMaskWidth;

		for (int x = maskStartX; x < maskEndX; x++)
		{
			uint8 aMask = maskBits[x - mLayerMaskX];
			PackedColor* aColor = (PackedColor*) (&colorBits[x - imageData->mX]);
			aColor->a = (aColor->a * aMask) / 255;
		}
	}

	if (mLayerMaskDefault != 255)
	{
		// Clear out alpha on borders

		// Top
		for (int y = imageData->mY; y < maskStartY; y++)
		{
			uint32* colorBits = imageData->mBits + (y - imageData->mY)*imageData->mWidth;				
			for (int x = imageData->mX; x < imageData->mX + imageData->mWidth; x++)
			{
				PackedColor* aColor = (PackedColor*) (&colorBits[x - imageData->mX]);
				aColor->a = 0;
			}
		}

		// Bottom
		for (int y = mLayerMaskY + mLayerMaskHeight; y < imageData->mY + imageData->mHeight; y++)
		{
			uint32* colorBits = imageData->mBits + (y - imageData->mY)*imageData->mWidth;				
			for (int x = imageData->mX; x < imageData->mX + imageData->mWidth; x++)
			{
				PackedColor* aColor = (PackedColor*) (&colorBits[x - imageData->mX]);
				aColor->a = 0;
			}
		}

		// Left
		for (int y = imageData->mY; y < imageData->mY + imageData->mHeight; y++)
		{
			uint32* colorBits = imageData->mBits + (y - imageData->mY)*imageData->mWidth;				
			for (int x = imageData->mX; x < mLayerMaskX; x++)
			{
				PackedColor* aColor = (PackedColor*) (&colorBits[x - imageData->mX]);
				aColor->a = 0;
			}
		}

		// Right
		for (int y = imageData->mY; y < imageData->mY + imageData->mHeight; y++)
		{
			uint32* colorBits = imageData->mBits + (y - imageData->mY)*imageData->mWidth;				
			for (int x = mLayerMaskX + mLayerMaskWidth; x < imageData->mX + imageData->mWidth; x++)
			{
				PackedColor* aColor = (PackedColor*) (&colorBits[x - imageData->mX]);
				aColor->a = 0;
			}
		}
	}
}



