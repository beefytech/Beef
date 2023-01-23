#define XMD_H

#include "JPEGData.h"
#include "MemStream.h"
#include "ImageUtils.h"

#include <setjmp.h>

extern "C"
{
#include "jpeg/jpeglib.h"
#include "jpeg/jerror.h"
#include "jpeg/jpegint.h"
}


USING_NS_BF;

// -----------------------------------------------------------------------
// JPEG error handling
// Will "longjmp" on error_exit.
// -----------------------------------------------------------------------

struct ErrorHandler
{
	/** "subclass" of jpeg_error_mgr */
	struct jpeg_error_mgr errorMgr;
	jmp_buf setjmpBuffer;

	ErrorHandler( j_decompress_ptr cinfo )
	{
		Init( (j_common_ptr)cinfo );
	}

	ErrorHandler( j_compress_ptr cinfo )
	{
		Init( (j_common_ptr)cinfo );
	}

	void Init( j_common_ptr cinfo )
	{
		// setup the standard error handling.
		cinfo->err = jpeg_std_error( &errorMgr );

		// then hook up our error_exit function.
		errorMgr.error_exit = &ErrorHandler::OnErrorExit;
	}

	static void OnErrorExit( j_common_ptr cinfo )
	{
		// recover the pointer to "derived class" instance
		ErrorHandler* errorHandler = (ErrorHandler*)cinfo->err;

		// use the default error message output.
		(*cinfo->err->output_message)( cinfo );

		// return control to the setjmp point.
		longjmp( errorHandler->setjmpBuffer, 1 );
	}
};

struct JpegMemSource
{
	JpegMemSource(JPEGData* buffer, j_decompress_ptr cinfo )
	{
		struct jpeg_source_mgr * src;

		if ( cinfo->src == NULL )
		{
			// Have the jpeg library allocate the source manager object so
			// that it is automatically deallocated by the decompressor.
			cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small)( (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr) );
		}

		src = cinfo->src;
		src->init_source = &JpegMemSource::InitSource;
		src->fill_input_buffer = &JpegMemSource::FillInputBuffer;
		src->skip_input_data = &JpegMemSource::SkipInputData;
		src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
		src->term_source = &JpegMemSource::TermSource;
		src->bytes_in_buffer = (size_t) buffer->mSrcDataLen;
		src->next_input_byte = (JOCTET *) buffer->mSrcData;
	}

	~JpegMemSource()
	{
		// The setjmp/longjmp action in ErrorHandler can actually
		// completely skip the destruction of this C++ data source wrapper,
		// but that is OK so long as there is no actual cleanup to do.
	}

	static void InitSource( j_decompress_ptr cinfo )
	{
		/* no work necessary here */
	}

	static boolean FillInputBuffer( j_decompress_ptr cinfo )
	{
		static JOCTET mybuffer[4];

		/* The whole JPEG data is expected to reside in the supplied memory
			* buffer, so any request for more data beyond the given buffer size
			* is treated as an error.
			*/
		WARNMS(cinfo, JWRN_JPEG_EOF);
		/* Insert a fake EOI marker */
		mybuffer[0] = (JOCTET) 0xFF;
		mybuffer[1] = (JOCTET) JPEG_EOI;

		cinfo->src->next_input_byte = mybuffer;
		cinfo->src->bytes_in_buffer = 2;

		return TRUE;
	}

	static void SkipInputData( j_decompress_ptr cinfo, long num_bytes )
	{
		struct jpeg_source_mgr * src = cinfo->src;

		/* Just a dumb implementation for now.  Could use fseek() except
			* it doesn't work on pipes.  Not clear that being smart is worth
			* any trouble anyway --- large skips are infrequent.
			*/
		if (num_bytes > 0) {
			while (num_bytes > (long) src->bytes_in_buffer) {
				num_bytes -= (long) src->bytes_in_buffer;
				(void) (*src->fill_input_buffer) (cinfo);
				/* note we assume that fill_input_buffer will never return FALSE,
					* so suspension need not be handled.
					*/
			}
			src->next_input_byte += (size_t) num_bytes;
			src->bytes_in_buffer -= (size_t) num_bytes;
		}
	}

	static void TermSource( j_decompress_ptr cinfo )
	{
		/* no work necessary here */
	}

};

#define JPEGMEMDEST_BLOCK_SIZE 16384

struct JpegMemDest : jpeg_destination_mgr
{
	Array<uint8> mData;

	JpegMemDest(JPEGData* buffer, j_compress_ptr cinfo)
	{
		if (cinfo->dest == NULL)
		{
			cinfo->dest = this;
		}

		init_destination = &InitDestination;
		empty_output_buffer = &EmptyOutputBuffer;
		term_destination = &TermDestination;
	}

	~JpegMemDest()
	{
	}

	static void InitDestination(j_compress_ptr cinfo)
	{
		auto self = (JpegMemDest*)cinfo->dest;
		self->mData.Resize(JPEGMEMDEST_BLOCK_SIZE);
		cinfo->dest->next_output_byte = &self->mData[0];
		cinfo->dest->free_in_buffer = self->mData.size();
	}

	static boolean EmptyOutputBuffer(j_compress_ptr cinfo)
	{
		auto self = (JpegMemDest*)cinfo->dest;
		size_t oldsize = self->mData.size();
		self->mData.Resize(oldsize + JPEGMEMDEST_BLOCK_SIZE);
		cinfo->dest->next_output_byte = &self->mData[oldsize];
		cinfo->dest->free_in_buffer = self->mData.size() - oldsize;
		return true;
	}

	static void TermDestination(j_compress_ptr cinfo)
	{
		auto self = (JpegMemDest*)cinfo->dest;
		self->mData.Resize(self->mData.size() - cinfo->dest->free_in_buffer);
	}
};

bool JPEGData::ReadData()
{
	jpeg_decompress_struct cinfo;
	ErrorHandler err( &cinfo );
	if ( setjmp( err.setjmpBuffer ) )
	{
		// ErrorHandler::OnErrorExit will longjmp back to here from
		// within the ReadImage call below.
		jpeg_destroy_decompress( &cinfo );
		return false;
	}

	jpeg_create_decompress( &cinfo );
	JpegMemSource(this, &cinfo );

	jpeg_read_header( &cinfo, TRUE );
	jpeg_start_decompress( &cinfo );

	mWidth = cinfo.output_width;
	mHeight = cinfo.output_height;
	mBits = new uint32[ mWidth * mHeight ];
	uint32* destPtr = mBits;

	// Have the jpeg library allocate a scan-line buffer as a
	// per-image resource so that it will be automatically deallocated
	// by jpeg_finish_decompress.
	int row_stride = cinfo.output_width * cinfo.output_components;
	unsigned char** scanline = (*cinfo.mem->alloc_sarray)( (j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1 );

	if ( cinfo.output_components == 1 )
	{
		while ( cinfo.output_scanline < cinfo.output_height )
		{
			jpeg_read_scanlines( &cinfo, scanline, 1 );

			uint8* p = *scanline;
			for ( JDIMENSION i = 0; i < cinfo.output_width; ++i )
			{
				int r = *p++;
				*destPtr++ = 0xFF000000 | (r << 16) | (r << 8) | (r);
			}
		}
	}
	else
	{
		while ( cinfo.output_scanline < cinfo.output_height )
		{
			jpeg_read_scanlines(&cinfo, scanline, 1 );

			uint8* p = *scanline;
			for ( JDIMENSION i = 0; i < cinfo.output_width; ++i )
			{
				int r = *p++;
				int g = *p++;
				int b = *p++;
				*destPtr++ = 0xFF000000 | (r << 16) | (g << 8) | (b);
			}
		}
	}

	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );

	return true;
}

void JPEGData::Compress(int quality)
{
	jpeg_compress_struct cinfo;

	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	ErrorHandler err(&cinfo);
	if (setjmp(err.setjmpBuffer))
	{
		// ErrorHandler::OnErrorExit will longjmp back to here from
		// within the ReadImage call below.
		jpeg_destroy_compress(&cinfo);
		return;
	}
	JSAMPROW row_pointer[1];

	JpegMemDest jpegMemDest(this, &cinfo);

	//jpeg_stdio_dest(&cinfo, outfile);

	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	 * Four fields of the cinfo struct must be filled in:
	 */
	cinfo.image_width = mWidth; 	/* image width and height, in pixels */
	cinfo.image_height = mHeight;
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
	/* Now use the library's routine to set default compression parameters.
	 * (You must set at least cinfo.in_color_space before calling this,
	 * since the defaults depend on the source color space.)
	 */
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	 * Here we just illustrate the use of quality (quantization table) scaling:
	 */
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	 * Pass TRUE unless you are very sure of what you're doing.
	 */
	jpeg_start_compress(&cinfo, TRUE);

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	 * loop counter, so that we don't have to keep track ourselves.
	 * To keep things simple, we pass one scanline per call; you can pass
	 * more if you wish, though.
	 */
	int row_stride = mWidth * 3;	/* JSAMPLEs per row in image_buffer */

	uint8* line = new uint8[mWidth * 3];
	row_pointer[0] = (JSAMPROW)line;

	while (cinfo.next_scanline < cinfo.image_height)
	{
		uint8* src = (uint8*)&mBits[cinfo.next_scanline * mWidth];

		uint8* dest = line;
		for (int x = 0; x < mWidth; x++)
		{
			*(dest++) = src[2];
			*(dest++) = src[1];
			*(dest++) = src[0];
			src += 4;
		}

		(void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	delete line;

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);
	/* After finish_compress, we can close the output file. */
	//fclose(outfile);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

	mSrcDataLen = (int)jpegMemDest.mData.size();
	mSrcData = new uint8[mSrcDataLen];
	memcpy(mSrcData, &jpegMemDest.mData[0], mSrcDataLen);
}