#include "Common.h"
#include "DXRenderDevice.h"
#include "WinBFApp.h"
#include "BFWindow.h"
#include "img/ImageData.h"
#include "util/PerfTimer.h"
#include "util/BeefPerf.h"
#include "FileStream.h"
#include "DDS.h"

using namespace DirectX;

#include <D3Dcompiler.h>

#pragma warning (push)
#pragma warning (disable:4005)
//#include <d2d1.h>
//#include <D3DX11async.h>
//#include <D3DX10math.h>
//#include <DxErr.h>
#pragma warning(pop)

#include "util/AllocDebug.h"

USING_NS_BF;


#pragma warning (disable:4996)

#pragma comment(lib, "d3d10.lib")
#pragma comment(lib, "d3d11.lib")
//#pragma comment(lib, "d3d11_1.lib")
//#pragma comment(lib, "d3dx11.lib")
#pragma comment(lib, "d2d1.lib")
//#pragma comment(lib, "dxerr.lib")
#pragma comment(lib, "dxgi.lib")
//#pragma comment(lib, "D3DCompiler.lib")

///

#define DXFAILED(check) ((hr = (check)) != 0)
#define DXCHECK(check) if ((check) != 0) BF_FATAL(StrFormat("DirectX call failed with result 0x%X", check).c_str());

static int GetBytesPerPixel(DXGI_FORMAT fmt, int& blockSize)
{
	blockSize = 1;
	switch (fmt)
	{
	case DXGI_FORMAT_UNKNOWN: return 0;
	case DXGI_FORMAT_R32G32B32A32_TYPELESS: return 4 + 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32A32_FLOAT: return 4 + 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32A32_UINT: return 4 + 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32A32_SINT: return 4 + 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32_TYPELESS: return 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32_FLOAT: return 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32_UINT: return 4 + 4 + 4;
	case DXGI_FORMAT_R32G32B32_SINT: return 4 + 4 + 4;
	case DXGI_FORMAT_R16G16B16A16_TYPELESS: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R16G16B16A16_FLOAT: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R16G16B16A16_UNORM: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R16G16B16A16_UINT: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R16G16B16A16_SNORM: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R16G16B16A16_SINT: return 2 + 2 + 2 + 2;
	case DXGI_FORMAT_R32G32_TYPELESS: return 4 + 4;
	case DXGI_FORMAT_R32G32_FLOAT: return 4 + 4;
	case DXGI_FORMAT_R32G32_UINT: return 4 + 4;
	case DXGI_FORMAT_R32G32_SINT: return 4 + 4;
	case DXGI_FORMAT_R32G8X24_TYPELESS: return 4 + 3;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return 4 + 1 + 3;
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return 4 + 1 + 3;
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: return 4 + 1 + 1 + 3;
	case DXGI_FORMAT_R10G10B10A2_TYPELESS: return 4;
	case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
	case DXGI_FORMAT_R10G10B10A2_UINT: return 4;
	case DXGI_FORMAT_R11G11B10_FLOAT: return 4;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return 4;
	case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
	case DXGI_FORMAT_R8G8B8A8_UINT: return 4;
	case DXGI_FORMAT_R8G8B8A8_SNORM: return 4;
	case DXGI_FORMAT_R8G8B8A8_SINT: return 4;
	case DXGI_FORMAT_R16G16_TYPELESS: return 4;
	case DXGI_FORMAT_R16G16_FLOAT: return 4;
	case DXGI_FORMAT_R16G16_UNORM: return 4;
	case DXGI_FORMAT_R16G16_UINT: return 4;
	case DXGI_FORMAT_R16G16_SNORM: return 4;
	case DXGI_FORMAT_R16G16_SINT: return 4;
	case DXGI_FORMAT_R32_TYPELESS: return 4;
	case DXGI_FORMAT_D32_FLOAT: return 4;
	case DXGI_FORMAT_R32_FLOAT: return 4;
	case DXGI_FORMAT_R32_UINT: return 4;
	case DXGI_FORMAT_R32_SINT: return 4;
	case DXGI_FORMAT_R24G8_TYPELESS: return 4;
	case DXGI_FORMAT_D24_UNORM_S8_UINT: return 4;
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return 4;
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return 4;
	case DXGI_FORMAT_R8G8_TYPELESS: return 2;
	case DXGI_FORMAT_R8G8_UNORM: return 2;
	case DXGI_FORMAT_R8G8_UINT: return 2;
	case DXGI_FORMAT_R8G8_SNORM: return 2;
	case DXGI_FORMAT_R8G8_SINT: return 2;
	case DXGI_FORMAT_R16_TYPELESS: return 2;
	case DXGI_FORMAT_R16_FLOAT: return 2;
	case DXGI_FORMAT_D16_UNORM: return 2;
	case DXGI_FORMAT_R16_UNORM: return 2;
	case DXGI_FORMAT_R16_UINT: return 2;
	case DXGI_FORMAT_R16_SNORM: return 2;
	case DXGI_FORMAT_R16_SINT: return 2;
	case DXGI_FORMAT_R8_TYPELESS: return 1;
	case DXGI_FORMAT_R8_UNORM: return 1;
	case DXGI_FORMAT_R8_UINT: return 1;
	case DXGI_FORMAT_R8_SNORM: return 1;
	case DXGI_FORMAT_R8_SINT: return 1;
	case DXGI_FORMAT_A8_UNORM: return 1;
	case DXGI_FORMAT_R1_UNORM: return 1;
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return 3;
	case DXGI_FORMAT_R8G8_B8G8_UNORM: return 4;
	case DXGI_FORMAT_G8R8_G8B8_UNORM: return 4;
	case DXGI_FORMAT_BC1_TYPELESS: blockSize = 4; return 8;
	case DXGI_FORMAT_BC1_UNORM: blockSize = 4; return 8;
	case DXGI_FORMAT_BC1_UNORM_SRGB: blockSize = 4; return 8;
	case DXGI_FORMAT_BC2_TYPELESS: blockSize = 4; return 16;
	case DXGI_FORMAT_BC2_UNORM: blockSize = 4; return 16;
	case DXGI_FORMAT_BC2_UNORM_SRGB: blockSize = 4; return 16;
	case DXGI_FORMAT_BC3_TYPELESS: blockSize = 4; return 16;
	case DXGI_FORMAT_BC3_UNORM: blockSize = 4; return 16;
	case DXGI_FORMAT_BC3_UNORM_SRGB: blockSize = 4; return 16;
	case DXGI_FORMAT_BC4_TYPELESS: blockSize = 4; return 8;
	case DXGI_FORMAT_BC4_UNORM: blockSize = 4; return 8;
	case DXGI_FORMAT_BC4_SNORM: blockSize = 4; return 8;
	case DXGI_FORMAT_BC5_TYPELESS: blockSize = 4; return 16;
	case DXGI_FORMAT_BC5_UNORM: blockSize = 4; return 16;
	case DXGI_FORMAT_BC5_SNORM: blockSize = 4; return 16;
	case DXGI_FORMAT_B5G6R5_UNORM: return 1;
	case DXGI_FORMAT_B5G5R5A1_UNORM: return 2;
	case DXGI_FORMAT_B8G8R8A8_UNORM: return 4;
	case DXGI_FORMAT_B8G8R8X8_UNORM: return 4;
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return 4;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return 4;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 4;
	case DXGI_FORMAT_B8G8R8X8_TYPELESS: return 4;
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return 4;
	case DXGI_FORMAT_BC6H_TYPELESS: return 1;
	case DXGI_FORMAT_BC6H_UF16: return 1;
	case DXGI_FORMAT_BC6H_SF16: return 1;
	case DXGI_FORMAT_BC7_TYPELESS: blockSize = 4; return 16;
	case DXGI_FORMAT_BC7_UNORM: blockSize = 4; return 16;
	case DXGI_FORMAT_BC7_UNORM_SRGB: blockSize = 4; return 16;
// 	case DXGI_FORMAT_AYUV: return 1;
// 	case DXGI_FORMAT_Y410: return 1;
// 	case DXGI_FORMAT_Y416: return 1;
// 	case DXGI_FORMAT_NV12: return 1;
// 	case DXGI_FORMAT_P010: return 1;
// 	case DXGI_FORMAT_P016: return 1;
// 	case DXGI_FORMAT_420_OPAQUE: return 1;
// 	case DXGI_FORMAT_YUY2: return 1;
// 	case DXGI_FORMAT_Y210: return 1;
// 	case DXGI_FORMAT_Y216: return 1;
// 	case DXGI_FORMAT_NV11: return 1;
// 	case DXGI_FORMAT_AI44: return 1;
// 	case DXGI_FORMAT_IA44: return 1;
// 	case DXGI_FORMAT_P8: return 1;
// 	case DXGI_FORMAT_A8P8: return 1;
// 	case DXGI_FORMAT_B4G4R4A4_UNORM: return 1;
	default: return 1;
	}
}

DXShaderParam::DXShaderParam()
{
	mD3DVariable = NULL;
}

DXShaderParam::~DXShaderParam()
{
}

void DXShaderParam::SetTexture(Texture* texture)
{
	DXTexture* dxTexture = (DXTexture*) texture;
	//? DXCHECK(mD3DVariable->AsShaderResource()->SetResource(dXTexture->mD3DTexture));
}

void DXShaderParam::SetFloat4(float x, float y, float z, float w)
{
	float v[4] = {x, y, z, w};
	DXCHECK(mD3DVariable->AsVector()->SetFloatVector(v));
}

///

DXShader::DXShader()
{
	//? mD3DEffect = NULL;
	mVertexDef = NULL;
	mD3DPixelShader = NULL;
	mD3DVertexShader = NULL;
	mD3DLayout = NULL;
	mConstBuffer = NULL;
	mHas2DPosition = false;
}

DXShader::~DXShader()
{
	delete mVertexDef;
	ReleaseNative();

	//? if (mD3DEffect != NULL)
	//? 	mD3DEffect->Release();
}

void DXShader::ReleaseNative()
{
	if (mD3DLayout != NULL)
		mD3DLayout->Release();
	mD3DLayout = NULL;
	if (mD3DVertexShader != NULL)
		mD3DVertexShader->Release();
	mD3DVertexShader = NULL;
	if (mD3DPixelShader != NULL)
		mD3DPixelShader->Release();
	mD3DPixelShader = NULL;
	if (mConstBuffer != NULL)
		mConstBuffer->Release();
	mConstBuffer = NULL;
}

extern "C"
typedef HRESULT(WINAPI* Func_D3DX10CompileFromFileW)(LPCWSTR pSrcFile, CONST D3D10_SHADER_MACRO* pDefines, LPD3D10INCLUDE pInclude,
	LPCSTR pFunctionName, LPCSTR pProfile, UINT Flags1, UINT Flags2, ID3D10Blob** ppShader, ID3D10Blob** ppErrorMsgs);

static Func_D3DX10CompileFromFileW gFunc_D3DX10CompileFromFileW;

static bool LoadDXShader(const StringImpl& filePath, const StringImpl& entry, const StringImpl& profile, ID3D10Blob** outBuffer)
{
	HRESULT hr;
	String outObj = filePath + "_" + entry + "_" + profile;

	bool useCache = false;
	auto srcDate = ::BfpFile_GetTime_LastWrite(filePath.c_str());
	auto cacheDate = ::BfpFile_GetTime_LastWrite(outObj.c_str());
	if (cacheDate >= srcDate)
		useCache = true;

	if (!useCache)
	{
		if (gFunc_D3DX10CompileFromFileW == NULL)
		{
			auto lib = LoadLibraryA("D3DCompiler_47.dll");
			if (lib != NULL)
				gFunc_D3DX10CompileFromFileW = (Func_D3DX10CompileFromFileW)::GetProcAddress(lib, "D3DCompileFromFile");
		}

		if (gFunc_D3DX10CompileFromFileW == NULL)
			useCache = true;
	}

	if (!useCache)
	{
		if (gFunc_D3DX10CompileFromFileW == NULL)
		{
			auto lib = LoadLibraryA("D3DCompiler_47.dll");
			if (lib != NULL)
				gFunc_D3DX10CompileFromFileW = (Func_D3DX10CompileFromFileW)::GetProcAddress(lib, "D3DCompileFromFile");
		}

		ID3D10Blob* errorMessage = NULL;
		auto dxResult = gFunc_D3DX10CompileFromFileW(UTF8Decode(filePath).c_str(), NULL, NULL, entry.c_str(), profile.c_str(),
			D3D10_SHADER_DEBUG | D3D10_SHADER_ENABLE_STRICTNESS, 0, outBuffer, &errorMessage);

		if (DXFAILED(dxResult))
		{
			if (errorMessage != NULL)
			{
				BF_FATAL(StrFormat("Vertex shader load failed: %s", (char*)errorMessage->GetBufferPointer()).c_str());
				errorMessage->Release();
			}
			else
				BF_FATAL("Shader load failed");
			return false;
		}

		auto ptr = (*outBuffer)->GetBufferPointer();
		int size = (int)(*outBuffer)->GetBufferSize();

		FILE* fp = fopen(outObj.c_str(), "wb");
		if (fp != NULL)
		{
			fwrite(ptr, 1, size, fp);
			fclose(fp);
		}
		return true;
	}

	FILE* fp = fopen(outObj.c_str(), "rb");
	if (fp == NULL)
	{
		BF_FATAL("Failed to load compiled shader");
		return false;
	}

	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	D3D10CreateBlob(size, outBuffer);
	auto ptr = (*outBuffer)->GetBufferPointer();
	fread(ptr, 1, size, fp);
	fclose(fp);

	return true;
}

bool DXShader::Load()
{
	//HRESULT hr;

	ID3D10Blob* errorMessage = NULL;
	ID3D10Blob* vertexShaderBuffer = NULL;
	ID3D10Blob* pixelShaderBuffer = NULL;

	LoadDXShader(mSrcPath + ".fx", "VS", "vs_4_0", &vertexShaderBuffer);
	LoadDXShader(mSrcPath + ".fx", "PS", "ps_4_0", &pixelShaderBuffer);

	defer(
		{
			vertexShaderBuffer->Release();
			pixelShaderBuffer->Release();
		});

	mHas2DPosition = false;
	mVertexSize = 0;
	mD3DLayout = NULL;

	static const char* semanticNames[] = {
		"POSITION",
		"POSITION",
		"COLOR",
		"TEXCOORD",
		"NORMAL",
		"BINORMAL",
		"TANGENT",
		"BLENDINDICES",
		"BLENDWEIGHT",
		"DEPTH",
		"FOG",
		"POINTSIZE",
		"SAMPLE",
		"TESSELLATEFACTOR" };

	static const DXGI_FORMAT dxgiFormat[] = {
		DXGI_FORMAT_R32_FLOAT/*VertexElementFormat_Single*/,
		DXGI_FORMAT_R32G32_FLOAT/*VertexElementFormat_Vector2*/,
		DXGI_FORMAT_R32G32B32_FLOAT/*VertexElementFormat_Vector3*/,
		DXGI_FORMAT_R32G32B32A32_FLOAT/*VertexElementFormat_Vector4*/,
		DXGI_FORMAT_R8G8B8A8_UNORM/*VertexElementFormat_Color*/,
		DXGI_FORMAT_R8G8B8A8_UINT/*VertexElementFormat_Byte4*/,
		DXGI_FORMAT_R16G16_UINT/*VertexElementFormat_Short2*/,
		DXGI_FORMAT_R16G16B16A16_UINT/*VertexElementFormat_Short4*/,
		DXGI_FORMAT_R16G16_UNORM/*VertexElementFormat_NormalizedShort2*/,
		DXGI_FORMAT_R16G16B16A16_UNORM/*VertexElementFormat_NormalizedShort4*/,
		DXGI_FORMAT_R16G16_FLOAT/*VertexElementFormat_HalfVector2*/,
		DXGI_FORMAT_R16G16B16A16_FLOAT/*VertexElementFormat_HalfVector4*/
	};

	static const int dxgiSize[] = {
		sizeof(float) * 1/*VertexElementFormat_Single*/,
		sizeof(float) * 2/*VertexElementFormat_Vector2*/,
		sizeof(float) * 3/*VertexElementFormat_Vector3*/,
		sizeof(float) * 4/*VertexElementFormat_Vector4*/,
		sizeof(uint32)/*VertexElementFormat_Color*/,
		sizeof(uint8) * 4/*VertexElementFormat_Byte4*/,
		sizeof(uint16) * 2/*VertexElementFormat_Short2*/,
		sizeof(uint16) * 4/*VertexElementFormat_Short4*/,
		sizeof(uint16) * 2/*VertexElementFormat_NormalizedShort2*/,
		sizeof(uint16) * 4/*VertexElementFormat_NormalizedShort4*/,
		sizeof(uint16) * 2/*VertexElementFormat_HalfVector2*/,
		sizeof(uint16) * 4/*VertexElementFormat_HalfVector4*/
	};

	D3D11_INPUT_ELEMENT_DESC layout[64];
	for (int elementIdx = 0; elementIdx < mVertexDef->mNumElements; elementIdx++)
	{
		VertexDefData* vertexDefData = &mVertexDef->mElementData[elementIdx];

		if (vertexDefData->mUsage == VertexElementUsage_Position2D)
			mHas2DPosition = true;

		D3D11_INPUT_ELEMENT_DESC* elementDesc = &layout[elementIdx];
		elementDesc->SemanticName = semanticNames[vertexDefData->mUsage];
		elementDesc->SemanticIndex = vertexDefData->mUsageIndex;
		elementDesc->Format = dxgiFormat[vertexDefData->mFormat];
		elementDesc->InputSlot = 0;
		elementDesc->AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		elementDesc->InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		elementDesc->InstanceDataStepRate = 0;
		mVertexSize += dxgiSize[vertexDefData->mFormat];
	}

	/* =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = sizeof(layout) / sizeof(layout[0]);*/
	HRESULT result = mRenderDevice->mD3DDevice->CreateInputLayout(layout, mVertexDef->mNumElements, vertexShaderBuffer->GetBufferPointer(),
		vertexShaderBuffer->GetBufferSize(), &mD3DLayout);
	DXCHECK(result);
	if (FAILED(result))
		return false;

	// Create the vertex shader from the buffer.
	result = mRenderDevice->mD3DDevice->CreateVertexShader(vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), NULL, &mD3DVertexShader);
	DXCHECK(result);
	if (FAILED(result))
		return false;

	// Create the pixel shader from the buffer.
	result = mRenderDevice->mD3DDevice->CreatePixelShader(pixelShaderBuffer->GetBufferPointer(), pixelShaderBuffer->GetBufferSize(), NULL, &mD3DPixelShader);
	DXCHECK(result);
	if (FAILED(result))
		return false;

	Init();
	return true;
}

void DXShader::ReinitNative()
{
	ReleaseNative();
	Load();
}

ShaderParam* DXShader::GetShaderParam(const StringImpl& name)
{
	DXShaderParamMap::iterator itr = mParamsMap.find(name);
	if (itr != mParamsMap.end())
		return itr->second;

	return NULL;

	/*ID3D11EffectVariable* d3DVariable = mD3DEffect->GetVariableByName(ToString(name).c_str());
	if (d3DVariable == NULL)
		return NULL;

	DXShaderParam* shaderParam = new DXShaderParam();
	shaderParam->mD3DVariable = d3DVariable;
	mParamsMap[name] = shaderParam;

	return shaderParam;*/
}

///

DXTexture::DXTexture()
{
	mD3DTexture = NULL;
	mD3DResourceView = NULL;
	mD3DRenderTargetView = NULL;
	mRenderDevice = NULL;
	mD3DDepthBuffer = NULL;
	mD3DDepthStencilView = NULL;
	mContentBits = NULL;
}

DXTexture::~DXTexture()
{
	if ((!mPath.IsEmpty()) && (mRenderDevice != NULL))
		((DXRenderDevice*)mRenderDevice)->mTextureMap.Remove(mPath);

	//OutputDebugStrF("DXTexture::~DXTexture %@\n", this);
	delete mContentBits;
	if (mD3DResourceView != NULL)
		mD3DResourceView->Release();
	if (mD3DDepthStencilView != NULL)
		mD3DDepthStencilView->Release();
	if (mD3DDepthBuffer != NULL)
		mD3DDepthBuffer->Release();
	if (mD3DTexture != NULL)
		mD3DTexture->Release();
	if (mRenderDevice != NULL)
		mRenderDevice->mTextures.Remove(this);
}

void DXTexture::ReleaseNative()
{
	//mRenderDevice->mD3DDeviceContext->CopyResource(newResource, mD3DTexture)

	if (mD3DResourceView != NULL)
	{
		mD3DResourceView->Release();
		mD3DResourceView = NULL;
	}
	if (mD3DDepthStencilView != NULL)
	{
		mD3DDepthStencilView->Release();
		mD3DDepthStencilView = NULL;
	}
	if (mD3DDepthBuffer != NULL)
	{
		mD3DDepthBuffer->Release();
		mD3DDepthBuffer = NULL;
	}
	if (mD3DTexture != NULL)
	{
		mD3DTexture->Release();
		mD3DTexture = NULL;
	}
}

void DXTexture::ReinitNative()
{
	ReleaseNative();

	int aWidth = 0;
	int aHeight = 0;

	D3D11_SUBRESOURCE_DATA resData;
	resData.pSysMem = mContentBits;
	resData.SysMemPitch = mWidth * 4;
	resData.SysMemSlicePitch = mWidth * mHeight * 4;

	// Create the target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = mWidth;
	desc.Height = mHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	//OutputDebugStrF("Creating texture\n");

	auto dxRenderDevice = (DXRenderDevice*)mRenderDevice;

	DXCHECK(dxRenderDevice->mD3DDevice->CreateTexture2D(&desc, (mContentBits != NULL) ? &resData : NULL, &mD3DTexture));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	DXCHECK(dxRenderDevice->mD3DDevice->CreateShaderResourceView(mD3DTexture, &srDesc, &mD3DResourceView));

	OutputDebugStrF("DXTexture::ReinitNative %p\n", this);
}

void DXTexture::PhysSetAsTarget()
{
	{
		D3D11_VIEWPORT viewPort;
		viewPort.Width = (float)mWidth;
		viewPort.Height = (float)mHeight;
		viewPort.MinDepth = 0.0f;
		viewPort.MaxDepth = 1.0f;
		viewPort.TopLeftX = 0;
		viewPort.TopLeftY = 0;

		mRenderDevice->mD3DDeviceContext->OMSetRenderTargets(1, &mD3DRenderTargetView, mD3DDepthStencilView);
		//mRenderDevice->mD3DDeviceContext->OMSetRenderTargets(1, &mD3DRenderTargetView, ((rand() % 2) != 0) ? NULL : mD3DDepthStencilView);
		//mRenderDevice->mD3DDeviceContext->OMSetRenderTargets(1, &mD3DRenderTargetView, NULL);
		mRenderDevice->mD3DDeviceContext->RSSetViewports(1, &viewPort);
	}

	//if (!mHasBeenDrawnTo)
	{
		float bgColor[4] = {1, (rand() % 256) / 256.0f, 0.5, 1};
		mRenderDevice->mD3DDeviceContext->ClearRenderTargetView(mD3DRenderTargetView, bgColor);
		if (mD3DDepthStencilView != NULL)
			mRenderDevice->mD3DDeviceContext->ClearDepthStencilView(mD3DDepthStencilView, D3D11_CLEAR_DEPTH/*|D3D11_CLEAR_STENCIL*/, 1.0f, 0);

		//mRenderDevice->mD3DDevice->ClearRenderTargetView(mD3DRenderTargetView, D3DXVECTOR4(1, 0.5, 0.5, 1));
		mHasBeenDrawnTo = true;
	}
}

void DXTexture::Blt(ImageData* imageData, int x, int y)
{
	D3D11_BOX box;
	box.left = x;
	box.right = x + imageData->mWidth;
	box.top = y;
	box.bottom = y + imageData->mHeight;
	box.front = 0;
	box.back = 1;
	mRenderDevice->mD3DDeviceContext->UpdateSubresource(mD3DTexture, 0, &box, imageData->mBits, imageData->mWidth * sizeof(uint32), 0);

	if (mContentBits != NULL)
	{
		for (int yOfs = 0; yOfs < imageData->mHeight; yOfs++)
			memcpy(mContentBits + x + (y + yOfs) * mWidth, imageData->mBits + yOfs * imageData->mWidth, imageData->mWidth * 4);
	}
}

void DXTexture::SetBits(int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits)
{
	D3D11_BOX box = { (UINT)destX, (UINT)destY, (UINT)0, (UINT)(destX + destWidth), (UINT)(destY + destHeight), 1 };
	mRenderDevice->mD3DDeviceContext->UpdateSubresource(mD3DTexture, 0, &box, bits, srcPitch * sizeof(uint32), 0);

	if (mContentBits != NULL)
	{
		for (int y = 0; y < destHeight; y++)
			memcpy(mContentBits + destX + (destY + y) * mWidth, bits + (y * srcPitch), destWidth * 4);
	}
}

void DXTexture::GetBits(int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits)
{
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = 0;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.Width = srcWidth;
	texDesc.Height = srcHeight;
	texDesc.MipLevels = 1;
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	D3D11_BOX srcBox = { 0 };
	srcBox.left = srcX;
	srcBox.top = srcY;
	srcBox.right = srcX + srcWidth;
	srcBox.bottom = srcY + srcHeight;
	srcBox.back = 1;

	ID3D11Texture2D *texture;
	DXCHECK(mRenderDevice->mD3DDevice->CreateTexture2D(&texDesc, 0, &texture));
	mRenderDevice->mD3DDeviceContext->CopySubresourceRegion(texture, 0, 0, 0, 0, mD3DTexture, 0, &srcBox);

	D3D11_MAPPED_SUBRESOURCE mapTex;
	DXCHECK(mRenderDevice->mD3DDeviceContext->Map(texture, 0, D3D11_MAP_READ, NULL, &mapTex));

	uint8* srcPtr = (uint8*) mapTex.pData;
	uint8* destPtr = (uint8*) bits;
	for (int y = 0; y < srcHeight; y++)
	{
		memcpy(destPtr, srcPtr, srcWidth*sizeof(uint32));
		srcPtr += mapTex.RowPitch;
		destPtr += destPitch * 4;
	}
	mRenderDevice->mD3DDeviceContext->Unmap(texture, 0);
	texture->Release();
}

///

static int GetPowerOfTwo(int input)
{
	int value = 1;
	while (value < input)
		value <<= 1;
	return value;
}

DXDrawBatch::DXDrawBatch()
{
}

DXDrawBatch::~DXDrawBatch()
{

}

void DXDrawBatch::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if (mVtxIdx == 0)
		return;

	if ((mRenderState->mClipped) &&
		((mRenderState->mClipRect.mWidth == 0) || (mRenderState->mClipRect.mHeight == 0)))
		return;

	if (mRenderState->mClipped)
		BF_ASSERT((mRenderState->mClipRect.mWidth > 0) && (mRenderState->mClipRect.mHeight > 0));

	DXRenderDevice* aRenderDevice = (DXRenderDevice*)renderDevice;
	/*if ((mDrawLayer->mRenderWindow != NULL) && (aRenderDevice->mPhysRenderWindow != mDrawLayer->mRenderWindow))
		aRenderDevice->PhysSetRenderWindow(mDrawLayer->mRenderWindow);*/

	D3D11_MAP idxMapType = D3D11_MAP_WRITE_NO_OVERWRITE;
	D3D11_MAP vtxMapType = D3D11_MAP_WRITE_NO_OVERWRITE;

	int idxByteStart = aRenderDevice->mIdxByteIdx;
	int idxDataSize = sizeof(uint16) * mIdxIdx;
	aRenderDevice->mIdxByteIdx += idxDataSize;
	if (aRenderDevice->mIdxByteIdx >= DX_IDXBUFFER_SIZE)
	{
		idxMapType = D3D11_MAP_WRITE_DISCARD;
		idxByteStart = 0;
		aRenderDevice->mIdxByteIdx = idxDataSize;
	}

	int vtxByteStart = aRenderDevice->mVtxByteIdx;
	int vtxDataSize = mVtxIdx * mVtxSize;

	//int vtxStartIdx = ((vtxByteStart + mVtxSize - 1) / mVtxSize);
	int vtxStartIdx = vtxByteStart / mVtxSize;
	int vtxOffset = vtxByteStart % mVtxSize;
	//vtxByteStart = vtxStartIdx * mVtxSize;

	//aRenderDevice->mVtxByteIdx += vtxDataSize;
	aRenderDevice->mVtxByteIdx = vtxByteStart + vtxDataSize;

	if (aRenderDevice->mVtxByteIdx >= DX_VTXBUFFER_SIZE)
	{
		vtxMapType = D3D11_MAP_WRITE_DISCARD;
		vtxByteStart = 0;
		vtxOffset = 0;
		vtxStartIdx = 0;
		aRenderDevice->mVtxByteIdx = vtxDataSize;
	}

	//TODO: Round up for various vertex formats, manage stride properly, etc

	D3D11_MAPPED_SUBRESOURCE mappedSubResource;
	DXCHECK(aRenderDevice->mD3DDeviceContext->Map(aRenderDevice->mD3DIndexBuffer, 0, idxMapType, 0, &mappedSubResource));
	void* dxIdxData = mappedSubResource.pData;
	DXCHECK(aRenderDevice->mD3DDeviceContext->Map(aRenderDevice->mD3DVertexBuffer, 0, vtxMapType, 0, &mappedSubResource));
	void* dxVtxData = mappedSubResource.pData;

	//mVtxByteIdx = ((mVtxByteIdx + vtxSize - 1) / vtxSize) * vtxSize;

	memcpy((uint8*)dxIdxData + idxByteStart, mIndices, idxDataSize);
	memcpy((uint8*)dxVtxData + vtxByteStart, mVertices, vtxDataSize);

	aRenderDevice->mD3DDeviceContext->Unmap(aRenderDevice->mD3DVertexBuffer, 0);
	aRenderDevice->mD3DDeviceContext->Unmap(aRenderDevice->mD3DIndexBuffer, 0);

	//DXTexture* dxTexture = (DXTexture*)mCurTexture;
	//aRenderDevice->mD3DDeviceContext->PSSetShaderResources(0, 1, &dxTexture->mD3DTexture);

	if (mRenderState != aRenderDevice->mPhysRenderState)
		aRenderDevice->PhysSetRenderState(mRenderState);

	// Set vertex buffer
	UINT stride = mVtxSize;
	UINT offset = vtxOffset;
	aRenderDevice->mD3DDeviceContext->IASetVertexBuffers(0, 1, &aRenderDevice->mD3DVertexBuffer, &stride, &offset);
	aRenderDevice->mD3DDeviceContext->IASetIndexBuffer(aRenderDevice->mD3DIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	aRenderDevice->mD3DDeviceContext->DrawIndexed(mIdxIdx, idxByteStart / sizeof(uint16), vtxStartIdx/*vtxByteStart / mVtxSize*/);
}

DXDrawLayer::DXDrawLayer()
{
}

DXDrawLayer::~DXDrawLayer()
{
}


DrawBatch* DXDrawLayer::CreateDrawBatch()
{
	return new DXDrawBatch();
}

RenderCmd* Beefy::DXDrawLayer::CreateSetTextureCmd(int textureIdx, Texture* texture)
{
	DXSetTextureCmd* setTextureCmd = AllocRenderCmd<DXSetTextureCmd>();
	setTextureCmd->mTextureIdx = textureIdx;
	setTextureCmd->mTexture = texture;
	return setTextureCmd;
}

void DXRenderDevice::PhysSetRenderState(RenderState* renderState)
{
	DXRenderState* dxRenderState = (DXRenderState*)renderState;
	DXShader* dxShader = (DXShader*)renderState->mShader;

	if (renderState->mTopology != mPhysRenderState->mTopology)
	{
		D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		if (dxRenderState->mTopology == Topology3D_LineLine)
			topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		mD3DDeviceContext->IASetPrimitiveTopology(topology);
	}

	if ((renderState->mShader != mPhysRenderState->mShader) && (renderState->mShader != NULL))
	{
		mD3DDeviceContext->PSSetSamplers(0, 1, renderState->mTexWrap ? &mD3DWrapSamplerState  : &mD3DDefaultSamplerState);
		mD3DDeviceContext->IASetInputLayout(dxShader->mD3DLayout);
		mD3DDeviceContext->VSSetShader(dxShader->mD3DVertexShader, NULL, 0);
		mD3DDeviceContext->PSSetShader(dxShader->mD3DPixelShader, NULL, 0);

		if (dxShader->mHas2DPosition)
		{
			HRESULT result = NULL;

			if (mMatrix2DBuffer == NULL)
			{
				D3D11_BUFFER_DESC matrixBufferDesc;
				matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
				matrixBufferDesc.ByteWidth = sizeof(float[4]);
				matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				matrixBufferDesc.MiscFlags = 0;
				matrixBufferDesc.StructureByteStride = 0;

				// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
				result = mD3DDevice->CreateBuffer(&matrixBufferDesc, NULL, &mMatrix2DBuffer);
				if (FAILED(result))
				{
					return;
				}
			}

			// Lock the constant buffer so it can be written to.
			D3D11_MAPPED_SUBRESOURCE mappedResource;
			result = mD3DDeviceContext->Map(mMatrix2DBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
			if (FAILED(result))
			{
				return;
			}

			// Get a pointer to the data in the constant buffer.
			float* dataPtr = (float*) mappedResource.pData;
			dataPtr[0] = (float) mCurRenderTarget->mWidth;
			dataPtr[1] = (float) mCurRenderTarget->mHeight;
			dataPtr[2] = 0;
			dataPtr[3] = 0;

			// Unlock the constant buffer.
			mD3DDeviceContext->Unmap(mMatrix2DBuffer, 0);

			//float params[4] = {mCurRenderTarget->mWidth, mCurRenderTarget->mHeight, 0, 0};
			mD3DDeviceContext->VSSetConstantBuffers(0, 1, &mMatrix2DBuffer);
		}
	}

	bool setRasterizerState = false;
	bool setDepthFuncState = false;

	if ((renderState->mClipped != mPhysRenderState->mClipped) ||
		((renderState->mClipped) && (renderState->mClipRect != mPhysRenderState->mClipRect)))
	{
		if (renderState->mClipped)
		{
			D3D11_RECT rects[1];
			rects[0].left = (int)renderState->mClipRect.mX;
			rects[0].right = (int) (renderState->mClipRect.mX + renderState->mClipRect.mWidth);
			rects[0].top = (int) renderState->mClipRect.mY;
			rects[0].bottom = (int) (renderState->mClipRect.mY + renderState->mClipRect.mHeight);
			mD3DDeviceContext->RSSetScissorRects(1, rects);
		}
		setRasterizerState = true;
	}

	if (renderState->mWriteDepthBuffer != mPhysRenderState->mWriteDepthBuffer)
		setDepthFuncState = true;

	if (renderState->mDepthFunc != mPhysRenderState->mDepthFunc)
	{
		setDepthFuncState = true;
		setRasterizerState = true;
	}

	if (renderState->mWireframe != mPhysRenderState->mWireframe)
		setRasterizerState = true;

	if (setRasterizerState)
	{
		if (dxRenderState->mD3DRasterizerState == NULL)
		{
			static const D3D11_CULL_MODE cullModes[] =
			{
				D3D11_CULL_NONE,
				D3D11_CULL_FRONT,
				D3D11_CULL_BACK
			};

			D3D11_RASTERIZER_DESC rasterizerState;
			rasterizerState.CullMode = cullModes[dxRenderState->mCullMode];
			//rasterizerState.CullMode = D3D11_CULL_BACK;
			rasterizerState.FillMode = renderState->mWireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
			rasterizerState.FrontCounterClockwise = false;
			rasterizerState.DepthBias = 0;
			rasterizerState.DepthBiasClamp = 0;
			rasterizerState.SlopeScaledDepthBias = 0;
			rasterizerState.DepthClipEnable = renderState->mDepthFunc != DepthFunc_Always;
			rasterizerState.ScissorEnable = renderState->mClipped;
			rasterizerState.MultisampleEnable = false;
			rasterizerState.AntialiasedLineEnable = false;

			mD3DDevice->CreateRasterizerState(&rasterizerState, &dxRenderState->mD3DRasterizerState);
		}

		mD3DDeviceContext->RSSetState(dxRenderState->mD3DRasterizerState);
	}

	if (setDepthFuncState)
	{
		if (dxRenderState->mD3DDepthStencilState == NULL)
		{
			static const D3D11_COMPARISON_FUNC comparisonArray[] =
			{
				D3D11_COMPARISON_NEVER,
				D3D11_COMPARISON_LESS,
				D3D11_COMPARISON_LESS_EQUAL,
				D3D11_COMPARISON_EQUAL,
				D3D11_COMPARISON_GREATER,
				D3D11_COMPARISON_NOT_EQUAL,
				D3D11_COMPARISON_GREATER_EQUAL,
				D3D11_COMPARISON_ALWAYS
			};

			D3D11_DEPTH_STENCIL_DESC depthStencilDesc;

			depthStencilDesc.DepthEnable = (dxRenderState->mDepthFunc != DepthFunc_Always) || (dxRenderState->mWriteDepthBuffer);
			depthStencilDesc.DepthWriteMask = dxRenderState->mWriteDepthBuffer ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			depthStencilDesc.DepthFunc = comparisonArray[dxRenderState->mDepthFunc];
			depthStencilDesc.StencilEnable = FALSE;
			depthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
			depthStencilDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
			depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
			depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
			depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
			depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
			depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
			depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

			mD3DDevice->CreateDepthStencilState(&depthStencilDesc, &dxRenderState->mD3DDepthStencilState);
		}

		mD3DDeviceContext->OMSetDepthStencilState(dxRenderState->mD3DDepthStencilState, 1);
	}

	mPhysRenderState = renderState;
}

void DXRenderDevice::PhysSetRenderWindow(RenderWindow* renderWindow)
{
	mPhysRenderWindow = renderWindow;
	mCurRenderTarget = renderWindow;
	((DXRenderWindow*)renderWindow)->PhysSetAsTarget();
}

void DXRenderDevice::PhysSetRenderTarget(Texture* renderTarget)
{
	mCurRenderTarget = renderTarget;
	renderTarget->PhysSetAsTarget();
}

RenderState* DXRenderDevice::CreateRenderState(RenderState* srcRenderState)
{
	DXRenderState* renderState = new DXRenderState();
	if (srcRenderState != NULL)
	{
		*renderState = *(DXRenderState*)srcRenderState;
		if (renderState->mD3DRasterizerState != NULL)
			renderState->mD3DRasterizerState->AddRef();
		if (renderState->mD3DDepthStencilState != NULL)
			renderState->mD3DDepthStencilState->AddRef();
	}
	mRenderStates.Add(renderState);
	return renderState;
}

struct DXModelVertex
{
	Vector3 mPosition;
	uint32 mColor;
	TexCoords mTexCoords;
	Vector3 mNormal;
	TexCoords mBumpTexCoords;
	Vector3 mTangent;
};

ModelInstance* DXRenderDevice::CreateModelInstance(ModelDef* modelDef, ModelCreateFlags flags)
{
	DXModelInstance* dxModelInstance = new DXModelInstance(modelDef);

	////

	VertexDefData vertexDefData[] =
	{
		{VertexElementUsage_Position3D,			0, VertexElementFormat_Vector3},
		{VertexElementUsage_Color,				0, VertexElementFormat_Color},
		{VertexElementUsage_TextureCoordinate,	0, VertexElementFormat_Vector2},
		{VertexElementUsage_Normal,				0, VertexElementFormat_Vector3},
		{VertexElementUsage_TextureCoordinate,	1, VertexElementFormat_Vector2},
		{VertexElementUsage_Tangent,			0, VertexElementFormat_Vector3}
	};

	auto vertexDefinition = CreateVertexDefinition(vertexDefData, sizeof(vertexDefData) / sizeof(vertexDefData[0]));
	RenderState* renderState = NULL;
	if ((flags & ModelCreateFlags_NoSetRenderState) == 0)
	{
		renderState = CreateRenderState(mDefaultRenderState);
		renderState->mShader = LoadShader(gBFApp->mInstallDir + "/shaders/ModelStd", vertexDefinition);
		renderState->mTexWrap = true;
		renderState->mDepthFunc = DepthFunc_LessEqual;
		renderState->mWriteDepthBuffer = true;
	}
	delete vertexDefinition;

	dxModelInstance->mRenderState = renderState;

	////

	dxModelInstance->mD3DRenderDevice = this;
	dxModelInstance->mDXModelMeshs.Resize(modelDef->mMeshes.size());
	int dxMeshIdx = 0;

	for (int meshIdx = 0; meshIdx < (int)modelDef->mMeshes.size(); meshIdx++)
	{
		ModelMesh* mesh = &modelDef->mMeshes[meshIdx];
		DXModelMesh* dxMesh = &dxModelInstance->mDXModelMeshs[dxMeshIdx];

		dxMesh->mPrimitives.Resize(mesh->mPrimitives.size());

		for (int primitivesIdx = 0 ; primitivesIdx < (int)mesh->mPrimitives.size(); primitivesIdx++)
		{
			auto primitives = &mesh->mPrimitives[primitivesIdx];
			auto dxPrimitives = &dxMesh->mPrimitives[primitivesIdx];

// 			String texPath = mesh->mTexFileName;
// 			if (!texPath.IsEmpty())
// 			{
// 				if ((int)texPath.IndexOf(':') == -1)
// 					texPath = modelDef->mLoadDir + "Textures/" + texPath;
// 				//texPath = gBFApp->mInstallDir + L"models/Textures/" + texPath;
//
// 				dxPrimitives->mTexture = (DXTexture*)((RenderDevice*)this)->LoadTexture(texPath, TextureFlag_NoPremult);
// 			}

			Array<String> texPaths = primitives->mTexPaths;


			if (primitives->mMaterial != NULL)
			{
				dxPrimitives->mMaterialName = primitives->mMaterial->mName;
				if (primitives->mMaterial->mDef != NULL)
				{
					for (auto& texParamVal : primitives->mMaterial->mDef->mTextureParameterValues)
					{
						if (texPaths.IsEmpty())
							texPaths.Add(texParamVal->mTexturePath);

// 						if (texPath.IsEmpty())
// 							texPath = texParamVal->mTexturePath;
// 						if ((texParamVal->mName == "Albedo_texture") || (texParamVal->mName.EndsWith("_Color")))
// 							texPath = texParamVal->mTexturePath;
// 						else if ((texParamVal->mName == "NM_texture") || (texParamVal->mName.EndsWith("_NM")))
// 							bumpTexPath = texParamVal->mTexturePath;
					}
				}
			}

			for (auto& texPath : texPaths)
			{
				if (!modelDef->mLoadDir.IsEmpty())
					texPath = GetAbsPath(texPath, modelDef->mLoadDir);
				dxPrimitives->mTextures.Add((DXTexture*)((RenderDevice*)this)->LoadTexture(texPath, TextureFlag_NoPremult));
			}

			dxPrimitives->mNumIndices = (int)primitives->mIndices.size();
			dxPrimitives->mNumVertices = (int)primitives->mVertices.size();

			D3D11_BUFFER_DESC bd;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = (int)primitives->mIndices.size() * sizeof(uint16);
			bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = 0;

			mD3DDevice->CreateBuffer(&bd, NULL, &dxPrimitives->mD3DIndexBuffer);

			D3D11_MAPPED_SUBRESOURCE mappedSubResource;

			DXCHECK(mD3DDeviceContext->Map(dxPrimitives->mD3DIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubResource));
			uint16* dxIdxData = (uint16*)mappedSubResource.pData;
			for (int idxIdx = 0; idxIdx < dxPrimitives->mNumIndices; idxIdx++)
				dxIdxData[idxIdx] = (uint16)primitives->mIndices[idxIdx];
			mD3DDeviceContext->Unmap(dxPrimitives->mD3DIndexBuffer, 0);

			//

			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = (int)primitives->mVertices.size() * sizeof(DXModelVertex);
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			bd.MiscFlags = 0;
			bd.StructureByteStride = 0;

			mD3DDevice->CreateBuffer(&bd, NULL, &dxPrimitives->mD3DVertexBuffer);

			DXCHECK(mD3DDeviceContext->Map(dxPrimitives->mD3DVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubResource));
			DXModelVertex* dxVtxData = (DXModelVertex*)mappedSubResource.pData;
			for (int vtxIdx = 0; vtxIdx < (int)primitives->mVertices.size(); vtxIdx++)
			{
				ModelVertex* srcVtxData = &primitives->mVertices[vtxIdx];
				DXModelVertex* destVtx = dxVtxData + vtxIdx;

				destVtx->mPosition = srcVtxData->mPosition;
				destVtx->mTexCoords = srcVtxData->mTexCoords;
				//destVtx->mTexCoords.mV = 1.0f - destVtx->mTexCoords.mV;
				destVtx->mTexCoords.mV = destVtx->mTexCoords.mV;
				destVtx->mBumpTexCoords = srcVtxData->mBumpTexCoords;
				destVtx->mColor = srcVtxData->mColor;
				destVtx->mTangent = srcVtxData->mTangent;
			}

			mD3DDeviceContext->Unmap(dxPrimitives->mD3DVertexBuffer, 0);

			dxMeshIdx++;
		}
	}

	return dxModelInstance;
}

void DXDrawLayer::SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size)
{
	DXSetConstantData* dxSetConstantData = AllocRenderCmd<DXSetConstantData>(size);
	dxSetConstantData->mRenderState = mRenderDevice->mCurRenderState;
	dxSetConstantData->mUsageIdx = usageIdx;
	dxSetConstantData->mSlotIdx = slotIdx;
	dxSetConstantData->mSize = size;

// 	if (size == 64) // Transpose for shader
// 		*((Matrix4*)dxSetConstantData->mData) = Matrix4::Transpose(*((Matrix4*)constData));
// 	else
		memcpy(dxSetConstantData->mData, constData, size);
	QueueRenderCmd(dxSetConstantData);
}

void DXDrawLayer::SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount)
{
	for (int usageIdx = 0; usageIdx < 2; usageIdx++)
	{
		uint8 destData[1024];
		uint8* destDataPtr = destData;
		float* floatData = (float*) destDataPtr;
		uint8* srcDataPtr = (uint8*) constData;
		for (int typeIdx = 0; typeIdx < typeCount; typeIdx++)
		{
			int typeNum = typeData[typeIdx] & 0xF;
			int dataUsage = typeData[typeIdx] >> 8;

			bool want = (dataUsage & (1 << usageIdx)) != 0;

			if (want)
			{
				switch (typeNum)
				{
				case ConstantDataType_Single:
					memcpy(destDataPtr, srcDataPtr, sizeof(float));
					srcDataPtr += sizeof(float);
					destDataPtr += sizeof(float) * 4;
					break;
				case ConstantDataType_Vector2:
					memcpy(destDataPtr, srcDataPtr, sizeof(float) * 2);
					srcDataPtr += sizeof(float) * 2;
					destDataPtr += sizeof(float) * 4;
					break;
				case ConstantDataType_Vector3:
					memcpy(destDataPtr, srcDataPtr, sizeof(float) * 3);
					srcDataPtr += sizeof(float) * 3;
					destDataPtr += sizeof(float) * 4;
					break;
				case ConstantDataType_Vector4:
					memcpy(destDataPtr, srcDataPtr, sizeof(float) * 4);
					srcDataPtr += sizeof(float) * 4;
					destDataPtr += sizeof(float) * 4;
					break;
				case ConstantDataType_Matrix:
					*((Matrix4*) destDataPtr) = Matrix4::Transpose(*((Matrix4*) srcDataPtr));
					srcDataPtr += sizeof(Matrix4);
					destDataPtr += sizeof(Matrix4);
					break;
				}
			}

			switch (typeNum)
			{
			case ConstantDataType_Single:
				srcDataPtr += sizeof(float);
				break;
			case ConstantDataType_Vector2:
				srcDataPtr += sizeof(float) * 2;
				break;
			case ConstantDataType_Vector3:
				srcDataPtr += sizeof(float) * 3;
				break;
			case ConstantDataType_Vector4:
				srcDataPtr += sizeof(float) * 4;
				break;
			case ConstantDataType_Matrix:
				srcDataPtr += sizeof(Matrix4);
				break;
			}

		}

		int destDataSize = (int)(destDataPtr - destData);
		if (destDataSize > 0)
		{
			DXSetConstantData* dxSetConstantData = AllocRenderCmd<DXSetConstantData>(destDataSize);
			dxSetConstantData->mUsageIdx = usageIdx;
			dxSetConstantData->mRenderState = mRenderDevice->mCurRenderState;
			dxSetConstantData->mSlotIdx = slotIdx;
			dxSetConstantData->mSize = destDataSize;
			memcpy(dxSetConstantData->mData, destData, destDataSize);
			QueueRenderCmd(dxSetConstantData);
		}
	}
}

///

DXModelPrimitives::DXModelPrimitives()
{
	mD3DIndexBuffer = NULL;
	mD3DVertexBuffer = NULL;
}

DXModelPrimitives::~DXModelPrimitives()
{
	if (mD3DIndexBuffer != NULL)
		mD3DIndexBuffer->Release();
	if (mD3DVertexBuffer != NULL)
		mD3DVertexBuffer->Release();
	for (auto tex : mTextures)
		tex->Release();
}

//////////////////////////////////////////////////////////////////////////

DXRenderState::DXRenderState()
{
	mD3DRasterizerState = NULL;
	mD3DDepthStencilState = NULL;
}

DXRenderState::~DXRenderState()
{
	if (mD3DRasterizerState != NULL)
		mD3DRasterizerState->Release();
	if (mD3DDepthStencilState != NULL)
		mD3DDepthStencilState->Release();
}

void DXRenderState::ReleaseNative()
{
	if (mD3DRasterizerState != NULL)
		mD3DRasterizerState->Release();
	mD3DRasterizerState = NULL;
	if (mD3DDepthStencilState != NULL)
		mD3DDepthStencilState->Release();
	mD3DDepthStencilState = NULL;
}

void DXRenderState::ReinitNative()
{
	ReleaseNative();
}

void DXRenderState::InvalidateRasterizerState()
{
	if (mD3DRasterizerState != NULL)
	{
		mD3DRasterizerState->Release();
		mD3DRasterizerState = NULL;
	}
}

void DXRenderState::IndalidateDepthStencilState()
{
	if (mD3DDepthStencilState != NULL)
	{
		mD3DDepthStencilState->Release();
		mD3DDepthStencilState = NULL;
	}
}

void DXRenderState::SetClipped(bool clipped)
{
	mClipped = clipped;
	InvalidateRasterizerState();
}

void DXRenderState::SetTexWrap(bool wrap)
{
	mTexWrap = wrap;
	InvalidateRasterizerState();
}

void DXRenderState::SetClipRect(const Rect& rect)
{
	BF_ASSERT((rect.mWidth >= 0) && (rect.mHeight >= 0));
	mClipRect = rect;
	InvalidateRasterizerState();
}

void DXRenderState::SetWriteDepthBuffer(bool writeDepthBuffer)
{
	mWriteDepthBuffer = writeDepthBuffer;
	IndalidateDepthStencilState();
}

void DXRenderState::SetDepthFunc(DepthFunc depthFunc)
{
	mDepthFunc = depthFunc;
	IndalidateDepthStencilState();
}

///

DXModelInstance::DXModelInstance(ModelDef* modelDef) : ModelInstance(modelDef)
{
}

DXModelInstance::~DXModelInstance()
{
}

void DXModelInstance::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	if (mRenderState != NULL)
		SetRenderState();

	for (int meshIdx = 0; meshIdx < (int)mDXModelMeshs.size(); meshIdx++)
	{
		if (!mMeshesVisible[meshIdx])
			continue;

		DXModelMesh* dxMesh = &mDXModelMeshs[meshIdx];

		for (auto primIdx = 0; primIdx < (int)dxMesh->mPrimitives.size(); primIdx++)
		{
			auto dxPrimitives = &dxMesh->mPrimitives[primIdx];

			if (dxPrimitives->mTextures.IsEmpty())
				continue;

			for (int i = 0; i < (int)dxPrimitives->mTextures.mSize; i++)
				mD3DRenderDevice->mD3DDeviceContext->PSSetShaderResources(i, 1, &dxPrimitives->mTextures[i]->mD3DResourceView);

			// Set vertex buffer
			UINT stride = sizeof(DXModelVertex);
			UINT offset = 0;
			mD3DRenderDevice->mD3DDeviceContext->IASetVertexBuffers(0, 1, &dxPrimitives->mD3DVertexBuffer, &stride, &offset);
			mD3DRenderDevice->mD3DDeviceContext->IASetIndexBuffer(dxPrimitives->mD3DIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
			mD3DRenderDevice->mD3DDeviceContext->DrawIndexed(dxPrimitives->mNumIndices, 0, 0);
		}
	}
}

void Beefy::DXModelInstance::CommandQueued(DrawLayer* drawLayer)
{
	mRenderState = drawLayer->mRenderDevice->mCurRenderState;
	BF_ASSERT(mRenderState->mShader->mVertexSize == sizeof(DXModelVertex));
	drawLayer->mCurTextures[0] = NULL;

#ifndef BF_NO_FBX
	ModelAnimation* fbxAnim = &mModelDef->mAnims[0];

	Matrix4 jointsMatrices[BF_MAX_NUM_BONES];
	for (int jointIdx = 0; jointIdx < (int)mJointTranslations.size(); jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];

		BF_ASSERT(joint->mParentIdx < jointIdx);

		ModelJointTranslation* jointPosition = &mJointTranslations[jointIdx];

		Matrix4* mtx = &jointsMatrices[jointIdx];

		*mtx = Matrix4::CreateTransform(jointPosition->mTrans, jointPosition->mScale, jointPosition->mQuat);
		if (joint->mParentIdx >= 0)
		{
			 Matrix4* parentMatrix = &jointsMatrices[joint->mParentIdx];
			 *mtx = Matrix4::Multiply(*parentMatrix, *mtx);
		}
	}

	for (int jointIdx = 0; jointIdx < (int)mModelDef->mJoints.size(); jointIdx++)
	{
		ModelJoint* joint = &mModelDef->mJoints[jointIdx];
		Matrix4* mtx = &jointsMatrices[jointIdx];
		*mtx = Matrix4::Multiply(*mtx, joint->mPoseInvMatrix);
	}

	for (int meshIdx = 0; meshIdx < (int) mModelDef->mMeshes.size(); meshIdx++)
	{
		if (!mMeshesVisible[meshIdx])
			continue;

		ModelMesh* mesh = &mModelDef->mMeshes[meshIdx];
		DXModelMesh* dxMesh = &mDXModelMeshs[meshIdx];

		D3D11_MAPPED_SUBRESOURCE mappedSubResource;
		DXRenderDevice* dxRenderDevice = (DXRenderDevice*)drawLayer->mRenderDevice;
		DXCHECK(dxRenderDevice->mD3DDeviceContext->Map(dxMesh->mD3DVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedSubResource));
		DXModelVertex* dxVtxData = (DXModelVertex*)mappedSubResource.pData;
		for (int vtxIdx = 0; vtxIdx < (int) mesh->mVertices.size(); vtxIdx++)
		{
			ModelVertex* srcVtxData = &mesh->mVertices[vtxIdx];

			Vector3 vtx(0, 0, 0);

			float totalWeight = 0;

			for (int weightIdx = 0; weightIdx < srcVtxData->mNumBoneWeights; weightIdx++)
			{
				int jointIdx = srcVtxData->mBoneIndices[weightIdx];
				float boneWeight = srcVtxData->mBoneWeights[weightIdx];

				Matrix4* mtx = &jointsMatrices[jointIdx];
				Vector3 origVec = srcVtxData->mPosition;

				Vector3 transVec = Vector3::Transform(origVec, *mtx);
				transVec = transVec * boneWeight;
				vtx = vtx + transVec;

				totalWeight += boneWeight;
			}

			BF_ASSERT(fabs(totalWeight - 1.0) < 0.1f);

			DXModelVertex* destVtx = dxVtxData + vtxIdx;

			//destVtx->mPosition = srcVtxData->mPosition;
			destVtx->mPosition = vtx;
			destVtx->mTexCoords = srcVtxData->mTexCoords;
			destVtx->mBumpTexCoords = srcVtxData->mTexCoords;
			destVtx->mColor = 0xFFFFFFFF; //TODO: Color
			destVtx->mTangent = srcVtxData->mTangent;
		}

		dxRenderDevice->mD3DDeviceContext->Unmap(dxMesh->mD3DVertexBuffer, 0);
	}
#endif
}

///


void DXSetTextureCmd::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;
	dxRenderDevice->mD3DDeviceContext->PSSetShaderResources(mTextureIdx, 1, &((DXTexture*)mTexture)->mD3DResourceView);
}

///

void DXSetConstantData::Render(RenderDevice* renderDevice, RenderWindow* renderWindow)
{
	//SetRenderState();

	DXShader* dxShader = (DXShader*)renderDevice->mCurRenderState->mShader;
	DXRenderDevice* dxRenderDevice = (DXRenderDevice*)renderDevice;

	HRESULT result = 0;

	int bufferSize = BF_ALIGN(mSize, 16);

	int id = (mSlotIdx << 24) | (bufferSize << 1) | (mUsageIdx);
	ID3D11Buffer* buffer = NULL;
	ID3D11Buffer** bufferPtr = NULL;
	if (dxRenderDevice->mBufferMap.TryAdd(id, NULL, &bufferPtr))
	{
		D3D11_BUFFER_DESC matrixBufferDesc;
		matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		matrixBufferDesc.ByteWidth = bufferSize;
		matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		matrixBufferDesc.MiscFlags = 0;
		matrixBufferDesc.StructureByteStride = 0;

		result = dxRenderDevice->mD3DDevice->CreateBuffer(&matrixBufferDesc, NULL, &buffer);
		if (FAILED(result))
			return;

		//OutputDebugStrF("Created Buffer %d %p\n", bufferSize, buffer);

		*bufferPtr = buffer;
	}
	else
		buffer = *bufferPtr;

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	result = dxRenderDevice->mD3DDeviceContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
		return;

	float* dataPtr = (float*)mappedResource.pData;
	memset(dataPtr, 0, bufferSize);
	memcpy(mappedResource.pData, mData, mSize);

	dxRenderDevice->mD3DDeviceContext->Unmap(buffer, 0);
	if (mUsageIdx == 0)
	{
		//OutputDebugStrF("VSSetConstantBuffers %d %p\n", mSlotIdx, buffer);
		dxRenderDevice->mD3DDeviceContext->VSSetConstantBuffers(mSlotIdx, 1, &buffer);
	}
	else
	{
		//OutputDebugStrF("PSSetConstantBuffers %d %p\n", mSlotIdx, buffer);
		dxRenderDevice->mD3DDeviceContext->PSSetConstantBuffers(mSlotIdx, 1, &buffer);
	}
}

///

DXRenderWindow::DXRenderWindow(DXRenderDevice* renderDevice, WinBFWindow* window, bool windowed)
{
	BP_ZONE("DXRenderWindow::DXRenderWindow");

	mWindowed = windowed;
	mDXSwapChain = NULL;
	mD3DBackBuffer = NULL;
	mD3DRenderTargetView = NULL;
	mD3DDepthBuffer = NULL;
	mD3DDepthStencilView = NULL;
	mRefreshRate = 0;
	mFrameWaitObject = NULL;

	mRenderDevice = renderDevice;
	mDXRenderDevice = renderDevice;
	mWindow = window;
	mHWnd = window->mHWnd;

	Resized();

	ReinitNative();
}

DXRenderWindow::~DXRenderWindow()
{
	ReleaseNative();
}

void DXRenderWindow::ReleaseNative()
{
	if (mFrameWaitObject != NULL)
		::CloseHandle(mFrameWaitObject);
	mFrameWaitObject = NULL;
	if (mD3DRenderTargetView != NULL)
		mD3DRenderTargetView->Release();
	mD3DRenderTargetView = NULL;
	if (mD3DBackBuffer != NULL)
		mD3DBackBuffer->Release();
	mD3DBackBuffer = NULL;
	if (mDXSwapChain != NULL)
		mDXSwapChain->Release();
	mDXSwapChain = NULL;
	if (mD3DRenderTargetView != NULL)
		mD3DRenderTargetView->Release();
	mD3DRenderTargetView = NULL;
	if (mD3DDepthStencilView != NULL)
		mD3DDepthStencilView->Release();
	mD3DDepthStencilView = NULL;
}

void DXRenderWindow::ReinitNative()
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
	swapChainDesc.BufferCount = 1;
	swapChainDesc.BufferDesc.Width = mWidth;
	swapChainDesc.BufferDesc.Height = mHeight;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = mHWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Windowed = mWindowed ? TRUE : FALSE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;// DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH /*| DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT*/;

	IDXGIDevice* pDXGIDevice = NULL;
	mDXRenderDevice->mD3DDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);

	DXCHECK(mDXRenderDevice->mDXGIFactory->CreateSwapChain(pDXGIDevice, &swapChainDesc, &mDXSwapChain));
	pDXGIDevice->Release();
	pDXGIDevice = NULL;

// 	IDXGISwapChain2* swapChain2 = NULL;
// 	mDXSwapChain->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&swapChain2);
// 	if (swapChain2 != NULL)
// 	{
// 		mFrameWaitObject = swapChain2->GetFrameLatencyWaitableObject();
// 		swapChain2->Release();
// 	}

	DXCHECK(mDXSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&mD3DBackBuffer));
	DXCHECK(mDXRenderDevice->mD3DDevice->CreateRenderTargetView(mD3DBackBuffer, NULL, &mD3DRenderTargetView));

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = mWidth;
	descDepth.Height = mHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	mDXRenderDevice->mD3DDevice->CreateTexture2D(&descDepth, NULL, &mD3DDepthBuffer);

	if ((mWindow->mFlags & BFWINDOW_ALLOW_FULLSCREEN) == 0)
		mDXRenderDevice->mDXGIFactory->MakeWindowAssociation(mHWnd, DXGI_MWA_NO_ALT_ENTER);

	DXCHECK(mDXRenderDevice->mD3DDevice->CreateDepthStencilView(mD3DDepthBuffer, NULL, &mD3DDepthStencilView));
}

void DXRenderWindow::PhysSetAsTarget()
{
	//if (mRenderDevice->mCurRenderTarget != this)
	{
		D3D11_VIEWPORT viewPort;
		viewPort.Width = (float)mWidth;
		viewPort.Height = (float)mHeight;
		viewPort.MinDepth = 0.0f;
		viewPort.MaxDepth = 1.0f;
		viewPort.TopLeftX = 0;
		viewPort.TopLeftY = 0;

		mDXRenderDevice->mD3DDeviceContext->OMSetRenderTargets(1, &mD3DRenderTargetView, mD3DDepthStencilView);
		mDXRenderDevice->mD3DDeviceContext->RSSetViewports(1, &viewPort);
	}

	if (!mHasBeenDrawnTo)
	{
		//mRenderDevice->mD3DDevice->ClearRenderTargetView(mD3DRenderTargetView, D3DXVECTOR4(rand() / (float) RAND_MAX, 0, 1, 0));
		float bgColor[4] = {0, 0, 0, 0};
		mDXRenderDevice->mD3DDeviceContext->ClearRenderTargetView(mD3DRenderTargetView, bgColor);
		mDXRenderDevice->mD3DDeviceContext->ClearDepthStencilView(mD3DDepthStencilView, D3D11_CLEAR_DEPTH/*|D3D11_CLEAR_STENCIL*/, 1.0f, 0);
	}

	mHasBeenDrawnTo = true;
}

void DXRenderWindow::SetAsTarget()
{
	//TODO: Handle this more elegantly when we actually handle draw layers properly...
	//if (mRenderDevice->mCurRenderTarget != NULL)
		//mRenderDevice->mCurDrawLayer->Flush();

	mHasBeenTargeted = true;
	mRenderDevice->mCurRenderTarget = this;
}

void DXRenderWindow::CheckDXResult(HRESULT hr)
{
	if ((hr == DXGI_ERROR_DEVICE_REMOVED) || (hr == DXGI_ERROR_DEVICE_RESET))
		((DXRenderDevice*)mRenderDevice)->mNeedsReinitNative = true;
	else
		DXCHECK(hr);
}

void DXRenderWindow::Resized()
{
	mRenderDevice->mResizeCount++;
	mResizeNum = mRenderDevice->mResizeCount;

	RECT rect;
	GetClientRect(mHWnd, &rect);

	if (rect.right <= rect.left)
		return;

	mWidth = rect.right - rect.left;
	mHeight = rect.bottom - rect.top;

	if (mDXSwapChain != NULL)
	{
		mD3DBackBuffer->Release();
		mD3DDepthBuffer->Release();
		mD3DRenderTargetView->Release();
		mD3DDepthStencilView->Release();

		CheckDXResult(mDXSwapChain->ResizeBuffers(0, mWidth, mHeight, DXGI_FORMAT_UNKNOWN,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH /*| DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT*/));

		D3D11_TEXTURE2D_DESC descDepth;
		ZeroMemory(&descDepth, sizeof(descDepth));
		descDepth.Width = mWidth;
		descDepth.Height = mHeight;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDepth.SampleDesc.Count = 1;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		descDepth.CPUAccessFlags = 0;
		descDepth.MiscFlags = 0;
		mDXRenderDevice->mD3DDevice->CreateTexture2D(&descDepth, NULL, &mD3DDepthBuffer);

		CheckDXResult(mDXSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&mD3DBackBuffer));
		CheckDXResult(mDXRenderDevice->mD3DDevice->CreateRenderTargetView(mD3DBackBuffer, NULL, &mD3DRenderTargetView));
		CheckDXResult(mDXRenderDevice->mD3DDevice->CreateDepthStencilView(mD3DDepthBuffer, NULL, &mD3DDepthStencilView));

		/*if (mRenderDevice->mCurRenderTarget == this)
			mRenderDevice->mCurRenderTarget = NULL;
		PhysSetAsTarget();*/
	}
}

void DXRenderWindow::Present()
{
	HRESULT hr = mDXSwapChain->Present((mWindow->mFlags & BFWINDOW_VSYNC) ? 1 : 0, 0);

	if ((hr == DXGI_ERROR_DEVICE_REMOVED) || (hr == DXGI_ERROR_DEVICE_RESET))
		((DXRenderDevice*)mRenderDevice)->mNeedsReinitNative = true;
}

void DXRenderWindow::CopyBitsTo(uint32* dest, int width, int height)
{
	mCurDrawLayer->Flush();

	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = 0;
	texDesc.CPUAccessFlags = 0;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_STAGING;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	ID3D11Texture2D *texture;
	DXCHECK(mDXRenderDevice->mD3DDevice->CreateTexture2D(&texDesc, 0, &texture));
	mDXRenderDevice->mD3DDeviceContext->CopyResource(texture, mD3DBackBuffer);

	/*? D3D11_MAPPED_TEXTURE2D mapTex;
	DXCHECK(texture->Map(D3D11CalcSubresource(0, 0, 1), D3D11_MAP_READ, 0, &mapTex));

	uint8* srcPtr = (uint8*) mapTex.pData;
	uint8* destPtr = (uint8*) dest;
	for (int y = 0; y < height; y++)
	{
		memcpy(destPtr, srcPtr, width*sizeof(uint32));
		srcPtr += mapTex.RowPitch;
		destPtr += width * 4;
	}
	texture->Unmap(0);*/
	texture->Release();
}

float DXRenderWindow::GetRefreshRate()
{
	if (mRefreshRate == 0)
	{
		mRefreshRate = -1;

		IDXGIOutput* output = NULL;
		mDXSwapChain->GetContainingOutput(&output);
		if (output != NULL)
		{
			DXGI_OUTPUT_DESC outputDesc;
			output->GetDesc(&outputDesc);

			MONITORINFOEXW info;
			info.cbSize = sizeof(info);
			// get the associated monitor info
			if (GetMonitorInfoW(outputDesc.Monitor, &info) != 0)
			{
				// using the CCD get the associated path and display configuration
				UINT32 requiredPaths, requiredModes;
				if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS)
				{
					std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
					std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
					if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS)
					{
						// iterate through all the paths until find the exact source to match
						for (auto& p : paths)
						{
							DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
							sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
							sourceName.header.size = sizeof(sourceName);
							sourceName.header.adapterId = p.sourceInfo.adapterId;
							sourceName.header.id = p.sourceInfo.id;
							if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS)
							{
								// find the matched device which is associated with current device
								// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
								// there may be two callback because source is same target will be different
								// as window is on both the display so either selecting either one is ok
								if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0)
								{
									// get the refresh rate
									UINT numerator = p.targetInfo.refreshRate.Numerator;
									UINT denominator = p.targetInfo.refreshRate.Denominator;
									mRefreshRate = (float)numerator / (float)denominator;
									break;
								}
							}
						}
					}
				}
			}

			output->Release();
		}
	}

	return mRefreshRate;
}

bool DXRenderWindow::WaitForVBlank()
{
	IDXGIOutput* output = NULL;
	mDXSwapChain->GetContainingOutput(&output);
	if (output == NULL)
		return false;
	bool success = output->WaitForVBlank() == 0;
	return success;
}

///

DXRenderDevice::DXRenderDevice()
{
	mD3DDevice = NULL;
	mNeedsReinitNative = false;
	mMatrix2DBuffer = NULL;
}

DXRenderDevice::~DXRenderDevice()
{
	for (auto window : mRenderWindowList)
		((DXRenderWindow*)window)->ReleaseNative();
	for (auto shader : mShaders)
		shader->ReleaseNative();
	for (auto renderState : mRenderStates)
		renderState->ReleaseNative();
	for (auto texture : mTextures)
	{
		texture->ReleaseNative();
		texture->mRenderDevice = NULL;
	}

	ReleaseNative();

	delete mDefaultRenderState;
}

bool DXRenderDevice::Init(BFApp* app)
{
	BP_ZONE("DXRenderDevice::Init");

	mApp = app;
	WinBFApp* winApp = (WinBFApp*) app;

	D3D_FEATURE_LEVEL featureLevelArr[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1,
	};;

	D3D_FEATURE_LEVEL d3dFeatureLevel = (D3D_FEATURE_LEVEL)0;
	int flags = 0;
	//TODO:
	//flags = D3D11_CREATE_DEVICE_DEBUG;
	DXCHECK(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, featureLevelArr, 6, D3D11_SDK_VERSION, &mD3DDevice, &d3dFeatureLevel, &mD3DDeviceContext));
	OutputDebugStrF("D3D Feature Level: %X\n", d3dFeatureLevel);

	IDXGIDevice* pDXGIDevice = NULL;
	DXCHECK(mD3DDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&pDXGIDevice)));

	IDXGIAdapter* pDXGIAdapter = NULL;
	DXCHECK(pDXGIDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&pDXGIAdapter)));

	IDXGIFactory* pDXGIFactory = NULL;
	DXCHECK(pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&mDXGIFactory)));

	DXRenderState* dxRenderState;
	if (mDefaultRenderState == NULL)
	{
		dxRenderState = (DXRenderState*)CreateRenderState(NULL);

		mDefaultRenderState = dxRenderState;
		mDefaultRenderState->mDepthFunc = DepthFunc_Less;
		mDefaultRenderState->mWriteDepthBuffer = true;

		mPhysRenderState = mDefaultRenderState;
	}
	else
	{
		dxRenderState = (DXRenderState*)mDefaultRenderState;
		dxRenderState->ReinitNative();
	}

	D3D11_RASTERIZER_DESC rasterizerState;
	rasterizerState.CullMode = D3D11_CULL_NONE;
	rasterizerState.FillMode = D3D11_FILL_SOLID;
	rasterizerState.FrontCounterClockwise = false;
    rasterizerState.DepthBias = false;
    rasterizerState.DepthBiasClamp = 0;
    rasterizerState.SlopeScaledDepthBias = 0;
    rasterizerState.DepthClipEnable = false;
    rasterizerState.ScissorEnable = false;
	rasterizerState.MultisampleEnable = false;
    rasterizerState.AntialiasedLineEnable = false;

	mD3DDevice->CreateRasterizerState(&rasterizerState, &dxRenderState->mD3DRasterizerState);
	mD3DDeviceContext->RSSetState(dxRenderState->mD3DRasterizerState);
	mD3DDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11BlendState* g_pBlendState = NULL;

	D3D11_BLEND_DESC BlendState;
	ZeroMemory(&BlendState, sizeof(D3D11_BLEND_DESC));
	BlendState.RenderTarget[0].BlendEnable = TRUE;
	BlendState.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;

	BlendState.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendState.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendState.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	BlendState.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendState.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	mD3DDevice->CreateBlendState(&BlendState, &mD3DNormalBlendState);

	mD3DDeviceContext->OMSetBlendState(mD3DNormalBlendState, NULL, 0xffffffff);

	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DXCHECK(mD3DDevice->CreateSamplerState(&sampDesc, &mD3DDefaultSamplerState));

	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	DXCHECK(mD3DDevice->CreateSamplerState(&sampDesc, &mD3DWrapSamplerState));

	D3D11_BUFFER_DESC bd;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = DX_VTXBUFFER_SIZE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;
	bd.StructureByteStride = 0;

	mD3DDevice->CreateBuffer(&bd, NULL, &mD3DVertexBuffer);

	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = DX_IDXBUFFER_SIZE;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = 0;
	bd.StructureByteStride = 0;

	mD3DDevice->CreateBuffer(&bd, NULL, &mD3DIndexBuffer);

	mVtxByteIdx = 0;
	mIdxByteIdx = 0;

	return true;
}

void DXRenderDevice::ReleaseNative()
{
	mD3DVertexBuffer->Release();
	mD3DVertexBuffer = NULL;
	if (mMatrix2DBuffer != NULL)
		mMatrix2DBuffer->Release();
	mMatrix2DBuffer = NULL;
	mD3DIndexBuffer->Release();
	mD3DIndexBuffer = NULL;
	mD3DNormalBlendState->Release();
	mD3DNormalBlendState = NULL;
	mD3DDefaultSamplerState->Release();
	mD3DDefaultSamplerState = NULL;
	mD3DWrapSamplerState->Release();
	mD3DWrapSamplerState = NULL;
	mD3DDeviceContext->Release();
	mD3DDeviceContext = NULL;

// 	ID3D11Debug* debug = NULL;
// 	mD3DDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&debug);
// 	if (debug != NULL)
// 	{
// 		debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
// 		debug->Release();
// 	}

	mD3DDevice->Release();
	mD3DDevice = NULL;
}

void DXRenderDevice::ReinitNative()
{
	AutoCrit autoCrit(mApp->mCritSect);

	if (mMatrix2DBuffer != NULL)
		mMatrix2DBuffer->Release();
	mMatrix2DBuffer = NULL;

	Init(mApp);

	for (auto window : mRenderWindowList)
		((DXRenderWindow*)window)->ReinitNative();
	for (auto shader : mShaders)
		shader->ReinitNative();
	for (auto renderState : mRenderStates)
		renderState->ReinitNative();
	for (auto tex : mTextures)
		tex->ReinitNative();
}

void DXRenderDevice::FrameStart()
{
	mCurRenderTarget = NULL;
	mPhysRenderWindow = NULL;
	for (auto renderWindow : mRenderWindowList)
	{
		renderWindow->mHasBeenDrawnTo = false;
		renderWindow->mHasBeenTargeted = false;
	}
}

void DXRenderDevice::FrameEnd()
{
	for (int renderWindowIdx = 0; renderWindowIdx < (int)mRenderWindowList.size(); renderWindowIdx++)
	{
		RenderWindow* aRenderWindow = mRenderWindowList[renderWindowIdx];
		if (aRenderWindow->mHasBeenTargeted)
		{
			PhysSetRenderState(mDefaultRenderState);
			PhysSetRenderWindow(aRenderWindow);

			for (int drawLayerIdx = 0; drawLayerIdx < (int)aRenderWindow->mDrawLayerList.size(); drawLayerIdx++)
			{
				DrawLayer* drawLayer = aRenderWindow->mDrawLayerList[drawLayerIdx];
				drawLayer->Draw();
			}

			aRenderWindow->Present();
		}
	}

	RenderDevice::FrameEnd();

	// Do 'clear' after frame end so we allocate new (and valid) vtx/idx draw buffers
	for (int renderWindowIdx = 0; renderWindowIdx < (int)mRenderWindowList.size(); renderWindowIdx++)
	{
		RenderWindow* aRenderWindow = mRenderWindowList[renderWindowIdx];
		if (aRenderWindow->mHasBeenTargeted)
		{
			for (int drawLayerIdx = 0; drawLayerIdx < (int)aRenderWindow->mDrawLayerList.size(); drawLayerIdx++)
			{
				DrawLayer* drawLayer = aRenderWindow->mDrawLayerList[drawLayerIdx];
				drawLayer->Clear();
			}
		}
	}
}

Texture* DXRenderDevice::LoadTexture(const StringImpl& fileName, int flags)
{
	if (fileName.StartsWith("!backbuffer:"))
	{
		int colon = (int)fileName.IndexOf(':');
		String addrStr = fileName.Substring(colon + 1);
		void* addr = (void*)(intptr)strtoll(addrStr.c_str(), NULL, 16);
		BFWindow* window = (BFWindow*)addr;
		DXRenderWindow* renderWindow = (DXRenderWindow*)window->mRenderWindow;

		DXTexture* aTexture = NULL;
		aTexture->mD3DRenderTargetView = renderWindow->mD3DRenderTargetView;
		aTexture->mD3DTexture = renderWindow->mD3DBackBuffer;

		aTexture->mD3DRenderTargetView->AddRef();
		aTexture->mD3DTexture->AddRef();
		aTexture->AddRef();
		return aTexture;
	}

	DXTexture* aTexture = NULL;
	if (mTextureMap.TryGetValue(fileName, &aTexture))
	{
		aTexture->AddRef();
		return aTexture;
	}

	int dotPos = (int)fileName.LastIndexOf('.');
	String ext;
	if (dotPos != -1)
		ext = fileName.Substring(dotPos);

	if (ext.Equals(".dds", StringImpl::CompareKind_OrdinalIgnoreCase))
	{
		FileStream fs;
		if (!fs.Open(fileName, "rb"))
			return NULL;

		int header = fs.ReadInt32();
		if (header != 0x20534444)
			return NULL;

		auto hdr = fs.ReadT<DDS_HEADER>();

		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

		if (hdr.ddspf.dwFlags == DDS_RGBA)
		{
			if (hdr.ddspf.dwRGBBitCount == 32)
			{
				if (hdr.ddspf.dwRBitMask == 0xff)
					format = DXGI_FORMAT_R8G8B8A8_UNORM;
				else if (hdr.ddspf.dwRBitMask = 0xff0000)
					format = DXGI_FORMAT_B8G8R8A8_UNORM;
				else if (hdr.ddspf.dwRBitMask == 0xffff)
					format = DXGI_FORMAT_R16G16_UNORM;
				else if (hdr.ddspf.dwRBitMask == 0x3ff)
					format = DXGI_FORMAT_R10G10B10A2_UNORM;
			}
			else if (hdr.ddspf.dwRGBBitCount == 16)
			{
				if (hdr.ddspf.dwRBitMask == 0x7c00)
					format = DXGI_FORMAT_B5G5R5A1_UNORM;
				else if (hdr.ddspf.dwRBitMask == 0xf800)
					format = DXGI_FORMAT_B5G6R5_UNORM;
			}
			else if (hdr.ddspf.dwRGBBitCount == 8)
			{
				if (hdr.ddspf.dwRBitMask == 0xff)
					format = DXGI_FORMAT_R8_UNORM;
				else if (hdr.ddspf.dwABitMask == 0xff)
					format = DXGI_FORMAT_A8_UNORM;
			}
		}

		if (hdr.ddspf.dwFourCC == '1TXD')
			format = DXGI_FORMAT_BC1_UNORM;
		if (hdr.ddspf.dwFourCC == '3TXD')
			format = DXGI_FORMAT_BC2_UNORM;
		if (hdr.ddspf.dwFourCC == '5TXD')
			format = DXGI_FORMAT_BC3_UNORM;
		if (hdr.ddspf.dwFourCC == 'U4CB')
			format = DXGI_FORMAT_BC4_UNORM;
		if (hdr.ddspf.dwFourCC == 'S4CB')
			format = DXGI_FORMAT_BC4_SNORM;
		if (hdr.ddspf.dwFourCC == '2ITA')
			format = DXGI_FORMAT_BC5_UNORM;
		if (hdr.ddspf.dwFourCC == 'S5CB')
			format = DXGI_FORMAT_BC5_SNORM;

		if (hdr.ddspf.dwFourCC == '01XD')
		{
			auto hdr10 = fs.ReadT<DDS_HEADER_DXT10>();
			format = hdr10.dxgiFormat;
		}

		int blockSize = 0;
		int bytesPerPixel = GetBytesPerPixel(format, blockSize);

		int mipSize = ((hdr.dwWidth + blockSize - 1) / blockSize) * ((hdr.dwHeight + blockSize - 1) / blockSize) * bytesPerPixel;
		Array<uint8> data;
		data.Resize(mipSize);
		fs.Read(data.mVals, data.mSize);

		D3D11_SUBRESOURCE_DATA resData;
		resData.pSysMem = data.mVals;
		resData.SysMemPitch = ((hdr.dwWidth + blockSize - 1) / blockSize) * bytesPerPixel;
		resData.SysMemSlicePitch = mipSize;

		// Create the target texture
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = hdr.dwWidth;
		desc.Height = hdr.dwHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		DXGI_FORMAT viewFormat = format;
		switch (viewFormat)
		{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS: viewFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS: viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
		case DXGI_FORMAT_BC1_TYPELESS: viewFormat = DXGI_FORMAT_BC1_UNORM; break;
		case DXGI_FORMAT_BC2_TYPELESS: viewFormat = DXGI_FORMAT_BC2_UNORM; break;
		case DXGI_FORMAT_BC3_TYPELESS: viewFormat = DXGI_FORMAT_BC3_UNORM; break;
		case DXGI_FORMAT_BC4_TYPELESS: viewFormat = DXGI_FORMAT_BC4_UNORM; break;
		case DXGI_FORMAT_BC5_TYPELESS: viewFormat = DXGI_FORMAT_BC5_UNORM; break;
		}

		//OutputDebugStrF("Creating texture\n");

		ID3D11Texture2D* d3DTexture = NULL;
		DXCHECK(mD3DDevice->CreateTexture2D(&desc, &resData, &d3DTexture));

		D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
		srDesc.Format = viewFormat;
		srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srDesc.Texture2D.MostDetailedMip = 0;
		srDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView* d3DShaderResourceView = NULL;
		DXCHECK(mD3DDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

		DXTexture* aTexture = new DXTexture();
		aTexture->mPath = fileName;
		aTexture->mRenderDevice = this;
		aTexture->mWidth = hdr.dwWidth;
		aTexture->mHeight = hdr.dwHeight;
		aTexture->mD3DTexture = d3DTexture;
		aTexture->mD3DResourceView = d3DShaderResourceView;
		aTexture->AddRef();

		mTextureMap[aTexture->mPath] = aTexture;
		mTextures.Add(aTexture);
		return aTexture;
	}

	aTexture = (DXTexture*)RenderDevice::LoadTexture(fileName, flags);
	if (aTexture != NULL)
	{
		aTexture->mPath = fileName;
		mTextureMap[aTexture->mPath] = aTexture;
	}

	return aTexture;
}

Texture* DXRenderDevice::LoadTexture(ImageData* imageData, int flags)
{
	ID3D11ShaderResourceView* d3DShaderResourceView = NULL;

	imageData->mIsAdditive = (flags & TextureFlag_Additive) != 0;
	if ((flags & TextureFlag_NoPremult) == 0)
		imageData->PremultiplyAlpha();

	int aWidth = 0;
	int aHeight = 0;

	D3D11_SUBRESOURCE_DATA resData;
	resData.pSysMem = imageData->mBits;
	resData.SysMemPitch = imageData->mWidth * 4;
	resData.SysMemSlicePitch = imageData->mWidth * imageData->mHeight * 4;

	// Create the target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = imageData->mWidth;
	desc.Height = imageData->mHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	//OutputDebugStrF("Creating texture\n");

	ID3D11Texture2D* d3DTexture = NULL;
	DXCHECK(mD3DDevice->CreateTexture2D(&desc, &resData, &d3DTexture));

	aWidth = imageData->mWidth;
	aHeight = imageData->mHeight;

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	DXCHECK(mD3DDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

	DXTexture* aTexture = new DXTexture();

	aTexture->mContentBits = new uint32[aWidth * aHeight];
	memcpy(aTexture->mContentBits, imageData->mBits, aWidth * aHeight * 4);

	aTexture->mRenderDevice = this;
	aTexture->mWidth = aWidth;
	aTexture->mHeight = aHeight;
	aTexture->mD3DTexture = d3DTexture;
	aTexture->mD3DResourceView = d3DShaderResourceView;
	aTexture->AddRef();

	mTextures.Add(aTexture);

	//OutputDebugStrF("gTextureIdx=%d %@\n", gTextureIdx, aTexture);

	return aTexture;
}

Texture* DXRenderDevice::CreateDynTexture(int width, int height)
{
	ID3D11ShaderResourceView* d3DShaderResourceView = NULL;

	// Create the target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D* d3DTexture = NULL;
	DXCHECK(mD3DDevice->CreateTexture2D(&desc, NULL, &d3DTexture));

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	DXCHECK(mD3DDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

	DXTexture* aTexture = new DXTexture();
	aTexture->mRenderDevice = this;
	aTexture->mWidth = width;
	aTexture->mHeight = height;
	aTexture->mD3DTexture = d3DTexture;
	aTexture->mD3DResourceView = d3DShaderResourceView;
	aTexture->AddRef();

	mTextures.Add(aTexture);

	//OutputDebugStrF("gTextureIdx=%d %@\n", gTextureIdx, aTexture);

	return aTexture;
}

Shader* DXRenderDevice::LoadShader(const StringImpl& fileName, VertexDefinition* vertexDefinition)
{
	BP_ZONE("DXRenderDevice::LoadShader");

	DXShader* dxShader = new DXShader();
	dxShader->mRenderDevice = this;
	dxShader->mSrcPath = fileName;
	dxShader->mVertexDef = new VertexDefinition(vertexDefinition);
	if (!dxShader->Load())
	{
		delete dxShader;
		return NULL;
	}
	mShaders.Add(dxShader);
	return dxShader;
}

void DXRenderDevice::SetRenderState(RenderState* renderState)
{
	mCurRenderState = renderState;
}

Texture* DXRenderDevice::CreateRenderTarget(int width, int height, bool destAlpha)
{
	ID3D11ShaderResourceView* d3DShaderResourceView = NULL;

	int aWidth = 0;
	int aHeight = 0;

	int sampleQuality = 0;

	// Create the render target texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	UINT qualityLevels = 0;

	int samples = 1;
	//DXCHECK(mD3DDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &qualityLevels));

	desc.SampleDesc.Count = samples;
	desc.SampleDesc.Quality = sampleQuality;

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0; //D3D11_CPU_ACCESS_WRITE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D* d3DTexture = NULL;
	DXCHECK(mD3DDevice->CreateTexture2D(&desc, NULL, &d3DTexture));

	aWidth = width;
	aHeight = height;

	D3D11_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = desc.Format;
	srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;

	if (qualityLevels != 0)
	{
		srDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
	}

	DXCHECK(mD3DDevice->CreateShaderResourceView(d3DTexture, &srDesc, &d3DShaderResourceView));

	ID3D11RenderTargetView*	d3DRenderTargetView;
	DXCHECK(mD3DDevice->CreateRenderTargetView(d3DTexture, NULL, &d3DRenderTargetView));

	DXTexture* aRenderTarget = new DXTexture();
	aRenderTarget->mWidth = width;
	aRenderTarget->mHeight = height;
	aRenderTarget->mRenderDevice = this;
	aRenderTarget->mD3DTexture = d3DTexture;
	aRenderTarget->mD3DResourceView = d3DShaderResourceView;
	aRenderTarget->mD3DRenderTargetView = d3DRenderTargetView;
	aRenderTarget->AddRef();

	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.SampleDesc.Quality = sampleQuality;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	mD3DDevice->CreateTexture2D(&descDepth, NULL, &aRenderTarget->mD3DDepthBuffer);

	DXCHECK(mD3DDevice->CreateDepthStencilView(aRenderTarget->mD3DDepthBuffer, NULL, &aRenderTarget->mD3DDepthStencilView));

	return aRenderTarget;
}


