#include "RenderDevice.h"
#include "BFApp.h"
#include "Shader.h"
#include "Texture.h"
#include "gfx/DrawLayer.h"
#include "img/TGAData.h"
#include "img/PNGData.h"
#include "img/PVRData.h"
#include "img/BFIData.h"
#include "img/JPEGData.h"
#include "util/PerfTimer.h"

#include "util/AllocDebug.h"

USING_NS_BF;

RenderState::RenderState()
{
	mWriteDepthBuffer = false;
	mCullMode = CullMode_None;
	mDepthFunc = DepthFunc_Always;
	mShader = NULL;	
	mClipped = false;
}

RenderTarget::RenderTarget()
{
	mWidth = 0;
	mHeight = 0;
	mHasBeenDrawnTo = false;
	mHasBeenTargeted = false;
	mResizeNum = 0;		
}

RenderWindow::RenderWindow()
{
	mCurDrawLayer = NULL;	
	mRenderDevice = NULL;
	mWindow = NULL;
}

RenderWindow::~RenderWindow()
{	
	for (auto drawLayer : mDrawLayerList)
		delete drawLayer;
}

///

RenderDevice::RenderDevice() :
	mPooledIndexBuffers(DRAWBUFFER_IDXBUFFER_SIZE),
	mPooledVertexBuffers(DRAWBUFFER_VTXBUFFER_SIZE),
	mPooledRenderCmdBuffers(DRAWBUFFER_CMDBUFFER_SIZE)
{	
	mCurRenderState = NULL;	
	mDefaultRenderState = NULL;
	mPhysRenderState = mDefaultRenderState;
	mResizeCount = 0;
	mCurRenderTarget = NULL;		
	mCurDrawLayer = NULL;
	mPhysRenderWindow = NULL;	
}

RenderDevice::~RenderDevice()
{
	for (auto batch : mDrawBatchPool)
		delete batch;
}

void RenderDevice::AddRenderWindow(RenderWindow* renderWindow)
{
	mRenderWindowList.push_back(renderWindow);
}

void RenderDevice::RemoveRenderWindow(RenderWindow* renderWindow)
{
	mRenderWindowList.erase(std::find(mRenderWindowList.begin(), mRenderWindowList.end(), renderWindow));
}

void RenderDevice::FrameEnd()
{
}

RenderState* RenderDevice::CreateRenderState(RenderState* srcRenderState)
{
	RenderState* renderState = new RenderState();
	if (srcRenderState != NULL)
		*renderState = *srcRenderState;
	return renderState;
}

VertexDefinition* Beefy::RenderDevice::CreateVertexDefinition(VertexDefData* elementData, int numElements)
{
	VertexDefinition* vertexDefinition = new VertexDefinition();
	vertexDefinition->mElementData = new VertexDefData[numElements];
	vertexDefinition->mNumElements = numElements;
	memcpy(vertexDefinition->mElementData, elementData, numElements * sizeof(VertexDefData));
	return vertexDefinition;
}

Texture* RenderDevice::LoadTexture(const StringImpl& fileName, int flags)
{
	int dotPos = (int)fileName.LastIndexOf('.');
	String ext = fileName.Substring(dotPos);
	
	ImageData* imageData = NULL;
	if (ext == ".tga")
		imageData = new TGAData();	
	else if (ext == ".png")
		imageData = new PNGData();
	else if (ext == ".jpg")
		imageData = new JPEGData();
	else if (ext == ".pvr")
		imageData = new PVRData();
	else
	{
		BF_FATAL("Unknown texture format");
		return NULL; // Unknown format
	}
	
	imageData->mWantsAlphaPremultiplied = (flags & TextureFlag_NoPremult) == 0;

	Texture* aTexture = NULL;
	if (imageData->LoadFromFile(fileName))
	{
// 		if ((int)fileName.IndexOf("fft") != -1)
// 		{
// 			BFIData bFIData;
// 			bFIData.Compress(imageData);
// 		}

		aTexture = LoadTexture(imageData, flags);
	}
	else
		BF_FATAL("Failed to load image");
	
	delete imageData;
	return aTexture;
}


