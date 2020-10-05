#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/img/PSDReader.h"
#include "BeefySysLib/img/PNGData.h"

USING_NS_BF;

void Resize(ImageData* srcImage, int srcX, int srcY, int srcScale, ImageData* destImage, int destX, int destY, int scale)
{
	if (srcScale > scale)
	{
		struct Color
		{
			uint8 mElems[4];
		};

		struct ColorT
		{
			float mElems[4];
		};

		ColorT colorT = { 0 };

		int sampleScale = srcScale / scale;
		float totalDiv = 0;
		for (int srcYOfs = 0; srcYOfs < sampleScale; srcYOfs++)
		{
			for (int srcXOfs = 0; srcXOfs < sampleScale; srcXOfs++)
			{
				auto color = *(Color*)&srcImage->mBits[(srcX - srcImage->mX + srcXOfs) + (srcY - srcImage->mY + srcYOfs)*srcImage->mWidth];

				totalDiv += 1.0f;

				float alpha = color.mElems[3];
				colorT.mElems[0] += color.mElems[0] * alpha;
				colorT.mElems[1] += color.mElems[1] * alpha;
				colorT.mElems[2] += color.mElems[2] * alpha;
				colorT.mElems[3] += alpha;
			}
		}

		Color outColor;
		float alpha = colorT.mElems[3] / totalDiv;
		float valScale = 0;
		if (alpha > 0)
			valScale = 1 / alpha / totalDiv;
		outColor.mElems[0] = (int)round(colorT.mElems[0] * valScale);
		outColor.mElems[1] = (int)round(colorT.mElems[1] * valScale);
		outColor.mElems[2] = (int)round(colorT.mElems[2] * valScale);
		outColor.mElems[3] = (int)round(alpha);

		destImage->mBits[destX + destY * destImage->mWidth] = *(uint32*)&outColor;
	}
	else
	{
		uint32 color = srcImage->mBits[(srcX - srcImage->mX) + (srcY - srcImage->mY)*srcImage->mWidth];
		destImage->mBits[destX + destY * destImage->mWidth] = color;
	}
}

void ConvImage(const StringImpl& baseName, int wantSize)
{
	PNGData srcImage;
	bool success = srcImage.LoadFromFile(baseName + "_4.png");
	BF_ASSERT(success);

	int srcScale = 4;
	int destScale = 1 << wantSize;

	PNGData destImage;
	destImage.CreateNew(srcImage.mWidth * destScale / srcScale, srcImage.mHeight * destScale / srcScale);
	for (int destY = 0; destY < destImage.mHeight; destY++)
	{
		for (int destX = 0; destX < destImage.mWidth; destX++)
		{
			Resize(&srcImage, destX * srcScale / destScale, destY * srcScale / destScale, srcScale, &destImage, destX, destY, destScale);
		}
	}
	String destName;
	if (wantSize == 0)
		destName = baseName + ".png";
	else
		destName = baseName + StrFormat("_%d.png", destScale);

	success = destImage.WriteToFile(destName);
	BF_ASSERT(success);
}

int main()
{
	int baseWidth = 0;
	int baseHeight = 0;

	bool isThemeDir = false;

	PSDReader readers[2][3];	
	ImageData* imageDatas[2][3] = { NULL };	

	for (int pass = 0; pass < 2; pass++)
	{
		for (int size = 0; size < 3; size++)
		{
			auto& reader = readers[pass][size];
			auto& imageData = imageDatas[pass][size];

			String fileName;

			if (pass == 0)
			{
				if (size == 0)
					fileName = "DarkUI.psd";
				else if (size == 1)
					fileName = "DarkUI_2.psd";
				else
					fileName = "DarkUI_4.psd";

				if ((!FileExists(fileName)) && (size == 0))
				{
					isThemeDir = true;
					fileName = "../../images/" + fileName;
				}
			}
			else
			{
				if (size == 0)
					fileName = "UI.psd";
				else if (size == 1)
					fileName = "UI_2.psd";
				else
					fileName = "UI_4.psd";

				if (!FileExists(fileName))
					continue;
			}			

			if (!reader.Init(fileName))
			{
				if (size == 0)
				{
					printf("Failed to open %s - incorrect working directory?", fileName.c_str());
					return 1;
				}

				imageData = NULL;
				continue;
			}

			if ((size == 0) && (pass == 0))
			{
				baseWidth = reader.mWidth;
				baseHeight = reader.mHeight;
			}

			std::vector<int> layerIndices;
			for (int layerIdx = 0; layerIdx < (int)reader.mPSDLayerInfoVector.size(); layerIdx++)
			{
				auto layer = reader.mPSDLayerInfoVector[layerIdx];
				if (layer->mVisible)
					layerIndices.insert(layerIndices.begin(), layerIdx);
			}
			
			imageData = reader.ReadImageData();
			if (imageData == NULL)
			{
				ImageData* rawImageData = reader.MergeLayers(NULL, layerIndices, NULL);;
				if ((rawImageData->mX == 0) && (rawImageData->mY == 0) &&
					(rawImageData->mWidth == reader.mWidth) &&
					(rawImageData->mHeight == reader.mHeight))
				{
					imageData = rawImageData;
				}
				else
				{
					imageData = new ImageData();
					imageData->CreateNew(reader.mWidth, reader.mHeight);
					imageData->CopyFrom(rawImageData, 0, 0);
					delete rawImageData;
				}
			}
// 			else
// 			{
// 				PNGData pngData;
// 				pngData.mWidth = imageData->mWidth;
// 				pngData.mHeight = imageData->mHeight;
// 				pngData.mBits = imageData->mBits;
// 				pngData.WriteToFile("c:\\temp\\test.png");
// 				pngData.mBits = NULL;
// 			}
		}
	}

	int numCols = baseWidth / 20;
	int numRows = baseHeight / 20;
	
	auto _HasImage = [&](int col, int row, int pass, int size)
	{
		int scale = 1 << size;
		auto srcImage = imageDatas[pass][size];
		if (srcImage == NULL)
			return false;
		
		int xStart = (col * 20 * scale);
		int yStart = (row * 20 * scale);

		if ((xStart < srcImage->mX) || (xStart + 20 * scale > srcImage->mX + srcImage->mWidth))
			return false;
		if ((yStart < srcImage->mY) || (yStart + 20 * scale > srcImage->mY + srcImage->mHeight))
			return false;

		for (int yOfs = 0; yOfs < 20 * scale; yOfs++)
		{
			for (int xOfs = 0; xOfs < 20 * scale; xOfs++)
			{
				int srcX = xStart + xOfs;
				int srcY = yStart + yOfs;
				auto color = srcImage->mBits[(srcX - srcImage->mX) + (srcY - srcImage->mY)*srcImage->mWidth];				
				if ((color & 0xFF000000) != 0)
					return true;
			}
		}			
		return false;
	};		

	auto _HasUniqueThemeImage = [&](int col, int row, int size)
	{
		int scale = 1 << size;
		auto srcImage0 = imageDatas[0][size];
		if (srcImage0 == NULL)
			return false;

		auto srcImage1 = imageDatas[1][size];
		if (srcImage1 == NULL)
			return false;

		int xStart = (col * 20 * scale);
		int yStart = (row * 20 * scale);

		if ((xStart < srcImage1->mX) || (xStart + 20 * scale > srcImage1->mX + srcImage1->mWidth))
			return false;
		if ((yStart < srcImage1->mY) || (yStart + 20 * scale > srcImage1->mY + srcImage1->mHeight))
			return false;

		for (int yOfs = 0; yOfs < 20 * scale; yOfs++)
		{
			for (int xOfs = 0; xOfs < 20 * scale; xOfs++)
			{
				int srcX = xStart + xOfs;
				int srcY = yStart + yOfs;
				auto color0 = srcImage0->mBits[(srcX - srcImage0->mX) + (srcY - srcImage0->mY)*srcImage0->mWidth];
				auto color1 = srcImage1->mBits[(srcX - srcImage1->mX) + (srcY - srcImage1->mY)*srcImage1->mWidth];

				if ((color0 & 0xFF000000) == 0)
					color0 = 0;
				if ((color1 & 0xFF000000) == 0)
					color1 = 0;

				if (color0 != color1)
					return true;
			}
		}
		return false;
	};

	for (int size = 0; size < 3; size++)
	{
		int scale = 1 << size;
		int outWidth = baseWidth * scale;
		int outHeight = baseHeight * scale;

		PNGData pngData;
		pngData.CreateNew(outWidth, outHeight);	
	
		if ((size < 2) && (!isThemeDir))
		{
			ConvImage("IconError", size);
			ConvImage("IconWarning", size);
		}

		String fileName;
		if (isThemeDir)			
		{
			if (size == 0)
				fileName = "UI.png";
			else if (size == 1)
				fileName = "UI_2.png";
			else
				fileName = "UI_4.png";
		}
		else
		{
			if (size == 0)
				fileName = "DarkUI.png";
			else if (size == 1)
				fileName = "DarkUI_2.png";
			else
				fileName = "DarkUI_4.png";
		}

		for (int row = 0; row < numRows; row++)		
		{
			for (int col = 0; col < numCols; col++)
			{				
				int srcPass = 0;
				int srcSize = size;

				if (_HasImage(col, row, 1, size)) // Theme has image of appropriate size
				{
					srcPass = 1;
					srcSize = size;
				}
				else if (_HasUniqueThemeImage(col, row, 2)) // Use resized theme image
				{
					srcPass = 1;
					srcSize = 2;
				}
				else if (_HasUniqueThemeImage(col, row, 1)) // Use resized theme image
				{					
					srcPass = 1;
					srcSize = 2;
				}
				else if (_HasUniqueThemeImage(col, row, 0)) // Use resized theme image instead
				{					
					srcPass = 1;
					srcSize = 0;
				}
				else if (_HasImage(col, row, 0, size)) // Use original image
				{					
					srcPass = 0;
					srcSize = size;
				}				
				else if (_HasImage(col, row, 0, 2)) // Use resized original image
				{
					srcPass = 0;
					srcSize = 2;
				}
				else if (_HasImage(col, row, 0, 1)) // Use resized original image
				{
					srcPass = 0;
					srcSize = 1;
				}
				else // Use resized original image
				{
					srcPass = 0;
					srcSize = 0;
				}
				
				int srcScale = 1 << srcSize;
				for (int yOfs = 0; yOfs < 20 * scale; yOfs++)
				{
					for (int xOfs = 0; xOfs < 20 * scale; xOfs++)
					{						
						int destX = (col * 20 * scale) + xOfs;
						int destY = (row * 20 * scale) + yOfs;

						int srcX = (col * 20 * srcScale) + xOfs * srcScale / scale;
						int srcY = (row * 20 * srcScale) + yOfs * srcScale / scale;
						
						auto srcImage = imageDatas[srcPass][srcSize];
						if ((srcX >= srcImage->mX) && (srcY >= srcImage->mY) &&
							(srcX < srcImage->mX + srcImage->mWidth) &&
							(srcY < srcImage->mY + srcImage->mHeight))
						{
							Resize(srcImage, srcX, srcY, srcScale, &pngData, destX, destY, scale);									
						}
					}
				}
			}
		}

		bool success = pngData.WriteToFile(fileName);
		BF_ASSERT(success);
	}	
}
