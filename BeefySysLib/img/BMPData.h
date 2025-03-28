#pragma once

#include "ImageData.h"

NS_BF_BEGIN;

struct bmp_palette_element_s
{
	unsigned char blue;
	unsigned char green;
	unsigned char red;
	unsigned char reserved;	/* alpha ? */
};
typedef struct bmp_palette_element_s bmp_palette_element_t;

class BMPData : public ImageData
{
public:
	int						mReadPos;
	bool					mHasTransFollowing;

	int						Read(void* ptr, int elemSize, int elemCount);
	unsigned char			ReadC();

	bool					ReadPixelsRLE8(bmp_palette_element_t* palette);
	bool					ReadPixelsRLE4(bmp_palette_element_t* palette);
	bool					ReadPixels32();
	bool					ReadPixels24();
	bool					ReadPixels16();
	bool					ReadPixels8(bmp_palette_element_t* palette);
	bool					ReadPixels4(bmp_palette_element_t* palette);
	bool					ReadPixels1(bmp_palette_element_t* palette);

public:
	BMPData();

	bool					ReadData();
	bool					WriteToFile(const StringImpl& path);
};

NS_BF_END;
