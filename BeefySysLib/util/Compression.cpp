#include "Compression.h"
#include "third_party/zlib/zlib.h"
#include "TLSingleton.h"

#pragma warning(disable:4190)

USING_NS_BF;

static TLSingleton<Array<uint8>> gCompression_TLDataReturn;

bool Compression::Compress(Span<uint8> inData, Array<uint8>& outData)
{
	outData.Reserve(128);

	z_stream zs;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;	
	zs.avail_in = (int)inData.mSize;
	zs.next_in = inData.mVals;
	zs.next_out = outData.mVals;
	zs.avail_out = outData.mAllocSize;
	
	deflateInit(&zs, Z_BEST_COMPRESSION);

	bool isDone = false;
	bool hadError = false;

	while (true)
	{
		bool isDone = zs.avail_in == 0;
		
		int err = deflate(&zs, isDone ? Z_FINISH : Z_NO_FLUSH);
		outData.mSize = (int)(zs.next_out - outData.mVals);

		if (err < 0)
		{
			hadError = true;
			break;
		}

		if ((isDone) && (err == Z_STREAM_END))
			break;

		if (zs.avail_out == 0)
		{
			outData.Reserve((int)outData.mAllocSize + (int)outData.mAllocSize / 2 + 1);
			zs.next_out = outData.mVals + outData.mSize;
			zs.avail_out = outData.mAllocSize - outData.mSize;
		}		
	}
	
	deflateEnd(&zs);	
	return !hadError;
}

bool Compression::Decompress(Span<uint8> inData, Array<uint8>& outData)
{
	outData.Reserve(128);

	z_stream zs;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	zs.avail_in = (int)inData.mSize;
	zs.next_in = inData.mVals;
	zs.next_out = outData.mVals;
	zs.avail_out = outData.mAllocSize;

	inflateInit(&zs);

	bool isDone = false;
	bool hadError = false;

	while (true)
	{
		bool isDone = zs.avail_in == 0;

		int err = inflate(&zs, isDone ? Z_FINISH : Z_NO_FLUSH);
		outData.mSize = (int)(zs.next_out - outData.mVals);

		if (err < 0)
		{
			hadError = true;
			break;
		}

		if ((isDone) && (err == Z_STREAM_END))
			break;
		
		if (zs.avail_out == 0)
		{
			outData.Reserve((int)outData.mAllocSize + (int)outData.mAllocSize / 2 + 1);
			zs.next_out = outData.mVals + outData.mSize;
			zs.avail_out = outData.mAllocSize - outData.mSize;
		}
	}

	inflateEnd(&zs);
	return !hadError;
}

//////////////////////////////////////////////////////////////////////////

BF_EXPORT bool BF_CALLTYPE Compression_Compress(void* ptr, intptr size, void** outPtr, intptr* outSize)
{	
	auto& outData = *gCompression_TLDataReturn.Get();
	outData.Reserve(128);
	if (!Compression::Compress(Span<uint8>((uint8*)ptr, size), outData))	
		return false;	
	*outPtr = outData.mVals;	
	*outSize = outData.mSize;	
	return true;
}

BF_EXPORT bool BF_CALLTYPE Compression_Decompress(void* ptr, intptr size, void** outPtr, intptr* outSize)
{
	auto& outData = *gCompression_TLDataReturn.Get();
	outData.Reserve(128);
	if (!Compression::Decompress(Span<uint8>((uint8*)ptr, size), outData))
		return false;
	*outPtr = outData.mVals;
	*outSize = outData.mSize;
	return true;
}
