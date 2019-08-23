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

	PSDReader readers[3];
	ImageData* imageDatas[3];
	for (int size = 0; size < 3; size++)
	{
		auto& reader = readers[size];
		auto& imageData = imageDatas[size];

		String fileName;		
		if (size == 0)
			fileName = "DarkUI.psd";
		else if (size == 1)
			fileName = "DarkUI_2.psd" ;
		else
			fileName = "DarkUI_4.psd";
					
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

		if (size == 0)
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

		imageData = reader.MergeLayers(NULL, layerIndices, NULL);
	}

	int numCols = baseWidth / 20;
	int numRows = baseHeight / 20;

	auto _HasImage = [&](int col, int row, int size)
	{
		int scale = 1 << size;
		auto srcImage = imageDatas[size];
		if (srcImage == NULL)
			return false;
				
		for (int yOfs = 0; yOfs < 20 * scale; yOfs++)
		{
			for (int xOfs = 0; xOfs < 20 * scale; xOfs++)
			{
				int srcX = (col * 20 * scale) + xOfs;
				int srcY = (row * 20 * scale) + yOfs;
				auto color = srcImage->mBits[(srcX - srcImage->mX) + (srcY - srcImage->mY)*srcImage->mWidth];
				if (color != 0)
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
	
		if (size < 2)
		{
			ConvImage("IconError", size);
			ConvImage("IconWarning", size);
		}

		String fileName;
		if (size == 0)
			fileName = "DarkUI.png";
		else if (size == 1)
			fileName = "DarkUI_2.png";
		else
			fileName = "DarkUI_4.png";

		for (int col = 0; col < numCols; col++)
		{
			for (int row = 0; row < numRows; row++)
			{
				if ((size == 2) && (col == 11) && (row == 7))
				{
					NOP;
				}

				int srcSize = size;
				if (!_HasImage(col, row, size))
				{
					if (_HasImage(col, row, 2))
					{
						srcSize = 2;
					}
					else
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
						
						auto srcImage = imageDatas[srcSize];
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
