#include "BMPData.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

USING_NS_BF;

#pragma warning(disable:4996)

int	BMPData::Read(void* ptr, int elemSize, int elemCount)
{	
	int maxReadCount = (mSrcDataLen - mReadPos) / elemSize;
	if (elemCount > maxReadCount)
		elemCount = maxReadCount;
	memcpy(ptr, mSrcData + mReadPos, elemCount * elemSize);
	mReadPos += elemCount * elemSize;
	return elemCount;
}

unsigned char BMPData::ReadC()
{
	if (mReadPos >= mSrcDataLen)
		return 0;
	return mSrcData[mReadPos++];
}

BMPData::BMPData()
{
	mHasTransFollowing = false;
	mReadPos = 0;
}

#define BITMAP_MAGIC_NUMBER 19778

typedef struct bmp_file_header_s bmp_file_header_t;
struct bmp_file_header_s
{
	/*	int16_t magic_number; */ /* because of padding, we don't put it into the struct */
	int32_t size;
	int32_t app_id;
	int32_t offset;
};

typedef struct bmp_bitmap_info_header_s bmp_bitmap_info_header_t;
struct bmp_bitmap_info_header_s
{
	int32_t header_size;
	int32_t width;
	int32_t height;
	int16_t num_planes;
	int16_t bpp;
	int32_t compression;
	int32_t image_size;
	int32_t horizontal_resolution;
	int32_t vertical_resolution;
	int32_t colors_used;
	int32_t colors_important;
};

//typedef struct bmp_palette_element_s bmp_palette_element_t;


bool BMPData::ReadPixelsRLE8(bmp_palette_element_t* palette)
{
	unsigned char byte, index, i, * p;
	unsigned char x_ofs, y_ofs;
	char keepreading = 1;
	int current_line = 0;
	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	while (keepreading) {	/* in each loop, we read a pair of bytes */
		byte = ReadC();	/* read the first byte */
		if (byte) {
			index = ReadC(); /* read the second byte */
			for (i = 0; i < byte; i++) {
				*p = palette[index].red; p++;	/* unindex pixels on the fly */
				*p = palette[index].green; p++;
				*p = palette[index].blue; p++;
				*p = 0xFF; p++;					/* add alpha */
			}
		}
		else {
			byte = ReadC();	/* read the second byte */
			switch (byte)
			{
			case 0: /* skip the end of the current line and go to the next line */
				current_line++;
				p = dest + current_line * mWidth * 4;
				break;
			case 1: /* stop reading */
				keepreading = 0;
				break;
			case 2: /* skip y_ofs lines and x_ofs columns. This means that the ignored pixels will be
					filled with black (Or maybe i didn't understand the spec ?). This has already be done :
					before starting to load pixel data, we filled all the dest[] array with 0x00 by a memset() */
				x_ofs = ReadC();
				y_ofs = ReadC();
				current_line += y_ofs;
				p += y_ofs * mWidth * 4 + x_ofs * 4;
				break;
			default: /* get the next n pixels, where n = byte */
				for (i = 0; i < byte; i++) {
					index = ReadC();
					*p = palette[index].red; p++;	/* unindex pixels on the fly */
					*p = palette[index].green; p++;
					*p = palette[index].blue; p++;
					*p = 0xFF; p++;					/* add alpha */
				}
				/* if n is not a multiple of 2, then skip one byte in the file, to respect int16_t alignment */
				if (byte % 2) mReadPos++;
				break;
			}
		}
		/* the place to which p is pointing is guided by the content of the file. A corrupted file could make p point to a wrong localization.
		   Hence, we must check that we don't point outside of the dest[] array. This may prevent an error of segmentation */
		if (p >= dest + mWidth * mHeight * 4) keepreading = 0;
	}

	return true;
}

bool BMPData::ReadPixelsRLE4(bmp_palette_element_t* palette)
{
	unsigned char byte1, byte2, index1, index2, * p;
	unsigned char x_ofs, y_ofs;
	unsigned char bitmask = 0x0F; /* bit mask : 00001111 */
	char keepreading = 1;
	int current_line = 0;
	int i;

	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	while (keepreading) {
		byte1 = ReadC();
		if (byte1) {	/* encoded mode */
			byte2 = ReadC();
			index1 = byte2 >> 4;		/* get the first 4 bits of byte2 */
			index2 = byte2 & bitmask;	/* get the next 4 bits of byte2 */
			for (i = 0; i < (byte1 / 2); i++) {
				*p = palette[index1].red; p++;
				*p = palette[index1].green; p++;
				*p = palette[index1].blue; p++;
				*p = 0x0F; p++;
				*p = palette[index2].red; p++;
				*p = palette[index2].green; p++;
				*p = palette[index2].blue; p++;
				*p = 0x0F; p++;
			}
			if (byte1 % 2) {
				*p = palette[index1].red; p++;
				*p = palette[index1].green; p++;
				*p = palette[index1].blue; p++;
				*p = 0x0F; p++;
			}
		}
		else {	/* absolute mode */
			byte2 = ReadC();
			switch (byte2)
			{
			case 0: /* skip the end of the current line and go to the next line */
				current_line++;
				p = dest + current_line * mWidth * 4;
				break;
			case 1: /* stop reading */
				keepreading = 0;
				break;
			case 2: /* skip y_ofs lines and x_ofs column */
				x_ofs = ReadC();
				y_ofs = ReadC();
				current_line += y_ofs;
				p += y_ofs * mWidth * 4 + x_ofs * 4;
				break;
			default: /* get the next n pixels, where n = byte2 */
				for (i = 0; i < (byte2 / 2); i++) {
					byte1 = ReadC();
					index1 = byte1 >> 4;
					*p = palette[index1].red; p++;
					*p = palette[index1].green; p++;
					*p = palette[index1].blue; p++;
					*p = 0x0F; p++;
					index2 = byte1 & bitmask;
					*p = palette[index2].red; p++;
					*p = palette[index2].green; p++;
					*p = palette[index2].blue; p++;
					*p = 0x0F; p++;
				}
				if (byte2 % 2) {
					byte1 = ReadC();
					index1 = byte1 >> 4;
					*p = palette[index1].red; p++;
					*p = palette[index1].green; p++;
					*p = palette[index1].blue; p++;
					*p = 0x0F; p++;
				}
				if (((byte2 + 1) / 2) % 2) /* int16_t alignment */
					mReadPos += 1;					
				break;
			}
		}
		/* the place to which p is pointing is guided by the content of the file. A corrupted file could make p point to a wrong localization.
		   Hence, we must check that we don't point outside of the dest[] array. This may prevent an error of segmentation */
		if (p >= dest + mWidth * mHeight * 4) keepreading = 0;
	}

	return true;
}

bool BMPData::ReadPixels32()
{
	int i, j;
	unsigned char px[4], * p;

	unsigned char* dest = (unsigned char*)mBits;
	
	for (i = 0; i < mHeight; i++) 
	{
		p = dest + (mHeight - i - 1) * mWidth * 4;
		for (j = 0; j < mWidth; j++) 
		{
			Read(px, 4, 1); /* convert BGRX to RGBA */ 
			*p = px[2]; p++; // R
			*p = px[1]; p++;
			*p = px[0]; p++;
			*p = px[3]; p++;
		}
	}

	return true;
}

bool BMPData::ReadPixels24()
{
	int i, j;
	unsigned char px[3], * p;

	unsigned char* dest = (unsigned char*)mBits;
	
	for (i = 0; i < mHeight; i++) 
	{
		p = dest + (mHeight - i - 1) * mWidth * 4;
		for (j = 0; j < mWidth; j++) 
		{
			Read(px, 3, 1);
			*p = px[2]; p++;	/* convert BGR to RGBA */
			*p = px[1]; p++;
			*p = px[0]; p++;
			*p = 0xFF;  p++;	/* add alpha component */
		}
		if (mWidth * 3 % 4 != 0)
			mReadPos += 4 - (mWidth * 3 % 4); /* if the width is not a multiple of 4, skip the end of the line */
	}

	return true;
}

/* Expected format : XBGR   0 11111 11111 11111 */
bool BMPData::ReadPixels16()
{
	uint16_t pxl, r, g, b;
	uint16_t bitmask = 0x1F;
	unsigned char* p;
	int i, j;

	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	for (i = 0; i < mHeight; i++) 
	{
		for (j = 0; j < mWidth; j++) 
		{
			Read(&pxl, 2, 1);
			b = (pxl >> 10) & bitmask;
			g = (pxl >> 5) & bitmask;
			r = pxl & bitmask;
			*p = r * 8; p++; /* fix me */
			*p = g * 8; p++;
			*p = b * 8; p++;
			*p = 0xFF; p++;
		}
		if ((2 * mWidth) % 4 != 0)
			mReadPos += 4 - ((2 * mWidth) % 4);
	}

	return true;
}

bool BMPData::ReadPixels8(bmp_palette_element_t* palette)
{
	int i, j;
	unsigned char px, * p;

	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	for (i = 0; i < mHeight; i++) {
		for (j = 0; j < mWidth; j++) {
			Read(&px, 1, 1);
			*p = palette[px].red; p++;
			*p = palette[px].green; p++;
			*p = palette[px].blue; p++;
			*p = 0xFF; p++;
		}
		if (mWidth % 4 != 0)
			mReadPos += 4 - (mWidth % 4);
	}

	return true;
}

bool BMPData::ReadPixels4(bmp_palette_element_t* palette)
{
	int size = (mWidth + 1) / 2;			/* byte alignment */
	unsigned char* row_stride = new unsigned char[size];	/* not C90 but convenient here */
	defer({ delete row_stride; });

	unsigned char index, byte, * p;
	unsigned char bitmask = 0x0F;	/* bit mask : 00001111 */

	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	for (int i = 0; i < mHeight; i++) 
	{
		Read(row_stride, size, 1);
		for (int j = 0; j < mWidth; j++) 
		{
			byte = row_stride[j / 2];
			index = (j % 2) ? bitmask & byte : byte >> 4;
			*p = palette[index].red; p++;
			*p = palette[index].green; p++;
			*p = palette[index].blue; p++;
			*p = 0xFF; p++;
		}
		if (size % 4 != 0)
			mReadPos += 4 - (size % 4);
	}

	return true;
}

bool BMPData::ReadPixels1(bmp_palette_element_t* palette)
{
	int size = (mWidth + 7) / 8;			/* byte alignment */
	unsigned char* row_stride = new unsigned char[size];	/* not C90 but convenient here */
	defer({ delete row_stride; });

	unsigned char index, byte, * p;
	unsigned char bitmask = 0x01;	/* bit mask : 00000001 */
	int bit;

	unsigned char* dest = (unsigned char*)mBits;

	p = dest;
	for (int i = 0; i < mHeight; i++) {
		Read(row_stride, size, 1);
		for (int j = 0; j < mWidth; j++) {
			bit = (j % 8) + 1;
			byte = row_stride[j / 8];
			index = byte >> (8 - bit);
			index &= bitmask;
			*p = palette[index].red; p++;
			*p = palette[index].green; p++;
			*p = palette[index].blue; p++;
			*p = 0xFF; p++;
		}
		if (size % 4 != 0)
			mReadPos += 4 - (size % 4);
	}

	return true;
}

bool BMPData::ReadData()
{	
	int16_t magic_number;
	bmp_file_header_t file_header;
	bmp_bitmap_info_header_t info_header;
	bmp_palette_element_t* palette = NULL;	
	
	Read(&magic_number, 2, 1);
	if (magic_number == BITMAP_MAGIC_NUMBER) 
	{		
		Read((void*)&file_header, 12, 1);
		Read((void*)&info_header, 40, 1);
		mReadPos = file_header.offset;
	}	
	else
	{
		mReadPos = 0;
		Read((void*)&info_header, 40, 1);
	}
	
	/* info_header sanity checks */
	/* accepted headers : bitmapinfoheader, bitmapv4header, bitmapv5header */
	if (!(info_header.header_size == 40 || info_header.header_size == 108 || info_header.header_size == 124)) {		
		return false;
	}
	if (info_header.num_planes != 1) {		
		return false;
	}
	if (info_header.compression == 4 || info_header.compression == 5) {		
		return false;
	}
	if (info_header.height < 0) {
		return false;
	}

	/* load palette, if present */
	if (info_header.bpp <= 8) {
		mReadPos = 14 + info_header.header_size;
		if ((info_header.bpp == 1) && (info_header.colors_used == 0)) info_header.colors_used = 2;
		if ((info_header.bpp == 4) && (info_header.colors_used == 0)) info_header.colors_used = 16;
		if ((info_header.bpp == 8) && (info_header.colors_used == 0)) info_header.colors_used = 256;
		palette = (bmp_palette_element_t*)malloc(info_header.colors_used * sizeof(bmp_palette_element_t));
		if (!palette) {			
			return false;
		}
		else
			Read((void*)palette, sizeof(bmp_palette_element_t), info_header.colors_used);
	}

	/* memory allocation */
	//buf = malloc(info_header.width * info_header.height * 4);
	mWidth = info_header.width;
	mHeight = info_header.height;
	mBits = new uint32[mWidth * mHeight];
	
	memset(mBits, 0x00, info_header.width * info_header.height * 4);

	/* load image data */	
	switch (info_header.bpp)
	{
	case 32:
		ReadPixels32();
		break;
	case 24:
		ReadPixels24();
		break;
	case 16:
		ReadPixels16();
		break;
	case 8:
		if (info_header.compression == 1)
			ReadPixelsRLE8(palette);
		else
			ReadPixels8(palette);
		break;
	case 4:
		if (info_header.compression == 2)
			ReadPixelsRLE4(palette);
		else
			ReadPixels4(palette);
		break;
	case 1:
		ReadPixels1(palette);
		break;
	}

	if (info_header.colors_used) free(palette);	

	if (mHasTransFollowing)
	{
		mHeight /= 2;
		auto newBits = new uint32[mWidth * mHeight];
		memcpy(newBits, mBits + mHeight * mWidth, mWidth * mHeight * 4);
		delete mBits;
		mBits = newBits;
	}

	return true;
}

bool BMPData::WriteToFile(const StringImpl& path)
{
	FILE* file;
	int16_t magic_number;
	bmp_file_header_t file_header;
	bmp_bitmap_info_header_t info_header;
	unsigned char sample[3], * p;
	int i, j;

	file = fopen(path.c_str(), "wb");
	if (!file) 
	{		
		return false;
	}

	magic_number = BITMAP_MAGIC_NUMBER;

	file_header.size = mWidth * mHeight * 4 + 54;
	file_header.app_id = 0;
	file_header.offset = 54;

	info_header.header_size = 40;
	info_header.width = mWidth;
	info_header.height = mHeight;
	info_header.num_planes = 1;
	info_header.bpp = 24;
	info_header.compression = 0;
	info_header.image_size = mWidth * mHeight * 4;
	info_header.horizontal_resolution = 0;
	info_header.vertical_resolution = 0;
	info_header.colors_used = 0;
	info_header.colors_important = 0;

	fwrite(&magic_number, sizeof(magic_number), 1, file);
	fwrite(&file_header, sizeof(bmp_file_header_t), 1, file);
	fwrite(&info_header, sizeof(bmp_bitmap_info_header_t), 1, file);

	p = (unsigned char*)mBits;
	for (i = 0; i < mHeight; i++) 
	{
		for (j = 0; j < mWidth; j++) 
		{
			/* convert RGBA to BGR */
			sample[2] = *p; p++;
			sample[1] = *p; p++;
			sample[0] = *p; p++;
			p++;

			fwrite(sample, 3, 1, file);
		}
	}

	fclose(file);

	return 0;
}
