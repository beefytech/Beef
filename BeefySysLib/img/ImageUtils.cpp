#include "ImageUtils.h"
#include "ImageData.h"
#include "Common.h"
#include "util/PerfTimer.h"

USING_NS_BF;

const int DISSOLVE_SIZE = 6679;
static uint8 gDissolveTable[DISSOLVE_SIZE];
static bool gDissolveInitialized = false;
static int gDissolveIdx = 0;
static int gSqrtTable[256];

struct WideColor
{
	int r;
	int g;
	int b;
	int a;
};

static inline float clamp(float val, float min, float max)
{
	return (val <= min) ? min : (val >= max) ? max : val;
}

static inline int clamp(int val, int min, int max)
{
	return (val <= min) ? min : (val >= max) ? max : val;
}

#define Blend_Apply(Blend_Channel) \
	aBlendedColor.r = Blend_Channel((int) aDestColor->r, (int) aSrcColor->r); \
	aBlendedColor.g = Blend_Channel((int) aDestColor->g, (int) aSrcColor->g); \
	aBlendedColor.b = Blend_Channel((int) aDestColor->b, (int) aSrcColor->b);

inline PackedColor SetSat(PackedColor color, int satVal)
{
	#define SetSatComponents(minComp, midComp, maxComp) \
		{ \
			midComp -= minComp; \
			maxComp -= minComp; \
			minComp = 0; \
			if (maxComp > 0) \
			{ \
				midComp = midComp*satVal/maxComp; \
				maxComp = satVal; \
			} \
		}

	if (color.r <= color.g) 
	{
		if (color.g <= color.b)
			SetSatComponents(color.r, color.g, color.b)
		else if (color.r < color.b)
			SetSatComponents(color.r, color.b, color.g)
		else
			SetSatComponents(color.b, color.r, color.g);					
	} 
	else 
	{
		if (color.r <= color.b) 
			SetSatComponents(color.g, color.r, color.b)
		else if (color.g < color.b)
			SetSatComponents(color.g, color.b, color.r)
		else 
			SetSatComponents(color.b, color.g, color.r);					
	}

	return color;
}

#define max3( x, y, z ) ( std::max((x), std::max((y), (z))) )				
#define min3( x, y, z ) ( std::min((x), std::min((y), (z))) )
#define Sat(C) ( (max3(((C).r), ((C).g), ((C).b)) - min3(((C).r), ((C).g), ((C).b))) )
#define Lum(C) ( ((((C).r) * 300) + (((C).g) * 586) + (((C).b) * 113)) / 1000 )


inline PackedColor SetLum(PackedColor color, int lum)
{

	//int lum_cl = luminance(color);
	int d = lum - Lum(color);
	
	WideColor color_cl = {color.r + d, color.g + d, color.b + d, color.a};

	int aLum = Lum(color_cl);

	int mini = min3(color_cl.r, color_cl.g, color_cl.b);
	int maxi = max3(color_cl.r, color_cl.g, color_cl.b);
	if (mini < 0) 
	{		
		color.r = (int) aLum + (color_cl.r - aLum)*aLum/std::max(aLum - mini, 1);
		color.g = (int) aLum + (color_cl.g - aLum)*aLum/std::max(aLum - mini, 1);
		color.b = (int) aLum + (color_cl.b - aLum)*aLum/std::max(aLum - mini, 1);
	}
	else if (maxi > 255) 
	{
		color.r = aLum + (color_cl.r - aLum)*(255 - aLum)/std::max(maxi - aLum, 1);
		color.g = aLum + (color_cl.g - aLum)*(255 - aLum)/std::max(maxi - aLum, 1);
		color.b = aLum + (color_cl.b - aLum)*(255 - aLum)/std::max(maxi - aLum, 1);
	}
	else
	{
		color.r = color_cl.r;
		color.g = color_cl.g;
		color.b = color_cl.b;
	}
	
	return color;
}

inline int ClampedOffset(int cur, int to, int maxDelta)
{
	int delta = abs(to - cur);
	if (abs(delta) < maxDelta)
		return to;
	if (cur < to)
		return cur + maxDelta;
	return cur - maxDelta;	
}

ImageData* Beefy::CreateResizedImageUnion(ImageData* src, int x, int y, int width, int height)
{
	int minX = std::min(src->mX, x);
	int minY = std::min(src->mY, y);
	int maxX = std::max(src->mX + src->mWidth, x + width);
	int maxY = std::max(src->mY + src->mHeight, y + height);

	/*if ((src->mX == minX) && (src->mY == minY) && (src->mWidth == maxX - minX) && (src->mHeight == maxY - minY))
		return src;*/

	ImageData* imageData = new ImageData();
	imageData->CreateNew(maxX - minX, maxY - minY);
	imageData->mX = minX;
	imageData->mY = minY;

	for (int y = src->mY; y < src->mY + src->mHeight; y++)
	{
		for (int x = src->mX; x < src->mX + src->mWidth; x++)
		{			
			PackedColor* aDest = (PackedColor*) &imageData->mBits[(x - imageData->mX) + (y - imageData->mY)*imageData->mWidth];
			PackedColor* aSrc = (PackedColor*) &src->mBits[(x - src->mX) + (y - src->mY)*src->mWidth];
			*aDest = *aSrc;
		}
	}

	return imageData;	
}

ImageData* Beefy::CreateEmptyResizedImageUnion(ImageData* src, int x, int y, int width, int height)
{
	ImageData* imageData = new ImageData();

	if (src == NULL)
	{
		imageData->CreateNew(x, y, width, height);
		return imageData;
	}

	int minX = std::min(src->mX, x);
	int minY = std::min(src->mY, y);
	int maxX = std::max(src->mX + src->mWidth, x + width);
	int maxY = std::max(src->mY + src->mHeight, y + height);
		
	imageData->CreateNew(maxX - minX, maxY - minY);
	imageData->mX = minX;
	imageData->mY = minY;

	return imageData;	
}

void Beefy::CrossfadeImage(ImageData* origImage, ImageData* newImage, float opacity)
{	
	AutoPerf gPerf("Beefy::CrossfadeImage");

	int a = (int) (opacity * 255 + 0.5f);
	int oma = 255 - a;

	if (a == 255) // No crossfade needed
		return;

	if (origImage == NULL)
	{
		for (int y = newImage->mY; y < newImage->mY + newImage->mHeight; y++)
		{
			for (int x = newImage->mX; x < newImage->mX + newImage->mWidth; x++)
			{
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				aDest->a = (aDest->a * a) / 255;
				aDest++;
			}
		}
	}
	else
	{		
		for (int x = newImage->mX; x < newImage->mX + newImage->mWidth; x++)
		{
			// Top
			for (int y = newImage->mY; y < origImage->mY; y++)
			{
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				aDest->a = (aDest->a * a) / 255;
				aDest++;
			}

			// Bottom
			for (int y = origImage->mY + origImage->mHeight; y < newImage->mY + newImage->mHeight; y++)
			{			
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				aDest->a = (aDest->a * a) / 255;
				aDest++;
			}
		}

		// Crash seems to be here:
		for (int y = origImage->mY; y < origImage->mY + origImage->mHeight; y++)
		{
			// Left
			for (int x = newImage->mX; x < origImage->mX; x++)
			{			
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				aDest->a = (aDest->a * a) / 255;
				aDest++;
			}

			// Right
			for (int x = origImage->mX + origImage->mWidth; x < newImage->mX + newImage->mWidth; x++)
			{			
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				aDest->a = (aDest->a * a) / 255;
				aDest++;
			}
		}

		for (int y = origImage->mY; y < origImage->mY + origImage->mHeight; y++)
		{
			for (int x = origImage->mX; x < origImage->mX + origImage->mWidth; x++)
			{			
				PackedColor* aDest = (PackedColor*) &newImage->mBits[(x - newImage->mX) + (y - newImage->mY)*newImage->mWidth];
				PackedColor* aSrc = (PackedColor*) &origImage->mBits[(x - origImage->mX) + (y - origImage->mY)*origImage->mWidth];

				int newDestAlpha = ((aDest->a * a) + (aSrc->a * oma)) / 255;
				
				if (newDestAlpha != 0)
				{
					int ca = (255 * a * aDest->a) / (aDest->a * a + aSrc->a * oma);					
					int coma = 255 - ca;

					aDest->a = newDestAlpha;
					aDest->r = ((aDest->r * ca) + (aSrc->r * coma)) / 255;
					aDest->g = ((aDest->g * ca) + (aSrc->g * coma)) / 255;
					aDest->b = ((aDest->b * ca) + (aSrc->b * coma)) / 255;
				}
								
				aSrc++;
				aDest++;
			}
		}
	}
}

// aSrcColor, aDestColor, aBlendAlpha
// doReverseBlend, doFullBlend, doAltBlend, normal blend

class BlendGetColor_Normal
{
public:
	PackedColor operator()(PackedColor* srcColor, PackedColor* destColor, int srcAlpha, int blendAlpha)
	{
		PackedColor aBlendedColor = *srcColor;
		aBlendedColor.a = srcAlpha;
		return aBlendedColor;
	}
};

class BlendMix_Normal
{
public:
	void operator()(PackedColor* srcColor, PackedColor* destColor, PackedColor blendedColor, int srcAlpha, int blendAlpha)
	{
		int newDestAlpha = 255;
		int a = blendedColor.a;
					
		if (destColor->a != 255)
			newDestAlpha = destColor->a + ((255 - destColor->a) * blendedColor.a) / 255;

		if (newDestAlpha != 0)
		{						
			a = 255 * blendedColor.a / newDestAlpha;
						
			int ba = destColor->a;
			int boma = 255 - destColor->a;
			blendedColor.r = ((blendedColor.r * ba) + (srcColor->r * boma)) / 255;
			blendedColor.g = ((blendedColor.g * ba) + (srcColor->g * boma)) / 255;
			blendedColor.b = ((blendedColor.b * ba) + (srcColor->b * boma)) / 255;
		}
					
		int oma = 255 - a;
		destColor->a = newDestAlpha;
		destColor->r = ((blendedColor.r * a) + (destColor->r * oma)) / 255;
		destColor->g = ((blendedColor.g * a) + (destColor->g * oma)) / 255;
		destColor->b = ((blendedColor.b * a) + (destColor->b * oma)) / 255;
	}
};

template <class GetColorFunctor, class MixFunctor>
static void BlendImage_Fast_T(ImageData* dest, ImageData* src, int destX, int destY, float alpha, int mixType, bool fullAlpha)
{
	AutoPerf gPerf("Beefy::BlendImage");

	BF_ASSERT(destX >= 0);
	BF_ASSERT(destY >= 0);
	BF_ASSERT(destX + src->mWidth <= dest->mWidth);
	BF_ASSERT(destY + src->mHeight <= dest->mHeight);

	if (!gDissolveInitialized)
	{
		for (int i = 0; i < DISSOLVE_SIZE; i++)
			gDissolveTable[i] = rand() % 1023;
		gDissolveInitialized = true;

		for (int i = 0; i < 256; i++)		
			gSqrtTable[i] = (int) (sqrt(i / 255.0f) * 255.0f + 0.5f);		
	}

	int aBlendAlpha = (int) (255 * alpha);	

	for (int y = 0; y < src->mHeight; y++)
	{
		PackedColor* aSrcColor = (PackedColor*) (src->mBits + (y * src->mWidth));
		PackedColor* aDestColor = (PackedColor*) (dest->mBits + destX + (y + destY) * dest->mWidth);

		for (int x = 0; x < src->mWidth; x++)
		{	
			int aSrcAlpha = (int) (aSrcColor->a * alpha + 0.5f);

			if (fullAlpha)
				aSrcAlpha = (int) (alpha * 255 + 0.5f);

			PackedColor aBlendedColor = GetColorFunctor()(aSrcColor, aDestColor, aSrcAlpha, aBlendAlpha);
			MixFunctor()(aSrcColor, aDestColor, aBlendedColor, aSrcAlpha, aBlendAlpha);
				
			aDestColor++;
			aSrcColor++;
		}
	}
}

static void BlendImage_Fast(ImageData* dest, ImageData* src, int destX, int destY, float alpha, int mixType, bool fullAlpha)
{
	BlendImage_Fast_T<BlendGetColor_Normal, BlendMix_Normal>(dest, src, destX, destY, alpha, mixType, fullAlpha);
}

void Beefy::BlendImage(ImageData* dest, ImageData* src, int destX, int destY, float alpha, int mixType, bool fullAlpha)
{	
	/*BlendImage_Fast(dest, src, destX, destY, alpha, mixType, fullAlpha);
	return;*/

	AutoPerf gPerf("Beefy::BlendImage");

	BF_ASSERT(destX >= 0);
	BF_ASSERT(destY >= 0);
	BF_ASSERT(destX + src->mWidth <= dest->mWidth);
	BF_ASSERT(destY + src->mHeight <= dest->mHeight);

	if (!gDissolveInitialized)
	{
		for (int i = 0; i < DISSOLVE_SIZE; i++)
			gDissolveTable[i] = rand() % 1023;
		gDissolveInitialized = true;

		for (int i = 0; i < 256; i++)		
			gSqrtTable[i] = (int) (sqrt(i / 255.0f) * 255.0f + 0.5f);		
	}

	int aBlendAlpha = (int) (255 * alpha);	

	for (int y = 0; y < src->mHeight; y++)
	{
		PackedColor* aSrcColor = (PackedColor*) (src->mBits + (y * src->mWidth));
		PackedColor* aDestColor = (PackedColor*) (dest->mBits + destX + (y + destY) * dest->mWidth);

		for (int x = 0; x < src->mWidth; x++)
		{	
			int aSrcAlpha = (int) (aSrcColor->a * alpha + 0.5f);

			if (fullAlpha)
				aSrcAlpha = (int) (alpha * 255 + 0.5f);

			PackedColor aBlendedColor = *aSrcColor;
			aBlendedColor.a = aSrcAlpha;
			bool doReverseBlend = false;
			bool doAltBlend = false;
			bool doFullBlend = false;
			
			if ((mixType == 'Nrml') || (mixType == 'norm')) // Normal
			{				
			}
			else if ((mixType == 'Lghn') || (mixType == 'lite')) // Lighten
			{
				aBlendedColor.r = std::max((int) aSrcColor->r, (int) aDestColor->r);
				aBlendedColor.g = std::max((int) aSrcColor->g, (int) aDestColor->g);
				aBlendedColor.b = std::max((int) aSrcColor->b, (int) aDestColor->b);
			}
			else if ((mixType == 'Drkn') || (mixType == 'dark')) // Darken
			{
				aBlendedColor.r = std::min((int) aSrcColor->r, (int) aDestColor->r);
				aBlendedColor.g = std::min((int) aSrcColor->g, (int) aDestColor->g);
				aBlendedColor.b = std::min((int) aSrcColor->b, (int) aDestColor->b);
			}
			else if (mixType == 'blen')
			{
				aBlendedColor.r = std::max(0, (int) aDestColor->r - (int) aSrcColor->r);
				aBlendedColor.g = std::max(0, (int) aDestColor->g - (int) aSrcColor->g);
				aBlendedColor.b = std::max(0, (int) aDestColor->b - (int) aSrcColor->b);
			}
			else if ((mixType == 'Dslv') || (mixType == 'diss')) // Disolve
			{	
				if ((dest->mX + x == 132) && (dest->mY + y == 19))
				{
					/*aBlendedColor.a = 255;
					aBlendedColor.r = 0;
					aBlendedColor.g = 255;
					aBlendedColor.b = 0;						*/					
				}

				int dissolveIdx = ((dest->mX + x) * 179 + (dest->mY + y) * 997) % DISSOLVE_SIZE;
				if ((int) (aSrcColor->a * alpha + 0.5f) < gDissolveTable[dissolveIdx])
					aBlendedColor.a = 0;				
				else
					aBlendedColor.a = 255;				
			}
			else if ((mixType == 'Mltp') || (mixType == 'mul ')) // Multiply
			{
				aBlendedColor.r = (int) aDestColor->r * (int) aSrcColor->r / 255;
				aBlendedColor.g = (int) aDestColor->g * (int) aSrcColor->g / 255;
				aBlendedColor.b = (int) aDestColor->b * (int) aSrcColor->b / 255;
			}
			else if ((mixType == 'CBrn') || (mixType == 'idiv')) // Color Burn
			{
				if (mixType == 'CBrn') // Effect
				{					 
					int blendVal = (aBlendAlpha * aSrcColor->a / 255);
					#define Blend_ColorBurn_Effect(A,B) \
						std::max(0, 255 - (((255 - (int) A) << 8 ) / std::max((((B * blendVal) + (255 * (255 - blendVal))) / 255), 1)))

					Blend_Apply(Blend_ColorBurn_Effect);
					//doFullBlend = true;
				}
				else // Layer
				{
					#define Blend_ColorBurn(A,B) \
						std::max(0, 255 - (((255 - (int) A) << 8 ) / std::max((((B * aBlendAlpha) + (255 * (255 - aBlendAlpha))) / 255), 1)))
					
					Blend_Apply(Blend_ColorBurn);
					doFullBlend = true;
				}
			}						
			else if ((mixType == 'lnDg') || (mixType == 'lddg')) // Linear Dodge
			{
				// Uses alternate blend methdod				
			}
			else if (mixType == 'lbrn') // Linear Burn (Layer)
			{
				/*int newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aBlendedColor.a) / 255;
				#define Blend_LinearDodge_Layer(A,B) \
					255 - std::min(255, (255 - A) + (255 - B))

				Blend_Apply(Blend_LinearDodge_Layer);	*/
			}
			else if (mixType == 'lnBn')
			{
				// Uses alternate blend methdod
			}
			else if (mixType == 'dkCl') // Darker Color
			{					
				// 299, 587, 144 is "correct"
				int intensityDest = (aDestColor->r * 300) + (aDestColor->g * 586) + (aDestColor->b * 113);
				int intensitySrc = (aSrcColor->r * 300) + (aSrcColor->g * 586) + (aSrcColor->b * 113);				
				if (intensityDest <= intensitySrc)				
					doReverseBlend = true;
			}
			else if ((mixType == 'ltCl') || (mixType == 'lgCl')) // Lighter Color
			{
				if ((dest->mX + x == 125) && (dest->mY + y == 299))
				{
					/*aDestColor->a = 255;
					aDestColor->r = 0;
					aDestColor->g = 255;
					aDestColor->b = 0;						*/
					
				}

				// 299, 587, 144 is "correct"
				int intensityDest = (aDestColor->r * 300) + (aDestColor->g * 586) + (aDestColor->b * 113);
				int intensitySrc = (aSrcColor->r * 300) + (aSrcColor->g * 586) + (aSrcColor->b * 113);				
				if (intensityDest >= intensitySrc)				
					doReverseBlend = true;	
			}
			else if ((mixType == 'Scrn') || (mixType == 'scrn')) // Screen
			{
				aBlendedColor.r = 255 - ((255 - aDestColor->r) * (255 - aSrcColor->r) / 255);
				aBlendedColor.g = 255 - ((255 - aDestColor->g) * (255 - aSrcColor->g) / 255);
				aBlendedColor.b = 255 - ((255 - aDestColor->b) * (255 - aSrcColor->b) / 255);
			}
			else if ((mixType == 'CDdg') || (mixType == 'div ')) // Color Dodge
			{				
				if (mixType == 'CDdg') // Effect
				{
					int blendVal = (aBlendAlpha * aSrcColor->a / 255);

					#define Blend_ColorDodge_Effect(A,B) \
							std::min(255, ((((int) A) << 8 ) / std::max((((255 - B) * blendVal) + (255 * (255 - blendVal))) / 255, 1)))
					
					Blend_Apply(Blend_ColorDodge_Effect);					
					doFullBlend = true;
				}
				else
				{
					#define Blend_ColorDodge_Layer(A,B) \
							std::min(255, ((((int) A) << 8 ) / std::max((((255 - B) * aBlendAlpha) + (255 * (255 - aBlendAlpha))) / 255, 1)))
					
					Blend_Apply(Blend_ColorDodge_Layer);					
					doFullBlend = true;
				}
			}
			else if ((mixType == 'Ovrl') || (mixType == 'over')) // Overlay
			{
				#define ChannelBlend_Overlay(A,B) \
					(A < 128) ? \
						(2 * A * B / 255) : \
						(255 - (2 * (255 - A) * (255 - B) / 255))

				Blend_Apply(ChannelBlend_Overlay);
			}
			else if ((mixType == 'SftL') || (mixType == 'sLit')) // Soft Light
			{				
				#define ChannelBlend_SoftLight(A,B) \
					(B < 128) ? (A - (255 - clamp(2 * B, 0, 255))*A*(255 - A)/255/255) : \
					(A < 64) ? A + ((2*B - 255) * ((((16*A - 12*255)*A/255 + 4*255) * A / 255) - A) / 255) : \
					A + ((2*B - 255) * (gSqrtTable[A] - A) / 255)

				Blend_Apply(ChannelBlend_SoftLight);				
			}
			else if ((mixType == 'HrdL') || (mixType == 'hLit')) // Hard Light
			{
				#define Blend_HardLight(A,B) \
					(B < 128) ? \
						(2 * B * A / 255) : \
						(255 - (2 * (255 - B) * (255 - A) / 255))

				Blend_Apply(Blend_HardLight);				
			}
			else if ((mixType == 'vivL') || (mixType == 'vLit')) // Vivid Light
			{
				if ((dest->mX + x == 149) && (dest->mY + y == 218))
				{
					/*aDestColor->a = 255;
					aDestColor->r = 0;
					aDestColor->g = 255;
					aDestColor->b = 0;*/
					
				}

				if (mixType == 'vivL') // Effect
				{
					int blendVal = (aBlendAlpha * aSrcColor->a / 255);
					#define Blend_VividLight_Effect(A,B) \
						(B < 128) ? \
							std::max(0, 255 - (((255 - (int) A) << 8 ) / \
								std::max((((2 * B * blendVal) + (255 * (255 - blendVal))) / 255), 1))) : \
							std::min(255, ((((int) A) << 8 ) / \
								std::max((((255 - B) * 2 * blendVal) + (255 * (255 - blendVal))) / 255, 1)))

					Blend_Apply(Blend_VividLight_Effect);
					doFullBlend = true;
				}
				else // Layer
				{
					#define Blend_VividLight_Layer(A,B) \
						(B < 128) ? \
							std::max(0, 255 - (((255 - (int) A) << 8 ) / \
								std::max(((2 * B * aBlendAlpha) + (255 * (255 - aBlendAlpha))) / 255, 1))) : \
							std::min(255, ((((int) A) << 8 ) / \
								std::max((((255 - B) * 2 * aBlendAlpha) + (255 * (255 - aBlendAlpha))) / 255, 1)))

					Blend_Apply(Blend_VividLight_Layer);
					//doAltBlend = (mixType == 'vivL');
					//doFullBlend = !doAltBlend;
					doFullBlend = true;
				}
			}
			else if (mixType == 'lLit') // Linear Light (Layer)
			{	
				// Alternate blend mode
				/*#define Blend_LinearLight_Layer(A,B) \
					(B < 128) ? \
						(255 - std::min(255, (255 - A) + (255 - B*2))) : \
						std::min(255, A + (B - 128) * 2)

				Blend_Apply(Blend_LinearLight_Layer);					*/
			}
			else if (mixType == 'linL') // Linear Light (Effect)
			{								
				int newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aBlendedColor.a) / 255;
				if (newDestAlpha > 0)
				{
					#define Blend_LinearLight(A,B) \
						(B < 128) ? \
							(255 - std::min(255, \
								(((255 - B*2) * aSrcAlpha) + ((255 - A) * aDestColor->a)) / newDestAlpha)) : \
							std::min(255, (((B - 128)*2 * aSrcAlpha) + (A * aDestColor->a)) / newDestAlpha)
					Blend_Apply(Blend_LinearLight);					
				}
				doFullBlend = true;
			}
			else if ((mixType == 'pinL') || (mixType == 'pLit')) // Pin Light
			{
				#define ChannelBlend_PinLight(A,B) \
					(B < 128) ? \
						std::min((int) A, (int) B * 2) : \
						std::max((int) A, (int) (B - 128) * 2)
				aBlendedColor.r = ChannelBlend_PinLight((int) aDestColor->r, (int) aSrcColor->r);
				aBlendedColor.g = ChannelBlend_PinLight((int) aDestColor->g, (int) aSrcColor->g);
				aBlendedColor.b = ChannelBlend_PinLight((int) aDestColor->b, (int) aSrcColor->b);
			}
			else if ((mixType == 'hdMx') || (mixType == 'hMix')) // Hard Mix
			{	
				if (mixType == 'hdMx') // Effect
				{					
					int scale = 255 * 255 / std::max(255 - (aBlendAlpha * aSrcColor->a)/255, 1);

					#define Blend_Scale(A,S) \
						(((A) - 128)*S/255+128)

					#define Blend_HardMix(A,B) \
						clamp(Blend_Scale(A,scale) - Blend_Scale(255 - B,scale) + (255 - B), 0, 255)

					Blend_Apply(Blend_HardMix);
																	
					doFullBlend = true;
				}
				else // Layer
				{
					int scale = 255 * 255 / std::max(255 - aBlendAlpha, 1);

					#define Blend_Scale(A,S) \
						(((A) - 128)*S/255+128)

					#define Blend_HardMix_Layer(A,B) \
						clamp(Blend_Scale(A,scale) - Blend_Scale(255 - B,scale) + (255 - B), 0, 255)

					Blend_Apply(Blend_HardMix_Layer);
																	
					doFullBlend = true;
				}
			}
			else if ((mixType == 'Dfrn') || (mixType == 'diff')) // Difference
			{				
				/*#define ChannelBlend_Difference(A,B) \
					abs(A - B*aBlendAlpha/255)
				Blend_Apply(ChannelBlend_Difference);
				doAltBlend = (mixType == 'Dfrn');
				doFullBlend = !doAltBlend;*/

				#define ChannelBlend_Difference(A,B) \
					abs(A - B*aSrcAlpha/255)
				Blend_Apply(ChannelBlend_Difference);
				//doAltBlend = (mixType == 'Dfrn');
				//doFullBlend = !doAltBlend;
				//doAltBlend = true;

				doFullBlend = true;
			}
			else if ((mixType == 'Xclu') || (mixType == 'smud')) // Exclusion
			{				
				#define ChannelBlend_Exclusion(A,B) \
					(A + B*aBlendAlpha/255 - 2*A*B*aBlendAlpha/255/255)
				Blend_Apply(ChannelBlend_Exclusion);						
				doAltBlend = (mixType == 'Xclu');
				doFullBlend = !doAltBlend;
			}
			else if ((mixType == 'subt') || (mixType == 'fsub')) // Subtract
			{
				if (mixType == 'fsub')
				{
					#define ChannelBlend_Subtract(A,B) \
						std::max(0, A - B)
					Blend_Apply(ChannelBlend_Subtract);									
				}
				else //TODO: Double check the Effect version here...
				{
					#define ChannelBlend_Subtract_Effect(A,B) \
						std::max(0, A - B*aBlendAlpha/255)
					Blend_Apply(ChannelBlend_Subtract_Effect);				
					doAltBlend = (mixType == 'subt');
				}
			}
			else if ((mixType == 'divi') || (mixType == 'fdiv')) // Divide
			{
				#define ChannelBlend_Divide(A,B) \
					std::min(255, A*255 / std::max(B, 1))
				Blend_Apply(ChannelBlend_Divide);				
			}
			else if ((mixType == 'H   ') || (mixType == 'hue ')) // Hue
			{
				aBlendedColor = SetLum(SetSat(*aSrcColor, Sat(*aDestColor)), Lum(*aDestColor));
				aBlendedColor.a = aSrcAlpha;
			}
			else if ((mixType == 'Strt') || (mixType == 'sat ')) // Saturation
			{
				aBlendedColor = SetLum(SetSat(*aDestColor, Sat(*aSrcColor)), Lum(*aDestColor));
				aBlendedColor.a = aSrcAlpha;
			}
			else if ((mixType == 'Clr ') || (mixType == 'colr')) // Color
			{
				aBlendedColor = SetLum(*aSrcColor, Lum(*aDestColor));
				aBlendedColor.a = aSrcAlpha;
			}
			else if ((mixType == 'Lmns') || (mixType == 'lum ')) // Luminosity
			{
				aBlendedColor = SetLum(*aDestColor, Lum(*aSrcColor));
				aBlendedColor.a = aSrcAlpha;
			}
			else
			{
				BF_FATAL("Unknown blend type");			
			}

			//if (aDestColor->a != 0)
			{					
				if ((mixType == 'lddg') || (mixType == 'lnDg')) // Linear Dodge
				{
					int newDestAlpha = 255;
					int a = aSrcAlpha;

					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aSrcAlpha) / 255;

					int blendThing = 255 - ((255 - aBlendAlpha) * aDestColor->a / 255);

					#define LinearDodge_Layer(A,B) \
						std::min(255, A + B * blendThing / 255) + ((255 - blendThing) * B) / 255
						
					WideColor blendWColor = 
						{LinearDodge_Layer((int) aDestColor->r, (int) aSrcColor->r),
						LinearDodge_Layer((int) aDestColor->g, (int) aSrcColor->g),
						LinearDodge_Layer((int) aDestColor->b, (int) aSrcColor->b),
						aSrcAlpha};

					

					if (newDestAlpha != 0)
					{
						a = 255 * blendWColor.a / newDestAlpha;
												
						int ba = aDestColor->a;
						int boma = 255 - ba;
						blendWColor.r = ((blendWColor.r * ba) + (aSrcColor->r * boma)) / 255;
						blendWColor.g = ((blendWColor.g * ba) + (aSrcColor->g * boma)) / 255;
						blendWColor.b = ((blendWColor.b * ba) + (aSrcColor->b * boma)) / 255;						
					}
					
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
															
					aDestColor->r = std::min(255, ((blendWColor.r * a) + (aDestColor->r * oma)) / 255);
					aDestColor->g = std::min(255, ((blendWColor.g * a) + (aDestColor->g * oma)) / 255);
					aDestColor->b = std::min(255, ((blendWColor.b * a) + (aDestColor->b * oma)) / 255);
				}
				else if ((mixType == 'lbrn') || (mixType == 'lnBn')) // Linear Burn
				{	
					int blendThing = 255 - ((255 - aBlendAlpha) * aDestColor->a / 255);

					#define Blend_LinearDodge_Layer(A,B) \
						255 - (std::min(255, ((255 - A) + (255 - B) * blendThing / 255)) + ((255 - blendThing) * (255 - B)) / 255)
						//255 - ((255 - A) + (255 - B))

					WideColor blendWColor = 
						{Blend_LinearDodge_Layer((int) aDestColor->r, (int) aSrcColor->r),
						Blend_LinearDodge_Layer((int) aDestColor->g, (int) aSrcColor->g),
						Blend_LinearDodge_Layer((int) aDestColor->b, (int) aSrcColor->b),
						aSrcAlpha};

					int newDestAlpha = 255;
					int a = blendWColor.a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * blendWColor.a) / 255;

					if (newDestAlpha != 0)
					{						
						a = 255 * blendWColor.a / newDestAlpha;
						
						int ba = aDestColor->a;
						int boma = 255 - aDestColor->a;
						blendWColor.r = ((blendWColor.r * ba) + (aSrcColor->r * boma)) / 255;
						blendWColor.g = ((blendWColor.g * ba) + (aSrcColor->g * boma)) / 255;
						blendWColor.b = ((blendWColor.b * ba) + (aSrcColor->b * boma)) / 255;
					}
					
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = std::max(0, ((blendWColor.r * a) + (aDestColor->r * oma)) / 255);
					aDestColor->g = std::max(0, ((blendWColor.g * a) + (aDestColor->g * oma)) / 255);
					aDestColor->b = std::max(0, ((blendWColor.b * a) + (aDestColor->b * oma)) / 255);
				}
				else if ((mixType == 'lLit') || (mixType == 'linL')) // Linear Light
				{	
					int blendThing = 255 - ((255 - aBlendAlpha) * aDestColor->a / 255);

					#define LinearLight_Contrib(A,B) \
						(B < 128) ? \
							(255 - (std::min(255, ((255 - A) + (255 - B*2) * blendThing / 255)) + ((255 - blendThing) * (255 - B*2)) / 255)) : \
							(std::min(255, A + (B - 128)*2 * blendThing / 255) + ((255 - blendThing) * (B - 128)*2) / 255)
					//((B - 128)*2 + A)

					WideColor blendWColor = 
						{LinearLight_Contrib((int) aDestColor->r, (int) aSrcColor->r),
						LinearLight_Contrib((int) aDestColor->g, (int) aSrcColor->g),
						LinearLight_Contrib((int) aDestColor->b, (int) aSrcColor->b),
						aSrcAlpha};

					int newDestAlpha = 255;
					int a = blendWColor.a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * blendWColor.a) / 255;

					if (newDestAlpha != 0)
					{						
						a = 255 * blendWColor.a / newDestAlpha;
						
						int ba = aDestColor->a;
						int boma = 255 - aDestColor->a;
						blendWColor.r = ((blendWColor.r * ba) + (aSrcColor->r * boma)) / 255;
						blendWColor.g = ((blendWColor.g * ba) + (aSrcColor->g * boma)) / 255;
						blendWColor.b = ((blendWColor.b * ba) + (aSrcColor->b * boma)) / 255;
					}
					
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = clamp((((blendWColor.r * a) + (aDestColor->r * oma)) / 255), 0, 255);
					aDestColor->g = clamp((((blendWColor.g * a) + (aDestColor->g * oma)) / 255), 0, 255);
					aDestColor->b = clamp((((blendWColor.b * a) + (aDestColor->b * oma)) / 255), 0, 255);
				}				
				else if (doReverseBlend)
				{						
					int newDestAlpha = 255;
					int a = aDestColor->a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aBlendedColor.a + ((255 - aBlendedColor.a) * aDestColor->a) / 255;

					if (newDestAlpha != 0)					
						a = 255 * aDestColor->a / newDestAlpha;
											
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = ((aDestColor->r * a) + (aBlendedColor.r * oma)) / 255;
					aDestColor->g = ((aDestColor->g * a) + (aBlendedColor.g * oma)) / 255;
					aDestColor->b = ((aDestColor->b * a) + (aBlendedColor.b * oma)) / 255;
				}				
				else if (doFullBlend)
				{
					if ((dest->mX + x == 231) && (dest->mY + y == 183))
					{
						/*aBlendedColor.a = 255;
						aBlendedColor.r = 0;
						aBlendedColor.g = 255;
						aBlendedColor.b = 0;*/
						
					}

					int newDestAlpha = 255;
					int a = aBlendedColor.a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aBlendedColor.a) / 255;

					/*if (newDestAlpha != 0)
					{						
						//a = 255 * aSrcColor->a / newDestAlpha;
						
						int ba = aDestColor->a;
						int boma = 255 - ba;
						aBlendedColor.r = ((aBlendedColor.r * ba) + (aSrcColor->r * boma)) / 255;
						aBlendedColor.g = ((aBlendedColor.g * ba) + (aSrcColor->g * boma)) / 255;
						aBlendedColor.b = ((aBlendedColor.b * ba) + (aSrcColor->b * boma)) / 255;
					}*/

					if (newDestAlpha != 0)
					{						
						//a = 255 * aSrcColor->a / newDestAlpha;
						
						int ba = 255 * aDestColor->a / newDestAlpha;
						int boma = 255 - ba;
						aBlendedColor.r = ((aBlendedColor.r * ba) + (aSrcColor->r * boma)) / 255;
						aBlendedColor.g = ((aBlendedColor.g * ba) + (aSrcColor->g * boma)) / 255;
						aBlendedColor.b = ((aBlendedColor.b * ba) + (aSrcColor->b * boma)) / 255;
					//}

					//if (aBlendedColor.a
					
						/*int coma = (255 * 255 * a) / (255 * a + aDestColor->a * (255 - a));
						//int ca = 255 - aBlendedColor.a;
						//coma = gSqrtTable[coma];

						//coma = std::min(255, coma * 255);

						if (coma != 255)
						{
							
						}

						coma = 255;

						int ca = 255 - coma;

						aDestColor->a = newDestAlpha;
						aDestColor->r = ((aDestColor->r * ca) + (aBlendedColor.r * coma)) / 255;
						aDestColor->g = ((aDestColor->g * ca) + (aBlendedColor.g * coma)) / 255;
						aDestColor->b = ((aDestColor->b * ca) + (aBlendedColor.b * coma)) / 255;*/

						aDestColor->a = newDestAlpha;
						aDestColor->r = aBlendedColor.r;
						aDestColor->g = aBlendedColor.g;
						aDestColor->b = aBlendedColor.b;
					}
					else
					{
						aDestColor->a = 0;
					}

					/*int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = aBlendedColor.r;
					aDestColor->g = aBlendedColor.g;
					aDestColor->b = aBlendedColor.b;*/

					/*int newDestAlpha = 255;
					int a = aBlendedColor.a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aBlendedColor.a) / 255;
										
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = aBlendedColor.r;
					aDestColor->g = aBlendedColor.g;
					aDestColor->b = aBlendedColor.b;

					if (newDestAlpha != 0)
					{						
						a = 255 * aBlendedColor.a / newDestAlpha;
												
						int ba = aDestColor->a;
						int boma = 255 - ba;
						aBlendedColor.r = ((aDestColor->r * ba) + (aSrcColor->r * boma)) / 255;
						aBlendedColor.g = ((aDestColor->g * ba) + (aSrcColor->g * boma)) / 255;
						aBlendedColor.b = ((aDestColor->b * ba) + (aSrcColor->b * boma)) / 255;
					}*/
				}
				else if (doAltBlend)
				{
					if ((dest->mX + x == 134) && (dest->mY + y == 153))
					{
						/*aDestColor->a = 255;
						aDestColor->r = 0;
						aDestColor->g = 255;
						aDestColor->b = 0;*/
						
					}

					int newDestAlpha = 255;					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aSrcAlpha) / 255;
					
					int destContrib = (255 - aSrcColor->a)*aDestColor->a;
					int srcContrib = (255 - aDestColor->a)*aSrcColor->a;
					int blendContrib = aSrcColor->a * aDestColor->a;
					int divTot = destContrib + srcContrib + blendContrib;
					if (divTot > 0)
					{
						aDestColor->r = ((destContrib * aDestColor->r) + (srcContrib * aSrcColor->r) + (blendContrib * aBlendedColor.r)) / divTot;
						aDestColor->g = ((destContrib * aDestColor->g) + (srcContrib * aSrcColor->g) + (blendContrib * aBlendedColor.g)) / divTot;
						aDestColor->b = ((destContrib * aDestColor->b) + (srcContrib * aSrcColor->b) + (blendContrib * aBlendedColor.b)) / divTot;
						aDestColor->a = newDestAlpha;
					}
				}
				else
				{
					if ((x == 140) && (y == 83))
					{
						
					}

					int newDestAlpha = 255;
					int a = aBlendedColor.a;
					
					if (aDestColor->a != 255)
						newDestAlpha = aDestColor->a + ((255 - aDestColor->a) * aBlendedColor.a) / 255;

					if (newDestAlpha != 0)
					{						
						a = 255 * aBlendedColor.a / newDestAlpha;
						
						int ba = aDestColor->a;
						int boma = 255 - aDestColor->a;
						aBlendedColor.r = ((aBlendedColor.r * ba) + (aSrcColor->r * boma)) / 255;
						aBlendedColor.g = ((aBlendedColor.g * ba) + (aSrcColor->g * boma)) / 255;
						aBlendedColor.b = ((aBlendedColor.b * ba) + (aSrcColor->b * boma)) / 255;
					}
					
					int oma = 255 - a;
					aDestColor->a = newDestAlpha;
					aDestColor->r = ((aBlendedColor.r * a) + (aDestColor->r * oma)) / 255;
					aDestColor->g = ((aBlendedColor.g * a) + (aDestColor->g * oma)) / 255;
					aDestColor->b = ((aBlendedColor.b * a) + (aDestColor->b * oma)) / 255;
				}
			}			
				
			aDestColor++;
			aSrcColor++;
		}
	}
}

void Beefy::BlendImagesTogether(ImageData* bottomImage, ImageData* topImage, ImageData* alphaImage)
{
	for (int y = alphaImage->mY; y < alphaImage->mY + alphaImage->mHeight; y++)
	{
		PackedColor* topColor = (PackedColor*) (topImage->mBits + (alphaImage->mX - topImage->mX) + ((y - topImage->mY) * topImage->mWidth));
		PackedColor* botColor = (PackedColor*) (bottomImage->mBits + (alphaImage->mX - bottomImage->mX) + ((y - bottomImage->mY) * bottomImage->mWidth));
		PackedColor* alphaColor = (PackedColor*) (alphaImage->mBits + (alphaImage->mX - alphaImage->mX) + ((y - alphaImage->mY) * alphaImage->mWidth));

		for (int x = 0; x < alphaImage->mWidth; x++)
		{
			int a = alphaColor->a;
			int oma = 255 - a;

			int newDestAlpha = ((topColor->a * a) + (botColor->a * oma)) / 255;				
			if (newDestAlpha != 0)
			{
				int ca = (255 * topColor->a * a) / ((topColor->a * a) + (botColor->a * oma));
				int coma = 255 - ca;

				botColor->a = newDestAlpha;
				botColor->r = ((topColor->r * ca) + (botColor->r * coma)) / 255;
				botColor->g = ((topColor->g * ca) + (botColor->g * coma)) / 255;
				botColor->b = ((topColor->b * ca) + (botColor->b * coma)) / 255;
			}
			
			topColor++;
			botColor++;
			alphaColor++;
		}
	}
}

void Beefy::SetImageAlpha(ImageData* image, ImageData* alphaImage)
{
	for (int i = 0; i < image->mWidth * image->mHeight; i++)
		image->mBits[i] = image->mBits[i] & 0x00FFFFFF;

	for (int y = alphaImage->mY; y < alphaImage->mY + alphaImage->mHeight; y++)
	{
		PackedColor* aColor = (PackedColor*) (image->mBits + (alphaImage->mX - image->mX) + ((y - image->mY) * image->mWidth));		
		PackedColor* alphaColor = (PackedColor*) (alphaImage->mBits + (alphaImage->mX - alphaImage->mX) + ((y - alphaImage->mY) * alphaImage->mWidth));

		for (int x = 0; x < alphaImage->mWidth; x++)
		{
			aColor->a = alphaColor->a;
			aColor++;
			alphaColor++;
		}
	}
}

void Beefy::MultiplyImageAlpha(ImageData* image, ImageData* alphaImage)
{	
	for (int y = alphaImage->mY; y < alphaImage->mY + alphaImage->mHeight; y++)
	{
		PackedColor* aColor = (PackedColor*) (image->mBits + (alphaImage->mX - image->mX) + ((y - image->mY) * image->mWidth));		
		PackedColor* alphaColor = (PackedColor*) (alphaImage->mBits + (alphaImage->mX - alphaImage->mX) + ((y - alphaImage->mY) * alphaImage->mWidth));

		for (int x = 0; x < alphaImage->mWidth; x++)
		{
			aColor->a = aColor->a * alphaColor->a / 255;
			aColor++;
			alphaColor++;
		}
	}
}

void Beefy::SetImageAlpha(ImageData* image, int alpha)
{	
	int size = image->mWidth * image->mHeight;
	for (int i = 0; i < size; i++)
		image->mBits[i] |= 0xFF000000;	
}

void Beefy::CopyImageBits(ImageData* dest, ImageData* src)
{
	for (int y = src->mY; y < src->mY + src->mHeight; y++)
	{
		PackedColor* aDestColor = (PackedColor*) (dest->mBits + (src->mX - dest->mX) + ((y - dest->mY) * dest->mWidth));
		PackedColor* aSrcColor = (PackedColor*) (src->mBits + (src->mX - src->mX) + ((y - src->mY) * src->mWidth));
		
		for (int x = 0; x < src->mWidth; x++)
		{
			*aDestColor = *aSrcColor;
			aDestColor++;
			aSrcColor++;
		}
	}
}
