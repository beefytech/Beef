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
	mTopology = Topology3D_TriangleList;
	mShader = NULL;
	mClipped = false;
	mTexWrap = false;
	mWireframe = false;
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
	mApp = NULL;
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
	String ext;
	if (dotPos != -1)
		ext = fileName.Substring(dotPos);

	ImageData* imageData = NULL;
	bool handled = false;
	bool failed = false;

	if (fileName == "!white")
	{
		imageData = new ImageData();
		imageData->CreateNew(1, 1, true);
		imageData->mBits[0] = 0xFFFFFFFF;
		handled = true;
	}
	else if (ext == ".tga")
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

	if (!handled)
	{
		imageData->mWantsAlphaPremultiplied = (flags & TextureFlag_NoPremult) == 0;
		if (fileName.StartsWith("@"))
		{
			int colon = (int)fileName.IndexOf(':');
			String addrStr = fileName.Substring(1, colon - 1);
			String lenStr = fileName.Substring(colon + 1);
			void* addr = (void*)(intptr)strtoll(addrStr.c_str(), NULL, 16);
			int len = (int)strtol(lenStr.c_str(), NULL, 10);
			if (!imageData->LoadFromMemory(addr, len))
			{
				failed = true;
				BF_FATAL("Failed to load image");
			}
		}
		else
		{
			if (!imageData->LoadFromFile(fileName))
			{
				failed = true;
				BF_FATAL("Failed to load image");
			}
		}
	}

	Texture* aTexture = NULL;
	if (!failed)
		aTexture = LoadTexture(imageData, flags);

	imageData->Deref();
	return aTexture;
}


