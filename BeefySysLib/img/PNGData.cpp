#include "PNGData.h"
#include "third_party/png/png.h"

USING_NS_BF;

#pragma warning(disable:4996)

static void PNGError(png_structp ptr, png_const_charp err)
{
	OutputDebugStrF("PNG ERROR: %s\r\n", err);
}

static void png_buffer_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
	PNGData* aData = (PNGData*)png_ptr->io_ptr;
	png_size_t bytesAvailable = aData->mSrcDataLen - aData->mReadPos;
	png_size_t bytesToRead = std::min( length, bytesAvailable );	
	memcpy(data, aData->mSrcData + aData->mReadPos, length);
	
	aData->mReadPos += (int)length;

	if ( bytesToRead != length )
	{
		png_error( png_ptr, "Read Error" );
	}
}

PNGData::PNGData()
{
	mReadPos = 0;
}

bool PNGData::ReadData()
{
	mReadPos = 0;

	png_uint_32 width, height;
	int bit_depth, color_type, interlace_type;

	png_structp png_ptr;
	png_infop info_ptr;
		
	png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	if (png_ptr == NULL)
	{
		return false;
	}
		
	png_set_read_fn( png_ptr, (png_voidp)this, &png_buffer_read_data );
	png_ptr->error_fn = PNGError;
		
	/* Allocate/initialize the memory for image information.  REQUIRED. */
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		return false;
	}
		
	/* Set error handling if you are using the setjmp/longjmp method (this is
		* the normal method of doing things with libpng).  REQUIRED unless you
		* set up your own error handlers in the png_create_read_struct() earlier.
		*/
	if (setjmp(png_ptr->jmpbuf))
	{
		/* Free all of the memory associated with the png_ptr and info_ptr */
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		/* If we get here, we had a problem reading the file */
		return false;
	}
		
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
					&interlace_type, NULL, NULL);
		
	/* Add filler (or alpha) byte (before/after each RGB triplet) */
	png_set_expand(png_ptr);
#ifdef BF_PLATFORM_BIG_ENDIAN
	png_set_filler(png_ptr, 0xff, PNG_FILLER_BEFORE);
	png_set_swap_alpha(png_ptr);
#else
	png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
#endif
		
	//png_set_gray_1_2_4_to_8(png_ptr);
	png_set_palette_to_rgb(png_ptr);
	png_set_gray_to_rgb(png_ptr);
		
#ifndef PLATFORM_BIG_ENDIAN
	png_set_bgr(png_ptr);
#endif
		
	//	int numBytes = png_get_rowbytes(png_ptr, info_ptr) * height / 4;
	uint32* bits = new uint32[width*height];
	uint32* addr = bits;

	//if (info_ptr->interlace_type == PNG_INTERLACE_ADAM7)
	//{
	//	uint32* rowData = new uint32[width + 1];
	//	memset(rowData, 0xBF, (width + 1) * sizeof(uint32));

	//	static const int passOffsets[7][3] = /* startOfs, colOfs, rowOfs */
	//	{
	//		{0,			8, 8}, // 1
	//		{4,			8, 8}, // 2
	//		{4 * width,	4, 8}, // 3
	//		{2,			4, 4}, // 4
	//		{2 * width, 2, 4}, // 5
	//		{1,			2, 2}, // 6
	//		{width,		1, 2}, // 7
	//	};

	//	for (int pass = 0; pass < 7; pass++)
	//	{			
	//		uint32* bitsWriteRow = bits + passOffsets[pass][0];
	//		int colOfs = passOffsets[pass][1];
	//		int rowOfs = passOffsets[pass][2];

	//		int virtWidth = (width + 7) & ~7;
	//		int virtHeight = (height + 7) & ~7;

	//		int colCount = (width - (passOffsets[pass][0] % width)) / colOfs; 
	//		int rowCount = (width * height - passOffsets[pass][0]) / (rowOfs * width);

	//		int skipRow = passOffsets[pass][0] / width;
	//		int virtRowCount = (virtHeight - skipRow) / rowOfs;
	//		
	//		for (int row = 0; row < rowCount; row++)
	//		{
	//			memset(rowData, 0xBF, (width + 1) * sizeof(uint32));
	//			png_read_rows(png_ptr, (png_bytepp)&rowData, NULL, 1);

	//			uint32* bitsRead = rowData;
	//			uint32* bitsWrite = bitsWriteRow;				
	//			for (int col = 0; col < colCount; col++)
	//			{	
	//				BF_ASSERT((bitsWrite < bits + width * height) && (bitsWrite >= bits));
	//				*bitsWrite = *(bitsRead++);
	//				bitsWrite += colOfs;
	//			}

	//			//BF_ASSERT(*(bitsRead - 1) != 0xCDCDCDCD);
	//			//BF_ASSERT(*bitsRead == 0xCDCDCDCD);

	//			bitsWriteRow += rowOfs * width;
	//		}

	//		/*for (int row = rowCount; row < virtRowCount; row++)
	//		{
	//			// Extra rows to ignore
	//			png_read_rows(png_ptr, (png_bytepp)&rowData, NULL, 1);
	//		}*/
	//	}

	//	delete rowData;
	//}
	//else
	//{
	//	for (png_uint_32 i = 0; i < height; i++)
	//	{
	//		png_read_rows(png_ptr, (png_bytepp) &addr, NULL, 1);
	//		addr += width;
	//	}
	//}

	BF_ASSERT(height < 16 * 1024);

	uint32* rowPtrs[16 * 1024];
	for (int row = 0; row < (int)height; row++)
		rowPtrs[row] = &bits[row * width];
	png_read_image(png_ptr, (png_bytepp)rowPtrs);
		
	/* read rest of file, and get additional chunks in info_ptr - REQUIRED */
	png_read_end(png_ptr, info_ptr);
		
	/* clean up after the read, and free any memory allocated - REQUIRED */
	png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		
	mWidth = width;
	mHeight = height;
	mBits = bits;			

	SwapRAndB();

	return true;
}

bool PNGData::WriteToFile(const StringImpl& path)
{
	png_structp png_ptr;
	png_infop info_ptr;

	FILE *fp;

	if ((fp = fopen(path.c_str(), "wb")) == NULL)
		return false;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
		NULL, NULL, NULL);

	if (png_ptr == NULL)
	{
		fclose(fp);
		return false;
	}

	// Allocate/initialize the memory for image information.  REQUIRED.
	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == NULL)
	{
		fclose(fp);
		png_destroy_write_struct(&png_ptr, (png_infopp) NULL);
		return false;
	}

	// Set error handling if you are using the setjmp/longjmp method (this is
	// the normal method of doing things with libpng).  REQUIRED unless you
	// set up your own error handlers in the png_create_write_struct() earlier.

	if (setjmp(png_ptr->jmpbuf))
	{
		// Free all of the memory associated with the png_ptr and info_ptr
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		// If we get here, we had a problem writing the file
		return false;
	}

	png_init_io(png_ptr, fp);

	png_color_8 sig_bit;
	sig_bit.red = 8;
	sig_bit.green = 8;
	sig_bit.blue = 8;
	/* if the image has an alpha channel then */
	sig_bit.alpha = 8;
	png_set_sBIT(png_ptr, info_ptr, &sig_bit);
	png_set_bgr(png_ptr);

	png_set_IHDR(png_ptr, info_ptr, mWidth, mHeight, 8, PNG_COLOR_TYPE_RGB_ALPHA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(png_ptr, info_ptr);

	SwapRAndB();
	for (int i = 0; i < mHeight; i++)
	{
		png_bytep aRowPtr = (png_bytep) (mBits + i*mWidth);
		png_write_rows(png_ptr, &aRowPtr, 1);
	}
	SwapRAndB();

	// write rest of file, and get additional chunks in info_ptr - REQUIRED
	png_write_end(png_ptr, info_ptr);

	// clean up after the write, and free any memory allocated - REQUIRED
	png_destroy_write_struct(&png_ptr, &info_ptr);

	// close the file
	fclose(fp);

	return true;
}
