#include "Common.h"
#include "BFApp.h"
#include "img/PSDReader.h"
#include "gfx/RenderDevice.h"
#include "gfx/Texture.h"
#include "util/PerfTimer.h"
#include "util/TLSingleton.h"
#include "img/JPEGData.h"

#pragma warning(disable:4190)

USING_NS_BF;

static TLSingleton<String> gResLib_TLStrReturn;

BF_EXPORT PSDReader* BF_CALLTYPE Res_OpenPSD(const char* fileName)
{
	//gPerfManager->StartRecording();	

	PSDReader* aPSDReader = new PSDReader();
	if (!aPSDReader->Init(fileName))
	{
		delete aPSDReader;
		return NULL;
	}

	return aPSDReader;
}

BF_EXPORT void BF_CALLTYPE Res_DeletePSDReader(PSDReader* pSDReader)
{
	delete pSDReader;

	gPerfManager->StopRecording();
	gPerfManager->DbgPrint();
}

BF_EXPORT TextureSegment* BF_CALLTYPE Res_PSD_GetLayerTexture(PSDReader* pSDReader, int layerIdx, int* ofsX, int* ofsY)
{
	Texture* texture = pSDReader->LoadLayerTexture(layerIdx, ofsX, ofsY);
	if (texture == NULL)
		return NULL;

	TextureSegment* textureSegment = new TextureSegment();		
	textureSegment->InitFromTexture(texture);
	return textureSegment;
}

BF_EXPORT TextureSegment* BF_CALLTYPE Res_PSD_GetMergedLayerTexture(PSDReader* pSDReader, int* layerIndices, int count, int* ofsX, int* ofsY)
{
	std::vector<int> aLayerIndices;
	aLayerIndices.insert(aLayerIndices.begin(), layerIndices, layerIndices + count);

	Texture* texture = pSDReader->LoadMergedLayerTexture(aLayerIndices, ofsX, ofsY);
	if (texture == NULL)
		return NULL;

	TextureSegment* textureSegment = new TextureSegment();		
	textureSegment->InitFromTexture(texture);
	return textureSegment;
}

BF_EXPORT int BF_CALLTYPE Res_PSD_GetLayerCount(PSDReader* pSDReader)
{
	return (int) pSDReader->mPSDLayerInfoVector.size();
}

BF_EXPORT PSDLayerInfo* BF_CALLTYPE Res_PSD_GetLayerInfo(PSDReader* pSDReader, int layerIdx)
{
	return pSDReader->mPSDLayerInfoVector[layerIdx];
}

BF_EXPORT void BF_CALLTYPE Res_PSDLayer_GetSize(PSDLayerInfo* layerInfo, int* x, int* y, int* width, int* height)
{	
	*x = layerInfo->mX;
	*y = layerInfo->mY;
	*width = layerInfo->mWidth;
	*height = layerInfo->mHeight;	
}

BF_EXPORT int BF_CALLTYPE Res_PSDLayer_GetLayerId(PSDLayerInfo* layerInfo)
{	
	return layerInfo->mLayerId;
}

BF_EXPORT const char* BF_CALLTYPE Res_PSDLayer_GetName(PSDLayerInfo* layerInfo)
{	
	return layerInfo->mName.c_str();	
}

BF_EXPORT int BF_CALLTYPE Res_PSDLayer_IsVisible(PSDLayerInfo* layerInfo)
{
	return layerInfo->mVisible ? 1 : 0;
}

///

BF_EXPORT StringView BF_CALLTYPE Res_JPEGCompress(uint32* bits, int width, int height, int quality)
{
	String& outString = *gResLib_TLStrReturn.Get();
	JPEGData jpegData;
	jpegData.mBits = bits;
	jpegData.mWidth = width;
	jpegData.mHeight = height;
	jpegData.Compress(quality);
	jpegData.mBits = NULL;
	outString.Clear();
	outString.Insert(0, (char*)jpegData.mSrcData, jpegData.mSrcDataLen);
	return outString;
}