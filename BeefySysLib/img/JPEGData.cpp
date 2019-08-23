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